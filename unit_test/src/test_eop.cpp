/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/**
 * Unit tests for the END OF PACKET (EOP) record.
 *
 *   EopRecord  the l3 EOP record: appended enum value, packed size, write/read
 *              round trip, read_l3 gives ts = 0, records after it still decode
 *   BfaEop     replay: BFA reads EOP records and drops them; no book receives
 *              one, order records are still routed and fills still broadcast
 */

#include <gtest/gtest.h>

#include <cstdio>
#include <memory>
#include <string>
#include <unistd.h>
#include <vector>

#include "bfile/r_l3.hpp"
#include "enum/l3.hpp"
#include "actors/msg/Continue.hpp"
#include "frame/mda/act/BFA.hpp"
#include "frame/mda/msg/Data.hpp"

namespace {

std::string tmp_path(const std::string& stem)
{
  return "/tmp/kaspar_test_eop_" + std::to_string(getpid()) + "_" + stem;
}

bfile::l3_eop_t make_eop(uint32_t seq, uint64_t send_time)
{
  bfile::l3_eop_t e{};
  e.typ = en::l3::EOP;
  e.venue = en::x::CMEMD;
  e.msgSeqNum = seq;
  e.sendingTime = send_time;
  return e;
}

bfile::l3_mbo_v2_t make_mbo(int32_t sec_id, uint64_t oid, double pxd, uint64_t txtime)
{
  bfile::l3_mbo_v2_t m{};
  m.typ = en::l3::MBO_V2;
  m.venue = en::x::CMEMD;
  m.transactTime = txtime;
  m.sendingTime = txtime + 1000;
  m.handlerendtim = txtime + 2000;
  m.orderUpdateAction = 0;
  m.securityID = sec_id;
  m.orderID = oid;
  m.priority = oid;
  m.pxd = pxd;
  m.displayQty = 1;
  m.side = '0';
  m.endOfEvent = true;
  return m;
}

bfile::l3_mbo_trd_v2_t make_fill(uint64_t oid, int32_t qty, uint64_t txtime)
{
  bfile::l3_mbo_trd_v2_t t{};
  t.typ = en::l3::MBOT_V2;
  t.venue = en::x::CMEMD;
  t.transactTime = txtime;
  t.sendingTime = txtime + 1000;
  t.lastQty = qty;
  t.orderID = oid;
  return t;
}

// Stands in for a book: records the l3 payload of every Data message sent to it.
struct Recorder : public actors::Actor {
  std::vector<bfile::l3_t> data;
  void send(const actors::Message* m, actors::Actor* = nullptr) noexcept override
  {
    if (auto d = dynamic_cast<const frame::mda::msg::Data*>(m))
      data.push_back(d->l3);
    delete m;
  }
  template <typename T> size_t count() const
  {
    size_t n = 0;
    for (auto& r : data) n += std::holds_alternative<T>(r);
    return n;
  }
};

} // namespace

// ---------------------------------------------------------------------------
// The record
// ---------------------------------------------------------------------------

TEST(EopRecord, AppendedAfterVolumeSoOldFilesKeepTheirTypeBytes)
{
  // The enum value is the type byte on disk; EOP must come after every type
  // that existed before it, or existing .bin files would be misread.
  EXPECT_EQ(static_cast<int>(en::l3::EOP), static_cast<int>(en::l3::VOLUME) + 1);
  EXPECT_STREQ(en::to_string(en::l3(en::l3::EOP)), "EOP");
  EXPECT_TRUE(en::is_valid(en::l3(en::l3::EOP)));
}

TEST(EopRecord, PackedSizeIsTypeVenueSeqTime)
{
  EXPECT_EQ(sizeof(bfile::l3_eop_packed_t), sizeof(bfile::l3_typ_t) + 1 + 4 + 8);
}

TEST(EopRecord, RoundTripAndNeighboursStillDecode)
{
  const auto path = tmp_path("roundtrip.bin.gz");
  {
    gzFile f = gzopen(path.c_str(), "wb");
    ASSERT_TRUE(f);
    bfile::write_l3(f, make_mbo(7, 1, 2100000.0, 100));
    bfile::write_l3(f, make_eop(4242, 123456789012345ULL));
    bfile::write_l3(f, make_fill(1, 2, 200));
    bfile::write_l3(f, make_eop(4243, 123456789099999ULL));
    gzclose(f);
  }
  gzFile f = gzopen(path.c_str(), "rb");
  ASSERT_TRUE(f);
  bfile::l3_t r; uint64_t ts = 0;

  ASSERT_TRUE(bfile::read_l3(f, r, ts));
  ASSERT_TRUE(std::holds_alternative<bfile::l3_mbo_v2_t>(r));
  EXPECT_EQ(ts, 100u);

  ASSERT_TRUE(bfile::read_l3(f, r, ts));
  ASSERT_TRUE(std::holds_alternative<bfile::l3_eop_t>(r));
  EXPECT_EQ(std::get<bfile::l3_eop_t>(r).msgSeqNum, 4242u);
  EXPECT_EQ(std::get<bfile::l3_eop_t>(r).sendingTime, 123456789012345ULL);
  EXPECT_EQ(ts, 0u);   // like EOB: no engine time; the send time is in the record

  ASSERT_TRUE(bfile::read_l3(f, r, ts));
  ASSERT_TRUE(std::holds_alternative<bfile::l3_mbo_trd_v2_t>(r));
  EXPECT_EQ(std::get<bfile::l3_mbo_trd_v2_t>(r).lastQty, 2);

  ASSERT_TRUE(bfile::read_l3(f, r, ts));
  ASSERT_TRUE(std::holds_alternative<bfile::l3_eop_t>(r));
  EXPECT_EQ(std::get<bfile::l3_eop_t>(r).msgSeqNum, 4243u);

  EXPECT_FALSE(bfile::read_l3(f, r, ts));
  gzclose(f);
  std::remove(path.c_str());
}

// ---------------------------------------------------------------------------
// Replay through BFA
// ---------------------------------------------------------------------------

// BFA is driven through its real input path: the records are written to a
// .bin and each Continue reads and routes one record (continue_handler).
class BfaEop : public ::testing::Test {
protected:
  std::string bin;
  Recorder book_a, book_b;
  std::unique_ptr<frame::mda::act::BFA<>> bfa;

  void run(const std::vector<bfile::l3_t>& recs)
  {
    bin = tmp_path("bfa.bin.gz");
    gzFile f = gzopen(bin.c_str(), "wb");
    for (auto& r : recs) bfile::write_l3(f, r);
    gzclose(f);
    std::vector<std::unordered_map<int32_t, actor_ptr>> da(en::x_num_syms());
    da[en::x::CMEMD][100] = &book_a;
    da[en::x::CMEMD][200] = &book_b;
    bfa = std::make_unique<frame::mda::act::BFA<>>(da, bin, nullptr);
    for (size_t i = 0; i < recs.size(); ++i) {   // never past EOF: that calls manager
      actors::msg::Continue c;
      bfa->fast_send(&c, nullptr);
    }
  }
  void TearDown() override { bfa.reset(); std::remove(bin.c_str()); }
};

TEST_F(BfaEop, NoBookReceivesEop)
{
  run({make_mbo(100, 1, 2100000.0, 10), make_eop(1, 1010),
       make_mbo(200, 2, 1900000.0, 20), make_eop(2, 1020)});
  EXPECT_EQ(book_a.count<bfile::l3_eop_t>(), 0u);
  EXPECT_EQ(book_b.count<bfile::l3_eop_t>(), 0u);
  EXPECT_EQ(bfa->eop_dropped(), 2u);
}

TEST_F(BfaEop, OrderRecordsStillRouteToTheirBook)
{
  run({make_mbo(100, 1, 2100000.0, 10), make_eop(1, 1010),
       make_mbo(200, 2, 1900000.0, 20), make_mbo(100, 3, 2100025.0, 30), make_eop(2, 1030)});
  EXPECT_EQ(book_a.count<bfile::l3_mbo_v2_t>(), 2u);
  EXPECT_EQ(book_b.count<bfile::l3_mbo_v2_t>(), 1u);
}

TEST_F(BfaEop, FillsStillBroadcast)
{
  run({make_mbo(100, 1, 2100000.0, 10), make_fill(1, 1, 20), make_eop(1, 1020)});
  EXPECT_EQ(book_a.count<bfile::l3_mbo_trd_v2_t>(), 1u);
  EXPECT_EQ(book_b.count<bfile::l3_mbo_trd_v2_t>(), 1u);
  EXPECT_EQ(book_b.data.size(), 1u);       // the fill only, no EOP
}
