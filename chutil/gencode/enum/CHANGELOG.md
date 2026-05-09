<!--
    Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
    Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
    Licensed under the MIT License. See LICENSE file in the project root.
-->

# Enum Generator Changelog

## 2025-11-24 - Container Support & Hash Specialization

### Added

1. **`std::hash` specialization for all enums**
   - Enables use with `std::unordered_map<en::mat, T>`
   - Enables use with `std::unordered_set<en::mat>`
   - Provides O(1) average-case lookups vs O(log N) for `std::map`
   - Generated in `std` namespace for ADL compatibility

2. **Type-safe array wrapper: `prefix_array<T>`**
   - Fixed-size array indexed by enum values
   - Compile-time type safety: `mat_array<double> prices; prices[mat::_5Y] = 100.0;`
   - Standard container interface (begin/end/size/etc.)
   - Bounds-checked `at()` method
   - Zero overhead - just a wrapper around `std::array`
   - Best performance for dense data (all enum values have data)

3. **Explicit comparison operators**
   - All six comparison operators: `==`, `!=`, `<`, `<=`, `>`, `>=`
   - Marked as `constexpr` and `noexcept`
   - Enables use in sorted containers and algorithms
   - Previously relied on implicit uint conversion, now explicit

### Benefits

**Before this update:**
```cpp
// ❌ Didn't compile - no std::hash specialization
std::unordered_map<en::mat, double> prices;  // ERROR

// ⚠️ Workaround - had to use uint32_t keys
boost::unordered_flat_map<uint32_t, double> prices;
for (auto t : en::mat_values) {
    prices[t] = 0.0;  // Lost type safety
}

// ⚠️ Raw arrays - no type safety
double price_history_[8];  // Which index is which tenor?
price_history_[3] = 100.0;  // Is this 5Y or 7Y? Who knows!
```

**After this update:**
```cpp
// ✅ std::unordered_map now works!
std::unordered_map<en::mat, double> prices;
prices[en::mat::_5Y] = 100.0;  // Type-safe, O(1) lookup

// ✅ Type-safe arrays with enum indexing
en::mat_array<double> price_history;
price_history[en::mat::_5Y] = 100.0;  // Compiler enforces correct type!

// ✅ All comparison operators work
if (tenor1 < tenor2) { ... }
std::sort(tenors.begin(), tenors.end());
```

### Use Cases from Real Codebase

**RVModel.hpp** - Now can use unordered_map instead of map:
```cpp
struct ModelParams {
    std::unordered_map<en::mat, double> mean_map;  // O(1) vs O(log N)
    std::unordered_map<en::mat, double> std_map;
};
```

**SuperAggr.hpp** - Replace uint32_t workaround:
```cpp
// Before: boost::unordered_flat_map<uint32_t, double> prev_nof_3_tenor;
// After:
std::unordered_map<en::mat, double> prev_nof_3_tenor;

// Or for better performance (all tenors used):
en::mat_array<double> prev_nof_3_tenor;
```

**Type-safe array indexing** - Replace raw arrays:
```cpp
// Before: double price_history_[8];
// After:
en::mat_array<double> price_history_;
price_history_[mat] = price;  // Type-checked by compiler!
```

### Technical Details

**Generated code additions:**

1. **Hash specialization** (in std namespace):
```cpp
namespace std {
    template<> struct hash<en::mat> {
        std::size_t operator()(en::mat e) const noexcept {
            return std::hash<std::uint32_t>{}(e.value);
        }
    };
}
```

2. **Array wrapper** (in en namespace):
```cpp
template<typename T>
struct mat_array {
    std::array<T, 8> data;
    constexpr T& operator[](mat idx) { return data[idx.value]; }
    constexpr const T& operator[](mat idx) const { return data[idx.value]; }
    // ... full container interface
};
```

3. **Comparison operators** (in en namespace):
```cpp
INLINE constexpr bool operator==(const mat& a, const mat& b) noexcept;
INLINE constexpr bool operator!=(const mat& a, const mat& b) noexcept;
INLINE constexpr bool operator<(const mat& a, const mat& b) noexcept;
// ... and <=, >, >=
```

### Performance Comparison

| Container | Lookup | Insert | Memory | Best For |
|-----------|--------|--------|--------|----------|
| `mat_array<T>` | O(1) | O(1) | `8*sizeof(T)` | All/most enums have data |
| `std::unordered_map` | O(1) avg | O(1) avg | Dynamic | Sparse, dynamic data |
| `std::map` | O(log N) | O(log N) | Dynamic | Need ordered iteration |

### Documentation Updates

- Added comprehensive "Using Enums in Containers" section to ENUM_GUIDE.md
- Examples for std::map, std::unordered_map, prefix_array<T>, std::unordered_set
- Performance comparison table
- Real-world examples from RVModel.hpp and SuperAggr.hpp
- Migration guide for existing code

### Files Modified
- `genenum.py` - Added generation for hash, array wrapper, comparison operators
- All generated enum headers (17 files)
- `ENUM_GUIDE.md` - Added container usage examples
- `CHANGELOG.md` - This file

### Backward Compatibility

✅ **Fully backward compatible** - all existing code continues to work:
- Implicit uint conversion still works
- `std::map<en::mat, T>` still works (now also with explicit operators)
- Raw array indexing `arr[enum]` still works via implicit conversion
- No breaking changes

---

## 2025-11-23 - Major Update

### Added
1. **Automatic underscore stripping for string lookups**
   - Enum values starting with `_` (like `_2Y`, `_10s`) now have the underscore automatically stripped in string arrays
   - C++ identifiers keep the underscore: `mat::_2Y`
   - String lookups use clean names: `"2Y"`
   - Affects: `mat_names`, `mat_name_hash`, and all string-based functions

2. **DO NOT MODIFY warnings**
   - All generated `.hpp` files now include prominent header comments
   - Clearly indicates files are auto-generated
   - Provides instructions for how to modify enums properly

3. **Comprehensive documentation**
   - Added `ENUM_GUIDE.md` - Complete usage guide
   - Added `def/README.md` - Quick reference for definition files
   - Documentation includes examples, best practices, and troubleshooting

### Changed
- `EnumItem.display_name()` - New method to get string representation (strips leading `_`)
- Updated code generation to use `display_name()` for all string arrays and hash maps

### Fixed
- **BREAKING FIX**: Maturity enum (`mat`) now uses clean names
  - Before: `mat_index_of("_2Y")` - required underscore
  - After: `mat_index_of("2Y")` - clean name (underscore rejected)
  - Applies to: `mat`, `rv_freq`, and any future enums with leading underscores

### Migration Guide

If you have existing code that parses maturity or frequency strings:

**Old code (no longer works):**
```cpp
mat m = mat_index_of("_2Y");   // ✗ Will fail - "_2Y" not found
```

**New code (correct):**
```cpp
mat m = mat_index_of("2Y");    // ✓ Works - clean name
```

**C++ usage unchanged:**
```cpp
mat m = mat::_2Y;              // ✓ Still works - C++ identifier unchanged
std::cout << to_string(m);     // Prints "2Y" (was "_2Y" before)
```

### Files Modified
- `genenum.py` - Core generator with underscore stripping logic
- All generated enum headers in `../../include/enum/*.hpp`
- `e_names.hpp` - Master include file

### Affected Enums
- `mat` - Maturities (2Y, 3Y, 5Y, 7Y, 10Y, 20Y, 30Y)
- `rv_freq` - Realized volatility frequencies (10s, 5min)
- Any future enums with leading underscores

### Rationale

**Why this change?**
1. **User-facing strings should be clean** - Input files, config files, and user interfaces shouldn't require underscores
2. **C++ identifiers need underscores** - C++ doesn't allow identifiers starting with digits
3. **Best of both worlds** - Clean external API, valid C++ internally

**Why this is safe:**
- C++ code unchanged (still uses `mat::_2Y`)
- Only string parsing/formatting affected
- Easy to find/fix: search for `mat_index_of`, `mat_try_parse`, etc.

### Testing

Verified:
- `mat_names` array contains `"2Y"` not `"_2Y"`
- `mat_name_hash` maps `mat::_2Y` to `"2Y"`
- `to_string(mat::_2Y)` returns `"2Y"`
- `mat_index_of("2Y")` correctly parses to `mat::_2Y`
- `mat_index_of("_2Y")` fails (old format rejected)

### Future Enhancements

Potential improvements:
- [ ] Add validation to reject invalid enum names at generation time
- [ ] Support custom display name mappings (beyond underscore stripping)
- [ ] Generate JSON/CSV mappings for external tools
- [ ] Add unit test generation
