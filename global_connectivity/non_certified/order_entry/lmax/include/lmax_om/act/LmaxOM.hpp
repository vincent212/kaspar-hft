/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// LmaxOM: order manager for LMAX Exchange FX over the Broker Trading FIX 4.4
// API. NOT CERTIFIED: see README.md for what has been checked.
//
// Registers with the SOM for its venue, turns the SOM's Order and Cancel into
// NewOrderSingle, OrderCancelReplaceRequest (an Order with oid_to_cancel) and
// OrderCancelRequest, and LMAX's ExecutionReports and OrderCancelRejects into
// Ack, Fill, Reject, CancAck and CancReject. Order state is kept in
// frame::som::BrokerOrderBook.
//
// Identifiers: ClOrdID is the BrokerOrderBook client id in decimal (at most
// 19 digits; LMAX allows 20 characters and requires uniqueness only among
// working orders, Broker Trading spec, ClOrdID). A cancel and a replace each
// take a new ClOrdID with OrigClOrdID the order's current one. A replace keeps
// the order's BrokerOrderBook entry; on Replaced the SOM gets CancAck for the
// old SOM order id and Ack for the new one, and later reports go to the new
// one.
//
// Quantities: Kaspar sizes are whole units of `unit` contracts per instrument
// (the instrument file's chunk; 0.1 contract = 1,000 base currency for LMAX
// spot FX, feed.md section 8). OrderQty = size x unit.
//
// Fills are counted from CumQty (14), not by adding LastQty: a fill is the
// increase of CumQty over what has been applied to the order, so a report
// resent after a reconnect, or one already covered by an OrderStatusRequest
// answer, adds nothing. ExecIDs (unique over all time) are also remembered.
//
// Reconnect: QuickFIX resumes the session's sequence numbers (FileStore) and
// LMAX resends up to 512 messages; on every logon the OM also sends an
// OrderStatusRequest for each live order (Application Recovery in the spec),
// which closes what a resend cannot (gateway failover, more than 512
// messages). Orders are cancelled by LMAX on disconnect unless
// reinstate_on_disconnect sets ExecInst H; those cancels arrive as reports.
//
// Refused locally with a Reject: at-the-open, at-the-close and extended-hours
// time in force, broker algorithms, routes other than LMAX, sizes that are
// not whole units, unknown instruments, anything over the configured limits,
// and orders while not logged on.

#include <chrono>
#include <cstdint>
#include <map>
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

#include "lmax_om/LmaxSession.hpp"
#include "lmax_om/msg/LmaxReport.hpp"

namespace lmax::om
{
  struct OmInstrument
  {
    std::string security_id; // LMAX SecurityID
    std::string symbol;
    double tick = 0;         // price increment; SOM prices are in ticks
    double unit = 0;         // contracts per Kaspar size unit
  };

  struct LmaxOMConfig
  {
    QfSessionConfig session;
    std::string account;                     // tag 1; empty: the user's default account
    std::map<int, OmInstrument> instruments; // Kaspar asset id -> LMAX instrument
    bool reinstate_on_disconnect = false;    // ExecInst H on every order
    bool status_on_logon = true;             // OrderStatusRequest per live order after each logon
    int max_order_qty = 0;                   // Kaspar units; 0: no limit
    int max_orders_per_sec = 0;              // new orders and replaces; 0: no limit
    int tick_ms = 500;                       // the actor's timer

    // key = value file (config/lmax_om_example.ini) and the key file it names.
    static bool from_file(const std::string &path, LmaxOMConfig &out, en::x &venue, std::string &err);
  };

  class LmaxOM : public actors::Actor
  {
  public:
    // trade_sink: receives an msg::ExecCopy of every trade, trade correction
    // and trade cancel report (the post-trade client); may be null.
    LmaxOM(LmaxOMConfig cfg, actors::Actor *som, SessionFactory factory, en::x venue,
           frame::som::ClientIdGen ids = frame::som::ClientIdGen::from_clock(),
           actors::Actor *trade_sink = nullptr);
    ~LmaxOM() override;
    const char *get_name() const override { return "LmaxOM"; }

    struct Counters
    {
      uint64_t logons = 0, logouts = 0;
      uint64_t sent_new = 0, sent_cancel = 0, sent_replace = 0, sent_status = 0;
      uint64_t execs = 0, duplicates = 0, unknown = 0, late = 0, invalid = 0;
      uint64_t covered_fills = 0; // trade reports whose quantity was already applied
      uint64_t status_fills = 0;  // fills learnt from an OrderStatusRequest answer
      uint64_t busts = 0;         // TradeCorrect / TradeCancel reports
      uint64_t session_rejects = 0, unparsed = 0;
    };

    // Read on the actor's thread, or while it is idle (tests).
    const frame::som::BrokerOrderBook &book() const { return book_; }
    const Counters &counters() const { return c_; }
    bool ready() const { return ready_; }
    // The ClOrdID an order (by SOM order id) is live under; empty if unknown.
    std::string clordid(int som_oid) const;
    // Contracts for `units` of instrument `sym`; false if sym is unknown.
    bool contracts(int sym, double units, std::string &out) const;

  private:
    class Events;

    struct Venue // what LMAX has said about one book entry
    {
      double cum_contracts = 0; // CumQty applied
      double notional = 0;      // sum of applied qty x price, contracts
      std::string order_id;     // LMAX OrderID
    };

    void start_handler(const actors::msg::Start *) noexcept;
    void shutdown_handler(const actors::msg::Shutdown *) noexcept;
    void order_handler(const frame::som::msg::Order *) noexcept;
    void cancel_handler(const frame::som::msg::Cancel *) noexcept;
    void report_handler(const msg::LmaxReport *) noexcept;

    void replace(const frame::som::msg::Order *m) noexcept;
    void on_exec(const ExecReport &r) noexcept;
    void on_cancel_reject(const CancelReject &r) noexcept;
    void apply_cum(frame::som::Entry &e, const OmInstrument &in, const ExecReport &r, bool from_status) noexcept;
    void ack(frame::som::Entry &e) noexcept;
    void request_status() noexcept;
    bool take_token() noexcept;
    void reject(actors::Actor *to, int oid, int reason, const char *why) noexcept;
    void cancel_reject(actors::Actor *to, int oid, int reason, const char *why) noexcept;
    int som_id(int book_oid) const;
    const OmInstrument *instrument(int sym) const;
    static uint64_t xordid(const std::string &order_id);
    static bool parse_cid(const std::string &s, frame::som::ClientId &cid);

    LmaxOMConfig cfg_;
    actors::Actor *som_;
    SessionFactory factory_;
    en::x venue_;
    actors::Actor *sink_;
    frame::som::BrokerOrderBook book_;

    std::unordered_map<int, int> som_id_;   // book oid -> SOM oid reported to (changes on replace)
    std::unordered_map<int, int> book_oid_; // SOM oid -> book oid
    std::unordered_map<int, frame::som::msg::Order> replace_; // book oid -> the replacing Order in flight
    std::unordered_map<int, Venue> venue_state_;              // book oid -> LMAX's view
    std::unordered_set<std::string> exec_ids_;

    std::unique_ptr<Events> events_;
    std::unique_ptr<LmaxSession> session_;
    uint64_t session_no_ = 0;
    bool ready_ = false;
    bool stopping_ = false;
    uint64_t next_status_ = 1;
    Counters c_;

    double tokens_ = 0;
    std::chrono::steady_clock::time_point refilled_{};
  };
}

// nullptr, with the reason on stderr, for a bad config.
actors::Actor *create_LmaxOM(const std::string &config_path, actors::Actor *som,
                             actors::Actor *trade_sink = nullptr);
