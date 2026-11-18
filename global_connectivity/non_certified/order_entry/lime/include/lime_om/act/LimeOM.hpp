/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// LimeOM: order manager for US equities through Lime (Lime Trading Corp.,
// "Lime Direct") over the Lime C++ Trading API. NOT CERTIFIED: it has never
// connected to Lime; the vendor SDK is not public and it has never been
// built against it.
//
// Registers with the SOM for its venue, turns the SOM's Order and Cancel into
// placeOrder / placeAlgoOrder and cancelOrder calls, and the API's callbacks
// into Ack, Fill, Reject, CancAck and CancReject for the order's sender.
// Order state is kept in frame::som::BrokerOrderBook.
//
// Identifiers: the API takes a client OrderId (uint64) that must be unique
// for the account for the day (TO 4.1). The book's client id (milliseconds
// since midnight << 36 | counter, 63 bits) is used as is, so a restart does
// not reuse one. A cancel names the order by its own id; Lime assigns the
// cancel's id (TO 4.5); every callback carries the order's id.
//
// Events and replay: every callback carries an eventId, increasing through
// the day (TO 3). The OM keeps the highest one applied and drops a callback
// at or below it; fills are also checked by execId. After a connection
// failure the session object is dead: the OM destroys it on its own thread
// and constructs a new one with eventId = highest applied + 1, so Lime
// replays what was missed. The first session of the process uses
// first_event_id (0: no replay).
//
// Threads: the session is created, called and destroyed on this actor's
// thread (the API must be destroyed on the thread that built it, and never
// called from a callback, TO 2). Callbacks only copy into a LimeEvent and
// send() it here.
//
// Refused locally, with a Reject: stop and stop-limit orders, market orders
// unless allowed, time in force FOK and GTC, a locate id (the API has no
// field for it), short sales when short selling is disabled, cancel/replace
// from the SOM, an order with no route, algo strategies not in the allowed
// list, and anything over the configured limits.

#include <chrono>
#include <cstdint>
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

#include "lime_om/LimeApi.hpp"
#include "lime_om/SimLime.hpp"
#include "lime_om/msg/LimeEvent.hpp"

namespace lime
{
  struct LimeOMConfig
  {
    // Session. Credentials come from a deployment file outside the
    // repository (LimeConfig.cpp); they are never compiled in.
    std::string account, user, password;
    std::string host;              // empty: the Lime platform on this host
    bool infiniband = false;
    bool cancel_all_on_disconnect = true;
    uint64_t first_event_id = 0;   // first session of the process: 0 = no replay, 1 = whole day
    int reconnect_ms = 2000;

    // Orders.
    std::string default_route;     // used when Order::route is empty; required
    std::string algo_route;        // route for orders with a strategy (e.g. LSS, LARE)
    std::set<std::string> algo_strategies{"VWAP", "TWAP", "IS", "CLOSE"}; // allowed strategies
    bool allow_market = false;     // market orders (price 0)
    bool allow_short = true;       // SHORT and SHORT_EXEMPT sells
    bool sale_affirm = false;      // set saleAffirm on sells (DVP accounts, FIX 9009/9010)
    bool regular_session_only = false; // day orders not eligible pre-market (FIX 9066)

    // Local risk limits, checked before an order is sent. 0 = no limit.
    int max_order_qty = 0;
    double max_order_notional = 0; // dollars, limit orders
    int max_orders_per_sec = 0;    // new orders; cancels are never throttled

    // Simulator instead of the SDK: create_LimeOM uses SimLime.
    bool sim = false;
    SimLime::Config sim_config;

    // Reads a key = value file (config/lime_om_example.ini). venue is the
    // en::x code named by the file's `venue` key (LIME by default).
    static bool from_file(const std::string &path, LimeOMConfig &out, en::x &venue, std::string &err);
    bool valid(std::string &err) const;
  };

  class LimeOM : public actors::Actor
  {
  public:
    LimeOM(LimeOMConfig cfg, actors::Actor *som, SessionFactory factory, en::x venue = en::x::LIME,
           frame::som::ClientIdGen ids = frame::som::ClientIdGen::from_clock());
    ~LimeOM() override;

    const char *get_name() const override { return "LimeOM"; }

    // Read-only views for tests and the console.
    const frame::som::BrokerOrderBook &book() const { return book_; }
    bool logged_in() const { return logged_in_; }
    bool busy() const { return busy_; }
    bool down() const { return down_; }
    uint64_t last_event_id() const { return last_event_; }
    int sessions_created() const { return sessions_created_; }
    uint64_t login_event_id() const { return login_event_id_; } // eventId the last session was built with

    // SOM price in ticks to a Lime scaled price.
    static ScaledPrice to_scaled(int ticks, double tick_size);

  private:
    class Events; // SessionEvents -> LimeEvent -> send()

    void start_handler(const actors::msg::Start *) noexcept;
    void shutdown_handler(const actors::msg::Shutdown *) noexcept;
    void order_handler(const frame::som::msg::Order *) noexcept;
    void cancel_handler(const frame::som::msg::Cancel *) noexcept;
    void event_handler(const msg::LimeEvent *) noexcept;

    bool fresh(const msg::LimeEvent &m) noexcept; // false: already applied
    void on_accepted(const msg::LimeEvent &m) noexcept;
    void on_filled(const msg::LimeEvent &m) noexcept;
    void on_canceled(const msg::LimeEvent &m) noexcept;
    void on_rejected(const msg::LimeEvent &m) noexcept;
    void on_cancel_rejected(const msg::LimeEvent &m) noexcept;

    void connect() noexcept;
    void drop_session() noexcept;
    void schedule_reconnect() noexcept;
    bool take_token() noexcept;
    void reject(actors::Actor *to, int oid, int reason, const char *why) noexcept;
    void cancel_reject(actors::Actor *to, int oid, int reason, const char *why) noexcept;

    LimeOMConfig cfg_;
    actors::Actor *som_;
    SessionFactory factory_;
    en::x venue_;
    frame::som::BrokerOrderBook book_;

    std::unordered_map<OrderId, std::unordered_set<std::string>> exec_ids_; // fills applied, per order

    std::unique_ptr<Events> events_;
    std::unique_ptr<LimeSession> session_;
    uint64_t session_no_ = 0; // bumps per session; events from older ones are dropped
    int sessions_created_ = 0;
    bool logged_in_ = false;
    bool busy_ = false;
    bool stopping_ = false;
    bool down_ = false;       // login refused: not retried until restart
    uint64_t last_event_ = 0; // highest eventId applied, across sessions
    uint64_t login_event_id_ = 0;

    double tokens_ = 0;
    std::chrono::steady_clock::time_point refilled_{};

    std::jthread reconnect_timer_;
  };
}

// The SDK session, or SimLime when cfg.sim. nullptr (reason on stderr) for a
// bad config, or when the SDK is wanted and this build has none, so the
// venue stays unrouted instead of stopping kaspar.
actors::Actor *create_LimeOM(const lime::LimeOMConfig &cfg, actors::Actor *som, en::x venue = en::x::LIME,
                             lime::SessionFactory factory = nullptr);

// Reads the config file first; nullptr if it cannot be read or is bad.
actors::Actor *create_LimeOM(const std::string &config_path, actors::Actor *som);
