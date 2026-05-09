#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Message.hpp"
#include <string>
#include <optional>
#include <cstdint>

namespace kasprowy {
namespace msg {

/**
 * Market data bar with VIX, SPX, and VVIX open prices only.
 * NO CLOSE/HIGH/LOW - those are future data (causality violation).
 * Must match Python DataBar in kasprowy_actors/messages.py
 */
struct DataBar : public actors::Message_N<48> {
    int64_t timestamp;       // Unix timestamp in milliseconds
    std::string date;        // "YYYY-MM-DD"
    std::string time;        // "HH:MM:SS"
    double vix_open;
    double spx_open;
    std::string contract;    // ES contract symbol: "ESZ0", "ESH1", etc.
    double vvix_open = 0.0;                // VVIX at bar timestamp (for VVIX filter)
    std::optional<double> prev_vix_close;  // Previous day's VIX close (known at bar time)

    DataBar() = default;
};

/**
 * Request to open a new position.
 * Must match Python OpenPosition in kasprowy_actors/messages.py
 */
struct OpenPosition : public actors::Message_N<50> {
    std::string strategy_id;    // "SPX_930", "SPX_930_CPP", etc.
    int64_t timestamp;
    std::string date;
    std::string time;
    std::string symbol;         // "ES", "SPX"
    int direction;              // +1 long, -1 short
    double entry_price;
    double leverage = 1.0;
    int position_size = 1;
    std::optional<double> stop_loss_pct;
    std::optional<double> profit_take_pct;
    std::string tag;            // Trade tag (e.g., "16-18_SLOPE", "12-14")

    OpenPosition() = default;
};

/**
 * Request to close an existing position.
 * Must match Python ClosePosition in kasprowy_actors/messages.py
 */
struct ClosePosition : public actors::Message_N<52> {
    std::string strategy_id;
    int64_t timestamp;
    std::string date;
    std::string time;
    double exit_price;
    std::string reason;         // "EOD", "STOP_LOSS", "PROFIT_TAKE", "SIGNAL_CHANGE"
    std::string tag;            // Trade tag (e.g., "16-18_SLOPE", "12-14")

    ClosePosition() = default;
};

/**
 * End of data signal (for backtests).
 */
struct EndOfData : public actors::Message_N<53> {
    int64_t final_timestamp = 0;

    EndOfData() = default;
};

/**
 * Trade was done buy or sell
 */
struct Trade : public actors::Message_N<54> {
    std::string strategy_id;
    std::string date;
    int direction;              // +1 long, -1 short
    double entry_price;
    std::string entry_time;
    double leverage;
    std::string event_type;     // "OPEN" or "CLOSE"
    std::string tag;            // Trade tag (e.g., "16-18_SLOPE", "12-14")

    // For completed trades (used by AuditLogger)
    double exit_price = 0.0;
    std::string exit_time = "";
    double return_pct = 0.0;

    Trade() = default;
};

/**
 * Log a completed trade to audit log.
 */
struct LogTrade : public actors::Message_N<55> {
    Trade trade;

    LogTrade() = default;
};

/**
 * Subscribe to events from another actor.
 * Must match Python Subscribe in kasprowy_actors/messages.py
 */
struct Subscribe : public actors::Message_N<56> {
    Subscribe() = default;
};

} // namespace msg
} // namespace kasprowy
