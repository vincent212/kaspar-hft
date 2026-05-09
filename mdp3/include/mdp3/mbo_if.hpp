#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <cstdint>
#include <math.h>

namespace mdp3
{

    struct feed_handler_if
    {

        virtual void disable_mbo(bool disable) noexcept = 0;

        virtual void set_max_mbp_level(uint32_t max_mbp_level) noexcept = 0;

        virtual void MDIncrementalRefreshBook(
            uint64_t recv_time,
            uint32_t msgSeqNum,
            uint64_t transactTime,
            uint64_t sendingTime,
            int32_t securityID,
            int64_t px_mantissa,
            int8_t px_exponent,
            char side,
            int32_t sz,
            int32_t numorders,
            uint8_t pxlevel,
            bool endOfEvent,
            bool recovery) noexcept = 0;

        virtual void MDIncrementalRefreshBook(
            uint64_t recv_time,
            uint32_t msgSeqNum,
            uint64_t transactTime,
            uint64_t sendingTime,
            int32_t securityID,
            int64_t px_mantissa,
            int8_t px_exponent,
            char side,
            int32_t displayQty,
            uint64_t orderID,
            uint8_t orderUpdateAction,
            uint64_t priority,
            bool lastQuote,
            bool endOfEvent,
            bool recovery) noexcept = 0;

        virtual void MDIncrementalRefreshTradeSummary(
            uint64_t recv_time,
            uint32_t msgSeqNum,
            uint64_t transactTime,
            uint64_t sendingTime,
            int32_t securityID,
            int64_t px_mantissa,
            int8_t px_exponent,
            char side,
            uint8_t aggressor_side,
            int32_t sz,
            int32_t numorders,
            bool lastTrade,
            bool endofEvent) noexcept = 0;

        virtual void MDIncrementalRefreshTradeSummary(
            uint64_t recv_time,
            uint32_t msgSeqNum,
            uint64_t transactTime,
            uint64_t sendingTime,
            int32_t lastQty,
            uint64_t orderID,
            bool lastTrade,
            bool endofEvent) noexcept = 0;

        virtual void MDIncrementalRefreshSessionStatistics(
            uint32_t msgSeqNum,
            uint64_t transactTime,
            uint64_t sendingTime,
            uint32_t secid,
            uint8_t openCloseSettleFlag,
            int64_t px_mantissa,
            int64_t px_exponent,
            uint8_t updateAction,
            char entryType) noexcept = 0;

        virtual void MDIncrementalRefreshDailyStatistics(
            uint32_t msgSeqNum,
            uint64_t transactTime,
            uint64_t sendingTime,
            uint32_t secid,
            int64_t px_mantissa,
            int8_t px_exponent,
            int32_t size,
            char side,
            bool finalDaily,
            bool intraday,
            uint8_t updateAction,
            uint16_t tradingReferenceDate) noexcept = 0;

        virtual void MDInstrumentDefinitionFuture(
            uint32_t msgSeqNum,
            uint64_t sendingTime,
            char *sym,
            char *asset,
            char *cfiCode,
            int64_t high_limit_px_mantissa,
            int8_t high_limit_px_exponent,
            int64_t low_limit_px_mantissa,
            int8_t low_limit_px_exponent,
            int64_t pxvar_mantissa,
            int8_t pxvar_exponent,
            int32_t sec_id,
            char updateAction,
            uint8_t tradingStatus,
            uint64_t activation,
            uint64_t expiration,
            char *sec_group,
            uint8_t seg_id,
            char matchAlgorithm,
            uint8_t mainFraction,
            int64_t minPriceIncrement_mantissa,
            uint8_t minPriceIncrement_exponent,
            uint8_t priceDisplayFormat,
            char userDefinedInstrument,

            int64_t dispFactor_mantissa,
            int8_t dispFactor_exponent,
            uint8_t subFraction,
            char* securityType,
            uint8_t maturityMont,
            uint16_t maturityYear,

            uint16_t issueDate,
            uint16_t maturityDate,
            
            int32_t openInterestQty,
            int32_t clearedVolume,
            uint16_t tradingRefDate,
            char* unitOfMeasure,

            int64_t unitOfMeasureQty_mantissa,
            uint8_t unitOfMeasureQty_exponent

            ) noexcept = 0;

        virtual void MDInstrumentDefinitionOption(
            uint32_t msgSeqNum,
            uint64_t sendingTime,
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
            char* securityType,
            uint8_t maturityMont,
            uint16_t maturityYear,
            int32_t openInterestQty,
            int32_t clearedVolume,
            uint16_t tradingRefDate,
            char* unitOfMeasure,

            int64_t unitOfMeasureQty_mantissa,
            uint8_t unitOfMeasureQty_exponent
            
            ) noexcept = 0;

        virtual void MDInstrumentDefinitionSpread(
            uint32_t msgSeqNum,
            uint64_t sendingTime,
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
            char* securityType,
            uint8_t maturityMont,
            uint16_t maturityYear,
            int32_t openInterestQty,
            int32_t clearedVolume,
            uint16_t tradingRefDate,
            char* unitOfMeasure
            
            ) noexcept = 0;

        virtual void ChannelReset(
            uint32_t msgSeqNum,
            uint64_t transactTime,
            uint64_t sendingTime,
            const char *mdEntryType) noexcept = 0;

#ifdef NOTIMPLEMENTED
        virtual void SnapshotFullRefresh(
            uint32_t msgSeqNum,
            uint64_t transactTime,
            uint64_t sendingTime,
            int32_t securityID,
            int64_t px_mantissa,
            int64_t px_exponent,
            uint32_t sz,
            int8_t px_level,
            int32_t numorders,
            char side) noexcept = 0;
#endif

        /**
         * @brief for recorders not doing recovery
         * This is to be recorded but not sent to order book directly
         * 
         */
        virtual void SnapshotFullRefreshOrderBook_NR(
            uint32_t lastSeqNum,
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
            uint64_t orderPriority,
            uint64_t orderID) noexcept = 0;

        virtual void SnapshotFullRefreshOrderBook(
            uint64_t transactTime,
            uint64_t sendingTime,
            int32_t securityID,
            int32_t displayQty,
            int64_t px_mantissa,
            int64_t px_exponent,
            char side,
            uint64_t orderPriority,
            uint64_t orderID) noexcept = 0;

        virtual void MDIncrementalRefreshLimitsBanding(
            uint32_t msgSeqNum,
            uint64_t transactTime,
            uint64_t sendingTime,
            int32_t securityID,
            int64_t pxh_mantissa,
            int8_t pxh_exponent,
            int64_t pxl_mantissa,
            int8_t pxl_exponent,
            int64_t pxvar_mantissa,
            int8_t pxvar_exponent,
            const char *entryType) noexcept = 0;

        virtual void SecurityStatus(
            uint32_t msgSeqNum,
            uint64_t transactTime,
            uint64_t sendingTime,
            int32_t securityID,
            uint8_t haltReason,
            uint8_t tradingStatus,
            uint8_t tradingEvent) noexcept = 0;

        virtual void MDIncrementalRefreshVolume(
            uint32_t msgSeqNum,
            uint64_t transactTime,
            uint64_t sendingTime,
            int32_t securityID,
            int32_t volume,
            char typ,
            uint8_t action) noexcept = 0;

        virtual void DataReceoveryRestart() noexcept = 0;

        virtual void Gap() noexcept = 0;

        virtual void BurstEnd(uint32_t cnt) noexcept = 0;

        virtual void EndOfPacket(
            u_int32_t msgSeqNum,
            uint64_t sendingTme) noexcept = 0;

        virtual void PrintStats() noexcept = 0;

        double to_price(int64_t mantissa, int8_t exponent)
        {
            return double(mantissa) * std::pow(10., double(exponent));
        }
    };

}