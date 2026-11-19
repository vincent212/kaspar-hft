/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// Factories for the Nasdaq OUCH 5.0 order manager. Not certified.
//
// Configuration: a key = value file (config/ouch_example.ini); credentials
// come from the file named by `credentials`, outside the repository. With
// `sim = true` the order manager talks to an in-process SimOuch instead of a
// SoupBinTCP socket.
//
// Both functions return nullptr, with the reason on stderr, for a bad
// configuration, so that Nasdaq stays unrouted instead of stopping kaspar.

#include <string>

#include "actors/Actor.hpp"
#include "enum/e_names.hpp"
#include "nasdaq_ouch/OuchSession.hpp"
#include "nasdaq_ouch/act/OuchOM.hpp"

// The real SoupBinTCP session, or SimOuch when cfg.sim; factory overrides
// both (the tests pass a fake).
actors::Actor *create_OuchOM(const nasdaq::ouch::OuchOMConfig &cfg, actors::Actor *som,
                             nasdaq::ouch::SessionFactory factory = nullptr,
                             en::x venue = en::x::NASDAQ);

// Reads the config file first; nullptr if it cannot be read or is bad.
actors::Actor *create_OuchOM(const std::string &config_path, actors::Actor *som);
