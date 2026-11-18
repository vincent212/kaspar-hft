#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <cstdint>
#include <utility>
#include <vector>

#include "sim/LevelWindow.hpp"

namespace ebs
{
  /**
   * MbpLevelBook -- PROTOTYPE, not wired into Kaspar. Part of the patch
   * proposal in ../../patch_proposal.md.
   *
   * One instrument's MDP 3.0 Market by Price book, maintained by price level
   * index as CME specifies ("MDP 3.0 - Market by Price - Multiple Depth Book",
   * CME Client Systems Wiki page 457574047, v2, 2024-12-26):
   *
   *   New (279=0)     insert at MDPriceLevel, shift lower levels down, drop
   *                   levels past MarketDepth (tag 264)
   *   Change (279=1)  quantity/order count at MDPriceLevel; the price does not
   *                   change
   *   Delete (279=2)  remove MDPriceLevel, shift lower levels up
   *   Overlay (279=5) listed as valid for EBS ("MDP 3.0 - Market Data
   *                   Incremental Refresh - MBP - LongQty", page 457423069, v3)
   *                   but not defined on the pages read; taken here as "replace
   *                   price and quantity at MDPriceLevel". feed.md section 10.
   *
   * After the entries of an event are applied, publish() writes both sides into
   * a sim::LevelWindow, which hands the changes to sim::Mbp2Mbo. Quantities stay
   * uint64 notional here; Mbp2Mbo's chunk (the quantity unit) turns them into
   * whole synthetic orders.
   */
  class MbpLevelBook
  {
  public:
    explicit MbpLevelBook(size_t depth) : depth_(depth) {}

    // false: the entry does not fit the book (a level index past depth, a
    // Change/Delete at an empty level, a Change whose price differs). The
    // caller treats that as a book error and recovers.
    bool apply(bool bid, uint8_t action, uint8_t level, double px, uint64_t qty)
    {
      auto &side = bid ? bids_ : asks_;
      if (level < 1 || level > depth_)
        return false;
      const size_t i = level - 1;
      switch (action)
      {
      case 0: // New
        if (i > side.size())
          return false;
        side.insert(side.begin() + long(i), {px, qty});
        if (side.size() > depth_)
          side.resize(depth_);
        return true;
      case 1: // Change
        if (i >= side.size() || side[i].first != px)
          return false;
        side[i].second = qty;
        return true;
      case 2: // Delete
        if (i >= side.size())
          return false;
        side.erase(side.begin() + long(i));
        return true;
      case 5: // Overlay (reading in feed.md section 10)
        if (i >= side.size())
          return false;
        side[i] = {px, qty};
        return true;
      default: // DeleteThru (3), DeleteFrom (4): not listed for EBS
        return false;
      }
    }

    void clear()
    {
      bids_.clear();
      asks_.clear();
    }

    // Replace the window's book with this one. LevelWindow::flush() then sends
    // only what changed.
    void publish(sim::LevelWindow &w) const
    {
      w.clear();
      for (const auto &[px, q] : bids_)
        w.set(true, px, double(q));
      for (const auto &[px, q] : asks_)
        w.set(false, px, double(q));
    }

    const std::vector<std::pair<double, uint64_t>> &side(bool bid) const
    {
      return bid ? bids_ : asks_;
    }

  private:
    size_t depth_;
    std::vector<std::pair<double, uint64_t>> bids_, asks_;   // index 0 = level 1
  };
}
