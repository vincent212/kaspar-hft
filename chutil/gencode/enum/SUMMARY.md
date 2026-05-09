<!--
    Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
    Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
    Licensed under the MIT License. See LICENSE file in the project root.
-->

# Enum Generator - Feature Summary

## Overview

The enum code generator (`genenum.py`) now provides **complete container support**, making generated enums first-class citizens in modern C++. You can now use enums as keys in hash maps, indices in type-safe arrays, and with all standard comparison operations.

## What's New (2025-11-24)

### 1. ✅ `std::unordered_map` Support (Hash Maps)

**Problem solved:** Before, you couldn't use enums as keys in unordered containers.

```cpp
// ❌ BEFORE: Didn't compile
std::unordered_map<en::mat, double> prices;  // ERROR: no hash function

// Workaround was ugly:
boost::unordered_flat_map<uint32_t, double> prices;  // Lost type safety
```

```cpp
// ✅ NOW: Just works!
std::unordered_map<en::mat, double> prices;
prices[en::mat::_5Y] = 100.0;
auto price = prices[en::mat::_5Y];  // O(1) lookup!
```

**Technical:** Added `std::hash<en::mat>` specialization to all enum headers.

### 2. ✅ Type-Safe Arrays (`mat_array<T>`)

**Problem solved:** Raw arrays indexed by enums had no type safety.

```cpp
// ⚠️ BEFORE: Easy to make mistakes
double price_history_[8];
price_history_[3] = 100.0;  // Which tenor is index 3?
price_history_[some_int] = 99.0;  // Compiler accepts ANY int!
```

```cpp
// ✅ NOW: Compiler-enforced type safety
en::mat_array<double> price_history_;
price_history_[en::mat::_5Y] = 100.0;  // Type-checked!
price_history_[some_int] = 99.0;       // Compile error!

// Access checking
try {
    double p = price_history_.at(tenor);  // Bounds checked
} catch (const std::out_of_range&) { }
```

**Technical:** Generated `prefix_array<T>` template for each enum with full STL container interface.

### 3. ✅ Explicit Comparison Operators

**Problem solved:** Comparisons relied on implicit uint conversion (unclear).

```cpp
// NOW: Clear, explicit operators
mat a = mat::_2Y;
mat b = mat::_5Y;

if (a == b) { ... }  // Explicit operator==
if (a < b) { ... }   // Explicit operator<
if (a != b) { ... }  // All six operators: ==, !=, <, <=, >, >=

// Works with algorithms
std::vector<mat> tenors = {mat::_10Y, mat::_2Y, mat::_5Y};
std::sort(tenors.begin(), tenors.end());  // Sorts by enum order
```

**Technical:** Generated all six comparison operators as `constexpr noexcept`.

## Real-World Impact

### Example 1: RVModel.hpp - Faster Lookups

```cpp
// BEFORE: O(log N) lookups
struct ModelParams {
    std::map<en::mat, double> mean_map;
    std::map<en::mat, double> std_map;
};

// AFTER: O(1) lookups!
struct ModelParams {
    std::unordered_map<en::mat, double> mean_map;  // 🚀 Faster!
    std::unordered_map<en::mat, double> std_map;
};
```

### Example 2: SuperAggr.hpp - Remove Workarounds

```cpp
// BEFORE: Workaround with uint32_t
boost::unordered_flat_map<uint32_t, double> prev_nof_3_tenor;
for (auto t : en::mat_values) {
    if (en::is_valid(en::mat(t))) {
        this->prev_nof_3_tenor[t] = 0;  // Type safety lost!
    }
}

// AFTER Option 1: Direct enum usage
std::unordered_map<en::mat, double> prev_nof_3_tenor;
for (auto t : en::mat_all) {
    prev_nof_3_tenor[en::mat(t)] = 0.0;
}

// AFTER Option 2: Even better with array (all tenors used)
en::mat_array<double> prev_nof_3_tenor;
prev_nof_3_tenor.data.fill(0.0);
```

### Example 3: Type Safety Prevents Bugs

```cpp
// BEFORE: Easy to mix up array indices
double price_history_[8];
double eob_counter_[8];
price_history_[5] = 100.0;  // Is 5 the right tenor? No idea!

// AFTER: Compiler catches mistakes
en::mat_array<double> price_history_;
en::mat_array<uint32_t> eob_counter_;
price_history_[en::mat::_10Y] = 100.0;  // Clear and type-safe!
eob_counter_[en::mat::_10Y]++;          // Can't mix them up!

// Wrong type = compile error
int some_int = 5;
price_history_[some_int] = 100.0;  // ❌ Compile error!
```

## Performance Comparison

| Container Type | Access | Insert | Memory | Use When |
|---------------|--------|--------|--------|----------|
| **`mat_array<T>`** | **O(1)** ⚡ | **O(1)** ⚡ | Fixed | All/most enum values used |
| **`std::unordered_map<mat,T>`** | O(1) avg | O(1) avg | Dynamic | Sparse data, subset of enums |
| **`std::map<mat,T>`** | O(log N) | O(log N) | Dynamic | Need ordered iteration |

**Recommendation:**
- **Use `mat_array<T>`** for best performance when you have data for all/most enum values
- **Use `unordered_map`** for sparse data or when only some enum values are used
- **Use `map`** when you need elements in sorted order

## Generated Code Structure

For each enum (e.g., `mat`), the generator now creates:

1. **Enum struct** - Original enum with all values
2. **Lookup tables** - `mat_names`, `mat_values`, `mat_all` (constexpr)
3. **Array wrapper** - `mat_array<T>` template for type-safe indexing
4. **Utility functions** - `to_string()`, `mat_index_of()`, `is_valid()`, etc.
5. **Comparison operators** - `==`, `!=`, `<`, `<=`, `>`, `>=` (constexpr, noexcept)
6. **Stream operator** - `operator<<` for printing
7. **Hash specialization** - `std::hash<en::mat>` in std namespace

## Example Code

### Complete Example

```cpp
#include "enum/mat.hpp"
#include <unordered_map>
#include <map>
#include <iostream>

using namespace en;

int main() {
    // 1. Unordered map for fast lookups
    std::unordered_map<mat, double> prices;
    prices[mat::_2Y] = 100.0;
    prices[mat::_5Y] = 102.5;
    prices[mat::_10Y] = 105.0;

    // 2. Type-safe array for all tenors
    mat_array<double> volatilities;
    volatilities.data.fill(0.0);
    volatilities[mat::_5Y] = 0.15;  // 15% vol

    // 3. Comparison and sorting
    mat a = mat::_2Y;
    mat b = mat::_5Y;
    if (a < b) {
        std::cout << to_string(a) << " comes before "
                  << to_string(b) << std::endl;
    }

    // 4. Ordered map for sorted iteration
    std::map<mat, double> yields;
    yields[mat::_10Y] = 3.5;
    yields[mat::_2Y] = 4.0;
    yields[mat::_5Y] = 3.8;

    // Iterate in order (2Y, 5Y, 10Y)
    for (const auto& [tenor, yield] : yields) {
        std::cout << to_string(tenor) << ": " << yield << "%" << std::endl;
    }

    return 0;
}
```

## Testing

All features have been verified:
- ✅ `std::unordered_map<mat, double>` compiles and works
- ✅ `mat_array<double>` provides type-safe indexing
- ✅ All comparison operators (`==`, `!=`, `<`, etc.) work
- ✅ `std::unordered_set<mat>` compiles and works
- ✅ `std::map<mat, double>` still works (backward compatible)
- ✅ Sorting with `std::sort()` works
- ✅ Stream output with `operator<<` works

See `/tmp/test_enum_containers.cpp` for complete test suite.

## Documentation

All features are fully documented:

- **[ENUM_GUIDE.md](ENUM_GUIDE.md)** - Complete guide with examples
  - How to add/create enums
  - Container usage examples (map, unordered_map, array)
  - Performance comparison
  - Best practices

- **[CHANGELOG.md](CHANGELOG.md)** - Detailed change history
  - What changed and why
  - Migration guide
  - Technical details

- **[def/README.md](def/README.md)** - Quick reference for enum definitions

## Backward Compatibility

✅ **100% backward compatible** - No breaking changes:
- Existing `std::map<en::mat, T>` code still works
- Raw array indexing `arr[enum]` still works via implicit uint conversion
- All existing code continues to compile and run
- Only additions, no removals or modifications to existing functionality

## How to Use

### For Existing Enums
Already done! All 17 enums have been regenerated with the new features:
- `mat`, `bs`, `x`, `trader`, `models`, `cal`, `l3`, etc.

Just include the enum header and start using the new features.

### For New Enums
Create a `.enum` file in `chutil/gencode/enum/def/`, run `./genenum.py`, and you automatically get all features:

```bash
cd /home/vm/m2/chutil/gencode/enum
cat > def/my_enum.enum << 'EOF'
prefix
VALUE1
VALUE2
VALUE3

EOF
./genenum.py
```

Now you can use:
- `std::unordered_map<en::prefix, T>`
- `en::prefix_array<T>`
- All comparison operators
- Everything else!

## Questions?

See the comprehensive [ENUM_GUIDE.md](ENUM_GUIDE.md) for:
- Complete API reference
- Usage examples
- Performance guidelines
- Troubleshooting
- Best practices

---

**Generated:** 2025-11-24
**Generator:** [genenum.py](genenum.py)
**All enums updated:** ✅ Yes (17 enums)
