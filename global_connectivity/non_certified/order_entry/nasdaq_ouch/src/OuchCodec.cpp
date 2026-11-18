/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "nasdaq_ouch/OuchCodec.hpp"

namespace nasdaq::ouch
{
  const char *reject_reason_text(uint16_t code)
  {
    // OUCH 5.0 Appendix C, Order Reject Reasons, pp.24-25.
    static const char *const t[] = {
        nullptr,
        "Quote Unavailable",                      // 0x01
        "Destination Closed",                     // 0x02
        "Invalid Display",                        // 0x03
        "Invalid Max Floor",                      // 0x04
        "Invalid Peg Type",                       // 0x05
        "Fat Finger",                             // 0x06
        "Halted",                                 // 0x07
        "ISO Not Allowed",                        // 0x08
        "Invalid Side",                           // 0x09
        "Processing Error",                       // 0x0A
        "Cancel Pending",                         // 0x0B
        "Firm Not Authorized",                    // 0x0C
        "Invalid Min Quantity",                   // 0x0D
        "No Closing Reference Price",             // 0x0E
        "Other",                                  // 0x0F
        "Cancel Not Allowed",                     // 0x10
        "Pegging Not Allowed",                    // 0x11
        "Crossed Market",                         // 0x12
        "Invalid Quantity",                       // 0x13
        "Invalid Cross Order",                    // 0x14
        "Replace Not Allowed",                    // 0x15
        "Routing Not Allowed",                    // 0x16
        "Invalid Symbol",                         // 0x17
        "Test",                                   // 0x18
        "Late LOC Too Aggressive",                // 0x19
        "Retail Not Allowed",                     // 0x1A
        "Invalid Midpoint Post Only Price",       // 0x1B
        "Invalid Destination",                    // 0x1C
        "Invalid Price",                          // 0x1D
        "Shares Exceed Threshold",                // 0x1E
        "Exceeds Maximum Allowed Notional Value", // 0x1F
        "Risk: Aggregate Exposure Exceeded",      // 0x20
        "Risk: Market Impact",                    // 0x21
        "Risk: Restricted Stock",                 // 0x22
        "Risk: Short Sell Restricted",            // 0x23
        "Risk: ISO Not Allowed",                  // 0x24
        "Risk: Exceeds ADV Limit",                // 0x25
        "Risk: Fat Finger",                       // 0x26
        "Risk: Locate Required",                  // 0x27
        "Risk: Symbol Message Rate Restriction",  // 0x28
        "Risk: Port Message Rate Restriction",    // 0x29
        "Risk: Duplicate Message Rate Restriction", // 0x2A
        "Risk: Short Sell Not Allowed",           // 0x2B
        "Risk: Market Order Not Allowed",         // 0x2C
        "Risk: Pre-Market Not Allowed",           // 0x2D
        "Risk: Post-Market Not Allowed",          // 0x2E
        "Risk: Short Sell Exempt Not Allowed",    // 0x2F
        "Risk: Single Order Notional Exceeded",   // 0x30
        "Risk: Max Quantity Exceeded",            // 0x31
        "Reg SHO State Not Available",            // 0x32
        "Risk: IPO Market Buy Not Allowed",       // 0x33
    };
    if (code == 0x40)
      return "Invalid AIQ";
    if (code > 0 && code < sizeof(t) / sizeof(t[0]))
      return t[code];
    return "unknown reject reason";
  }

  const char *cancel_reason_text(char code)
  {
    // OUCH 5.0 Appendix B, Order Cancel Reasons, pp.23-24.
    switch (code)
    {
    case 'D': return "regulatory restriction";
    case 'E': return "closed";
    case 'F': return "post only: would be price slid for NMS";
    case 'G': return "post only: would be price slid by a displayed contra order";
    case 'H': return "halted after the opening cross";
    case 'I': return "immediate or cancel";
    case 'K': return "market collars";
    case 'Q': return "self match prevention";
    case 'S': return "supervisory";
    case 'T': return "timeout";
    case 'U': return "user requested";
    case 'X': return "open protection";
    case 'Z': return "system cancel";
    case 'e': return "direct listing capital raise exceeds shares offered";
    }
    return "unknown cancel reason";
  }

  namespace
  {
    constexpr int64_t NS_PER_HOUR = 3'600'000'000'000LL;
    constexpr int64_t NS_PER_DAY = 24 * NS_PER_HOUR;

    // Days since 1970-01-01 of a proleptic Gregorian date (H. Hinnant).
    int64_t days_from_civil(int64_t y, unsigned m, unsigned d)
    {
      y -= m <= 2;
      const int64_t era = (y >= 0 ? y : y - 399) / 400;
      const unsigned yoe = unsigned(y - era * 400);
      const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
      const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
      return era * 146097 + int64_t(doe) - 719468;
    }

    int64_t year_of_days(int64_t z)
    {
      z += 719468;
      const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
      const unsigned doe = unsigned(z - era * 146097);
      const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
      const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
      const unsigned mp = (5 * doy + 2) / 153;
      const unsigned m = mp < 10 ? mp + 3 : mp - 9;
      return int64_t(yoe) + era * 400 + (m <= 2);
    }

    // The n-th Sunday (1-based) of month m of year y, as days since epoch.
    int64_t nth_sunday(int64_t y, unsigned m, int n)
    {
      int64_t first = days_from_civil(y, m, 1);
      int64_t wd = (first + 4) % 7; // 1970-01-01 was a Thursday; 0 = Sunday
      if (wd < 0)
        wd += 7;
      return first + (7 - wd) % 7 + 7 * (n - 1);
    }

    // Daylight time from the second Sunday of March, 2:00 EST (7:00 UTC), to
    // the first Sunday of November, 2:00 EDT (6:00 UTC).
    bool eastern_dst(int64_t utc_ns)
    {
      int64_t y = year_of_days((utc_ns - 5 * NS_PER_HOUR) / NS_PER_DAY);
      int64_t start = nth_sunday(y, 3, 2) * NS_PER_DAY + 7 * NS_PER_HOUR;
      int64_t end = nth_sunday(y, 11, 1) * NS_PER_DAY + 6 * NS_PER_HOUR;
      return utc_ns >= start && utc_ns < end;
    }
  }

  uint64_t eastern_midnight_epoch_ns(uint64_t epoch_ns)
  {
    int64_t t = int64_t(epoch_ns);
    int64_t off = (eastern_dst(t) ? 4 : 5) * NS_PER_HOUR;
    int64_t local_day = (t - off) / NS_PER_DAY;
    // Midnight is never inside a transition (they happen at 2:00), so the
    // offset in force at midnight is that of the UTC instant it maps to.
    int64_t mid = local_day * NS_PER_DAY + 5 * NS_PER_HOUR;
    if (eastern_dst(mid))
      mid -= NS_PER_HOUR;
    return uint64_t(mid);
  }
}
