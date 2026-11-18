/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// LmaxTradeClient: post-trade capture for LMAX (Broker Trading FIX 4.4).
// NOT CERTIFIED: see README.md for what has been checked.
//
// Two inputs, either or both:
//   - its own Broker Trading session (a SessionFactory): on every logon it
//     sends a TradeCaptureReportRequest for matched trades since the start of
//     the trading day (17:00 New York), SubscriptionRequestType 1 (snapshot
//     and updates), and maps every TradeCaptureReport;
//   - msg::ExecCopy messages from LmaxOM: the ExecutionReports of trades,
//     trade corrections and trade cancels on the order session.
// Each is mapped to post_trade::msg::TradeCapture (TradeMap.hpp), applied to
// a post_trade::TradeLedger, and sent to the subscriber only when it changed
// the ledger: a trade seen from both inputs, a resend after a reconnect and
// the day's replay at each logon reach the subscriber once.
//
// LMAX does not ask for a TradeCaptureReportAck (it is not in the Broker
// Trading dictionary), so none is sent. The ledger is cleared when a logon
// falls in a new trading day.

#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include "actors/Actor.hpp"
#include "actors/msg/Shutdown.hpp"
#include "actors/msg/Start.hpp"
#include "lmax_om/LmaxSession.hpp"
#include "lmax_om/msg/LmaxReport.hpp"
#include "lmax_post_trade/TradeMap.hpp"
#include "post_trade/TradeLedger.hpp"

namespace lmax::pt
{
  struct TradeClientConfig
  {
    MapContext map;            // venue and instruments
    std::string account;       // tag 1 on the request; empty: the user's default account
    bool subscribe = true;     // 263=1: snapshot and updates; 0: snapshot only
  };

  class LmaxTradeClient : public actors::Actor
  {
  public:
    struct Stats
    {
      uint64_t logons = 0, requests = 0, acks = 0, request_rejected = 0;
      uint64_t tcr = 0, execs = 0, unmapped = 0;
      uint64_t emitted = 0, duplicates = 0, stale = 0, orphans = 0, invalid = 0;
    };

    // factory: its own session (may be empty: ExecCopy input only).
    LmaxTradeClient(TradeClientConfig cfg, om::SessionFactory factory, actors::Actor *subscriber);
    ~LmaxTradeClient() override;
    const char *get_name() const override { return "LmaxTradeClient"; }

    // Read on the actor's thread or while it is idle (tests).
    const post_trade::TradeLedger &ledger() const { return ledger_; }
    const Stats &stats() const { return st_; }

    // For tests: the clock used for the trading day (ns since the epoch).
    void set_clock(uint64_t (*now)()) { now_ = now; }

  private:
    class Events;
    void start_handler(const actors::msg::Start *) noexcept;
    void shutdown_handler(const actors::msg::Shutdown *) noexcept;
    void report_handler(const om::msg::LmaxReport *) noexcept;
    void copy_handler(const om::msg::ExecCopy *) noexcept;
    void apply(const post_trade::msg::TradeCapture &t) noexcept;

    TradeClientConfig cfg_;
    om::SessionFactory factory_;
    actors::Actor *sub_;
    post_trade::TradeLedger ledger_;
    std::unique_ptr<Events> events_;
    std::unique_ptr<om::LmaxSession> session_;
    uint64_t session_no_ = 0;
    uint64_t day_start_ = 0; // trading day the ledger holds
    uint64_t next_req_ = 1;
    uint64_t (*now_)() = nullptr;
    Stats st_;
  };
}
