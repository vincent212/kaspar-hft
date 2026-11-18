#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// CbxOM: order manager for Coinbase Exchange spot over FIX 5.0 SP2 (FIXT 1.1).
// NOT CERTIFIED: it has not connected to Coinbase; tested against FakeCbx and
// SimCbx only (README.md).
//
// Registers with the SOM for its venue (en::x::COINBASEX), turns the SOM's
// Order and Cancel into NewOrderSingle (35=D) and OrderCancelRequest (35=F),
// and Coinbase's ExecutionReport (35=8), OrderCancelReject (35=9) and
// BusinessMessageReject (35=j) into Ack, Fill, Reject, CancAck and CancReject
// for the order's sender. Order state is kept in frame::som::BrokerOrderBook.
//
// Identifiers: Coinbase requires ClOrdID to be a lowercase variant-1 UUIDv4.
// BrokerOrderBook's client ids are 63-bit integers, so each client id an
// order uses (the new order's, then each cancel's) gets a random UUIDv4,
// mapped both ways for the life of the process. The exchange order id in Ack
// and Fill is order_id64(OrderID): the UUID's high 64 bits, top bit cleared,
// the same id the market-data handler gives the order in OB.
//
// Units: SOM prices are ticks of the product's quote_increment (as OB's);
// SOM sizes are whole units of the product's `qty_unit` (default its
// base_increment). OrderQty and Price are written as exact decimals.
//
// Duplicates: a fill is applied once per ExecID, and a Trade report whose
// CumQty is not above what the order has already filled is a replay. After a
// reconnect the OM sends OrderStatusRequest (35=H) for every live order;
// the status report (150=I) settles orders that ended while disconnected
// (CancAck, or a Fill for quantity filled but never reported, priced from
// AvgPx and logged as an error to check).
//
// Refused locally, with a Reject: DAY when day_tif = reject (Coinbase has no
// DAY; the default sends GTC), AT_OPEN, AT_CLOSE, EXTENDED_DAY, plain STOP,
// short-sale marks, routes, broker algorithms, cancel/replace, instruments
// not in the products table, quantities and prices that are not positive,
// and anything over the configured limits.

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "actors/Actor.hpp"
#include "actors/msg/Shutdown.hpp"
#include "actors/msg/Start.hpp"
#include "coinbase_exchange/Num.hpp"
#include "coinbase_exchange_oe/CbxSession.hpp"
#include "coinbase_exchange_oe/SimCbx.hpp"
#include "coinbase_exchange_oe/msg/CbxReport.hpp"
#include "enum/e_names.hpp"
#include "frame/som/BrokerOrderBook.hpp"
#include "frame/som/msg/Cancel.hpp"
#include "frame/som/msg/Order.hpp"

namespace coinbase_exchange::oe
{
  // One product as the OM writes it (GET /products/<id>).
  struct OEProduct
  {
    std::string product;          // FIX Symbol, e.g. "BTC-USD"
    int price_decimals = 2;
    int64_t price_increment = 1;  // quote_increment in 10^-price_decimals
    int size_decimals = 8;
    int64_t qty_unit = 1;         // one SOM size unit in 10^-size_decimals

    // From the strings of the product and the chosen unit; false with err.
    static bool make(const std::string &product, const std::string &quote_increment,
                     const std::string &base_increment, const std::string &qty_unit, OEProduct &out,
                     std::string &err);
    std::string price(int ticks) const { return format_scaled(int64_t(ticks) * price_increment, price_decimals); }
    std::string qty(int64_t units) const { return format_scaled(units * qty_unit, size_decimals); }
    // "64000.01" -> ticks; nullopt if malformed or off the increment
    std::optional<int64_t> ticks(const std::string &px) const;
    // "0.0012" -> SOM units; nullopt if malformed or not a whole number of units
    std::optional<int64_t> units(const std::string &qty) const;
  };

  struct CbxOMConfig
  {
    SessionConfig session;
    std::map<std::string, OEProduct> products; // by Kaspar asset name (the product id)

    char day_tif = '1';          // what SOM DAY becomes: '1' GTC, '6' GTD to next 00:00 UTC, 'R' reject
    bool post_only = false;      // ExecInst 18=A on every limit order
    char self_trade = 'D';       // SelfTradeType 7928: D, O, N, B

    // Local risk limits, checked before an order is sent. 0 = no limit.
    int max_order_qty = 0;            // SOM units
    double max_order_notional = 0;    // quote currency
    int max_orders_per_sec = 50;      // new orders; Coinbase allows 100 requests/s
    int reconnect_ms = 2000;

    bool sim = false;
    SimCbx::Config sim_config;

    // key = value file (config/cbx_om_example.ini). false with the reason.
    static bool from_file(const std::string &path, CbxOMConfig &out, std::string &err);
    bool valid(std::string &err) const;
  };

  class CbxOM : public actors::Actor
  {
  public:
    CbxOM(CbxOMConfig cfg, actors::Actor *som, SessionFactory factory, en::x venue = en::x::COINBASEX,
          frame::som::ClientIdGen ids = frame::som::ClientIdGen::from_clock(), uint64_t uuid_seed = 0);
    ~CbxOM() override;

    const char *get_name() const override { return "CbxOM"; }

    const frame::som::BrokerOrderBook &book() const { return book_; }
    bool ready() const { return ready_; }
    bool busy() const { return busy_; }
    int sessions_created() const { return sessions_created_; }
    // the ClOrdID an order's current client id is sent under ("" if none)
    std::string clordid(int oid) const;
    uint64_t fills_applied() const { return fills_; }
    uint64_t duplicates_skipped() const { return dups_; }

  private:
    class Events;

    struct VenueOrder // what Coinbase has said about one order
    {
      std::string order_id;   // OrderID (37), a UUID string
      std::string symbol;
      const OEProduct *prod = nullptr;
      bool acked = false;
      int64_t venue_cum = 0;  // SOM units, from CumQty
      long double notional = 0; // sum of fill qty * price (quote currency), for AvgPx checks
    };

    void start_handler(const actors::msg::Start *) noexcept;
    void shutdown_handler(const actors::msg::Shutdown *) noexcept;
    void order_handler(const frame::som::msg::Order *) noexcept;
    void cancel_handler(const frame::som::msg::Cancel *) noexcept;
    void report_handler(const msg::CbxReport *) noexcept;

    void on_execution_report(const fix::FixMsg &m) noexcept;
    void on_cancel_reject(const fix::FixMsg &m) noexcept;
    void on_business_reject(const fix::FixMsg &m) noexcept;
    void on_trade(const fix::FixMsg &m, frame::som::ClientId cid) noexcept;
    void on_status(const fix::FixMsg &m, frame::som::ClientId cid) noexcept;
    void send_fill(frame::som::ClientId cid, int qty, int64_t px_ticks, uint64_t tim, const char *why) noexcept;
    void ack_once(frame::som::ClientId cid) noexcept;
    void canceled(frame::som::ClientId cid, const char *why) noexcept;
    void request_status() noexcept;

    frame::som::ClientId cid_of(const std::string &uuid) const;
    std::string new_uuid(frame::som::ClientId cid);
    VenueOrder *venue_of(frame::som::ClientId cid);

    void connect() noexcept;
    void drop_session() noexcept;
    void schedule_reconnect() noexcept;
    bool take_token() noexcept;
    void reject(actors::Actor *to, int oid, int reason, const char *why) noexcept;
    void cancel_reject(actors::Actor *to, int oid, int reason, const char *why) noexcept;

    CbxOMConfig cfg_;
    actors::Actor *som_;
    SessionFactory factory_;
    en::x venue_;
    frame::som::BrokerOrderBook book_;
    std::mt19937_64 rng_;
    std::unordered_map<frame::som::ClientId, std::string> uuid_of_cid_;
    std::unordered_map<std::string, frame::som::ClientId> cid_of_uuid_;
    std::unordered_map<int, VenueOrder> vorders_; // by SOM oid
    std::unordered_set<std::string> exec_ids_;  // ExecIDs applied

    std::unique_ptr<Events> events_;
    std::unique_ptr<CbxSession> session_;
    uint64_t session_no_ = 0;
    int sessions_created_ = 0;
    bool ready_ = false;
    bool busy_ = false;
    bool stopping_ = false;
    bool rejected_ = false; // logon rejected: no retry
    uint64_t fills_ = 0, dups_ = 0;

    double tokens_ = 0;
    std::chrono::steady_clock::time_point refilled_{};
    std::jthread reconnect_timer_;
  };
}

// The QuickFIX session, or SimCbx when cfg.sim. nullptr (reason on stderr)
// for a bad config, so Coinbase Exchange stays unrouted instead of stopping
// kaspar.
actors::Actor *create_CbxOM(const coinbase_exchange::oe::CbxOMConfig &cfg, actors::Actor *som,
                            coinbase_exchange::oe::SessionFactory factory = nullptr,
                            en::x venue = en::x::COINBASEX);
actors::Actor *create_CbxOM(const std::string &config_path, actors::Actor *som);
