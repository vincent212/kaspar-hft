/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// The Nasdaq DROP trade message line (DROP 2.20, 1.6): parsing into plain
// fields, formatting (for the simulator and the tests), and the mapping to the
// normalized post_trade::msg::TradeCapture (feed.md, section 7).
//
// The byte layout is the generated nasdaq::drop::from_nasdaq::TradeMessageLine
// (nasdaq_drop.tbl); this file only converts its text fields.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "nasdaq_post_trade/drop_msgs.hpp"
#include "post_trade/msg/TradeCapture.hpp"

namespace nasdaq_post_trade
{
  using Line = nasdaq::drop::from_nasdaq::TradeMessageLine;
  constexpr size_t LINE_SIZE = sizeof(Line); // 126, without the CR/LF

  // DROP message types (DROP 2.20, 1.6, p.3).
  enum class DropType : char
  {
    Accepted = 'A',
    Executed = 'E',
    Canceled = 'X',
    Broken = 'B',
    Replaced = 'U',
    CanceledAIQ = 'Y',
    Correction = 'C',
  };

  struct DropReport
  {
    char type = 0;
    int64_t time_ns = 0; // since midnight Eastern Time
    std::string source;
    std::string order_token;
    std::string replaced_token;
    char side = ' '; // B, S, T, E
    int64_t shares = 0;
    std::string stock;
    int64_t price_e4 = 0; // price in units of 0.0001
    std::string firm;
    uint64_t reference = 0;
    uint64_t match_tif = 0; // match number (E, B, C) or time in force (others)
    char capacity = ' ';
    char liquidity = ' '; // liquidity code (E) or cancel reason (X)
    char clearing = ' ';
    int64_t ref_price_e4 = 0;
    char ref_price_type = ' ';
  };

  // Parse one line: LINE_SIZE bytes, optionally followed by CR/LF or LF.
  // On failure returns false and says why in *err.
  bool parse_line(std::string_view bytes, DropReport &out, std::string *err = nullptr);

  // Format a report as a line of LINE_SIZE bytes, plus CR/LF when crlf is set.
  std::string format_line(const DropReport &r, bool crlf = false);

  // Text conversions, exposed for the tests.
  bool parse_uint(std::string_view s, uint64_t &v);   // digits, padded with spaces
  bool parse_price(std::string_view s, int64_t &e4);  // "    12.8750", "12.875", "0"
  bool parse_time(std::string_view s, int64_t &ns);   // "34293.1", "34293.125"
  std::string format_price(int64_t e4);               // "12.8750"
  std::string format_time(int64_t ns);                // "34293.125"

  // What the mapping needs besides the line.
  struct MapContext
  {
    std::string session;   // SoupBinTCP session id
    uint64_t seq = 0;      // SoupBinTCP sequence number of the line
    uint32_t trade_date = 0;
    bool poss_dup = false;
    const std::map<std::string, int> *assets = nullptr; // symbol -> Kaspar asset
  };

  // The trade id of a report: one party's side of one match.
  std::string trade_id(const DropReport &r);

  // E -> New, C -> Amend, B -> Bust. Order events (A, X, U, Y) are not
  // trades: nullopt.
  std::optional<post_trade::msg::TradeCapture>
  to_trade_capture(const DropReport &r, std::string_view raw, const MapContext &c);

  // The Eastern Time calendar date (YYYYMMDD) of a UTC time
  // (post_trade::eastern_time::date).
  uint32_t eastern_date(int64_t utc_seconds);
}
