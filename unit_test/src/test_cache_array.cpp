/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/**
 * Unit Tests for cache_array
 *
 * These tests verify that cache_array:
 * - Stores and retrieves values correctly
 * - Handles different keys correctly (no false collisions)
 * - Clears all entries
 * - Supports overwrite mode
 *
 * cache_array uses boost::unordered_flat_map internally for O(1) lookups
 * with proper key handling.
 */

#include <gtest/gtest.h>
#include "chutil/cache_array.hpp"
#include <cstdint>

using namespace chutil;

class CacheArrayTest : public ::testing::Test {
protected:
  cache_array<int, 256> cache;

  void SetUp() override {
    cache.clear();
  }
};

/**
 * Test 1: InitialStateIsEmpty
 *
 * VERIFY: get() returns false for any key (no values stored)
 */
TEST_F(CacheArrayTest, InitialStateIsEmpty) {
  const int* val;
  EXPECT_FALSE(cache.get(0, val)) << "Empty cache should return false";
  EXPECT_FALSE(cache.get(100, val)) << "Empty cache should return false";
  EXPECT_FALSE(cache.get(255, val)) << "Empty cache should return false";
}

/**
 * Test 2: InsertAndRetrieve
 *
 * INPUT:  insert(key=100, val=42)
 * VERIFY: get(100) returns true and val=42
 */
TEST_F(CacheArrayTest, InsertAndRetrieve) {
  cache.insert(100, 42);

  const int* val;
  EXPECT_TRUE(cache.get(100, val)) << "get() should return true after insert";
  EXPECT_EQ(*val, 42) << "Retrieved value should be 42";
}

/**
 * Test 3: MultipleInserts
 *
 * INPUT:  insert(0, 10), insert(1, 20), insert(255, 30)
 * VERIFY: All values retrievable
 */
TEST_F(CacheArrayTest, MultipleInserts) {
  cache.insert(0, 10);
  cache.insert(1, 20);
  cache.insert(255, 30);

  const int* val;

  EXPECT_TRUE(cache.get(0, val));
  EXPECT_EQ(*val, 10);

  EXPECT_TRUE(cache.get(1, val));
  EXPECT_EQ(*val, 20);

  EXPECT_TRUE(cache.get(255, val));
  EXPECT_EQ(*val, 30);
}

/**
 * Test 4: DifferentKeysStayDistinct
 *
 * Keys that would have hashed to the same slot in the old implementation
 * should now stay distinct.
 *
 * INPUT:  insert(100, 42), insert(356, 99)
 * VERIFY: get(100) returns 42 (unchanged)
 *         get(356) returns 99
 */
TEST_F(CacheArrayTest, DifferentKeysStayDistinct) {
  cache.insert(100, 42);
  cache.insert(356, 99);

  const int* val;

  // Each key returns its own value
  EXPECT_TRUE(cache.get(100, val));
  EXPECT_EQ(*val, 42) << "Key 100 should still have value 42";

  EXPECT_TRUE(cache.get(356, val));
  EXPECT_EQ(*val, 99) << "Key 356 should have value 99";
}

/**
 * Test 5: ClearRemovesAllValues
 *
 * INPUT:  insert several values, then clear()
 * VERIFY: get() returns false for all previously inserted keys
 */
TEST_F(CacheArrayTest, ClearRemovesAllValues) {
  cache.insert(0, 10);
  cache.insert(50, 20);
  cache.insert(100, 30);
  cache.insert(200, 40);

  cache.clear();

  const int* val;
  EXPECT_FALSE(cache.get(0, val));
  EXPECT_FALSE(cache.get(50, val));
  EXPECT_FALSE(cache.get(100, val));
  EXPECT_FALSE(cache.get(200, val));
}

/**
 * Test 6: OverwriteExistingValue
 *
 * INPUT:  insert(100, 42), insert(100, 99, overwrite=true)
 * VERIFY: get(100) returns 99
 */
TEST_F(CacheArrayTest, OverwriteExistingValue) {
  cache.insert(100, 42);
  cache.insert(100, 99, true);  // overwrite=true

  const int* val;
  EXPECT_TRUE(cache.get(100, val));
  EXPECT_EQ(*val, 99);
}

/**
 * Test 7: LargeKeysHandledCorrectly
 *
 * Large keys should be stored and retrieved correctly
 *
 * INPUT:  insert(1000000, 42), insert(64, 99)
 * VERIFY: get(1000000) returns 42
 *         get(64) returns 99 (different key, different value)
 */
TEST_F(CacheArrayTest, LargeKeysHandledCorrectly) {
  uint64_t large_key = 1000000;

  cache.insert(large_key, 42);
  cache.insert(64, 99);

  const int* val;
  EXPECT_TRUE(cache.get(large_key, val));
  EXPECT_EQ(*val, 42);

  // Different key returns different value
  EXPECT_TRUE(cache.get(64, val));
  EXPECT_EQ(*val, 99);
}

/**
 * Test 8: HashFunction
 *
 * Verify the hash function behavior (kept for API compatibility)
 */
TEST_F(CacheArrayTest, HashFunction) {
  // hash(k) = k % 256
  EXPECT_EQ(cache.hash(0), 0);
  EXPECT_EQ(cache.hash(100), 100);
  EXPECT_EQ(cache.hash(255), 255);
  EXPECT_EQ(cache.hash(256), 0);
  EXPECT_EQ(cache.hash(257), 1);
  EXPECT_EQ(cache.hash(512), 0);
  EXPECT_EQ(cache.hash(1000000), 1000000 % 256);
}

/**
 * Test 9: DifferentValueTypes
 *
 * Test with a struct value type
 */
TEST(CacheArrayTypesTest, StructValueType) {
  struct Point { int x, y; };
  cache_array<Point, 64> point_cache;

  Point p1{10, 20};
  point_cache.insert(5, p1);

  const Point* retrieved = nullptr;
  EXPECT_TRUE(point_cache.get(5, retrieved));
  EXPECT_EQ(retrieved->x, 10);
  EXPECT_EQ(retrieved->y, 20);
}

/**
 * Test 10: ManyDistinctKeys
 *
 * Test that many keys can coexist without collision
 */
TEST(CacheArraySizeTest, ManyDistinctKeys) {
  cache_array<int, 4> small_cache;

  // Insert keys that would have collided in old implementation
  small_cache.insert(0, 100);
  small_cache.insert(4, 200);   // 4 % 4 = 0
  small_cache.insert(8, 300);   // 8 % 4 = 0
  small_cache.insert(12, 400);  // 12 % 4 = 0

  const int* val;

  // Each key should have its own value
  EXPECT_TRUE(small_cache.get(0, val));
  EXPECT_EQ(*val, 100);

  EXPECT_TRUE(small_cache.get(4, val));
  EXPECT_EQ(*val, 200);

  EXPECT_TRUE(small_cache.get(8, val));
  EXPECT_EQ(*val, 300);

  EXPECT_TRUE(small_cache.get(12, val));
  EXPECT_EQ(*val, 400);
}

/**
 * Test 11: InsertAfterClear
 *
 * Verify that insert works correctly after clear
 */
TEST_F(CacheArrayTest, InsertAfterClear) {
  cache.insert(100, 42);

  const int* val;
  EXPECT_TRUE(cache.get(100, val));
  EXPECT_EQ(*val, 42);

  cache.clear();
  EXPECT_FALSE(cache.get(100, val));

  cache.insert(100, 99);
  EXPECT_TRUE(cache.get(100, val));
  EXPECT_EQ(*val, 99);
}

/**
 * Test 12: GetReturnsPointerToStoredValue
 *
 * Verify that get() returns a pointer to the actual stored value
 */
TEST_F(CacheArrayTest, GetReturnsPointerToStoredValue) {
  cache.insert(100, 42);
  cache.insert(101, 43);

  const int* val100;
  const int* val101;

  cache.get(100, val100);
  cache.get(101, val101);

  // Pointers should be different (different entries)
  EXPECT_NE(val100, val101);

  // Values should be correct
  EXPECT_EQ(*val100, 42);
  EXPECT_EQ(*val101, 43);
}
