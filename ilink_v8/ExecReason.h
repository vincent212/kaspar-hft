/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/* Generated SBE (Simple Binary Encoding) message codec */
#ifndef _SBE_EXECREASON_CXX_H_
#define _SBE_EXECREASON_CXX_H_

#if !defined(__STDC_LIMIT_MACROS)
#  define __STDC_LIMIT_MACROS 1
#endif

#include <cstdint>
#include <iomanip>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <sstream>
#include <string>

#define SBE_NULLVALUE_INT8 (std::numeric_limits<std::int8_t>::min)()
#define SBE_NULLVALUE_INT16 (std::numeric_limits<std::int16_t>::min)()
#define SBE_NULLVALUE_INT32 (std::numeric_limits<std::int32_t>::min)()
#define SBE_NULLVALUE_INT64 (std::numeric_limits<std::int64_t>::min)()
#define SBE_NULLVALUE_UINT8 (std::numeric_limits<std::uint8_t>::max)()
#define SBE_NULLVALUE_UINT16 (std::numeric_limits<std::uint16_t>::max)()
#define SBE_NULLVALUE_UINT32 (std::numeric_limits<std::uint32_t>::max)()
#define SBE_NULLVALUE_UINT64 (std::numeric_limits<std::uint64_t>::max)()

namespace sbe {

class ExecReason
{
public:
    enum Value
    {
        MarketExchangeOption = static_cast<std::uint8_t>(8),
        CancelledNotBest = static_cast<std::uint8_t>(9),
        CancelOnDisconnect = static_cast<std::uint8_t>(100),
        SelfMatchPreventionOldestOrderCancelled = static_cast<std::uint8_t>(103),
        CancelOnGlobexCreditControlsViolation = static_cast<std::uint8_t>(104),
        CancelFromFirmsoft = static_cast<std::uint8_t>(105),
        CancelFromRiskManagementAPI = static_cast<std::uint8_t>(106),
        SelfMatchPreventionNewestOrderCancelled = static_cast<std::uint8_t>(107),
        Cancelduetovolquotedoptionorderrestedqtylessthanminordersize = static_cast<std::uint8_t>(108),
        CancelRFCOrder = static_cast<std::uint8_t>(109),
        CancelUponContractExpiration = static_cast<std::uint8_t>(110),
        SystemCancel = static_cast<std::uint8_t>(111),
        NULL_VALUE = static_cast<std::uint8_t>(255)
    };

    static ExecReason::Value get(const std::uint8_t value)
    {
        switch (value)
        {
            case static_cast<std::uint8_t>(8): return MarketExchangeOption;
            case static_cast<std::uint8_t>(9): return CancelledNotBest;
            case static_cast<std::uint8_t>(100): return CancelOnDisconnect;
            case static_cast<std::uint8_t>(103): return SelfMatchPreventionOldestOrderCancelled;
            case static_cast<std::uint8_t>(104): return CancelOnGlobexCreditControlsViolation;
            case static_cast<std::uint8_t>(105): return CancelFromFirmsoft;
            case static_cast<std::uint8_t>(106): return CancelFromRiskManagementAPI;
            case static_cast<std::uint8_t>(107): return SelfMatchPreventionNewestOrderCancelled;
            case static_cast<std::uint8_t>(108): return Cancelduetovolquotedoptionorderrestedqtylessthanminordersize;
            case static_cast<std::uint8_t>(109): return CancelRFCOrder;
            case static_cast<std::uint8_t>(110): return CancelUponContractExpiration;
            case static_cast<std::uint8_t>(111): return SystemCancel;
            case static_cast<std::uint8_t>(255): return NULL_VALUE;
        }

        throw std::runtime_error("unknown value for enum ExecReason [E103]");
    }

    static const char *c_str(const ExecReason::Value value)
    {
        switch (value)
        {
            case MarketExchangeOption: return "MarketExchangeOption";
            case CancelledNotBest: return "CancelledNotBest";
            case CancelOnDisconnect: return "CancelOnDisconnect";
            case SelfMatchPreventionOldestOrderCancelled: return "SelfMatchPreventionOldestOrderCancelled";
            case CancelOnGlobexCreditControlsViolation: return "CancelOnGlobexCreditControlsViolation";
            case CancelFromFirmsoft: return "CancelFromFirmsoft";
            case CancelFromRiskManagementAPI: return "CancelFromRiskManagementAPI";
            case SelfMatchPreventionNewestOrderCancelled: return "SelfMatchPreventionNewestOrderCancelled";
            case Cancelduetovolquotedoptionorderrestedqtylessthanminordersize: return "Cancelduetovolquotedoptionorderrestedqtylessthanminordersize";
            case CancelRFCOrder: return "CancelRFCOrder";
            case CancelUponContractExpiration: return "CancelUponContractExpiration";
            case SystemCancel: return "SystemCancel";
            case NULL_VALUE: return "NULL_VALUE";
        }

        throw std::runtime_error("unknown value for enum ExecReason [E103]:");
    }

    template<typename CharT, typename Traits>
    friend std::basic_ostream<CharT, Traits> & operator << (
        std::basic_ostream<CharT, Traits> &os, ExecReason::Value m)
    {
        return os << ExecReason::c_str(m);
    }
};

}

#endif
