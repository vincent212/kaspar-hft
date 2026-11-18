#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Configuration of a Kraken order-entry or fills session: which product and
// environment, the hosts, the key file, and the guard that keeps a testnet
// configuration off production (crypto.md, "Rules").
//
//   product = futures, testnet = true    Kraken Futures demo. rest_host must be
//                                        demo-futures.kraken.com; anything else
//                                        is refused before a connection is made.
//   product = spot,    testnet = true    refused: Kraken has no spot testnet.
//   testnet = false                      production (spot: api.kraken.com and
//                                        ws-auth.kraken.com; futures:
//                                        futures.kraken.com). Never run by the
//                                        agent; for the owner, after review.
//
// The ini file ([kraken] section; config/kraken_futures_demo_example.ini) is
// read by load_settings().

#include <string>

#include "kraken_oe/act/KrakenOM.hpp"

namespace kraken::oe
{
  enum class Product
  {
    Spot,
    Futures,
  };

  inline constexpr const char *kFuturesDemoHost = "demo-futures.kraken.com";
  inline constexpr const char *kFuturesProdHost = "futures.kraken.com";
  inline constexpr const char *kSpotRestHost = "api.kraken.com";
  inline constexpr const char *kSpotWsAuthHost = "ws-auth.kraken.com";

  struct Endpoint
  {
    Product product = Product::Futures;
    bool testnet = true;
    std::string rest_host = kFuturesDemoHost;
    std::string ws_host; // spot only: ws-auth.kraken.com

    // false with the reason when the combination is not allowed (above).
    bool check(std::string &err) const;
  };

  struct Settings
  {
    Endpoint ep;
    std::string key_file;        // JSON {"api_key","api_secret"}; outside the repository
    int poll_ms = 1000;          // futures: fills and order status polling period
    int cancel_after_s = 0;      // dead man's switch; 0 = off
    bool sim = false;            // use SimKraken instead of the venue
    KrakenOMConfig om;
  };

  // Reads the [kraken] section of an ini file. false with the reason.
  bool load_settings(const std::string &path, Settings &out, std::string &err);
}
