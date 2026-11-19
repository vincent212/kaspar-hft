#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Helpers shared by the Kraken handlers: JSON parsing that keeps the text of
// every decimal number, exact fixed-point decimals, Kraken's CRC32 checksum
// string, and RFC3339 timestamps.
//
// Kraken's WebSocket v2 sends prices and quantities as JSON numbers
// ("qty":0.00006120). The book checksum is computed over their text with the
// dot and leading zeros removed, so the trailing zeros matter and a double
// loses them. parse_raw() stores every non-integer number as a JSON string
// holding its text; integers (checksum, trade_id, sequence) stay numbers.

#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace kraken
{
  // Parse one frame. Floating-point numbers become strings with their exact
  // text. Throws nlohmann::json::parse_error on malformed input.
  nlohmann::json parse_raw(std::string_view text);

  // Decimal text of a price or quantity, whether it arrived as a string (REST,
  // the documentation's examples, parse_raw) or a number.
  std::string num_text(const nlohmann::json &v);

  // Decimal text -> fixed point with kScale decimals, exactly. false if the
  // text is not a plain decimal, has more than kScale decimals, or overflows.
  constexpr int kScale = 10;
  constexpr int64_t kOne = 10'000'000'000LL; // 10^kScale
  bool to_fixed(std::string_view text, int64_t &out);
  int64_t fixed(const nlohmann::json &v); // throws std::invalid_argument
  inline double fixed_to_double(int64_t f) { return double(f) / double(kOne); }

  // The checksum token of one price or quantity: '.' removed, then leading
  // zeros removed ("0.00100000" -> "100000"). An all-zero value gives "".
  std::string checksum_token(std::string_view text);

  // CRC32 (zlib / IEEE 802.3) of a string.
  uint32_t crc32(std::string_view s);

  // "2026-11-17T13:59:46.893523Z" -> ns since the epoch (UTC); up to 9
  // fractional digits. 0 if malformed.
  uint64_t parse_rfc3339(std::string_view s);
}
