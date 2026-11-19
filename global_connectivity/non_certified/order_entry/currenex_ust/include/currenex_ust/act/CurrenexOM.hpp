/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// CurrenexOM: order manager for US Treasuries on Currenex over its OUCH 2.0
// binary protocol (version 200). NOT CERTIFIED: it has never connected to
// Currenex.
//
// Registers with the SOM for its venue, turns the SOM's Order and Cancel
// into NewOrder and OrderCancelRequest messages, and Currenex's reports into
// Ack, Fill, Reject, CancAck and CancReject for the order's sender. Order
// state is kept in frame::som::BrokerOrderBook.
//
// Instruments: orders name an instrument by its InstrumentIndex, which is
// valid for one session only (UST OUCH 6, p.12). After every Logon the OM
// sends an InstrumentInfoRequest and maps each outright's CUSIP (SecurityID)
// to its index; an order is sent only when its CUSIP is listed exactly once.
// Spread instruments are not traded.
//
// Identifiers: every request takes a client id (ClOrderID, a signed 32-bit
// Integer): new orders, and each cancel (NewClOrderID, naming the order by
// PrevClOrderID). The spec does not say how long they must be unique. They
// are taken from the clock as milliseconds since the start of the UTC week
// (Saturday 00:00), and kept strictly increasing, so a restart does not
// reuse one within the trading week the persistent session lives for
// (5.5, p.9). BrokerOrderBook's 63-bit client ids map to them.
//
// Sizes: SOM sizes are in units of face_per_unit dollars of face value
// (1,000,000 by default); Currenex amounts are dollars with 2 decimals.
// Trades smaller than one unit are passed to the SOM with their fractional
// size; the book counts whole units.
//
// Sequence numbers: the OM keeps the last Currenex sequence number applied,
// across connections. On a Logon response numbered beyond it, with
// persistence configured, it sends a ResendRequest and applies the replay;
// reports already applied are skipped by sequence number, and trades also by
// ExecutionID. Without persistence, or after the weekly reset, Currenex
// numbers from 1 again: what happened while disconnected is unknown, and
// orders that were live are reported cancelled (cancel on disconnect, 8.7)
// with an error to check the FIX STP drop copy.
//
// Threads: the session is created, used and destroyed on this actor's
// thread. The session's thread only decodes each report into a CxReport and
// send()s it here.
//
// Refused locally, with a Reject: anything but a limit order, time in force
// other than day, GTC or IOC (day is sent as GTC: OUCH has no day order),
// broker algorithms, routes, cancel/replace from the SOM, instruments not
// listed in this session, and anything over the configured limits.

#include <chrono>
#include <climits>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
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

#include "currenex_ust/CxSession.hpp"
#include "currenex_ust/SimCurrenex.hpp"
#include "currenex_ust/msg/CxReport.hpp"

namespace currenex::ouch
{
  // ClOrderIDs: milliseconds since the start of the UTC week, strictly
  // increasing (see above). The clock is injectable for tests.
  class ClOrderIdGen
  {
  public:
    using Clock = std::function<int64_t()>; // epoch milliseconds
    explicit ClOrderIdGen(Clock now = epoch_ms_now) : now_(std::move(now)) {}
    // 0 when the week's ids are exhausted (more than one id per millisecond
    // on average since the start of the week).
    int32_t next();
    static int64_t week_start_ms(int64_t epoch_ms); // the Saturday 00:00 UTC at or before
  private:
    Clock now_;
    int64_t week_ = 0; // start of the week of last_
    int64_t last_ = 0;
  };

  struct CurrenexOMConfig
  {
    // Session. Credentials are read from a deployment file outside the
    // repository; they are never compiled in.
    SessionConfig session;
    std::string user, password;
    int16_t version = VERSION;
    int reconnect_ms = 2000;
    // Currenex keeps outbound numbering across connections for this user
    // (UST OUCH 5.5; configured by Currenex on request).
    bool persistence = false;
    // Currenex cancels all active orders on a logout or disconnect unless
    // asked not to (8.7). When numbering restarts, orders live before it
    // are then reported cancelled; false: they are left as they were.
    bool cancel_on_disconnect = true;
    // First Logon of the process: ResendRequest from this number (0: none;
    // the reports of orders from before the restart are not wanted).
    uint32_t first_resend_from = 0;
    // On-behalf-of trading (7.1): RoutedUserKey in every order. If
    // routed_user_id is set, the key is looked up with a
    // RoutingUserInfoRequest after each Logon and no order goes out until it
    // is known.
    int32_t routed_user_key = NOT_ROUTED;
    std::string routed_user_id;

    // Units.
    double face_per_unit = 1'000'000; // dollars of face value per SOM size unit
    double min_amt = 0;               // dollars; MinAmt of every order

    // Local risk limits, checked before an order is sent. 0 = no limit.
    double max_order_face = 0;   // dollars
    int max_orders_per_sec = 0;  // new orders; cancels are never throttled

    // Simulator instead of TCP: create_CurrenexOM uses SimCurrenex.
    bool sim = false;
    SimCurrenex::Config sim_config;

    // Reads a key = value file (see config/currenex_ust_example.ini). Returns
    // false with the reason in err on a missing or bad value. venue is the
    // en::x code named by the file's `venue` key.
    static bool from_file(const std::string &path, CurrenexOMConfig &out, en::x &venue, std::string &err);
    bool valid(std::string &err) const;
  };

  class CurrenexOM : public actors::Actor
  {
  public:
    CurrenexOM(CurrenexOMConfig cfg, actors::Actor *som, SessionFactory factory, en::x venue,
               frame::som::ClientIdGen ids = frame::som::ClientIdGen::from_clock(),
               ClOrderIdGen clids = ClOrderIdGen());
    ~CurrenexOM() override;

    const char *get_name() const override { return "CurrenexOM"; }

    // Read-only views for tests and the console.
    const frame::som::BrokerOrderBook &book() const { return book_; }
    bool ready() const { return ready_; }
    bool busy() const { return busy_; }
    uint32_t next_sequence() const { return expected_; } // 0: none yet
    int sessions_created() const { return sessions_created_; }
    int32_t cl_order_id(int oid) const;  // the order's ClOrderID, 0 if none
    int16_t instrument_index(const std::string &cusip) const; // -1: not listed, -2: listed twice
    int32_t routed_user_key() const { return routed_key_; }

    // SOM price in ticks to a Currenex rate (9 decimals).
    static int64_t to_rate(int ticks, double tick_size);

  private:
    class Events; // SessionEvents -> CxReport -> send()

    struct Venue // what Currenex has said about one order
    {
      int32_t clid = 0;            // the order's ClOrderID
      frame::som::ClientId cid = 0;
      int16_t index = 0;           // InstrumentIndex when sent
      std::string cusip;
      int64_t amt = 0;             // cents
      int64_t executed = 0;        // cents
      int booked = 0;              // whole units passed to the book
      int64_t order_id = 0;
      bool acked = false;
      std::set<int32_t> pending;   // TradeLinkIDs of pending fills not yet settled
      std::unordered_set<std::string> exec_ids;
    };

    void start_handler(const actors::msg::Start *) noexcept;
    void shutdown_handler(const actors::msg::Shutdown *) noexcept;
    void order_handler(const frame::som::msg::Order *) noexcept;
    void cancel_handler(const frame::som::msg::Cancel *) noexcept;
    void report_handler(const msg::CxReport *) noexcept;

    void on_logged_in(const msg::CxReport &m) noexcept;
    void on_logged_out(const msg::CxReport &m) noexcept;
    bool sequence_ok(uint32_t seq) noexcept;   // false: already applied
    void on_gap_fill(uint32_t seq, const from::GapFill &g) noexcept;
    void on_message(const msg::CxReport &r) noexcept;
    void on_new_order_ack(const from::NewOrderAck &m) noexcept;
    void on_cancelled(const from::OrderCancelledOrExpired &m) noexcept;
    void on_cancel_reject(const from::OrderCancelReject &m) noexcept;
    void on_pending(int32_t clid, int32_t link, int64_t amt, bool cancel) noexcept;
    void on_trade(int32_t clid, const std::string &exec_id, int64_t amt, int64_t rate, int64_t leaves,
                  int64_t transact_ms, int32_t link, const char *what) noexcept;
    void on_instrument(const from::OutrightInstrumentInfo &m) noexcept;
    void venue_done(Venue &v, const char *why) noexcept;
    void session_reset() noexcept;

    Venue *venue_order(int32_t any_clid) noexcept;
    void connect() noexcept;
    void drop_session() noexcept;
    void schedule_reconnect() noexcept;
    bool take_token() noexcept;
    void update_ready() noexcept;
    void reject(actors::Actor *to, int oid, int reason, const char *why) noexcept;
    void cancel_reject(actors::Actor *to, int oid, int reason, const char *why) noexcept;

    CurrenexOMConfig cfg_;
    actors::Actor *som_;
    SessionFactory factory_;
    en::x venue_;
    frame::som::BrokerOrderBook book_;
    ClOrderIdGen clids_;
    int64_t unit_cents_;
    std::unordered_map<int32_t, Venue> by_clid_;           // the order's ClOrderID -> order
    std::unordered_map<int32_t, int32_t> owner_;           // any ClOrderID used -> the order's ClOrderID
    std::unordered_map<int32_t, frame::som::ClientId> book_cid_; // any ClOrderID used -> book client id
    std::unordered_map<frame::som::ClientId, int32_t> clid_of_cid_;

    // Instruments of the current session (UST OUCH 6): CUSIP -> index, -2 if listed twice.
    std::unordered_map<std::string, int16_t> index_of_cusip_;
    int instruments_listed_ = 0;

    std::unique_ptr<Events> events_;
    std::unique_ptr<CxSession> session_;
    uint64_t session_no_ = 0; // bumps per session; reports from older ones are dropped
    int sessions_created_ = 0;
    bool logged_in_ = false;
    bool ready_ = false;      // logged in and the routed user (if any) known
    bool busy_ = false;
    bool stopping_ = false;
    bool down_ = false;       // a logout reason that retrying will not cure: no reconnect
    int32_t routed_key_;      // UNKNOWN_KEY until looked up, when routed_user_id is set
    static constexpr int32_t UNKNOWN_KEY = INT32_MIN;

    // Sequencing of Currenex's outbound messages.
    uint32_t expected_ = 0;   // next sequence number expected; 0 = none yet
    uint32_t logon_seq_ = 0;  // sequence number of the current Logon response
    std::set<uint32_t> ahead_; // applied out of order (live messages during a replay)

    // Token bucket for max_orders_per_sec.
    double tokens_ = 0;
    std::chrono::steady_clock::time_point refilled_{};

    std::jthread reconnect_timer_;
  };
}

// The real TCP session, or SimCurrenex when cfg.sim. nullptr (reason on
// stderr) for a bad config, so the venue stays unrouted instead of stopping
// kaspar.
actors::Actor *create_CurrenexOM(const currenex::ouch::CurrenexOMConfig &cfg, actors::Actor *som, en::x venue,
                                 currenex::ouch::SessionFactory factory = nullptr);

// Reads the config file first (venue from its `venue` key); nullptr if it
// cannot be read or is bad.
actors::Actor *create_CurrenexOM(const std::string &config_path, actors::Actor *som);
