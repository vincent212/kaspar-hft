#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "mdp3/mbo_if.hpp"
#include "mdp3/msg/ParsedMsg.hpp"
#include "actors/Actor.hpp"
#include "bfile/r_l3.hpp"
#include "enum/e_names.hpp"

namespace mdp3
{
  // Worker-side feed_handler_if callback -- the stateless half of decode.
  //
  // For each decoded MBO order or trade it builds the l3 record (mirroring
  // handler_if's field build, but WITHOUT the orderid_to_securityid map and
  // WITHOUT BOOKSEND) and APPENDS it to a per-message batch. After the worker
  // finishes decoding one SBE message it calls flush(), which sends the whole
  // batch as one ParsedMsg (tagged order_seq) to the single Reconstructor. The
  // Reconstructor owns the map + routing and applies batches in order_seq order.
  //
  // Only hot MBO-incremental templates reach a worker, so every non-hot callback
  // is a no-op.
  //
  // Not an actor: a plain callback owned by a DecodeWorker, touched only by that
  // worker's thread -- no synchronization. `order_seq_` is set by the worker
  // before each decode; `batch_` is reused across messages.
  struct DecodeSink : public feed_handler_if
  {
    actor_ptr reconstructor_ = nullptr; // destination for parsed batches
    actor_ptr self_ = nullptr;          // owning worker (sender, for reply routing)
    en::x     xchg_ = en::x::UNI;        // venue stamp

    uint64_t  order_seq_ = 0;            // per-message resequence key (set per DecodeReq)
    // Ingress mailbox depth of the packet this message came from. Set by the
    // worker from DecodeReq before each decode -- NOT shared state: each worker
    // owns its sink, and the value rides the request, so two workers decoding
    // different packets cannot cross-contaminate. handler_if keeps the same
    // number in a member (ingress_qlen_) because its decode is serial.
    uint32_t  ingress_qlen_ = 0;
    std::vector<bfile::l3_t> batch_;     // entries built for the current message

    DecodeSink(actor_ptr reconstructor, actor_ptr self, en::x xchg)
        : reconstructor_(reconstructor), self_(self), xchg_(xchg)
    {
      batch_.reserve(64); // typical worst-case entries per message; avoids regrowth
    }

    // Send the current message's accumulated entries to the Reconstructor and
    // reset for the next message. Called by the worker after decode_one returns.
    // ALWAYS sends -- even an empty batch -- so the Reconstructor advances its
    // expected order_seq for every dispatched message (else it stalls waiting for
    // a message that produced no entries).
    void flush()
    {
      auto *pm = new msg::ParsedMsg(order_seq_);
      pm->entries.swap(batch_); // hand off entries; batch_ becomes empty
      reconstructor_->send(pm, self_);
    }

    // ---- HOT: MBO incremental book (order add / modify / delete) ----
    void MDIncrementalRefreshBook(
        uint64_t recv_time, uint32_t /*msgSeqNum*/, uint64_t transactTime,
        uint64_t sendingTime, int32_t securityID, int64_t px_mantissa,
        int8_t px_exponent, char side, int32_t displayQty, uint64_t orderID,
        uint8_t orderUpdateAction, uint64_t priority, bool lastQuote,
        bool endOfEvent, bool recovery) noexcept override
    {
      bfile::l3_mbo_v2_t l3;
      memset(&l3, 0, sizeof(l3));
      l3.typ = en::l3::MBO_V2;
      l3.venue = xchg_;
      l3.transactTime = transactTime;
      l3.sendingTime = sendingTime;
      l3.handlerendtim = recv_time;
      l3.orderUpdateAction = orderUpdateAction;
      l3.displayQty = displayQty;
      l3.orderID = orderID;
      l3.priority = priority;
      l3.pxd = to_price(px_mantissa, px_exponent);
      l3.securityID = securityID;              // routing resolved at the Reconstructor
      l3.side = side;
      l3.lastQuote = lastQuote;
      l3.endOfEvent = endOfEvent;
      l3.recovery = recovery;
      l3.ingress_qlen = ingress_qlen_;
      batch_.emplace_back(l3);
    }

    // ---- HOT: MBO trade (carries only orderID; securityID resolved downstream) ----
    void MDIncrementalRefreshTradeSummary(
        uint64_t recv_time, uint32_t /*msgSeqNum*/, uint64_t transactTime,
        uint64_t sendingTime, int32_t lastQty, uint64_t orderID,
        bool lastTrade, bool endOfEvent) noexcept override
    {
      bfile::l3_mbo_trd_v2_t l3;
      memset(&l3, 0, sizeof(l3));
      l3.typ = en::l3::MBOT_V2;
      l3.venue = xchg_;
      l3.transactTime = transactTime;
      l3.sendingTime = sendingTime;
      l3.handlerendtim = recv_time;
      l3.lastQty = lastQty;
      l3.orderID = orderID;
      l3.endOfEvent = endOfEvent;
      l3.lastTrade = lastTrade;
      l3.ingress_qlen = ingress_qlen_;
      batch_.emplace_back(l3);
    }

    // ---- MBP variants: options MBP not traded (handler_if filters them) ----
    void MDIncrementalRefreshBook(uint64_t, uint32_t, uint64_t, uint64_t, int32_t,
        int64_t, int8_t, char, int32_t, int32_t, uint8_t, bool, bool) noexcept override {}
    void MDIncrementalRefreshTradeSummary(uint64_t, uint32_t, uint64_t, uint64_t,
        int32_t, int64_t, int8_t, char, uint8_t, int32_t, int32_t, bool, bool) noexcept override {}

    // ---- COLD: never fanned out to a worker (inline serial path handles them) ----
    void disable_mbo(bool) noexcept override {}
    void set_max_mbp_level(uint32_t) noexcept override {}
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
