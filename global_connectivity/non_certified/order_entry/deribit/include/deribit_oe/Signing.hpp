#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Deribit request signing (feed.md 2, "API Authentication"). NOT CERTIFIED.
//
// WebSocket (public/auth, grant_type client_signature):
//   StringToSign = Timestamp + "\n" + Nonce + "\n" + Data
//   Signature    = lower-case hex of HMAC-SHA256(ClientSecret, StringToSign)
// HTTP REST (Authorization: deri-hmac-sha256 id=..,ts=..,sig=..,nonce=..):
//   RequestData  = UPPERCASE(method) + "\n" + URI + "\n" + Body + "\n"
//   StringToSign = Timestamp + "\n" + Nonce + "\n" + RequestData
// The timestamp is milliseconds since the epoch and is valid for 60 seconds.
// Only shared-secret (HMAC) keys are implemented; Deribit's Ed25519 and RSA
// keys sign the same string differently and are not supported.

#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "crypto_common/Hmac.hpp"

namespace deribit::oe
{
  using crypto_common::hmac_sha256_hex;

  std::string ws_signature(std::string_view secret, uint64_t ts_ms, std::string_view nonce,
                           std::string_view data);

  std::string rest_signature(std::string_view secret, uint64_t ts_ms, std::string_view nonce,
                             std::string_view method, std::string_view uri, std::string_view body);

  // The params of public/auth with grant_type client_signature.
  nlohmann::json auth_params(const std::string &client_id, std::string_view secret, uint64_t ts_ms,
                             const std::string &nonce, const std::string &data = "");

  // The value of the Authorization header for an HTTP REST request.
  std::string rest_authorization(const std::string &client_id, std::string_view secret, uint64_t ts_ms,
                                 const std::string &nonce, std::string_view method, std::string_view uri,
                                 std::string_view body);

  // Eight random characters [a-z0-9], from the OS random source.
  std::string make_nonce();
}
