#pragma once
// Epoch nanoseconds of midnight US Eastern on a trading date. ITCH timestamps
// are nanoseconds since that midnight (feed.md section 3; time zone settled by
// the Sample, section 10).
#include <cstdint>

namespace nasdaq_md
{
// Days since 1970-01-01 of a civil date (H. Hinnant's algorithm).
constexpr int64_t days_from_civil(int y, unsigned m, unsigned d)
{
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = unsigned(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + int64_t(doe) - 719468;
}

// 0 = Sunday.
constexpr int weekday(int64_t days) { return int((days % 7 + 11) % 7); }

// Day of the n-th Sunday (n from 1) of a month.
constexpr int64_t nth_sunday(int y, unsigned m, int n)
{
  const int64_t first = days_from_civil(y, m, 1);
  const int wd = weekday(first);
  return first + (7 - wd) % 7 + 7 * (n - 1);
}

// US daylight saving (since 2007): from the second Sunday of March 02:00 to the
// first Sunday of November 02:00, local. Midnight of the March change day is
// still EST; midnight of the November change day is still EDT.
constexpr bool midnight_is_edt(int y, unsigned m, unsigned d)
{
  const int64_t day = days_from_civil(y, m, d);
  return day > nth_sunday(y, 3, 2) && day <= nth_sunday(y, 11, 1);
}

constexpr uint64_t et_midnight_epoch_ns(int y, unsigned m, unsigned d)
{
  const int64_t offset_h = midnight_is_edt(y, m, d) ? 4 : 5;
  return uint64_t(days_from_civil(y, m, d) * 86400 + offset_h * 3600) * 1000000000ull;
}

// "YYYYMMDD" -> epoch ns of midnight ET; 0 if malformed.
inline uint64_t et_midnight_epoch_ns(const char *yyyymmdd)
{
  int v[8];
  for (int i = 0; i < 8; ++i)
  {
    if (yyyymmdd[i] < '0' || yyyymmdd[i] > '9')
      return 0;
    v[i] = yyyymmdd[i] - '0';
  }
  if (yyyymmdd[8] != 0)
    return 0;
  const int y = v[0] * 1000 + v[1] * 100 + v[2] * 10 + v[3];
  const unsigned m = unsigned(v[4] * 10 + v[5]), d = unsigned(v[6] * 10 + v[7]);
  if (m < 1 || m > 12 || d < 1 || d > 31)
    return 0;
  return et_midnight_epoch_ns(y, m, d);
}

static_assert(et_midnight_epoch_ns(2026, 6, 12) == 1781236800000000000ull); // 2026-06-12 04:00 UTC
static_assert(et_midnight_epoch_ns(2026, 1, 15) == 1768453200000000000ull); // 2026-01-15 05:00 UTC
} // namespace nasdaq_md
