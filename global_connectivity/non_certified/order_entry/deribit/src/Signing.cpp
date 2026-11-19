/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "deribit_oe/Signing.hpp"

#include <algorithm>
#include <cctype>
#include <random>

#include <openssl/rand.h>

namespace deribit::oe
{
  std::string ws_signature(std::string_view secret, uint64_t ts_ms, std::string_view nonce,
                           std::string_view data)
  {
    std::string s = std::to_string(ts_ms);
    s += '\n';
    s += nonce;
    s += '\n';
    s += data;
    return hmac_sha256_hex(secret, s);
  }

  std::string rest_signature(std::string_view secret, uint64_t ts_ms, std::string_view nonce,
                             std::string_view method, std::string_view uri, std::string_view body)
  {
    std::string m(method);
    std::transform(m.begin(), m.end(), m.begin(), [](unsigned char c) { return char(std::toupper(c)); });
    std::string s = std::to_string(ts_ms);
    s += '\n';
    s += nonce;
    s += '\n';
    s += m;
    s += '\n';
    s += uri;
    s += '\n';
    s += body;
    s += '\n';
    return hmac_sha256_hex(secret, s);
  }

  nlohmann::json auth_params(const std::string &client_id, std::string_view secret, uint64_t ts_ms,
                             const std::string &nonce, const std::string &data)
  {
    return {{"grant_type", "client_signature"},
            {"client_id", client_id},
            {"timestamp", ts_ms},
            {"nonce", nonce},
            {"data", data},
            {"signature", ws_signature(secret, ts_ms, nonce, data)}};
  }

  std::string rest_authorization(const std::string &client_id, std::string_view secret, uint64_t ts_ms,
                                 const std::string &nonce, std::string_view method, std::string_view uri,
                                 std::string_view body)
  {
    return "deri-hmac-sha256 id=" + client_id + ",ts=" + std::to_string(ts_ms) + ",nonce=" + nonce +
           ",sig=" + rest_signature(secret, ts_ms, nonce, method, uri, body);
  }

  std::string make_nonce()
  {
    static const char *chars = "abcdefghijklmnopqrstuvwxyz0123456789";
    unsigned char buf[8];
    if (RAND_bytes(buf, sizeof buf) != 1)
    {
      std::random_device rd;
      for (auto &b : buf)
        b = static_cast<unsigned char>(rd());
    }
    std::string s;
    for (unsigned char b : buf)
      s.push_back(chars[b % 36]);
    return s;
  }
}
