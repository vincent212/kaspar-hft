/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/**
 * Unit tests for mdp3::DataDecoder::scan() -- the packet pre-pass. scan() counts
 * the SBE messages in a packet (so on_decode_packet can reserve that many
 * order_seqs) and flags a corrupt packet (a zero-length SBE message). It no
 * longer classifies hot/cold -- every message is dispatched.
 *
 * scan() reads only the SBE framing (MsgSize @ +0, stride = MsgSize) after the
 * 12-byte packet header, so we can drive it with header-only messages and never
 * touch the feed_handler_if body.
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

// scan() counts every message regardless of template -- there is no hot/cold
// classification anymore; on_decode_packet dispatches them all.
TEST_F(DataDecoderScanTest, CountsEveryMessage)
{
  auto pkt = make_pkt({HOT_BOOK46, HOT_OBOOK47, HOT_TRADE48, COLD_SECSTAT30, COLD_RESET4});
  auto sr = decoder.scan(pkt.data(), pkt.size());
  EXPECT_EQ(sr.count, 5u);
  EXPECT_FALSE(sr.corrupt);
}

TEST_F(DataDecoderScanTest, ZeroMsgSizeIsFlaggedCorrupt)
{
  // one good message, then a MsgSize==0 message that can never advance.
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
}

} // namespace
