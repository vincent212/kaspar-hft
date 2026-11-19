/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// Factory for the Kraken post-trade client. Not certified.
//
// Returns nullptr, with the reason on stderr, for a bad configuration or no
// factory. The session comes from the caller (KrakenFills.hpp's factories,
// or SimFills for a --sim run).

#include "actors/Actor.hpp"
#include "kraken_pt/KrakenFills.hpp"
#include "kraken_pt/act/KrakenFillsClient.hpp"

actors::Actor *create_KrakenFillsClient(kraken::pt::FillsConfig cfg, actors::Actor *subscriber,
                                        kraken::pt::FillsSessionFactory factory);
