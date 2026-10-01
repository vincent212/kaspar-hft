#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <cstdint>
#include <string>

#include "mdp3/mbo_if.hpp"

namespace unit_test
{
  // A feed_handler_if whose every callback is a no-op. feed_handler_if has ~20
  // pure virtuals; a test that cares about one or two derives from this and
  // overrides just those, instead of carrying its own stub of the whole
  // interface that breaks every time a callback signature moves.
  struct NullFeedHandler : public mdp3::feed_handler_if
  {
    void disable_mbo(bool) noexcept override {}
    void set_max_mbp_level(uint32_t) noexcept override {}
    void MDIncrementalRefreshBook(uint64_t, uint32_t, uint64_t, uint64_t, int32_t,
        int64_t, int8_t, char, int32_t, int32_t, uint8_t, bool, bool) noexcept override {}
    void MDIncrementalRefreshBook(uint64_t, uint32_t, uint64_t, uint64_t, int32_t,
        int64_t, int8_t, char, int32_t, uint64_t, uint8_t, uint64_t, bool, bool,
        bool) noexcept override {}
    void MDIncrementalRefreshTradeSummary(uint64_t, uint32_t, uint64_t, uint64_t,
        int32_t, int64_t, int8_t, char, uint8_t, int32_t, int32_t, bool, bool) noexcept override {}
    void MDIncrementalRefreshTradeSummary(uint64_t, uint32_t, uint64_t, uint64_t,
        int32_t, uint64_t, bool, bool) noexcept override {}
    void MDIncrementalRefreshSessionStatistics(uint32_t, uint64_t, uint64_t, uint32_t,
        uint8_t, int64_t, int64_t, uint8_t, char) noexcept override {}
    void MDIncrementalRefreshDailyStatistics(uint32_t, uint64_t, uint64_t, uint32_t,
        int64_t, int8_t, int32_t, char, bool, bool, uint8_t, uint16_t) noexcept override {}
    void MDInstrumentDefinitionFuture(uint32_t, uint64_t, char*, char*, char*, int64_t,
        int8_t, int64_t, int8_t, int64_t, int8_t, int32_t, char, uint8_t, uint64_t,
        uint64_t, char*, uint8_t, char, uint8_t, int64_t, uint8_t, uint8_t, char,
        int64_t, int8_t, uint8_t, char*, uint8_t, uint16_t, uint16_t, uint16_t, int32_t,
        int32_t, uint16_t, char*, int64_t, uint8_t) noexcept override {}
    void MDInstrumentDefinitionOption(uint32_t, uint64_t, char*, char*, char*, int64_t,
        int8_t, int64_t, int8_t, int32_t, char, uint64_t, uint64_t, char*, uint8_t,
        uint8_t, char, uint8_t, int64_t, uint8_t, uint8_t, int64_t, uint8_t, uint8_t,
        int64_t, uint8_t, char, int8_t, uint8_t, uint32_t*, std::string*, int64_t,
        int8_t, uint8_t, char*, uint8_t, uint16_t, int32_t, int32_t, uint16_t, char*,
        int64_t, uint8_t) noexcept override {}
    void MDInstrumentDefinitionSpread(uint32_t, uint64_t, char*, char*, char*, int64_t,
        int8_t, int64_t, int8_t, int32_t, char, uint64_t, uint64_t, char*, uint8_t,
        uint8_t, char, uint8_t, int64_t, uint8_t, uint8_t, char, int8_t, uint8_t,
        int32_t*, int8_t*, int64_t*, int8_t*, int8_t*, int32_t*, uint8_t*, int64_t,
        int8_t, uint8_t, char*, uint8_t, uint16_t, int32_t, int32_t, uint16_t,
        char*) noexcept override {}
    void ChannelReset(uint32_t, uint64_t, uint64_t, const char*) noexcept override {}
    void SnapshotFullRefreshOrderBook_NR(uint32_t, uint32_t, uint32_t, uint64_t,
        uint64_t, uint32_t, uint32_t, int32_t, int32_t, int64_t, int64_t, char,
        uint64_t, uint64_t) noexcept override {}
    void SnapshotFullRefreshOrderBook(uint64_t, uint64_t, int32_t, int32_t, int64_t,
        int64_t, char, uint64_t, uint64_t) noexcept override {}
    void MDIncrementalRefreshLimitsBanding(uint32_t, uint64_t, uint64_t, int32_t,
        int64_t, int8_t, int64_t, int8_t, int64_t, int8_t, const char*) noexcept override {}
    void SecurityStatus(uint32_t, uint64_t, uint64_t, int32_t, uint8_t, uint8_t,
        uint8_t) noexcept override {}
    void MDIncrementalRefreshVolume(uint32_t, uint64_t, uint64_t, int32_t, int32_t,
        char, uint8_t) noexcept override {}
    void DataReceoveryRestart() noexcept override {}
    void Gap() noexcept override {}
    void BurstEnd(uint32_t) noexcept override {}
    void EndOfPacket(uint32_t, uint64_t) noexcept override {}
    void PrintStats() noexcept override {}
  };
}
