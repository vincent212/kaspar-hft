/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// The recorded Kraken frames through KrakenBookSync, Mbp2Mbo and a real OB
// (in-process and synchronous, as unit_test/src/test_mbp2mbo_ob.cpp does):
// OB's book must hold what Mbp2Mbo holds.

#include <gtest/gtest.h>

#include <boost/property_tree/ptree.hpp>
#include <cstdio>

#include "enum/e_names.hpp"
#include "frame/cons/msg/Get.hpp"
#include "frame/cons/msg/Page.hpp"
#include "frame/mda/msg/Data.hpp"
#include "frame/ob/act/OB.hpp"
#include "frame/ref/RefData.hpp"
#include "kraken_md/FrameRecorder.hpp"
#include "kraken_md/KrakenBookSync.hpp"
#include "kraken_md/KrakenJson.hpp"
#include "unit_test/MockActor.hpp"
#include "unit_test/TestHelper.hpp"

using namespace frame;
using namespace kraken;

namespace
{
  // The venue on the records sent to OB. KRAKEN when the Kaspar libraries were
  // built from a tree that has it in exch_code.enum; libraries built before
  // that reject it (en::is_valid in frame::mda::msg::Data), so the Makefile
  // passes -DKRAKEN_OB_VENUE=COINBASE when linking against older libraries.
#ifndef KRAKEN_OB_VENUE
#define KRAKEN_OB_VENUE KRAKEN
#endif
  constexpr en::x kObVenue = en::x::KRAKEN_OB_VENUE;

  struct TestableOB : public ob::act::OB
  {
    using ob::act::OB::OB;
    using ob::act::OB::reply_to;
  };
}

TEST(KrakenBookOB, RecordedFramesOBMatchesMbp2Mbo)
{
  const char *k = std::getenv("KSPRPROJ");
  ref::RefData::set_universe(std::string(k ? k : "") + "/unit_test/config/universe.csv", "");
  ref::RefData::inst();
  auto a = ref::RefData::add_future_asset("KRAKEN_BTCUSD_T", kObVenue, 9902, "", "BTC/USD", 1.0);
  ASSERT_NE(a, nullptr);
  a->maxpx = 1 << 30;
  boost::property_tree::ptree pt;
  pt.put("ladder_ticks", 1 << 19);
  unit_test::MockActor probe{"probe"};
  TestableOB ob(nullptr, true, nullptr, a->id, pt);
  auto dispatch = [&](const actors::Message *m) {
    ob.reply_to = &probe;
    unit_test::TestHelper::invoke_handler(&ob, m, &probe);
    ob.reply_to = nullptr;
  };
  size_t records = 0;
  sim::Mbp2Mbo m(sim::Mbp2Mbo::Config{9902, char(kObVenue), 0.1, 0.001}, [&](const bfile::l3_t &r) {
    ++records;
    mda::msg::Data d(true);
    d.l3 = r;
    dispatch(&d);
  });
  KrakenBookSync s("BTC/USD", 1000, &m, 1000);
  FrameReader in("data/btcusd_book1000_trade.txt");
  ASSERT_TRUE(in.ok());
  uint64_t t;
  std::string f;
  while (in.next(t, f))
    ASSERT_EQ(s.on_message(parse_raw(f)), KrakenBookSync::Result::Ok);
  EXPECT_GT(records, 10000u);

  auto qat = [&](bool bid, int64_t px) {
    probe.clear();
    cons::msg::Get g("qat", {{"side", bid ? "B" : "S"}, {"px", std::to_string(px)}});
    dispatch(&g);
    auto page = probe.get_message<cons::msg::Page>(0);
    int sz = -1, cnt = -1, simq = -1;
    if (page)
      std::sscanf(page->val.c_str(), "QAT: %d %d %d", &sz, &cnt, &simq);
    return sz;
  };
  size_t checked = 0;
  for (bool bid : {true, false})
    for (auto [px, qty] : s.top(bid, 50))
    {
      const int64_t tk = m.ticks(fixed_to_double(px));
      EXPECT_EQ(qat(bid, tk), int(m.chunks(bid, tk))) << (bid ? "bid " : "ask ") << tk;
      ++checked;
    }
  EXPECT_EQ(checked, 100u);
  probe.clear();
  cons::msg::Get g("bbbo");
  dispatch(&g);
  auto page = probe.get_message<cons::msg::Page>(0);
  ASSERT_NE(page, nullptr);
  int b = -1, as = -1;
  std::sscanf(page->val.c_str(), "BBBO: %d %d", &b, &as);
  EXPECT_EQ(b, int(m.best(true)));
  EXPECT_EQ(as, int(m.best(false)));
}
