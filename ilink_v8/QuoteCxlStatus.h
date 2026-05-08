/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/* Generated SBE (Simple Binary Encoding) message codec */
#ifndef _SBE_QUOTECXLSTATUS_CXX_H_
#define _SBE_QUOTECXLSTATUS_CXX_H_

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

class QuoteCxlStatus
{
public:
    enum Value
    {
        CancelperInstrument = static_cast<std::uint8_t>(1),
        CancelperInstrumentgroup = static_cast<std::uint8_t>(3),
        Cancelallquotes = static_cast<std::uint8_t>(4),
        Rejected = static_cast<std::uint8_t>(5),
        CancelperQuoteSet = static_cast<std::uint8_t>(100),
        NULL_VALUE = static_cast<std::uint8_t>(255)
    };

    static QuoteCxlStatus::Value get(const std::uint8_t value)
    {
        switch (value)
        {
            case static_cast<std::uint8_t>(1): return CancelperInstrument;
            case static_cast<std::uint8_t>(3): return CancelperInstrumentgroup;
            case static_cast<std::uint8_t>(4): return Cancelallquotes;
            case static_cast<std::uint8_t>(5): return Rejected;
            case static_cast<std::uint8_t>(100): return CancelperQuoteSet;
            case static_cast<std::uint8_t>(255): return NULL_VALUE;
        }

        throw std::runtime_error("unknown value for enum QuoteCxlStatus [E103]");
    }

    static const char *c_str(const QuoteCxlStatus::Value value)
    {
        switch (value)
        {
            case CancelperInstrument: return "CancelperInstrument";
            case CancelperInstrumentgroup: return "CancelperInstrumentgroup";
            case Cancelallquotes: return "Cancelallquotes";
            case Rejected: return "Rejected";
            case CancelperQuoteSet: return "CancelperQuoteSet";
            case NULL_VALUE: return "NULL_VALUE";
        }

        throw std::runtime_error("unknown value for enum QuoteCxlStatus [E103]:");
    }

    template<typename CharT, typename Traits>
    friend std::basic_ostream<CharT, Traits> & operator << (
        std::basic_ostream<CharT, Traits> &os, QuoteCxlStatus::Value m)
    {
        return os << QuoteCxlStatus::c_str(m);
    }
};

}

#endif
