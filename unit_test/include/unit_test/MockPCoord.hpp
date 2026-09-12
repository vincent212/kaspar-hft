#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "light/qcoord.hpp"
#include <vector>

namespace unit_test {

/**
 * MockPCoord - Mock implementation of light::PCoord for testing
 *
 * Inherits from light::PCoord but tracks all calls to add_position,
 * allowing tests to verify position updates and cancel reason tracking.
 *
 * Unlike the real PCoord, this mock:
 * - Stores calls in a vector for later inspection
 * - Allows direct manipulation of position for test setup
 *
 * Note: light::PCoord doesn't use virtual methods, but we can use
 * the mock directly since PositionManager stores PCoord* and calls
 * methods directly (not through vtable). The inherited methods work
 * normally, we just add tracking capabilities.
 */
struct MockPCoord : public light::PCoord {
  struct Call {
    en::bs side;
    int sz;
  };

  std::vector<Call> calls;

  MockPCoord() : light::PCoord() {}

  // Override add_position to track calls while still using base implementation
  void add_position(en::bs side, int sz) override {
    calls.push_back({side, sz});
    // Call base class method
    light::PCoord::add_position(side, sz);
  }

  // Override get_position to ensure mock works properly
  int get_position() noexcept override {
    return light::PCoord::get_position();
  }

  size_t call_count() const { return calls.size(); }

  void clear() {
    calls.clear();
    // Reset position to 0 by subtracting/adding back
    int pos = get_position();
    if (pos > 0)
      light::PCoord::add_position(en::bs::SEL, pos);
    else if (pos < 0)
      light::PCoord::add_position(en::bs::BUY, -pos);
    // Reset counters
    hedge_pos_size_breach = 0;
    directional_pos_breach = 0;
    out_px_breach = 0;
    exceeded_reset_dist = 0;
    expectation_breach = 0;
    zero_pos = 0;
    no_out_px_ = 0;
    following = 0;
    canc_to_aggr = 0;
    delay_skip = 0;
    attached_order_id_match = 0;
    last_trade_px_buy = 0;
    last_trade_px_sel = 0;
  }

  // Test helper to set position directly (for test setup)
  void set_position(int pos) {
    int current = get_position();
    int diff = pos - current;
    if (diff > 0)
      light::PCoord::add_position(en::bs::BUY, diff);
    else if (diff < 0)
      light::PCoord::add_position(en::bs::SEL, -diff);
  }
};

} // namespace unit_test
