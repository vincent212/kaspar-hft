/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "nasdaq_ouch/OuchCodec.hpp"

#include "post_trade/EasternTime.hpp"

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

  uint64_t eastern_midnight_epoch_ns(uint64_t epoch_ns)
  {
    constexpr uint64_t NS = 1'000'000'000;
    return uint64_t(post_trade::eastern_time::midnight(int64_t(epoch_ns / NS))) * NS;
  }
}
