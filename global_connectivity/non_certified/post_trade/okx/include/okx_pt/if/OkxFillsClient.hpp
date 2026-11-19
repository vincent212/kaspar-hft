/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// Factory for the OKX V5 post-trade client. Not certified.
//
// Returns nullptr, with the reason on stderr, for a bad configuration or a
// refused environment. The real session, or SimFills when cfg.sim; factory
// overrides both (the tests pass a fake).

#include "actors/Actor.hpp"
#include "okx_pt/OkxFills.hpp"
#include "okx_pt/act/OkxFillsClient.hpp"

actors::Actor *create_OkxFillsClient(okx::pt::FillsConfig cfg, actors::Actor *subscriber,
                                     okx::pt::FillsSessionFactory factory = nullptr);
