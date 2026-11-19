#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// DeribitOM: order manager for Deribit futures and perpetuals over the
// JSON-RPC WebSocket. NOT CERTIFIED: it has never connected to Deribit, test
// or production (no test keys were supplied; README.md).
//
// Registers with the SOM for en::x::DERIBIT, turns Order into private/buy or
// private/sell and Cancel into private/cancel (or private/cancel_by_label
// before Deribit has given the order an id), and turns responses and the
// user.orders / user.trades notifications into Ack, Fill, Reject, CancAck
// and CancReject. Order state is kept in frame::som::BrokerOrderBook.
//
// Identifiers (feed.md 4): `label` is the BrokerOrderBook client id in
// decimal (Deribit allows 64 characters and does not require uniqueness; the
// OM's ids are unique for the day and not reused after a restart). The
// JSON-RPC id of each request is the OM's own counter.
//
// Reports (feed.md 4, "Reports"):
//   buy/sell result               trades first (Fill, once per trade_id), then
//                                 the order: open -> Ack (order_id is the
//                                 venue order id); filled -> Ack; cancelled
//                                 (IOC remainder) -> Ack, CancAck; rejected ->
//                                 Reject
//   buy/sell error                Reject (reason from the error code)
//   cancel result                 the order object, handled as below
//   cancel error                  CancReject; the order stays working
//   user.orders                   open -> Ack; cancelled -> CancAck (ours or
//                                 unsolicited: cancel-on-disconnect, MMP,
//                                 expiry); rejected -> Reject
//   user.trades                   Fill, once per trade_id
// A fill after the cancel ack still counts (BrokerOrderBook).
//
// Reconnect: the user channels resend nothing, so after every login the OM
// asks the session to recover() each order still live in the book; fills
// already applied are recognised by trade_id. A lost connection refuses
// every cancel in flight (CancReject DISCONNECTED; the order stays working).
// A new order whose request was in flight stays PendingNew until that
// recovery: one Deribit does not know never reached it and is rejected
// (Reject DISCONNECTED).
//
// Refused locally with a Reject: market, stop and stop-limit orders; time in
// force other than DAY (good_til_day), GTC, IOC, FOK; short-sale marking;
// routes other than DERIBIT; broker algorithms; replace; sizes that are not
// whole units; anything over the configured limits.

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "actors/Actor.hpp"
#include "actors/msg/Shutdown.hpp"
#include "actors/msg/Start.hpp"
#include "frame/som/BrokerOrderBook.hpp"
#include "frame/som/msg/Cancel.hpp"
#include "frame/som/msg/Order.hpp"

#include "deribit_oe/DeribitConfig.hpp"
#include "deribit_oe/DeribitSession.hpp"
#include "deribit_oe/msg/DeribitReport.hpp"
#include "oe_common/OmSupport.hpp"

namespace deribit::oe
{
  class DeribitOM : public actors::Actor
  {
  public:
    DeribitOM(DeribitOMConfig cfg, actors::Actor *som, SessionFactory factory,
              frame::som::ClientIdGen ids = frame::som::ClientIdGen::from_clock());
    ~DeribitOM() override;

    const char *get_name() const override { return "DeribitOM"; }

    const frame::som::BrokerOrderBook &book() const { return book_; }
    bool ready() const { return ready_; }
    bool busy() const { return busy_; }
    int sessions_created() const { return sessions_created_; }
    std::string order_id(int oid) const;   // Deribit order_id, "" if none yet
    const DeribitOMConfig &config() const { return cfg_; }

    // Deribit order_id -> the number Ack carries as xordid: the digits after
    // the last '-' ("ETH-584849853" -> 584849853), or 0.
    static uint64_t xordid(const std::string &order_id);

  private:
    class Events;

    struct Venue
    {
      frame::som::ClientId cid = 0;
      const Instrument *ins = nullptr;
      std::string order_id;
      bool acked = false;
      std::unordered_set<std::string> trades;   // trade_ids applied
    };

    struct Pending
    {
      enum Kind { NEW, CANCEL } kind;
      frame::som::ClientId cid;   // NEW: the order; CANCEL: the cancel's pending cid
      frame::som::ClientId order_cid;
    };

    void start_handler(const actors::msg::Start *) noexcept;
    void shutdown_handler(const actors::msg::Shutdown *) noexcept;
    void order_handler(const frame::som::msg::Order *) noexcept;
    void cancel_handler(const frame::som::msg::Cancel *) noexcept;
    void report_handler(const msg::DeribitReport *) noexcept;

    void on_rpc_result(const nlohmann::json &r) noexcept;
    void on_order(const nlohmann::json &o) noexcept;
    void on_trade(const nlohmann::json &t) noexcept;
    void ack(Venue &v) noexcept;
    void recover_live() noexcept;

    Venue *find(const nlohmann::json &j) noexcept;
    void learn_order_id(Venue &v, const nlohmann::json &j) noexcept;
    void connect() noexcept;
    void drop_session() noexcept;
    void schedule_reconnect() noexcept;
    void reject(actors::Actor *to, int oid, int reason, const char *why) noexcept
    {
      oe_common::reject(this, to, oid, reason, why);
    }
    void cancel_reject(actors::Actor *to, int oid, int reason, const char *why) noexcept
    {
      oe_common::cancel_reject(this, to, oid, reason, why);
    }

    DeribitOMConfig cfg_;
    actors::Actor *som_;
    SessionFactory factory_;
    frame::som::BrokerOrderBook book_;
    std::unordered_map<frame::som::ClientId, Venue> by_cid_;
    std::unordered_map<std::string, frame::som::ClientId> cid_of_order_;
    std::unordered_map<int64_t, Pending> pending_;   // JSON-RPC id -> request
    int64_t next_rpc_id_ = 1;

    std::unique_ptr<Events> events_;
    std::unique_ptr<DeribitSession> session_;
    uint64_t session_no_ = 0;
    int sessions_created_ = 0;
    bool ready_ = false;
    bool busy_ = false;
    bool stopping_ = false;
    bool login_refused_ = false;

    oe_common::TokenBucket tokens_;
    oe_common::ReconnectTimer reconnect_;
  };
}
