#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Exact parsing of the strings Coinbase Exchange sends: decimal prices and
// sizes ("63186.98", "0.00016074"), order ids (lowercase UUIDs) and ISO 8601
// times ("2026-11-18T17:25:47.637647565Z"). Shared by the market-data, order
// entry and post-trade handlers. No doubles: a size is held as an integer
// count of the product's base increment, a price as a count of its quote
// increment, so nothing is lost to binary floating point.

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <optional>
#include <string>
#include <string_view>

namespace coinbase_exchange
{
  // "0.01" -> 2, "1" -> 0, "0.00000001" -> 8: the decimals of an increment.
  // -1 if the string is not a plain non-negative decimal.
  inline int decimals_of(std::string_view s)
  {
    size_t dot = s.find('.');
    if (s.empty())
      return -1;
    for (size_t i = 0; i < s.size(); ++i)
      if (i != dot && (s[i] < '0' || s[i] > '9'))
        return -1;
    if (dot == std::string_view::npos)
      return 0;
    // trailing zeros do not add precision: "0.010" is 0.01
    size_t end = s.size();
    while (end > dot + 1 && s[end - 1] == '0')
      --end;
    return int(end - dot - 1);
  }

  // A non-negative decimal string as an integer number of 10^-decimals.
  // nullopt if the string is malformed, negative, too large for int64, or has
  // a non-zero digit beyond `decimals` (the venue sent more precision than
  // the product allows: a bug somewhere, never rounded silently).
  inline std::optional<int64_t> scaled(std::string_view s, int decimals)
  {
    if (s.empty() || decimals < 0 || decimals > 18)
      return std::nullopt;
    int64_t v = 0;
    int frac = -1; // digits seen after the dot; -1 before it
    for (char c : s)
    {
      if (c == '.')
      {
        if (frac >= 0)
          return std::nullopt;
        frac = 0;
        continue;
      }
      if (c < '0' || c > '9')
        return std::nullopt;
      if (frac >= 0)
      {
        if (frac >= decimals)
        {
          if (c != '0')
            return std::nullopt;
          continue;
        }
        ++frac;
      }
      if (v > (INT64_MAX - 9) / 10)
        return std::nullopt;
      v = v * 10 + (c - '0');
    }
    for (int f = frac < 0 ? 0 : frac; f < decimals; ++f)
    {
      if (v > INT64_MAX / 10)
        return std::nullopt;
      v *= 10;
    }
    return v;
  }

  // The inverse: 1234567, 8 -> "0.01234567". Trailing zeros are kept, so the
  // string has exactly `decimals` digits after the dot (none if 0).
  inline std::string format_scaled(int64_t v, int decimals)
  {
    bool neg = v < 0;
    uint64_t u = neg ? uint64_t(-(v + 1)) + 1 : uint64_t(v);
    std::string digits = std::to_string(u);
    if (decimals > 0)
    {
      if (int(digits.size()) <= decimals)
        digits.insert(0, size_t(decimals + 1 - int(digits.size())), '0');
      digits.insert(digits.size() - size_t(decimals), 1, '.');
    }
    return neg ? "-" + digits : digits;
  }

  // A UUID as two 64-bit halves. Coinbase order ids and client order ids are
  // UUIDs in the 8-4-4-4-12 hex form.
  struct Uuid
  {
    uint64_t hi = 0, lo = 0;
    bool operator==(const Uuid &o) const { return hi == o.hi && lo == o.lo; }
    bool operator!=(const Uuid &o) const { return !(*this == o); }
    bool operator<(const Uuid &o) const { return hi < o.hi || (hi == o.hi && lo < o.lo); }
    bool nil() const { return hi == 0 && lo == 0; }
  };

  struct UuidHash
  {
    size_t operator()(const Uuid &u) const noexcept
    {
      // v4 UUIDs are random: mixing the halves is enough.
      return size_t(u.hi ^ (u.lo * 0x9e3779b97f4a7c15ull));
    }
  };

  inline int hexval(char c)
  {
    if (c >= '0' && c <= '9')
      return c - '0';
    if (c >= 'a' && c <= 'f')
      return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
      return c - 'A' + 10;
    return -1;
  }

  // "d50ec984-77a8-460a-b958-66f114b0de9b" -> Uuid. nullopt unless the string
  // is 36 characters with hyphens at 8, 13, 18 and 23 and hex elsewhere.
  inline std::optional<Uuid> parse_uuid(std::string_view s)
  {
    if (s.size() != 36 || s[8] != '-' || s[13] != '-' || s[18] != '-' || s[23] != '-')
      return std::nullopt;
    Uuid u;
    int n = 0;
    for (size_t i = 0; i < 36; ++i)
    {
      if (i == 8 || i == 13 || i == 18 || i == 23)
        continue;
      int h = hexval(s[i]);
      if (h < 0)
        return std::nullopt;
      if (n < 16)
        u.hi = (u.hi << 4) | uint64_t(h);
      else
        u.lo = (u.lo << 4) | uint64_t(h);
      ++n;
    }
    return u;
  }

  // The 64-bit id Kaspar gives a Coinbase order: the UUID's high 64 bits with
  // the top bit cleared (the market-data handler keeps the top bit for the
  // synthetic ids it gives the rare second order with the same value). The
  // order manager reports the same id as the exchange order id, so an order
  // of ours can be found in the book.
  inline uint64_t order_id64(const Uuid &u) { return u.hi & ~(1ull << 63); }

  inline std::string to_string(const Uuid &u)
  {
    static const char *hex = "0123456789abcdef";
    std::string s(36, '-');
    int n = 0;
    for (size_t i = 0; i < 36; ++i)
    {
      if (i == 8 || i == 13 || i == 18 || i == 23)
        continue;
      uint64_t w = n < 16 ? u.hi : u.lo;
      int shift = 60 - 4 * (n % 16);
      s[i] = hex[(w >> shift) & 0xf];
      ++n;
    }
    return s;
  }

  // Coinbase requires ClOrdID to be a "variant 1 UUIDv4" in lowercase:
  // version nibble 4, variant bits 10.
  inline bool is_uuid_v4_lower(std::string_view s)
  {
    auto u = parse_uuid(s);
    if (!u)
      return false;
    for (char c : s)
      if (c >= 'A' && c <= 'F')
        return false;
    return ((u->hi >> 12) & 0xf) == 4 && ((u->lo >> 62) & 0x3) == 2;
  }

  // Set the version and variant bits of 128 random bits.
  inline Uuid make_uuid_v4(uint64_t r1, uint64_t r2)
  {
    Uuid u{r1, r2};
    u.hi = (u.hi & ~0xf000ull) | 0x4000ull;
    u.lo = (u.lo & ~(3ull << 62)) | (2ull << 62);
    return u;
  }

  // "2026-11-18T17:25:47.637647565Z" (any number of fraction digits, or none)
  // -> nanoseconds since the epoch, UTC. 0 if malformed.
  inline uint64_t parse_iso_time(std::string_view s)
  {
    if (s.size() < 19 || s[4] != '-' || s[7] != '-' || (s[10] != 'T' && s[10] != ' ') || s[13] != ':' ||
        s[16] != ':')
      return 0;
    auto num = [&](size_t at, size_t len) -> int {
      int v = 0;
      for (size_t i = at; i < at + len; ++i)
      {
        if (s[i] < '0' || s[i] > '9')
          return -1;
        v = v * 10 + (s[i] - '0');
      }
      return v;
    };
    int Y = num(0, 4), M = num(5, 2), D = num(8, 2), h = num(11, 2), m = num(14, 2), sec = num(17, 2);
    if (Y < 0 || M < 1 || D < 1 || h < 0 || m < 0 || sec < 0)
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
    if (s.size() > 19 && s[19] == '.')
      for (size_t i = 20; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i)
        if (digits < 9)
        {
          frac = frac * 10 + uint64_t(s[i] - '0');
          ++digits;
        }
    for (; digits < 9; ++digits)
      frac *= 10;
    return uint64_t(epoch) * 1000000000ull + frac;
  }

  // FIX UTCTimestamp "20230822-20:43:30.000" (any fraction, or none) -> ns
  // since the epoch. 0 if malformed.
  inline uint64_t parse_fix_time(std::string_view s)
  {
    if (s.size() < 17 || s[8] != '-' || s[11] != ':' || s[14] != ':')
      return 0;
    std::string iso;
    iso.reserve(s.size() + 3);
    iso.append(s.substr(0, 4)).append("-").append(s.substr(4, 2)).append("-").append(s.substr(6, 2));
    iso.append("T").append(s.substr(9));
    return parse_iso_time(iso);
  }

  // ns since the epoch -> FIX UTCTimestamp with exactly three fraction
  // digits, as Coinbase's Logon signature requires ("20230822-20:43:30.000").
  inline std::string fix_time_ms(uint64_t ns)
  {
    time_t secs = time_t(ns / 1000000000ull);
    unsigned ms = unsigned((ns / 1000000ull) % 1000ull);
    std::tm tm{};
    gmtime_r(&secs, &tm);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d%02d%02d-%02d:%02d:%02d.%03u", tm.tm_year + 1900, tm.tm_mon + 1,
                  tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, ms);
    return buf;
  }
}
