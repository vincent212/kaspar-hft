/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "kraken_md/KrakenJson.hpp"

#include <cstdio>
#include <ctime>
#include <stdexcept>
#include <vector>

#include <zlib.h>

namespace kraken
{
  namespace
  {
    using json = nlohmann::json;

    // A SAX handler that builds the DOM, storing floats as their text.
    struct RawSax
    {
      json root;
      std::vector<json *> stack;
      json *key_slot = nullptr;
      bool have_root = false;

      json *put(json v)
      {
        if (stack.empty())
        {
          root = std::move(v);
          have_root = true;
          return &root;
        }
        json &top = *stack.back();
        if (top.is_array())
        {
          top.push_back(std::move(v));
          return &top.back();
        }
        *key_slot = std::move(v);
        return key_slot;
      }

      bool null() { put(nullptr); return true; }
      bool boolean(bool b) { put(b); return true; }
      bool number_integer(json::number_integer_t v) { put(v); return true; }
      bool number_unsigned(json::number_unsigned_t v) { put(v); return true; }
      bool number_float(json::number_float_t, const json::string_t &s) { put(s); return true; }
      bool string(json::string_t &s) { put(s); return true; }
      bool binary(json::binary_t &b) { put(json::binary(b)); return true; }
      bool start_object(std::size_t)
      {
        stack.push_back(put(json::object()));
        return true;
      }
      bool key(json::string_t &k)
      {
        key_slot = &(*stack.back())[k];
        return true;
      }
      bool end_object() { stack.pop_back(); return true; }
      bool start_array(std::size_t)
      {
        stack.push_back(put(json::array()));
        return true;
      }
      bool end_array() { stack.pop_back(); return true; }
      bool parse_error(std::size_t, const std::string &, const nlohmann::detail::exception &e)
      {
        if (auto *p = dynamic_cast<const json::parse_error *>(&e))
          throw *p;
        throw std::invalid_argument(e.what());
      }
    };
  }

  nlohmann::json parse_raw(std::string_view text)
  {
    RawSax sax;
    json::sax_parse(text.begin(), text.end(), &sax);
    return std::move(sax.root);
  }

  std::string num_text(const nlohmann::json &v)
  {
    if (v.is_string())
      return v.get<std::string>();
    if (v.is_number_integer() || v.is_number_unsigned())
      return v.dump();
    if (v.is_number_float())
    {
      // Only reached for a DOM built by nlohmann::json::parse; the shortest
      // round-trip text, which may differ from what was sent.
      return v.dump();
    }
    throw std::invalid_argument("not a number: " + v.dump());
  }

  bool to_fixed(std::string_view t, int64_t &out)
  {
    if (t.empty())
      return false;
    bool neg = false;
    size_t i = 0;
    if (t[0] == '-' || t[0] == '+')
    {
      neg = t[0] == '-';
      i = 1;
    }
    __int128 v = 0;
    int decimals = -1;
    bool digits = false;
    for (; i < t.size(); ++i)
    {
      char c = t[i];
      if (c == '.')
      {
        if (decimals >= 0)
          return false;
        decimals = 0;
        continue;
      }
      if (c < '0' || c > '9')
        return false; // no exponents: Kraken does not send them
      digits = true;
      if (decimals >= 0)
      {
        if (decimals == kScale)
        {
          if (c != '0')
            return false; // more precision than kScale
          continue;
        }
        ++decimals;
      }
      v = v * 10 + (c - '0');
      if (v > (__int128(1) << 100))
        return false;
    }
    if (!digits)
      return false;
    for (int d = decimals < 0 ? 0 : decimals; d < kScale; ++d)
      v *= 10;
    if (v > INT64_MAX)
      return false;
    out = neg ? -int64_t(v) : int64_t(v);
    return true;
  }

  int64_t fixed(const nlohmann::json &v)
  {
    int64_t out;
    auto t = num_text(v);
    if (!to_fixed(t, out))
      throw std::invalid_argument("not a decimal of at most 10 places: " + t);
    return out;
  }

  std::string checksum_token(std::string_view text)
  {
    std::string s;
    s.reserve(text.size());
    for (char c : text)
      if (c != '.')
        s.push_back(c);
    size_t z = s.find_first_not_of('0');
    return z == std::string::npos ? std::string() : s.substr(z);
  }

  uint32_t crc32(std::string_view s)
  {
    return uint32_t(::crc32(::crc32(0L, Z_NULL, 0), reinterpret_cast<const Bytef *>(s.data()), uInt(s.size())));
  }

  uint64_t parse_rfc3339(std::string_view sv)
  {
    std::string s(sv);
    int Y, M, D, h, m, sec, n = 0;
    if (std::sscanf(s.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d%n", &Y, &M, &D, &h, &m, &sec, &n) != 6)
      return 0;
    std::tm tm{};
    tm.tm_year = Y - 1900;
    tm.tm_mon = M - 1;
    tm.tm_mday = D;
    tm.tm_hour = h;
    tm.tm_min = m;
    tm.tm_sec = sec;
    const int64_t epoch = int64_t(timegm(&tm));
    uint64_t frac = 0;
    int digits = 0;
    if (size_t(n) < s.size() && s[n] == '.')
      for (size_t i = n + 1; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i)
        if (digits < 9)
        {
          frac = frac * 10 + uint64_t(s[i] - '0');
          ++digits;
        }
    for (; digits < 9; ++digits)
      frac *= 10;
    return uint64_t(epoch) * 1000000000ull + frac;
  }
}
