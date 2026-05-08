/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/*
 * remote_messages.hpp - Messages for kaspr <-> Rust MTD communication
 *
 * These messages are serialized to JSON and sent over ZMQ.
 *
 * Copyright 2025 Vincent Maciejewski, & M2 Tech
 */

#pragma once

#include "actors/Message.hpp"
#include "actors/remote/Serialization.hpp"
#include <string>
#include <array>

namespace kaspr::msg {

// Message IDs for remote messages (must be < 512, using 210-219 range to avoid conflicts)
constexpr int MSG_BBBO_UPDATE = 210;
constexpr int MSG_FILL = 211;
constexpr int MSG_ORDER_ACK = 212;
constexpr int MSG_CANC_ACK = 213;
constexpr int MSG_REJECT = 214;
constexpr int MSG_SEND_ORDER = 215;
constexpr int MSG_CANCEL_ORDER = 216;
constexpr int MSG_INIT_ILINK = 217;
constexpr int MSG_TERMINATE_ILINK = 218;

/**
 * BBBOUpdate - Best bid/offer update from C++ to Rust
 */
struct BBBOUpdate : public actors::Message_N<MSG_BBBO_UPDATE> {
    int sym;           // Symbol ID
    int venue;         // Venue ID (0=CME, etc.)
    int bid;           // Best bid price (512ths)
    int ask;           // Best ask price (512ths)
    int bid_sz;        // Bid size
    int ask_sz;        // Ask size

    BBBOUpdate() = default;
    BBBOUpdate(int s, int v, int b, int a, int bs, int as)
        : sym(s), venue(v), bid(b), ask(a), bid_sz(bs), ask_sz(as) {}
};

/**
 * Fill - Fill notification from C++ to Rust
 */
struct Fill : public actors::Message_N<MSG_FILL> {
    int64_t id;        // Order ID
    int sym;           // Symbol ID
    int side;          // 0=buy, 1=sell
    int sz;            // Fill size
    int px;            // Fill price (512ths)
    int still_to_fill; // Remaining quantity

    Fill() = default;
    Fill(int64_t i, int s, int sd, int sz_, int p, int stf)
        : id(i), sym(s), side(sd), sz(sz_), px(p), still_to_fill(stf) {}
};

/**
 * OrderAck - Order acknowledgment from C++ to Rust
 */
struct OrderAck : public actors::Message_N<MSG_ORDER_ACK> {
    int64_t id;        // Order ID

    OrderAck() = default;
    explicit OrderAck(int64_t i) : id(i) {}
};

/**
 * CancAck - Cancel acknowledgment from C++ to Rust
 */
struct CancAck : public actors::Message_N<MSG_CANC_ACK> {
    int64_t id;        // Order ID

    CancAck() = default;
    explicit CancAck(int64_t i) : id(i) {}
};

/**
 * OrderReject - Order rejection from C++ to Rust
 */
struct OrderReject : public actors::Message_N<MSG_REJECT> {
    int64_t id;        // Order ID
    std::string reason;

    OrderReject() = default;
    OrderReject(int64_t i, std::string r) : id(i), reason(std::move(r)) {}
};

/**
 * SendOrder - Order request from Rust to C++
 */
struct SendOrder : public actors::Message_N<MSG_SEND_ORDER> {
    int sym;           // Symbol ID
    int sz;            // Order size
    int px;            // Limit price (512ths)
    int side;          // 0=buy, 1=sell
    int venue;         // Venue ID

    SendOrder() = default;
    SendOrder(int s, int sz_, int p, int sd, int v)
        : sym(s), sz(sz_), px(p), side(sd), venue(v) {}
};

/**
 * CancelOrder - Cancel request from Rust to C++
 */
struct CancelOrder : public actors::Message_N<MSG_CANCEL_ORDER> {
    int64_t id;        // Order ID
    int venue;         // Venue ID

    CancelOrder() = default;
    CancelOrder(int64_t i, int v) : id(i), venue(v) {}
};

/**
 * InitILink - Initialize iLink session from Rust to C++
 */
struct InitILink : public actors::Message_N<MSG_INIT_ILINK> {
    int session_id;    // 0=primary, 1=secondary

    InitILink() = default;
    explicit InitILink(int id) : session_id(id) {}
};

/**
 * TerminateILink - Terminate iLink session from Rust to C++
 */
struct TerminateILink : public actors::Message_N<MSG_TERMINATE_ILINK> {
    TerminateILink() = default;
};

// Register messages for remote serialization
REGISTER_REMOTE_MESSAGE_6(BBBOUpdate, sym, int, venue, int, bid, int, ask, int, bid_sz, int, ask_sz, int)
REGISTER_REMOTE_MESSAGE_6(Fill, id, int64_t, sym, int, side, int, sz, int, px, int, still_to_fill, int)
REGISTER_REMOTE_MESSAGE_1(OrderAck, id, int64_t)
REGISTER_REMOTE_MESSAGE_1(CancAck, id, int64_t)
REGISTER_REMOTE_MESSAGE_2(OrderReject, id, int64_t, reason, std::string)
REGISTER_REMOTE_MESSAGE_5(SendOrder, sym, int, sz, int, px, int, side, int, venue, int)
REGISTER_REMOTE_MESSAGE_2(CancelOrder, id, int64_t, venue, int)
REGISTER_REMOTE_MESSAGE_1(InitILink, session_id, int)
REGISTER_REMOTE_MESSAGE_0(TerminateILink)

} // namespace kaspr::msg

// Include generated remote message registrations (includes headers and using declarations)
#include "actors/generated/cpp/RemoteMessages.hpp"

// Manual registration for BBBOChg (requires enum-to-int translation)
#include "frame/ob/msg/BBBOChg.hpp"
#include "frame/ob/msg/TradeNotify.hpp"
namespace frame { namespace ob { namespace msg {
    // Register BBBOChg with custom serialization for enum fields
    REGISTER_REMOTE_MESSAGE(BBBOChg,
        {
            // Serialize: enum -> int for JSON
            nlohmann::json j;
            j["tx_time"] = msg->tx_time;
            j["venue"] = static_cast<int>(msg->venue);
            j["side"] = static_cast<int>(msg->side);
            j["sym"] = msg->sym;
            j["best_bid"] = msg->best_bid;
            j["best_ask"] = msg->best_ask;
            return j;
        },
        {
            // Deserialize: int -> enum from JSON
            auto* m = new BBBOChg();
            m->tx_time = j.value("tx_time", 0UL);
            m->venue = static_cast<en::x>(j.value("venue", 0));
            m->side = static_cast<en::bs>(j.value("side", 0));
            m->sym = j.value("sym", 0U);
            m->best_bid = j.value("best_bid", 0);
            m->best_ask = j.value("best_ask", 0);
            return m;
        }
    )

    // Register TradeNotify (but don't serialize payload - not needed for Python)
    REGISTER_REMOTE_MESSAGE(TradeNotify,
        {
            // Serialize: empty (Python doesn't need trade notifications)
            return nlohmann::json::object();
        },
        {
            // Deserialize: empty
            return new TradeNotify();
        }
    )
}}}

// Register AddToPos (requires enum-to-int translation for side field)
#include "positionman/msg/AddToPos.hpp"
namespace positionman { namespace msg {
    REGISTER_REMOTE_MESSAGE(AddToPos,
        {
            // Serialize: enum -> int for JSON
            nlohmann::json j;
            j["instrument"] = msg->instrument;
            j["side"] = static_cast<int>(msg->side);
            j["sz"] = msg->sz;
            return j;
        },
        {
            // Deserialize: int -> enum from JSON
            auto* m = new AddToPos(
                j.value("instrument", std::string("")),
                static_cast<en::bs>(j.value("side", 0)),
                j.value("sz", 0.0)
            );
            return m;
        }
    )
}}
