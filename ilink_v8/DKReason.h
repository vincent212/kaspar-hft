/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/* Generated SBE (Simple Binary Encoding) message codec */
#ifndef _SBE_DKREASON_CXX_H_
#define _SBE_DKREASON_CXX_H_

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

class DKReason
{
public:
    enum Value
    {
        UnknownSecurity = static_cast<char>(65),
        WrongSide = static_cast<char>(66),
        QuantityExceedsOrder = static_cast<char>(67),
        NoMatchingOrder = static_cast<char>(68),
        PriceExceedsLimit = static_cast<char>(69),
        CalculationDifference = static_cast<char>(70),
        NoMatchingExecutionReport = static_cast<char>(71),
        Other = static_cast<char>(90),
        NULL_VALUE = static_cast<char>(48)
    };

    static DKReason::Value get(const char value)
    {
        switch (value)
        {
            case static_cast<char>(65): return UnknownSecurity;
            case static_cast<char>(66): return WrongSide;
            case static_cast<char>(67): return QuantityExceedsOrder;
            case static_cast<char>(68): return NoMatchingOrder;
            case static_cast<char>(69): return PriceExceedsLimit;
            case static_cast<char>(70): return CalculationDifference;
            case static_cast<char>(71): return NoMatchingExecutionReport;
            case static_cast<char>(90): return Other;
            case static_cast<char>(48): return NULL_VALUE;
        }

        throw std::runtime_error("unknown value for enum DKReason [E103]");
    }

    static const char *c_str(const DKReason::Value value)
    {
        switch (value)
        {
            case UnknownSecurity: return "UnknownSecurity";
            case WrongSide: return "WrongSide";
            case QuantityExceedsOrder: return "QuantityExceedsOrder";
            case NoMatchingOrder: return "NoMatchingOrder";
            case PriceExceedsLimit: return "PriceExceedsLimit";
            case CalculationDifference: return "CalculationDifference";
            case NoMatchingExecutionReport: return "NoMatchingExecutionReport";
            case Other: return "Other";
            case NULL_VALUE: return "NULL_VALUE";
        }

        throw std::runtime_error("unknown value for enum DKReason [E103]:");
    }

    template<typename CharT, typename Traits>
    friend std::basic_ostream<CharT, Traits> & operator << (
        std::basic_ostream<CharT, Traits> &os, DKReason::Value m)
    {
        return os << DKReason::c_str(m);
    }
};

}

#endif
