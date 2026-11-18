#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// KrakenOM: order manager for Kraken. NOT CERTIFIED: it has never sent an
// order to Kraken (README.md says what was and was not checked).
//
// Registers with the SOM for its venue (en::x::KRAKEN for spot, KRAKENF for
// the futures demo), turns the SOM's Order and Cancel into the session's
// add_order and cancel_order, and the session's Reports into Ack, Fill,
// Reject, CancAck and CancReject for the order's sender. Order state is kept
// in frame::som::BrokerOrderBook.
//
// Identifiers. The client order id is the BrokerOrderBook client id in base
// 36 (at most 13 characters, inside spot's 18-character free-text limit),
// after a configured prefix (the futures demo uses the date, since futures
// client ids must be unique forever). Reports carry the client id, the venue
// order id, or both; the OM learns the pairing from whichever report carries
// both, and holds reports that name only an unknown venue id until it does.
// The venue order id is text (OTCFZG-YOE2Q-LQKNM3, or a UUID on futures); Ack
// carries its 64-bit FNV-1a hash as xordid.
//
// Quantities. SOM sizes are whole units of qty_unit (default 1e-8, Kraken's
// BTC lot); prices are SOM ticks times the asset's tick.
//
// Fills are counted once per exec_id. A reconnect resubscribes with the last
// trades in the snapshot, so a fill missed while disconnected arrives then,
// and one already applied is skipped.
//
// Refused locally, with a Reject: stop orders, DAY unless day_as_gtc, at the
// open/close, extended hours, short-sale marks, broker algorithms, routes
// other than KRAKEN, cancel/replace, sizes that are not whole units, and
// anything over the configured limits.
//
// Threads: the session is created, used and destroyed on this actor's thread;
// its own thread only send()s KrakenEvent messages here.

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "actors/Actor.hpp"
#include "actors/msg/Shutdown.hpp"
#include "actors/msg/Start.hpp"
#include "enum/e_names.hpp"
#include "frame/som/BrokerOrderBook.hpp"
#include "frame/som/msg/Cancel.hpp"
#include "frame/som/msg/Order.hpp"

#include "kraken_oe/KrakenSession.hpp"
#include "kraken_oe/msg/KrakenEvent.hpp"

namespace kraken::oe
{
  struct KrakenOMConfig
  {
    std::map<std::string, std::string> symbols; // Kaspar asset name -> venue symbol; absent: the asset name
    double qty_unit = 1e-8;  // venue quantity of one SOM unit
    std::string clid_prefix; // prepended to every client order id
    bool day_as_gtc = true;  // SOM DAY orders go out GTC (crypto has no trading day); false: refused
    bool post_only = false;  // every limit order post-only
    int reconnect_ms = 2000;

    // Local risk limits. 0 = no limit.
    int64_t max_order_units = 0;
    double max_order_notional = 0; // quote currency
    int max_orders_per_sec = 0;    // new orders; cancels are never throttled

    bool valid(std::string &err) const;
  };

  class KrakenOM : public actors::Actor
  {
  public:
    KrakenOM(KrakenOMConfig cfg, actors::Actor *som, SessionFactory factory, en::x venue = en::x::KRAKEN,
             frame::som::ClientIdGen ids = frame::som::ClientIdGen::from_clock());
    ~KrakenOM() override;

    const char *get_name() const override { return "KrakenOM"; }

    // Base 36, lower case.
    static std::string encode_cid(frame::som::ClientId cid);
    static bool decode_cid(const std::string &s, frame::som::ClientId &cid);
    static uint64_t fnv1a(const std::string &s);
    // A decimal with `decimals` places, e.g. (12345, 1e-4) -> "1.2345".
    static std::string decimal(int64_t units, double unit);

    // Read-only views for tests and the console.
    const frame::som::BrokerOrderBook &book() const { return book_; }
    bool ready() const { return ready_; }
    bool busy() const { return busy_; }
    int sessions_created() const { return sessions_created_; }
    std::string client_order_id(int oid) const; // "" if none
    size_t parked() const;

  private:
    class Events;

    struct Venue
    {
      frame::som::ClientId cid = 0;
      std::string cl_ord_id, order_id;
      std::unordered_set<std::string> execs; // exec_ids applied
      bool acked = false;
      double tick = 0;
    };

    void start_handler(const actors::msg::Start *) noexcept;
    void shutdown_handler(const actors::msg::Shutdown *) noexcept;
    void order_handler(const frame::som::msg::Order *) noexcept;
    void cancel_handler(const frame::som::msg::Cancel *) noexcept;
    void event_handler(const msg::KrakenEvent *) noexcept;

    void on_report(const Report &r) noexcept;
    Venue *resolve(const Report &r) noexcept;
    void apply(Venue &v, const Report &r) noexcept;
    void connect() noexcept;
    void drop_session() noexcept;
    void schedule_reconnect() noexcept;
    bool take_token() noexcept;
    void reject(actors::Actor *to, int oid, int reason, const char *why) noexcept;
    void cancel_reject(actors::Actor *to, int oid, int reason, const char *why) noexcept;

    KrakenOMConfig cfg_;
    actors::Actor *som_;
    SessionFactory factory_;
    en::x venue_;
    frame::som::BrokerOrderBook book_;
    std::unordered_map<std::string, Venue> by_clid_;              // cl_ord_id -> venue state
    std::unordered_map<std::string, std::string> clid_of_order_;  // venue order id -> cl_ord_id
    std::unordered_map<std::string, std::vector<Report>> parked_; // reports for an unknown venue order id

    std::unique_ptr<Events> events_;
    std::unique_ptr<KrakenSession> session_;
    uint64_t session_no_ = 0;
    int sessions_created_ = 0;
    bool ready_ = false;
    bool busy_ = false;
    bool stopping_ = false;
    bool login_failed_ = false;

    double tokens_ = 0;
    std::chrono::steady_clock::time_point refilled_{};
    std::jthread reconnect_timer_;
  };
}

// nullptr (reason on stderr) for a bad config or no factory, so Kraken stays
// unrouted instead of stopping kaspar.
actors::Actor *create_KrakenOM(const kraken::oe::KrakenOMConfig &cfg, actors::Actor *som,
                               kraken::oe::SessionFactory factory, en::x venue = en::x::KRAKEN);
