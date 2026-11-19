/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// The records each Deribit path emits, into a real OB:
//   - StarbaseBook (order by order): OB's levels equal the book's, a priority
//     reset sends the order to the back, and a simulated order queued behind a
//     resting order fills only once the executions reach it;
//   - DeribitBookSync -> Mbp2Mbo (JSON levels) on the recorded live frames:
//     OB's best bid and offer equal Mbp2Mbo's after every notification.

#include <gtest/gtest.h>

#include <boost/property_tree/ptree.hpp>
#include <cstdio>
#include <cstdlib>
#include <memory>

#include "deribit_md/DeribitBookSync.hpp"
#include "deribit_md/FrameRecorder.hpp"
#include "deribit_md/StarbaseBook.hpp"
#include "enum/e_names.hpp"
#include "frame/cons/msg/Get.hpp"
#include "frame/cons/msg/Page.hpp"
#include "frame/mda/msg/Data.hpp"
#include "frame/ob/act/OB.hpp"
#include "frame/ref/RefData.hpp"
#include "frame/som/msg/Fill.hpp"
#include "sim/Mbp2Mbo.hpp"
#include "unit_test/MockActor.hpp"
#include "unit_test/TestHelper.hpp"

using namespace frame;
using namespace unit_test;
using namespace deribit::starbase;

namespace
{
  struct TestableOB : public ob::act::OB
  {
    using ob::act::OB::OB;
    using ob::act::OB::reply_to;
  };

  constexpr int32_t CH = 44849;
  constexpr int64_t INST = 210838;
  constexpr int64_t px9(double p) { return int64_t(p * 1e9); }

  class ChainTest : public ::testing::Test
  {
  protected:
    MockActor probe{"probe"};
    std::unique_ptr<TestableOB> ob;
    int sym = 0;
    uint64_t now = 1777450683000000000ULL;

    static void SetUpTestSuite()
    {
      const char *k = std::getenv("KSPRPROJ");
      ref::RefData::set_universe(std::string(k ? k : "..") + "/unit_test/config/universe.csv", "");
    }

    void make_ob(const std::string &name, int32_t sec)
    {
      ref::RefData::inst();
      auto a = ref::RefData::add_future_asset(name, en::x::DERIBIT, sec, "", name, 1.0);
      ASSERT_NE(a, nullptr);
      a->maxpx = 1 << 30;
      sym = a->id;
      boost::property_tree::ptree pt;
      pt.put("ladder_ticks", 1 << 16);
      ob = std::make_unique<TestableOB>(nullptr, true, nullptr, sym, pt);
    }

    void dispatch(const actors::Message *msg)
    {
      ob->reply_to = &probe;
      TestHelper::invoke_handler(ob.get(), msg, &probe);
      ob->reply_to = nullptr;
    }
    void feed(const bfile::l3_t &r)
    {
      mda::msg::Data d(true);
      d.l3 = r;
      dispatch(&d);
    }

    struct Level
    {
      int sz = -1, cnt = -1, sim = -1;
    };
    Level qat(bool bid, int64_t t)
    {
      probe.clear();
      cons::msg::Get g("qat", {{"side", bid ? "B" : "S"}, {"px", std::to_string(t)}});
      dispatch(&g);
      auto page = probe.get_message<cons::msg::Page>(0);
      Level l;
      if (page)
        std::sscanf(page->val.c_str(), "QAT: %d %d %d", &l.sz, &l.cnt, &l.sim);
      return l;
    }
    std::pair<int, int> bbo()
    {
      probe.clear();
      cons::msg::Get g("bbbo");
      dispatch(&g);
      auto page = probe.get_message<cons::msg::Page>(0);
      int b = -1, a = -1;
      if (page)
        std::sscanf(page->val.c_str(), "BBBO: %d %d", &b, &a);
      return {b, a};
    }

    // One of our 1-unit buy orders at `t` ticks, then time for OB's delay queue.
    void ours_buy(int t, uint oid)
    {
      auto d = std::make_unique<mda::msg::Data>();
      d->israw = false;
      d->l3 = bfile::l3_sim_t();
      auto p = boost::intrusive_ptr<mda::msg::data_pay_load>(new mda::msg::data_pay_load());
      p->mkt = en::x::SIM;
      p->sym = sym;
      p->px = ref::Price(t, sym);
      p->sz = 1;
      p->disp_sz = 1;
      p->side = en::bs::BUY;
      p->order_ref = mda::OrderID::longid(en::x::SIM, oid);
      p->action = en::mt::NONE;
      p->ot = en::ot::LIMIT;
      p->mev = en::md::ADD;
      p->owner = en::trader::SIMULATOR;
      p->ts0 = now;
      d->payload = p;
      dispatch(d.get());
      now += 2 * 1000ULL * 1000;   // past OB's 1 ms order delay
    }
  };

  class StarbaseChain : public ChainTest
  {
  protected:
    std::unique_ptr<StarbaseBook> book;
    int64_t inc_seq = 100, snap_seq = 1;

    void SetUp() override
    {
      make_ob("BTC-PERPETUAL_SBT", 7101);
      book = std::make_unique<StarbaseBook>(StarbaseBook::Config{
          char(en::x::DERIBIT), [](const InstrumentDefinition &) { return 7101; },
          [this](int32_t, const bfile::l3_t &r) { feed(r); }});
      book->on_packet(PacketHeader{0, inc_seq, CH, INCREMENTAL, 0}, {});
    }
    Message msg(Body b, uint16_t flags = 3)
    {
      Message m;
      m.flags = flags;
      m.transactTime = int64_t(now);
      m.body = std::move(b);
      return m;
    }
    void inc(std::vector<Message> ms)
    {
      for (size_t i = 0; i < ms.size(); ++i)
        ms[i].seq = inc_seq + int64_t(i);
      book->on_packet(PacketHeader{int64_t(now), inc_seq, CH, INCREMENTAL, uint16_t(ms.size())}, ms);
      inc_seq += int64_t(ms.size());
      now += 1000ULL * 1000;
    }
    void snapshot(std::vector<Put> puts)
    {
      InstrumentDefinition d;
      d.instrumentId = INST;
      d.name = "BTC-PERPETUAL";
      d.tickSize9 = 500000000;
      d.minOrderQuantity = 10;
      std::vector<Message> ms{msg(SnapshotBound{true, INST, int64_t(now), inc_seq - 1}, 0), msg(d, 0)};
      for (auto &p : puts)
        ms.push_back(msg(p, 0));
      ms.push_back(msg(SnapshotBound{false, INST, int64_t(now), inc_seq - 1}, 0));
      for (auto &m : ms)
        m.seq = snap_seq++;
      book->on_packet(PacketHeader{int64_t(now), ms.front().seq, CH, SNAPSHOT, uint16_t(ms.size())}, ms);
      now += 1000ULL * 1000;
    }
    static Put bid(int64_t id, double p, int64_t q, int64_t so) { return Put{true, id, INST, q, px9(p), so}; }
    static Put ask(int64_t id, double p, int64_t q, int64_t so) { return Put{false, id, INST, q, px9(p), so}; }

    void expect_levels_equal()
    {
      const auto *in = book->instrument(INST);
      ASSERT_NE(in, nullptr);
      for (int s = 0; s < 2; ++s)
        for (const auto &l : in->levels(s == 0))
        {
          const auto got = qat(s == 0, book->ticks(*in, l.price9));
          EXPECT_EQ(got.sz, int(l.qty / 10)) << (s == 0 ? "bid " : "ask ") << l.price9;
          EXPECT_EQ(got.cnt, int(l.orders));
        }
      const auto bl = in->levels(true), al = in->levels(false);
      if (!bl.empty() && !al.empty())
        EXPECT_EQ(bbo(), std::make_pair(int(book->ticks(*in, bl[0].price9)),
                                        int(book->ticks(*in, al[0].price9))));
    }
  };
}

TEST_F(StarbaseChain, OBHoldsWhatTheBookHolds)
{
  snapshot({bid(1, 63000, 100, 11), bid(2, 63000, 50, 13), bid(3, 62999.5, 30, 15), ask(4, 63000.5, 70, 17)});
  expect_levels_equal();
  inc({msg(QtyReduced{true, 1, INST, 40})});
  inc({msg(bid(3, 63000, 30, 19))});   // amended up a tick: new sortOrderId
  inc({msg(Delete{false, 4, INST})});
  inc({msg(ask(5, 63001, 20, 21))});
  expect_levels_equal();
  EXPECT_EQ(qat(true, 126000).cnt, 3);
}

// Our bid joins behind orders 1 and 2. Executions against 1 and 2 do not fill
// us; an execution against order 3, which joined behind us, does.
TEST_F(StarbaseChain, SimulatedOrderFillsWhenExecutionsReachIt)
{
  snapshot({bid(1, 63000, 10, 11), bid(2, 63000, 10, 13), ask(9, 63005, 10, 15)});
  inc({msg(ask(8, 63006, 10, 16))});   // live data first: OB takes its clock from it
  ours_buy(126000, 1);
  inc({msg(bid(3, 63000, 10, 17))});
  ASSERT_EQ(qat(true, 126000).sim, 1);
  probe.clear();
  for (int64_t maker : {1, 2})
    inc({msg(TradeSummary{INST, 50 + maker, 10, px9(63000), 0, 0, 1, 1}, kStartOfTransaction),
         msg(Trade{maker, INST, maker, 10, px9(63000), 0}, 0),
         msg(Delete{true, maker, INST}, kEndOfTransaction)});
  EXPECT_FALSE(probe.has_message_of_type<som::msg::Fill>()) << "orders 1 and 2 were ahead of ours";
  // Starbase order: book change first, then the Trade; the handler moves the
  // execution ahead of the delete, so OB still finds order 3.
  inc({msg(Delete{true, 3, INST}, kStartOfTransaction),
       msg(TradeSummary{INST, 60, 10, px9(63000), 0, 0, 1, 1}, 0),
       msg(Trade{3, INST, 3, 10, px9(63000), 0}, kEndOfTransaction)});
  EXPECT_TRUE(probe.has_message_of_type<som::msg::Fill>());
  expect_levels_equal();
}

// A priority reset (new sortOrderId) puts the order behind ours.
TEST_F(StarbaseChain, PriorityResetMovesTheOrderBehindOurs)
{
  snapshot({bid(1, 63000, 10, 11), ask(9, 63005, 10, 15)});
  inc({msg(ask(8, 63006, 10, 16))});
  ours_buy(126000, 1);
  inc({msg(bid(1, 63000, 20, 19))});   // size up: re-Put with a new sortOrderId
  probe.clear();
  inc({msg(TradeSummary{INST, 70, 10, px9(63000), 0, 0, 1, 1}, kStartOfTransaction),
       msg(Trade{1, INST, 1, 10, px9(63000), 0}, 0),
       msg(bid(1, 63000, 10, 19), kEndOfTransaction)});
  EXPECT_TRUE(probe.has_message_of_type<som::msg::Fill>()) << "order 1 went to the back, behind ours";
}

// Recorded live JSON frames through Mbp2Mbo into OB. The recording holds two
// subscriptions whose snapshots have no live record between them: without the
// l3_chr_v2_t on resync, OB keeps the first book's levels and crosses.
TEST_F(ChainTest, RecordedJsonFramesIntoOB)
{
  make_ob("BTC-PERPETUAL_JSONT", 7102);
  sim::Mbp2Mbo m(sim::Mbp2Mbo::Config{7102, char(en::x::DERIBIT), 0.5, 1000},
                 [this](const bfile::l3_t &r) { feed(r); });
  deribit::DeribitBookSync s("BTC-PERPETUAL", 0.5, &m, 50);
  s.on_resync([this](uint64_t) {
    bfile::l3_chr_v2_t c{};
    c.typ = en::l3::CHR_V2;
    c.venue = char(en::x::DERIBIT);
    feed(c);
  });
  deribit::FrameReader rd(std::string(DERIBIT_TEST_DATA) + "/btc_perpetual_100ms.frames");
  ASSERT_TRUE(rd.ok());
  uint64_t ns;
  std::string text;
  int checks = 0;
  while (rd.next(ns, text))
  {
    const auto msg = nlohmann::json::parse(text);
    ASSERT_TRUE(s.on_message(msg));
    if (msg.value("method", "") == "subscription" &&
        msg["params"]["channel"].get<std::string>().rfind("book.", 0) == 0)
    {
      EXPECT_EQ(bbo(), std::make_pair(int(m.best(true)), int(m.best(false))));
      EXPECT_EQ(qat(true, m.best(true)).sz, int(m.chunks(true, m.best(true))));
      ++checks;
    }
  }
  EXPECT_EQ(checks, 183);
}
