#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Bybit V5 request signing (HMAC-SHA256 keys) and credentials. NOT CERTIFIED.
//
// REST (https://bybit-exchange.github.io/docs/v5/guide, "Create A Request"):
//   GET   sign  timestamp + api_key + recv_window + queryString
//   POST  sign  timestamp + api_key + recv_window + jsonBodyString
//   HMAC-SHA256 with the secret, lower-case hex, in X-BAPI-SIGN, with
//   X-BAPI-API-KEY, X-BAPI-TIMESTAMP (ms) and X-BAPI-RECV-WINDOW. The body or
//   query string signed must be byte for byte the one sent. Bybit accepts
//   server_time - recv_window <= timestamp < server_time + 1000.
// WebSocket (https://bybit-exchange.github.io/docs/v5/ws/connect):
//   {"op":"auth","args":[api_key, expires, signature]} with signature =
//   HMAC-SHA256(secret, "GET/realtime" + expires), expires in ms.
//
// Credentials come from a JSON file outside the repository (default
// ~/.kaspar/keys/bybit_testnet.json): {"api_key": "...", "api_secret": "..."}.
// They are never logged; nothing here prints them.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <openssl/hmac.h>

namespace bybit::auth
{
  struct Credentials
  {
    std::string api_key;
    std::string api_secret;
    bool empty() const { return api_key.empty() || api_secret.empty(); }
  };

  inline std::string hmac_sha256_hex(const std::string &key, const std::string &msg)
  {
    unsigned char out[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    HMAC(EVP_sha256(), key.data(), int(key.size()), reinterpret_cast<const unsigned char *>(msg.data()),
         msg.size(), out, &len);
    static const char *hex = "0123456789abcdef";
    std::string s(len * 2, '0');
    for (unsigned i = 0; i < len; ++i)
    {
      s[2 * i] = hex[out[i] >> 4];
      s[2 * i + 1] = hex[out[i] & 15];
    }
    return s;
  }

  // The string Bybit signs for a REST request (payload: query string or body).
  inline std::string rest_prehash(uint64_t ts_ms, const std::string &api_key, int recv_window_ms,
                                  const std::string &payload)
  {
    return std::to_string(ts_ms) + api_key + std::to_string(recv_window_ms) + payload;
  }

  inline std::string rest_sign(const Credentials &c, uint64_t ts_ms, int recv_window_ms,
                               const std::string &payload)
  {
    return hmac_sha256_hex(c.api_secret, rest_prehash(ts_ms, c.api_key, recv_window_ms, payload));
  }

  inline std::string ws_prehash(uint64_t expires_ms) { return "GET/realtime" + std::to_string(expires_ms); }

  // The auth frame for a private WebSocket.
  inline std::string ws_auth_frame(const Credentials &c, uint64_t expires_ms)
  {
    nlohmann::json j = {{"op", "auth"},
                        {"args", {c.api_key, expires_ms, hmac_sha256_hex(c.api_secret, ws_prehash(expires_ms))}}};
    return j.dump();
  }

  // ~ expanded.
  inline std::string expand_home(const std::string &p)
  {
    if (!p.empty() && p[0] == '~')
    {
      const char *h = std::getenv("HOME");
      return std::string(h ? h : "") + p.substr(1);
    }
    return p;
  }

  // Reads {"api_key": ..., "api_secret": ...}. false with the reason in err
  // (never the key material).
  inline bool load(const std::string &path, Credentials &out, std::string &err)
  {
    std::ifstream in(expand_home(path));
    if (!in)
    {
      err = "cannot open key file " + path;
      return false;
    }
    try
    {
      auto j = nlohmann::json::parse(in);
      out.api_key = j.value("api_key", std::string());
      out.api_secret = j.value("api_secret", std::string());
    }
    catch (const std::exception &)
    {
      err = "key file " + path + " is not valid JSON";
      return false;
    }
    if (out.empty())
    {
      err = "key file " + path + " lacks api_key or api_secret";
      return false;
    }
    return true;
  }
}
