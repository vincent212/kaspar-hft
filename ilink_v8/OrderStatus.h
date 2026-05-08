/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/* Generated SBE (Simple Binary Encoding) message codec */
#ifndef _SBE_ORDERSTATUS_CXX_H_
#define _SBE_ORDERSTATUS_CXX_H_

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

class OrderStatus
{
public:
    enum Value
    {
        New = static_cast<char>(48),
        PartiallyFilled = static_cast<char>(49),
        Filled = static_cast<char>(50),
        Cancelled = static_cast<char>(52),
        Replaced = static_cast<char>(53),
        PendingCancel = static_cast<char>(54),
        Rejected = static_cast<char>(56),
        Expired = static_cast<char>(67),
        PendingReplace = static_cast<char>(69),
        Undefined = static_cast<char>(85),
        NULL_VALUE = static_cast<char>(0)
    };

    static OrderStatus::Value get(const char value)
    {
        switch (value)
        {
            case static_cast<char>(48): return New;
            case static_cast<char>(49): return PartiallyFilled;
            case static_cast<char>(50): return Filled;
            case static_cast<char>(52): return Cancelled;
            case static_cast<char>(53): return Replaced;
            case static_cast<char>(54): return PendingCancel;
            case static_cast<char>(56): return Rejected;
            case static_cast<char>(67): return Expired;
            case static_cast<char>(69): return PendingReplace;
            case static_cast<char>(85): return Undefined;
            case static_cast<char>(0): return NULL_VALUE;
        }

        throw std::runtime_error("unknown value for enum OrderStatus [E103]");
    }

    static const char *c_str(const OrderStatus::Value value)
    {
        switch (value)
        {
            case New: return "New";
            case PartiallyFilled: return "PartiallyFilled";
            case Filled: return "Filled";
            case Cancelled: return "Cancelled";
            case Replaced: return "Replaced";
            case PendingCancel: return "PendingCancel";
            case Rejected: return "Rejected";
            case Expired: return "Expired";
            case PendingReplace: return "PendingReplace";
            case Undefined: return "Undefined";
            case NULL_VALUE: return "NULL_VALUE";
        }

        throw std::runtime_error("unknown value for enum OrderStatus [E103]:");
    }

    template<typename CharT, typename Traits>
    friend std::basic_ostream<CharT, Traits> & operator << (
        std::basic_ostream<CharT, Traits> &os, OrderStatus::Value m)
    {
        return os << OrderStatus::c_str(m);
    }
};

}

#endif
