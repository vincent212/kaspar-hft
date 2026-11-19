#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// CbxDropClient: post-trade capture from the Coinbase Exchange FIX 5.0 drop
// copy (exchange/fix-api/drop-copy). NOT CERTIFIED: it has not connected to
// Coinbase; tested against a fake session and SimDropCopy only.
//
// The drop-copy session (DropCopyFlag 9406=Y on the Logon) receives an
// ExecutionReport for every order of the API key's profile, placed through
// any FIX session or REST. Each Trade report (ExecType F) becomes a
// post_trade::msg::TradeCapture, is applied to the TradeLedger, and goes to
// the subscriber if it changed the ledger: duplicates (the same ExecID
// again, a resend with PossDupFlag) never reach it. Order events (New,
// Canceled, Replaced, Restated, Expired, Status) are counted, not forwarded.
// Trade Cancel (ExecType H) and Trade Correct (G) are not in Coinbase's list
// of ExecTypes; if they arrive they are mapped to Bust and Amend.
//
// Nothing is sent on the session but the Logon and heartbeats: the drop copy
// has no acknowledgement and no request.

#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include "actors/Actor.hpp"
#include "actors/msg/Shutdown.hpp"
#include "actors/msg/Start.hpp"
#include "coinbase_exchange_oe/CbxSession.hpp"
#include "coinbase_exchange_oe/msg/CbxReport.hpp"
#include "post_trade/TradeLedger.hpp"
#include "post_trade/msg/TradeCapture.hpp"

namespace coinbase_exchange::pt
{
  struct DropConfig
  {
    oe::SessionConfig session;          // drop_copy is forced on
    // venue symbol -> (Kaspar asset id, decimals of the product's base_increment)
    struct Product
    {
      int asset = -1;
      int size_decimals = 8;
    };
    std::map<std::string, Product> products;
    int reconnect_ms = 5000;

    static bool from_file(const std::string &path, DropConfig &out, std::string &err);
  };

  // An ExecutionReport as a TradeCapture (feed.md section 7). nullopt for a
  // report that is not a trade event (New, Canceled, ...); `why` says so.
  std::optional<post_trade::msg::TradeCapture> to_trade_capture(const fix::FixMsg &er, const DropConfig &cfg,
                                                                std::string &why);

  class CbxDropClient : public actors::Actor
  {
  public:
    struct Stats
    {
      size_t reports = 0, trades = 0, order_events = 0, emitted = 0, duplicates = 0, stale = 0, orphans = 0,
             invalid = 0, unparsed = 0, logons = 0, disconnects = 0;
    };

    CbxDropClient(DropConfig cfg, oe::SessionFactory factory, actors::Actor *subscriber);
    ~CbxDropClient() override;

    const char *get_name() const override { return "CbxDropClient"; }
    const post_trade::TradeLedger &ledger() const { return ledger_; }
    const Stats &stats() const { return stats_; }
    bool logged_on() const { return logged_on_; }
    bool stopped() const { return rejected_; }

  private:
    class Events;
    void start_handler(const actors::msg::Start *) noexcept;
    void shutdown_handler(const actors::msg::Shutdown *) noexcept;
    void report_handler(const oe::msg::CbxReport *) noexcept;
    void connect() noexcept;
    void schedule_reconnect() noexcept;
    void on_report(const fix::FixMsg &m) noexcept;

    DropConfig cfg_;
    oe::SessionFactory factory_;
    actors::Actor *subscriber_;
    post_trade::TradeLedger ledger_;
    Stats stats_;
    std::unique_ptr<Events> events_;
    std::unique_ptr<oe::CbxSession> session_;
    uint64_t session_no_ = 0;
    bool logged_on_ = false, stopping_ = false, rejected_ = false;
    std::jthread reconnect_timer_;
  };
}

// nullptr with the reason on stderr for a bad configuration.
actors::Actor *create_CbxDropClient(const coinbase_exchange::pt::DropConfig &cfg, actors::Actor *subscriber,
                                    coinbase_exchange::oe::SessionFactory factory = nullptr);
actors::Actor *create_CbxDropClient(const std::string &config_path, actors::Actor *subscriber);
