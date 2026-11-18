#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// OKX V5 request signing (docs-v5, "REST Authentication" and "WebSocket /
// Login"), and the key file.
//
//   sign = Base64(HMAC-SHA256(secret, timestamp + METHOD + requestPath + body))
//
// REST: timestamp is ISO 8601 UTC with milliseconds (2020-12-08T09:08:57.715Z),
// requestPath includes the query string, body is the JSON body or empty. The
// server rejects a timestamp more than 30 s from its clock (error 50102).
// WebSocket login: timestamp is Unix seconds, method "GET", requestPath
// "/users/self/verify", body empty; the login expires 30 s after timestamp.
//
// Keys are read from a JSON file outside the repository, by default
// ~/.kaspar/keys/okx_demo.json:
//   {"api_key": "...", "secret_key": "...", "passphrase": "..."}
// Nothing here prints, logs or copies a key into an error message.

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace okx::auth
{
  struct Keys
  {
    std::string api_key, secret_key, passphrase;
    bool complete() const { return !api_key.empty() && !secret_key.empty() && !passphrase.empty(); }
  };

  // Reads the key file. false with a reason in err (the reason never contains
  // the file's content).
  bool load_keys(const std::string &path, Keys &out, std::string &err);

  // ~/.kaspar/keys/<name>.json
  std::string default_key_path(const std::string &name);

  // Base64(HMAC-SHA256(key, msg)).
  std::string hmac_sha256_base64(const std::string &key, const std::string &msg);

  // The documented pre-hash string.
  std::string prehash(const std::string &timestamp, const std::string &method, const std::string &request_path,
                      const std::string &body);

  // OK-ACCESS-SIGN for a REST request.
  std::string rest_sign(const std::string &secret, const std::string &iso_timestamp, const std::string &method,
                        const std::string &request_path, const std::string &body);

  // ms since the epoch -> "2020-12-08T09:08:57.715Z".
  std::string iso_timestamp(uint64_t epoch_ms);

  // The WebSocket login request {"op":"login","args":[{apiKey, passphrase,
  // timestamp, sign}]} for Unix time `epoch_s`.
  nlohmann::json ws_login(const Keys &k, uint64_t epoch_s);
}
