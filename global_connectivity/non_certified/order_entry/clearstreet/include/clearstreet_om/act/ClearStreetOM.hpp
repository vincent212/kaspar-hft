/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// ClearStreetOM: order manager for Clear Street US equities over its FIX 4.2
// order-entry API. NOT CERTIFIED and never connected to Clear Street: see
// README.md for what has been checked.
//
// Registers with the SOM for its venue, turns the SOM's Order and Cancel into
// NewOrderSingle, OrderCancelReplaceRequest (an Order with oid_to_cancel) and
// OrderCancelRequest, and Clear Street's ExecutionReports, OrderCancelRejects
// and session Rejects into Ack, Fill, Reject, CancAck and CancReject. Order
// state is kept in frame::som::BrokerOrderBook.
//
// Identifiers: ClOrdID = <prefix><YYYYMMDD>-<client id>, the client id from
// BrokerOrderBook in decimal and the UTC date the OM started. Clear Street
// requires a ClOrdID never to be reused, "even after reconnecting or on a
// later trading day" (guide, Submitting an order); BrokerOrderBook ids are
// unique only within a day, so the date makes them unique forever. Cancel and
// replace take a new ClOrdID with OrigClOrdID the order's current one.
// OrderID (37) is a UUID-like string; the SOM's xordid is its 64-bit FNV-1a
// hash, and the string is logged.
//
// Fills are counted from CumQty (14), not by adding LastShares: a fill is the
// increase of CumQty over what has been applied, so a report resent after a
// reconnect adds nothing. ExecIDs are also remembered.
//
// Sides: Clear Street accepts only 54=1 and 54=2 and decides the position
// effect itself. A SOM short sale is sent as 54=2 with OpenClose 77=O when
// mark_short_open is set (Clear Street rejects the order if it would close a
// long instead, changelog 2026-10-10); a short-exempt sale and an order
// carrying a locate id are refused locally (feed.md section 4).
//
// Refused locally with a Reject: anything Clear Street's order-type, time in
// force, strategy and extended-hours matrices reject; unknown instruments;
// sizes that are not whole shares; anything over the configured limits; and
// orders while not logged on.

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "actors/Actor.hpp"
#include "actors/msg/Shutdown.hpp"
#include "actors/msg/Start.hpp"
#include "enum/e_names.hpp"
#include "frame/som/BrokerOrderBook.hpp"
#include "frame/som/msg/Cancel.hpp"
#include "frame/som/msg/Order.hpp"

#include "clearstreet_om/CsSession.hpp"
#include "clearstreet_om/msg/CsReport.hpp"

namespace clearstreet::om
{
  struct CsInstrument
  {
    std::string symbol; // Clear Street ticker (55), case-sensitive
    double tick = 0.01; // SOM prices are in ticks of this
  };

  struct ClearStreetOMConfig
  {
    std::string environment = "sim";             // sim, uat or production
    QfSessionConfig session;                     // used unless environment = sim
    std::string account;                         // tag 1, required
    std::map<int, CsInstrument> instruments;     // Kaspar asset id -> instrument
    std::string clordid_prefix;                  // optional, letters and digits
    bool mark_short_open = true;                 // SOM SHORT -> 54=2 + 77=O; false: refuse shorts
    int max_order_qty = 0;                       // shares; 0: no limit
    double max_notional = 0;                     // price x shares, dollars; 0: no limit
    int max_orders_per_sec = 0;                  // new orders and replaces; 0: no limit
    std::set<std::string> dma_destinations;      // MICs accepted with strategy DMA (guide list by default)

    // key = value file (config/clearstreet_om_example.ini) and the files it names.
    static bool from_file(const std::string &path, ClearStreetOMConfig &out, en::x &venue, std::string &err);
    static std::set<std::string> guide_mics();
  };

  class ClearStreetOM : public actors::Actor
  {
  public:
    ClearStreetOM(ClearStreetOMConfig cfg, actors::Actor *som, SessionFactory factory, en::x venue = en::x::CLEARSTREET,
                  frame::som::ClientIdGen ids = frame::som::ClientIdGen::from_clock(),
                  std::string date = utc_date());
    ~ClearStreetOM() override;
    const char *get_name() const override { return "ClearStreetOM"; }

    struct Counters
    {
      uint64_t logons = 0, logouts = 0;
      uint64_t sent_new = 0, sent_cancel = 0, sent_replace = 0;
      uint64_t execs = 0, duplicates = 0, unknown = 0, late = 0, invalid = 0;
      uint64_t busts = 0;            // ExecTransType other than New: not a fill
      uint64_t covered_fills = 0;    // fill reports whose quantity was already applied
      uint64_t session_rejects = 0, unparsed = 0;
      uint64_t throttled = 0;
      uint64_t account_not_listed = 0; // logons whose account list lacked the configured account
    };

    // Read on the actor's thread, or while it is idle (tests).
    const frame::som::BrokerOrderBook &book() const { return book_; }
    const Counters &counters() const { return c_; }
    bool ready() const { return ready_; }
    // The ClOrdID an order (by SOM order id) is live under; empty if unknown.
    std::string clordid(int som_oid) const;
    std::string clordid_of(frame::som::ClientId cid) const;
    // The client id in a ClOrdID this OM made today; false otherwise.
    bool parse_clordid(const std::string &s, frame::som::ClientId &cid) const;
    static uint64_t xordid(const std::string &order_id);

  private:
    class Events;

    struct Venue // what Clear Street has said about one book entry
    {
      double cum = 0;      // CumQty applied
      double notional = 0; // sum of applied qty x price
      std::string order_id;
    };

    void start_handler(const actors::msg::Start *) noexcept;
    void shutdown_handler(const actors::msg::Shutdown *) noexcept;
    void order_handler(const frame::som::msg::Order *) noexcept;
    void cancel_handler(const frame::som::msg::Cancel *) noexcept;
    void report_handler(const msg::CsReport *) noexcept;

    // Fills NewOrderReq from a SOM order; returns the refusal, or nullptr.
    const char *describe(const frame::som::msg::Order &m, const CsInstrument &in, NewOrderReq &q) const noexcept;
    void replace(const frame::som::msg::Order *m, const CsInstrument &in) noexcept;
    void on_logon(const std::string &text) noexcept;
    void on_exec(const ExecReport &r) noexcept;
    void on_cancel_reject(const CancelReject &r) noexcept;
    void on_session_reject(const SessionReject &r) noexcept;
    void apply_cum(frame::som::Entry &e, const CsInstrument &in, const ExecReport &r) noexcept;
    void canceled(frame::som::Entry &e, frame::som::ClientId cid, const ExecReport &r, const char *why) noexcept;
    void finish_replace(frame::som::Entry &e, bool replaced) noexcept;
    void ack(frame::som::Entry &e) noexcept;
    bool take_token() noexcept;
    void reject(actors::Actor *to, int oid, int reason, const char *why) noexcept;
    void cancel_reject(actors::Actor *to, int oid, int reason, const char *why) noexcept;
    int som_id(int book_oid) const;
    const CsInstrument *instrument(int sym) const;

    ClearStreetOMConfig cfg_;
    actors::Actor *som_;
    SessionFactory factory_;
    en::x venue_;
    frame::som::BrokerOrderBook book_;
    std::string id_head_; // <prefix><YYYYMMDD>-

    std::unordered_map<int, int> som_id_;   // book oid -> SOM oid reported to (changes on replace)
    std::unordered_map<int, int> book_oid_; // SOM oid -> book oid
    std::unordered_map<int, frame::som::msg::Order> replace_; // book oid -> the replacing Order in flight
    std::unordered_map<int, Venue> venue_state_;              // book oid -> Clear Street's view
    std::unordered_set<std::string> exec_ids_;

    std::unique_ptr<Events> events_;
    std::unique_ptr<CsSession> session_;
    uint64_t session_no_ = 0;
    bool ready_ = false;
    bool stopping_ = false;
    Counters c_;

    double tokens_ = 0;
    std::chrono::steady_clock::time_point refilled_{};
  };
}

// nullptr, with the reason on stderr, for a bad config.
actors::Actor *create_ClearStreetOM(const std::string &config_path, actors::Actor *som);
