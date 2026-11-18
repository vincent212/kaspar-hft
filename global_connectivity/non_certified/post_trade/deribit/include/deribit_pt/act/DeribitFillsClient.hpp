#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// DeribitFillsClient: Deribit trades into post_trade::TradeLedger, and a
// TradeCapture to the subscriber for each trade the ledger had not seen. NOT
// CERTIFIED: it has never connected to Deribit (no test keys; README.md).
//
//   user.trades.any.any.raw (WebSocket)                -> trades as they happen
//   private/get_user_trades_by_currency_and_time (REST) -> the same trades again,
//                                                        at logon and every
//                                                        reconcile_every_s
//
// The ledger keys every trade by currency:trade_id, so a trade reported by
// both sources, or again after a reconnect, reaches the subscriber once.
// After each reconciliation pass the client counts:
//   added_by_rest      REST rows the WebSocket had not delivered
//   missing_from_rest  WebSocket trades inside the pass's time range that REST
//                      did not return (should be 0; logged as an error)
// A trade that cannot be mapped is logged as an error and counted.

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>

#include "actors/Actor.hpp"
#include "actors/Message.hpp"
#include "actors/msg/Shutdown.hpp"
#include "actors/msg/Start.hpp"
#include "deribit_pt/DeribitFills.hpp"
#include "post_trade/TradeLedger.hpp"

namespace deribit::pt
{
  namespace msg
  {
    struct FillsReport : public actors::MessageT<FillsReport>
    {
      enum Kind
      {
        LOGGED_IN,
        LOGIN_REJECTED,
        DISCONNECTED,
        FILL,
        RECONCILED,
        RECONNECT,
        RECONCILE,
      };
      Kind kind;
      uint64_t session = 0;
      nlohmann::json body;
      bool from_rest = false;
      size_t rows = 0;
      bool ok = true;
      std::string text;
      FillsReport(Kind k, uint64_t s) : kind(k), session(s) {}
    };
  }

  struct FillsStats
  {
    uint64_t ws_fills = 0, rest_rows = 0, emitted = 0, duplicates = 0, unmapped = 0;
    uint64_t added_by_rest = 0, missing_from_rest = 0, passes = 0, failed_passes = 0;
  };

  class DeribitFillsClient : public actors::Actor
  {
  public:
    DeribitFillsClient(FillsConfig cfg, actors::Actor *subscriber, FillsSessionFactory factory);
    ~DeribitFillsClient() override;
    const char *get_name() const override { return "DeribitFillsClient"; }

    const post_trade::TradeLedger &ledger() const { return ledger_; }
    const FillsStats &stats() const { return st_; }
    bool ready() const { return ready_; }
    int sessions_created() const { return sessions_created_; }

  private:
    class Events;

    void start_handler(const actors::msg::Start *) noexcept;
    void shutdown_handler(const actors::msg::Shutdown *) noexcept;
    void report_handler(const msg::FillsReport *) noexcept;
    void on_fill(const nlohmann::json &t, bool from_rest) noexcept;
    void reconcile() noexcept;
    void connect() noexcept;
    void drop() noexcept;
    void schedule_reconnect() noexcept;

    FillsConfig cfg_;
    actors::Actor *sub_;
    FillsSessionFactory factory_;
    post_trade::TradeLedger ledger_;
    FillsStats st_;

    std::unique_ptr<Events> events_;
    std::unique_ptr<FillsSession> session_;
    uint64_t session_no_ = 0;
    int sessions_created_ = 0;
    bool ready_ = false, stopping_ = false, refused_ = false;

    bool in_pass_ = false;
    uint64_t pass_begin_ms_ = 0, pass_end_ms_ = 0;
    std::set<std::string> pass_rest_;            // trade ids REST returned in this pass
    std::map<std::string, uint64_t> ws_seen_;    // trade id -> trade time (ms), from the WebSocket

    std::jthread reconnect_timer_, reconcile_timer_;
  };
}

actors::Actor *create_DeribitFillsClient(deribit::pt::FillsConfig cfg, actors::Actor *subscriber,
                                         deribit::pt::FillsSessionFactory factory = nullptr);
