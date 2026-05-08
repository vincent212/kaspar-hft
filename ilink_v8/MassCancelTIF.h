/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/* Generated SBE (Simple Binary Encoding) message codec */
#ifndef _SBE_MASSCANCELTIF_CXX_H_
#define _SBE_MASSCANCELTIF_CXX_H_

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

class MassCancelTIF
{
public:
    enum Value
    {
        Day = static_cast<std::uint8_t>(0),
        GoodTillCancel = static_cast<std::uint8_t>(1),
        GoodTillDate = static_cast<std::uint8_t>(6),
        NULL_VALUE = static_cast<std::uint8_t>(255)
    };

    static MassCancelTIF::Value get(const std::uint8_t value)
    {
        switch (value)
        {
            case static_cast<std::uint8_t>(0): return Day;
            case static_cast<std::uint8_t>(1): return GoodTillCancel;
            case static_cast<std::uint8_t>(6): return GoodTillDate;
            case static_cast<std::uint8_t>(255): return NULL_VALUE;
        }

        throw std::runtime_error("unknown value for enum MassCancelTIF [E103]");
    }

    static const char *c_str(const MassCancelTIF::Value value)
    {
        switch (value)
        {
            case Day: return "Day";
            case GoodTillCancel: return "GoodTillCancel";
            case GoodTillDate: return "GoodTillDate";
            case NULL_VALUE: return "NULL_VALUE";
        }

        throw std::runtime_error("unknown value for enum MassCancelTIF [E103]:");
    }

    template<typename CharT, typename Traits>
    friend std::basic_ostream<CharT, Traits> & operator << (
        std::basic_ostream<CharT, Traits> &os, MassCancelTIF::Value m)
    {
        return os << MassCancelTIF::c_str(m);
    }
};

}

#endif
