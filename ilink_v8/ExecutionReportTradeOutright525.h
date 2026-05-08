/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/* Generated SBE (Simple Binary Encoding) message codec */
#ifndef _SBE_EXECUTIONREPORTTRADEOUTRIGHT525_CXX_H_
#define _SBE_EXECUTIONREPORTTRADEOUTRIGHT525_CXX_H_

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

class ExecutionReportTradeOutright525
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
    static const std::uint16_t SBE_BLOCK_LENGTH = static_cast<std::uint16_t>(293);
    static const std::uint16_t SBE_TEMPLATE_ID = static_cast<std::uint16_t>(525);
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

    ExecutionReportTradeOutright525() = default;

    ExecutionReportTradeOutright525(
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

    ExecutionReportTradeOutright525(char *buffer, const std::uint64_t bufferLength) :
        ExecutionReportTradeOutright525(buffer, 0, bufferLength, sbeBlockLength(), sbeSchemaVersion())
    {
    }

    ExecutionReportTradeOutright525(
        char *buffer,
        const std::uint64_t bufferLength,
        const std::uint64_t actingBlockLength,
        const std::uint64_t actingVersion) :
        ExecutionReportTradeOutright525(buffer, 0, bufferLength, actingBlockLength, actingVersion)
    {
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint16_t sbeBlockLength() SBE_NOEXCEPT
    {
        return static_cast<std::uint16_t>(293);
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t sbeBlockAndHeaderLength() SBE_NOEXCEPT
    {
        return messageHeader::encodedLength() + sbeBlockLength();
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint16_t sbeTemplateId() SBE_NOEXCEPT
    {
        return static_cast<std::uint16_t>(525);
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
        return "8";
    }

    SBE_NODISCARD std::uint64_t offset() const SBE_NOEXCEPT
    {
        return m_offset;
    }

    ExecutionReportTradeOutright525 &wrapForEncode(char *buffer, const std::uint64_t offset, const std::uint64_t bufferLength)
    {
        return *this = ExecutionReportTradeOutright525(buffer, offset, bufferLength, sbeBlockLength(), sbeSchemaVersion());
    }

    ExecutionReportTradeOutright525 &wrapAndApplyHeader(char *buffer, const std::uint64_t offset, const std::uint64_t bufferLength)
    {
        messageHeader hdr(buffer, offset, bufferLength, sbeSchemaVersion());

        hdr
            .blockLength(sbeBlockLength())
            .templateId(sbeTemplateId())
            .schemaId(sbeSchemaId())
            .version(sbeSchemaVersion());

        return *this = ExecutionReportTradeOutright525(
            buffer,
            offset + messageHeader::encodedLength(),
            bufferLength,
            sbeBlockLength(),
            sbeSchemaVersion());
    }

    ExecutionReportTradeOutright525 &wrapForDecode(
        char *buffer,
        const std::uint64_t offset,
        const std::uint64_t actingBlockLength,
        const std::uint64_t actingVersion,
        const std::uint64_t bufferLength)
    {
        return *this = ExecutionReportTradeOutright525(buffer, offset, bufferLength, actingBlockLength, actingVersion);
    }

    ExecutionReportTradeOutright525 &sbeRewind()
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
        ExecutionReportTradeOutright525 skipper(m_buffer, m_offset, m_bufferLength, sbeBlockLength(), m_actingVersion);
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

    ExecutionReportTradeOutright525 &seqNum(const std::uint32_t value) SBE_NOEXCEPT
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

    ExecutionReportTradeOutright525 &uUID(const std::uint64_t value) SBE_NOEXCEPT
    {
        std::uint64_t val = SBE_LITTLE_ENDIAN_ENCODE_64(value);
        std::memcpy(m_buffer + m_offset + 4, &val, sizeof(std::uint64_t));
        return *this;
    }

    SBE_NODISCARD static const char *ExecIDMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "String";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t execIDId() SBE_NOEXCEPT
    {
        return 17;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t execIDSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool execIDInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t execIDEncodingOffset() SBE_NOEXCEPT
    {
        return 12;
    }

    static SBE_CONSTEXPR char execIDNullValue() SBE_NOEXCEPT
    {
        return static_cast<char>(0);
    }

    static SBE_CONSTEXPR char execIDMinValue() SBE_NOEXCEPT
    {
        return static_cast<char>(32);
    }

    static SBE_CONSTEXPR char execIDMaxValue() SBE_NOEXCEPT
    {
        return static_cast<char>(126);
    }

    static SBE_CONSTEXPR std::size_t execIDEncodingLength() SBE_NOEXCEPT
    {
        return 40;
    }

    static SBE_CONSTEXPR std::uint64_t execIDLength() SBE_NOEXCEPT
    {
        return 40;
    }

    SBE_NODISCARD const char *execID() const SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 12;
    }

    SBE_NODISCARD char *execID() SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 12;
    }

    SBE_NODISCARD char execID(const std::uint64_t index) const
    {
        if (index >= 40)
        {
            throw std::runtime_error("index out of range for execID [E104]");
        }

        char val;
        std::memcpy(&val, m_buffer + m_offset + 12 + (index * 1), sizeof(char));
        return (val);
    }

    ExecutionReportTradeOutright525 &execID(const std::uint64_t index, const char value)
    {
        if (index >= 40)
        {
            throw std::runtime_error("index out of range for execID [E105]");
        }

        char val = (value);
        std::memcpy(m_buffer + m_offset + 12 + (index * 1), &val, sizeof(char));
        return *this;
    }

    std::uint64_t getExecID(char *const dst, const std::uint64_t length) const
    {
        if (length > 40)
        {
            throw std::runtime_error("length too large for getExecID [E106]");
        }

        std::memcpy(dst, m_buffer + m_offset + 12, sizeof(char) * static_cast<std::size_t>(length));
        return length;
    }

    ExecutionReportTradeOutright525 &putExecID(const char *const src) SBE_NOEXCEPT
    {
        std::memcpy(m_buffer + m_offset + 12, src, sizeof(char) * 40);
        return *this;
    }

    SBE_NODISCARD std::string getExecIDAsString() const
    {
        const char *buffer = m_buffer + m_offset + 12;
        std::size_t length = 0;

        for (; length < 40 && *(buffer + length) != '\0'; ++length);
        std::string result(buffer, length);

        return result;
    }

    std::string getExecIDAsJsonEscapedString()
    {
        std::ostringstream oss;
        std::string s = getExecIDAsString();

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
    SBE_NODISCARD std::string_view getExecIDAsStringView() const SBE_NOEXCEPT
    {
        const char *buffer = m_buffer + m_offset + 12;
        std::size_t length = 0;

        for (; length < 40 && *(buffer + length) != '\0'; ++length);
        std::string_view result(buffer, length);

        return result;
    }
    #endif

    #if __cplusplus >= 201703L
    ExecutionReportTradeOutright525 &putExecID(const std::string_view str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 40)
        {
            throw std::runtime_error("string too large for putExecID [E106]");
        }

        std::memcpy(m_buffer + m_offset + 12, str.data(), srcLength);
        for (std::size_t start = srcLength; start < 40; ++start)
        {
            m_buffer[m_offset + 12 + start] = 0;
        }

        return *this;
    }
    #else
    ExecutionReportTradeOutright525 &putExecID(const std::string &str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 40)
        {
            throw std::runtime_error("string too large for putExecID [E106]");
        }

        std::memcpy(m_buffer + m_offset + 12, str.c_str(), srcLength);
        for (std::size_t start = srcLength; start < 40; ++start)
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
            case MetaAttribute::PRESENCE: return "required";
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
        return 52;
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
        return m_buffer + m_offset + 52;
    }

    SBE_NODISCARD char *senderID() SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 52;
    }

    SBE_NODISCARD char senderID(const std::uint64_t index) const
    {
        if (index >= 20)
        {
            throw std::runtime_error("index out of range for senderID [E104]");
        }

        char val;
        std::memcpy(&val, m_buffer + m_offset + 52 + (index * 1), sizeof(char));
        return (val);
    }

    ExecutionReportTradeOutright525 &senderID(const std::uint64_t index, const char value)
    {
        if (index >= 20)
        {
            throw std::runtime_error("index out of range for senderID [E105]");
        }

        char val = (value);
        std::memcpy(m_buffer + m_offset + 52 + (index * 1), &val, sizeof(char));
        return *this;
    }

    std::uint64_t getSenderID(char *const dst, const std::uint64_t length) const
    {
        if (length > 20)
        {
            throw std::runtime_error("length too large for getSenderID [E106]");
        }

        std::memcpy(dst, m_buffer + m_offset + 52, sizeof(char) * static_cast<std::size_t>(length));
        return length;
    }

    ExecutionReportTradeOutright525 &putSenderID(const char *const src) SBE_NOEXCEPT
    {
        std::memcpy(m_buffer + m_offset + 52, src, sizeof(char) * 20);
        return *this;
    }

    SBE_NODISCARD std::string getSenderIDAsString() const
    {
        const char *buffer = m_buffer + m_offset + 52;
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
        const char *buffer = m_buffer + m_offset + 52;
        std::size_t length = 0;

        for (; length < 20 && *(buffer + length) != '\0'; ++length);
        std::string_view result(buffer, length);

        return result;
    }
    #endif

    #if __cplusplus >= 201703L
    ExecutionReportTradeOutright525 &putSenderID(const std::string_view str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 20)
        {
            throw std::runtime_error("string too large for putSenderID [E106]");
        }

        std::memcpy(m_buffer + m_offset + 52, str.data(), srcLength);
        for (std::size_t start = srcLength; start < 20; ++start)
        {
            m_buffer[m_offset + 52 + start] = 0;
        }

        return *this;
    }
    #else
    ExecutionReportTradeOutright525 &putSenderID(const std::string &str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 20)
        {
            throw std::runtime_error("string too large for putSenderID [E106]");
        }

        std::memcpy(m_buffer + m_offset + 52, str.c_str(), srcLength);
        for (std::size_t start = srcLength; start < 20; ++start)
        {
            m_buffer[m_offset + 52 + start] = 0;
        }

        return *this;
    }
    #endif

    SBE_NODISCARD static const char *ClOrdIDMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "String";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t clOrdIDId() SBE_NOEXCEPT
    {
        return 11;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t clOrdIDSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool clOrdIDInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t clOrdIDEncodingOffset() SBE_NOEXCEPT
    {
        return 72;
    }

    static SBE_CONSTEXPR char clOrdIDNullValue() SBE_NOEXCEPT
    {
        return static_cast<char>(0);
    }

    static SBE_CONSTEXPR char clOrdIDMinValue() SBE_NOEXCEPT
    {
        return static_cast<char>(32);
    }

    static SBE_CONSTEXPR char clOrdIDMaxValue() SBE_NOEXCEPT
    {
        return static_cast<char>(126);
    }

    static SBE_CONSTEXPR std::size_t clOrdIDEncodingLength() SBE_NOEXCEPT
    {
        return 20;
    }

    static SBE_CONSTEXPR std::uint64_t clOrdIDLength() SBE_NOEXCEPT
    {
        return 20;
    }

    SBE_NODISCARD const char *clOrdID() const SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 72;
    }

    SBE_NODISCARD char *clOrdID() SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 72;
    }

    SBE_NODISCARD char clOrdID(const std::uint64_t index) const
    {
        if (index >= 20)
        {
            throw std::runtime_error("index out of range for clOrdID [E104]");
        }

        char val;
        std::memcpy(&val, m_buffer + m_offset + 72 + (index * 1), sizeof(char));
        return (val);
    }

    ExecutionReportTradeOutright525 &clOrdID(const std::uint64_t index, const char value)
    {
        if (index >= 20)
        {
            throw std::runtime_error("index out of range for clOrdID [E105]");
        }

        char val = (value);
        std::memcpy(m_buffer + m_offset + 72 + (index * 1), &val, sizeof(char));
        return *this;
    }

    std::uint64_t getClOrdID(char *const dst, const std::uint64_t length) const
    {
        if (length > 20)
        {
            throw std::runtime_error("length too large for getClOrdID [E106]");
        }

        std::memcpy(dst, m_buffer + m_offset + 72, sizeof(char) * static_cast<std::size_t>(length));
        return length;
    }

    ExecutionReportTradeOutright525 &putClOrdID(const char *const src) SBE_NOEXCEPT
    {
        std::memcpy(m_buffer + m_offset + 72, src, sizeof(char) * 20);
        return *this;
    }

    SBE_NODISCARD std::string getClOrdIDAsString() const
    {
        const char *buffer = m_buffer + m_offset + 72;
        std::size_t length = 0;

        for (; length < 20 && *(buffer + length) != '\0'; ++length);
        std::string result(buffer, length);

        return result;
    }

    std::string getClOrdIDAsJsonEscapedString()
    {
        std::ostringstream oss;
        std::string s = getClOrdIDAsString();

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
    SBE_NODISCARD std::string_view getClOrdIDAsStringView() const SBE_NOEXCEPT
    {
        const char *buffer = m_buffer + m_offset + 72;
        std::size_t length = 0;

        for (; length < 20 && *(buffer + length) != '\0'; ++length);
        std::string_view result(buffer, length);

        return result;
    }
    #endif

    #if __cplusplus >= 201703L
    ExecutionReportTradeOutright525 &putClOrdID(const std::string_view str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 20)
        {
            throw std::runtime_error("string too large for putClOrdID [E106]");
        }

        std::memcpy(m_buffer + m_offset + 72, str.data(), srcLength);
        for (std::size_t start = srcLength; start < 20; ++start)
        {
            m_buffer[m_offset + 72 + start] = 0;
        }

        return *this;
    }
    #else
    ExecutionReportTradeOutright525 &putClOrdID(const std::string &str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 20)
        {
            throw std::runtime_error("string too large for putClOrdID [E106]");
        }

        std::memcpy(m_buffer + m_offset + 72, str.c_str(), srcLength);
        for (std::size_t start = srcLength; start < 20; ++start)
        {
            m_buffer[m_offset + 72 + start] = 0;
        }

        return *this;
    }
    #endif

    SBE_NODISCARD static const char *PartyDetailsListReqIDMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
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
        return 92;
    }

    static SBE_CONSTEXPR std::uint64_t partyDetailsListReqIDNullValue() SBE_NOEXCEPT
    {
        return SBE_NULLVALUE_UINT64;
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
        std::memcpy(&val, m_buffer + m_offset + 92, sizeof(std::uint64_t));
        return SBE_LITTLE_ENDIAN_ENCODE_64(val);
    }

    ExecutionReportTradeOutright525 &partyDetailsListReqID(const std::uint64_t value) SBE_NOEXCEPT
    {
        std::uint64_t val = SBE_LITTLE_ENDIAN_ENCODE_64(value);
        std::memcpy(m_buffer + m_offset + 92, &val, sizeof(std::uint64_t));
        return *this;
    }

    SBE_NODISCARD static const char *LastPxMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "Price";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t lastPxId() SBE_NOEXCEPT
    {
        return 31;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t lastPxSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool lastPxInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t lastPxEncodingOffset() SBE_NOEXCEPT
    {
        return 100;
    }

private:
    PRICE9 m_lastPx;

public:
    SBE_NODISCARD PRICE9 &lastPx()
    {
        m_lastPx.wrap(m_buffer, m_offset + 100, m_actingVersion, m_bufferLength);
        return m_lastPx;
    }

    SBE_NODISCARD static const char *OrderIDMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t orderIDId() SBE_NOEXCEPT
    {
        return 37;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t orderIDSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool orderIDInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t orderIDEncodingOffset() SBE_NOEXCEPT
    {
        return 108;
    }

    static SBE_CONSTEXPR std::uint64_t orderIDNullValue() SBE_NOEXCEPT
    {
        return SBE_NULLVALUE_UINT64;
    }

    static SBE_CONSTEXPR std::uint64_t orderIDMinValue() SBE_NOEXCEPT
    {
        return UINT64_C(0x0);
    }

    static SBE_CONSTEXPR std::uint64_t orderIDMaxValue() SBE_NOEXCEPT
    {
        return UINT64_C(0xfffffffffffffffe);
    }

    static SBE_CONSTEXPR std::size_t orderIDEncodingLength() SBE_NOEXCEPT
    {
        return 8;
    }

    SBE_NODISCARD std::uint64_t orderID() const SBE_NOEXCEPT
    {
        std::uint64_t val;
        std::memcpy(&val, m_buffer + m_offset + 108, sizeof(std::uint64_t));
        return SBE_LITTLE_ENDIAN_ENCODE_64(val);
    }

    ExecutionReportTradeOutright525 &orderID(const std::uint64_t value) SBE_NOEXCEPT
    {
        std::uint64_t val = SBE_LITTLE_ENDIAN_ENCODE_64(value);
        std::memcpy(m_buffer + m_offset + 108, &val, sizeof(std::uint64_t));
        return *this;
    }

    SBE_NODISCARD static const char *PriceMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "Price";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t priceId() SBE_NOEXCEPT
    {
        return 44;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t priceSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool priceInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t priceEncodingOffset() SBE_NOEXCEPT
    {
        return 116;
    }

private:
    PRICE9 m_price;

public:
    SBE_NODISCARD PRICE9 &price()
    {
        m_price.wrap(m_buffer, m_offset + 116, m_actingVersion, m_bufferLength);
        return m_price;
    }

    SBE_NODISCARD static const char *StopPxMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "Price";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t stopPxId() SBE_NOEXCEPT
    {
        return 99;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t stopPxSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool stopPxInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t stopPxEncodingOffset() SBE_NOEXCEPT
    {
        return 124;
    }

private:
    PRICENULL9 m_stopPx;

public:
    SBE_NODISCARD PRICENULL9 &stopPx()
    {
        m_stopPx.wrap(m_buffer, m_offset + 124, m_actingVersion, m_bufferLength);
        return m_stopPx;
    }

    SBE_NODISCARD static const char *TransactTimeMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t transactTimeId() SBE_NOEXCEPT
    {
        return 60;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t transactTimeSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool transactTimeInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t transactTimeEncodingOffset() SBE_NOEXCEPT
    {
        return 132;
    }

    static SBE_CONSTEXPR std::uint64_t transactTimeNullValue() SBE_NOEXCEPT
    {
        return SBE_NULLVALUE_UINT64;
    }

    static SBE_CONSTEXPR std::uint64_t transactTimeMinValue() SBE_NOEXCEPT
    {
        return UINT64_C(0x0);
    }

    static SBE_CONSTEXPR std::uint64_t transactTimeMaxValue() SBE_NOEXCEPT
    {
        return UINT64_C(0xfffffffffffffffe);
    }

    static SBE_CONSTEXPR std::size_t transactTimeEncodingLength() SBE_NOEXCEPT
    {
        return 8;
    }

    SBE_NODISCARD std::uint64_t transactTime() const SBE_NOEXCEPT
    {
        std::uint64_t val;
        std::memcpy(&val, m_buffer + m_offset + 132, sizeof(std::uint64_t));
        return SBE_LITTLE_ENDIAN_ENCODE_64(val);
    }

    ExecutionReportTradeOutright525 &transactTime(const std::uint64_t value) SBE_NOEXCEPT
    {
        std::uint64_t val = SBE_LITTLE_ENDIAN_ENCODE_64(value);
        std::memcpy(m_buffer + m_offset + 132, &val, sizeof(std::uint64_t));
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
        return 140;
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
        std::memcpy(&val, m_buffer + m_offset + 140, sizeof(std::uint64_t));
        return SBE_LITTLE_ENDIAN_ENCODE_64(val);
    }

    ExecutionReportTradeOutright525 &sendingTimeEpoch(const std::uint64_t value) SBE_NOEXCEPT
    {
        std::uint64_t val = SBE_LITTLE_ENDIAN_ENCODE_64(value);
        std::memcpy(m_buffer + m_offset + 140, &val, sizeof(std::uint64_t));
        return *this;
    }

    SBE_NODISCARD static const char *OrderRequestIDMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t orderRequestIDId() SBE_NOEXCEPT
    {
        return 2422;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t orderRequestIDSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool orderRequestIDInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t orderRequestIDEncodingOffset() SBE_NOEXCEPT
    {
        return 148;
    }

    static SBE_CONSTEXPR std::uint64_t orderRequestIDNullValue() SBE_NOEXCEPT
    {
        return SBE_NULLVALUE_UINT64;
    }

    static SBE_CONSTEXPR std::uint64_t orderRequestIDMinValue() SBE_NOEXCEPT
    {
        return UINT64_C(0x0);
    }

    static SBE_CONSTEXPR std::uint64_t orderRequestIDMaxValue() SBE_NOEXCEPT
    {
        return UINT64_C(0xfffffffffffffffe);
    }

    static SBE_CONSTEXPR std::size_t orderRequestIDEncodingLength() SBE_NOEXCEPT
    {
        return 8;
    }

    SBE_NODISCARD std::uint64_t orderRequestID() const SBE_NOEXCEPT
    {
        std::uint64_t val;
        std::memcpy(&val, m_buffer + m_offset + 148, sizeof(std::uint64_t));
        return SBE_LITTLE_ENDIAN_ENCODE_64(val);
    }

    ExecutionReportTradeOutright525 &orderRequestID(const std::uint64_t value) SBE_NOEXCEPT
    {
        std::uint64_t val = SBE_LITTLE_ENDIAN_ENCODE_64(value);
        std::memcpy(m_buffer + m_offset + 148, &val, sizeof(std::uint64_t));
        return *this;
    }

    SBE_NODISCARD static const char *SecExecIDMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t secExecIDId() SBE_NOEXCEPT
    {
        return 527;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t secExecIDSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool secExecIDInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t secExecIDEncodingOffset() SBE_NOEXCEPT
    {
        return 156;
    }

    static SBE_CONSTEXPR std::uint64_t secExecIDNullValue() SBE_NOEXCEPT
    {
        return SBE_NULLVALUE_UINT64;
    }

    static SBE_CONSTEXPR std::uint64_t secExecIDMinValue() SBE_NOEXCEPT
    {
        return UINT64_C(0x0);
    }

    static SBE_CONSTEXPR std::uint64_t secExecIDMaxValue() SBE_NOEXCEPT
    {
        return UINT64_C(0xfffffffffffffffe);
    }

    static SBE_CONSTEXPR std::size_t secExecIDEncodingLength() SBE_NOEXCEPT
    {
        return 8;
    }

    SBE_NODISCARD std::uint64_t secExecID() const SBE_NOEXCEPT
    {
        std::uint64_t val;
        std::memcpy(&val, m_buffer + m_offset + 156, sizeof(std::uint64_t));
        return SBE_LITTLE_ENDIAN_ENCODE_64(val);
    }

    ExecutionReportTradeOutright525 &secExecID(const std::uint64_t value) SBE_NOEXCEPT
    {
        std::uint64_t val = SBE_LITTLE_ENDIAN_ENCODE_64(value);
        std::memcpy(m_buffer + m_offset + 156, &val, sizeof(std::uint64_t));
        return *this;
    }

    SBE_NODISCARD static const char *CrossIDMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "optional";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t crossIDId() SBE_NOEXCEPT
    {
        return 548;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t crossIDSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool crossIDInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t crossIDEncodingOffset() SBE_NOEXCEPT
    {
        return 164;
    }

    static SBE_CONSTEXPR std::uint64_t crossIDNullValue() SBE_NOEXCEPT
    {
        return UINT64_C(0xffffffffffffffff);
    }

    static SBE_CONSTEXPR std::uint64_t crossIDMinValue() SBE_NOEXCEPT
    {
        return UINT64_C(0x0);
    }

    static SBE_CONSTEXPR std::uint64_t crossIDMaxValue() SBE_NOEXCEPT
    {
        return UINT64_C(0xfffffffffffffffe);
    }

    static SBE_CONSTEXPR std::size_t crossIDEncodingLength() SBE_NOEXCEPT
    {
        return 8;
    }

    SBE_NODISCARD std::uint64_t crossID() const SBE_NOEXCEPT
    {
        std::uint64_t val;
        std::memcpy(&val, m_buffer + m_offset + 164, sizeof(std::uint64_t));
        return SBE_LITTLE_ENDIAN_ENCODE_64(val);
    }

    ExecutionReportTradeOutright525 &crossID(const std::uint64_t value) SBE_NOEXCEPT
    {
        std::uint64_t val = SBE_LITTLE_ENDIAN_ENCODE_64(value);
        std::memcpy(m_buffer + m_offset + 164, &val, sizeof(std::uint64_t));
        return *this;
    }

    SBE_NODISCARD static const char *HostCrossIDMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "optional";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t hostCrossIDId() SBE_NOEXCEPT
    {
        return 961;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t hostCrossIDSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool hostCrossIDInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t hostCrossIDEncodingOffset() SBE_NOEXCEPT
    {
        return 172;
    }

    static SBE_CONSTEXPR std::uint64_t hostCrossIDNullValue() SBE_NOEXCEPT
    {
        return UINT64_C(0xffffffffffffffff);
    }

    static SBE_CONSTEXPR std::uint64_t hostCrossIDMinValue() SBE_NOEXCEPT
    {
        return UINT64_C(0x0);
    }

    static SBE_CONSTEXPR std::uint64_t hostCrossIDMaxValue() SBE_NOEXCEPT
    {
        return UINT64_C(0xfffffffffffffffe);
    }

    static SBE_CONSTEXPR std::size_t hostCrossIDEncodingLength() SBE_NOEXCEPT
    {
        return 8;
    }

    SBE_NODISCARD std::uint64_t hostCrossID() const SBE_NOEXCEPT
    {
        std::uint64_t val;
        std::memcpy(&val, m_buffer + m_offset + 172, sizeof(std::uint64_t));
        return SBE_LITTLE_ENDIAN_ENCODE_64(val);
    }

    ExecutionReportTradeOutright525 &hostCrossID(const std::uint64_t value) SBE_NOEXCEPT
    {
        std::uint64_t val = SBE_LITTLE_ENDIAN_ENCODE_64(value);
        std::memcpy(m_buffer + m_offset + 172, &val, sizeof(std::uint64_t));
        return *this;
    }

    SBE_NODISCARD static const char *LocationMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "String";
            case MetaAttribute::PRESENCE: return "required";
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
        return 180;
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
        return m_buffer + m_offset + 180;
    }

    SBE_NODISCARD char *location() SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 180;
    }

    SBE_NODISCARD char location(const std::uint64_t index) const
    {
        if (index >= 5)
        {
            throw std::runtime_error("index out of range for location [E104]");
        }

        char val;
        std::memcpy(&val, m_buffer + m_offset + 180 + (index * 1), sizeof(char));
        return (val);
    }

    ExecutionReportTradeOutright525 &location(const std::uint64_t index, const char value)
    {
        if (index >= 5)
        {
            throw std::runtime_error("index out of range for location [E105]");
        }

        char val = (value);
        std::memcpy(m_buffer + m_offset + 180 + (index * 1), &val, sizeof(char));
        return *this;
    }

    std::uint64_t getLocation(char *const dst, const std::uint64_t length) const
    {
        if (length > 5)
        {
            throw std::runtime_error("length too large for getLocation [E106]");
        }

        std::memcpy(dst, m_buffer + m_offset + 180, sizeof(char) * static_cast<std::size_t>(length));
        return length;
    }

    ExecutionReportTradeOutright525 &putLocation(const char *const src) SBE_NOEXCEPT
    {
        std::memcpy(m_buffer + m_offset + 180, src, sizeof(char) * 5);
        return *this;
    }

    SBE_NODISCARD std::string getLocationAsString() const
    {
        const char *buffer = m_buffer + m_offset + 180;
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
        const char *buffer = m_buffer + m_offset + 180;
        std::size_t length = 0;

        for (; length < 5 && *(buffer + length) != '\0'; ++length);
        std::string_view result(buffer, length);

        return result;
    }
    #endif

    #if __cplusplus >= 201703L
    ExecutionReportTradeOutright525 &putLocation(const std::string_view str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 5)
        {
            throw std::runtime_error("string too large for putLocation [E106]");
        }

        std::memcpy(m_buffer + m_offset + 180, str.data(), srcLength);
        for (std::size_t start = srcLength; start < 5; ++start)
        {
            m_buffer[m_offset + 180 + start] = 0;
        }

        return *this;
    }
    #else
    ExecutionReportTradeOutright525 &putLocation(const std::string &str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 5)
        {
            throw std::runtime_error("string too large for putLocation [E106]");
        }

        std::memcpy(m_buffer + m_offset + 180, str.c_str(), srcLength);
        for (std::size_t start = srcLength; start < 5; ++start)
        {
            m_buffer[m_offset + 180 + start] = 0;
        }

        return *this;
    }
    #endif

    SBE_NODISCARD static const char *SecurityIDMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t securityIDId() SBE_NOEXCEPT
    {
        return 48;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t securityIDSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool securityIDInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t securityIDEncodingOffset() SBE_NOEXCEPT
    {
        return 185;
    }

    static SBE_CONSTEXPR std::int32_t securityIDNullValue() SBE_NOEXCEPT
    {
        return SBE_NULLVALUE_INT32;
    }

    static SBE_CONSTEXPR std::int32_t securityIDMinValue() SBE_NOEXCEPT
    {
        return INT32_C(-2147483647);
    }

    static SBE_CONSTEXPR std::int32_t securityIDMaxValue() SBE_NOEXCEPT
    {
        return INT32_C(2147483647);
    }

    static SBE_CONSTEXPR std::size_t securityIDEncodingLength() SBE_NOEXCEPT
    {
        return 4;
    }

    SBE_NODISCARD std::int32_t securityID() const SBE_NOEXCEPT
    {
        std::int32_t val;
        std::memcpy(&val, m_buffer + m_offset + 185, sizeof(std::int32_t));
        return SBE_LITTLE_ENDIAN_ENCODE_32(val);
    }

    ExecutionReportTradeOutright525 &securityID(const std::int32_t value) SBE_NOEXCEPT
    {
        std::int32_t val = SBE_LITTLE_ENDIAN_ENCODE_32(value);
        std::memcpy(m_buffer + m_offset + 185, &val, sizeof(std::int32_t));
        return *this;
    }

    SBE_NODISCARD static const char *OrderQtyMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t orderQtyId() SBE_NOEXCEPT
    {
        return 38;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t orderQtySinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool orderQtyInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t orderQtyEncodingOffset() SBE_NOEXCEPT
    {
        return 189;
    }

    static SBE_CONSTEXPR std::uint32_t orderQtyNullValue() SBE_NOEXCEPT
    {
        return SBE_NULLVALUE_UINT32;
    }

    static SBE_CONSTEXPR std::uint32_t orderQtyMinValue() SBE_NOEXCEPT
    {
        return UINT32_C(0x0);
    }

    static SBE_CONSTEXPR std::uint32_t orderQtyMaxValue() SBE_NOEXCEPT
    {
        return UINT32_C(0xfffffffe);
    }

    static SBE_CONSTEXPR std::size_t orderQtyEncodingLength() SBE_NOEXCEPT
    {
        return 4;
    }

    SBE_NODISCARD std::uint32_t orderQty() const SBE_NOEXCEPT
    {
        std::uint32_t val;
        std::memcpy(&val, m_buffer + m_offset + 189, sizeof(std::uint32_t));
        return SBE_LITTLE_ENDIAN_ENCODE_32(val);
    }

    ExecutionReportTradeOutright525 &orderQty(const std::uint32_t value) SBE_NOEXCEPT
    {
        std::uint32_t val = SBE_LITTLE_ENDIAN_ENCODE_32(value);
        std::memcpy(m_buffer + m_offset + 189, &val, sizeof(std::uint32_t));
        return *this;
    }

    SBE_NODISCARD static const char *LastQtyMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t lastQtyId() SBE_NOEXCEPT
    {
        return 32;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t lastQtySinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool lastQtyInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t lastQtyEncodingOffset() SBE_NOEXCEPT
    {
        return 193;
    }

    static SBE_CONSTEXPR std::uint32_t lastQtyNullValue() SBE_NOEXCEPT
    {
        return SBE_NULLVALUE_UINT32;
    }

    static SBE_CONSTEXPR std::uint32_t lastQtyMinValue() SBE_NOEXCEPT
    {
        return UINT32_C(0x0);
    }

    static SBE_CONSTEXPR std::uint32_t lastQtyMaxValue() SBE_NOEXCEPT
    {
        return UINT32_C(0xfffffffe);
    }

    static SBE_CONSTEXPR std::size_t lastQtyEncodingLength() SBE_NOEXCEPT
    {
        return 4;
    }

    SBE_NODISCARD std::uint32_t lastQty() const SBE_NOEXCEPT
    {
        std::uint32_t val;
        std::memcpy(&val, m_buffer + m_offset + 193, sizeof(std::uint32_t));
        return SBE_LITTLE_ENDIAN_ENCODE_32(val);
    }

    ExecutionReportTradeOutright525 &lastQty(const std::uint32_t value) SBE_NOEXCEPT
    {
        std::uint32_t val = SBE_LITTLE_ENDIAN_ENCODE_32(value);
        std::memcpy(m_buffer + m_offset + 193, &val, sizeof(std::uint32_t));
        return *this;
    }

    SBE_NODISCARD static const char *CumQtyMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t cumQtyId() SBE_NOEXCEPT
    {
        return 14;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t cumQtySinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool cumQtyInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t cumQtyEncodingOffset() SBE_NOEXCEPT
    {
        return 197;
    }

    static SBE_CONSTEXPR std::uint32_t cumQtyNullValue() SBE_NOEXCEPT
    {
        return SBE_NULLVALUE_UINT32;
    }

    static SBE_CONSTEXPR std::uint32_t cumQtyMinValue() SBE_NOEXCEPT
    {
        return UINT32_C(0x0);
    }

    static SBE_CONSTEXPR std::uint32_t cumQtyMaxValue() SBE_NOEXCEPT
    {
        return UINT32_C(0xfffffffe);
    }

    static SBE_CONSTEXPR std::size_t cumQtyEncodingLength() SBE_NOEXCEPT
    {
        return 4;
    }

    SBE_NODISCARD std::uint32_t cumQty() const SBE_NOEXCEPT
    {
        std::uint32_t val;
        std::memcpy(&val, m_buffer + m_offset + 197, sizeof(std::uint32_t));
        return SBE_LITTLE_ENDIAN_ENCODE_32(val);
    }

    ExecutionReportTradeOutright525 &cumQty(const std::uint32_t value) SBE_NOEXCEPT
    {
        std::uint32_t val = SBE_LITTLE_ENDIAN_ENCODE_32(value);
        std::memcpy(m_buffer + m_offset + 197, &val, sizeof(std::uint32_t));
        return *this;
    }

    SBE_NODISCARD static const char *MDTradeEntryIDMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t mDTradeEntryIDId() SBE_NOEXCEPT
    {
        return 37711;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t mDTradeEntryIDSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool mDTradeEntryIDInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t mDTradeEntryIDEncodingOffset() SBE_NOEXCEPT
    {
        return 201;
    }

    static SBE_CONSTEXPR std::uint32_t mDTradeEntryIDNullValue() SBE_NOEXCEPT
    {
        return SBE_NULLVALUE_UINT32;
    }

    static SBE_CONSTEXPR std::uint32_t mDTradeEntryIDMinValue() SBE_NOEXCEPT
    {
        return UINT32_C(0x0);
    }

    static SBE_CONSTEXPR std::uint32_t mDTradeEntryIDMaxValue() SBE_NOEXCEPT
    {
        return UINT32_C(0xfffffffe);
    }

    static SBE_CONSTEXPR std::size_t mDTradeEntryIDEncodingLength() SBE_NOEXCEPT
    {
        return 4;
    }

    SBE_NODISCARD std::uint32_t mDTradeEntryID() const SBE_NOEXCEPT
    {
        std::uint32_t val;
        std::memcpy(&val, m_buffer + m_offset + 201, sizeof(std::uint32_t));
        return SBE_LITTLE_ENDIAN_ENCODE_32(val);
    }

    ExecutionReportTradeOutright525 &mDTradeEntryID(const std::uint32_t value) SBE_NOEXCEPT
    {
        std::uint32_t val = SBE_LITTLE_ENDIAN_ENCODE_32(value);
        std::memcpy(m_buffer + m_offset + 201, &val, sizeof(std::uint32_t));
        return *this;
    }

    SBE_NODISCARD static const char *SideTradeIDMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t sideTradeIDId() SBE_NOEXCEPT
    {
        return 1506;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t sideTradeIDSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool sideTradeIDInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t sideTradeIDEncodingOffset() SBE_NOEXCEPT
    {
        return 205;
    }

    static SBE_CONSTEXPR std::uint32_t sideTradeIDNullValue() SBE_NOEXCEPT
    {
        return SBE_NULLVALUE_UINT32;
    }

    static SBE_CONSTEXPR std::uint32_t sideTradeIDMinValue() SBE_NOEXCEPT
    {
        return UINT32_C(0x0);
    }

    static SBE_CONSTEXPR std::uint32_t sideTradeIDMaxValue() SBE_NOEXCEPT
    {
        return UINT32_C(0xfffffffe);
    }

    static SBE_CONSTEXPR std::size_t sideTradeIDEncodingLength() SBE_NOEXCEPT
    {
        return 4;
    }

    SBE_NODISCARD std::uint32_t sideTradeID() const SBE_NOEXCEPT
    {
        std::uint32_t val;
        std::memcpy(&val, m_buffer + m_offset + 205, sizeof(std::uint32_t));
        return SBE_LITTLE_ENDIAN_ENCODE_32(val);
    }

    ExecutionReportTradeOutright525 &sideTradeID(const std::uint32_t value) SBE_NOEXCEPT
    {
        std::uint32_t val = SBE_LITTLE_ENDIAN_ENCODE_32(value);
        std::memcpy(m_buffer + m_offset + 205, &val, sizeof(std::uint32_t));
        return *this;
    }

    SBE_NODISCARD static const char *TradeLinkIDMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "optional";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t tradeLinkIDId() SBE_NOEXCEPT
    {
        return 820;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t tradeLinkIDSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool tradeLinkIDInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t tradeLinkIDEncodingOffset() SBE_NOEXCEPT
    {
        return 209;
    }

    static SBE_CONSTEXPR std::uint32_t tradeLinkIDNullValue() SBE_NOEXCEPT
    {
        return UINT32_C(0xffffffff);
    }

    static SBE_CONSTEXPR std::uint32_t tradeLinkIDMinValue() SBE_NOEXCEPT
    {
        return UINT32_C(0x0);
    }

    static SBE_CONSTEXPR std::uint32_t tradeLinkIDMaxValue() SBE_NOEXCEPT
    {
        return UINT32_C(0xfffffffe);
    }

    static SBE_CONSTEXPR std::size_t tradeLinkIDEncodingLength() SBE_NOEXCEPT
    {
        return 4;
    }

    SBE_NODISCARD std::uint32_t tradeLinkID() const SBE_NOEXCEPT
    {
        std::uint32_t val;
        std::memcpy(&val, m_buffer + m_offset + 209, sizeof(std::uint32_t));
        return SBE_LITTLE_ENDIAN_ENCODE_32(val);
    }

    ExecutionReportTradeOutright525 &tradeLinkID(const std::uint32_t value) SBE_NOEXCEPT
    {
        std::uint32_t val = SBE_LITTLE_ENDIAN_ENCODE_32(value);
        std::memcpy(m_buffer + m_offset + 209, &val, sizeof(std::uint32_t));
        return *this;
    }

    SBE_NODISCARD static const char *LeavesQtyMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t leavesQtyId() SBE_NOEXCEPT
    {
        return 151;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t leavesQtySinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool leavesQtyInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t leavesQtyEncodingOffset() SBE_NOEXCEPT
    {
        return 213;
    }

    static SBE_CONSTEXPR std::uint32_t leavesQtyNullValue() SBE_NOEXCEPT
    {
        return SBE_NULLVALUE_UINT32;
    }

    static SBE_CONSTEXPR std::uint32_t leavesQtyMinValue() SBE_NOEXCEPT
    {
        return UINT32_C(0x0);
    }

    static SBE_CONSTEXPR std::uint32_t leavesQtyMaxValue() SBE_NOEXCEPT
    {
        return UINT32_C(0xfffffffe);
    }

    static SBE_CONSTEXPR std::size_t leavesQtyEncodingLength() SBE_NOEXCEPT
    {
        return 4;
    }

    SBE_NODISCARD std::uint32_t leavesQty() const SBE_NOEXCEPT
    {
        std::uint32_t val;
        std::memcpy(&val, m_buffer + m_offset + 213, sizeof(std::uint32_t));
        return SBE_LITTLE_ENDIAN_ENCODE_32(val);
    }

    ExecutionReportTradeOutright525 &leavesQty(const std::uint32_t value) SBE_NOEXCEPT
    {
        std::uint32_t val = SBE_LITTLE_ENDIAN_ENCODE_32(value);
        std::memcpy(m_buffer + m_offset + 213, &val, sizeof(std::uint32_t));
        return *this;
    }

    SBE_NODISCARD static const char *TradeDateMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "LocalMktDate";
            case MetaAttribute::PRESENCE: return "optional";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t tradeDateId() SBE_NOEXCEPT
    {
        return 75;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t tradeDateSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool tradeDateInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t tradeDateEncodingOffset() SBE_NOEXCEPT
    {
        return 217;
    }

    static SBE_CONSTEXPR std::uint16_t tradeDateNullValue() SBE_NOEXCEPT
    {
        return static_cast<std::uint16_t>(65535);
    }

    static SBE_CONSTEXPR std::uint16_t tradeDateMinValue() SBE_NOEXCEPT
    {
        return static_cast<std::uint16_t>(0);
    }

    static SBE_CONSTEXPR std::uint16_t tradeDateMaxValue() SBE_NOEXCEPT
    {
        return static_cast<std::uint16_t>(65534);
    }

    static SBE_CONSTEXPR std::size_t tradeDateEncodingLength() SBE_NOEXCEPT
    {
        return 2;
    }

    SBE_NODISCARD std::uint16_t tradeDate() const SBE_NOEXCEPT
    {
        std::uint16_t val;
        std::memcpy(&val, m_buffer + m_offset + 217, sizeof(std::uint16_t));
        return SBE_LITTLE_ENDIAN_ENCODE_16(val);
    }

    ExecutionReportTradeOutright525 &tradeDate(const std::uint16_t value) SBE_NOEXCEPT
    {
        std::uint16_t val = SBE_LITTLE_ENDIAN_ENCODE_16(value);
        std::memcpy(m_buffer + m_offset + 217, &val, sizeof(std::uint16_t));
        return *this;
    }

    SBE_NODISCARD static const char *ExpireDateMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "LocalMktDate";
            case MetaAttribute::PRESENCE: return "optional";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t expireDateId() SBE_NOEXCEPT
    {
        return 432;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t expireDateSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool expireDateInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t expireDateEncodingOffset() SBE_NOEXCEPT
    {
        return 219;
    }

    static SBE_CONSTEXPR std::uint16_t expireDateNullValue() SBE_NOEXCEPT
    {
        return static_cast<std::uint16_t>(65535);
    }

    static SBE_CONSTEXPR std::uint16_t expireDateMinValue() SBE_NOEXCEPT
    {
        return static_cast<std::uint16_t>(0);
    }

    static SBE_CONSTEXPR std::uint16_t expireDateMaxValue() SBE_NOEXCEPT
    {
        return static_cast<std::uint16_t>(65534);
    }

    static SBE_CONSTEXPR std::size_t expireDateEncodingLength() SBE_NOEXCEPT
    {
        return 2;
    }

    SBE_NODISCARD std::uint16_t expireDate() const SBE_NOEXCEPT
    {
        std::uint16_t val;
        std::memcpy(&val, m_buffer + m_offset + 219, sizeof(std::uint16_t));
        return SBE_LITTLE_ENDIAN_ENCODE_16(val);
    }

    ExecutionReportTradeOutright525 &expireDate(const std::uint16_t value) SBE_NOEXCEPT
    {
        std::uint16_t val = SBE_LITTLE_ENDIAN_ENCODE_16(value);
        std::memcpy(m_buffer + m_offset + 219, &val, sizeof(std::uint16_t));
        return *this;
    }

    SBE_NODISCARD static const char *OrdStatusMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t ordStatusId() SBE_NOEXCEPT
    {
        return 39;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t ordStatusSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool ordStatusInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t ordStatusEncodingOffset() SBE_NOEXCEPT
    {
        return 221;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t ordStatusEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD std::uint8_t ordStatusRaw() const SBE_NOEXCEPT
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 221, sizeof(std::uint8_t));
        return (val);
    }

    SBE_NODISCARD OrdStatusTrd::Value ordStatus() const
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 221, sizeof(std::uint8_t));
        return OrdStatusTrd::get((val));
    }

    ExecutionReportTradeOutright525 &ordStatus(const OrdStatusTrd::Value value) SBE_NOEXCEPT
    {
        std::uint8_t val = (value);
        std::memcpy(m_buffer + m_offset + 221, &val, sizeof(std::uint8_t));
        return *this;
    }

    SBE_NODISCARD static const char *ExecTypeMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "char";
            case MetaAttribute::PRESENCE: return "constant";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t execTypeId() SBE_NOEXCEPT
    {
        return 150;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t execTypeSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool execTypeInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t execTypeEncodingOffset() SBE_NOEXCEPT
    {
        return 222;
    }

    static SBE_CONSTEXPR char execTypeNullValue() SBE_NOEXCEPT
    {
        return static_cast<char>(0);
    }

    static SBE_CONSTEXPR char execTypeMinValue() SBE_NOEXCEPT
    {
        return static_cast<char>(32);
    }

    static SBE_CONSTEXPR char execTypeMaxValue() SBE_NOEXCEPT
    {
        return static_cast<char>(126);
    }

    static SBE_CONSTEXPR std::size_t execTypeEncodingLength() SBE_NOEXCEPT
    {
        return 0;
    }

    static SBE_CONSTEXPR std::uint64_t execTypeLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD const char *execType() const
    {
        static const std::uint8_t execTypeValues[] = { 70, 0 };

        return (const char *)execTypeValues;
    }

    SBE_NODISCARD char execType(const std::uint64_t index) const
    {
        static const std::uint8_t execTypeValues[] = { 70, 0 };

        return (char)execTypeValues[index];
    }

    std::uint64_t getExecType(char *dst, const std::uint64_t length) const
    {
        static std::uint8_t execTypeValues[] = { 70 };
        std::uint64_t bytesToCopy = length < sizeof(execTypeValues) ? length : sizeof(execTypeValues);

        std::memcpy(dst, execTypeValues, static_cast<std::size_t>(bytesToCopy));
        return bytesToCopy;
    }

    std::string getExecTypeAsString() const
    {
        static const std::uint8_t ExecTypeValues[] = { 70 };

        return std::string((const char *)ExecTypeValues, 1);
    }

    std::string getExecTypeAsJsonEscapedString()
    {
        std::ostringstream oss;
        std::string s = getExecTypeAsString();

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

    SBE_NODISCARD static const char *OrdTypeMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "char";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t ordTypeId() SBE_NOEXCEPT
    {
        return 40;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t ordTypeSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool ordTypeInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t ordTypeEncodingOffset() SBE_NOEXCEPT
    {
        return 222;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t ordTypeEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD char ordTypeRaw() const SBE_NOEXCEPT
    {
        char val;
        std::memcpy(&val, m_buffer + m_offset + 222, sizeof(char));
        return (val);
    }

    SBE_NODISCARD OrderType::Value ordType() const
    {
        char val;
        std::memcpy(&val, m_buffer + m_offset + 222, sizeof(char));
        return OrderType::get((val));
    }

    ExecutionReportTradeOutright525 &ordType(const OrderType::Value value) SBE_NOEXCEPT
    {
        char val = (value);
        std::memcpy(m_buffer + m_offset + 222, &val, sizeof(char));
        return *this;
    }

    SBE_NODISCARD static const char *SideMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t sideId() SBE_NOEXCEPT
    {
        return 54;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t sideSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool sideInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t sideEncodingOffset() SBE_NOEXCEPT
    {
        return 223;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t sideEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD std::uint8_t sideRaw() const SBE_NOEXCEPT
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 223, sizeof(std::uint8_t));
        return (val);
    }

    SBE_NODISCARD SideReq::Value side() const
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 223, sizeof(std::uint8_t));
        return SideReq::get((val));
    }

    ExecutionReportTradeOutright525 &side(const SideReq::Value value) SBE_NOEXCEPT
    {
        std::uint8_t val = (value);
        std::memcpy(m_buffer + m_offset + 223, &val, sizeof(std::uint8_t));
        return *this;
    }

    SBE_NODISCARD static const char *TimeInForceMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t timeInForceId() SBE_NOEXCEPT
    {
        return 59;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t timeInForceSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool timeInForceInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t timeInForceEncodingOffset() SBE_NOEXCEPT
    {
        return 224;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t timeInForceEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD std::uint8_t timeInForceRaw() const SBE_NOEXCEPT
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 224, sizeof(std::uint8_t));
        return (val);
    }

    SBE_NODISCARD TimeInForce::Value timeInForce() const
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 224, sizeof(std::uint8_t));
        return TimeInForce::get((val));
    }

    ExecutionReportTradeOutright525 &timeInForce(const TimeInForce::Value value) SBE_NOEXCEPT
    {
        std::uint8_t val = (value);
        std::memcpy(m_buffer + m_offset + 224, &val, sizeof(std::uint8_t));
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
        return 225;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t manualOrderIndicatorEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD std::uint8_t manualOrderIndicatorRaw() const SBE_NOEXCEPT
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 225, sizeof(std::uint8_t));
        return (val);
    }

    SBE_NODISCARD ManualOrdIndReq::Value manualOrderIndicator() const
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 225, sizeof(std::uint8_t));
        return ManualOrdIndReq::get((val));
    }

    ExecutionReportTradeOutright525 &manualOrderIndicator(const ManualOrdIndReq::Value value) SBE_NOEXCEPT
    {
        std::uint8_t val = (value);
        std::memcpy(m_buffer + m_offset + 225, &val, sizeof(std::uint8_t));
        return *this;
    }

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
        return 226;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t possRetransFlagEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD std::uint8_t possRetransFlagRaw() const SBE_NOEXCEPT
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 226, sizeof(std::uint8_t));
        return (val);
    }

    SBE_NODISCARD BooleanFlag::Value possRetransFlag() const
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 226, sizeof(std::uint8_t));
        return BooleanFlag::get((val));
    }

    ExecutionReportTradeOutright525 &possRetransFlag(const BooleanFlag::Value value) SBE_NOEXCEPT
    {
        std::uint8_t val = (value);
        std::memcpy(m_buffer + m_offset + 226, &val, sizeof(std::uint8_t));
        return *this;
    }

    SBE_NODISCARD static const char *AggressorIndicatorMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t aggressorIndicatorId() SBE_NOEXCEPT
    {
        return 1057;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t aggressorIndicatorSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool aggressorIndicatorInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t aggressorIndicatorEncodingOffset() SBE_NOEXCEPT
    {
        return 227;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t aggressorIndicatorEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD std::uint8_t aggressorIndicatorRaw() const SBE_NOEXCEPT
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 227, sizeof(std::uint8_t));
        return (val);
    }

    SBE_NODISCARD BooleanFlag::Value aggressorIndicator() const
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 227, sizeof(std::uint8_t));
        return BooleanFlag::get((val));
    }

    ExecutionReportTradeOutright525 &aggressorIndicator(const BooleanFlag::Value value) SBE_NOEXCEPT
    {
        std::uint8_t val = (value);
        std::memcpy(m_buffer + m_offset + 227, &val, sizeof(std::uint8_t));
        return *this;
    }

    SBE_NODISCARD static const char *CrossTypeMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "optional";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t crossTypeId() SBE_NOEXCEPT
    {
        return 549;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t crossTypeSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool crossTypeInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t crossTypeEncodingOffset() SBE_NOEXCEPT
    {
        return 228;
    }

    static SBE_CONSTEXPR std::uint8_t crossTypeNullValue() SBE_NOEXCEPT
    {
        return static_cast<std::uint8_t>(255);
    }

    static SBE_CONSTEXPR std::uint8_t crossTypeMinValue() SBE_NOEXCEPT
    {
        return static_cast<std::uint8_t>(0);
    }

    static SBE_CONSTEXPR std::uint8_t crossTypeMaxValue() SBE_NOEXCEPT
    {
        return static_cast<std::uint8_t>(254);
    }

    static SBE_CONSTEXPR std::size_t crossTypeEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD std::uint8_t crossType() const SBE_NOEXCEPT
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 228, sizeof(std::uint8_t));
        return (val);
    }

    ExecutionReportTradeOutright525 &crossType(const std::uint8_t value) SBE_NOEXCEPT
    {
        std::uint8_t val = (value);
        std::memcpy(m_buffer + m_offset + 228, &val, sizeof(std::uint8_t));
        return *this;
    }

    SBE_NODISCARD static const char *ExecInstMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "MultipleCharValue";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t execInstId() SBE_NOEXCEPT
    {
        return 18;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t execInstSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool execInstInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t execInstEncodingOffset() SBE_NOEXCEPT
    {
        return 229;
    }

private:
    ExecInst m_execInst;

public:
    SBE_NODISCARD ExecInst &execInst()
    {
        m_execInst.wrap(m_buffer, m_offset + 229, m_actingVersion, m_bufferLength);
        return m_execInst;
    }

    static SBE_CONSTEXPR std::size_t execInstEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD static const char *ExecutionModeMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "char";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t executionModeId() SBE_NOEXCEPT
    {
        return 5906;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t executionModeSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool executionModeInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t executionModeEncodingOffset() SBE_NOEXCEPT
    {
        return 230;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t executionModeEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD char executionModeRaw() const SBE_NOEXCEPT
    {
        char val;
        std::memcpy(&val, m_buffer + m_offset + 230, sizeof(char));
        return (val);
    }

    SBE_NODISCARD ExecMode::Value executionMode() const
    {
        char val;
        std::memcpy(&val, m_buffer + m_offset + 230, sizeof(char));
        return ExecMode::get((val));
    }

    ExecutionReportTradeOutright525 &executionMode(const ExecMode::Value value) SBE_NOEXCEPT
    {
        char val = (value);
        std::memcpy(m_buffer + m_offset + 230, &val, sizeof(char));
        return *this;
    }

    SBE_NODISCARD static const char *LiquidityFlagMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t liquidityFlagId() SBE_NOEXCEPT
    {
        return 9373;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t liquidityFlagSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool liquidityFlagInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t liquidityFlagEncodingOffset() SBE_NOEXCEPT
    {
        return 231;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t liquidityFlagEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD std::uint8_t liquidityFlagRaw() const SBE_NOEXCEPT
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 231, sizeof(std::uint8_t));
        return (val);
    }

    SBE_NODISCARD BooleanNULL::Value liquidityFlag() const
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 231, sizeof(std::uint8_t));
        return BooleanNULL::get((val));
    }

    ExecutionReportTradeOutright525 &liquidityFlag(const BooleanNULL::Value value) SBE_NOEXCEPT
    {
        std::uint8_t val = (value);
        std::memcpy(m_buffer + m_offset + 231, &val, sizeof(std::uint8_t));
        return *this;
    }

    SBE_NODISCARD static const char *ManagedOrderMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t managedOrderId() SBE_NOEXCEPT
    {
        return 6881;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t managedOrderSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool managedOrderInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t managedOrderEncodingOffset() SBE_NOEXCEPT
    {
        return 232;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t managedOrderEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD std::uint8_t managedOrderRaw() const SBE_NOEXCEPT
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 232, sizeof(std::uint8_t));
        return (val);
    }

    SBE_NODISCARD BooleanNULL::Value managedOrder() const
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 232, sizeof(std::uint8_t));
        return BooleanNULL::get((val));
    }

    ExecutionReportTradeOutright525 &managedOrder(const BooleanNULL::Value value) SBE_NOEXCEPT
    {
        std::uint8_t val = (value);
        std::memcpy(m_buffer + m_offset + 232, &val, sizeof(std::uint8_t));
        return *this;
    }

    SBE_NODISCARD static const char *ShortSaleTypeMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t shortSaleTypeId() SBE_NOEXCEPT
    {
        return 5409;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t shortSaleTypeSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool shortSaleTypeInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t shortSaleTypeEncodingOffset() SBE_NOEXCEPT
    {
        return 233;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t shortSaleTypeEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD std::uint8_t shortSaleTypeRaw() const SBE_NOEXCEPT
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 233, sizeof(std::uint8_t));
        return (val);
    }

    SBE_NODISCARD ShortSaleType::Value shortSaleType() const
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 233, sizeof(std::uint8_t));
        return ShortSaleType::get((val));
    }

    ExecutionReportTradeOutright525 &shortSaleType(const ShortSaleType::Value value) SBE_NOEXCEPT
    {
        std::uint8_t val = (value);
        std::memcpy(m_buffer + m_offset + 233, &val, sizeof(std::uint8_t));
        return *this;
    }

    SBE_NODISCARD static const char *OwnershipMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t ownershipId() SBE_NOEXCEPT
    {
        return 7191;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t ownershipSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool ownershipInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t ownershipEncodingOffset() SBE_NOEXCEPT
    {
        return 234;
    }

    static SBE_CONSTEXPR std::uint8_t ownershipNullValue() SBE_NOEXCEPT
    {
        return SBE_NULLVALUE_UINT8;
    }

    static SBE_CONSTEXPR std::uint8_t ownershipMinValue() SBE_NOEXCEPT
    {
        return static_cast<std::uint8_t>(0);
    }

    static SBE_CONSTEXPR std::uint8_t ownershipMaxValue() SBE_NOEXCEPT
    {
        return static_cast<std::uint8_t>(254);
    }

    static SBE_CONSTEXPR std::size_t ownershipEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD std::uint8_t ownership() const SBE_NOEXCEPT
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 234, sizeof(std::uint8_t));
        return (val);
    }

    ExecutionReportTradeOutright525 &ownership(const std::uint8_t value) SBE_NOEXCEPT
    {
        std::uint8_t val = (value);
        std::memcpy(m_buffer + m_offset + 234, &val, sizeof(std::uint8_t));
        return *this;
    }

    SBE_NODISCARD static const char *DiscretionPriceMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "Price";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t discretionPriceId() SBE_NOEXCEPT
    {
        return 845;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t discretionPriceSinceVersion() SBE_NOEXCEPT
    {
        return 6;
    }

    SBE_NODISCARD bool discretionPriceInActingVersion() SBE_NOEXCEPT
    {
        return m_actingVersion >= discretionPriceSinceVersion();
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t discretionPriceEncodingOffset() SBE_NOEXCEPT
    {
        return 235;
    }

private:
    PRICENULL9 m_discretionPrice;

public:
    SBE_NODISCARD PRICENULL9 &discretionPrice()
    {
        m_discretionPrice.wrap(m_buffer, m_offset + 235, m_actingVersion, m_bufferLength);
        return m_discretionPrice;
    }

    SBE_NODISCARD static const char *TrdTypeMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "optional";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t trdTypeId() SBE_NOEXCEPT
    {
        return 828;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t trdTypeSinceVersion() SBE_NOEXCEPT
    {
        return 6;
    }

    SBE_NODISCARD bool trdTypeInActingVersion() SBE_NOEXCEPT
    {
        return m_actingVersion >= trdTypeSinceVersion();
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t trdTypeEncodingOffset() SBE_NOEXCEPT
    {
        return 243;
    }

    static SBE_CONSTEXPR std::uint16_t trdTypeNullValue() SBE_NOEXCEPT
    {
        return static_cast<std::uint16_t>(65535);
    }

    static SBE_CONSTEXPR std::uint16_t trdTypeMinValue() SBE_NOEXCEPT
    {
        return static_cast<std::uint16_t>(0);
    }

    static SBE_CONSTEXPR std::uint16_t trdTypeMaxValue() SBE_NOEXCEPT
    {
        return static_cast<std::uint16_t>(65534);
    }

    static SBE_CONSTEXPR std::size_t trdTypeEncodingLength() SBE_NOEXCEPT
    {
        return 2;
    }

    SBE_NODISCARD std::uint16_t trdType() const SBE_NOEXCEPT
    {
        if (m_actingVersion < 6)
        {
            return static_cast<std::uint16_t>(65535);
        }

        std::uint16_t val;
        std::memcpy(&val, m_buffer + m_offset + 243, sizeof(std::uint16_t));
        return SBE_LITTLE_ENDIAN_ENCODE_16(val);
    }

    ExecutionReportTradeOutright525 &trdType(const std::uint16_t value) SBE_NOEXCEPT
    {
        std::uint16_t val = SBE_LITTLE_ENDIAN_ENCODE_16(value);
        std::memcpy(m_buffer + m_offset + 243, &val, sizeof(std::uint16_t));
        return *this;
    }

    SBE_NODISCARD static const char *ExecRestatementReasonMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t execRestatementReasonId() SBE_NOEXCEPT
    {
        return 378;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t execRestatementReasonSinceVersion() SBE_NOEXCEPT
    {
        return 6;
    }

    SBE_NODISCARD bool execRestatementReasonInActingVersion() SBE_NOEXCEPT
    {
        return m_actingVersion >= execRestatementReasonSinceVersion();
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t execRestatementReasonEncodingOffset() SBE_NOEXCEPT
    {
        return 245;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t execRestatementReasonEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD std::uint8_t execRestatementReasonRaw() const SBE_NOEXCEPT
    {
        if (m_actingVersion < 6)
        {
            return static_cast<std::uint8_t>(255);
        }

        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 245, sizeof(std::uint8_t));
        return (val);
    }

    SBE_NODISCARD ExecReason::Value execRestatementReason() const
    {
        if (m_actingVersion < 6)
        {
            return ExecReason::NULL_VALUE;
        }

        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 245, sizeof(std::uint8_t));
        return ExecReason::get((val));
    }

    ExecutionReportTradeOutright525 &execRestatementReason(const ExecReason::Value value) SBE_NOEXCEPT
    {
        std::uint8_t val = (value);
        std::memcpy(m_buffer + m_offset + 245, &val, sizeof(std::uint8_t));
        return *this;
    }

    SBE_NODISCARD static const char *SettlDateMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "LocalMktDate";
            case MetaAttribute::PRESENCE: return "optional";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t settlDateId() SBE_NOEXCEPT
    {
        return 64;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t settlDateSinceVersion() SBE_NOEXCEPT
    {
        return 6;
    }

    SBE_NODISCARD bool settlDateInActingVersion() SBE_NOEXCEPT
    {
        return m_actingVersion >= settlDateSinceVersion();
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t settlDateEncodingOffset() SBE_NOEXCEPT
    {
        return 246;
    }

    static SBE_CONSTEXPR std::uint16_t settlDateNullValue() SBE_NOEXCEPT
    {
        return static_cast<std::uint16_t>(65535);
    }

    static SBE_CONSTEXPR std::uint16_t settlDateMinValue() SBE_NOEXCEPT
    {
        return static_cast<std::uint16_t>(0);
    }

    static SBE_CONSTEXPR std::uint16_t settlDateMaxValue() SBE_NOEXCEPT
    {
        return static_cast<std::uint16_t>(65534);
    }

    static SBE_CONSTEXPR std::size_t settlDateEncodingLength() SBE_NOEXCEPT
    {
        return 2;
    }

    SBE_NODISCARD std::uint16_t settlDate() const SBE_NOEXCEPT
    {
        if (m_actingVersion < 6)
        {
            return static_cast<std::uint16_t>(65535);
        }

        std::uint16_t val;
        std::memcpy(&val, m_buffer + m_offset + 246, sizeof(std::uint16_t));
        return SBE_LITTLE_ENDIAN_ENCODE_16(val);
    }

    ExecutionReportTradeOutright525 &settlDate(const std::uint16_t value) SBE_NOEXCEPT
    {
        std::uint16_t val = SBE_LITTLE_ENDIAN_ENCODE_16(value);
        std::memcpy(m_buffer + m_offset + 246, &val, sizeof(std::uint16_t));
        return *this;
    }

    SBE_NODISCARD static const char *MaturityDateMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "LocalMktDate";
            case MetaAttribute::PRESENCE: return "optional";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t maturityDateId() SBE_NOEXCEPT
    {
        return 541;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t maturityDateSinceVersion() SBE_NOEXCEPT
    {
        return 6;
    }

    SBE_NODISCARD bool maturityDateInActingVersion() SBE_NOEXCEPT
    {
        return m_actingVersion >= maturityDateSinceVersion();
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t maturityDateEncodingOffset() SBE_NOEXCEPT
    {
        return 248;
    }

    static SBE_CONSTEXPR std::uint16_t maturityDateNullValue() SBE_NOEXCEPT
    {
        return static_cast<std::uint16_t>(65535);
    }

    static SBE_CONSTEXPR std::uint16_t maturityDateMinValue() SBE_NOEXCEPT
    {
        return static_cast<std::uint16_t>(0);
    }

    static SBE_CONSTEXPR std::uint16_t maturityDateMaxValue() SBE_NOEXCEPT
    {
        return static_cast<std::uint16_t>(65534);
    }

    static SBE_CONSTEXPR std::size_t maturityDateEncodingLength() SBE_NOEXCEPT
    {
        return 2;
    }

    SBE_NODISCARD std::uint16_t maturityDate() const SBE_NOEXCEPT
    {
        if (m_actingVersion < 6)
        {
            return static_cast<std::uint16_t>(65535);
        }

        std::uint16_t val;
        std::memcpy(&val, m_buffer + m_offset + 248, sizeof(std::uint16_t));
        return SBE_LITTLE_ENDIAN_ENCODE_16(val);
    }

    ExecutionReportTradeOutright525 &maturityDate(const std::uint16_t value) SBE_NOEXCEPT
    {
        std::uint16_t val = SBE_LITTLE_ENDIAN_ENCODE_16(value);
        std::memcpy(m_buffer + m_offset + 248, &val, sizeof(std::uint16_t));
        return *this;
    }

    SBE_NODISCARD static const char *CalculatedCcyLastQtyMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t calculatedCcyLastQtyId() SBE_NOEXCEPT
    {
        return 1056;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t calculatedCcyLastQtySinceVersion() SBE_NOEXCEPT
    {
        return 6;
    }

    SBE_NODISCARD bool calculatedCcyLastQtyInActingVersion() SBE_NOEXCEPT
    {
        return m_actingVersion >= calculatedCcyLastQtySinceVersion();
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t calculatedCcyLastQtyEncodingOffset() SBE_NOEXCEPT
    {
        return 250;
    }

private:
    Decimal64NULL m_calculatedCcyLastQty;

public:
    SBE_NODISCARD Decimal64NULL &calculatedCcyLastQty()
    {
        m_calculatedCcyLastQty.wrap(m_buffer, m_offset + 250, m_actingVersion, m_bufferLength);
        return m_calculatedCcyLastQty;
    }

    SBE_NODISCARD static const char *GrossTradeAmtMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t grossTradeAmtId() SBE_NOEXCEPT
    {
        return 381;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t grossTradeAmtSinceVersion() SBE_NOEXCEPT
    {
        return 6;
    }

    SBE_NODISCARD bool grossTradeAmtInActingVersion() SBE_NOEXCEPT
    {
        return m_actingVersion >= grossTradeAmtSinceVersion();
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t grossTradeAmtEncodingOffset() SBE_NOEXCEPT
    {
        return 259;
    }

private:
    Decimal64NULL m_grossTradeAmt;

public:
    SBE_NODISCARD Decimal64NULL &grossTradeAmt()
    {
        m_grossTradeAmt.wrap(m_buffer, m_offset + 259, m_actingVersion, m_bufferLength);
        return m_grossTradeAmt;
    }

    SBE_NODISCARD static const char *BenchmarkPriceMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "Price";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t benchmarkPriceId() SBE_NOEXCEPT
    {
        return 6262;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t benchmarkPriceSinceVersion() SBE_NOEXCEPT
    {
        return 6;
    }

    SBE_NODISCARD bool benchmarkPriceInActingVersion() SBE_NOEXCEPT
    {
        return m_actingVersion >= benchmarkPriceSinceVersion();
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t benchmarkPriceEncodingOffset() SBE_NOEXCEPT
    {
        return 268;
    }

private:
    PRICENULL9 m_benchmarkPrice;

public:
    SBE_NODISCARD PRICENULL9 &benchmarkPrice()
    {
        m_benchmarkPrice.wrap(m_buffer, m_offset + 268, m_actingVersion, m_bufferLength);
        return m_benchmarkPrice;
    }

    SBE_NODISCARD static const char *ReservationPriceMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "Price";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t reservationPriceId() SBE_NOEXCEPT
    {
        return 9562;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t reservationPriceSinceVersion() SBE_NOEXCEPT
    {
        return 8;
    }

    SBE_NODISCARD bool reservationPriceInActingVersion() SBE_NOEXCEPT
    {
        return m_actingVersion >= reservationPriceSinceVersion();
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t reservationPriceEncodingOffset() SBE_NOEXCEPT
    {
        return 276;
    }

private:
    PRICENULL9 m_reservationPrice;

public:
    SBE_NODISCARD PRICENULL9 &reservationPrice()
    {
        m_reservationPrice.wrap(m_buffer, m_offset + 276, m_actingVersion, m_bufferLength);
        return m_reservationPrice;
    }

    SBE_NODISCARD static const char *PriorityIndicatorMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "optional";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t priorityIndicatorId() SBE_NOEXCEPT
    {
        return 638;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t priorityIndicatorSinceVersion() SBE_NOEXCEPT
    {
        return 8;
    }

    SBE_NODISCARD bool priorityIndicatorInActingVersion() SBE_NOEXCEPT
    {
        return m_actingVersion >= priorityIndicatorSinceVersion();
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t priorityIndicatorEncodingOffset() SBE_NOEXCEPT
    {
        return 284;
    }

    static SBE_CONSTEXPR std::uint8_t priorityIndicatorNullValue() SBE_NOEXCEPT
    {
        return static_cast<std::uint8_t>(255);
    }

    static SBE_CONSTEXPR std::uint8_t priorityIndicatorMinValue() SBE_NOEXCEPT
    {
        return static_cast<std::uint8_t>(0);
    }

    static SBE_CONSTEXPR std::uint8_t priorityIndicatorMaxValue() SBE_NOEXCEPT
    {
        return static_cast<std::uint8_t>(254);
    }

    static SBE_CONSTEXPR std::size_t priorityIndicatorEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD std::uint8_t priorityIndicator() const SBE_NOEXCEPT
    {
        if (m_actingVersion < 8)
        {
            return static_cast<std::uint8_t>(255);
        }

        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 284, sizeof(std::uint8_t));
        return (val);
    }

    ExecutionReportTradeOutright525 &priorityIndicator(const std::uint8_t value) SBE_NOEXCEPT
    {
        std::uint8_t val = (value);
        std::memcpy(m_buffer + m_offset + 284, &val, sizeof(std::uint8_t));
        return *this;
    }

    SBE_NODISCARD static const char *DisplayLimitPriceMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "Price";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t displayLimitPriceId() SBE_NOEXCEPT
    {
        return 9264;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t displayLimitPriceSinceVersion() SBE_NOEXCEPT
    {
        return 8;
    }

    SBE_NODISCARD bool displayLimitPriceInActingVersion() SBE_NOEXCEPT
    {
        return m_actingVersion >= displayLimitPriceSinceVersion();
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t displayLimitPriceEncodingOffset() SBE_NOEXCEPT
    {
        return 285;
    }

private:
    PRICENULL9 m_displayLimitPrice;

public:
    SBE_NODISCARD PRICENULL9 &displayLimitPrice()
    {
        m_displayLimitPrice.wrap(m_buffer, m_offset + 285, m_actingVersion, m_bufferLength);
        return m_displayLimitPrice;
    }

    class NoFills
    {
    private:
        char *m_buffer = nullptr;
        std::uint64_t m_bufferLength = 0;
        std::uint64_t m_initialPosition = 0;
        std::uint64_t *m_positionPtr = nullptr;
        std::uint64_t m_blockLength = 0;
        std::uint64_t m_count = 0;
        std::uint64_t m_index = 0;
        std::uint64_t m_offset = 0;
        std::uint64_t m_actingVersion = 0;

        SBE_NODISCARD std::uint64_t *sbePositionPtr() SBE_NOEXCEPT
        {
            return m_positionPtr;
        }

    public:
        inline void wrapForDecode(
            char *buffer,
            std::uint64_t *pos,
            const std::uint64_t actingVersion,
            const std::uint64_t bufferLength)
        {
            GroupSize dimensions(buffer, *pos, bufferLength, actingVersion);
            m_buffer = buffer;
            m_bufferLength = bufferLength;
            m_blockLength = dimensions.blockLength();
            m_count = dimensions.numInGroup();
            m_index = 0;
            m_actingVersion = actingVersion;
            m_initialPosition = *pos;
            m_positionPtr = pos;
            *m_positionPtr = *m_positionPtr + 3;
        }

        inline void wrapForEncode(
            char *buffer,
            const std::uint8_t count,
            std::uint64_t *pos,
            const std::uint64_t actingVersion,
            const std::uint64_t bufferLength)
        {
    #if defined(__GNUG__) && !defined(__clang__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wtype-limits"
    #endif
            if (count > 254)
            {
                throw std::runtime_error("count outside of allowed range [E110]");
            }
    #if defined(__GNUG__) && !defined(__clang__)
    #pragma GCC diagnostic pop
    #endif
            m_buffer = buffer;
            m_bufferLength = bufferLength;
            GroupSize dimensions(buffer, *pos, bufferLength, actingVersion);
            dimensions.blockLength(static_cast<std::uint16_t>(15));
            dimensions.numInGroup(static_cast<std::uint8_t>(count));
            m_index = 0;
            m_count = count;
            m_blockLength = 15;
            m_actingVersion = actingVersion;
            m_initialPosition = *pos;
            m_positionPtr = pos;
            *m_positionPtr = *m_positionPtr + 3;
        }

        static SBE_CONSTEXPR std::uint64_t sbeHeaderSize() SBE_NOEXCEPT
        {
            return 3;
        }

        static SBE_CONSTEXPR std::uint64_t sbeBlockLength() SBE_NOEXCEPT
        {
            return 15;
        }

        SBE_NODISCARD std::uint64_t sbePosition() const SBE_NOEXCEPT
        {
            return *m_positionPtr;
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
            *m_positionPtr = sbeCheckPosition(position);
        }

        SBE_NODISCARD inline std::uint64_t count() const SBE_NOEXCEPT
        {
            return m_count;
        }

        SBE_NODISCARD inline bool hasNext() const SBE_NOEXCEPT
        {
            return m_index < m_count;
        }

        inline NoFills &next()
        {
            if (m_index >= m_count)
            {
                throw std::runtime_error("index >= count [E108]");
            }
            m_offset = *m_positionPtr;
            if (SBE_BOUNDS_CHECK_EXPECT(((m_offset + m_blockLength) > m_bufferLength), false))
            {
                throw std::runtime_error("buffer too short for next group index [E108]");
            }
            *m_positionPtr = m_offset + m_blockLength;
            ++m_index;

            return *this;
        }

        inline std::uint64_t resetCountToIndex()
        {
            m_count = m_index;
            GroupSize dimensions(m_buffer, m_initialPosition, m_bufferLength, m_actingVersion);
            dimensions.numInGroup(static_cast<std::uint8_t>(m_count));
            return m_count;
        }

        template<class Func> inline void forEach(Func &&func)
        {
            while (hasNext())
            {
                next();
                func(*this);
            }
        }


        SBE_NODISCARD static const char *FillPxMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
        {
            switch (metaAttribute)
            {
                case MetaAttribute::SEMANTIC_TYPE: return "Price";
                case MetaAttribute::PRESENCE: return "required";
                default: return "";
            }
        }

        static SBE_CONSTEXPR std::uint16_t fillPxId() SBE_NOEXCEPT
        {
            return 1364;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t fillPxSinceVersion() SBE_NOEXCEPT
        {
            return 0;
        }

        SBE_NODISCARD bool fillPxInActingVersion() SBE_NOEXCEPT
        {
            return true;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::size_t fillPxEncodingOffset() SBE_NOEXCEPT
        {
            return 0;
        }

private:
        PRICE9 m_fillPx;

public:
        SBE_NODISCARD PRICE9 &fillPx()
        {
            m_fillPx.wrap(m_buffer, m_offset + 0, m_actingVersion, m_bufferLength);
            return m_fillPx;
        }

        SBE_NODISCARD static const char *FillQtyMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
        {
            switch (metaAttribute)
            {
                case MetaAttribute::SEMANTIC_TYPE: return "int";
                case MetaAttribute::PRESENCE: return "required";
                default: return "";
            }
        }

        static SBE_CONSTEXPR std::uint16_t fillQtyId() SBE_NOEXCEPT
        {
            return 1365;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t fillQtySinceVersion() SBE_NOEXCEPT
        {
            return 0;
        }

        SBE_NODISCARD bool fillQtyInActingVersion() SBE_NOEXCEPT
        {
            return true;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::size_t fillQtyEncodingOffset() SBE_NOEXCEPT
        {
            return 8;
        }

        static SBE_CONSTEXPR std::uint32_t fillQtyNullValue() SBE_NOEXCEPT
        {
            return SBE_NULLVALUE_UINT32;
        }

        static SBE_CONSTEXPR std::uint32_t fillQtyMinValue() SBE_NOEXCEPT
        {
            return UINT32_C(0x0);
        }

        static SBE_CONSTEXPR std::uint32_t fillQtyMaxValue() SBE_NOEXCEPT
        {
            return UINT32_C(0xfffffffe);
        }

        static SBE_CONSTEXPR std::size_t fillQtyEncodingLength() SBE_NOEXCEPT
        {
            return 4;
        }

        SBE_NODISCARD std::uint32_t fillQty() const SBE_NOEXCEPT
        {
            std::uint32_t val;
            std::memcpy(&val, m_buffer + m_offset + 8, sizeof(std::uint32_t));
            return SBE_LITTLE_ENDIAN_ENCODE_32(val);
        }

        NoFills &fillQty(const std::uint32_t value) SBE_NOEXCEPT
        {
            std::uint32_t val = SBE_LITTLE_ENDIAN_ENCODE_32(value);
            std::memcpy(m_buffer + m_offset + 8, &val, sizeof(std::uint32_t));
            return *this;
        }

        SBE_NODISCARD static const char *FillExecIDMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
        {
            switch (metaAttribute)
            {
                case MetaAttribute::SEMANTIC_TYPE: return "String";
                case MetaAttribute::PRESENCE: return "required";
                default: return "";
            }
        }

        static SBE_CONSTEXPR std::uint16_t fillExecIDId() SBE_NOEXCEPT
        {
            return 1363;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t fillExecIDSinceVersion() SBE_NOEXCEPT
        {
            return 0;
        }

        SBE_NODISCARD bool fillExecIDInActingVersion() SBE_NOEXCEPT
        {
            return true;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::size_t fillExecIDEncodingOffset() SBE_NOEXCEPT
        {
            return 12;
        }

        static SBE_CONSTEXPR char fillExecIDNullValue() SBE_NOEXCEPT
        {
            return static_cast<char>(0);
        }

        static SBE_CONSTEXPR char fillExecIDMinValue() SBE_NOEXCEPT
        {
            return static_cast<char>(32);
        }

        static SBE_CONSTEXPR char fillExecIDMaxValue() SBE_NOEXCEPT
        {
            return static_cast<char>(126);
        }

        static SBE_CONSTEXPR std::size_t fillExecIDEncodingLength() SBE_NOEXCEPT
        {
            return 2;
        }

        static SBE_CONSTEXPR std::uint64_t fillExecIDLength() SBE_NOEXCEPT
        {
            return 2;
        }

        SBE_NODISCARD const char *fillExecID() const SBE_NOEXCEPT
        {
            return m_buffer + m_offset + 12;
        }

        SBE_NODISCARD char *fillExecID() SBE_NOEXCEPT
        {
            return m_buffer + m_offset + 12;
        }

        SBE_NODISCARD char fillExecID(const std::uint64_t index) const
        {
            if (index >= 2)
            {
                throw std::runtime_error("index out of range for fillExecID [E104]");
            }

            char val;
            std::memcpy(&val, m_buffer + m_offset + 12 + (index * 1), sizeof(char));
            return (val);
        }

        NoFills &fillExecID(const std::uint64_t index, const char value)
        {
            if (index >= 2)
            {
                throw std::runtime_error("index out of range for fillExecID [E105]");
            }

            char val = (value);
            std::memcpy(m_buffer + m_offset + 12 + (index * 1), &val, sizeof(char));
            return *this;
        }

        std::uint64_t getFillExecID(char *const dst, const std::uint64_t length) const
        {
            if (length > 2)
            {
                throw std::runtime_error("length too large for getFillExecID [E106]");
            }

            std::memcpy(dst, m_buffer + m_offset + 12, sizeof(char) * static_cast<std::size_t>(length));
            return length;
        }

        NoFills &putFillExecID(const char *const src) SBE_NOEXCEPT
        {
            std::memcpy(m_buffer + m_offset + 12, src, sizeof(char) * 2);
            return *this;
        }

        NoFills &putFillExecID(
            const char value0,
            const char value1) SBE_NOEXCEPT
        {
            char val0 = (value0);
            std::memcpy(m_buffer + m_offset + 12, &val0, sizeof(char));
            char val1 = (value1);
            std::memcpy(m_buffer + m_offset + 13, &val1, sizeof(char));

            return *this;
        }

        SBE_NODISCARD std::string getFillExecIDAsString() const
        {
            const char *buffer = m_buffer + m_offset + 12;
            std::size_t length = 0;

            for (; length < 2 && *(buffer + length) != '\0'; ++length);
            std::string result(buffer, length);

            return result;
        }

        std::string getFillExecIDAsJsonEscapedString()
        {
            std::ostringstream oss;
            std::string s = getFillExecIDAsString();

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
        SBE_NODISCARD std::string_view getFillExecIDAsStringView() const SBE_NOEXCEPT
        {
            const char *buffer = m_buffer + m_offset + 12;
            std::size_t length = 0;

            for (; length < 2 && *(buffer + length) != '\0'; ++length);
            std::string_view result(buffer, length);

            return result;
        }
        #endif

        #if __cplusplus >= 201703L
        NoFills &putFillExecID(const std::string_view str)
        {
            const std::size_t srcLength = str.length();
            if (srcLength > 2)
            {
                throw std::runtime_error("string too large for putFillExecID [E106]");
            }

            std::memcpy(m_buffer + m_offset + 12, str.data(), srcLength);
            for (std::size_t start = srcLength; start < 2; ++start)
            {
                m_buffer[m_offset + 12 + start] = 0;
            }

            return *this;
        }
        #else
        NoFills &putFillExecID(const std::string &str)
        {
            const std::size_t srcLength = str.length();
            if (srcLength > 2)
            {
                throw std::runtime_error("string too large for putFillExecID [E106]");
            }

            std::memcpy(m_buffer + m_offset + 12, str.c_str(), srcLength);
            for (std::size_t start = srcLength; start < 2; ++start)
            {
                m_buffer[m_offset + 12 + start] = 0;
            }

            return *this;
        }
        #endif

        SBE_NODISCARD static const char *FillYieldTypeMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
        {
            switch (metaAttribute)
            {
                case MetaAttribute::SEMANTIC_TYPE: return "int";
                case MetaAttribute::PRESENCE: return "required";
                default: return "";
            }
        }

        static SBE_CONSTEXPR std::uint16_t fillYieldTypeId() SBE_NOEXCEPT
        {
            return 1622;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t fillYieldTypeSinceVersion() SBE_NOEXCEPT
        {
            return 0;
        }

        SBE_NODISCARD bool fillYieldTypeInActingVersion() SBE_NOEXCEPT
        {
            return true;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::size_t fillYieldTypeEncodingOffset() SBE_NOEXCEPT
        {
            return 14;
        }

        static SBE_CONSTEXPR std::uint8_t fillYieldTypeNullValue() SBE_NOEXCEPT
        {
            return SBE_NULLVALUE_UINT8;
        }

        static SBE_CONSTEXPR std::uint8_t fillYieldTypeMinValue() SBE_NOEXCEPT
        {
            return static_cast<std::uint8_t>(0);
        }

        static SBE_CONSTEXPR std::uint8_t fillYieldTypeMaxValue() SBE_NOEXCEPT
        {
            return static_cast<std::uint8_t>(254);
        }

        static SBE_CONSTEXPR std::size_t fillYieldTypeEncodingLength() SBE_NOEXCEPT
        {
            return 1;
        }

        SBE_NODISCARD std::uint8_t fillYieldType() const SBE_NOEXCEPT
        {
            std::uint8_t val;
            std::memcpy(&val, m_buffer + m_offset + 14, sizeof(std::uint8_t));
            return (val);
        }

        NoFills &fillYieldType(const std::uint8_t value) SBE_NOEXCEPT
        {
            std::uint8_t val = (value);
            std::memcpy(m_buffer + m_offset + 14, &val, sizeof(std::uint8_t));
            return *this;
        }

        template<typename CharT, typename Traits>
        friend std::basic_ostream<CharT, Traits> & operator << (
            std::basic_ostream<CharT, Traits> &builder, NoFills &writer)
        {
            builder << '{';
            builder << R"("FillPx": )";
            builder << writer.fillPx();

            builder << ", ";
            builder << R"("FillQty": )";
            builder << +writer.fillQty();

            builder << ", ";
            builder << R"("FillExecID": )";
            builder << '"' <<
                writer.getFillExecIDAsJsonEscapedString().c_str() << '"';

            builder << ", ";
            builder << R"("FillYieldType": )";
            builder << +writer.fillYieldType();

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

private:
    NoFills m_noFills;

public:
    SBE_NODISCARD static SBE_CONSTEXPR std::uint16_t NoFillsId() SBE_NOEXCEPT
    {
        return 1362;
    }

    SBE_NODISCARD inline NoFills &noFills()
    {
        m_noFills.wrapForDecode(m_buffer, sbePositionPtr(), m_actingVersion, m_bufferLength);
        return m_noFills;
    }

    NoFills &noFillsCount(const std::uint8_t count)
    {
        m_noFills.wrapForEncode(m_buffer, count, sbePositionPtr(), m_actingVersion, m_bufferLength);
        return m_noFills;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t noFillsSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool noFillsInActingVersion() const SBE_NOEXCEPT
    {
        return true;
    }

    class NoOrderEvents
    {
    private:
        char *m_buffer = nullptr;
        std::uint64_t m_bufferLength = 0;
        std::uint64_t m_initialPosition = 0;
        std::uint64_t *m_positionPtr = nullptr;
        std::uint64_t m_blockLength = 0;
        std::uint64_t m_count = 0;
        std::uint64_t m_index = 0;
        std::uint64_t m_offset = 0;
        std::uint64_t m_actingVersion = 0;

        SBE_NODISCARD std::uint64_t *sbePositionPtr() SBE_NOEXCEPT
        {
            return m_positionPtr;
        }

    public:
        inline void wrapForDecode(
            char *buffer,
            std::uint64_t *pos,
            const std::uint64_t actingVersion,
            const std::uint64_t bufferLength)
        {
            GroupSize dimensions(buffer, *pos, bufferLength, actingVersion);
            m_buffer = buffer;
            m_bufferLength = bufferLength;
            m_blockLength = dimensions.blockLength();
            m_count = dimensions.numInGroup();
            m_index = 0;
            m_actingVersion = actingVersion;
            m_initialPosition = *pos;
            m_positionPtr = pos;
            *m_positionPtr = *m_positionPtr + 3;
        }

        inline void wrapForEncode(
            char *buffer,
            const std::uint8_t count,
            std::uint64_t *pos,
            const std::uint64_t actingVersion,
            const std::uint64_t bufferLength)
        {
    #if defined(__GNUG__) && !defined(__clang__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wtype-limits"
    #endif
            if (count > 254)
            {
                throw std::runtime_error("count outside of allowed range [E110]");
            }
    #if defined(__GNUG__) && !defined(__clang__)
    #pragma GCC diagnostic pop
    #endif
            m_buffer = buffer;
            m_bufferLength = bufferLength;
            GroupSize dimensions(buffer, *pos, bufferLength, actingVersion);
            dimensions.blockLength(static_cast<std::uint16_t>(41));
            dimensions.numInGroup(static_cast<std::uint8_t>(count));
            m_index = 0;
            m_count = count;
            m_blockLength = 41;
            m_actingVersion = actingVersion;
            m_initialPosition = *pos;
            m_positionPtr = pos;
            *m_positionPtr = *m_positionPtr + 3;
        }

        static SBE_CONSTEXPR std::uint64_t sbeHeaderSize() SBE_NOEXCEPT
        {
            return 3;
        }

        static SBE_CONSTEXPR std::uint64_t sbeBlockLength() SBE_NOEXCEPT
        {
            return 41;
        }

        SBE_NODISCARD std::uint64_t sbePosition() const SBE_NOEXCEPT
        {
            return *m_positionPtr;
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
            *m_positionPtr = sbeCheckPosition(position);
        }

        SBE_NODISCARD inline std::uint64_t count() const SBE_NOEXCEPT
        {
            return m_count;
        }

        SBE_NODISCARD inline bool hasNext() const SBE_NOEXCEPT
        {
            return m_index < m_count;
        }

        inline NoOrderEvents &next()
        {
            if (m_index >= m_count)
            {
                throw std::runtime_error("index >= count [E108]");
            }
            m_offset = *m_positionPtr;
            if (SBE_BOUNDS_CHECK_EXPECT(((m_offset + m_blockLength) > m_bufferLength), false))
            {
                throw std::runtime_error("buffer too short for next group index [E108]");
            }
            *m_positionPtr = m_offset + m_blockLength;
            ++m_index;

            return *this;
        }

        inline std::uint64_t resetCountToIndex()
        {
            m_count = m_index;
            GroupSize dimensions(m_buffer, m_initialPosition, m_bufferLength, m_actingVersion);
            dimensions.numInGroup(static_cast<std::uint8_t>(m_count));
            return m_count;
        }

        template<class Func> inline void forEach(Func &&func)
        {
            while (hasNext())
            {
                next();
                func(*this);
            }
        }


        SBE_NODISCARD static const char *OrderEventPxMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
        {
            switch (metaAttribute)
            {
                case MetaAttribute::SEMANTIC_TYPE: return "Price";
                case MetaAttribute::PRESENCE: return "required";
                default: return "";
            }
        }

        static SBE_CONSTEXPR std::uint16_t orderEventPxId() SBE_NOEXCEPT
        {
            return 1799;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t orderEventPxSinceVersion() SBE_NOEXCEPT
        {
            return 0;
        }

        SBE_NODISCARD bool orderEventPxInActingVersion() SBE_NOEXCEPT
        {
            return true;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::size_t orderEventPxEncodingOffset() SBE_NOEXCEPT
        {
            return 0;
        }

private:
        PRICE9 m_orderEventPx;

public:
        SBE_NODISCARD PRICE9 &orderEventPx()
        {
            m_orderEventPx.wrap(m_buffer, m_offset + 0, m_actingVersion, m_bufferLength);
            return m_orderEventPx;
        }

        SBE_NODISCARD static const char *OrderEventTextMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
        {
            switch (metaAttribute)
            {
                case MetaAttribute::SEMANTIC_TYPE: return "String";
                case MetaAttribute::PRESENCE: return "optional";
                default: return "";
            }
        }

        static SBE_CONSTEXPR std::uint16_t orderEventTextId() SBE_NOEXCEPT
        {
            return 1802;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t orderEventTextSinceVersion() SBE_NOEXCEPT
        {
            return 0;
        }

        SBE_NODISCARD bool orderEventTextInActingVersion() SBE_NOEXCEPT
        {
            return true;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::size_t orderEventTextEncodingOffset() SBE_NOEXCEPT
        {
            return 8;
        }

        static SBE_CONSTEXPR char orderEventTextNullValue() SBE_NOEXCEPT
        {
            return static_cast<char>(0);
        }

        static SBE_CONSTEXPR char orderEventTextMinValue() SBE_NOEXCEPT
        {
            return static_cast<char>(32);
        }

        static SBE_CONSTEXPR char orderEventTextMaxValue() SBE_NOEXCEPT
        {
            return static_cast<char>(126);
        }

        static SBE_CONSTEXPR std::size_t orderEventTextEncodingLength() SBE_NOEXCEPT
        {
            return 5;
        }

        static SBE_CONSTEXPR std::uint64_t orderEventTextLength() SBE_NOEXCEPT
        {
            return 5;
        }

        SBE_NODISCARD const char *orderEventText() const SBE_NOEXCEPT
        {
            return m_buffer + m_offset + 8;
        }

        SBE_NODISCARD char *orderEventText() SBE_NOEXCEPT
        {
            return m_buffer + m_offset + 8;
        }

        SBE_NODISCARD char orderEventText(const std::uint64_t index) const
        {
            if (index >= 5)
            {
                throw std::runtime_error("index out of range for orderEventText [E104]");
            }

            char val;
            std::memcpy(&val, m_buffer + m_offset + 8 + (index * 1), sizeof(char));
            return (val);
        }

        NoOrderEvents &orderEventText(const std::uint64_t index, const char value)
        {
            if (index >= 5)
            {
                throw std::runtime_error("index out of range for orderEventText [E105]");
            }

            char val = (value);
            std::memcpy(m_buffer + m_offset + 8 + (index * 1), &val, sizeof(char));
            return *this;
        }

        std::uint64_t getOrderEventText(char *const dst, const std::uint64_t length) const
        {
            if (length > 5)
            {
                throw std::runtime_error("length too large for getOrderEventText [E106]");
            }

            std::memcpy(dst, m_buffer + m_offset + 8, sizeof(char) * static_cast<std::size_t>(length));
            return length;
        }

        NoOrderEvents &putOrderEventText(const char *const src) SBE_NOEXCEPT
        {
            std::memcpy(m_buffer + m_offset + 8, src, sizeof(char) * 5);
            return *this;
        }

        SBE_NODISCARD std::string getOrderEventTextAsString() const
        {
            const char *buffer = m_buffer + m_offset + 8;
            std::size_t length = 0;

            for (; length < 5 && *(buffer + length) != '\0'; ++length);
            std::string result(buffer, length);

            return result;
        }

        std::string getOrderEventTextAsJsonEscapedString()
        {
            std::ostringstream oss;
            std::string s = getOrderEventTextAsString();

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
        SBE_NODISCARD std::string_view getOrderEventTextAsStringView() const SBE_NOEXCEPT
        {
            const char *buffer = m_buffer + m_offset + 8;
            std::size_t length = 0;

            for (; length < 5 && *(buffer + length) != '\0'; ++length);
            std::string_view result(buffer, length);

            return result;
        }
        #endif

        #if __cplusplus >= 201703L
        NoOrderEvents &putOrderEventText(const std::string_view str)
        {
            const std::size_t srcLength = str.length();
            if (srcLength > 5)
            {
                throw std::runtime_error("string too large for putOrderEventText [E106]");
            }

            std::memcpy(m_buffer + m_offset + 8, str.data(), srcLength);
            for (std::size_t start = srcLength; start < 5; ++start)
            {
                m_buffer[m_offset + 8 + start] = 0;
            }

            return *this;
        }
        #else
        NoOrderEvents &putOrderEventText(const std::string &str)
        {
            const std::size_t srcLength = str.length();
            if (srcLength > 5)
            {
                throw std::runtime_error("string too large for putOrderEventText [E106]");
            }

            std::memcpy(m_buffer + m_offset + 8, str.c_str(), srcLength);
            for (std::size_t start = srcLength; start < 5; ++start)
            {
                m_buffer[m_offset + 8 + start] = 0;
            }

            return *this;
        }
        #endif

        SBE_NODISCARD static const char *OrderEventExecIDMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
        {
            switch (metaAttribute)
            {
                case MetaAttribute::SEMANTIC_TYPE: return "int";
                case MetaAttribute::PRESENCE: return "required";
                default: return "";
            }
        }

        static SBE_CONSTEXPR std::uint16_t orderEventExecIDId() SBE_NOEXCEPT
        {
            return 1797;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t orderEventExecIDSinceVersion() SBE_NOEXCEPT
        {
            return 0;
        }

        SBE_NODISCARD bool orderEventExecIDInActingVersion() SBE_NOEXCEPT
        {
            return true;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::size_t orderEventExecIDEncodingOffset() SBE_NOEXCEPT
        {
            return 13;
        }

        static SBE_CONSTEXPR std::uint32_t orderEventExecIDNullValue() SBE_NOEXCEPT
        {
            return SBE_NULLVALUE_UINT32;
        }

        static SBE_CONSTEXPR std::uint32_t orderEventExecIDMinValue() SBE_NOEXCEPT
        {
            return UINT32_C(0x0);
        }

        static SBE_CONSTEXPR std::uint32_t orderEventExecIDMaxValue() SBE_NOEXCEPT
        {
            return UINT32_C(0xfffffffe);
        }

        static SBE_CONSTEXPR std::size_t orderEventExecIDEncodingLength() SBE_NOEXCEPT
        {
            return 4;
        }

        SBE_NODISCARD std::uint32_t orderEventExecID() const SBE_NOEXCEPT
        {
            std::uint32_t val;
            std::memcpy(&val, m_buffer + m_offset + 13, sizeof(std::uint32_t));
            return SBE_LITTLE_ENDIAN_ENCODE_32(val);
        }

        NoOrderEvents &orderEventExecID(const std::uint32_t value) SBE_NOEXCEPT
        {
            std::uint32_t val = SBE_LITTLE_ENDIAN_ENCODE_32(value);
            std::memcpy(m_buffer + m_offset + 13, &val, sizeof(std::uint32_t));
            return *this;
        }

        SBE_NODISCARD static const char *OrderEventQtyMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
        {
            switch (metaAttribute)
            {
                case MetaAttribute::SEMANTIC_TYPE: return "int";
                case MetaAttribute::PRESENCE: return "required";
                default: return "";
            }
        }

        static SBE_CONSTEXPR std::uint16_t orderEventQtyId() SBE_NOEXCEPT
        {
            return 1800;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t orderEventQtySinceVersion() SBE_NOEXCEPT
        {
            return 0;
        }

        SBE_NODISCARD bool orderEventQtyInActingVersion() SBE_NOEXCEPT
        {
            return true;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::size_t orderEventQtyEncodingOffset() SBE_NOEXCEPT
        {
            return 17;
        }

        static SBE_CONSTEXPR std::uint32_t orderEventQtyNullValue() SBE_NOEXCEPT
        {
            return SBE_NULLVALUE_UINT32;
        }

        static SBE_CONSTEXPR std::uint32_t orderEventQtyMinValue() SBE_NOEXCEPT
        {
            return UINT32_C(0x0);
        }

        static SBE_CONSTEXPR std::uint32_t orderEventQtyMaxValue() SBE_NOEXCEPT
        {
            return UINT32_C(0xfffffffe);
        }

        static SBE_CONSTEXPR std::size_t orderEventQtyEncodingLength() SBE_NOEXCEPT
        {
            return 4;
        }

        SBE_NODISCARD std::uint32_t orderEventQty() const SBE_NOEXCEPT
        {
            std::uint32_t val;
            std::memcpy(&val, m_buffer + m_offset + 17, sizeof(std::uint32_t));
            return SBE_LITTLE_ENDIAN_ENCODE_32(val);
        }

        NoOrderEvents &orderEventQty(const std::uint32_t value) SBE_NOEXCEPT
        {
            std::uint32_t val = SBE_LITTLE_ENDIAN_ENCODE_32(value);
            std::memcpy(m_buffer + m_offset + 17, &val, sizeof(std::uint32_t));
            return *this;
        }

        SBE_NODISCARD static const char *OrderEventTypeMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
        {
            switch (metaAttribute)
            {
                case MetaAttribute::SEMANTIC_TYPE: return "int";
                case MetaAttribute::PRESENCE: return "required";
                default: return "";
            }
        }

        static SBE_CONSTEXPR std::uint16_t orderEventTypeId() SBE_NOEXCEPT
        {
            return 1796;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t orderEventTypeSinceVersion() SBE_NOEXCEPT
        {
            return 0;
        }

        SBE_NODISCARD bool orderEventTypeInActingVersion() SBE_NOEXCEPT
        {
            return true;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::size_t orderEventTypeEncodingOffset() SBE_NOEXCEPT
        {
            return 21;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::size_t orderEventTypeEncodingLength() SBE_NOEXCEPT
        {
            return 1;
        }

        SBE_NODISCARD std::uint8_t orderEventTypeRaw() const SBE_NOEXCEPT
        {
            std::uint8_t val;
            std::memcpy(&val, m_buffer + m_offset + 21, sizeof(std::uint8_t));
            return (val);
        }

        SBE_NODISCARD OrderEventType::Value orderEventType() const
        {
            std::uint8_t val;
            std::memcpy(&val, m_buffer + m_offset + 21, sizeof(std::uint8_t));
            return OrderEventType::get((val));
        }

        NoOrderEvents &orderEventType(const OrderEventType::Value value) SBE_NOEXCEPT
        {
            std::uint8_t val = (value);
            std::memcpy(m_buffer + m_offset + 21, &val, sizeof(std::uint8_t));
            return *this;
        }

        SBE_NODISCARD static const char *OrderEventReasonMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
        {
            switch (metaAttribute)
            {
                case MetaAttribute::SEMANTIC_TYPE: return "int";
                case MetaAttribute::PRESENCE: return "required";
                default: return "";
            }
        }

        static SBE_CONSTEXPR std::uint16_t orderEventReasonId() SBE_NOEXCEPT
        {
            return 1798;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t orderEventReasonSinceVersion() SBE_NOEXCEPT
        {
            return 0;
        }

        SBE_NODISCARD bool orderEventReasonInActingVersion() SBE_NOEXCEPT
        {
            return true;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::size_t orderEventReasonEncodingOffset() SBE_NOEXCEPT
        {
            return 22;
        }

        static SBE_CONSTEXPR std::uint8_t orderEventReasonNullValue() SBE_NOEXCEPT
        {
            return SBE_NULLVALUE_UINT8;
        }

        static SBE_CONSTEXPR std::uint8_t orderEventReasonMinValue() SBE_NOEXCEPT
        {
            return static_cast<std::uint8_t>(0);
        }

        static SBE_CONSTEXPR std::uint8_t orderEventReasonMaxValue() SBE_NOEXCEPT
        {
            return static_cast<std::uint8_t>(254);
        }

        static SBE_CONSTEXPR std::size_t orderEventReasonEncodingLength() SBE_NOEXCEPT
        {
            return 1;
        }

        SBE_NODISCARD std::uint8_t orderEventReason() const SBE_NOEXCEPT
        {
            std::uint8_t val;
            std::memcpy(&val, m_buffer + m_offset + 22, sizeof(std::uint8_t));
            return (val);
        }

        NoOrderEvents &orderEventReason(const std::uint8_t value) SBE_NOEXCEPT
        {
            std::uint8_t val = (value);
            std::memcpy(m_buffer + m_offset + 22, &val, sizeof(std::uint8_t));
            return *this;
        }

        SBE_NODISCARD static const char *ContraGrossTradeAmtMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
        {
            switch (metaAttribute)
            {
                case MetaAttribute::SEMANTIC_TYPE: return "int";
                case MetaAttribute::PRESENCE: return "required";
                default: return "";
            }
        }

        static SBE_CONSTEXPR std::uint16_t contraGrossTradeAmtId() SBE_NOEXCEPT
        {
            return 5542;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t contraGrossTradeAmtSinceVersion() SBE_NOEXCEPT
        {
            return 6;
        }

        SBE_NODISCARD bool contraGrossTradeAmtInActingVersion() SBE_NOEXCEPT
        {
            return m_actingVersion >= contraGrossTradeAmtSinceVersion();
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::size_t contraGrossTradeAmtEncodingOffset() SBE_NOEXCEPT
        {
            return 23;
        }

private:
        Decimal64NULL m_contraGrossTradeAmt;

public:
        SBE_NODISCARD Decimal64NULL &contraGrossTradeAmt()
        {
            m_contraGrossTradeAmt.wrap(m_buffer, m_offset + 23, m_actingVersion, m_bufferLength);
            return m_contraGrossTradeAmt;
        }

        SBE_NODISCARD static const char *ContraCalculatedCcyLastQtyMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
        {
            switch (metaAttribute)
            {
                case MetaAttribute::SEMANTIC_TYPE: return "int";
                case MetaAttribute::PRESENCE: return "required";
                default: return "";
            }
        }

        static SBE_CONSTEXPR std::uint16_t contraCalculatedCcyLastQtyId() SBE_NOEXCEPT
        {
            return 5971;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t contraCalculatedCcyLastQtySinceVersion() SBE_NOEXCEPT
        {
            return 6;
        }

        SBE_NODISCARD bool contraCalculatedCcyLastQtyInActingVersion() SBE_NOEXCEPT
        {
            return m_actingVersion >= contraCalculatedCcyLastQtySinceVersion();
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::size_t contraCalculatedCcyLastQtyEncodingOffset() SBE_NOEXCEPT
        {
            return 32;
        }

private:
        Decimal64NULL m_contraCalculatedCcyLastQty;

public:
        SBE_NODISCARD Decimal64NULL &contraCalculatedCcyLastQty()
        {
            m_contraCalculatedCcyLastQty.wrap(m_buffer, m_offset + 32, m_actingVersion, m_bufferLength);
            return m_contraCalculatedCcyLastQty;
        }

        template<typename CharT, typename Traits>
        friend std::basic_ostream<CharT, Traits> & operator << (
            std::basic_ostream<CharT, Traits> &builder, NoOrderEvents &writer)
        {
            builder << '{';
            builder << R"("OrderEventPx": )";
            builder << writer.orderEventPx();

            builder << ", ";
            builder << R"("OrderEventText": )";
            builder << '"' <<
                writer.getOrderEventTextAsJsonEscapedString().c_str() << '"';

            builder << ", ";
            builder << R"("OrderEventExecID": )";
            builder << +writer.orderEventExecID();

            builder << ", ";
            builder << R"("OrderEventQty": )";
            builder << +writer.orderEventQty();

            builder << ", ";
            builder << R"("OrderEventType": )";
            builder << '"' << writer.orderEventType() << '"';

            builder << ", ";
            builder << R"("OrderEventReason": )";
            builder << +writer.orderEventReason();

            builder << ", ";
            builder << R"("ContraGrossTradeAmt": )";
            if (writer.contraGrossTradeAmtInActingVersion())
            {
                builder << writer.contraGrossTradeAmt();
            }
            else
            {
                builder << "{}";
            }

            builder << ", ";
            builder << R"("ContraCalculatedCcyLastQty": )";
            if (writer.contraCalculatedCcyLastQtyInActingVersion())
            {
                builder << writer.contraCalculatedCcyLastQty();
            }
            else
            {
                builder << "{}";
            }

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

private:
    NoOrderEvents m_noOrderEvents;

public:
    SBE_NODISCARD static SBE_CONSTEXPR std::uint16_t NoOrderEventsId() SBE_NOEXCEPT
    {
        return 1795;
    }

    SBE_NODISCARD inline NoOrderEvents &noOrderEvents()
    {
        m_noOrderEvents.wrapForDecode(m_buffer, sbePositionPtr(), m_actingVersion, m_bufferLength);
        return m_noOrderEvents;
    }

    NoOrderEvents &noOrderEventsCount(const std::uint8_t count)
    {
        m_noOrderEvents.wrapForEncode(m_buffer, count, sbePositionPtr(), m_actingVersion, m_bufferLength);
        return m_noOrderEvents;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t noOrderEventsSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool noOrderEventsInActingVersion() const SBE_NOEXCEPT
    {
        return true;
    }

template<typename CharT, typename Traits>
friend std::basic_ostream<CharT, Traits> & operator << (
    std::basic_ostream<CharT, Traits> &builder, const ExecutionReportTradeOutright525 &_writer)
{
    ExecutionReportTradeOutright525 writer(
        _writer.m_buffer,
        _writer.m_offset,
        _writer.m_bufferLength,
        _writer.m_actingBlockLength,
        _writer.m_actingVersion);

    builder << '{';
    builder << R"("Name": "ExecutionReportTradeOutright525", )";
    builder << R"("sbeTemplateId": )";
    builder << writer.sbeTemplateId();
    builder << ", ";

    builder << R"("SeqNum": )";
    builder << +writer.seqNum();

    builder << ", ";
    builder << R"("UUID": )";
    builder << +writer.uUID();

    builder << ", ";
    builder << R"("ExecID": )";
    builder << '"' <<
        writer.getExecIDAsJsonEscapedString().c_str() << '"';

    builder << ", ";
    builder << R"("SenderID": )";
    builder << '"' <<
        writer.getSenderIDAsJsonEscapedString().c_str() << '"';

    builder << ", ";
    builder << R"("ClOrdID": )";
    builder << '"' <<
        writer.getClOrdIDAsJsonEscapedString().c_str() << '"';

    builder << ", ";
    builder << R"("PartyDetailsListReqID": )";
    builder << +writer.partyDetailsListReqID();

    builder << ", ";
    builder << R"("LastPx": )";
    builder << writer.lastPx();

    builder << ", ";
    builder << R"("OrderID": )";
    builder << +writer.orderID();

    builder << ", ";
    builder << R"("Price": )";
    builder << writer.price();

    builder << ", ";
    builder << R"("StopPx": )";
    builder << writer.stopPx();

    builder << ", ";
    builder << R"("TransactTime": )";
    builder << +writer.transactTime();

    builder << ", ";
    builder << R"("SendingTimeEpoch": )";
    builder << +writer.sendingTimeEpoch();

    builder << ", ";
    builder << R"("OrderRequestID": )";
    builder << +writer.orderRequestID();

    builder << ", ";
    builder << R"("SecExecID": )";
    builder << +writer.secExecID();

    builder << ", ";
    builder << R"("CrossID": )";
    builder << +writer.crossID();

    builder << ", ";
    builder << R"("HostCrossID": )";
    builder << +writer.hostCrossID();

    builder << ", ";
    builder << R"("Location": )";
    builder << '"' <<
        writer.getLocationAsJsonEscapedString().c_str() << '"';

    builder << ", ";
    builder << R"("SecurityID": )";
    builder << +writer.securityID();

    builder << ", ";
    builder << R"("OrderQty": )";
    builder << +writer.orderQty();

    builder << ", ";
    builder << R"("LastQty": )";
    builder << +writer.lastQty();

    builder << ", ";
    builder << R"("CumQty": )";
    builder << +writer.cumQty();

    builder << ", ";
    builder << R"("MDTradeEntryID": )";
    builder << +writer.mDTradeEntryID();

    builder << ", ";
    builder << R"("SideTradeID": )";
    builder << +writer.sideTradeID();

    builder << ", ";
    builder << R"("TradeLinkID": )";
    builder << +writer.tradeLinkID();

    builder << ", ";
    builder << R"("LeavesQty": )";
    builder << +writer.leavesQty();

    builder << ", ";
    builder << R"("TradeDate": )";
    builder << +writer.tradeDate();

    builder << ", ";
    builder << R"("ExpireDate": )";
    builder << +writer.expireDate();

    builder << ", ";
    builder << R"("OrdStatus": )";
    builder << '"' << writer.ordStatus() << '"';

    builder << ", ";
    builder << R"("OrdType": )";
    builder << '"' << writer.ordType() << '"';

    builder << ", ";
    builder << R"("Side": )";
    builder << '"' << writer.side() << '"';

    builder << ", ";
    builder << R"("TimeInForce": )";
    builder << '"' << writer.timeInForce() << '"';

    builder << ", ";
    builder << R"("ManualOrderIndicator": )";
    builder << '"' << writer.manualOrderIndicator() << '"';

    builder << ", ";
    builder << R"("PossRetransFlag": )";
    builder << '"' << writer.possRetransFlag() << '"';

    builder << ", ";
    builder << R"("AggressorIndicator": )";
    builder << '"' << writer.aggressorIndicator() << '"';

    builder << ", ";
    builder << R"("CrossType": )";
    builder << +writer.crossType();

    builder << ", ";
    builder << R"("ExecInst": )";
    builder << writer.execInst();

    builder << ", ";
    builder << R"("ExecutionMode": )";
    builder << '"' << writer.executionMode() << '"';

    builder << ", ";
    builder << R"("LiquidityFlag": )";
    builder << '"' << writer.liquidityFlag() << '"';

    builder << ", ";
    builder << R"("ManagedOrder": )";
    builder << '"' << writer.managedOrder() << '"';

    builder << ", ";
    builder << R"("ShortSaleType": )";
    builder << '"' << writer.shortSaleType() << '"';

    builder << ", ";
    builder << R"("Ownership": )";
    builder << +writer.ownership();

    builder << ", ";
    builder << R"("DiscretionPrice": )";
    if (writer.discretionPriceInActingVersion())
    {
        builder << writer.discretionPrice();
    }
    else
    {
        builder << "{}";
    }

    builder << ", ";
    builder << R"("TrdType": )";
    builder << +writer.trdType();

    builder << ", ";
    builder << R"("ExecRestatementReason": )";
    builder << '"' << writer.execRestatementReason() << '"';

    builder << ", ";
    builder << R"("SettlDate": )";
    builder << +writer.settlDate();

    builder << ", ";
    builder << R"("MaturityDate": )";
    builder << +writer.maturityDate();

    builder << ", ";
    builder << R"("CalculatedCcyLastQty": )";
    if (writer.calculatedCcyLastQtyInActingVersion())
    {
        builder << writer.calculatedCcyLastQty();
    }
    else
    {
        builder << "{}";
    }

    builder << ", ";
    builder << R"("GrossTradeAmt": )";
    if (writer.grossTradeAmtInActingVersion())
    {
        builder << writer.grossTradeAmt();
    }
    else
    {
        builder << "{}";
    }

    builder << ", ";
    builder << R"("BenchmarkPrice": )";
    if (writer.benchmarkPriceInActingVersion())
    {
        builder << writer.benchmarkPrice();
    }
    else
    {
        builder << "{}";
    }

    builder << ", ";
    builder << R"("ReservationPrice": )";
    if (writer.reservationPriceInActingVersion())
    {
        builder << writer.reservationPrice();
    }
    else
    {
        builder << "{}";
    }

    builder << ", ";
    builder << R"("PriorityIndicator": )";
    builder << +writer.priorityIndicator();

    builder << ", ";
    builder << R"("DisplayLimitPrice": )";
    if (writer.displayLimitPriceInActingVersion())
    {
        builder << writer.displayLimitPrice();
    }
    else
    {
        builder << "{}";
    }

    builder << ", ";
    {
        bool atLeastOne = false;
        builder << R"("NoFills": [)";
        writer.noFills().forEach(
            [&](NoFills &noFills)
            {
                if (atLeastOne)
                {
                    builder << ", ";
                }
                atLeastOne = true;
                builder << noFills;
            });
        builder << ']';
    }

    builder << ", ";
    {
        bool atLeastOne = false;
        builder << R"("NoOrderEvents": [)";
        writer.noOrderEvents().forEach(
            [&](NoOrderEvents &noOrderEvents)
            {
                if (atLeastOne)
                {
                    builder << ", ";
                }
                atLeastOne = true;
                builder << noOrderEvents;
            });
        builder << ']';
    }

    builder << '}';

    return builder;
}

void skip()
{
    auto &noFillsGroup { noFills() };
    while (noFillsGroup.hasNext())
    {
        noFillsGroup.next().skip();
    }
    auto &noOrderEventsGroup { noOrderEvents() };
    while (noOrderEventsGroup.hasNext())
    {
        noOrderEventsGroup.next().skip();
    }
}

SBE_NODISCARD static SBE_CONSTEXPR bool isConstLength() SBE_NOEXCEPT
{
    return false;
}

SBE_NODISCARD static std::size_t computeLength(
    std::size_t noFillsLength = 0,
    std::size_t noOrderEventsLength = 0)
{
#if defined(__GNUG__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wtype-limits"
#endif
    std::size_t length = sbeBlockLength();

    length += NoFills::sbeHeaderSize();
    if (noFillsLength > 254LL)
    {
        throw std::runtime_error("noFillsLength outside of allowed range [E110]");
    }
    length += noFillsLength *NoFills::sbeBlockLength();

    length += NoOrderEvents::sbeHeaderSize();
    if (noOrderEventsLength > 254LL)
    {
        throw std::runtime_error("noOrderEventsLength outside of allowed range [E110]");
    }
    length += noOrderEventsLength *NoOrderEvents::sbeBlockLength();

    return length;
#if defined(__GNUG__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
}
};
}
#endif
