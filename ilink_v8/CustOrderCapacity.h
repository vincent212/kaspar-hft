/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/* Generated SBE (Simple Binary Encoding) message codec */
#ifndef _SBE_CUSTORDERCAPACITY_CXX_H_
#define _SBE_CUSTORDERCAPACITY_CXX_H_

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

class CustOrderCapacity
{
public:
    enum Value
    {
        Membertradingfortheirownaccount = static_cast<std::uint8_t>(1),
        Memberfirmtradingforitsproprietaryaccount = static_cast<std::uint8_t>(2),
        Membertradingforanothermemberornonmember = static_cast<std::uint8_t>(3),
        Allother = static_cast<std::uint8_t>(4),
        NULL_VALUE = static_cast<std::uint8_t>(255)
    };

    static CustOrderCapacity::Value get(const std::uint8_t value)
    {
        switch (value)
        {
            case static_cast<std::uint8_t>(1): return Membertradingfortheirownaccount;
            case static_cast<std::uint8_t>(2): return Memberfirmtradingforitsproprietaryaccount;
            case static_cast<std::uint8_t>(3): return Membertradingforanothermemberornonmember;
            case static_cast<std::uint8_t>(4): return Allother;
            case static_cast<std::uint8_t>(255): return NULL_VALUE;
        }

        throw std::runtime_error("unknown value for enum CustOrderCapacity [E103]");
    }

    static const char *c_str(const CustOrderCapacity::Value value)
    {
        switch (value)
        {
            case Membertradingfortheirownaccount: return "Membertradingfortheirownaccount";
            case Memberfirmtradingforitsproprietaryaccount: return "Memberfirmtradingforitsproprietaryaccount";
            case Membertradingforanothermemberornonmember: return "Membertradingforanothermemberornonmember";
            case Allother: return "Allother";
            case NULL_VALUE: return "NULL_VALUE";
        }

        throw std::runtime_error("unknown value for enum CustOrderCapacity [E103]:");
    }

    template<typename CharT, typename Traits>
    friend std::basic_ostream<CharT, Traits> & operator << (
        std::basic_ostream<CharT, Traits> &os, CustOrderCapacity::Value m)
    {
        return os << CustOrderCapacity::c_str(m);
    }
};

}

#endif
