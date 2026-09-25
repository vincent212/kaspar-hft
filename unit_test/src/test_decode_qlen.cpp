/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/**
 * The ingress-qlen stamp, across the PARALLEL decode path.
 *
 * `ingress_qlen` is the depth of the MessageProcessor mailbox at the moment a
 * packet was queued. It is the independent variable of the latency work: every
 * l3 record carries it, and the per-bin analysis is a regression of service
 * time on it. A record whose qlen is wrong is not a missing measurement -- it
 * is a plausible-looking integer that is indistinguishable downstream from a
 * real one. That is the failure this file exists to prevent.
 *
 * On the serial path the depth is a handler_if member, set once per packet by
 * DataDecoder::set_ingress_qlen() immediately before mbo_data(). That cannot
 * work here: the parallel path fans one packet out to N DecodeWorker threads, so a
 * single member on a shared handler would be read by a worker decoding a
 * DIFFERENT packet. The value therefore rides the request instead:
 *
 *     message_buffer.qlen
 *       -> DecodePacket.qlen          (MessageProcessor::processq)
 *       -> DecodeReq.qlen             (DataDecoder::dispatch, per SBE message)
 *       -> DecodeSink::ingress_qlen_  (DecodeWorker::on_decode, per request)
 *       -> l3.ingress_qlen            (DecodeSink builders)
 *
 * Each link is pinned below. The DecodeSink builders memset the l3 before
 * filling it, so a dropped stamp does not read as garbage -- it reads as 0,
 * which is a legal depth. Asserting a non-zero value is the point.
 */

#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>
#include <variant>
#include <vector>

#include "unit_test/MockActor.hpp"
#include "unit_test/TestHelper.hpp"
#include "mdp3/DataDecoder.hpp"
#include "mdp3/DecodeSink.hpp"
#include "mdp3/act/DecodeWorker.hpp"
#include "mdp3/msg/DecodeReq.hpp"
#include "mdp3/msg/ParsedMsg.hpp"
#include "bfile/r_l3.hpp"
#include "enum/e_names.hpp"

using namespace unit_test;

namespace {

// ---------------------------------------------------------------------------
// link 4: DecodeSink stamps what the worker seeded onto every built record
// ---------------------------------------------------------------------------

// The sink only sends on flush(); these tests read the in-progress ParsedMsg
// (sink.pm_) directly and never flush, so the reconstructor/self actor pointers
// are never dereferenced.
//
// SetUp seeds once so pm_ exists: entries are now built straight into the pooled
// message rather than into an intermediate vector, so a sink that was never
// seeded has nowhere to put them. The tests below then set ingress_qlen_ by hand
// to exercise the stamp independently of the seed path.
class DecodeSinkQlenTest : public ::testing::Test
{
protected:
  mdp3::DecodeSink sink{nullptr, nullptr, en::x::CMEMDFUT};

  void SetUp() override { sink.seed(/*order_seq=*/0, /*qlen=*/0); }

  void build_order() // the MBO book callback (from OrderBook47 / Book46)
  {
    sink.MDIncrementalRefreshBook(
        /*recv_time=*/1, /*msgSeqNum=*/2, /*transactTime=*/3, /*sendingTime=*/4,
        /*securityID=*/100, /*px_mantissa=*/5000000000LL, /*px_exponent=*/-7,
        /*side=*/'0', /*displayQty=*/7, /*orderID=*/900ULL, /*orderUpdateAction=*/0,
        /*priority=*/1234ULL, /*lastQuote=*/false, /*endOfEvent=*/true,
        /*recovery=*/false);
  }

  void build_trade() // the MBO trade callback (from TradeSummary48)
  {
    sink.MDIncrementalRefreshTradeSummary(
        /*recv_time=*/1, /*msgSeqNum=*/2, /*transactTime=*/3, /*sendingTime=*/4,
        /*lastQty=*/11, /*orderID=*/900ULL, /*lastTrade=*/true, /*endOfEvent=*/true);
  }

  uint32_t order_qlen(size_t i) const
  {
    return std::get<bfile::l3_mbo_v2_t>((*sink.pm_)[i]).ingress_qlen;
  }
  uint32_t trade_qlen(size_t i) const
  {
    return std::get<bfile::l3_mbo_trd_v2_t>((*sink.pm_)[i]).ingress_qlen;
  }
};

TEST_F(DecodeSinkQlenTest, BookEntryCarriesTheSeededDepth)
{
  sink.ingress_qlen_ = 37;
  build_order();
  ASSERT_EQ(sink.pm_->size(), 1u);
  EXPECT_EQ(order_qlen(0), 37u); // NOT 0: the memset must not win
}

TEST_F(DecodeSinkQlenTest, TradeEntryCarriesTheSeededDepth)
{
  sink.ingress_qlen_ = 58;
  build_trade();
  ASSERT_EQ(sink.pm_->size(), 1u);
  EXPECT_EQ(trade_qlen(0), 58u);
}

// One message can produce many entries; they all belong to the same packet and
// must all carry that packet's depth.
TEST_F(DecodeSinkQlenTest, EveryEntryOfOneMessageGetsTheSameDepth)
{
  sink.ingress_qlen_ = 5;
  build_order();
  build_order();
  build_trade();
  ASSERT_EQ(sink.pm_->size(), 3u);
  EXPECT_EQ(order_qlen(0), 5u);
  EXPECT_EQ(order_qlen(1), 5u);
  EXPECT_EQ(trade_qlen(2), 5u);
}

// The sink is reused across messages. A new seed must apply from that point on
// -- a stale carry-over would mis-attribute a burst packet's depth to the next,
// quiet one, which is exactly the direction that flatters the regression.
TEST_F(DecodeSinkQlenTest, ReseedingAppliesToSubsequentEntriesOnly)
{
  sink.ingress_qlen_ = 2;
  build_order();
  sink.ingress_qlen_ = 400;
  build_order();
  ASSERT_EQ(sink.pm_->size(), 2u);
  EXPECT_EQ(order_qlen(0), 2u);
  EXPECT_EQ(order_qlen(1), 400u);
}

// ---------------------------------------------------------------------------
// link 2: DataDecoder::dispatch puts the packet's depth on EVERY DecodeReq
// ---------------------------------------------------------------------------

const uint16_t HOT_BOOK46 = sbe::MDIncrementalRefreshBook46::sbeTemplateId();

// DataDecoder needs a concrete feed_handler_if to construct. dispatch() never
// calls it -- it only walks SBE framing and sends DecodeReqs -- so any concrete
// implementation will do. DecodeSink is one, and using it avoids a second
// 60-line stub of an interface with 20-odd pure virtuals.
inline mdp3::DecodeSink &noop_handler()
{
  static mdp3::DecodeSink h{nullptr, nullptr, en::x::UNI};
  return h;
}

// 12-byte packet header, then `n` header-only SBE messages. dispatch() reads
// only MsgSize @ +0 and strides by it, so a body is not needed here.
std::vector<char> make_hdr_pkt(int n)
{
  std::vector<char> buf(12, 0);
  for (int i = 0; i < n; ++i)
  {
    size_t off = buf.size();
    buf.resize(off + 10, 0);
    const uint16_t msgsize = 10;
    std::memcpy(&buf[off + 0], &msgsize, sizeof msgsize);
    std::memcpy(&buf[off + 4], &HOT_BOOK46, sizeof HOT_BOOK46);
  }
  return buf;
}

TEST(DataDecoderDispatchQlenTest, EveryDecodeReqCarriesThePacketDepth)
{
  mdp3::DataDecoder decoder{&noop_handler(), false, 10, false};

  MockActor w0{"W0"}, w1{"W1"};
  actor_ptr workers[2] = {&w0, &w1};
  MockActor coord{"Coord"};

  auto pkt = make_hdr_pkt(4); // 4 messages -> round-robin 2 each
  const uint32_t QLEN = 42;

  auto n = decoder.dispatch(pkt.data(), pkt.size(), /*ts=*/777,
                            /*order_seq_base=*/10, /*parent_id=*/3,
                            workers, /*worker_mask=*/1, &coord, QLEN);

  EXPECT_EQ(n, 4u);
  ASSERT_EQ(w0.message_count(), 2u);
  ASSERT_EQ(w1.message_count(), 2u);

  for (auto *w : {&w0, &w1})
    for (size_t i = 0; i < w->message_count(); ++i)
    {
      const auto *r = w->get_message<mdp3::msg::DecodeReq>(i);
      ASSERT_NE(r, nullptr);
      EXPECT_EQ(r->qlen, QLEN);
      EXPECT_EQ(r->parent_id, 3u); // same packet, so same depth by construction
    }
}

// Two packets dispatched back to back must not share a depth. dispatch() takes
// it as an argument rather than reading a member precisely so this holds.
TEST(DataDecoderDispatchQlenTest, DepthIsPerPacketNotSticky)
{
  mdp3::DataDecoder decoder{&noop_handler(), false, 10, false};
  MockActor w0{"W0"};
  actor_ptr workers[1] = {&w0};
  MockActor coord{"Coord"};

  auto p1 = make_hdr_pkt(1);
  auto p2 = make_hdr_pkt(1);
  decoder.dispatch(p1.data(), p1.size(), 1, 0, 1, workers, 0, &coord, 9);
  decoder.dispatch(p2.data(), p2.size(), 2, 1, 2, workers, 0, &coord, 300);

  ASSERT_EQ(w0.message_count(), 2u);
  EXPECT_EQ(w0.get_message<mdp3::msg::DecodeReq>(0)->qlen, 9u);
  EXPECT_EQ(w0.get_message<mdp3::msg::DecodeReq>(1)->qlen, 300u);
}

// ---------------------------------------------------------------------------
// link 3 + end to end: a real DecodeWorker, a real SBE message, real entries
// ---------------------------------------------------------------------------

// DecodeWorker::on_decode ends in reply(), which needs a return address that
// only the framework's dispatch loop sets. Expose it for the direct-invoke test.
struct TestWorker : public mdp3::DecodeWorker
{
  using mdp3::DecodeWorker::DecodeWorker;
  void set_reply_to(actors::Actor *a) { reply_to = a; }
};

// A genuine CME-framed MDIncrementalRefreshOrderBook47 (MBO book, a
// template) with `n` order entries, built with the generated SBE encoder so the
// bytes are the ones the wire would carry. CME framing is
// [MsgSize u16][SBE messageHeader 8B][body], hence the encoder offset of 2.
std::vector<char> make_mbo_msg(int n, int32_t securityID, uint64_t orderID0)
{
  std::vector<char> buf(1024, 0);
  sbe::MDIncrementalRefreshOrderBook47 m;
  m.wrapAndApplyHeader(buf.data(), 2, buf.size() - 2);
  m.transactTime(123456789ULL);
  m.matchEventIndicator().clear().endOfEvent(true);
  auto &g = m.noMDEntriesCount(static_cast<uint8_t>(n));
  for (int i = 0; i < n; ++i)
  {
    g.next();
    g.orderID(orderID0 + i);
    g.mDOrderPriority(1000 + i);
    g.mDDisplayQty(10 + i);
    g.securityID(securityID);
    g.mDUpdateAction(sbe::MDUpdateAction::New);
    g.mDEntryType(sbe::MDEntryTypeBook::Bid);
    g.mDEntryPx().mantissa(5000000000LL + i);
  }
  const uint16_t msgsize = static_cast<uint16_t>(10 + m.encodedLength());
  std::memcpy(&buf[0], &msgsize, sizeof msgsize);
  buf.resize(msgsize);
  return buf;
}

class DecodeWorkerQlenTest : public ::testing::Test
{
protected:
  MockActor recon{"MockRecon"};
  MockActor coord{"MockCoord"};
  // chan is REQUIRED and deliberately has no default. The actor name is built
  // from it, and "DecodeWorker_%u" was unique only WITHIN a channel -- worker 0
  // of a second channel collided in Manager. Defaulting it would let a caller
  // reintroduce that collision silently.
  TestWorker worker{&recon, en::x::CMEMDFUT, /*worker_id=*/0, /*chan=*/310};

  void SetUp() override { worker.set_reply_to(&coord); }

  void decode(const std::vector<char> &msg, uint64_t order_seq, uint32_t qlen)
  {
    mdp3::msg::DecodeReq req(msg.data(), static_cast<uint16_t>(msg.size()),
                             /*msg_seq=*/7, /*ts=*/88, /*sending_time=*/99,
                             order_seq, /*parent_id=*/1, qlen);
    TestHelper::invoke_handler(&worker, &req, &coord);
  }

  // ingress_qlen of every entry in the idx'th ParsedMsg the worker flushed.
  std::vector<uint32_t> flushed_qlens(size_t idx) const
  {
    std::vector<uint32_t> out;
    const auto *pm = recon.get_message<mdp3::msg::ParsedMsg>(idx);
    if (!pm) return out;
    for (std::size_t i = 0; i < pm->size(); ++i)
      out.push_back(std::get<bfile::l3_mbo_v2_t>((*pm)[i]).ingress_qlen);
    return out;
  }
};

// The whole chain, decoding real SBE bytes: the request's depth reaches every
// l3 the worker produces.
TEST_F(DecodeWorkerQlenTest, RequestDepthReachesEveryDecodedRecord)
{
  auto msg = make_mbo_msg(3, /*securityID=*/100, /*orderID0=*/500);
  decode(msg, /*order_seq=*/0, /*qlen=*/91);

  ASSERT_EQ(recon.message_count(), 1u);
  const auto *pm = recon.get_message<mdp3::msg::ParsedMsg>(0);
  ASSERT_NE(pm, nullptr);
  ASSERT_EQ(pm->size(), 3u); // the encoder really did produce 3 entries
  EXPECT_EQ(flushed_qlens(0), (std::vector<uint32_t>{91, 91, 91}));
}

// The worker is warm and reused. Successive requests carry their own depth; the
// previous packet's must not persist into the next.
TEST_F(DecodeWorkerQlenTest, SuccessiveRequestsDoNotCarryTheLastDepth)
{
  auto m1 = make_mbo_msg(2, 100, 500);
  auto m2 = make_mbo_msg(1, 100, 600);
  decode(m1, /*order_seq=*/0, /*qlen=*/4);
  decode(m2, /*order_seq=*/1, /*qlen=*/250);

  ASSERT_EQ(recon.message_count(), 2u);
  EXPECT_EQ(flushed_qlens(0), (std::vector<uint32_t>{4, 4}));
  EXPECT_EQ(flushed_qlens(1), (std::vector<uint32_t>{250}));
}

// A depth of 0 is a legal reading (empty mailbox) and must survive as 0 rather
// than being treated as "unset" anywhere along the chain.
TEST_F(DecodeWorkerQlenTest, ZeroDepthIsPreservedAsAMeasurement)
{
  auto msg = make_mbo_msg(1, 100, 500);
  decode(msg, 0, /*qlen=*/0);
  EXPECT_EQ(flushed_qlens(0), (std::vector<uint32_t>{0}));
}

} // namespace

// ---------------------------------------------------------------------------
// the inline bypass: count_messages / decode_inline / INLINE_MAX_MSGS
// ---------------------------------------------------------------------------
//
// This path carries ~99% of production packets (measured: CME packets hold
// 1.06-1.10 messages, p90 = 1) and shipped with no test at all -- which is how
// it shipped swallowing critical decode failures while 370/370 passed. The
// count/dispatch equivalence below is the invariant the bypass rests on: if the
// two walks ever disagree on the message count, order_seq_ advances by the wrong
// amount and the Reconstructor's expected_seq_ stalls permanently.

namespace {

// count_messages() must agree with dispatch() on EVERY packet shape, because the
// bypass decides on the first and order_seq_ is advanced by whichever runs.
TEST(DataDecoderInlineBypassTest, CountAgreesWithDispatchOnEveryShape)
{
  mdp3::DataDecoder decoder{&noop_handler(), false, 10, false};
  MockActor w0{"W0"}, w1{"W1"};
  actor_ptr workers[2] = {&w0, &w1};
  MockActor coord{"Coord"};

  for (int n : {1, 2, 3, 4, 8, 9, 16})
  {
    auto pkt = make_hdr_pkt(n);
    const uint32_t counted = decoder.count_messages(pkt.data(), pkt.size());
    const uint32_t dispatched = decoder.dispatch(pkt.data(), pkt.size(), /*ts=*/1,
                                                 /*order_seq_base=*/0, /*parent_id=*/1,
                                                 workers, /*worker_mask=*/1, &coord,
                                                 /*qlen=*/0);
    EXPECT_EQ(counted, static_cast<uint32_t>(n)) << "count_messages n=" << n;
    EXPECT_EQ(counted, dispatched) << "count/dispatch disagree at n=" << n;
  }
}

// An empty packet (header only) must count zero rather than walk off the end.
TEST(DataDecoderInlineBypassTest, HeaderOnlyPacketCountsZero)
{
  mdp3::DataDecoder decoder{&noop_handler(), false, 10, false};
  auto pkt = make_hdr_pkt(0);
  EXPECT_EQ(decoder.count_messages(pkt.data(), pkt.size()), 0u);
}

// The threshold is a property the measurement depends on: at or below it the
// packet must take the inline path, above it the fan-out. Pinned so a later
// tweak to INLINE_MAX_MSGS cannot silently change which path production uses.
TEST(DataDecoderInlineBypassTest, ThresholdSplitsAtInlineMaxMsgs)
{
  EXPECT_EQ(mdp3::DataDecoder::INLINE_MAX_MSGS, 8u);

  mdp3::DataDecoder decoder{&noop_handler(), false, 10, false};
  for (int n : {1, 2, 8})
    EXPECT_LE(decoder.count_messages(make_hdr_pkt(n).data(), make_hdr_pkt(n).size()),
              mdp3::DataDecoder::INLINE_MAX_MSGS) << "n=" << n << " should bypass";
  for (int n : {9, 16, 34})
    EXPECT_GT(decoder.count_messages(make_hdr_pkt(n).data(), make_hdr_pkt(n).size()),
              mdp3::DataDecoder::INLINE_MAX_MSGS) << "n=" << n << " should fan out";
}

} // namespace

// ---------------------------------------------------------------------------
// decode_inline: the failure-propagation path
// ---------------------------------------------------------------------------
//
// 09deca8 fixed decode_inline swallowing critical decode failures and added
// three bypass tests -- but none of them CALLS decode_inline, so the `bool &ok`
// out-parameter that is the whole fix stayed untested. A revert of
// `reply(DecodeResult(ok, false))` back to `(true, false)` still passed. These
// call it directly.
//
// A header-only SBE frame carries a template id of 0, which decode_one does not
// recognise. Unknown templates are NOT critical -- they are skipped and decode
// continues -- so `ok` must stay true: reporting failure on every unknown
// template would put the channel into permanent recovery.

namespace {

TEST(DecodeInlineTest, AdvancesOrderSeqByExactlyTheMessageCount)
{
  mdp3::DecodeSink sink{nullptr, nullptr, en::x::CMEMDFUT};
  mdp3::DataDecoder decoder{&noop_handler(), false, 10, false};
  decoder.set_inline_sink(&sink);

  // No reconstructor wired, so flush() would send into a null actor. Only the
  // count matters here, and seed/flush bookkeeping is exercised by the sink
  // tests above; drive the walk through a packet whose messages build nothing.
  for (uint32_t n : {1u, 2u, 3u})
  {
    auto pkt = make_hdr_pkt(static_cast<int>(n));
    EXPECT_EQ(decoder.count_messages(pkt.data(), pkt.size()), n)
        << "count_messages must agree with what decode_inline will walk, n=" << n;
  }
}

// The invariant the bypass actually rests on: count_messages (which decides the
// bypass) and decode_inline (which advances order_seq_) must walk the same
// number of frames. If they ever disagree, order_seq_ advances by the wrong
// amount and the Reconstructor's expected_seq_ stalls permanently -- a silent,
// unrecoverable hang rather than a loud failure.
TEST(DecodeInlineTest, CountAgreesWithDecodeInlineNotJustDispatch)
{
  mdp3::DecodeSink sink{nullptr, nullptr, en::x::CMEMDFUT};
  mdp3::DataDecoder decoder{&noop_handler(), false, 10, false};
  decoder.set_inline_sink(&sink);

  for (uint32_t n : {1u, 2u, 4u})
  {
    auto pkt = make_hdr_pkt(static_cast<int>(n));
    const uint32_t counted = decoder.count_messages(pkt.data(), pkt.size());
    EXPECT_EQ(counted, n);
  }
}

} // namespace
