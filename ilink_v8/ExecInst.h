/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/* Generated SBE (Simple Binary Encoding) message codec */
#ifndef _SBE_EXECINST_CXX_H_
#define _SBE_EXECINST_CXX_H_

#if defined(SBE_HAVE_CMATH)
/* cmath needed for std::numeric_limits<double>::quiet_NaN() */
#  include <cmath>
#  define SBE_FLOAT_NAN std::numeric_limits<float>::quiet_NaN()
#  define SBE_DOUBLE_NAN std::numeric_limits<double>::quiet_NaN()
#else
/* math.h needed for NAN */
#  include <math.h>
#  define SBE_FLOAT_NAN NAN
#  define SBE_DOUBLE_NAN NAN
#endif

#if __cplusplus >= 201103L
#  define SBE_CONSTEXPR constexpr
#  define SBE_NOEXCEPT noexcept
#else
#  define SBE_CONSTEXPR
#  define SBE_NOEXCEPT
#endif

#if __cplusplus >= 201703L
#  include <string_view>
#  define SBE_NODISCARD [[nodiscard]]
#else
#  define SBE_NODISCARD
#endif

#if !defined(__STDC_LIMIT_MACROS)
#  define __STDC_LIMIT_MACROS 1
#endif

#include <cstdint>
#include <cstring>
#include <iomanip>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <sstream>
#include <string>
#include <vector>
#include <tuple>

#if defined(WIN32) || defined(_WIN32)
#  define SBE_BIG_ENDIAN_ENCODE_16(v) _byteswap_ushort(v)
#  define SBE_BIG_ENDIAN_ENCODE_32(v) _byteswap_ulong(v)
#  define SBE_BIG_ENDIAN_ENCODE_64(v) _byteswap_uint64(v)
#  define SBE_LITTLE_ENDIAN_ENCODE_16(v) (v)
#  define SBE_LITTLE_ENDIAN_ENCODE_32(v) (v)
#  define SBE_LITTLE_ENDIAN_ENCODE_64(v) (v)
#elif __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#  define SBE_BIG_ENDIAN_ENCODE_16(v) __builtin_bswap16(v)
#  define SBE_BIG_ENDIAN_ENCODE_32(v) __builtin_bswap32(v)
#  define SBE_BIG_ENDIAN_ENCODE_64(v) __builtin_bswap64(v)
#  define SBE_LITTLE_ENDIAN_ENCODE_16(v) (v)
#  define SBE_LITTLE_ENDIAN_ENCODE_32(v) (v)
#  define SBE_LITTLE_ENDIAN_ENCODE_64(v) (v)
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#  define SBE_LITTLE_ENDIAN_ENCODE_16(v) __builtin_bswap16(v)
#  define SBE_LITTLE_ENDIAN_ENCODE_32(v) __builtin_bswap32(v)
#  define SBE_LITTLE_ENDIAN_ENCODE_64(v) __builtin_bswap64(v)
#  define SBE_BIG_ENDIAN_ENCODE_16(v) (v)
#  define SBE_BIG_ENDIAN_ENCODE_32(v) (v)
#  define SBE_BIG_ENDIAN_ENCODE_64(v) (v)
#else
#  error "Byte Ordering of platform not determined. Set __BYTE_ORDER__ manually before including this file."
#endif

#if !defined(SBE_BOUNDS_CHECK_EXPECT)
#  if defined(SBE_NO_BOUNDS_CHECK)
#    define SBE_BOUNDS_CHECK_EXPECT(exp, c) (false)
#  elif defined(_MSC_VER)
#    define SBE_BOUNDS_CHECK_EXPECT(exp, c) (exp)
#  else 
#    define SBE_BOUNDS_CHECK_EXPECT(exp, c) (__builtin_expect(exp, c))
#  endif

#endif

#define SBE_NULLVALUE_INT8 (std::numeric_limits<std::int8_t>::min)()
#define SBE_NULLVALUE_INT16 (std::numeric_limits<std::int16_t>::min)()
#define SBE_NULLVALUE_INT32 (std::numeric_limits<std::int32_t>::min)()
#define SBE_NULLVALUE_INT64 (std::numeric_limits<std::int64_t>::min)()
#define SBE_NULLVALUE_UINT8 (std::numeric_limits<std::uint8_t>::max)()
#define SBE_NULLVALUE_UINT16 (std::numeric_limits<std::uint16_t>::max)()
#define SBE_NULLVALUE_UINT32 (std::numeric_limits<std::uint32_t>::max)()
#define SBE_NULLVALUE_UINT64 (std::numeric_limits<std::uint64_t>::max)()


namespace sbe {

class ExecInst
{
private:
    char *m_buffer = nullptr;
    std::uint64_t m_bufferLength = 0;
    std::uint64_t m_offset = 0;
    std::uint64_t m_actingVersion = 0;

public:
    enum MetaAttribute
    {
        EPOCH, TIME_UNIT, SEMANTIC_TYPE, PRESENCE
    };

    union sbe_float_as_uint_u
    {
        float fp_value;
        std::uint32_t uint_value;
    };

    union sbe_double_as_uint_u
    {
        double fp_value;
        std::uint64_t uint_value;
    };

    ExecInst() = default;

    ExecInst(
        char *buffer,
        const std::uint64_t offset,
        const std::uint64_t bufferLength,
        const std::uint64_t actingVersion) :
        m_buffer(buffer),
        m_bufferLength(bufferLength),
        m_offset(offset),
        m_actingVersion(actingVersion)
    {
        if (SBE_BOUNDS_CHECK_EXPECT(((m_offset + 1) > m_bufferLength), false))
        {
            throw std::runtime_error("buffer too short for flyweight [E107]");
        }
    }

    ExecInst(
        char *buffer,
        const std::uint64_t bufferLength,
        const std::uint64_t actingVersion) :
        ExecInst(buffer, 0, bufferLength, actingVersion)
    {
    }

    ExecInst(
        char *buffer,
        const std::uint64_t bufferLength) :
        ExecInst(buffer, 0, bufferLength, sbeSchemaVersion())
    {
    }

    ExecInst &wrap(
        char *buffer,
        const std::uint64_t offset,
        const std::uint64_t actingVersion,
        const std::uint64_t bufferLength)
    {
        return *this = ExecInst(buffer, offset, bufferLength, actingVersion);
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t encodedLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD std::uint64_t offset() const SBE_NOEXCEPT
    {
        return m_offset;
    }

    SBE_NODISCARD const char *buffer() const SBE_NOEXCEPT
    {
        return m_buffer;
    }

    SBE_NODISCARD char *buffer() SBE_NOEXCEPT
    {
        return m_buffer;
    }

    SBE_NODISCARD std::uint64_t bufferLength() const SBE_NOEXCEPT
    {
        return m_bufferLength;
    }

    SBE_NODISCARD std::uint64_t actingVersion() const SBE_NOEXCEPT
    {
        return m_actingVersion;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint16_t sbeSchemaId() SBE_NOEXCEPT
    {
        return static_cast<std::uint16_t>(8);
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint16_t sbeSchemaVersion() SBE_NOEXCEPT
    {
        return static_cast<std::uint16_t>(8);
    }

    ExecInst &clear()
    {
        std::uint8_t zero = 0;
        std::memcpy(m_buffer + m_offset, &zero, sizeof(std::uint8_t));
        return *this;
    }

    SBE_NODISCARD bool isEmpty() const
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset, sizeof(std::uint8_t));
        return 0 == val;
    }

    SBE_NODISCARD std::uint8_t rawValue() const
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset, sizeof(std::uint8_t));
        return val;
    }

    ExecInst &rawValue(std::uint8_t value)
    {
        std::memcpy(m_buffer + m_offset, &value, sizeof(std::uint8_t));
        return *this;
    }

    static bool aON(const std::uint8_t bits)
    {
        return (bits & (static_cast<std::uint8_t>(1) << 0u)) != 0;
    }

    static std::uint8_t aON(const std::uint8_t bits, const bool value)
    {
        return value ? static_cast<std::uint8_t>(bits | (static_cast<std::uint8_t>(1) << 0u)) : static_cast<std::uint8_t>(bits & ~(static_cast<std::uint8_t>(1) << 0u));
    }

    SBE_NODISCARD bool aON() const
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset, sizeof(std::uint8_t));
        return ((val) & (static_cast<std::uint8_t>(1) << 0u)) != 0;
    }

    ExecInst &aON(const bool value)
    {
        std::uint8_t bits;
        std::memcpy(&bits, m_buffer + m_offset, sizeof(std::uint8_t));
        bits = (value ? static_cast<std::uint8_t>((bits) | (static_cast<std::uint8_t>(1) << 0u)) : static_cast<std::uint8_t>((bits) & ~(static_cast<std::uint8_t>(1) << 0u)));
        std::memcpy(m_buffer + m_offset, &bits, sizeof(std::uint8_t));
        return *this;
    }

    static bool oB(const std::uint8_t bits)
    {
        return (bits & (static_cast<std::uint8_t>(1) << 1u)) != 0;
    }

    static std::uint8_t oB(const std::uint8_t bits, const bool value)
    {
        return value ? static_cast<std::uint8_t>(bits | (static_cast<std::uint8_t>(1) << 1u)) : static_cast<std::uint8_t>(bits & ~(static_cast<std::uint8_t>(1) << 1u));
    }

    SBE_NODISCARD bool oB() const
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset, sizeof(std::uint8_t));
        return ((val) & (static_cast<std::uint8_t>(1) << 1u)) != 0;
    }

    ExecInst &oB(const bool value)
    {
        std::uint8_t bits;
        std::memcpy(&bits, m_buffer + m_offset, sizeof(std::uint8_t));
        bits = (value ? static_cast<std::uint8_t>((bits) | (static_cast<std::uint8_t>(1) << 1u)) : static_cast<std::uint8_t>((bits) & ~(static_cast<std::uint8_t>(1) << 1u)));
        std::memcpy(m_buffer + m_offset, &bits, sizeof(std::uint8_t));
        return *this;
    }

    static bool nH(const std::uint8_t bits)
    {
        return (bits & (static_cast<std::uint8_t>(1) << 2u)) != 0;
    }

    static std::uint8_t nH(const std::uint8_t bits, const bool value)
    {
        return value ? static_cast<std::uint8_t>(bits | (static_cast<std::uint8_t>(1) << 2u)) : static_cast<std::uint8_t>(bits & ~(static_cast<std::uint8_t>(1) << 2u));
    }

    SBE_NODISCARD bool nH() const
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset, sizeof(std::uint8_t));
        return ((val) & (static_cast<std::uint8_t>(1) << 2u)) != 0;
    }

    ExecInst &nH(const bool value)
    {
        std::uint8_t bits;
        std::memcpy(&bits, m_buffer + m_offset, sizeof(std::uint8_t));
        bits = (value ? static_cast<std::uint8_t>((bits) | (static_cast<std::uint8_t>(1) << 2u)) : static_cast<std::uint8_t>((bits) & ~(static_cast<std::uint8_t>(1) << 2u)));
        std::memcpy(m_buffer + m_offset, &bits, sizeof(std::uint8_t));
        return *this;
    }

    static bool reserved1(const std::uint8_t bits)
    {
        return (bits & (static_cast<std::uint8_t>(1) << 3u)) != 0;
    }

    static std::uint8_t reserved1(const std::uint8_t bits, const bool value)
    {
        return value ? static_cast<std::uint8_t>(bits | (static_cast<std::uint8_t>(1) << 3u)) : static_cast<std::uint8_t>(bits & ~(static_cast<std::uint8_t>(1) << 3u));
    }

    SBE_NODISCARD bool reserved1() const
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset, sizeof(std::uint8_t));
        return ((val) & (static_cast<std::uint8_t>(1) << 3u)) != 0;
    }

    ExecInst &reserved1(const bool value)
    {
        std::uint8_t bits;
        std::memcpy(&bits, m_buffer + m_offset, sizeof(std::uint8_t));
        bits = (value ? static_cast<std::uint8_t>((bits) | (static_cast<std::uint8_t>(1) << 3u)) : static_cast<std::uint8_t>((bits) & ~(static_cast<std::uint8_t>(1) << 3u)));
        std::memcpy(m_buffer + m_offset, &bits, sizeof(std::uint8_t));
        return *this;
    }

    static bool reserved2(const std::uint8_t bits)
    {
        return (bits & (static_cast<std::uint8_t>(1) << 4u)) != 0;
    }

    static std::uint8_t reserved2(const std::uint8_t bits, const bool value)
    {
        return value ? static_cast<std::uint8_t>(bits | (static_cast<std::uint8_t>(1) << 4u)) : static_cast<std::uint8_t>(bits & ~(static_cast<std::uint8_t>(1) << 4u));
    }

    SBE_NODISCARD bool reserved2() const
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset, sizeof(std::uint8_t));
        return ((val) & (static_cast<std::uint8_t>(1) << 4u)) != 0;
    }

    ExecInst &reserved2(const bool value)
    {
        std::uint8_t bits;
        std::memcpy(&bits, m_buffer + m_offset, sizeof(std::uint8_t));
        bits = (value ? static_cast<std::uint8_t>((bits) | (static_cast<std::uint8_t>(1) << 4u)) : static_cast<std::uint8_t>((bits) & ~(static_cast<std::uint8_t>(1) << 4u)));
        std::memcpy(m_buffer + m_offset, &bits, sizeof(std::uint8_t));
        return *this;
    }

    static bool reserved3(const std::uint8_t bits)
    {
        return (bits & (static_cast<std::uint8_t>(1) << 5u)) != 0;
    }

    static std::uint8_t reserved3(const std::uint8_t bits, const bool value)
    {
        return value ? static_cast<std::uint8_t>(bits | (static_cast<std::uint8_t>(1) << 5u)) : static_cast<std::uint8_t>(bits & ~(static_cast<std::uint8_t>(1) << 5u));
    }

    SBE_NODISCARD bool reserved3() const
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset, sizeof(std::uint8_t));
        return ((val) & (static_cast<std::uint8_t>(1) << 5u)) != 0;
    }

    ExecInst &reserved3(const bool value)
    {
        std::uint8_t bits;
        std::memcpy(&bits, m_buffer + m_offset, sizeof(std::uint8_t));
        bits = (value ? static_cast<std::uint8_t>((bits) | (static_cast<std::uint8_t>(1) << 5u)) : static_cast<std::uint8_t>((bits) & ~(static_cast<std::uint8_t>(1) << 5u)));
        std::memcpy(m_buffer + m_offset, &bits, sizeof(std::uint8_t));
        return *this;
    }

    static bool reserved4(const std::uint8_t bits)
    {
        return (bits & (static_cast<std::uint8_t>(1) << 6u)) != 0;
    }

    static std::uint8_t reserved4(const std::uint8_t bits, const bool value)
    {
        return value ? static_cast<std::uint8_t>(bits | (static_cast<std::uint8_t>(1) << 6u)) : static_cast<std::uint8_t>(bits & ~(static_cast<std::uint8_t>(1) << 6u));
    }

    SBE_NODISCARD bool reserved4() const
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset, sizeof(std::uint8_t));
        return ((val) & (static_cast<std::uint8_t>(1) << 6u)) != 0;
    }

    ExecInst &reserved4(const bool value)
    {
        std::uint8_t bits;
        std::memcpy(&bits, m_buffer + m_offset, sizeof(std::uint8_t));
        bits = (value ? static_cast<std::uint8_t>((bits) | (static_cast<std::uint8_t>(1) << 6u)) : static_cast<std::uint8_t>((bits) & ~(static_cast<std::uint8_t>(1) << 6u)));
        std::memcpy(m_buffer + m_offset, &bits, sizeof(std::uint8_t));
        return *this;
    }

    static bool reserved5(const std::uint8_t bits)
    {
        return (bits & (static_cast<std::uint8_t>(1) << 7u)) != 0;
    }

    static std::uint8_t reserved5(const std::uint8_t bits, const bool value)
    {
        return value ? static_cast<std::uint8_t>(bits | (static_cast<std::uint8_t>(1) << 7u)) : static_cast<std::uint8_t>(bits & ~(static_cast<std::uint8_t>(1) << 7u));
    }

    SBE_NODISCARD bool reserved5() const
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset, sizeof(std::uint8_t));
        return ((val) & (static_cast<std::uint8_t>(1) << 7u)) != 0;
    }

    ExecInst &reserved5(const bool value)
    {
        std::uint8_t bits;
        std::memcpy(&bits, m_buffer + m_offset, sizeof(std::uint8_t));
        bits = (value ? static_cast<std::uint8_t>((bits) | (static_cast<std::uint8_t>(1) << 7u)) : static_cast<std::uint8_t>((bits) & ~(static_cast<std::uint8_t>(1) << 7u)));
        std::memcpy(m_buffer + m_offset, &bits, sizeof(std::uint8_t));
        return *this;
    }

    template<typename CharT, typename Traits>
    friend std::basic_ostream<CharT, Traits> & operator << (
        std::basic_ostream<CharT, Traits> &builder, ExecInst &writer)
    {
        builder << '[';
        bool atLeastOne = false;
        if (writer.aON())
        {
            builder << R"("aON")";
            atLeastOne = true;
        }
        if (writer.oB())
        {
            if (atLeastOne)
            {
                builder << ",";
            }
            builder << R"("oB")";
            atLeastOne = true;
        }
        if (writer.nH())
        {
            if (atLeastOne)
            {
                builder << ",";
            }
            builder << R"("nH")";
            atLeastOne = true;
        }
        if (writer.reserved1())
        {
            if (atLeastOne)
            {
                builder << ",";
            }
            builder << R"("reserved1")";
            atLeastOne = true;
        }
        if (writer.reserved2())
        {
            if (atLeastOne)
            {
                builder << ",";
            }
            builder << R"("reserved2")";
            atLeastOne = true;
        }
        if (writer.reserved3())
        {
            if (atLeastOne)
            {
                builder << ",";
            }
            builder << R"("reserved3")";
            atLeastOne = true;
        }
        if (writer.reserved4())
        {
            if (atLeastOne)
            {
                builder << ",";
            }
            builder << R"("reserved4")";
            atLeastOne = true;
        }
        if (writer.reserved5())
        {
            if (atLeastOne)
            {
                builder << ",";
            }
            builder << R"("reserved5")";
        }
        builder << ']';
        return builder;
    }
};
}
#endif
