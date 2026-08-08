<!--
    Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
    Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
    Licensed under the MIT License. See LICENSE file in the project root.
-->

# RefData Usage Guide

RefData is a singleton that manages asset/instrument definitions (universe).

## Location
`frame_kaspr/include/frame/ref/RefData.hpp`

## Initialization

RefData uses lazy initialization. You must set the universe file path first,
then access the singleton via `inst()`.

### Standard Initialization

```cpp
#include "frame/ref/RefData.hpp"

// 1. Set the universe file path (MUST be called before any RefData access)
frame::ref::RefData::set_universe("/path/to/universe.csv");

// 2. Access the singleton - this triggers lazy initialization
frame::ref::RefData::inst();

// Or just start using it - inst() is called automatically
auto n = frame::ref::RefData::inst().num_assets();
```

### With Rounding Info

```cpp
frame::ref::RefData::set_universe(
    "/path/to/universe.csv",
    "/path/to/rounding_info.csv"  // optional
);
frame::ref::RefData::inst();
```

## Common Usage

### Get Number of Assets

```cpp
auto n = frame::ref::RefData::inst().num_assets();
// Or use static method:
auto n = frame::ref::RefData::get_num_assets();
```

### Get Asset by ID

```cpp
auto a = frame::ref::RefData::inst().get_asset(asset_id);
// Or:
auto a = frame::ref::RefData::get_asset(asset_id);

if (a) {
  std::cout << "Asset: " << a->name << std::endl;
}
```

### Get Asset by Name

```cpp
auto a = frame::ref::RefData::inst().get_asset("ZNH6");
// Or:
auto a = frame::ref::RefData::get_asset("ZNH6");
```

### Get Asset by Security ID

```cpp
auto a = frame::ref::RefData::get_asset_from_sec_id(sec_id);
```

### Iterate Over All Assets

```cpp
auto n = frame::ref::RefData::inst().num_assets();
for (std::size_t i = 1; i < n; i++) {
  auto a = frame::ref::RefData::inst().get_asset(i);
  if (a) {
    std::cout << a->name << " mnemonic=" << a->mnemonic << std::endl;
  }
}
```

### Check if Asset Exists

```cpp
if (frame::ref::RefData::is_asset("ZNH6")) {
  // Asset exists
}

if (frame::ref::RefData::is_asset(asset_id)) {
  // Asset with this ID exists
}
```

## Universe File Format

The universe CSV file defines all tradeable instruments. Format:
```
type,name,underlying,mnemonic,units,bo_spread,ord_sz,maxpx,lev_orders_max,max_ord_per_sec,sector,otid,visibility_group,exchange
```

Example:
```csv
F,ZNH6,UB10,ZN,1.,1,1,100000,1,1,unk,x,0,CMEMDFUT
F,ZFH6,UB05,ZF,1.,1,1,100000,1,1,unk,x,0,CMEMDFUT
```

Fields:
- `type`: F=Future, E=Equity, etc.
- `name`: Full instrument name (e.g., "ZNH6")
- `underlying`: Underlying asset code
- `mnemonic`: Short symbol (e.g., "ZN")
- `units`: Contract multiplier
- `bo_spread`: Bid/offer spread
- `ord_sz`: Default order size
- `maxpx`: Maximum price (determines price array size)
- `lev_orders_max`: Max orders per level
- `max_ord_per_sec`: Rate limit
- `sector`: Sector classification
- `otid`: Order type ID
- `visibility_group`: Visibility grouping
- `exchange`: Exchange code (CMEMDFUT, CMEMD, etc.)

## Dynamic Asset Creation

BFA can dynamically create assets from `l3_fdf_t` (symbol definition) messages:

```cpp
// BFA calls this when it sees a new symbol in the data file
auto &r = const_cast<frame::ref::RefData&>(frame::ref::RefData::inst());
r.add_asset(...);  // or add_cusip_asset for CUSIP-based instruments
```

## Important Notes

1. **Call set_universe() BEFORE any RefData access**
   - If you access inst() before set_universe(), you get an empty RefData

2. **Thread Safety**
   - RefData uses `std::shared_mutex` for thread-safe access
   - Reads take a shared (read) lock; after initialization the table is effectively read-only, so reader contention is minimal

3. **Asset IDs start at 1**
   - Asset ID 0 is reserved/invalid
   - Loop from 1 to num_assets() when iterating

4. **Lazy Initialization**
   - `inst()` creates the singleton on first call
   - Don't call `init()` directly - it's private
