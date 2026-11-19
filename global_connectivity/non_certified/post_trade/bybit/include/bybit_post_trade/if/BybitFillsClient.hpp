/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// Factories for the Bybit V5 post-trade client. Not certified.
//
// Configuration: a key = value file (config/bybit_fills_example.ini); the API
// key comes from the file named by `key_file`, outside the repository. With
// `sim = true` the client reads an in-process SimFillSource instead of Bybit.
//
// Both functions return nullptr, with the reason on stderr, for a bad
// configuration (including a production host with testnet = true).

#include <string>

#include "actors/Actor.hpp"
#include "bybit_post_trade/FillSource.hpp"
#include "bybit_post_trade/act/BybitFillsClient.hpp"

// The real source, or SimFillSource when cfg.sim; factory overrides both
// (the tests pass a fake).
actors::Actor *create_BybitFillsClient(const bybit_pt::ClientConfig &cfg, actors::Actor *subscriber,
                                       bybit_pt::SourceFactory factory = nullptr);

// Reads the config file first; nullptr if it cannot be read or is bad.
actors::Actor *create_BybitFillsClient(const std::string &config_path, actors::Actor *subscriber);
