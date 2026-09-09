/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 *
 * Dispatch-mechanism cost: fast_send against a plain call and a virtual call.
 *
 * bench_pingpong section D compares fast_send to a direct free-function call
 * only. That is the wrong baseline for the question people actually ask, which
 * is "what does fast_send cost against the C++ dispatch I would have written by
 * hand" -- and by hand that is a virtual call, not a static one. This bench
 * fills that in and decomposes the difference.
 *
 * Every arm does the SAME work -- g_sink += m->seq on a stack-allocated Dis --
 * and differs only in how that work is reached:
 *
 *   A empty          loop + message construction + the work inlined.
 *                    The floor: loop overhead, nothing else.
 *   B direct         free function, real call (noipa).
 *   C nonvirt/ptr    load a Handler* from the dispatch table, call a NON-virtual
 *                    method. B plus the pointer load, minus the vtable. This
 *                    exists so the virtual arms can be charged for the vtable
 *                    alone rather than for the pointer load they also need.
 *   D virtual x1     virtual call, table holds ONE dynamic type.
 *   E virtual x2     virtual call, two types, alternating.
 *   F virtual x4     virtual call, four types, cyclic period 4.
 *   G virtual x4 rnd four types in a fixed shuffled order.
 *   H virtual x8 rnd eight types in a fixed shuffled order.
 *   I ptr-to-member  indirect call through a member function pointer loaded
 *                    from a table. This is the mechanism Actor::handler_cache
 *                    uses, isolated from the rest of the framework.
 *   J fast_send      the framework: id lookup, handler_cache dispatch, reply
 *                    plumbing. Handler does no reply, so this is pure dispatch.
 *
 * D-C is the vtable cost with the target predicted. E..H - D is the indirect
 * branch misprediction cost as the call site gets more polymorphic; the sweep
 * is there because a SINGLE polymorphic arm would be a number with no error
 * bar, and "virtual calls cost X" depends entirely on how predictable the site
 * is. J - I is what the framework adds over the bare indirect call it is
 * built from.
 *
 * Arms C..H all index a 1024-entry table of Handler* and differ ONLY in how
 * many distinct dynamic types those entries have. Identical loads, identical
 * cache footprint, identical instruction sequence (verified in the
 * disassembly: mov (%rax,%rdx,8),%rdi / mov (%rdi),%rax / callq *0x10(%rax)).
 * The only variable is what the branch predictor can learn.
 *
 * Timing is amortized: one clock-read pair per arm per repeat, total/N. The
 * per-op costs here (1-30 ns) are at or below steady_clock resolution, so
 * per-sample percentiles would measure the clock, not the code. The measured
 * tick is printed so you can see that for yourself rather than taking a
 * platform constant on faith.
 *
 * Each arm is run REPEATS times; min and median are both reported. Use min for
 * comparisons -- it is the run least contaminated by preemption -- and read the
 * spread between min and median as the noise floor of the box.
 *
 *   usage: bench_dispatch [N] [warmup] [repeats]
 *     N        measured iterations per arm per repeat (default 5000000)
 *     warmup   discarded iterations                   (default 50000)
 *     repeats  runs per arm                           (default 5)
 */

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "actors/Actor.hpp"

#include "bench_common.hpp"

using namespace actors;

namespace
{

// MessageT, not Message_N: the id is auto-assigned and collision-free, which is
// what Message.hpp tells new types to use. bench_pingpong hand-assigns 100-103
// and #37 records that range already has documented reuse -- no reason to add
// another hand-picked id to that pile.
//
// One measurement consequence: MessageT's ctor calls message_id<Dis>(), a
// function-local static, so each construction pays a guard-variable load and a
// well-predicted branch that a Message_N would not. Every arm below -- including
// the empty-loop floor -- constructs a Dis, so this cancels exactly out of the
// net-of-floor column and out of every difference between arms. It does mean
// the raw ns figures here sit slightly above bench_pingpong's for the same
// operation; compare deltas across benches, not absolutes.
struct Dis : public MessageT<Dis>
{
  uint64_t seq;
  explicit Dis(uint64_t s) : seq(s) {}
};

// Sink so no arm can be optimized away, and so every arm is forced to actually
// perform the load and the add.
volatile uint64_t g_sink = 0;

// noipa on every work function, uniformly: it defeats inlining, cloning AND
// identical-code folding. Without the ICF part, GCC merges the four virtual
// overrides below into one symbol, all four vtable slots point at it, and the
// polymorphic arm silently becomes the monomorphic arm.

// --- B: plain function -------------------------------------------------------
[[gnu::noipa]] void direct_work(const Dis* m) noexcept { g_sink += m->seq; }

// --- C/D/E: virtual dispatch -------------------------------------------------
struct Handler
{
  virtual ~Handler() = default;
  virtual void handle(const Dis* m) const noexcept = 0;
  // Same body, reached without the vtable.
  [[gnu::noipa]] void plain(const Dis* m) const noexcept { g_sink += m->seq; }
};

template <int Tag>
struct HandlerN final : public Handler
{
  [[gnu::noipa]] void handle(const Dis* m) const noexcept override { g_sink += m->seq; }
};

// --- F: pointer-to-member ----------------------------------------------------
struct MemHost
{
  [[gnu::noipa]] void work(const Dis* m) noexcept { g_sink += m->seq; }
};
using MemFn = void (MemHost::*)(const Dis*) noexcept;

// --- G: the framework --------------------------------------------------------
class SinkActor : public Actor
{
public:
  SinkActor()
  {
    std::strncpy(name, "SinkActor", sizeof(name) - 1);
    MESSAGE_HANDLER(Dis, on_dis);
  }

private:
  void on_dis(const Dis* m) noexcept { g_sink += m->seq; } // same work as the rest
};

class NullActor : public Actor
{
public:
  NullActor() { std::strncpy(name, "NullActor", sizeof(name) - 1); }
};

// --- harness -----------------------------------------------------------------

// One clock-read pair for the whole measured window. Resolves below the tick.
// The `i == warmup` test is inside the loop for every arm, including the empty
// one, so its cost is in the floor and cancels out of every difference.
template <class F>
double amort_ns(size_t measured, size_t warmup, F&& body)
{
  const size_t total = measured + warmup;
  uint64_t t0 = 0;
  for (size_t i = 0; i < total; ++i)
  {
    if (i == warmup)
      t0 = perf::now_ns();
    body(i);
  }
  const uint64_t t1 = perf::now_ns();
  return measured ? static_cast<double>(t1 - t0) / measured : 0.0;
}

struct Arm
{
  const char* label;
  std::vector<double> runs;
  double min = 0.0;
  double med = 0.0;
};

void reduce(Arm& a)
{
  std::vector<double> v = a.runs;
  std::sort(v.begin(), v.end());
  a.min = v.empty() ? 0.0 : v.front();
  a.med = v.empty() ? 0.0 : v[v.size() / 2];
}

// Smallest non-zero delta between two back-to-back clock reads: the tick the
// per-sample percentiles in bench_pingpong are quantized to.
double measure_tick_ns()
{
  uint64_t best = UINT64_MAX;
  for (int i = 0; i < 200000; ++i)
  {
    const uint64_t a = perf::now_ns();
    const uint64_t b = perf::now_ns();
    if (b > a && (b - a) < best)
      best = b - a;
  }
  return best == UINT64_MAX ? 0.0 : static_cast<double>(best);
}

} // namespace

int main(int argc, char** argv)
{
  const size_t measured = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 5000000;
  const size_t warmup = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 50000;
  const int repeats = argc > 3 ? std::atoi(argv[3]) : 5;

  std::printf("dispatch cost: fast_send vs plain call vs virtual call\n");
  std::printf("  N=%zu  warmup=%zu  repeats=%d\n", measured, warmup, repeats);
  std::printf("  steady_clock measured tick: %.1f ns\n", measure_tick_ns());
  std::printf("  (all arms do the identical work: g_sink += m->seq)\n\n");

  // Eight distinct dynamic types. noipa keeps them eight distinct symbols; ICF
  // would otherwise fold these identical bodies into one and every "polymorphic"
  // arm would silently collapse into the monomorphic one.
  HandlerN<0> t0;
  HandlerN<1> t1;
  HandlerN<2> t2;
  HandlerN<3> t3;
  HandlerN<4> t4;
  HandlerN<5> t5;
  HandlerN<6> t6;
  HandlerN<7> t7;
  Handler* all[8] = {&t0, &t1, &t2, &t3, &t4, &t5, &t6, &t7};

  // One table shape for every virtual arm: 1024 entries, 8 KB, L1-resident on
  // any relevant core. Only the CONTENT varies. Indexed i & kMask so the index
  // arithmetic is identical too.
  constexpr size_t kTab = 1024;
  constexpr size_t kMask = kTab - 1;
  std::vector<Handler*> tab_x1(kTab), tab_x2(kTab), tab_x4(kTab), tab_x4r(kTab), tab_x8r(kTab);
  for (size_t i = 0; i < kTab; ++i)
  {
    tab_x1[i] = all[0];
    tab_x2[i] = all[i & 1];
    tab_x4[i] = all[i & 3];
    tab_x4r[i] = all[0]; // filled below
    tab_x8r[i] = all[0];
  }
  // Fixed-seed LCG, not std::random_device: the shuffled order must be identical
  // on every run and every box, or the "random" arms are not comparable between
  // runs and the numbers cannot be reproduced.
  uint64_t rng = 0x243F6A8885A308D3ull;
  auto next = [&rng]() { rng = rng * 6364136223846793005ull + 1442695040888963407ull;
                         return static_cast<uint32_t>(rng >> 33); };
  for (size_t i = 0; i < kTab; ++i)
  {
    tab_x4r[i] = all[next() & 3];
    tab_x8r[i] = all[next() & 7];
  }

  MemHost host;
  std::vector<MemFn> fns(kTab, &MemHost::work);

  SinkActor sink;
  NullActor sender;

  Arm arms[] = {{"A empty loop (floor)", {}, 0, 0},
                {"B direct call", {}, 0, 0},
                {"C non-virtual via ptr", {}, 0, 0},
                {"D virtual, 1 type", {}, 0, 0},
                {"E virtual, 2 types cyclic", {}, 0, 0},
                {"F virtual, 4 types cyclic", {}, 0, 0},
                {"G virtual, 4 types shuffled", {}, 0, 0},
                {"H virtual, 8 types shuffled", {}, 0, 0},
                {"I ptr-to-member call", {}, 0, 0},
                {"J fast_send", {}, 0, 0}};

  for (int r = 0; r < repeats; ++r)
  {
    arms[0].runs.push_back(amort_ns(measured, warmup, [&](size_t i) {
      Dis m(i);
      g_sink += m.seq;
    }));
    arms[1].runs.push_back(amort_ns(measured, warmup, [&](size_t i) {
      Dis m(i);
      direct_work(&m);
    }));
    arms[2].runs.push_back(amort_ns(measured, warmup, [&](size_t i) {
      Dis m(i);
      tab_x1[i & kMask]->plain(&m);
    }));
    arms[3].runs.push_back(amort_ns(measured, warmup, [&](size_t i) {
      Dis m(i);
      tab_x1[i & kMask]->handle(&m);
    }));
    arms[4].runs.push_back(amort_ns(measured, warmup, [&](size_t i) {
      Dis m(i);
      tab_x2[i & kMask]->handle(&m);
    }));
    arms[5].runs.push_back(amort_ns(measured, warmup, [&](size_t i) {
      Dis m(i);
      tab_x4[i & kMask]->handle(&m);
    }));
    arms[6].runs.push_back(amort_ns(measured, warmup, [&](size_t i) {
      Dis m(i);
      tab_x4r[i & kMask]->handle(&m);
    }));
    arms[7].runs.push_back(amort_ns(measured, warmup, [&](size_t i) {
      Dis m(i);
      tab_x8r[i & kMask]->handle(&m);
    }));
    // The Itanium ABI represents a pointer-to-member as {ptr, adjustment} and a
    // call through one tests the low bit to decide virtual vs non-virtual --
    // `test $0x1,%dl` / `callq *-0x1(%rdx,%rax,1)` in the disassembly. GCC's
    // -Warray-bounds models that as a possible vtable load out of the 1-byte
    // MemHost and warns. It is a false positive on the not-taken path; the
    // sequence is exactly the indirect call this arm is meant to measure.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
    arms[8].runs.push_back(amort_ns(measured, warmup, [&](size_t i) {
      Dis m(i);
      (host.*fns[i & kMask])(&m);
    }));
#pragma GCC diagnostic pop
    arms[9].runs.push_back(amort_ns(measured, warmup, [&](size_t i) {
      Dis m(i);
      auto reply = sink.fast_send(&m, &sender);
      (void)reply;
    }));
  }

  for (auto& a : arms)
    reduce(a);

  const double floor_ns = arms[0].min;
  const double direct = arms[1].min;

  std::printf("%-30s %9s %9s %11s %10s\n", "arm", "min ns", "med ns", "net of floor", "x direct");
  std::printf("%-30s %9s %9s %11s %10s\n", "---", "------", "------", "-----------", "--------");
  for (const auto& a : arms)
    std::printf("%-30s %9.2f %9.2f %11.2f %9.1fx\n", a.label, a.min, a.med, a.min - floor_ns,
                direct > 0 ? a.min / direct : 0.0);

  std::printf("\ndecomposition (min):\n");
  std::printf("  pointer load                = C - B : %6.2f ns\n", arms[2].min - arms[1].min);
  std::printf("  vtable, target predicted    = D - C : %6.2f ns\n", arms[3].min - arms[2].min);
  std::printf("  mispredict, 2 types cyclic  = E - D : %6.2f ns\n", arms[4].min - arms[3].min);
  std::printf("  mispredict, 4 types cyclic  = F - D : %6.2f ns\n", arms[5].min - arms[3].min);
  std::printf("  mispredict, 4 types random  = G - D : %6.2f ns\n", arms[6].min - arms[3].min);
  std::printf("  mispredict, 8 types random  = H - D : %6.2f ns\n", arms[7].min - arms[3].min);
  std::printf("  ptr-to-member vs virtual    = I - D : %6.2f ns\n", arms[8].min - arms[3].min);
  std::printf("  framework over a bare call  = J - I : %6.2f ns\n", arms[9].min - arms[8].min);

  std::printf("\nheadline -- fast_send measured against each hand-written alternative:\n");
  const int cmp[] = {1, 3, 5, 6, 7};
  for (int c : cmp)
    std::printf("  vs %-28s %+7.2f ns  (%.2fx)\n", arms[c].label + 2, arms[9].min - arms[c].min,
                arms[c].min > 0 ? arms[9].min / arms[c].min : 0.0);

  std::printf("\nall runs, ns per op:\n");
  for (const auto& a : arms)
  {
    std::printf("  %-30s", a.label);
    for (double d : a.runs)
      std::printf(" %8.2f", d);
    std::printf("\n");
  }

  std::printf("\nsink=%llu (ignore; prevents dead-code elimination)\n",
              static_cast<unsigned long long>(g_sink));
  return 0;
}
