/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// SimLmaxGateway: an in-process stand-in for LMAX's Broker FIX gateways, for
// the loopback tests and --sim runs. A QuickFIX acceptor on 127.0.0.1 (plain
// TCP) with two sessions, each validated against the LMAX dictionary:
//
//   market data  Logon with Username/Password and 141=Y; MarketDataRequest
//                answered with a W per instrument every md_interval_ms from a
//                random-walk book, or a MarketDataRequestReject. Trades appear
//                in the next W, and (repeat_trades) in the one after, as the
//                reading in LmaxMdSync allows.
//   trading      NewOrderSingle, OrderCancelRequest, OrderCancelReplaceRequest,
//                OrderStatusRequest and TradeCaptureReportRequest. A limit
//                order that reaches the simulated best price fills there in
//                full (or in two halves with partial_fills); a resting order
//                waits for cancel, replace or force_fill(). Orders without
//                ExecInst H are cancelled when the session disconnects.
//
// What it does not know about LMAX is in the feed descriptions (which ExecType
// LMAX uses for an expired IOC, whether a W repeats trades, the size unit):
// the simulator takes one reading of each and is not evidence for it.

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "quickfix/Application.h"
#include "quickfix/SessionID.h"

namespace FIX
{
  class Acceptor;
  class MessageStoreFactory;
  class SessionSettings;
  class LogFactory;
}

namespace lmax::fix
{
  class SimLmaxGateway : public FIX::Application
  {
  public:
    struct Instrument
    {
      std::string security_id;
      double mid = 1.0;
      double tick = 0.00001;
      double qty_step = 0.1; // OrderQty and MDEntrySize grid
    };

    struct Config
    {
      int port = 0;                    // 0: pick a free one
      std::string md_dictionary, trading_dictionary;
      std::string username = "user", password = "pass";
      std::string md_comp = "LMXBDM", trade_comp = "LMXBD"; // the gateway's CompIDs
      std::string client_md_comp = "CLIENTMD", client_trade_comp = "CLIENTTR";
      std::vector<Instrument> instruments;
      int md_interval_ms = 20;
      int levels = 5;
      bool repeat_trades = true;
      bool partial_fills = false;
      bool ignore_resend = false; // answer a market-data ResendRequest with a session Reject
      std::string log_dir;        // FIX log of the gateway; empty: none
      uint64_t seed = 42;
    };

    explicit SimLmaxGateway(Config cfg);
    ~SimLmaxGateway() override;

    void start();
    void stop();
    int port() const { return port_; }
    const Config &config() const { return cfg_; }

    // Test controls.
    void inject_md_gap(int n = 1);         // skip n outgoing market-data MsgSeqNums
    void pause_md(bool p) { md_paused_ = p; }
    void disconnect_md();
    void disconnect_trading();
    bool force_fill(const std::string &clordid, double qty); // fill a working order
    bool unsolicited_cancel(const std::string &clordid);
    void set_market(const std::string &security_id, double bid, double ask);
    void trade_print(const std::string &security_id, double px, double size); // a market trade in the next W

    // Observations.
    struct Counts
    {
      std::atomic<int> md_logons{0}, trade_logons{0}, logon_rejects{0};
      std::atomic<int> md_requests{0}, md_request_rejects{0}, snapshots{0};
      std::atomic<int> resend_requests{0};
      std::atomic<int> new_orders{0}, cancels{0}, replaces{0}, status_requests{0}, tcr_requests{0};
      std::atomic<int> validation_errors{0};
    };
    Counts &counts() { return n_; }
    std::vector<std::string> received(const std::string &msg_type); // raw text, SOH as '|'
    bool md_logged_on() const;
    bool trading_logged_on() const;
    size_t working_orders();

    // FIX::Application
    void onCreate(const FIX::SessionID &) override {}
    void onLogon(const FIX::SessionID &) override;
    void onLogout(const FIX::SessionID &) override;
    void toAdmin(FIX::Message &, const FIX::SessionID &) override {}
    void toApp(FIX::Message &, const FIX::SessionID &) override {}
    void fromAdmin(const FIX::Message &, const FIX::SessionID &) override;
    void fromApp(const FIX::Message &, const FIX::SessionID &) override;

  private:
    struct Book
    {
      Instrument inst;
      double bid = 0, ask = 0;
      std::vector<std::pair<double, double>> bids, asks; // full simulated depth
      std::vector<std::tuple<double, double, std::string, std::string>> trades, last_trades;
    };
    struct Sub
    {
      std::string req_id, security_id;
      int depth = 20;
      bool trades = false;
    };
    struct Ord
    {
      std::string clordid, order_id, security_id, account;
      char side = '1', ord_type = '2', tif = '0';
      double qty = 0, cum = 0, px = 0, notional = 0;
      bool reinstate = false;
      bool done = false;
      char status = '0';
    };
    struct Fill
    {
      std::string exec_id, secondary, order_id, clordid, security_id, account;
      char side;
      double qty, px;
      std::string trade_date, transact_time;
    };

    void md_loop(std::stop_token st);
    void step_book(Book &b);
    void send_snapshot(const Sub &s, Book &b);
    void on_md_request(const FIX::Message &m);
    void on_new_order(const FIX::Message &m);
    void on_cancel(const FIX::Message &m);
    void on_replace(const FIX::Message &m);
    void on_status(const FIX::Message &m);
    void on_tcr_request(const FIX::Message &m);
    void match(Ord &o);
    void fill(Ord &o, double qty, double px);
    void send_exec(const Ord &o, char exec_type, const std::string &orig = {}, double last_qty = 0,
                   double last_px = 0, const std::string &exec_id = {}, const std::string &secondary = {},
                   const std::string &status_req_id = {}, const std::string &text = {});
    void send_reject(const FIX::Message &m, int reason, const std::string &text);
    void send_cancel_reject(const std::string &clordid, const std::string &orig, char response_to, int reason,
                            const std::string &order_id, char status);
    void send_tcr(const Fill &f, const std::string &req_id, bool last);
    std::string next_exec_id();
    Book *book(const std::string &security_id);
    static std::string now_ts();
    static std::string today();

    Config cfg_;
    int port_ = 0;
    std::unique_ptr<FIX::SessionSettings> settings_;
    std::unique_ptr<FIX::MessageStoreFactory> store_;
    std::unique_ptr<FIX::LogFactory> log_;
    std::unique_ptr<FIX::Acceptor> acceptor_;
    FIX::SessionID md_sid_, trade_sid_;
    std::jthread md_thread_;
    std::atomic<bool> md_paused_{false};
    Counts n_;

    std::recursive_mutex mu_;
    std::map<std::string, Book> books_;
    std::vector<Sub> subs_;
    std::map<std::string, Ord> orders_; // by current ClOrdID
    std::vector<Fill> fills_;
    std::set<std::string> tcr_subs_;
    std::multimap<std::string, std::string> received_;
    std::mt19937_64 rng_;
    uint64_t exec_seq_ = 0, order_seq_ = 0;
  };
}
