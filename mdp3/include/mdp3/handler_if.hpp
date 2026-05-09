#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <cstdio>
#include <functional>
#include <type_traits>
#include "mdp3/mbo_if.hpp"
#include "actors/Actor.hpp"
#include "logger/act/Logger.hpp"
#include "frame/ref/RefData.hpp"
#include "chutil/Time.hpp"
#include "bfile/r_l3.hpp"
#include "boost/lexical_cast.hpp"
#include <vector>
#include "oogsl/gvector.hpp"
#include "frame/mda/msg/Data.hpp"

#include <boost/unordered/unordered_flat_map.hpp>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-truncation"

#define BOOKSEND(msgptr) do { \
  if constexpr (UseFastSend) { \
    book->fast_send(msgptr, nullptr); \
  } else { \
    using _MsgT = std::remove_pointer_t<decltype(msgptr)>; \
    auto heap_msg = new _MsgT(*msgptr); \
    book->send(heap_msg, nullptr); \
  } \
} while(0)

template<bool UseFastSend = false, bool TreasOnly = false>
struct handler_if : public mdp3::feed_handler_if
{

  const char* get_name() const { return name; }

  char name[256];
  actor_ptr binrec = 0;
  std::vector<actor_ptr> mbo_order_books;  // Indexed by asset_id for MBO messages (futures - OB.cpp/TachBook)
  std::vector<double> latency, cmelatency;
  std::set<uint32_t> instruments;
  uint32_t max_mbp_level=1000;
  bool disable_mbo_ = false;
  bool filter_mbp_ = true;  // MBP (options) not used — always skip
#define SKIP 100000
  int ltcskip = SKIP;

  std::map<int32_t, std::tuple<double, double>> secid_to_bbbo;
  boost::unordered_flat_map<int32_t, int32_t> securityid_to_asset_id;
  boost::unordered_flat_map<uint64_t, int32_t> orderid_to_securityid;


  uint32_t chan;
  bool print_latency = false;

  int32_t errcnt = 100;
  en::x xchg;


  handler_if(
    en::x x,
    uint32_t chan,
    bool print_latency = false)
    : chan(chan), xchg(x),
    print_latency(print_latency)
  {
    snprintf(name, sizeof(name), "handler_if_%u", chan);
  }

  virtual void set_max_mbp_level(uint32_t level) noexcept override
  {
    max_mbp_level = level;
  }

  virtual void disable_mbo(bool disable) noexcept override
  {
      disable_mbo_ = disable;
  }


  /**
   * Note on implied orders markets
   *
   * https://www.cmegroup.com/confluence/display/EPICSANDBOX/Implied+Orders+-+Examples
   *
   * An implied order is a spread order created based on orders available in the outright market ('IN')
   * or an outright order created based on orders available in the spread market ('OUT').
   * CME Globex creates Implied IN/OUT orders in outright and spread markets simultaneously
   * without the risk to the trader/broker of being double filled or filled on one leg and not on the other leg.
   * By conjoining the spread and outright markets, implied functionality substantially increases market liquidity.
   *
   * More about spreads:
   * https://www.cmegroup.com/education/courses/understanding-futures-spreads/futures-spread-overview.html
   *
   */

  virtual void MDInstrumentDefinitionSpread(
      [[maybe_unused]] uint32_t msgSeqNum,
      [[maybe_unused]] uint64_t sendingTime,
      char *sym,
      char *asset,
      char *cfiCode,
      int64_t high_limit_px_mantissa,
      int8_t high_limit_px_exponent,
      int64_t low_limit_px_mantissa,
      int8_t low_limit_px_exponent,
      int32_t securityID,
      char securityUpdateAction,
      uint64_t activation,
      uint64_t expiration,
      char *securityGroup,
      uint8_t marketSegmentID,
      uint8_t mDSecurityTradingStatus,
      char matchAlgorithm,
      uint8_t mainFraction,
      int64_t minPriceIncrement_mantissa,
      uint8_t minPriceIncrement_exponent,
      uint8_t priceDisplayFormat,
      char userDefinedInstrument,
      int8_t tickRule,
      uint8_t noLegsCount,
      int32_t legOptionDelta_mantissa[8],
      int8_t legOptionDelta_exponent[8],
      int64_t legPrice_mantissa[8],
      int8_t legPrice_exponent[8],
      int8_t legRatioQty[8],
      int32_t legSecurityID[8],
      uint8_t legSide[8],

      int64_t dispFactor_mantissa,
      int8_t dispFactor_exponent,
      uint8_t subFraction,
      char *securityType,
      uint8_t maturityMont,
      uint16_t maturityYear,
      int32_t openInterestQty,
      int32_t clearedVolume,
      uint16_t tradingRefDate,
      char *unitOfMeasure

      ) noexcept override
  {
    log_inf("SDF RECEIVED: sym=%s securityID=%d updateAction=%c", std::string(sym), securityID, securityUpdateAction);

    // Symbol filtering: check symbol prefix

    // log_inf("got sec info for spread id: %d : %s %s %s", securityID, std::string(sym), std::string(asset), std::string(securityGroup));

    bfile::l3_sdf_t l3;
    memset(&l3, 0, sizeof(l3));

    l3.typ = en::l3::SDF;
    l3.venue = xchg;
    strncpy(l3.sym, sym, sizeof(l3.sym));
    strncpy(l3.asset, asset, sizeof(l3.asset));
    strncpy(l3.cfiCode, cfiCode, sizeof(l3.cfiCode));

    l3.high_limit_px = to_price(high_limit_px_mantissa, high_limit_px_exponent);
    l3.low_limit_px = to_price(low_limit_px_mantissa, low_limit_px_exponent);
    l3.securityID = securityID;
    l3.activation = activation;
    l3.expiration = expiration;
    l3.securityUpdateAction = securityUpdateAction;
    strncpy(l3.sec_group, securityGroup, sizeof(l3.sec_group));
    l3.marketSegmentID = marketSegmentID;
    l3.matchAlgorithm = matchAlgorithm;
    l3.mainFraction = mainFraction;
    l3.minPriceIncrement = to_price(minPriceIncrement_mantissa, minPriceIncrement_exponent);
    l3.priceDisplayFormat = priceDisplayFormat;
    l3.userDefinedInstrument = userDefinedInstrument;
    l3.tickRule = tickRule;
    l3.mDSecurityTradingStatus = mDSecurityTradingStatus;

    l3.dispFactor = to_price(dispFactor_mantissa, dispFactor_exponent);
    l3.subFraction = subFraction;
    strncpy(l3.securityType, securityType, sizeof(l3.securityType));
    l3.maturityMont = maturityMont;
    l3.maturityYear = maturityYear;
    l3.openInterestQty = openInterestQty;
    l3.clearedVolume = clearedVolume;
    l3.tradingRefDate = tradingRefDate;
    strncpy(l3.unitOfMeasure, unitOfMeasure, sizeof(l3.unitOfMeasure));

    l3.noLegsCount = noLegsCount;

    for (std::size_t i = 0; i < std::min(uint8_t(8), noLegsCount); i++)
    {

      l3.legOptionDelta[i] = to_price(legOptionDelta_mantissa[i], legOptionDelta_exponent[i]);
      l3.legPrice[i] = to_price(legPrice_mantissa[i], legPrice_exponent[i]);
      l3.legRatioQty[i] = legRatioQty[i];
      l3.legSecurityID[i] = legSecurityID[i];
      l3.legSide[i] = legSide[i];

      // log_dbg("secid: %d, leg: %d, side: %d, id: %d", securityID, i, int(legSide[i]), legSecurityID[i]);
    }

    if (securityUpdateAction == 'A' || securityUpdateAction == 'M')
    {
      instruments.insert(securityID);
      log_dbg("added sym: %s, asset: %s, secid: %d, group: %s, activation: %s, expiration: %s, marketsegmentid: %d",
              std::string(l3.sym), std::string(l3.asset), l3.securityID, std::string(l3.sec_group),
              chutil::Time::from_epoch(l3.activation).to_string(),
              chutil::Time::from_epoch(l3.expiration).to_string(),
              int(l3.marketSegmentID));
    }
    else if (securityUpdateAction == 'D')
    {
    }
    else
      ERR("securityUpdateAction");

    if (binrec)
    {
      auto recmsg = new frame::mda::msg::Data();
      recmsg->l3 = l3;
      binrec->send(recmsg, 0);
      log_inf("SDF WRITTEN TO BINREC: sym=%s securityID=%d", std::string(sym), securityID);
    }
  }

  virtual void MDInstrumentDefinitionOption(
      [[maybe_unused]] uint32_t msgSeqNum,
      [[maybe_unused]] uint64_t sendingTime,
      char *sym,
      char *asset,
      char *cfiCode,
      int64_t high_limit_px_mantissa,
      int8_t high_limit_px_exponent,
      int64_t low_limit_px_mantissa,
      int8_t low_limit_px_exponent,
      int32_t securityID,
      char securityUpdateAction,
      uint64_t activation,
      uint64_t expiration,
      char *securityGroup,
      uint8_t marketSegmentID,
      uint8_t mDSecurityTradingStatus,
      char matchAlgorithm,
      uint8_t mainFraction,
      int64_t minPriceIncrement_mantissa,
      uint8_t minPriceIncrement_exponent,
      uint8_t priceDisplayFormat,
      int64_t minCabPrice_mantissa,
      uint8_t minCabPrice_exponent,
      uint8_t putOrCall,
      int64_t strikePrice_mantissa,
      uint8_t strikePrice_exponent,
      char userDefinedInstrument,
      int8_t tickRule,
      uint8_t noUnderlyingsCount,

      uint32_t underlyingSecurityID[4],
      std::string underlyingSymbol[4],

      int64_t dispFactor_mantissa,
      int8_t dispFactor_exponent,
      uint8_t subFraction,
      char *securityType,
      uint8_t maturityMont,
      uint16_t maturityYear,
      int32_t openInterestQty,
      int32_t clearedVolume,
      uint16_t tradingRefDate,
      char *unitOfMeasure,

      int64_t unitOfMeasureQty_mantissa,
      uint8_t unitOfMeasureQty_exponent

      ) noexcept override
  {
    log_inf("ODF RECEIVED: sym=%s securityID=%d updateAction=%c", std::string(sym), securityID, securityUpdateAction);

    // log_inf("got sec info for option id: %d : %s %s %s", securityID, std::string(sym), std::string(asset), std::string(securityGroup));

    bfile::l3_odf_t l3;
    memset(&l3, 0, sizeof(l3));
    l3.typ = en::l3::ODF;
    l3.venue = xchg;

    strncpy(l3.sym, sym, sizeof(l3.sym));
    strncpy(l3.asset, asset, sizeof(l3.asset));
    strncpy(l3.cfiCode, cfiCode, sizeof(l3.cfiCode));

    l3.high_limit_px = to_price(high_limit_px_mantissa, high_limit_px_exponent);
    l3.low_limit_px = to_price(low_limit_px_mantissa, low_limit_px_exponent);
    l3.securityID = securityID;
    l3.activation = activation;
    l3.expiration = expiration;
    l3.updateAction = securityUpdateAction;
    strncpy(l3.sec_group, securityGroup, sizeof(l3.sec_group));
    l3.marketSegmentID = marketSegmentID;
    l3.matchAlgorithm = matchAlgorithm;
    l3.mainFraction = mainFraction;
    l3.minPriceIncrement = to_price(minPriceIncrement_mantissa, minPriceIncrement_exponent);
    l3.priceDisplayFormat = priceDisplayFormat;
    l3.userDefinedInstrument = userDefinedInstrument;
    l3.tickRule = tickRule;
    l3.mDSecurityTradingStatus = mDSecurityTradingStatus;

    l3.minCabPrice = to_price(minCabPrice_mantissa, minCabPrice_exponent);

    l3.putOrCall = putOrCall;
    l3.strikePrice = to_price(strikePrice_mantissa, strikePrice_exponent);

    l3.dispFactor = to_price(dispFactor_mantissa, dispFactor_exponent);
    l3.subFraction = subFraction;
    strncpy(l3.securityType, securityType, sizeof(l3.securityType));
    l3.maturityMont = maturityMont;
    l3.maturityYear = maturityYear;
    l3.openInterestQty = openInterestQty;
    l3.clearedVolume = clearedVolume;
    l3.tradingRefDate = tradingRefDate;
    strncpy(l3.unitOfMeasure, unitOfMeasure, sizeof(l3.unitOfMeasure));
    l3.unitOfMeasureQty = to_price(unitOfMeasureQty_mantissa, unitOfMeasureQty_exponent);

    l3.noUnderlyingsCount = noUnderlyingsCount;
    for (std::size_t i = 0; i < std::min(uint8_t(4), noUnderlyingsCount); i++)
    {
      l3.underlyingSecurityID[i] = underlyingSecurityID[i];
    }

    char __cfiCode[10];
    memset(__cfiCode, 0, 10);
    strncpy(__cfiCode, l3.cfiCode, 6);

    if (securityUpdateAction == 'A' || securityUpdateAction == 'M')
    {
      instruments.insert(securityID);
      log_dbg("added sym: %s, asset: %s, secid: %d, group: %s, activation: %s, expiration: %s, marketsegmentid: %d, strike_price_mantissa: %d, strike_price_exponent: %d",
              std::string(l3.sym), std::string(l3.asset), l3.securityID, std::string(l3.sec_group),
              chutil::Time::from_epoch(l3.activation).to_string(),
              chutil::Time::from_epoch(l3.expiration).to_string(),
              int(l3.marketSegmentID),
              strikePrice_mantissa, int(strikePrice_exponent)
              );
      auto a = frame::ref::RefData::inst().get_asset(std::string(l3.sym));
      if (a)
      {
        auto &a_ = const_cast<frame::ref::Asset &>(*a);
        auto &r = const_cast<frame::ref::RefData&>(frame::ref::RefData::inst());
        r.update_sec_id(securityID, &a_);
        std::cerr << "mapped securityID: " << securityID << " to asset id: " << a_.id << " name: " << a_.name << std::endl;
        securityid_to_asset_id[securityID] = a_.id;
        //a_.cme_sec_id = securityID;
        a_.cfi_code = __cfiCode;
        a_.security_group = std::string(l3.sec_group);
        double unit = l3.minPriceIncrement;
        a_.set_units(unit);
        a_.bo_spread = 1;
        if constexpr (TreasOnly) {
          a_.maxpx = int(180 / unit);
        }
        log_inf("updated asset: %s, secid: %d, cfi: %s, group: %s, unit: %f, bospread: %d, subfraction: %d, mainfraction: %d, minpriceincrement: %f",
                a_.name, a_.sec_id, std::string(a_.cfi_code), a_.security_group, a_.get_units(), a_.bo_spread, int(l3.subFraction), int(l3.mainFraction), l3.minPriceIncrement);
        ASSERT(a_.name == std::string(l3.sym), "sym mismatch");
        for (std::size_t i = 0; i < std::min(uint8_t(4), noUnderlyingsCount); i++)
        {
          log_inf("secid: %d, cnt: %d, underlyingid: %d, underlyingsym: %s", securityID, i, underlyingSecurityID[i], underlyingSymbol[i]);
        }

      }
    }
    else if (securityUpdateAction == 'D')
    {
    }
    else
    {
      ERR("securityUpdateAction");
    }

    if (binrec)
    {
      auto recmsg = new frame::mda::msg::Data();
      recmsg->l3 = l3;
      binrec->send(recmsg, 0);
      log_inf("ODF WRITTEN TO BINREC: sym=%s securityID=%d", std::string(sym), securityID);
    }
  }

  virtual void MDInstrumentDefinitionFuture(
      [[maybe_unused]] uint32_t msgSeqNum,
      [[maybe_unused]] uint64_t sendingTime,
      char *sym,
      char *asset,
      char *cfiCode,
      int64_t high_limit_px_mantissa,
      int8_t high_limit_px_exponent,
      int64_t low_limit_px_mantissa,
      int8_t low_limit_px_exponent,
      int64_t pxvar_mantissa,
      int8_t pxvar_exponent,
      int32_t securityID,
      char updateAction,
      uint8_t mDSecurityTradingStatus,
      uint64_t activation,
      uint64_t expiration,
      char *securityGroup,
      uint8_t marketSegmentID,
      char matchAlgorithm,
      uint8_t mainFraction,
      int64_t minPriceIncrement_mantissa,
      uint8_t minPriceIncrement_exponent,
      uint8_t priceDisplayFormat,
      char userDefinedInstrument,

      int64_t dispFactor_mantissa,
      int8_t dispFactor_exponent,
      uint8_t subFraction,
      char *securityType,
      uint8_t maturityMont,
      uint16_t maturityYear,
      uint16_t issueDate,
      uint16_t maturityDate,
      int32_t openInterestQty,
      int32_t clearedVolume,
      uint16_t tradingRefDate,
      char *unitOfMeasure,

      int64_t unitOfMeasureQty_mantissa,
      uint8_t unitOfMeasureQty_exponent

      ) noexcept override
  {
    log_inf("FDF RECEIVED: sym=%s securityID=%d updateAction=%c", std::string(sym), securityID, updateAction);

    // Symbol filtering: check symbol prefix

    bfile::l3_fdf_t l3;
    memset(&l3, 0, sizeof(l3));
    l3.typ = en::l3::FDF;
    l3.venue = xchg;

    strncpy(l3.sym, sym, sizeof(l3.sym));
    strncpy(l3.asset, asset, sizeof(l3.asset));

    std::string sym_to_lookup;

    if constexpr (TreasOnly)
    {
      sym_to_lookup = std::string(asset);
    }
    else
    {
      sym_to_lookup = std::string(sym);
    }

    strncpy(l3.cfiCode, cfiCode, sizeof(l3.cfiCode));
    l3.high_limit_px = to_price(high_limit_px_mantissa, high_limit_px_exponent);
    l3.low_limit_px = to_price(low_limit_px_mantissa, low_limit_px_exponent);
    l3.securityID = securityID;
    l3.mDSecurityTradingStatus = mDSecurityTradingStatus;
    l3.activation = activation;
    l3.expiration = expiration;
    l3.updateAction = updateAction;
    strncpy(l3.securityGroup, securityGroup, sizeof(l3.securityGroup));
    l3.marketSegmentID = marketSegmentID;
    l3.matchAlgorithm = matchAlgorithm;
    l3.mainFraction = mainFraction;
    l3.minPriceIncrement = to_price(minPriceIncrement_mantissa, minPriceIncrement_exponent);
    l3.priceDisplayFormat = priceDisplayFormat;
    l3.userDefinedInstrument = userDefinedInstrument;
    l3.pxvar = to_price(pxvar_mantissa, pxvar_exponent);
    l3.dispFactor = to_price(dispFactor_mantissa, dispFactor_exponent);
    l3.subFraction = subFraction;
    strncpy(l3.securityType, securityType, sizeof(l3.securityType));
    l3.maturityMont = maturityMont;
    l3.maturityYear = maturityYear;
    l3.openInterestQty = openInterestQty;
    l3.clearedVolume = clearedVolume;
    l3.tradingRefDate = tradingRefDate;
    strncpy(l3.unitOfMeasure, unitOfMeasure, sizeof(l3.unitOfMeasure));
    l3.unitOfMeasureQty = to_price(unitOfMeasureQty_mantissa, unitOfMeasureQty_exponent);

    char __cfiCode[10];
    memset(__cfiCode, 0, 10);
    strncpy(__cfiCode, l3.cfiCode, 6);

    if (updateAction == 'A' || updateAction == 'M')
    {
      instruments.insert(securityID);
      log_inf("fdf add/mod for sym: %s, asset: %s, secid: %d, group: %s, activation: %s, expiration: %s, marketsegmentid: %d, issueDate: %d, maturityDate: %d",
              std::string(sym_to_lookup), std::string(l3.asset), l3.securityID, std::string(l3.securityGroup),
              chutil::Time::from_epoch(l3.activation).to_string(),
              chutil::Time::from_epoch(l3.expiration).to_string(),
              int(l3.marketSegmentID),
              issueDate, maturityDate
              );

      auto a = frame::ref::RefData::inst().get_asset(std::string(sym_to_lookup));
      if (a)
      {
        auto &a_ = const_cast<frame::ref::Asset &>(*a);
        auto &r = const_cast<frame::ref::RefData&>(frame::ref::RefData::inst());
        std::cerr << "adding mapping securityID: " << securityID << " to asset id: " << a_.id << " name: " << a_.name << std::endl;
        securityid_to_asset_id[securityID] = a_.id;

        if (a_.sec_id != 0)
        {
          log_inf("we have already updated this asset sym: %s", sym_to_lookup);
          if (a_.cme_activation > l3.activation)
          {
            log_inf("activation is older, skipping, current activation: %d, incoming activation: %d", a_.cme_activation, l3.activation);
            return;
          }
          else
          {
            log_inf("activation is newer, updating, current activation: %d, incoming activation: %d", a_.cme_activation, l3.activation);
          }
        }
        r.update_sec_id(l3.securityID, &a_);
        //a_.cme_sec_id = securityID;
        a_.cfi_code = __cfiCode;
        a_.cme_activation = l3.activation;
        a_.security_group = std::string(l3.securityGroup);
        double unit = l3.minPriceIncrement;
        a_.set_units(unit);
        if constexpr (TreasOnly) {
          a_.maxpx = int(180 / unit);
        }
        a_.bo_spread = 1;
        log_inf("updated asset: %s, secid: %d, cfi: %s, group: %s, unit: %f, bospread: %d, subfraction: %d, mainfraction: %d, minpriceincrement: %f",
                a_.name, a_.sec_id, std::string(a_.cfi_code), a_.security_group, a_.get_units(), a_.bo_spread, int(l3.subFraction), int(l3.mainFraction), l3.minPriceIncrement);
        ASSERT(a_.name == std::string(sym_to_lookup), "sym mismatch");

      }
    }
    else if (updateAction == 'D')
    {
    }
    else
      abort();

    if (binrec)
    {
      auto recmsg = new frame::mda::msg::Data();
      recmsg->l3 = l3;
      binrec->send(recmsg, 0);
      log_inf("FDF WRITTEN TO BINREC: sym=%s securityID=%d", sym_to_lookup, securityID);
    }
  }

  //
  // MBP order update
  //
  virtual void MDIncrementalRefreshBook(
      [[maybe_unused]] uint64_t recv_time,
      [[maybe_unused]] uint32_t msgSeqNum,
      [[maybe_unused]] uint64_t transactTime,
      [[maybe_unused]] uint64_t sendingTime,
      [[maybe_unused]] int32_t securityID,
      [[maybe_unused]] int64_t px_mantissa,
      [[maybe_unused]] int8_t px_exponent,
      [[maybe_unused]] char side,
      [[maybe_unused]] int32_t sz,
      [[maybe_unused]] int32_t numorders,
      [[maybe_unused]] uint8_t pxlevel,
      [[maybe_unused]] bool endOfEvent,
      [[maybe_unused]] bool recovery) noexcept override
  {
    if (filter_mbp_) return;
#ifdef mbp
    bfile::l3_mbp_t l3;
    memset(&l3, 0, sizeof(l3));
    l3.typ = en::l3::MBP;
    l3.venue = xchg;
    l3.transactTime = transactTime;
    l3.sendingTime = sendingTime;
    l3.handlerendtim = recv_time;
    l3.pxd = to_price(px_mantissa, px_exponent);
    l3.securityID = securityID;
    l3.side = side; // 0,1,E,F
    l3.sz = sz;
    l3.numorders = numorders;
    l3.pxlevel = pxlevel;
    l3.endOfEvent = endOfEvent;
    l3.recovery = recovery;

    // Route to mbp_order_books based on securityID lookup
    auto it = securityid_to_asset_id.find(securityID);
    if (it != securityid_to_asset_id.end()) [[likely]] {
      int32_t asset_id = it->second;
      if (asset_id >= 0 && static_cast<size_t>(asset_id) < mbp_order_books.size()) {
        const auto book = mbp_order_books[asset_id];
        if (book) [[likely]] {
          frame::mda::msg::Data stack_msg;
          stack_msg.l3 = l3;
          stack_msg.israw = true;
          BOOKSEND(&stack_msg);
        }
      }
    }

    if (binrec) {
      auto recmsg = new frame::mda::msg::Data();
      recmsg->l3 = l3;
      recmsg->israw = true;
      binrec->send(recmsg, 0);
    }
  #endif
  }

  //
  // MBO
  //
  virtual void MDIncrementalRefreshBook(
      uint64_t recv_time,
      [[maybe_unused]] uint32_t msgSeqNum,
      uint64_t transactTime,
      uint64_t sendingTime,
      int32_t securityID,
      int64_t px_mantissa,
      int8_t px_exponent,
      char side, // '0' = buy, '1' = sell or implied
      int32_t displayQty,
      uint64_t orderID,
      uint8_t orderUpdateAction,
      uint64_t priority,
      bool lastQuote,
      bool endOfEvent,
      bool recovery) noexcept override
  {

    if (disable_mbo_)
    {
      log_dbg("mbo disabled");
      return;
    }

    // log_dbg("got mbo");

    // Implied order book information is not sent in MBO format.

    auto recv_ts = recv_time;

    // std::cout << sendingTime << " " << recv_ts  << std::endl;
#ifdef CHECKTIME
    if (std::abs(int64_t(recv_time) - int64_t(sendingTime)) > 100LL * 1000LL * 1000LL)
    {
      if (errcnt-- < 0)
      {
        log_tim("sending time %d and current time %d are off by %d", sendingTime, recv_ts, (recv_ts - int64_t(sendingTime)));
        errcnt = 50000;
      }
    }
#endif
    // if (int64_t(sendingTime) > recv_ts)
    // {
    //   if (errcnt-- < 0)
    //   {
    //     log_tim("sending time %d > recv time %d diff %d", sendingTime, recv_ts, (recv_ts - int64_t(sendingTime)));
    //     errcnt = 50000;
    //   }
    // }

    if (print_latency) [[unlikely]]
    {
      if (recovery)
        ltcskip = SKIP;
      if (recv_time > 0 && !recovery && ltcskip-- < 0)
      {
        latency.push_back(recv_ts - recv_time);
        cmelatency.push_back(recv_ts - sendingTime);
        if (latency.size() > 100000)
        {
          PrintStats();
          latency.clear();
          cmelatency.clear();
        }
      }
    }

    double px = to_price(px_mantissa, px_exponent);

    bfile::l3_mbo_v2_t l3;

    memset(&l3, 0, sizeof(l3));
    l3.typ = en::l3::MBO_V2;
    l3.venue = xchg;

    l3.transactTime = transactTime;
    l3.sendingTime = sendingTime;
    l3.handlerendtim = recv_time;
    l3.orderUpdateAction = orderUpdateAction;

    l3.displayQty = displayQty;
    l3.orderID = orderID;
    l3.priority = priority;
    l3.pxd = px;
    l3.securityID = securityID;

    if (orderUpdateAction == 0) { // New
      orderid_to_securityid[orderID] = securityID;
    } else if (orderUpdateAction == 2) { // Delete
      orderid_to_securityid.erase(orderID);
    }

    l3.side = side;
    l3.lastQuote = lastQuote;
    l3.endOfEvent = endOfEvent;
    l3.recovery = recovery;

    l3.venue = xchg;

    // Record to binrec BEFORE routing to books
    if (binrec)
    {
      auto recmsg = new frame::mda::msg::Data();
      recmsg->l3 = l3;
#ifdef DEBUG_REC
      log_inf("Recording MBO to binrec: handler=%s typ=%d secid=%d orderid=%llu action=%d",
              get_name(), (int)l3.typ, l3.securityID, l3.orderID, (int)l3.orderUpdateAction);
#endif
      binrec->send(recmsg, 0);
    }

    // Send to specific book based on securityID lookup
    auto it = securityid_to_asset_id.find(l3.securityID);
    if (it != securityid_to_asset_id.end()) [[likely]] {
      int32_t asset_id = it->second;
      if (asset_id >= 0 && static_cast<size_t>(asset_id) < mbo_order_books.size()) {
        const auto book = mbo_order_books[asset_id];
        if (book) [[likely]] {
          frame::mda::msg::Data msg;
          msg.l3 = l3;
          msg.israw = true;
          BOOKSEND(&msg);
        }
      }
    } else {
      return; // not in universe
      //ERR("securityID not found in securityid_to_asset_id map");
    }
  }

  //
  // MBP trade
  //
  virtual void MDIncrementalRefreshTradeSummary(
      [[maybe_unused]] uint64_t recv_time,
      [[maybe_unused]] uint32_t msgSeqNum,
      [[maybe_unused]] uint64_t transactTime,
      [[maybe_unused]] uint64_t sendingTime,
      [[maybe_unused]] int32_t securityID,
      [[maybe_unused]] int64_t px_mantissa,
      [[maybe_unused]] int8_t px_exponent,
      [[maybe_unused]] char side,
      [[maybe_unused]] uint8_t aggressor_side,
      [[maybe_unused]] int32_t sz,
      [[maybe_unused]] int32_t numorders,
      [[maybe_unused]] bool lastTrade,
      [[maybe_unused]] bool endOfEvent) noexcept override
  {
    if (filter_mbp_) return;

#ifdef mbp
    bfile::l3_mbp_trd_t l3;
    memset(&l3, 0, sizeof(l3));
    l3.typ = en::l3::MBPT;
    l3.venue = xchg;
    l3.transactTime = transactTime;
    l3.sendingTime = sendingTime;
    l3.handlerendtim = recv_time;
    l3.pxd = to_price(px_mantissa, px_exponent);

    l3.securityID = securityID;
    l3.side = side;
    l3.sz = sz;
    l3.numorders = numorders;
    l3.aggresorSide = aggressor_side;
    l3.lastTrade = lastTrade;
    l3.endOfEvent = endOfEvent;

    // Send to specific book based on securityID lookup
    auto it = securityid_to_asset_id.find(securityID);
    if (it != securityid_to_asset_id.end()) [[likely]] {
      int32_t asset_id = it->second;
      if (asset_id >= 0 && static_cast<size_t>(asset_id) < mbo_order_books.size()) {
        const auto book = mbo_order_books[asset_id];
        if (book) [[likely]] {
          frame::mda::msg::Data stack_msg;
          stack_msg.l3 = l3;
          stack_msg.israw = true;
          BOOKSEND(&stack_msg);
        }
      }
    } else {
      ERR("securityID not found in securityid_to_asset_id map");
    }

    if (binrec)
    {
      auto recmsg = new frame::mda::msg::Data();
      recmsg->l3 = l3;
      recmsg->israw = true;
      binrec->send(recmsg, 0);
    }
  #endif
  }

  //
  // MBO trade
  //
  virtual void MDIncrementalRefreshTradeSummary(
      uint64_t recv_time,
      [[maybe_unused]] uint32_t msgSeqNum,
      uint64_t transactTime,
      uint64_t sendingTime,
      int32_t lastQty,
      uint64_t orderID,
      bool lastTrade,
      bool endOfEvent) noexcept override
  {

    if (disable_mbo_)
      return;

    bfile::l3_mbo_trd_v2_t l3;
    memset(&l3, 0, sizeof(l3));
    l3.typ = en::l3::MBOT_V2;
    l3.venue = xchg;
    l3.transactTime = transactTime;
    l3.sendingTime = sendingTime;
    l3.handlerendtim = recv_time;
    l3.lastQty = lastQty;
    l3.orderID = orderID;
    l3.endOfEvent = endOfEvent;
    l3.lastTrade = lastTrade;
    l3.venue = xchg;

    // Record to binrec BEFORE routing to books
    if (binrec)
    {
      auto recmsg = new frame::mda::msg::Data();
      recmsg->l3 = l3;
      binrec->send(recmsg, 0);
    }

    // Look up securityID from orderID, then send to specific book
    auto order_it = orderid_to_securityid.find(orderID);
    if (order_it != orderid_to_securityid.end()) [[likely]] {
      int32_t securityID = order_it->second;
      auto it = securityid_to_asset_id.find(securityID);
      if (it != securityid_to_asset_id.end()) [[likely]] {
        int32_t asset_id = it->second;
        if (asset_id >= 0 && static_cast<size_t>(asset_id) < mbo_order_books.size()) {
          const auto book = mbo_order_books[asset_id];
          if (book) [[likely]] {
            frame::mda::msg::Data msg;
            msg.l3 = l3;
            msg.israw = true;
            BOOKSEND(&msg);
          }
        }
      } else {
        return; // not in universe
        //ERR("securityID not found in securityid_to_asset_id map");
      }
    } else {
      return; // not in universe
      //ERR("orderID not found in orderid_to_securityid map");
    }
  }

  virtual void SnapshotFullRefreshOrderBook(
      uint64_t transactTime,
      uint64_t sendingTime,
      int32_t securityID,
      int32_t displayQty,
      int64_t px_mantissa,
      int64_t px_exponent,
      char side,
      uint64_t priority,
      uint64_t orderID) noexcept override
  {

    // 0=New 1=Change 2=Delete

    bfile::l3_mbo_v2_t l3;
    memset(&l3, 0, sizeof(l3));
    l3.typ = en::l3::MBO_V2;
    l3.venue = xchg;
    l3.transactTime = transactTime;
    l3.sendingTime = sendingTime;
    l3.handlerendtim = 0;
    l3.displayQty = displayQty;
    l3.orderID = orderID;
    l3.priority = priority;
    l3.pxd = to_price(px_mantissa, px_exponent);
    l3.securityID = securityID;
    l3.side = side;
    l3.recovery = true;
    l3.venue = xchg;

    // Update orderID to securityID mapping (snapshot orders are always added)
    orderid_to_securityid[orderID] = securityID;

    // Record to binrec BEFORE routing to books
    if (binrec)
    {
      auto recmsg = new frame::mda::msg::Data();
      recmsg->l3 = l3;
      binrec->send(recmsg, 0);
    }

    // Send to specific book based on securityID lookup
    auto it = securityid_to_asset_id.find(l3.securityID);
    if (it != securityid_to_asset_id.end()) [[likely]] {
      int32_t asset_id = it->second;
      if (asset_id >= 0 && static_cast<size_t>(asset_id) < mbo_order_books.size()) {
        const auto book = mbo_order_books[asset_id];
        if (book) [[likely]] {
          frame::mda::msg::Data msg;
          msg.l3 = l3;
          msg.israw = true;
          BOOKSEND(&msg);
        }
      }
    } else {
      return; // not in universe
      //ERRF(boost::format("securityID: %d not found in securityid_to_asset_id map") % l3.securityID);
    }
  }

  virtual void SnapshotFullRefreshOrderBook_NR(
      uint32_t last_seq_num,
      uint32_t numReports,
      uint32_t msgSeqNum,
      uint64_t transactTime,
      uint64_t sendingTime,
      uint32_t currentChunk,
      uint32_t numChunks,
      int32_t securityID,
      int32_t displayQty,
      int64_t px_mantissa,
      int64_t px_exponent,
      char side,
      uint64_t priority,
      uint64_t orderID) noexcept override
  {

    // 0=New 1=Change 2=Delete

    bfile::l3_mbo_snap_t l3;
    memset(&l3, 0, sizeof(l3));
    l3.typ = en::l3::MBOS;
    l3.venue = xchg;
    l3.transactTime = transactTime;
    l3.sendingTime = sendingTime;
    l3.handlerendtim = 0;
    l3.displayQty = displayQty;
    l3.orderID = orderID;
    l3.priority = priority;
    l3.pxd = to_price(px_mantissa, px_exponent);
    l3.securityID = securityID;
    l3.side = side;
    l3.numReports = numReports;
    l3.currentChunk = currentChunk;
    l3.numChunks = numChunks;
    l3.lastSeqNum = last_seq_num;
    l3.msgSeqNum = msgSeqNum;

    // do not send to order book directly

    if (binrec)
    {
      auto recmsg = new frame::mda::msg::Data();
      recmsg->l3 = l3;
      binrec->send(recmsg, 0);
    }
  }

  virtual void MDIncrementalRefreshLimitsBanding(
      [[maybe_unused]] uint32_t msgSeqNum,
      [[maybe_unused]] uint64_t transactTime,
      [[maybe_unused]] uint64_t sendingTime,
      int32_t securityID,
      int64_t pxh_mantissa,
      int8_t pxh_exponent,
      int64_t pxl_mantissa,
      int8_t pxl_exponent,
      int64_t pxvar_mantissa,
      int8_t pxvar_exponent,
      const char *entryType) noexcept override
  {
    auto pxh = to_price(pxh_mantissa, pxh_exponent);
    auto pxl = to_price(pxl_mantissa, pxl_exponent);
    auto pxv = to_price(pxvar_mantissa, pxvar_exponent);
    log_dbg("MDIncrementalRefreshLimitsBanding: securityID: %d, pxh: %f, pxl: %f, pxv: %f, ent: %s",
            securityID,
            pxh,
            pxl,
            pxv,
            std::string(entryType));

    bfile::l3_lim_t l3;
    memset(&l3, 0, sizeof(l3));
    l3.typ= en::l3::LIM;
    l3.venue = xchg;
    l3.securityID = securityID;
    l3.pxh = pxh;
    l3.pxl = pxl;
    l3.pxvar = pxv;
    strncpy(l3.entryType, entryType, sizeof(l3.entryType));

    if (binrec)
    {
      auto recmsg = new frame::mda::msg::Data();
      recmsg->l3 = l3;
      binrec->send(recmsg, 0);
    }
  }

  virtual void SecurityStatus(
      [[maybe_unused]] uint32_t msgSeqNum,
      uint64_t transactTime,
      uint64_t sendingTime,
      int32_t securityID,
      uint8_t haltReason,
      uint8_t tradingStatus,
      uint8_t tradingEvent) noexcept override
  {
    log_dbg("SecurityStatus securityID: %d, haltReason: %d, tradingStatus: %d, tradingEvent: %d",
            securityID, int(haltReason), int(tradingStatus), int(tradingEvent));

    bfile::l3_sst_t l3;
    memset(&l3, 0, sizeof(l3));
    l3.typ = en::l3::SST;
    l3.venue = xchg;
    l3.haltReason = haltReason;
    l3.securityID = securityID;
    l3.sendtim = sendingTime;
    l3.tradingEvent = tradingEvent;
    l3.tradingStatus = tradingStatus;
    l3.txtim = transactTime;

    if (binrec)
    {
      auto recmsg = new frame::mda::msg::Data();
      recmsg->l3 = l3;
      binrec->send(recmsg, 0);
    }
  }

  virtual void Gap() noexcept override
  {

    log_err("Gap");

    bfile::l3_gap_v2_t l3;
    memset(&l3, 0, sizeof(l3));
    l3.typ = en::l3::GAP_V2;
    l3.venue = xchg;

    // Send GAP to all mbo_order_books (futures)
    for (const auto book : mbo_order_books)
    {
      if (!book)
        continue;
      frame::mda::msg::Data msg;
      msg.l3 = l3;
      msg.israw = true;
      BOOKSEND(&msg);
    }

    if (binrec)
    {
      auto recmsg = new frame::mda::msg::Data();
      recmsg->l3 = l3;
      binrec->send(recmsg, 0);
    }
  }

  virtual void ChannelReset(
      [[maybe_unused]] uint32_t msgSeqNum,
      [[maybe_unused]] uint64_t transactTime,
      [[maybe_unused]] uint64_t sendingTime,
      const char *mdEntryType) noexcept override
  {
    log_err("ChannelReset %s", std::string(mdEntryType));

    bfile::l3_chr_v2_t l3;
    memset(&l3, 0, sizeof(l3));
    l3.typ = en::l3::CHR_V2;
    l3.venue = xchg;

    // Send CHR to all mbo_order_books (futures)
    for (const auto book : mbo_order_books)
    {
      if (!book)
        continue;
      frame::mda::msg::Data msg;
      msg.l3 = l3;
      msg.israw = true;
      BOOKSEND(&msg);
    }

    if (binrec)
    {
      auto recmsg = new frame::mda::msg::Data();
      recmsg->l3 = l3;
      binrec->send(recmsg, 0);
    }
  }

  virtual void MDIncrementalRefreshVolume(
      [[maybe_unused]] uint32_t msgSeqNum,
      uint64_t transactTime,
      uint64_t sendingTime,
      int32_t securityID,
      int32_t volume,
      char typ,
      uint8_t action) noexcept override
  {
    bfile::l3_vol_t l3;
    l3.typ = en::l3::VOL;
    l3.venue = xchg;
    l3.action = action;
    l3.securityID = securityID;
    l3.sendtim = sendingTime;
    l3.txtim = transactTime;
    l3.vtyp = typ;
    l3.vol = volume;

    if (binrec)
    {
      auto recmsg = new frame::mda::msg::Data();
      recmsg->l3 = l3;
      binrec->send(recmsg, 0);
    }
  }

  virtual void MDIncrementalRefreshSessionStatistics(
      [[maybe_unused]] uint32_t msgSeqNum,
      [[maybe_unused]] uint64_t transactTime,
      [[maybe_unused]] uint64_t sendingTime,
      [[maybe_unused]] uint32_t secid,
      [[maybe_unused]] uint8_t openCloseSettleFlag,
      [[maybe_unused]] int64_t px_mantissa,
      [[maybe_unused]] int64_t px_exponent,
      [[maybe_unused]] uint8_t updateAction,
      [[maybe_unused]] char entryType) noexcept override {}

  virtual void MDIncrementalRefreshDailyStatistics(
      [[maybe_unused]] uint32_t msgSeqNum,
      [[maybe_unused]] uint64_t transactTime,
      [[maybe_unused]] uint64_t sendingTime,
      [[maybe_unused]] uint32_t secid,
      [[maybe_unused]] int64_t px_mantissa,
      [[maybe_unused]] int8_t px_exponent,
      [[maybe_unused]] int32_t size,
      [[maybe_unused]] char side,
      [[maybe_unused]] bool finalDaily,
      [[maybe_unused]] bool intraday,
      [[maybe_unused]] uint8_t updateAction,
      [[maybe_unused]] uint16_t tradingReferenceDate) noexcept override {}

  virtual void EndOfPacket(
      [[maybe_unused]] u_int32_t msgSeqNum,
      [[maybe_unused]] uint64_t sendingTme) noexcept override {}

  // the parameter here ?
  virtual void BurstEnd([[maybe_unused]]uint32_t cnt) noexcept override
  {

    if (disable_mbo_)
      return;

    bfile::l3_eob_t l3;
    l3.typ = en::l3::EOB;
    l3.venue = xchg;
    //l3.cnt = cnt;

    for (size_t i = 0; i < mbo_order_books.size(); i++)
    {
      const auto book = mbo_order_books[i];
      if (!book)
        continue;
      auto a = frame::ref::RefData::inst().get_asset(i);
      if (!a)
        continue;
      if (a->is_option())
        continue;
      frame::mda::msg::Data msg;
      msg.l3 = l3;
      msg.israw = true;
      BOOKSEND(&msg);
    }

    if (binrec)
    {
      auto recmsg = new frame::mda::msg::Data();
      recmsg->l3 = l3;
      binrec->send(recmsg, 0);
    }
  }

  virtual void PrintStats() noexcept override
  {
    if (latency.size() == 0)
    {
      std::cout << "Handler Latency has no samples\n";
      return;
    }
    {
      oogsl::gvector gv(latency);
      gv.sort();
      auto q01 = gv.quantile(.01);
      auto q05 = gv.quantile(.05);
      auto q25 = gv.quantile(.25);
      auto q50 = gv.quantile(.5);
      auto q75 = gv.quantile(.75);
      auto q85 = gv.quantile(.85);
      auto q95 = gv.quantile(.95);
      auto q99 = gv.quantile(.99);
      auto q999 = gv.quantile(.999);
      log_inf("Handler RCV Latency n: %d, q01: %f, q05: %f, q25: %f, q50: %f, q75: %f, q85: %f, q95: %f, q99: %f q999: %f",
              latency.size(), q01, q05, q25, q50, q75, q85, q95, q99, q999);
    }

    if (cmelatency.size() == 0)
    {
      std::cout << "Handler Latency has no samples\n";
      return;
    }
    {
      oogsl::gvector gv(cmelatency);
      gv.sort();
      auto q01 = gv.quantile(.01);
      auto q05 = gv.quantile(.05);
      auto q25 = gv.quantile(.25);
      auto q50 = gv.quantile(.5);
      auto q75 = gv.quantile(.75);
      auto q85 = gv.quantile(.85);
      auto q95 = gv.quantile(.95);
      auto q99 = gv.quantile(.99);
      auto q999 = gv.quantile(.999);
      log_inf("Handler Sending Time Latency n: %d, q01: %f, q05: %f, q25: %f, q50: %f, q75: %f, q85: %f, q95: %f, q99: %f, q999: %f\n",
              cmelatency.size(), q01, q05, q25, q50, q75, q85, q95, q99, q999);
    }
  }

  virtual void DataReceoveryRestart() noexcept override
  {
    // clear all books
    log_err("*** DataReceoveryRestart ***");
    ChannelReset(0, 0, 0, "DataReceoveryRestart");
  }
};

#pragma GCC diagnostic pop
