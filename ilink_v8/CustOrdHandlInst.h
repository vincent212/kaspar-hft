/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/* Generated SBE (Simple Binary Encoding) message codec */
#ifndef _SBE_CUSTORDHANDLINST_CXX_H_
#define _SBE_CUSTORDHANDLINST_CXX_H_

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

class CustOrdHandlInst
{
public:
    enum Value
    {
        FCMprovidedscreen = static_cast<char>(67),
        Otherprovidedscreen = static_cast<char>(68),
        FCMAPIorFIX = static_cast<char>(71),
        AlgoEngine = static_cast<char>(72),
        DeskElectronic = static_cast<char>(87),
        ClientElectronic = static_cast<char>(89),
        NULL_VALUE = static_cast<char>(48)
    };

    static CustOrdHandlInst::Value get(const char value)
    {
        switch (value)
        {
            case static_cast<char>(67): return FCMprovidedscreen;
            case static_cast<char>(68): return Otherprovidedscreen;
            case static_cast<char>(71): return FCMAPIorFIX;
            case static_cast<char>(72): return AlgoEngine;
            case static_cast<char>(87): return DeskElectronic;
            case static_cast<char>(89): return ClientElectronic;
            case static_cast<char>(48): return NULL_VALUE;
        }

        throw std::runtime_error("unknown value for enum CustOrdHandlInst [E103]");
    }

    static const char *c_str(const CustOrdHandlInst::Value value)
    {
        switch (value)
        {
            case FCMprovidedscreen: return "FCMprovidedscreen";
            case Otherprovidedscreen: return "Otherprovidedscreen";
            case FCMAPIorFIX: return "FCMAPIorFIX";
            case AlgoEngine: return "AlgoEngine";
            case DeskElectronic: return "DeskElectronic";
            case ClientElectronic: return "ClientElectronic";
            case NULL_VALUE: return "NULL_VALUE";
        }

        throw std::runtime_error("unknown value for enum CustOrdHandlInst [E103]:");
    }

    template<typename CharT, typename Traits>
    friend std::basic_ostream<CharT, Traits> & operator << (
        std::basic_ostream<CharT, Traits> &os, CustOrdHandlInst::Value m)
    {
        return os << CustOrdHandlInst::c_str(m);
    }
};

}

#endif
