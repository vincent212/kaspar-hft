/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// Factories for the Deribit order manager. Not certified.
//
// Configuration: a key = value file (config/deribit_test_example.ini); the
// API key comes from the file named by `key_file`, outside the repository.
// With `sim = true` the order manager talks to an in-process SimDeribit
// instead of Deribit. Instruments are resolved to Kaspar assets by name.
//
// Both functions return nullptr, with the reason on stderr, for a bad
// configuration or a refused environment, so that DERIBIT stays unrouted
// instead of stopping kaspar.

#include <string>

#include "actors/Actor.hpp"
#include "deribit_oe/DeribitConfig.hpp"
#include "deribit_oe/DeribitSession.hpp"
#include "deribit_oe/act/DeribitOM.hpp"

// The real session, or SimDeribit when cfg.sim; factory overrides both (the
// tests pass a fake).
actors::Actor *create_DeribitOM(deribit::oe::DeribitOMConfig cfg, actors::Actor *som,
                                deribit::oe::SessionFactory factory = nullptr);

// Reads the config file first; nullptr if it cannot be read or is bad.
actors::Actor *create_DeribitOM(const std::string &config_path, actors::Actor *som);
