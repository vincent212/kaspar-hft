#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "mdp3/mbo_if.hpp"

#include <array>
#include <functional>
#include <string>
#include <vector>

namespace mdp3
{
    // One recorded feed_handler_if call, replayed later against the real handler.
    using RecordedCall = std::function<void(feed_handler_if &)>;

    // A feed_handler_if that records every callback instead of acting on it, so
    // a parallel decode worker can run the unchanged DataDecoder::mbo_data and the
    // single-threaded HandlerIfActor can replay the calls, in packet order, into
    // the real handler_if.
    //
    // Pointer arguments are captured as raw pointers, with two exceptions. The
    // char* fields (symbol, asset, securityType, ...) point INTO THE PACKET BYTES
    // and are not NUL-terminated, so copying them as C strings would read past
    // the field. They stay valid because the bytes travel with the recorded calls
    // in the same msg::DecodedPacket. The fixed-size arrays (underlyings, legs)
    // point at locals in the decoder's stack frame, so those are copied.
    struct RecordingHandler : public feed_handler_if
    {
        std::vector<RecordedCall> *out = nullptr;

        // Fixed by config on the real handler, not per packet.
        void disable_mbo(bool) noexcept override {}
        void set_max_mbp_level(uint32_t) noexcept override {}

        void MDIncrementalRefreshBook(uint64_t recv_time, uint32_t msgSeqNum,
            uint64_t transactTime, uint64_t sendingTime, int32_t securityID,
            int64_t px_mantissa, int8_t px_exponent, char side, int32_t sz,
            int32_t numorders, uint8_t pxlevel, bool endOfEvent,
            bool recovery) noexcept override
        {
            out->emplace_back([=](feed_handler_if &h) {
                h.MDIncrementalRefreshBook(recv_time, msgSeqNum, transactTime,
                    sendingTime, securityID, px_mantissa, px_exponent, side, sz,
                    numorders, pxlevel, endOfEvent, recovery);
            });
        }

        void MDIncrementalRefreshBook(uint64_t recv_time, uint32_t msgSeqNum,
            uint64_t transactTime, uint64_t sendingTime, int32_t securityID,
            int64_t px_mantissa, int8_t px_exponent, char side, int32_t displayQty,
            uint64_t orderID, uint8_t orderUpdateAction, uint64_t priority,
            bool lastQuote, bool endOfEvent, bool recovery) noexcept override
        {
            out->emplace_back([=](feed_handler_if &h) {
                h.MDIncrementalRefreshBook(recv_time, msgSeqNum, transactTime,
                    sendingTime, securityID, px_mantissa, px_exponent, side,
                    displayQty, orderID, orderUpdateAction, priority, lastQuote,
                    endOfEvent, recovery);
            });
        }

        void MDIncrementalRefreshTradeSummary(uint64_t recv_time, uint32_t msgSeqNum,
            uint64_t transactTime, uint64_t sendingTime, int32_t securityID,
            int64_t px_mantissa, int8_t px_exponent, char side,
            uint8_t aggressor_side, int32_t sz, int32_t numorders, bool lastTrade,
            bool endofEvent) noexcept override
        {
            out->emplace_back([=](feed_handler_if &h) {
                h.MDIncrementalRefreshTradeSummary(recv_time, msgSeqNum, transactTime,
                    sendingTime, securityID, px_mantissa, px_exponent, side,
                    aggressor_side, sz, numorders, lastTrade, endofEvent);
            });
        }

        void MDIncrementalRefreshTradeSummary(uint64_t recv_time, uint32_t msgSeqNum,
            uint64_t transactTime, uint64_t sendingTime, int32_t lastQty,
            uint64_t orderID, bool lastTrade, bool endofEvent) noexcept override
        {
            out->emplace_back([=](feed_handler_if &h) {
                h.MDIncrementalRefreshTradeSummary(recv_time, msgSeqNum, transactTime,
                    sendingTime, lastQty, orderID, lastTrade, endofEvent);
            });
        }

        void MDIncrementalRefreshSessionStatistics(uint32_t msgSeqNum,
            uint64_t transactTime, uint64_t sendingTime, uint32_t secid,
            uint8_t openCloseSettleFlag, int64_t px_mantissa, int64_t px_exponent,
            uint8_t updateAction, char entryType) noexcept override
        {
            out->emplace_back([=](feed_handler_if &h) {
                h.MDIncrementalRefreshSessionStatistics(msgSeqNum, transactTime,
                    sendingTime, secid, openCloseSettleFlag, px_mantissa,
                    px_exponent, updateAction, entryType);
            });
        }

        void MDIncrementalRefreshDailyStatistics(uint32_t msgSeqNum,
            uint64_t transactTime, uint64_t sendingTime, uint32_t secid,
            int64_t px_mantissa, int8_t px_exponent, int32_t size, char side,
            bool finalDaily, bool intraday, uint8_t updateAction,
            uint16_t tradingReferenceDate) noexcept override
        {
            out->emplace_back([=](feed_handler_if &h) {
                h.MDIncrementalRefreshDailyStatistics(msgSeqNum, transactTime,
                    sendingTime, secid, px_mantissa, px_exponent, size, side,
                    finalDaily, intraday, updateAction, tradingReferenceDate);
            });
        }

        void MDInstrumentDefinitionFuture(uint32_t msgSeqNum, uint64_t sendingTime,
            char *sym, char *asset, char *cfiCode, int64_t high_limit_px_mantissa,
            int8_t high_limit_px_exponent, int64_t low_limit_px_mantissa,
            int8_t low_limit_px_exponent, int64_t pxvar_mantissa,
            int8_t pxvar_exponent, int32_t sec_id, char updateAction,
            uint8_t tradingStatus, uint64_t activation, uint64_t expiration,
            char *sec_group, uint8_t seg_id, char matchAlgorithm,
            uint8_t mainFraction, int64_t minPriceIncrement_mantissa,
            uint8_t minPriceIncrement_exponent, uint8_t priceDisplayFormat,
            char userDefinedInstrument, int64_t dispFactor_mantissa,
            int8_t dispFactor_exponent, uint8_t subFraction, char *securityType,
            uint8_t maturityMont, uint16_t maturityYear, uint16_t issueDate,
            uint16_t maturityDate, int32_t openInterestQty, int32_t clearedVolume,
            uint16_t tradingRefDate, char *unitOfMeasure,
            int64_t unitOfMeasureQty_mantissa,
            uint8_t unitOfMeasureQty_exponent) noexcept override
        {
            out->emplace_back([=](feed_handler_if &h) {
                h.MDInstrumentDefinitionFuture(msgSeqNum, sendingTime, sym, asset,
                    cfiCode, high_limit_px_mantissa, high_limit_px_exponent,
                    low_limit_px_mantissa, low_limit_px_exponent, pxvar_mantissa,
                    pxvar_exponent, sec_id, updateAction, tradingStatus, activation,
                    expiration, sec_group, seg_id, matchAlgorithm, mainFraction,
                    minPriceIncrement_mantissa, minPriceIncrement_exponent,
                    priceDisplayFormat, userDefinedInstrument, dispFactor_mantissa,
                    dispFactor_exponent, subFraction, securityType, maturityMont,
                    maturityYear, issueDate, maturityDate, openInterestQty,
                    clearedVolume, tradingRefDate, unitOfMeasure,
                    unitOfMeasureQty_mantissa, unitOfMeasureQty_exponent);
            });
        }

        void MDInstrumentDefinitionOption(uint32_t msgSeqNum, uint64_t sendingTime,
            char *sym, char *asset, char *cfiCode, int64_t high_limit_px_mantissa,
            int8_t high_limit_px_exponent, int64_t low_limit_px_mantissa,
            int8_t low_limit_px_exponent, int32_t securityID,
            char securityUpdateAction, uint64_t activation, uint64_t expiration,
            char *securityGroup, uint8_t marketSegmentID,
            uint8_t mDSecurityTradingStatus, char matchAlgorithm,
            uint8_t mainFraction, int64_t minPriceIncrement_mantissa,
            uint8_t minPriceIncrement_exponent, uint8_t priceDisplayFormat,
            int64_t minCabPrice_mantissa, uint8_t minCabPrice_exponent,
            uint8_t putOrCall, int64_t strikePrice_mantissa,
            uint8_t strikePrice_exponent, char userDefinedInstrument,
            int8_t tickRule, uint8_t noUnderlyingsCount,
            uint32_t underlyingSecurityID[4], std::string underlyingSymbol[4],
            int64_t dispFactor_mantissa, int8_t dispFactor_exponent,
            uint8_t subFraction, char *securityType, uint8_t maturityMont,
            uint16_t maturityYear, int32_t openInterestQty, int32_t clearedVolume,
            uint16_t tradingRefDate, char *unitOfMeasure,
            int64_t unitOfMeasureQty_mantissa,
            uint8_t unitOfMeasureQty_exponent) noexcept override
        {
            std::array<uint32_t, 4> und_id;
            std::array<std::string, 4> und_sym;
            for (int i = 0; i < 4; ++i)
            {
                und_id[i] = underlyingSecurityID[i];
                und_sym[i] = underlyingSymbol[i];
            }
            out->emplace_back([=](feed_handler_if &h) mutable {
                h.MDInstrumentDefinitionOption(msgSeqNum, sendingTime, sym, asset,
                    cfiCode, high_limit_px_mantissa, high_limit_px_exponent,
                    low_limit_px_mantissa, low_limit_px_exponent, securityID,
                    securityUpdateAction, activation, expiration, securityGroup,
                    marketSegmentID, mDSecurityTradingStatus, matchAlgorithm,
                    mainFraction, minPriceIncrement_mantissa,
                    minPriceIncrement_exponent, priceDisplayFormat,
                    minCabPrice_mantissa, minCabPrice_exponent, putOrCall,
                    strikePrice_mantissa, strikePrice_exponent,
                    userDefinedInstrument, tickRule, noUnderlyingsCount,
                    und_id.data(), und_sym.data(), dispFactor_mantissa,
                    dispFactor_exponent, subFraction, securityType, maturityMont,
                    maturityYear, openInterestQty, clearedVolume, tradingRefDate,
                    unitOfMeasure, unitOfMeasureQty_mantissa,
                    unitOfMeasureQty_exponent);
            });
        }

        void MDInstrumentDefinitionSpread(uint32_t msgSeqNum, uint64_t sendingTime,
            char *sym, char *asset, char *cfiCode, int64_t high_limit_px_mantissa,
            int8_t high_limit_px_exponent, int64_t low_limit_px_mantissa,
            int8_t low_limit_px_exponent, int32_t securityID,
            char securityUpdateAction, uint64_t activation, uint64_t expiration,
            char *securityGroup, uint8_t marketSegmentID,
            uint8_t mDSecurityTradingStatus, char matchAlgorithm,
            uint8_t mainFraction, int64_t minPriceIncrement_mantissa,
            uint8_t minPriceIncrement_exponent, uint8_t priceDisplayFormat,
            char userDefinedInstrument, int8_t tickRule, uint8_t noLegsCount,
            int32_t legOptionDelta_mantissa[8], int8_t legOptionDelta_exponent[8],
            int64_t legPrice_mantissa[8], int8_t legPrice_exponent[8],
            int8_t legRatioQty[8], int32_t legSecurityID[8], uint8_t legSide[8],
            int64_t dispFactor_mantissa, int8_t dispFactor_exponent,
            uint8_t subFraction, char *securityType, uint8_t maturityMont,
            uint16_t maturityYear, int32_t openInterestQty, int32_t clearedVolume,
            uint16_t tradingRefDate, char *unitOfMeasure) noexcept override
        {
            std::array<int32_t, 8> d_m, sec;
            std::array<int8_t, 8> d_e, p_e, ratio;
            std::array<int64_t, 8> p_m;
            std::array<uint8_t, 8> lside;
            for (int i = 0; i < 8; ++i)
            {
                d_m[i] = legOptionDelta_mantissa[i];
                d_e[i] = legOptionDelta_exponent[i];
                p_m[i] = legPrice_mantissa[i];
                p_e[i] = legPrice_exponent[i];
                ratio[i] = legRatioQty[i];
                sec[i] = legSecurityID[i];
                lside[i] = legSide[i];
            }
            out->emplace_back([=](feed_handler_if &h) mutable {
                h.MDInstrumentDefinitionSpread(msgSeqNum, sendingTime, sym, asset,
                    cfiCode, high_limit_px_mantissa, high_limit_px_exponent,
                    low_limit_px_mantissa, low_limit_px_exponent, securityID,
                    securityUpdateAction, activation, expiration, securityGroup,
                    marketSegmentID, mDSecurityTradingStatus, matchAlgorithm,
                    mainFraction, minPriceIncrement_mantissa,
                    minPriceIncrement_exponent, priceDisplayFormat,
                    userDefinedInstrument, tickRule, noLegsCount, d_m.data(),
                    d_e.data(), p_m.data(), p_e.data(), ratio.data(), sec.data(),
                    lside.data(), dispFactor_mantissa, dispFactor_exponent,
                    subFraction, securityType, maturityMont, maturityYear,
                    openInterestQty, clearedVolume, tradingRefDate, unitOfMeasure);
            });
        }

        void ChannelReset(uint32_t msgSeqNum, uint64_t transactTime,
            uint64_t sendingTime, const char *mdEntryType) noexcept override
        {
            out->emplace_back([=](feed_handler_if &h) {
                h.ChannelReset(msgSeqNum, transactTime, sendingTime, mdEntryType);
            });
        }

        void SnapshotFullRefreshOrderBook_NR(uint32_t lastSeqNum, uint32_t numReports,
            uint32_t msgSeqNum, uint64_t transactTime, uint64_t sendingTime,
            uint32_t currentChunk, uint32_t numChunks, int32_t securityID,
            int32_t displayQty, int64_t px_mantissa, int64_t px_exponent, char side,
            uint64_t orderPriority, uint64_t orderID) noexcept override
        {
            out->emplace_back([=](feed_handler_if &h) {
                h.SnapshotFullRefreshOrderBook_NR(lastSeqNum, numReports, msgSeqNum,
                    transactTime, sendingTime, currentChunk, numChunks, securityID,
                    displayQty, px_mantissa, px_exponent, side, orderPriority,
                    orderID);
            });
        }

        void SnapshotFullRefreshOrderBook(uint64_t transactTime, uint64_t sendingTime,
            int32_t securityID, int32_t displayQty, int64_t px_mantissa,
            int64_t px_exponent, char side, uint64_t orderPriority,
            uint64_t orderID) noexcept override
        {
            out->emplace_back([=](feed_handler_if &h) {
                h.SnapshotFullRefreshOrderBook(transactTime, sendingTime, securityID,
                    displayQty, px_mantissa, px_exponent, side, orderPriority,
                    orderID);
            });
        }

        void MDIncrementalRefreshLimitsBanding(uint32_t msgSeqNum,
            uint64_t transactTime, uint64_t sendingTime, int32_t securityID,
            int64_t pxh_mantissa, int8_t pxh_exponent, int64_t pxl_mantissa,
            int8_t pxl_exponent, int64_t pxvar_mantissa, int8_t pxvar_exponent,
            const char *entryType) noexcept override
        {
            out->emplace_back([=](feed_handler_if &h) {
                h.MDIncrementalRefreshLimitsBanding(msgSeqNum, transactTime,
                    sendingTime, securityID, pxh_mantissa, pxh_exponent,
                    pxl_mantissa, pxl_exponent, pxvar_mantissa, pxvar_exponent,
                    entryType);
            });
        }

        void SecurityStatus(uint32_t msgSeqNum, uint64_t transactTime,
            uint64_t sendingTime, int32_t securityID, uint8_t haltReason,
            uint8_t tradingStatus, uint8_t tradingEvent) noexcept override
        {
            out->emplace_back([=](feed_handler_if &h) {
                h.SecurityStatus(msgSeqNum, transactTime, sendingTime, securityID,
                    haltReason, tradingStatus, tradingEvent);
            });
        }

        void MDIncrementalRefreshVolume(uint32_t msgSeqNum, uint64_t transactTime,
            uint64_t sendingTime, int32_t securityID, int32_t volume, char typ,
            uint8_t action) noexcept override
        {
            out->emplace_back([=](feed_handler_if &h) {
                h.MDIncrementalRefreshVolume(msgSeqNum, transactTime, sendingTime,
                    securityID, volume, typ, action);
            });
        }

        void DataReceoveryRestart() noexcept override
        {
            out->emplace_back([](feed_handler_if &h) { h.DataReceoveryRestart(); });
        }

        void Gap() noexcept override
        {
            out->emplace_back([](feed_handler_if &h) { h.Gap(); });
        }

        void BurstEnd(uint32_t cnt) noexcept override
        {
            out->emplace_back([=](feed_handler_if &h) { h.BurstEnd(cnt); });
        }

        void EndOfPacket(u_int32_t msgSeqNum, uint64_t sendingTime) noexcept override
        {
            out->emplace_back([=](feed_handler_if &h) {
                h.EndOfPacket(msgSeqNum, sendingTime);
            });
        }

        void PrintStats() noexcept override
        {
            out->emplace_back([](feed_handler_if &h) { h.PrintStats(); });
        }
    };
}
