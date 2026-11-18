// AquisFeed actor: routing of the handler's records to OBs and the recorder.
// The actor is not started; its handler is called directly and `deliver` is
// captured.
#include <gtest/gtest.h>

#include "Pkt.hpp"
#include "aquis/act/AquisFeed.hpp"
#include "enum/e_names.hpp"

using namespace aquis_test;

namespace
{

struct Sink : actors::Actor
{
  explicit Sink(const char *n) { snprintf(name, sizeof(name), "%s", n); }
};

struct TestFeed : aquis::AquisFeed
{
  using AquisFeed::AquisFeed;
  std::vector<std::pair<actors::Actor *, bfile::l3_t>> got;
  void deliver(actors::Actor *t, const bfile::l3_t &r) override { got.emplace_back(t, r); }
  void bytes(aquis::msg::AquisBytes::Source s, const std::string &d)
  {
    aquis::msg::AquisBytes m(s, d);
    // The registered handler, as the actor's thread would call it.
    process_message_internal(&m, true);
  }
};

} // namespace

TEST(AquisFeed, RoutesRecords)
{
  auto ins = std::make_unique<aquis::Instruments>();
  ins->add_universe("VODl", 1001);
  ins->add_universe("DPLMl", 1002);
  ins->on_definition(1, "VODl", "", "GBX", "XLON", 0, 0, "");
  ins->on_definition(2, "DPLMl", "", "GBX", "XLON", 0, 0, "");
  aquis::AquisFeed::Config cfg;
  cfg.handler.venue = char(en::x::AQXE);
  std::vector<std::pair<uint32_t, uint32_t>> req;
  TestFeed f(cfg, std::move(ins), [&](uint32_t b, uint32_t e) { req.emplace_back(b, e); });
  Sink ob1("ob1"), ob2("ob2"), rec("rec");
  f.add_book(1001, &ob1);
  f.add_book(1002, &ob2);
  f.set_recorder(&rec);

  f.bytes(aquis::msg::AquisBytes::Source::live, packet({add(1, 2, 1, 10, 100000, 5, 1)}));
  // add -> ob2 and recorder; end of packet -> recorder
  ASSERT_EQ(f.got.size(), 3u);
  EXPECT_EQ(f.got[0].first, &ob2);
  EXPECT_EQ(f.got[1].first, &rec);
  EXPECT_EQ(f.got[2].first, &rec);
  EXPECT_TRUE(std::holds_alternative<bfile::l3_eop_t>(f.got[2].second));

  f.got.clear();
  f.bytes(aquis::msg::AquisBytes::Source::live, packet({add(3, 1, 1, 10, 100000, 6, 2)})); // gap 2
  // gap -> ob1, ob2, recorder; then end of packet
  size_t gaps = 0;
  for (auto &[t, r] : f.got)
    gaps += std::holds_alternative<bfile::l3_gap_v2_t>(r);
  EXPECT_EQ(gaps, 3u);
  ASSERT_EQ(req.size(), 1u);
  EXPECT_EQ(req[0], std::make_pair(2u, 2u));

  f.got.clear();
  f.bytes(aquis::msg::AquisBytes::Source::replay_lost, "");
  size_t clears = 0;
  for (auto &[t, r] : f.got)
    clears += std::holds_alternative<bfile::l3_chr_v2_t>(r);
  EXPECT_EQ(clears, 3u);
  EXPECT_EQ(f.handler().state(), aquis::Handler::State::snapshot);
}

TEST(AquisFeed, CreateReportsBadFiles)
{
  aquis::AquisFeed::Config cfg;
  cfg.universe_file = "/nonexistent/universe.csv";
  std::string err;
  EXPECT_EQ(aquis::AquisFeed::create(cfg, nullptr, &err), nullptr);
  EXPECT_NE(err.find("cannot open"), std::string::npos);
}
