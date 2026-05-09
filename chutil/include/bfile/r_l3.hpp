#pragma once
/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <stdint.h>
#include <cstddef>
#include <optional>
#include <variant>
#include <sstream>
#include "zlib.h"

#include "enum/e_names.hpp"
#include "chutil/Time.hpp"


namespace bfile
{

  typedef uint8_t l3_typ_t;

  typedef struct [[gnu::packed]]
  {
    l3_typ_t typ;
    char venue;
  } l3_hdr_t;

  typedef struct [[gnu::packed]]
  {
    l3_typ_t typ;
    char venue;
    uint64_t transactTime;
    uint64_t sendingTime;
    uint64_t handlerendtim;
    uint8_t orderUpdateAction;
    int32_t securityID;
    uint64_t orderID;
    uint64_t priority;
    double pxd;
    uint32_t displayQty;
    char side;
    bool endOfEvent;
    bool lastQuote;
    bool recovery;
    uint8_t order_flags;
    uint8_t visibility_group;

  } l3_mbo_v2_packed_t;

  typedef struct 
  {
    l3_typ_t typ;
    char venue;
    uint64_t transactTime;
    uint64_t sendingTime;
    uint64_t handlerendtim;
    uint8_t orderUpdateAction;
    int32_t securityID;
    uint64_t orderID;
    uint64_t priority;
    double pxd;
    uint32_t displayQty;
    char side;
    bool endOfEvent;
    bool lastQuote;
    bool recovery;
    uint8_t order_flags;
    uint8_t visibility_group;

  } l3_mbo_v2_t;

  typedef struct [[gnu::packed]]
  {
    l3_typ_t typ;
    char venue;
    uint64_t transactTime;
    uint64_t sendingTime;
    uint64_t handlerendtim;
    uint8_t orderUpdateAction;
    int32_t securityID;
    uint64_t orderID;
    uint64_t priority;
    double pxd;
    uint32_t displayQty;
    char side;
    uint32_t numReports;
    uint32_t currentChunk;
    uint32_t numChunks;
    uint32_t lastSeqNum;
    uint32_t msgSeqNum;

  } l3_mbo_snap_packed_t;

  typedef struct 
  {
    l3_typ_t typ;
    char venue;
    uint64_t transactTime;
    uint64_t sendingTime;
    uint64_t handlerendtim;
    uint8_t orderUpdateAction;
    int32_t securityID;
    uint64_t orderID;
    uint64_t priority;
    double pxd;
    uint32_t displayQty;
    char side;
    uint32_t numReports;
    uint32_t currentChunk;
    uint32_t numChunks;
    uint32_t lastSeqNum;
    uint32_t msgSeqNum;

  } l3_mbo_snap_t;

  typedef struct [[gnu::packed]]
  {
    l3_typ_t typ;
    char venue;
    uint64_t recv_time;
    uint64_t transactTime;
    uint64_t sendingTime;
    uint64_t handlerendtim;
    int32_t lastQty;
    uint64_t orderID;
    bool lastTrade;
    bool endOfEvent;

  } l3_mbo_trd_v2_packed_t;

  typedef struct 
  {
    l3_typ_t typ;
    char venue;
    uint64_t recv_time;
    uint64_t transactTime;
    uint64_t sendingTime;
    uint64_t handlerendtim;
    int32_t lastQty;
    uint64_t orderID;
    bool lastTrade;
    bool endOfEvent;

  } l3_mbo_trd_v2_t;

  typedef struct [[gnu::packed]]
  {
    l3_typ_t typ;
    char venue;
    uint64_t transactTime;
    uint64_t sendingTime;
    uint64_t handlerendtim;
    int32_t securityID;
    double pxd;
    char side;
    int32_t sz;
    int32_t numorders;
    uint8_t pxlevel;
    bool endOfEvent;
    bool recovery;

  } l3_mbp_packed_t;

  typedef struct 
  {
    l3_typ_t typ;
    char venue;
    uint64_t transactTime;
    uint64_t sendingTime;
    uint64_t handlerendtim;
    int32_t securityID;
    double pxd;
    char side;
    int32_t sz;
    int32_t numorders;
    uint8_t pxlevel;
    bool endOfEvent;
    bool recovery;

  } l3_mbp_t;

  typedef struct [[gnu::packed]]
  {
    l3_typ_t typ;
    char venue;
    uint64_t transactTime;
    uint64_t sendingTime;
    uint64_t handlerendtim;
    int32_t securityID;
    double pxd;
    char side;
    uint8_t aggresorSide;
    int32_t sz;
    int32_t numorders;
    bool endOfEvent;
    bool lastTrade;

  } l3_mbp_trd_packed_t;

  typedef struct 
  {
    l3_typ_t typ;
    char venue;
    uint64_t transactTime;
    uint64_t sendingTime;
    uint64_t handlerendtim;
    int32_t securityID;
    double pxd;
    char side;
    uint8_t aggresorSide;
    int32_t sz;
    int32_t numorders;
    bool endOfEvent;
    bool lastTrade;

  } l3_mbp_trd_t;

  typedef struct [[gnu::packed]]
  {
    l3_typ_t typ;
    char venue;
    char name[256];
    uint8_t somcode;
    uint64_t som_tim_epoch;
    uint64_t handlerend_tim_epoch;
    uint64_t xordid;
    uint32_t sordid;
    uint16_t symid;
    uint32_t sz;
    uint8_t side;
    int32_t px;
    uint32_t stbf;

  } l3_som_packed_t;

  typedef struct 
  {
    l3_typ_t typ;
    char venue;
    char name[256];
    uint8_t somcode;
    uint64_t som_tim_epoch;
    uint64_t handlerend_tim_epoch;
    uint64_t xordid;
    uint32_t sordid;
    uint16_t symid;
    uint32_t sz;
    uint8_t side;
    int32_t px;
    uint32_t stbf;

  } l3_som_t;

  typedef struct [[gnu::packed]]
  {
    l3_typ_t typ;
    uint64_t t0;
    int32_t th_400;
    int32_t th_1000;
    int32_t th_5000;
    uint32_t vol_sum;

  } l3_volume_packed_t;

  typedef struct 
  {
    l3_typ_t typ;
    uint64_t t0;
    int32_t th_400;
    int32_t th_1000;
    int32_t th_5000;
    uint32_t vol_sum;

  } l3_volume_t;

  typedef struct [[gnu::packed]]
  {
    l3_typ_t typ;
    char venue;
    char id;
    uint64_t t_handler;
    uint64_t t0;
    uint64_t t1;

  } l3_interval_packed_t;

  typedef struct 
  {
    l3_typ_t typ;
    char venue;
    char id;
    uint64_t t_handler;
    uint64_t t0;
    uint64_t t1;

  } l3_interval_t;

  typedef struct [[gnu::packed]]
  {
    l3_typ_t typ;
    uint64_t t0;
    char name[64];
    uint32_t size;
    uint32_t msg;
    uint32_t nv_ctx;
    uint32_t v_ctx;

  } l3_qlen_packed_t;

  typedef struct 
  {
    l3_typ_t typ;
    uint64_t t0;
    char name[64];
    uint32_t size;
    uint32_t msg;
    uint32_t nv_ctx;
    uint32_t v_ctx;

  } l3_qlen_t;

  typedef struct [[gnu::packed]]
  {
    l3_typ_t typ;
    char venue;
    uint64_t txtim;
    uint64_t sendtim;
    int32_t securityID;
    int32_t vol;
    char vtyp;
    uint8_t action;

  } l3_vol_packed_t;

  typedef struct 
  {
    l3_typ_t typ;
    char venue;
    uint64_t txtim;
    uint64_t sendtim;
    int32_t securityID;
    int32_t vol;
    char vtyp;
    uint8_t action;

  } l3_vol_t;

  typedef struct [[gnu::packed]]
  {
    l3_typ_t typ;
    char venue;
    uint64_t txtim;
    uint64_t sendtim;
    int32_t securityID;
    uint8_t haltReason;
    uint8_t tradingStatus;
    uint8_t tradingEvent;

  } l3_sst_packed_t;

  typedef struct 
  {
    l3_typ_t typ;
    char venue;
    uint64_t txtim;
    uint64_t sendtim;
    int32_t securityID;
    uint8_t haltReason;
    uint8_t tradingStatus;
    uint8_t tradingEvent;

  } l3_sst_t;

  typedef struct [[gnu::packed]]
  {
    l3_typ_t typ;
    char venue;
    char sym[24];
    char asset[8];
    char cfiCode[8];
    double high_limit_px;
    double low_limit_px;
    double pxvar;
    int32_t securityID;
    uint8_t mDSecurityTradingStatus;
    uint64_t activation;
    uint64_t expiration;
    char securityGroup[8];
    uint8_t marketSegmentID;
    char matchAlgorithm;
    uint8_t mainFraction;
    double minPriceIncrement;
    uint8_t priceDisplayFormat;
    char userDefinedInstrument;
    char updateAction;
    double dispFactor;
    uint8_t subFraction;
    char securityType[16];
    uint8_t maturityMont;
    uint16_t maturityYear;
    int32_t openInterestQty;
    int32_t clearedVolume;
    uint16_t tradingRefDate;
    char unitOfMeasure[16];
    double unitOfMeasureQty;

  } l3_fdf_packed_t;

  typedef struct 
  {
    l3_typ_t typ;
    char venue;
    char sym[24];
    char asset[8];
    char cfiCode[8];
    double high_limit_px;
    double low_limit_px;
    double pxvar;
    int32_t securityID;
    uint8_t mDSecurityTradingStatus;
    uint64_t activation;
    uint64_t expiration;
    char securityGroup[8];
    uint8_t marketSegmentID;
    char matchAlgorithm;
    uint8_t mainFraction;
    double minPriceIncrement;
    uint8_t priceDisplayFormat;
    char userDefinedInstrument;
    char updateAction;
    double dispFactor;
    uint8_t subFraction;
    char securityType[16];
    uint8_t maturityMont;
    uint16_t maturityYear;
    int32_t openInterestQty;
    int32_t clearedVolume;
    uint16_t tradingRefDate;
    char unitOfMeasure[16];
    double unitOfMeasureQty;

  } l3_fdf_t;

  typedef struct [[gnu::packed]]
  {
    l3_typ_t typ;
    char venue;
    char sym[24];
    char asset[8];
    char cfiCode[8];
    double high_limit_px;
    double low_limit_px;
    double pxvar;
    int32_t securityID;
    uint8_t mDSecurityTradingStatus;
    uint64_t activation;
    uint64_t expiration;
    char sec_group[8];
    uint8_t marketSegmentID;
    char matchAlgorithm;
    uint8_t mainFraction;
    double minPriceIncrement;
    double minCabPrice;
    uint8_t priceDisplayFormat;
    char updateAction;
    uint8_t putOrCall;
    double strikePrice;
    char userDefinedInstrument;
    int8_t tickRule;
    uint8_t noUnderlyingsCount;
    uint32_t underlyingSecurityID[4];
    double dispFactor;
    uint8_t subFraction;
    char securityType[16];
    uint8_t maturityMont;
    uint16_t maturityYear;
    int32_t openInterestQty;
    int32_t clearedVolume;
    uint16_t tradingRefDate;
    char unitOfMeasure[16];
    double unitOfMeasureQty;

  } l3_odf_packed_t;

  typedef struct 
  {
    l3_typ_t typ;
    char venue;
    char sym[24];
    char asset[8];
    char cfiCode[8];
    double high_limit_px;
    double low_limit_px;
    double pxvar;
    int32_t securityID;
    uint8_t mDSecurityTradingStatus;
    uint64_t activation;
    uint64_t expiration;
    char sec_group[8];
    uint8_t marketSegmentID;
    char matchAlgorithm;
    uint8_t mainFraction;
    double minPriceIncrement;
    double minCabPrice;
    uint8_t priceDisplayFormat;
    char updateAction;
    uint8_t putOrCall;
    double strikePrice;
    char userDefinedInstrument;
    int8_t tickRule;
    uint8_t noUnderlyingsCount;
    uint32_t underlyingSecurityID[4];
    double dispFactor;
    uint8_t subFraction;
    char securityType[16];
    uint8_t maturityMont;
    uint16_t maturityYear;
    int32_t openInterestQty;
    int32_t clearedVolume;
    uint16_t tradingRefDate;
    char unitOfMeasure[16];
    double unitOfMeasureQty;

  } l3_odf_t;

  typedef struct [[gnu::packed]]
  {
    l3_typ_t typ;
    char venue;
    char sym[24];
    char asset[8];
    char cfiCode[8];
    double high_limit_px;
    double low_limit_px;
    int32_t securityID;
    char securityUpdateAction;
    uint64_t activation;
    uint64_t expiration;
    char sec_group[8];
    uint8_t marketSegmentID;
    uint8_t mDSecurityTradingStatus;
    char matchAlgorithm;
    uint8_t mainFraction;
    double minPriceIncrement;
    uint8_t priceDisplayFormat;
    char userDefinedInstrument;
    int8_t tickRule;
    uint8_t noLegsCount;
    double legOptionDelta[8];
    double legPrice[8];
    int8_t legRatioQty[8];
    int32_t legSecurityID[8];
    uint8_t legSide[8];
    double dispFactor;
    uint8_t subFraction;
    char securityType[16];
    uint8_t maturityMont;
    uint16_t maturityYear;
    int32_t openInterestQty;
    int32_t clearedVolume;
    uint16_t tradingRefDate;
    char unitOfMeasure[16];

  } l3_sdf_packed_t;

  typedef struct 
  {
    l3_typ_t typ;
    char venue;
    char sym[24];
    char asset[8];
    char cfiCode[8];
    double high_limit_px;
    double low_limit_px;
    int32_t securityID;
    char securityUpdateAction;
    uint64_t activation;
    uint64_t expiration;
    char sec_group[8];
    uint8_t marketSegmentID;
    uint8_t mDSecurityTradingStatus;
    char matchAlgorithm;
    uint8_t mainFraction;
    double minPriceIncrement;
    uint8_t priceDisplayFormat;
    char userDefinedInstrument;
    int8_t tickRule;
    uint8_t noLegsCount;
    double legOptionDelta[8];
    double legPrice[8];
    int8_t legRatioQty[8];
    int32_t legSecurityID[8];
    uint8_t legSide[8];
    double dispFactor;
    uint8_t subFraction;
    char securityType[16];
    uint8_t maturityMont;
    uint16_t maturityYear;
    int32_t openInterestQty;
    int32_t clearedVolume;
    uint16_t tradingRefDate;
    char unitOfMeasure[16];

  } l3_sdf_t;

  typedef struct [[gnu::packed]]
  {
    l3_typ_t typ;
    char venue;
    int32_t securityID;
    double pxh;
    double pxl;
    double pxvar;
    char entryType[8];

  } l3_lim_packed_t;

  typedef struct 
  {
    l3_typ_t typ;
    char venue;
    int32_t securityID;
    double pxh;
    double pxl;
    double pxvar;
    char entryType[8];

  } l3_lim_t;

  typedef struct [[gnu::packed]]
  {
    l3_typ_t typ;
    char venue;

  } l3_gap_v2_packed_t;

  typedef struct 
  {
    l3_typ_t typ;
    char venue;

  } l3_gap_v2_t;

  typedef struct [[gnu::packed]]
  {
    l3_typ_t typ;
    char venue;

  } l3_chr_v2_packed_t;

  typedef struct 
  {
    l3_typ_t typ;
    char venue;

  } l3_chr_v2_t;

  typedef struct [[gnu::packed]]
  {
    l3_typ_t typ;
    char venue;

  } l3_eob_packed_t;

  typedef struct 
  {
    l3_typ_t typ;
    char venue;

  } l3_eob_t;

  typedef struct [[gnu::packed]]
  {
    l3_typ_t typ;

  } l3_sim_packed_t;

  typedef struct 
  {
    l3_typ_t typ;

  } l3_sim_t;

  //
  // FENICS specific
  //

  typedef struct [[gnu::packed]]
  {
    l3_typ_t typ;
    char venue;
    char EventCode;

  } l3_fenics_sys_event_packed_t;

  typedef struct 
  {
    l3_typ_t typ;
    char venue;
    char EventCode;

  } l3_fenics_sys_event_t;

  typedef struct [[gnu::packed]]
  {
    l3_typ_t typ;
    char venue;
    uint32_t InstrumentLocate;
    uint64_t InstrumentId;
    char EnhancedSymbol[30];
    char IndustryIdentifier[12];
    char InstrumentType;
    char InstrumentSubType[2];
    uint64_t MinimumOrderSize;
    uint64_t MaximumOrderSize;	
    uint64_t RoundLotSize;	
    uint16_t PriceType;	
    uint64_t DecimalPriceTick;
    uint64_t FractionalPriceTick;	
    uint64_t PriceMultiplier;
    char SymbolSuffix[10];
    char MatchAlgorithm;
    char MIC[4];
    char CFIcode[6];
    char Currency[3];

  } l3_fenics_bdf_packed_t;

  typedef struct 
  {
    l3_typ_t typ;
    char venue;
    uint32_t InstrumentLocate;
    uint64_t InstrumentId;
    char EnhancedSymbol[30];
    char IndustryIdentifier[12];
    char InstrumentType;
    char InstrumentSubType[2];
    uint64_t MinimumOrderSize;
    uint64_t MaximumOrderSize;
    uint64_t RoundLotSize;
    uint16_t PriceType;
    uint64_t DecimalPriceTick;
    uint64_t FractionalPriceTick;
    uint64_t PriceMultiplier;
    char SymbolSuffix[10];
    char MatchAlgorithm;
    char MIC[4];
    char CFIcode[6];
    char Currency[3];

  } l3_fenics_bdf_t;

  typedef struct [[gnu::packed]]
  {
    l3_typ_t typ;
    char venue;
    uint32_t InstrumentLocate;
    uint64_t InstrumentId;
    char TradingState;

  } l3_fenics_trading_action_packed_t;

  typedef struct 
  {
    l3_typ_t typ;
    char venue;
    uint32_t InstrumentLocate;
    uint64_t InstrumentId;
    char TradingState;

  } l3_fenics_trading_action_t;

  typedef struct [[gnu::packed]]
  {
    l3_typ_t typ;
    char venue;
    uint64_t Timestamp;
    uint32_t InstrumentLocate;
    uint64_t TradeVolume;
    int64_t HighPrice;
    int64_t LowPrice;
    int32_t OpenOrders;

  } l3_fenics_instrumentstats_packed_t;

  typedef struct 
  {
    l3_typ_t typ;
    char venue;
    uint64_t Timestamp;
    uint32_t InstrumentLocate;
    uint64_t TradeVolume;
    int64_t HighPrice;
    int64_t LowPrice;
    int32_t OpenOrders;

  } l3_fenics_instrumentstats_t;

  //
  // DEALERWEB
  //

  typedef struct [[gnu::packed]]
  {
    l3_typ_t typ;
    char venue;
    uint64_t TimestampNanoseconds;
    uint32_t OrderBookID;
    char Symbol[20];
    char SecurityDescription[16];
    char CUSIP[9];
    char Product;
    char ProductSubtype;
    char PriceType;
    uint16_t PriceDecimals;
    uint16_t YieldDecimals;
    uint16_t CouponDecimals;
    uint32_t QuantityMultiplier;
    uint32_t Maturity;
    uint32_t Coupon;
    uint32_t DatedDate;
    uint32_t IssueDate;
    uint32_t AuctionDate;
    uint32_t AnnouncementDate;
    uint32_t FirstCouponDate;
    uint32_t SettlementDate;
    uint32_t Index;
    uint32_t Spread;
    uint16_t TradingFeatures;
    uint32_t MinimumEntryQuantity;
    uint32_t MinimumQuantityIncrement;
    uint16_t IssuedasBenchmark;
    uint64_t PriceTickSize;
    char ShortSym[10];

  } l3_dealerweb_bookdir_packed_t;

  typedef struct 
  {
    l3_typ_t typ;
    char venue;
    uint64_t TimestampNanoseconds;
    uint32_t OrderBookID;
    char Symbol[20];
    char SecurityDescription[16];
    char CUSIP[9];
    char Product;
    char ProductSubtype;
    char PriceType;
    uint16_t PriceDecimals;
    uint16_t YieldDecimals;
    uint16_t CouponDecimals;
    uint32_t QuantityMultiplier;
    uint32_t Maturity;
    uint32_t Coupon;
    uint32_t DatedDate;
    uint32_t IssueDate;
    uint32_t AuctionDate;
    uint32_t AnnouncementDate;
    uint32_t FirstCouponDate;
    uint32_t SettlementDate;
    uint32_t Index;
    uint32_t Spread;
    uint16_t TradingFeatures;
    uint32_t MinimumEntryQuantity;
    uint32_t MinimumQuantityIncrement;
    uint16_t IssuedasBenchmark;
    uint64_t PriceTickSize;
    char ShortSym[10];

  } l3_dealerweb_bookdir_t;

  typedef struct [[gnu::packed]]
  {
    l3_typ_t typ;
    char venue;
    uint64_t TimestampNanoseconds;
    char EventCode;
    char EventReason;
    uint32_t OrderBookID;

  } l3_dealerweb_sys_event_packed_t;

  typedef struct 
  {
    l3_typ_t typ;
    char venue;
    uint64_t TimestampNanoseconds;
    char EventCode;
    char EventReason;
    uint32_t OrderBookID;

  } l3_dealerweb_sys_event_t;

  typedef struct [[gnu::packed]]
  {
    l3_typ_t typ;
    char venue;
    uint64_t TimestampNanoseconds;
    uint32_t OrderBookID;
    char SecurityEventCode;

  } l3_dealerweb_orderbookstate_packed_t;

  typedef struct 
  {
    l3_typ_t typ;
    char venue;
    uint64_t TimestampNanoseconds;
    uint32_t OrderBookID;
    char SecurityEventCode;

  } l3_dealerweb_orderbookstate_t;

  typedef struct [[gnu::packed]]
  {
    l3_typ_t typ;
    char venue;
    uint64_t TimestampNanoseconds;
    uint32_t OrderBookID;
    uint64_t PrimaryInformation;
    uint32_t SecondaryInformation;
    uint16_t InformationType;

  } l3_dealerweb_information_packed_t;

  typedef struct 
  {
    l3_typ_t typ;
    char venue;
    uint64_t TimestampNanoseconds;
    uint32_t OrderBookID;
    uint64_t PrimaryInformation;
    uint32_t SecondaryInformation;
    uint16_t InformationType;

  } l3_dealerweb_information_t;

  typedef struct [[gnu::packed]]
  {
    l3_typ_t typ;
    char venue;
    uint64_t TimestampNanoseconds;
    uint32_t OrderBookID;
    uint32_t TransactionID;
    uint32_t ExecutedQuantity;
    char MatchID[14];
    int64_t TradePrice;

  } l3_dealerweb_brokentrade_packed_t;

  typedef struct 
  {
    l3_typ_t typ;
    char venue;
    uint64_t TimestampNanoseconds;
    uint32_t OrderBookID;
    uint32_t TransactionID;
    uint32_t ExecutedQuantity;
    char MatchID[14];
    int64_t TradePrice;

  } l3_dealerweb_brokentrade_t;


  //
  // CONVERSION FUNCTIONS
  //

  // Convert from unpacked to packed
  inline static l3_mbo_v2_packed_t to_packed(const l3_mbo_v2_t& unpacked) {
    l3_mbo_v2_packed_t packed = {};
    packed.typ = unpacked.typ;
    packed.venue = unpacked.venue;
    packed.transactTime = unpacked.transactTime;
    packed.sendingTime = unpacked.sendingTime;
    packed.handlerendtim = unpacked.handlerendtim;
    packed.orderUpdateAction = unpacked.orderUpdateAction;
    packed.securityID = unpacked.securityID;
    packed.orderID = unpacked.orderID;
    packed.priority = unpacked.priority;
    packed.pxd = unpacked.pxd;
    packed.displayQty = unpacked.displayQty;
    packed.side = unpacked.side;
    packed.endOfEvent = unpacked.endOfEvent;
    packed.lastQuote = unpacked.lastQuote;
    packed.recovery = unpacked.recovery;
    packed.order_flags = unpacked.order_flags;
    packed.visibility_group = unpacked.visibility_group;
    return packed;
  }

  // Convert from packed to unpacked
  inline static l3_mbo_v2_t from_packed(const l3_mbo_v2_packed_t& packed) {
    l3_mbo_v2_t unpacked = {};
    unpacked.typ = packed.typ;
    unpacked.venue = packed.venue;
    unpacked.transactTime = packed.transactTime;
    unpacked.sendingTime = packed.sendingTime;
    unpacked.handlerendtim = packed.handlerendtim;
    unpacked.orderUpdateAction = packed.orderUpdateAction;
    unpacked.securityID = packed.securityID;
    unpacked.orderID = packed.orderID;
    unpacked.priority = packed.priority;
    unpacked.pxd = packed.pxd;
    unpacked.displayQty = packed.displayQty;
    unpacked.side = packed.side;
    unpacked.endOfEvent = packed.endOfEvent;
    unpacked.lastQuote = packed.lastQuote;
    unpacked.recovery = packed.recovery;
    unpacked.order_flags = packed.order_flags;
    unpacked.visibility_group = packed.visibility_group;
    return unpacked;
  }

  // l3_mbo_snap_t conversion functions
  inline static l3_mbo_snap_packed_t to_packed(const l3_mbo_snap_t& unpacked) {
    l3_mbo_snap_packed_t packed = {};
    packed.typ = unpacked.typ;
    packed.venue = unpacked.venue;
    packed.transactTime = unpacked.transactTime;
    packed.sendingTime = unpacked.sendingTime;
    packed.handlerendtim = unpacked.handlerendtim;
    packed.orderUpdateAction = unpacked.orderUpdateAction;
    packed.securityID = unpacked.securityID;
    packed.orderID = unpacked.orderID;
    packed.priority = unpacked.priority;
    packed.pxd = unpacked.pxd;
    packed.displayQty = unpacked.displayQty;
    packed.side = unpacked.side;
    packed.numReports = unpacked.numReports;
    packed.currentChunk = unpacked.currentChunk;
    packed.numChunks = unpacked.numChunks;
    packed.lastSeqNum = unpacked.lastSeqNum;
    packed.msgSeqNum = unpacked.msgSeqNum;
    return packed;
  }

  inline static l3_mbo_snap_t from_packed(const l3_mbo_snap_packed_t& packed) {
    l3_mbo_snap_t unpacked = {};
    unpacked.typ = packed.typ;
    unpacked.venue = packed.venue;
    unpacked.transactTime = packed.transactTime;
    unpacked.sendingTime = packed.sendingTime;
    unpacked.handlerendtim = packed.handlerendtim;
    unpacked.orderUpdateAction = packed.orderUpdateAction;
    unpacked.securityID = packed.securityID;
    unpacked.orderID = packed.orderID;
    unpacked.priority = packed.priority;
    unpacked.pxd = packed.pxd;
    unpacked.displayQty = packed.displayQty;
    unpacked.side = packed.side;
    unpacked.numReports = packed.numReports;
    unpacked.currentChunk = packed.currentChunk;
    unpacked.numChunks = packed.numChunks;
    unpacked.lastSeqNum = packed.lastSeqNum;
    unpacked.msgSeqNum = packed.msgSeqNum;
    return unpacked;
  }

  // l3_mbo_trd_v2_t conversion functions
  inline static l3_mbo_trd_v2_packed_t to_packed(const l3_mbo_trd_v2_t& unpacked) {
    l3_mbo_trd_v2_packed_t packed = {};
    packed.typ = unpacked.typ;
    packed.venue = unpacked.venue;
    packed.recv_time = unpacked.recv_time;
    packed.transactTime = unpacked.transactTime;
    packed.sendingTime = unpacked.sendingTime;
    packed.handlerendtim = unpacked.handlerendtim;
    packed.lastQty = unpacked.lastQty;
    packed.orderID = unpacked.orderID;
    packed.lastTrade = unpacked.lastTrade;
    packed.endOfEvent = unpacked.endOfEvent;
    return packed;
  }

  inline static l3_mbo_trd_v2_t from_packed(const l3_mbo_trd_v2_packed_t& packed) {
    l3_mbo_trd_v2_t unpacked = {};
    unpacked.typ = packed.typ;
    unpacked.venue = packed.venue;
    unpacked.recv_time = packed.recv_time;
    unpacked.transactTime = packed.transactTime;
    unpacked.sendingTime = packed.sendingTime;
    unpacked.handlerendtim = packed.handlerendtim;
    unpacked.lastQty = packed.lastQty;
    unpacked.orderID = packed.orderID;
    unpacked.lastTrade = packed.lastTrade;
    unpacked.endOfEvent = packed.endOfEvent;
    return unpacked;
  }

  // l3_mbp_t conversion functions
  inline static l3_mbp_packed_t to_packed(const l3_mbp_t& unpacked) {
    l3_mbp_packed_t packed = {};
    packed.typ = unpacked.typ;
    packed.venue = unpacked.venue;
    packed.transactTime = unpacked.transactTime;
    packed.sendingTime = unpacked.sendingTime;
    packed.handlerendtim = unpacked.handlerendtim;
    packed.securityID = unpacked.securityID;
    packed.pxd = unpacked.pxd;
    packed.side = unpacked.side;
    packed.sz = unpacked.sz;
    packed.numorders = unpacked.numorders;
    packed.pxlevel = unpacked.pxlevel;
    packed.endOfEvent = unpacked.endOfEvent;
    packed.recovery = unpacked.recovery;
    return packed;
  }

  inline static l3_mbp_t from_packed(const l3_mbp_packed_t& packed) {
    l3_mbp_t unpacked = {};
    unpacked.typ = packed.typ;
    unpacked.venue = packed.venue;
    unpacked.transactTime = packed.transactTime;
    unpacked.sendingTime = packed.sendingTime;
    unpacked.handlerendtim = packed.handlerendtim;
    unpacked.securityID = packed.securityID;
    unpacked.pxd = packed.pxd;
    unpacked.side = packed.side;
    unpacked.sz = packed.sz;
    unpacked.numorders = packed.numorders;
    unpacked.pxlevel = packed.pxlevel;
    unpacked.endOfEvent = packed.endOfEvent;
    unpacked.recovery = packed.recovery;
    return unpacked;
  }

  // l3_mbp_trd_t conversion functions
  inline static l3_mbp_trd_packed_t to_packed(const l3_mbp_trd_t& unpacked) {
    l3_mbp_trd_packed_t packed = {};
    packed.typ = unpacked.typ;
    packed.venue = unpacked.venue;
    packed.transactTime = unpacked.transactTime;
    packed.sendingTime = unpacked.sendingTime;
    packed.handlerendtim = unpacked.handlerendtim;
    packed.securityID = unpacked.securityID;
    packed.pxd = unpacked.pxd;
    packed.side = unpacked.side;
    packed.aggresorSide = unpacked.aggresorSide;
    packed.sz = unpacked.sz;
    packed.numorders = unpacked.numorders;
    packed.endOfEvent = unpacked.endOfEvent;
    packed.lastTrade = unpacked.lastTrade;
    return packed;
  }

  inline static l3_mbp_trd_t from_packed(const l3_mbp_trd_packed_t& packed) {
    l3_mbp_trd_t unpacked = {};
    unpacked.typ = packed.typ;
    unpacked.venue = packed.venue;
    unpacked.transactTime = packed.transactTime;
    unpacked.sendingTime = packed.sendingTime;
    unpacked.handlerendtim = packed.handlerendtim;
    unpacked.securityID = packed.securityID;
    unpacked.pxd = packed.pxd;
    unpacked.side = packed.side;
    unpacked.aggresorSide = packed.aggresorSide;
    unpacked.sz = packed.sz;
    unpacked.numorders = packed.numorders;
    unpacked.endOfEvent = packed.endOfEvent;
    unpacked.lastTrade = packed.lastTrade;
    return unpacked;
  }

  // l3_som_t conversion functions
  inline static l3_som_packed_t to_packed(const l3_som_t& unpacked) {
    l3_som_packed_t packed = {};
    packed.typ = unpacked.typ;
    packed.venue = unpacked.venue;
    memcpy(packed.name, unpacked.name, sizeof(packed.name));
    packed.somcode = unpacked.somcode;
    packed.som_tim_epoch = unpacked.som_tim_epoch;
    packed.handlerend_tim_epoch = unpacked.handlerend_tim_epoch;
    packed.xordid = unpacked.xordid;
    packed.sordid = unpacked.sordid;
    packed.symid = unpacked.symid;
    packed.sz = unpacked.sz;
    packed.side = unpacked.side;
    packed.px = unpacked.px;
    packed.stbf = unpacked.stbf;
    return packed;
  }

  inline static l3_som_t from_packed(const l3_som_packed_t& packed) {
    l3_som_t unpacked = {};
    unpacked.typ = packed.typ;
    unpacked.venue = packed.venue;
    memcpy(unpacked.name, packed.name, sizeof(unpacked.name));
    unpacked.somcode = packed.somcode;
    unpacked.som_tim_epoch = packed.som_tim_epoch;
    unpacked.handlerend_tim_epoch = packed.handlerend_tim_epoch;
    unpacked.xordid = packed.xordid;
    unpacked.sordid = packed.sordid;
    unpacked.symid = packed.symid;
    unpacked.sz = packed.sz;
    unpacked.side = packed.side;
    unpacked.px = packed.px;
    unpacked.stbf = packed.stbf;
    return unpacked;
  }

  // l3_volume_t conversion functions
  inline static l3_volume_packed_t to_packed(const l3_volume_t& unpacked) {
    l3_volume_packed_t packed = {};
    packed.typ = unpacked.typ;
    packed.t0 = unpacked.t0;
    packed.th_400 = unpacked.th_400;
    packed.th_1000 = unpacked.th_1000;
    packed.th_5000 = unpacked.th_5000;
    packed.vol_sum = unpacked.vol_sum;
    return packed;
  }

  inline static l3_volume_t from_packed(const l3_volume_packed_t& packed) {
    l3_volume_t unpacked = {};
    unpacked.typ = packed.typ;
    unpacked.t0 = packed.t0;
    unpacked.th_400 = packed.th_400;
    unpacked.th_1000 = packed.th_1000;
    unpacked.th_5000 = packed.th_5000;
    unpacked.vol_sum = packed.vol_sum;
    return unpacked;
  }

  // l3_interval_t conversion functions
  inline static l3_interval_packed_t to_packed(const l3_interval_t& unpacked) {
    l3_interval_packed_t packed = {};
    packed.typ = unpacked.typ;
    packed.venue = unpacked.venue;
    packed.id = unpacked.id;
    packed.t_handler = unpacked.t_handler;
    packed.t0 = unpacked.t0;
    packed.t1 = unpacked.t1;
    return packed;
  }

  inline static l3_interval_t from_packed(const l3_interval_packed_t& packed) {
    l3_interval_t unpacked = {};
    unpacked.typ = packed.typ;
    unpacked.venue = packed.venue;
    unpacked.id = packed.id;
    unpacked.t_handler = packed.t_handler;
    unpacked.t0 = packed.t0;
    unpacked.t1 = packed.t1;
    return unpacked;
  }

  // l3_qlen_t conversion functions
  inline static l3_qlen_packed_t to_packed(const l3_qlen_t& unpacked) {
    l3_qlen_packed_t packed = {};
    packed.typ = unpacked.typ;
    packed.t0 = unpacked.t0;
    memcpy(packed.name, unpacked.name, sizeof(packed.name));
    packed.size = unpacked.size;
    packed.msg = unpacked.msg;
    packed.nv_ctx = unpacked.nv_ctx;
    packed.v_ctx = unpacked.v_ctx;
    return packed;
  }

  inline static l3_qlen_t from_packed(const l3_qlen_packed_t& packed) {
    l3_qlen_t unpacked = {};
    unpacked.typ = packed.typ;
    unpacked.t0 = packed.t0;
    memcpy(unpacked.name, packed.name, sizeof(unpacked.name));
    unpacked.size = packed.size;
    unpacked.msg = packed.msg;
    unpacked.nv_ctx = packed.nv_ctx;
    unpacked.v_ctx = packed.v_ctx;
    return unpacked;
  }

  // l3_vol_t conversion functions
  inline static l3_vol_packed_t to_packed(const l3_vol_t& unpacked) {
    l3_vol_packed_t packed = {};
    packed.typ = unpacked.typ;
    packed.venue = unpacked.venue;
    packed.txtim = unpacked.txtim;
    packed.sendtim = unpacked.sendtim;
    packed.securityID = unpacked.securityID;
    packed.vol = unpacked.vol;
    packed.vtyp = unpacked.vtyp;
    packed.action = unpacked.action;
    return packed;
  }

  inline static l3_vol_t from_packed(const l3_vol_packed_t& packed) {
    l3_vol_t unpacked = {};
    unpacked.typ = packed.typ;
    unpacked.venue = packed.venue;
    unpacked.txtim = packed.txtim;
    unpacked.sendtim = packed.sendtim;
    unpacked.securityID = packed.securityID;
    unpacked.vol = packed.vol;
    unpacked.vtyp = packed.vtyp;
    unpacked.action = packed.action;
    return unpacked;
  }

  // l3_sst_t conversion functions
  inline static l3_sst_packed_t to_packed(const l3_sst_t& unpacked) {
    l3_sst_packed_t packed = {};
    packed.typ = unpacked.typ;
    packed.venue = unpacked.venue;
    packed.txtim = unpacked.txtim;
    packed.sendtim = unpacked.sendtim;
    packed.securityID = unpacked.securityID;
    packed.haltReason = unpacked.haltReason;
    packed.tradingStatus = unpacked.tradingStatus;
    packed.tradingEvent = unpacked.tradingEvent;
    return packed;
  }

  inline static l3_sst_t from_packed(const l3_sst_packed_t& packed) {
    l3_sst_t unpacked = {};
    unpacked.typ = packed.typ;
    unpacked.venue = packed.venue;
    unpacked.txtim = packed.txtim;
    unpacked.sendtim = packed.sendtim;
    unpacked.securityID = packed.securityID;
    unpacked.haltReason = packed.haltReason;
    unpacked.tradingStatus = packed.tradingStatus;
    unpacked.tradingEvent = packed.tradingEvent;
    return unpacked;
  }

  // l3_fdf_t conversion functions
  inline static l3_fdf_packed_t to_packed(const l3_fdf_t& unpacked) {
    l3_fdf_packed_t packed = {};
    packed.typ = unpacked.typ;
    packed.venue = unpacked.venue;
    memcpy(packed.sym, unpacked.sym, sizeof(packed.sym));
    memcpy(packed.asset, unpacked.asset, sizeof(packed.asset));
    memcpy(packed.cfiCode, unpacked.cfiCode, sizeof(packed.cfiCode));
    packed.high_limit_px = unpacked.high_limit_px;
    packed.low_limit_px = unpacked.low_limit_px;
    packed.pxvar = unpacked.pxvar;
    packed.securityID = unpacked.securityID;
    packed.mDSecurityTradingStatus = unpacked.mDSecurityTradingStatus;
    packed.activation = unpacked.activation;
    packed.expiration = unpacked.expiration;
    memcpy(packed.securityGroup, unpacked.securityGroup, sizeof(packed.securityGroup));
    packed.marketSegmentID = unpacked.marketSegmentID;
    packed.matchAlgorithm = unpacked.matchAlgorithm;
    packed.mainFraction = unpacked.mainFraction;
    packed.minPriceIncrement = unpacked.minPriceIncrement;
    packed.priceDisplayFormat = unpacked.priceDisplayFormat;
    packed.userDefinedInstrument = unpacked.userDefinedInstrument;
    packed.updateAction = unpacked.updateAction;
    packed.dispFactor = unpacked.dispFactor;
    packed.subFraction = unpacked.subFraction;
    memcpy(packed.securityType, unpacked.securityType, sizeof(packed.securityType));
    packed.maturityMont = unpacked.maturityMont;
    packed.maturityYear = unpacked.maturityYear;
    packed.openInterestQty = unpacked.openInterestQty;
    packed.clearedVolume = unpacked.clearedVolume;
    packed.tradingRefDate = unpacked.tradingRefDate;
    memcpy(packed.unitOfMeasure, unpacked.unitOfMeasure, sizeof(packed.unitOfMeasure));
    packed.unitOfMeasureQty = unpacked.unitOfMeasureQty;
    return packed;
  }

  inline static l3_fdf_t from_packed(const l3_fdf_packed_t& packed) {
    l3_fdf_t unpacked = {};
    unpacked.typ = packed.typ;
    unpacked.venue = packed.venue;
    memcpy(unpacked.sym, packed.sym, sizeof(unpacked.sym));
    memcpy(unpacked.asset, packed.asset, sizeof(unpacked.asset));
    memcpy(unpacked.cfiCode, packed.cfiCode, sizeof(unpacked.cfiCode));
    unpacked.high_limit_px = packed.high_limit_px;
    unpacked.low_limit_px = packed.low_limit_px;
    unpacked.pxvar = packed.pxvar;
    unpacked.securityID = packed.securityID;
    unpacked.mDSecurityTradingStatus = packed.mDSecurityTradingStatus;
    unpacked.activation = packed.activation;
    unpacked.expiration = packed.expiration;
    memcpy(unpacked.securityGroup, packed.securityGroup, sizeof(unpacked.securityGroup));
    unpacked.marketSegmentID = packed.marketSegmentID;
    unpacked.matchAlgorithm = packed.matchAlgorithm;
    unpacked.mainFraction = packed.mainFraction;
    unpacked.minPriceIncrement = packed.minPriceIncrement;
    unpacked.priceDisplayFormat = packed.priceDisplayFormat;
    unpacked.userDefinedInstrument = packed.userDefinedInstrument;
    unpacked.updateAction = packed.updateAction;
    unpacked.dispFactor = packed.dispFactor;
    unpacked.subFraction = packed.subFraction;
    memcpy(unpacked.securityType, packed.securityType, sizeof(unpacked.securityType));
    unpacked.maturityMont = packed.maturityMont;
    unpacked.maturityYear = packed.maturityYear;
    unpacked.openInterestQty = packed.openInterestQty;
    unpacked.clearedVolume = packed.clearedVolume;
    unpacked.tradingRefDate = packed.tradingRefDate;
    memcpy(unpacked.unitOfMeasure, packed.unitOfMeasure, sizeof(unpacked.unitOfMeasure));
    unpacked.unitOfMeasureQty = packed.unitOfMeasureQty;
    return unpacked;
  }

  // l3_odf_t conversion functions
  inline static l3_odf_packed_t to_packed(const l3_odf_t& unpacked) {
    l3_odf_packed_t packed = {};
    packed.typ = unpacked.typ;
    packed.venue = unpacked.venue;
    memcpy(packed.sym, unpacked.sym, sizeof(packed.sym));
    memcpy(packed.asset, unpacked.asset, sizeof(packed.asset));
    memcpy(packed.cfiCode, unpacked.cfiCode, sizeof(packed.cfiCode));
    packed.high_limit_px = unpacked.high_limit_px;
    packed.low_limit_px = unpacked.low_limit_px;
    packed.pxvar = unpacked.pxvar;
    packed.securityID = unpacked.securityID;
    packed.mDSecurityTradingStatus = unpacked.mDSecurityTradingStatus;
    packed.activation = unpacked.activation;
    packed.expiration = unpacked.expiration;
    memcpy(packed.sec_group, unpacked.sec_group, sizeof(packed.sec_group));
    packed.marketSegmentID = unpacked.marketSegmentID;
    packed.matchAlgorithm = unpacked.matchAlgorithm;
    packed.mainFraction = unpacked.mainFraction;
    packed.minPriceIncrement = unpacked.minPriceIncrement;
    packed.minCabPrice = unpacked.minCabPrice;
    packed.priceDisplayFormat = unpacked.priceDisplayFormat;
    packed.updateAction = unpacked.updateAction;
    packed.putOrCall = unpacked.putOrCall;
    packed.strikePrice = unpacked.strikePrice;
    packed.userDefinedInstrument = unpacked.userDefinedInstrument;
    packed.tickRule = unpacked.tickRule;
    packed.noUnderlyingsCount = unpacked.noUnderlyingsCount;
    memcpy(packed.underlyingSecurityID, unpacked.underlyingSecurityID, sizeof(packed.underlyingSecurityID));
    packed.dispFactor = unpacked.dispFactor;
    packed.subFraction = unpacked.subFraction;
    memcpy(packed.securityType, unpacked.securityType, sizeof(packed.securityType));
    packed.maturityMont = unpacked.maturityMont;
    packed.maturityYear = unpacked.maturityYear;
    packed.openInterestQty = unpacked.openInterestQty;
    packed.clearedVolume = unpacked.clearedVolume;
    packed.tradingRefDate = unpacked.tradingRefDate;
    memcpy(packed.unitOfMeasure, unpacked.unitOfMeasure, sizeof(packed.unitOfMeasure));
    packed.unitOfMeasureQty = unpacked.unitOfMeasureQty;
    return packed;
  }

  inline static l3_odf_t from_packed(const l3_odf_packed_t& packed) {
    l3_odf_t unpacked = {};
    unpacked.typ = packed.typ;
    unpacked.venue = packed.venue;
    memcpy(unpacked.sym, packed.sym, sizeof(unpacked.sym));
    memcpy(unpacked.asset, packed.asset, sizeof(unpacked.asset));
    memcpy(unpacked.cfiCode, packed.cfiCode, sizeof(unpacked.cfiCode));
    unpacked.high_limit_px = packed.high_limit_px;
    unpacked.low_limit_px = packed.low_limit_px;
    unpacked.pxvar = packed.pxvar;
    unpacked.securityID = packed.securityID;
    unpacked.mDSecurityTradingStatus = packed.mDSecurityTradingStatus;
    unpacked.activation = packed.activation;
    unpacked.expiration = packed.expiration;
    memcpy(unpacked.sec_group, packed.sec_group, sizeof(unpacked.sec_group));
    unpacked.marketSegmentID = packed.marketSegmentID;
    unpacked.matchAlgorithm = packed.matchAlgorithm;
    unpacked.mainFraction = packed.mainFraction;
    unpacked.minPriceIncrement = packed.minPriceIncrement;
    unpacked.minCabPrice = packed.minCabPrice;
    unpacked.priceDisplayFormat = packed.priceDisplayFormat;
    unpacked.updateAction = packed.updateAction;
    unpacked.putOrCall = packed.putOrCall;
    unpacked.strikePrice = packed.strikePrice;
    unpacked.userDefinedInstrument = packed.userDefinedInstrument;
    unpacked.tickRule = packed.tickRule;
    unpacked.noUnderlyingsCount = packed.noUnderlyingsCount;
    memcpy(unpacked.underlyingSecurityID, packed.underlyingSecurityID, sizeof(unpacked.underlyingSecurityID));
    unpacked.dispFactor = packed.dispFactor;
    unpacked.subFraction = packed.subFraction;
    memcpy(unpacked.securityType, packed.securityType, sizeof(unpacked.securityType));
    unpacked.maturityMont = packed.maturityMont;
    unpacked.maturityYear = packed.maturityYear;
    unpacked.openInterestQty = packed.openInterestQty;
    unpacked.clearedVolume = packed.clearedVolume;
    unpacked.tradingRefDate = packed.tradingRefDate;
    memcpy(unpacked.unitOfMeasure, packed.unitOfMeasure, sizeof(unpacked.unitOfMeasure));
    unpacked.unitOfMeasureQty = packed.unitOfMeasureQty;
    return unpacked;
  }

  // l3_sdf_t conversion functions
  inline static l3_sdf_packed_t to_packed(const l3_sdf_t& unpacked) {
    l3_sdf_packed_t packed = {};
    packed.typ = unpacked.typ;
    packed.venue = unpacked.venue;
    memcpy(packed.sym, unpacked.sym, sizeof(packed.sym));
    memcpy(packed.asset, unpacked.asset, sizeof(packed.asset));
    memcpy(packed.cfiCode, unpacked.cfiCode, sizeof(packed.cfiCode));
    packed.high_limit_px = unpacked.high_limit_px;
    packed.low_limit_px = unpacked.low_limit_px;
    packed.securityID = unpacked.securityID;
    packed.securityUpdateAction = unpacked.securityUpdateAction;
    packed.activation = unpacked.activation;
    packed.expiration = unpacked.expiration;
    memcpy(packed.sec_group, unpacked.sec_group, sizeof(packed.sec_group));
    packed.marketSegmentID = unpacked.marketSegmentID;
    packed.mDSecurityTradingStatus = unpacked.mDSecurityTradingStatus;
    packed.matchAlgorithm = unpacked.matchAlgorithm;
    packed.mainFraction = unpacked.mainFraction;
    packed.minPriceIncrement = unpacked.minPriceIncrement;
    packed.priceDisplayFormat = unpacked.priceDisplayFormat;
    packed.userDefinedInstrument = unpacked.userDefinedInstrument;
    packed.tickRule = unpacked.tickRule;
    packed.noLegsCount = unpacked.noLegsCount;
    memcpy(packed.legOptionDelta, unpacked.legOptionDelta, sizeof(packed.legOptionDelta));
    memcpy(packed.legPrice, unpacked.legPrice, sizeof(packed.legPrice));
    memcpy(packed.legRatioQty, unpacked.legRatioQty, sizeof(packed.legRatioQty));
    memcpy(packed.legSecurityID, unpacked.legSecurityID, sizeof(packed.legSecurityID));
    memcpy(packed.legSide, unpacked.legSide, sizeof(packed.legSide));
    packed.dispFactor = unpacked.dispFactor;
    packed.subFraction = unpacked.subFraction;
    memcpy(packed.securityType, unpacked.securityType, sizeof(packed.securityType));
    packed.maturityMont = unpacked.maturityMont;
    packed.maturityYear = unpacked.maturityYear;
    packed.openInterestQty = unpacked.openInterestQty;
    packed.clearedVolume = unpacked.clearedVolume;
    packed.tradingRefDate = unpacked.tradingRefDate;
    memcpy(packed.unitOfMeasure, unpacked.unitOfMeasure, sizeof(packed.unitOfMeasure));
    return packed;
  }

  inline static l3_sdf_t from_packed(const l3_sdf_packed_t& packed) {
    l3_sdf_t unpacked = {};
    unpacked.typ = packed.typ;
    unpacked.venue = packed.venue;
    memcpy(unpacked.sym, packed.sym, sizeof(unpacked.sym));
    memcpy(unpacked.asset, packed.asset, sizeof(unpacked.asset));
    memcpy(unpacked.cfiCode, packed.cfiCode, sizeof(unpacked.cfiCode));
    unpacked.high_limit_px = packed.high_limit_px;
    unpacked.low_limit_px = packed.low_limit_px;
    unpacked.securityID = packed.securityID;
    unpacked.securityUpdateAction = packed.securityUpdateAction;
    unpacked.activation = packed.activation;
    unpacked.expiration = packed.expiration;
    memcpy(unpacked.sec_group, packed.sec_group, sizeof(unpacked.sec_group));
    unpacked.marketSegmentID = packed.marketSegmentID;
    unpacked.mDSecurityTradingStatus = packed.mDSecurityTradingStatus;
    unpacked.matchAlgorithm = packed.matchAlgorithm;
    unpacked.mainFraction = packed.mainFraction;
    unpacked.minPriceIncrement = packed.minPriceIncrement;
    unpacked.priceDisplayFormat = packed.priceDisplayFormat;
    unpacked.userDefinedInstrument = packed.userDefinedInstrument;
    unpacked.tickRule = packed.tickRule;
    unpacked.noLegsCount = packed.noLegsCount;
    memcpy(unpacked.legOptionDelta, packed.legOptionDelta, sizeof(unpacked.legOptionDelta));
    memcpy(unpacked.legPrice, packed.legPrice, sizeof(unpacked.legPrice));
    memcpy(unpacked.legRatioQty, packed.legRatioQty, sizeof(unpacked.legRatioQty));
    memcpy(unpacked.legSecurityID, packed.legSecurityID, sizeof(unpacked.legSecurityID));
    memcpy(unpacked.legSide, packed.legSide, sizeof(unpacked.legSide));
    unpacked.dispFactor = packed.dispFactor;
    unpacked.subFraction = packed.subFraction;
    memcpy(unpacked.securityType, packed.securityType, sizeof(unpacked.securityType));
    unpacked.maturityMont = packed.maturityMont;
    unpacked.maturityYear = packed.maturityYear;
    unpacked.openInterestQty = packed.openInterestQty;
    unpacked.clearedVolume = packed.clearedVolume;
    unpacked.tradingRefDate = packed.tradingRefDate;
    memcpy(unpacked.unitOfMeasure, packed.unitOfMeasure, sizeof(unpacked.unitOfMeasure));
    return unpacked;
  }

  // l3_lim_t conversion functions
  inline static l3_lim_packed_t to_packed(const l3_lim_t& unpacked) {
    l3_lim_packed_t packed = {};
    packed.typ = unpacked.typ;
    packed.venue = unpacked.venue;
    packed.securityID = unpacked.securityID;
    packed.pxh = unpacked.pxh;
    packed.pxl = unpacked.pxl;
    packed.pxvar = unpacked.pxvar;
    memcpy(packed.entryType, unpacked.entryType, sizeof(packed.entryType));
    return packed;
  }

  inline static l3_lim_t from_packed(const l3_lim_packed_t& packed) {
    l3_lim_t unpacked = {};
    unpacked.typ = packed.typ;
    unpacked.venue = packed.venue;
    unpacked.securityID = packed.securityID;
    unpacked.pxh = packed.pxh;
    unpacked.pxl = packed.pxl;
    unpacked.pxvar = packed.pxvar;
    memcpy(unpacked.entryType, packed.entryType, sizeof(unpacked.entryType));
    return unpacked;
  }

  // l3_gap_v2_t conversion functions
  inline static l3_gap_v2_packed_t to_packed(const l3_gap_v2_t& unpacked) {
    l3_gap_v2_packed_t packed = {};
    packed.typ = unpacked.typ;
    packed.venue = unpacked.venue;
    return packed;
  }

  inline static l3_gap_v2_t from_packed(const l3_gap_v2_packed_t& packed) {
    l3_gap_v2_t unpacked = {};
    unpacked.typ = packed.typ;
    unpacked.venue = packed.venue;
    return unpacked;
  }

  // l3_chr_v2_t conversion functions
  inline static l3_chr_v2_packed_t to_packed(const l3_chr_v2_t& unpacked) {
    l3_chr_v2_packed_t packed = {};
    packed.typ = unpacked.typ;
    packed.venue = unpacked.venue;
    return packed;
  }

  inline static l3_chr_v2_t from_packed(const l3_chr_v2_packed_t& packed) {
    l3_chr_v2_t unpacked = {};
    unpacked.typ = packed.typ;
    unpacked.venue = packed.venue;
    return unpacked;
  }

  // l3_eob_t conversion functions
  inline static l3_eob_packed_t to_packed(const l3_eob_t& unpacked) {
    l3_eob_packed_t packed = {};
    packed.typ = unpacked.typ;
    packed.venue = unpacked.venue;
    return packed;
  }

  inline static l3_eob_t from_packed(const l3_eob_packed_t& packed) {
    l3_eob_t unpacked = {};
    unpacked.typ = packed.typ;
    unpacked.venue = packed.venue;
    return unpacked;
  }

  // l3_sim_t conversion functions
  inline static l3_sim_packed_t to_packed(const l3_sim_t& unpacked) {
    l3_sim_packed_t packed = {};
    packed.typ = unpacked.typ;
    return packed;
  }

  inline static l3_sim_t from_packed(const l3_sim_packed_t& packed) {
    l3_sim_t unpacked = {};
    unpacked.typ = packed.typ;
    return unpacked;
  }

  // l3_fenics_sys_event_t conversion functions
  inline static l3_fenics_sys_event_packed_t to_packed(const l3_fenics_sys_event_t& unpacked) {
    l3_fenics_sys_event_packed_t packed = {};
    packed.typ = unpacked.typ;
    packed.venue = unpacked.venue;
    packed.EventCode = unpacked.EventCode;
    return packed;
  }

  inline static l3_fenics_sys_event_t from_packed(const l3_fenics_sys_event_packed_t& packed) {
    l3_fenics_sys_event_t unpacked = {};
    unpacked.typ = packed.typ;
    unpacked.venue = packed.venue;
    unpacked.EventCode = packed.EventCode;
    return unpacked;
  }

  // l3_fenics_bdf_t conversion functions
  inline static l3_fenics_bdf_packed_t to_packed(const l3_fenics_bdf_t& unpacked) {
    l3_fenics_bdf_packed_t packed = {};
    packed.typ = unpacked.typ;
    packed.venue = unpacked.venue;
    packed.InstrumentLocate = unpacked.InstrumentLocate;
    packed.InstrumentId = unpacked.InstrumentId;
    memcpy(packed.EnhancedSymbol, unpacked.EnhancedSymbol, sizeof(packed.EnhancedSymbol));
    memcpy(packed.IndustryIdentifier, unpacked.IndustryIdentifier, sizeof(packed.IndustryIdentifier));
    packed.InstrumentType = unpacked.InstrumentType;
    memcpy(packed.InstrumentSubType, unpacked.InstrumentSubType, sizeof(packed.InstrumentSubType));
    packed.MinimumOrderSize = unpacked.MinimumOrderSize;
    packed.MaximumOrderSize = unpacked.MaximumOrderSize;
    packed.RoundLotSize = unpacked.RoundLotSize;
    packed.PriceType = unpacked.PriceType;
    packed.DecimalPriceTick = unpacked.DecimalPriceTick;
    packed.FractionalPriceTick = unpacked.FractionalPriceTick;
    packed.PriceMultiplier = unpacked.PriceMultiplier;
    memcpy(packed.SymbolSuffix, unpacked.SymbolSuffix, sizeof(packed.SymbolSuffix));
    packed.MatchAlgorithm = unpacked.MatchAlgorithm;
    memcpy(packed.MIC, unpacked.MIC, sizeof(packed.MIC));
    memcpy(packed.CFIcode, unpacked.CFIcode, sizeof(packed.CFIcode));
    memcpy(packed.Currency, unpacked.Currency, sizeof(packed.Currency));
    return packed;
  }

  inline static l3_fenics_bdf_t from_packed(const l3_fenics_bdf_packed_t& packed) {
    l3_fenics_bdf_t unpacked = {};
    unpacked.typ = packed.typ;
    unpacked.venue = packed.venue;
    unpacked.InstrumentLocate = packed.InstrumentLocate;
    unpacked.InstrumentId = packed.InstrumentId;
    memcpy(unpacked.EnhancedSymbol, packed.EnhancedSymbol, sizeof(unpacked.EnhancedSymbol));
    memcpy(unpacked.IndustryIdentifier, packed.IndustryIdentifier, sizeof(unpacked.IndustryIdentifier));
    unpacked.InstrumentType = packed.InstrumentType;
    memcpy(unpacked.InstrumentSubType, packed.InstrumentSubType, sizeof(unpacked.InstrumentSubType));
    unpacked.MinimumOrderSize = packed.MinimumOrderSize;
    unpacked.MaximumOrderSize = packed.MaximumOrderSize;
    unpacked.RoundLotSize = packed.RoundLotSize;
    unpacked.PriceType = packed.PriceType;
    unpacked.DecimalPriceTick = packed.DecimalPriceTick;
    unpacked.FractionalPriceTick = packed.FractionalPriceTick;
    unpacked.PriceMultiplier = packed.PriceMultiplier;
    memcpy(unpacked.SymbolSuffix, packed.SymbolSuffix, sizeof(unpacked.SymbolSuffix));
    unpacked.MatchAlgorithm = packed.MatchAlgorithm;
    memcpy(unpacked.MIC, packed.MIC, sizeof(unpacked.MIC));
    memcpy(unpacked.CFIcode, packed.CFIcode, sizeof(unpacked.CFIcode));
    memcpy(unpacked.Currency, packed.Currency, sizeof(unpacked.Currency));
    return unpacked;
  }

  // l3_fenics_trading_action_t conversion functions
  inline static l3_fenics_trading_action_packed_t to_packed(const l3_fenics_trading_action_t& unpacked) {
    l3_fenics_trading_action_packed_t packed = {};
    packed.typ = unpacked.typ;
    packed.venue = unpacked.venue;
    packed.InstrumentLocate = unpacked.InstrumentLocate;
    packed.InstrumentId = unpacked.InstrumentId;
    packed.TradingState = unpacked.TradingState;
    return packed;
  }

  inline static l3_fenics_trading_action_t from_packed(const l3_fenics_trading_action_packed_t& packed) {
    l3_fenics_trading_action_t unpacked = {};
    unpacked.typ = packed.typ;
    unpacked.venue = packed.venue;
    unpacked.InstrumentLocate = packed.InstrumentLocate;
    unpacked.InstrumentId = packed.InstrumentId;
    unpacked.TradingState = packed.TradingState;
    return unpacked;
  }

  // l3_fenics_instrumentstats_t conversion functions
  inline static l3_fenics_instrumentstats_packed_t to_packed(const l3_fenics_instrumentstats_t& unpacked) {
    l3_fenics_instrumentstats_packed_t packed = {};
    packed.typ = unpacked.typ;
    packed.venue = unpacked.venue;
    packed.Timestamp = unpacked.Timestamp;
    packed.InstrumentLocate = unpacked.InstrumentLocate;
    packed.TradeVolume = unpacked.TradeVolume;
    packed.HighPrice = unpacked.HighPrice;
    packed.LowPrice = unpacked.LowPrice;
    packed.OpenOrders = unpacked.OpenOrders;
    return packed;
  }

  inline static l3_fenics_instrumentstats_t from_packed(const l3_fenics_instrumentstats_packed_t& packed) {
    l3_fenics_instrumentstats_t unpacked = {};
    unpacked.typ = packed.typ;
    unpacked.venue = packed.venue;
    unpacked.Timestamp = packed.Timestamp;
    unpacked.InstrumentLocate = packed.InstrumentLocate;
    unpacked.TradeVolume = packed.TradeVolume;
    unpacked.HighPrice = packed.HighPrice;
    unpacked.LowPrice = packed.LowPrice;
    unpacked.OpenOrders = packed.OpenOrders;
    return unpacked;
  }

  // l3_dealerweb_bookdir_t conversion functions
  inline static l3_dealerweb_bookdir_packed_t to_packed(const l3_dealerweb_bookdir_t& unpacked) {
    l3_dealerweb_bookdir_packed_t packed = {};
    packed.typ = unpacked.typ;
    packed.venue = unpacked.venue;
    packed.TimestampNanoseconds = unpacked.TimestampNanoseconds;
    packed.OrderBookID = unpacked.OrderBookID;
    memcpy(packed.Symbol, unpacked.Symbol, sizeof(packed.Symbol));
    memcpy(packed.SecurityDescription, unpacked.SecurityDescription, sizeof(packed.SecurityDescription));
    memcpy(packed.CUSIP, unpacked.CUSIP, sizeof(packed.CUSIP));
    packed.Product = unpacked.Product;
    packed.ProductSubtype = unpacked.ProductSubtype;
    packed.PriceType = unpacked.PriceType;
    packed.PriceDecimals = unpacked.PriceDecimals;
    packed.YieldDecimals = unpacked.YieldDecimals;
    packed.CouponDecimals = unpacked.CouponDecimals;
    packed.QuantityMultiplier = unpacked.QuantityMultiplier;
    packed.Maturity = unpacked.Maturity;
    packed.Coupon = unpacked.Coupon;
    packed.DatedDate = unpacked.DatedDate;
    packed.IssueDate = unpacked.IssueDate;
    packed.AuctionDate = unpacked.AuctionDate;
    packed.AnnouncementDate = unpacked.AnnouncementDate;
    packed.FirstCouponDate = unpacked.FirstCouponDate;
    packed.SettlementDate = unpacked.SettlementDate;
    packed.Index = unpacked.Index;
    packed.Spread = unpacked.Spread;
    packed.TradingFeatures = unpacked.TradingFeatures;
    packed.MinimumEntryQuantity = unpacked.MinimumEntryQuantity;
    packed.MinimumQuantityIncrement = unpacked.MinimumQuantityIncrement;
    packed.IssuedasBenchmark = unpacked.IssuedasBenchmark;
    packed.PriceTickSize = unpacked.PriceTickSize;
    memcpy(packed.ShortSym, unpacked.ShortSym, sizeof(packed.ShortSym));
    return packed;
  }

  inline static l3_dealerweb_bookdir_t from_packed(const l3_dealerweb_bookdir_packed_t& packed) {
    l3_dealerweb_bookdir_t unpacked = {};
    unpacked.typ = packed.typ;
    unpacked.venue = packed.venue;
    unpacked.TimestampNanoseconds = packed.TimestampNanoseconds;
    unpacked.OrderBookID = packed.OrderBookID;
    memcpy(unpacked.Symbol, packed.Symbol, sizeof(unpacked.Symbol));
    memcpy(unpacked.SecurityDescription, packed.SecurityDescription, sizeof(unpacked.SecurityDescription));
    memcpy(unpacked.CUSIP, packed.CUSIP, sizeof(unpacked.CUSIP));
    unpacked.Product = packed.Product;
    unpacked.ProductSubtype = packed.ProductSubtype;
    unpacked.PriceType = packed.PriceType;
    unpacked.PriceDecimals = packed.PriceDecimals;
    unpacked.YieldDecimals = packed.YieldDecimals;
    unpacked.CouponDecimals = packed.CouponDecimals;
    unpacked.QuantityMultiplier = packed.QuantityMultiplier;
    unpacked.Maturity = packed.Maturity;
    unpacked.Coupon = packed.Coupon;
    unpacked.DatedDate = packed.DatedDate;
    unpacked.IssueDate = packed.IssueDate;
    unpacked.AuctionDate = packed.AuctionDate;
    unpacked.AnnouncementDate = packed.AnnouncementDate;
    unpacked.FirstCouponDate = packed.FirstCouponDate;
    unpacked.SettlementDate = packed.SettlementDate;
    unpacked.Index = packed.Index;
    unpacked.Spread = packed.Spread;
    unpacked.TradingFeatures = packed.TradingFeatures;
    unpacked.MinimumEntryQuantity = packed.MinimumEntryQuantity;
    unpacked.MinimumQuantityIncrement = packed.MinimumQuantityIncrement;
    unpacked.IssuedasBenchmark = packed.IssuedasBenchmark;
    unpacked.PriceTickSize = packed.PriceTickSize;
    memcpy(unpacked.ShortSym, packed.ShortSym, sizeof(unpacked.ShortSym));
    return unpacked;
  }

  // l3_dealerweb_sys_event_t conversion functions
  inline static l3_dealerweb_sys_event_packed_t to_packed(const l3_dealerweb_sys_event_t& unpacked) {
    l3_dealerweb_sys_event_packed_t packed = {};
    packed.typ = unpacked.typ;
    packed.venue = unpacked.venue;
    packed.TimestampNanoseconds = unpacked.TimestampNanoseconds;
    packed.EventCode = unpacked.EventCode;
    packed.EventReason = unpacked.EventReason;
    packed.OrderBookID = unpacked.OrderBookID;
    return packed;
  }

  inline static l3_dealerweb_sys_event_t from_packed(const l3_dealerweb_sys_event_packed_t& packed) {
    l3_dealerweb_sys_event_t unpacked = {};
    unpacked.typ = packed.typ;
    unpacked.venue = packed.venue;
    unpacked.TimestampNanoseconds = packed.TimestampNanoseconds;
    unpacked.EventCode = packed.EventCode;
    unpacked.EventReason = packed.EventReason;
    unpacked.OrderBookID = packed.OrderBookID;
    return unpacked;
  }

  // l3_dealerweb_orderbookstate_t conversion functions
  inline static l3_dealerweb_orderbookstate_packed_t to_packed(const l3_dealerweb_orderbookstate_t& unpacked) {
    l3_dealerweb_orderbookstate_packed_t packed = {};
    packed.typ = unpacked.typ;
    packed.venue = unpacked.venue;
    packed.TimestampNanoseconds = unpacked.TimestampNanoseconds;
    packed.OrderBookID = unpacked.OrderBookID;
    packed.SecurityEventCode = unpacked.SecurityEventCode;
    return packed;
  }

  inline static l3_dealerweb_orderbookstate_t from_packed(const l3_dealerweb_orderbookstate_packed_t& packed) {
    l3_dealerweb_orderbookstate_t unpacked = {};
    unpacked.typ = packed.typ;
    unpacked.venue = packed.venue;
    unpacked.TimestampNanoseconds = packed.TimestampNanoseconds;
    unpacked.OrderBookID = packed.OrderBookID;
    unpacked.SecurityEventCode = packed.SecurityEventCode;
    return unpacked;
  }

  // l3_dealerweb_information_t conversion functions
  inline static l3_dealerweb_information_packed_t to_packed(const l3_dealerweb_information_t& unpacked) {
    l3_dealerweb_information_packed_t packed = {};
    packed.typ = unpacked.typ;
    packed.venue = unpacked.venue;
    packed.TimestampNanoseconds = unpacked.TimestampNanoseconds;
    packed.OrderBookID = unpacked.OrderBookID;
    packed.PrimaryInformation = unpacked.PrimaryInformation;
    packed.SecondaryInformation = unpacked.SecondaryInformation;
    packed.InformationType = unpacked.InformationType;
    return packed;
  }

  inline static l3_dealerweb_information_t from_packed(const l3_dealerweb_information_packed_t& packed) {
    l3_dealerweb_information_t unpacked = {};
    unpacked.typ = packed.typ;
    unpacked.venue = packed.venue;
    unpacked.TimestampNanoseconds = packed.TimestampNanoseconds;
    unpacked.OrderBookID = packed.OrderBookID;
    unpacked.PrimaryInformation = packed.PrimaryInformation;
    unpacked.SecondaryInformation = packed.SecondaryInformation;
    unpacked.InformationType = packed.InformationType;
    return unpacked;
  }

  // l3_dealerweb_brokentrade_t conversion functions
  inline static l3_dealerweb_brokentrade_packed_t to_packed(const l3_dealerweb_brokentrade_t& unpacked) {
    l3_dealerweb_brokentrade_packed_t packed = {};
    packed.typ = unpacked.typ;
    packed.venue = unpacked.venue;
    packed.TimestampNanoseconds = unpacked.TimestampNanoseconds;
    packed.OrderBookID = unpacked.OrderBookID;
    packed.TransactionID = unpacked.TransactionID;
    packed.ExecutedQuantity = unpacked.ExecutedQuantity;
    memcpy(packed.MatchID, unpacked.MatchID, sizeof(packed.MatchID));
    packed.TradePrice = unpacked.TradePrice;
    return packed;
  }

  inline static l3_dealerweb_brokentrade_t from_packed(const l3_dealerweb_brokentrade_packed_t& packed) {
    l3_dealerweb_brokentrade_t unpacked = {};
    unpacked.typ = packed.typ;
    unpacked.venue = packed.venue;
    unpacked.TimestampNanoseconds = packed.TimestampNanoseconds;
    unpacked.OrderBookID = packed.OrderBookID;
    unpacked.TransactionID = packed.TransactionID;
    unpacked.ExecutedQuantity = packed.ExecutedQuantity;
    memcpy(unpacked.MatchID, packed.MatchID, sizeof(unpacked.MatchID));
    unpacked.TradePrice = packed.TradePrice;
    return unpacked;
  }

  //
  // UNION
  //

  typedef std::variant<
      l3_mbo_v2_t,
      l3_mbo_trd_v2_t,
      l3_mbo_snap_t,
      l3_mbp_t,
      l3_mbp_trd_t,
      l3_som_t,
      l3_volume_t,
      l3_interval_t,
      l3_qlen_t,
      l3_vol_t,
      l3_sst_t,
      l3_fdf_t,
      l3_odf_t,
      l3_sdf_t,
      l3_lim_t,
      l3_gap_v2_t,
      l3_chr_v2_t,
      l3_eob_t,
      l3_sim_t,
      l3_fenics_sys_event_t,
      l3_fenics_bdf_t,
      l3_fenics_trading_action_t,
      l3_fenics_instrumentstats_t,
      l3_dealerweb_bookdir_t,
      l3_dealerweb_sys_event_t,
      l3_dealerweb_orderbookstate_t,
      l3_dealerweb_information_t,
      l3_dealerweb_brokentrade_t
      >
      l3_t;

  //
  // IO
  //

  inline static void
  write_record_l3(const char *dat, size_t len, gzFile f)
  {
    size_t num_wr = gzfwrite(dat, len, 1, f);
    if (num_wr != 1)
    {
      perror("fwrite");
      ASSERT(0, "error while writing");
    }
  }

  inline static void write_l3(gzFile outf, const l3_t &l3)
  {
    size_t datsz;

    if (std::holds_alternative<l3_mbo_v2_t>(l3))
    {
      const auto &m = std::get<l3_mbo_v2_t>(l3);
      auto packed = to_packed(m);
      packed.typ = en::l3::MBO_V2;
      datsz = sizeof(l3_mbo_v2_packed_t);
      write_record_l3((const char *)&packed, datsz, outf);
    }
    else if (std::holds_alternative<l3_mbo_trd_v2_t>(l3))
    {
      const auto &m = std::get<l3_mbo_trd_v2_t>(l3);
      auto packed = to_packed(m);
      packed.typ = en::l3::MBOT_V2;
      datsz = sizeof(l3_mbo_trd_v2_packed_t);
      write_record_l3((const char *)&packed, datsz, outf);
    }
    else if (std::holds_alternative<l3_mbo_snap_t>(l3))
    {
      const auto &m = std::get<l3_mbo_snap_t>(l3);
      auto packed = to_packed(m);
      packed.typ = en::l3::MBOS;
      datsz = sizeof(l3_mbo_snap_packed_t);
      write_record_l3((const char *)&packed, datsz, outf);
    }
    else if (std::holds_alternative<l3_mbp_t>(l3))
    {
      const auto &m = std::get<l3_mbp_t>(l3);
      auto packed = to_packed(m);
      packed.typ = en::l3::MBP;
      datsz = sizeof(l3_mbp_packed_t);
      write_record_l3((const char *)&packed, datsz, outf);
    }
    else if (std::holds_alternative<l3_mbp_trd_t>(l3))
    {
      const auto &m = std::get<l3_mbp_trd_t>(l3);
      auto packed = to_packed(m);
      packed.typ = en::l3::MBPT;
      datsz = sizeof(l3_mbp_trd_packed_t);
      write_record_l3((const char *)&packed, datsz, outf);
    }
    else if (std::holds_alternative<l3_som_t>(l3))
    {
      const auto &m = std::get<l3_som_t>(l3);
      auto packed = to_packed(m);
      packed.typ = en::l3::SOM;
      datsz = sizeof(l3_som_packed_t);
      write_record_l3((const char *)&packed, datsz, outf);
    }
    else if (std::holds_alternative<l3_volume_t>(l3))
    {
      const auto &m = std::get<l3_volume_t>(l3);
      auto packed = to_packed(m);
      packed.typ = en::l3::VOLUME;
      datsz = sizeof(l3_volume_packed_t);
      write_record_l3((const char *)&packed, datsz, outf);
    }
    else if (std::holds_alternative<l3_interval_t>(l3))
    {
      const auto &m = std::get<l3_interval_t>(l3);
      auto packed = to_packed(m);
      packed.typ = en::l3::INTERVAL;
      datsz = sizeof(l3_interval_packed_t);
      write_record_l3((const char *)&packed, datsz, outf);
    }
    else if (std::holds_alternative<l3_qlen_t>(l3))
    {
      const auto &m = std::get<l3_qlen_t>(l3);
      auto packed = to_packed(m);
      packed.typ = en::l3::QLEN;
      datsz = sizeof(l3_qlen_packed_t);
      write_record_l3((const char *)&packed, datsz, outf);
    }
    else if (std::holds_alternative<l3_vol_t>(l3))
    {
      const auto &m = std::get<l3_vol_t>(l3);
      auto packed = to_packed(m);
      packed.typ = en::l3::VOL;
      datsz = sizeof(l3_vol_packed_t);
      write_record_l3((const char *)&packed, datsz, outf);
    }
    else if (std::holds_alternative<l3_sst_t>(l3))
    {
      const auto &m = std::get<l3_sst_t>(l3);
      auto packed = to_packed(m);
      packed.typ = en::l3::SST;
      datsz = sizeof(l3_sst_packed_t);
      write_record_l3((const char *)&packed, datsz, outf);
    }
    else if (std::holds_alternative<l3_fdf_t>(l3))
    {
      const auto &m = std::get<l3_fdf_t>(l3);
      auto packed = to_packed(m);
      packed.typ = en::l3::FDF;
      datsz = sizeof(l3_fdf_packed_t);
      write_record_l3((const char *)&packed, datsz, outf);
    }
    else if (std::holds_alternative<l3_odf_t>(l3))
    {
      const auto &m = std::get<l3_odf_t>(l3);
      auto packed = to_packed(m);
      packed.typ = en::l3::ODF;
      datsz = sizeof(l3_odf_packed_t);
      write_record_l3((const char *)&packed, datsz, outf);
    }
    else if (std::holds_alternative<l3_sdf_t>(l3))
    {
      const auto &m = std::get<l3_sdf_t>(l3);
      auto packed = to_packed(m);
      packed.typ = en::l3::SDF;
      datsz = sizeof(l3_sdf_packed_t);
      write_record_l3((const char *)&packed, datsz, outf);
    }
    else if (std::holds_alternative<l3_lim_t>(l3))
    {
      const auto &m = std::get<l3_lim_t>(l3);
      auto packed = to_packed(m);
      packed.typ = en::l3::LIM;
      datsz = sizeof(l3_lim_packed_t);
      write_record_l3((const char *)&packed, datsz, outf);
    }
    else if (std::holds_alternative<l3_gap_v2_t>(l3))
    {
      const auto &m = std::get<l3_gap_v2_t>(l3);
      auto packed = to_packed(m);
      packed.typ = en::l3::GAP_V2;
      datsz = sizeof(l3_gap_v2_packed_t);
      write_record_l3((const char *)&packed, datsz, outf);
    }
    else if (std::holds_alternative<l3_chr_v2_t>(l3))
    {
      const auto &m = std::get<l3_chr_v2_t>(l3);
      auto packed = to_packed(m);
      packed.typ = en::l3::CHR_V2;
      datsz = sizeof(l3_chr_v2_packed_t);
      write_record_l3((const char *)&packed, datsz, outf);
    }
    else if (std::holds_alternative<l3_eob_t>(l3))
    {
      const auto &m = std::get<l3_eob_t>(l3);
      auto packed = to_packed(m);
      packed.typ = en::l3::EOB;
      datsz = sizeof(l3_eob_packed_t);
      write_record_l3((const char *)&packed, datsz, outf);
    }
    else if (std::holds_alternative<l3_sim_t>(l3))
    {
      ERR("not implemented");
    }
    else if (std::holds_alternative<l3_fenics_sys_event_t>(l3))
    {
      const auto &m = std::get<l3_fenics_sys_event_t>(l3);
      auto packed = to_packed(m);
      packed.typ = en::l3::FENICS_SYSTEMEVENT;
      datsz = sizeof(l3_fenics_sys_event_packed_t);
      write_record_l3((const char *)&packed, datsz, outf);
    }
    else if (std::holds_alternative<l3_fenics_bdf_t>(l3))
    {
      const auto &m = std::get<l3_fenics_bdf_t>(l3);
      auto packed = to_packed(m);
      packed.typ = en::l3::FENICS_BDF;
      datsz = sizeof(l3_fenics_bdf_packed_t);
      write_record_l3((const char *)&packed, datsz, outf);
    }
    else if (std::holds_alternative<l3_fenics_trading_action_t>(l3))
    {
      const auto &m = std::get<l3_fenics_trading_action_t>(l3);
      auto packed = to_packed(m);
      packed.typ = en::l3::FENICS_INSTRUMENTTRADINGACTION;
      datsz = sizeof(l3_fenics_trading_action_packed_t);
      write_record_l3((const char *)&packed, datsz, outf);
    }
    else if (std::holds_alternative<l3_fenics_instrumentstats_t>(l3))
    {
      const auto &m = std::get<l3_fenics_instrumentstats_t>(l3);
      auto packed = to_packed(m);
      packed.typ = en::l3::FENICS_INSTRUMENTSTATS;
      datsz = sizeof(l3_fenics_instrumentstats_packed_t);
      write_record_l3((const char *)&packed, datsz, outf);
    }
    else if (std::holds_alternative<l3_dealerweb_bookdir_t>(l3))
    {
      const auto &m = std::get<l3_dealerweb_bookdir_t>(l3);
      auto packed = to_packed(m);
      packed.typ = en::l3::DEALERWEB_BOOKDIR;
      datsz = sizeof(l3_dealerweb_bookdir_packed_t);
      write_record_l3((const char *)&packed, datsz, outf);
    }
    else if (std::holds_alternative<l3_dealerweb_sys_event_t>(l3))
    {
      const auto &m = std::get<l3_dealerweb_sys_event_t>(l3);
      auto packed = to_packed(m);
      packed.typ = en::l3::DEALERWEB_SYSEVENT;
      datsz = sizeof(l3_dealerweb_sys_event_packed_t);
      write_record_l3((const char *)&packed, datsz, outf);
    }
    else if (std::holds_alternative<l3_dealerweb_orderbookstate_t>(l3))
    {
      const auto &m = std::get<l3_dealerweb_orderbookstate_t>(l3);
      auto packed = to_packed(m);
      packed.typ = en::l3::DEALERWEB_ORDERBOOKSTATE;
      datsz = sizeof(l3_dealerweb_orderbookstate_packed_t);
      write_record_l3((const char *)&packed, datsz, outf);
    }
    else if (std::holds_alternative<l3_dealerweb_information_t>(l3))
    {
      const auto &m = std::get<l3_dealerweb_information_t>(l3);
      auto packed = to_packed(m);
      packed.typ = en::l3::DEALERWEB_INFORMATION;
      datsz = sizeof(l3_dealerweb_information_packed_t);
      write_record_l3((const char *)&packed, datsz, outf);
    }
    else if (std::holds_alternative<l3_dealerweb_brokentrade_t>(l3))
    {
      const auto &m = std::get<l3_dealerweb_brokentrade_t>(l3);
      auto packed = to_packed(m);
      packed.typ = en::l3::DEALERWEB_BROKENTRADE;
      datsz = sizeof(l3_dealerweb_brokentrade_packed_t);
      write_record_l3((const char *)&packed, datsz, outf);
    }
    else
    {
      ERR("cant encode data type");
    }

  }

  inline static bool
  read_l3(gzFile inf, l3_t &l3, uint64_t &ts)
  {
    l3_typ_t rectyp;

    auto n = gzread(inf, &rectyp, sizeof(rectyp));
    if (n < int(sizeof(rectyp)))
      return false;

    auto read_rec = [inf, rectyp](auto &rec)
    {
      auto sz_to_read = sizeof(rec) - sizeof(rectyp);
      rec.typ = rectyp;
      if (!sz_to_read)
        return true;
      char buf[sizeof(rec)];
      // should not have to read into buffer first
      memset(buf, 0, sizeof(buf));
      memcpy(buf, &rectyp, sizeof(rectyp));
      auto n = gzread(inf, buf + sizeof(rectyp), sz_to_read);
      // auto n = gzread(inf, &rec + sizeof(rectyp), sz_to_read); does not work?
      if (n < int(sz_to_read))
        return false;
      memcpy(&rec, buf, sizeof(rec));
      return true;
    };

    switch (rectyp)
    {
    case en::l3::MBO_V2:
    {
      l3_mbo_v2_packed_t mbo_packed;
      if (!read_rec(mbo_packed))
        return false;
      auto mbo = from_packed(mbo_packed);
      l3 = mbo;
      ts = mbo.transactTime;
      break;
    }
    case en::l3::MBOT_V2:
    {
      l3_mbo_trd_v2_packed_t mbot_packed;
      if (!read_rec(mbot_packed))
        return false;
      auto mbot = from_packed(mbot_packed);
      l3 = mbot;
      ts = mbot.transactTime;
      break;
    }
    case en::l3::MBOS:
    {
      l3_mbo_snap_packed_t mbos_packed;
      if (!read_rec(mbos_packed))
        return false;
      auto mbos = from_packed(mbos_packed);
      l3 = mbos;
      ts = mbos.transactTime;
      break;
    }
    case en::l3::MBP:
    {
      l3_mbp_packed_t mbp_packed;
      if (!read_rec(mbp_packed))
        return false;
      auto mbp = from_packed(mbp_packed);
      l3 = mbp;
      ts = mbp.transactTime;
      break;
    }
    case en::l3::MBPT:
    {
      l3_mbp_trd_packed_t mbpt_packed;
      if (!read_rec(mbpt_packed))
        return false;
      auto mbpt = from_packed(mbpt_packed);
      l3 = mbpt;
      ts = mbpt.transactTime;
      break;
    }
    case en::l3::SOM:
    {
      l3_som_packed_t som_packed;
      if (!read_rec(som_packed))
        return false;
      auto som = from_packed(som_packed);
      l3 = som;
      ts = 0;
      break;
    }
    case en::l3::VOLUME:
    {
      l3_volume_packed_t volume_packed;
      if (!read_rec(volume_packed))
        return false;
      auto volume = from_packed(volume_packed);
      l3 = volume;
      ts = volume.t0;
      break;
    }
    case en::l3::INTERVAL:
    {
      l3_interval_packed_t interval_packed;
      if (!read_rec(interval_packed))
        return false;
      auto interval = from_packed(interval_packed);
      l3 = interval;
      ts = 0;
      break;
    }
    case en::l3::QLEN:
    {
      l3_qlen_packed_t qlen_packed;
      if (!read_rec(qlen_packed))
        return false;
      auto qlen = from_packed(qlen_packed);
      l3 = qlen;
      ts = 0;
      break;
    }
    case en::l3::VOL:
    {
      l3_vol_packed_t vol_packed;
      if (!read_rec(vol_packed))
        return false;
      auto vol = from_packed(vol_packed);
      l3 = vol;
      ts = vol.txtim;
      break;
    }
    case en::l3::SST:
    {
      l3_sst_packed_t sst_packed;
      if (!read_rec(sst_packed))
        return false;
      auto sst = from_packed(sst_packed);
      l3 = sst;
      ts = sst.txtim;
      break;
    }
    case en::l3::FDF:
    {
      l3_fdf_packed_t fdf_packed;
      if (!read_rec(fdf_packed))
        return false;
      auto fdf = from_packed(fdf_packed);
      l3 = fdf;
      ts = 0;
      break;
    }
    case en::l3::ODF:
    {
      l3_odf_packed_t odf_packed;
      if (!read_rec(odf_packed))
        return false;
      auto odf = from_packed(odf_packed);
      l3 = odf;
      ts = 0;
      break;
    }
    case en::l3::SDF:
    {
      l3_sdf_packed_t sdf_packed;
      if (!read_rec(sdf_packed))
        return false;
      auto sdf = from_packed(sdf_packed);
      l3 = sdf;
      ts = 0;
      break;
    }
    case en::l3::LIM:
    {
      l3_lim_packed_t lim_packed;
      if (!read_rec(lim_packed))
        return false;
      auto lim = from_packed(lim_packed);
      l3 = lim;
      ts = 0;
      break;
    }
    case en::l3::CHR_V1:
    {
      // l3_chr_v1_t chr;
      // if (!read_rec(chr))
      //   return false;
      // l3_chr_v2_t chr2;
      // memcpy(&chr2, &chr, sizeof(chr));
      // chr2.venue = en::x::CMEMD;
      // l3 = chr2;
      // ts = 0;
      // break;
      SNGH;
      break;
    }
    case en::l3::CHR_V2:
    {
      l3_chr_v2_packed_t chr_packed;
      if (!read_rec(chr_packed))
        return false;
      auto chr = from_packed(chr_packed);
      l3 = chr;
      ts = 0;
      break;
    }
    case en::l3::GAP_V2:
    {
      l3_gap_v2_packed_t gap_packed;
      if (!read_rec(gap_packed))
        return false;
      auto gap = from_packed(gap_packed);
      l3 = gap;
      ts = 0;
      break;
    }
    case en::l3::EOB:
    {
      l3_eob_packed_t eob_packed;
      if (!read_rec(eob_packed))
        return false;
      auto eob = from_packed(eob_packed);
      l3 = eob;
      ts = 0;
      break;
    }
    case en::l3::FENICS_SYSTEMEVENT:
    {
      l3_fenics_sys_event_packed_t fenics_sys_event_packed;
      if (!read_rec(fenics_sys_event_packed))
        return false;
      auto fenics_sys_event = from_packed(fenics_sys_event_packed);
      l3 = fenics_sys_event;
      ts = 0;
      break;
    }
    case en::l3::FENICS_BDF:
    {
      l3_fenics_bdf_packed_t fenics_bdf_packed;
      if (!read_rec(fenics_bdf_packed))
        return false;
      auto fenics_bdf = from_packed(fenics_bdf_packed);
      l3 = fenics_bdf;
      ts = 0;
      break;
    }
    case en::l3::FENICS_INSTRUMENTTRADINGACTION:
    {
      l3_fenics_trading_action_packed_t fenics_trading_action_packed;
      if (!read_rec(fenics_trading_action_packed))
        return false;
      auto fenics_trading_action = from_packed(fenics_trading_action_packed);
      l3 = fenics_trading_action;
      ts = 0;
      break;
    }
    case en::l3::FENICS_INSTRUMENTSTATS:
    {
      l3_fenics_instrumentstats_packed_t fenics_instrumentstats_packed;
      if (!read_rec(fenics_instrumentstats_packed))
        return false;
      auto fenics_instrumentstats = from_packed(fenics_instrumentstats_packed);
      l3 = fenics_instrumentstats;
      ts = fenics_instrumentstats.Timestamp;
      break;
    }
    case en::l3::DEALERWEB_BOOKDIR:
    {
      l3_dealerweb_bookdir_packed_t dealerweb_bookdir_packed;
      if (!read_rec(dealerweb_bookdir_packed))
        return false;
      auto dealerweb_bookdir = from_packed(dealerweb_bookdir_packed);
      l3 = dealerweb_bookdir;
      ts = dealerweb_bookdir.TimestampNanoseconds;
      break;
    }
    case en::l3::DEALERWEB_SYSEVENT:
    {
      l3_dealerweb_sys_event_packed_t dealerweb_sys_event_packed;
      if (!read_rec(dealerweb_sys_event_packed))
        return false;
      auto dealerweb_sys_event = from_packed(dealerweb_sys_event_packed);
      l3 = dealerweb_sys_event;
      ts = dealerweb_sys_event.TimestampNanoseconds;
      break;
    }
    case en::l3::DEALERWEB_ORDERBOOKSTATE:
    {
      l3_dealerweb_orderbookstate_packed_t dealerweb_orderbookstate_packed;
      if (!read_rec(dealerweb_orderbookstate_packed))
        return false;
      auto dealerweb_orderbookstate = from_packed(dealerweb_orderbookstate_packed);
      l3 = dealerweb_orderbookstate;
      ts = dealerweb_orderbookstate.TimestampNanoseconds;
      break;
    }
    case en::l3::DEALERWEB_INFORMATION:
    {
      l3_dealerweb_information_packed_t dealerweb_information_packed;
      if (!read_rec(dealerweb_information_packed))
        return false;
      auto dealerweb_information = from_packed(dealerweb_information_packed);
      l3 = dealerweb_information;
      ts = dealerweb_information.TimestampNanoseconds;
      break;
    }
    case en::l3::DEALERWEB_BROKENTRADE:
    {
      l3_dealerweb_brokentrade_packed_t dealerweb_brokentrade_packed;
      if (!read_rec(dealerweb_brokentrade_packed))
        return false;
      auto dealerweb_brokentrade = from_packed(dealerweb_brokentrade_packed);
      l3 = dealerweb_brokentrade;
      ts = dealerweb_brokentrade.TimestampNanoseconds;
      break;
    }
    
    default:
      ERR("cant decode data type");
    }
    return true;
  }

}

static 
inline std::ostream &operator<<(std::ostream &os, const bfile::l3_volume_t &volume) {
  os << "l3_volume_t { "
     << "typ: " << static_cast<int>(volume.typ) << ", "
     << "t0: " << volume.t0 << ", "
     << "th_400: " << volume.th_400 << ", "
     << "th_1000: " << volume.th_1000 << ", "
     << "th_5000: " << volume.th_5000 << ", "
     << "vol_sum: " << volume.vol_sum
     << " }";
  return os;
}

static 
inline std::ostream &operator<<(std::ostream &os, const bfile::l3_som_t &som) {
  auto tim_str = chutil::Time::from_epoch(som.som_tim_epoch).to_string();
  os << "l3_som_t { "
     << "name: " << som.name << ", "
     << "venue: " << en::to_string(en::x(som.venue)) << ", "
     << "somcode: " << en::to_string(en::som(som.somcode)) << ", "
     << "som_tim_epoch: " << som.som_tim_epoch << ", "
     << "handlerend_tim_epoch: " << som.handlerend_tim_epoch << ", "
     << "tstr: " << tim_str << ", "
     << "xordid: " << som.xordid << ", "
     << "sordid: " << som.sordid << ", "
     << "symid: " << som.symid << ", "
     << "sz: " << som.sz << ", "
     << "side: " << en::to_string(en::bs(som.side)) << ", "
     << "px: " << som.px << ", "
     << "stbf: " << som.stbf
     << " }";
  return os;
}

static 
inline std::ostream &operator<<(std::ostream &os, const bfile::l3_interval_t &interval) {
  os << "l3_interval_t { "
     << "id: " << en::to_string((en::intreval) interval.id) << ", "
     << "t0: " << interval.t0 << ", "
     << "t1: " << interval.t1 << ", "
     << "thand: " << interval.t_handler 
     << " }";
  return os;
}

static 
inline std::ostream &operator<<(std::ostream &os, const bfile::l3_qlen_t &qlen) {
  os << "l3_qlen_t { "
     << "t0: " << qlen.t0 << ", "
     << "name: " << qlen.name << ", "
     << "qlen: " << qlen.size << ", "
     << "msg: " << qlen.msg << ", "
     << "nv_ctx: " << qlen.nv_ctx << ", "
     << "v_ctx: " << qlen.v_ctx
     << " }";
  return os;
}

static 
inline std::ostream &operator<<(std::ostream &os, const bfile::l3_mbo_v2_t &mbo) {
  os << "l3_mbo_v2_t { "
     << "typ: " << static_cast<int>(mbo.typ) << ", "
     << "venue: " << en::to_string(en::x(mbo.venue)) << ", "
     << "transactTime: " << mbo.transactTime << ", "
     << "sendingTime: " << mbo.sendingTime << ", "
     << "handlerendtim: " << mbo.handlerendtim << ", "
     << "orderUpdateAction: " << static_cast<int>(mbo.orderUpdateAction) << ", "
     << "securityID: " << mbo.securityID << ", "
     << "orderID: " << mbo.orderID << ", "
     << "priority: " << mbo.priority << ", "
     << "pxd: " << mbo.pxd << ", "
     << "side: " << mbo.side << ", "
     << "displayQty: " << mbo.displayQty << ", "
     << "endOfEvent: " << mbo.endOfEvent << ", "
     << "lastQuote: " << mbo.lastQuote << ", "
     << "recovery: " << mbo.recovery << ", "
     << "order_flags: " << static_cast<int>(mbo.order_flags) << ", "
     << "visibility_group: " << static_cast<int>(mbo.visibility_group)
     << " }";
  return os;
}

static
inline std::ostream &operator<<(std::ostream &os, const bfile::l3_mbo_trd_v2_t &mbot) {
  os << "l3_mbo_trd_v2_t { "
     << "typ: " << static_cast<int>(mbot.typ) << ", "
     << "venue: " << en::to_string(en::x(mbot.venue)) << ", "
     << "recv_time: " << mbot.recv_time << ", "
     << "transactTime: " << mbot.transactTime << ", "
     << "sendingTime: " << mbot.sendingTime << ", "
     << "handlerendtim: " << mbot.handlerendtim << ", "
     << "lastQty: " << mbot.lastQty << ", "
     << "orderID: " << mbot.orderID << ", "
     << "lastTrade: " << mbot.lastTrade << ", "
     << "endOfEvent: " << mbot.endOfEvent
     << " }";
  return os;
}

// to_string() helpers for use with printf-style logging
static
inline std::string to_string(const bfile::l3_mbo_v2_t &mbo) {
  std::ostringstream oss;
  oss << mbo;
  return oss.str();
}

static
inline std::string to_string(const bfile::l3_mbo_trd_v2_t &mbot) {
  std::ostringstream oss;
  oss << mbot;
  return oss.str();
}

static 
inline std::ostream &operator<<(std::ostream &os, const bfile::l3_fdf_t &fdf) {
  os << "l3_fdf_t { "
     << "typ: " << static_cast<int>(fdf.typ) << ", "
     << "venue: " << en::to_string(en::x(fdf.venue)) << ", "
     << "sym: " << fdf.sym << ", "
     << "asset: " << fdf.asset << ", "
     << "cfiCode: " << fdf.cfiCode << ", "
     << "high_limit_px: " << fdf.high_limit_px << ", "
     << "low_limit_px: " << fdf.low_limit_px << ", "
     << "pxvar: " << fdf.pxvar << ", "
     << "securityID: " << fdf.securityID << ", "
     << "mDSecurityTradingStatus: " << static_cast<int>(fdf.mDSecurityTradingStatus) << ", "
     << "activation: " << fdf.activation << ", "
     << "expiration: " << fdf.expiration << ", "
     << "securityGroup: " << fdf.securityGroup << ", "
     << "marketSegmentID: " << static_cast<int>(fdf.marketSegmentID) << ", "
     << "matchAlgorithm: " << fdf.matchAlgorithm << ", "
     << "mainFraction: " << static_cast<int>(fdf.mainFraction) << ", "
     << "minPriceIncrement: " << fdf.minPriceIncrement << ", "
     << "priceDisplayFormat: " << static_cast<int>(fdf.priceDisplayFormat) << ", "
     << "userDefinedInstrument: " << fdf.userDefinedInstrument << ", "
     << "updateAction: " << fdf.updateAction << ", "
     << "dispFactor: " << fdf.dispFactor << ", "
     << "subFraction: " << static_cast<int>(fdf.subFraction) << ", "
     << "securityType: " << fdf.securityType << ", "
     << "maturityMont: " << static_cast<int>(fdf.maturityMont) << ", "
     << "maturityYear: " << fdf.maturityYear << ", "
     << "openInterestQty: " << fdf.openInterestQty << ", "
     << "clearedVolume: " << fdf.clearedVolume << ", "
     << "tradingRefDate: " << fdf.tradingRefDate << ", "
     << "unitOfMeasure: " << fdf.unitOfMeasure << ", "
     << "unitOfMeasureQty: " << fdf.unitOfMeasureQty
     << " }";
  return os;
}

static 
inline std::ostream &operator<<(std::ostream &os, const bfile::l3_fenics_bdf_t &bdf) {
  os << "l3_fenics_bdf_t { "
     << "typ: " << static_cast<int>(bdf.typ) << ", "
     << "venue: " << en::to_string(en::x(bdf.venue)) << ", "
     << "InstrumentLocate: " << bdf.InstrumentLocate << ", "
     << "InstrumentId: " << bdf.InstrumentId << ", "
     << "EnhancedSymbol: " << bdf.EnhancedSymbol << ", "
     << "IndustryIdentifier: " << bdf.IndustryIdentifier << ", "
     << "InstrumentType: " << bdf.InstrumentType << ", "
     << "InstrumentSubType: " << bdf.InstrumentSubType << ", "
     << "MinimumOrderSize: " << bdf.MinimumOrderSize << ", "
     << "MaximumOrderSize: " << bdf.MaximumOrderSize << ", "
     << "RoundLotSize: " << bdf.RoundLotSize << ", "
     << "PriceType: " << bdf.PriceType << ", "
     << "DecimalPriceTick: " << bdf.DecimalPriceTick << ", "
     << "FractionalPriceTick: " << bdf.FractionalPriceTick << ", "
     << "PriceMultiplier: " << bdf.PriceMultiplier << ", "
     << "SymbolSuffix: " << bdf.SymbolSuffix << ", "
     << "MatchAlgorithm: " << bdf.MatchAlgorithm << ", "
     << "MIC: " << bdf.MIC << ", "
     << "CFIcode: " << bdf.CFIcode << ", "
     << "Currency: " << bdf.Currency
     << " }";
  return os;
}

static 
inline std::ostream &operator<<(std::ostream &os, const bfile::l3_dealerweb_bookdir_t &bookdir) {
  os << "l3_dealerweb_bookdir_t { "
     << "typ: " << static_cast<int>(bookdir.typ) << ", "
     << "venue: " << en::to_string(en::x(bookdir.venue)) << ", "
     << "TimestampNanoseconds: " << bookdir.TimestampNanoseconds << ", "
     << "OrderBookID: " << bookdir.OrderBookID << ", "
     << "Symbol: " << bookdir.Symbol << ", "
     << "SecurityDescription: " << bookdir.SecurityDescription << ", "
     << "CUSIP: " << bookdir.CUSIP << ", "
     << "Product: " << bookdir.Product << ", "
     << "ProductSubtype: " << bookdir.ProductSubtype << ", "
     << "PriceType: " << bookdir.PriceType << ", "
     << "PriceDecimals: " << bookdir.PriceDecimals << ", "
     << "YieldDecimals: " << bookdir.YieldDecimals << ", "
     << "CouponDecimals: " << bookdir.CouponDecimals << ", "
     << "QuantityMultiplier: " << bookdir.QuantityMultiplier << ", "
     << "Maturity: " << bookdir.Maturity << ", "
     << "Coupon: " << bookdir.Coupon << ", "
     << "DatedDate: " << bookdir.DatedDate << ", "
     << "IssueDate: " << bookdir.IssueDate << ", "
     << "AuctionDate: " << bookdir.AuctionDate << ", "
     << "AnnouncementDate: " << bookdir.AnnouncementDate << ", "
     << "FirstCouponDate: " << bookdir.FirstCouponDate << ", "
     << "SettlementDate: " << bookdir.SettlementDate << ", "
     << "Index: " << bookdir.Index << ", "
     << "Spread: " << bookdir.Spread << ", "
     << "TradingFeatures: " << bookdir.TradingFeatures << ", "
     << "MinimumEntryQuantity: " << bookdir.MinimumEntryQuantity << ", "
     << "MinimumQuantityIncrement: " << bookdir.MinimumQuantityIncrement << ", "
     << "IssuedasBenchmark: " << bookdir.IssuedasBenchmark << ", "
     << "PriceTickSize: " << bookdir.PriceTickSize << ", "
     << "ShortSym: " << bookdir.ShortSym
     << " }";
  return os;
}