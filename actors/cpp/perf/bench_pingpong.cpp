/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 *
 * Actor messaging round-trip latency. Three groups of measurements:
 *
 * A. Transport (heap-allocated messages, with reply):
 *      send ungrouped  - ping/pong on separate threads; each round trip crosses
 *                        cores twice (mailbox mutex + condvar wakeup).
 *      send grouped    - both actors share one Group thread+queue; no cross-core
 *                        wakeup, just queue push/pop + dispatch.
 *      fast_send       - synchronous: receiver's handler runs inline in the
 *                        caller's thread; no queue, no thread hop.
 *
 * B. Allocation (grouped send, isolates per-message new/delete):
 *      grouped plain   - messages via global new/delete.
 *      grouped pooled  - messages via the framework MemoryPool. Shows the
 *                        per-message heap-allocation cost the pool removes.
 *
 * C. fast_send variants:
 *      heap  + reply   - new Ping in, Pong allocated and returned.
 *      pooled+ reply   - MemoryPool Ping/Pong.
 *      stack + reply   - Ping on the caller's stack (no alloc for the input).
 *      stack, no reply - fast_send in a loop, handler does no work/no reply;
 *                        pure dispatch cost.
 *
 * Every round trip is measured sequentially (one outstanding message), so each
 * sample is a clean round-trip latency, not saturated throughput.
 *
 *   usage: bench_pingpong [N] [warmup] [section: transport|alloc|fastsend|all]
 *     N       measured round trips per row   (default 1000000)
 *     warmup  discarded round trips per row  (default 10000)
 */

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <string>
#include <vector>

#include "actors/Actor.hpp"
#include "actors/MemoryPool.hpp"
#include "actors/act/Group.hpp"
#include "actors/act/Manager.hpp"
#include "actors/msg/Start.hpp"

#include "bench_common.hpp"

using namespace actors;

namespace
{

// Plain messages: global new/delete.
struct Ping : public Message_N<100>
{
  uint64_t seq;
  explicit Ping(uint64_t s) : seq(s) {}
};
struct Pong : public Message_N<101>
{
  uint64_t seq;
  explicit Pong(uint64_t s) : seq(s) {}
};

// Pooled messages: allocation served by the framework MemoryPool.
struct PingPool : public Message_N<102>, public MemoryPool<PingPool, 64, 16, 4096>
{
  uint64_t seq;
  explicit PingPool(uint64_t s) : seq(s) {}
};
struct PongPool : public Message_N<103>, public MemoryPool<PongPool, 64, 16, 4096>
{
  uint64_t seq;
  explicit PongPool(uint64_t s) : seq(s) {}
};

// Echoes each ping back as a pong. Templated so it works for plain or pooled types.
template <class PingT, class PongT>
class PongActor : public Actor
{
public:
  // mbk selects this actor's mailbox (see Actor::set_mailbox). Default keeps the
  // stock BQueue so existing rows are unchanged.
  explicit PongActor(Actor::MailboxKind mbk = Actor::MailboxKind::BQueue,
                     size_t cap = ACTOR_BQUEUE_SIZE)
  {
    set_mailbox(mbk, cap);
    std::strncpy(name, "PongActor", sizeof(name) - 1);
    MESSAGE_HANDLER(PingT, on_ping);
  }

private:
  void on_ping(const PingT* m) noexcept { reply(new PongT(m->seq)); }
};

// Drives sequential round trips and records per-round-trip latency (async send).
template <class PingT, class PongT>
class DriverActor : public Actor
{
public:
  DriverActor(Actor* pong, size_t measured, size_t warmup, std::vector<uint64_t>* samples,
              std::promise<void>* done, uint64_t* measured_wall_ns)
  : pong_(pong), warmup_(warmup), total_(measured + warmup), samples_(samples), done_(done),
    measured_wall_ns_(measured_wall_ns)
  {
    std::strncpy(name, "DriverActor", sizeof(name) - 1);
    samples_->reserve(measured);
    MESSAGE_HANDLER(msg::Start, on_start);
    MESSAGE_HANDLER(PongT, on_pong);
  }

private:
  void on_start(const msg::Start*) noexcept
  {
    t0_ = perf::now_ns();
    if (warmup_ == 0)
      win_start_ = t0_; // no warmup: the very first round trip is measured
    pong_->send(new PingT(seq_), this);
  }

  void on_pong(const PongT*) noexcept
  {
    const uint64_t t1 = perf::now_ns();
    if (done_count_ >= warmup_)
      samples_->push_back(t1 - t0_);
    ++done_count_;
    ++seq_;
    if (done_count_ < total_)
    {
      t0_ = perf::now_ns();
      if (done_count_ == warmup_)
        win_start_ = t0_; // window opens at the START of the first measured RT
      pong_->send(new PingT(seq_), this);
    }
    else if (!signalled_)
    {
      // Window spans exactly `measured` round trips (start of the first to end
      // of the last), matching the fast_send runners' amortized convention.
      *measured_wall_ns_ = t1 - win_start_;
      signalled_ = true;
      done_->set_value();
    }
  }

  Actor* pong_;
  size_t warmup_, total_;
  std::vector<uint64_t>* samples_;
  std::promise<void>* done_;
  uint64_t* measured_wall_ns_;
  uint64_t t0_ = 0;
  uint64_t seq_ = 0;
  uint64_t win_start_ = 0;
  size_t done_count_ = 0;
  bool signalled_ = false;
};

// Minimal actor used only as the `sender` argument to fast_send.
class NullActor : public Actor
{
public:
  NullActor() { std::strncpy(name, "NullActor", sizeof(name) - 1); }
};

template <class PingT, class PongT>
perf::LatencyStats run_send(const std::string& label, bool grouped, size_t measured, size_t warmup)
{
  std::vector<uint64_t> samples;
  std::promise<void> done;
  auto fut = done.get_future();
  uint64_t measured_wall = 0;

  Manager mgr("bench_mgr");
  auto* pong = new PongActor<PingT, PongT>();
  auto* driver =
    new DriverActor<PingT, PongT>(pong, measured, warmup, &samples, &done, &measured_wall);

  if (grouped)
  {
    auto* g = new Group("bench_group");
    g->add(pong);
    g->add(driver);
    mgr.add_to_manage_q(g);
  }
  else
  {
    mgr.add_to_manage_q(pong);
    mgr.add_to_manage_q(driver);
  }

  mgr.init();
  fut.wait();
  mgr.end();
  auto s = perf::LatencyStats::from(label, samples);
  s.amortized = measured ? static_cast<double>(measured_wall) / measured : 0.0;
  return s;
}

// Keeps `window` messages outstanding (a burst/pipeline) instead of one at a
// time, so the receiver's mailbox actually accumulates a backlog — the load
// that exercises batch drain and separates the queue types. Each ping carries
// its send timestamp (via send_ts_ indexed by seq) so we still get a
// per-message latency-under-load distribution; amort is the throughput.
template <class PingT, class PongT>
class BurstDriverActor : public Actor
{
public:
  BurstDriverActor(Actor* pong, size_t window, size_t measured, size_t warmup,
                   std::vector<uint64_t>* samples, std::promise<void>* done,
                   uint64_t* measured_wall_ns)
  : pong_(pong), window_(window), warmup_(warmup), total_(measured + warmup),
    samples_(samples), done_(done), measured_wall_ns_(measured_wall_ns)
  {
    std::strncpy(name, "BurstDriver", sizeof(name) - 1);
    samples_->reserve(measured);
    send_ts_.resize(total_, 0);
    MESSAGE_HANDLER(msg::Start, on_start);
    MESSAGE_HANDLER(PongT, on_pong);
  }

private:
  void send_one() noexcept
  {
    const uint64_t ts = perf::now_ns();
    send_ts_[sent_] = ts;
    if (sent_ == warmup_)
      win_start_ = ts; // window opens when the first measured message is sent
    pong_->send(new PingT(sent_), this);
    ++sent_;
  }

  void on_start(const msg::Start*) noexcept
  {
    const size_t initial = std::min(window_, total_);
    for (size_t i = 0; i < initial; ++i)
      send_one();
  }

  void on_pong(const PongT* m) noexcept
  {
    const uint64_t t1 = perf::now_ns();
    const uint64_t seq = m->seq;
    if (seq >= warmup_)
      samples_->push_back(t1 - send_ts_[seq]);
    ++recv_;

    if (sent_ < total_)
      send_one();               // refill the window
    else if (recv_ == total_ && !signalled_)
    {
      *measured_wall_ns_ = t1 - win_start_;
      signalled_ = true;
      done_->set_value();
    }
  }

  Actor* pong_;
  size_t window_, warmup_, total_;
  std::vector<uint64_t>* samples_;
  std::promise<void>* done_;
  uint64_t* measured_wall_ns_;
  std::vector<uint64_t> send_ts_;
  uint64_t win_start_ = 0;
  size_t sent_ = 0;
  size_t recv_ = 0;
  bool signalled_ = false;
};

// Ungrouped burst run: pong on its own thread using mailbox `mbk`, driver
// keeps `window` pings outstanding.
template <class PingT, class PongT>
perf::LatencyStats run_burst(const std::string& label, Actor::MailboxKind mbk,
                             size_t window, size_t measured, size_t warmup)
{
  std::vector<uint64_t> samples;
  std::promise<void> done;
  auto fut = done.get_future();
  uint64_t measured_wall = 0;

  // Ring/overflow big enough to hold the burst; sharded uses a lane count.
  const size_t cap = (mbk == Actor::MailboxKind::ShardedBQueue) ? 8 : 4 * window + 64;

  Manager mgr("bench_mgr");
  auto* pong = new PongActor<PingT, PongT>(mbk, cap);
  auto* driver = new BurstDriverActor<PingT, PongT>(
      pong, window, measured, warmup, &samples, &done, &measured_wall);

  mgr.add_to_manage_q(pong);
  mgr.add_to_manage_q(driver);

  mgr.init();
  fut.wait();
  mgr.end();
  auto s = perf::LatencyStats::from(label, samples);
  s.amortized = measured ? static_cast<double>(measured_wall) / measured : 0.0;
  return s;
}

// fast_send round trip with a heap-allocated input message and a reply.
template <class PingT, class PongT>
perf::LatencyStats run_fastsend_heap(const std::string& label, size_t measured, size_t warmup)
{
  std::vector<uint64_t> samples;
  samples.reserve(measured);
  PongActor<PingT, PongT> pong;
  NullActor sender;
  const size_t total = measured + warmup;
  uint64_t win_start = 0;
  for (size_t i = 0; i < total; ++i)
  {
    if (i == warmup)
      win_start = perf::now_ns();
    const auto* ping = new PingT(i);
    const uint64_t t0 = perf::now_ns();
    auto reply = pong.fast_send(ping, &sender);
    const uint64_t t1 = perf::now_ns();
    if (i >= warmup)
      samples.push_back(t1 - t0);
    delete ping; // fast_send does not take ownership of the input
  }
  const uint64_t win_end = perf::now_ns();
  auto s = perf::LatencyStats::from(label, samples);
  s.amortized = measured ? static_cast<double>(win_end - win_start) / measured : 0.0;
  return s;
}

// fast_send round trip with a stack-allocated input message and a reply.
perf::LatencyStats run_fastsend_stack(const std::string& label, size_t measured, size_t warmup)
{
  std::vector<uint64_t> samples;
  samples.reserve(measured);
  PongActor<Ping, Pong> pong;
  NullActor sender;
  const size_t total = measured + warmup;
  uint64_t win_start = 0;
  for (size_t i = 0; i < total; ++i)
  {
    if (i == warmup)
      win_start = perf::now_ns();
    Ping ping(i); // on the stack: no heap allocation for the input
    const uint64_t t0 = perf::now_ns();
    auto reply = pong.fast_send(&ping, &sender);
    const uint64_t t1 = perf::now_ns();
    if (i >= warmup)
      samples.push_back(t1 - t0);
    // nothing to free for the input; reply (Pong) freed by unique_ptr
  }
  const uint64_t win_end = perf::now_ns();
  auto s = perf::LatencyStats::from(label, samples);
  s.amortized = measured ? static_cast<double>(win_end - win_start) / measured : 0.0;
  return s;
}

// Sink so the compiler cannot optimize the trivial handler work away.
volatile uint64_t g_sink = 0;

// The handler's "work", also callable directly (no framework) as a baseline.
// noinline so the direct-call baseline is a real call comparable to the
// framework's indirect dispatch, rather than being inlined to nothing.
[[gnu::noinline]] void echo_work(const Ping* m) noexcept { g_sink += m->seq; }

// Handler that does not reply -- isolates dispatch (+ input alloc) from the reply.
template <class PingT>
class SinkActor : public Actor
{
public:
  SinkActor()
  {
    std::strncpy(name, "SinkActor", sizeof(name) - 1);
    MESSAGE_HANDLER(PingT, on_ping);
  }

private:
  void on_ping(const PingT* m) noexcept { g_sink += m->seq; } // same work as echo_work
};

// Baseline: call the handler's work as a plain function -- no actor framework,
// no mutex, no dispatch table, no message wrapping. Pairs with
// run_fastsend_stack_noreply (same stack input, same trivial work), so the
// difference is exactly fast_send's per-call overhead.
perf::LatencyStats run_direct_call(const std::string& label, size_t measured, size_t warmup)
{
  std::vector<uint64_t> samples;
  samples.reserve(measured);
  const size_t total = measured + warmup;
  uint64_t win_start = 0;
  for (size_t i = 0; i < total; ++i)
  {
    if (i == warmup)
      win_start = perf::now_ns();
    Ping ping(i);
    const uint64_t t0 = perf::now_ns();
    echo_work(&ping); // direct function call, no framework
    const uint64_t t1 = perf::now_ns();
    if (i >= warmup)
      samples.push_back(t1 - t0);
  }
  const uint64_t win_end = perf::now_ns();
  auto s = perf::LatencyStats::from(label, samples);
  s.amortized = measured ? static_cast<double>(win_end - win_start) / measured : 0.0;
  return s;
}

// fast_send in a loop, stack input, no reply: pure dispatch cost.
perf::LatencyStats run_fastsend_stack_noreply(const std::string& label, size_t measured,
                                              size_t warmup)
{
  std::vector<uint64_t> samples;
  samples.reserve(measured);
  SinkActor<Ping> sink;
  NullActor sender;
  const size_t total = measured + warmup;
  uint64_t win_start = 0;
  for (size_t i = 0; i < total; ++i)
  {
    if (i == warmup)
      win_start = perf::now_ns();
    Ping ping(i);
    const uint64_t t0 = perf::now_ns();
    auto reply = sink.fast_send(&ping, &sender); // returns null (no reply)
    const uint64_t t1 = perf::now_ns();
    if (i >= warmup)
      samples.push_back(t1 - t0);
  }
  const uint64_t win_end = perf::now_ns();
  auto s = perf::LatencyStats::from(label, samples);
  s.amortized = measured ? static_cast<double>(win_end - win_start) / measured : 0.0;
  return s;
}

// fast_send in a loop, heap input (PingT allocator), no reply. Pairs with
// run_fastsend_heap<PingT,PongT>: identical input handling, so the difference
// is exactly the reply (its allocation + reply()/unique_ptr plumbing).
template <class PingT>
perf::LatencyStats run_fastsend_heap_noreply(const std::string& label, size_t measured,
                                             size_t warmup)
{
  std::vector<uint64_t> samples;
  samples.reserve(measured);
  SinkActor<PingT> sink;
  NullActor sender;
  const size_t total = measured + warmup;
  uint64_t win_start = 0;
  for (size_t i = 0; i < total; ++i)
  {
    if (i == warmup)
      win_start = perf::now_ns();
    const auto* ping = new PingT(i);
    const uint64_t t0 = perf::now_ns();
    auto reply = sink.fast_send(ping, &sender); // returns null (no reply)
    const uint64_t t1 = perf::now_ns();
    if (i >= warmup)
      samples.push_back(t1 - t0);
    delete ping;
  }
  const uint64_t win_end = perf::now_ns();
  auto s = perf::LatencyStats::from(label, samples);
  s.amortized = measured ? static_cast<double>(win_end - win_start) / measured : 0.0;
  return s;
}

} // namespace

int main(int argc, char** argv)
{
  size_t measured = 1000000;
  size_t warmup = 10000;
  std::string section = "all";

  if (argc > 1)
  {
    if (std::strcmp(argv[1], "-h") == 0 || std::strcmp(argv[1], "--help") == 0)
    {
      std::printf("usage: %s [N] [warmup] [section: transport|alloc|fastsend|batch|all]\n", argv[0]);
      return 0;
    }
    measured = std::strtoull(argv[1], nullptr, 10);
  }
  if (argc > 2)
    warmup = std::strtoull(argv[2], nullptr, 10);
  if (argc > 3)
    section = argv[3];
  if (measured == 0)
  {
    std::fprintf(stderr, "N must be > 0\n");
    return 2;
  }

  const bool all = (section == "all");
  std::vector<perf::LatencyStats> rows;

  if (all || section == "transport")
  {
    rows.push_back(run_send<Ping, Pong>("send ungrouped", false, measured, warmup));
    rows.push_back(run_send<Ping, Pong>("send grouped", true, measured, warmup));
    rows.push_back(run_fastsend_heap<Ping, Pong>("fast_send", measured, warmup));
  }
  if (all || section == "alloc")
  {
    rows.push_back(run_send<Ping, Pong>("grouped plain-new", true, measured, warmup));
    rows.push_back(run_send<PingPool, PongPool>("grouped pooled", true, measured, warmup));
  }
  if (all || section == "fastsend")
  {
    rows.push_back(run_fastsend_heap<Ping, Pong>("fs heap+reply", measured, warmup));
    rows.push_back(run_fastsend_heap_noreply<Ping>("fs heap noreply", measured, warmup));
    rows.push_back(run_fastsend_heap<PingPool, PongPool>("fs pooled+reply", measured, warmup));
    rows.push_back(run_fastsend_heap_noreply<PingPool>("fs pooled noreply", measured, warmup));
    rows.push_back(run_fastsend_stack("fs stack+reply", measured, warmup));
    rows.push_back(run_fastsend_stack_noreply("fs stack noreply", measured, warmup));
    rows.push_back(run_direct_call("direct call (base)", measured, warmup));
  }

  if (all || section == "batch")
  {
    // A 16-deep burst keeps the receiver's mailbox backlogged, so the queue
    // type (single vs whole-mailbox batch drain, sharded, lock-free) actually
    // matters. amort is the per-message throughput under load.
    const size_t W = 16;
    rows.push_back(run_burst<Ping, Pong>("burst16 BQueue",        Actor::MailboxKind::BQueue,        W, measured, warmup));
    rows.push_back(run_burst<Ping, Pong>("burst16 BQueueBatched", Actor::MailboxKind::BQueueBatched, W, measured, warmup));
    rows.push_back(run_burst<Ping, Pong>("burst16 ShardedBQueue", Actor::MailboxKind::ShardedBQueue, W, measured, warmup));
    rows.push_back(run_burst<Ping, Pong>("burst16 LockFreeMPSC",  Actor::MailboxKind::LockFreeMPSC,  W, measured, warmup));
  }

  if (rows.empty())
  {
    std::fprintf(stderr, "unknown section: %s\n", section.c_str());
    return 2;
  }

#ifdef DISABLE_MEMORY_POOL
  const char* pool_state = "MemoryPool DISABLED (global new/delete)";
#else
  const char* pool_state = "MemoryPool ENABLED";
#endif
  std::printf("\nbuild: %s\n", pool_state);
  perf::print_header("actor messaging round-trip latency", measured, warmup);
  for (const auto& r : rows)
    perf::print_row(r);
  std::printf("(one-way ~ round-trip / 2 for the symmetric echo handler; "
              "fast_send rows near ~40ns are at steady_clock resolution)\n");

  // fast_send vs a plain function call, measured CLEANLY: each is timed with a
  // single clock-read pair around a tight loop (no per-iteration clock reads),
  // so neither number carries the ~28 ns of clock overhead the table's amort
  // column does. Both do identical trivial work on a stack input, so the delta
  // is fast_send's framework overhead (mutex + message-field setup +
  // handler-cache dispatch + reply wrapping) over a bare call.
  if (all || section == "fastsend")
  {
    SinkActor<Ping> sink;
    NullActor sender;
    const size_t total = measured + warmup;

    uint64_t c0 = 0;
    for (size_t i = 0; i < total; ++i)
    {
      if (i == warmup)
        c0 = perf::now_ns();
      Ping ping(i);
      echo_work(&ping);
    }
    const double call_ns = static_cast<double>(perf::now_ns() - c0) / measured;

    uint64_t f0 = 0;
    for (size_t i = 0; i < total; ++i)
    {
      if (i == warmup)
        f0 = perf::now_ns();
      Ping ping(i);
      auto reply = sink.fast_send(&ping, &sender);
    }
    const double fs_ns = static_cast<double>(perf::now_ns() - f0) / measured;

    std::printf("\nfast_send vs direct function call (clean amortized, per op):\n");
    std::printf("  direct call : %5.1f ns\n", call_ns);
    std::printf("  fast_send   : %5.1f ns  (+%.1f ns, %.1fx a bare call)\n", fs_ns,
                fs_ns - call_ns, call_ns > 0 ? fs_ns / call_ns : 0.0);
  }
  return 0;
}
