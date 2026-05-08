/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/* Generated SBE (Simple Binary Encoding) message codec */
#ifndef _SBE_PARTYDETAILROLE_CXX_H_
#define _SBE_PARTYDETAILROLE_CXX_H_

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

class PartyDetailRole
{
public:
    enum Value
    {
        ExecutingFirm = static_cast<std::uint16_t>(1),
        CustomerAccount = static_cast<std::uint16_t>(24),
        TakeUpFirm = static_cast<std::uint16_t>(96),
        Operator = static_cast<std::uint16_t>(118),
        TakeUpAccount = static_cast<std::uint16_t>(1000),
        NULL_VALUE = static_cast<std::uint16_t>(65535)
    };

    static PartyDetailRole::Value get(const std::uint16_t value)
    {
        switch (value)
        {
            case static_cast<std::uint16_t>(1): return ExecutingFirm;
            case static_cast<std::uint16_t>(24): return CustomerAccount;
            case static_cast<std::uint16_t>(96): return TakeUpFirm;
            case static_cast<std::uint16_t>(118): return Operator;
            case static_cast<std::uint16_t>(1000): return TakeUpAccount;
            case static_cast<std::uint16_t>(65535): return NULL_VALUE;
        }

        throw std::runtime_error("unknown value for enum PartyDetailRole [E103]");
    }

    static const char *c_str(const PartyDetailRole::Value value)
    {
        switch (value)
        {
            case ExecutingFirm: return "ExecutingFirm";
            case CustomerAccount: return "CustomerAccount";
            case TakeUpFirm: return "TakeUpFirm";
            case Operator: return "Operator";
            case TakeUpAccount: return "TakeUpAccount";
            case NULL_VALUE: return "NULL_VALUE";
        }

        throw std::runtime_error("unknown value for enum PartyDetailRole [E103]:");
    }

    template<typename CharT, typename Traits>
    friend std::basic_ostream<CharT, Traits> & operator << (
        std::basic_ostream<CharT, Traits> &os, PartyDetailRole::Value m)
    {
        return os << PartyDetailRole::c_str(m);
    }
};

}

#endif
