/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/**
 * Unit Tests for RefData and Asset Components
 *
 * RefData is a singleton that:
 * - Loads universe CSV file with asset definitions
 * - Provides O(1) lookup by asset ID, name, or sec_id
 * - Manages asset aliases
 * - Caches sector-exchange mappings for fast lookup
 *
 * Asset struct contains:
 * - Symbol identification (id, name, mnemonic, cusip)
 * - Trading parameters (units, bo_spread, maxpx, contract_size)
 * - Exchange info (exchange_md, exchange_trad)
 * - Type info (is_bond, is_fut, is_option, etc.)
 *
 * RefData Dependencies:
 * - Universe CSV file for full initialization
 * - Rounding info file (optional)
 * - Alias file (optional)
 *
 * These tests focus on testable components without requiring file I/O:
 * - Asset class construction and methods
 * - Asset type identification
 * - Asset property access
 *
 * For full RefData tests, create test CSV files in a test data directory.
 */

#include <gtest/gtest.h>
#include "frame/ref/Asset.hpp"
#include "enum/e_names.hpp"
#include <vector>
#include <string>

using namespace frame::ref;

// =============================================================================
// Asset Default Construction Tests
// =============================================================================

class AssetDefaultConstructionTest : public ::testing::Test {
protected:
  Asset asset;
};

/**
 * Test 1: DefaultConstructedAssetHasNotSetName
 *
 * VERIFY: Default constructed Asset has "not set" name
 */
TEST_F(AssetDefaultConstructionTest, DefaultConstructedAssetHasNotSetName) {
  EXPECT_EQ(asset.name, "not set");
}

/**
 * Test 2: DefaultConstructedAssetHasMaxId
 *
 * VERIFY: Default constructed Asset has max unsigned int as id
 */
TEST_F(AssetDefaultConstructionTest, DefaultConstructedAssetHasMaxId) {
  EXPECT_EQ(asset.id, std::numeric_limits<unsigned>::max());
}

/**
 * Test 3: DefaultConstructedAssetTypeIsX
 *
 * VERIFY: Default constructed Asset has type 'X' (unknown)
 */
TEST_F(AssetDefaultConstructionTest, DefaultConstructedAssetTypeIsX) {
  EXPECT_EQ(asset.typ(), 'X');
}

/**
 * Test 4: DefaultConstructedAssetHasZeroUnits
 *
 * VERIFY: Default constructed Asset has zero units (min price increment)
 */
TEST_F(AssetDefaultConstructionTest, DefaultConstructedAssetHasZeroUnits) {
  EXPECT_DOUBLE_EQ(asset.get_units(), 0.0);
}

/**
 * Test 5: DefaultConstructedAssetHasNegativeMaxPx
 *
 * VERIFY: Default constructed Asset has maxpx=-1 (not set)
 */
TEST_F(AssetDefaultConstructionTest, DefaultConstructedAssetHasNegativeMaxPx) {
  EXPECT_EQ(asset.maxpx, -1);
}

// =============================================================================
// Asset ID Construction Tests
// =============================================================================

class AssetIdConstructionTest : public ::testing::Test {
protected:
  Asset asset{42};
};

/**
 * Test 6: IdConstructedAssetHasCorrectId
 *
 * VERIFY: Asset constructed with ID has that ID
 */
TEST_F(AssetIdConstructionTest, IdConstructedAssetHasCorrectId) {
  EXPECT_EQ(asset.id, 42u);
}

/**
 * Test 7: IdConstructedAssetHasUnknownName
 *
 * VERIFY: Asset constructed with only ID has "UNK" name
 */
TEST_F(AssetIdConstructionTest, IdConstructedAssetHasUnknownName) {
  EXPECT_EQ(asset.name, "UNK");
}

/**
 * Test 8: IdConstructedAssetTypeIsU
 *
 * VERIFY: Asset constructed with only ID has type 'U' (unknown)
 */
TEST_F(AssetIdConstructionTest, IdConstructedAssetTypeIsU) {
  EXPECT_EQ(asset.typ(), 'U');
}

/**
 * Test 9: IdConstructedAssetMnemonicIsUNK
 *
 * VERIFY: Asset constructed with only ID has "UNK" mnemonic
 */
TEST_F(AssetIdConstructionTest, IdConstructedAssetMnemonicIsUNK) {
  EXPECT_EQ(asset.mnemonic, "UNK");
}

// =============================================================================
// Asset Type Tests
// =============================================================================

class AssetTypeTest : public ::testing::Test {
protected:
  Asset bond{1};
  Asset currency{2};
  Asset future{3};
  Asset k1000{4};
  Asset option{5};

  void SetUp() override {
    // Set types directly (normally set from CSV)
    bond._typ = 'B';
    currency._typ = 'C';
    future._typ = 'F';
    k1000._typ = 'K';
    option._typ = 'O';
  }
};

/**
 * Test 10: IsBondReturnsCorrectly
 *
 * VERIFY: is_bond() returns true only for 'B' type
 */
TEST_F(AssetTypeTest, IsBondReturnsCorrectly) {
  EXPECT_TRUE(bond.is_bond());
  EXPECT_FALSE(currency.is_bond());
  EXPECT_FALSE(future.is_bond());
  EXPECT_FALSE(k1000.is_bond());
  EXPECT_FALSE(option.is_bond());
}

/**
 * Test 11: IsCurrencyReturnsCorrectly
 *
 * VERIFY: is_currency() returns true only for 'C' type
 */
TEST_F(AssetTypeTest, IsCurrencyReturnsCorrectly) {
  EXPECT_FALSE(bond.is_currency());
  EXPECT_TRUE(currency.is_currency());
  EXPECT_FALSE(future.is_currency());
}

/**
 * Test 12: IsFutReturnsCorrectly
 *
 * VERIFY: is_fut() returns true only for 'F' type
 */
TEST_F(AssetTypeTest, IsFutReturnsCorrectly) {
  EXPECT_FALSE(bond.is_fut());
  EXPECT_FALSE(currency.is_fut());
  EXPECT_TRUE(future.is_fut());
}

/**
 * Test 13: Is1000ReturnsCorrectly
 *
 * VERIFY: is_1000() returns true only for 'K' type
 */
TEST_F(AssetTypeTest, Is1000ReturnsCorrectly) {
  EXPECT_FALSE(bond.is_1000());
  EXPECT_TRUE(k1000.is_1000());
}

/**
 * Test 14: IsOptionReturnsCorrectly
 *
 * VERIFY: is_option() returns true only for 'O' type
 */
TEST_F(AssetTypeTest, IsOptionReturnsCorrectly) {
  EXPECT_FALSE(bond.is_option());
  EXPECT_FALSE(future.is_option());
  EXPECT_TRUE(option.is_option());
}

// =============================================================================
// Asset Units Tests
// =============================================================================

class AssetUnitsTest : public ::testing::Test {
protected:
  Asset asset{1};
};

/**
 * Test 15: SetUnitsUpdatesValue
 *
 * VERIFY: set_units() updates the units value
 */
TEST_F(AssetUnitsTest, SetUnitsUpdatesValue) {
  asset.set_units(0.03125);  // 1/32 for Treasury futures

  EXPECT_DOUBLE_EQ(asset.get_units(), 0.03125);
}

/**
 * Test 16: SetUnitsUpdatesBo512
 *
 * VERIFY: set_units() also updates bo512 (units * 512)
 */
TEST_F(AssetUnitsTest, SetUnitsUpdatesBo512) {
  asset.set_units(0.03125);

  // bo512 = 0.03125 * 512 = 16
  EXPECT_EQ(asset.bo512(), 16);
}

/**
 * Test 17: Bo512WithDifferentUnits
 *
 * VERIFY: bo512 calculated correctly for different unit values
 */
TEST_F(AssetUnitsTest, Bo512WithDifferentUnits) {
  // Test with typical futures tick sizes
  asset.set_units(0.01);  // 1 cent
  EXPECT_EQ(asset.bo512(), 5);  // 0.01 * 512 = 5.12, truncated to 5

  asset.set_units(0.0625);  // 1/16
  EXPECT_EQ(asset.bo512(), 32);  // 0.0625 * 512 = 32
}

// =============================================================================
// Asset Exchange Tests
// =============================================================================

class AssetExchangeTest : public ::testing::Test {
protected:
  Asset asset{1};
};

/**
 * Test 18: ExchangeMdNotSetByDefault
 *
 * VERIFY: exchange_md is not set by default
 */
TEST_F(AssetExchangeTest, ExchangeMdNotSetByDefault) {
  EXPECT_FALSE(asset.is_exchange_md_set());
}

/**
 * Test 19: SetExchangeMdWorks
 *
 * VERIFY: set_exchange_md() sets the exchange
 */
TEST_F(AssetExchangeTest, SetExchangeMdWorks) {
  asset.set_exchange_md(en::x::SIM);

  EXPECT_TRUE(asset.is_exchange_md_set());
  EXPECT_EQ(asset.get_exchange_md(), en::x::SIM);
}

/**
 * Test 20: ResetExchangeMdWorks
 *
 * VERIFY: reset_exchange_md() can change exchange
 */
TEST_F(AssetExchangeTest, ResetExchangeMdWorks) {
  asset.set_exchange_md(en::x::SIM);
  asset.reset_exchange_md(en::x::FENICS);

  EXPECT_EQ(asset.get_exchange_md(), en::x::FENICS);
}

// =============================================================================
// Asset Property Tests
// =============================================================================

class AssetPropertyTest : public ::testing::Test {
protected:
  Asset asset{1};
};

/**
 * Test 21: HasBookDefaultFalse
 *
 * VERIFY: has_book is false by default
 */
TEST_F(AssetPropertyTest, HasBookDefaultFalse) {
  EXPECT_FALSE(asset.has_book);
}

/**
 * Test 22: InvisibleDefaultFalse
 *
 * VERIFY: invisible is false by default
 */
TEST_F(AssetPropertyTest, InvisibleDefaultFalse) {
  EXPECT_FALSE(asset.invisible);
}

/**
 * Test 23: IsCusipAssetDefaultFalse
 *
 * VERIFY: is_cusip_asset is false by default
 */
TEST_F(AssetPropertyTest, IsCusipAssetDefaultFalse) {
  EXPECT_FALSE(asset.is_cusip_asset);
}

/**
 * Test 24: IsInGoodTradingStateDefaultFalse
 *
 * VERIFY: is_in_good_trading_state is false by default
 */
TEST_F(AssetPropertyTest, IsInGoodTradingStateDefaultFalse) {
  EXPECT_FALSE(asset.is_in_good_trading_state);
}

// =============================================================================
// Asset CSV Construction Tests
// =============================================================================

class AssetCSVConstructionTest : public ::testing::Test {
protected:
  // Simulated CSV line: typ,name,btec,mnemonic,units,bo_spread,rng_adj,maxpx,has_book,contract_size,cfi,sec_group,sec_id,exchange
  std::vector<std::string> csv_line{
    "F",           // type (Future)
    "ZNH6",        // name
    "ZN",          // btec equiv
    "ZN",          // mnemonic
    "0.015625",    // units (1/64)
    "2",           // bo_spread
    "0",           // rng_adj
    "200",         // maxpx
    "1",           // has_book
    "1000",        // contract_size
    "FFDXSX",      // cfi_code
    "ZN",          // security_group
    "12345",       // sec_id
    "SIM"          // exchange_md
  };
};

/**
 * Test 25: CSVConstructionSetsName
 *
 * VERIFY: CSV construction sets name correctly
 */
TEST_F(AssetCSVConstructionTest, CSVConstructionSetsName) {
  Asset asset(1, csv_line);

  EXPECT_EQ(asset.name, "ZNH6");
}

/**
 * Test 26: CSVConstructionSetsType
 *
 * VERIFY: CSV construction sets type correctly
 */
TEST_F(AssetCSVConstructionTest, CSVConstructionSetsType) {
  Asset asset(1, csv_line);

  EXPECT_EQ(asset.typ(), 'F');
  EXPECT_TRUE(asset.is_fut());
}

/**
 * Test 27: CSVConstructionSetsMnemonic
 *
 * VERIFY: CSV construction sets mnemonic correctly
 */
TEST_F(AssetCSVConstructionTest, CSVConstructionSetsMnemonic) {
  Asset asset(1, csv_line);

  EXPECT_EQ(asset.mnemonic, "ZN");
}

/**
 * Test 28: CSVConstructionSetsUnits
 *
 * VERIFY: CSV construction sets units correctly
 */
TEST_F(AssetCSVConstructionTest, CSVConstructionSetsUnits) {
  Asset asset(1, csv_line);

  EXPECT_DOUBLE_EQ(asset.get_units(), 0.015625);
}

/**
 * Test 29: CSVConstructionSetsBoSpread
 *
 * VERIFY: CSV construction sets bo_spread correctly
 */
TEST_F(AssetCSVConstructionTest, CSVConstructionSetsBoSpread) {
  Asset asset(1, csv_line);

  EXPECT_EQ(asset.bo_spread, 2);
}

/**
 * Test 30: CSVConstructionSetsMaxPx
 *
 * VERIFY: CSV construction sets maxpx correctly
 */
TEST_F(AssetCSVConstructionTest, CSVConstructionSetsMaxPx) {
  Asset asset(1, csv_line);

  EXPECT_EQ(asset.maxpx, 200);
}

/**
 * Test 31: CSVConstructionSetsHasBook
 *
 * VERIFY: CSV construction sets has_book correctly
 */
TEST_F(AssetCSVConstructionTest, CSVConstructionSetsHasBook) {
  Asset asset(1, csv_line);

  EXPECT_TRUE(asset.has_book);
}

/**
 * Test 32: CSVConstructionSetsContractSize
 *
 * VERIFY: CSV construction sets contract_size correctly
 */
TEST_F(AssetCSVConstructionTest, CSVConstructionSetsContractSize) {
  Asset asset(1, csv_line);

  EXPECT_EQ(asset.contract_size, 1000);
}

/**
 * Test 33: CSVConstructionSetsSecId
 *
 * VERIFY: CSV construction sets sec_id correctly
 */
TEST_F(AssetCSVConstructionTest, CSVConstructionSetsSecId) {
  Asset asset(1, csv_line);

  EXPECT_EQ(asset.sec_id, 12345u);
}

/**
 * Test 34: CSVConstructionSetsCfiCode
 *
 * VERIFY: CSV construction sets cfi_code correctly
 */
TEST_F(AssetCSVConstructionTest, CSVConstructionSetsCfiCode) {
  Asset asset(1, csv_line);

  EXPECT_EQ(asset.cfi_code, "FFDXSX");
}

/**
 * Test 35: CSVConstructionSetsSecurityGroup
 *
 * VERIFY: CSV construction sets security_group correctly
 */
TEST_F(AssetCSVConstructionTest, CSVConstructionSetsSecurityGroup) {
  Asset asset(1, csv_line);

  EXPECT_EQ(asset.security_group, "ZN");
}

/**
 * Test 36: CSVConstructionSetsBtecEquivName
 *
 * VERIFY: CSV construction sets btec_equiv_name correctly
 */
TEST_F(AssetCSVConstructionTest, CSVConstructionSetsBtecEquivName) {
  Asset asset(1, csv_line);

  EXPECT_EQ(asset.btec_equiv_name, "ZN");
}

/**
 * Test 37: CSVConstructionSetsId
 *
 * VERIFY: CSV construction preserves the passed ID
 */
TEST_F(AssetCSVConstructionTest, CSVConstructionSetsId) {
  Asset asset(42, csv_line);

  EXPECT_EQ(asset.id, 42u);
}

// =============================================================================
// Asset Option Strike Tests
// =============================================================================

class AssetOptionStrikeTest : public ::testing::Test {
protected:
};

/**
 * Test 38: NonOptionReturnsZeroStrike
 *
 * VERIFY: get_strike() returns (0, 0.0) for non-options
 */
TEST_F(AssetOptionStrikeTest, NonOptionReturnsZeroStrike) {
  Asset future{1};
  future._typ = 'F';
  future.name = "ZNH6";

  auto [sig, strike] = future.get_strike();
  EXPECT_EQ(sig, 0);
  EXPECT_DOUBLE_EQ(strike, 0.0);
}

/**
 * Test 39: CallOptionReturnsPositiveSignal
 *
 * VERIFY: get_strike() returns sig=1 for calls
 */
TEST_F(AssetOptionStrikeTest, CallOptionReturnsPositiveSignal) {
  Asset call{1};
  call._typ = 'O';
  call.name = "ZN C1200";  // Call with 120.0 strike

  auto [sig, strike] = call.get_strike();
  EXPECT_EQ(sig, 1);  // Positive for calls
  EXPECT_DOUBLE_EQ(strike, 120.0);
}

/**
 * Test 40: PutOptionReturnsNegativeSignal
 *
 * VERIFY: get_strike() returns sig=-1 for puts
 */
TEST_F(AssetOptionStrikeTest, PutOptionReturnsNegativeSignal) {
  Asset put{1};
  put._typ = 'O';
  put.name = "ZN P1150";  // Put with 115.0 strike

  auto [sig, strike] = put.get_strike();
  EXPECT_EQ(sig, -1);  // Negative for puts
  EXPECT_DOUBLE_EQ(strike, 115.0);
}

/**
 * Test 41: OptionWithHalfStrike
 *
 * VERIFY: get_strike() handles .5 strikes (suffix '5')
 */
TEST_F(AssetOptionStrikeTest, OptionWithHalfStrike) {
  Asset option{1};
  option._typ = 'O';
  option.name = "ZN C1205";  // 120.5 strike

  auto [sig, strike] = option.get_strike();
  EXPECT_DOUBLE_EQ(strike, 120.5);
}

/**
 * Test 42: OptionWithQuarterStrike
 *
 * VERIFY: get_strike() handles .25 strikes (suffix '2')
 */
TEST_F(AssetOptionStrikeTest, OptionWithQuarterStrike) {
  Asset option{1};
  option._typ = 'O';
  option.name = "ZN C1202";  // 120.25 strike

  auto [sig, strike] = option.get_strike();
  EXPECT_DOUBLE_EQ(strike, 120.25);
}

/**
 * Test 43: OptionWithThreeQuarterStrike
 *
 * VERIFY: get_strike() handles .75 strikes (suffix '7')
 */
TEST_F(AssetOptionStrikeTest, OptionWithThreeQuarterStrike) {
  Asset option{1};
  option._typ = 'O';
  option.name = "ZN C1207";  // 120.75 strike

  auto [sig, strike] = option.get_strike();
  EXPECT_DOUBLE_EQ(strike, 120.75);
}

// =============================================================================
// Asset Copy Tests
// =============================================================================

class AssetCopyTest : public ::testing::Test {
protected:
  std::vector<std::string> csv_line{
    "F", "ZNH6", "ZN", "ZN", "0.015625", "2", "0", "200", "1", "1000",
    "FFDXSX", "ZN", "12345", "SIM"
  };
};

/**
 * Test 44: CopyConstructorWorks
 *
 * VERIFY: Asset can be copy constructed
 */
TEST_F(AssetCopyTest, CopyConstructorWorks) {
  Asset original(1, csv_line);
  Asset copy(original);

  EXPECT_EQ(copy.id, original.id);
  EXPECT_EQ(copy.name, original.name);
  EXPECT_EQ(copy.mnemonic, original.mnemonic);
  EXPECT_DOUBLE_EQ(copy.get_units(), original.get_units());
  EXPECT_EQ(copy.typ(), original.typ());
}

/**
 * Test 45: CopyAssignmentWorks
 *
 * VERIFY: Asset can be copy assigned
 */
TEST_F(AssetCopyTest, CopyAssignmentWorks) {
  Asset original(1, csv_line);
  Asset copy{2};  // Different ID initially

  copy = original;

  EXPECT_EQ(copy.name, original.name);
  EXPECT_EQ(copy.mnemonic, original.mnemonic);
}

// =============================================================================
// RefData Documentation Tests
// =============================================================================

/**
 * Test 46: DocumentRefDataInitialization
 *
 * This test documents RefData initialization requirements:
 *
 * RefData::set_universe(universe_fname, rounding_fname);
 * const RefData& rd = RefData::inst();  // Singleton initialized here
 *
 * Universe CSV format (14 columns):
 *   typ,name,btec,mnemonic,units,bo_spread,rng_adj,maxpx,has_book,
 *   contract_size,cfi_code,security_group,sec_id,exchange_md
 *
 * Example line:
 *   F,ZNH6,ZN,ZN,0.015625,2,0,200,1,1000,FFDXSX,ZN,12345,CME
 */
TEST(RefDataDocumentationTest, DocumentRefDataInitialization) {
  SUCCEED();
}

/**
 * Test 47: DocumentRefDataLookupMethods
 *
 * RefData provides O(1) lookup methods:
 *
 * By ID:
 *   const Asset* a = RefData::get_asset(42);
 *
 * By name:
 *   const Asset* a = RefData::get_asset("ZNH6");
 *
 * By sec_id:
 *   const Asset* a = RefData::get_asset_from_sec_id(12345);
 *
 * By sector and exchange (using pre-built map):
 *   const Asset* a = RefData::asset_from_sector_exchange("ZN", en::x::CME);
 */
TEST(RefDataDocumentationTest, DocumentRefDataLookupMethods) {
  SUCCEED();
}

/**
 * Test 48: DocumentRefDataDynamicAssetCreation
 *
 * RefData supports dynamic asset creation:
 *
 * For CUSIP-based assets (bonds):
 *   Asset* a = RefData::add_cusip_asset(en::x::FEN, "912828XY1");
 *   // Creates "FEN.912828XY1" asset
 *
 * For duplicating existing assets:
 *   RefData::duplicate_asset("ZNH6_COPY", original_asset);
 */
TEST(RefDataDocumentationTest, DocumentRefDataDynamicAssetCreation) {
  SUCCEED();
}

/**
 * Test 49: DocumentRefDataThreadSafety
 *
 * RefData is thread-safe:
 * - Uses std::shared_mutex for read/write locking
 * - Read operations use shared_lock (multiple readers)
 * - Write operations use unique_lock (exclusive access)
 *
 * Lookup methods (read, shared lock):
 *   - asset(id), asset(name), asset_from_sec_id(id)
 *   - asset_from_sector_exchange()
 *
 * Modification methods (write, unique lock):
 *   - add_cusip_asset(), duplicate_asset()
 *   - set_exchange_sector_mapping()
 *   - update_sec_id()
 */
TEST(RefDataDocumentationTest, DocumentRefDataThreadSafety) {
  SUCCEED();
}

/**
 * Test 50: DocumentAssetExchangeConfiguration
 *
 * Assets have two exchange configurations:
 *
 * 1. exchange_md (market data source):
 *    - Set from universe CSV file column 14
 *    - Or via set_exchange_md()
 *    - Accessed via get_exchange_md()
 *
 * 2. exchange_trad (trading venue):
 *    - Only available when tradingvenueinasset is defined
 *    - Set via set_trading_venue()
 *    - Accessed via get_trading_venue()
 *    - Used for sector->exchange->asset mapping
 */
TEST(RefDataDocumentationTest, DocumentAssetExchangeConfiguration) {
  SUCCEED();
}

