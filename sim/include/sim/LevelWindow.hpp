#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <cstddef>
#include <cstdint>
#include <map>
#include <utility>

#include "sim/Mbp2Mbo.hpp"

namespace sim
{
  /**
   * LevelWindow -- the venue's full price-level book, of which only the best
   * `depth` levels per side reach Mbp2Mbo.
   *
   * Coinbase sends its whole book (tens of thousands of levels, most of them
   * far from the inside); Hyperliquid sends the best 20 levels per side as a
   * fresh snapshot each time. Either way the feed writes levels into the
   * window with set() (or clears it), and flush() works out what changed in
   * the best `depth` levels since the last flush and sends only that to
   * Mbp2Mbo: a level that changed size, a level that entered the window, a
   * level that left it (as quantity 0), and a level whose chunks in Mbp2Mbo no
   * longer match its size -- a trade takes chunks off a level, and if the venue
   * then shows the level at the size last sent (refilled), only this check
   * puts the chunks back. resync() sends the window as a
   * Mbp2Mbo snapshot instead, for the start and after a reconnect.
   *
   * Levels are keyed by tick, so a price that arrives as "65049.01" one time
   * and "65049.010" the next is one level.
   */
  class LevelWindow
  {
  public:
    LevelWindow(Mbp2Mbo &out, size_t depth) : out_(out), depth_(depth) {}

    // Quantity at one level of the venue's book; 0 removes it.
    void set(bool bid, double px, double qty);

    // Empty the venue's book (before a snapshot that replaces it).
    void clear()
    {
      bids_.clear();
      asks_.clear();
    }

    // Send the levels that changed inside the window since the last flush.
    // Does not end the event.
    void flush(uint64_t tx);

    // Send the window as a Mbp2Mbo snapshot (recovery records). Does not end
    // the event.
    void resync(uint64_t tx);

    size_t size(bool bid) const { return bid ? bids_.size() : asks_.size(); }

  private:
    using Side = std::map<int64_t, std::pair<double, double>>;   // tick -> (price, qty)

    Mbp2Mbo &out_;
    size_t depth_;
    Side bids_, asks_;       // the venue's book
    Side sent_b_, sent_a_;   // the window as last sent to Mbp2Mbo

    Side top(bool bid) const;
    void flush_side(bool bid, uint64_t tx);
  };
}
