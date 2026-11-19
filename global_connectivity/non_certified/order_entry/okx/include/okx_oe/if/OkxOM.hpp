/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// Factories for the OKX V5 order manager. Not certified.
//
// Configuration: a key = value file (config/okx_demo_example.ini); the API
// key comes from the file named by `key_file`, outside the repository. With
// `sim = true` the order manager talks to an in-process SimOkx instead of
// OKX. Instruments are resolved to Kaspar assets by name.
//
// Both functions return nullptr, with the reason on stderr, for a bad
// configuration or a refused environment, so that OKX stays unrouted instead
// of stopping kaspar.

#include <string>

#include "actors/Actor.hpp"
#include "okx_oe/OkxConfig.hpp"
#include "okx_oe/OkxSession.hpp"
#include "okx_oe/act/OkxOM.hpp"

// The real session, or SimOkx when cfg.sim; factory overrides both (the
// tests pass a fake).
actors::Actor *create_OkxOM(okx::oe::OkxOMConfig cfg, actors::Actor *som, okx::oe::SessionFactory factory = nullptr);

// Reads the config file first; nullptr if it cannot be read or is bad.
actors::Actor *create_OkxOM(const std::string &config_path, actors::Actor *som);
