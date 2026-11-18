/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// EBS Market on CME Globex over MDP 3.0: what Kaspar's existing MDP 3.0 code
// does with the messages EBS sends.
//
// No EBS capture was available. Every message here is CONSTRUCTED with the
// codecs generated from CME's templates_FixBinary.xml (schema 1, version 13),
// with field values taken from CME's EBS documentation (cited per test). The
// tests check three things:
//
//   1. Layout: the generated codecs carry the EBS templates with the sizes the
//      EBS pages give.
//   2. Round trip: each EBS message encodes and decodes field for field,
//      including the version-13 fields of MDInstrumentDefinitionFX63.
//   3. Dispatch: what mdp3::DataDecoder (unchanged) does with an EBS packet.
//      These tests pin today's behaviour -- the gaps listed in feed.md
//      section 7 -- so they PASS while the gaps exist. A test named
//      Gap_... fails once the gap is closed and must then be rewritten.
//
// The last group exercises the prototype in include/ebs/MbpLevelBook.hpp
// (patch_proposal.md) against sim::LevelWindow / sim::Mbp2Mbo.

#include <gtest/gtest.h>

#include <climits>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "mdp3_sbe/MessageHeader.h"
#include "mdp3_sbe/MDInstrumentDefinitionFX63.h"
#include "mdp3_sbe/MDIncrementalRefreshBookLongQty64.h"
#include "mdp3_sbe/MDIncrementalRefreshTradeSummaryLongQty65.h"
#include "mdp3_sbe/MDIncrementalRefreshVolumeLongQty66.h"
#include "mdp3_sbe/SnapshotFullRefreshLongQty69.h"
#include "mdp3_sbe/SnapshotFullRefreshTCPLongQty68.h"
#include "mdp3_sbe/SecurityStatus30.h"
#include "mdp3_sbe/ChannelReset4.h"
#include "mdp3_sbe/AdminHeartbeat12.h"
#include "mdp3_sbe/MDIncrementalRefreshLimitsBanding50.h"

#include "mdp3/DataDecoder.hpp"
#include "unit_test/NullFeedHandler.hpp"

#include "sim/Mbp2Mbo.hpp"
#include "sim/LevelWindow.hpp"
#include "../include/ebs/MbpLevelBook.hpp"

namespace
{
  constexpr int32_t EURUSD = 4242;          // constructed SecurityID
  constexpr uint64_t T0 = 1'790'000'000'000'000'000ULL;
  constexpr int64_t E9 = 1'000'000'000LL;   // PRICE9 exponent is -9

  int64_t px9(double p) { return llround(p * double(E9)); }

  // One MDP 3.0 packet: MsgSeqNum (4), SendingTime (8), then messages, each
  // MsgSize (2, counts itself) + SBE header (8) + body. Little endian.
  // ("MDP 3.0 - Packet Structure", as implemented by DataDecoder::mbo_data.)
  struct Packet
  {
    std::vector<char> buf;
    Packet(uint32_t seq, uint64_t sending)
    {
      buf.resize(12);
      std::memcpy(buf.data(), &seq, 4);
      std::memcpy(buf.data() + 4, &sending, 8);
    }
    template <class M, class F>
    void add(F fill)
    {
      const size_t start = buf.size();
      buf.resize(start + 4096);
      M m;
      m.wrapAndApplyHeader(buf.data() + start + 2, 0, 4094);
      fill(m);
      const uint16_t size =
          uint16_t(2 + sbe::MessageHeader::encodedLength() + m.encodedLength());
      std::memcpy(buf.data() + start, &size, 2);
      buf.resize(start + size);
    }
    // The first message, for decode-only tests: pointer to its SBE header.
    char *msg(size_t off = 12) { return buf.data() + off; }
  };

  template <class M>
  M decode(Packet &p, size_t off = 12, uint16_t acting_version = 13)
  {
    const char *m = p.buf.data() + off;
    uint16_t size, block;
    std::memcpy(&size, m, 2);
    std::memcpy(&block, m + 2, 2);
    M d;
    d.wrapForDecode(p.buf.data() + off, 10, block, acting_version, size);
    return d;
  }

  // ---- constructed EBS messages ------------------------------------------

  // EUR/USD spot, values per "MDP 3.0 - Market Data Security Definition - FX"
  // (wiki 457327502 v4, 2026-04-20) and "EBS Conditional Price Increments"
  // (wiki 457704180 v8): Asset 6EUS, group 6SUS, MIC EBSC, segment 36, standard
  // tick 0.00005, alternate tick 0.000025, MQL 20 ms / alternate 100 ms,
  // PricePrecision 6 for CPI products. Symbol is a placeholder: no EBS tag-55
  // value was available.
  void fx_eurusd(sbe::MDInstrumentDefinitionFX63 &d)
  {
    d.matchEventIndicator().clear().endOfEvent(true);
    d.totNumReports(sbe::MDInstrumentDefinitionFX63::totNumReportsNullValue());
    d.securityUpdateAction(sbe::SecurityUpdateAction::Add);
    d.lastUpdateTime(T0);
    d.mDSecurityTradingStatus(sbe::SecurityTradingStatus::ReadyToTrade);
    d.applID(518);
    d.marketSegmentID(36);
    d.underlyingProduct(4);
    d.putSecurityExchange(std::string("EBSC"));
    d.putSecurityGroup(std::string("6SUS"));
    d.putAsset(std::string("6EUS"));
    d.putSymbol(std::string("EUR/USD-PLACEHOLDER"));
    d.securityID(EURUSD);
    d.putSecurityType(std::string("FXSPOT"));
    d.putCFICode(std::string("IFXXXP"));
    d.putCurrency(std::string("EUR"));
    d.putSettlCurrency(std::string("USD"));
    d.putPriceQuoteCurrency(std::string("USD"));
    d.matchAlgorithm('F');
    d.minTradeVol(1'000'000);
    d.maxTradeVol(999'000'000);
    d.minPriceIncrement().mantissa(px9(0.00005));
    d.displayFactor().mantissa(E9);
    d.pricePrecision(6);
    d.putUnitOfMeasure(std::string("EUR"));
    d.unitOfMeasureQty().mantissa(E9);
    d.highLimitPrice().mantissa(sbe::PRICENULL9::mantissaNullValue());
    d.lowLimitPrice().mantissa(sbe::PRICENULL9::mantissaNullValue());
    d.maxPriceVariation().mantissa(sbe::PRICENULL9::mantissaNullValue());
    d.userDefinedInstrument('N');
    d.putFinancialInstrumentFullName(std::string("FXSPOT.EUR/USD"));
    d.putFXCurrencySymbol(std::string("EUR/USD"));
    d.putSettlType(std::string("0"));
    d.interveningDays(2);
    d.minQuoteLife(20'000);                      // microseconds
    d.maxPriceDiscretionOffset().mantissa(0);
    d.instrumentGUID(1234567890123ULL);
    d.altMinPriceIncrement().mantissa(px9(0.000025));
    d.altMinQuoteLife(100'000);
    d.altPriceIncrementConstraint().mantissa(px9(0.00002));
    d.maxBidAskConstraint().mantissa(px9(0.00015));
    auto ev = d.noEventsCount(2);
    ev.next().eventType(sbe::EventType::Activation).eventTime(T0 - 1000);
    ev.next().eventType(sbe::EventType::LastEligibleTradeDate).eventTime(T0 + 1000);
    auto ft = d.noMDFeedTypesCount(1);
    ft.next().putMDFeedType(std::string("GBX")).marketDepth(10);
    auto ia = d.noInstAttribCount(1);
    ia.next().instAttribValue().clear().electronicMatchEligible(true);
    auto lot = d.noLotTypeRulesCount(2);
    lot.next().lotType(2).minLotSize(1'000'000);
    lot.next().lotType(5).minLotSize(1'000'000);
    auto ts = d.noTradingSessionsCount(1);
    ts.next().tradeDate(20775).settlDate(20777).maturityDate(
        sbe::MDInstrumentDefinitionFX63::NoTradingSessions::maturityDateNullValue());
  }

  struct BookEntry
  {
    char type;          // '0' bid, '1' offer, 'w' market best offer, 'x' market best bid
    uint8_t action;     // 0 New, 1 Change, 2 Delete, 5 Overlay
    uint8_t level;
    double px;
    uint64_t qty;
    int32_t orders;
  };

  void book(sbe::MDIncrementalRefreshBookLongQty64 &m, const std::vector<BookEntry> &es,
            uint64_t tx, uint32_t rptseq = 1)
  {
    m.transactTime(tx);
    m.matchEventIndicator().clear().lastQuoteMsg(true).endOfEvent(true);
    auto g = m.noMDEntriesCount(uint8_t(es.size()));
    for (const auto &e : es)
    {
      g.next();
      g.mDEntryPx().mantissa(px9(e.px));
      g.mDEntrySize(e.qty);
      g.securityID(EURUSD);
      g.rptSeq(rptseq++);
      g.numberOfOrders(e.orders);
      g.mDPriceLevel(e.level);
      g.mDUpdateAction(sbe::MDUpdateAction::get(e.action));
      g.mDEntryType(sbe::MDEntryTypeBook::get(e.type));
    }
    m.noOrderIDEntriesCount(0);   // "Not used for EBS markets" (wiki 457423069)
  }

  // Trade Summary LongQty: MDEntrySize and NumberOfOrders "Set to zero (EBS
  // markets only)" (wiki 457227775 v4; 457418925 v3).
  void trade(sbe::MDIncrementalRefreshTradeSummaryLongQty65 &m, double px, uint64_t tx)
  {
    m.transactTime(tx);
    m.matchEventIndicator().clear().lastTradeMsg(true);
    auto g = m.noMDEntriesCount(1);
    g.next()
        .mDEntrySize(0)
        .securityID(EURUSD)
        .rptSeq(10)
        .numberOfOrders(0)
        .mDTradeEntryID(77)
        .aggressorSide(sbe::AggressorSide::Buy)
        .mDUpdateAction(sbe::MDUpdateAction::New);
    g.mDEntryPx().mantissa(px9(px));
    m.noOrderIDEntriesCount(0);
  }

  // Delayed electronic volume (wiki 1870299138 v1, 2026-09-22): cumulative
  // notional, sent 60 s after the trade on the Ultra 1 ms channels.
  void volume(sbe::MDIncrementalRefreshVolumeLongQty66 &m, uint64_t cum, uint64_t tx)
  {
    m.transactTime(tx);
    m.matchEventIndicator().clear().lastVolumeMsg(true).endOfEvent(true);
    auto g = m.noMDEntriesCount(1);
    g.next().mDEntrySize(cum).securityID(EURUSD).rptSeq(11).mDUpdateAction(
        sbe::MDUpdateAction::New);
  }

  // Records what the unchanged MDP 3.0 decoder hands to the feed handler.
  struct Recorder : unit_test::NullFeedHandler
  {
    struct Mbp { int32_t sec; int64_t mant; int8_t exp; char side; int32_t sz; uint8_t lvl; };
    std::vector<Mbp> mbp;
    std::vector<int32_t> mbp_trade_sz;
    std::vector<int32_t> status_secid;
    std::vector<uint8_t> status_value;
    int resets = 0, limits = 0, volumes = 0, eop = 0;

    void MDIncrementalRefreshBook(uint64_t, uint32_t, uint64_t, uint64_t, int32_t sec,
        int64_t mant, int8_t exp, char side, int32_t sz, int32_t, uint8_t lvl, bool,
        bool) noexcept override
    {
      mbp.push_back({sec, mant, exp, side, sz, lvl});
    }
    void MDIncrementalRefreshTradeSummary(uint64_t, uint32_t, uint64_t, uint64_t, int32_t,
        int64_t, int8_t, char, uint8_t, int32_t sz, int32_t, bool, bool) noexcept override
    {
      mbp_trade_sz.push_back(sz);
    }
    void SecurityStatus(uint32_t, uint64_t, uint64_t, int32_t sec, uint8_t, uint8_t st,
        uint8_t) noexcept override
    {
      status_secid.push_back(sec);
      status_value.push_back(st);
    }
    void ChannelReset(uint32_t, uint64_t, uint64_t, const char *) noexcept override { ++resets; }
    void MDIncrementalRefreshLimitsBanding(uint32_t, uint64_t, uint64_t, int32_t, int64_t,
        int8_t, int64_t, int8_t, int64_t, int8_t, const char *) noexcept override { ++limits; }
    void MDIncrementalRefreshVolume(uint32_t, uint64_t, uint64_t, int32_t, int32_t, char,
        uint8_t) noexcept override { ++volumes; }
    void EndOfPacket(uint32_t, uint64_t) noexcept override { ++eop; }
  };

  bool run_decoder(Recorder &rec, Packet &p)
  {
    mdp3::DataDecoder dec(&rec, false, 10, false, 518);
    bool reset = false;
    return dec.mbo_data(p.buf.data(), p.buf.size(), 0, reset);
  }
}

// ===========================================================================
// 1. Layout
// ===========================================================================

TEST(EbsLayout, TemplatesPresentInSchemaVersion13)
{
  EXPECT_EQ(sbe::MDInstrumentDefinitionFX63::sbeSchemaId(), 1);
  EXPECT_EQ(sbe::MDInstrumentDefinitionFX63::sbeSchemaVersion(), 13);
  EXPECT_EQ(sbe::MDInstrumentDefinitionFX63::sbeTemplateId(), 63);
  EXPECT_EQ(sbe::MDIncrementalRefreshBookLongQty64::sbeTemplateId(), 64);
  EXPECT_EQ(sbe::MDIncrementalRefreshTradeSummaryLongQty65::sbeTemplateId(), 65);
  EXPECT_EQ(sbe::MDIncrementalRefreshVolumeLongQty66::sbeTemplateId(), 66);
  EXPECT_EQ(sbe::SnapshotFullRefreshTCPLongQty68::sbeTemplateId(), 68);
  EXPECT_EQ(sbe::SnapshotFullRefreshLongQty69::sbeTemplateId(), 69);
}

TEST(EbsLayout, BlockLengthsMatchDocumentation)
{
  // Root block lengths from templates_FixBinary.xml v13.
  EXPECT_EQ(sbe::MDInstrumentDefinitionFX63::sbeBlockLength(), 337);
  EXPECT_EQ(sbe::MDIncrementalRefreshBookLongQty64::sbeBlockLength(), 11);
  EXPECT_EQ(sbe::MDIncrementalRefreshTradeSummaryLongQty65::sbeBlockLength(), 11);
  EXPECT_EQ(sbe::SnapshotFullRefreshLongQty69::sbeBlockLength(), 59);
  // Group entry sizes. 66's is printed on wiki 1870299138 (blockLength="24").
  EXPECT_EQ(sbe::MDIncrementalRefreshBookLongQty64::NoMDEntries::sbeBlockLength(), 32u);
  EXPECT_EQ(sbe::MDIncrementalRefreshTradeSummaryLongQty65::NoMDEntries::sbeBlockLength(), 40u);
  EXPECT_EQ(sbe::MDIncrementalRefreshVolumeLongQty66::NoMDEntries::sbeBlockLength(), 24u);
  EXPECT_EQ(sbe::SnapshotFullRefreshLongQty69::NoMDEntries::sbeBlockLength(), 23u);
  // The four version-13 FX fields (wiki 457327502: "offset=309 sinceVersion=13").
  EXPECT_EQ(sbe::MDInstrumentDefinitionFX63::altMinPriceIncrementEncodingOffset(), 309u);
  EXPECT_EQ(sbe::MDInstrumentDefinitionFX63::altMinPriceIncrementSinceVersion(), 13u);
  EXPECT_EQ(sbe::MDInstrumentDefinitionFX63::maxBidAskConstraintEncodingOffset(), 329u);
}

TEST(EbsLayout, BookEntryTypesIncludeMarketBest)
{
  // MarketBestOffer 'w', MarketBestBid 'x' (sinceVersion 12), wiki 457325891.
  EXPECT_EQ(sbe::MDEntryTypeBook::get('w'), sbe::MDEntryTypeBook::MarketBestOffer);
  EXPECT_EQ(sbe::MDEntryTypeBook::get('x'), sbe::MDEntryTypeBook::MarketBestBid);
  EXPECT_EQ(sbe::MDUpdateAction::get(5), sbe::MDUpdateAction::Overlay);
}

// ===========================================================================
// 2. Round trip
// ===========================================================================

TEST(EbsRoundTrip, InstrumentDefinitionFX)
{
  Packet p(1, T0);
  p.add<sbe::MDInstrumentDefinitionFX63>(fx_eurusd);
  auto d = decode<sbe::MDInstrumentDefinitionFX63>(p);
  EXPECT_EQ(d.securityID(), EURUSD);
  EXPECT_EQ(d.getSecurityExchangeAsString(), "EBSC");
  EXPECT_EQ(d.getAssetAsString(), "6EUS");
  EXPECT_EQ(d.getSecurityGroupAsString(), "6SUS");
  EXPECT_EQ(d.getSecurityTypeAsString(), "FXSPOT");
  EXPECT_EQ(d.getFXCurrencySymbolAsString(), "EUR/USD");
  EXPECT_EQ(d.getFinancialInstrumentFullNameAsString(), "FXSPOT.EUR/USD");
  EXPECT_EQ(d.applID(), 518);
  EXPECT_EQ(d.marketSegmentID(), 36);
  EXPECT_EQ(d.minTradeVol(), 1'000'000u);
  EXPECT_EQ(d.minPriceIncrement().mantissa(), px9(0.00005));
  EXPECT_EQ(d.minPriceIncrement().exponent(), -9);
  EXPECT_EQ(d.pricePrecision(), 6);
  EXPECT_EQ(d.minQuoteLife(), 20'000u);
  EXPECT_EQ(d.interveningDays(), 2);
  EXPECT_TRUE(d.altMinPriceIncrementInActingVersion());
  EXPECT_EQ(d.altMinPriceIncrement().mantissa(), px9(0.000025));
  EXPECT_EQ(d.altMinQuoteLife(), 100'000u);
  EXPECT_EQ(d.maxBidAskConstraint().mantissa(), px9(0.00015));

  auto ev = d.noEvents();
  ASSERT_EQ(ev.count(), 2u);
  ev.next();
  EXPECT_EQ(ev.eventType(), sbe::EventType::Activation);
  ev.next();   // SBE groups decode in order: consume every entry first
  EXPECT_EQ(ev.eventType(), sbe::EventType::LastEligibleTradeDate);
  auto ft = d.noMDFeedTypes();
  ASSERT_EQ(ft.count(), 1u);
  ft.next();
  EXPECT_EQ(ft.getMDFeedTypeAsString(), "GBX");
  EXPECT_EQ(ft.marketDepth(), 10);
  auto ia = d.noInstAttrib();
  ASSERT_EQ(ia.count(), 1u);
  ia.next();
  EXPECT_TRUE(ia.instAttribValue().electronicMatchEligible());
  auto lot = d.noLotTypeRules();
  ASSERT_EQ(lot.count(), 2u);
  lot.next();
  EXPECT_EQ(lot.lotType(), 2);
  EXPECT_EQ(lot.minLotSize(), 1'000'000u);
  lot.next();
  EXPECT_EQ(lot.lotType(), 5);
  auto ts = d.noTradingSessions();
  ASSERT_EQ(ts.count(), 1u);
  ts.next();
  EXPECT_EQ(ts.tradeDate(), 20775);
  EXPECT_EQ(ts.settlDate(), 20777);
}

TEST(EbsRoundTrip, VersionTwelveDecoderSeesNoAltTickFields)
{
  // A decoder acting at version 12 (the schema before the September 2023 EBS
  // notice moved EBS to 13) must not read the four sub-tick fields.
  Packet p(1, T0);
  p.add<sbe::MDInstrumentDefinitionFX63>(fx_eurusd);
  auto d = decode<sbe::MDInstrumentDefinitionFX63>(p, 12, 12);
  EXPECT_FALSE(d.altMinPriceIncrementInActingVersion());
  EXPECT_EQ(d.altMinQuoteLife(), sbe::MDInstrumentDefinitionFX63::altMinQuoteLifeNullValue());
  EXPECT_EQ(d.minQuoteLife(), 20'000u);
}

TEST(EbsRoundTrip, BookLongQtyKeepsQuantitiesAboveInt32)
{
  Packet p(2, T0);
  p.add<sbe::MDIncrementalRefreshBookLongQty64>([](auto &m) {
    book(m, {{'x', 0, 1, 1.08500, 5'000'000, 3},
             {'w', 0, 1, 1.08505, 2'000'000, 1},
             {'0', 0, 1, 1.08500, 3'000'000'000ULL, 7},
             {'1', 0, 1, 1.08505, 2'000'000, 1}}, T0);
  });
  auto d = decode<sbe::MDIncrementalRefreshBookLongQty64>(p);
  auto g = d.noMDEntries();
  ASSERT_EQ(g.count(), 4u);
  std::vector<char> types;
  std::vector<uint64_t> qty;
  while (g.hasNext())
  {
    g.next();
    types.push_back(g.mDEntryTypeRaw());
    qty.push_back(g.mDEntrySize());
  }
  EXPECT_EQ(types, (std::vector<char>{'x', 'w', '0', '1'}));
  EXPECT_EQ(qty[2], uint64_t(3'000'000'000ULL));
  EXPECT_EQ(d.noOrderIDEntries().count(), 0u);
}

TEST(EbsRoundTrip, TradeSummaryLongQtyHasZeroSize)
{
  Packet p(3, T0);
  p.add<sbe::MDIncrementalRefreshTradeSummaryLongQty65>([](auto &m) { trade(m, 1.08505, T0); });
  auto d = decode<sbe::MDIncrementalRefreshTradeSummaryLongQty65>(p);
  auto g = d.noMDEntries();
  ASSERT_EQ(g.count(), 1u);
  g.next();
  EXPECT_EQ(g.mDEntryPx().mantissa(), px9(1.08505));
  EXPECT_EQ(g.mDEntrySize(), 0u);
  EXPECT_EQ(g.numberOfOrders(), 0);
  EXPECT_EQ(g.aggressorSide(), sbe::AggressorSide::Buy);
  EXPECT_EQ(g.mDTradeEntryID(), 77u);
}

TEST(EbsRoundTrip, SnapshotLongQty)
{
  Packet p(1, T0);
  p.add<sbe::SnapshotFullRefreshLongQty69>([](auto &m) {
    m.lastMsgSeqNumProcessed(1234).totNumReports(1).securityID(EURUSD).rptSeq(9)
        .transactTime(T0).lastUpdateTime(T0).tradeDate(20775)
        .mDSecurityTradingStatus(sbe::SecurityTradingStatus::ReadyToTrade);
    m.highLimitPrice().mantissa(sbe::PRICENULL9::mantissaNullValue());
    m.lowLimitPrice().mantissa(sbe::PRICENULL9::mantissaNullValue());
    m.maxPriceVariation().mantissa(sbe::PRICENULL9::mantissaNullValue());
    auto g = m.noMDEntriesCount(3);
    g.next().mDEntrySize(4'000'000'000ULL).numberOfOrders(9).mDPriceLevel(1)
        .mDEntryType(sbe::MDEntryType::Bid);
    g.mDEntryPx().mantissa(px9(1.08500));
    g.next().mDEntrySize(1'000'000).numberOfOrders(1).mDPriceLevel(1)
        .mDEntryType(sbe::MDEntryType::Offer);
    g.mDEntryPx().mantissa(px9(1.08505));
    g.next().mDEntrySize(250'000'000).mDEntryType(sbe::MDEntryType::ElectronicVolume);
    g.mDEntryPx().mantissa(sbe::PRICENULL9::mantissaNullValue());
  });
  auto d = decode<sbe::SnapshotFullRefreshLongQty69>(p);
  EXPECT_EQ(d.lastMsgSeqNumProcessed(), 1234u);
  auto g = d.noMDEntries();
  ASSERT_EQ(g.count(), 3u);
  g.next();
  EXPECT_EQ(g.mDEntrySize(), 4'000'000'000ULL);
  g.next();
  g.next();
  EXPECT_EQ(g.mDEntryTypeRaw(), 'e');
}

// ===========================================================================
// 3. Dispatch through the unchanged mdp3::DataDecoder
// ===========================================================================

TEST(EbsDispatch, PacketWithEveryIncrementalTemplateDecodesWithoutError)
{
  Packet p(5, T0);
  p.add<sbe::MDInstrumentDefinitionFX63>(fx_eurusd);
  p.add<sbe::MDIncrementalRefreshBookLongQty64>([](auto &m) {
    book(m, {{'0', 0, 1, 1.08500, 3'000'000, 2}}, T0 + 1);
  });
  p.add<sbe::MDIncrementalRefreshTradeSummaryLongQty65>([](auto &m) { trade(m, 1.08505, T0 + 2); });
  p.add<sbe::MDIncrementalRefreshVolumeLongQty66>([](auto &m) { volume(m, 12'000'000, T0 + 3); });
  p.add<sbe::SecurityStatus30>([](auto &m) {
    m.transactTime(T0 + 4).securityID(EURUSD).tradeDate(20775)
        .securityTradingStatus(sbe::SecurityTradingStatus::TradingHalt)
        .haltReason(sbe::HaltReason::SurveillanceIntervention)
        .securityTradingEvent(sbe::SecurityTradingEvent::NoEvent);
    m.putSecurityGroup(std::string(""));
    m.putAsset(std::string(""));
    m.matchEventIndicator().clear().endOfEvent(true);
  });
  p.add<sbe::AdminHeartbeat12>([](auto &) {});

  Recorder rec;
  EXPECT_TRUE(run_decoder(rec, p));
  EXPECT_EQ(rec.eop, 1);
  // The only EBS message that reaches the handler is Security Status.
  ASSERT_EQ(rec.status_secid.size(), 1u);
  EXPECT_EQ(rec.status_secid[0], EURUSD);
  EXPECT_EQ(rec.status_value[0], 2);
}

TEST(EbsDispatch, Gap_InstrumentDefinitionFXIsNotDispatched)
{
  // DataDecoder.hpp has no case for template 63; it logs "unknown message"
  // and skips it. InstrumentRecoveryRecorder.hpp handles 54, 55, 56 only.
  Packet p(6, T0);
  p.add<sbe::MDInstrumentDefinitionFX63>(fx_eurusd);
  Recorder rec;
  EXPECT_TRUE(run_decoder(rec, p));
  EXPECT_TRUE(rec.mbp.empty());
  EXPECT_EQ(rec.eop, 1);
}

TEST(EbsDispatch, Gap_BookLongQtyIsNotDispatched)
{
  Packet p(7, T0);
  p.add<sbe::MDIncrementalRefreshBookLongQty64>([](auto &m) {
    book(m, {{'0', 0, 1, 1.08500, 3'000'000, 2}, {'1', 0, 1, 1.08505, 1'000'000, 1}}, T0);
  });
  Recorder rec;
  EXPECT_TRUE(run_decoder(rec, p));
  EXPECT_TRUE(rec.mbp.empty());   // template 64 has no case in DataDecoder
}

TEST(EbsDispatch, Gap_TradeAndVolumeLongQtyAreNotDispatched)
{
  Packet p(8, T0);
  p.add<sbe::MDIncrementalRefreshTradeSummaryLongQty65>([](auto &m) { trade(m, 1.08505, T0); });
  p.add<sbe::MDIncrementalRefreshVolumeLongQty66>([](auto &m) { volume(m, 12'000'000, T0); });
  Recorder rec;
  EXPECT_TRUE(run_decoder(rec, p));
  EXPECT_TRUE(rec.mbp_trade_sz.empty());
  EXPECT_EQ(rec.volumes, 0);
}

TEST(EbsDispatch, ChannelResetAndLimitsBandingAreDispatched)
{
  // Templates EBS shares with CME futures reach the handler unchanged.
  Packet p(9, T0);
  p.add<sbe::ChannelReset4>([](auto &m) {
    m.transactTime(T0);
    m.matchEventIndicator().clear().endOfEvent(true);
    m.noMDEntriesCount(1).next().applID(518);
  });
  p.add<sbe::MDIncrementalRefreshLimitsBanding50>([](auto &m) {
    m.transactTime(T0);
    m.matchEventIndicator().clear().endOfEvent(true);
    auto g = m.noMDEntriesCount(1);
    g.next().securityID(EURUSD).rptSeq(3);
    g.highLimitPrice().mantissa(px9(1.2));
    g.lowLimitPrice().mantissa(px9(1.0));
    g.maxPriceVariation().mantissa(px9(0.01));
  });
  Recorder rec;
  EXPECT_TRUE(run_decoder(rec, p));
  EXPECT_EQ(rec.resets, 1);
  EXPECT_EQ(rec.limits, 1);
}

TEST(EbsDispatch, Gap_Int32QuantityInterfaceWouldTruncate)
{
  // feed_handler_if::MDIncrementalRefreshBook and ...TradeSummary take sz as
  // int32_t (mbo_if.hpp). An EBS level of 3,000,000,000 (notional, wiki
  // 457423069: "This field is in a notional format", uInt64NULL) does not fit.
  const uint64_t q = 3'000'000'000ULL;
  EXPECT_GT(q, uint64_t(INT32_MAX));
  const int32_t narrowed = static_cast<int32_t>(q);
  EXPECT_LT(narrowed, 0);
}

// ===========================================================================
// 4. Prototype: MBP level book -> sim::LevelWindow -> sim::Mbp2Mbo
// ===========================================================================

namespace
{
  struct Chain
  {
    std::vector<bfile::l3_t> out;
    sim::Mbp2Mbo m2m;
    sim::LevelWindow win;
    ebs::MbpLevelBook book;
    // tick 0.000025 (alternate tick, so every CPI price is a whole tick);
    // chunk 1,000,000 base-currency units. Both are decisions in review.md.
    Chain()
        : m2m({EURUSD, 0, 0.000025, 1'000'000.0},
              [this](const bfile::l3_t &r) { out.push_back(r); }),
          win(m2m, 10), book(10)
    {
    }
    void event(const std::vector<BookEntry> &es, uint64_t tx)
    {
      for (const auto &e : es)
        if (e.type == '0' || e.type == '1')   // market best (w/x) is not book data
          EXPECT_TRUE(book.apply(e.type == '0', e.action, e.level, e.px, e.qty));
      book.publish(win);
      win.flush(tx);
      m2m.end_event();
    }
  };
}

TEST(EbsPrototype, LevelIndexSemantics)
{
  Chain c;
  c.event({{'0', 0, 1, 1.08500, 3'000'000, 2}, {'0', 0, 2, 1.08495, 1'000'000, 1},
           {'1', 0, 1, 1.08505, 2'000'000, 1}}, T0);
  EXPECT_EQ(c.m2m.chunks(true, c.m2m.ticks(1.08500)), 3u);
  EXPECT_EQ(c.m2m.chunks(true, c.m2m.ticks(1.08495)), 1u);
  EXPECT_EQ(c.m2m.chunks(false, c.m2m.ticks(1.08505)), 2u);

  // New at level 1 shifts the others down.
  c.event({{'0', 0, 1, 1.085025, 1'000'000, 1}}, T0 + 10);   // a CPI sub-tick price
  EXPECT_EQ(c.book.side(true)[1].first, 1.08500);
  EXPECT_EQ(c.m2m.best(true), c.m2m.ticks(1.085025));

  // Delete level 1 shifts them back up.
  c.event({{'0', 2, 1, 1.085025, 0, 0}}, T0 + 20);
  EXPECT_EQ(c.m2m.best(true), c.m2m.ticks(1.08500));

  // Overlay (reading in feed.md section 10): level 2 becomes a new price.
  c.event({{'0', 5, 2, 1.08490, 4'000'000, 2}}, T0 + 30);
  EXPECT_EQ(c.m2m.chunks(true, c.m2m.ticks(1.08495)), 0u);
  EXPECT_EQ(c.m2m.chunks(true, c.m2m.ticks(1.08490)), 4u);
}

TEST(EbsPrototype, DepthTenDropsTheEleventhLevel)
{
  Chain c;
  std::vector<BookEntry> es;
  for (int i = 0; i < 10; ++i)
    es.push_back({'0', 0, uint8_t(i + 1), 1.08500 - 0.00005 * i, 1'000'000, 1});
  c.event(es, T0);
  ASSERT_EQ(c.book.side(true).size(), 10u);
  const double worst = c.book.side(true)[9].first;
  c.event({{'0', 0, 1, 1.08505, 1'000'000, 1}}, T0 + 10);
  ASSERT_EQ(c.book.side(true).size(), 10u);
  EXPECT_EQ(c.m2m.chunks(true, c.m2m.ticks(worst)), 0u);
}

TEST(EbsPrototype, QuantityAboveInt32SurvivesAsChunks)
{
  Chain c;
  c.event({{'0', 0, 1, 1.08500, 3'000'000'000ULL, 40}}, T0);
  EXPECT_EQ(c.m2m.chunks(true, c.m2m.ticks(1.08500)), 3000u);
}

TEST(EbsPrototype, ZeroSizeTradeExecutesNothing)
{
  // With 271=0 on EBS trades, Mbp2Mbo::trade executes no chunk; the next book
  // update then takes the traded quantity off the BACK of the queue, as if it
  // were a cancel. Orders queued at the front are never filled by the trade.
  // This is review.md decision 1.
  Chain c;
  c.event({{'1', 0, 1, 1.08505, 3'000'000, 3}}, T0);
  const auto front = c.m2m.queue(false, c.m2m.ticks(1.08505))->front();
  c.out.clear();
  c.m2m.trade(1.08505, 0.0, false, T0 + 1);
  c.m2m.end_event();
  int execs = 0;
  for (const auto &r : c.out)
    if (std::holds_alternative<bfile::l3_mbo_trd_v2_t>(r))
      ++execs;
  EXPECT_EQ(execs, 0);
  c.event({{'1', 1, 1, 1.08505, 1'000'000, 1}}, T0 + 2);
  EXPECT_EQ(c.m2m.queue(false, c.m2m.ticks(1.08505))->front(), front);
}
