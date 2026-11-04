/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// The MDP3 handler end to end, the cpu_fpga setup: the card's actors on a SoftCard
// (a test harness, no timing), a real FpgaFeed actor on its FpgaLanes mailbox, and
// a sink standing for the strategy that checks every packet's book against the
// software book. The card side in hardware is hls/feed_top.cpp.
//
//   feed_run packets.bin ref.book <contracts, comma list; the first is compared> [max packets]

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <thread>
#include <vector>

#include "actors/act/Manager.hpp"
#include "fpga_feed.hpp"
#include "kfpga/soft_card.hpp"
#include "mdp3fpga/decoder.hpp"
#include "mdp3fpga/feed.hpp"

using namespace mdp3fpga;
using kfpga::Fifo;
using DecoderProc = kfpga::Process<SbeDecoder>;
using FilterProc = kfpga::Process<Filter>;
using BatcherProc = kfpga::Process<Batcher>;

namespace {

class Card
{
public:
  WordStream words;
  Fifo decoded{4096}, kept{4096};
  kfpga::Link table{256};     // CPU -> Filter, the table
  kfpga::Link lines{65536};   // Batcher -> CPU, the lane
  DecoderProc decoder;
  FilterProc filter;
  BatcherProc batcher;
  kfpga::OutPort decoder_ports[1] = {{decoded}};
  kfpga::Port filter_ports[2] = {{decoded, kept}, table.server()};
  kfpga::Port batcher_ports[1] = {{kept, lines.fwd}};

  Card()
  {
    DecoderProc::member<SbeDecoder>().books = kfpga::ActorRef(0, kFilter);
    FilterProc::member<Filter>().batcher = kfpga::ActorRef(0, kBatcher);
    BatcherProc::member<Batcher>().feed = kfpga::ActorRef(0, kFeed);
  }
};

// Stands for the strategy: runs on FpgaFeed's thread, in one call per packet.
class Check : public PacketSink
{
  FILE *ref_;
  char want_[1024], got_[1024];

public:
  std::atomic<uint64_t> packets{0};
  uint64_t compared = 0, differ = 0, events = 0, missing = 0, most = 0;

  explicit Check(FILE *ref) : ref_(ref) {}

  void on_packet(const Books &books, const Event *ev, size_t n, bool gap) override
  {
    uint32_t mine = 0;
    for (size_t i = 0; i < n; ++i)
      if (ev[i].contract() == 0 || (ev[i].action() == kActionOther && ev[i].level() == kOtherChannelReset))
        ++mine;
    events += n;
    most = n > most ? n : most;
    if (mine != 0)
    {
      const Book &b = books.book(0);
      int at = std::snprintf(got_, sizeof(got_), "Z 0 %u %u b", gap ? 1 : 0, mine);
      for (int i = 0; i < kLevels; ++i)
        at += std::snprintf(got_ + at, sizeof(got_) - at, " %lld %d %d", b.bid[i].price, b.bid[i].size, b.bid[i].orders);
      at += std::snprintf(got_ + at, sizeof(got_) - at, " a");
      for (int i = 0; i < kLevels; ++i)
        at += std::snprintf(got_ + at, sizeof(got_) - at, " %lld %d %d", b.ask[i].price, b.ask[i].size, b.ask[i].orders);
      std::snprintf(got_ + at, sizeof(got_) - at, "\n");
      if (!std::fgets(want_, sizeof(want_), ref_))
        ++missing;
      else
      {
        char *a = std::strchr(want_, ' ');
        char *c = a ? std::strchr(a + 1, ' ') : nullptr;
        if (c)
        {
          a[1] = '0';
          std::memmove(a + 2, c, std::strlen(c) + 1);
        }
        if (std::strcmp(got_, want_) != 0 && ++differ <= 3)
          std::printf("packet end %llu\n  card: %s  ref:  %s", (unsigned long long)compared + 1, got_, want_);
      }
      ++compared;
    }
    packets.store(packets.load(std::memory_order_relaxed) + 1, std::memory_order_release);
  }
};

bool read_packet(FILE *f, std::vector<uint8_t> &p)
{
  uint8_t l[4];
  if (std::fread(l, 1, 4, f) != 4)
    return false;
  const uint32_t n = l[0] | (l[1] << 8) | (l[2] << 16) | (uint32_t(l[3]) << 24);
  p.resize(n);
  return std::fread(p.data(), 1, n, f) == n;
}

} // namespace

int main(int argc, char **argv)
{
  if (argc < 4)
  {
    std::fprintf(stderr, "usage: feed_run packets.bin ref.book contracts [max packets]\n");
    return 2;
  }
  FILE *packets = std::fopen(argv[1], "rb");
  FILE *ref = std::fopen(argv[2], "r");
  if (!packets || !ref)
  {
    std::perror("open");
    return 1;
  }
  std::vector<int32_t> contracts;
  for (const char *s = argv[3]; s && *s;)
  {
    contracts.push_back(std::atoi(s));
    s = std::strchr(s, ',');
    s = s ? s + 1 : nullptr;
  }
  const uint64_t max_packets = argc > 4 ? std::strtoull(argv[4], nullptr, 10) : ~0ull;

  Card card;
  kfpga::SoftCard soft;
  soft.add_step([&card] {
    card.decoder.step_stream<SbeDecoder>(card.decoder_ports, card.words);
    return true;
  });
  soft.add(card.filter, card.filter_ports);
  soft.add(card.batcher, card.batcher_ports);

  kfpga::Codec codec;
  cpu::add_messages(codec);

  Check check(ref);
  std::atomic<bool> ready{false};
  auto *feed = new FpgaFeed(contracts, check, [&ready] { ready.store(true, std::memory_order_release); });
  kfpga::CpuEnd *table = kfpga::CpuEnd::client(card.table, feed, kFeed, kFilter, codec);
  kfpga::CpuEnd *lane = kfpga::CpuEnd::server(card.lines, feed, kFeed, kBatcher, codec);
  feed->set_filter(table->ref());

  actors::Manager mgr;
  mgr.add_to_manage_q(feed);
  soft.start();
  mgr.init();
  while (!ready.load(std::memory_order_acquire))
    std::this_thread::yield();

  // The wire: a packet's words, held back while the card is behind.
  std::vector<uint8_t> p;
  uint64_t sent = 0;
  while (sent < max_packets && read_packet(packets, p))
  {
    const size_t n = p.size();
    for (size_t at = 0; at < n || at == 0; at += 8)
    {
      Word w;
      w.data = 0;
      w.keep = 0;
      for (int i = 0; i < 8 && at + i < n; ++i)
      {
        w.data.range(8 * i + 7, 8 * i) = p[at + i];
        w.keep[i] = 1;
      }
      w.last = at + 8 >= n;
      while (card.words.size() > 4096)
        std::this_thread::yield();
      card.words.write(w);
    }
    ++sent;
  }

  // Done when nothing has moved for a while.
  uint64_t last = ~0ull;
  for (;;)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    const uint64_t now = check.packets.load(std::memory_order_acquire);
    if (now == last && card.words.empty())
      break;
    last = now;
  }
  mgr.end();
  soft.stop();

  // With every packet sent, the reference must be used up too.
  char line[1024];
  const bool extra = max_packets == ~0ull && std::fgets(line, sizeof(line), ref) != nullptr;
  std::printf("%llu packets sent, %llu reached the strategy, %llu compared, %llu differ, %llu missing\n",
              (unsigned long long)sent, (unsigned long long)check.packets.load(), (unsigned long long)check.compared,
              (unsigned long long)check.differ, (unsigned long long)check.missing);
  std::printf("%llu events, %llu lines, %llu gaps, %llu buffer overflows, most events in a packet %llu, codec errors %u\n",
              (unsigned long long)check.events, (unsigned long long)feed->lines, (unsigned long long)feed->gaps,
              (unsigned long long)feed->overflows, (unsigned long long)check.most, table->errors() + lane->errors());
  const bool ok = check.differ == 0 && check.missing == 0 && !extra && check.compared != 0 && feed->overflows == 0 &&
                  table->errors() + lane->errors() == 0;
  std::printf("%s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}
