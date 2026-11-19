#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// The decimal text the venues send ("95900.10", "-0.00192834") as an integer
// mantissa and a count of decimals, exactly: no floating point.

#include <cstdint>
#include <string>

namespace post_trade
{
  // "123.45" -> mantissa 12345, decimals 2; "-1.5" -> -15, 1; "7" -> 7, 0.
  // An optional leading '-', digits, at most one '.'. false for anything
  // else (an exponent, a second '.', spaces), for no digits at all, and when
  // the mantissa would not fit an int64_t.
  inline bool parse_decimal(const std::string &s, int64_t &mantissa, int &decimals)
  {
    size_t i = 0;
    bool neg = false;
    if (!s.empty() && s[0] == '-')
    {
      neg = true;
      i = 1;
    }
    int64_t m = 0;
    int d = 0;
    bool dot = false, digit = false;
    for (; i < s.size(); ++i)
    {
      const char c = s[i];
      if (c == '.')
      {
        if (dot)
          return false;
        dot = true;
        continue;
      }
      if (c < '0' || c > '9')
        return false;
      if (m > (INT64_MAX - (c - '0')) / 10)
        return false;
      m = m * 10 + (c - '0');
      digit = true;
      if (dot)
        ++d;
    }
    if (!digit)
      return false;
    mantissa = neg ? -m : m;
    decimals = d;
    return true;
  }
}
