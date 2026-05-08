/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/* Generated SBE (Simple Binary Encoding) message codec */
#ifndef _SBE_SECRSPTYP_CXX_H_
#define _SBE_SECRSPTYP_CXX_H_

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

class SecRspTyp
{
public:
    enum Value
    {
        AcceptSecurityProposalasis = static_cast<std::uint8_t>(1),
        AcceptSecurityproposalwithrevisionsasindicatedinthemessage = static_cast<std::uint8_t>(2),
        RejectSecurityProposal = static_cast<std::uint8_t>(5),
        NULL_VALUE = static_cast<std::uint8_t>(255)
    };

    static SecRspTyp::Value get(const std::uint8_t value)
    {
        switch (value)
        {
            case static_cast<std::uint8_t>(1): return AcceptSecurityProposalasis;
            case static_cast<std::uint8_t>(2): return AcceptSecurityproposalwithrevisionsasindicatedinthemessage;
            case static_cast<std::uint8_t>(5): return RejectSecurityProposal;
            case static_cast<std::uint8_t>(255): return NULL_VALUE;
        }

        throw std::runtime_error("unknown value for enum SecRspTyp [E103]");
    }

    static const char *c_str(const SecRspTyp::Value value)
    {
        switch (value)
        {
            case AcceptSecurityProposalasis: return "AcceptSecurityProposalasis";
            case AcceptSecurityproposalwithrevisionsasindicatedinthemessage: return "AcceptSecurityproposalwithrevisionsasindicatedinthemessage";
            case RejectSecurityProposal: return "RejectSecurityProposal";
            case NULL_VALUE: return "NULL_VALUE";
        }

        throw std::runtime_error("unknown value for enum SecRspTyp [E103]:");
    }

    template<typename CharT, typename Traits>
    friend std::basic_ostream<CharT, Traits> & operator << (
        std::basic_ostream<CharT, Traits> &os, SecRspTyp::Value m)
    {
        return os << SecRspTyp::c_str(m);
    }
};

}

#endif
