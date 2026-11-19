/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// Builds raw FIX 4.4 text for the tests: the body is written with '|' between
// fields; BodyLength and CheckSum are computed. LMAX publishes no sample
// messages, so the tests write each message from the fields its spec lists.

#include <cstdio>
#include <string>

namespace lmax::test
{
  inline std::string fixmsg(const std::string &type, const std::string &body, int seq = 1,
                         const std::string &sender = "LMXBDM", const std::string &target = "CLIENT",
                         const std::string &sending_time = "20261118-10:00:00.000")
  {
    std::string b = "35=" + type + "|49=" + sender + "|56=" + target + "|34=" + std::to_string(seq) + "|52=" +
                    sending_time + "|" + body;
    if (b.back() != '|')
      b += '|';
    for (auto &c : b)
      if (c == '|')
        c = '\x01';
    std::string m = "8=FIX.4.4\x01" "9=" + std::to_string(b.size()) + "\x01" + b;
    unsigned sum = 0;
    for (unsigned char c : m)
      sum += c;
    char cs[8];
    std::snprintf(cs, sizeof cs, "%03u", sum % 256);
    return m + "10=" + cs + "\x01";
  }

  // FIX text with SOH shown as '|', for messages.
  inline std::string visible(std::string s)
  {
    for (auto &c : s)
      if (c == '\x01')
        c = '|';
    return s;
  }
}
