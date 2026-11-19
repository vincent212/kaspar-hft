#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Configuration of the Deribit order manager and fills client, the
// environment guard, and the key file. NOT CERTIFIED.
//
// Deribit's test environment is a separate host, test.deribit.com, with its
// own accounts and keys (feed.md 1). With environment = test:
//   - the host must be test.deribit.com; www.deribit.com (or any other host)
//     is refused, so a test configuration cannot reach production;
//   - every request is built against Endpoint::host, and the session checks
//     the host again (check()) before it opens a connection.
// environment = production is refused unless allow_production = true, which
// only the owner sets, after review.
//
// Keys are read from a JSON file outside the repository, by default
// ~/.kaspar/keys/deribit_test.json:
//   {"client_id": "...", "client_secret": "..."}
// Nothing here prints, logs or copies a key into an error message.

#include <cstdint>
#include <string>
#include <vector>

#include "enum/e_names.hpp"

namespace deribit::oe
{
  constexpr const char *kTestHost = "test.deribit.com";
  constexpr const char *kProductionHost = "www.deribit.com";

  struct Endpoint
  {
    bool test = true;
    std::string host = kTestHost;
    bool allow_production = false;
    // false with the reason when the endpoint may not be used.
    bool check(std::string &err) const;
  };

  struct Keys
  {
    std::string client_id, client_secret;
    bool complete() const { return !client_id.empty() && !client_secret.empty(); }
  };
  // false with a reason that never contains the file's content.
  bool load_keys(const std::string &path, Keys &out, std::string &err);
  std::string default_key_path();   // ~/.kaspar/keys/deribit_test.json

  struct Instrument
  {
    std::string asset;        // Kaspar asset name (RefData); the SOM's Order.sym
    std::string name;         // Deribit instrument_name, e.g. BTC-PERPETUAL
    std::string currency;     // BTC, ETH, USDC: for the *_by_label calls
    std::string tick;         // tick_size as a decimal string, e.g. "0.5"
    std::string qty_unit;     // one SOM unit in Deribit's `amount`, e.g. "10" (USD) for BTC-PERPETUAL
    bool inverse = true;      // amount is USD (inverse futures and perpetuals); false: amount is coin
    en::x venue = en::x::DERIBIT;
    int sym = -1;             // resolved Kaspar asset id; -1 until resolved

    double tick_d() const { return std::stod(tick); }
    double unit_d() const { return std::stod(qty_unit); }
  };

  struct DeribitOMConfig
  {
    Endpoint ep;
    std::string key_file;
    std::vector<Instrument> instruments;
    int reconnect_ms = 2000;
    int heartbeat_s = 30;          // public/set_heartbeat interval (Deribit minimum 10)
    bool cancel_on_disconnect = true;   // private/enable_cancel_on_disconnect, scope connection
    bool post_only = false;        // DAY/GTC limit orders with post_only and reject_post_only
    int max_orders_per_sec = 5;    // new orders; Deribit's lowest matching-engine tier is 5/s, burst 20
    int max_order_units = 0;       // 0 = no limit
    double max_order_notional = 0; // USD; 0 = no limit
    bool sim = false;              // SimDeribit instead of Deribit

    static bool from_file(const std::string &path, DeribitOMConfig &out, std::string &err);
    bool valid(std::string &err) const;

    const Instrument *by_sym(int sym) const;
    const Instrument *by_name(const std::string &name) const;
  };

  // Decimal helpers (the venue's JSON numbers are doubles; these keep the
  // conversion to SOM units exact or report that it is not).
  int decimals(const std::string &dec);                       // "0.0001" -> 4
  std::string scaled(int64_t n, const std::string &unit);     // (3, "0.5") -> "1.5"
  int64_t units_of(double value, const std::string &unit, bool &exact);
}
