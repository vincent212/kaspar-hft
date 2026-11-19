/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// Factories for the Bybit V5 order manager. Not certified.
//
// Configuration: a key = value file (config/bybit_om_example.ini); the API
// key comes from the file named by `key_file`, outside the repository. With
// `sim = true` the order manager talks to an in-process SimBybit instead of
// Bybit.
//
// Both functions return nullptr, with the reason on stderr, for a bad
// configuration (including a production host with testnet = true), so that
// Bybit stays unrouted instead of stopping kaspar.

#include <string>

#include "actors/Actor.hpp"
#include "bybit_oe/BybitSession.hpp"
#include "bybit_oe/act/BybitOM.hpp"
#include "enum/e_names.hpp"

// The real session, or SimBybit when cfg.sim; factory overrides both (the
// tests pass a fake). venue: en::x::BYBIT for spot, BYBITF for linear.
actors::Actor *create_BybitOM(const bybit::oe::BybitOMConfig &cfg, actors::Actor *som, en::x venue,
                              bybit::oe::SessionFactory factory = nullptr);

// Reads the config file first; nullptr if it cannot be read or is bad.
actors::Actor *create_BybitOM(const std::string &config_path, actors::Actor *som, en::x venue);
