/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// OuchCodec: what the generated OUCH 5.0 header (ouch50.tbl) does not cover:
// the Optional Appendage's TagValue elements, prices, reason codes, the
// venue's timestamps, and writing an outbound message as bytes (for the
// simulator and the test fake, which play Nasdaq's side).

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

#include "nasdaq_ouch/gen/ouch50.hpp"

namespace nasdaq::ouch
{
  // Prices: unsigned, 4 implied decimals (OUCH 5.0 1.2, p.3).
  constexpr uint64_t PRICE_SCALE = 10'000;
  constexpr uint64_t MAX_PRICE = 1'999'999'900;   // $199,999.9900
  constexpr uint64_t MARKET_PRICE = 0x7FFFFFFF;   // $214,748.3647: market order for a cross
  constexpr uint32_t MAX_QUANTITY = 999'999;      // "less than 1,000,000" (OUCH 5.0 2.1, p.4)

  // OptionTag values used here (OUCH 5.0 Appendix A, pp.21-23).
  enum OptionTag : uint8_t
  {
    TAG_FIRM = 2,           // 4 Alpha
    TAG_MIN_QTY = 3,        // 4 Integer
    TAG_SHARES_LOCATED = 25, // 1 Alpha, Y or N
    TAG_LOCATE_BROKER = 26, // 4 Alpha
    TAG_USER_REF_IDX = 28,  // 1 Numeric
  };

  // Builds an Optional Appendage: TagValue elements, each Length (1, the
  // bytes after itself), OptionTag (1), OptionValue (OUCH 5.0 1.2, p.4).
  class Appendage
  {
  public:
    Appendage &alpha(uint8_t tag, std::string_view v, size_t width)
    {
      start(tag, width);
      for (size_t i = 0; i < width; ++i)
        s_.push_back(i < v.size() ? v[i] : ' ');
      return *this;
    }
    Appendage &number(uint8_t tag, uint64_t v, size_t width)
    {
      start(tag, width);
      for (size_t i = width; i-- > 0;)
        s_.push_back(char((v >> (8 * i)) & 0xff));
      return *this;
    }
    const std::string &str() const { return s_; }

  private:
    void start(uint8_t tag, size_t width)
    {
      s_.push_back(char(1 + width));
      s_.push_back(char(tag));
    }
    std::string s_;
  };

  // Finds one option in an Optional Appendage (the bytes after Appendage
  // Length). Returns its value bytes, or nothing.
  inline std::optional<std::string_view> find_option(std::string_view app, uint8_t tag)
  {
    size_t i = 0;
    while (i < app.size())
    {
      size_t len = uint8_t(app[i]);
      if (len == 0 || i + 1 + len > app.size())
        return std::nullopt; // malformed
      if (uint8_t(app[i + 1]) == tag)
        return app.substr(i + 2, len - 1);
      i += 1 + len;
    }
    return std::nullopt;
  }

  // The bytes of an outbound message (Nasdaq's side): the struct as it is
  // on the wire, then its trailing field. For a message with Appendage
  // Length, the length is set from the tail.
  template <class M>
  std::string to_bytes(M m, std::string_view tail = {})
  {
    m.MessageType = M::type;
    if constexpr (requires { m.AppendageLength; })
      m.AppendageLength = uint16_t(tail.size());
    std::string s(reinterpret_cast<const char *>(&m), sizeof(m));
    s.append(tail);
    return s;
  }

  // Order Reject Reasons (OUCH 5.0 Appendix C, pp.24-25).
  const char *reject_reason_text(uint16_t code);
  // Order Cancel Reasons (OUCH 5.0 Appendix B, pp.23-24).
  const char *cancel_reason_text(char code);

  // OUCH timestamps are nanoseconds since midnight (OUCH 5.0 3.1); the spec
  // does not name the time zone. Taken as US Eastern time, Nasdaq's: the
  // epoch time, in ns, of the most recent Eastern midnight at or before
  // epoch_ns, using the US daylight-saving rule in force since 2007.
  uint64_t eastern_midnight_epoch_ns(uint64_t epoch_ns);
}
