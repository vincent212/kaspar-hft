#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Deribit fills for post-trade: the session interface, the mapping of one
// Deribit trade to post_trade::msg::TradeCapture, and the configuration.
// NOT CERTIFIED.
//
// Sources (feed.md):
//   WebSocket  private user.trades.any.any.raw: every trade of the account,
//              as it happens (the subscription needs an authenticated
//              connection).
//   REST       GET /api/v2/private/get_user_trades_by_currency_and_time,
//              signed with deri-hmac-sha256 (feed.md 2): the reconciliation
//              source, per configured currency, at logon and every
//              reconcile_every_s; paged with has_more, 1000 rows per page.
// Both give trade_id (unique per currency), order_id, label, instrument_name,
// direction (the account's side), amount, price and timestamp (ms). Deribit
// sends no amendment, cancellation or bust of a trade on these channels:
// every report is Event::New.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "deribit_oe/DeribitConfig.hpp"
#include "enum/e_names.hpp"
#include "post_trade/msg/TradeCapture.hpp"

namespace deribit::pt
{
  struct PtInstrument
  {
    std::string name;       // BTC-PERPETUAL
    std::string currency;   // BTC
    std::string asset;      // Kaspar asset name; "" if not mapped
    std::string qty_unit;   // TradeCapture.qty unit in Deribit's amount: "10" (USD, one contract)
    en::x venue = en::x::DERIBIT;
    int sym = -1;
  };

  struct FillsConfig
  {
    oe::Endpoint ep;
    std::string key_file;
    int reconcile_every_s = 60;        // 0: only at logon
    int reconcile_lookback_s = 3600;   // start_timestamp = now - lookback
    int heartbeat_s = 30;
    int reconnect_ms = 2000;
    std::vector<PtInstrument> instruments;
    bool sim = false;

    // The [deribit] and [instrument.*] sections of the order manager's INI
    // (order_entry/deribit/config/deribit_test_example.ini) and [post_trade].
    static bool from_file(const std::string &path, FillsConfig &out, std::string &err);
    bool valid(std::string &err) const;
    const PtInstrument *find(const std::string &name) const;
    std::vector<std::string> currencies() const;
  };

  // One Deribit trade (a user.trades element or a REST row) as a
  // TradeCapture. false with the reason when it does not map (unknown
  // instrument, amount not a whole number of units).
  bool to_trade_capture(const nlohmann::json &t, const FillsConfig &cfg, bool from_rest,
                        post_trade::msg::TradeCapture &out, std::string &err);

  // "63123.45" -> (6312345, 2); false if not a plain decimal.
  bool parse_decimal(const std::string &s, int64_t &mantissa, int &decimals);

  enum class CallStatus
  {
    Success,
    Disconnected,
  };

  struct FillsEvents
  {
    virtual ~FillsEvents() = default;
    virtual void logged_in() = 0;
    virtual void login_rejected(const std::string &code, const std::string &msg) = 0;
    virtual void disconnected(const std::string &reason) = 0;
    virtual void fill(const nlohmann::json &t, bool from_rest) = 0;
    virtual void reconciled(size_t rows, bool ok, const std::string &err) = 0;
  };

  class FillsSession
  {
  public:
    virtual ~FillsSession() = default;
    virtual void logon() = 0;
    // REST trades from begin_ms to end_ms: rows come as fill(..., true), then reconciled().
    virtual CallStatus reconcile(uint64_t begin_ms, uint64_t end_ms) = 0;
    virtual void logout() = 0;
  };

  using FillsSessionFactory = std::function<std::unique_ptr<FillsSession>(const FillsConfig &, FillsEvents &)>;
  FillsSessionFactory ws_fills_factory();
}
