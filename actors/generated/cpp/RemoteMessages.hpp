#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Auto-generated from messages.schema.json
// Version: 1.0.0
// DO NOT EDIT MANUALLY

#include "actors/remote/Serialization.hpp"

// Fill notification from SOM after order execution (maps to frame::som::msg::Fill)
// Skipped - requires manual registration with field translation

// Subscribe to fill notifications from SOM (maps to frame::som::msg::FillSub)
#include "frame/som/msg/FillSub.hpp"
using FillSubscribe = frame::som::msg::FillSub;
REGISTER_REMOTE_MESSAGE(FillSubscribe,
    { return nlohmann::json::object(); },
    { return new frame::som::msg::FillSub(); }
)

// Best bid/best offer change from order book (maps to frame::ob::msg::BBBOChg)
// Skipped - requires manual registration with field translation

// Subscribe to BBO changes from order book (maps to frame::ob::msg::BBBOSub)
#include "frame/ob/msg/BBBOSub.hpp"
using BBBOSub = frame::ob::msg::BBBOSub;
REGISTER_REMOTE_MESSAGE(BBBOSub,
    { return nlohmann::json::object(); },
    { return new frame::ob::msg::BBBOSub(); }
)

// Add to position (triggers shadow lights) (maps to positionman::msg::AddToPos)
// Skipped - requires manual registration with field translation

// Market data bar with VIX and SPX open prices only (Kasprowy backtest)
#include "kasprowy_messages.hpp"
using DataBar = kasprowy::msg::DataBar;
REGISTER_REMOTE_MESSAGE(DataBar,
    { return nlohmann::json::object(); },
    { auto* m = new kasprowy::msg::DataBar(); m->timestamp = j["timestamp"].get<int64_t>(); m->date = j["date"].get<std::string>(); m->time = j["time"].get<std::string>(); m->vix_open = j["vix_open"].get<double>(); m->spx_open = j["spx_open"].get<double>(); m->contract = j.value("contract", ""); m->vvix_open = j.value("vvix_open", 0.0); if (j.contains("prev_vix_close") && !j["prev_vix_close"].is_null()) { m->prev_vix_close = j["prev_vix_close"].get<double>(); } return m; }
)

// Trade event (uses Trade message, not TradeComplete)
#include "kasprowy_messages.hpp"
using Trade = kasprowy::msg::Trade;
REGISTER_REMOTE_MESSAGE(Trade,
    { return nlohmann::json::object(); },
    { auto* m = new kasprowy::msg::Trade(); m->strategy_id = j["strategy_id"].get<std::string>(); m->date = j["date"].get<std::string>(); m->direction = j["direction"].get<int>(); m->entry_price = j["entry_price"].get<double>(); m->entry_time = j["entry_time"].get<std::string>(); m->leverage = j.value("leverage", 1.0); m->event_type = j.value("event_type", ""); m->tag = j.value("tag", ""); if (j.contains("exit_price")) { m->exit_price = j["exit_price"].get<double>(); } if (j.contains("exit_time")) { m->exit_time = j["exit_time"].get<std::string>(); } if (j.contains("return_pct")) { m->return_pct = j["return_pct"].get<double>(); } return m; }
)
