#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// One HTTPS request (peer and host name verified, 10 s deadline):
// crypto_common::https_request (market_data/crypto_common, shared with OKX,
// Deribit and Bybit) with headers as a list of pairs, the way Kraken's
// signers build them. Used by the verify tool for public REST snapshots and
// by the order-entry and post-trade REST clients.

#include <string>
#include <utility>
#include <vector>

#include "crypto_common/PollingWs.hpp"

namespace kraken
{
  struct HttpsResponse
  {
    int status = 0;
    std::string body;
  };

  inline HttpsResponse https_request(const std::string &method, const std::string &host, const std::string &target,
                                     const std::vector<std::pair<std::string, std::string>> &headers = {},
                                     const std::string &body = {})
  {
    crypto_common::Headers h;
    for (const auto &[k, v] : headers)
      h[k] = v;
    auto r = crypto_common::https_request(host, method, target, body, h);
    return {r.status, std::move(r.body)};
  }
}
