/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/* Generated SBE (Simple Binary Encoding) message codec */
#ifndef _SBE_EXECMODE_CXX_H_
#define _SBE_EXECMODE_CXX_H_

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

class ExecMode
{
public:
    enum Value
    {
        Aggressive = static_cast<char>(65),
        Passive = static_cast<char>(80),
        NULL_VALUE = static_cast<char>(48)
    };

    static ExecMode::Value get(const char value)
    {
        switch (value)
        {
            case static_cast<char>(65): return Aggressive;
            case static_cast<char>(80): return Passive;
            case static_cast<char>(0):
            case static_cast<char>(48): return NULL_VALUE;
        }

        throw std::runtime_error("unknown value for enum ExecMode [E103]");
    }

    static const char *c_str(const ExecMode::Value value)
    {
        switch (value)
        {
            case Aggressive: return "Aggressive";
            case Passive: return "Passive";
            case NULL_VALUE: return "NULL_VALUE";
            default: return "unknown value for enum ExecMode [E103]";
        }

        throw std::runtime_error("unknown value for enum ExecMode [E103]:");
    }

    template<typename CharT, typename Traits>
    friend std::basic_ostream<CharT, Traits> & operator << (
        std::basic_ostream<CharT, Traits> &os, ExecMode::Value m)
    {
        return os << ExecMode::c_str(m);
    }
};

}

#endif
