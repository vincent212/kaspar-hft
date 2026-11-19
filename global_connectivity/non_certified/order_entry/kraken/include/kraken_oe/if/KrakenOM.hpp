/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// Factory for the Kraken order manager. Not certified.
//
// The session comes from the caller: spot_session_factory or
// futures_session_factory (KrakenSessions.hpp), or SimKraken::make_factory
// for a --sim run. Returns nullptr, with the reason on stderr, for a bad
// configuration or no factory, so that Kraken stays unrouted instead of
// stopping kaspar.

#include "actors/Actor.hpp"
#include "enum/e_names.hpp"
#include "kraken_oe/KrakenSession.hpp"
#include "kraken_oe/act/KrakenOM.hpp"

actors::Actor *create_KrakenOM(const kraken::oe::KrakenOMConfig &cfg, actors::Actor *som,
                               kraken::oe::SessionFactory factory, en::x venue = en::x::KRAKEN);
