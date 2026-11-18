#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// OKX V5 fills for post-trade: the session interface, the mapping of one OKX
// fill to post_trade::msg::TradeCapture, and the configuration.
//
// Sources (feed.md):
//   WebSocket  private `orders` channel: an element with fillSz > 0 and a
//              tradeId is one fill. (The `fills` channel needs VIP4 or above;
//              channel = fills selects it where the account qualifies.)
//   REST       GET /api/v5/trade/fills (last 3 days), paged by billId: the
//              reconciliation source, fetched at logon and every
//              reconcile_every_s.
// Both give instId, tradeId, ordId, clOrdId, side, fillSz, fillPx and the
// fill time; tradeId is per instrument and is the same in both, and in OKX's
// public trade feed. OKX sends no amendment, cancellation or bust of a fill:
// every report is Event::New (liquidation and ADL fills are treated as
// fills like any other; their shape was not checked).

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "enum/e_names.hpp"
#include "okx_oe/OkxConfig.hpp"
#include "post_trade/msg/TradeCapture.hpp"

namespace okx::pt
{
  struct PtInstrument
  {
    std::string inst_id;   // BTC-USDT
    std::string asset;     // Kaspar asset name; "" if not mapped
    std::string lot;       // quantity unit for TradeCapture.qty: the instrument's lotSz
    en::x venue;           // OKX spot, OKXF swap
    int sym = -1;
  };

  struct FillsConfig
  {
    oe::Endpoint ep;
    std::string key_file;
    std::string channel = "orders";   // or "fills" (VIP4+)
    int reconcile_every_s = 60;       // 0: only at logon
    int reconcile_lookback_s = 3600;  // REST begin = now - lookback (at most 3 days)
    int reconnect_ms = 2000;
    std::vector<PtInstrument> instruments;
    bool sim = false;

    static bool from_file(const std::string &path, FillsConfig &out, std::string &err);
    bool valid(std::string &err) const;
    const PtInstrument *find(const std::string &inst_id) const;
  };

  // One OKX fill (an orders/fills channel element or a REST fills row) as a
  // TradeCapture. false with the reason in err when it is not a fill or does
  // not map (unknown instrument, size not a whole number of lots).
  bool to_trade_capture(const nlohmann::json &f, const FillsConfig &cfg, bool from_rest,
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
    virtual void fill(const nlohmann::json &f, bool from_rest) = 0;
    // A reconciliation pass finished: rows = REST rows read, ok = no error.
    virtual void reconciled(size_t rows, bool ok, const std::string &err) = 0;
  };

  class FillsSession
  {
  public:
    virtual ~FillsSession() = default;
    virtual void logon() = 0;
    // REST fills from begin_ms; rows come as fill(..., true), then reconciled().
    virtual CallStatus reconcile(uint64_t begin_ms) = 0;
    virtual void logout() = 0;
  };

  using FillsSessionFactory = std::function<std::unique_ptr<FillsSession>(const FillsConfig &, FillsEvents &)>;
  FillsSessionFactory ws_fills_factory();
}
