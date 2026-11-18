#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// BybitOM: order manager for Bybit V5 spot and linear perpetuals. NOT
// CERTIFIED: it has never connected to Bybit (no testnet keys were supplied).
//
// Registers with the SOM for its venue (en::x::BYBIT for spot, BYBITF for
// linear; one OM per venue code), turns the SOM's Order and Cancel into
// signed REST requests, and the private stream's `order` and `execution`
// updates into Ack, Fill, Reject, CancAck and CancReject. Order state is kept
// in frame::som::BrokerOrderBook.
//
// Identifiers: each order is sent with orderLinkId "k<YYYYMMDD>-<client id>"
// (at most 29 characters of [0-9A-Za-z-]; Bybit allows 36). The client id is
// BrokerOrderBook's, unique within a UTC day across restarts; the date makes
// the link unique across days, as Bybit requires ("always unique"). Cancels
// name the order's orderLinkId. Ack and Fill carry xordid: Bybit's orderId
// when it is a decimal number (spot), else the FNV-1a 64-bit hash of it
// (linear orderIds are UUIDs); the string is logged.
//
// Reports:
//   order New / PartiallyFilled / Untriggered   Ack, once
//   execution execType Trade                     Fill, once per execId
//   order Filled                                 Ack if none yet; fills come
//                                                from executions
//   order Cancelled / PartiallyFilledCanceled /  CancAck, after the fills the
//         Deactivated                            update counts (cumExecQty)
//                                                have arrived; if they have
//                                                not, they are fetched over
//                                                REST first
//   order Cancelled, never acked, nothing filled, Reject (a PostOnly that
//         rejectReason set                       would have crossed)
//   order Rejected                               Reject
//   REST create answer retCode != 0              Reject (10006: THROTTLE)
//   REST cancel answer retCode != 0              CancReject; the order works on
// Executions of other types (Funding, AdlTrade, BustTrade, Delivery,
// Settle) are logged, never fills. Reports for orderLinkIds this OM did not
// make are logged and ignored.
//
// Reconnect: Bybit's private stream has no replay. After every login the OM
// fetches, for each order still live in its book, the order
// (/v5/order/realtime) and its executions (/v5/execution/list); executions
// are applied once by execId, so a fill missed while disconnected arrives
// exactly once.
//
// Refused locally, with a Reject: STOP and STOP_LIMIT, AT_OPEN, AT_CLOSE,
// EXTENDED_DAY, short-sale flags, broker algorithms other than "POSTONLY",
// cancel/replace, an instrument not in the config, sizes that are not whole
// units, and anything over the configured limits. DAY is sent as GTC when
// day_as_gtc (Bybit has no day orders).
//
// Threads: the session is created, used and destroyed on this actor's thread;
// the session's threads only send BybitReports here.

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

#include "bybit_oe/BybitSession.hpp"
#include "bybit_oe/SimBybit.hpp"
#include "bybit_oe/msg/BybitReport.hpp"

namespace bybit::oe
{
  struct Instrument
  {
    std::string category; // spot | linear
    std::string symbol;   // e.g. BTCUSDT
    double tick = 0;      // price of one SOM price tick
    double qty_unit = 0;  // quantity of one SOM size unit (the qtyStep, or a multiple)
  };

  struct BybitOMConfig
  {
    SessionConfig session;
    std::string key_file = "~/.kaspar/keys/bybit_testnet.json";
    int reconnect_ms = 2000;
    std::map<int, Instrument> instruments; // SOM sym -> instrument
    bool day_as_gtc = true;

    int max_order_qty = 0;          // SOM units; 0 = no limit
    double max_order_notional = 0;  // quote currency; 0 = no limit
    int max_orders_per_sec = 10;    // Bybit's default create limit for linear

    bool sim = false;
    SimBybit::Config sim_config;

    // key = value file (config/bybit_om_example.ini); reads the key file
    // unless sim. false with the reason in err.
    static bool from_file(const std::string &path, BybitOMConfig &out, std::string &err);
    bool valid(std::string &err) const;
  };

  class BybitOM : public actors::Actor
  {
  public:
    BybitOM(BybitOMConfig cfg, actors::Actor *som, SessionFactory factory, en::x venue,
            frame::som::ClientIdGen ids = frame::som::ClientIdGen::from_clock(),
            uint32_t yyyymmdd = 0 /* 0: today, UTC */);
    ~BybitOM() override;

    const char *get_name() const override { return "BybitOM"; }

    const frame::som::BrokerOrderBook &book() const { return book_; }
    bool ready() const { return ready_; }
    bool busy() const { return busy_; }
    int sessions_created() const { return sessions_created_; }
    std::string link_of(int oid) const; // "" if none

    static std::string link_id(uint32_t yyyymmdd, frame::som::ClientId cid);
    static bool parse_link(const std::string &link, frame::som::ClientId &cid);
    static uint64_t xordid(const std::string &order_id);
    static std::string decimal(double v, double step); // v with step's decimals

  private:
    class Events;

    struct Venue // what Bybit has said about one order
    {
      std::string link, order_id, category;
      frame::som::ClientId cid = 0;
      const Instrument *ins = nullptr;
      bool acked = false;
      int filled = 0;                      // SOM units applied from executions
      int venue_cum = 0;                   // cumExecQty from the latest order update
      bool cancel_waiting = false;         // canceled at Bybit; waiting for fills
      std::unordered_set<std::string> exec_ids;
    };

    void start_handler(const actors::msg::Start *) noexcept;
    void shutdown_handler(const actors::msg::Shutdown *) noexcept;
    void order_handler(const frame::som::msg::Order *) noexcept;
    void cancel_handler(const frame::som::msg::Cancel *) noexcept;
    void report_handler(const msg::BybitReport *) noexcept;

    void on_order(const std::string &json) noexcept;
    void on_execution(const std::string &json) noexcept;
    void on_response(const msg::BybitReport &r) noexcept;
    void on_logged_in() noexcept;
    void ensure_ack(Venue &v) noexcept;
    void finish_cancel(Venue &v, const char *why) noexcept;

    void connect() noexcept;
    void drop_session() noexcept;
    void schedule_reconnect() noexcept;
    bool take_token() noexcept;
    void reject(actors::Actor *to, int oid, int reason, const char *why) noexcept;
    void cancel_reject(actors::Actor *to, int oid, int reason, const char *why) noexcept;
    Venue *venue_of(const std::string &link) noexcept;

    BybitOMConfig cfg_;
    actors::Actor *som_;
    SessionFactory factory_;
    en::x venue_;
    uint32_t date_;
    frame::som::BrokerOrderBook book_;
    std::unordered_map<std::string, Venue> by_link_;
    std::unordered_map<frame::som::ClientId, std::string> link_of_cid_;

    std::unique_ptr<Events> events_;
    std::unique_ptr<BybitSession> session_;
    uint64_t session_no_ = 0;
    int sessions_created_ = 0;
    bool ready_ = false;
    bool busy_ = false;
    bool stopping_ = false;

    double tokens_ = 0;
    std::chrono::steady_clock::time_point refilled_{};
    std::jthread reconnect_timer_;
  };
}

// The real session, or SimBybit when cfg.sim. nullptr (reason on stderr) for
// a bad config, including a production host with testnet = true.
actors::Actor *create_BybitOM(const bybit::oe::BybitOMConfig &cfg, actors::Actor *som, en::x venue,
                              bybit::oe::SessionFactory factory = nullptr);
actors::Actor *create_BybitOM(const std::string &config_path, actors::Actor *som, en::x venue);
