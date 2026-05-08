/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/* Generated SBE (Simple Binary Encoding) message codec */
#ifndef _SBE_TRADEADDENDUM_CXX_H_
#define _SBE_TRADEADDENDUM_CXX_H_

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

class TradeAddendum
{
public:
    enum Value
    {
        PartiallyFilled = static_cast<std::uint8_t>(4),
        Filled = static_cast<std::uint8_t>(5),
        TradeCancel = static_cast<std::uint8_t>(100),
        TradeCorrection = static_cast<std::uint8_t>(101),
        NULL_VALUE = static_cast<std::uint8_t>(255)
    };

    static TradeAddendum::Value get(const std::uint8_t value)
    {
        switch (value)
        {
            case static_cast<std::uint8_t>(4): return PartiallyFilled;
            case static_cast<std::uint8_t>(5): return Filled;
            case static_cast<std::uint8_t>(100): return TradeCancel;
            case static_cast<std::uint8_t>(101): return TradeCorrection;
            case static_cast<std::uint8_t>(255): return NULL_VALUE;
        }

        throw std::runtime_error("unknown value for enum TradeAddendum [E103]");
    }

    static const char *c_str(const TradeAddendum::Value value)
    {
        switch (value)
        {
            case PartiallyFilled: return "PartiallyFilled";
            case Filled: return "Filled";
            case TradeCancel: return "TradeCancel";
            case TradeCorrection: return "TradeCorrection";
            case NULL_VALUE: return "NULL_VALUE";
        }

        throw std::runtime_error("unknown value for enum TradeAddendum [E103]:");
    }

    template<typename CharT, typename Traits>
    friend std::basic_ostream<CharT, Traits> & operator << (
        std::basic_ostream<CharT, Traits> &os, TradeAddendum::Value m)
    {
        return os << TradeAddendum::c_str(m);
    }
};

}

#endif
