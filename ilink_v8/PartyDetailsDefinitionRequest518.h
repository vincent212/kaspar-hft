/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/* Generated SBE (Simple Binary Encoding) message codec */
#ifndef _SBE_PARTYDETAILSDEFINITIONREQUEST518_CXX_H_
#define _SBE_PARTYDETAILSDEFINITIONREQUEST518_CXX_H_

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

class PartyDetailsDefinitionRequest518
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
    static const std::uint16_t SBE_BLOCK_LENGTH = static_cast<std::uint16_t>(147);
    static const std::uint16_t SBE_TEMPLATE_ID = static_cast<std::uint16_t>(518);
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

    PartyDetailsDefinitionRequest518() = default;

    PartyDetailsDefinitionRequest518(
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

    PartyDetailsDefinitionRequest518(char *buffer, const std::uint64_t bufferLength) :
        PartyDetailsDefinitionRequest518(buffer, 0, bufferLength, sbeBlockLength(), sbeSchemaVersion())
    {
    }

    PartyDetailsDefinitionRequest518(
        char *buffer,
        const std::uint64_t bufferLength,
        const std::uint64_t actingBlockLength,
        const std::uint64_t actingVersion) :
        PartyDetailsDefinitionRequest518(buffer, 0, bufferLength, actingBlockLength, actingVersion)
    {
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint16_t sbeBlockLength() SBE_NOEXCEPT
    {
        return static_cast<std::uint16_t>(147);
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t sbeBlockAndHeaderLength() SBE_NOEXCEPT
    {
        return messageHeader::encodedLength() + sbeBlockLength();
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint16_t sbeTemplateId() SBE_NOEXCEPT
    {
        return static_cast<std::uint16_t>(518);
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
        return "CX";
    }

    SBE_NODISCARD std::uint64_t offset() const SBE_NOEXCEPT
    {
        return m_offset;
    }

    PartyDetailsDefinitionRequest518 &wrapForEncode(char *buffer, const std::uint64_t offset, const std::uint64_t bufferLength)
    {
        return *this = PartyDetailsDefinitionRequest518(buffer, offset, bufferLength, sbeBlockLength(), sbeSchemaVersion());
    }

    PartyDetailsDefinitionRequest518 &wrapAndApplyHeader(char *buffer, const std::uint64_t offset, const std::uint64_t bufferLength)
    {
        messageHeader hdr(buffer, offset, bufferLength, sbeSchemaVersion());

        hdr
            .blockLength(sbeBlockLength())
            .templateId(sbeTemplateId())
            .schemaId(sbeSchemaId())
            .version(sbeSchemaVersion());

        return *this = PartyDetailsDefinitionRequest518(
            buffer,
            offset + messageHeader::encodedLength(),
            bufferLength,
            sbeBlockLength(),
            sbeSchemaVersion());
    }

    PartyDetailsDefinitionRequest518 &wrapForDecode(
        char *buffer,
        const std::uint64_t offset,
        const std::uint64_t actingBlockLength,
        const std::uint64_t actingVersion,
        const std::uint64_t bufferLength)
    {
        return *this = PartyDetailsDefinitionRequest518(buffer, offset, bufferLength, actingBlockLength, actingVersion);
    }

    PartyDetailsDefinitionRequest518 &sbeRewind()
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
        PartyDetailsDefinitionRequest518 skipper(m_buffer, m_offset, m_bufferLength, sbeBlockLength(), m_actingVersion);
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
        return 0;
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
        std::memcpy(&val, m_buffer + m_offset + 0, sizeof(std::uint64_t));
        return SBE_LITTLE_ENDIAN_ENCODE_64(val);
    }

    PartyDetailsDefinitionRequest518 &partyDetailsListReqID(const std::uint64_t value) SBE_NOEXCEPT
    {
        std::uint64_t val = SBE_LITTLE_ENDIAN_ENCODE_64(value);
        std::memcpy(m_buffer + m_offset + 0, &val, sizeof(std::uint64_t));
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
        return 8;
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
        std::memcpy(&val, m_buffer + m_offset + 8, sizeof(std::uint64_t));
        return SBE_LITTLE_ENDIAN_ENCODE_64(val);
    }

    PartyDetailsDefinitionRequest518 &sendingTimeEpoch(const std::uint64_t value) SBE_NOEXCEPT
    {
        std::uint64_t val = SBE_LITTLE_ENDIAN_ENCODE_64(value);
        std::memcpy(m_buffer + m_offset + 8, &val, sizeof(std::uint64_t));
        return *this;
    }

    SBE_NODISCARD static const char *ListUpdateActionMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "char";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t listUpdateActionId() SBE_NOEXCEPT
    {
        return 1324;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t listUpdateActionSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool listUpdateActionInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t listUpdateActionEncodingOffset() SBE_NOEXCEPT
    {
        return 16;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t listUpdateActionEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD char listUpdateActionRaw() const SBE_NOEXCEPT
    {
        char val;
        std::memcpy(&val, m_buffer + m_offset + 16, sizeof(char));
        return (val);
    }

    SBE_NODISCARD ListUpdAct::Value listUpdateAction() const
    {
        char val;
        std::memcpy(&val, m_buffer + m_offset + 16, sizeof(char));
        return ListUpdAct::get((val));
    }

    PartyDetailsDefinitionRequest518 &listUpdateAction(const ListUpdAct::Value value) SBE_NOEXCEPT
    {
        char val = (value);
        std::memcpy(m_buffer + m_offset + 16, &val, sizeof(char));
        return *this;
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
        return 17;
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
        std::memcpy(&val, m_buffer + m_offset + 17, sizeof(std::uint32_t));
        return SBE_LITTLE_ENDIAN_ENCODE_32(val);
    }

    PartyDetailsDefinitionRequest518 &seqNum(const std::uint32_t value) SBE_NOEXCEPT
    {
        std::uint32_t val = SBE_LITTLE_ENDIAN_ENCODE_32(value);
        std::memcpy(m_buffer + m_offset + 17, &val, sizeof(std::uint32_t));
        return *this;
    }

    SBE_NODISCARD static const char *MemoMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "String";
            case MetaAttribute::PRESENCE: return "optional";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t memoId() SBE_NOEXCEPT
    {
        return 5149;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t memoSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool memoInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t memoEncodingOffset() SBE_NOEXCEPT
    {
        return 21;
    }

    static SBE_CONSTEXPR char memoNullValue() SBE_NOEXCEPT
    {
        return static_cast<char>(0);
    }

    static SBE_CONSTEXPR char memoMinValue() SBE_NOEXCEPT
    {
        return static_cast<char>(32);
    }

    static SBE_CONSTEXPR char memoMaxValue() SBE_NOEXCEPT
    {
        return static_cast<char>(126);
    }

    static SBE_CONSTEXPR std::size_t memoEncodingLength() SBE_NOEXCEPT
    {
        return 75;
    }

    static SBE_CONSTEXPR std::uint64_t memoLength() SBE_NOEXCEPT
    {
        return 75;
    }

    SBE_NODISCARD const char *memo() const SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 21;
    }

    SBE_NODISCARD char *memo() SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 21;
    }

    SBE_NODISCARD char memo(const std::uint64_t index) const
    {
        if (index >= 75)
        {
            throw std::runtime_error("index out of range for memo [E104]");
        }

        char val;
        std::memcpy(&val, m_buffer + m_offset + 21 + (index * 1), sizeof(char));
        return (val);
    }

    PartyDetailsDefinitionRequest518 &memo(const std::uint64_t index, const char value)
    {
        if (index >= 75)
        {
            throw std::runtime_error("index out of range for memo [E105]");
        }

        char val = (value);
        std::memcpy(m_buffer + m_offset + 21 + (index * 1), &val, sizeof(char));
        return *this;
    }

    std::uint64_t getMemo(char *const dst, const std::uint64_t length) const
    {
        if (length > 75)
        {
            throw std::runtime_error("length too large for getMemo [E106]");
        }

        std::memcpy(dst, m_buffer + m_offset + 21, sizeof(char) * static_cast<std::size_t>(length));
        return length;
    }

    PartyDetailsDefinitionRequest518 &putMemo(const char *const src) SBE_NOEXCEPT
    {
        std::memcpy(m_buffer + m_offset + 21, src, sizeof(char) * 75);
        return *this;
    }

    SBE_NODISCARD std::string getMemoAsString() const
    {
        const char *buffer = m_buffer + m_offset + 21;
        std::size_t length = 0;

        for (; length < 75 && *(buffer + length) != '\0'; ++length);
        std::string result(buffer, length);

        return result;
    }

    std::string getMemoAsJsonEscapedString()
    {
        std::ostringstream oss;
        std::string s = getMemoAsString();

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
    SBE_NODISCARD std::string_view getMemoAsStringView() const SBE_NOEXCEPT
    {
        const char *buffer = m_buffer + m_offset + 21;
        std::size_t length = 0;

        for (; length < 75 && *(buffer + length) != '\0'; ++length);
        std::string_view result(buffer, length);

        return result;
    }
    #endif

    #if __cplusplus >= 201703L
    PartyDetailsDefinitionRequest518 &putMemo(const std::string_view str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 75)
        {
            throw std::runtime_error("string too large for putMemo [E106]");
        }

        std::memcpy(m_buffer + m_offset + 21, str.data(), srcLength);
        for (std::size_t start = srcLength; start < 75; ++start)
        {
            m_buffer[m_offset + 21 + start] = 0;
        }

        return *this;
    }
    #else
    PartyDetailsDefinitionRequest518 &putMemo(const std::string &str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 75)
        {
            throw std::runtime_error("string too large for putMemo [E106]");
        }

        std::memcpy(m_buffer + m_offset + 21, str.c_str(), srcLength);
        for (std::size_t start = srcLength; start < 75; ++start)
        {
            m_buffer[m_offset + 21 + start] = 0;
        }

        return *this;
    }
    #endif

    SBE_NODISCARD static const char *AvgPxGroupIDMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "String";
            case MetaAttribute::PRESENCE: return "optional";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t avgPxGroupIDId() SBE_NOEXCEPT
    {
        return 1731;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t avgPxGroupIDSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool avgPxGroupIDInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t avgPxGroupIDEncodingOffset() SBE_NOEXCEPT
    {
        return 96;
    }

    static SBE_CONSTEXPR char avgPxGroupIDNullValue() SBE_NOEXCEPT
    {
        return static_cast<char>(0);
    }

    static SBE_CONSTEXPR char avgPxGroupIDMinValue() SBE_NOEXCEPT
    {
        return static_cast<char>(32);
    }

    static SBE_CONSTEXPR char avgPxGroupIDMaxValue() SBE_NOEXCEPT
    {
        return static_cast<char>(126);
    }

    static SBE_CONSTEXPR std::size_t avgPxGroupIDEncodingLength() SBE_NOEXCEPT
    {
        return 20;
    }

    static SBE_CONSTEXPR std::uint64_t avgPxGroupIDLength() SBE_NOEXCEPT
    {
        return 20;
    }

    SBE_NODISCARD const char *avgPxGroupID() const SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 96;
    }

    SBE_NODISCARD char *avgPxGroupID() SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 96;
    }

    SBE_NODISCARD char avgPxGroupID(const std::uint64_t index) const
    {
        if (index >= 20)
        {
            throw std::runtime_error("index out of range for avgPxGroupID [E104]");
        }

        char val;
        std::memcpy(&val, m_buffer + m_offset + 96 + (index * 1), sizeof(char));
        return (val);
    }

    PartyDetailsDefinitionRequest518 &avgPxGroupID(const std::uint64_t index, const char value)
    {
        if (index >= 20)
        {
            throw std::runtime_error("index out of range for avgPxGroupID [E105]");
        }

        char val = (value);
        std::memcpy(m_buffer + m_offset + 96 + (index * 1), &val, sizeof(char));
        return *this;
    }

    std::uint64_t getAvgPxGroupID(char *const dst, const std::uint64_t length) const
    {
        if (length > 20)
        {
            throw std::runtime_error("length too large for getAvgPxGroupID [E106]");
        }

        std::memcpy(dst, m_buffer + m_offset + 96, sizeof(char) * static_cast<std::size_t>(length));
        return length;
    }

    PartyDetailsDefinitionRequest518 &putAvgPxGroupID(const char *const src) SBE_NOEXCEPT
    {
        std::memcpy(m_buffer + m_offset + 96, src, sizeof(char) * 20);
        return *this;
    }

    SBE_NODISCARD std::string getAvgPxGroupIDAsString() const
    {
        const char *buffer = m_buffer + m_offset + 96;
        std::size_t length = 0;

        for (; length < 20 && *(buffer + length) != '\0'; ++length);
        std::string result(buffer, length);

        return result;
    }

    std::string getAvgPxGroupIDAsJsonEscapedString()
    {
        std::ostringstream oss;
        std::string s = getAvgPxGroupIDAsString();

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
    SBE_NODISCARD std::string_view getAvgPxGroupIDAsStringView() const SBE_NOEXCEPT
    {
        const char *buffer = m_buffer + m_offset + 96;
        std::size_t length = 0;

        for (; length < 20 && *(buffer + length) != '\0'; ++length);
        std::string_view result(buffer, length);

        return result;
    }
    #endif

    #if __cplusplus >= 201703L
    PartyDetailsDefinitionRequest518 &putAvgPxGroupID(const std::string_view str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 20)
        {
            throw std::runtime_error("string too large for putAvgPxGroupID [E106]");
        }

        std::memcpy(m_buffer + m_offset + 96, str.data(), srcLength);
        for (std::size_t start = srcLength; start < 20; ++start)
        {
            m_buffer[m_offset + 96 + start] = 0;
        }

        return *this;
    }
    #else
    PartyDetailsDefinitionRequest518 &putAvgPxGroupID(const std::string &str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 20)
        {
            throw std::runtime_error("string too large for putAvgPxGroupID [E106]");
        }

        std::memcpy(m_buffer + m_offset + 96, str.c_str(), srcLength);
        for (std::size_t start = srcLength; start < 20; ++start)
        {
            m_buffer[m_offset + 96 + start] = 0;
        }

        return *this;
    }
    #endif

    SBE_NODISCARD static const char *SelfMatchPreventionIDMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "optional";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t selfMatchPreventionIDId() SBE_NOEXCEPT
    {
        return 2362;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t selfMatchPreventionIDSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool selfMatchPreventionIDInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t selfMatchPreventionIDEncodingOffset() SBE_NOEXCEPT
    {
        return 116;
    }

    static SBE_CONSTEXPR std::uint64_t selfMatchPreventionIDNullValue() SBE_NOEXCEPT
    {
        return UINT64_C(0xffffffffffffffff);
    }

    static SBE_CONSTEXPR std::uint64_t selfMatchPreventionIDMinValue() SBE_NOEXCEPT
    {
        return UINT64_C(0x0);
    }

    static SBE_CONSTEXPR std::uint64_t selfMatchPreventionIDMaxValue() SBE_NOEXCEPT
    {
        return UINT64_C(0xfffffffffffffffe);
    }

    static SBE_CONSTEXPR std::size_t selfMatchPreventionIDEncodingLength() SBE_NOEXCEPT
    {
        return 8;
    }

    SBE_NODISCARD std::uint64_t selfMatchPreventionID() const SBE_NOEXCEPT
    {
        std::uint64_t val;
        std::memcpy(&val, m_buffer + m_offset + 116, sizeof(std::uint64_t));
        return SBE_LITTLE_ENDIAN_ENCODE_64(val);
    }

    PartyDetailsDefinitionRequest518 &selfMatchPreventionID(const std::uint64_t value) SBE_NOEXCEPT
    {
        std::uint64_t val = SBE_LITTLE_ENDIAN_ENCODE_64(value);
        std::memcpy(m_buffer + m_offset + 116, &val, sizeof(std::uint64_t));
        return *this;
    }

    SBE_NODISCARD static const char *CmtaGiveupCDMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "char";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t cmtaGiveupCDId() SBE_NOEXCEPT
    {
        return 9708;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t cmtaGiveupCDSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool cmtaGiveupCDInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t cmtaGiveupCDEncodingOffset() SBE_NOEXCEPT
    {
        return 124;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t cmtaGiveupCDEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD char cmtaGiveupCDRaw() const SBE_NOEXCEPT
    {
        char val;
        std::memcpy(&val, m_buffer + m_offset + 124, sizeof(char));
        return (val);
    }

    SBE_NODISCARD CmtaGiveUpCD::Value cmtaGiveupCD() const
    {
        char val;
        std::memcpy(&val, m_buffer + m_offset + 124, sizeof(char));
        return CmtaGiveUpCD::get((val));
    }

    PartyDetailsDefinitionRequest518 &cmtaGiveupCD(const CmtaGiveUpCD::Value value) SBE_NOEXCEPT
    {
        char val = (value);
        std::memcpy(m_buffer + m_offset + 124, &val, sizeof(char));
        return *this;
    }

    SBE_NODISCARD static const char *CustOrderCapacityMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t custOrderCapacityId() SBE_NOEXCEPT
    {
        return 582;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t custOrderCapacitySinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool custOrderCapacityInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t custOrderCapacityEncodingOffset() SBE_NOEXCEPT
    {
        return 125;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t custOrderCapacityEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD std::uint8_t custOrderCapacityRaw() const SBE_NOEXCEPT
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 125, sizeof(std::uint8_t));
        return (val);
    }

    SBE_NODISCARD CustOrderCapacity::Value custOrderCapacity() const
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 125, sizeof(std::uint8_t));
        return CustOrderCapacity::get((val));
    }

    PartyDetailsDefinitionRequest518 &custOrderCapacity(const CustOrderCapacity::Value value) SBE_NOEXCEPT
    {
        std::uint8_t val = (value);
        std::memcpy(m_buffer + m_offset + 125, &val, sizeof(std::uint8_t));
        return *this;
    }

    SBE_NODISCARD static const char *ClearingAccountTypeMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t clearingAccountTypeId() SBE_NOEXCEPT
    {
        return 1816;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t clearingAccountTypeSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool clearingAccountTypeInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t clearingAccountTypeEncodingOffset() SBE_NOEXCEPT
    {
        return 126;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t clearingAccountTypeEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD std::uint8_t clearingAccountTypeRaw() const SBE_NOEXCEPT
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 126, sizeof(std::uint8_t));
        return (val);
    }

    SBE_NODISCARD ClearingAcctType::Value clearingAccountType() const
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 126, sizeof(std::uint8_t));
        return ClearingAcctType::get((val));
    }

    PartyDetailsDefinitionRequest518 &clearingAccountType(const ClearingAcctType::Value value) SBE_NOEXCEPT
    {
        std::uint8_t val = (value);
        std::memcpy(m_buffer + m_offset + 126, &val, sizeof(std::uint8_t));
        return *this;
    }

    SBE_NODISCARD static const char *SelfMatchPreventionInstructionMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "char";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t selfMatchPreventionInstructionId() SBE_NOEXCEPT
    {
        return 8000;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t selfMatchPreventionInstructionSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool selfMatchPreventionInstructionInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t selfMatchPreventionInstructionEncodingOffset() SBE_NOEXCEPT
    {
        return 127;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t selfMatchPreventionInstructionEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD char selfMatchPreventionInstructionRaw() const SBE_NOEXCEPT
    {
        char val;
        std::memcpy(&val, m_buffer + m_offset + 127, sizeof(char));
        return (val);
    }

    SBE_NODISCARD SMPI::Value selfMatchPreventionInstruction() const
    {
        char val;
        std::memcpy(&val, m_buffer + m_offset + 127, sizeof(char));
        return SMPI::get((val));
    }

    PartyDetailsDefinitionRequest518 &selfMatchPreventionInstruction(const SMPI::Value value) SBE_NOEXCEPT
    {
        char val = (value);
        std::memcpy(m_buffer + m_offset + 127, &val, sizeof(char));
        return *this;
    }

    SBE_NODISCARD static const char *AvgPxIndicatorMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t avgPxIndicatorId() SBE_NOEXCEPT
    {
        return 819;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t avgPxIndicatorSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool avgPxIndicatorInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t avgPxIndicatorEncodingOffset() SBE_NOEXCEPT
    {
        return 128;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t avgPxIndicatorEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD std::uint8_t avgPxIndicatorRaw() const SBE_NOEXCEPT
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 128, sizeof(std::uint8_t));
        return (val);
    }

    SBE_NODISCARD AvgPxInd::Value avgPxIndicator() const
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 128, sizeof(std::uint8_t));
        return AvgPxInd::get((val));
    }

    PartyDetailsDefinitionRequest518 &avgPxIndicator(const AvgPxInd::Value value) SBE_NOEXCEPT
    {
        std::uint8_t val = (value);
        std::memcpy(m_buffer + m_offset + 128, &val, sizeof(std::uint8_t));
        return *this;
    }

    SBE_NODISCARD static const char *ClearingTradePriceTypeMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t clearingTradePriceTypeId() SBE_NOEXCEPT
    {
        return 1598;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t clearingTradePriceTypeSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool clearingTradePriceTypeInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t clearingTradePriceTypeEncodingOffset() SBE_NOEXCEPT
    {
        return 129;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t clearingTradePriceTypeEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD std::uint8_t clearingTradePriceTypeRaw() const SBE_NOEXCEPT
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 129, sizeof(std::uint8_t));
        return (val);
    }

    SBE_NODISCARD SLEDS::Value clearingTradePriceType() const
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 129, sizeof(std::uint8_t));
        return SLEDS::get((val));
    }

    PartyDetailsDefinitionRequest518 &clearingTradePriceType(const SLEDS::Value value) SBE_NOEXCEPT
    {
        std::uint8_t val = (value);
        std::memcpy(m_buffer + m_offset + 129, &val, sizeof(std::uint8_t));
        return *this;
    }

    SBE_NODISCARD static const char *CustOrderHandlingInstMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "char";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t custOrderHandlingInstId() SBE_NOEXCEPT
    {
        return 1031;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t custOrderHandlingInstSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool custOrderHandlingInstInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t custOrderHandlingInstEncodingOffset() SBE_NOEXCEPT
    {
        return 130;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t custOrderHandlingInstEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD char custOrderHandlingInstRaw() const SBE_NOEXCEPT
    {
        char val;
        std::memcpy(&val, m_buffer + m_offset + 130, sizeof(char));
        return (val);
    }

    SBE_NODISCARD CustOrdHandlInst::Value custOrderHandlingInst() const
    {
        char val;
        std::memcpy(&val, m_buffer + m_offset + 130, sizeof(char));
        return CustOrdHandlInst::get((val));
    }

    PartyDetailsDefinitionRequest518 &custOrderHandlingInst(const CustOrdHandlInst::Value value) SBE_NOEXCEPT
    {
        char val = (value);
        std::memcpy(m_buffer + m_offset + 130, &val, sizeof(char));
        return *this;
    }

    SBE_NODISCARD static const char *ExecutorMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "optional";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t executorId() SBE_NOEXCEPT
    {
        return 5290;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t executorSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool executorInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t executorEncodingOffset() SBE_NOEXCEPT
    {
        return 131;
    }

    static SBE_CONSTEXPR std::uint64_t executorNullValue() SBE_NOEXCEPT
    {
        return UINT64_C(0xffffffffffffffff);
    }

    static SBE_CONSTEXPR std::uint64_t executorMinValue() SBE_NOEXCEPT
    {
        return UINT64_C(0x0);
    }

    static SBE_CONSTEXPR std::uint64_t executorMaxValue() SBE_NOEXCEPT
    {
        return UINT64_C(0xfffffffffffffffe);
    }

    static SBE_CONSTEXPR std::size_t executorEncodingLength() SBE_NOEXCEPT
    {
        return 8;
    }

    SBE_NODISCARD std::uint64_t executor() const SBE_NOEXCEPT
    {
        std::uint64_t val;
        std::memcpy(&val, m_buffer + m_offset + 131, sizeof(std::uint64_t));
        return SBE_LITTLE_ENDIAN_ENCODE_64(val);
    }

    PartyDetailsDefinitionRequest518 &executor(const std::uint64_t value) SBE_NOEXCEPT
    {
        std::uint64_t val = SBE_LITTLE_ENDIAN_ENCODE_64(value);
        std::memcpy(m_buffer + m_offset + 131, &val, sizeof(std::uint64_t));
        return *this;
    }

    SBE_NODISCARD static const char *IDMShortCodeMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "optional";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t iDMShortCodeId() SBE_NOEXCEPT
    {
        return 36023;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t iDMShortCodeSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool iDMShortCodeInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t iDMShortCodeEncodingOffset() SBE_NOEXCEPT
    {
        return 139;
    }

    static SBE_CONSTEXPR std::uint64_t iDMShortCodeNullValue() SBE_NOEXCEPT
    {
        return UINT64_C(0xffffffffffffffff);
    }

    static SBE_CONSTEXPR std::uint64_t iDMShortCodeMinValue() SBE_NOEXCEPT
    {
        return UINT64_C(0x0);
    }

    static SBE_CONSTEXPR std::uint64_t iDMShortCodeMaxValue() SBE_NOEXCEPT
    {
        return UINT64_C(0xfffffffffffffffe);
    }

    static SBE_CONSTEXPR std::size_t iDMShortCodeEncodingLength() SBE_NOEXCEPT
    {
        return 8;
    }

    SBE_NODISCARD std::uint64_t iDMShortCode() const SBE_NOEXCEPT
    {
        std::uint64_t val;
        std::memcpy(&val, m_buffer + m_offset + 139, sizeof(std::uint64_t));
        return SBE_LITTLE_ENDIAN_ENCODE_64(val);
    }

    PartyDetailsDefinitionRequest518 &iDMShortCode(const std::uint64_t value) SBE_NOEXCEPT
    {
        std::uint64_t val = SBE_LITTLE_ENDIAN_ENCODE_64(value);
        std::memcpy(m_buffer + m_offset + 139, &val, sizeof(std::uint64_t));
        return *this;
    }

    SBE_NODISCARD static const char *NoPartyUpdatesMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "constant";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t noPartyUpdatesId() SBE_NOEXCEPT
    {
        return 1676;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t noPartyUpdatesSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool noPartyUpdatesInActingVersion() SBE_NOEXCEPT
    {
        return true;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t noPartyUpdatesEncodingOffset() SBE_NOEXCEPT
    {
        return 147;
    }

    static SBE_CONSTEXPR std::uint8_t noPartyUpdatesNullValue() SBE_NOEXCEPT
    {
        return SBE_NULLVALUE_UINT8;
    }

    static SBE_CONSTEXPR std::uint8_t noPartyUpdatesMinValue() SBE_NOEXCEPT
    {
        return static_cast<std::uint8_t>(0);
    }

    static SBE_CONSTEXPR std::uint8_t noPartyUpdatesMaxValue() SBE_NOEXCEPT
    {
        return static_cast<std::uint8_t>(254);
    }

    static SBE_CONSTEXPR std::size_t noPartyUpdatesEncodingLength() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint8_t noPartyUpdates() SBE_NOEXCEPT
    {
        return static_cast<std::uint8_t>(1);
    }

    class NoPartyDetails
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
            dimensions.blockLength(static_cast<std::uint16_t>(22));
            dimensions.numInGroup(static_cast<std::uint8_t>(count));
            m_index = 0;
            m_count = count;
            m_blockLength = 22;
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
            return 22;
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

        inline NoPartyDetails &next()
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


        SBE_NODISCARD static const char *PartyDetailIDMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
        {
            switch (metaAttribute)
            {
                case MetaAttribute::SEMANTIC_TYPE: return "String";
                case MetaAttribute::PRESENCE: return "required";
                default: return "";
            }
        }

        static SBE_CONSTEXPR std::uint16_t partyDetailIDId() SBE_NOEXCEPT
        {
            return 1691;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t partyDetailIDSinceVersion() SBE_NOEXCEPT
        {
            return 0;
        }

        SBE_NODISCARD bool partyDetailIDInActingVersion() SBE_NOEXCEPT
        {
            return true;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::size_t partyDetailIDEncodingOffset() SBE_NOEXCEPT
        {
            return 0;
        }

        static SBE_CONSTEXPR char partyDetailIDNullValue() SBE_NOEXCEPT
        {
            return static_cast<char>(0);
        }

        static SBE_CONSTEXPR char partyDetailIDMinValue() SBE_NOEXCEPT
        {
            return static_cast<char>(32);
        }

        static SBE_CONSTEXPR char partyDetailIDMaxValue() SBE_NOEXCEPT
        {
            return static_cast<char>(126);
        }

        static SBE_CONSTEXPR std::size_t partyDetailIDEncodingLength() SBE_NOEXCEPT
        {
            return 20;
        }

        static SBE_CONSTEXPR std::uint64_t partyDetailIDLength() SBE_NOEXCEPT
        {
            return 20;
        }

        SBE_NODISCARD const char *partyDetailID() const SBE_NOEXCEPT
        {
            return m_buffer + m_offset + 0;
        }

        SBE_NODISCARD char *partyDetailID() SBE_NOEXCEPT
        {
            return m_buffer + m_offset + 0;
        }

        SBE_NODISCARD char partyDetailID(const std::uint64_t index) const
        {
            if (index >= 20)
            {
                throw std::runtime_error("index out of range for partyDetailID [E104]");
            }

            char val;
            std::memcpy(&val, m_buffer + m_offset + 0 + (index * 1), sizeof(char));
            return (val);
        }

        NoPartyDetails &partyDetailID(const std::uint64_t index, const char value)
        {
            if (index >= 20)
            {
                throw std::runtime_error("index out of range for partyDetailID [E105]");
            }

            char val = (value);
            std::memcpy(m_buffer + m_offset + 0 + (index * 1), &val, sizeof(char));
            return *this;
        }

        std::uint64_t getPartyDetailID(char *const dst, const std::uint64_t length) const
        {
            if (length > 20)
            {
                throw std::runtime_error("length too large for getPartyDetailID [E106]");
            }

            std::memcpy(dst, m_buffer + m_offset + 0, sizeof(char) * static_cast<std::size_t>(length));
            return length;
        }

        NoPartyDetails &putPartyDetailID(const char *const src) SBE_NOEXCEPT
        {
            std::memcpy(m_buffer + m_offset + 0, src, sizeof(char) * 20);
            return *this;
        }

        SBE_NODISCARD std::string getPartyDetailIDAsString() const
        {
            const char *buffer = m_buffer + m_offset + 0;
            std::size_t length = 0;

            for (; length < 20 && *(buffer + length) != '\0'; ++length);
            std::string result(buffer, length);

            return result;
        }

        std::string getPartyDetailIDAsJsonEscapedString()
        {
            std::ostringstream oss;
            std::string s = getPartyDetailIDAsString();

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
        SBE_NODISCARD std::string_view getPartyDetailIDAsStringView() const SBE_NOEXCEPT
        {
            const char *buffer = m_buffer + m_offset + 0;
            std::size_t length = 0;

            for (; length < 20 && *(buffer + length) != '\0'; ++length);
            std::string_view result(buffer, length);

            return result;
        }
        #endif

        #if __cplusplus >= 201703L
        NoPartyDetails &putPartyDetailID(const std::string_view str)
        {
            const std::size_t srcLength = str.length();
            if (srcLength > 20)
            {
                throw std::runtime_error("string too large for putPartyDetailID [E106]");
            }

            std::memcpy(m_buffer + m_offset + 0, str.data(), srcLength);
            for (std::size_t start = srcLength; start < 20; ++start)
            {
                m_buffer[m_offset + 0 + start] = 0;
            }

            return *this;
        }
        #else
        NoPartyDetails &putPartyDetailID(const std::string &str)
        {
            const std::size_t srcLength = str.length();
            if (srcLength > 20)
            {
                throw std::runtime_error("string too large for putPartyDetailID [E106]");
            }

            std::memcpy(m_buffer + m_offset + 0, str.c_str(), srcLength);
            for (std::size_t start = srcLength; start < 20; ++start)
            {
                m_buffer[m_offset + 0 + start] = 0;
            }

            return *this;
        }
        #endif

        SBE_NODISCARD static const char *PartyDetailIDSourceMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
        {
            switch (metaAttribute)
            {
                case MetaAttribute::SEMANTIC_TYPE: return "char";
                case MetaAttribute::PRESENCE: return "constant";
                default: return "";
            }
        }

        static SBE_CONSTEXPR std::uint16_t partyDetailIDSourceId() SBE_NOEXCEPT
        {
            return 1692;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t partyDetailIDSourceSinceVersion() SBE_NOEXCEPT
        {
            return 0;
        }

        SBE_NODISCARD bool partyDetailIDSourceInActingVersion() SBE_NOEXCEPT
        {
            return true;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::size_t partyDetailIDSourceEncodingOffset() SBE_NOEXCEPT
        {
            return 20;
        }

        static SBE_CONSTEXPR char partyDetailIDSourceNullValue() SBE_NOEXCEPT
        {
            return static_cast<char>(0);
        }

        static SBE_CONSTEXPR char partyDetailIDSourceMinValue() SBE_NOEXCEPT
        {
            return static_cast<char>(32);
        }

        static SBE_CONSTEXPR char partyDetailIDSourceMaxValue() SBE_NOEXCEPT
        {
            return static_cast<char>(126);
        }

        static SBE_CONSTEXPR std::size_t partyDetailIDSourceEncodingLength() SBE_NOEXCEPT
        {
            return 0;
        }

        static SBE_CONSTEXPR std::uint64_t partyDetailIDSourceLength() SBE_NOEXCEPT
        {
            return 1;
        }

        SBE_NODISCARD const char *partyDetailIDSource() const
        {
            static const std::uint8_t partyDetailIDSourceValues[] = { 67, 0 };

            return (const char *)partyDetailIDSourceValues;
        }

        SBE_NODISCARD char partyDetailIDSource(const std::uint64_t index) const
        {
            static const std::uint8_t partyDetailIDSourceValues[] = { 67, 0 };

            return (char)partyDetailIDSourceValues[index];
        }

        std::uint64_t getPartyDetailIDSource(char *dst, const std::uint64_t length) const
        {
            static std::uint8_t partyDetailIDSourceValues[] = { 67 };
            std::uint64_t bytesToCopy = length < sizeof(partyDetailIDSourceValues) ? length : sizeof(partyDetailIDSourceValues);

            std::memcpy(dst, partyDetailIDSourceValues, static_cast<std::size_t>(bytesToCopy));
            return bytesToCopy;
        }

        std::string getPartyDetailIDSourceAsString() const
        {
            static const std::uint8_t PartyDetailIDSourceValues[] = { 67 };

            return std::string((const char *)PartyDetailIDSourceValues, 1);
        }

        std::string getPartyDetailIDSourceAsJsonEscapedString()
        {
            std::ostringstream oss;
            std::string s = getPartyDetailIDSourceAsString();

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

        SBE_NODISCARD static const char *PartyDetailRoleMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
        {
            switch (metaAttribute)
            {
                case MetaAttribute::SEMANTIC_TYPE: return "int";
                case MetaAttribute::PRESENCE: return "required";
                default: return "";
            }
        }

        static SBE_CONSTEXPR std::uint16_t partyDetailRoleId() SBE_NOEXCEPT
        {
            return 1693;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t partyDetailRoleSinceVersion() SBE_NOEXCEPT
        {
            return 0;
        }

        SBE_NODISCARD bool partyDetailRoleInActingVersion() SBE_NOEXCEPT
        {
            return true;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::size_t partyDetailRoleEncodingOffset() SBE_NOEXCEPT
        {
            return 20;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::size_t partyDetailRoleEncodingLength() SBE_NOEXCEPT
        {
            return 2;
        }

        SBE_NODISCARD std::uint16_t partyDetailRoleRaw() const SBE_NOEXCEPT
        {
            std::uint16_t val;
            std::memcpy(&val, m_buffer + m_offset + 20, sizeof(std::uint16_t));
            return SBE_LITTLE_ENDIAN_ENCODE_16(val);
        }

        SBE_NODISCARD PartyDetailRole::Value partyDetailRole() const
        {
            std::uint16_t val;
            std::memcpy(&val, m_buffer + m_offset + 20, sizeof(std::uint16_t));
            return PartyDetailRole::get(SBE_LITTLE_ENDIAN_ENCODE_16(val));
        }

        NoPartyDetails &partyDetailRole(const PartyDetailRole::Value value) SBE_NOEXCEPT
        {
            std::uint16_t val = SBE_LITTLE_ENDIAN_ENCODE_16(value);
            std::memcpy(m_buffer + m_offset + 20, &val, sizeof(std::uint16_t));
            return *this;
        }

        template<typename CharT, typename Traits>
        friend std::basic_ostream<CharT, Traits> & operator << (
            std::basic_ostream<CharT, Traits> &builder, NoPartyDetails &writer)
        {
            builder << '{';
            builder << R"("PartyDetailID": )";
            builder << '"' <<
                writer.getPartyDetailIDAsJsonEscapedString().c_str() << '"';

            builder << ", ";
            builder << R"("PartyDetailRole": )";
            builder << '"' << writer.partyDetailRole() << '"';

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
    NoPartyDetails m_noPartyDetails;

public:
    SBE_NODISCARD static SBE_CONSTEXPR std::uint16_t NoPartyDetailsId() SBE_NOEXCEPT
    {
        return 1671;
    }

    SBE_NODISCARD inline NoPartyDetails &noPartyDetails()
    {
        m_noPartyDetails.wrapForDecode(m_buffer, sbePositionPtr(), m_actingVersion, m_bufferLength);
        return m_noPartyDetails;
    }

    NoPartyDetails &noPartyDetailsCount(const std::uint8_t count)
    {
        m_noPartyDetails.wrapForEncode(m_buffer, count, sbePositionPtr(), m_actingVersion, m_bufferLength);
        return m_noPartyDetails;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t noPartyDetailsSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool noPartyDetailsInActingVersion() const SBE_NOEXCEPT
    {
        return true;
    }

    class NoTrdRegPublications
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
            dimensions.blockLength(static_cast<std::uint16_t>(2));
            dimensions.numInGroup(static_cast<std::uint8_t>(count));
            m_index = 0;
            m_count = count;
            m_blockLength = 2;
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
            return 2;
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

        inline NoTrdRegPublications &next()
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


        SBE_NODISCARD static const char *TrdRegPublicationTypeMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
        {
            switch (metaAttribute)
            {
                case MetaAttribute::SEMANTIC_TYPE: return "int";
                case MetaAttribute::PRESENCE: return "required";
                default: return "";
            }
        }

        static SBE_CONSTEXPR std::uint16_t trdRegPublicationTypeId() SBE_NOEXCEPT
        {
            return 2669;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t trdRegPublicationTypeSinceVersion() SBE_NOEXCEPT
        {
            return 0;
        }

        SBE_NODISCARD bool trdRegPublicationTypeInActingVersion() SBE_NOEXCEPT
        {
            return true;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::size_t trdRegPublicationTypeEncodingOffset() SBE_NOEXCEPT
        {
            return 0;
        }

        static SBE_CONSTEXPR std::uint8_t trdRegPublicationTypeNullValue() SBE_NOEXCEPT
        {
            return SBE_NULLVALUE_UINT8;
        }

        static SBE_CONSTEXPR std::uint8_t trdRegPublicationTypeMinValue() SBE_NOEXCEPT
        {
            return static_cast<std::uint8_t>(0);
        }

        static SBE_CONSTEXPR std::uint8_t trdRegPublicationTypeMaxValue() SBE_NOEXCEPT
        {
            return static_cast<std::uint8_t>(254);
        }

        static SBE_CONSTEXPR std::size_t trdRegPublicationTypeEncodingLength() SBE_NOEXCEPT
        {
            return 1;
        }

        SBE_NODISCARD std::uint8_t trdRegPublicationType() const SBE_NOEXCEPT
        {
            std::uint8_t val;
            std::memcpy(&val, m_buffer + m_offset + 0, sizeof(std::uint8_t));
            return (val);
        }

        NoTrdRegPublications &trdRegPublicationType(const std::uint8_t value) SBE_NOEXCEPT
        {
            std::uint8_t val = (value);
            std::memcpy(m_buffer + m_offset + 0, &val, sizeof(std::uint8_t));
            return *this;
        }

        SBE_NODISCARD static const char *TrdRegPublicationReasonMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
        {
            switch (metaAttribute)
            {
                case MetaAttribute::SEMANTIC_TYPE: return "int";
                case MetaAttribute::PRESENCE: return "required";
                default: return "";
            }
        }

        static SBE_CONSTEXPR std::uint16_t trdRegPublicationReasonId() SBE_NOEXCEPT
        {
            return 2670;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t trdRegPublicationReasonSinceVersion() SBE_NOEXCEPT
        {
            return 0;
        }

        SBE_NODISCARD bool trdRegPublicationReasonInActingVersion() SBE_NOEXCEPT
        {
            return true;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::size_t trdRegPublicationReasonEncodingOffset() SBE_NOEXCEPT
        {
            return 1;
        }

        static SBE_CONSTEXPR std::uint8_t trdRegPublicationReasonNullValue() SBE_NOEXCEPT
        {
            return SBE_NULLVALUE_UINT8;
        }

        static SBE_CONSTEXPR std::uint8_t trdRegPublicationReasonMinValue() SBE_NOEXCEPT
        {
            return static_cast<std::uint8_t>(0);
        }

        static SBE_CONSTEXPR std::uint8_t trdRegPublicationReasonMaxValue() SBE_NOEXCEPT
        {
            return static_cast<std::uint8_t>(254);
        }

        static SBE_CONSTEXPR std::size_t trdRegPublicationReasonEncodingLength() SBE_NOEXCEPT
        {
            return 1;
        }

        SBE_NODISCARD std::uint8_t trdRegPublicationReason() const SBE_NOEXCEPT
        {
            std::uint8_t val;
            std::memcpy(&val, m_buffer + m_offset + 1, sizeof(std::uint8_t));
            return (val);
        }

        NoTrdRegPublications &trdRegPublicationReason(const std::uint8_t value) SBE_NOEXCEPT
        {
            std::uint8_t val = (value);
            std::memcpy(m_buffer + m_offset + 1, &val, sizeof(std::uint8_t));
            return *this;
        }

        template<typename CharT, typename Traits>
        friend std::basic_ostream<CharT, Traits> & operator << (
            std::basic_ostream<CharT, Traits> &builder, NoTrdRegPublications &writer)
        {
            builder << '{';
            builder << R"("TrdRegPublicationType": )";
            builder << +writer.trdRegPublicationType();

            builder << ", ";
            builder << R"("TrdRegPublicationReason": )";
            builder << +writer.trdRegPublicationReason();

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
    NoTrdRegPublications m_noTrdRegPublications;

public:
    SBE_NODISCARD static SBE_CONSTEXPR std::uint16_t NoTrdRegPublicationsId() SBE_NOEXCEPT
    {
        return 2668;
    }

    SBE_NODISCARD inline NoTrdRegPublications &noTrdRegPublications()
    {
        m_noTrdRegPublications.wrapForDecode(m_buffer, sbePositionPtr(), m_actingVersion, m_bufferLength);
        return m_noTrdRegPublications;
    }

    NoTrdRegPublications &noTrdRegPublicationsCount(const std::uint8_t count)
    {
        m_noTrdRegPublications.wrapForEncode(m_buffer, count, sbePositionPtr(), m_actingVersion, m_bufferLength);
        return m_noTrdRegPublications;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t noTrdRegPublicationsSinceVersion() SBE_NOEXCEPT
    {
        return 2;
    }

    SBE_NODISCARD bool noTrdRegPublicationsInActingVersion() const SBE_NOEXCEPT
    {
        return m_actingVersion >= noTrdRegPublicationsSinceVersion();
    }

template<typename CharT, typename Traits>
friend std::basic_ostream<CharT, Traits> & operator << (
    std::basic_ostream<CharT, Traits> &builder, const PartyDetailsDefinitionRequest518 &_writer)
{
    PartyDetailsDefinitionRequest518 writer(
        _writer.m_buffer,
        _writer.m_offset,
        _writer.m_bufferLength,
        _writer.m_actingBlockLength,
        _writer.m_actingVersion);

    builder << '{';
    builder << R"("Name": "PartyDetailsDefinitionRequest518", )";
    builder << R"("sbeTemplateId": )";
    builder << writer.sbeTemplateId();
    builder << ", ";

    builder << R"("PartyDetailsListReqID": )";
    builder << +writer.partyDetailsListReqID();

    builder << ", ";
    builder << R"("SendingTimeEpoch": )";
    builder << +writer.sendingTimeEpoch();

    builder << ", ";
    builder << R"("ListUpdateAction": )";
    builder << '"' << writer.listUpdateAction() << '"';

    builder << ", ";
    builder << R"("SeqNum": )";
    builder << +writer.seqNum();

    builder << ", ";
    builder << R"("Memo": )";
    builder << '"' <<
        writer.getMemoAsJsonEscapedString().c_str() << '"';

    builder << ", ";
    builder << R"("AvgPxGroupID": )";
    builder << '"' <<
        writer.getAvgPxGroupIDAsJsonEscapedString().c_str() << '"';

    builder << ", ";
    builder << R"("SelfMatchPreventionID": )";
    builder << +writer.selfMatchPreventionID();

    builder << ", ";
    builder << R"("CmtaGiveupCD": )";
    builder << '"' << writer.cmtaGiveupCD() << '"';

    builder << ", ";
    builder << R"("CustOrderCapacity": )";
    builder << '"' << writer.custOrderCapacity() << '"';

    builder << ", ";
    builder << R"("ClearingAccountType": )";
    builder << '"' << writer.clearingAccountType() << '"';

    builder << ", ";
    builder << R"("SelfMatchPreventionInstruction": )";
    builder << '"' << writer.selfMatchPreventionInstruction() << '"';

    builder << ", ";
    builder << R"("AvgPxIndicator": )";
    builder << '"' << writer.avgPxIndicator() << '"';

    builder << ", ";
    builder << R"("ClearingTradePriceType": )";
    builder << '"' << writer.clearingTradePriceType() << '"';

    builder << ", ";
    builder << R"("CustOrderHandlingInst": )";
    builder << '"' << writer.custOrderHandlingInst() << '"';

    builder << ", ";
    builder << R"("Executor": )";
    builder << +writer.executor();

    builder << ", ";
    builder << R"("IDMShortCode": )";
    builder << +writer.iDMShortCode();

    builder << ", ";
    {
        bool atLeastOne = false;
        builder << R"("NoPartyDetails": [)";
        writer.noPartyDetails().forEach(
            [&](NoPartyDetails &noPartyDetails)
            {
                if (atLeastOne)
                {
                    builder << ", ";
                }
                atLeastOne = true;
                builder << noPartyDetails;
            });
        builder << ']';
    }

    builder << ", ";
    {
        bool atLeastOne = false;
        builder << R"("NoTrdRegPublications": [)";
        writer.noTrdRegPublications().forEach(
            [&](NoTrdRegPublications &noTrdRegPublications)
            {
                if (atLeastOne)
                {
                    builder << ", ";
                }
                atLeastOne = true;
                builder << noTrdRegPublications;
            });
        builder << ']';
    }

    builder << '}';

    return builder;
}

void skip()
{
    auto &noPartyDetailsGroup { noPartyDetails() };
    while (noPartyDetailsGroup.hasNext())
    {
        noPartyDetailsGroup.next().skip();
    }
    auto &noTrdRegPublicationsGroup { noTrdRegPublications() };
    while (noTrdRegPublicationsGroup.hasNext())
    {
        noTrdRegPublicationsGroup.next().skip();
    }
}

SBE_NODISCARD static SBE_CONSTEXPR bool isConstLength() SBE_NOEXCEPT
{
    return false;
}

SBE_NODISCARD static std::size_t computeLength(
    std::size_t noPartyDetailsLength = 0,
    std::size_t noTrdRegPublicationsLength = 0)
{
#if defined(__GNUG__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wtype-limits"
#endif
    std::size_t length = sbeBlockLength();

    length += NoPartyDetails::sbeHeaderSize();
    if (noPartyDetailsLength > 254LL)
    {
        throw std::runtime_error("noPartyDetailsLength outside of allowed range [E110]");
    }
    length += noPartyDetailsLength *NoPartyDetails::sbeBlockLength();

    length += NoTrdRegPublications::sbeHeaderSize();
    if (noTrdRegPublicationsLength > 254LL)
    {
        throw std::runtime_error("noTrdRegPublicationsLength outside of allowed range [E110]");
    }
    length += noTrdRegPublicationsLength *NoTrdRegPublications::sbeBlockLength();

    return length;
#if defined(__GNUG__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
}
};
}
#endif
