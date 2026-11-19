/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// US Eastern civil time from UTC, for the venues that stamp their messages
// in Eastern time without saying so (Nasdaq OUCH timestamps are nanoseconds
// since midnight; Nasdaq DROP lines are dated by the trading day). Uses the
// US daylight-saving rule in force since 2007: from 2:00 EST on the second
// Sunday of March to 2:00 EDT on the first Sunday of November. No time zone
// database is read, so the result does not depend on the host's settings.
//
// Shared by order_entry/nasdaq_ouch (OuchCodec) and post_trade/nasdaq
// (DropLine); both Makefiles compile EasternTime.cpp in.

#include <cstdint>

namespace post_trade::eastern_time
{
  // Days since 1970-01-01 of a proleptic Gregorian date, and back
  // (H. Hinnant's algorithms).
  int64_t days_from_civil(int64_t y, unsigned m, unsigned d);
  void civil_from_days(int64_t days, int64_t &y, unsigned &m, unsigned &d);

  // Whether daylight time is in force at a UTC instant.
  bool dst(int64_t utc_seconds);

  // Seconds to add to UTC for Eastern local time: -4 h (EDT) or -5 h (EST).
  int64_t offset(int64_t utc_seconds);

  // The Eastern calendar date of a UTC instant, as YYYYMMDD.
  uint32_t date(int64_t utc_seconds);

  // The UTC time of the most recent Eastern midnight at or before utc_seconds.
  int64_t midnight(int64_t utc_seconds);
}
