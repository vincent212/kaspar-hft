#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// BybitFillsClient: the firm's Bybit V5 executions (spot and linear) as
// post_trade::msg::TradeCapture. NOT CERTIFIED: it has never connected to
// Bybit (no testnet keys were supplied).
//
// On Start it logs in through its FillSource (private stream topic
// `execution`). After every login it reconciles over REST
// (GET /v5/execution/list) each configured category from `from_ms` to now:
// on the first login from the start of the UTC day (or cfg.since_ms), after
// that from the last reconciled time minus a margin. It reconciles again
// every reconcile_every_s. Bybit's private stream has no replay, so the REST
// pass is what recovers executions missed while disconnected.
//
// Every execution goes through BybitFills (to_capture) and a
// post_trade::TradeLedger; the subscriber gets one TradeCapture for each
// execution that changed the ledger, so an execution delivered by the
// stream and again by REST, or by two overlapping REST windows, reaches it
// once. The ledger holds one UTC day; the previous day's is kept so that a
// late copy of yesterday's execution is still recognised.

#include <cstdint>
#include <memory>
#include <string>
#include <thread>

#include "actors/Actor.hpp"
#include "actors/msg/Shutdown.hpp"
#include "actors/msg/Start.hpp"
#include "bybit_post_trade/BybitFills.hpp"
#include "bybit_post_trade/FillSource.hpp"
#include "bybit_post_trade/msg/FillEvent.hpp"
#include "post_trade/TradeLedger.hpp"
#include "post_trade/msg/TradeCapture.hpp"

namespace bybit_pt
{
  struct ClientConfig
  {
    SourceConfig source;
    std::string key_file = "~/.kaspar/keys/bybit_testnet.json";
    Instruments instruments;
    uint64_t since_ms = 0;       // first reconciliation from here; 0: start of the UTC day
    int reconcile_every_s = 300; // 0: only after a login
    int margin_s = 60;           // each window starts this much before the last one ended
    int reconnect_ms = 2000;
    bool sim = false;
    SimConfig sim_config;

    static bool from_file(const std::string &path, ClientConfig &out, std::string &err);
    bool valid(std::string &err) const;
  };

  class BybitFillsClient : public actors::Actor
  {
  public:
    struct Stats
    {
      size_t executions = 0;     // delivered by the source (stream and REST)
      size_t from_stream = 0, from_rest = 0;
      size_t emitted = 0;        // TradeCapture sent
      size_t duplicates = 0;     // already in the ledger
      size_t not_trades = 0;     // Funding, Settle, unknown types
      size_t unmapped = 0;       // executions in instruments not configured: CHECK
      size_t bad = 0;            // malformed or not whole units: CHECK
      size_t older_than_ledger = 0;
      size_t logins = 0, disconnects = 0;
      size_t reconciliations = 0, reconcile_failures = 0, rest_pages = 0;
    };

    BybitFillsClient(ClientConfig cfg, SourceFactory factory, actors::Actor *subscriber);
    ~BybitFillsClient() override;
    const char *get_name() const override { return "BybitFillsClient"; }

    // Read from the actor's thread, or when it is idle (tests).
    const Stats &stats() const { return stats_; }
    const post_trade::TradeLedger &ledger() const { return ledger_; }
    uint32_t ledger_date() const { return date_; }
    bool ready() const { return ready_; }

  private:
    class Forwarder;
    void on_start(const actors::msg::Start *) noexcept;
    void on_shutdown(const actors::msg::Shutdown *) noexcept;
    void on_event(const msg::FillEvent *) noexcept;
    void on_execution(const msg::FillEvent &e) noexcept;
    void connect() noexcept;
    void reconcile_all() noexcept;
    void schedule_reconnect() noexcept;

    ClientConfig cfg_;
    SourceFactory factory_;
    actors::Actor *subscriber_;
    std::unique_ptr<Forwarder> fwd_;
    std::unique_ptr<FillSource> source_;
    uint64_t source_no_ = 0;
    bool ready_ = false, stopping_ = false;

    post_trade::TradeLedger ledger_, prev_;
    uint32_t date_ = 0, prev_date_ = 0;
    uint64_t reconciled_to_ms_ = 0; // end of the last successful window (all categories)
    int pending_ = 0;               // categories of the current reconciliation not yet done
    bool pending_ok_ = true;
    uint64_t pending_to_ = 0;
    Stats stats_;
    std::jthread reconnect_timer_, ticker_;
  };
}

// nullptr (reason on stderr) for a bad config, including a production host
// with testnet = true.
actors::Actor *create_BybitFillsClient(const bybit_pt::ClientConfig &cfg, actors::Actor *subscriber,
                                       bybit_pt::SourceFactory factory = nullptr);
actors::Actor *create_BybitFillsClient(const std::string &config_path, actors::Actor *subscriber);
