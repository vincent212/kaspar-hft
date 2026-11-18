/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// NasdaqDropClient: post-trade capture from Nasdaq DROP (feed.md).
//
// Not certified; it has never connected to Nasdaq.
//
// On Start it starts its DropSession (SoupSession over TCP, or a fake in the
// tests). The session's thread parses each DROP line and sends it here as a
// msg::Report; this actor maps executions, corrections and breaks to
// post_trade::msg::TradeCapture, applies them to its TradeLedger, and sends
// the subscriber one TradeCapture for each event that changed the ledger.
// Duplicates (a replay of the day at logon, an overlap after a reconnect) are
// dropped by the ledger and never reach the subscriber. Order events (accept,
// cancel, replace) are counted and not forwarded.
//
// DROP has no application acknowledgement (DROP 2.20; SoupBinTCP 3.00): the
// session acknowledges implicitly by the sequence number it requests when it
// logs in again, and sends Client Heartbeats. Nothing else is sent.

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "actors/Actor.hpp"
#include "actors/msg/Shutdown.hpp"
#include "actors/msg/Start.hpp"
#include "nasdaq_post_trade/DropSession.hpp"
#include "nasdaq_post_trade/SimDropServer.hpp"
#include "nasdaq_post_trade/SoupSession.hpp"
#include "nasdaq_post_trade/msg/SessionEvents.hpp"
#include "post_trade/TradeLedger.hpp"
#include "post_trade/msg/TradeCapture.hpp"

namespace nasdaq_post_trade
{
  struct ClientConfig
  {
    SoupSessionConfig soup;
    LoginParams login;                // credentials, session, first sequence number
    std::map<std::string, int> assets; // venue symbol -> Kaspar asset id
    uint32_t trade_date = 0;          // YYYYMMDD; 0: the Eastern Time date at login

    // In-process simulator instead of Nasdaq (SimDropServer on 127.0.0.1).
    bool sim = false;
    int sim_trade_ms = 0; // 0: no generated trading
    std::vector<std::string> sim_symbols;
  };
}

namespace nasdaq_post_trade::act
{
  class NasdaqDropClient : public actors::Actor
  {
  public:
    enum class State
    {
      Idle,       // not started, or shut down
      Connecting, // started, not logged in
      LoggedIn,
      Down,       // connection lost; the session is reconnecting
      Rejected,   // login rejected: stopped
      Ended,      // end of session: stopped
    };

    struct Stats
    {
      size_t lines = 0;        // DROP lines received
      size_t parse_errors = 0;
      size_t order_events = 0; // A, X, U, Y: not trades
      size_t emitted = 0;      // TradeCapture messages sent to the subscriber
      size_t duplicates = 0;
      size_t stale = 0;
      size_t orphans = 0;
      size_t invalid = 0;
      size_t gaps = 0;         // a sequence number past the one expected
      size_t logins = 0;
      size_t disconnects = 0;
    };

    // session: the connection (owned). subscriber: receives TradeCapture.
    // sim: an in-process simulator the session connects to, owned so that it
    // lives as long as the client.
    NasdaqDropClient(ClientConfig cfg, std::unique_ptr<DropSession> session,
                     actors::Actor *subscriber, std::unique_ptr<SimDropServer> sim = nullptr);
    ~NasdaqDropClient() override;

    const char *get_name() const override { return "NasdaqDropClient"; }

    // Read these only from the actor's thread, or when it is idle (tests).
    const post_trade::TradeLedger &ledger() const { return ledger_; }
    const Stats &stats() const { return stats_; }
    State state() const { return state_; }
    const std::string &session() const { return day_session_; }
    uint64_t next_seq() const { return expected_next_; }
    SimDropServer *simulator() { return sim_.get(); }

  private:
    // Runs on the session's thread: parses, then sends to the actor.
    class Forwarder : public DropSessionListener
    {
    public:
      explicit Forwarder(NasdaqDropClient *a) : a_(a) {}
      void on_login_accepted(const std::string &session, uint64_t next_seq) override;
      void on_login_rejected(char reason) override;
      void on_line(const std::string &session, uint64_t seq, std::string_view payload) override;
      void on_end_of_session(const std::string &session) override;
      void on_disconnected(const std::string &reason, bool will_retry) override;

    private:
      NasdaqDropClient *a_;
    };

    void on_start(const actors::msg::Start *);
    void on_shutdown(const actors::msg::Shutdown *);
    void on_login_accepted(const msg::LoginAccepted *m);
    void on_login_rejected(const msg::LoginRejected *m);
    void on_report(const msg::Report *m);
    void on_end_of_session(const msg::EndOfSession *m);
    void on_disconnected(const msg::Disconnected *m);

    ClientConfig cfg_;
    std::unique_ptr<SimDropServer> sim_; // before session_: outlives it
    std::unique_ptr<DropSession> session_;
    actors::Actor *subscriber_;
    Forwarder fwd_{this};

    post_trade::TradeLedger ledger_;
    Stats stats_;
    State state_ = State::Idle;
    std::string day_session_;     // SoupBinTCP session of the trading day held in the ledger
    uint32_t trade_date_ = 0;
    uint64_t expected_next_ = 0;  // sequence number of the next line
    uint64_t max_seq_seen_ = 0;
  };
}
