/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/* Generated SBE (Simple Binary Encoding) message codec */
#ifndef _SBE_SECURITYSTATUSWORKUP60_H_
#define _SBE_SECURITYSTATUSWORKUP60_H_

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

#if defined(SBE_NO_BOUNDS_CHECK)
#  define SBE_BOUNDS_CHECK_EXPECT(exp, c) (false)
#elif defined(_MSC_VER)
#  define SBE_BOUNDS_CHECK_EXPECT(exp, c) (exp)
#else
#  define SBE_BOUNDS_CHECK_EXPECT(exp, c) (__builtin_expect(exp, c))
#endif

#define SBE_NULLVALUE_INT8 (std::numeric_limits<std::int8_t>::min)()
#define SBE_NULLVALUE_INT16 (std::numeric_limits<std::int16_t>::min)()
#define SBE_NULLVALUE_INT32 (std::numeric_limits<std::int32_t>::min)()
#define SBE_NULLVALUE_INT64 (std::numeric_limits<std::int64_t>::min)()
#define SBE_NULLVALUE_UINT8 (std::numeric_limits<std::uint8_t>::max)()
#define SBE_NULLVALUE_UINT16 (std::numeric_limits<std::uint16_t>::max)()
#define SBE_NULLVALUE_UINT32 (std::numeric_limits<std::uint32_t>::max)()
#define SBE_NULLVALUE_UINT64 (std::numeric_limits<std::uint64_t>::max)()


#include "MDEntryTypeBook.h"
#include "OpenCloseSettlFlag.h"
#include "MatchEventIndicator.h"
#include "MaturityMonthYear.h"
#include "MDEntryTypeDailyStatistics.h"
#include "EventType.h"
#include "DecimalQty.h"
#include "MDUpdateAction.h"
#include "PRICENULL9.h"
#include "RepoSubType.h"
#include "Side.h"
#include "GroupSize8Byte.h"
#include "HaltReason.h"
#include "PRICE9.h"
#include "MoneyOrPar.h"
#include "MDEntryType.h"
#include "SecurityTradingStatus.h"
#include "LegSide.h"
#include "MessageHeader.h"
#include "OrderUpdateAction.h"
#include "PutOrCall.h"
#include "SecurityTradingEvent.h"
#include "SecurityUpdateAction.h"
#include "Decimal9.h"
#include "MDEntryTypeStatistics.h"
#include "WorkupTradingStatus.h"
#include "InstAttribValue.h"
#include "AggressorSide.h"
#include "SecurityAltIDSource.h"
#include "AggressorFlag.h"
#include "PriceSource.h"
#include "GroupSize.h"
#include "SettlPriceType.h"
#include "Decimal9NULL.h"

namespace sbe {

class SecurityStatusWorkup60
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
    static const std::uint16_t SBE_BLOCK_LENGTH = static_cast<std::uint16_t>(30);
    static const std::uint16_t SBE_TEMPLATE_ID = static_cast<std::uint16_t>(60);
    static const std::uint16_t SBE_SCHEMA_ID = static_cast<std::uint16_t>(1);
    static const std::uint16_t SBE_SCHEMA_VERSION = static_cast<std::uint16_t>(12);

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

    SecurityStatusWorkup60() = default;

    SecurityStatusWorkup60(
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

    SecurityStatusWorkup60(char *buffer, const std::uint64_t bufferLength) :
        SecurityStatusWorkup60(buffer, 0, bufferLength, sbeBlockLength(), sbeSchemaVersion())
    {
    }

    SecurityStatusWorkup60(
        char *buffer,
        const std::uint64_t bufferLength,
        const std::uint64_t actingBlockLength,
        const std::uint64_t actingVersion) :
        SecurityStatusWorkup60(buffer, 0, bufferLength, actingBlockLength, actingVersion)
    {
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint16_t sbeBlockLength() SBE_NOEXCEPT
    {
        return static_cast<std::uint16_t>(30);
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t sbeBlockAndHeaderLength() SBE_NOEXCEPT
    {
        return messageHeader::encodedLength() + sbeBlockLength();
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint16_t sbeTemplateId() SBE_NOEXCEPT
    {
        return static_cast<std::uint16_t>(60);
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint16_t sbeSchemaId() SBE_NOEXCEPT
    {
        return static_cast<std::uint16_t>(1);
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint16_t sbeSchemaVersion() SBE_NOEXCEPT
    {
        return static_cast<std::uint16_t>(12);
    }

    SBE_NODISCARD static SBE_CONSTEXPR const char *sbeSemanticType() SBE_NOEXCEPT
    {
        return "f";
    }

    SBE_NODISCARD std::uint64_t offset() const SBE_NOEXCEPT
    {
        return m_offset;
    }

    SecurityStatusWorkup60 &wrapForEncode(char *buffer, const std::uint64_t offset, const std::uint64_t bufferLength)
    {
        return *this = SecurityStatusWorkup60(buffer, offset, bufferLength, sbeBlockLength(), sbeSchemaVersion());
    }

    SecurityStatusWorkup60 &wrapAndApplyHeader(char *buffer, const std::uint64_t offset, const std::uint64_t bufferLength)
    {
        messageHeader hdr(buffer, offset, bufferLength, sbeSchemaVersion());

        hdr
            .blockLength(sbeBlockLength())
            .templateId(sbeTemplateId())
            .schemaId(sbeSchemaId())
            .version(sbeSchemaVersion());

        return *this = SecurityStatusWorkup60(
            buffer,
            offset + messageHeader::encodedLength(),
            bufferLength,
            sbeBlockLength(),
            sbeSchemaVersion());
    }

    SecurityStatusWorkup60 &wrapForDecode(
        char *buffer,
        const std::uint64_t offset,
        const std::uint64_t actingBlockLength,
        const std::uint64_t actingVersion,
        const std::uint64_t bufferLength)
    {
        return *this = SecurityStatusWorkup60(buffer, offset, bufferLength, actingBlockLength, actingVersion);
    }

    SecurityStatusWorkup60 &sbeRewind()
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
        SecurityStatusWorkup60 skipper(m_buffer, m_offset, m_bufferLength, sbeBlockLength(), m_actingVersion);
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

    SBE_NODISCARD static const char *TransactTimeMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "UTCTimestamp";
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
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= transactTimeSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t transactTimeEncodingOffset() SBE_NOEXCEPT
    {
        return 0;
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
        std::memcpy(&val, m_buffer + m_offset + 0, sizeof(std::uint64_t));
        return SBE_LITTLE_ENDIAN_ENCODE_64(val);
    }

    SecurityStatusWorkup60 &transactTime(const std::uint64_t value) SBE_NOEXCEPT
    {
        std::uint64_t val = SBE_LITTLE_ENDIAN_ENCODE_64(value);
        std::memcpy(m_buffer + m_offset + 0, &val, sizeof(std::uint64_t));
        return *this;
    }

    SBE_NODISCARD static const char *MDEntryPxMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "Price";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t mDEntryPxId() SBE_NOEXCEPT
    {
        return 270;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t mDEntryPxSinceVersion() SBE_NOEXCEPT
    {
        return 9;
    }

    SBE_NODISCARD bool mDEntryPxInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= mDEntryPxSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t mDEntryPxEncodingOffset() SBE_NOEXCEPT
    {
        return 8;
    }

private:
    PRICENULL9 m_mDEntryPx;

public:
    SBE_NODISCARD PRICENULL9 &mDEntryPx()
    {
        m_mDEntryPx.wrap(m_buffer, m_offset + 8, m_actingVersion, m_bufferLength);
        return m_mDEntryPx;
    }

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
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= securityIDSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t securityIDEncodingOffset() SBE_NOEXCEPT
    {
        return 16;
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
        std::memcpy(&val, m_buffer + m_offset + 16, sizeof(std::int32_t));
        return SBE_LITTLE_ENDIAN_ENCODE_32(val);
    }

    SecurityStatusWorkup60 &securityID(const std::int32_t value) SBE_NOEXCEPT
    {
        std::int32_t val = SBE_LITTLE_ENDIAN_ENCODE_32(value);
        std::memcpy(m_buffer + m_offset + 16, &val, sizeof(std::int32_t));
        return *this;
    }

    SBE_NODISCARD static const char *MatchEventIndicatorMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "MultipleCharValue";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t matchEventIndicatorId() SBE_NOEXCEPT
    {
        return 5799;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t matchEventIndicatorSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool matchEventIndicatorInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= matchEventIndicatorSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t matchEventIndicatorEncodingOffset() SBE_NOEXCEPT
    {
        return 20;
    }

private:
    MatchEventIndicator m_matchEventIndicator;

public:
    SBE_NODISCARD MatchEventIndicator &matchEventIndicator()
    {
        m_matchEventIndicator.wrap(m_buffer, m_offset + 20, m_actingVersion, m_bufferLength);
        return m_matchEventIndicator;
    }

    static SBE_CONSTEXPR std::size_t matchEventIndicatorEncodingLength() SBE_NOEXCEPT
    {
        return 1;
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
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= tradeDateSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t tradeDateEncodingOffset() SBE_NOEXCEPT
    {
        return 21;
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
        std::memcpy(&val, m_buffer + m_offset + 21, sizeof(std::uint16_t));
        return SBE_LITTLE_ENDIAN_ENCODE_16(val);
    }

    SecurityStatusWorkup60 &tradeDate(const std::uint16_t value) SBE_NOEXCEPT
    {
        std::uint16_t val = SBE_LITTLE_ENDIAN_ENCODE_16(value);
        std::memcpy(m_buffer + m_offset + 21, &val, sizeof(std::uint16_t));
        return *this;
    }

    SBE_NODISCARD static const char *TradeLinkIDMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
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
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= tradeLinkIDSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t tradeLinkIDEncodingOffset() SBE_NOEXCEPT
    {
        return 23;
    }

    static SBE_CONSTEXPR std::uint32_t tradeLinkIDNullValue() SBE_NOEXCEPT
    {
        return SBE_NULLVALUE_UINT32;
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
        std::memcpy(&val, m_buffer + m_offset + 23, sizeof(std::uint32_t));
        return SBE_LITTLE_ENDIAN_ENCODE_32(val);
    }

    SecurityStatusWorkup60 &tradeLinkID(const std::uint32_t value) SBE_NOEXCEPT
    {
        std::uint32_t val = SBE_LITTLE_ENDIAN_ENCODE_32(value);
        std::memcpy(m_buffer + m_offset + 23, &val, sizeof(std::uint32_t));
        return *this;
    }

    SBE_NODISCARD static const char *SecurityTradingStatusMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t securityTradingStatusId() SBE_NOEXCEPT
    {
        return 326;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t securityTradingStatusSinceVersion() SBE_NOEXCEPT
    {
        return 10;
    }

    SBE_NODISCARD bool securityTradingStatusInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= securityTradingStatusSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t securityTradingStatusEncodingOffset() SBE_NOEXCEPT
    {
        return 27;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t securityTradingStatusEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD std::uint8_t securityTradingStatusRaw() const SBE_NOEXCEPT
    {
        if (m_actingVersion < 10)
        {
            return static_cast<std::uint8_t>(255);
        }

        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 27, sizeof(std::uint8_t));
        return (val);
    }

    SBE_NODISCARD WorkupTradingStatus::Value securityTradingStatus() const
    {
        if (m_actingVersion < 10)
        {
            return WorkupTradingStatus::NULL_VALUE;
        }

        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 27, sizeof(std::uint8_t));
        return WorkupTradingStatus::get((val));
    }

    SecurityStatusWorkup60 &securityTradingStatus(const WorkupTradingStatus::Value value) SBE_NOEXCEPT
    {
        std::uint8_t val = (value);
        std::memcpy(m_buffer + m_offset + 27, &val, sizeof(std::uint8_t));
        return *this;
    }

    SBE_NODISCARD static const char *HaltReasonMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t haltReasonId() SBE_NOEXCEPT
    {
        return 327;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t haltReasonSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool haltReasonInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= haltReasonSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t haltReasonEncodingOffset() SBE_NOEXCEPT
    {
        return 28;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t haltReasonEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD std::uint8_t haltReasonRaw() const SBE_NOEXCEPT
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 28, sizeof(std::uint8_t));
        return (val);
    }

    SBE_NODISCARD HaltReason::Value haltReason() const
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 28, sizeof(std::uint8_t));
        return HaltReason::get((val));
    }

    SecurityStatusWorkup60 &haltReason(const HaltReason::Value value) SBE_NOEXCEPT
    {
        std::uint8_t val = (value);
        std::memcpy(m_buffer + m_offset + 28, &val, sizeof(std::uint8_t));
        return *this;
    }

    SBE_NODISCARD static const char *SecurityTradingEventMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t securityTradingEventId() SBE_NOEXCEPT
    {
        return 1174;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t securityTradingEventSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool securityTradingEventInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= securityTradingEventSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t securityTradingEventEncodingOffset() SBE_NOEXCEPT
    {
        return 29;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t securityTradingEventEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD std::uint8_t securityTradingEventRaw() const SBE_NOEXCEPT
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 29, sizeof(std::uint8_t));
        return (val);
    }

    SBE_NODISCARD SecurityTradingEvent::Value securityTradingEvent() const
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 29, sizeof(std::uint8_t));
        return SecurityTradingEvent::get((val));
    }

    SecurityStatusWorkup60 &securityTradingEvent(const SecurityTradingEvent::Value value) SBE_NOEXCEPT
    {
        std::uint8_t val = (value);
        std::memcpy(m_buffer + m_offset + 29, &val, sizeof(std::uint8_t));
        return *this;
    }

    class NoOrderIDEntries
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
            dimensions.blockLength(static_cast<std::uint16_t>(10));
            dimensions.numInGroup(static_cast<std::uint8_t>(count));
            m_index = 0;
            m_count = count;
            m_blockLength = 10;
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
            return 10;
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

        inline NoOrderIDEntries &next()
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

    #if __cplusplus < 201103L
        template<class Func> inline void forEach(Func &func)
        {
            while (hasNext())
            {
                next();
                func(*this);
            }
        }

    #else
        template<class Func> inline void forEach(Func &&func)
        {
            while (hasNext())
            {
                next();
                func(*this);
            }
        }

    #endif

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
    #if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wtautological-compare"
    #endif
            return m_actingVersion >= orderIDSinceVersion();
    #if defined(__clang__)
    #pragma clang diagnostic pop
    #endif
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::size_t orderIDEncodingOffset() SBE_NOEXCEPT
        {
            return 0;
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
            std::memcpy(&val, m_buffer + m_offset + 0, sizeof(std::uint64_t));
            return SBE_LITTLE_ENDIAN_ENCODE_64(val);
        }

        NoOrderIDEntries &orderID(const std::uint64_t value) SBE_NOEXCEPT
        {
            std::uint64_t val = SBE_LITTLE_ENDIAN_ENCODE_64(value);
            std::memcpy(m_buffer + m_offset + 0, &val, sizeof(std::uint64_t));
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
            return 10;
        }

        SBE_NODISCARD bool sideInActingVersion() SBE_NOEXCEPT
        {
    #if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wtautological-compare"
    #endif
            return m_actingVersion >= sideSinceVersion();
    #if defined(__clang__)
    #pragma clang diagnostic pop
    #endif
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::size_t sideEncodingOffset() SBE_NOEXCEPT
        {
            return 8;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::size_t sideEncodingLength() SBE_NOEXCEPT
        {
            return 1;
        }

        SBE_NODISCARD std::uint8_t sideRaw() const SBE_NOEXCEPT
        {
            if (m_actingVersion < 10)
            {
                return static_cast<std::uint8_t>(255);
            }

            std::uint8_t val;
            std::memcpy(&val, m_buffer + m_offset + 8, sizeof(std::uint8_t));
            return (val);
        }

        SBE_NODISCARD Side::Value side() const
        {
            if (m_actingVersion < 10)
            {
                return Side::NULL_VALUE;
            }

            std::uint8_t val;
            std::memcpy(&val, m_buffer + m_offset + 8, sizeof(std::uint8_t));
            return Side::get((val));
        }

        NoOrderIDEntries &side(const Side::Value value) SBE_NOEXCEPT
        {
            std::uint8_t val = (value);
            std::memcpy(m_buffer + m_offset + 8, &val, sizeof(std::uint8_t));
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
            return 10;
        }

        SBE_NODISCARD bool aggressorIndicatorInActingVersion() SBE_NOEXCEPT
        {
    #if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wtautological-compare"
    #endif
            return m_actingVersion >= aggressorIndicatorSinceVersion();
    #if defined(__clang__)
    #pragma clang diagnostic pop
    #endif
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::size_t aggressorIndicatorEncodingOffset() SBE_NOEXCEPT
        {
            return 9;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::size_t aggressorIndicatorEncodingLength() SBE_NOEXCEPT
        {
            return 1;
        }

        SBE_NODISCARD std::uint8_t aggressorIndicatorRaw() const SBE_NOEXCEPT
        {
            if (m_actingVersion < 10)
            {
                return static_cast<std::uint8_t>(255);
            }

            std::uint8_t val;
            std::memcpy(&val, m_buffer + m_offset + 9, sizeof(std::uint8_t));
            return (val);
        }

        SBE_NODISCARD AggressorFlag::Value aggressorIndicator() const
        {
            if (m_actingVersion < 10)
            {
                return AggressorFlag::NULL_VALUE;
            }

            std::uint8_t val;
            std::memcpy(&val, m_buffer + m_offset + 9, sizeof(std::uint8_t));
            return AggressorFlag::get((val));
        }

        NoOrderIDEntries &aggressorIndicator(const AggressorFlag::Value value) SBE_NOEXCEPT
        {
            std::uint8_t val = (value);
            std::memcpy(m_buffer + m_offset + 9, &val, sizeof(std::uint8_t));
            return *this;
        }

        template<typename CharT, typename Traits>
        friend std::basic_ostream<CharT, Traits> & operator << (
            std::basic_ostream<CharT, Traits> &builder, NoOrderIDEntries writer)
        {
            builder << '{';
            builder << R"("OrderID": )";
            builder << +writer.orderID();

            builder << ", ";
            builder << R"("Side": )";
            builder << '"' << writer.side() << '"';

            builder << ", ";
            builder << R"("AggressorIndicator": )";
            builder << '"' << writer.aggressorIndicator() << '"';

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
    NoOrderIDEntries m_noOrderIDEntries;

public:
    SBE_NODISCARD static SBE_CONSTEXPR std::uint16_t NoOrderIDEntriesId() SBE_NOEXCEPT
    {
        return 37705;
    }

    SBE_NODISCARD inline NoOrderIDEntries &noOrderIDEntries()
    {
        m_noOrderIDEntries.wrapForDecode(m_buffer, sbePositionPtr(), m_actingVersion, m_bufferLength);
        return m_noOrderIDEntries;
    }

    NoOrderIDEntries &noOrderIDEntriesCount(const std::uint8_t count)
    {
        m_noOrderIDEntries.wrapForEncode(m_buffer, count, sbePositionPtr(), m_actingVersion, m_bufferLength);
        return m_noOrderIDEntries;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t noOrderIDEntriesSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool noOrderIDEntriesInActingVersion() const SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= noOrderIDEntriesSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

template<typename CharT, typename Traits>
friend std::basic_ostream<CharT, Traits> & operator << (
    std::basic_ostream<CharT, Traits> &builder, SecurityStatusWorkup60 _writer)
{
    SecurityStatusWorkup60 writer(
        _writer.m_buffer,
        _writer.m_offset,
        _writer.m_bufferLength,
        _writer.m_actingBlockLength,
        _writer.m_actingVersion);

    builder << '{';
    builder << R"("Name": "SecurityStatusWorkup60", )";
    builder << R"("sbeTemplateId": )";
    builder << writer.sbeTemplateId();
    builder << ", ";

    builder << R"("TransactTime": )";
    builder << +writer.transactTime();

    builder << ", ";
    builder << R"("MDEntryPx": )";
    if (writer.mDEntryPxInActingVersion())
    {
        builder << writer.mDEntryPx();
    }
    else
    {
        builder << "{}";
    }

    builder << ", ";
    builder << R"("SecurityID": )";
    builder << +writer.securityID();

    builder << ", ";
    builder << R"("MatchEventIndicator": )";
    builder << writer.matchEventIndicator();

    builder << ", ";
    builder << R"("TradeDate": )";
    builder << +writer.tradeDate();

    builder << ", ";
    builder << R"("TradeLinkID": )";
    builder << +writer.tradeLinkID();

    builder << ", ";
    builder << R"("SecurityTradingStatus": )";
    builder << '"' << writer.securityTradingStatus() << '"';

    builder << ", ";
    builder << R"("HaltReason": )";
    builder << '"' << writer.haltReason() << '"';

    builder << ", ";
    builder << R"("SecurityTradingEvent": )";
    builder << '"' << writer.securityTradingEvent() << '"';

    builder << ", ";
    {
        bool atLeastOne = false;
        builder << R"("NoOrderIDEntries": [)";
        writer.noOrderIDEntries().forEach(
            [&](NoOrderIDEntries &noOrderIDEntries)
            {
                if (atLeastOne)
                {
                    builder << ", ";
                }
                atLeastOne = true;
                builder << noOrderIDEntries;
            });
        builder << ']';
    }

    builder << '}';

    return builder;
}

void skip()
{
    noOrderIDEntries().forEach([](NoOrderIDEntries &e){ e.skip(); });
}

SBE_NODISCARD static SBE_CONSTEXPR bool isConstLength() SBE_NOEXCEPT
{
    return false;
}

SBE_NODISCARD static std::size_t computeLength(std::size_t noOrderIDEntriesLength = 0)
{
#if defined(__GNUG__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wtype-limits"
#endif
    std::size_t length = sbeBlockLength();

    length += NoOrderIDEntries::sbeHeaderSize();
    if (noOrderIDEntriesLength > 254LL)
    {
        throw std::runtime_error("noOrderIDEntriesLength outside of allowed range [E110]");
    }
    length += noOrderIDEntriesLength *NoOrderIDEntries::sbeBlockLength();

    return length;
#if defined(__GNUG__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
}
};
}
#endif
