/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "kraken_oe/KrakenAuth.hpp"

#include <atomic>
#include <chrono>
#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>

namespace kraken::oe
{
  std::string base64_encode(std::string_view b)
  {
    std::string out(4 * ((b.size() + 2) / 3), '\0');
    int n = EVP_EncodeBlock(reinterpret_cast<unsigned char *>(out.data()),
                            reinterpret_cast<const unsigned char *>(b.data()), int(b.size()));
    out.resize(size_t(n));
    return out;
  }

  bool base64_decode(std::string_view t, std::string &out)
  {
    if (t.size() % 4)
      return false;
    out.assign(3 * t.size() / 4, '\0');
    int n = EVP_DecodeBlock(reinterpret_cast<unsigned char *>(out.data()),
                            reinterpret_cast<const unsigned char *>(t.data()), int(t.size()));
    if (n < 0)
      return false;
    size_t pad = 0;
    if (!t.empty() && t.back() == '=')
      pad = t.size() >= 2 && t[t.size() - 2] == '=' ? 2 : 1;
    out.resize(size_t(n) - pad);
    return true;
  }

  std::string sha256(std::string_view b)
  {
    std::string out(SHA256_DIGEST_LENGTH, '\0');
    SHA256(reinterpret_cast<const unsigned char *>(b.data()), b.size(), reinterpret_cast<unsigned char *>(out.data()));
    return out;
  }

  std::string hmac_sha512(std::string_view key, std::string_view msg)
  {
    std::string out(64, '\0');
    unsigned int len = 64;
    HMAC(EVP_sha512(), key.data(), int(key.size()), reinterpret_cast<const unsigned char *>(msg.data()), msg.size(),
         reinterpret_cast<unsigned char *>(out.data()), &len);
    out.resize(len);
    return out;
  }

  namespace
  {
    std::string secret_bytes(std::string_view secret_b64)
    {
      std::string k;
      if (!base64_decode(secret_b64, k))
        throw std::invalid_argument("API secret is not base64");
      return k;
    }
  }

  std::string spot_api_sign(std::string_view uri_path, std::string_view nonce, std::string_view post_data,
                            std::string_view secret_b64)
  {
    std::string msg(uri_path);
    msg += sha256(std::string(nonce) + std::string(post_data));
    return base64_encode(hmac_sha512(secret_bytes(secret_b64), msg));
  }

  std::string futures_authent(std::string_view post_data, std::string_view nonce, std::string_view endpoint_path,
                              std::string_view secret_b64)
  {
    std::string in(post_data);
    in += nonce;
    in += endpoint_path;
    return base64_encode(hmac_sha512(secret_bytes(secret_b64), sha256(in)));
  }

  std::string futures_sign_challenge(std::string_view challenge, std::string_view secret_b64)
  {
    return base64_encode(hmac_sha512(secret_bytes(secret_b64), sha256(challenge)));
  }

  std::string url_encode(std::string_view s)
  {
    static const char *hex = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : s)
    {
      if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
          c == '.' || c == '~')
        o.push_back(char(c));
      else
      {
        o.push_back('%');
        o.push_back(hex[c >> 4]);
        o.push_back(hex[c & 15]);
      }
    }
    return o;
  }

  uint64_t next_nonce()
  {
    static std::atomic<uint64_t> last{0};
    uint64_t now = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::system_clock::now().time_since_epoch())
                                .count());
    uint64_t prev = last.load();
    uint64_t next;
    do
      next = now > prev ? now : prev + 1;
    while (!last.compare_exchange_weak(prev, next));
    return next;
  }

  bool load_keys(const std::string &path, Keys &out, std::string &err)
  {
    std::ifstream f(path);
    if (!f)
    {
      err = "cannot read " + path;
      return false;
    }
    try
    {
      auto j = nlohmann::json::parse(f);
      out.api_key = j.value("api_key", std::string());
      out.api_secret = j.value("api_secret", std::string());
    }
    catch (const std::exception &)
    {
      err = path + " is not valid JSON";
      return false;
    }
    std::string tmp;
    if (out.api_key.empty() || out.api_secret.empty())
    {
      err = path + " needs api_key and api_secret";
      return false;
    }
    if (!base64_decode(out.api_secret, tmp))
    {
      err = path + ": api_secret is not base64";
      return false;
    }
    return true;
  }
}
