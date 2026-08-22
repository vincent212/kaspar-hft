#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "mktdata_v12/MDIncrementalRefreshBook46.h"
#include "mktdata_v12/MDIncrementalRefreshSessionStatistics51.h"
#include "mktdata_v12/MDInstrumentDefinitionOption55.h"
#include "mktdata_v12/MDInstrumentDefinitionFuture54.h"
#include "mktdata_v12/MDInstrumentDefinitionSpread56.h"
#include "mktdata_v12/MDInstrumentDefinitionFixedIncome57.h"
#include "mktdata_v12/MDIncrementalRefreshTradeSummary48.h"
#include "mktdata_v12/MDIncrementalRefreshOrderBook47.h"
#include "mktdata_v12/SnapshotFullRefreshOrderBook53.h"
#include "mktdata_v12/ChannelReset4.h"
#include "mktdata_v12/MDIncrementalRefreshLimitsBanding50.h"
#include "mktdata_v12/MDIncrementalRefreshVolume37.h"
#include "mktdata_v12/SecurityStatus30.h"
#include "mktdata_v12/QuoteRequest39.h"
#include "mktdata_v12/AdminHeartbeat12.h"
#include "mktdata_v12/MDIncrementalRefreshDailyStatistics49.h"


#include "mdp3/mbo_if.hpp"
#include "logger/act/Logger.hpp"

namespace mdp3
{

  typedef struct
  {
    int32_t secid;
    int64_t px_mantissa;
    int8_t px_exponent;
    uint32_t seq;
    uint8_t side;
    uint32_t sz;
    int32_t numorders;
    uint8_t pxlevel;
  } repg1_t;

  static const char* get_name() { return "msg_decoder"; }

  static void
  set_max_mbp_level(uint32_t level, mdp3::feed_handler_if *cb)
  {
    cb->set_max_mbp_level(level);
  }

  static void
  decode_MDIncrementalRefreshDailyStatistics49(
      [[maybe_unused]] uint64_t ts,
      uint32_t MsgSeqNum,
      uint64_t SendingTime,
      sbe::MDIncrementalRefreshDailyStatistics49 &stats,
      mdp3::feed_handler_if *cb
  )
  {
    auto txtim = stats.transactTime();
    auto mdentries = stats.noMDEntries();

    while (mdentries.hasNext())
    {
      mdentries.next();
      auto securityID = mdentries.securityID();
      auto px_mantissa = mdentries.mDEntryPx().mantissa();
      auto px_exponent = mdentries.mDEntryPx().exponent();
      auto size = mdentries.mDEntrySize();
      auto side = mdentries.mDEntryType();
      auto finalDaily = mdentries.settlPriceType().finalDaily();
      auto intraday = mdentries.settlPriceType().intraday();
      auto updateAction = mdentries.mDUpdateAction();
      auto tradingReferenceDate = mdentries.tradingReferenceDate();
      cb->MDIncrementalRefreshDailyStatistics(
        MsgSeqNum,
        txtim,
        SendingTime,
        securityID,
        px_mantissa,
        px_exponent,
        size,
        side,
        finalDaily,
        intraday,
        updateAction,
        tradingReferenceDate
      );
    }

  }

  //
  // MBP and MBO
  //
  static void
  decode_MDIncrementalRefreshBook46(
      uint64_t ts,
      uint32_t MsgSeqNum,
      uint64_t SendingTime,
      sbe::MDIncrementalRefreshBook46 &incr,
      mdp3::feed_handler_if *cb)
  {
    auto txtim = incr.transactTime();
    auto matchevent = incr.matchEventIndicator();
    auto noMDEntries = incr.noMDEntries();
    std::vector<repg1_t> g1;
    g1.clear();
    while (noMDEntries.hasNext())
    {
      noMDEntries.next();
      repg1_t g;
      g.secid = noMDEntries.securityID();
      g.px_mantissa = noMDEntries.mDEntryPx().mantissa();
      g.px_exponent = noMDEntries.mDEntryPx().exponent();
      g.seq = noMDEntries.rptSeq();
      g.side = noMDEntries.mDEntryType();
      g.sz = noMDEntries.mDEntrySize();
      g.numorders = noMDEntries.numberOfOrders();
      g.pxlevel = noMDEntries.mDPriceLevel();
      g1.push_back(g);
      cb->MDIncrementalRefreshBook(
          ts,
          MsgSeqNum,
          txtim,
          SendingTime,
          g.secid,
          g.px_mantissa,
          g.px_exponent,
          g.side,
          g.sz,
          g.numorders,
          g.pxlevel,
          matchevent.endOfEvent(),
          matchevent.recoveryMsg());
    }
    auto oidentries = incr.noOrderIDEntries();

    while (oidentries.hasNext())
    {
      oidentries.next();
      auto dispq = oidentries.mDDisplayQty();
      auto oid = oidentries.orderID();
      auto act = oidentries.orderUpdateAction();
      auto prio = oidentries.mDOrderPriority();
      auto idx = oidentries.referenceID();

      cb->MDIncrementalRefreshBook(
          ts,
          MsgSeqNum,
          txtim,
          SendingTime,
          g1[idx - 1].secid,
          g1[idx - 1].px_mantissa,
          g1[idx - 1].px_exponent,
          g1[idx - 1].side,
          dispq,
          oid,
          act,
          prio,
          matchevent.lastQuoteMsg(),
          matchevent.endOfEvent(),
          matchevent.recoveryMsg());
    }
  }

  //
  // MBP & MBO trade
  //
  // todo: disable mbp/mbo trade
  static void
  decode_MDIncrementalRefreshTradeSummary48(
      uint64_t ts,
      uint32_t MsgSeqNum,
      uint64_t SendingTime,
      sbe::MDIncrementalRefreshTradeSummary48 &trade,
      mdp3::feed_handler_if *cb)
  {

    auto txtim = trade.transactTime();
    auto matchevent = trade.matchEventIndicator();
    auto noMDEntries = trade.noMDEntries();
    int32_t securityId = 0;
    while (noMDEntries.hasNext())
    {
      noMDEntries.next();
      securityId = noMDEntries.securityID();
      auto px_mant = noMDEntries.mDEntryPx().mantissa();
      auto px_exp = noMDEntries.mDEntryPx().exponent();
      auto side = noMDEntries.mDEntryType();
      auto aggr_side = noMDEntries.aggressorSide();
      auto sz = noMDEntries.mDEntrySize();
      auto numorders = noMDEntries.numberOfOrders();
      
      //
      // MBP trade
      //
      cb->MDIncrementalRefreshTradeSummary(
          ts,
          MsgSeqNum,
          txtim,
          SendingTime,
          securityId,
          px_mant,
          px_exp,
          *side,
          aggr_side,
          sz,
          numorders,
          matchevent.lastTradeMsg(),
          matchevent.endOfEvent());
    }

    auto oidentries = trade.noOrderIDEntries();

    while (oidentries.hasNext())
    {
      oidentries.next();
      auto lastQty = oidentries.lastQty();
      auto oid = oidentries.orderID();
      //
      // MBO trade
      //
      cb->MDIncrementalRefreshTradeSummary(
          ts,
          MsgSeqNum,
          txtim,
          SendingTime,
          lastQty,
          oid,
          matchevent.lastTradeMsg(),
          matchevent.endOfEvent());
    }
  }

  static void
  decode_MDIncrementalRefreshSessionStatistics51(
      [[maybe_unused]] uint64_t ts,
      uint32_t MsgSeqNum,
      uint64_t SendingTime,
      sbe::MDIncrementalRefreshSessionStatistics51 &stats,
      mdp3::feed_handler_if *cb)
  {
    auto txtim = stats.transactTime();
    auto mdentries = stats.noMDEntries();

    while (mdentries.hasNext())
    {
      mdentries.next();
      auto secid = mdentries.securityID();
      auto ocsflag = mdentries.openCloseSettlFlagRaw();
      auto px_mantissa = mdentries.mDEntryPx().mantissa();
      auto px_exponent = mdentries.mDEntryPx().mantissa();
      auto update_act = mdentries.mDUpdateActionRaw();
      auto entry_typ = mdentries.mDEntryTypeRaw();

      cb->MDIncrementalRefreshSessionStatistics(
          MsgSeqNum,
          txtim,
          SendingTime,
          secid,
          ocsflag,
          px_mantissa,
          px_exponent,
          update_act,
          entry_typ);
    }
  }

  static int32_t
  decode_MDInstrumentDefinitionSpread56(
      [[maybe_unused]] uint64_t ts,
      uint32_t MsgSeqNum,
      uint64_t SendingTime,
      sbe::MDInstrumentDefinitionSpread56 &def,
      mdp3::feed_handler_if *cb)
  {
    auto sym = def.symbol();
    auto asset = def.asset();
    auto securityID = def.securityID();
    auto securityGroup = def.securityGroup();
    auto securityUpdateAction = def.securityUpdateAction();
    auto mDSecurityTradingStatus = def.mDSecurityTradingStatus();
    auto cFICode = def.cFICode();
    auto highLimitPrice_mantissa = def.highLimitPrice().mantissa();
    auto highLimitPrice_exponent = def.highLimitPrice().exponent();
    auto lowLimitPrice_mantinssa = def.lowLimitPrice().mantissa();
    auto lowLimitPrice_exponent = def.lowLimitPrice().exponent();
    auto mainFraction = def.mainFraction();
    auto marketSegmentID = def.marketSegmentID();
    auto matchAlgorithm = def.matchAlgorithm();
    auto minPriceIncrement_exponent = def.minPriceIncrement().exponent();
    auto minPriceIncrement_mantissa = def.minPriceIncrement().mantissa();
    auto priceDisplayFormat = def.priceDisplayFormat();
    auto userDefinedInstrument = def.userDefinedInstrument();

    auto tickRule = def.tickRule();
    auto dispFactor_mantissa = def.displayFactor().mantissa();
    auto dispFactor_exponent = def.displayFactor().exponent();
    auto subFraction = def.subFraction();
    auto securityType = def.securityType();
    auto maturityMonth = def.maturityMonthYear().month();
    auto maturityYear = def.maturityMonthYear().year();
    auto openInterestQty = def.openInterestQty();
    auto clearedVolume = def.clearedVolume();
    auto tradingRefDate = def.tradingReferenceDate();

    auto unitOfMeasure = def.unitOfMeasure();

    auto events = def.noEvents();
    uint64_t activation = 0, expiration = 0;
    while (events.hasNext())
    {
      events.next();
      auto evtime = events.eventTime();
      auto evid = events.eventType();
      if (evid == 5)
      {
        activation = evtime;
      }
      else if (evid == 7)
      {
        expiration = evtime;
      }
      else
        SNGH;
    }

    // SBE repeating groups are read off a single sequential cursor.  A
    // group accessor does not seek — it parses a group header at the
    // codec's current sbePosition(), which advances only as groups are
    // iterated.  Spread56's wire order is
    //
    //   NoEvents -> NoMDFeedTypes -> NoInstAttrib -> NoLotTypeRules
    //            -> NoLegs
    //
    // so going straight from noEvents() to noLegs() does not skip ahead,
    // it makes noLegs() parse NoMDFeedTypes' bytes and every leg's
    // securityID / ratio / side / price comes out of that blob.  Drain
    // the three intervening groups even though we want nothing in them.
    {
      auto feedTypes = def.noMDFeedTypes();
      while (feedTypes.hasNext()) feedTypes.next();
      auto instAttrib = def.noInstAttrib();
      while (instAttrib.hasNext()) instAttrib.next();
      auto lotTypeRules = def.noLotTypeRules();
      while (lotTypeRules.hasNext()) lotTypeRules.next();
    }

    auto noLegs = def.noLegs();

    int32_t legOptionDelta_mantissa[8];
    int8_t legOptionDelta_exponent[8];
    int64_t legPrice_mantissa[8];
    int8_t legPrice_exponent[8];
    int8_t legRatioQty[8];
    int32_t legSecurityID[8];
    uint8_t legSide[8];

    std::size_t cnt = 0;
    while (noLegs.hasNext())
    {
      noLegs.next();
      int32_t legOptionDelta_mantissa_ = noLegs.legOptionDelta().mantissa();
      int8_t legOptionDelta_exponent_ = noLegs.legOptionDelta().exponent();
      int64_t legPrice_mantissa_ = noLegs.legPrice().mantissa();
      int8_t legPrice_exponent_ = noLegs.legPrice().exponent();
      int8_t legRatioQty_ = noLegs.legRatioQty();
      int32_t legSecurityID_ = noLegs.legSecurityID();
      uint8_t legSide_ = noLegs.legSideRaw();
      //uint8_t legSide_ = noLegs.legSide();
      legOptionDelta_mantissa[cnt] = legOptionDelta_mantissa_;
      legOptionDelta_exponent[cnt] = legOptionDelta_exponent_;
      legPrice_mantissa[cnt] = legPrice_mantissa_;
      legPrice_exponent[cnt] = legPrice_exponent_;
      legRatioQty[cnt] = legRatioQty_;
      legSecurityID[cnt] = legSecurityID_;
      legSide[cnt] = legSide_;
      if (++cnt == 8)
        break;
    }

    if (cnt >= 8)
    {
      log_err("have more than 3 legs in this security id: %d", securityID);
    }

    cb->MDInstrumentDefinitionSpread(
        MsgSeqNum,
        SendingTime,
        sym,
        asset,
        cFICode,
        highLimitPrice_mantissa,
        highLimitPrice_exponent,
        lowLimitPrice_mantinssa,
        lowLimitPrice_exponent,
        securityID,
        securityUpdateAction,
        activation,
        expiration,
        securityGroup,
        marketSegmentID,
        mDSecurityTradingStatus,
        matchAlgorithm,
        mainFraction,
        minPriceIncrement_mantissa,
        minPriceIncrement_exponent,
        priceDisplayFormat,
        userDefinedInstrument,
        tickRule,
        cnt,
        legOptionDelta_mantissa,
        legOptionDelta_exponent,
        legPrice_mantissa,
        legPrice_exponent,
        legRatioQty,
        legSecurityID,
        legSide,

        dispFactor_mantissa,
        dispFactor_exponent,

        subFraction,
        securityType,
        maturityMonth,
        maturityYear,

        openInterestQty,
        clearedVolume,
        tradingRefDate,
        unitOfMeasure

        );

    return securityID;
  }

  static int32_t
  decode_MDInstrumentDefinitionOption55(
      [[maybe_unused]] uint64_t ts,
      uint32_t MsgSeqNum,
      uint64_t SendingTime,
      sbe::MDInstrumentDefinitionOption55 &def,
      mdp3::feed_handler_if *cb)
  {
    auto sym = def.symbol();
    auto asset = def.asset();
    auto securityID = def.securityID();
    auto securityGroup = def.securityGroup();
    auto securityUpdateAction = def.securityUpdateAction();
    auto mDSecurityTradingStatus = def.mDSecurityTradingStatus();
    auto cFICode = def.cFICode();

    auto highLimitPrice_mantissa = def.highLimitPrice().mantissa();
    auto highLimitPrice_exponent = def.highLimitPrice().exponent();

    auto lowLimitPrice_mantinssa = def.lowLimitPrice().mantissa();
    auto lowLimitPrice_exponent = def.lowLimitPrice().exponent();
    auto mainFraction = def.mainFraction();
    auto marketSegmentID = def.marketSegmentID();
    auto matchAlgorithm = def.matchAlgorithm();

    auto minPriceIncrement_exponent = def.minPriceIncrement().exponent();
    auto minPriceIncrement_mantissa = def.minPriceIncrement().mantissa();
    auto priceDisplayFormat = def.priceDisplayFormat();
    auto minCabPrice_exponent = def.minCabPrice().exponent();
    auto minCabPrice_mantissa = def.minCabPrice().mantissa();

    auto putOrCall = def.putOrCallRaw();

    auto strikePrice_mantissa = def.strikePrice().mantissa();
    auto strikePrice_exponent = def.strikePrice().exponent();
    auto userDefinedInstrument = def.userDefinedInstrument();

    auto tickRule = def.tickRule();


    auto dispFactor_mantissa = def.displayFactor().mantissa();
    auto dispFactor_exponent = def.displayFactor().exponent();

    auto subFraction = def.subFraction();
    auto securityType = def.securityType();
    auto maturityMont = def.maturityMonthYear().month();
    auto maturityYear = def.maturityMonthYear().year();
    auto openInterestQty = def.openInterestQty();
    auto clearedVolume = def.clearedVolume();
    auto tradingRefDate = def.tradingReferenceDate();
    auto unitOfMeasure = def.unitOfMeasure();
    auto unitOfMeasureQty_exponent = def.unitOfMeasureQty().exponent();
    auto unitOfMeasureQty_mantissa = def.unitOfMeasureQty().mantissa();

    auto events = def.noEvents();
    uint64_t activation = 0, expiration = 0;
    while (events.hasNext())
    {
      events.next();
      auto evtime = events.eventTime();
      auto evid = events.eventType();
      if (evid == 5)
      {
        activation = evtime;
      }
      else if (evid == 7)
      {
        expiration = evtime;
      }
      else
        SNGH;
    }

    // Same positional-group hazard as the spread decoder above.
    // Option55's wire order is
    //
    //   NoEvents -> NoMDFeedTypes -> NoInstAttrib -> NoLotTypeRules
    //            -> NoUnderlyings -> NoRelatedInstruments
    //
    // Without draining the three intervening groups, noUnderlyings()
    // parses NoMDFeedTypes' bytes.  The result is stable rather than
    // random, which makes it easy to miss: the first field there is
    // mDFeedType, a char[3] of "GBX" (full depth) or "GBI" (implied), so
    // every option decodes to underlyingSecurityID 56115783 ("GBX\x03")
    // or 21578311 ("GBI\x01") and an underlyingSymbol of "" or "GBI".
    {
      auto feedTypes = def.noMDFeedTypes();
      while (feedTypes.hasNext()) feedTypes.next();
      auto instAttrib = def.noInstAttrib();
      while (instAttrib.hasNext()) instAttrib.next();
      auto lotTypeRules = def.noLotTypeRules();
      while (lotTypeRules.hasNext()) lotTypeRules.next();
    }

    auto noUnderlyings = def.noUnderlyings();
    uint32_t underlyingSecurityID[4];
    std::string underlyingSecuritySym[4];

    uint8_t cnt = 0;
    while (noUnderlyings.hasNext())
    {
      noUnderlyings.next();
      auto underlyingSecurityID_ = noUnderlyings.underlyingSecurityID();
      underlyingSecurityID[cnt] = underlyingSecurityID_;
      // getUnderlyingSymbolAsString(), NOT underlyingSymbol().  The
      // latter returns a bare pointer into the SBE frame; the field is a
      // FIXED 20-byte char array that CME space-pads rather than
      // NUL-terminates, so constructing a std::string from it runs off
      // the end of the field.  The AsString form bounds the scan at 20.
      underlyingSecuritySym[cnt] = noUnderlyings.getUnderlyingSymbolAsString();

      if (++cnt == 4)
        break;
    }

    cb->MDInstrumentDefinitionOption(
        MsgSeqNum,
        SendingTime,
        sym,
        asset,
        cFICode,
        highLimitPrice_mantissa,
        highLimitPrice_exponent,
        lowLimitPrice_mantinssa,
        lowLimitPrice_exponent,
        securityID,
        securityUpdateAction,
        activation,
        expiration,
        securityGroup,
        marketSegmentID,
        mDSecurityTradingStatus,
        matchAlgorithm,
        mainFraction,
        minPriceIncrement_mantissa,
        minPriceIncrement_exponent,
        priceDisplayFormat,
        minCabPrice_mantissa,
        minCabPrice_exponent,
        putOrCall,
        strikePrice_mantissa,
        strikePrice_exponent,
        userDefinedInstrument,
        tickRule,
        cnt,

        underlyingSecurityID,
        underlyingSecuritySym,

        dispFactor_mantissa,
        dispFactor_exponent,

        subFraction,
        securityType,
        maturityMont,
        maturityYear,
        openInterestQty,
        clearedVolume,
        tradingRefDate,
        unitOfMeasure,
        unitOfMeasureQty_mantissa,
        unitOfMeasureQty_exponent
        
        );

    return securityID;
  }

  static int32_t
  decode_MDInstrumentDefinitionFuture54(
      [[maybe_unused]] uint64_t ts,
      uint32_t MsgSeqNum,
      uint64_t SendingTime,
      sbe::MDInstrumentDefinitionFuture54 &def,
      mdp3::feed_handler_if *cb)
  {
    auto sym = def.symbol();
    auto asset = def.asset();
    auto cFICode = def.cFICode();

    auto high_limit_px_mantissa = def.highLimitPrice().mantissa();
    auto high_limit_px_exponent = def.highLimitPrice().exponent();
    auto low_limit_px_mantissa = def.lowLimitPrice().mantissa();
    auto low_limit_px_exponent = def.lowLimitPrice().exponent();
    auto mainFraction = def.mainFraction();

    auto pxvar_mantissa = def.maxPriceVariation().mantissa();
    auto pxvar_exponent = def.maxPriceVariation().exponent();
    auto securityID = def.securityID();
    auto securityUpdateAction = def.securityUpdateAction();
    auto mDSecurityTradingStatus = def.mDSecurityTradingStatus();
    auto securityGroup = def.securityGroup();
    auto marketSegmentID = def.marketSegmentID();

    auto matchAlgorithm = def.matchAlgorithm();
    auto minPriceIncrement_mantissa = def.minPriceIncrement().mantissa();
    auto minPriceIncrement_exponent = def.minPriceIncrement().exponent();
    auto priceDisplayFormat = def.priceDisplayFormat();
    auto userDefinedInstrument = def.userDefinedInstrument();

    auto dispFactor_mantissa = def.displayFactor().mantissa();
    auto dispFactor_exponent = def.displayFactor().exponent();

    auto subFraction = def.subFraction();
    auto securityType = def.securityType();
    auto maturityMonth = def.maturityMonthYear().month();
    auto maturityYear = def.maturityMonthYear().year();
    auto openInterestQty = def.openInterestQty();
    auto clearedVolume = def.clearedVolume();
    auto tradingRefDate = def.tradingReferenceDate();

    auto unitOfMeasure = def.unitOfMeasure();
    auto unitOfMeasureQty_mantissa = def.unitOfMeasureQty().mantissa();
    auto unitOfMeasureQty_exponent = def.unitOfMeasureQty().exponent();

    uint64_t activation = 0, expiration = 0;
    auto events = def.noEvents();
    while (events.hasNext())
    {
      events.next();
      auto evtime = events.eventTime();
      auto evid = events.eventType();
      if (evid == 5)
      {
        activation = evtime;
      }
      else if (evid == 7)
      {
        expiration = evtime;
      }
      else
        SNGH;
    }

    cb->MDInstrumentDefinitionFuture(
        MsgSeqNum,
        SendingTime,
        sym,
        asset,
        cFICode,
        high_limit_px_mantissa,
        high_limit_px_exponent,
        low_limit_px_mantissa,
        low_limit_px_exponent,
        pxvar_mantissa,
        pxvar_exponent,
        securityID,
        securityUpdateAction,
        mDSecurityTradingStatus,
        activation,
        expiration,
        securityGroup,
        marketSegmentID,
        matchAlgorithm,
        mainFraction,
        minPriceIncrement_mantissa,
        minPriceIncrement_exponent,
        priceDisplayFormat,
        userDefinedInstrument,

        dispFactor_mantissa,
        dispFactor_exponent,
        subFraction,
        securityType,
        maturityMonth,
        maturityYear,

        0, // issue date
        0, // maturity date

        openInterestQty,
        clearedVolume,
        tradingRefDate,
        unitOfMeasure,
        unitOfMeasureQty_mantissa,
        unitOfMeasureQty_exponent

        );

    return securityID;
  }

  static int32_t
  decode_MDInstrumentDefinitionFixedIncome57(
      [[maybe_unused]] uint64_t ts,
      uint32_t MsgSeqNum,
      uint64_t SendingTime,
      sbe::MDInstrumentDefinitionFixedIncome57 &def,
      mdp3::feed_handler_if *cb)
  {
    auto sym = def.symbol();
    auto asset = def.asset();
    auto cFICode = def.cFICode();

    auto high_limit_px_mantissa = def.highLimitPrice().mantissa();
    auto high_limit_px_exponent = def.highLimitPrice().exponent();
    auto low_limit_px_mantissa = def.lowLimitPrice().mantissa();
    auto low_limit_px_exponent = def.lowLimitPrice().exponent();
    auto mainFraction = def.mainFraction();

    auto pxvar_mantissa = def.maxPriceVariation().mantissa();
    auto pxvar_exponent = def.maxPriceVariation().exponent();
    auto securityID = def.securityID();
    auto securityUpdateAction = def.securityUpdateAction();
    auto mDSecurityTradingStatus = def.mDSecurityTradingStatus();
    auto securityGroup = def.securityGroup();
    auto marketSegmentID = def.marketSegmentID();

    auto matchAlgorithm = def.matchAlgorithm();
    auto minPriceIncrement_mantissa = def.minPriceIncrement().mantissa();
    auto minPriceIncrement_exponent = def.minPriceIncrement().exponent();
    auto priceDisplayFormat = def.priceDisplayFormat();
    auto userDefinedInstrument = def.userDefinedInstrument();

    auto dispFactor_mantissa = def.displayFactor().mantissa();
    auto dispFactor_exponent = def.displayFactor().exponent();

    auto subFraction = def.subFraction();
    auto securityType = def.securityType();
    auto maturityDate = def.maturityDate();
    auto issueDate = def.issueDate();
    auto maturityMonth = 0;
    auto maturityYear = 0;
    auto openInterestQty = 0;
    auto clearedVolume = 0;
    auto tradingRefDate = def.tradingReferenceDate();

    auto unitOfMeasure = def.unitOfMeasure();
    auto unitOfMeasureQty_mantissa = def.unitOfMeasureQty().mantissa();
    auto unitOfMeasureQty_exponent = def.unitOfMeasureQty().exponent();

    uint64_t activation = 0, expiration = 0;
    auto events = def.noEvents();
    while (events.hasNext())
    {
      events.next();
      auto evtime = events.eventTime();
      auto evid = events.eventType();
      if (evid == 5)
      {
        activation = evtime;
      }
      else if (evid == 7)
      {
        expiration = evtime;
      }
      else
        SNGH;
    }

    cb->MDInstrumentDefinitionFuture(
        MsgSeqNum,
        SendingTime,
        sym,
        asset,
        cFICode,
        high_limit_px_mantissa,
        high_limit_px_exponent,
        low_limit_px_mantissa,
        low_limit_px_exponent,
        pxvar_mantissa,
        pxvar_exponent,
        securityID,
        securityUpdateAction,
        mDSecurityTradingStatus,
        activation,
        expiration,
        securityGroup,
        marketSegmentID,
        matchAlgorithm,
        mainFraction,
        minPriceIncrement_mantissa,
        minPriceIncrement_exponent,
        priceDisplayFormat,
        userDefinedInstrument,
        dispFactor_mantissa,
        dispFactor_exponent,
        subFraction,
        securityType,
        maturityMonth,
        maturityYear,
        issueDate,
        maturityDate,
        openInterestQty,
        clearedVolume,
        tradingRefDate,
        unitOfMeasure,
        unitOfMeasureQty_mantissa,
        unitOfMeasureQty_exponent

        );

    return securityID;
  }


  static void
  decode_ChannelReset4(
      [[maybe_unused]]uint64_t ts,
      uint32_t MsgSeqNum,
      uint64_t SendingTime,
      sbe::ChannelReset4 &reset,
      mdp3::feed_handler_if *cb)
  {
    auto txtim = reset.transactTime();
    auto mdentries = reset.noMDEntries();
    while (mdentries.hasNext())
    {
      mdentries.next();
      auto entry_type = mdentries.mDEntryType();
      cb->ChannelReset(
          MsgSeqNum,
          txtim,
          SendingTime,
          entry_type);
    }
  }

  static void
  decode_MDIncrementalRefreshLimitsBanding50(
      [[maybe_unused]]uint64_t ts,
      uint32_t MsgSeqNum,
      uint64_t SendingTime,
      sbe::MDIncrementalRefreshLimitsBanding50 &limits,
      mdp3::feed_handler_if *cb)
  {

    auto txtim = limits.transactTime();
    auto mdentries = limits.noMDEntries();
    while (mdentries.hasNext())
    {
      mdentries.next();
      auto secid = mdentries.securityID();
      auto pxh_mantissa = mdentries.highLimitPrice().mantissa();
      auto pxh_exponent = mdentries.highLimitPrice().exponent();
      auto pxl_mantissa = mdentries.lowLimitPrice().mantissa();
      auto pxl_exponent = mdentries.lowLimitPrice().exponent();
      auto pxvar_mantissa = mdentries.maxPriceVariation().mantissa();
      auto pxvar_exponent = mdentries.maxPriceVariation().exponent();
      auto entry_type = mdentries.mDEntryType();
      cb->MDIncrementalRefreshLimitsBanding(
          MsgSeqNum,
          txtim,
          SendingTime,
          secid,
          pxh_mantissa,
          pxh_exponent,
          pxl_mantissa,
          pxl_exponent,
          pxvar_mantissa,
          pxvar_exponent,
          entry_type);
    }
  }

  static void
  decode_SecurityStatus30(
      [[maybe_unused]] uint64_t ts,
      uint32_t MsgSeqNum,
      uint64_t SendingTime,
      sbe::SecurityStatus30 &status,
      mdp3::feed_handler_if *cb)
  {
    auto txtim = status.transactTime();
    auto secid = status.securityID();
    auto haltreason = status.haltReason();
    auto tradingstaus = status.securityTradingStatus();
    auto tradingevent = status.securityTradingEvent();

    cb->SecurityStatus(
        MsgSeqNum,
        txtim,
        SendingTime,
        secid,
        haltreason,
        tradingstaus,
        tradingevent);
  }

  // MBO
  static void
  decode_MDIncrementalRefreshOrderBook47(
      uint64_t ts,
      uint32_t MsgSeqNum,
      uint64_t SendingTime,
      sbe::MDIncrementalRefreshOrderBook47 &incr,
      mdp3::feed_handler_if *cb)
  {
    auto txtim = incr.transactTime();
    auto matchevent = incr.matchEventIndicator();

    auto noMdEntries = incr.noMDEntries();

    while (noMdEntries.hasNext())
    {
      noMdEntries.next();
      auto dispq = noMdEntries.mDDisplayQty();
      auto oid = noMdEntries.orderID();
      auto act = noMdEntries.mDUpdateAction();
      auto side = noMdEntries.mDEntryType();
      auto prio = noMdEntries.mDOrderPriority();
      auto px_mantissa = noMdEntries.mDEntryPx().mantissa();
      auto px_exponent = noMdEntries.mDEntryPx().exponent();
      auto secid = noMdEntries.securityID();

      cb->MDIncrementalRefreshBook(
          ts,
          MsgSeqNum,
          txtim,
          SendingTime,
          secid,
          px_mantissa,
          px_exponent,
          side,
          dispq,
          oid,
          act,
          prio,
          matchevent.lastQuoteMsg(),
          matchevent.endOfEvent(),
          matchevent.recoveryMsg());
    }
  }

  static void
  decode_MDIncrementalRefreshVolume37(
      [[maybe_unused]]uint64_t ts,
      uint32_t MsgSeqNum,
      uint64_t SendingTime,
      sbe::MDIncrementalRefreshVolume37 &vol,
      mdp3::feed_handler_if *cb)
  {
    auto txtim = vol.transactTime();

    auto noMdEntries = vol.noMDEntries();

    while (noMdEntries.hasNext())
    {
      noMdEntries.next();
      auto vol = noMdEntries.mDEntrySize();
      auto secid = noMdEntries.securityID();
      auto typ = noMdEntries.mDEntryType();
      auto act = noMdEntries.mDUpdateAction();

      cb->MDIncrementalRefreshVolume(
          MsgSeqNum,
          txtim,
          SendingTime,
          secid,
          vol,
          *typ,
          act);
    }
  }

  static void
  decode_QuoteRequest39(
      [[maybe_unused]] uint64_t ts,
      [[maybe_unused]] uint32_t MsgSeqNum,
      [[maybe_unused]] uint64_t SendingTime,
      [[maybe_unused]] sbe::QuoteRequest39 &rfq,
      [[maybe_unused]] mdp3::feed_handler_if *cb)
  {
#if defined(DEBUG) || defined(_DEBUG)
    auto rid = rfq.quoteReqID();
    log_dbg("received rfq id: %d", rid);
#endif

    auto noRelatedSym = rfq.noRelatedSym();
    while (noRelatedSym.hasNext())
    {
      noRelatedSym.next();
#if defined(DEBUG) || defined(_DEBUG)
      auto sec_id = noRelatedSym.securityID();
      auto orderqty = noRelatedSym.orderQty();
      auto quotetype = noRelatedSym.quoteType();
      auto side = noRelatedSym.side();
      log_dbg("sec_id: %d, orderqty: %d, qtype: %d, side: %d",
              sec_id,
              orderqty,
              int(quotetype),
              int(side));
#endif
    }
  }

}
