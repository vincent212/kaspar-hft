/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// OuchOM: order manager for Nasdaq U.S. equities over OUCH 5.0 and
// SoupBinTCP 3.0. NOT CERTIFIED: it has never connected to Nasdaq.
//
// Registers with the SOM for its venue (en::x::NASDAQ), turns the SOM's
// Order and Cancel into Enter Order and Cancel Order requests, and Nasdaq's
// reports into Ack, Fill, Reject, CancAck and CancReject for the order's
// sender. Order state is kept in frame::som::BrokerOrderBook.
//
// Identifiers: OUCH identifies an order by its UserRefNum, a 32-bit number
// that must be day-unique and strictly increasing per OUCH account (OUCH 5.0
// 1.2). BrokerOrderBook's client ids are 63-bit, so OuchOM maps each order's
// client id to the UserRefNum it was entered with. After every login it asks
// Nasdaq for the next available UserRefNum (Account Query, OUCH 5.0 2.8) and
// accepts no order until the answer arrives, so a restart never reuses one.
// Cancels name the order's own UserRefNum and take no new one.
//
// Threads: the session is created, used and destroyed on this actor's
// thread. The session's thread only decodes each report into an OuchReport
// and send()s it here.
//
// Reconnect: a lost connection is reopened after reconnect_ms, logging in to
// the same SoupBinTCP session at the sequence number after the last report
// applied, so reports sent while disconnected arrive exactly once. Reports
// at a sequence number already applied are skipped, and fills are also
// checked against the Match Numbers already applied to the order.
//
// What is refused locally, with a Reject: stop and stop-limit orders (OUCH
// has none), GTC and FOK, broker algorithms, routes other than Nasdaq,
// cancel/replace, quantities of 1,000,000 or more, prices above $199,999.99,
// and anything over the configured limits.

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "actors/Actor.hpp"
#include "actors/msg/Shutdown.hpp"
#include "actors/msg/Start.hpp"
#include "enum/e_names.hpp"
#include "frame/som/BrokerOrderBook.hpp"
#include "frame/som/msg/Cancel.hpp"
#include "frame/som/msg/Order.hpp"

#include "nasdaq_ouch/OuchSession.hpp"
#include "nasdaq_ouch/SimOuch.hpp"
#include "nasdaq_ouch/msg/OuchReport.hpp"

namespace nasdaq::ouch
{
  struct OuchOMConfig
  {
    // Session. Credentials are read from a deployment file outside the
    // repository; they are never compiled in.
    SessionConfig session;
    std::string username, password;
    uint64_t first_sequence = 0; // SoupBinTCP sequence asked for on the first login of the process
    int reconnect_ms = 2000;

    // Order defaults (OUCH 5.0 2.1).
    std::string firm;       // Firm option (4 characters); empty: the account's default firm
    char capacity = 'A';    // A agency, P principal, R riskless, O other
    char display = 'Y';     // Y visible, N hidden, A attributable
    char intermarket_sweep = 'N';

    // Local risk limits, checked before an order is sent. 0 = no limit.
    int max_order_qty = 0;
    double max_order_notional = 0; // dollars
    int max_orders_per_sec = 0;    // new orders; cancels are never throttled

    // Simulator instead of SoupBinTCP: create_OuchOM uses SimOuch.
    bool sim = false;
    SimOuch::Config sim_config;

    // Reads a key = value file (see config/ouch_example.ini). Returns false
    // with the reason in err on a missing or bad value.
    static bool from_file(const std::string &path, OuchOMConfig &out, std::string &err);
    // Checks the values; false with the reason in err.
    bool valid(std::string &err) const;
  };

  class OuchOM : public actors::Actor
  {
  public:
    OuchOM(OuchOMConfig cfg, actors::Actor *som, SessionFactory factory,
           en::x venue = en::x::NASDAQ,
           frame::som::ClientIdGen ids = frame::som::ClientIdGen::from_clock());
    ~OuchOM() override;

    const char *get_name() const override { return "OuchOM"; }

    // Read-only views for tests and the console.
    const frame::som::BrokerOrderBook &book() const { return book_; }
    size_t venue_orders() const { return by_urn_.size(); } // orders with a UserRefNum
    bool ready() const { return ready_; }
    bool busy() const { return busy_; }
    uint64_t next_sequence() const { return next_seq_; }
    uint32_t next_user_ref_num() const { return next_urn_; }
    int sessions_created() const { return sessions_created_; }
    uint32_t user_ref_num(int oid) const; // 0 if none

    // SOM price in ticks to OUCH price (4 decimals), and back to dollars.
    static uint64_t to_ouch_price(int ticks, double tick_size);

  private:
    class Events; // SessionEvents -> OuchReport -> send()

    struct Venue // what Nasdaq has said about one order
    {
      uint32_t urn = 0;
      frame::som::ClientId cid = 0;
      int shares = 0;      // entered, then as accepted
      int executed = 0;
      int decremented = 0; // canceled or reduced by Nasdaq
      bool acked = false;
      std::unordered_set<uint64_t> matches; // Match Numbers applied
      int open() const { return shares - executed - decremented; }
    };

    void start_handler(const actors::msg::Start *) noexcept;
    void shutdown_handler(const actors::msg::Shutdown *) noexcept;
    void order_handler(const frame::som::msg::Order *) noexcept;
    void cancel_handler(const frame::som::msg::Cancel *) noexcept;
    void report_handler(const msg::OuchReport *) noexcept;

    void on_message(const msg::OuchReport &m) noexcept;
    void on_accepted(const from_nasdaq::OrderAccepted &m) noexcept;
    void on_executed(const from_nasdaq::OrderExecuted &m) noexcept;
    void on_decrement(uint32_t urn, int shares, char reason, const char *what) noexcept;
    void on_rejected(const from_nasdaq::Rejected &m) noexcept;
    void on_cancel_reject(const from_nasdaq::CancelReject &m) noexcept;
    void venue_done(Venue &v, const char *why) noexcept;
    // The link is gone: every cancel in flight is over without an answer.
    void abandon_pending_cancels() noexcept;
    // Forget finished orders, in the book and in the UserRefNum maps.
    void purge_done() noexcept;

    Venue *venue_order(uint32_t urn) noexcept;
    void connect() noexcept;
    void drop_session() noexcept;
    void schedule_reconnect() noexcept;
    bool take_token() noexcept;
    void reject(actors::Actor *to, int oid, int reason, const char *why) noexcept;
    void cancel_reject(actors::Actor *to, int oid, int reason, const char *why) noexcept;

    OuchOMConfig cfg_;
    actors::Actor *som_;
    SessionFactory factory_;
    en::x venue_;
    frame::som::BrokerOrderBook book_;
    std::unordered_map<uint32_t, Venue> by_urn_;
    std::unordered_map<frame::som::ClientId, uint32_t> urn_of_cid_;

    std::unique_ptr<Events> events_;
    std::unique_ptr<OuchSession> session_;
    uint64_t session_no_ = 0; // bumps per session; reports from older ones are dropped
    int sessions_created_ = 0;
    bool logged_in_ = false;  // SoupBinTCP login accepted
    bool ready_ = false;      // logged in and the next UserRefNum is known
    bool busy_ = false;
    bool stopping_ = false;
    bool ended_ = false;      // End of Session: no reconnect
    std::string soup_session_;
    uint64_t next_seq_ = 0;   // next SoupBinTCP sequence number expected; 0 = none yet
    uint32_t next_urn_ = 1;

    // Token bucket for max_orders_per_sec.
    double tokens_ = 0;
    std::chrono::steady_clock::time_point refilled_{};

    std::jthread reconnect_timer_;
  };
}

// The factories, create_OuchOM, are declared in nasdaq_ouch/if/OuchOM.hpp.
