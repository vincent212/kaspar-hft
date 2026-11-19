#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// HMAC-SHA256 over OpenSSL, as the venues' request signing needs it: Bybit
// and Deribit sign with the lower-case hex digest.

#include <string>
#include <string_view>

#include <openssl/evp.h>
#include <openssl/hmac.h>

namespace crypto_common
{
  // The 32 raw bytes.
  inline std::string hmac_sha256(std::string_view key, std::string_view msg)
  {
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    HMAC(EVP_sha256(), key.data(), int(key.size()), reinterpret_cast<const unsigned char *>(msg.data()),
         msg.size(), md, &len);
    return std::string(reinterpret_cast<const char *>(md), len);
  }

  // Lower-case hex, 64 characters.
  inline std::string hmac_sha256_hex(std::string_view key, std::string_view msg)
  {
    static const char *hex = "0123456789abcdef";
    const std::string raw = hmac_sha256(key, msg);
    std::string out;
    out.reserve(raw.size() * 2);
    for (unsigned char c : raw)
    {
      out.push_back(hex[c >> 4]);
      out.push_back(hex[c & 15]);
    }
    return out;
  }
}
