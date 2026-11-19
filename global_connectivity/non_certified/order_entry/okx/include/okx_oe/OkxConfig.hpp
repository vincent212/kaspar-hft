#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Configuration of the OKX order manager and post-trade client, and the
// environment guard.
//
// OKX demo trading uses the production REST host (openapi.okx.com) with the header
// `x-simulated-trading: 1`, and its own WebSocket host wspap.okx.com
// (docs-v5, "Demo Trading Services"). With environment = demo:
//   - the WebSocket host must be wspap.okx.com: a production host
//     (ws.okx.com, wsaws.okx.com, ...) is refused;
//   - every REST request and the WebSocket upgrade carry
//     x-simulated-trading: 1 (Endpoint::headers()), and require_demo_header()
//     refuses to send a request without it.
// environment = production is refused unless allow_production = true, which
// only the owner sets, after review.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "enum/e_names.hpp"

namespace okx::oe
{
  using Headers = std::map<std::string, std::string>;

  struct Endpoint
  {
    bool demo = true;
    std::string ws_host = "wspap.okx.com";
    std::string ws_port = "443";
    std::string rest_host = "openapi.okx.com"; // the recommended REST domain since 2026-05-20; www.okx.com also works
    bool allow_production = false;

    // Headers for every request: x-simulated-trading: 1 in demo.
    Headers headers() const;
    // false with the reason when the endpoint may not be used.
    bool check(std::string &err) const;
  };

  // true when h may be sent to the endpoint: in demo it must carry
  // x-simulated-trading: 1.
  bool require_demo_header(const Endpoint &ep, const Headers &h, std::string &err);

  struct Instrument
  {
    std::string asset;     // Kaspar asset name (RefData); the SOM's Order.sym
    std::string inst_id;   // e.g. BTC-USDT, BTC-USDT-SWAP
    int64_t inst_id_code = 0; // instIdCode from GET /api/v5/public/instruments; differs between demo and production
    std::string inst_type; // SPOT, SWAP
    std::string td_mode;   // cash (spot), cross or isolated (swap)
    std::string tick;      // tickSz as a decimal string, e.g. "0.1"
    std::string qty_unit;  // size of one SOM unit in OKX's sz unit, e.g. "0.0001" (BTC) or "0.01" (contracts)
    en::x venue;           // en::x::OKX (spot) or OKXF (swap)
    int sym = -1;          // resolved Kaspar asset id; -1 until resolved

    double tick_d() const { return std::stod(tick); }
    double unit_d() const { return std::stod(qty_unit); }
  };

  struct OkxOMConfig
  {
    Endpoint ep;
    std::string key_file;   // JSON {api_key, secret_key, passphrase}; outside the repository
    std::vector<Instrument> instruments;
    int reconnect_ms = 2000;
    bool post_only = false;        // day/GTC limit orders as post_only instead of limit
    int max_orders_per_sec = 10;   // new orders; OKX allows 60 per 2 s per instrument
    int max_order_units = 0;       // 0 = no limit
    double max_order_notional = 0; // quote currency; 0 = no limit
    int cancel_all_after_s = 0;    // 0 off; else 10..120: OKX cancels all orders if not refreshed
    bool sim = false;              // SimOkx instead of OKX

    // Reads an INI file (config/okx_demo_example.ini). false with the reason.
    static bool from_file(const std::string &path, OkxOMConfig &out, std::string &err);
    // Checks the values and the environment guard. false with the reason.
    bool valid(std::string &err) const;

    const Instrument *by_sym(int sym) const;
    const Instrument *by_inst_id(const std::string &id) const;
  };

  // value * 10^decimals of a decimal string ("0.0001" -> 4), for formatting.
  int decimals(const std::string &dec);
  // n * unit formatted with unit's decimals, e.g. (3, "0.0001") -> "0.0003".
  std::string scaled(int64_t n, const std::string &unit);
  // A decimal string divided by unit, rounded to the nearest integer; `exact`
  // false when it is not a whole multiple.
  int64_t units_of(const std::string &value, const std::string &unit, bool &exact);
}
