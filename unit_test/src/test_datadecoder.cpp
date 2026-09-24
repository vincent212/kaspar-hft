/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/**
 * Unit tests for mdp3::DataDecoder's packet classifier -- scan() and
 * is_hot_template(). scan() is the gate that decides, per packet, whether the
 * hot parallel path is taken (all messages hot), the inline path (any cold), or
 * recovery (a corrupt zero-length SBE message). Getting this wrong either
 * mis-parallelizes a cold message or, worse, silently drops a corrupt packet --
 * so it is worth pinning down directly.
 *
 * scan() reads only the SBE framing (MsgSize @ +0, TemplateID @ +4, stride =
 * MsgSize) after the 12-byte packet header, so we can drive it with header-only
 * messages and never touch the feed_handler_if body.
 */

#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "mdp3/DataDecoder.hpp"
#include "mdp3/mbo_if.hpp"

namespace {

// DataDecoder needs a feed_handler_if to construct; scan()/is_hot_template()
// never call into it, so every method is a no-op. Signatures copied verbatim
// from mdp3/mbo_if.hpp.
struct NoopHandler : public mdp3::feed_handler_if
{
  void disable_mbo(bool) noexcept override {}
  void set_max_mbp_level(uint32_t) noexcept override {}
  void MDIncrementalRefreshBook(uint64_t, uint32_t, uint64_t, uint64_t, int32_t,
                                int64_t, int8_t, char, int32_t, int32_t, uint8_t,
                                bool, bool) noexcept override {}
  void MDIncrementalRefreshBook(uint64_t, uint32_t, uint64_t, uint64_t, int32_t,
                                int64_t, int8_t, char, int32_t, uint64_t, uint8_t,
                                uint64_t, bool, bool, bool) noexcept override {}
  void MDIncrementalRefreshTradeSummary(uint64_t, uint32_t, uint64_t, uint64_t,
                                        int32_t, int64_t, int8_t, char, uint8_t,
                                        int32_t, int32_t, bool, bool) noexcept override {}
  void MDIncrementalRefreshTradeSummary(uint64_t, uint32_t, uint64_t, uint64_t,
                                        int32_t, uint64_t, bool, bool) noexcept override {}
  void MDIncrementalRefreshSessionStatistics(uint32_t, uint64_t, uint64_t, uint32_t,
                                             uint8_t, int64_t, int64_t, uint8_t,
                                             char) noexcept override {}
  void MDIncrementalRefreshDailyStatistics(uint32_t, uint64_t, uint64_t, uint32_t,
                                           int64_t, int8_t, int32_t, char, bool,
                                           bool, uint8_t, uint16_t) noexcept override {}
  void MDInstrumentDefinitionFuture(uint32_t, uint64_t, char *, char *, char *,
                                    int64_t, int8_t, int64_t, int8_t, int64_t, int8_t,
                                    int32_t, char, uint8_t, uint64_t, uint64_t, char *,
                                    uint8_t, char, uint8_t, int64_t, uint8_t, uint8_t,
                                    char, int64_t, int8_t, uint8_t, char *, uint8_t,
                                    uint16_t, uint16_t, uint16_t, int32_t, int32_t,
                                    uint16_t, char *, int64_t, uint8_t) noexcept override {}
  void MDInstrumentDefinitionOption(uint32_t, uint64_t, char *, char *, char *,
                                    int64_t, int8_t, int64_t, int8_t, int32_t, char,
                                    uint64_t, uint64_t, char *, uint8_t, uint8_t, char,
                                    uint8_t, int64_t, uint8_t, uint8_t, int64_t, uint8_t,
                                    uint8_t, int64_t, uint8_t, char, int8_t, uint8_t,
                                    uint32_t[4], std::string[4], int64_t, int8_t, uint8_t,
                                    char *, uint8_t, uint16_t, int32_t, int32_t, uint16_t,
                                    char *, int64_t, uint8_t) noexcept override {}
  void MDInstrumentDefinitionSpread(uint32_t, uint64_t, char *, char *, char *,
                                    int64_t, int8_t, int64_t, int8_t, int32_t, char,
                                    uint64_t, uint64_t, char *, uint8_t, uint8_t, char,
                                    uint8_t, int64_t, uint8_t, uint8_t, char, int8_t,
                                    uint8_t, int32_t[8], int8_t[8], int64_t[8], int8_t[8],
                                    int8_t[8], int32_t[8], uint8_t[8], int64_t, int8_t,
                                    uint8_t, char *, uint8_t, uint16_t, int32_t, int32_t,
                                    uint16_t, char *) noexcept override {}
  void ChannelReset(uint32_t, uint64_t, uint64_t, const char *) noexcept override {}
  void SnapshotFullRefreshOrderBook_NR(uint32_t, uint32_t, uint32_t, uint64_t, uint64_t,
                                       uint32_t, uint32_t, int32_t, int32_t, int64_t,
                                       int64_t, char, uint64_t, uint64_t) noexcept override {}
  void SnapshotFullRefreshOrderBook(uint64_t, uint64_t, int32_t, int32_t, int64_t,
                                    int64_t, char, uint64_t, uint64_t) noexcept override {}
  void MDIncrementalRefreshLimitsBanding(uint32_t, uint64_t, uint64_t, int32_t, int64_t,
                                         int8_t, int64_t, int8_t, int64_t, int8_t,
                                         const char *) noexcept override {}
  void SecurityStatus(uint32_t, uint64_t, uint64_t, int32_t, uint8_t, uint8_t,
                      uint8_t) noexcept override {}
  void MDIncrementalRefreshVolume(uint32_t, uint64_t, uint64_t, int32_t, int32_t, char,
                                  uint8_t) noexcept override {}
  void DataReceoveryRestart() noexcept override {}
  void Gap() noexcept override {}
  void BurstEnd(uint32_t) noexcept override {}
  void EndOfPacket(u_int32_t, uint64_t) noexcept override {}
  void PrintStats() noexcept override {}
};

// SBE template IDs, taken from the codecs so the test tracks the real wire IDs.
const uint16_t HOT_BOOK46 = sbe::MDIncrementalRefreshBook46::sbeTemplateId();
const uint16_t HOT_OBOOK47 = sbe::MDIncrementalRefreshOrderBook47::sbeTemplateId();
const uint16_t HOT_TRADE48 = sbe::MDIncrementalRefreshTradeSummary48::sbeTemplateId();
const uint16_t COLD_SECSTAT30 = sbe::SecurityStatus30::sbeTemplateId();
const uint16_t COLD_RESET4 = sbe::ChannelReset4::sbeTemplateId();
// Order-INDEPENDENT colds: none of these reads or writes orderid_to_securityid.
// VOL37 is the one that matters in practice -- in the live ES chan 310 census
// it was every cold message seen, 3,353 of 3,353.
const uint16_t COLD_VOL37 = sbe::MDIncrementalRefreshVolume37::sbeTemplateId();
const uint16_t COLD_DAILY49 = sbe::MDIncrementalRefreshDailyStatistics49::sbeTemplateId();
const uint16_t COLD_BANDING50 = sbe::MDIncrementalRefreshLimitsBanding50::sbeTemplateId();
const uint16_t COLD_SESS51 = sbe::MDIncrementalRefreshSessionStatistics51::sbeTemplateId();
const uint16_t COLD_RFQ39 = sbe::QuoteRequest39::sbeTemplateId();
// Order-CRITICAL cold: a definition populates the securityID->asset_id map that
// Reconstructor::route() reads, so it cannot float past queued book messages.
const uint16_t COLD_DEFFUT54 = sbe::MDInstrumentDefinitionFuture54::sbeTemplateId();

// Build a packet: 12-byte header, then one 10-byte header-only SBE message per
// template id (MsgSize @ +0 = 10, TemplateID @ +4). If trailing_zero, append a
// final message with MsgSize == 0 (the corrupt case scan must catch).
std::vector<char> make_pkt(const std::vector<uint16_t> &templates, bool trailing_zero = false)
{
  std::vector<char> buf(12, 0); // MsgSeqNum(4) + SendingTime(8), contents irrelevant
  auto append = [&](uint16_t msgsize, uint16_t tid) {
    size_t off = buf.size();
    buf.resize(off + 10, 0);
    std::memcpy(&buf[off + 0], &msgsize, sizeof msgsize);
    std::memcpy(&buf[off + 4], &tid, sizeof tid);
  };
  for (uint16_t tid : templates)
    append(10, tid);
  if (trailing_zero)
    append(0, HOT_BOOK46);
  return buf;
}

class DataDecoderScanTest : public ::testing::Test
{
protected:
  NoopHandler cb;
  mdp3::DataDecoder decoder{&cb, /*disable_mbo=*/false, /*max_mbp_level=*/10, /*debug=*/false};
};

TEST_F(DataDecoderScanTest, AllHotPacketIsParallelizable)
{
  auto pkt = make_pkt({HOT_BOOK46, HOT_OBOOK47, HOT_TRADE48});
  auto sr = decoder.scan(pkt.data(), pkt.size());
  EXPECT_EQ(sr.count, 3u);
  EXPECT_TRUE(sr.all_hot);
  EXPECT_TRUE(sr.has_hot);
  EXPECT_FALSE(sr.corrupt);
}

TEST_F(DataDecoderScanTest, MixedHotAndColdIsNotAllHot)
{
  auto pkt = make_pkt({HOT_BOOK46, COLD_SECSTAT30});
  auto sr = decoder.scan(pkt.data(), pkt.size());
  EXPECT_EQ(sr.count, 2u);
  EXPECT_FALSE(sr.all_hot); // the cold message forces the inline path
  EXPECT_TRUE(sr.has_hot);
  EXPECT_FALSE(sr.corrupt);
}

TEST_F(DataDecoderScanTest, AllColdPacketHasNoHot)
{
  auto pkt = make_pkt({COLD_SECSTAT30, COLD_RESET4});
  auto sr = decoder.scan(pkt.data(), pkt.size());
  EXPECT_EQ(sr.count, 2u);
  EXPECT_FALSE(sr.all_hot);
  EXPECT_FALSE(sr.has_hot);
  EXPECT_FALSE(sr.corrupt);
}

TEST_F(DataDecoderScanTest, ZeroMsgSizeIsFlaggedCorrupt)
{
  // one good hot message, then a MsgSize==0 message that can never advance.
  auto pkt = make_pkt({HOT_BOOK46}, /*trailing_zero=*/true);
  auto sr = decoder.scan(pkt.data(), pkt.size());
  EXPECT_TRUE(sr.corrupt);  // must be caught, not silently stop
  EXPECT_EQ(sr.count, 1u);  // only the first message counted before the break
}

TEST_F(DataDecoderScanTest, EmptyPacketIsNotCorrupt)
{
  auto pkt = make_pkt({}); // header only, no SBE messages
  auto sr = decoder.scan(pkt.data(), pkt.size());
  EXPECT_EQ(sr.count, 0u);
  EXPECT_FALSE(sr.corrupt);
  EXPECT_FALSE(sr.has_hot);
}

TEST(DataDecoderHotTemplateTest, HotTemplatesRecognized)
{
  EXPECT_TRUE(mdp3::DataDecoder::is_hot_template(HOT_BOOK46));
  EXPECT_TRUE(mdp3::DataDecoder::is_hot_template(HOT_OBOOK47));
  EXPECT_TRUE(mdp3::DataDecoder::is_hot_template(HOT_TRADE48));
  EXPECT_FALSE(mdp3::DataDecoder::is_hot_template(COLD_SECSTAT30));
  EXPECT_FALSE(mdp3::DataDecoder::is_hot_template(COLD_RESET4));
}

// ---------------------------------------------------------------------------
// classify(): the three-way scheduling decision the packet split is built on.
// ---------------------------------------------------------------------------

using tclass = mdp3::DataDecoder::tclass;

// is_hot_template() is a wrapper over classify(); they must never disagree,
// because DecodeWorker asserts on is_hot_template() while the dispatcher will
// route on classify(). Two sources of truth here means a worker abort.
TEST(DataDecoderClassifyTest, HotAgreesWithIsHotTemplate)
{
  for (uint16_t tid : {HOT_BOOK46, HOT_OBOOK47, HOT_TRADE48, COLD_VOL37,
                       COLD_DAILY49, COLD_BANDING50, COLD_SESS51, COLD_RFQ39,
                       COLD_SECSTAT30, COLD_RESET4, COLD_DEFFUT54,
                       uint16_t(60000)})
    EXPECT_EQ(mdp3::DataDecoder::is_hot_template(tid),
              mdp3::DataDecoder::classify(tid) == tclass::HOT)
        << "disagreement on template " << tid;
}

TEST(DataDecoderClassifyTest, BookAndTradeAreHot)
{
  EXPECT_EQ(mdp3::DataDecoder::classify(HOT_BOOK46), tclass::HOT);
  EXPECT_EQ(mdp3::DataDecoder::classify(HOT_OBOOK47), tclass::HOT);
  EXPECT_EQ(mdp3::DataDecoder::classify(HOT_TRADE48), tclass::HOT);
}

// These touch neither orderid_to_securityid nor a book's order state, so they
// may be decoded inline while workers are still parsing book messages.
TEST(DataDecoderClassifyTest, StatisticsAreOrderIndependent)
{
  EXPECT_EQ(mdp3::DataDecoder::classify(COLD_VOL37), tclass::COLD_INDEPENDENT);
  EXPECT_EQ(mdp3::DataDecoder::classify(COLD_DAILY49), tclass::COLD_INDEPENDENT);
  EXPECT_EQ(mdp3::DataDecoder::classify(COLD_BANDING50), tclass::COLD_INDEPENDENT);
  EXPECT_EQ(mdp3::DataDecoder::classify(COLD_SESS51), tclass::COLD_INDEPENDENT);
  EXPECT_EQ(mdp3::DataDecoder::classify(COLD_RFQ39), tclass::COLD_INDEPENDENT);
}

// ChannelReset clears the book and definitions feed the asset map; letting
// either overtake queued book messages corrupts state, so both serialize.
TEST(DataDecoderClassifyTest, ResetStatusAndDefinitionsAreOrdered)
{
  EXPECT_EQ(mdp3::DataDecoder::classify(COLD_RESET4), tclass::COLD_ORDERED);
  EXPECT_EQ(mdp3::DataDecoder::classify(COLD_SECSTAT30), tclass::COLD_ORDERED);
  EXPECT_EQ(mdp3::DataDecoder::classify(COLD_DEFFUT54), tclass::COLD_ORDERED);
}

// An unknown template must fall to the SAFE side. If a schema bump adds a
// template that mutates the orderid map and it defaulted to INDEPENDENT, the
// split would silently corrupt books.
TEST(DataDecoderClassifyTest, UnknownTemplateSerializes)
{
  EXPECT_EQ(mdp3::DataDecoder::classify(60000), tclass::COLD_ORDERED);
  EXPECT_EQ(mdp3::DataDecoder::classify(0), tclass::COLD_ORDERED);
}

// ---------------------------------------------------------------------------
// scan() per-class counts and splittable().
// ---------------------------------------------------------------------------

TEST_F(DataDecoderScanTest, CountsPartitionTheMessages)
{
  auto pkt = make_pkt({HOT_BOOK46, COLD_VOL37, HOT_TRADE48, COLD_RESET4, COLD_DAILY49});
  auto sr = decoder.scan(pkt.data(), pkt.size());
  EXPECT_EQ(sr.n_hot, 2u);
  EXPECT_EQ(sr.n_cold_independent, 2u);
  EXPECT_EQ(sr.n_cold_ordered, 1u);
  // The three classes must account for every message, or the dispatcher would
  // either drop a message or decode one twice.
  EXPECT_EQ(sr.n_hot + sr.n_cold_independent + sr.n_cold_ordered, sr.count);
}

// The shape the census says is 2.79% of live ES chan 310 packets (3,351 of
// 120,000): book/trade plus a Volume37. Exactly the case the split exists for.
TEST_F(DataDecoderScanTest, HotPlusVolume37IsSplittable)
{
  auto pkt = make_pkt({HOT_BOOK46, COLD_VOL37, HOT_BOOK46});
  auto sr = decoder.scan(pkt.data(), pkt.size());
  EXPECT_FALSE(sr.all_hot);   // still blocked from the all-or-nothing fast path
  EXPECT_TRUE(sr.splittable()); // but the split can take it
}

TEST_F(DataDecoderScanTest, AllHotIsNotSplittableItIsAlreadyFast)
{
  auto pkt = make_pkt({HOT_BOOK46, HOT_TRADE48});
  auto sr = decoder.scan(pkt.data(), pkt.size());
  EXPECT_TRUE(sr.all_hot);
  // No cold message to peel off -- the existing all_hot path handles it.
  EXPECT_FALSE(sr.splittable());
}

TEST_F(DataDecoderScanTest, OrderedColdBlocksTheSplit)
{
  auto pkt = make_pkt({HOT_BOOK46, COLD_RESET4});
  auto sr = decoder.scan(pkt.data(), pkt.size());
  EXPECT_EQ(sr.n_cold_ordered, 1u);
  EXPECT_FALSE(sr.splittable()); // needs a barrier, not a split
}

TEST_F(DataDecoderScanTest, AllColdIsNotSplittable)
{
  auto pkt = make_pkt({COLD_VOL37, COLD_DAILY49});
  auto sr = decoder.scan(pkt.data(), pkt.size());
  EXPECT_EQ(sr.n_hot, 0u);
  EXPECT_FALSE(sr.splittable()); // nothing to hand a worker; inline is correct
}

// A corrupt packet must never be split, however clean its prefix looks --
// it has to reach mbo_data so recovery is triggered.
TEST_F(DataDecoderScanTest, CorruptIsNeverSplittable)
{
  auto pkt = make_pkt({HOT_BOOK46, COLD_VOL37}, /*trailing_zero=*/true);
  auto sr = decoder.scan(pkt.data(), pkt.size());
  EXPECT_TRUE(sr.corrupt);
  EXPECT_FALSE(sr.splittable());
}

} // namespace
