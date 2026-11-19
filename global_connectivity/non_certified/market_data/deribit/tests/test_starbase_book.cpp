/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// StarbaseBook: synchronisation, recovery and the mapping to Kaspar records,
// from packets built with the generated encoders.

#include <gtest/gtest.h>

#include <map>

#include "deribit_md/StarbaseBook.hpp"
#include "enum/e_names.hpp"

using namespace deribit::starbase;

namespace
{
  constexpr int32_t CH = 44849;
  constexpr int64_t INST = 210838;     // BTC-PERPETUAL's instrument id
  constexpr int64_t TICK = 500000000;  // 0.5 at exponent -9
  constexpr int64_t px(double p) { return int64_t(p * 1e9); }

  struct Out
  {
    int32_t sec;
    bfile::l3_t r;
  };

  class BookTest : public ::testing::Test
  {
  protected:
    std::vector<Out> out;
    StarbaseBook book{StarbaseBook::Config{
        char(en::x::DERIBIT),
        [](const InstrumentDefinition &d) { return d.type == 0 ? 7 : 0; },
        [this](int32_t sec, const bfile::l3_t &r) { out.push_back({sec, r}); }}};
    int64_t inc_seq = 100, snap_seq = 1;
    int64_t tx = 1777450683000000000;

    static Message msg(Body b, uint16_t flags = 3, int64_t t = 0)
    {
      Message m;
      m.flags = flags;
      m.transactTime = t;
      m.body = std::move(b);
      return m;
    }
    void inc(std::vector<Message> ms)
    {
      PacketHeader ph{tx, inc_seq, CH, INCREMENTAL, uint16_t(ms.size())};
      for (size_t i = 0; i < ms.size(); ++i)
      {
        ms[i].seq = inc_seq + int64_t(i);
        if (!ms[i].transactTime)
          ms[i].transactTime = tx;
      }
      inc_seq += int64_t(ms.size());
      tx += 1000;
      book.on_packet(ph, ms);
    }
    void heartbeat(int64_t seq)
    {
      book.on_packet(PacketHeader{tx, seq, CH, INCREMENTAL, 0}, {});
    }
    InstrumentDefinition def(int8_t status = 0)
    {
      InstrumentDefinition d;
      d.instrumentId = INST;
      d.name = "BTC-PERPETUAL";
      d.tickSize9 = TICK;
      d.minOrderQuantity = 10;
      d.type = 0;
      d.status = status;
      return d;
    }
    // A snapshot of the instrument at incremental sequence `at`.
    void snapshot(int64_t at, std::vector<Put> puts)
    {
      std::vector<Message> ms;
      ms.push_back(msg(SnapshotBound{true, INST, tx, at}, 0));
      ms.push_back(msg(def(), 0));
      for (auto &p : puts)
        ms.push_back(msg(p, 0));
      ms.push_back(msg(SnapshotBound{false, INST, tx, at}, 0));
      for (auto &m : ms)
      {
        m.seq = snap_seq++;
        m.transactTime = tx;
      }
      book.on_packet(PacketHeader{tx, ms.front().seq, CH, SNAPSHOT, uint16_t(ms.size())}, ms);
    }
    static Put bid(int64_t id, double p, int64_t q, int64_t so) { return Put{true, id, INST, q, px(p), so}; }
    static Put ask(int64_t id, double p, int64_t q, int64_t so) { return Put{false, id, INST, q, px(p), so}; }

    template <class T>
    std::vector<T> recs()
    {
      std::vector<T> v;
      for (const auto &o : out)
        if (auto p = std::get_if<T>(&o.r))
          v.push_back(*p);
      return v;
    }
    // MBO records only, as "action:orderID" strings, in order.
    std::vector<std::string> mbo_seq()
    {
      std::vector<std::string> v;
      for (const auto &o : out)
      {
        if (auto m = std::get_if<bfile::l3_mbo_v2_t>(&o.r))
          v.push_back(std::to_string(m->orderUpdateAction) + ":" + std::to_string(m->orderID) +
                      (m->endOfEvent ? "e" : ""));
        else if (auto x = std::get_if<bfile::l3_mbo_trd_v2_t>(&o.r))
          v.push_back("x:" + std::to_string(x->orderID) + "/" + std::to_string(x->lastQty) +
                      (x->endOfEvent ? "e" : ""));
      }
      return v;
    }
    const Instrument &in() { return *book.instrument(INST); }
  };
}

// Join the incremental channel, buffer, then the snapshot at seq 101: the
// buffered message at 101 is already in it, the one at 102 is applied.
TEST_F(BookTest, SnapshotAlignsWithBufferedIncrementals)
{
  heartbeat(100);
  inc({msg(bid(1, 63000, 100, 11))});   // seq 100
  inc({msg(bid(2, 63000, 50, 13))});    // seq 101
  inc({msg(ask(3, 63000.5, 20, 15))});  // seq 102
  EXPECT_FALSE(in().synced);
  EXPECT_EQ(book.stats().buffered, 3u);

  snapshot(101, {bid(1, 63000, 100, 11), bid(2, 63000, 50, 13)});
  ASSERT_TRUE(in().synced);
  EXPECT_EQ(in().orders.size(), 3u);
  EXPECT_EQ(book.stats().dropped_before_sync, 2u);
  // CHR, SST 17, two recovery adds, then the live add of order 3.
  ASSERT_GE(out.size(), 5u);
  EXPECT_TRUE(std::holds_alternative<bfile::l3_chr_v2_t>(out[0].r));
  EXPECT_EQ(std::get<bfile::l3_sst_t>(out[1].r).tradingStatus, 17);
  auto mbos = recs<bfile::l3_mbo_v2_t>();
  ASSERT_EQ(mbos.size(), 3u);
  EXPECT_TRUE(mbos[0].recovery && mbos[1].recovery && !mbos[2].recovery);
  EXPECT_EQ(mbos[0].pxd, 126000.0);       // 63000 / 0.5
  EXPECT_EQ(mbos[0].displayQty, 10u);     // 100 / minOrderQuantity 10
  EXPECT_EQ(mbos[0].priority, 11u);       // sortOrderId
  EXPECT_TRUE(mbos[1].endOfEvent);
  EXPECT_EQ(mbos[2].orderID, 3u);
}

// Recovery adds go to OB in sortOrderId order, whatever order the snapshot
// sent them in (feed.md 10: the sample capture sends 31 levels out of order).
TEST_F(BookTest, RecoveryAddsFollowSortOrderId)
{
  heartbeat(100);
  snapshot(99, {bid(5, 63000, 10, 40), bid(6, 63000, 10, 20), bid(7, 63000, 10, 30)});
  EXPECT_EQ(book.stats().snap_priority_disorder, 1u);   // 20 after 40
  auto m = mbo_seq();
  ASSERT_EQ(m.size(), 3u);
  EXPECT_EQ(m[0], "0:6");
  EXPECT_EQ(m[1], "0:7");
  EXPECT_EQ(m[2], "0:5e");
}

TEST_F(BookTest, PutWithANewSortOrderIdIsDeleteThenAdd)
{
  heartbeat(100);
  snapshot(99, {bid(1, 63000, 100, 11), bid(2, 63000, 100, 13)});
  out.clear();
  // Order 1 amended to a new price: a new sortOrderId (as every re-Put in the
  // sample capture).
  inc({msg(bid(1, 62999.5, 100, 17))});
  EXPECT_EQ(book.stats().put_new_sort, 1u);
  auto m = mbo_seq();
  ASSERT_EQ(m.size(), 2u);
  EXPECT_EQ(m[0], "2:1");
  EXPECT_EQ(m[1], "0:1e");
  auto mbos = recs<bfile::l3_mbo_v2_t>();
  EXPECT_EQ(mbos[0].transactTime, mbos[1].transactTime);
  EXPECT_EQ(mbos[0].pxd, 126000.0);
  EXPECT_EQ(mbos[1].pxd, 125999.0);
}

TEST_F(BookTest, PutWithTheSameSortOrderIdIsAChange)
{
  heartbeat(100);
  snapshot(99, {bid(1, 63000, 100, 11)});
  out.clear();
  inc({msg(bid(1, 63000, 60, 11))});   // a partial execution re-Puts the order
  EXPECT_EQ(book.stats().put_smaller, 1u);
  EXPECT_EQ(mbo_seq(), std::vector<std::string>{"1:1e"});
  EXPECT_EQ(recs<bfile::l3_mbo_v2_t>()[0].displayQty, 6u);
}

TEST_F(BookTest, ReduceAndDelete)
{
  heartbeat(100);
  snapshot(99, {ask(4, 63001, 100, 11)});
  out.clear();
  inc({msg(QtyReduced{false, 4, INST, 40})});
  inc({msg(Delete{false, 4, INST})});
  EXPECT_EQ(mbo_seq(), (std::vector<std::string>{"1:4e", "2:4e"}));
  EXPECT_TRUE(in().orders.empty());
  EXPECT_EQ(book.stats().unknown_ref, 0u);
  inc({msg(Delete{false, 4, INST})});
  inc({msg(QtyReduced{true, 99, INST, 1})});
  EXPECT_EQ(book.stats().unknown_ref, 2u);
}

// Trade after the maker's book change in the transaction: the execution is
// moved ahead of the change. The print carries the aggressor side.
TEST_F(BookTest, ExecutionPrecedesTheMakersBookChange)
{
  heartbeat(100);
  snapshot(99, {bid(1, 63000, 100, 11), bid(2, 63000, 100, 13)});
  out.clear();
  inc({msg(Delete{true, 1, INST}, kStartOfTransaction),
       msg(bid(2, 63000, 70, 13), 0),
       msg(TradeSummary{INST, 77, 130, px(63000), 0, 0, 2, 1}, 0),
       msg(Trade{1, INST, 1, 100, px(63000), 0}, 0),
       msg(Trade{2, INST, 2, 30, px(63000), 0}, kEndOfTransaction)});
  EXPECT_EQ(mbo_seq(), (std::vector<std::string>{"x:1/10", "2:1", "x:2/3", "1:2e"}));
  auto prints = recs<bfile::l3_mbp_trd_t>();
  ASSERT_EQ(prints.size(), 2u);
  EXPECT_EQ(prints[0].aggresorSide, 2);   // takerFlags.isSell
  EXPECT_EQ(prints[0].sz, 10);
  EXPECT_EQ(prints[1].pxd, 126000.0);
  EXPECT_EQ(book.stats().fill_consistent, 2u);
  EXPECT_EQ(book.stats().fill_inconsistent, 0u);
  // One transactTime for the whole transaction; endOfEvent on its last record.
  uint64_t t = 0;
  for (const auto &o : out)
    if (auto m = std::get_if<bfile::l3_mbo_v2_t>(&o.r))
    {
      if (!t)
        t = m->transactTime;
      EXPECT_EQ(m->transactTime, t);
    }
}

// Trade before the book change: the order Kaspar needs already.
TEST_F(BookTest, ExecutionBeforeTheBookChange)
{
  heartbeat(100);
  snapshot(99, {ask(1, 63000.5, 100, 11)});
  out.clear();
  inc({msg(TradeSummary{INST, 77, 40, px(63000.5), 0, 0, 1, 0}, kStartOfTransaction),
       msg(Trade{1, INST, 1, 40, px(63000.5), 0}, 0),
       msg(ask(1, 63000.5, 60, 11), kEndOfTransaction)});
  EXPECT_EQ(mbo_seq(), (std::vector<std::string>{"x:1/4", "1:1e"}));
  EXPECT_EQ(recs<bfile::l3_mbp_trd_t>()[0].aggresorSide, 1);   // buyer was the taker
  EXPECT_EQ(book.stats().fill_consistent, 1u);
}

TEST_F(BookTest, InconsistentFillIsCounted)
{
  heartbeat(100);
  snapshot(99, {ask(1, 63000.5, 100, 11)});
  inc({msg(Trade{1, INST, 1, 40, px(63000.5), 0}, kStartOfTransaction),
       msg(ask(1, 63000.5, 70, 11), kEndOfTransaction)});
  EXPECT_EQ(book.stats().fill_inconsistent, 1u);
}

TEST_F(BookTest, ImpliedFillIsAPrintOnly)
{
  heartbeat(100);
  snapshot(99, {});
  out.clear();
  inc({msg(Trade{1, INST, kNullInt64, 10, px(63000), 0})});
  EXPECT_EQ(book.stats().implied_fills, 1u);
  EXPECT_EQ(book.stats().unknown_ref, 0u);
  EXPECT_TRUE(recs<bfile::l3_mbo_trd_v2_t>().empty());
  EXPECT_EQ(recs<bfile::l3_mbp_trd_t>().size(), 1u);
}

// A gap unsynchronises the channel's instruments (gap record to their books);
// only a snapshot at or past the last missing number resynchronises.
TEST_F(BookTest, GapThenResyncFromALaterSnapshot)
{
  heartbeat(100);
  snapshot(99, {bid(1, 63000, 100, 11)});
  inc({msg(bid(2, 63000, 10, 13))});   // 100
  inc_seq = 105;                        // 101..104 lost
  inc({msg(bid(3, 63000, 10, 15))});   // 105, buffered
  EXPECT_EQ(book.stats().gaps, 1u);
  EXPECT_EQ(book.stats().missing, 4u);
  EXPECT_FALSE(in().synced);
  EXPECT_EQ(recs<bfile::l3_gap_v2_t>().size(), 1u);

  snapshot(103, {bid(1, 63000, 100, 11)});   // before the end of the gap: unusable
  EXPECT_FALSE(in().synced);
  snapshot(104, {bid(1, 63000, 100, 11), bid(2, 63000, 10, 13)});
  ASSERT_TRUE(in().synced);
  EXPECT_EQ(in().orders.size(), 3u);   // 105 replayed from the buffer
  EXPECT_EQ(book.stats().resyncs, 1u);
}

TEST_F(BookTest, HeartbeatRevealsAGap)
{
  heartbeat(100);
  snapshot(99, {});
  heartbeat(103);
  EXPECT_EQ(book.stats().gaps, 1u);
  EXPECT_EQ(book.stats().missing, 3u);
}

TEST_F(BookTest, DuplicatesAreDropped)
{
  heartbeat(100);
  snapshot(99, {});
  inc({msg(bid(1, 63000, 10, 11))});
  inc_seq = 100;
  inc({msg(bid(1, 63000, 10, 11))});
  EXPECT_EQ(book.stats().duplicates, 1u);
  EXPECT_EQ(in().orders.size(), 1u);
}

// A later snapshot is compared with the rebuilt book, order by order.
TEST_F(BookTest, LaterSnapshotIsComparedWithTheRebuiltBook)
{
  heartbeat(100);
  snapshot(99, {bid(1, 63000, 100, 11)});
  inc({msg(QtyReduced{true, 1, INST, 50})});   // 100
  snapshot(100, {bid(1, 63000, 50, 11)});
  EXPECT_EQ(book.stats().snap_compared, 1u);
  EXPECT_EQ(book.stats().snap_compared_changed, 1u);
  EXPECT_EQ(book.stats().snap_match, 1u);
  snapshot(100, {bid(1, 63000, 40, 11)});
  EXPECT_EQ(book.stats().snap_mismatch, 1u);
}

TEST_F(BookTest, InvariantsAreCounted)
{
  heartbeat(100);
  snapshot(99, {bid(1, 63000, 100, 11)});
  inc({msg(bid(2, 63000, 100, 9))});        // joins ahead of 11
  inc({msg(QtyReduced{true, 1, INST, 200})}); // not a reduction
  inc({msg(bid(3, 63000, 15, 21))});        // off the 10-unit step
  inc({msg(ask(4, 62999.5, 10, 23))});      // crosses the bid
  const auto &s = book.stats();
  EXPECT_EQ(s.joined_ahead, 1u);
  EXPECT_EQ(s.reduce_not_smaller, 1u);
  EXPECT_EQ(s.qty_off_step, 1u);
  EXPECT_EQ(s.crossed, 1u);
}

TEST_F(BookTest, StatusUpdatesMapToCmeStatus)
{
  heartbeat(100);
  snapshot(99, {});
  out.clear();
  inc({msg(InstrumentStatusUpdate{INST, 5})});   // HALTED
  inc({msg(InstrumentStatusUpdate{INST, 0})});   // OPEN
  auto s = recs<bfile::l3_sst_t>();
  ASSERT_EQ(s.size(), 2u);
  EXPECT_EQ(s[0].tradingStatus, 2);
  EXPECT_EQ(s[1].tradingStatus, 17);
  EXPECT_EQ(trading_status(3), 4);
  EXPECT_EQ(trading_status(1), 18);
}

// End of packet: l3_eop_t with the packet's last sequence number, then
// l3_eob_t, to each book the packet touched.
TEST_F(BookTest, EndOfPacketRecords)
{
  heartbeat(100);
  snapshot(99, {});
  out.clear();
  inc({msg(bid(1, 63000, 10, 11)), msg(bid(2, 63000, 10, 13))});
  auto e = recs<bfile::l3_eop_t>();
  ASSERT_EQ(e.size(), 1u);
  EXPECT_EQ(e[0].msgSeqNum, 101u);
  EXPECT_EQ(recs<bfile::l3_eob_t>().size(), 1u);
  EXPECT_TRUE(std::holds_alternative<bfile::l3_eob_t>(out.back().r));
}

// Instruments the route does not send to an OB keep their book but emit nothing.
TEST_F(BookTest, UnroutedInstrumentEmitsNothing)
{
  heartbeat(100);
  std::vector<Message> ms;
  InstrumentDefinition d = def();
  d.instrumentId = 999;
  d.type = 3;   // FUTURE_COMBO
  ms.push_back(msg(SnapshotBound{true, 999, tx, 99}, 0));
  ms.push_back(msg(d, 0));
  ms.push_back(msg(Put{true, 1, 999, 10, px(-5), 1}, 0));
  ms.push_back(msg(SnapshotBound{false, 999, tx, 99}, 0));
  for (auto &m : ms)
    m.seq = snap_seq++;
  book.on_packet(PacketHeader{tx, ms.front().seq, CH, SNAPSHOT, 4}, ms);
  ASSERT_TRUE(book.instrument(999)->synced);
  EXPECT_EQ(book.instrument(999)->orders.size(), 1u);
  EXPECT_TRUE(out.empty());
}
