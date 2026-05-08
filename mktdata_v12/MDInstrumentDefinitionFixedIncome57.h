/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/* Generated SBE (Simple Binary Encoding) message codec */
#ifndef _SBE_MDINSTRUMENTDEFINITIONFIXEDINCOME57_H_
#define _SBE_MDINSTRUMENTDEFINITIONFIXEDINCOME57_H_

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

class MDInstrumentDefinitionFixedIncome57
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
    static const std::uint16_t SBE_BLOCK_LENGTH = static_cast<std::uint16_t>(338);
    static const std::uint16_t SBE_TEMPLATE_ID = static_cast<std::uint16_t>(57);
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

    MDInstrumentDefinitionFixedIncome57() = default;

    MDInstrumentDefinitionFixedIncome57(
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

    MDInstrumentDefinitionFixedIncome57(char *buffer, const std::uint64_t bufferLength) :
        MDInstrumentDefinitionFixedIncome57(buffer, 0, bufferLength, sbeBlockLength(), sbeSchemaVersion())
    {
    }

    MDInstrumentDefinitionFixedIncome57(
        char *buffer,
        const std::uint64_t bufferLength,
        const std::uint64_t actingBlockLength,
        const std::uint64_t actingVersion) :
        MDInstrumentDefinitionFixedIncome57(buffer, 0, bufferLength, actingBlockLength, actingVersion)
    {
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint16_t sbeBlockLength() SBE_NOEXCEPT
    {
        return static_cast<std::uint16_t>(338);
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t sbeBlockAndHeaderLength() SBE_NOEXCEPT
    {
        return messageHeader::encodedLength() + sbeBlockLength();
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint16_t sbeTemplateId() SBE_NOEXCEPT
    {
        return static_cast<std::uint16_t>(57);
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
        return "d";
    }

    SBE_NODISCARD std::uint64_t offset() const SBE_NOEXCEPT
    {
        return m_offset;
    }

    MDInstrumentDefinitionFixedIncome57 &wrapForEncode(char *buffer, const std::uint64_t offset, const std::uint64_t bufferLength)
    {
        return *this = MDInstrumentDefinitionFixedIncome57(buffer, offset, bufferLength, sbeBlockLength(), sbeSchemaVersion());
    }

    MDInstrumentDefinitionFixedIncome57 &wrapAndApplyHeader(char *buffer, const std::uint64_t offset, const std::uint64_t bufferLength)
    {
        messageHeader hdr(buffer, offset, bufferLength, sbeSchemaVersion());

        hdr
            .blockLength(sbeBlockLength())
            .templateId(sbeTemplateId())
            .schemaId(sbeSchemaId())
            .version(sbeSchemaVersion());

        return *this = MDInstrumentDefinitionFixedIncome57(
            buffer,
            offset + messageHeader::encodedLength(),
            bufferLength,
            sbeBlockLength(),
            sbeSchemaVersion());
    }

    MDInstrumentDefinitionFixedIncome57 &wrapForDecode(
        char *buffer,
        const std::uint64_t offset,
        const std::uint64_t actingBlockLength,
        const std::uint64_t actingVersion,
        const std::uint64_t bufferLength)
    {
        return *this = MDInstrumentDefinitionFixedIncome57(buffer, offset, bufferLength, actingBlockLength, actingVersion);
    }

    MDInstrumentDefinitionFixedIncome57 &sbeRewind()
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
        MDInstrumentDefinitionFixedIncome57 skipper(m_buffer, m_offset, m_bufferLength, sbeBlockLength(), m_actingVersion);
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
        return 0;
    }

private:
    MatchEventIndicator m_matchEventIndicator;

public:
    SBE_NODISCARD MatchEventIndicator &matchEventIndicator()
    {
        m_matchEventIndicator.wrap(m_buffer, m_offset + 0, m_actingVersion, m_bufferLength);
        return m_matchEventIndicator;
    }

    static SBE_CONSTEXPR std::size_t matchEventIndicatorEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD static const char *TotNumReportsMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "optional";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t totNumReportsId() SBE_NOEXCEPT
    {
        return 911;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t totNumReportsSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool totNumReportsInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= totNumReportsSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t totNumReportsEncodingOffset() SBE_NOEXCEPT
    {
        return 1;
    }

    static SBE_CONSTEXPR std::uint32_t totNumReportsNullValue() SBE_NOEXCEPT
    {
        return UINT32_C(0xffffffff);
    }

    static SBE_CONSTEXPR std::uint32_t totNumReportsMinValue() SBE_NOEXCEPT
    {
        return UINT32_C(0x0);
    }

    static SBE_CONSTEXPR std::uint32_t totNumReportsMaxValue() SBE_NOEXCEPT
    {
        return UINT32_C(0xfffffffe);
    }

    static SBE_CONSTEXPR std::size_t totNumReportsEncodingLength() SBE_NOEXCEPT
    {
        return 4;
    }

    SBE_NODISCARD std::uint32_t totNumReports() const SBE_NOEXCEPT
    {
        std::uint32_t val;
        std::memcpy(&val, m_buffer + m_offset + 1, sizeof(std::uint32_t));
        return SBE_LITTLE_ENDIAN_ENCODE_32(val);
    }

    MDInstrumentDefinitionFixedIncome57 &totNumReports(const std::uint32_t value) SBE_NOEXCEPT
    {
        std::uint32_t val = SBE_LITTLE_ENDIAN_ENCODE_32(value);
        std::memcpy(m_buffer + m_offset + 1, &val, sizeof(std::uint32_t));
        return *this;
    }

    SBE_NODISCARD static const char *SecurityUpdateActionMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "char";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t securityUpdateActionId() SBE_NOEXCEPT
    {
        return 980;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t securityUpdateActionSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool securityUpdateActionInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= securityUpdateActionSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t securityUpdateActionEncodingOffset() SBE_NOEXCEPT
    {
        return 5;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t securityUpdateActionEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD char securityUpdateActionRaw() const SBE_NOEXCEPT
    {
        char val;
        std::memcpy(&val, m_buffer + m_offset + 5, sizeof(char));
        return (val);
    }

    SBE_NODISCARD SecurityUpdateAction::Value securityUpdateAction() const
    {
        char val;
        std::memcpy(&val, m_buffer + m_offset + 5, sizeof(char));
        return SecurityUpdateAction::get((val));
    }

    MDInstrumentDefinitionFixedIncome57 &securityUpdateAction(const SecurityUpdateAction::Value value) SBE_NOEXCEPT
    {
        char val = (value);
        std::memcpy(m_buffer + m_offset + 5, &val, sizeof(char));
        return *this;
    }

    SBE_NODISCARD static const char *LastUpdateTimeMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "UTCTimestamp";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t lastUpdateTimeId() SBE_NOEXCEPT
    {
        return 779;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t lastUpdateTimeSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool lastUpdateTimeInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= lastUpdateTimeSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t lastUpdateTimeEncodingOffset() SBE_NOEXCEPT
    {
        return 6;
    }

    static SBE_CONSTEXPR std::uint64_t lastUpdateTimeNullValue() SBE_NOEXCEPT
    {
        return SBE_NULLVALUE_UINT64;
    }

    static SBE_CONSTEXPR std::uint64_t lastUpdateTimeMinValue() SBE_NOEXCEPT
    {
        return UINT64_C(0x0);
    }

    static SBE_CONSTEXPR std::uint64_t lastUpdateTimeMaxValue() SBE_NOEXCEPT
    {
        return UINT64_C(0xfffffffffffffffe);
    }

    static SBE_CONSTEXPR std::size_t lastUpdateTimeEncodingLength() SBE_NOEXCEPT
    {
        return 8;
    }

    SBE_NODISCARD std::uint64_t lastUpdateTime() const SBE_NOEXCEPT
    {
        std::uint64_t val;
        std::memcpy(&val, m_buffer + m_offset + 6, sizeof(std::uint64_t));
        return SBE_LITTLE_ENDIAN_ENCODE_64(val);
    }

    MDInstrumentDefinitionFixedIncome57 &lastUpdateTime(const std::uint64_t value) SBE_NOEXCEPT
    {
        std::uint64_t val = SBE_LITTLE_ENDIAN_ENCODE_64(value);
        std::memcpy(m_buffer + m_offset + 6, &val, sizeof(std::uint64_t));
        return *this;
    }

    SBE_NODISCARD static const char *MDSecurityTradingStatusMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t mDSecurityTradingStatusId() SBE_NOEXCEPT
    {
        return 1682;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t mDSecurityTradingStatusSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool mDSecurityTradingStatusInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= mDSecurityTradingStatusSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t mDSecurityTradingStatusEncodingOffset() SBE_NOEXCEPT
    {
        return 14;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t mDSecurityTradingStatusEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD std::uint8_t mDSecurityTradingStatusRaw() const SBE_NOEXCEPT
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 14, sizeof(std::uint8_t));
        return (val);
    }

    SBE_NODISCARD SecurityTradingStatus::Value mDSecurityTradingStatus() const
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 14, sizeof(std::uint8_t));
        return SecurityTradingStatus::get((val));
    }

    MDInstrumentDefinitionFixedIncome57 &mDSecurityTradingStatus(const SecurityTradingStatus::Value value) SBE_NOEXCEPT
    {
        std::uint8_t val = (value);
        std::memcpy(m_buffer + m_offset + 14, &val, sizeof(std::uint8_t));
        return *this;
    }

    SBE_NODISCARD static const char *ApplIDMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t applIDId() SBE_NOEXCEPT
    {
        return 1180;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t applIDSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool applIDInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= applIDSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t applIDEncodingOffset() SBE_NOEXCEPT
    {
        return 15;
    }

    static SBE_CONSTEXPR std::int16_t applIDNullValue() SBE_NOEXCEPT
    {
        return SBE_NULLVALUE_INT16;
    }

    static SBE_CONSTEXPR std::int16_t applIDMinValue() SBE_NOEXCEPT
    {
        return static_cast<std::int16_t>(-32767);
    }

    static SBE_CONSTEXPR std::int16_t applIDMaxValue() SBE_NOEXCEPT
    {
        return static_cast<std::int16_t>(32767);
    }

    static SBE_CONSTEXPR std::size_t applIDEncodingLength() SBE_NOEXCEPT
    {
        return 2;
    }

    SBE_NODISCARD std::int16_t applID() const SBE_NOEXCEPT
    {
        std::int16_t val;
        std::memcpy(&val, m_buffer + m_offset + 15, sizeof(std::int16_t));
        return SBE_LITTLE_ENDIAN_ENCODE_16(val);
    }

    MDInstrumentDefinitionFixedIncome57 &applID(const std::int16_t value) SBE_NOEXCEPT
    {
        std::int16_t val = SBE_LITTLE_ENDIAN_ENCODE_16(value);
        std::memcpy(m_buffer + m_offset + 15, &val, sizeof(std::int16_t));
        return *this;
    }

    SBE_NODISCARD static const char *MarketSegmentIDMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t marketSegmentIDId() SBE_NOEXCEPT
    {
        return 1300;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t marketSegmentIDSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool marketSegmentIDInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= marketSegmentIDSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t marketSegmentIDEncodingOffset() SBE_NOEXCEPT
    {
        return 17;
    }

    static SBE_CONSTEXPR std::uint8_t marketSegmentIDNullValue() SBE_NOEXCEPT
    {
        return SBE_NULLVALUE_UINT8;
    }

    static SBE_CONSTEXPR std::uint8_t marketSegmentIDMinValue() SBE_NOEXCEPT
    {
        return static_cast<std::uint8_t>(0);
    }

    static SBE_CONSTEXPR std::uint8_t marketSegmentIDMaxValue() SBE_NOEXCEPT
    {
        return static_cast<std::uint8_t>(254);
    }

    static SBE_CONSTEXPR std::size_t marketSegmentIDEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD std::uint8_t marketSegmentID() const SBE_NOEXCEPT
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 17, sizeof(std::uint8_t));
        return (val);
    }

    MDInstrumentDefinitionFixedIncome57 &marketSegmentID(const std::uint8_t value) SBE_NOEXCEPT
    {
        std::uint8_t val = (value);
        std::memcpy(m_buffer + m_offset + 17, &val, sizeof(std::uint8_t));
        return *this;
    }

    SBE_NODISCARD static const char *UnderlyingProductMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t underlyingProductId() SBE_NOEXCEPT
    {
        return 462;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t underlyingProductSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool underlyingProductInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= underlyingProductSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t underlyingProductEncodingOffset() SBE_NOEXCEPT
    {
        return 18;
    }

    static SBE_CONSTEXPR std::uint8_t underlyingProductNullValue() SBE_NOEXCEPT
    {
        return SBE_NULLVALUE_UINT8;
    }

    static SBE_CONSTEXPR std::uint8_t underlyingProductMinValue() SBE_NOEXCEPT
    {
        return static_cast<std::uint8_t>(0);
    }

    static SBE_CONSTEXPR std::uint8_t underlyingProductMaxValue() SBE_NOEXCEPT
    {
        return static_cast<std::uint8_t>(254);
    }

    static SBE_CONSTEXPR std::size_t underlyingProductEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD std::uint8_t underlyingProduct() const SBE_NOEXCEPT
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 18, sizeof(std::uint8_t));
        return (val);
    }

    MDInstrumentDefinitionFixedIncome57 &underlyingProduct(const std::uint8_t value) SBE_NOEXCEPT
    {
        std::uint8_t val = (value);
        std::memcpy(m_buffer + m_offset + 18, &val, sizeof(std::uint8_t));
        return *this;
    }

    SBE_NODISCARD static const char *SecurityExchangeMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "Exchange";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t securityExchangeId() SBE_NOEXCEPT
    {
        return 207;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t securityExchangeSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool securityExchangeInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= securityExchangeSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t securityExchangeEncodingOffset() SBE_NOEXCEPT
    {
        return 19;
    }

    static SBE_CONSTEXPR char securityExchangeNullValue() SBE_NOEXCEPT
    {
        return static_cast<char>(0);
    }

    static SBE_CONSTEXPR char securityExchangeMinValue() SBE_NOEXCEPT
    {
        return static_cast<char>(32);
    }

    static SBE_CONSTEXPR char securityExchangeMaxValue() SBE_NOEXCEPT
    {
        return static_cast<char>(126);
    }

    static SBE_CONSTEXPR std::size_t securityExchangeEncodingLength() SBE_NOEXCEPT
    {
        return 4;
    }

    static SBE_CONSTEXPR std::uint64_t securityExchangeLength() SBE_NOEXCEPT
    {
        return 4;
    }

    SBE_NODISCARD const char *securityExchange() const SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 19;
    }

    SBE_NODISCARD char *securityExchange() SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 19;
    }

    SBE_NODISCARD char securityExchange(const std::uint64_t index) const
    {
        if (index >= 4)
        {
            throw std::runtime_error("index out of range for securityExchange [E104]");
        }

        char val;
        std::memcpy(&val, m_buffer + m_offset + 19 + (index * 1), sizeof(char));
        return (val);
    }

    MDInstrumentDefinitionFixedIncome57 &securityExchange(const std::uint64_t index, const char value)
    {
        if (index >= 4)
        {
            throw std::runtime_error("index out of range for securityExchange [E105]");
        }

        char val = (value);
        std::memcpy(m_buffer + m_offset + 19 + (index * 1), &val, sizeof(char));
        return *this;
    }

    std::uint64_t getSecurityExchange(char *const dst, const std::uint64_t length) const
    {
        if (length > 4)
        {
            throw std::runtime_error("length too large for getSecurityExchange [E106]");
        }

        std::memcpy(dst, m_buffer + m_offset + 19, sizeof(char) * static_cast<std::size_t>(length));
        return length;
    }

    MDInstrumentDefinitionFixedIncome57 &putSecurityExchange(const char *const src) SBE_NOEXCEPT
    {
        std::memcpy(m_buffer + m_offset + 19, src, sizeof(char) * 4);
        return *this;
    }

    MDInstrumentDefinitionFixedIncome57 &putSecurityExchange(
        const char value0,
        const char value1,
        const char value2,
        const char value3) SBE_NOEXCEPT
    {
        char val0 = (value0);
        std::memcpy(m_buffer + m_offset + 19, &val0, sizeof(char));
        char val1 = (value1);
        std::memcpy(m_buffer + m_offset + 20, &val1, sizeof(char));
        char val2 = (value2);
        std::memcpy(m_buffer + m_offset + 21, &val2, sizeof(char));
        char val3 = (value3);
        std::memcpy(m_buffer + m_offset + 22, &val3, sizeof(char));

        return *this;
    }

    SBE_NODISCARD std::string getSecurityExchangeAsString() const
    {
        const char *buffer = m_buffer + m_offset + 19;
        std::size_t length = 0;

        for (; length < 4 && *(buffer + length) != '\0'; ++length);
        std::string result(buffer, length);

        return result;
    }

    std::string getSecurityExchangeAsJsonEscapedString()
    {
        std::ostringstream oss;
        std::string s = getSecurityExchangeAsString();

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
    SBE_NODISCARD std::string_view getSecurityExchangeAsStringView() const SBE_NOEXCEPT
    {
        const char *buffer = m_buffer + m_offset + 19;
        std::size_t length = 0;

        for (; length < 4 && *(buffer + length) != '\0'; ++length);
        std::string_view result(buffer, length);

        return result;
    }
    #endif

    #if __cplusplus >= 201703L
    MDInstrumentDefinitionFixedIncome57 &putSecurityExchange(const std::string_view str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 4)
        {
            throw std::runtime_error("string too large for putSecurityExchange [E106]");
        }

        std::memcpy(m_buffer + m_offset + 19, str.data(), srcLength);
        for (std::size_t start = srcLength; start < 4; ++start)
        {
            m_buffer[m_offset + 19 + start] = 0;
        }

        return *this;
    }
    #else
    MDInstrumentDefinitionFixedIncome57 &putSecurityExchange(const std::string &str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 4)
        {
            throw std::runtime_error("string too large for putSecurityExchange [E106]");
        }

        std::memcpy(m_buffer + m_offset + 19, str.c_str(), srcLength);
        for (std::size_t start = srcLength; start < 4; ++start)
        {
            m_buffer[m_offset + 19 + start] = 0;
        }

        return *this;
    }
    #endif

    SBE_NODISCARD static const char *SecurityGroupMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "String";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t securityGroupId() SBE_NOEXCEPT
    {
        return 1151;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t securityGroupSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool securityGroupInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= securityGroupSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t securityGroupEncodingOffset() SBE_NOEXCEPT
    {
        return 23;
    }

    static SBE_CONSTEXPR char securityGroupNullValue() SBE_NOEXCEPT
    {
        return static_cast<char>(0);
    }

    static SBE_CONSTEXPR char securityGroupMinValue() SBE_NOEXCEPT
    {
        return static_cast<char>(32);
    }

    static SBE_CONSTEXPR char securityGroupMaxValue() SBE_NOEXCEPT
    {
        return static_cast<char>(126);
    }

    static SBE_CONSTEXPR std::size_t securityGroupEncodingLength() SBE_NOEXCEPT
    {
        return 6;
    }

    static SBE_CONSTEXPR std::uint64_t securityGroupLength() SBE_NOEXCEPT
    {
        return 6;
    }

    SBE_NODISCARD const char *securityGroup() const SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 23;
    }

    SBE_NODISCARD char *securityGroup() SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 23;
    }

    SBE_NODISCARD char securityGroup(const std::uint64_t index) const
    {
        if (index >= 6)
        {
            throw std::runtime_error("index out of range for securityGroup [E104]");
        }

        char val;
        std::memcpy(&val, m_buffer + m_offset + 23 + (index * 1), sizeof(char));
        return (val);
    }

    MDInstrumentDefinitionFixedIncome57 &securityGroup(const std::uint64_t index, const char value)
    {
        if (index >= 6)
        {
            throw std::runtime_error("index out of range for securityGroup [E105]");
        }

        char val = (value);
        std::memcpy(m_buffer + m_offset + 23 + (index * 1), &val, sizeof(char));
        return *this;
    }

    std::uint64_t getSecurityGroup(char *const dst, const std::uint64_t length) const
    {
        if (length > 6)
        {
            throw std::runtime_error("length too large for getSecurityGroup [E106]");
        }

        std::memcpy(dst, m_buffer + m_offset + 23, sizeof(char) * static_cast<std::size_t>(length));
        return length;
    }

    MDInstrumentDefinitionFixedIncome57 &putSecurityGroup(const char *const src) SBE_NOEXCEPT
    {
        std::memcpy(m_buffer + m_offset + 23, src, sizeof(char) * 6);
        return *this;
    }

    SBE_NODISCARD std::string getSecurityGroupAsString() const
    {
        const char *buffer = m_buffer + m_offset + 23;
        std::size_t length = 0;

        for (; length < 6 && *(buffer + length) != '\0'; ++length);
        std::string result(buffer, length);

        return result;
    }

    std::string getSecurityGroupAsJsonEscapedString()
    {
        std::ostringstream oss;
        std::string s = getSecurityGroupAsString();

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
    SBE_NODISCARD std::string_view getSecurityGroupAsStringView() const SBE_NOEXCEPT
    {
        const char *buffer = m_buffer + m_offset + 23;
        std::size_t length = 0;

        for (; length < 6 && *(buffer + length) != '\0'; ++length);
        std::string_view result(buffer, length);

        return result;
    }
    #endif

    #if __cplusplus >= 201703L
    MDInstrumentDefinitionFixedIncome57 &putSecurityGroup(const std::string_view str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 6)
        {
            throw std::runtime_error("string too large for putSecurityGroup [E106]");
        }

        std::memcpy(m_buffer + m_offset + 23, str.data(), srcLength);
        for (std::size_t start = srcLength; start < 6; ++start)
        {
            m_buffer[m_offset + 23 + start] = 0;
        }

        return *this;
    }
    #else
    MDInstrumentDefinitionFixedIncome57 &putSecurityGroup(const std::string &str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 6)
        {
            throw std::runtime_error("string too large for putSecurityGroup [E106]");
        }

        std::memcpy(m_buffer + m_offset + 23, str.c_str(), srcLength);
        for (std::size_t start = srcLength; start < 6; ++start)
        {
            m_buffer[m_offset + 23 + start] = 0;
        }

        return *this;
    }
    #endif

    SBE_NODISCARD static const char *AssetMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "String";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t assetId() SBE_NOEXCEPT
    {
        return 6937;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t assetSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool assetInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= assetSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t assetEncodingOffset() SBE_NOEXCEPT
    {
        return 29;
    }

    static SBE_CONSTEXPR char assetNullValue() SBE_NOEXCEPT
    {
        return static_cast<char>(0);
    }

    static SBE_CONSTEXPR char assetMinValue() SBE_NOEXCEPT
    {
        return static_cast<char>(32);
    }

    static SBE_CONSTEXPR char assetMaxValue() SBE_NOEXCEPT
    {
        return static_cast<char>(126);
    }

    static SBE_CONSTEXPR std::size_t assetEncodingLength() SBE_NOEXCEPT
    {
        return 6;
    }

    static SBE_CONSTEXPR std::uint64_t assetLength() SBE_NOEXCEPT
    {
        return 6;
    }

    SBE_NODISCARD const char *asset() const SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 29;
    }

    SBE_NODISCARD char *asset() SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 29;
    }

    SBE_NODISCARD char asset(const std::uint64_t index) const
    {
        if (index >= 6)
        {
            throw std::runtime_error("index out of range for asset [E104]");
        }

        char val;
        std::memcpy(&val, m_buffer + m_offset + 29 + (index * 1), sizeof(char));
        return (val);
    }

    MDInstrumentDefinitionFixedIncome57 &asset(const std::uint64_t index, const char value)
    {
        if (index >= 6)
        {
            throw std::runtime_error("index out of range for asset [E105]");
        }

        char val = (value);
        std::memcpy(m_buffer + m_offset + 29 + (index * 1), &val, sizeof(char));
        return *this;
    }

    std::uint64_t getAsset(char *const dst, const std::uint64_t length) const
    {
        if (length > 6)
        {
            throw std::runtime_error("length too large for getAsset [E106]");
        }

        std::memcpy(dst, m_buffer + m_offset + 29, sizeof(char) * static_cast<std::size_t>(length));
        return length;
    }

    MDInstrumentDefinitionFixedIncome57 &putAsset(const char *const src) SBE_NOEXCEPT
    {
        std::memcpy(m_buffer + m_offset + 29, src, sizeof(char) * 6);
        return *this;
    }

    SBE_NODISCARD std::string getAssetAsString() const
    {
        const char *buffer = m_buffer + m_offset + 29;
        std::size_t length = 0;

        for (; length < 6 && *(buffer + length) != '\0'; ++length);
        std::string result(buffer, length);

        return result;
    }

    std::string getAssetAsJsonEscapedString()
    {
        std::ostringstream oss;
        std::string s = getAssetAsString();

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
    SBE_NODISCARD std::string_view getAssetAsStringView() const SBE_NOEXCEPT
    {
        const char *buffer = m_buffer + m_offset + 29;
        std::size_t length = 0;

        for (; length < 6 && *(buffer + length) != '\0'; ++length);
        std::string_view result(buffer, length);

        return result;
    }
    #endif

    #if __cplusplus >= 201703L
    MDInstrumentDefinitionFixedIncome57 &putAsset(const std::string_view str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 6)
        {
            throw std::runtime_error("string too large for putAsset [E106]");
        }

        std::memcpy(m_buffer + m_offset + 29, str.data(), srcLength);
        for (std::size_t start = srcLength; start < 6; ++start)
        {
            m_buffer[m_offset + 29 + start] = 0;
        }

        return *this;
    }
    #else
    MDInstrumentDefinitionFixedIncome57 &putAsset(const std::string &str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 6)
        {
            throw std::runtime_error("string too large for putAsset [E106]");
        }

        std::memcpy(m_buffer + m_offset + 29, str.c_str(), srcLength);
        for (std::size_t start = srcLength; start < 6; ++start)
        {
            m_buffer[m_offset + 29 + start] = 0;
        }

        return *this;
    }
    #endif

    SBE_NODISCARD static const char *SymbolMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "String";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t symbolId() SBE_NOEXCEPT
    {
        return 55;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t symbolSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool symbolInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= symbolSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t symbolEncodingOffset() SBE_NOEXCEPT
    {
        return 35;
    }

    static SBE_CONSTEXPR char symbolNullValue() SBE_NOEXCEPT
    {
        return static_cast<char>(0);
    }

    static SBE_CONSTEXPR char symbolMinValue() SBE_NOEXCEPT
    {
        return static_cast<char>(32);
    }

    static SBE_CONSTEXPR char symbolMaxValue() SBE_NOEXCEPT
    {
        return static_cast<char>(126);
    }

    static SBE_CONSTEXPR std::size_t symbolEncodingLength() SBE_NOEXCEPT
    {
        return 20;
    }

    static SBE_CONSTEXPR std::uint64_t symbolLength() SBE_NOEXCEPT
    {
        return 20;
    }

    SBE_NODISCARD const char *symbol() const SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 35;
    }

    SBE_NODISCARD char *symbol() SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 35;
    }

    SBE_NODISCARD char symbol(const std::uint64_t index) const
    {
        if (index >= 20)
        {
            throw std::runtime_error("index out of range for symbol [E104]");
        }

        char val;
        std::memcpy(&val, m_buffer + m_offset + 35 + (index * 1), sizeof(char));
        return (val);
    }

    MDInstrumentDefinitionFixedIncome57 &symbol(const std::uint64_t index, const char value)
    {
        if (index >= 20)
        {
            throw std::runtime_error("index out of range for symbol [E105]");
        }

        char val = (value);
        std::memcpy(m_buffer + m_offset + 35 + (index * 1), &val, sizeof(char));
        return *this;
    }

    std::uint64_t getSymbol(char *const dst, const std::uint64_t length) const
    {
        if (length > 20)
        {
            throw std::runtime_error("length too large for getSymbol [E106]");
        }

        std::memcpy(dst, m_buffer + m_offset + 35, sizeof(char) * static_cast<std::size_t>(length));
        return length;
    }

    MDInstrumentDefinitionFixedIncome57 &putSymbol(const char *const src) SBE_NOEXCEPT
    {
        std::memcpy(m_buffer + m_offset + 35, src, sizeof(char) * 20);
        return *this;
    }

    SBE_NODISCARD std::string getSymbolAsString() const
    {
        const char *buffer = m_buffer + m_offset + 35;
        std::size_t length = 0;

        for (; length < 20 && *(buffer + length) != '\0'; ++length);
        std::string result(buffer, length);

        return result;
    }

    std::string getSymbolAsJsonEscapedString()
    {
        std::ostringstream oss;
        std::string s = getSymbolAsString();

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
    SBE_NODISCARD std::string_view getSymbolAsStringView() const SBE_NOEXCEPT
    {
        const char *buffer = m_buffer + m_offset + 35;
        std::size_t length = 0;

        for (; length < 20 && *(buffer + length) != '\0'; ++length);
        std::string_view result(buffer, length);

        return result;
    }
    #endif

    #if __cplusplus >= 201703L
    MDInstrumentDefinitionFixedIncome57 &putSymbol(const std::string_view str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 20)
        {
            throw std::runtime_error("string too large for putSymbol [E106]");
        }

        std::memcpy(m_buffer + m_offset + 35, str.data(), srcLength);
        for (std::size_t start = srcLength; start < 20; ++start)
        {
            m_buffer[m_offset + 35 + start] = 0;
        }

        return *this;
    }
    #else
    MDInstrumentDefinitionFixedIncome57 &putSymbol(const std::string &str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 20)
        {
            throw std::runtime_error("string too large for putSymbol [E106]");
        }

        std::memcpy(m_buffer + m_offset + 35, str.c_str(), srcLength);
        for (std::size_t start = srcLength; start < 20; ++start)
        {
            m_buffer[m_offset + 35 + start] = 0;
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
        return 55;
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
        std::memcpy(&val, m_buffer + m_offset + 55, sizeof(std::int32_t));
        return SBE_LITTLE_ENDIAN_ENCODE_32(val);
    }

    MDInstrumentDefinitionFixedIncome57 &securityID(const std::int32_t value) SBE_NOEXCEPT
    {
        std::int32_t val = SBE_LITTLE_ENDIAN_ENCODE_32(value);
        std::memcpy(m_buffer + m_offset + 55, &val, sizeof(std::int32_t));
        return *this;
    }

    SBE_NODISCARD static const char *SecurityIDSourceMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "char";
            case MetaAttribute::PRESENCE: return "constant";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t securityIDSourceId() SBE_NOEXCEPT
    {
        return 22;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t securityIDSourceSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool securityIDSourceInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= securityIDSourceSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t securityIDSourceEncodingOffset() SBE_NOEXCEPT
    {
        return 59;
    }

    static SBE_CONSTEXPR char securityIDSourceNullValue() SBE_NOEXCEPT
    {
        return static_cast<char>(0);
    }

    static SBE_CONSTEXPR char securityIDSourceMinValue() SBE_NOEXCEPT
    {
        return static_cast<char>(32);
    }

    static SBE_CONSTEXPR char securityIDSourceMaxValue() SBE_NOEXCEPT
    {
        return static_cast<char>(126);
    }

    static SBE_CONSTEXPR std::size_t securityIDSourceEncodingLength() SBE_NOEXCEPT
    {
        return 0;
    }

    static SBE_CONSTEXPR std::uint64_t securityIDSourceLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD const char *securityIDSource() const
    {
        static const std::uint8_t securityIDSourceValues[] = { 56, 0 };

        return (const char *)securityIDSourceValues;
    }

    SBE_NODISCARD char securityIDSource(const std::uint64_t index) const
    {
        static const std::uint8_t securityIDSourceValues[] = { 56, 0 };

        return (char)securityIDSourceValues[index];
    }

    std::uint64_t getSecurityIDSource(char *dst, const std::uint64_t length) const
    {
        static std::uint8_t securityIDSourceValues[] = { 56 };
        std::uint64_t bytesToCopy = length < sizeof(securityIDSourceValues) ? length : sizeof(securityIDSourceValues);

        std::memcpy(dst, securityIDSourceValues, static_cast<std::size_t>(bytesToCopy));
        return bytesToCopy;
    }

    std::string getSecurityIDSourceAsString() const
    {
        static const std::uint8_t SecurityIDSourceValues[] = { 56 };

        return std::string((const char *)SecurityIDSourceValues, 1);
    }

    std::string getSecurityIDSourceAsJsonEscapedString()
    {
        std::ostringstream oss;
        std::string s = getSecurityIDSourceAsString();

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

    SBE_NODISCARD static const char *SecurityTypeMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "String";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t securityTypeId() SBE_NOEXCEPT
    {
        return 167;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t securityTypeSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool securityTypeInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= securityTypeSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t securityTypeEncodingOffset() SBE_NOEXCEPT
    {
        return 59;
    }

    static SBE_CONSTEXPR char securityTypeNullValue() SBE_NOEXCEPT
    {
        return static_cast<char>(0);
    }

    static SBE_CONSTEXPR char securityTypeMinValue() SBE_NOEXCEPT
    {
        return static_cast<char>(32);
    }

    static SBE_CONSTEXPR char securityTypeMaxValue() SBE_NOEXCEPT
    {
        return static_cast<char>(126);
    }

    static SBE_CONSTEXPR std::size_t securityTypeEncodingLength() SBE_NOEXCEPT
    {
        return 6;
    }

    static SBE_CONSTEXPR std::uint64_t securityTypeLength() SBE_NOEXCEPT
    {
        return 6;
    }

    SBE_NODISCARD const char *securityType() const SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 59;
    }

    SBE_NODISCARD char *securityType() SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 59;
    }

    SBE_NODISCARD char securityType(const std::uint64_t index) const
    {
        if (index >= 6)
        {
            throw std::runtime_error("index out of range for securityType [E104]");
        }

        char val;
        std::memcpy(&val, m_buffer + m_offset + 59 + (index * 1), sizeof(char));
        return (val);
    }

    MDInstrumentDefinitionFixedIncome57 &securityType(const std::uint64_t index, const char value)
    {
        if (index >= 6)
        {
            throw std::runtime_error("index out of range for securityType [E105]");
        }

        char val = (value);
        std::memcpy(m_buffer + m_offset + 59 + (index * 1), &val, sizeof(char));
        return *this;
    }

    std::uint64_t getSecurityType(char *const dst, const std::uint64_t length) const
    {
        if (length > 6)
        {
            throw std::runtime_error("length too large for getSecurityType [E106]");
        }

        std::memcpy(dst, m_buffer + m_offset + 59, sizeof(char) * static_cast<std::size_t>(length));
        return length;
    }

    MDInstrumentDefinitionFixedIncome57 &putSecurityType(const char *const src) SBE_NOEXCEPT
    {
        std::memcpy(m_buffer + m_offset + 59, src, sizeof(char) * 6);
        return *this;
    }

    SBE_NODISCARD std::string getSecurityTypeAsString() const
    {
        const char *buffer = m_buffer + m_offset + 59;
        std::size_t length = 0;

        for (; length < 6 && *(buffer + length) != '\0'; ++length);
        std::string result(buffer, length);

        return result;
    }

    std::string getSecurityTypeAsJsonEscapedString()
    {
        std::ostringstream oss;
        std::string s = getSecurityTypeAsString();

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
    SBE_NODISCARD std::string_view getSecurityTypeAsStringView() const SBE_NOEXCEPT
    {
        const char *buffer = m_buffer + m_offset + 59;
        std::size_t length = 0;

        for (; length < 6 && *(buffer + length) != '\0'; ++length);
        std::string_view result(buffer, length);

        return result;
    }
    #endif

    #if __cplusplus >= 201703L
    MDInstrumentDefinitionFixedIncome57 &putSecurityType(const std::string_view str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 6)
        {
            throw std::runtime_error("string too large for putSecurityType [E106]");
        }

        std::memcpy(m_buffer + m_offset + 59, str.data(), srcLength);
        for (std::size_t start = srcLength; start < 6; ++start)
        {
            m_buffer[m_offset + 59 + start] = 0;
        }

        return *this;
    }
    #else
    MDInstrumentDefinitionFixedIncome57 &putSecurityType(const std::string &str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 6)
        {
            throw std::runtime_error("string too large for putSecurityType [E106]");
        }

        std::memcpy(m_buffer + m_offset + 59, str.c_str(), srcLength);
        for (std::size_t start = srcLength; start < 6; ++start)
        {
            m_buffer[m_offset + 59 + start] = 0;
        }

        return *this;
    }
    #endif

    SBE_NODISCARD static const char *CFICodeMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "String";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t cFICodeId() SBE_NOEXCEPT
    {
        return 461;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t cFICodeSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool cFICodeInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= cFICodeSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t cFICodeEncodingOffset() SBE_NOEXCEPT
    {
        return 65;
    }

    static SBE_CONSTEXPR char cFICodeNullValue() SBE_NOEXCEPT
    {
        return static_cast<char>(0);
    }

    static SBE_CONSTEXPR char cFICodeMinValue() SBE_NOEXCEPT
    {
        return static_cast<char>(32);
    }

    static SBE_CONSTEXPR char cFICodeMaxValue() SBE_NOEXCEPT
    {
        return static_cast<char>(126);
    }

    static SBE_CONSTEXPR std::size_t cFICodeEncodingLength() SBE_NOEXCEPT
    {
        return 6;
    }

    static SBE_CONSTEXPR std::uint64_t cFICodeLength() SBE_NOEXCEPT
    {
        return 6;
    }

    SBE_NODISCARD const char *cFICode() const SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 65;
    }

    SBE_NODISCARD char *cFICode() SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 65;
    }

    SBE_NODISCARD char cFICode(const std::uint64_t index) const
    {
        if (index >= 6)
        {
            throw std::runtime_error("index out of range for cFICode [E104]");
        }

        char val;
        std::memcpy(&val, m_buffer + m_offset + 65 + (index * 1), sizeof(char));
        return (val);
    }

    MDInstrumentDefinitionFixedIncome57 &cFICode(const std::uint64_t index, const char value)
    {
        if (index >= 6)
        {
            throw std::runtime_error("index out of range for cFICode [E105]");
        }

        char val = (value);
        std::memcpy(m_buffer + m_offset + 65 + (index * 1), &val, sizeof(char));
        return *this;
    }

    std::uint64_t getCFICode(char *const dst, const std::uint64_t length) const
    {
        if (length > 6)
        {
            throw std::runtime_error("length too large for getCFICode [E106]");
        }

        std::memcpy(dst, m_buffer + m_offset + 65, sizeof(char) * static_cast<std::size_t>(length));
        return length;
    }

    MDInstrumentDefinitionFixedIncome57 &putCFICode(const char *const src) SBE_NOEXCEPT
    {
        std::memcpy(m_buffer + m_offset + 65, src, sizeof(char) * 6);
        return *this;
    }

    SBE_NODISCARD std::string getCFICodeAsString() const
    {
        const char *buffer = m_buffer + m_offset + 65;
        std::size_t length = 0;

        for (; length < 6 && *(buffer + length) != '\0'; ++length);
        std::string result(buffer, length);

        return result;
    }

    std::string getCFICodeAsJsonEscapedString()
    {
        std::ostringstream oss;
        std::string s = getCFICodeAsString();

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
    SBE_NODISCARD std::string_view getCFICodeAsStringView() const SBE_NOEXCEPT
    {
        const char *buffer = m_buffer + m_offset + 65;
        std::size_t length = 0;

        for (; length < 6 && *(buffer + length) != '\0'; ++length);
        std::string_view result(buffer, length);

        return result;
    }
    #endif

    #if __cplusplus >= 201703L
    MDInstrumentDefinitionFixedIncome57 &putCFICode(const std::string_view str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 6)
        {
            throw std::runtime_error("string too large for putCFICode [E106]");
        }

        std::memcpy(m_buffer + m_offset + 65, str.data(), srcLength);
        for (std::size_t start = srcLength; start < 6; ++start)
        {
            m_buffer[m_offset + 65 + start] = 0;
        }

        return *this;
    }
    #else
    MDInstrumentDefinitionFixedIncome57 &putCFICode(const std::string &str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 6)
        {
            throw std::runtime_error("string too large for putCFICode [E106]");
        }

        std::memcpy(m_buffer + m_offset + 65, str.c_str(), srcLength);
        for (std::size_t start = srcLength; start < 6; ++start)
        {
            m_buffer[m_offset + 65 + start] = 0;
        }

        return *this;
    }
    #endif

    SBE_NODISCARD static const char *CurrencyMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "Currency";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t currencyId() SBE_NOEXCEPT
    {
        return 15;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t currencySinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool currencyInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= currencySinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t currencyEncodingOffset() SBE_NOEXCEPT
    {
        return 71;
    }

    static SBE_CONSTEXPR char currencyNullValue() SBE_NOEXCEPT
    {
        return static_cast<char>(0);
    }

    static SBE_CONSTEXPR char currencyMinValue() SBE_NOEXCEPT
    {
        return static_cast<char>(32);
    }

    static SBE_CONSTEXPR char currencyMaxValue() SBE_NOEXCEPT
    {
        return static_cast<char>(126);
    }

    static SBE_CONSTEXPR std::size_t currencyEncodingLength() SBE_NOEXCEPT
    {
        return 3;
    }

    static SBE_CONSTEXPR std::uint64_t currencyLength() SBE_NOEXCEPT
    {
        return 3;
    }

    SBE_NODISCARD const char *currency() const SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 71;
    }

    SBE_NODISCARD char *currency() SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 71;
    }

    SBE_NODISCARD char currency(const std::uint64_t index) const
    {
        if (index >= 3)
        {
            throw std::runtime_error("index out of range for currency [E104]");
        }

        char val;
        std::memcpy(&val, m_buffer + m_offset + 71 + (index * 1), sizeof(char));
        return (val);
    }

    MDInstrumentDefinitionFixedIncome57 &currency(const std::uint64_t index, const char value)
    {
        if (index >= 3)
        {
            throw std::runtime_error("index out of range for currency [E105]");
        }

        char val = (value);
        std::memcpy(m_buffer + m_offset + 71 + (index * 1), &val, sizeof(char));
        return *this;
    }

    std::uint64_t getCurrency(char *const dst, const std::uint64_t length) const
    {
        if (length > 3)
        {
            throw std::runtime_error("length too large for getCurrency [E106]");
        }

        std::memcpy(dst, m_buffer + m_offset + 71, sizeof(char) * static_cast<std::size_t>(length));
        return length;
    }

    MDInstrumentDefinitionFixedIncome57 &putCurrency(const char *const src) SBE_NOEXCEPT
    {
        std::memcpy(m_buffer + m_offset + 71, src, sizeof(char) * 3);
        return *this;
    }

    MDInstrumentDefinitionFixedIncome57 &putCurrency(
        const char value0,
        const char value1,
        const char value2) SBE_NOEXCEPT
    {
        char val0 = (value0);
        std::memcpy(m_buffer + m_offset + 71, &val0, sizeof(char));
        char val1 = (value1);
        std::memcpy(m_buffer + m_offset + 72, &val1, sizeof(char));
        char val2 = (value2);
        std::memcpy(m_buffer + m_offset + 73, &val2, sizeof(char));

        return *this;
    }

    SBE_NODISCARD std::string getCurrencyAsString() const
    {
        const char *buffer = m_buffer + m_offset + 71;
        std::size_t length = 0;

        for (; length < 3 && *(buffer + length) != '\0'; ++length);
        std::string result(buffer, length);

        return result;
    }

    std::string getCurrencyAsJsonEscapedString()
    {
        std::ostringstream oss;
        std::string s = getCurrencyAsString();

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
    SBE_NODISCARD std::string_view getCurrencyAsStringView() const SBE_NOEXCEPT
    {
        const char *buffer = m_buffer + m_offset + 71;
        std::size_t length = 0;

        for (; length < 3 && *(buffer + length) != '\0'; ++length);
        std::string_view result(buffer, length);

        return result;
    }
    #endif

    #if __cplusplus >= 201703L
    MDInstrumentDefinitionFixedIncome57 &putCurrency(const std::string_view str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 3)
        {
            throw std::runtime_error("string too large for putCurrency [E106]");
        }

        std::memcpy(m_buffer + m_offset + 71, str.data(), srcLength);
        for (std::size_t start = srcLength; start < 3; ++start)
        {
            m_buffer[m_offset + 71 + start] = 0;
        }

        return *this;
    }
    #else
    MDInstrumentDefinitionFixedIncome57 &putCurrency(const std::string &str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 3)
        {
            throw std::runtime_error("string too large for putCurrency [E106]");
        }

        std::memcpy(m_buffer + m_offset + 71, str.c_str(), srcLength);
        for (std::size_t start = srcLength; start < 3; ++start)
        {
            m_buffer[m_offset + 71 + start] = 0;
        }

        return *this;
    }
    #endif

    SBE_NODISCARD static const char *SettlCurrencyMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "Currency";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t settlCurrencyId() SBE_NOEXCEPT
    {
        return 120;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t settlCurrencySinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool settlCurrencyInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= settlCurrencySinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t settlCurrencyEncodingOffset() SBE_NOEXCEPT
    {
        return 74;
    }

    static SBE_CONSTEXPR char settlCurrencyNullValue() SBE_NOEXCEPT
    {
        return static_cast<char>(0);
    }

    static SBE_CONSTEXPR char settlCurrencyMinValue() SBE_NOEXCEPT
    {
        return static_cast<char>(32);
    }

    static SBE_CONSTEXPR char settlCurrencyMaxValue() SBE_NOEXCEPT
    {
        return static_cast<char>(126);
    }

    static SBE_CONSTEXPR std::size_t settlCurrencyEncodingLength() SBE_NOEXCEPT
    {
        return 3;
    }

    static SBE_CONSTEXPR std::uint64_t settlCurrencyLength() SBE_NOEXCEPT
    {
        return 3;
    }

    SBE_NODISCARD const char *settlCurrency() const SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 74;
    }

    SBE_NODISCARD char *settlCurrency() SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 74;
    }

    SBE_NODISCARD char settlCurrency(const std::uint64_t index) const
    {
        if (index >= 3)
        {
            throw std::runtime_error("index out of range for settlCurrency [E104]");
        }

        char val;
        std::memcpy(&val, m_buffer + m_offset + 74 + (index * 1), sizeof(char));
        return (val);
    }

    MDInstrumentDefinitionFixedIncome57 &settlCurrency(const std::uint64_t index, const char value)
    {
        if (index >= 3)
        {
            throw std::runtime_error("index out of range for settlCurrency [E105]");
        }

        char val = (value);
        std::memcpy(m_buffer + m_offset + 74 + (index * 1), &val, sizeof(char));
        return *this;
    }

    std::uint64_t getSettlCurrency(char *const dst, const std::uint64_t length) const
    {
        if (length > 3)
        {
            throw std::runtime_error("length too large for getSettlCurrency [E106]");
        }

        std::memcpy(dst, m_buffer + m_offset + 74, sizeof(char) * static_cast<std::size_t>(length));
        return length;
    }

    MDInstrumentDefinitionFixedIncome57 &putSettlCurrency(const char *const src) SBE_NOEXCEPT
    {
        std::memcpy(m_buffer + m_offset + 74, src, sizeof(char) * 3);
        return *this;
    }

    MDInstrumentDefinitionFixedIncome57 &putSettlCurrency(
        const char value0,
        const char value1,
        const char value2) SBE_NOEXCEPT
    {
        char val0 = (value0);
        std::memcpy(m_buffer + m_offset + 74, &val0, sizeof(char));
        char val1 = (value1);
        std::memcpy(m_buffer + m_offset + 75, &val1, sizeof(char));
        char val2 = (value2);
        std::memcpy(m_buffer + m_offset + 76, &val2, sizeof(char));

        return *this;
    }

    SBE_NODISCARD std::string getSettlCurrencyAsString() const
    {
        const char *buffer = m_buffer + m_offset + 74;
        std::size_t length = 0;

        for (; length < 3 && *(buffer + length) != '\0'; ++length);
        std::string result(buffer, length);

        return result;
    }

    std::string getSettlCurrencyAsJsonEscapedString()
    {
        std::ostringstream oss;
        std::string s = getSettlCurrencyAsString();

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
    SBE_NODISCARD std::string_view getSettlCurrencyAsStringView() const SBE_NOEXCEPT
    {
        const char *buffer = m_buffer + m_offset + 74;
        std::size_t length = 0;

        for (; length < 3 && *(buffer + length) != '\0'; ++length);
        std::string_view result(buffer, length);

        return result;
    }
    #endif

    #if __cplusplus >= 201703L
    MDInstrumentDefinitionFixedIncome57 &putSettlCurrency(const std::string_view str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 3)
        {
            throw std::runtime_error("string too large for putSettlCurrency [E106]");
        }

        std::memcpy(m_buffer + m_offset + 74, str.data(), srcLength);
        for (std::size_t start = srcLength; start < 3; ++start)
        {
            m_buffer[m_offset + 74 + start] = 0;
        }

        return *this;
    }
    #else
    MDInstrumentDefinitionFixedIncome57 &putSettlCurrency(const std::string &str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 3)
        {
            throw std::runtime_error("string too large for putSettlCurrency [E106]");
        }

        std::memcpy(m_buffer + m_offset + 74, str.c_str(), srcLength);
        for (std::size_t start = srcLength; start < 3; ++start)
        {
            m_buffer[m_offset + 74 + start] = 0;
        }

        return *this;
    }
    #endif

    SBE_NODISCARD static const char *MatchAlgorithmMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "char";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t matchAlgorithmId() SBE_NOEXCEPT
    {
        return 1142;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t matchAlgorithmSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool matchAlgorithmInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= matchAlgorithmSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t matchAlgorithmEncodingOffset() SBE_NOEXCEPT
    {
        return 77;
    }

    static SBE_CONSTEXPR char matchAlgorithmNullValue() SBE_NOEXCEPT
    {
        return static_cast<char>(0);
    }

    static SBE_CONSTEXPR char matchAlgorithmMinValue() SBE_NOEXCEPT
    {
        return static_cast<char>(32);
    }

    static SBE_CONSTEXPR char matchAlgorithmMaxValue() SBE_NOEXCEPT
    {
        return static_cast<char>(126);
    }

    static SBE_CONSTEXPR std::size_t matchAlgorithmEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD char matchAlgorithm() const SBE_NOEXCEPT
    {
        char val;
        std::memcpy(&val, m_buffer + m_offset + 77, sizeof(char));
        return (val);
    }

    MDInstrumentDefinitionFixedIncome57 &matchAlgorithm(const char value) SBE_NOEXCEPT
    {
        char val = (value);
        std::memcpy(m_buffer + m_offset + 77, &val, sizeof(char));
        return *this;
    }

    SBE_NODISCARD static const char *MinTradeVolMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "Qty";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t minTradeVolId() SBE_NOEXCEPT
    {
        return 562;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t minTradeVolSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool minTradeVolInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= minTradeVolSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t minTradeVolEncodingOffset() SBE_NOEXCEPT
    {
        return 78;
    }

    static SBE_CONSTEXPR std::uint32_t minTradeVolNullValue() SBE_NOEXCEPT
    {
        return SBE_NULLVALUE_UINT32;
    }

    static SBE_CONSTEXPR std::uint32_t minTradeVolMinValue() SBE_NOEXCEPT
    {
        return UINT32_C(0x0);
    }

    static SBE_CONSTEXPR std::uint32_t minTradeVolMaxValue() SBE_NOEXCEPT
    {
        return UINT32_C(0xfffffffe);
    }

    static SBE_CONSTEXPR std::size_t minTradeVolEncodingLength() SBE_NOEXCEPT
    {
        return 4;
    }

    SBE_NODISCARD std::uint32_t minTradeVol() const SBE_NOEXCEPT
    {
        std::uint32_t val;
        std::memcpy(&val, m_buffer + m_offset + 78, sizeof(std::uint32_t));
        return SBE_LITTLE_ENDIAN_ENCODE_32(val);
    }

    MDInstrumentDefinitionFixedIncome57 &minTradeVol(const std::uint32_t value) SBE_NOEXCEPT
    {
        std::uint32_t val = SBE_LITTLE_ENDIAN_ENCODE_32(value);
        std::memcpy(m_buffer + m_offset + 78, &val, sizeof(std::uint32_t));
        return *this;
    }

    SBE_NODISCARD static const char *MaxTradeVolMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "Qty";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t maxTradeVolId() SBE_NOEXCEPT
    {
        return 1140;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t maxTradeVolSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool maxTradeVolInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= maxTradeVolSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t maxTradeVolEncodingOffset() SBE_NOEXCEPT
    {
        return 82;
    }

    static SBE_CONSTEXPR std::uint32_t maxTradeVolNullValue() SBE_NOEXCEPT
    {
        return SBE_NULLVALUE_UINT32;
    }

    static SBE_CONSTEXPR std::uint32_t maxTradeVolMinValue() SBE_NOEXCEPT
    {
        return UINT32_C(0x0);
    }

    static SBE_CONSTEXPR std::uint32_t maxTradeVolMaxValue() SBE_NOEXCEPT
    {
        return UINT32_C(0xfffffffe);
    }

    static SBE_CONSTEXPR std::size_t maxTradeVolEncodingLength() SBE_NOEXCEPT
    {
        return 4;
    }

    SBE_NODISCARD std::uint32_t maxTradeVol() const SBE_NOEXCEPT
    {
        std::uint32_t val;
        std::memcpy(&val, m_buffer + m_offset + 82, sizeof(std::uint32_t));
        return SBE_LITTLE_ENDIAN_ENCODE_32(val);
    }

    MDInstrumentDefinitionFixedIncome57 &maxTradeVol(const std::uint32_t value) SBE_NOEXCEPT
    {
        std::uint32_t val = SBE_LITTLE_ENDIAN_ENCODE_32(value);
        std::memcpy(m_buffer + m_offset + 82, &val, sizeof(std::uint32_t));
        return *this;
    }

    SBE_NODISCARD static const char *MinPriceIncrementMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "Price";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t minPriceIncrementId() SBE_NOEXCEPT
    {
        return 969;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t minPriceIncrementSinceVersion() SBE_NOEXCEPT
    {
        return 9;
    }

    SBE_NODISCARD bool minPriceIncrementInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= minPriceIncrementSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t minPriceIncrementEncodingOffset() SBE_NOEXCEPT
    {
        return 86;
    }

private:
    PRICENULL9 m_minPriceIncrement;

public:
    SBE_NODISCARD PRICENULL9 &minPriceIncrement()
    {
        m_minPriceIncrement.wrap(m_buffer, m_offset + 86, m_actingVersion, m_bufferLength);
        return m_minPriceIncrement;
    }

    SBE_NODISCARD static const char *DisplayFactorMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "float";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t displayFactorId() SBE_NOEXCEPT
    {
        return 9787;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t displayFactorSinceVersion() SBE_NOEXCEPT
    {
        return 9;
    }

    SBE_NODISCARD bool displayFactorInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= displayFactorSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t displayFactorEncodingOffset() SBE_NOEXCEPT
    {
        return 94;
    }

private:
    Decimal9 m_displayFactor;

public:
    SBE_NODISCARD Decimal9 &displayFactor()
    {
        m_displayFactor.wrap(m_buffer, m_offset + 94, m_actingVersion, m_bufferLength);
        return m_displayFactor;
    }

    SBE_NODISCARD static const char *MainFractionMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "optional";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t mainFractionId() SBE_NOEXCEPT
    {
        return 37702;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t mainFractionSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool mainFractionInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= mainFractionSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t mainFractionEncodingOffset() SBE_NOEXCEPT
    {
        return 102;
    }

    static SBE_CONSTEXPR std::uint8_t mainFractionNullValue() SBE_NOEXCEPT
    {
        return static_cast<std::uint8_t>(255);
    }

    static SBE_CONSTEXPR std::uint8_t mainFractionMinValue() SBE_NOEXCEPT
    {
        return static_cast<std::uint8_t>(0);
    }

    static SBE_CONSTEXPR std::uint8_t mainFractionMaxValue() SBE_NOEXCEPT
    {
        return static_cast<std::uint8_t>(254);
    }

    static SBE_CONSTEXPR std::size_t mainFractionEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD std::uint8_t mainFraction() const SBE_NOEXCEPT
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 102, sizeof(std::uint8_t));
        return (val);
    }

    MDInstrumentDefinitionFixedIncome57 &mainFraction(const std::uint8_t value) SBE_NOEXCEPT
    {
        std::uint8_t val = (value);
        std::memcpy(m_buffer + m_offset + 102, &val, sizeof(std::uint8_t));
        return *this;
    }

    SBE_NODISCARD static const char *SubFractionMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "optional";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t subFractionId() SBE_NOEXCEPT
    {
        return 37703;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t subFractionSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool subFractionInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= subFractionSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t subFractionEncodingOffset() SBE_NOEXCEPT
    {
        return 103;
    }

    static SBE_CONSTEXPR std::uint8_t subFractionNullValue() SBE_NOEXCEPT
    {
        return static_cast<std::uint8_t>(255);
    }

    static SBE_CONSTEXPR std::uint8_t subFractionMinValue() SBE_NOEXCEPT
    {
        return static_cast<std::uint8_t>(0);
    }

    static SBE_CONSTEXPR std::uint8_t subFractionMaxValue() SBE_NOEXCEPT
    {
        return static_cast<std::uint8_t>(254);
    }

    static SBE_CONSTEXPR std::size_t subFractionEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD std::uint8_t subFraction() const SBE_NOEXCEPT
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 103, sizeof(std::uint8_t));
        return (val);
    }

    MDInstrumentDefinitionFixedIncome57 &subFraction(const std::uint8_t value) SBE_NOEXCEPT
    {
        std::uint8_t val = (value);
        std::memcpy(m_buffer + m_offset + 103, &val, sizeof(std::uint8_t));
        return *this;
    }

    SBE_NODISCARD static const char *PriceDisplayFormatMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "optional";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t priceDisplayFormatId() SBE_NOEXCEPT
    {
        return 9800;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t priceDisplayFormatSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool priceDisplayFormatInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= priceDisplayFormatSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t priceDisplayFormatEncodingOffset() SBE_NOEXCEPT
    {
        return 104;
    }

    static SBE_CONSTEXPR std::uint8_t priceDisplayFormatNullValue() SBE_NOEXCEPT
    {
        return static_cast<std::uint8_t>(255);
    }

    static SBE_CONSTEXPR std::uint8_t priceDisplayFormatMinValue() SBE_NOEXCEPT
    {
        return static_cast<std::uint8_t>(0);
    }

    static SBE_CONSTEXPR std::uint8_t priceDisplayFormatMaxValue() SBE_NOEXCEPT
    {
        return static_cast<std::uint8_t>(254);
    }

    static SBE_CONSTEXPR std::size_t priceDisplayFormatEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD std::uint8_t priceDisplayFormat() const SBE_NOEXCEPT
    {
        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 104, sizeof(std::uint8_t));
        return (val);
    }

    MDInstrumentDefinitionFixedIncome57 &priceDisplayFormat(const std::uint8_t value) SBE_NOEXCEPT
    {
        std::uint8_t val = (value);
        std::memcpy(m_buffer + m_offset + 104, &val, sizeof(std::uint8_t));
        return *this;
    }

    SBE_NODISCARD static const char *UnitOfMeasureMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "String";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t unitOfMeasureId() SBE_NOEXCEPT
    {
        return 996;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t unitOfMeasureSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool unitOfMeasureInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= unitOfMeasureSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t unitOfMeasureEncodingOffset() SBE_NOEXCEPT
    {
        return 105;
    }

    static SBE_CONSTEXPR char unitOfMeasureNullValue() SBE_NOEXCEPT
    {
        return static_cast<char>(0);
    }

    static SBE_CONSTEXPR char unitOfMeasureMinValue() SBE_NOEXCEPT
    {
        return static_cast<char>(32);
    }

    static SBE_CONSTEXPR char unitOfMeasureMaxValue() SBE_NOEXCEPT
    {
        return static_cast<char>(126);
    }

    static SBE_CONSTEXPR std::size_t unitOfMeasureEncodingLength() SBE_NOEXCEPT
    {
        return 30;
    }

    static SBE_CONSTEXPR std::uint64_t unitOfMeasureLength() SBE_NOEXCEPT
    {
        return 30;
    }

    SBE_NODISCARD const char *unitOfMeasure() const SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 105;
    }

    SBE_NODISCARD char *unitOfMeasure() SBE_NOEXCEPT
    {
        return m_buffer + m_offset + 105;
    }

    SBE_NODISCARD char unitOfMeasure(const std::uint64_t index) const
    {
        if (index >= 30)
        {
            throw std::runtime_error("index out of range for unitOfMeasure [E104]");
        }

        char val;
        std::memcpy(&val, m_buffer + m_offset + 105 + (index * 1), sizeof(char));
        return (val);
    }

    MDInstrumentDefinitionFixedIncome57 &unitOfMeasure(const std::uint64_t index, const char value)
    {
        if (index >= 30)
        {
            throw std::runtime_error("index out of range for unitOfMeasure [E105]");
        }

        char val = (value);
        std::memcpy(m_buffer + m_offset + 105 + (index * 1), &val, sizeof(char));
        return *this;
    }

    std::uint64_t getUnitOfMeasure(char *const dst, const std::uint64_t length) const
    {
        if (length > 30)
        {
            throw std::runtime_error("length too large for getUnitOfMeasure [E106]");
        }

        std::memcpy(dst, m_buffer + m_offset + 105, sizeof(char) * static_cast<std::size_t>(length));
        return length;
    }

    MDInstrumentDefinitionFixedIncome57 &putUnitOfMeasure(const char *const src) SBE_NOEXCEPT
    {
        std::memcpy(m_buffer + m_offset + 105, src, sizeof(char) * 30);
        return *this;
    }

    SBE_NODISCARD std::string getUnitOfMeasureAsString() const
    {
        const char *buffer = m_buffer + m_offset + 105;
        std::size_t length = 0;

        for (; length < 30 && *(buffer + length) != '\0'; ++length);
        std::string result(buffer, length);

        return result;
    }

    std::string getUnitOfMeasureAsJsonEscapedString()
    {
        std::ostringstream oss;
        std::string s = getUnitOfMeasureAsString();

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
    SBE_NODISCARD std::string_view getUnitOfMeasureAsStringView() const SBE_NOEXCEPT
    {
        const char *buffer = m_buffer + m_offset + 105;
        std::size_t length = 0;

        for (; length < 30 && *(buffer + length) != '\0'; ++length);
        std::string_view result(buffer, length);

        return result;
    }
    #endif

    #if __cplusplus >= 201703L
    MDInstrumentDefinitionFixedIncome57 &putUnitOfMeasure(const std::string_view str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 30)
        {
            throw std::runtime_error("string too large for putUnitOfMeasure [E106]");
        }

        std::memcpy(m_buffer + m_offset + 105, str.data(), srcLength);
        for (std::size_t start = srcLength; start < 30; ++start)
        {
            m_buffer[m_offset + 105 + start] = 0;
        }

        return *this;
    }
    #else
    MDInstrumentDefinitionFixedIncome57 &putUnitOfMeasure(const std::string &str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 30)
        {
            throw std::runtime_error("string too large for putUnitOfMeasure [E106]");
        }

        std::memcpy(m_buffer + m_offset + 105, str.c_str(), srcLength);
        for (std::size_t start = srcLength; start < 30; ++start)
        {
            m_buffer[m_offset + 105 + start] = 0;
        }

        return *this;
    }
    #endif

    SBE_NODISCARD static const char *UnitOfMeasureQtyMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "Qty";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t unitOfMeasureQtyId() SBE_NOEXCEPT
    {
        return 1147;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t unitOfMeasureQtySinceVersion() SBE_NOEXCEPT
    {
        return 9;
    }

    SBE_NODISCARD bool unitOfMeasureQtyInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= unitOfMeasureQtySinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t unitOfMeasureQtyEncodingOffset() SBE_NOEXCEPT
    {
        return 135;
    }

private:
    Decimal9NULL m_unitOfMeasureQty;

public:
    SBE_NODISCARD Decimal9NULL &unitOfMeasureQty()
    {
        m_unitOfMeasureQty.wrap(m_buffer, m_offset + 135, m_actingVersion, m_bufferLength);
        return m_unitOfMeasureQty;
    }

    SBE_NODISCARD static const char *TradingReferencePriceMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "Price";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t tradingReferencePriceId() SBE_NOEXCEPT
    {
        return 1150;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t tradingReferencePriceSinceVersion() SBE_NOEXCEPT
    {
        return 9;
    }

    SBE_NODISCARD bool tradingReferencePriceInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= tradingReferencePriceSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t tradingReferencePriceEncodingOffset() SBE_NOEXCEPT
    {
        return 143;
    }

private:
    PRICENULL9 m_tradingReferencePrice;

public:
    SBE_NODISCARD PRICENULL9 &tradingReferencePrice()
    {
        m_tradingReferencePrice.wrap(m_buffer, m_offset + 143, m_actingVersion, m_bufferLength);
        return m_tradingReferencePrice;
    }

    SBE_NODISCARD static const char *TradingReferenceDateMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "LocalMktDate";
            case MetaAttribute::PRESENCE: return "optional";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t tradingReferenceDateId() SBE_NOEXCEPT
    {
        return 5796;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t tradingReferenceDateSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool tradingReferenceDateInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= tradingReferenceDateSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t tradingReferenceDateEncodingOffset() SBE_NOEXCEPT
    {
        return 151;
    }

    static SBE_CONSTEXPR std::uint16_t tradingReferenceDateNullValue() SBE_NOEXCEPT
    {
        return static_cast<std::uint16_t>(65535);
    }

    static SBE_CONSTEXPR std::uint16_t tradingReferenceDateMinValue() SBE_NOEXCEPT
    {
        return static_cast<std::uint16_t>(0);
    }

    static SBE_CONSTEXPR std::uint16_t tradingReferenceDateMaxValue() SBE_NOEXCEPT
    {
        return static_cast<std::uint16_t>(65534);
    }

    static SBE_CONSTEXPR std::size_t tradingReferenceDateEncodingLength() SBE_NOEXCEPT
    {
        return 2;
    }

    SBE_NODISCARD std::uint16_t tradingReferenceDate() const SBE_NOEXCEPT
    {
        std::uint16_t val;
        std::memcpy(&val, m_buffer + m_offset + 151, sizeof(std::uint16_t));
        return SBE_LITTLE_ENDIAN_ENCODE_16(val);
    }

    MDInstrumentDefinitionFixedIncome57 &tradingReferenceDate(const std::uint16_t value) SBE_NOEXCEPT
    {
        std::uint16_t val = SBE_LITTLE_ENDIAN_ENCODE_16(value);
        std::memcpy(m_buffer + m_offset + 151, &val, sizeof(std::uint16_t));
        return *this;
    }

    SBE_NODISCARD static const char *HighLimitPriceMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "Price";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t highLimitPriceId() SBE_NOEXCEPT
    {
        return 1149;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t highLimitPriceSinceVersion() SBE_NOEXCEPT
    {
        return 9;
    }

    SBE_NODISCARD bool highLimitPriceInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= highLimitPriceSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t highLimitPriceEncodingOffset() SBE_NOEXCEPT
    {
        return 153;
    }

private:
    PRICENULL9 m_highLimitPrice;

public:
    SBE_NODISCARD PRICENULL9 &highLimitPrice()
    {
        m_highLimitPrice.wrap(m_buffer, m_offset + 153, m_actingVersion, m_bufferLength);
        return m_highLimitPrice;
    }

    SBE_NODISCARD static const char *LowLimitPriceMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "Price";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t lowLimitPriceId() SBE_NOEXCEPT
    {
        return 1148;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t lowLimitPriceSinceVersion() SBE_NOEXCEPT
    {
        return 9;
    }

    SBE_NODISCARD bool lowLimitPriceInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= lowLimitPriceSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t lowLimitPriceEncodingOffset() SBE_NOEXCEPT
    {
        return 161;
    }

private:
    PRICENULL9 m_lowLimitPrice;

public:
    SBE_NODISCARD PRICENULL9 &lowLimitPrice()
    {
        m_lowLimitPrice.wrap(m_buffer, m_offset + 161, m_actingVersion, m_bufferLength);
        return m_lowLimitPrice;
    }

    SBE_NODISCARD static const char *MaxPriceVariationMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "Price";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t maxPriceVariationId() SBE_NOEXCEPT
    {
        return 1143;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t maxPriceVariationSinceVersion() SBE_NOEXCEPT
    {
        return 9;
    }

    SBE_NODISCARD bool maxPriceVariationInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= maxPriceVariationSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t maxPriceVariationEncodingOffset() SBE_NOEXCEPT
    {
        return 169;
    }

private:
    PRICENULL9 m_maxPriceVariation;

public:
    SBE_NODISCARD PRICENULL9 &maxPriceVariation()
    {
        m_maxPriceVariation.wrap(m_buffer, m_offset + 169, m_actingVersion, m_bufferLength);
        return m_maxPriceVariation;
    }

    SBE_NODISCARD static const char *MinPriceIncrementAmountMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "Price";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t minPriceIncrementAmountId() SBE_NOEXCEPT
    {
        return 1146;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t minPriceIncrementAmountSinceVersion() SBE_NOEXCEPT
    {
        return 9;
    }

    SBE_NODISCARD bool minPriceIncrementAmountInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= minPriceIncrementAmountSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t minPriceIncrementAmountEncodingOffset() SBE_NOEXCEPT
    {
        return 177;
    }

private:
    PRICENULL9 m_minPriceIncrementAmount;

public:
    SBE_NODISCARD PRICENULL9 &minPriceIncrementAmount()
    {
        m_minPriceIncrementAmount.wrap(m_buffer, m_offset + 177, m_actingVersion, m_bufferLength);
        return m_minPriceIncrementAmount;
    }

    SBE_NODISCARD static const char *IssueDateMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "LocalMktDate";
            case MetaAttribute::PRESENCE: return "optional";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t issueDateId() SBE_NOEXCEPT
    {
        return 225;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t issueDateSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool issueDateInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= issueDateSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t issueDateEncodingOffset() SBE_NOEXCEPT
    {
        return 185;
    }

    static SBE_CONSTEXPR std::uint16_t issueDateNullValue() SBE_NOEXCEPT
    {
        return static_cast<std::uint16_t>(65535);
    }

    static SBE_CONSTEXPR std::uint16_t issueDateMinValue() SBE_NOEXCEPT
    {
        return static_cast<std::uint16_t>(0);
    }

    static SBE_CONSTEXPR std::uint16_t issueDateMaxValue() SBE_NOEXCEPT
    {
        return static_cast<std::uint16_t>(65534);
    }

    static SBE_CONSTEXPR std::size_t issueDateEncodingLength() SBE_NOEXCEPT
    {
        return 2;
    }

    SBE_NODISCARD std::uint16_t issueDate() const SBE_NOEXCEPT
    {
        std::uint16_t val;
        std::memcpy(&val, m_buffer + m_offset + 185, sizeof(std::uint16_t));
        return SBE_LITTLE_ENDIAN_ENCODE_16(val);
    }

    MDInstrumentDefinitionFixedIncome57 &issueDate(const std::uint16_t value) SBE_NOEXCEPT
    {
        std::uint16_t val = SBE_LITTLE_ENDIAN_ENCODE_16(value);
        std::memcpy(m_buffer + m_offset + 185, &val, sizeof(std::uint16_t));
        return *this;
    }

    SBE_NODISCARD static const char *DatedDateMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "LocalMktDate";
            case MetaAttribute::PRESENCE: return "optional";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t datedDateId() SBE_NOEXCEPT
    {
        return 873;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t datedDateSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool datedDateInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= datedDateSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t datedDateEncodingOffset() SBE_NOEXCEPT
    {
        return 187;
    }

    static SBE_CONSTEXPR std::uint16_t datedDateNullValue() SBE_NOEXCEPT
    {
        return static_cast<std::uint16_t>(65535);
    }

    static SBE_CONSTEXPR std::uint16_t datedDateMinValue() SBE_NOEXCEPT
    {
        return static_cast<std::uint16_t>(0);
    }

    static SBE_CONSTEXPR std::uint16_t datedDateMaxValue() SBE_NOEXCEPT
    {
        return static_cast<std::uint16_t>(65534);
    }

    static SBE_CONSTEXPR std::size_t datedDateEncodingLength() SBE_NOEXCEPT
    {
        return 2;
    }

    SBE_NODISCARD std::uint16_t datedDate() const SBE_NOEXCEPT
    {
        std::uint16_t val;
        std::memcpy(&val, m_buffer + m_offset + 187, sizeof(std::uint16_t));
        return SBE_LITTLE_ENDIAN_ENCODE_16(val);
    }

    MDInstrumentDefinitionFixedIncome57 &datedDate(const std::uint16_t value) SBE_NOEXCEPT
    {
        std::uint16_t val = SBE_LITTLE_ENDIAN_ENCODE_16(value);
        std::memcpy(m_buffer + m_offset + 187, &val, sizeof(std::uint16_t));
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
        return 0;
    }

    SBE_NODISCARD bool maturityDateInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= maturityDateSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t maturityDateEncodingOffset() SBE_NOEXCEPT
    {
        return 189;
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
        std::uint16_t val;
        std::memcpy(&val, m_buffer + m_offset + 189, sizeof(std::uint16_t));
        return SBE_LITTLE_ENDIAN_ENCODE_16(val);
    }

    MDInstrumentDefinitionFixedIncome57 &maturityDate(const std::uint16_t value) SBE_NOEXCEPT
    {
        std::uint16_t val = SBE_LITTLE_ENDIAN_ENCODE_16(value);
        std::memcpy(m_buffer + m_offset + 189, &val, sizeof(std::uint16_t));
        return *this;
    }

    SBE_NODISCARD static const char *CouponRateMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "Percentage";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t couponRateId() SBE_NOEXCEPT
    {
        return 223;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t couponRateSinceVersion() SBE_NOEXCEPT
    {
        return 9;
    }

    SBE_NODISCARD bool couponRateInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= couponRateSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t couponRateEncodingOffset() SBE_NOEXCEPT
    {
        return 191;
    }

private:
    Decimal9NULL m_couponRate;

public:
    SBE_NODISCARD Decimal9NULL &couponRate()
    {
        m_couponRate.wrap(m_buffer, m_offset + 191, m_actingVersion, m_bufferLength);
        return m_couponRate;
    }

    SBE_NODISCARD static const char *ParValueMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "Price";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t parValueId() SBE_NOEXCEPT
    {
        return 37723;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t parValueSinceVersion() SBE_NOEXCEPT
    {
        return 9;
    }

    SBE_NODISCARD bool parValueInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= parValueSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t parValueEncodingOffset() SBE_NOEXCEPT
    {
        return 199;
    }

private:
    PRICENULL9 m_parValue;

public:
    SBE_NODISCARD PRICENULL9 &parValue()
    {
        m_parValue.wrap(m_buffer, m_offset + 199, m_actingVersion, m_bufferLength);
        return m_parValue;
    }

    SBE_NODISCARD static const char *CouponFrequencyUnitMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "String";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t couponFrequencyUnitId() SBE_NOEXCEPT
    {
        return 1949;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t couponFrequencyUnitSinceVersion() SBE_NOEXCEPT
    {
        return 10;
    }

    SBE_NODISCARD bool couponFrequencyUnitInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= couponFrequencyUnitSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t couponFrequencyUnitEncodingOffset() SBE_NOEXCEPT
    {
        return 207;
    }

    static SBE_CONSTEXPR char couponFrequencyUnitNullValue() SBE_NOEXCEPT
    {
        return static_cast<char>(0);
    }

    static SBE_CONSTEXPR char couponFrequencyUnitMinValue() SBE_NOEXCEPT
    {
        return static_cast<char>(32);
    }

    static SBE_CONSTEXPR char couponFrequencyUnitMaxValue() SBE_NOEXCEPT
    {
        return static_cast<char>(126);
    }

    static SBE_CONSTEXPR std::size_t couponFrequencyUnitEncodingLength() SBE_NOEXCEPT
    {
        return 3;
    }

    static SBE_CONSTEXPR std::uint64_t couponFrequencyUnitLength() SBE_NOEXCEPT
    {
        return 3;
    }

    SBE_NODISCARD const char *couponFrequencyUnit() const SBE_NOEXCEPT
    {
        if (m_actingVersion < 10)
        {
            return nullptr;
        }

        return m_buffer + m_offset + 207;
    }

    SBE_NODISCARD char *couponFrequencyUnit() SBE_NOEXCEPT
    {
        if (m_actingVersion < 10)
        {
            return nullptr;
        }

        return m_buffer + m_offset + 207;
    }

    SBE_NODISCARD char couponFrequencyUnit(const std::uint64_t index) const
    {
        if (index >= 3)
        {
            throw std::runtime_error("index out of range for couponFrequencyUnit [E104]");
        }

        if (m_actingVersion < 10)
        {
            return static_cast<char>(0);
        }

        char val;
        std::memcpy(&val, m_buffer + m_offset + 207 + (index * 1), sizeof(char));
        return (val);
    }

    MDInstrumentDefinitionFixedIncome57 &couponFrequencyUnit(const std::uint64_t index, const char value)
    {
        if (index >= 3)
        {
            throw std::runtime_error("index out of range for couponFrequencyUnit [E105]");
        }

        char val = (value);
        std::memcpy(m_buffer + m_offset + 207 + (index * 1), &val, sizeof(char));
        return *this;
    }

    std::uint64_t getCouponFrequencyUnit(char *const dst, const std::uint64_t length) const
    {
        if (length > 3)
        {
            throw std::runtime_error("length too large for getCouponFrequencyUnit [E106]");
        }

        if (m_actingVersion < 10)
        {
            return 0;
        }

        std::memcpy(dst, m_buffer + m_offset + 207, sizeof(char) * static_cast<std::size_t>(length));
        return length;
    }

    MDInstrumentDefinitionFixedIncome57 &putCouponFrequencyUnit(const char *const src) SBE_NOEXCEPT
    {
        std::memcpy(m_buffer + m_offset + 207, src, sizeof(char) * 3);
        return *this;
    }

    MDInstrumentDefinitionFixedIncome57 &putCouponFrequencyUnit(
        const char value0,
        const char value1,
        const char value2) SBE_NOEXCEPT
    {
        char val0 = (value0);
        std::memcpy(m_buffer + m_offset + 207, &val0, sizeof(char));
        char val1 = (value1);
        std::memcpy(m_buffer + m_offset + 208, &val1, sizeof(char));
        char val2 = (value2);
        std::memcpy(m_buffer + m_offset + 209, &val2, sizeof(char));

        return *this;
    }

    SBE_NODISCARD std::string getCouponFrequencyUnitAsString() const
    {
        const char *buffer = m_buffer + m_offset + 207;
        std::size_t length = 0;

        for (; length < 3 && *(buffer + length) != '\0'; ++length);
        std::string result(buffer, length);

        return result;
    }

    std::string getCouponFrequencyUnitAsJsonEscapedString()
    {
        if (m_actingVersion < 10)
        {
            return std::string("");
        }

        std::ostringstream oss;
        std::string s = getCouponFrequencyUnitAsString();

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
    SBE_NODISCARD std::string_view getCouponFrequencyUnitAsStringView() const SBE_NOEXCEPT
    {
        const char *buffer = m_buffer + m_offset + 207;
        std::size_t length = 0;

        for (; length < 3 && *(buffer + length) != '\0'; ++length);
        std::string_view result(buffer, length);

        return result;
    }
    #endif

    #if __cplusplus >= 201703L
    MDInstrumentDefinitionFixedIncome57 &putCouponFrequencyUnit(const std::string_view str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 3)
        {
            throw std::runtime_error("string too large for putCouponFrequencyUnit [E106]");
        }

        std::memcpy(m_buffer + m_offset + 207, str.data(), srcLength);
        for (std::size_t start = srcLength; start < 3; ++start)
        {
            m_buffer[m_offset + 207 + start] = 0;
        }

        return *this;
    }
    #else
    MDInstrumentDefinitionFixedIncome57 &putCouponFrequencyUnit(const std::string &str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 3)
        {
            throw std::runtime_error("string too large for putCouponFrequencyUnit [E106]");
        }

        std::memcpy(m_buffer + m_offset + 207, str.c_str(), srcLength);
        for (std::size_t start = srcLength; start < 3; ++start)
        {
            m_buffer[m_offset + 207 + start] = 0;
        }

        return *this;
    }
    #endif

    SBE_NODISCARD static const char *CouponFrequencyPeriodMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "optional";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t couponFrequencyPeriodId() SBE_NOEXCEPT
    {
        return 1948;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t couponFrequencyPeriodSinceVersion() SBE_NOEXCEPT
    {
        return 9;
    }

    SBE_NODISCARD bool couponFrequencyPeriodInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= couponFrequencyPeriodSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t couponFrequencyPeriodEncodingOffset() SBE_NOEXCEPT
    {
        return 210;
    }

    static SBE_CONSTEXPR std::uint16_t couponFrequencyPeriodNullValue() SBE_NOEXCEPT
    {
        return static_cast<std::uint16_t>(65535);
    }

    static SBE_CONSTEXPR std::uint16_t couponFrequencyPeriodMinValue() SBE_NOEXCEPT
    {
        return static_cast<std::uint16_t>(0);
    }

    static SBE_CONSTEXPR std::uint16_t couponFrequencyPeriodMaxValue() SBE_NOEXCEPT
    {
        return static_cast<std::uint16_t>(65534);
    }

    static SBE_CONSTEXPR std::size_t couponFrequencyPeriodEncodingLength() SBE_NOEXCEPT
    {
        return 2;
    }

    SBE_NODISCARD std::uint16_t couponFrequencyPeriod() const SBE_NOEXCEPT
    {
        if (m_actingVersion < 9)
        {
            return static_cast<std::uint16_t>(65535);
        }

        std::uint16_t val;
        std::memcpy(&val, m_buffer + m_offset + 210, sizeof(std::uint16_t));
        return SBE_LITTLE_ENDIAN_ENCODE_16(val);
    }

    MDInstrumentDefinitionFixedIncome57 &couponFrequencyPeriod(const std::uint16_t value) SBE_NOEXCEPT
    {
        std::uint16_t val = SBE_LITTLE_ENDIAN_ENCODE_16(value);
        std::memcpy(m_buffer + m_offset + 210, &val, sizeof(std::uint16_t));
        return *this;
    }

    SBE_NODISCARD static const char *CouponDayCountMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "String";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t couponDayCountId() SBE_NOEXCEPT
    {
        return 1950;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t couponDayCountSinceVersion() SBE_NOEXCEPT
    {
        return 10;
    }

    SBE_NODISCARD bool couponDayCountInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= couponDayCountSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t couponDayCountEncodingOffset() SBE_NOEXCEPT
    {
        return 212;
    }

    static SBE_CONSTEXPR char couponDayCountNullValue() SBE_NOEXCEPT
    {
        return static_cast<char>(0);
    }

    static SBE_CONSTEXPR char couponDayCountMinValue() SBE_NOEXCEPT
    {
        return static_cast<char>(32);
    }

    static SBE_CONSTEXPR char couponDayCountMaxValue() SBE_NOEXCEPT
    {
        return static_cast<char>(126);
    }

    static SBE_CONSTEXPR std::size_t couponDayCountEncodingLength() SBE_NOEXCEPT
    {
        return 20;
    }

    static SBE_CONSTEXPR std::uint64_t couponDayCountLength() SBE_NOEXCEPT
    {
        return 20;
    }

    SBE_NODISCARD const char *couponDayCount() const SBE_NOEXCEPT
    {
        if (m_actingVersion < 10)
        {
            return nullptr;
        }

        return m_buffer + m_offset + 212;
    }

    SBE_NODISCARD char *couponDayCount() SBE_NOEXCEPT
    {
        if (m_actingVersion < 10)
        {
            return nullptr;
        }

        return m_buffer + m_offset + 212;
    }

    SBE_NODISCARD char couponDayCount(const std::uint64_t index) const
    {
        if (index >= 20)
        {
            throw std::runtime_error("index out of range for couponDayCount [E104]");
        }

        if (m_actingVersion < 10)
        {
            return static_cast<char>(0);
        }

        char val;
        std::memcpy(&val, m_buffer + m_offset + 212 + (index * 1), sizeof(char));
        return (val);
    }

    MDInstrumentDefinitionFixedIncome57 &couponDayCount(const std::uint64_t index, const char value)
    {
        if (index >= 20)
        {
            throw std::runtime_error("index out of range for couponDayCount [E105]");
        }

        char val = (value);
        std::memcpy(m_buffer + m_offset + 212 + (index * 1), &val, sizeof(char));
        return *this;
    }

    std::uint64_t getCouponDayCount(char *const dst, const std::uint64_t length) const
    {
        if (length > 20)
        {
            throw std::runtime_error("length too large for getCouponDayCount [E106]");
        }

        if (m_actingVersion < 10)
        {
            return 0;
        }

        std::memcpy(dst, m_buffer + m_offset + 212, sizeof(char) * static_cast<std::size_t>(length));
        return length;
    }

    MDInstrumentDefinitionFixedIncome57 &putCouponDayCount(const char *const src) SBE_NOEXCEPT
    {
        std::memcpy(m_buffer + m_offset + 212, src, sizeof(char) * 20);
        return *this;
    }

    SBE_NODISCARD std::string getCouponDayCountAsString() const
    {
        const char *buffer = m_buffer + m_offset + 212;
        std::size_t length = 0;

        for (; length < 20 && *(buffer + length) != '\0'; ++length);
        std::string result(buffer, length);

        return result;
    }

    std::string getCouponDayCountAsJsonEscapedString()
    {
        if (m_actingVersion < 10)
        {
            return std::string("");
        }

        std::ostringstream oss;
        std::string s = getCouponDayCountAsString();

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
    SBE_NODISCARD std::string_view getCouponDayCountAsStringView() const SBE_NOEXCEPT
    {
        const char *buffer = m_buffer + m_offset + 212;
        std::size_t length = 0;

        for (; length < 20 && *(buffer + length) != '\0'; ++length);
        std::string_view result(buffer, length);

        return result;
    }
    #endif

    #if __cplusplus >= 201703L
    MDInstrumentDefinitionFixedIncome57 &putCouponDayCount(const std::string_view str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 20)
        {
            throw std::runtime_error("string too large for putCouponDayCount [E106]");
        }

        std::memcpy(m_buffer + m_offset + 212, str.data(), srcLength);
        for (std::size_t start = srcLength; start < 20; ++start)
        {
            m_buffer[m_offset + 212 + start] = 0;
        }

        return *this;
    }
    #else
    MDInstrumentDefinitionFixedIncome57 &putCouponDayCount(const std::string &str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 20)
        {
            throw std::runtime_error("string too large for putCouponDayCount [E106]");
        }

        std::memcpy(m_buffer + m_offset + 212, str.c_str(), srcLength);
        for (std::size_t start = srcLength; start < 20; ++start)
        {
            m_buffer[m_offset + 212 + start] = 0;
        }

        return *this;
    }
    #endif

    SBE_NODISCARD static const char *CountryOfIssueMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "String";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t countryOfIssueId() SBE_NOEXCEPT
    {
        return 470;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t countryOfIssueSinceVersion() SBE_NOEXCEPT
    {
        return 10;
    }

    SBE_NODISCARD bool countryOfIssueInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= countryOfIssueSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t countryOfIssueEncodingOffset() SBE_NOEXCEPT
    {
        return 232;
    }

    static SBE_CONSTEXPR char countryOfIssueNullValue() SBE_NOEXCEPT
    {
        return static_cast<char>(0);
    }

    static SBE_CONSTEXPR char countryOfIssueMinValue() SBE_NOEXCEPT
    {
        return static_cast<char>(32);
    }

    static SBE_CONSTEXPR char countryOfIssueMaxValue() SBE_NOEXCEPT
    {
        return static_cast<char>(126);
    }

    static SBE_CONSTEXPR std::size_t countryOfIssueEncodingLength() SBE_NOEXCEPT
    {
        return 2;
    }

    static SBE_CONSTEXPR std::uint64_t countryOfIssueLength() SBE_NOEXCEPT
    {
        return 2;
    }

    SBE_NODISCARD const char *countryOfIssue() const SBE_NOEXCEPT
    {
        if (m_actingVersion < 10)
        {
            return nullptr;
        }

        return m_buffer + m_offset + 232;
    }

    SBE_NODISCARD char *countryOfIssue() SBE_NOEXCEPT
    {
        if (m_actingVersion < 10)
        {
            return nullptr;
        }

        return m_buffer + m_offset + 232;
    }

    SBE_NODISCARD char countryOfIssue(const std::uint64_t index) const
    {
        if (index >= 2)
        {
            throw std::runtime_error("index out of range for countryOfIssue [E104]");
        }

        if (m_actingVersion < 10)
        {
            return static_cast<char>(0);
        }

        char val;
        std::memcpy(&val, m_buffer + m_offset + 232 + (index * 1), sizeof(char));
        return (val);
    }

    MDInstrumentDefinitionFixedIncome57 &countryOfIssue(const std::uint64_t index, const char value)
    {
        if (index >= 2)
        {
            throw std::runtime_error("index out of range for countryOfIssue [E105]");
        }

        char val = (value);
        std::memcpy(m_buffer + m_offset + 232 + (index * 1), &val, sizeof(char));
        return *this;
    }

    std::uint64_t getCountryOfIssue(char *const dst, const std::uint64_t length) const
    {
        if (length > 2)
        {
            throw std::runtime_error("length too large for getCountryOfIssue [E106]");
        }

        if (m_actingVersion < 10)
        {
            return 0;
        }

        std::memcpy(dst, m_buffer + m_offset + 232, sizeof(char) * static_cast<std::size_t>(length));
        return length;
    }

    MDInstrumentDefinitionFixedIncome57 &putCountryOfIssue(const char *const src) SBE_NOEXCEPT
    {
        std::memcpy(m_buffer + m_offset + 232, src, sizeof(char) * 2);
        return *this;
    }

    MDInstrumentDefinitionFixedIncome57 &putCountryOfIssue(
        const char value0,
        const char value1) SBE_NOEXCEPT
    {
        char val0 = (value0);
        std::memcpy(m_buffer + m_offset + 232, &val0, sizeof(char));
        char val1 = (value1);
        std::memcpy(m_buffer + m_offset + 233, &val1, sizeof(char));

        return *this;
    }

    SBE_NODISCARD std::string getCountryOfIssueAsString() const
    {
        const char *buffer = m_buffer + m_offset + 232;
        std::size_t length = 0;

        for (; length < 2 && *(buffer + length) != '\0'; ++length);
        std::string result(buffer, length);

        return result;
    }

    std::string getCountryOfIssueAsJsonEscapedString()
    {
        if (m_actingVersion < 10)
        {
            return std::string("");
        }

        std::ostringstream oss;
        std::string s = getCountryOfIssueAsString();

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
    SBE_NODISCARD std::string_view getCountryOfIssueAsStringView() const SBE_NOEXCEPT
    {
        const char *buffer = m_buffer + m_offset + 232;
        std::size_t length = 0;

        for (; length < 2 && *(buffer + length) != '\0'; ++length);
        std::string_view result(buffer, length);

        return result;
    }
    #endif

    #if __cplusplus >= 201703L
    MDInstrumentDefinitionFixedIncome57 &putCountryOfIssue(const std::string_view str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 2)
        {
            throw std::runtime_error("string too large for putCountryOfIssue [E106]");
        }

        std::memcpy(m_buffer + m_offset + 232, str.data(), srcLength);
        for (std::size_t start = srcLength; start < 2; ++start)
        {
            m_buffer[m_offset + 232 + start] = 0;
        }

        return *this;
    }
    #else
    MDInstrumentDefinitionFixedIncome57 &putCountryOfIssue(const std::string &str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 2)
        {
            throw std::runtime_error("string too large for putCountryOfIssue [E106]");
        }

        std::memcpy(m_buffer + m_offset + 232, str.c_str(), srcLength);
        for (std::size_t start = srcLength; start < 2; ++start)
        {
            m_buffer[m_offset + 232 + start] = 0;
        }

        return *this;
    }
    #endif

    SBE_NODISCARD static const char *IssuerMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "String";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t issuerId() SBE_NOEXCEPT
    {
        return 106;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t issuerSinceVersion() SBE_NOEXCEPT
    {
        return 10;
    }

    SBE_NODISCARD bool issuerInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= issuerSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t issuerEncodingOffset() SBE_NOEXCEPT
    {
        return 234;
    }

    static SBE_CONSTEXPR char issuerNullValue() SBE_NOEXCEPT
    {
        return static_cast<char>(0);
    }

    static SBE_CONSTEXPR char issuerMinValue() SBE_NOEXCEPT
    {
        return static_cast<char>(32);
    }

    static SBE_CONSTEXPR char issuerMaxValue() SBE_NOEXCEPT
    {
        return static_cast<char>(126);
    }

    static SBE_CONSTEXPR std::size_t issuerEncodingLength() SBE_NOEXCEPT
    {
        return 25;
    }

    static SBE_CONSTEXPR std::uint64_t issuerLength() SBE_NOEXCEPT
    {
        return 25;
    }

    SBE_NODISCARD const char *issuer() const SBE_NOEXCEPT
    {
        if (m_actingVersion < 10)
        {
            return nullptr;
        }

        return m_buffer + m_offset + 234;
    }

    SBE_NODISCARD char *issuer() SBE_NOEXCEPT
    {
        if (m_actingVersion < 10)
        {
            return nullptr;
        }

        return m_buffer + m_offset + 234;
    }

    SBE_NODISCARD char issuer(const std::uint64_t index) const
    {
        if (index >= 25)
        {
            throw std::runtime_error("index out of range for issuer [E104]");
        }

        if (m_actingVersion < 10)
        {
            return static_cast<char>(0);
        }

        char val;
        std::memcpy(&val, m_buffer + m_offset + 234 + (index * 1), sizeof(char));
        return (val);
    }

    MDInstrumentDefinitionFixedIncome57 &issuer(const std::uint64_t index, const char value)
    {
        if (index >= 25)
        {
            throw std::runtime_error("index out of range for issuer [E105]");
        }

        char val = (value);
        std::memcpy(m_buffer + m_offset + 234 + (index * 1), &val, sizeof(char));
        return *this;
    }

    std::uint64_t getIssuer(char *const dst, const std::uint64_t length) const
    {
        if (length > 25)
        {
            throw std::runtime_error("length too large for getIssuer [E106]");
        }

        if (m_actingVersion < 10)
        {
            return 0;
        }

        std::memcpy(dst, m_buffer + m_offset + 234, sizeof(char) * static_cast<std::size_t>(length));
        return length;
    }

    MDInstrumentDefinitionFixedIncome57 &putIssuer(const char *const src) SBE_NOEXCEPT
    {
        std::memcpy(m_buffer + m_offset + 234, src, sizeof(char) * 25);
        return *this;
    }

    SBE_NODISCARD std::string getIssuerAsString() const
    {
        const char *buffer = m_buffer + m_offset + 234;
        std::size_t length = 0;

        for (; length < 25 && *(buffer + length) != '\0'; ++length);
        std::string result(buffer, length);

        return result;
    }

    std::string getIssuerAsJsonEscapedString()
    {
        if (m_actingVersion < 10)
        {
            return std::string("");
        }

        std::ostringstream oss;
        std::string s = getIssuerAsString();

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
    SBE_NODISCARD std::string_view getIssuerAsStringView() const SBE_NOEXCEPT
    {
        const char *buffer = m_buffer + m_offset + 234;
        std::size_t length = 0;

        for (; length < 25 && *(buffer + length) != '\0'; ++length);
        std::string_view result(buffer, length);

        return result;
    }
    #endif

    #if __cplusplus >= 201703L
    MDInstrumentDefinitionFixedIncome57 &putIssuer(const std::string_view str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 25)
        {
            throw std::runtime_error("string too large for putIssuer [E106]");
        }

        std::memcpy(m_buffer + m_offset + 234, str.data(), srcLength);
        for (std::size_t start = srcLength; start < 25; ++start)
        {
            m_buffer[m_offset + 234 + start] = 0;
        }

        return *this;
    }
    #else
    MDInstrumentDefinitionFixedIncome57 &putIssuer(const std::string &str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 25)
        {
            throw std::runtime_error("string too large for putIssuer [E106]");
        }

        std::memcpy(m_buffer + m_offset + 234, str.c_str(), srcLength);
        for (std::size_t start = srcLength; start < 25; ++start)
        {
            m_buffer[m_offset + 234 + start] = 0;
        }

        return *this;
    }
    #endif

    SBE_NODISCARD static const char *FinancialInstrumentFullNameMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "String";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t financialInstrumentFullNameId() SBE_NOEXCEPT
    {
        return 2714;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t financialInstrumentFullNameSinceVersion() SBE_NOEXCEPT
    {
        return 10;
    }

    SBE_NODISCARD bool financialInstrumentFullNameInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= financialInstrumentFullNameSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t financialInstrumentFullNameEncodingOffset() SBE_NOEXCEPT
    {
        return 259;
    }

    static SBE_CONSTEXPR char financialInstrumentFullNameNullValue() SBE_NOEXCEPT
    {
        return static_cast<char>(0);
    }

    static SBE_CONSTEXPR char financialInstrumentFullNameMinValue() SBE_NOEXCEPT
    {
        return static_cast<char>(32);
    }

    static SBE_CONSTEXPR char financialInstrumentFullNameMaxValue() SBE_NOEXCEPT
    {
        return static_cast<char>(126);
    }

    static SBE_CONSTEXPR std::size_t financialInstrumentFullNameEncodingLength() SBE_NOEXCEPT
    {
        return 35;
    }

    static SBE_CONSTEXPR std::uint64_t financialInstrumentFullNameLength() SBE_NOEXCEPT
    {
        return 35;
    }

    SBE_NODISCARD const char *financialInstrumentFullName() const SBE_NOEXCEPT
    {
        if (m_actingVersion < 10)
        {
            return nullptr;
        }

        return m_buffer + m_offset + 259;
    }

    SBE_NODISCARD char *financialInstrumentFullName() SBE_NOEXCEPT
    {
        if (m_actingVersion < 10)
        {
            return nullptr;
        }

        return m_buffer + m_offset + 259;
    }

    SBE_NODISCARD char financialInstrumentFullName(const std::uint64_t index) const
    {
        if (index >= 35)
        {
            throw std::runtime_error("index out of range for financialInstrumentFullName [E104]");
        }

        if (m_actingVersion < 10)
        {
            return static_cast<char>(0);
        }

        char val;
        std::memcpy(&val, m_buffer + m_offset + 259 + (index * 1), sizeof(char));
        return (val);
    }

    MDInstrumentDefinitionFixedIncome57 &financialInstrumentFullName(const std::uint64_t index, const char value)
    {
        if (index >= 35)
        {
            throw std::runtime_error("index out of range for financialInstrumentFullName [E105]");
        }

        char val = (value);
        std::memcpy(m_buffer + m_offset + 259 + (index * 1), &val, sizeof(char));
        return *this;
    }

    std::uint64_t getFinancialInstrumentFullName(char *const dst, const std::uint64_t length) const
    {
        if (length > 35)
        {
            throw std::runtime_error("length too large for getFinancialInstrumentFullName [E106]");
        }

        if (m_actingVersion < 10)
        {
            return 0;
        }

        std::memcpy(dst, m_buffer + m_offset + 259, sizeof(char) * static_cast<std::size_t>(length));
        return length;
    }

    MDInstrumentDefinitionFixedIncome57 &putFinancialInstrumentFullName(const char *const src) SBE_NOEXCEPT
    {
        std::memcpy(m_buffer + m_offset + 259, src, sizeof(char) * 35);
        return *this;
    }

    SBE_NODISCARD std::string getFinancialInstrumentFullNameAsString() const
    {
        const char *buffer = m_buffer + m_offset + 259;
        std::size_t length = 0;

        for (; length < 35 && *(buffer + length) != '\0'; ++length);
        std::string result(buffer, length);

        return result;
    }

    std::string getFinancialInstrumentFullNameAsJsonEscapedString()
    {
        if (m_actingVersion < 10)
        {
            return std::string("");
        }

        std::ostringstream oss;
        std::string s = getFinancialInstrumentFullNameAsString();

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
    SBE_NODISCARD std::string_view getFinancialInstrumentFullNameAsStringView() const SBE_NOEXCEPT
    {
        const char *buffer = m_buffer + m_offset + 259;
        std::size_t length = 0;

        for (; length < 35 && *(buffer + length) != '\0'; ++length);
        std::string_view result(buffer, length);

        return result;
    }
    #endif

    #if __cplusplus >= 201703L
    MDInstrumentDefinitionFixedIncome57 &putFinancialInstrumentFullName(const std::string_view str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 35)
        {
            throw std::runtime_error("string too large for putFinancialInstrumentFullName [E106]");
        }

        std::memcpy(m_buffer + m_offset + 259, str.data(), srcLength);
        for (std::size_t start = srcLength; start < 35; ++start)
        {
            m_buffer[m_offset + 259 + start] = 0;
        }

        return *this;
    }
    #else
    MDInstrumentDefinitionFixedIncome57 &putFinancialInstrumentFullName(const std::string &str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 35)
        {
            throw std::runtime_error("string too large for putFinancialInstrumentFullName [E106]");
        }

        std::memcpy(m_buffer + m_offset + 259, str.c_str(), srcLength);
        for (std::size_t start = srcLength; start < 35; ++start)
        {
            m_buffer[m_offset + 259 + start] = 0;
        }

        return *this;
    }
    #endif

    SBE_NODISCARD static const char *SecurityAltIDMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "String";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t securityAltIDId() SBE_NOEXCEPT
    {
        return 455;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t securityAltIDSinceVersion() SBE_NOEXCEPT
    {
        return 10;
    }

    SBE_NODISCARD bool securityAltIDInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= securityAltIDSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t securityAltIDEncodingOffset() SBE_NOEXCEPT
    {
        return 294;
    }

    static SBE_CONSTEXPR char securityAltIDNullValue() SBE_NOEXCEPT
    {
        return static_cast<char>(0);
    }

    static SBE_CONSTEXPR char securityAltIDMinValue() SBE_NOEXCEPT
    {
        return static_cast<char>(32);
    }

    static SBE_CONSTEXPR char securityAltIDMaxValue() SBE_NOEXCEPT
    {
        return static_cast<char>(126);
    }

    static SBE_CONSTEXPR std::size_t securityAltIDEncodingLength() SBE_NOEXCEPT
    {
        return 12;
    }

    static SBE_CONSTEXPR std::uint64_t securityAltIDLength() SBE_NOEXCEPT
    {
        return 12;
    }

    SBE_NODISCARD const char *securityAltID() const SBE_NOEXCEPT
    {
        if (m_actingVersion < 10)
        {
            return nullptr;
        }

        return m_buffer + m_offset + 294;
    }

    SBE_NODISCARD char *securityAltID() SBE_NOEXCEPT
    {
        if (m_actingVersion < 10)
        {
            return nullptr;
        }

        return m_buffer + m_offset + 294;
    }

    SBE_NODISCARD char securityAltID(const std::uint64_t index) const
    {
        if (index >= 12)
        {
            throw std::runtime_error("index out of range for securityAltID [E104]");
        }

        if (m_actingVersion < 10)
        {
            return static_cast<char>(0);
        }

        char val;
        std::memcpy(&val, m_buffer + m_offset + 294 + (index * 1), sizeof(char));
        return (val);
    }

    MDInstrumentDefinitionFixedIncome57 &securityAltID(const std::uint64_t index, const char value)
    {
        if (index >= 12)
        {
            throw std::runtime_error("index out of range for securityAltID [E105]");
        }

        char val = (value);
        std::memcpy(m_buffer + m_offset + 294 + (index * 1), &val, sizeof(char));
        return *this;
    }

    std::uint64_t getSecurityAltID(char *const dst, const std::uint64_t length) const
    {
        if (length > 12)
        {
            throw std::runtime_error("length too large for getSecurityAltID [E106]");
        }

        if (m_actingVersion < 10)
        {
            return 0;
        }

        std::memcpy(dst, m_buffer + m_offset + 294, sizeof(char) * static_cast<std::size_t>(length));
        return length;
    }

    MDInstrumentDefinitionFixedIncome57 &putSecurityAltID(const char *const src) SBE_NOEXCEPT
    {
        std::memcpy(m_buffer + m_offset + 294, src, sizeof(char) * 12);
        return *this;
    }

    SBE_NODISCARD std::string getSecurityAltIDAsString() const
    {
        const char *buffer = m_buffer + m_offset + 294;
        std::size_t length = 0;

        for (; length < 12 && *(buffer + length) != '\0'; ++length);
        std::string result(buffer, length);

        return result;
    }

    std::string getSecurityAltIDAsJsonEscapedString()
    {
        if (m_actingVersion < 10)
        {
            return std::string("");
        }

        std::ostringstream oss;
        std::string s = getSecurityAltIDAsString();

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
    SBE_NODISCARD std::string_view getSecurityAltIDAsStringView() const SBE_NOEXCEPT
    {
        const char *buffer = m_buffer + m_offset + 294;
        std::size_t length = 0;

        for (; length < 12 && *(buffer + length) != '\0'; ++length);
        std::string_view result(buffer, length);

        return result;
    }
    #endif

    #if __cplusplus >= 201703L
    MDInstrumentDefinitionFixedIncome57 &putSecurityAltID(const std::string_view str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 12)
        {
            throw std::runtime_error("string too large for putSecurityAltID [E106]");
        }

        std::memcpy(m_buffer + m_offset + 294, str.data(), srcLength);
        for (std::size_t start = srcLength; start < 12; ++start)
        {
            m_buffer[m_offset + 294 + start] = 0;
        }

        return *this;
    }
    #else
    MDInstrumentDefinitionFixedIncome57 &putSecurityAltID(const std::string &str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 12)
        {
            throw std::runtime_error("string too large for putSecurityAltID [E106]");
        }

        std::memcpy(m_buffer + m_offset + 294, str.c_str(), srcLength);
        for (std::size_t start = srcLength; start < 12; ++start)
        {
            m_buffer[m_offset + 294 + start] = 0;
        }

        return *this;
    }
    #endif

    SBE_NODISCARD static const char *SecurityAltIDSourceMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t securityAltIDSourceId() SBE_NOEXCEPT
    {
        return 456;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t securityAltIDSourceSinceVersion() SBE_NOEXCEPT
    {
        return 10;
    }

    SBE_NODISCARD bool securityAltIDSourceInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= securityAltIDSourceSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t securityAltIDSourceEncodingOffset() SBE_NOEXCEPT
    {
        return 306;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t securityAltIDSourceEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD std::uint8_t securityAltIDSourceRaw() const SBE_NOEXCEPT
    {
        if (m_actingVersion < 10)
        {
            return static_cast<std::uint8_t>(255);
        }

        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 306, sizeof(std::uint8_t));
        return (val);
    }

    SBE_NODISCARD SecurityAltIDSource::Value securityAltIDSource() const
    {
        if (m_actingVersion < 10)
        {
            return SecurityAltIDSource::NULL_VALUE;
        }

        std::uint8_t val;
        std::memcpy(&val, m_buffer + m_offset + 306, sizeof(std::uint8_t));
        return SecurityAltIDSource::get((val));
    }

    MDInstrumentDefinitionFixedIncome57 &securityAltIDSource(const SecurityAltIDSource::Value value) SBE_NOEXCEPT
    {
        std::uint8_t val = (value);
        std::memcpy(m_buffer + m_offset + 306, &val, sizeof(std::uint8_t));
        return *this;
    }

    SBE_NODISCARD static const char *PriceQuoteMethodMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "String";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t priceQuoteMethodId() SBE_NOEXCEPT
    {
        return 1196;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t priceQuoteMethodSinceVersion() SBE_NOEXCEPT
    {
        return 10;
    }

    SBE_NODISCARD bool priceQuoteMethodInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= priceQuoteMethodSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t priceQuoteMethodEncodingOffset() SBE_NOEXCEPT
    {
        return 307;
    }

    static SBE_CONSTEXPR char priceQuoteMethodNullValue() SBE_NOEXCEPT
    {
        return static_cast<char>(0);
    }

    static SBE_CONSTEXPR char priceQuoteMethodMinValue() SBE_NOEXCEPT
    {
        return static_cast<char>(32);
    }

    static SBE_CONSTEXPR char priceQuoteMethodMaxValue() SBE_NOEXCEPT
    {
        return static_cast<char>(126);
    }

    static SBE_CONSTEXPR std::size_t priceQuoteMethodEncodingLength() SBE_NOEXCEPT
    {
        return 5;
    }

    static SBE_CONSTEXPR std::uint64_t priceQuoteMethodLength() SBE_NOEXCEPT
    {
        return 5;
    }

    SBE_NODISCARD const char *priceQuoteMethod() const SBE_NOEXCEPT
    {
        if (m_actingVersion < 10)
        {
            return nullptr;
        }

        return m_buffer + m_offset + 307;
    }

    SBE_NODISCARD char *priceQuoteMethod() SBE_NOEXCEPT
    {
        if (m_actingVersion < 10)
        {
            return nullptr;
        }

        return m_buffer + m_offset + 307;
    }

    SBE_NODISCARD char priceQuoteMethod(const std::uint64_t index) const
    {
        if (index >= 5)
        {
            throw std::runtime_error("index out of range for priceQuoteMethod [E104]");
        }

        if (m_actingVersion < 10)
        {
            return static_cast<char>(0);
        }

        char val;
        std::memcpy(&val, m_buffer + m_offset + 307 + (index * 1), sizeof(char));
        return (val);
    }

    MDInstrumentDefinitionFixedIncome57 &priceQuoteMethod(const std::uint64_t index, const char value)
    {
        if (index >= 5)
        {
            throw std::runtime_error("index out of range for priceQuoteMethod [E105]");
        }

        char val = (value);
        std::memcpy(m_buffer + m_offset + 307 + (index * 1), &val, sizeof(char));
        return *this;
    }

    std::uint64_t getPriceQuoteMethod(char *const dst, const std::uint64_t length) const
    {
        if (length > 5)
        {
            throw std::runtime_error("length too large for getPriceQuoteMethod [E106]");
        }

        if (m_actingVersion < 10)
        {
            return 0;
        }

        std::memcpy(dst, m_buffer + m_offset + 307, sizeof(char) * static_cast<std::size_t>(length));
        return length;
    }

    MDInstrumentDefinitionFixedIncome57 &putPriceQuoteMethod(const char *const src) SBE_NOEXCEPT
    {
        std::memcpy(m_buffer + m_offset + 307, src, sizeof(char) * 5);
        return *this;
    }

    SBE_NODISCARD std::string getPriceQuoteMethodAsString() const
    {
        const char *buffer = m_buffer + m_offset + 307;
        std::size_t length = 0;

        for (; length < 5 && *(buffer + length) != '\0'; ++length);
        std::string result(buffer, length);

        return result;
    }

    std::string getPriceQuoteMethodAsJsonEscapedString()
    {
        if (m_actingVersion < 10)
        {
            return std::string("");
        }

        std::ostringstream oss;
        std::string s = getPriceQuoteMethodAsString();

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
    SBE_NODISCARD std::string_view getPriceQuoteMethodAsStringView() const SBE_NOEXCEPT
    {
        const char *buffer = m_buffer + m_offset + 307;
        std::size_t length = 0;

        for (; length < 5 && *(buffer + length) != '\0'; ++length);
        std::string_view result(buffer, length);

        return result;
    }
    #endif

    #if __cplusplus >= 201703L
    MDInstrumentDefinitionFixedIncome57 &putPriceQuoteMethod(const std::string_view str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 5)
        {
            throw std::runtime_error("string too large for putPriceQuoteMethod [E106]");
        }

        std::memcpy(m_buffer + m_offset + 307, str.data(), srcLength);
        for (std::size_t start = srcLength; start < 5; ++start)
        {
            m_buffer[m_offset + 307 + start] = 0;
        }

        return *this;
    }
    #else
    MDInstrumentDefinitionFixedIncome57 &putPriceQuoteMethod(const std::string &str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 5)
        {
            throw std::runtime_error("string too large for putPriceQuoteMethod [E106]");
        }

        std::memcpy(m_buffer + m_offset + 307, str.c_str(), srcLength);
        for (std::size_t start = srcLength; start < 5; ++start)
        {
            m_buffer[m_offset + 307 + start] = 0;
        }

        return *this;
    }
    #endif

    SBE_NODISCARD static const char *PartyRoleClearingOrgMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "String";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t partyRoleClearingOrgId() SBE_NOEXCEPT
    {
        return 9736;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t partyRoleClearingOrgSinceVersion() SBE_NOEXCEPT
    {
        return 10;
    }

    SBE_NODISCARD bool partyRoleClearingOrgInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= partyRoleClearingOrgSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t partyRoleClearingOrgEncodingOffset() SBE_NOEXCEPT
    {
        return 312;
    }

    static SBE_CONSTEXPR char partyRoleClearingOrgNullValue() SBE_NOEXCEPT
    {
        return static_cast<char>(0);
    }

    static SBE_CONSTEXPR char partyRoleClearingOrgMinValue() SBE_NOEXCEPT
    {
        return static_cast<char>(32);
    }

    static SBE_CONSTEXPR char partyRoleClearingOrgMaxValue() SBE_NOEXCEPT
    {
        return static_cast<char>(126);
    }

    static SBE_CONSTEXPR std::size_t partyRoleClearingOrgEncodingLength() SBE_NOEXCEPT
    {
        return 5;
    }

    static SBE_CONSTEXPR std::uint64_t partyRoleClearingOrgLength() SBE_NOEXCEPT
    {
        return 5;
    }

    SBE_NODISCARD const char *partyRoleClearingOrg() const SBE_NOEXCEPT
    {
        if (m_actingVersion < 10)
        {
            return nullptr;
        }

        return m_buffer + m_offset + 312;
    }

    SBE_NODISCARD char *partyRoleClearingOrg() SBE_NOEXCEPT
    {
        if (m_actingVersion < 10)
        {
            return nullptr;
        }

        return m_buffer + m_offset + 312;
    }

    SBE_NODISCARD char partyRoleClearingOrg(const std::uint64_t index) const
    {
        if (index >= 5)
        {
            throw std::runtime_error("index out of range for partyRoleClearingOrg [E104]");
        }

        if (m_actingVersion < 10)
        {
            return static_cast<char>(0);
        }

        char val;
        std::memcpy(&val, m_buffer + m_offset + 312 + (index * 1), sizeof(char));
        return (val);
    }

    MDInstrumentDefinitionFixedIncome57 &partyRoleClearingOrg(const std::uint64_t index, const char value)
    {
        if (index >= 5)
        {
            throw std::runtime_error("index out of range for partyRoleClearingOrg [E105]");
        }

        char val = (value);
        std::memcpy(m_buffer + m_offset + 312 + (index * 1), &val, sizeof(char));
        return *this;
    }

    std::uint64_t getPartyRoleClearingOrg(char *const dst, const std::uint64_t length) const
    {
        if (length > 5)
        {
            throw std::runtime_error("length too large for getPartyRoleClearingOrg [E106]");
        }

        if (m_actingVersion < 10)
        {
            return 0;
        }

        std::memcpy(dst, m_buffer + m_offset + 312, sizeof(char) * static_cast<std::size_t>(length));
        return length;
    }

    MDInstrumentDefinitionFixedIncome57 &putPartyRoleClearingOrg(const char *const src) SBE_NOEXCEPT
    {
        std::memcpy(m_buffer + m_offset + 312, src, sizeof(char) * 5);
        return *this;
    }

    SBE_NODISCARD std::string getPartyRoleClearingOrgAsString() const
    {
        const char *buffer = m_buffer + m_offset + 312;
        std::size_t length = 0;

        for (; length < 5 && *(buffer + length) != '\0'; ++length);
        std::string result(buffer, length);

        return result;
    }

    std::string getPartyRoleClearingOrgAsJsonEscapedString()
    {
        if (m_actingVersion < 10)
        {
            return std::string("");
        }

        std::ostringstream oss;
        std::string s = getPartyRoleClearingOrgAsString();

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
    SBE_NODISCARD std::string_view getPartyRoleClearingOrgAsStringView() const SBE_NOEXCEPT
    {
        const char *buffer = m_buffer + m_offset + 312;
        std::size_t length = 0;

        for (; length < 5 && *(buffer + length) != '\0'; ++length);
        std::string_view result(buffer, length);

        return result;
    }
    #endif

    #if __cplusplus >= 201703L
    MDInstrumentDefinitionFixedIncome57 &putPartyRoleClearingOrg(const std::string_view str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 5)
        {
            throw std::runtime_error("string too large for putPartyRoleClearingOrg [E106]");
        }

        std::memcpy(m_buffer + m_offset + 312, str.data(), srcLength);
        for (std::size_t start = srcLength; start < 5; ++start)
        {
            m_buffer[m_offset + 312 + start] = 0;
        }

        return *this;
    }
    #else
    MDInstrumentDefinitionFixedIncome57 &putPartyRoleClearingOrg(const std::string &str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 5)
        {
            throw std::runtime_error("string too large for putPartyRoleClearingOrg [E106]");
        }

        std::memcpy(m_buffer + m_offset + 312, str.c_str(), srcLength);
        for (std::size_t start = srcLength; start < 5; ++start)
        {
            m_buffer[m_offset + 312 + start] = 0;
        }

        return *this;
    }
    #endif

    SBE_NODISCARD static const char *UserDefinedInstrumentMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "char";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t userDefinedInstrumentId() SBE_NOEXCEPT
    {
        return 9779;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t userDefinedInstrumentSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool userDefinedInstrumentInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= userDefinedInstrumentSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t userDefinedInstrumentEncodingOffset() SBE_NOEXCEPT
    {
        return 317;
    }

    static SBE_CONSTEXPR char userDefinedInstrumentNullValue() SBE_NOEXCEPT
    {
        return static_cast<char>(0);
    }

    static SBE_CONSTEXPR char userDefinedInstrumentMinValue() SBE_NOEXCEPT
    {
        return static_cast<char>(32);
    }

    static SBE_CONSTEXPR char userDefinedInstrumentMaxValue() SBE_NOEXCEPT
    {
        return static_cast<char>(126);
    }

    static SBE_CONSTEXPR std::size_t userDefinedInstrumentEncodingLength() SBE_NOEXCEPT
    {
        return 1;
    }

    SBE_NODISCARD char userDefinedInstrument() const SBE_NOEXCEPT
    {
        char val;
        std::memcpy(&val, m_buffer + m_offset + 317, sizeof(char));
        return (val);
    }

    MDInstrumentDefinitionFixedIncome57 &userDefinedInstrument(const char value) SBE_NOEXCEPT
    {
        char val = (value);
        std::memcpy(m_buffer + m_offset + 317, &val, sizeof(char));
        return *this;
    }

    SBE_NODISCARD static const char *RiskSetMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "String";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t riskSetId() SBE_NOEXCEPT
    {
        return 37721;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t riskSetSinceVersion() SBE_NOEXCEPT
    {
        return 10;
    }

    SBE_NODISCARD bool riskSetInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= riskSetSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t riskSetEncodingOffset() SBE_NOEXCEPT
    {
        return 318;
    }

    static SBE_CONSTEXPR char riskSetNullValue() SBE_NOEXCEPT
    {
        return static_cast<char>(0);
    }

    static SBE_CONSTEXPR char riskSetMinValue() SBE_NOEXCEPT
    {
        return static_cast<char>(32);
    }

    static SBE_CONSTEXPR char riskSetMaxValue() SBE_NOEXCEPT
    {
        return static_cast<char>(126);
    }

    static SBE_CONSTEXPR std::size_t riskSetEncodingLength() SBE_NOEXCEPT
    {
        return 6;
    }

    static SBE_CONSTEXPR std::uint64_t riskSetLength() SBE_NOEXCEPT
    {
        return 6;
    }

    SBE_NODISCARD const char *riskSet() const SBE_NOEXCEPT
    {
        if (m_actingVersion < 10)
        {
            return nullptr;
        }

        return m_buffer + m_offset + 318;
    }

    SBE_NODISCARD char *riskSet() SBE_NOEXCEPT
    {
        if (m_actingVersion < 10)
        {
            return nullptr;
        }

        return m_buffer + m_offset + 318;
    }

    SBE_NODISCARD char riskSet(const std::uint64_t index) const
    {
        if (index >= 6)
        {
            throw std::runtime_error("index out of range for riskSet [E104]");
        }

        if (m_actingVersion < 10)
        {
            return static_cast<char>(0);
        }

        char val;
        std::memcpy(&val, m_buffer + m_offset + 318 + (index * 1), sizeof(char));
        return (val);
    }

    MDInstrumentDefinitionFixedIncome57 &riskSet(const std::uint64_t index, const char value)
    {
        if (index >= 6)
        {
            throw std::runtime_error("index out of range for riskSet [E105]");
        }

        char val = (value);
        std::memcpy(m_buffer + m_offset + 318 + (index * 1), &val, sizeof(char));
        return *this;
    }

    std::uint64_t getRiskSet(char *const dst, const std::uint64_t length) const
    {
        if (length > 6)
        {
            throw std::runtime_error("length too large for getRiskSet [E106]");
        }

        if (m_actingVersion < 10)
        {
            return 0;
        }

        std::memcpy(dst, m_buffer + m_offset + 318, sizeof(char) * static_cast<std::size_t>(length));
        return length;
    }

    MDInstrumentDefinitionFixedIncome57 &putRiskSet(const char *const src) SBE_NOEXCEPT
    {
        std::memcpy(m_buffer + m_offset + 318, src, sizeof(char) * 6);
        return *this;
    }

    SBE_NODISCARD std::string getRiskSetAsString() const
    {
        const char *buffer = m_buffer + m_offset + 318;
        std::size_t length = 0;

        for (; length < 6 && *(buffer + length) != '\0'; ++length);
        std::string result(buffer, length);

        return result;
    }

    std::string getRiskSetAsJsonEscapedString()
    {
        if (m_actingVersion < 10)
        {
            return std::string("");
        }

        std::ostringstream oss;
        std::string s = getRiskSetAsString();

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
    SBE_NODISCARD std::string_view getRiskSetAsStringView() const SBE_NOEXCEPT
    {
        const char *buffer = m_buffer + m_offset + 318;
        std::size_t length = 0;

        for (; length < 6 && *(buffer + length) != '\0'; ++length);
        std::string_view result(buffer, length);

        return result;
    }
    #endif

    #if __cplusplus >= 201703L
    MDInstrumentDefinitionFixedIncome57 &putRiskSet(const std::string_view str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 6)
        {
            throw std::runtime_error("string too large for putRiskSet [E106]");
        }

        std::memcpy(m_buffer + m_offset + 318, str.data(), srcLength);
        for (std::size_t start = srcLength; start < 6; ++start)
        {
            m_buffer[m_offset + 318 + start] = 0;
        }

        return *this;
    }
    #else
    MDInstrumentDefinitionFixedIncome57 &putRiskSet(const std::string &str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 6)
        {
            throw std::runtime_error("string too large for putRiskSet [E106]");
        }

        std::memcpy(m_buffer + m_offset + 318, str.c_str(), srcLength);
        for (std::size_t start = srcLength; start < 6; ++start)
        {
            m_buffer[m_offset + 318 + start] = 0;
        }

        return *this;
    }
    #endif

    SBE_NODISCARD static const char *MarketSetMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "String";
            case MetaAttribute::PRESENCE: return "required";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t marketSetId() SBE_NOEXCEPT
    {
        return 37722;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t marketSetSinceVersion() SBE_NOEXCEPT
    {
        return 10;
    }

    SBE_NODISCARD bool marketSetInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= marketSetSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t marketSetEncodingOffset() SBE_NOEXCEPT
    {
        return 324;
    }

    static SBE_CONSTEXPR char marketSetNullValue() SBE_NOEXCEPT
    {
        return static_cast<char>(0);
    }

    static SBE_CONSTEXPR char marketSetMinValue() SBE_NOEXCEPT
    {
        return static_cast<char>(32);
    }

    static SBE_CONSTEXPR char marketSetMaxValue() SBE_NOEXCEPT
    {
        return static_cast<char>(126);
    }

    static SBE_CONSTEXPR std::size_t marketSetEncodingLength() SBE_NOEXCEPT
    {
        return 6;
    }

    static SBE_CONSTEXPR std::uint64_t marketSetLength() SBE_NOEXCEPT
    {
        return 6;
    }

    SBE_NODISCARD const char *marketSet() const SBE_NOEXCEPT
    {
        if (m_actingVersion < 10)
        {
            return nullptr;
        }

        return m_buffer + m_offset + 324;
    }

    SBE_NODISCARD char *marketSet() SBE_NOEXCEPT
    {
        if (m_actingVersion < 10)
        {
            return nullptr;
        }

        return m_buffer + m_offset + 324;
    }

    SBE_NODISCARD char marketSet(const std::uint64_t index) const
    {
        if (index >= 6)
        {
            throw std::runtime_error("index out of range for marketSet [E104]");
        }

        if (m_actingVersion < 10)
        {
            return static_cast<char>(0);
        }

        char val;
        std::memcpy(&val, m_buffer + m_offset + 324 + (index * 1), sizeof(char));
        return (val);
    }

    MDInstrumentDefinitionFixedIncome57 &marketSet(const std::uint64_t index, const char value)
    {
        if (index >= 6)
        {
            throw std::runtime_error("index out of range for marketSet [E105]");
        }

        char val = (value);
        std::memcpy(m_buffer + m_offset + 324 + (index * 1), &val, sizeof(char));
        return *this;
    }

    std::uint64_t getMarketSet(char *const dst, const std::uint64_t length) const
    {
        if (length > 6)
        {
            throw std::runtime_error("length too large for getMarketSet [E106]");
        }

        if (m_actingVersion < 10)
        {
            return 0;
        }

        std::memcpy(dst, m_buffer + m_offset + 324, sizeof(char) * static_cast<std::size_t>(length));
        return length;
    }

    MDInstrumentDefinitionFixedIncome57 &putMarketSet(const char *const src) SBE_NOEXCEPT
    {
        std::memcpy(m_buffer + m_offset + 324, src, sizeof(char) * 6);
        return *this;
    }

    SBE_NODISCARD std::string getMarketSetAsString() const
    {
        const char *buffer = m_buffer + m_offset + 324;
        std::size_t length = 0;

        for (; length < 6 && *(buffer + length) != '\0'; ++length);
        std::string result(buffer, length);

        return result;
    }

    std::string getMarketSetAsJsonEscapedString()
    {
        if (m_actingVersion < 10)
        {
            return std::string("");
        }

        std::ostringstream oss;
        std::string s = getMarketSetAsString();

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
    SBE_NODISCARD std::string_view getMarketSetAsStringView() const SBE_NOEXCEPT
    {
        const char *buffer = m_buffer + m_offset + 324;
        std::size_t length = 0;

        for (; length < 6 && *(buffer + length) != '\0'; ++length);
        std::string_view result(buffer, length);

        return result;
    }
    #endif

    #if __cplusplus >= 201703L
    MDInstrumentDefinitionFixedIncome57 &putMarketSet(const std::string_view str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 6)
        {
            throw std::runtime_error("string too large for putMarketSet [E106]");
        }

        std::memcpy(m_buffer + m_offset + 324, str.data(), srcLength);
        for (std::size_t start = srcLength; start < 6; ++start)
        {
            m_buffer[m_offset + 324 + start] = 0;
        }

        return *this;
    }
    #else
    MDInstrumentDefinitionFixedIncome57 &putMarketSet(const std::string &str)
    {
        const std::size_t srcLength = str.length();
        if (srcLength > 6)
        {
            throw std::runtime_error("string too large for putMarketSet [E106]");
        }

        std::memcpy(m_buffer + m_offset + 324, str.c_str(), srcLength);
        for (std::size_t start = srcLength; start < 6; ++start)
        {
            m_buffer[m_offset + 324 + start] = 0;
        }

        return *this;
    }
    #endif

    SBE_NODISCARD static const char *InstrumentGUIDMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
    {
        switch (metaAttribute)
        {
            case MetaAttribute::SEMANTIC_TYPE: return "int";
            case MetaAttribute::PRESENCE: return "optional";
            default: return "";
        }
    }

    static SBE_CONSTEXPR std::uint16_t instrumentGUIDId() SBE_NOEXCEPT
    {
        return 37513;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t instrumentGUIDSinceVersion() SBE_NOEXCEPT
    {
        return 7;
    }

    SBE_NODISCARD bool instrumentGUIDInActingVersion() SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= instrumentGUIDSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::size_t instrumentGUIDEncodingOffset() SBE_NOEXCEPT
    {
        return 330;
    }

    static SBE_CONSTEXPR std::uint64_t instrumentGUIDNullValue() SBE_NOEXCEPT
    {
        return UINT64_C(0xffffffffffffffff);
    }

    static SBE_CONSTEXPR std::uint64_t instrumentGUIDMinValue() SBE_NOEXCEPT
    {
        return UINT64_C(0x0);
    }

    static SBE_CONSTEXPR std::uint64_t instrumentGUIDMaxValue() SBE_NOEXCEPT
    {
        return UINT64_C(0xfffffffffffffffe);
    }

    static SBE_CONSTEXPR std::size_t instrumentGUIDEncodingLength() SBE_NOEXCEPT
    {
        return 8;
    }

    SBE_NODISCARD std::uint64_t instrumentGUID() const SBE_NOEXCEPT
    {
        if (m_actingVersion < 7)
        {
            return UINT64_C(0xffffffffffffffff);
        }

        std::uint64_t val;
        std::memcpy(&val, m_buffer + m_offset + 330, sizeof(std::uint64_t));
        return SBE_LITTLE_ENDIAN_ENCODE_64(val);
    }

    MDInstrumentDefinitionFixedIncome57 &instrumentGUID(const std::uint64_t value) SBE_NOEXCEPT
    {
        std::uint64_t val = SBE_LITTLE_ENDIAN_ENCODE_64(value);
        std::memcpy(m_buffer + m_offset + 330, &val, sizeof(std::uint64_t));
        return *this;
    }

    class NoEvents
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
            dimensions.blockLength(static_cast<std::uint16_t>(9));
            dimensions.numInGroup(static_cast<std::uint8_t>(count));
            m_index = 0;
            m_count = count;
            m_blockLength = 9;
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
            return 9;
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

        inline NoEvents &next()
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

        SBE_NODISCARD static const char *EventTypeMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
        {
            switch (metaAttribute)
            {
                case MetaAttribute::SEMANTIC_TYPE: return "int";
                case MetaAttribute::PRESENCE: return "required";
                default: return "";
            }
        }

        static SBE_CONSTEXPR std::uint16_t eventTypeId() SBE_NOEXCEPT
        {
            return 865;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t eventTypeSinceVersion() SBE_NOEXCEPT
        {
            return 0;
        }

        SBE_NODISCARD bool eventTypeInActingVersion() SBE_NOEXCEPT
        {
    #if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wtautological-compare"
    #endif
            return m_actingVersion >= eventTypeSinceVersion();
    #if defined(__clang__)
    #pragma clang diagnostic pop
    #endif
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::size_t eventTypeEncodingOffset() SBE_NOEXCEPT
        {
            return 0;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::size_t eventTypeEncodingLength() SBE_NOEXCEPT
        {
            return 1;
        }

        SBE_NODISCARD std::uint8_t eventTypeRaw() const SBE_NOEXCEPT
        {
            std::uint8_t val;
            std::memcpy(&val, m_buffer + m_offset + 0, sizeof(std::uint8_t));
            return (val);
        }

        SBE_NODISCARD EventType::Value eventType() const
        {
            std::uint8_t val;
            std::memcpy(&val, m_buffer + m_offset + 0, sizeof(std::uint8_t));
            return EventType::get((val));
        }

        NoEvents &eventType(const EventType::Value value) SBE_NOEXCEPT
        {
            std::uint8_t val = (value);
            std::memcpy(m_buffer + m_offset + 0, &val, sizeof(std::uint8_t));
            return *this;
        }

        SBE_NODISCARD static const char *EventTimeMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
        {
            switch (metaAttribute)
            {
                case MetaAttribute::SEMANTIC_TYPE: return "UTCTimestamp";
                case MetaAttribute::PRESENCE: return "required";
                default: return "";
            }
        }

        static SBE_CONSTEXPR std::uint16_t eventTimeId() SBE_NOEXCEPT
        {
            return 1145;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t eventTimeSinceVersion() SBE_NOEXCEPT
        {
            return 0;
        }

        SBE_NODISCARD bool eventTimeInActingVersion() SBE_NOEXCEPT
        {
    #if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wtautological-compare"
    #endif
            return m_actingVersion >= eventTimeSinceVersion();
    #if defined(__clang__)
    #pragma clang diagnostic pop
    #endif
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::size_t eventTimeEncodingOffset() SBE_NOEXCEPT
        {
            return 1;
        }

        static SBE_CONSTEXPR std::uint64_t eventTimeNullValue() SBE_NOEXCEPT
        {
            return SBE_NULLVALUE_UINT64;
        }

        static SBE_CONSTEXPR std::uint64_t eventTimeMinValue() SBE_NOEXCEPT
        {
            return UINT64_C(0x0);
        }

        static SBE_CONSTEXPR std::uint64_t eventTimeMaxValue() SBE_NOEXCEPT
        {
            return UINT64_C(0xfffffffffffffffe);
        }

        static SBE_CONSTEXPR std::size_t eventTimeEncodingLength() SBE_NOEXCEPT
        {
            return 8;
        }

        SBE_NODISCARD std::uint64_t eventTime() const SBE_NOEXCEPT
        {
            std::uint64_t val;
            std::memcpy(&val, m_buffer + m_offset + 1, sizeof(std::uint64_t));
            return SBE_LITTLE_ENDIAN_ENCODE_64(val);
        }

        NoEvents &eventTime(const std::uint64_t value) SBE_NOEXCEPT
        {
            std::uint64_t val = SBE_LITTLE_ENDIAN_ENCODE_64(value);
            std::memcpy(m_buffer + m_offset + 1, &val, sizeof(std::uint64_t));
            return *this;
        }

        template<typename CharT, typename Traits>
        friend std::basic_ostream<CharT, Traits> & operator << (
            std::basic_ostream<CharT, Traits> &builder, NoEvents writer)
        {
            builder << '{';
            builder << R"("EventType": )";
            builder << '"' << writer.eventType() << '"';

            builder << ", ";
            builder << R"("EventTime": )";
            builder << +writer.eventTime();

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
    NoEvents m_noEvents;

public:
    SBE_NODISCARD static SBE_CONSTEXPR std::uint16_t NoEventsId() SBE_NOEXCEPT
    {
        return 864;
    }

    SBE_NODISCARD inline NoEvents &noEvents()
    {
        m_noEvents.wrapForDecode(m_buffer, sbePositionPtr(), m_actingVersion, m_bufferLength);
        return m_noEvents;
    }

    NoEvents &noEventsCount(const std::uint8_t count)
    {
        m_noEvents.wrapForEncode(m_buffer, count, sbePositionPtr(), m_actingVersion, m_bufferLength);
        return m_noEvents;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t noEventsSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool noEventsInActingVersion() const SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= noEventsSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    class NoMDFeedTypes
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
            dimensions.blockLength(static_cast<std::uint16_t>(4));
            dimensions.numInGroup(static_cast<std::uint8_t>(count));
            m_index = 0;
            m_count = count;
            m_blockLength = 4;
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
            return 4;
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

        inline NoMDFeedTypes &next()
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

        SBE_NODISCARD static const char *MDFeedTypeMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
        {
            switch (metaAttribute)
            {
                case MetaAttribute::SEMANTIC_TYPE: return "String";
                case MetaAttribute::PRESENCE: return "required";
                default: return "";
            }
        }

        static SBE_CONSTEXPR std::uint16_t mDFeedTypeId() SBE_NOEXCEPT
        {
            return 1022;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t mDFeedTypeSinceVersion() SBE_NOEXCEPT
        {
            return 0;
        }

        SBE_NODISCARD bool mDFeedTypeInActingVersion() SBE_NOEXCEPT
        {
    #if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wtautological-compare"
    #endif
            return m_actingVersion >= mDFeedTypeSinceVersion();
    #if defined(__clang__)
    #pragma clang diagnostic pop
    #endif
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::size_t mDFeedTypeEncodingOffset() SBE_NOEXCEPT
        {
            return 0;
        }

        static SBE_CONSTEXPR char mDFeedTypeNullValue() SBE_NOEXCEPT
        {
            return static_cast<char>(0);
        }

        static SBE_CONSTEXPR char mDFeedTypeMinValue() SBE_NOEXCEPT
        {
            return static_cast<char>(32);
        }

        static SBE_CONSTEXPR char mDFeedTypeMaxValue() SBE_NOEXCEPT
        {
            return static_cast<char>(126);
        }

        static SBE_CONSTEXPR std::size_t mDFeedTypeEncodingLength() SBE_NOEXCEPT
        {
            return 3;
        }

        static SBE_CONSTEXPR std::uint64_t mDFeedTypeLength() SBE_NOEXCEPT
        {
            return 3;
        }

        SBE_NODISCARD const char *mDFeedType() const SBE_NOEXCEPT
        {
            return m_buffer + m_offset + 0;
        }

        SBE_NODISCARD char *mDFeedType() SBE_NOEXCEPT
        {
            return m_buffer + m_offset + 0;
        }

        SBE_NODISCARD char mDFeedType(const std::uint64_t index) const
        {
            if (index >= 3)
            {
                throw std::runtime_error("index out of range for mDFeedType [E104]");
            }

            char val;
            std::memcpy(&val, m_buffer + m_offset + 0 + (index * 1), sizeof(char));
            return (val);
        }

        NoMDFeedTypes &mDFeedType(const std::uint64_t index, const char value)
        {
            if (index >= 3)
            {
                throw std::runtime_error("index out of range for mDFeedType [E105]");
            }

            char val = (value);
            std::memcpy(m_buffer + m_offset + 0 + (index * 1), &val, sizeof(char));
            return *this;
        }

        std::uint64_t getMDFeedType(char *const dst, const std::uint64_t length) const
        {
            if (length > 3)
            {
                throw std::runtime_error("length too large for getMDFeedType [E106]");
            }

            std::memcpy(dst, m_buffer + m_offset + 0, sizeof(char) * static_cast<std::size_t>(length));
            return length;
        }

        NoMDFeedTypes &putMDFeedType(const char *const src) SBE_NOEXCEPT
        {
            std::memcpy(m_buffer + m_offset + 0, src, sizeof(char) * 3);
            return *this;
        }

        NoMDFeedTypes &putMDFeedType(
            const char value0,
            const char value1,
            const char value2) SBE_NOEXCEPT
        {
            char val0 = (value0);
            std::memcpy(m_buffer + m_offset + 0, &val0, sizeof(char));
            char val1 = (value1);
            std::memcpy(m_buffer + m_offset + 1, &val1, sizeof(char));
            char val2 = (value2);
            std::memcpy(m_buffer + m_offset + 2, &val2, sizeof(char));

            return *this;
        }

        SBE_NODISCARD std::string getMDFeedTypeAsString() const
        {
            const char *buffer = m_buffer + m_offset + 0;
            std::size_t length = 0;

            for (; length < 3 && *(buffer + length) != '\0'; ++length);
            std::string result(buffer, length);

            return result;
        }

        std::string getMDFeedTypeAsJsonEscapedString()
        {
            std::ostringstream oss;
            std::string s = getMDFeedTypeAsString();

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
        SBE_NODISCARD std::string_view getMDFeedTypeAsStringView() const SBE_NOEXCEPT
        {
            const char *buffer = m_buffer + m_offset + 0;
            std::size_t length = 0;

            for (; length < 3 && *(buffer + length) != '\0'; ++length);
            std::string_view result(buffer, length);

            return result;
        }
        #endif

        #if __cplusplus >= 201703L
        NoMDFeedTypes &putMDFeedType(const std::string_view str)
        {
            const std::size_t srcLength = str.length();
            if (srcLength > 3)
            {
                throw std::runtime_error("string too large for putMDFeedType [E106]");
            }

            std::memcpy(m_buffer + m_offset + 0, str.data(), srcLength);
            for (std::size_t start = srcLength; start < 3; ++start)
            {
                m_buffer[m_offset + 0 + start] = 0;
            }

            return *this;
        }
        #else
        NoMDFeedTypes &putMDFeedType(const std::string &str)
        {
            const std::size_t srcLength = str.length();
            if (srcLength > 3)
            {
                throw std::runtime_error("string too large for putMDFeedType [E106]");
            }

            std::memcpy(m_buffer + m_offset + 0, str.c_str(), srcLength);
            for (std::size_t start = srcLength; start < 3; ++start)
            {
                m_buffer[m_offset + 0 + start] = 0;
            }

            return *this;
        }
        #endif

        SBE_NODISCARD static const char *MarketDepthMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
        {
            switch (metaAttribute)
            {
                case MetaAttribute::SEMANTIC_TYPE: return "int";
                case MetaAttribute::PRESENCE: return "required";
                default: return "";
            }
        }

        static SBE_CONSTEXPR std::uint16_t marketDepthId() SBE_NOEXCEPT
        {
            return 264;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t marketDepthSinceVersion() SBE_NOEXCEPT
        {
            return 0;
        }

        SBE_NODISCARD bool marketDepthInActingVersion() SBE_NOEXCEPT
        {
    #if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wtautological-compare"
    #endif
            return m_actingVersion >= marketDepthSinceVersion();
    #if defined(__clang__)
    #pragma clang diagnostic pop
    #endif
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::size_t marketDepthEncodingOffset() SBE_NOEXCEPT
        {
            return 3;
        }

        static SBE_CONSTEXPR std::int8_t marketDepthNullValue() SBE_NOEXCEPT
        {
            return SBE_NULLVALUE_INT8;
        }

        static SBE_CONSTEXPR std::int8_t marketDepthMinValue() SBE_NOEXCEPT
        {
            return static_cast<std::int8_t>(-127);
        }

        static SBE_CONSTEXPR std::int8_t marketDepthMaxValue() SBE_NOEXCEPT
        {
            return static_cast<std::int8_t>(127);
        }

        static SBE_CONSTEXPR std::size_t marketDepthEncodingLength() SBE_NOEXCEPT
        {
            return 1;
        }

        SBE_NODISCARD std::int8_t marketDepth() const SBE_NOEXCEPT
        {
            std::int8_t val;
            std::memcpy(&val, m_buffer + m_offset + 3, sizeof(std::int8_t));
            return (val);
        }

        NoMDFeedTypes &marketDepth(const std::int8_t value) SBE_NOEXCEPT
        {
            std::int8_t val = (value);
            std::memcpy(m_buffer + m_offset + 3, &val, sizeof(std::int8_t));
            return *this;
        }

        template<typename CharT, typename Traits>
        friend std::basic_ostream<CharT, Traits> & operator << (
            std::basic_ostream<CharT, Traits> &builder, NoMDFeedTypes writer)
        {
            builder << '{';
            builder << R"("MDFeedType": )";
            builder << '"' <<
                writer.getMDFeedTypeAsJsonEscapedString().c_str() << '"';

            builder << ", ";
            builder << R"("MarketDepth": )";
            builder << +writer.marketDepth();

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
    NoMDFeedTypes m_noMDFeedTypes;

public:
    SBE_NODISCARD static SBE_CONSTEXPR std::uint16_t NoMDFeedTypesId() SBE_NOEXCEPT
    {
        return 1141;
    }

    SBE_NODISCARD inline NoMDFeedTypes &noMDFeedTypes()
    {
        m_noMDFeedTypes.wrapForDecode(m_buffer, sbePositionPtr(), m_actingVersion, m_bufferLength);
        return m_noMDFeedTypes;
    }

    NoMDFeedTypes &noMDFeedTypesCount(const std::uint8_t count)
    {
        m_noMDFeedTypes.wrapForEncode(m_buffer, count, sbePositionPtr(), m_actingVersion, m_bufferLength);
        return m_noMDFeedTypes;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t noMDFeedTypesSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool noMDFeedTypesInActingVersion() const SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= noMDFeedTypesSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    class NoInstAttrib
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
            dimensions.blockLength(static_cast<std::uint16_t>(4));
            dimensions.numInGroup(static_cast<std::uint8_t>(count));
            m_index = 0;
            m_count = count;
            m_blockLength = 4;
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
            return 4;
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

        inline NoInstAttrib &next()
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

        SBE_NODISCARD static const char *InstAttribTypeMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
        {
            switch (metaAttribute)
            {
                case MetaAttribute::SEMANTIC_TYPE: return "int";
                case MetaAttribute::PRESENCE: return "constant";
                default: return "";
            }
        }

        static SBE_CONSTEXPR std::uint16_t instAttribTypeId() SBE_NOEXCEPT
        {
            return 871;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t instAttribTypeSinceVersion() SBE_NOEXCEPT
        {
            return 0;
        }

        SBE_NODISCARD bool instAttribTypeInActingVersion() SBE_NOEXCEPT
        {
    #if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wtautological-compare"
    #endif
            return m_actingVersion >= instAttribTypeSinceVersion();
    #if defined(__clang__)
    #pragma clang diagnostic pop
    #endif
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::size_t instAttribTypeEncodingOffset() SBE_NOEXCEPT
        {
            return 0;
        }

        static SBE_CONSTEXPR std::int8_t instAttribTypeNullValue() SBE_NOEXCEPT
        {
            return SBE_NULLVALUE_INT8;
        }

        static SBE_CONSTEXPR std::int8_t instAttribTypeMinValue() SBE_NOEXCEPT
        {
            return static_cast<std::int8_t>(-127);
        }

        static SBE_CONSTEXPR std::int8_t instAttribTypeMaxValue() SBE_NOEXCEPT
        {
            return static_cast<std::int8_t>(127);
        }

        static SBE_CONSTEXPR std::size_t instAttribTypeEncodingLength() SBE_NOEXCEPT
        {
            return 0;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::int8_t instAttribType() SBE_NOEXCEPT
        {
            return static_cast<std::int8_t>(24);
        }

        SBE_NODISCARD static const char *InstAttribValueMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
        {
            switch (metaAttribute)
            {
                case MetaAttribute::SEMANTIC_TYPE: return "MultipleCharValue";
                case MetaAttribute::PRESENCE: return "required";
                default: return "";
            }
        }

        static SBE_CONSTEXPR std::uint16_t instAttribValueId() SBE_NOEXCEPT
        {
            return 872;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t instAttribValueSinceVersion() SBE_NOEXCEPT
        {
            return 0;
        }

        SBE_NODISCARD bool instAttribValueInActingVersion() SBE_NOEXCEPT
        {
    #if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wtautological-compare"
    #endif
            return m_actingVersion >= instAttribValueSinceVersion();
    #if defined(__clang__)
    #pragma clang diagnostic pop
    #endif
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::size_t instAttribValueEncodingOffset() SBE_NOEXCEPT
        {
            return 0;
        }

    private:
        InstAttribValue m_instAttribValue;

    public:
        SBE_NODISCARD InstAttribValue &instAttribValue()
        {
            m_instAttribValue.wrap(m_buffer, m_offset + 0, m_actingVersion, m_bufferLength);
            return m_instAttribValue;
        }

        static SBE_CONSTEXPR std::size_t instAttribValueEncodingLength() SBE_NOEXCEPT
        {
            return 4;
        }

        template<typename CharT, typename Traits>
        friend std::basic_ostream<CharT, Traits> & operator << (
            std::basic_ostream<CharT, Traits> &builder, NoInstAttrib writer)
        {
            builder << '{';
            builder << R"("InstAttribValue": )";
            builder << writer.instAttribValue();

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
    NoInstAttrib m_noInstAttrib;

public:
    SBE_NODISCARD static SBE_CONSTEXPR std::uint16_t NoInstAttribId() SBE_NOEXCEPT
    {
        return 870;
    }

    SBE_NODISCARD inline NoInstAttrib &noInstAttrib()
    {
        m_noInstAttrib.wrapForDecode(m_buffer, sbePositionPtr(), m_actingVersion, m_bufferLength);
        return m_noInstAttrib;
    }

    NoInstAttrib &noInstAttribCount(const std::uint8_t count)
    {
        m_noInstAttrib.wrapForEncode(m_buffer, count, sbePositionPtr(), m_actingVersion, m_bufferLength);
        return m_noInstAttrib;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t noInstAttribSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool noInstAttribInActingVersion() const SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= noInstAttribSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

    class NoLotTypeRules
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
            dimensions.blockLength(static_cast<std::uint16_t>(5));
            dimensions.numInGroup(static_cast<std::uint8_t>(count));
            m_index = 0;
            m_count = count;
            m_blockLength = 5;
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
            return 5;
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

        inline NoLotTypeRules &next()
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

        SBE_NODISCARD static const char *LotTypeMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
        {
            switch (metaAttribute)
            {
                case MetaAttribute::SEMANTIC_TYPE: return "int";
                case MetaAttribute::PRESENCE: return "required";
                default: return "";
            }
        }

        static SBE_CONSTEXPR std::uint16_t lotTypeId() SBE_NOEXCEPT
        {
            return 1093;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t lotTypeSinceVersion() SBE_NOEXCEPT
        {
            return 0;
        }

        SBE_NODISCARD bool lotTypeInActingVersion() SBE_NOEXCEPT
        {
    #if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wtautological-compare"
    #endif
            return m_actingVersion >= lotTypeSinceVersion();
    #if defined(__clang__)
    #pragma clang diagnostic pop
    #endif
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::size_t lotTypeEncodingOffset() SBE_NOEXCEPT
        {
            return 0;
        }

        static SBE_CONSTEXPR std::int8_t lotTypeNullValue() SBE_NOEXCEPT
        {
            return SBE_NULLVALUE_INT8;
        }

        static SBE_CONSTEXPR std::int8_t lotTypeMinValue() SBE_NOEXCEPT
        {
            return static_cast<std::int8_t>(-127);
        }

        static SBE_CONSTEXPR std::int8_t lotTypeMaxValue() SBE_NOEXCEPT
        {
            return static_cast<std::int8_t>(127);
        }

        static SBE_CONSTEXPR std::size_t lotTypeEncodingLength() SBE_NOEXCEPT
        {
            return 1;
        }

        SBE_NODISCARD std::int8_t lotType() const SBE_NOEXCEPT
        {
            std::int8_t val;
            std::memcpy(&val, m_buffer + m_offset + 0, sizeof(std::int8_t));
            return (val);
        }

        NoLotTypeRules &lotType(const std::int8_t value) SBE_NOEXCEPT
        {
            std::int8_t val = (value);
            std::memcpy(m_buffer + m_offset + 0, &val, sizeof(std::int8_t));
            return *this;
        }

        SBE_NODISCARD static const char *MinLotSizeMetaAttribute(const MetaAttribute metaAttribute) SBE_NOEXCEPT
        {
            switch (metaAttribute)
            {
                case MetaAttribute::SEMANTIC_TYPE: return "Qty";
                case MetaAttribute::PRESENCE: return "required";
                default: return "";
            }
        }

        static SBE_CONSTEXPR std::uint16_t minLotSizeId() SBE_NOEXCEPT
        {
            return 1231;
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t minLotSizeSinceVersion() SBE_NOEXCEPT
        {
            return 0;
        }

        SBE_NODISCARD bool minLotSizeInActingVersion() SBE_NOEXCEPT
        {
    #if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wtautological-compare"
    #endif
            return m_actingVersion >= minLotSizeSinceVersion();
    #if defined(__clang__)
    #pragma clang diagnostic pop
    #endif
        }

        SBE_NODISCARD static SBE_CONSTEXPR std::size_t minLotSizeEncodingOffset() SBE_NOEXCEPT
        {
            return 1;
        }

private:
        DecimalQty m_minLotSize;

public:
        SBE_NODISCARD DecimalQty &minLotSize()
        {
            m_minLotSize.wrap(m_buffer, m_offset + 1, m_actingVersion, m_bufferLength);
            return m_minLotSize;
        }

        template<typename CharT, typename Traits>
        friend std::basic_ostream<CharT, Traits> & operator << (
            std::basic_ostream<CharT, Traits> &builder, NoLotTypeRules writer)
        {
            builder << '{';
            builder << R"("LotType": )";
            builder << +writer.lotType();

            builder << ", ";
            builder << R"("MinLotSize": )";
            builder << writer.minLotSize();

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
    NoLotTypeRules m_noLotTypeRules;

public:
    SBE_NODISCARD static SBE_CONSTEXPR std::uint16_t NoLotTypeRulesId() SBE_NOEXCEPT
    {
        return 1234;
    }

    SBE_NODISCARD inline NoLotTypeRules &noLotTypeRules()
    {
        m_noLotTypeRules.wrapForDecode(m_buffer, sbePositionPtr(), m_actingVersion, m_bufferLength);
        return m_noLotTypeRules;
    }

    NoLotTypeRules &noLotTypeRulesCount(const std::uint8_t count)
    {
        m_noLotTypeRules.wrapForEncode(m_buffer, count, sbePositionPtr(), m_actingVersion, m_bufferLength);
        return m_noLotTypeRules;
    }

    SBE_NODISCARD static SBE_CONSTEXPR std::uint64_t noLotTypeRulesSinceVersion() SBE_NOEXCEPT
    {
        return 0;
    }

    SBE_NODISCARD bool noLotTypeRulesInActingVersion() const SBE_NOEXCEPT
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
        return m_actingVersion >= noLotTypeRulesSinceVersion();
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }

template<typename CharT, typename Traits>
friend std::basic_ostream<CharT, Traits> & operator << (
    std::basic_ostream<CharT, Traits> &builder, MDInstrumentDefinitionFixedIncome57 _writer)
{
    MDInstrumentDefinitionFixedIncome57 writer(
        _writer.m_buffer,
        _writer.m_offset,
        _writer.m_bufferLength,
        _writer.m_actingBlockLength,
        _writer.m_actingVersion);

    builder << '{';
    builder << R"("Name": "MDInstrumentDefinitionFixedIncome57", )";
    builder << R"("sbeTemplateId": )";
    builder << writer.sbeTemplateId();
    builder << ", ";

    builder << R"("MatchEventIndicator": )";
    builder << writer.matchEventIndicator();

    builder << ", ";
    builder << R"("TotNumReports": )";
    builder << +writer.totNumReports();

    builder << ", ";
    builder << R"("SecurityUpdateAction": )";
    builder << '"' << writer.securityUpdateAction() << '"';

    builder << ", ";
    builder << R"("LastUpdateTime": )";
    builder << +writer.lastUpdateTime();

    builder << ", ";
    builder << R"("MDSecurityTradingStatus": )";
    builder << '"' << writer.mDSecurityTradingStatus() << '"';

    builder << ", ";
    builder << R"("ApplID": )";
    builder << +writer.applID();

    builder << ", ";
    builder << R"("MarketSegmentID": )";
    builder << +writer.marketSegmentID();

    builder << ", ";
    builder << R"("UnderlyingProduct": )";
    builder << +writer.underlyingProduct();

    builder << ", ";
    builder << R"("SecurityExchange": )";
    builder << '"' <<
        writer.getSecurityExchangeAsJsonEscapedString().c_str() << '"';

    builder << ", ";
    builder << R"("SecurityGroup": )";
    builder << '"' <<
        writer.getSecurityGroupAsJsonEscapedString().c_str() << '"';

    builder << ", ";
    builder << R"("Asset": )";
    builder << '"' <<
        writer.getAssetAsJsonEscapedString().c_str() << '"';

    builder << ", ";
    builder << R"("Symbol": )";
    builder << '"' <<
        writer.getSymbolAsJsonEscapedString().c_str() << '"';

    builder << ", ";
    builder << R"("SecurityID": )";
    builder << +writer.securityID();

    builder << ", ";
    builder << R"("SecurityType": )";
    builder << '"' <<
        writer.getSecurityTypeAsJsonEscapedString().c_str() << '"';

    builder << ", ";
    builder << R"("CFICode": )";
    builder << '"' <<
        writer.getCFICodeAsJsonEscapedString().c_str() << '"';

    builder << ", ";
    builder << R"("Currency": )";
    builder << '"' <<
        writer.getCurrencyAsJsonEscapedString().c_str() << '"';

    builder << ", ";
    builder << R"("SettlCurrency": )";
    builder << '"' <<
        writer.getSettlCurrencyAsJsonEscapedString().c_str() << '"';

    builder << ", ";
    builder << R"("MatchAlgorithm": )";
    if (std::isprint(writer.matchAlgorithm()))
    {
        builder << '"' << (char)writer.matchAlgorithm() << '"';
    }
    else
    {
        builder << (int)writer.matchAlgorithm();
    }

    builder << ", ";
    builder << R"("MinTradeVol": )";
    builder << +writer.minTradeVol();

    builder << ", ";
    builder << R"("MaxTradeVol": )";
    builder << +writer.maxTradeVol();

    builder << ", ";
    builder << R"("MinPriceIncrement": )";
    if (writer.minPriceIncrementInActingVersion())
    {
        builder << writer.minPriceIncrement();
    }
    else
    {
        builder << "{}";
    }

    builder << ", ";
    builder << R"("DisplayFactor": )";
    if (writer.displayFactorInActingVersion())
    {
        builder << writer.displayFactor();
    }
    else
    {
        builder << "{}";
    }

    builder << ", ";
    builder << R"("MainFraction": )";
    builder << +writer.mainFraction();

    builder << ", ";
    builder << R"("SubFraction": )";
    builder << +writer.subFraction();

    builder << ", ";
    builder << R"("PriceDisplayFormat": )";
    builder << +writer.priceDisplayFormat();

    builder << ", ";
    builder << R"("UnitOfMeasure": )";
    builder << '"' <<
        writer.getUnitOfMeasureAsJsonEscapedString().c_str() << '"';

    builder << ", ";
    builder << R"("UnitOfMeasureQty": )";
    if (writer.unitOfMeasureQtyInActingVersion())
    {
        builder << writer.unitOfMeasureQty();
    }
    else
    {
        builder << "{}";
    }

    builder << ", ";
    builder << R"("TradingReferencePrice": )";
    if (writer.tradingReferencePriceInActingVersion())
    {
        builder << writer.tradingReferencePrice();
    }
    else
    {
        builder << "{}";
    }

    builder << ", ";
    builder << R"("TradingReferenceDate": )";
    builder << +writer.tradingReferenceDate();

    builder << ", ";
    builder << R"("HighLimitPrice": )";
    if (writer.highLimitPriceInActingVersion())
    {
        builder << writer.highLimitPrice();
    }
    else
    {
        builder << "{}";
    }

    builder << ", ";
    builder << R"("LowLimitPrice": )";
    if (writer.lowLimitPriceInActingVersion())
    {
        builder << writer.lowLimitPrice();
    }
    else
    {
        builder << "{}";
    }

    builder << ", ";
    builder << R"("MaxPriceVariation": )";
    if (writer.maxPriceVariationInActingVersion())
    {
        builder << writer.maxPriceVariation();
    }
    else
    {
        builder << "{}";
    }

    builder << ", ";
    builder << R"("MinPriceIncrementAmount": )";
    if (writer.minPriceIncrementAmountInActingVersion())
    {
        builder << writer.minPriceIncrementAmount();
    }
    else
    {
        builder << "{}";
    }

    builder << ", ";
    builder << R"("IssueDate": )";
    builder << +writer.issueDate();

    builder << ", ";
    builder << R"("DatedDate": )";
    builder << +writer.datedDate();

    builder << ", ";
    builder << R"("MaturityDate": )";
    builder << +writer.maturityDate();

    builder << ", ";
    builder << R"("CouponRate": )";
    if (writer.couponRateInActingVersion())
    {
        builder << writer.couponRate();
    }
    else
    {
        builder << "{}";
    }

    builder << ", ";
    builder << R"("ParValue": )";
    if (writer.parValueInActingVersion())
    {
        builder << writer.parValue();
    }
    else
    {
        builder << "{}";
    }

    builder << ", ";
    builder << R"("CouponFrequencyUnit": )";
    builder << '"' <<
        writer.getCouponFrequencyUnitAsJsonEscapedString().c_str() << '"';

    builder << ", ";
    builder << R"("CouponFrequencyPeriod": )";
    builder << +writer.couponFrequencyPeriod();

    builder << ", ";
    builder << R"("CouponDayCount": )";
    builder << '"' <<
        writer.getCouponDayCountAsJsonEscapedString().c_str() << '"';

    builder << ", ";
    builder << R"("CountryOfIssue": )";
    builder << '"' <<
        writer.getCountryOfIssueAsJsonEscapedString().c_str() << '"';

    builder << ", ";
    builder << R"("Issuer": )";
    builder << '"' <<
        writer.getIssuerAsJsonEscapedString().c_str() << '"';

    builder << ", ";
    builder << R"("FinancialInstrumentFullName": )";
    builder << '"' <<
        writer.getFinancialInstrumentFullNameAsJsonEscapedString().c_str() << '"';

    builder << ", ";
    builder << R"("SecurityAltID": )";
    builder << '"' <<
        writer.getSecurityAltIDAsJsonEscapedString().c_str() << '"';

    builder << ", ";
    builder << R"("SecurityAltIDSource": )";
    builder << '"' << writer.securityAltIDSource() << '"';

    builder << ", ";
    builder << R"("PriceQuoteMethod": )";
    builder << '"' <<
        writer.getPriceQuoteMethodAsJsonEscapedString().c_str() << '"';

    builder << ", ";
    builder << R"("PartyRoleClearingOrg": )";
    builder << '"' <<
        writer.getPartyRoleClearingOrgAsJsonEscapedString().c_str() << '"';

    builder << ", ";
    builder << R"("UserDefinedInstrument": )";
    if (std::isprint(writer.userDefinedInstrument()))
    {
        builder << '"' << (char)writer.userDefinedInstrument() << '"';
    }
    else
    {
        builder << (int)writer.userDefinedInstrument();
    }

    builder << ", ";
    builder << R"("RiskSet": )";
    builder << '"' <<
        writer.getRiskSetAsJsonEscapedString().c_str() << '"';

    builder << ", ";
    builder << R"("MarketSet": )";
    builder << '"' <<
        writer.getMarketSetAsJsonEscapedString().c_str() << '"';

    builder << ", ";
    builder << R"("InstrumentGUID": )";
    builder << +writer.instrumentGUID();

    builder << ", ";
    {
        bool atLeastOne = false;
        builder << R"("NoEvents": [)";
        writer.noEvents().forEach(
            [&](NoEvents &noEvents)
            {
                if (atLeastOne)
                {
                    builder << ", ";
                }
                atLeastOne = true;
                builder << noEvents;
            });
        builder << ']';
    }

    builder << ", ";
    {
        bool atLeastOne = false;
        builder << R"("NoMDFeedTypes": [)";
        writer.noMDFeedTypes().forEach(
            [&](NoMDFeedTypes &noMDFeedTypes)
            {
                if (atLeastOne)
                {
                    builder << ", ";
                }
                atLeastOne = true;
                builder << noMDFeedTypes;
            });
        builder << ']';
    }

    builder << ", ";
    {
        bool atLeastOne = false;
        builder << R"("NoInstAttrib": [)";
        writer.noInstAttrib().forEach(
            [&](NoInstAttrib &noInstAttrib)
            {
                if (atLeastOne)
                {
                    builder << ", ";
                }
                atLeastOne = true;
                builder << noInstAttrib;
            });
        builder << ']';
    }

    builder << ", ";
    {
        bool atLeastOne = false;
        builder << R"("NoLotTypeRules": [)";
        writer.noLotTypeRules().forEach(
            [&](NoLotTypeRules &noLotTypeRules)
            {
                if (atLeastOne)
                {
                    builder << ", ";
                }
                atLeastOne = true;
                builder << noLotTypeRules;
            });
        builder << ']';
    }

    builder << '}';

    return builder;
}

void skip()
{
    noEvents().forEach([](NoEvents &e){ e.skip(); });
    noMDFeedTypes().forEach([](NoMDFeedTypes &e){ e.skip(); });
    noInstAttrib().forEach([](NoInstAttrib &e){ e.skip(); });
    noLotTypeRules().forEach([](NoLotTypeRules &e){ e.skip(); });
}

SBE_NODISCARD static SBE_CONSTEXPR bool isConstLength() SBE_NOEXCEPT
{
    return false;
}

SBE_NODISCARD static std::size_t computeLength(
    std::size_t noEventsLength = 0,
    std::size_t noMDFeedTypesLength = 0,
    std::size_t noInstAttribLength = 0,
    std::size_t noLotTypeRulesLength = 0)
{
#if defined(__GNUG__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wtype-limits"
#endif
    std::size_t length = sbeBlockLength();

    length += NoEvents::sbeHeaderSize();
    if (noEventsLength > 254LL)
    {
        throw std::runtime_error("noEventsLength outside of allowed range [E110]");
    }
    length += noEventsLength *NoEvents::sbeBlockLength();

    length += NoMDFeedTypes::sbeHeaderSize();
    if (noMDFeedTypesLength > 254LL)
    {
        throw std::runtime_error("noMDFeedTypesLength outside of allowed range [E110]");
    }
    length += noMDFeedTypesLength *NoMDFeedTypes::sbeBlockLength();

    length += NoInstAttrib::sbeHeaderSize();
    if (noInstAttribLength > 254LL)
    {
        throw std::runtime_error("noInstAttribLength outside of allowed range [E110]");
    }
    length += noInstAttribLength *NoInstAttrib::sbeBlockLength();

    length += NoLotTypeRules::sbeHeaderSize();
    if (noLotTypeRulesLength > 254LL)
    {
        throw std::runtime_error("noLotTypeRulesLength outside of allowed range [E110]");
    }
    length += noLotTypeRulesLength *NoLotTypeRules::sbeBlockLength();

    return length;
#if defined(__GNUG__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
}
};
}
#endif
