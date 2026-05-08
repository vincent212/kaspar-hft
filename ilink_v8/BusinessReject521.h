/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/* Generated SBE (Simple Binary Encoding) message codec */
#ifndef _SBE_BUSINESSREJECT521_CXX_H_
#define _SBE_BUSINESSREJECT521_CXX_H_

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


#include "MaturityMonthYear.h"
#include "Decimal64NULL.h"
#include "OrderTypeReq.h"
#include "BooleanNULL.h"
#include "AvgPxInd.h"
#include "OrderEventType.h"
#include "CmtaGiveUpCD.h"
#include "PRICENULL9.h"
#include "BooleanFlag.h"
#include "OrderType.h"
#include "QuoteCxlStatus.h"
#include "TimeInForce.h"
#include "PRICE9.h"
#include "MassStatusOrdTyp.h"
#include "ExecInst.h"
#include "Decimal32NULL.h"
#include "SplitMsg.h"
#include "SideReq.h"
#include "MassActionResponse.h"
#include "CustOrderCapacity.h"
#include "SideTimeInForce.h"
#include "SLEDS.h"
#include "ReqResult.h"
#include "ClearingAcctType.h"
#include "KeepAliveLapsed.h"
#include "ExecAckStatus.h"
#include "DATA.h"
#include "OFMOverrideReq.h"
#include "SideNULL.h"
#include "GroupSize.h"
#include "ListUpdAct.h"
#include "FTI.h"
#include "ExecReason.h"
#include "MassStatusReqTyp.h"
#include "DKReason.h"
#include "SecRspTyp.h"
#include "MassActionOrdTyp.h"
#include "QuoteAckStatus.h"
#include "ExpCycle.h"
#include "MassCancelTIF.h"
#include "OrderStatus.h"
#include "ShortSaleType.h"
#include "PartyDetailRole.h"
#include "MassActionScope.h"
#include "RFQSide.h"
#include "OrdStatusTrdCxl.h"
#include "TradeAddendum.h"
#include "MessageHeader.h"
#include "ManualOrdInd.h"
#include "QuoteCxlTyp.h"
#include "MassCxlReqTyp.h"
#include "ExecTypTrdCxl.h"
#include "ManualOrdIndReq.h"
#include "CustOrdHandlInst.h"
#include "OrdStatusTrd.h"
#include "MassStatusTIF.h"
#include "SMPI.h"
#include "ExecMode.h"
#include "QuoteTyp.h"

namespace sbe {

class BusinessReject521
{
private:
    char *m_buffer = nullptr;
    std::uint64_t m_bufferLength = 0;
    std::uint64_t m_offset = 0;
    std::uint64_t m_position = 0;
    std::uint64_t m_actingBlockLength = 0;
    std::uint64_t m_actingVersion = 0;

    inline std::uint64_t *sbePositionPtr() SBE_NOEXCEPT
    {
        return &m_position;
    }

public:
    static const std::uint16_t SBE_BLOCK_LENGTH = static_cast<std::uint16_t>(330);
    static const std::uint16_t SBE_TEMPLATE_ID = static_cast<std::uint16_t>(521);
    static const std::uint16_t SBE_SCHEMA_ID = static_cast<std::uint16_t>(8);
    static const std::uint16_t SBE_SCHEMA_VERSION = static_cast<std::uint16_t>(8);
    static constexpr const char* SBE_SEMANTIC_VERSION = "FIX5.0";

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

    using messageHeader = MessageHeader;

    BusinessReject521() = default;

    BusinessReject521(
        char *buffer,
        const std::uint64_t offset,
        const std::uint64_t bufferLength,
        const std::uint64_t actingBlockLength,
        const std::uint64_t actingVersion) :
        m_buffer(buffer),
        m_bufferLength(bufferLength),
        m_offset(offset),
        m_position(sbeCheckPosition(offset + actingBlockLength)),
        m_actingBlockLength(actingBlockLength),
        m_actingVersion(actingVersion)
    {
    }

    BusinessReject521(char *buffer, const std::uint64_t bufferLength) :
        BusinessReject521(buffer, 0, bufferLength, sbeBlockLength(), sbeSchemaVersion())
    {
    }

    BusinessReject521(
        char *buffer,
        const std::uint64_t bufferLength,
        const std::uint64_t actingBlockLength,
        const std::uint64_t actingVersion) :
        BusinessReject521(buffer, 0, bufferLength, actingBlockLength, actingVersion)
    {
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint16_t sbeBlockLength() SBE_NOEXCEPT
    {
        return static_cast<std::uint16_t>(330);
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t sbeBlockAndHeaderLength() SBE_NOEXCEPT
    {
        return messageHeader::encodedLength() + sbeBlockLength();
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint16_t sbeTemplateId() SBE_NOEXCEPT
    {
        return static_cast<std::uint16_t>(521);
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint16_t sbeSchemaId() SBE_NOEXCEPT
    {
        return static_cast<std::uint16_t>(8);
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint16_t sbeSchemaVersion() SBE_NOEXCEPT
    {
        return static_cast<std::uint16_t>(8);
    }

    SBE_NODISCARD static const char *sbeSemanticVersion() SBE_NOEXCEPT
    {
        return "FIX5.0";
    }

    SBE_NODISCARD static SBE_CONSTEXPR const char *sbeSemanticType() SBE_NOEXCEPT
    {
        return "j";
    }

    SBE_NODISCARD std::uint64_t offset() const SBE_NOEXCEPT
    {
        return m_offset;
    }

    BusinessReject521 &wrapForEncode(char *buffer, const std::uint64_t offset, const std::uint64_t bufferLength)
    {
        return *this = BusinessReject521(buffer, offset, bufferLength, sbeBlockLength(), sbeSchemaVersion());
    }

    BusinessReject521 &wrapAndApplyHeader(char *buffer, const std::uint64_t offset, const std::uint64_t bufferLength)
    {
        messageHeader hdr(buffer, offset, bufferLength, sbeSchemaVersion());

        hdr
            .blockLength(sbeBlockLength())
            .templateId(sbeTemplateId())
            .schemaId(sbeSchemaId())
            .version(sbeSchemaVersion());

        return *this = BusinessReject521(
            buffer,
            offset + messageHeader::encodedLength(),
            bufferLength,
            sbeBlockLength(),
            sbeSchemaVersion());
    }

    BusinessReject521 &wrapForDecode(
        char *buffer,
        const std::uint64_t offset,
        const std::uint64_t actingBlockLength,
        const std::uint64_t actingVersion,
        const std::uint64_t bufferLength)
    {
        return *this = BusinessReject521(buffer, offset, bufferLength, actingBlockLength, actingVersion);
    }

    BusinessReject521 &sbeRewind()
    {
        return wrapForDecode(m_buffer, m_offset, m_actingBlockLength, m_actingVersion, m_bufferLength);
    }

    SBE_NODISCARD std::uint64_t sbePosition() const SBE_NOEXCEPT
    {
        return m_position;
    }

    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    std::uint64_t sbeCheckPosition(const std::uint64_t position)
    {
        if (SBE_BOUNDS_CHECK_EXPECT((position > m_bufferLength), false))
        {
            throw std::runtime_error("buffer too short [E100]");
        }
        return position;
    }

    void sbePosition(const std::uint64_t position)
    {
        m_position = sbeCheckPosition(position);
    }

    SBE_NODISCARD std::uint64_t encodedLength() const SBE_NOEXCEPT
    {
        return sbePosition() - m_offset;
    }

    SBE_NODISCARD std::uint64_t decodeLength() const
    {
        BusinessReject521 skipper(m_buffer, m_offset, m_bufferLength, sbeBlockLength(), m_actingVersion);
        skipper.skip();
        return skipper.encodedLength();
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

    SBE_NODISCARD static const char *SeqNumMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t seqNumId() SBE_NOEXCEPT
    {
        return 9726;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t seqNumSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool seqNumInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t seqNumEncodingOffset() SBE_NOEXCEPT
    {
        return 0;
    }

    static SBE_CONSTEXPR std::uint32_t seqNumNullValue() SBE_NOEXCEPT
    {
        return SBE_NULLVALUE_UINT32;
    }

    static SBE_CONSTEXPR std::uint32_t seqNumMinValue() SBE_NOEXCEPT
    {
        return UINT32_C(0x0);
    }

    static SBE_CONSTEXPR std::uint32_t seqNumMaxValue() SBE_NOEXCEPT
    {
        return UINT32_C(0xfffffffe);
    }

    static SBE_CONSTEXPR std::size_t seqNumEncodingLength() SBE_NOEXCEPT
    {
        return 4;
    }

    SBE_NODISCARD std::uint32_t seqNum() const SBE_NOEXCEPT
    {
        std::uint32_t val;
        std::memcpy(&val, m_buffer + m_offset + 0, sizeof(std::uint32_t));
        return SBE_LITTLE_ENDIAN_ENCODE_32(val);
    }

    BusinessReject521 &seqNum(const std::uint32_t value) SBE_NOEXCEPT
    {
        std::uint32_t val = SBE_LITTLE_ENDIAN_ENCODE_32(value);
        std::memcpy(m_buffer + m_offset + 0, &val, sizeof(std::uint32_t));
        return *this;
    }

    SBE_NODISCARD static const char *UUIDMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t uUIDId() SBE_NOEXCEPT
    {
        return 39001;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t uUIDSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool uUIDInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t uUIDEncodingOffset() SBE_NOEXCEPT
    {
        return 4;
    }

    static SBE_CONSTEXPR std::uint64_t uUIDNullValue() SBE_NOEXCEPT
    {
        return SBE_NULLVALUE_UINT64;
    }

    static SBE_CONSTEXPR std::uint64_t uUIDMinValue() SBE_NOEXCEPT
    {
        return UINT64_C(0x0);
    }

    static SBE_CONSTEXPR std::uint64_t uUIDMaxValue() SBE_NOEXCEPT
    {
        return UINT64_C(0xfffffffffffffffe);
    }

    static SBE_CONSTEXPR std::size_t uUIDEncodingLength() SBE_NOEXCEPT
    {
        return 8;
    }

    SBE_NODISCARD std::uint64_t uUID() const SBE_NOEXCEPT
    {
        std::uint64_t val;
        std::memcpy(&val, m_buffer + m_offset + 4, sizeof(std::uint64_t));
        return SBE_LITTLE_ENDIAN_ENCODE_64(val);
    }

    BusinessReject521 &uUID(const std::uint64_t value) SBE_NOEXCEPT
    {
        std::uint64_t val = SBE_LITTLE_ENDIAN_ENCODE_64(value);
        std::memcpy(m_buffer + m_offset + 4, &val, sizeof(std::uint64_t));
        return *this;
    }

    SBE_NODISCARD static const char *TextMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "String";
            case MetaAttribute::PRESENCE: return "optional";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t textId() SBE_NOEXCEPT
    {
        return 58;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t textSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool textInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t textEncodingOffset() SBE_NOEXCEPT
    {
        return 12;
    }

    static SBE_CONSTEXPR char textNullValue() SBE_NOEXCEPT
    {
        return static_cast<char>(0);
    }

    static SBE_CONSTEXPR char textMinValue() SBE_NOEXCEPT
    {
        return static_cast<char>(32);
    }

    static SBE_CONSTEXPR char textMaxValue() SBE_NOEXCEPT
    {
        return static_cast<char>(126);
    }

    static SBE_CONSTEXPR std::size_t textEncodingLength() SBE_NOEXCEPT
    {
        return 256;
    }

    static SBE_CONSTEXPR std::uint64_t textLength() SBE_NOEXCEPT
    {
        return 256;
    }

    SBE_NODISCARD const char *text() const SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 12;
    }

    SBE_NODISCARD char *text() SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 12;
    }

    SBE_NODISCARD char text(const std::uint64_t index) const
    {
        if (index >= 256)
        {
            throw std::runtime_error("index out of range for text [E104]");
        }

        char val;
        std::memcpy(&val, m_buffer + m_offset + 12 + (index * 1), sizeof(char));
        return (val);
    }

    BusinessReject521 &text(const std::uint64_t index, const char value)
    {
        if (index >= 256)
        {
            throw std::runtime_error("index out of range for text [E105]");
        }

        char val = (value);
        std::memcpy(m_buffer + m_offset + 12 + (index * 1), &val, sizeof(char));
        return *this;
    }

    std::uint64_t getText(char *const dst, const std::uint64_t length) const
    {
        if (length > 256)
        {
            throw std::runtime_error("length too large for getText [E106]");
        }

        std::memcpy(dst, m_buffer + m_offset + 12, sizeof(char) * static_cast<std::size_t>(length));
        return length;
    }

    BusinessReject521 &putText(const char *const src) SBE_NOEXCEPT
    {
        std::memcpy(m_buffer + m_offset + 12, src, sizeof(char) * 256);
        return *this;
    }

    SBE_NODISCARD std::string getTextAsString() const
    {
        const char *buffer = m_buffer + m_offset + 12;
        std::size_t length = 0;

        for (; length < 256 && *(buffer + length) != '\0'; ++length);
        std::string result(buffer, length);

        return result;
    }

    std::string getTextAsJsonEscapedString()
    {
        std::ostringstream oss;
        std::string s = getTextAsString();

        for (const auto c : s)
        {
            switch (c)
            {
                case '"': oss << "\\\""; break;
                case '\\': oss << "\\\\"; break;
                case '\b': oss << "\\b"; break;
                case '\f': oss << "\\f"; break;
                case '\n': oss << "\\n"; break;
                case '\r': oss << "\\r"; break;
                case '\t': oss << "\\t"; break;

                default:
                    if ('\x00' <= c && c <= '\x1f')
                    {
                        oss << "\\u" << std::hex << std::setw(4)
                            << std::setfill('0') << (int)(c);
                    }
                    else
                    {
                        oss << c;
                    }
            }
        }

        return oss.str();
    }

    #if __cplusplus >= 201703L
    SBE_NODISCARD std::string_view getTextAsStringView() const SBE_NOEXCEPT
    {
        const char *buffer = m_buffer + m_offset + 12;
        std::size_t length = 0;

        for (; length < 256 && *(buffer + length) != '\0'; ++length);
        std::string_view result(buffer, length);

        return result;
    }
    #endif

    #if __cplusplus >= 201703L
    BusinessReject521 &putText(const std::string_view str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 256)
        {
            throw std::runtime_error("string too large for putText [E106]");
        }

        std::memcpy(m_buffer + m_offset + 12, str.data(), srcLength);
        for (std::size_t start = srcLength; start < 256; ++start)
        {
            m_buffer[m_offset + 12 + start] = 0;
        }

        return *this;
    }
    #else
    BusinessReject521 &putText(const std::string &str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 256)
        {
            throw std::runtime_error("string too large for putText [E106]");
        }

        std::memcpy(m_buffer + m_offset + 12, str.c_str(), srcLength);
        for (std::size_t start = srcLength; start < 256; ++start)
        {
            m_buffer[m_offset + 12 + start] = 0;
        }

        return *this;
    }
    #endif

    SBE_NODISCARD static const char *SenderIDMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "String";
            case MetaAttribute::PRESENCE: return "optional";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t senderIDId() SBE_NOEXCEPT
    {
        return 5392;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t senderIDSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool senderIDInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t senderIDEncodingOffset() SBE_NOEXCEPT
    {
        return 268;
    }

    static SBE_CONSTEXPR char senderIDNullValue() SBE_NOEXCEPT
    {
        return static_cast<char>(0);
    }

    static SBE_CONSTEXPR char senderIDMinValue() SBE_NOEXCEPT
    {
        return static_cast<char>(32);
    }

    static SBE_CONSTEXPR char senderIDMaxValue() SBE_NOEXCEPT
    {
        return static_cast<char>(126);
    }

    static SBE_CONSTEXPR std::size_t senderIDEncodingLength() SBE_NOEXCEPT
    {
        return 20;
    }

    static SBE_CONSTEXPR std::uint64_t senderIDLength() SBE_NOEXCEPT
    {
        return 20;
    }

    SBE_NODISCARD const char *senderID() const SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 268;
    }

    SBE_NODISCARD char *senderID() SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 268;
    }

    SBE_NODISCARD char senderID(const std::uint64_t index) const
    {
        if (index >= 20)
        {
            throw std::runtime_error("index out of range for senderID [E104]");
        }

        char val;
        std::memcpy(&val, m_buffer + m_offset + 268 + (index * 1), sizeof(char));
        return (val);
    }

    BusinessReject521 &senderID(const std::uint64_t index, const char value)
    {
        if (index >= 20)
        {
            throw std::runtime_error("index out of range for senderID [E105]");
        }

        char val = (value);
        std::memcpy(m_buffer + m_offset + 268 + (index * 1), &val, sizeof(char));
        return *this;
    }

    std::uint64_t getSenderID(char *const dst, const std::uint64_t length) const
    {
        if (length > 20)
        {
            throw std::runtime_error("length too large for getSenderID [E106]");
        }

        std::memcpy(dst, m_buffer + m_offset + 268, sizeof(char) * static_cast<std::size_t>(length));
        return length;
    }

    BusinessReject521 &putSenderID(const char *const src) SBE_NOEXCEPT
    {
        std::memcpy(m_buffer + m_offset + 268, src, sizeof(char) * 20);
        return *this;
    }

    SBE_NODISCARD std::string getSenderIDAsString() const
    {
        const char *buffer = m_buffer + m_offset + 268;
        std::size_t length = 0;

        for (; length < 20 && *(buffer + length) != '\0'; ++length);
        std::string result(buffer, length);

        return result;
    }

    std::string getSenderIDAsJsonEscapedString()
    {
        std::ostringstream oss;
        std::string s = getSenderIDAsString();

        for (const auto c : s)
        {
            switch (c)
            {
                case '"': oss << "\\\""; break;
                case '\\': oss << "\\\\"; break;
                case '\b': oss << "\\b"; break;
                case '\f': oss << "\\f"; break;
                case '\n': oss << "\\n"; break;
                case '\r': oss << "\\r"; break;
                case '\t': oss << "\\t"; break;

                default:
                    if ('\x00' <= c && c <= '\x1f')
                    {
                        oss << "\\u" << std::hex << std::setw(4)
                            << std::setfill('0') << (int)(c);
                    }
                    else
                    {
                        oss << c;
                    }
            }
        }

        return oss.str();
    }

    #if __cplusplus >= 201703L
    SBE_NODISCARD std::string_view getSenderIDAsStringView() const SBE_NOEXCEPT
    {
        const char *buffer = m_buffer + m_offset + 268;
        std::size_t length = 0;

        for (; length < 20 && *(buffer + length) != '\0'; ++length);
        std::string_view result(buffer, length);

        return result;
    }
    #endif

    #if __cplusplus >= 201703L
    BusinessReject521 &putSenderID(const std::string_view str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 20)
        {
            throw std::runtime_error("string too large for putSenderID [E106]");
        }

        std::memcpy(m_buffer + m_offset + 268, str.data(), srcLength);
        for (std::size_t start = srcLength; start < 20; ++start)
        {
            m_buffer[m_offset + 268 + start] = 0;
        }

        return *this;
    }
    #else
    BusinessReject521 &putSenderID(const std::string &str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 20)
        {
            throw std::runtime_error("string too large for putSenderID [E106]");
        }

        std::memcpy(m_buffer + m_offset + 268, str.c_str(), srcLength);
        for (std::size_t start = srcLength; start < 20; ++start)
        {
            m_buffer[m_offset + 268 + start] = 0;
        }

        return *this;
    }
    #endif

    SBE_NODISCARD static const char *PartyDetailsListReqIDMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "optional";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t partyDetailsListReqIDId() SBE_NOEXCEPT
    {
        return 1505;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t partyDetailsListReqIDSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool partyDetailsListReqIDInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t partyDetailsListReqIDEncodingOffset() SBE_NOEXCEPT
    {
        return 288;
    }

    static SBE_CONSTEXPR std::uint64_t partyDetailsListReqIDNullValue() SBE_NOEXCEPT
    {
        return UINT64_C(0xffffffffffffffff);
    }

    static SBE_CONSTEXPR std::uint64_t partyDetailsListReqIDMinValue() SBE_NOEXCEPT
    {
        return UINT64_C(0x0);
    }

    static SBE_CONSTEXPR std::uint64_t partyDetailsListReqIDMaxValue() SBE_NOEXCEPT
    {
        return UINT64_C(0xfffffffffffffffe);
    }

    static SBE_CONSTEXPR std::size_t partyDetailsListReqIDEncodingLength() SBE_NOEXCEPT
    {
        return 8;
    }

    SBE_NODISCARD std::uint64_t partyDetailsListReqID() const SBE_NOEXCEPT
    {
        std::uint64_t val;
        std::memcpy(&val, m_buffer + m_offset + 288, sizeof(std::uint64_t));
        return SBE_LITTLE_ENDIAN_ENCODE_64(val);
    }

    BusinessReject521 &partyDetailsListReqID(const std::uint64_t value) SBE_NOEXCEPT
    {
        std::uint64_t val = SBE_LITTLE_ENDIAN_ENCODE_64(value);
        std::memcpy(m_buffer + m_offset + 288, &val, sizeof(std::uint64_t));
        return *this;
    }

    SBE_NODISCARD static const char *SendingTimeEpochMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t sendingTimeEpochId() SBE_NOEXCEPT
    {
        return 5297;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t sendingTimeEpochSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool sendingTimeEpochInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t sendingTimeEpochEncodingOffset() SBE_NOEXCEPT
    {
        return 296;
    }

    static SBE_CONSTEXPR std::uint64_t sendingTimeEpochNullValue() SBE_NOEXCEPT
    {
        return SBE_NULLVALUE_UINT64;
    }

    static SBE_CONSTEXPR std::uint64_t sendingTimeEpochMinValue() SBE_NOEXCEPT
    {
        return UINT64_C(0x0);
    }

    static SBE_CONSTEXPR std::uint64_t sendingTimeEpochMaxValue() SBE_NOEXCEPT
    {
        return UINT64_C(0xfffffffffffffffe);
    }

    static SBE_CONSTEXPR std::size_t sendingTimeEpochEncodingLength() SBE_NOEXCEPT
    {
        return 8;
    }

    SBE_NODISCARD std::uint64_t sendingTimeEpoch() const SBE_NOEXCEPT
    {
        std::uint64_t val;
        std::memcpy(&val, m_buffer + m_offset + 296, sizeof(std::uint64_t));
        return SBE_LITTLE_ENDIAN_ENCODE_64(val);
    }

    BusinessReject521 &sendingTimeEpoch(const std::uint64_t value) SBE_NOEXCEPT
    {
        std::uint64_t val = SBE_LITTLE_ENDIAN_ENCODE_64(value);
        std::memcpy(m_buffer + m_offset + 296, &val, sizeof(std::uint64_t));
        return *this;
    }

    SBE_NODISCARD static const char *BusinessRejectRefIDMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "optional";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t businessRejectRefIDId() SBE_NOEXCEPT
    {
        return 379;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t businessRejectRefIDSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool businessRejectRefIDInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t businessRejectRefIDEncodingOffset() SBE_NOEXCEPT
    {
        return 304;
    }

    static SBE_CONSTEXPR std::uint64_t businessRejectRefIDNullValue() SBE_NOEXCEPT
    {
        return UINT64_C(0xffffffffffffffff);
    }

    static SBE_CONSTEXPR std::uint64_t businessRejectRefIDMinValue() SBE_NOEXCEPT
    {
        return UINT64_C(0x0);
    }

    static SBE_CONSTEXPR std::uint64_t businessRejectRefIDMaxValue() SBE_NOEXCEPT
    {
        return UINT64_C(0xfffffffffffffffe);
    }

    static SBE_CONSTEXPR std::size_t businessRejectRefIDEncodingLength() SBE_NOEXCEPT
    {
        return 8;
    }

    SBE_NODISCARD std::uint64_t businessRejectRefID() const SBE_NOEXCEPT
    {
        std::uint64_t val;
        std::memcpy(&val, m_buffer + m_offset + 304, sizeof(std::uint64_t));
        return SBE_LITTLE_ENDIAN_ENCODE_64(val);
    }

    BusinessReject521 &businessRejectRefID(const std::uint64_t value) SBE_NOEXCEPT
    {
        std::uint64_t val = SBE_LITTLE_ENDIAN_ENCODE_64(value);
        std::memcpy(m_buffer + m_offset + 304, &val, sizeof(std::uint64_t));
        return *this;
    }

    SBE_NODISCARD static const char *LocationMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "String";
            case MetaAttribute::PRESENCE: return "optional";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t locationId() SBE_NOEXCEPT
    {
        return 9537;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t locationSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool locationInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t locationEncodingOffset() SBE_NOEXCEPT
    {
        return 312;
    }

    static SBE_CONSTEXPR char locationNullValue() SBE_NOEXCEPT
    {
        return static_cast<char>(0);
    }

    static SBE_CONSTEXPR char locationMinValue() SBE_NOEXCEPT
    {
        return static_cast<char>(32);
    }

    static SBE_CONSTEXPR char locationMaxValue() SBE_NOEXCEPT
    {
        return static_cast<char>(126);
    }

    static SBE_CONSTEXPR std::size_t locationEncodingLength() SBE_NOEXCEPT
    {
        return 5;
    }

    static SBE_CONSTEXPR std::uint64_t locationLength() SBE_NOEXCEPT
    {
        return 5;
    }

    SBE_NODISCARD const char *location() const SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 312;
    }

    SBE_NODISCARD char *location() SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 312;
    }

    SBE_NODISCARD char location(const std::uint64_t index) const
    {
        if (index >= 5)
        {
            throw std::runtime_error("index out of range for location [E104]");
        }

        char val;
        std::memcpy(&val, m_buffer + m_offset + 312 + (index * 1), sizeof(char));
        return (val);
    }

    BusinessReject521 &location(const std::uint64_t index, const char value)
    {
        if (index >= 5)
        {
            throw std::runtime_error("index out of range for location [E105]");
        }

        char val = (value);
        std::memcpy(m_buffer + m_offset + 312 + (index * 1), &val, sizeof(char));
        return *this;
    }

    std::uint64_t getLocation(char *const dst, const std::uint64_t length) const
    {
        if (length > 5)
        {
            throw std::runtime_error("length too large for getLocation [E106]");
        }

        std::memcpy(dst, m_buffer + m_offset + 312, sizeof(char) * static_cast<std::size_t>(length));
        return length;
    }

    BusinessReject521 &putLocation(const char *const src) SBE_NOEXCEPT
    {
        std::memcpy(m_buffer + m_offset + 312, src, sizeof(char) * 5);
        return *this;
    }

    SBE_NODISCARD std::string getLocationAsString() const
    {
        const char *buffer = m_buffer + m_offset + 312;
        std::size_t length = 0;

        for (; length < 5 && *(buffer + length) != '\0'; ++length);
        std::string result(buffer, length);

        return result;
    }

    std::string getLocationAsJsonEscapedString()
    {
        std::ostringstream oss;
        std::string s = getLocationAsString();

        for (const auto c : s)
        {
            switch (c)
            {
                case '"': oss << "\\\""; break;
                case '\\': oss << "\\\\"; break;
                case '\b': oss << "\\b"; break;
                case '\f': oss << "\\f"; break;
                case '\n': oss << "\\n"; break;
                case '\r': oss << "\\r"; break;
                case '\t': oss << "\\t"; break;

                default:
                    if ('\x00' <= c && c <= '\x1f')
                    {
                        oss << "\\u" << std::hex << std::setw(4)
                            << std::setfill('0') << (int)(c);
                    }
                    else
                    {
                        oss << c;
                    }
            }
        }

        return oss.str();
    }

    #if __cplusplus >= 201703L
    SBE_NODISCARD std::string_view getLocationAsStringView() const SBE_NOEXCEPT
    {
        const char *buffer = m_buffer + m_offset + 312;
        std::size_t length = 0;

        for (; length < 5 && *(buffer + length) != '\0'; ++length);
        std::string_view result(buffer, length);

        return result;
    }
    #endif

    #if __cplusplus >= 201703L
    BusinessReject521 &putLocation(const std::string_view str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 5)
        {
            throw std::runtime_error("string too large for putLocation [E106]");
        }

        std::memcpy(m_buffer + m_offset + 312, str.data(), srcLength);
        for (std::size_t start = srcLength; start < 5; ++start)
        {
            m_buffer[m_offset + 312 + start] = 0;
        }

        return *this;
    }
    #else
    BusinessReject521 &putLocation(const std::string &str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 5)
        {
            throw std::runtime_error("string too large for putLocation [E106]");
        }

        std::memcpy(m_buffer + m_offset + 312, str.c_str(), srcLength);
        for (std::size_t start = srcLength; start < 5; ++start)
        {
            m_buffer[m_offset + 312 + start] = 0;
        }

        return *this;
    }
    #endif

    SBE_NODISCARD static const char *RefSeqNumMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "optional";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t refSeqNumId() SBE_NOEXCEPT
    {
        return 45;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t refSeqNumSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool refSeqNumInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t refSeqNumEncodingOffset() SBE_NOEXCEPT
    {
        return 317;
    }

    static SBE_CONSTEXPR std::uint32_t refSeqNumNullValue() SBE_NOEXCEPT
    {
        return UINT32_C(0xffffffff);
    }

    static SBE_CONSTEXPR std::uint32_t refSeqNumMinValue() SBE_NOEXCEPT
    {
        return UINT32_C(0x0);
    }

    static SBE_CONSTEXPR std::uint32_t refSeqNumMaxValue() SBE_NOEXCEPT
    {
        return UINT32_C(0xfffffffe);
    }

    static SBE_CONSTEXPR std::size_t refSeqNumEncodingLength() SBE_NOEXCEPT
    {
        return 4;
    }

    SBE_NODISCARD std::uint32_t refSeqNum() const SBE_NOEXCEPT
    {
        std::uint32_t val;
        std::memcpy(&val, m_buffer + m_offset + 317, sizeof(std::uint32_t));
        return SBE_LITTLE_ENDIAN_ENCODE_32(val);
    }

    BusinessReject521 &refSeqNum(const std::uint32_t value) SBE_NOEXCEPT
    {
        std::uint32_t val = SBE_LITTLE_ENDIAN_ENCODE_32(value);
        std::memcpy(m_buffer + m_offset + 317, &val, sizeof(std::uint32_t));
        return *this;
    }

    SBE_NODISCARD static const char *RefTagIDMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "optional";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t refTagIDId() SBE_NOEXCEPT
    {
        return 371;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t refTagIDSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool refTagIDInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t refTagIDEncodingOffset() SBE_NOEXCEPT
    {
        return 321;
    }

    static SBE_CONSTEXPR std::uint16_t refTagIDNullValue() SBE_NOEXCEPT
    {
        return static_cast<std::uint16_t>(65535);
    }

    static SBE_CONSTEXPR std::uint16_t refTagIDMinValue() SBE_NOEXCEPT
    {
        return static_cast<std::uint16_t>(0);
    }

    static SBE_CONSTEXPR std::uint16_t refTagIDMaxValue() SBE_NOEXCEPT
    {
        return static_cast<std::uint16_t>(65534);
    }

    static SBE_CONSTEXPR std::size_t refTagIDEncodingLength() SBE_NOEXCEPT
    {
        return 2;
    }

    SBE_NODISCARD std::uint16_t refTagID() const SBE_NOEXCEPT
    {
        std::uint16_t val;
        std::memcpy(&val, m_buffer + m_offset + 321, sizeof(std::uint16_t));
        return SBE_LITTLE_ENDIAN_ENCODE_16(val);
    }

    BusinessReject521 &refTagID(const std::uint16_t value) SBE_NOEXCEPT
    {
        std::uint16_t val = SBE_LITTLE_ENDIAN_ENCODE_16(value);
        std::memcpy(m_buffer + m_offset + 321, &val, sizeof(std::uint16_t));
        return *this;
    }

    SBE_NODISCARD static const char *BusinessRejectReasonMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t businessRejectReasonId() SBE_NOEXCEPT
    {
        return 380;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t businessRejectReasonSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool businessRejectReasonInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t businessRejectReasonEncodingOffset() SBE_NOEXCEPT
    {
        return 323;
    }

    static SBE_CONSTEXPR std::uint16_t businessRejectReasonNullValue() SBE_NOEXCEPT
    {
        return SBE_NULLVALUE_UINT16;
    }

    static SBE_CONSTEXPR std::uint16_t businessRejectReasonMinValue() SBE_NOEXCEPT
    {
        return static_cast<std::uint16_t>(0);
    }

    static SBE_CONSTEXPR std::uint16_t businessRejectReasonMaxValue() SBE_NOEXCEPT
    {
        return static_cast<std::uint16_t>(65534);
    }

    static SBE_CONSTEXPR std::size_t businessRejectReasonEncodingLength() SBE_NOEXCEPT
    {
        return 2;
    }

    SBE_NODISCARD std::uint16_t businessRejectReason() const SBE_NOEXCEPT
    {
        std::uint16_t val;
        std::memcpy(&val, m_buffer + m_offset + 323, sizeof(std::uint16_t));
        return SBE_LITTLE_ENDIAN_ENCODE_16(val);
    }

    BusinessReject521 &businessRejectReason(const std::uint16_t value) SBE_NOEXCEPT
    {
        std::uint16_t val = SBE_LITTLE_ENDIAN_ENCODE_16(value);
        std::memcpy(m_buffer + m_offset + 323, &val, sizeof(std::uint16_t));
        return *this;
    }

    SBE_NODISCARD static const char *RefMsgTypeMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "String";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t refMsgTypeId() SBE_NOEXCEPT
    {
        return 372;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t refMsgTypeSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool refMsgTypeInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t refMsgTypeEncodingOffset() SBE_NOEXCEPT
    {
        return 325;
    }

    static SBE_CONSTEXPR char refMsgTypeNullValue() SBE_NOEXCEPT
    {
        return static_cast<char>(0);
    }

    static SBE_CONSTEXPR char refMsgTypeMinValue() SBE_NOEXCEPT
    {
        return static_cast<char>(32);
    }

    static SBE_CONSTEXPR char refMsgTypeMaxValue() SBE_NOEXCEPT
    {
        return static_cast<char>(126);
    }

    static SBE_CONSTEXPR std::size_t refMsgTypeEncodingLength() SBE_NOEXCEPT
    {
        return 2;
    }

    static SBE_CONSTEXPR std::uint64_t refMsgTypeLength() SBE_NOEXCEPT
    {
        return 2;
    }

    SBE_NODISCARD const char *refMsgType() const SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 325;
    }

    SBE_NODISCARD char *refMsgType() SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 325;
    }

    SBE_NODISCARD char refMsgType(const std::uint64_t index) const
    {
        if (index >= 2)
        {
            throw std::runtime_error("index out of range for refMsgType [E104]");
        }

        char val;
        std::memcpy(&val, m_buffer + m_offset + 325 + (index * 1), sizeof(char));
        return (val);
    }

    BusinessReject521 &refMsgType(const std::uint64_t index, const char value)
    {
        if (index >= 2)
        {
            throw std::runtime_error("index out of range for refMsgType [E105]");
        }

        char val = (value);
        std::memcpy(m_buffer + m_offset + 325 + (index * 1), &val, sizeof(char));
        return *this;
    }

    std::uint64_t getRefMsgType(char *const dst, const std::uint64_t length) const
    {
        if (length > 2)
        {
            throw std::runtime_error("length too large for getRefMsgType [E106]");
        }

        std::memcpy(dst, m_buffer + m_offset + 325, sizeof(char) * static_cast<std::size_t>(length));
        return length;
    }

    BusinessReject521 &putRefMsgType(const char *const src) SBE_NOEXCEPT
    {
        std::memcpy(m_buffer + m_offset + 325, src, sizeof(char) * 2);
        return *this;
    }

    BusinessReject521 &putRefMsgType(
        const char value0,
        const char value1) SBE_NOEXCEPT
    {
        char val0 = (value0);
        std::memcpy(m_buffer + m_offset + 325, &val0, sizeof(char));
        char val1 = (value1);
        std::memcpy(m_buffer + m_offset + 326, &val1, sizeof(char));

        return *this;
    }

    SBE_NODISCARD std::string getRefMsgTypeAsString() const
    {
        const char *buffer = m_buffer + m_offset + 325;
        std::size_t length = 0;

        for (; length < 2 && *(buffer + length) != '\0'; ++length);
        std::string result(buffer, length);

        return result;
    }

    std::string getRefMsgTypeAsJsonEscapedString()
    {
        std::ostringstream oss;
        std::string s = getRefMsgTypeAsString();

        for (const auto c : s)
        {
            switch (c)
            {
                case '"': oss << "\\\""; break;
                case '\\': oss << "\\\\"; break;
                case '\b': oss << "\\b"; break;
                case '\f': oss << "\\f"; break;
                case '\n': oss << "\\n"; break;
                case '\r': oss << "\\r"; break;
                case '\t': oss << "\\t"; break;

                default:
                    if ('\x00' <= c && c <= '\x1f')
                    {
                        oss << "\\u" << std::hex << std::setw(4)
                            << std::setfill('0') << (int)(c);
                    }
                    else
                    {
                        oss << c;
                    }
            }
        }

        return oss.str();
    }

    #if __cplusplus >= 201703L
    SBE_NODISCARD std::string_view getRefMsgTypeAsStringView() const SBE_NOEXCEPT
    {
        const char *buffer = m_buffer + m_offset + 325;
        std::size_t length = 0;

        for (; length < 2 && *(buffer + length) != '\0'; ++length);
        std::string_view result(buffer, length);

        return result;
    }
    #endif

    #if __cplusplus >= 201703L
    BusinessReject521 &putRefMsgType(const std::string_view str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 2)
        {
            throw std::runtime_error("string too large for putRefMsgType [E106]");
        }

        std::memcpy(m_buffer + m_offset + 325, str.data(), srcLength);
        for (std::size_t start = srcLength; start < 2; ++start)
        {
            m_buffer[m_offset + 325 + start] = 0;
        }

        return *this;
    }
    #else
    BusinessReject521 &putRefMsgType(const std::string &str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 2)
        {
            throw std::runtime_error("string too large for putRefMsgType [E106]");
        }

        std::memcpy(m_buffer + m_offset + 325, str.c_str(), srcLength);
        for (std::size_t start = srcLength; start < 2; ++start)
        {
            m_buffer[m_offset + 325 + start] = 0;
        }

        return *this;
    }
    #endif

    SBE_NODISCARD static const char *PossRetransFlagMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t possRetransFlagId() SBE_NOEXCEPT
    {
        return 9765;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t possRetransFlagSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool possRetransFlagInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t possRetransFlagEncodingOffset() SBE_NOEXCEPT
    {
        return 327;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t possRetransFlagEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD std::uint8_t possRetransFlagRaw() const SBE_NOEXCEPT
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 327, sizeof(std::uint8_t));
        return (val);
    }

    SBE_NODISCARD BooleanFlag::Value possRetransFlag() const
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 327, sizeof(std::uint8_t));
        return BooleanFlag::get((val));
    }

    BusinessReject521 &possRetransFlag(const BooleanFlag::Value value) SBE_NOEXCEPT
    {
        std::uint8_t val = (value);
        std::memcpy(m_buffer + m_offset + 327, &val, sizeof(std::uint8_t));
        return *this;
    }

    SBE_NODISCARD static const char *ManualOrderIndicatorMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t manualOrderIndicatorId() SBE_NOEXCEPT
    {
        return 1028;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t manualOrderIndicatorSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool manualOrderIndicatorInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t manualOrderIndicatorEncodingOffset() SBE_NOEXCEPT
    {
        return 328;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t manualOrderIndicatorEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD std::uint8_t manualOrderIndicatorRaw() const SBE_NOEXCEPT
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 328, sizeof(std::uint8_t));
        return (val);
    }

    SBE_NODISCARD ManualOrdInd::Value manualOrderIndicator() const
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 328, sizeof(std::uint8_t));
        return ManualOrdInd::get((val));
    }

    BusinessReject521 &manualOrderIndicator(const ManualOrdInd::Value value) SBE_NOEXCEPT
    {
        std::uint8_t val = (value);
        std::memcpy(m_buffer + m_offset + 328, &val, sizeof(std::uint8_t));
        return *this;
    }

    SBE_NODISCARD static const char *SplitMsgMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t splitMsgId() SBE_NOEXCEPT
    {
        return 9553;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t splitMsgSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool splitMsgInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t splitMsgEncodingOffset() SBE_NOEXCEPT
    {
        return 329;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t splitMsgEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD std::uint8_t splitMsgRaw() const SBE_NOEXCEPT
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 329, sizeof(std::uint8_t));
        return (val);
    }

    SBE_NODISCARD SplitMsg::Value splitMsg() const
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 329, sizeof(std::uint8_t));
        return SplitMsg::get((val));
    }

    BusinessReject521 &splitMsg(const SplitMsg::Value value) SBE_NOEXCEPT
    {
        std::uint8_t val = (value);
        std::memcpy(m_buffer + m_offset + 329, &val, sizeof(std::uint8_t));
        return *this;
    }

template<typename CharT, typename Traits>
friend std::basic_ostream<CharT, Traits> & operator << (
    std::basic_ostream<CharT, Traits> &builder, const BusinessReject521 &_writer)
{
    BusinessReject521 writer(
        _writer.m_buffer,
        _writer.m_offset,
        _writer.m_bufferLength,
        _writer.m_actingBlockLength,
        _writer.m_actingVersion);

    builder << '{';
    builder << R"("Name": "BusinessReject521", )";
    builder << R"("sbeTemplateId": )";
    builder << writer.sbeTemplateId();
    builder << ", ";

    builder << R"("SeqNum": )";
    builder << +writer.seqNum();

    builder << ", ";
    builder << R"("UUID": )";
    builder << +writer.uUID();

    builder << ", ";
    builder << R"("Text": )";
    builder << '"' <<
        writer.getTextAsJsonEscapedString().c_str() << '"';

    builder << ", ";
    builder << R"("SenderID": )";
    builder << '"' <<
        writer.getSenderIDAsJsonEscapedString().c_str() << '"';

    builder << ", ";
    builder << R"("PartyDetailsListReqID": )";
    builder << +writer.partyDetailsListReqID();

    builder << ", ";
    builder << R"("SendingTimeEpoch": )";
    builder << +writer.sendingTimeEpoch();

    builder << ", ";
    builder << R"("BusinessRejectRefID": )";
    builder << +writer.businessRejectRefID();

    builder << ", ";
    builder << R"("Location": )";
    builder << '"' <<
        writer.getLocationAsJsonEscapedString().c_str() << '"';

    builder << ", ";
    builder << R"("RefSeqNum": )";
    builder << +writer.refSeqNum();

    builder << ", ";
    builder << R"("RefTagID": )";
    builder << +writer.refTagID();

    builder << ", ";
    builder << R"("BusinessRejectReason": )";
    builder << +writer.businessRejectReason();

    builder << ", ";
    builder << R"("RefMsgType": )";
    builder << '"' <<
        writer.getRefMsgTypeAsJsonEscapedString().c_str() << '"';

    builder << ", ";
    builder << R"("PossRetransFlag": )";
    builder << '"' << writer.possRetransFlag() << '"';

    builder << ", ";
    builder << R"("ManualOrderIndicator": )";
    builder << '"' << writer.manualOrderIndicator() << '"';

    builder << ", ";
    builder << R"("SplitMsg": )";
    builder << '"' << writer.splitMsg() << '"';

    builder << '}';

    return builder;
}

void skip()
{
}

SBE_NODISCARD static SBE_CONSTEXPR bool isConstLength() SBE_NOEXCEPT
{
    return true;
}

SBE_NODISCARD static std::size_t computeLength()
{
#if defined(__GNUG__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wtype-limits"
#endif
    std::size_t length = sbeBlockLength();

    return length;
#if defined(__GNUG__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
}
};
}
#endif
