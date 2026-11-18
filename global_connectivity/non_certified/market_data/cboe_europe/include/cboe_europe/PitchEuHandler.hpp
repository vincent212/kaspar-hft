#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// Cboe Europe Multicast PITCH (BXE, CXE) to Kaspar L3 records. NOT CERTIFIED:
// tested only on synthetic packets; see ../../README.md.
//
// A plain class with no network and no actor, so that it is tested from bytes
// (as sim/BinanceSync is). The caller feeds it:
//   - every UDP payload from the real-time and gap-response multicast groups
//     of one book (BXE or CXE), A/B-arbitrated or not: duplicates are dropped
//     by sequence;
//   - the PITCH messages of a spin, between spin_begin() and spin_finished(),
//     after its own Spin Server session has decoded the spin-layer messages.
// It emits Kaspar records through Emit, with the Kaspar securityID of the book
// each record is for (-1 for records that belong to no book: the end-of-packet
// record, which goes to the recorder only, as in mdp3/handler_if.hpp). Gaps
// are reported through OnGap; requesting retransmission or a spin is the
// caller's job.
//
// The translation follows feed.md section 7.

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "bfile/r_l3.hpp"

namespace cboe_europe
{
  // London midnight of a trade date (YYYYMMDD) in nanoseconds since the Unix
  // epoch. PITCH Time is "whole seconds from midnight London time" (EU 4.1, p.14).
  uint64_t london_midnight_ns(uint32_t yyyymmdd);

  // Instrument loader: lines "<Cboe symbol>,<Kaspar securityID>"; '#' starts a comment.
  std::unordered_map<std::string, int32_t> load_instruments(const std::string &path);

  // Trading Status (EU 4.13, p.27) to CME SecurityTradingStatus (feed.md section 7).
  uint8_t trading_status(char pitch_status);

  class PitchEuHandler
  {
  public:
    // Which Trade - Extended messages send a print (review.md decision 1).
    enum class ExtendedPrints
    {
      none,     // never
      on_book,  // MMT level 1 in {1, 3, 5}, not negotiated, not an amendment or cancellation, price forming, price > 0
      all,      // every one with a price
    };

    struct Config
    {
      char venue = 0;                                  // en::x::CBOEBXE or en::x::CBOECXE
      uint32_t trade_date = 0;                         // YYYYMMDD
      std::unordered_map<std::string, int32_t> symbols; // Cboe symbol (trimmed) -> securityID
      size_t max_buffer = 1u << 20;                    // messages kept per unit while not live
      ExtendedPrints extended_prints = ExtendedPrints::on_book;
      bool honour_transactions = true;                 // review.md decision 2
    };

    using Emit = std::function<void(int32_t sec_id, const bfile::l3_t &)>;
    // A unit has a gap starting at `first`; `count` messages known missing
    // (0: unknown, e.g. a buffer overflow; only a spin can recover it).
    using OnGap = std::function<void(uint8_t unit, uint32_t first, uint32_t count)>;

    enum class UnitState
    {
      live,      // applying messages in sequence
      gapped,    // waiting for retransmission or a spin; buffering
      spinning,  // between spin_begin and spin_finished; buffering live
    };

    struct Stats
    {
      uint64_t packets = 0, heartbeats = 0, applied = 0, duplicates = 0, buffered = 0;
      uint64_t gaps = 0, overflows = 0, unknown = 0, malformed = 0, unsequenced = 0;
      uint64_t unknown_orders = 0, unknown_symbols = 0, si_quotes = 0, trade_breaks = 0;
      uint64_t extended_printed = 0, extended_skipped = 0, over_executions = 0, spins = 0;
      uint64_t records = 0;
    };

    PitchEuHandler(Config cfg, Emit emit, OnGap on_gap = {});

    // One UDP payload (a Sequenced Unit Header and its messages). Returns false
    // if the payload is not a well-formed unit (it is then ignored).
    bool on_packet(const char *buf, size_t len);

    // Spin of one unit. spin_usable() says whether a Spin Image Available
    // sequence can be requested: every message after it must be buffered or
    // still to come (EU Appendix C, p.85). spin_begin() is called on an accepted
    // Spin Response; it clears the unit's books.
    bool spin_usable(uint8_t unit, uint32_t image_seq) const;
    void spin_begin(uint8_t unit, uint32_t image_seq);
    void spin_message(uint8_t unit, const char *buf, size_t len); // one PITCH message, from its Length byte
    void spin_finished(uint8_t unit);

    UnitState state(uint8_t unit) const { return units_[unit].state; }
    uint32_t expected(uint8_t unit) const { return units_[unit].expected; }
    size_t orders(uint8_t unit) const { return units_[unit].orders.size(); }
    const Stats &stats() const { return stats_; }

  private:
    struct Order
    {
      int32_t sec_id;  // -1: symbol not in the instrument loader; tracked, not emitted
      char side;       // '0' buy, '1' sell
      double px;
      uint32_t qty;
    };

    struct Unit
    {
      UnitState state = UnitState::live;
      uint32_t expected = 1;            // next sequence to apply
      std::map<uint32_t, std::string> buffer; // sequence -> message bytes
      uint32_t spin_seq = 0;
      uint64_t time_s = 0;              // last Time message
      uint64_t last_tx = 0;             // last transactTime stamped
      uint64_t event_tx = 0;            // transactTime of the open event
      bool in_txn = false;
      bool closed = false;
      bool overflowed = false;          // buffer overflowed since the last spin
      std::unordered_map<uint64_t, Order> orders;
      std::set<int32_t> books;          // securityIDs seen on this unit
      std::vector<std::pair<int32_t, bfile::l3_t>> pending;
    };

    Config cfg_;
    Emit emit_;
    OnGap on_gap_;
    uint64_t midnight_ns_;
    std::array<Unit, 256> units_;
    Stats stats_;

    void process(uint8_t u, uint32_t seq, std::string_view msg);
    void drain(uint8_t u);
    void apply(uint8_t u, uint32_t seq, const char *buf, size_t len, bool recovery);
    void gap(uint8_t u, uint32_t first, uint32_t count);

    int32_t sec_of(uint8_t u, std::string_view sym);
    uint64_t stamp(Unit &unit, uint32_t time_offset);
    void push(Unit &unit, int32_t sec, const bfile::l3_t &r);
    void end_event(Unit &unit);
    void flush(Unit &unit);
    void to_books(uint8_t u, const bfile::l3_t &r);

    void add(uint8_t u, uint32_t seq, uint32_t to, uint64_t id, char side, uint32_t qty, std::string_view sym,
             double px, bool recovery);
    void execute(uint8_t u, uint32_t to, uint64_t id, uint32_t executed, const uint32_t *remaining,
                 const double *price, const char *flags);
    void change(uint8_t u, uint32_t to, uint64_t id, uint32_t qty, double px);
    void remove(uint8_t u, uint32_t to, uint64_t id);
    void print(Unit &unit, int32_t sec, uint64_t tx, double px, int64_t qty, uint8_t aggressor);
  };
}
