/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/**
 * OB — book reconstruction and the no-cross invariant.
 *
 * The simulator's only output is a slippage number, and that number is only
 * worth anything if the book it was measured against is the book CME actually
 * published. So the reconstruction itself needs pinning, and above all the
 * invariant that took the longest to get right:
 *
 *   CME never publishes a crossed book in continuous trading. If ours crosses,
 *   it is our bug, and the run must die rather than produce plausible fills
 *   against a corrupt ladder.
 *
 * The subtlety, and the reason the first three attempts at this were wrong, is
 * WHEN to check. CME sends a sweep as one transaction: the aggressing order
 * first, then the deletes of everything it consumed, every record sharing a
 * transactTime and carrying eoe=0. Between the first and last of those the book
 * is crossed BY CONSTRUCTION. Checking per-record aborts on healthy data;
 * checking only at end-of-transaction is correct, which is why OB defers the
 * check to the first record of the NEXT transaction (`xcheck_tx`).
 *
 * Locked (bid == ask) is legal and must NOT abort — CME quotes locked markets
 * during pre-open and around the open.
 *
 * The death tests here are the point: an invariant that merely logs is an
 * invariant that gets ignored, and this one exists specifically to stop a run.
 */

#include <gtest/gtest.h>

#include <boost/property_tree/ptree.hpp>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <utility>

#include "bfile/r_l3.hpp"
#include "enum/e_names.hpp"
#include "frame/cons/msg/Get.hpp"
#include "frame/cons/msg/Page.hpp"
#include "frame/mda/msg/Data.hpp"
#include "frame/ob/act/OB.hpp"
#include "frame/ref/RefData.hpp"
#include "unit_test/MockActor.hpp"
#include "unit_test/TestHelper.hpp"

using namespace frame;
using namespace unit_test;

namespace {

struct TestableOB : public ob::act::OB {
  using ob::act::OB::OB;
  using ob::act::OB::reply_to;
};

constexpr uint64_t kT0 = 1736960000000000000ULL;   // epoch ns

class OBBookTest : public ::testing::Test {
protected:
  MockActor probe{"probe"};
  boost::property_tree::ptree pt;
  std::unique_ptr<TestableOB> ob;
  int sym = 0;
  uint64_t next_oid = 1000;

  static void SetUpTestSuite() {
    const char* proj_root = std::getenv("KSPRPROJ");
    std::string universe_path = proj_root
        ? std::string(proj_root) + "/unit_test/config/universe.csv"
        : "../config/universe.csv";
    ref::RefData::set_universe(universe_path, "");
  }

  void SetUp() override {
    auto asset = ref::RefData::get_asset("ESH6");
    ASSERT_NE(asset, nullptr);
    sym = asset->id;
    ob = std::make_unique<TestableOB>(nullptr, true /*do_cross_check*/,
                                      nullptr /*manager*/, sym, pt);
    probe.clear();
  }

  void dispatch(const actors::Message* m) {
    ob->reply_to = &probe;
    TestHelper::invoke_handler(ob.get(), m, &probe);
    ob->reply_to = nullptr;
  }

  // One exchange MBO record. `action`: 0 ADD, 1 CHANGE, 2 DELETE.
  // `eoe` false marks a record that is not the last of its packet, which is
  // how CME flags the middle of a sweep.
  uint64_t mbo(int action, en::bs side, double px, uint32_t qty, uint64_t txtim,
               uint64_t oid = 0, bool eoe = true) {
    if (oid == 0) oid = next_oid++;

    bfile::l3_mbo_v2_t r{};
    r.typ               = en::l3::MBO_V2;
    r.venue             = char(en::x::CMEMD);
    r.transactTime      = txtim;
    r.sendingTime       = txtim;
    r.orderUpdateAction = uint8_t(action);
    r.orderID           = oid;
    r.priority          = oid;
    r.pxd               = px;
    r.displayQty        = qty;
    r.side              = (side == en::bs::BUY) ? '0' : '1';
    r.endOfEvent        = eoe;

    const auto mev = (action == 0) ? en::md::ADD
                   : (action == 2) ? en::md::DEL : en::md::MOD;

    auto d = std::make_unique<mda::msg::Data>();
    d->israw   = true;
    d->l3      = r;
    d->payload = mda::msg::data_pay_load::make_payload(
        txtim, side, oid, mda::OrderID::longid(en::x::CMEMD, oid), sym,
        mev, en::mt::NONE, qty, qty, px, txtim, txtim,
        en::x::CMEMD, eoe, false);
    dispatch(d.get());
    return oid;
  }

  uint64_t add(en::bs side, int px, uint32_t qty, uint64_t txtim, bool eoe = true) {
    return mbo(0, side, double(px), qty, txtim, 0, eoe);
  }
  void del(en::bs side, int px, uint32_t qty, uint64_t txtim, uint64_t oid,
           bool eoe = true) {
    mbo(2, side, double(px), qty, txtim, oid, eoe);
  }

  // A channel-wide security-status record, shaped as CME sends it: no
  // securityID (INT32_MAX is the SBE null), so BFA broadcasts it to every book.
  void status(uint8_t trading_status, uint8_t halt_reason = 0) {
    bfile::l3_sst_t r{};
    r.typ           = en::l3::SST;
    r.venue         = char(en::x::CMEMD);
    r.txtim         = kT0;
    r.sendtim       = kT0;
    r.securityID    = std::numeric_limits<int32_t>::max();
    r.tradingStatus = trading_status;
    r.haltReason    = halt_reason;
    r.tradingEvent  = 0;

    auto d = std::make_unique<mda::msg::Data>();
    d->israw = true;
    d->l3    = r;
    dispatch(d.get());
  }

  std::pair<int, int> bbo() {
    probe.clear();
    cons::msg::Get g("bbbo");
    dispatch(&g);
    auto page = probe.get_message<cons::msg::Page>(0);
    if (!page) return {-1, -1};
    int bid = -1, ask = -1;
    std::sscanf(page->val.c_str(), "BBBO: %d %d", &bid, &ask);
    return {bid, ask};
  }

  // real size, real order count, our own size at one level
  struct Level { int sz = -1, cnt = -1, sim = -1; };
  Level level(en::bs side, int px) {
    probe.clear();
    cons::msg::Get g("qat", {{"side", side == en::bs::BUY ? "B" : "S"},
                             {"px", std::to_string(px)}});
    dispatch(&g);
    auto page = probe.get_message<cons::msg::Page>(0);
    Level l;
    if (page) std::sscanf(page->val.c_str(), "QAT: %d %d %d", &l.sz, &l.cnt, &l.sim);
    return l;
  }
};

// ---------------------------------------------------------------------------
// Reconstruction
// ---------------------------------------------------------------------------

TEST_F(OBBookTest, AddsBuildBothSidesOfTheTouch) {
  add(en::bs::BUY, 100, 7, kT0);
  add(en::bs::SEL, 104, 9, kT0 + 1);

  EXPECT_EQ(bbo(), std::make_pair(100, 104));
  EXPECT_EQ(level(en::bs::BUY, 100).sz, 7);
  EXPECT_EQ(level(en::bs::SEL, 104).sz, 9);
}

TEST_F(OBBookTest, OrdersAtOnePriceAggregate) {
  add(en::bs::BUY, 100, 5, kT0);
  add(en::bs::BUY, 100, 3, kT0 + 1);
  add(en::bs::BUY, 100, 2, kT0 + 2);

  auto l = level(en::bs::BUY, 100);
  EXPECT_EQ(l.sz, 10) << "size at a level is the sum of the resting orders";
  EXPECT_EQ(l.cnt, 3) << "and the count is how many there are";
}

TEST_F(OBBookTest, ABetterBidMovesTheTouch) {
  add(en::bs::BUY, 100, 5, kT0);
  add(en::bs::SEL, 104, 5, kT0 + 1);
  ASSERT_EQ(bbo().first, 100);

  add(en::bs::BUY, 102, 5, kT0 + 2);
  EXPECT_EQ(bbo(), std::make_pair(102, 104));
}

TEST_F(OBBookTest, DeletingTheTouchFallsBackToTheNextLevel) {
  add(en::bs::BUY, 100, 5, kT0);
  const auto top = add(en::bs::BUY, 102, 5, kT0 + 1);
  add(en::bs::SEL, 104, 5, kT0 + 2);
  ASSERT_EQ(bbo().first, 102);

  del(en::bs::BUY, 102, 5, kT0 + 3, top);

  EXPECT_EQ(bbo().first, 100) << "the bid should drop to the next real level";
  EXPECT_EQ(level(en::bs::BUY, 102).sz, 0);
  EXPECT_EQ(level(en::bs::BUY, 100).sz, 5);
}

TEST_F(OBBookTest, DeletingOneOfSeveralLeavesTheRest) {
  add(en::bs::BUY, 100, 5, kT0);
  const auto second = add(en::bs::BUY, 100, 3, kT0 + 1);
  ASSERT_EQ(level(en::bs::BUY, 100).cnt, 2);

  del(en::bs::BUY, 100, 3, kT0 + 2, second);

  auto l = level(en::bs::BUY, 100);
  EXPECT_EQ(l.sz, 5);
  EXPECT_EQ(l.cnt, 1);
}

// ---------------------------------------------------------------------------
// The no-cross invariant
// ---------------------------------------------------------------------------

// Locked is legal. CME quotes locked markets in pre-open and around the open,
// and an earlier version of this check aborted on them.
TEST_F(OBBookTest, ALockedBookIsAllowed) {
  add(en::bs::BUY, 100, 5, kT0);
  add(en::bs::SEL, 100, 5, kT0 + 1000);
  add(en::bs::SEL, 101, 5, kT0 + 2000);   // a later transaction: the check runs

  EXPECT_EQ(bbo(), std::make_pair(100, 100))
      << "bid == ask must survive the end-of-transaction check";
}

// A sweep: aggressor first, then the deletes of the liquidity it consumed, all
// sharing one transactTime. The book is crossed in the middle of it and must
// not abort -- this is the case that made per-record checking wrong.
TEST_F(OBBookTest, AnIntraTransactionCrossDuringASweepIsAllowed) {
  add(en::bs::BUY, 100, 5, kT0);
  const auto a1 = add(en::bs::SEL, 102, 5, kT0 + 1000);
  const auto a2 = add(en::bs::SEL, 103, 5, kT0 + 2000);
  ASSERT_EQ(bbo(), std::make_pair(100, 102));

  // One transaction, eoe=0 throughout: a buy through 103, then both asks go.
  const uint64_t tx = kT0 + 3000;
  add(en::bs::BUY, 103, 10, tx, false);
  del(en::bs::SEL, 102, 5, tx, a1, false);
  del(en::bs::SEL, 103, 5, tx, a2, false);

  // Next transaction: the check fires here, and the book is healthy by now.
  add(en::bs::SEL, 105, 5, kT0 + 4000);

  EXPECT_EQ(bbo(), std::make_pair(103, 105));
}

// A genuine inversion that survives to end-of-transaction must kill the run.
// An invariant that only logs is one that gets ignored, and this one exists to
// stop a replay producing plausible fills against a corrupt ladder.
TEST_F(OBBookTest, AGenuineCrossAbortsTheRun) {
  EXPECT_DEATH(
      {
        add(en::bs::BUY, 100, 5, kT0);
        add(en::bs::SEL, 102, 5, kT0 + 1000);
        // A bid above the ask, left standing: no deletes follow it.
        add(en::bs::BUY, 104, 5, kT0 + 2000);
        // First record of the next transaction -> the check runs.
        add(en::bs::SEL, 106, 5, kT0 + 3000);
      },
      "CROSSED book");
}

// The check is deferred to the next transaction, so a cross that is still open
// when the data ends is simply never reported. Worth pinning so nobody
// "fixes" it into a per-record check again.
TEST_F(OBBookTest, ACrossIsNotReportedUntilTheNextTransactionArrives) {
  add(en::bs::BUY, 100, 5, kT0);
  add(en::bs::SEL, 102, 5, kT0 + 1000);
  add(en::bs::BUY, 104, 5, kT0 + 2000);   // crossed, and nothing follows

  EXPECT_EQ(bbo(), std::make_pair(104, 102))
      << "the book is crossed but no further transaction has arrived";
}

// ---------------------------------------------------------------------------
// The invariant only holds while the exchange is matching
// ---------------------------------------------------------------------------

// 2025-01-15, the CPI release: CME fired a Velocity Logic event and put the
// channel into PreOpen (tradingStatus 21, haltReason 2 MarketEvent) at
// 13:30:01.217639991 -- the exact nanosecond an aggressive bid rested 18 ticks
// through the ask stack -- then reopened (15 -> 17) five seconds later, at the
// exact nanosecond those orders were cleared. Orders rest and cancel during a
// reserve but do not match, so the crossed book was the exchange's own correct
// state. The capture is complete across it: no sequence gaps, and every order
// involved has exactly its NEW and its DELETE.
TEST_F(OBBookTest, ACrossWhileHaltedIsAllowed) {
  status(21, 2);   // PreOpen on a market event

  add(en::bs::BUY, 100, 5, kT0);
  add(en::bs::SEL, 102, 5, kT0 + 1000);
  add(en::bs::BUY, 104, 5, kT0 + 2000);   // rests through the ask: no matching
  add(en::bs::SEL, 106, 5, kT0 + 3000);   // next transaction -> check would run

  EXPECT_EQ(bbo(), std::make_pair(104, 102))
      << "a crossed book is legitimate while the instrument is not matching";
}

TEST_F(OBBookTest, ACrossAfterTheMarketReopensStillAborts) {
  EXPECT_DEATH(
      {
        status(21, 2);                          // halted
        add(en::bs::BUY, 100, 5, kT0);
        add(en::bs::SEL, 102, 5, kT0 + 1000);
        status(17);                             // ReadyToTrade: matching again
        add(en::bs::BUY, 104, 5, kT0 + 2000);
        add(en::bs::SEL, 106, 5, kT0 + 3000);
      },
      "CROSSED book");
}

// Only ReadyToTrade means matching. Everything else -- halt, close, pre-open,
// the price-indication phase of an open -- accepts orders without crossing
// them off.
TEST_F(OBBookTest, OnlyReadyToTradeCountsAsMatching) {
  for (uint8_t st : {uint8_t(2), uint8_t(4), uint8_t(15), uint8_t(18),
                     uint8_t(21), uint8_t(24), uint8_t(25), uint8_t(26)}) {
    SetUp();                     // a fresh book per status
    status(st);
    add(en::bs::BUY, 100, 5, kT0);
    add(en::bs::SEL, 102, 5, kT0 + 1000);
    add(en::bs::BUY, 104, 5, kT0 + 2000);
    add(en::bs::SEL, 106, 5, kT0 + 3000);
    EXPECT_EQ(bbo().first, 104) << "status " << int(st) << " must not match";
  }
}

// Silence is not permission: a session that never carries a status record has
// to keep enforcing the invariant, because catching our own reconstruction
// bugs is the entire point of it.
TEST_F(OBBookTest, WithNoStatusMessageTheInvariantIsStillEnforced) {
  EXPECT_DEATH(
      {
        add(en::bs::BUY, 100, 5, kT0);
        add(en::bs::SEL, 102, 5, kT0 + 1000);
        add(en::bs::BUY, 104, 5, kT0 + 2000);
        add(en::bs::SEL, 106, 5, kT0 + 3000);
      },
      "CROSSED book");
}

// ---------------------------------------------------------------------------
// The bad-price guard
// ---------------------------------------------------------------------------

// 2025-01-30, 1 record in 11,673,971: an order at 610600 was CHANGEd to
// 610608800 -- the two values run together, a decode fault in the source. That
// indexes about 1000x past the end of the ladder and the process died on a
// signal rather than an assert. One corrupt print must not cost a session.
TEST_F(OBBookTest, AnImpossiblePriceIsDroppedAndTheSessionContinues) {
  add(en::bs::BUY, 100, 5, kT0);
  add(en::bs::SEL, 104, 5, kT0 + 1000);
  ASSERT_EQ(bbo(), std::make_pair(100, 104));

  add(en::bs::BUY, 610608800, 5, kT0 + 2000);

  EXPECT_EQ(bbo(), std::make_pair(100, 104))
      << "the corrupt record must leave the book untouched";

  add(en::bs::BUY, 101, 5, kT0 + 3000);
  EXPECT_EQ(bbo().first, 101) << "and the session must carry on";
}

// No companion test for a zero or negative price: data_pay_load::make_payload
// rejects those with SNGH before a payload can even be built, so OB never sees
// one. The guard tested above is OB's own, and it catches the other end -- a
// price past the top of the ladder, which make_payload is happy to construct.

// ---------------------------------------------------------------------------
// The console query the tests above lean on
// ---------------------------------------------------------------------------

// kv comes straight from user-typed console tokens and get_handler is noexcept,
// so a parse failure must return a value, not throw: a throw out of a noexcept
// function terminates the process before the console's own catch can run.
TEST_F(OBBookTest, AMalformedLevelQueryDoesNotKillTheProcess) {
  add(en::bs::BUY, 100, 5, kT0);

  for (const char* bad : {"x", "", "99999999999999999999", "-1", "12abc"})
  {
    probe.clear();
    cons::msg::Get g("qat", {{"side", "B"}, {"px", bad}});
    EXPECT_NO_THROW(dispatch(&g));
    auto page = probe.get_message<cons::msg::Page>(0);
    ASSERT_NE(page, nullptr) << "should still reply, for px=\"" << bad << "\"";
    EXPECT_NE(page->val.find("QAT: -1"), std::string::npos)
        << "expected the not-found reply for px=\"" << bad << "\"";
  }
}

TEST_F(OBBookTest, AnUnknownSideIsRejectedRatherThanGuessed) {
  add(en::bs::BUY, 100, 5, kT0);
  probe.clear();
  cons::msg::Get g("qat", {{"side", "X"}, {"px", "100"}});
  dispatch(&g);
  auto page = probe.get_message<cons::msg::Page>(0);
  ASSERT_NE(page, nullptr);
  EXPECT_NE(page->val.find("QAT: -1"), std::string::npos)
      << "a side that is neither B nor S must not be read as the ask";
}

}  // namespace
