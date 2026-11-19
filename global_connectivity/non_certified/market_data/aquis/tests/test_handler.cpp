// Aquis handler core: dispatch to Kaspar records, sequencing, replay and
// snapshot recovery, on synthetic packets built from the specification
// (tests/Pkt.hpp). Expected records follow feed.md section 7.
#include <gtest/gtest.h>

#include <map>
#include <random>
#include <set>

#include "Pkt.hpp"
#include "aquis/Handler.hpp"
#include "enum/e_names.hpp"

using namespace aquis_test;
using aquis::Handler;

namespace
{

constexpr uint64_t T0 = 1763456400000000000ull; // 2025-11-18 09:00:00 UTC
constexpr char VENUE = char(en::x::AQXE);

struct Rec
{
  int32_t sec;
  bfile::l3_t r;
};

struct Fixture : ::testing::Test
{
  aquis::Instruments ins;
  std::vector<Rec> out;
  std::vector<std::pair<uint32_t, uint32_t>> requests;
  aquis::HandlerConfig cfg;
  std::unique_ptr<Handler> h;

  void SetUp() override
  {
    ins.add_universe("VODl", 1001);
    ins.add_universe("DPLMl", 1002);
    ins.on_definition(340, "VODl", "GB00BH4HKS39", "GBX", "XLON", 1, 0, "VOD");
    ins.on_definition(341, "DPLMl", "GB0001826634", "GBX", "XLON", 1, 0, "DPLM");
    ins.on_definition(999, "XXXl", "GB0000000000", "GBX", "XLON", 1, 0, "XXX");
    cfg.venue = VENUE;
    make();
  }
  void make()
  {
    h = std::make_unique<Handler>(
        cfg, ins, [this](int32_t s, const bfile::l3_t &r) { out.push_back({s, r}); },
        [this](uint32_t b, uint32_t e) { requests.emplace_back(b, e); });
  }
  void feed(const std::vector<std::string> &msgs)
  {
    auto p = packet(msgs);
    h->on_packet(p.data(), p.size());
  }
  void snap(const std::vector<std::string> &msgs)
  {
    auto p = packet(msgs);
    h->on_snapshot_packet(p.data(), p.size());
  }
  void replay(const std::string &bytes) { h->on_replay_data(bytes.data(), bytes.size()); }

  // Records other than the end-of-packet record.
  std::vector<Rec> book_records() const
  {
    std::vector<Rec> v;
    for (const auto &r : out)
      if (r.sec != Handler::RECORDER)
        v.push_back(r);
    return v;
  }
  template <typename T>
  std::vector<T> all() const
  {
    std::vector<T> v;
    for (const auto &r : out)
      if (auto *x = std::get_if<T>(&r.r))
        v.push_back(*x);
    return v;
  }
};

const bfile::l3_mbo_v2_t &mbo(const Rec &r) { return std::get<bfile::l3_mbo_v2_t>(r.r); }

} // namespace

TEST_F(Fixture, AddModifyCancel)
{
  feed({add(1, 340, 1, 500, 10000000, 77, T0 + 1, T0 + 9)});
  auto v = book_records();
  ASSERT_EQ(v.size(), 1u);
  auto a = mbo(v[0]);
  EXPECT_EQ(v[0].sec, 1001);
  EXPECT_EQ(a.typ, en::l3::MBO_V2);
  EXPECT_EQ(a.venue, VENUE);
  EXPECT_EQ(a.orderUpdateAction, 0);
  EXPECT_EQ(a.securityID, 1001);
  EXPECT_EQ(a.orderID, 77u);
  EXPECT_DOUBLE_EQ(a.pxd, 100.0);
  EXPECT_EQ(a.displayQty, 500u);
  EXPECT_EQ(a.side, '0');
  EXPECT_EQ(a.transactTime, T0 + 1);
  EXPECT_EQ(a.sendingTime, T0 + 9);
  EXPECT_TRUE(a.endOfEvent);
  EXPECT_FALSE(a.recovery);

  out.clear();
  feed({modify(2, 340, 300, 10000000, 77, T0 + 2)}); // size down: change
  feed({modify(3, 340, 300, 10000000, 77, T0 + 3)}); // nothing changed: delete + add
  feed({modify(4, 340, 300, 10050000, 77, T0 + 4)}); // new price: change
  feed({modify(5, 340, 900, 10050000, 77, T0 + 5)}); // size up: change
  feed({cancel(6, 340, 77, T0 + 6)});
  v = book_records();
  ASSERT_EQ(v.size(), 6u);
  EXPECT_EQ(mbo(v[0]).orderUpdateAction, 1);
  EXPECT_EQ(mbo(v[0]).displayQty, 300u);
  EXPECT_EQ(mbo(v[1]).orderUpdateAction, 2);
  EXPECT_FALSE(mbo(v[1]).endOfEvent);
  EXPECT_EQ(mbo(v[2]).orderUpdateAction, 0);
  EXPECT_TRUE(mbo(v[2]).endOfEvent);
  EXPECT_EQ(mbo(v[1]).transactTime, mbo(v[2]).transactTime);
  EXPECT_EQ(mbo(v[2]).orderID, 77u);
  EXPECT_EQ(mbo(v[3]).orderUpdateAction, 1);
  EXPECT_DOUBLE_EQ(mbo(v[3]).pxd, 100.5);
  EXPECT_EQ(mbo(v[4]).displayQty, 900u);
  EXPECT_EQ(mbo(v[5]).orderUpdateAction, 2);
  EXPECT_EQ(h->open_orders(), 0u);
  EXPECT_EQ(h->stats().same_size_modifies, 1u);
}

TEST_F(Fixture, VisibleTradePartialThenFull)
{
  feed({add(1, 340, 2, 500, 10000000, 77, T0)});
  out.clear();
  feed({trade(2, 340, 1, 200, 10000000, 77, 9001, T0 + 5)});
  auto v = book_records();
  ASSERT_EQ(v.size(), 3u);
  auto &p = std::get<bfile::l3_mbp_trd_t>(v[0].r);
  EXPECT_EQ(p.typ, en::l3::MBPT);
  EXPECT_EQ(p.securityID, 1001);
  EXPECT_EQ(p.sz, 200);
  EXPECT_DOUBLE_EQ(p.pxd, 100.0);
  EXPECT_EQ(p.aggresorSide, 1); // resting sell, so the buyer aggressed
  EXPECT_FALSE(p.endOfEvent);
  auto &x = std::get<bfile::l3_mbo_trd_v2_t>(v[1].r);
  EXPECT_EQ(v[1].sec, 1001);
  EXPECT_EQ(x.orderID, 77u);
  EXPECT_EQ(x.lastQty, 200);
  EXPECT_EQ(mbo(v[2]).orderUpdateAction, 1);
  EXPECT_EQ(mbo(v[2]).displayQty, 300u);
  EXPECT_TRUE(mbo(v[2]).endOfEvent);
  EXPECT_EQ(p.transactTime, x.transactTime);
  EXPECT_EQ(x.transactTime, mbo(v[2]).transactTime);

  out.clear();
  feed({trade(3, 340, 1, 300, 10000000, 77, 9002, T0 + 6)});
  v = book_records();
  ASSERT_EQ(v.size(), 3u);
  EXPECT_EQ(mbo(v[2]).orderUpdateAction, 2);
  EXPECT_EQ(h->open_orders(), 0u);
}

TEST_F(Fixture, IcebergPeakReserveAndRefresh)
{
  // MD p.14, 3.4.6: peak trade type 1 with its orderRef, reserve trade type 2
  // with orderRef 0, refreshed peak as a new Order Add with a new orderRef.
  feed({add(1, 340, 1, 100, 10000000, 50, T0)});
  out.clear();
  feed({trade(2, 340, 1, 100, 10000000, 50, 1, T0 + 1), trade(3, 340, 2, 40, 10000000, 0, 2, T0 + 1),
        add(4, 340, 1, 100, 10000000, 51, T0 + 1)});
  auto v = book_records();
  ASSERT_EQ(v.size(), 5u); // print, exec, delete; print; add
  EXPECT_EQ(mbo(v[2]).orderUpdateAction, 2);
  EXPECT_EQ(std::get<bfile::l3_mbp_trd_t>(v[3].r).sz, 40);
  EXPECT_EQ(std::get<bfile::l3_mbp_trd_t>(v[3].r).aggresorSide, 0);
  EXPECT_EQ(mbo(v[4]).orderID, 51u);
  EXPECT_EQ(h->stats().hidden_trades, 1u);
  EXPECT_EQ(all<bfile::l3_mbp_trd_t>().size(), 2u); // one print per trade
}

TEST_F(Fixture, PrintOnlyTradeTypesAndBust)
{
  uint32_t seq = 1;
  for (int tt : {3, 4, 5, 6, 12})
    feed({trade(seq++, 340, tt, 10, 10000000, 0, 100 + tt, T0)});
  feed({bust(seq, 340, 10, 10000000, 103, T0)});
  EXPECT_TRUE(requests.empty());
  EXPECT_EQ(all<bfile::l3_mbp_trd_t>().size(), 5u);
  EXPECT_EQ(all<bfile::l3_mbo_trd_v2_t>().size(), 0u);
  EXPECT_EQ(h->stats().busts, 1u);
}

TEST_F(Fixture, OverfillIsClampedAndCounted)
{
  feed({add(1, 340, 1, 100, 10000000, 50, T0)});
  feed({trade(2, 340, 1, 150, 10000000, 50, 1, T0)});
  EXPECT_EQ(std::get<bfile::l3_mbo_trd_v2_t>(book_records()[2].r).lastQty, 100);
  EXPECT_EQ(h->stats().overfills, 1u);
  EXPECT_EQ(h->open_orders(), 0u);
}

TEST_F(Fixture, BundleIsOneEvent)
{
  // MD p.11, 3.4.1-3.4.2. The bundle crosses a packet boundary.
  feed({bundle_start(1), add(2, 340, 1, 100, 10000000, 1, T0 + 10), add(3, 340, 2, 100, 10010000, 2, T0 + 12)});
  EXPECT_TRUE(book_records().empty()); // held until Bundle End
  feed({add(4, 341, 1, 5, 20000000, 3, T0 + 13), bundle_end(5)});
  auto v = book_records();
  ASSERT_EQ(v.size(), 3u);
  for (size_t i = 0; i < v.size(); ++i)
  {
    EXPECT_EQ(mbo(v[i]).transactTime, T0 + 10) << i;
    EXPECT_EQ(mbo(v[i]).endOfEvent, i + 1 == v.size()) << i;
  }
  EXPECT_EQ(v[2].sec, 1002);
  EXPECT_EQ(h->expected(), 6u);
}

TEST_F(Fixture, TransactTimeNeverGoesBack)
{
  feed({add(1, 340, 1, 100, 10000000, 1, T0 + 100)});
  feed({add(2, 340, 1, 100, 10000000, 2, T0 + 50)});
  auto v = book_records();
  EXPECT_EQ(mbo(v[1]).transactTime, T0 + 100);
}

TEST_F(Fixture, TradingStatus)
{
  EXPECT_EQ(Handler::kaspar_status(1, 1), 17);
  EXPECT_EQ(Handler::kaspar_status(1, 0), 4);
  EXPECT_EQ(Handler::kaspar_status(2, 1), 2);
  EXPECT_EQ(Handler::kaspar_status(3, 1), 18);
  EXPECT_EQ(Handler::kaspar_status(8, 1), 17);
  EXPECT_EQ(Handler::kaspar_status(24, 1), 17);
  EXPECT_EQ(Handler::kaspar_status(8, 0), 4);
  EXPECT_EQ(Handler::kaspar_status(99, 1), 18);
  feed({status(1, 340, 2, 1, T0)});
  auto s = all<bfile::l3_sst_t>();
  ASSERT_EQ(s.size(), 1u);
  EXPECT_EQ(s[0].securityID, 1001);
  EXPECT_EQ(s[0].tradingStatus, 2);
  EXPECT_EQ(s[0].typ, en::l3::SST);
}

TEST_F(Fixture, NotRoutedSecuritiesProduceNothing)
{
  feed({add(1, 999, 1, 1, 100000, 5, T0), trade(2, 999, 1, 1, 100000, 5, 1, T0), cancel(3, 999, 5, T0),
        aod_update(4, 340, 10000000, 10, T0)});
  EXPECT_TRUE(book_records().empty());
  EXPECT_EQ(h->stats().not_routed, 2u);
  EXPECT_EQ(h->stats().unknown_orders, 0u);
  EXPECT_EQ(h->expected(), 5u);
}

TEST_F(Fixture, DefinitionsAndTickTablesFromTheFeed)
{
  ins.add_universe("NEWl", 1003);
  feed({tick_band(1, 7, "lse1", 0, 50), tick_band(2, 7, "lse1", 10000, 100), tick_band(3, 7, "lse1", 500000, 250),
        definition(4, 400, "NEWl", "GB00NEW00001", 7)});
  EXPECT_EQ(ins.sec_id(400), 1003);
  EXPECT_EQ(ins.find(400)->exchange_symbol, "NEWl");
  EXPECT_EQ(*ins.tick(400, 5000), 50u);
  EXPECT_EQ(*ins.tick(400, 10000), 100u);
  EXPECT_EQ(*ins.tick(400, 600000), 250u);
  feed({add(5, 400, 1, 1, 600000, 9, T0)});
  EXPECT_EQ(book_records().back().sec, 1003);
}

TEST_F(Fixture, ABDuplicatesAreDropped)
{
  auto p1 = packet({add(1, 340, 1, 100, 10000000, 1, T0)});
  auto p2 = packet({add(2, 340, 1, 100, 10000000, 2, T0), cancel(3, 340, 1, T0)});
  for (auto *p : {&p1, &p1, &p2, &p1, &p2})
    h->on_packet(p->data(), p->size());
  EXPECT_EQ(book_records().size(), 3u);
  EXPECT_EQ(h->stats().duplicates, 4u);
  EXPECT_EQ(h->expected(), 4u);
  EXPECT_TRUE(requests.empty());
}

TEST_F(Fixture, EndOfPacketRecord)
{
  feed({add(1, 340, 1, 100, 10000000, 1, T0, T0 + 3), add(2, 340, 1, 100, 10000000, 2, T0, T0 + 4)});
  ASSERT_FALSE(out.empty());
  EXPECT_EQ(out.back().sec, Handler::RECORDER);
  auto e = std::get<bfile::l3_eop_t>(out.back().r);
  EXPECT_EQ(e.msgSeqNum, 1u);
  EXPECT_EQ(e.sendingTime, T0 + 4);
}

TEST_F(Fixture, GapReplayedThenBufferApplied)
{
  feed({add(1, 340, 1, 100, 10000000, 1, T0)});
  feed({add(4, 340, 1, 100, 10000000, 4, T0 + 4)}); // 2-3 lost
  ASSERT_EQ(requests.size(), 1u);
  EXPECT_EQ(requests[0], std::make_pair(2u, 3u));
  EXPECT_EQ(h->state(), Handler::State::replay);
  EXPECT_EQ(all<bfile::l3_gap_v2_t>().size(), 1u);
  feed({add(5, 340, 1, 100, 10000000, 5, T0 + 5)});
  EXPECT_EQ(h->buffered(), 2u);

  // Replayed messages in a byte stream split at arbitrary points.
  std::string s = add(2, 340, 1, 100, 10000000, 2, T0 + 2) + add(3, 340, 1, 100, 10000000, 3, T0 + 3);
  replay(s.substr(0, 3));
  replay(s.substr(3, 30));
  replay(s.substr(33));
  EXPECT_EQ(h->state(), Handler::State::live);
  EXPECT_EQ(h->expected(), 6u);
  std::vector<uint64_t> ids;
  for (auto &m : all<bfile::l3_mbo_v2_t>())
    ids.push_back(m.orderID);
  EXPECT_EQ(ids, (std::vector<uint64_t>{1, 2, 3, 4, 5}));
}

TEST_F(Fixture, HeartbeatRevealsGap)
{
  feed({add(1, 340, 1, 100, 10000000, 1, T0)});
  feed({heartbeat(2)});
  EXPECT_TRUE(requests.empty());
  feed({heartbeat(4)});
  ASSERT_EQ(requests.size(), 1u);
  EXPECT_EQ(requests[0], std::make_pair(2u, 3u));
  replay(add(2, 340, 1, 1, 10000000, 2, T0) + add(3, 340, 1, 1, 10000000, 3, T0));
  EXPECT_EQ(h->state(), Handler::State::live);
  EXPECT_EQ(h->expected(), 4u);
}

TEST_F(Fixture, StartOfDayGapFromOne)
{
  feed({add(3, 340, 1, 100, 10000000, 3, T0)});
  ASSERT_EQ(requests.size(), 1u);
  EXPECT_EQ(requests[0], std::make_pair(1u, 2u));
}

TEST_F(Fixture, ReplayRefusedFallsBackToSnapshot)
{
  feed({add(1, 340, 1, 100, 10000000, 1, T0), add(2, 341, 2, 50, 20000000, 2, T0)});
  feed({add(5, 340, 1, 100, 10000000, 5, T0 + 5)});
  ASSERT_EQ(requests.size(), 1u);
  out.clear();
  replay(replay_response(1));
  EXPECT_EQ(h->state(), Handler::State::snapshot);
  EXPECT_EQ(h->stats().replay_refused, 1u);
  EXPECT_EQ(all<bfile::l3_chr_v2_t>().size(), 1u);
  EXPECT_EQ(h->open_orders(), 0u);

  feed({add(6, 340, 2, 70, 10010000, 6, T0 + 6), cancel(7, 340, 1, T0 + 7)});
  // Snapshot through 5: orders 1, 2 and 5 (3 and 4 were cancels of nothing).
  out.clear();
  snap({snap_start(5, 3, T0 + 5), book_status(340, 1, 1, 2), book_entry(340, 1, 100, 10000000, 1),
        book_entry(340, 1, 100, 10000000, 5)});
  EXPECT_TRUE(book_records().empty()); // not complete yet
  snap({book_status(341, 1, 1, 1), book_entry(341, 2, 50, 20000000, 2), book_status(999, 1, 1, 0)});
  EXPECT_EQ(h->state(), Handler::State::live);
  EXPECT_EQ(h->stats().snapshots_applied, 1u);
  EXPECT_EQ(h->expected(), 8u);

  auto m = all<bfile::l3_mbo_v2_t>();
  ASSERT_EQ(m.size(), 5u); // 3 snapshot adds, then seq 6 add and seq 7 delete
  EXPECT_TRUE(m[0].recovery && m[1].recovery && m[2].recovery);
  EXPECT_EQ(m[0].priority, 1u);
  EXPECT_EQ(m[1].priority, 2u);
  EXPECT_EQ(m[3].orderID, 6u);
  EXPECT_FALSE(m[3].recovery);
  EXPECT_EQ(m[4].orderUpdateAction, 2);
  EXPECT_EQ(m[4].orderID, 1u);
  auto s = all<bfile::l3_sst_t>();
  ASSERT_EQ(s.size(), 2u);
  EXPECT_EQ(s[0].tradingStatus, 17);
  EXPECT_EQ(h->open_orders(), 3u); // 5, 2, 6
}

TEST_F(Fixture, IncompleteSnapshotDiscarded)
{
  cfg.start_of_day = false;
  make();
  EXPECT_EQ(h->state(), Handler::State::snapshot);
  feed({add(11, 340, 1, 100, 10000000, 11, T0)});
  // Book Status announces 2 entries; the next Snapshot Start comes after 1.
  snap({snap_start(10, 1, T0), book_status(340, 1, 1, 2), book_entry(340, 1, 1, 10000000, 3)});
  snap({snap_start(10, 1, T0), book_status(340, 1, 1, 1), book_entry(340, 1, 7, 10000000, 3)});
  EXPECT_EQ(h->stats().snapshots_discarded, 1u);
  EXPECT_EQ(h->stats().snapshots_applied, 1u);
  EXPECT_EQ(h->state(), Handler::State::live);
  EXPECT_EQ(h->expected(), 12u);
  EXPECT_EQ(h->open_orders(), 2u);
}

TEST_F(Fixture, SnapshotBehindTheBufferIsCompletedByReplay)
{
  cfg.start_of_day = false;
  make();
  feed({add(20, 340, 1, 1, 10000000, 20, T0)});
  feed({add(21, 340, 1, 1, 10000000, 21, T0)});
  // Snapshot through 17: 18 and 19 are missing from the buffer.
  snap({snap_start(17, 0, T0)});
  ASSERT_EQ(requests.size(), 1u);
  EXPECT_EQ(requests[0], std::make_pair(18u, 19u));
  replay(add(18, 340, 1, 1, 10000000, 18, T0) + add(19, 340, 1, 1, 10000000, 19, T0));
  EXPECT_EQ(h->state(), Handler::State::live);
  EXPECT_EQ(h->expected(), 22u);
  EXPECT_EQ(h->open_orders(), 4u);
}

TEST_F(Fixture, LargeGapAndLostReplayGoToSnapshot)
{
  cfg.max_replay = 10;
  make();
  feed({add(1, 340, 1, 1, 10000000, 1, T0)});
  feed({add(100, 340, 1, 1, 10000000, 100, T0)});
  EXPECT_TRUE(requests.empty());
  EXPECT_EQ(h->state(), Handler::State::snapshot);

  make();
  feed({add(1, 340, 1, 1, 10000000, 1, T0)});
  feed({add(4, 340, 1, 1, 10000000, 4, T0)});
  EXPECT_EQ(h->state(), Handler::State::replay);
  h->on_replay_lost();
  EXPECT_EQ(h->state(), Handler::State::snapshot);
}

TEST_F(Fixture, MalformedAndLongerMessages)
{
  // A message longer than the table (a later version appending a field) is
  // stepped over by its length; the next one decodes.
  std::string longer = add(1, 340, 1, 100, 10000000, 1, T0);
  longer += std::string(4, '\x7f');
  longer[1] = char(longer.size());
  feed({longer, add(2, 340, 1, 100, 10000000, 2, T0)});
  EXPECT_EQ(all<bfile::l3_mbo_v2_t>().size(), 2u);

  // Unknown type: skipped by length, sequence consumed.
  aquis_test::W u = hdr(99, 3);
  u.u(0, 4);
  feed({done(u), add(4, 340, 1, 1, 10000000, 4, T0)});
  EXPECT_EQ(h->stats().unknown, 1u);
  EXPECT_EQ(h->expected(), 5u);

  // Count says 2, one message present: malformed packet.
  std::string p = packet({add(5, 340, 1, 1, 10000000, 5, T0)});
  p[0] = 2;
  h->on_packet(p.data(), p.size());
  EXPECT_EQ(h->stats().malformed_packets, 1u);
  // Length below the header size.
  std::string q = packet({add(6, 340, 1, 1, 10000000, 6, T0)});
  q[2] = 3;
  h->on_packet(q.data(), q.size());
  EXPECT_EQ(h->stats().malformed_packets, 2u);
}

TEST_F(Fixture, BundleMarkersNotSequenced)
{
  cfg.bundles_sequenced = false;
  make();
  feed({bundle_start(1), add(1, 340, 1, 1, 10000000, 1, T0), add(2, 340, 1, 1, 10000000, 2, T0), bundle_end(3)});
  EXPECT_TRUE(requests.empty());
  EXPECT_EQ(h->expected(), 3u);
  auto m = all<bfile::l3_mbo_v2_t>();
  ASSERT_EQ(m.size(), 2u);
  EXPECT_FALSE(m[0].endOfEvent);
  EXPECT_TRUE(m[1].endOfEvent);
}

TEST(Requests, LoginAndReplayRequestBytes)
{
  // MD p.20-21, 3.8.1-3.8.2
  aquis_test::W l = hdr(13, 0);
  l.s("user", 10).s("secret", 10);
  EXPECT_EQ(aquis::encode_login("user", "secret"), done(l));
  aquis_test::W r = hdr(14, 0);
  r.u(1000, 4).u(1099, 4);
  EXPECT_EQ(aquis::encode_replay_request(1000, 1099), done(r));
}

// Replay with invariants: a random order flow over three securities is sent as
// A and B copies with losses, duplicates and reordering of whole packets; the
// replay service answers from the generated stream. The book rebuilt from the
// records must equal the generator's book, and every message must be applied
// exactly once.
class RandomFlow : public Fixture, public ::testing::WithParamInterface<uint32_t>
{
};

TEST_P(RandomFlow, MatchesTruth)
{
  std::mt19937 rng(GetParam());
  struct O
  {
    uint16_t sec;
    int side;
    uint32_t qty;
    uint64_t px;
  };
  std::map<uint32_t, O> truth;
  std::vector<std::string> msgs; // index = seq - 1
  uint32_t next_ref = 1;
  const uint16_t secs[] = {340, 341, 999};
  for (int i = 0; i < 4000; ++i)
  {
    const uint32_t seq = uint32_t(msgs.size() + 1);
    const uint64_t ts = T0 + uint64_t(i) * 1000;
    const int what = truth.empty() ? 0 : int(rng() % 10);
    auto pick = [&] {
      auto it = truth.begin();
      std::advance(it, rng() % truth.size());
      return it;
    };
    if (what < 4)
    {
      O o{secs[rng() % 3], int(rng() % 2) + 1, 1 + rng() % 500, 10000000 + (rng() % 50) * 500};
      truth[next_ref] = o;
      msgs.push_back(add(seq, o.sec, o.side, o.qty, o.px, next_ref++, ts));
    }
    else if (what < 6)
    {
      auto it = pick();
      msgs.push_back(cancel(seq, it->second.sec, it->first, ts));
      truth.erase(it);
    }
    else if (what < 8)
    {
      auto it = pick();
      it->second.qty = 1 + rng() % 500;
      if (rng() % 2)
        it->second.px += 500;
      msgs.push_back(modify(seq, it->second.sec, it->second.qty, it->second.px, it->first, ts));
    }
    else
    {
      auto it = pick();
      const uint32_t q = 1 + rng() % it->second.qty;
      msgs.push_back(trade(seq, it->second.sec, 1, q, it->second.px, it->first, seq, ts));
      if ((it->second.qty -= q) == 0)
        truth.erase(it);
    }
  }
  // Packets of 1-5 messages; each sent on A and B, each copy lost with p=0.1.
  std::vector<std::string> packets;
  for (size_t i = 0; i < msgs.size();)
  {
    const size_t k = std::min<size_t>(1 + rng() % 5, msgs.size() - i);
    packets.push_back(packet(std::vector<std::string>(msgs.begin() + long(i), msgs.begin() + long(i + k))));
    i += k;
  }
  std::vector<std::pair<uint32_t, uint32_t>> served;
  for (size_t i = 0; i < packets.size(); ++i)
  {
    for (int copy = 0; copy < 2; ++copy)
      if (rng() % 10)
        h->on_packet(packets[i].data(), packets[i].size());
    if (i % 7 == 6 && rng() % 2)
      h->on_packet(packets[i - 3].data(), packets[i - 3].size()); // late duplicate
    while (served.size() < requests.size())
    {
      auto [b, e] = requests[served.size()];
      served.push_back(requests[served.size()]);
      std::string bytes;
      for (uint32_t s = b; s <= e; ++s)
        bytes += msgs[s - 1];
      h->on_replay_data(bytes.data(), bytes.size());
    }
  }
  EXPECT_EQ(h->state(), Handler::State::live);
  EXPECT_EQ(h->expected(), msgs.size() + 1);
  EXPECT_GT(h->stats().gaps, 0u);

  std::map<uint64_t, bfile::l3_mbo_v2_t> book;
  for (auto &m : all<bfile::l3_mbo_v2_t>())
  {
    if (m.orderUpdateAction == 2)
      EXPECT_EQ(book.erase(m.orderID), 1u) << m.orderID;
    else if (m.orderUpdateAction == 0)
      EXPECT_TRUE(book.emplace(m.orderID, m).second) << m.orderID;
    else
    {
      ASSERT_TRUE(book.count(m.orderID)) << m.orderID;
      book[m.orderID] = m;
    }
  }
  size_t routed = 0;
  for (auto &[ref, o] : truth)
  {
    if (o.sec == 999)
      continue;
    ++routed;
    ASSERT_TRUE(book.count(ref)) << ref;
    EXPECT_EQ(book[ref].displayQty, o.qty);
    EXPECT_DOUBLE_EQ(book[ref].pxd, double(o.px) / 1e5);
    EXPECT_EQ(book[ref].securityID, o.sec == 340 ? 1001 : 1002);
  }
  EXPECT_EQ(book.size(), routed);
  std::printf("random flow seed %u: %zu messages, %llu gaps, %llu replay requests, %llu duplicates\n", GetParam(), msgs.size(),
              (unsigned long long)h->stats().gaps, (unsigned long long)h->stats().replay_requests,
              (unsigned long long)h->stats().duplicates);
}

INSTANTIATE_TEST_SUITE_P(Seeds, RandomFlow, ::testing::Values(20261118u, 1u, 2u, 3u, 4u, 5u, 6u, 7u));
