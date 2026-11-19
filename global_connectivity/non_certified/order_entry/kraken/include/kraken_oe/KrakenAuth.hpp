#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Kraken request signing (feed.md section 2), and the key file.
//
//   spot REST  API-Sign = base64(HMAC-SHA512(base64dec(secret),
//                          uri_path + SHA256(nonce + post_data)))
//              (exchange/guides/rest/authentication)
//   futures REST  Authent = base64(HMAC-SHA512(base64dec(secret),
//                          SHA256(post_data + nonce + endpoint_path)))
//              (exchange/guides/futures/rest)
//   futures WS challenge  signed = base64(HMAC-SHA512(base64dec(secret),
//                          SHA256(challenge)))
//              (exchange/guides/futures/websockets)
//
// Keys are never in the repository, a log or a report: they are read at run
// time from a JSON file outside the tree (README.md says which).

#include <cstdint>
#include <string>
#include <string_view>

namespace kraken::oe
{
  std::string base64_encode(std::string_view bytes);
  // false on a malformed input.
  bool base64_decode(std::string_view text, std::string &out);
  std::string sha256(std::string_view bytes);                         // 32 raw bytes
  std::string hmac_sha512(std::string_view key, std::string_view msg); // 64 raw bytes

  // Throw std::invalid_argument if the secret is not base64.
  std::string spot_api_sign(std::string_view uri_path, std::string_view nonce, std::string_view post_data,
                            std::string_view secret_b64);
  std::string futures_authent(std::string_view post_data, std::string_view nonce, std::string_view endpoint_path,
                              std::string_view secret_b64);
  std::string futures_sign_challenge(std::string_view challenge, std::string_view secret_b64);

  // x-www-form-urlencoded value (RFC 3986 unreserved characters kept).
  std::string url_encode(std::string_view s);

  // Nonces: milliseconds since the epoch, strictly increasing within the
  // process (Kraken requires an always-increasing nonce per key).
  uint64_t next_nonce();

  // A key file: {"api_key": "...", "api_secret": "..."} (more fields are
  // ignored). false with the reason in err; the reason never contains the key.
  struct Keys
  {
    std::string api_key, api_secret;
  };
  bool load_keys(const std::string &path, Keys &out, std::string &err);
}
