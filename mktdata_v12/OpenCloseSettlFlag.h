/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/* Generated SBE (Simple Binary Encoding) message codec */
#ifndef _SBE_OPENCLOSESETTLFLAG_H_
#define _SBE_OPENCLOSESETTLFLAG_H_

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

class OpenCloseSettlFlag
{
public:
    enum Value
    {
        DailyOpenPrice = static_cast<std::uint8_t>(0),
        IndicativeOpeningPrice = static_cast<std::uint8_t>(5),
        IntradayVWAP = static_cast<std::uint8_t>(100),
        RepoAverage8_30AM = static_cast<std::uint8_t>(101),
        RepoAverage10AM = static_cast<std::uint8_t>(102),
        PrevSessionRepoAverage10AM = static_cast<std::uint8_t>(103),
        NULL_VALUE = static_cast<std::uint8_t>(255)
    };

    static OpenCloseSettlFlag::Value get(const std::uint8_t value)
    {
        switch (value)
        {
            case static_cast<std::uint8_t>(0): return DailyOpenPrice;
            case static_cast<std::uint8_t>(5): return IndicativeOpeningPrice;
            case static_cast<std::uint8_t>(100): return IntradayVWAP;
            case static_cast<std::uint8_t>(101): return RepoAverage8_30AM;
            case static_cast<std::uint8_t>(102): return RepoAverage10AM;
            case static_cast<std::uint8_t>(103): return PrevSessionRepoAverage10AM;
            case static_cast<std::uint8_t>(255): return NULL_VALUE;
        }

        throw std::runtime_error("unknown value for enum OpenCloseSettlFlag [E103]");
    }

    static const char *c_str(const OpenCloseSettlFlag::Value value)
    {
        switch (value)
        {
            case DailyOpenPrice: return "DailyOpenPrice";
            case IndicativeOpeningPrice: return "IndicativeOpeningPrice";
            case IntradayVWAP: return "IntradayVWAP";
            case RepoAverage8_30AM: return "RepoAverage8_30AM";
            case RepoAverage10AM: return "RepoAverage10AM";
            case PrevSessionRepoAverage10AM: return "PrevSessionRepoAverage10AM";
            case NULL_VALUE: return "NULL_VALUE";
        }

        throw std::runtime_error("unknown value for enum OpenCloseSettlFlag [E103]:");
    }

    template<typename CharT, typename Traits>
    friend std::basic_ostream<CharT, Traits> & operator << (
        std::basic_ostream<CharT, Traits> &os, OpenCloseSettlFlag::Value m)
    {
        return os << OpenCloseSettlFlag::c_str(m);
    }
};

}

#endif
