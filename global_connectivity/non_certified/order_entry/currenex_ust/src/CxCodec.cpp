/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "currenex_ust/CxCodec.hpp"

#include <chrono>

namespace currenex::ouch
{
  Frame deframe(const char *buf, size_t len, size_t max_unknown)
  {
    Frame f;
    if (len == 0)
      return f;
    if (buf[0] != SOH)
    {
      // Not at a frame: skip to the next SOH.
      size_t i = 1;
      while (i < len && buf[i] != SOH)
        ++i;
      f.st = Frame::garbage;
      f.consumed = i;
      return f;
    }
    if (len < 1 + HEADER_SIZE + 1)
      return f; // incomplete
    Header h;
    std::memcpy(&h, buf + 1, sizeof(h));
    f.seq = h.SequenceNumber;
    f.ts = h.Timestamp;
    f.type = buf[1 + HEADER_SIZE];
    const char *m = buf + 1 + HEADER_SIZE;
    size_t avail = len - 1 - HEADER_SIZE;
    auto d = from::decode(m, avail);
    if (d.st == currenex::status::incomplete)
      return f;
    if (d.st == currenex::status::ok)
    {
      if (avail < d.size + 1)
        return f; // the ETX has not arrived
      if (m[d.size] != ETX)
      {
        // The type's length does not end at an ETX: not a frame of this
        // protocol. Skip the SOH and look for the next one.
        f.st = Frame::garbage;
        f.consumed = 1;
        return f;
      }
      f.st = Frame::ok;
      f.consumed = 1 + HEADER_SIZE + d.size + 1;
      f.data = m;
      f.len = d.size;
      f.msg = d.msg;
      return f;
    }
    // Unknown type: no length to go by.
    for (size_t i = 1 + HEADER_SIZE + 1; i < len; ++i)
    {
      if (buf[i] != ETX)
        continue;
      if (i + 1 < len && buf[i + 1] == SOH)
      {
        f.st = Frame::unknown;
        f.consumed = i + 1;
        return f;
      }
    }
    if (len >= max_unknown)
    {
      f.st = Frame::unknown;
      f.consumed = len;
      return f;
    }
    f.st = Frame::incomplete;
    return f;
  }

  const char *error_text(int code)
  {
    switch (code)
    {
    case 0x00: return "No Error";
    case 0x01: return "Invalid Instrument";
    case 0x02: return "Invalid Side";
    case 0x03: return "Invalid Price";
    case 0x04: return "Invalid Expiry";
    case 0x05: return "Invalid Amount";
    case 0x06: return "Invalid Show Amount";
    case 0x07: return "Invalid Permission";
    case 0x08: return "Invalid Order";
    case 0x09: return "Invalid Order Type";
    case 0x0a: return "Invalid Client Order Id";
    case 0x0b: return "Invalid Credit";
    case 0x0c: return "Maximum Permitted Operations Exceeded";
    case 0x0d: return "Invalid New Client Order";
    case 0x0e: return "Order not Active";
    case 0x0f: return "Invalid Specified Amount";
    case 0x10: return "Maximum Number of Active Orders Exceeded";
    case 0x11: return "Rate precision error";
    case 0x12: return "Invalid Settlement Date";
    case 0x13: return "Cannot Replace Inactive Order";
    case 0x14: return "Order Min Greater Than System Min Max";
    case 0x15: return "Invalid Limit Rate Error";
    case 0x16: return "SEF Validation Failed";
    case 0x17: return "Cannot Replace Already Filled";
    case 0x18: return "Cannot Replace Order Is In Process";
    case 0x19: return "Cannot Cancel Already Filled";
    case 0x1a: return "Absolute Trading Limit Exceeded";
    case 0x1b: return "Application Not Available";
    case 0x1c: return "Trading Disabled Off Hours";
    case 0x1d: return "No Execution Relationship set up";
    case 0x1e: return "Invalid Routing User Permission";
    case 0x63: return "Invalid Error";
    }
    return "unlisted error code";
  }

  const char *logout_text(std::string_view c)
  {
    if (c == "A1") return "Replaced by New Session";
    if (c == "A2") return "Session Timed Out";
    if (c == "A3") return "Invalid Session Id";
    if (c == "A4") return "Internal Session Error";
    if (c == "A5") return "Authentication Failure";
    if (c == "A6") return "User Logout";
    if (c == "A7") return "Failed Message Delivery";
    if (c == "A8") return "Internal Session Closed";
    if (c == "A9") return "Second Missed Heartbeat";
    if (c == "A10") return "Invalid First Sequence Number";
    if (c == "A11") return "Invalid version";
    return "unlisted logout reason";
  }

  int64_t epoch_ms_now()
  {
    using namespace std::chrono;
    return int64_t(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
  }
}
