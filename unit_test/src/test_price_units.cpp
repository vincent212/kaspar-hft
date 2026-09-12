/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/**
 * Tick units, and the ladder they have to agree with.
 *
 * This exists because getting it wrong cost the project every result it had
 * produced. The simulator registered assets with
 *
 *     units = minPriceIncrement * dispFactor      (25 * 0.01 = 0.25)
 *
 * which is the tick of the DISPLAY price. But a .bin carries CME's native
 * prices -- ESH5 at 5884.00 is stored as 588400 -- and OB hands mbo.pxd
 * straight to ref::Price, whose tick index is pxd / units. Every ES record
 * therefore converted to ~2.35M ticks against a 25,592-tick ladder, the price
 * guard dropped all of them, no book was ever built, no EndOfBurst was ever
 * published, and every run completed normally reporting zeros.
 *
 * Two numbers have to be on the same scale:
 *
 *     units  -- the divisor Price uses to turn a price into a tick index
 *     maxpx  -- the size of the ladder those indices address
 *
 * `maxpx` is built as high_limit_px / minPriceIncrement, both native. So units
 * must be minPriceIncrement, also native. Mixing the two conventions is not a
 * rounding problem; it is off by dispFactor, a factor of 100 for ES, and it
 * fails without an error message.
 */

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

#include "frame/ref/Price.hpp"
#include "frame/ref/RefData.hpp"

using namespace frame;

namespace {

// As CME ships them for ES.
constexpr double kMinPriceIncrement = 25.0;    // native
constexpr double kDispFactor        = 0.01;
constexpr double kHighLimitPx       = 639800.0;  // native, from the l3_lim record

// A real ESH5 print: 5884.00 index points, stored natively.
constexpr int kNativePx   = 588400;
constexpr int kExpectedTick = 23536;           // 588400 / 25

class PriceUnitsTest : public ::testing::Test {
protected:
  static void SetUpTestSuite() {
    const char* proj_root = std::getenv("KSPRPROJ");
    std::string universe_path = proj_root
        ? std::string(proj_root) + "/unit_test/config/universe.csv"
        : "../config/universe.csv";
    ref::RefData::set_universe(universe_path, "");
  }

  const ref::Asset* es() { return ref::RefData::get_asset("ESZ5"); }
};

// The fixture has to keep being the shape we think it is, or everything below
// is vacuous.
TEST_F(PriceUnitsTest, TheFixtureInstrumentIsOnTheNativeScale) {
  auto a = es();
  ASSERT_NE(a, nullptr) << "ESZ5 must exist in the fixture universe";
  EXPECT_DOUBLE_EQ(a->get_units(), kMinPriceIncrement)
      << "units must be the native minPriceIncrement, not the display tick";
  EXPECT_EQ(a->maxpx, int(kHighLimitPx / kMinPriceIncrement));
}

// The conversion the whole replay path depends on.
TEST_F(PriceUnitsTest, ANativePriceConvertsToATickInsideTheLadder) {
  auto a = es();
  ASSERT_NE(a, nullptr);

  const int tick = ref::Price((long double)kNativePx, uint(a->id)).to_int();

  EXPECT_EQ(tick, kExpectedTick);
  EXPECT_GT(tick, 0);
  EXPECT_LT(tick, a->maxpx)
      << "a price the market actually traded at must land inside the ladder";
}

// The bug, stated as a test. If someone reintroduces units = mpi * dispFactor
// this is what happens, and it is worth being able to point at.
TEST_F(PriceUnitsTest, DisplayUnitsPutEveryRealPriceOffTheLadder) {
  const double display_units = kMinPriceIncrement * kDispFactor;   // 0.25
  const int bad_tick = int(kNativePx / display_units);

  EXPECT_EQ(bad_tick, 2353600);
  EXPECT_GT(bad_tick, int(kHighLimitPx / kMinPriceIncrement))
      << "which is why every record was dropped as out of range";

  // Exactly dispFactor apart -- not a rounding error, a convention error.
  EXPECT_EQ(bad_tick, int(kExpectedTick / kDispFactor));
}

// The ladder has to hold the whole permitted daily range: CME's limits are the
// widest the price can legally go, so a price at the limit must still index.
TEST_F(PriceUnitsTest, TheLadderSpansTheFullDailyLimitRange) {
  auto a = es();
  ASSERT_NE(a, nullptr);

  const int at_limit = ref::Price((long double)(kHighLimitPx - kMinPriceIncrement),
                                  uint(a->id)).to_int();
  EXPECT_LT(at_limit, a->maxpx)
      << "one tick below the daily high limit must still be addressable";

  const int over = ref::Price((long double)(kHighLimitPx * 2), uint(a->id)).to_int();
  EXPECT_GT(over, a->maxpx)
      << "and a price that cannot legally occur must fall outside it";
}

// Price is its own inverse, which is what lets a fill price be reported back
// in the units the data arrived in.
TEST_F(PriceUnitsTest, TickAndPriceRoundTrip) {
  auto a = es();
  ASSERT_NE(a, nullptr);

  for (int px : {588400, 600000, 500025, 25}) {
    const int tick = ref::Price((long double)px, uint(a->id)).to_int();
    const ref::Price back(tick, uint(a->id));
    EXPECT_EQ(int(back.to_double()), px) << "round trip failed for " << px;
  }
}

}  // namespace
