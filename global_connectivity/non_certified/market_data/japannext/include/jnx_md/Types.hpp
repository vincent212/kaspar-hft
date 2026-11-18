#pragma once
/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Japannext PTS equities market data (ITCH over MoldUDP64, GLIMPSE snapshots):
// the types every part of the handler shares. Not certified; see README.md.

#include <array>
#include <cstdint>
#include <map>
#include <string>

#include "bfile/r_l3.hpp"
#include "enum/e_names.hpp"

namespace jnx_md
{
  // Where the handler's records go. securityID in every record is the Kaspar
  // asset id (Instrument::asset), and book() is called with it.
  struct Sink
  {
    virtual ~Sink() = default;
    // A record for one instrument's book.
    virtual void book(int32_t asset, const bfile::l3_t &r) = 0;
    // A record for every book on the channel: channel reset, gap.
    virtual void all_books(const bfile::l3_t &r) = 0;
    // A record for the recorder only: end of packet (as handler_if does).
    virtual void recorder(const bfile::l3_t &r) { (void)r; }
  };

  // Counters the handler keeps; the replay tool and the actor print them.
  struct Stats
  {
    // packets and sequencing
    uint64_t packets = 0, data_packets = 0, heartbeats = 0, end_of_session = 0;
    uint64_t short_packets = 0, dup_packets = 0, messages = 0, old_messages = 0;
    uint64_t gaps = 0, gap_messages = 0, rerequests = 0, escalations = 0;
    uint64_t snapshots_started = 0, snapshots_done = 0, snapshot_messages = 0;
    uint64_t buffered_packets = 0, session_changes = 0, resets = 0;
    uint64_t packets_ignored_after_eos = 0;
    // decoding
    uint64_t unknown = 0, malformed = 0, empty_blocks = 0;
    std::map<char, uint64_t> by_type;            // live ITCH messages by type code
    std::map<char, uint64_t> snapshot_by_type;   // GLIMPSE messages by type code
    // semantics
    uint64_t reference_prices = 0, unknown_book = 0, dup_add = 0;
    uint64_t unknown_exec = 0, unknown_delete = 0, unknown_replace = 0;
    uint64_t replace_to_existing = 0, over_exec = 0, wide_order_number = 0;
    uint64_t replace_keep_place = 0, replace_lose_place = 0;
    uint64_t seconds_wraps = 0;
    // timestamps that go back from one message to the next, and the largest step
    uint64_t time_backsteps = 0, max_backstep_ns = 0;
    // prices (section 8): off the Kaspar unit, off the venue's tick grid,
    // outside the daily limits
    uint64_t px_off_unit = 0, px_off_tick = 0, px_out_of_limits = 0;
    // the handler's own book: crossed at the end of a timestamp, by status
    uint64_t crossed_checks = 0, crossed_at_17 = 0, crossed_other = 0, locked_at_17 = 0;
    // records sent, by en::l3 code
    std::map<uint32_t, uint64_t> records;
  };

  // Japannext's market-hours phase for a group (section 7, Trading status).
  enum class Phase : uint8_t
  {
    Unknown,
    Pre,      // before start of market hours (Q)
    Open,     // between Q and M
    Closed    // after end of market hours (M)
  };

  const char *to_string(Phase p);

  // CME SecurityTradingStatus values used (kaspar_mapping.md).
  namespace status
  {
    constexpr uint8_t Halt = 2, Close = 4, Trading = 17, NotAvailable = 18, PreOpen = 21;
  }

  // UTC epoch nanoseconds of local midnight in Japan (JST, UTC+9, no daylight
  // saving) of the civil date y-m-d.
  uint64_t jst_midnight_ns(int y, int m, int d);

  // The trading-session date for a UTC epoch time: the JST calendar date six
  // hours earlier, so that the night session (17:00 to 06:00 JST) keeps the
  // date it started on (feed.md section 8). Returns y*10000+m*100+d.
  int session_date(uint64_t utc_ns);

  // jst_midnight_ns for a date written y*10000+m*100+d.
  uint64_t jst_midnight_ns(int yyyymmdd);

  // The market-hours phase the Market Events Schedule gives for a group at
  // `secs` seconds past the session date's midnight (decision 5 in review.md).
  Phase clock_phase(const std::string &group, uint64_t secs);

  // Key of an order book: SICC code and group, both as the 4-byte Alpha fields
  // carry them (space padded).
  using BookKey = std::array<char, 8>;
  inline BookKey book_key(const char (&sicc)[4], const char (&group)[4])
  {
    BookKey k;
    for (int i = 0; i < 4; ++i)
    {
      k[i] = sicc[i];
      k[4 + i] = group[i];
    }
    return k;
  }
}
