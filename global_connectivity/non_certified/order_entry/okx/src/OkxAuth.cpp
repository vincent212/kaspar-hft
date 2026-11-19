/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "okx_oe/OkxAuth.hpp"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>

#include <openssl/evp.h>
#include <openssl/hmac.h>

namespace okx::auth
{
  bool load_keys(const std::string &path, Keys &out, std::string &err)
  {
    std::ifstream f(path);
    if (!f)
    {
      err = "cannot read key file " + path;
      return false;
    }
    try
    {
      auto j = nlohmann::json::parse(f);
      out.api_key = j.value("api_key", std::string());
      out.secret_key = j.value("secret_key", std::string());
      out.passphrase = j.value("passphrase", std::string());
    }
    catch (const std::exception &)
    {
      err = "key file " + path + " is not valid JSON";
      return false;
    }
    if (!out.complete())
    {
      err = "key file " + path + " lacks api_key, secret_key or passphrase";
      return false;
    }
    return true;
  }

  std::string default_key_path(const std::string &name)
  {
    const char *home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/.kaspar/keys/" + name + ".json";
  }

  std::string hmac_sha256_base64(const std::string &key, const std::string &msg)
  {
    unsigned char mac[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    HMAC(EVP_sha256(), key.data(), int(key.size()), reinterpret_cast<const unsigned char *>(msg.data()), msg.size(),
         mac, &len);
    std::string out(4 * ((len + 2) / 3), '\0');
    const int n = EVP_EncodeBlock(reinterpret_cast<unsigned char *>(out.data()), mac, int(len));
    out.resize(size_t(n));
    return out;
  }

  std::string prehash(const std::string &timestamp, const std::string &method, const std::string &request_path,
                      const std::string &body)
  {
    return timestamp + method + request_path + body;
  }

  std::string rest_sign(const std::string &secret, const std::string &iso_ts, const std::string &method,
                        const std::string &request_path, const std::string &body)
  {
    return hmac_sha256_base64(secret, prehash(iso_ts, method, request_path, body));
  }

  std::string iso_timestamp(uint64_t epoch_ms)
  {
    const std::time_t s = std::time_t(epoch_ms / 1000);
    std::tm tm{};
    gmtime_r(&s, &tm);
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", tm.tm_year + 1900, tm.tm_mon + 1,
                  tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, int(epoch_ms % 1000));
    return buf;
  }

  nlohmann::json ws_login(const Keys &k, uint64_t epoch_s)
  {
    const std::string ts = std::to_string(epoch_s);
    nlohmann::json arg = {{"apiKey", k.api_key},
                          {"passphrase", k.passphrase},
                          {"timestamp", ts},
                          {"sign", hmac_sha256_base64(k.secret_key, prehash(ts, "GET", "/users/self/verify", ""))}};
    return {{"op", "login"}, {"args", nlohmann::json::array({arg})}};
  }
}
