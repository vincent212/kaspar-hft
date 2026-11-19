/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "post_trade/EasternTime.hpp"

namespace post_trade::eastern_time
{
  namespace
  {
    constexpr int64_t DAY = 86400;
    constexpr int64_t HOUR = 3600;

    int64_t floor_div(int64_t a, int64_t b) { return (a >= 0 ? a : a - (b - 1)) / b; }

    // The n-th Sunday (n >= 1) of month m of year y, as days since 1970-01-01.
    int64_t nth_sunday(int64_t y, unsigned m, unsigned n)
    {
      const int64_t first = days_from_civil(y, m, 1);
      const int64_t wd = ((first % 7) + 7 + 4) % 7; // 1970-01-01 was a Thursday; 0 = Sunday
      return first + (7 - wd) % 7 + 7 * int64_t(n - 1);
    }
  }

  int64_t days_from_civil(int64_t y, unsigned m, unsigned d)
  {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = unsigned(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + int64_t(doe) - 719468;
  }

  void civil_from_days(int64_t z, int64_t &y, unsigned &m, unsigned &d)
  {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = unsigned(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = int64_t(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    y += m <= 2;
  }

  bool dst(int64_t utc_seconds)
  {
    // The year is taken in EST: a transition is never within 5 hours of a
    // UTC year boundary, so this picks the right year's rule.
    int64_t y;
    unsigned m, d;
    civil_from_days(floor_div(utc_seconds - 5 * HOUR, DAY), y, m, d);
    // 2:00 EST on the second Sunday of March is 07:00 UTC; 2:00 EDT on the
    // first Sunday of November is 06:00 UTC.
    const int64_t start = nth_sunday(y, 3, 2) * DAY + 7 * HOUR;
    const int64_t end = nth_sunday(y, 11, 1) * DAY + 6 * HOUR;
    return utc_seconds >= start && utc_seconds < end;
  }

  int64_t offset(int64_t utc_seconds) { return (dst(utc_seconds) ? -4 : -5) * HOUR; }

  uint32_t date(int64_t utc_seconds)
  {
    int64_t y;
    unsigned m, d;
    civil_from_days(floor_div(utc_seconds + offset(utc_seconds), DAY), y, m, d);
    return uint32_t(y * 10000 + m * 100 + d);
  }

  int64_t midnight(int64_t utc_seconds)
  {
    const int64_t local_day = floor_div(utc_seconds + offset(utc_seconds), DAY);
    // Midnight is never inside a transition (they happen at 2:00), so the
    // offset in force at midnight is that of the UTC instant it maps to:
    // 05:00 UTC in standard time, 04:00 UTC in daylight time.
    int64_t mid = local_day * DAY + 5 * HOUR;
    if (dst(mid))
      mid -= HOUR;
    return mid;
  }
}
