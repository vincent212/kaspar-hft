/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// Factory for the Nasdaq DROP post-trade client. Not certified.
//
// Configuration (boost INFO format), section nasdaq_drop:
//
//   nasdaq_drop
//   {
//       host               <DROP host>          ; given by Nasdaq
//       port               <port>
//       credentials        /secure/drop.info    ; username and password, outside the repository
//       session            ""                   ; blank: the currently active session
//       replay_from_start  true                 ; request sequence number 1 (else 0, most recent)
//       heartbeat_ms       1000
//       server_timeout_ms  15000
//       reconnect_ms       2000
//       connect_timeout_ms 5000
//       trade_date         0                    ; YYYYMMDD; 0: Eastern date at login
//       assets             { INTC 101 }         ; venue symbol -> Kaspar asset id
//       sim                { enabled false  trade_ms 500  symbols "INTC MSFT" }
//   }
//
// With sim.enabled the client starts a SimDropServer on 127.0.0.1 and
// connects to it; host, port and credentials are then not read.
//
// Both functions return nullptr, with the reason on stderr, for a bad
// configuration, so that one broken venue does not stop the process.

#include <optional>
#include <string>

#include <boost/property_tree/ptree.hpp>

#include "actors/Actor.hpp"
#include "nasdaq_post_trade/act/NasdaqDropClient.hpp"

// config_path: a file with a nasdaq_drop section. A relative credentials
// path is taken relative to the file's directory.
actors::Actor *create_NasdaqDropClient(const std::string &config_path, actors::Actor *subscriber);

// cfg: the nasdaq_drop section itself.
actors::Actor *create_NasdaqDropClient(const boost::property_tree::ptree &cfg,
                                       actors::Actor *subscriber,
                                       const std::string &base_dir = ".");

namespace nasdaq_post_trade
{
  // The configuration the factory builds, or nullopt with the reason in *err.
  std::optional<ClientConfig> parse_config(const boost::property_tree::ptree &cfg,
                                           const std::string &base_dir, std::string *err);
}
