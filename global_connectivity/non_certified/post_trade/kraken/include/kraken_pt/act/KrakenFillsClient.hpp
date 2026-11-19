#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// KrakenFillsClient: Kraken fills into post_trade::TradeLedger, and a
// TradeCapture to the subscriber for each fill the ledger had not seen. NOT
// CERTIFIED: it has never connected to Kraken (README.md).
//
//   stream          spot executions channel, or the futures /fills poll
//   reconciliation  spot TradesHistory, or futures /fills paged back; at
//                   logon and every reconcile_every_s, over the last
//                   reconcile_lookback_s
//
// The ledger keys every fill by its Kraken id (spot exec_id = trade txid;
// futures fill_id), so a fill reported by both sources, or again after a
// reconnect, reaches the subscriber once. After each pass the client counts:
//   added_by_rest      REST rows the stream had not delivered
//   missing_from_rest  stream fills inside the pass's time range that REST
//                      did not return (should be 0; logged as an error)
// A fill that does not map (instrument not configured, size not a whole
// number of lots) is logged as an error and counted.

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
#include "kraken_pt/KrakenFills.hpp"
#include "post_trade/TradeLedger.hpp"

namespace kraken::pt
{
  namespace msg
  {
    struct FillsEvent : public actors::MessageT<FillsEvent>
    {
      enum Kind
      {
        LOGGED_IN,
        LOGIN_FAILED,
        DISCONNECTED,
        FILL,
        RECONCILED,
        RECONNECT,
        RECONCILE,
      };
      Kind kind;
      uint64_t session = 0;
      nlohmann::json body;
      std::string key;
      Source src = Source::SpotWs;
      size_t rows = 0;
      bool ok = true;
      std::string text;
      FillsEvent(Kind k, uint64_t s) : kind(k), session(s) {}
    };
  }

  struct FillsStats
  {
    uint64_t stream_fills = 0, rest_rows = 0, emitted = 0, duplicates = 0, unmapped = 0;
    uint64_t added_by_rest = 0, missing_from_rest = 0, passes = 0, failed_passes = 0;
  };

  class KrakenFillsClient : public actors::Actor
  {
  public:
    KrakenFillsClient(FillsConfig cfg, actors::Actor *subscriber, FillsSessionFactory factory);
    ~KrakenFillsClient() override;
    const char *get_name() const override { return "KrakenFillsClient"; }

    const post_trade::TradeLedger &ledger() const { return ledger_; }
    const FillsStats &stats() const { return st_; }
    bool ready() const { return ready_; }
    int sessions_created() const { return sessions_created_; }

  private:
    class Events;

    void start_handler(const actors::msg::Start *) noexcept;
    void shutdown_handler(const actors::msg::Shutdown *) noexcept;
    void event_handler(const msg::FillsEvent *) noexcept;
    void on_fill(const msg::FillsEvent &e) noexcept;
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
    uint64_t pass_start_ns_ = 0, pass_end_ns_ = 0;
    std::set<std::string> pass_rest_;               // fill ids REST returned in this pass
    std::map<std::string, uint64_t> stream_seen_;   // fill id -> fill time (ns), from the stream

    std::jthread reconnect_timer_, reconcile_timer_;
  };
}
