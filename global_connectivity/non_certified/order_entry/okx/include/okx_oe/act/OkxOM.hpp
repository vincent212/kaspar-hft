#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// OkxOM: order manager for OKX V5 spot and perpetual swaps over the private
// WebSocket. NOT CERTIFIED: it has never connected to OKX, demo or
// production (no demo keys were supplied; see README.md).
//
// Registers with the SOM for each venue it has instruments on (en::x::OKX
// for spot, OKXF for swaps), turns Order and Cancel into the `order` and
// `cancel-order` operations, and OKX's responses and `orders` channel pushes
// into Ack, Fill, Reject, CancAck and CancReject. Order state is kept in
// frame::som::BrokerOrderBook.
//
// Identifiers: clOrdId is the BrokerOrderBook client id in decimal (at most
// 19 digits; OKX allows 32 alphanumerics, unique among the account's pending
// orders). Ids are unique for the day and never reused after a restart
// (ClientIdGen). Cancels name the order by clOrdId. The op request id is
// "n<cid>" for a new order and "c<cid>r<k>" for its k-th cancel.
//
// Reports (feed.md, "Reports"):
//   op "order" sCode 0       Ack with ordId as the venue order id
//   op "order" failure       Reject (reason mapped from sCode)
//   op "cancel-order" fail   CancReject; the order stays working
//   orders: fillSz > 0       Fill, once per tradeId per order
//   orders: live / partially_filled before the op result: Ack
//   orders: canceled, mmp_canceled: CancAck (ours or unsolicited)
// A fill that arrives after the cancel ack still counts (BrokerOrderBook).
//
// Reconnect: the orders channel pushes nothing on subscribe, so after every
// login the OM asks the session to recover() each order still live in the
// book (REST fills and order state); fills already applied are recognised by
// tradeId. A lost connection refuses every cancel in flight (CancReject
// DISCONNECTED; the order stays working). A new order whose request was in
// flight stays PendingNew until that recovery: one OKX does not know never
// reached it and is rejected (Reject DISCONNECTED).
//
// Refused locally with a Reject: market, stop and stop-limit orders;
// time in force other than DAY, GTC (both sent as limit, or post_only when
// configured), IOC and FOK; short-sale marking; routes other than OKX;
// broker algorithms; replace; sizes that are not whole units; anything over
// the configured limits.

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

#include "oe_common/OmSupport.hpp"
#include "okx_oe/OkxConfig.hpp"
#include "okx_oe/OkxSession.hpp"
#include "okx_oe/msg/OkxReport.hpp"

namespace okx::oe
{
  class OkxOM : public actors::Actor
  {
  public:
    OkxOM(OkxOMConfig cfg, actors::Actor *som, SessionFactory factory,
          frame::som::ClientIdGen ids = frame::som::ClientIdGen::from_clock());
    ~OkxOM() override;

    const char *get_name() const override { return "OkxOM"; }

    const frame::som::BrokerOrderBook &book() const { return book_; }
    bool ready() const { return ready_; }
    bool busy() const { return busy_; }
    int sessions_created() const { return sessions_created_; }
    std::string ord_id(int oid) const; // OKX ordId, "" if none yet
    const OkxOMConfig &config() const { return cfg_; }

  private:
    class Events;

    struct Venue
    {
      frame::som::ClientId cid = 0;
      const Instrument *ins = nullptr;
      std::string ord_id;
      bool acked = false;
      std::unordered_set<std::string> trades; // tradeIds applied
      int cancels = 0;
    };

    void start_handler(const actors::msg::Start *) noexcept;
    void shutdown_handler(const actors::msg::Shutdown *) noexcept;
    void order_handler(const frame::som::msg::Order *) noexcept;
    void cancel_handler(const frame::som::msg::Cancel *) noexcept;
    void report_handler(const msg::OkxReport *) noexcept;

    void on_op_result(const nlohmann::json &r) noexcept;
    void on_order_update(const nlohmann::json &d) noexcept;
    void apply_fill(Venue &v, const nlohmann::json &d) noexcept;
    void ack(Venue &v) noexcept;
    void recover_live() noexcept;

    Venue *find(const nlohmann::json &d) noexcept;
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

    OkxOMConfig cfg_;
    actors::Actor *som_;
    SessionFactory factory_;
    frame::som::BrokerOrderBook book_;
    std::unordered_map<frame::som::ClientId, Venue> by_cid_;
    std::unordered_map<std::string, frame::som::ClientId> cid_of_ord_;
    std::unordered_map<std::string, frame::som::ClientId> cancel_req_; // request id -> the cancel's pending cid

    std::unique_ptr<Events> events_;
    std::unique_ptr<OkxSession> session_;
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
