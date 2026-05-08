/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/* Generated SBE (Simple Binary Encoding) message codec */
#ifndef _SBE_NEGOTIATE500_CXX_H_
#define _SBE_NEGOTIATE500_CXX_H_

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

class Negotiate500
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
    static const std::uint16_t SBE_BLOCK_LENGTH = static_cast<std::uint16_t>(76);
    static const std::uint16_t SBE_TEMPLATE_ID = static_cast<std::uint16_t>(500);
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

    Negotiate500() = default;

    Negotiate500(
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

    Negotiate500(char *buffer, const std::uint64_t bufferLength) :
        Negotiate500(buffer, 0, bufferLength, sbeBlockLength(), sbeSchemaVersion())
    {
    }

    Negotiate500(
        char *buffer,
        const std::uint64_t bufferLength,
        const std::uint64_t actingBlockLength,
        const std::uint64_t actingVersion) :
        Negotiate500(buffer, 0, bufferLength, actingBlockLength, actingVersion)
    {
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint16_t sbeBlockLength() SBE_NOEXCEPT
    {
        return static_cast<std::uint16_t>(76);
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t sbeBlockAndHeaderLength() SBE_NOEXCEPT
    {
        return messageHeader::encodedLength() + sbeBlockLength();
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint16_t sbeTemplateId() SBE_NOEXCEPT
    {
        return static_cast<std::uint16_t>(500);
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
        return "Negotiate";
    }

    SBE_NODISCARD std::uint64_t offset() const SBE_NOEXCEPT
    {
        return m_offset;
    }

    Negotiate500 &wrapForEncode(char *buffer, const std::uint64_t offset, const std::uint64_t bufferLength)
    {
        return *this = Negotiate500(buffer, offset, bufferLength, sbeBlockLength(), sbeSchemaVersion());
    }

    Negotiate500 &wrapAndApplyHeader(char *buffer, const std::uint64_t offset, const std::uint64_t bufferLength)
    {
        messageHeader hdr(buffer, offset, bufferLength, sbeSchemaVersion());

        hdr
            .blockLength(sbeBlockLength())
            .templateId(sbeTemplateId())
            .schemaId(sbeSchemaId())
            .version(sbeSchemaVersion());

        return *this = Negotiate500(
            buffer,
            offset + messageHeader::encodedLength(),
            bufferLength,
            sbeBlockLength(),
            sbeSchemaVersion());
    }

    Negotiate500 &wrapForDecode(
        char *buffer,
        const std::uint64_t offset,
        const std::uint64_t actingBlockLength,
        const std::uint64_t actingVersion,
        const std::uint64_t bufferLength)
    {
        return *this = Negotiate500(buffer, offset, bufferLength, actingBlockLength, actingVersion);
    }

    Negotiate500 &sbeRewind()
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
        Negotiate500 skipper(m_buffer, m_offset, m_bufferLength, sbeBlockLength(), m_actingVersion);
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

    SBE_NODISCARD static const char *CustomerFlowMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "String";
            case MetaAttribute::PRESENCE: return "constant";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t customerFlowId() SBE_NOEXCEPT
    {
        return 39000;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t customerFlowSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool customerFlowInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t customerFlowEncodingOffset() SBE_NOEXCEPT
    {
        return 0;
    }

    static SBE_CONSTEXPR char customerFlowNullValue() SBE_NOEXCEPT
    {
        return static_cast<char>(0);
    }

    static SBE_CONSTEXPR char customerFlowMinValue() SBE_NOEXCEPT
    {
        return static_cast<char>(32);
    }

    static SBE_CONSTEXPR char customerFlowMaxValue() SBE_NOEXCEPT
    {
        return static_cast<char>(126);
    }

    static SBE_CONSTEXPR std::size_t customerFlowEncodingLength() SBE_NOEXCEPT
    {
        return 0;
    }

    static SBE_CONSTEXPR std::uint64_t customerFlowLength() SBE_NOEXCEPT
    {
        return 10;
    }

    SBE_NODISCARD const char *customerFlow() const
    {
        static const std::uint8_t customerFlowValues[] = { 73, 68, 69, 77, 80, 79, 84, 69, 78, 84, 0 };

        return (const char *)customerFlowValues;
    }

    SBE_NODISCARD char customerFlow(const std::uint64_t index) const
    {
        static const std::uint8_t customerFlowValues[] = { 73, 68, 69, 77, 80, 79, 84, 69, 78, 84, 0 };

        return (char)customerFlowValues[index];
    }

    std::uint64_t getCustomerFlow(char *dst, const std::uint64_t length) const
    {
        static std::uint8_t customerFlowValues[] = { 73, 68, 69, 77, 80, 79, 84, 69, 78, 84 };
        std::uint64_t bytesToCopy = length < sizeof(customerFlowValues) ? length : sizeof(customerFlowValues);

        std::memcpy(dst, customerFlowValues, static_cast<std::size_t>(bytesToCopy));
        return bytesToCopy;
    }

    std::string getCustomerFlowAsString() const
    {
        static const std::uint8_t CustomerFlowValues[] = { 73, 68, 69, 77, 80, 79, 84, 69, 78, 84 };

        return std::string((const char *)CustomerFlowValues, 10);
    }

    std::string getCustomerFlowAsJsonEscapedString()
    {
        std::ostringstream oss;
        std::string s = getCustomerFlowAsString();

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

    SBE_NODISCARD static const char *HMACVersionMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "String";
            case MetaAttribute::PRESENCE: return "constant";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t hMACVersionId() SBE_NOEXCEPT
    {
        return 39003;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t hMACVersionSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool hMACVersionInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t hMACVersionEncodingOffset() SBE_NOEXCEPT
    {
        return 0;
    }

    static SBE_CONSTEXPR char hMACVersionNullValue() SBE_NOEXCEPT
    {
        return static_cast<char>(0);
    }

    static SBE_CONSTEXPR char hMACVersionMinValue() SBE_NOEXCEPT
    {
        return static_cast<char>(32);
    }

    static SBE_CONSTEXPR char hMACVersionMaxValue() SBE_NOEXCEPT
    {
        return static_cast<char>(126);
    }

    static SBE_CONSTEXPR std::size_t hMACVersionEncodingLength() SBE_NOEXCEPT
    {
        return 0;
    }

    static SBE_CONSTEXPR std::uint64_t hMACVersionLength() SBE_NOEXCEPT
    {
        return 13;
    }

    SBE_NODISCARD const char *hMACVersion() const
    {
        static const std::uint8_t hMACVersionValues[] = { 67, 77, 69, 45, 49, 45, 83, 72, 65, 45, 50, 53, 54, 0 };

        return (const char *)hMACVersionValues;
    }

    SBE_NODISCARD char hMACVersion(const std::uint64_t index) const
    {
        static const std::uint8_t hMACVersionValues[] = { 67, 77, 69, 45, 49, 45, 83, 72, 65, 45, 50, 53, 54, 0 };

        return (char)hMACVersionValues[index];
    }

    std::uint64_t getHMACVersion(char *dst, const std::uint64_t length) const
    {
        static std::uint8_t hMACVersionValues[] = { 67, 77, 69, 45, 49, 45, 83, 72, 65, 45, 50, 53, 54 };
        std::uint64_t bytesToCopy = length < sizeof(hMACVersionValues) ? length : sizeof(hMACVersionValues);

        std::memcpy(dst, hMACVersionValues, static_cast<std::size_t>(bytesToCopy));
        return bytesToCopy;
    }

    std::string getHMACVersionAsString() const
    {
        static const std::uint8_t HMACVersionValues[] = { 67, 77, 69, 45, 49, 45, 83, 72, 65, 45, 50, 53, 54 };

        return std::string((const char *)HMACVersionValues, 13);
    }

    std::string getHMACVersionAsJsonEscapedString()
    {
        std::ostringstream oss;
        std::string s = getHMACVersionAsString();

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

    SBE_NODISCARD static const char *HMACSignatureMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "String";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t hMACSignatureId() SBE_NOEXCEPT
    {
        return 39005;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t hMACSignatureSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool hMACSignatureInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t hMACSignatureEncodingOffset() SBE_NOEXCEPT
    {
        return 0;
    }

    static SBE_CONSTEXPR char hMACSignatureNullValue() SBE_NOEXCEPT
    {
        return static_cast<char>(0);
    }

    static SBE_CONSTEXPR char hMACSignatureMinValue() SBE_NOEXCEPT
    {
        return static_cast<char>(32);
    }

    static SBE_CONSTEXPR char hMACSignatureMaxValue() SBE_NOEXCEPT
    {
        return static_cast<char>(126);
    }

    static SBE_CONSTEXPR std::size_t hMACSignatureEncodingLength() SBE_NOEXCEPT
    {
        return 32;
    }

    static SBE_CONSTEXPR std::uint64_t hMACSignatureLength() SBE_NOEXCEPT
    {
        return 32;
    }

    SBE_NODISCARD const char *hMACSignature() const SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 0;
    }

    SBE_NODISCARD char *hMACSignature() SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 0;
    }

    SBE_NODISCARD char hMACSignature(const std::uint64_t index) const
    {
        if (index >= 32)
        {
            throw std::runtime_error("index out of range for hMACSignature [E104]");
        }

        char val;
        std::memcpy(&val, m_buffer + m_offset + 0 + (index * 1), sizeof(char));
        return (val);
    }

    Negotiate500 &hMACSignature(const std::uint64_t index, const char value)
    {
        if (index >= 32)
        {
            throw std::runtime_error("index out of range for hMACSignature [E105]");
        }

        char val = (value);
        std::memcpy(m_buffer + m_offset + 0 + (index * 1), &val, sizeof(char));
        return *this;
    }

    std::uint64_t getHMACSignature(char *const dst, const std::uint64_t length) const
    {
        if (length > 32)
        {
            throw std::runtime_error("length too large for getHMACSignature [E106]");
        }

        std::memcpy(dst, m_buffer + m_offset + 0, sizeof(char) * static_cast<std::size_t>(length));
        return length;
    }

    Negotiate500 &putHMACSignature(const char *const src) SBE_NOEXCEPT
    {
        std::memcpy(m_buffer + m_offset + 0, src, sizeof(char) * 32);
        return *this;
    }

    SBE_NODISCARD std::string getHMACSignatureAsString() const
    {
        const char *buffer = m_buffer + m_offset + 0;
        std::size_t length = 0;

        for (; length < 32 && *(buffer + length) != '\0'; ++length);
        std::string result(buffer, length);

        return result;
    }

    std::string getHMACSignatureAsJsonEscapedString()
    {
        std::ostringstream oss;
        std::string s = getHMACSignatureAsString();

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
    SBE_NODISCARD std::string_view getHMACSignatureAsStringView() const SBE_NOEXCEPT
    {
        const char *buffer = m_buffer + m_offset + 0;
        std::size_t length = 0;

        for (; length < 32 && *(buffer + length) != '\0'; ++length);
        std::string_view result(buffer, length);

        return result;
    }
    #endif

    #if __cplusplus >= 201703L
    Negotiate500 &putHMACSignature(const std::string_view str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 32)
        {
            throw std::runtime_error("string too large for putHMACSignature [E106]");
        }

        std::memcpy(m_buffer + m_offset + 0, str.data(), srcLength);
        for (std::size_t start = srcLength; start < 32; ++start)
        {
            m_buffer[m_offset + 0 + start] = 0;
        }

        return *this;
    }
    #else
    Negotiate500 &putHMACSignature(const std::string &str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 32)
        {
            throw std::runtime_error("string too large for putHMACSignature [E106]");
        }

        std::memcpy(m_buffer + m_offset + 0, str.c_str(), srcLength);
        for (std::size_t start = srcLength; start < 32; ++start)
        {
            m_buffer[m_offset + 0 + start] = 0;
        }

        return *this;
    }
    #endif

    SBE_NODISCARD static const char *AccessKeyIDMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "String";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t accessKeyIDId() SBE_NOEXCEPT
    {
        return 39004;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t accessKeyIDSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool accessKeyIDInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t accessKeyIDEncodingOffset() SBE_NOEXCEPT
    {
        return 32;
    }

    static SBE_CONSTEXPR char accessKeyIDNullValue() SBE_NOEXCEPT
    {
        return static_cast<char>(0);
    }

    static SBE_CONSTEXPR char accessKeyIDMinValue() SBE_NOEXCEPT
    {
        return static_cast<char>(32);
    }

    static SBE_CONSTEXPR char accessKeyIDMaxValue() SBE_NOEXCEPT
    {
        return static_cast<char>(126);
    }

    static SBE_CONSTEXPR std::size_t accessKeyIDEncodingLength() SBE_NOEXCEPT
    {
        return 20;
    }

    static SBE_CONSTEXPR std::uint64_t accessKeyIDLength() SBE_NOEXCEPT
    {
        return 20;
    }

    SBE_NODISCARD const char *accessKeyID() const SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 32;
    }

    SBE_NODISCARD char *accessKeyID() SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 32;
    }

    SBE_NODISCARD char accessKeyID(const std::uint64_t index) const
    {
        if (index >= 20)
        {
            throw std::runtime_error("index out of range for accessKeyID [E104]");
        }

        char val;
        std::memcpy(&val, m_buffer + m_offset + 32 + (index * 1), sizeof(char));
        return (val);
    }

    Negotiate500 &accessKeyID(const std::uint64_t index, const char value)
    {
        if (index >= 20)
        {
            throw std::runtime_error("index out of range for accessKeyID [E105]");
        }

        char val = (value);
        std::memcpy(m_buffer + m_offset + 32 + (index * 1), &val, sizeof(char));
        return *this;
    }

    std::uint64_t getAccessKeyID(char *const dst, const std::uint64_t length) const
    {
        if (length > 20)
        {
            throw std::runtime_error("length too large for getAccessKeyID [E106]");
        }

        std::memcpy(dst, m_buffer + m_offset + 32, sizeof(char) * static_cast<std::size_t>(length));
        return length;
    }

    Negotiate500 &putAccessKeyID(const char *const src) SBE_NOEXCEPT
    {
        std::memcpy(m_buffer + m_offset + 32, src, sizeof(char) * 20);
        return *this;
    }

    SBE_NODISCARD std::string getAccessKeyIDAsString() const
    {
        const char *buffer = m_buffer + m_offset + 32;
        std::size_t length = 0;

        for (; length < 20 && *(buffer + length) != '\0'; ++length);
        std::string result(buffer, length);

        return result;
    }

    std::string getAccessKeyIDAsJsonEscapedString()
    {
        std::ostringstream oss;
        std::string s = getAccessKeyIDAsString();

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
    SBE_NODISCARD std::string_view getAccessKeyIDAsStringView() const SBE_NOEXCEPT
    {
        const char *buffer = m_buffer + m_offset + 32;
        std::size_t length = 0;

        for (; length < 20 && *(buffer + length) != '\0'; ++length);
        std::string_view result(buffer, length);

        return result;
    }
    #endif

    #if __cplusplus >= 201703L
    Negotiate500 &putAccessKeyID(const std::string_view str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 20)
        {
            throw std::runtime_error("string too large for putAccessKeyID [E106]");
        }

        std::memcpy(m_buffer + m_offset + 32, str.data(), srcLength);
        for (std::size_t start = srcLength; start < 20; ++start)
        {
            m_buffer[m_offset + 32 + start] = 0;
        }

        return *this;
    }
    #else
    Negotiate500 &putAccessKeyID(const std::string &str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 20)
        {
            throw std::runtime_error("string too large for putAccessKeyID [E106]");
        }

        std::memcpy(m_buffer + m_offset + 32, str.c_str(), srcLength);
        for (std::size_t start = srcLength; start < 20; ++start)
        {
            m_buffer[m_offset + 32 + start] = 0;
        }

        return *this;
    }
    #endif

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
        return 52;
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
        std::memcpy(&val, m_buffer + m_offset + 52, sizeof(std::uint64_t));
        return SBE_LITTLE_ENDIAN_ENCODE_64(val);
    }

    Negotiate500 &uUID(const std::uint64_t value) SBE_NOEXCEPT
    {
        std::uint64_t val = SBE_LITTLE_ENDIAN_ENCODE_64(value);
        std::memcpy(m_buffer + m_offset + 52, &val, sizeof(std::uint64_t));
        return *this;
    }

    SBE_NODISCARD static const char *RequestTimestampMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t requestTimestampId() SBE_NOEXCEPT
    {
        return 39002;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t requestTimestampSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool requestTimestampInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t requestTimestampEncodingOffset() SBE_NOEXCEPT
    {
        return 60;
    }

    static SBE_CONSTEXPR std::uint64_t requestTimestampNullValue() SBE_NOEXCEPT
    {
        return SBE_NULLVALUE_UINT64;
    }

    static SBE_CONSTEXPR std::uint64_t requestTimestampMinValue() SBE_NOEXCEPT
    {
        return UINT64_C(0x0);
    }

    static SBE_CONSTEXPR std::uint64_t requestTimestampMaxValue() SBE_NOEXCEPT
    {
        return UINT64_C(0xfffffffffffffffe);
    }

    static SBE_CONSTEXPR std::size_t requestTimestampEncodingLength() SBE_NOEXCEPT
    {
        return 8;
    }

    SBE_NODISCARD std::uint64_t requestTimestamp() const SBE_NOEXCEPT
    {
        std::uint64_t val;
        std::memcpy(&val, m_buffer + m_offset + 60, sizeof(std::uint64_t));
        return SBE_LITTLE_ENDIAN_ENCODE_64(val);
    }

    Negotiate500 &requestTimestamp(const std::uint64_t value) SBE_NOEXCEPT
    {
        std::uint64_t val = SBE_LITTLE_ENDIAN_ENCODE_64(value);
        std::memcpy(m_buffer + m_offset + 60, &val, sizeof(std::uint64_t));
        return *this;
    }

    SBE_NODISCARD static const char *SessionMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "String";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t sessionId() SBE_NOEXCEPT
    {
        return 39006;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t sessionSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool sessionInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t sessionEncodingOffset() SBE_NOEXCEPT
    {
        return 68;
    }

    static SBE_CONSTEXPR char sessionNullValue() SBE_NOEXCEPT
    {
        return static_cast<char>(0);
    }

    static SBE_CONSTEXPR char sessionMinValue() SBE_NOEXCEPT
    {
        return static_cast<char>(32);
    }

    static SBE_CONSTEXPR char sessionMaxValue() SBE_NOEXCEPT
    {
        return static_cast<char>(126);
    }

    static SBE_CONSTEXPR std::size_t sessionEncodingLength() SBE_NOEXCEPT
    {
        return 3;
    }

    static SBE_CONSTEXPR std::uint64_t sessionLength() SBE_NOEXCEPT
    {
        return 3;
    }

    SBE_NODISCARD const char *session() const SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 68;
    }

    SBE_NODISCARD char *session() SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 68;
    }

    SBE_NODISCARD char session(const std::uint64_t index) const
    {
        if (index >= 3)
        {
            throw std::runtime_error("index out of range for session [E104]");
        }

        char val;
        std::memcpy(&val, m_buffer + m_offset + 68 + (index * 1), sizeof(char));
        return (val);
    }

    Negotiate500 &session(const std::uint64_t index, const char value)
    {
        if (index >= 3)
        {
            throw std::runtime_error("index out of range for session [E105]");
        }

        char val = (value);
        std::memcpy(m_buffer + m_offset + 68 + (index * 1), &val, sizeof(char));
        return *this;
    }

    std::uint64_t getSession(char *const dst, const std::uint64_t length) const
    {
        if (length > 3)
        {
            throw std::runtime_error("length too large for getSession [E106]");
        }

        std::memcpy(dst, m_buffer + m_offset + 68, sizeof(char) * static_cast<std::size_t>(length));
        return length;
    }

    Negotiate500 &putSession(const char *const src) SBE_NOEXCEPT
    {
        std::memcpy(m_buffer + m_offset + 68, src, sizeof(char) * 3);
        return *this;
    }

    Negotiate500 &putSession(
        const char value0,
        const char value1,
        const char value2) SBE_NOEXCEPT
    {
        char val0 = (value0);
        std::memcpy(m_buffer + m_offset + 68, &val0, sizeof(char));
        char val1 = (value1);
        std::memcpy(m_buffer + m_offset + 69, &val1, sizeof(char));
        char val2 = (value2);
        std::memcpy(m_buffer + m_offset + 70, &val2, sizeof(char));

        return *this;
    }

    SBE_NODISCARD std::string getSessionAsString() const
    {
        const char *buffer = m_buffer + m_offset + 68;
        std::size_t length = 0;

        for (; length < 3 && *(buffer + length) != '\0'; ++length);
        std::string result(buffer, length);

        return result;
    }

    std::string getSessionAsJsonEscapedString()
    {
        std::ostringstream oss;
        std::string s = getSessionAsString();

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
    SBE_NODISCARD std::string_view getSessionAsStringView() const SBE_NOEXCEPT
    {
        const char *buffer = m_buffer + m_offset + 68;
        std::size_t length = 0;

        for (; length < 3 && *(buffer + length) != '\0'; ++length);
        std::string_view result(buffer, length);

        return result;
    }
    #endif

    #if __cplusplus >= 201703L
    Negotiate500 &putSession(const std::string_view str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 3)
        {
            throw std::runtime_error("string too large for putSession [E106]");
        }

        std::memcpy(m_buffer + m_offset + 68, str.data(), srcLength);
        for (std::size_t start = srcLength; start < 3; ++start)
        {
            m_buffer[m_offset + 68 + start] = 0;
        }

        return *this;
    }
    #else
    Negotiate500 &putSession(const std::string &str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 3)
        {
            throw std::runtime_error("string too large for putSession [E106]");
        }

        std::memcpy(m_buffer + m_offset + 68, str.c_str(), srcLength);
        for (std::size_t start = srcLength; start < 3; ++start)
        {
            m_buffer[m_offset + 68 + start] = 0;
        }

        return *this;
    }
    #endif

    SBE_NODISCARD static const char *FirmMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "String";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t firmId() SBE_NOEXCEPT
    {
        return 39007;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t firmSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool firmInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t firmEncodingOffset() SBE_NOEXCEPT
    {
        return 71;
    }

    static SBE_CONSTEXPR char firmNullValue() SBE_NOEXCEPT
    {
        return static_cast<char>(0);
    }

    static SBE_CONSTEXPR char firmMinValue() SBE_NOEXCEPT
    {
        return static_cast<char>(32);
    }

    static SBE_CONSTEXPR char firmMaxValue() SBE_NOEXCEPT
    {
        return static_cast<char>(126);
    }

    static SBE_CONSTEXPR std::size_t firmEncodingLength() SBE_NOEXCEPT
    {
        return 5;
    }

    static SBE_CONSTEXPR std::uint64_t firmLength() SBE_NOEXCEPT
    {
        return 5;
    }

    SBE_NODISCARD const char *firm() const SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 71;
    }

    SBE_NODISCARD char *firm() SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 71;
    }

    SBE_NODISCARD char firm(const std::uint64_t index) const
    {
        if (index >= 5)
        {
            throw std::runtime_error("index out of range for firm [E104]");
        }

        char val;
        std::memcpy(&val, m_buffer + m_offset + 71 + (index * 1), sizeof(char));
        return (val);
    }

    Negotiate500 &firm(const std::uint64_t index, const char value)
    {
        if (index >= 5)
        {
            throw std::runtime_error("index out of range for firm [E105]");
        }

        char val = (value);
        std::memcpy(m_buffer + m_offset + 71 + (index * 1), &val, sizeof(char));
        return *this;
    }

    std::uint64_t getFirm(char *const dst, const std::uint64_t length) const
    {
        if (length > 5)
        {
            throw std::runtime_error("length too large for getFirm [E106]");
        }

        std::memcpy(dst, m_buffer + m_offset + 71, sizeof(char) * static_cast<std::size_t>(length));
        return length;
    }

    Negotiate500 &putFirm(const char *const src) SBE_NOEXCEPT
    {
        std::memcpy(m_buffer + m_offset + 71, src, sizeof(char) * 5);
        return *this;
    }

    SBE_NODISCARD std::string getFirmAsString() const
    {
        const char *buffer = m_buffer + m_offset + 71;
        std::size_t length = 0;

        for (; length < 5 && *(buffer + length) != '\0'; ++length);
        std::string result(buffer, length);

        return result;
    }

    std::string getFirmAsJsonEscapedString()
    {
        std::ostringstream oss;
        std::string s = getFirmAsString();

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
    SBE_NODISCARD std::string_view getFirmAsStringView() const SBE_NOEXCEPT
    {
        const char *buffer = m_buffer + m_offset + 71;
        std::size_t length = 0;

        for (; length < 5 && *(buffer + length) != '\0'; ++length);
        std::string_view result(buffer, length);

        return result;
    }
    #endif

    #if __cplusplus >= 201703L
    Negotiate500 &putFirm(const std::string_view str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 5)
        {
            throw std::runtime_error("string too large for putFirm [E106]");
        }

        std::memcpy(m_buffer + m_offset + 71, str.data(), srcLength);
        for (std::size_t start = srcLength; start < 5; ++start)
        {
            m_buffer[m_offset + 71 + start] = 0;
        }

        return *this;
    }
    #else
    Negotiate500 &putFirm(const std::string &str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 5)
        {
            throw std::runtime_error("string too large for putFirm [E106]");
        }

        std::memcpy(m_buffer + m_offset + 71, str.c_str(), srcLength);
        for (std::size_t start = srcLength; start < 5; ++start)
        {
            m_buffer[m_offset + 71 + start] = 0;
        }

        return *this;
    }
    #endif

    SBE_NODISCARD static const char *CredentialsMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "data";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static const char *credentialsCharacterEncoding() SBE_NOEXCEPT
    {
        return "US-ASCII";
    }

    static SBE_CONSTEXPR std::uint64_t credentialsSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    bool credentialsInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    static SBE_CONSTEXPR std::uint16_t credentialsId() SBE_NOEXCEPT
    {
        return 39008;
    }

    static SBE_CONSTEXPR std::uint64_t credentialsHeaderLength() SBE_NOEXCEPT
    {
        return 2;
    }

    SBE_NODISCARD std::uint16_t credentialsLength() const
    {
        std::uint16_t length;
        std::memcpy(&length, m_buffer + sbePosition(), sizeof(std::uint16_t));
        return SBE_LITTLE_ENDIAN_ENCODE_16(length);
    }

    std::uint64_t skipCredentials()
    {
        std::uint64_t lengthOfLengthField = 2;
        std::uint64_t lengthPosition = sbePosition();
        std::uint16_t lengthFieldValue;
        std::memcpy(&lengthFieldValue, m_buffer + lengthPosition, sizeof(std::uint16_t));
        std::uint64_t dataLength = SBE_LITTLE_ENDIAN_ENCODE_16(lengthFieldValue);
        sbePosition(lengthPosition + lengthOfLengthField + dataLength);
        return dataLength;
    }

    SBE_NODISCARD const char *credentials()
    {
        std::uint16_t lengthFieldValue;
        std::memcpy(&lengthFieldValue, m_buffer + sbePosition(), sizeof(std::uint16_t));
        const char *fieldPtr = m_buffer + sbePosition() + 2;
        sbePosition(sbePosition() + 2 + SBE_LITTLE_ENDIAN_ENCODE_16(lengthFieldValue));
        return fieldPtr;
    }

    std::uint64_t getCredentials(char *dst, const std::uint64_t length)
    {
        std::uint64_t lengthOfLengthField = 2;
        std::uint64_t lengthPosition = sbePosition();
        sbePosition(lengthPosition + lengthOfLengthField);
        std::uint16_t lengthFieldValue;
        std::memcpy(&lengthFieldValue, m_buffer + lengthPosition, sizeof(std::uint16_t));
        std::uint64_t dataLength = SBE_LITTLE_ENDIAN_ENCODE_16(lengthFieldValue);
        std::uint64_t bytesToCopy = length < dataLength ? length : dataLength;
        std::uint64_t pos = sbePosition();
        sbePosition(pos + dataLength);
        std::memcpy(dst, m_buffer + pos, static_cast<std::size_t>(bytesToCopy));
        return bytesToCopy;
    }

    Negotiate500 &putCredentials(const char *src, const std::uint16_t length)
    {
        std::uint64_t lengthOfLengthField = 2;
        std::uint64_t lengthPosition = sbePosition();
        std::uint16_t lengthFieldValue = SBE_LITTLE_ENDIAN_ENCODE_16(length);
        sbePosition(lengthPosition + lengthOfLengthField);
        std::memcpy(m_buffer + lengthPosition, &lengthFieldValue, sizeof(std::uint16_t));
        if (length != std::uint16_t(0))
        {
            std::uint64_t pos = sbePosition();
            sbePosition(pos + length);
            std::memcpy(m_buffer + pos, src, length);
        }
        return *this;
    }

    std::string getCredentialsAsString()
    {
        std::uint64_t lengthOfLengthField = 2;
        std::uint64_t lengthPosition = sbePosition();
        sbePosition(lengthPosition + lengthOfLengthField);
        std::uint16_t lengthFieldValue;
        std::memcpy(&lengthFieldValue, m_buffer + lengthPosition, sizeof(std::uint16_t));
        std::uint64_t dataLength = SBE_LITTLE_ENDIAN_ENCODE_16(lengthFieldValue);
        std::uint64_t pos = sbePosition();
        const std::string result(m_buffer + pos, dataLength);
        sbePosition(pos + dataLength);
        return result;
    }

    std::string getCredentialsAsJsonEscapedString()
    {
        std::ostringstream oss;
        std::string s = getCredentialsAsString();

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
    std::string_view getCredentialsAsStringView()
    {
        std::uint64_t lengthOfLengthField = 2;
        std::uint64_t lengthPosition = sbePosition();
        sbePosition(lengthPosition + lengthOfLengthField);
        std::uint16_t lengthFieldValue;
        std::memcpy(&lengthFieldValue, m_buffer + lengthPosition, sizeof(std::uint16_t));
        std::uint64_t dataLength = SBE_LITTLE_ENDIAN_ENCODE_16(lengthFieldValue);
        std::uint64_t pos = sbePosition();
        const std::string_view result(m_buffer + pos, dataLength);
        sbePosition(pos + dataLength);
        return result;
    }
    #endif

    Negotiate500 &putCredentials(const std::string &str)
    {
        if (str.length() > 65534)
        {
            throw std::runtime_error("std::string too long for length type [E109]");
        }
        return putCredentials(str.data(), static_cast<std::uint16_t>(str.length()));
    }

    #if __cplusplus >= 201703L
    Negotiate500 &putCredentials(const std::string_view str)
    {
        if (str.length() > 65534)
        {
            throw std::runtime_error("std::string too long for length type [E109]");
        }
        return putCredentials(str.data(), static_cast<std::uint16_t>(str.length()));
    }
    #endif

template<typename CharT, typename Traits>
friend std::basic_ostream<CharT, Traits> & operator << (
    std::basic_ostream<CharT, Traits> &builder, const Negotiate500 &_writer)
{
    Negotiate500 writer(
        _writer.m_buffer,
        _writer.m_offset,
        _writer.m_bufferLength,
        _writer.m_actingBlockLength,
        _writer.m_actingVersion);

    builder << '{';
    builder << R"("Name": "Negotiate500", )";
    builder << R"("sbeTemplateId": )";
    builder << writer.sbeTemplateId();
    builder << ", ";

    builder << R"("HMACSignature": )";
    builder << '"' <<
        writer.getHMACSignatureAsJsonEscapedString().c_str() << '"';

    builder << ", ";
    builder << R"("AccessKeyID": )";
    builder << '"' <<
        writer.getAccessKeyIDAsJsonEscapedString().c_str() << '"';

    builder << ", ";
    builder << R"("UUID": )";
    builder << +writer.uUID();

    builder << ", ";
    builder << R"("RequestTimestamp": )";
    builder << +writer.requestTimestamp();

    builder << ", ";
    builder << R"("Session": )";
    builder << '"' <<
        writer.getSessionAsJsonEscapedString().c_str() << '"';

    builder << ", ";
    builder << R"("Firm": )";
    builder << '"' <<
        writer.getFirmAsJsonEscapedString().c_str() << '"';

    builder << ", ";
    builder << R"("Credentials": )";
    builder << '"' <<
        writer.getCredentialsAsJsonEscapedString().c_str() << '"';

    builder << '}';

    return builder;
}

void skip()
{
    skipCredentials();
}

SBE_NODISCARD static SBE_CONSTEXPR bool isConstLength() SBE_NOEXCEPT
{
    return false;
}

SBE_NODISCARD static std::size_t computeLength(std::size_t credentialsLength = 0)
{
#if defined(__GNUG__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wtype-limits"
#endif
    std::size_t length = sbeBlockLength();

    length += credentialsHeaderLength();
    if (credentialsLength > 65534LL)
    {
        throw std::runtime_error("credentialsLength too long for length type [E109]");
    }
    length += credentialsLength;

    return length;
#if defined(__GNUG__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
}
};
}
#endif
