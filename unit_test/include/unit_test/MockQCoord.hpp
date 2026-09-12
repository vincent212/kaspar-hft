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
 * MockQCoord - Mock implementation of light::QCoord for testing
 *
 * Inherits from light::QCoord to be type-compatible with light22.
 * Adds call tracking for verification in tests.
 */
struct MockQCoord : public light::QCoord {
  struct OrderCall {
    int px;
    int sz;
    int mmid;
    int oid;
    bool is_add;  // true for add, false for remove
  };

  std::vector<OrderCall> calls;
  std::map<int, int> mock_sz_at_px_map;  // Override size tracking for test setup
  int mock_tot_sz = -1;  // -1 means use real implementation

  MockQCoord() : light::QCoord() {}

  // Override add_order to track calls
  void add_order(int px, int sz, int mmid, int id, int mxsz) noexcept {
    calls.push_back({px, sz, mmid, id, true});
    light::QCoord::add_order(px, sz, mmid, id, mxsz);
  }

  // Override remove_order to track calls
  int remove_order(int px, int sz, int mmid, int id, bool all = false) noexcept {
    calls.push_back({px, sz, mmid, id, false});
    return light::QCoord::remove_order(px, sz, mmid, id, all);
  }

  // Override sz_at_px to allow test setup
  int sz_at_px(int px) noexcept override {
    auto it = mock_sz_at_px_map.find(px);
    if (it != mock_sz_at_px_map.end()) {
      return it->second;
    }
    return light::QCoord::sz_at_px(px);
  }

  // Override total_sz to allow test setup
  int total_sz() noexcept override {
    if (mock_tot_sz >= 0) {
      return mock_tot_sz;
    }
    return light::QCoord::total_sz();
  }

  // Test helpers
  void clear() {
    calls.clear();
    mock_sz_at_px_map.clear();
    mock_tot_sz = -1;
    // Reset base class state
    px_mmid_ord.clear();
    tot_sz_cache = 0;
    mmid_orders = 0;
    cache_sz_at_px.clear();
    chache_cum_sz.clear();
    last_in_at_px.clear();
    num_canc = 0;
  }

  size_t call_count() const { return calls.size(); }

  // Set up initial state for tests (bypasses real implementation)
  void set_total_sz(int sz) { mock_tot_sz = sz; }
  void set_sz_at_px(int px, int sz) { mock_sz_at_px_map[px] = sz; }
};

} // namespace unit_test
