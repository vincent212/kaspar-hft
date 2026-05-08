<!--
    Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
    Licensed under the MIT License. See LICENSE file in the project root.
-->

# Enum Code Generator Guide

## Overview

The enum code generator (`genenum.py`) converts simple text-based enum definitions into modern C++ enum structs with rich utility functions. This guide explains how to add new values to existing enums or create entirely new enums.

---

## Table of Contents

1. [Adding a Value to an Existing Enum](#adding-a-value-to-an-existing-enum)
2. [Creating a New Enum](#creating-a-new-enum)
3. [Enum File Format Reference](#enum-file-format-reference)
4. [Generated Code Reference](#generated-code-reference)
5. [Best Practices](#best-practices)
6. [Troubleshooting](#troubleshooting)

---

## Adding a Value to an Existing Enum

### Step 1: Locate the Enum Definition File

All enum definitions are in: `m2/chutil/gencode/enum/def/`

Example files:
- `buy_sell.enum` - Trading directions (BUY, SEL)
- `mat.enum` - Bond maturities (2Y, 3Y, 5Y, etc.)
- `exch_code.enum` - Exchange identifiers
- `trader.enum` - Trader accounts

### Step 2: Edit the Enum File

**IMPORTANT**:
- For enums marked with "do not change order", ALWAYS append new values at the END (before the final newline)
- Never change the order of existing values (this would break serialized data)
- Never remove values unless you're certain they're unused

**Example: Adding a new maturity to `mat.enum`**

Before:
```
mat
UNI
_2Y
_3Y
_5Y
_7Y
_10Y
_20Y
_30Y

```

After (adding 15Y):
```
mat
UNI
_2Y
_3Y
_5Y
_7Y
_10Y
_20Y
_30Y
_15Y

```

**Example: Adding a new exchange to `exch_code.enum`**

Before:
```
# do not change order always append at the end
x
UNI
CMEMD
CMEILINK1
CMEILINK2
...
FENICSDVRV

```

After (adding NEWEXCH):
```
# do not change order always append at the end
x
UNI
CMEMD
CMEILINK1
CMEILINK2
...
FENICSDVRV
NEWEXCH

```

### Step 3: Regenerate C++ Headers

```bash
cd /home/vm/m2/chutil/gencode/enum
./genenum.py
```

The script will:
- Parse all `.enum` files in `def/`
- Generate corresponding `.hpp` files in `../../include/enum/`
- Update the master header `e_names.hpp`

### Step 4: Rebuild Your Code

```bash
cd /home/vm/m2
# Rebuild affected components
make clean && make
# Or use ninja if that's your build system
```

### Step 5: Verify the Changes

Check the generated header to confirm your new value:

```bash
# View the generated enum
cat /home/vm/m2/chutil/include/enum/mat.hpp | grep -A 20 "enum {"
```

---

## Creating a New Enum

### Step 1: Create the Definition File

Create a new `.enum` file in `m2/chutil/gencode/enum/def/`

**Naming convention**: `lowercase_name.enum`

### Step 2: Define Your Enum

**Format**:
```
prefix_name         # Line 1: The C++ struct name (short, lowercase recommended)
ITEM1              # Line 2+: Enum values (UPPERCASE recommended)
ITEM2
ITEM3
UNI                # Optional: Special "undefined" marker (if needed)
```

**Example: Creating `order_side.enum`**

```bash
cd /home/vm/m2/chutil/gencode/enum/def
cat > order_side.enum << 'EOF'
os
BID
ASK
BOTH
UNI

EOF
```

**Explanation**:
- `os` - The struct name will be `en::os`
- `BID`, `ASK`, `BOTH` - Enum values (automatically numbered 0, 1, 2)
- `UNI` - Special undefined marker (excluded from `is_valid()` checks)

### Step 3: Run the Generator

```bash
cd /home/vm/m2/chutil/gencode/enum
./genenum.py
```

**Expected output**:
```
Found 18 enum files:
...

Processing order_side (prefix: os)
  Items: ['BID', 'ASK', 'BOTH', 'UNI']
  Generated: ../../include/enum/order_side.hpp

Generated master header: ../../include/enum/e_names.hpp
```

### Step 4: Verify Generated Code

Check the generated header:

```bash
cat /home/vm/m2/chutil/include/enum/order_side.hpp
```

You should see:
- `struct os { ... }` - The enum struct
- `os_names` - Constexpr array of strings
- `os_values` - Constexpr array of values
- `os_all` - Valid values (excludes UNI)
- `to_string()`, `os_index_of()`, `is_valid()` - Utility functions

### Step 5: Use in Your Code

```cpp
#include "enum/order_side.hpp"  // Or include "enum/e_names.hpp" for all enums

using namespace en;

// Usage examples:
os side = os::BID;
std::cout << to_string(side) << std::endl;  // Prints "BID"

os parsed = os_index_of("ASK");  // Parse from string
if (is_valid(parsed)) {
    // Use parsed value
}

auto maybe_side = os_try_parse("INVALID");  // Returns std::nullopt
if (maybe_side) {
    // Valid side
}

// Iterate over all valid values
for (auto val : os_all) {
    os side(val);
    std::cout << to_string(side) << std::endl;
}

// Map-like interface (backward compatibility)
if (os_hash.contains("BID")) {
    os side = os_hash["BID"];
}
```

---

## Enum File Format Reference

### Basic Format

```
prefix              # Required: C++ struct name
VALUE1             # Required: At least one value
VALUE2             # Optional: Additional values
VALUE3
...
```

### Comments

```
# This is a comment
prefix              # Inline comments supported
VALUE1             # Comment after value
# Another comment
VALUE2
```

### Special Values

**UNI** - "Undefined/Invalid" marker:
- Automatically excluded from `is_valid()` checks
- Excluded from the `prefix_all` array
- Useful for representing null/invalid states

**Example**:
```
bs
BUY                # Value 0
SEL                # Value 1
UNK                # Value 2 (unknown)
UNI                # Value 3 (undefined - excluded from is_valid)
```

### Leading Underscores (for numeric identifiers)

C++ identifiers cannot start with digits, so numeric enum values need a leading underscore. The generator automatically strips this underscore for string lookups:

**Example** (`mat.enum`):
```
mat
UNI
_2Y        # C++ name: mat::_2Y, string name: "2Y"
_3Y        # C++ name: mat::_3Y, string name: "3Y"
_5Y
```

**Generated code:**
```cpp
struct mat {
    enum {
        UNI,
        _2Y,      // C++ identifier (with underscore)
        _3Y,
        _5Y
    };
};

// String arrays use clean names (without underscore)
inline constexpr std::array<const char*, 8> mat_names = {{
    "UNI",
    "2Y",     // ✅ Underscore stripped for string lookup
    "3Y",
    "5Y"
}};
```

**Usage:**
```cpp
mat m = mat::_2Y;               // ✅ Use underscore in C++ code
std::cout << to_string(m);      // Prints "2Y" (no underscore)
mat parsed = mat_index_of("2Y"); // ✅ Parse using clean name
```

### Explicit Values

You can assign explicit values (though this is rare):

```
priority
HIGH:100
MEDIUM:50
LOW:10
```

The parser will tokenize on `:` and use the second value.

---

## Generated Code Reference

For each enum `prefix.enum`, the generator creates:

### 1. Enum Struct

```cpp
struct prefix {
    std::uint32_t value;
    enum {
        VALUE1,      // = 0
        VALUE2,      // = 1
        VALUE3,      // = 2
        UNI          // = 3
    };

    // Implicit conversion to uint (for backward compatibility)
    constexpr operator std::uint32_t() const { return value; }

    // Constructors
    constexpr prefix(int _value);               // Implicit from enum value
    constexpr prefix(std::size_t _value);       // Implicit from size_t
    explicit constexpr prefix(std::uint32_t);   // Explicit from uint
    constexpr prefix();                          // Default = max uint
};
```

### 2. Constexpr Lookup Tables

```cpp
// Names array
inline constexpr std::array<const char*, N> prefix_names = {{
    "VALUE1", "VALUE2", "VALUE3", "UNI"
}};

// Values array
inline constexpr std::array<std::uint32_t, N> prefix_values = {{
    prefix::VALUE1, prefix::VALUE2, prefix::VALUE3, prefix::UNI
}};

// All valid values (excludes UNI)
inline constexpr std::array<std::uint32_t, N-1> prefix_all = {{
    prefix::VALUE1, prefix::VALUE2, prefix::VALUE3
}};

// Name-value pairs for iteration
using prefix_pair = std::pair<prefix, const char*>;
inline constexpr std::array<prefix_pair, N> prefix_name_hash = {{
    prefix_pair{prefix::VALUE1, "VALUE1"},
    prefix_pair{prefix::VALUE2, "VALUE2"},
    ...
}};
```

### 3. Type-Safe Array Wrapper

```cpp
// Array indexed by enum values
template<typename T>
struct prefix_array {
    std::array<T, N> data;

    // Type-safe indexing
    constexpr T& operator[](prefix idx);
    constexpr const T& operator[](prefix idx) const;
    constexpr T& at(prefix idx);  // With bounds checking
    constexpr const T& at(prefix idx) const;

    // Standard container interface
    constexpr auto begin();
    constexpr auto end();
    constexpr std::size_t size() const;
    constexpr T& front();
    constexpr T& back();
    constexpr T* ptr();
    // ... more standard methods
};
```

### 4. Utility Functions

```cpp
// Convert to string (constexpr)
INLINE constexpr const char* to_string(prefix n);

// Get number of enum values
INLINE constexpr std::size_t prefix_num_syms();

// Check if value is valid (excludes UNI)
INLINE constexpr bool is_valid(prefix n);

// Check if string is valid enum name
INLINE bool prefix_is_valid(std::string_view nam);

// Parse from string (throws on error)
INLINE prefix prefix_index_of(std::string_view str);

// Parse from string (returns optional)
INLINE std::optional<prefix> prefix_try_parse(std::string_view str);
```

### 5. Comparison Operators

```cpp
// Explicit comparison operators (constexpr, noexcept)
INLINE constexpr bool operator==(const prefix& a, const prefix& b) noexcept;
INLINE constexpr bool operator!=(const prefix& a, const prefix& b) noexcept;
INLINE constexpr bool operator<(const prefix& a, const prefix& b) noexcept;
INLINE constexpr bool operator<=(const prefix& a, const prefix& b) noexcept;
INLINE constexpr bool operator>(const prefix& a, const prefix& b) noexcept;
INLINE constexpr bool operator>=(const prefix& a, const prefix& b) noexcept;
```

### 6. Map-like Interface

```cpp
struct prefix_hash_t {
    constexpr bool contains(std::string_view key) const;
    constexpr prefix operator[](std::string_view key) const;
    constexpr auto find(std::string_view key) const;
    constexpr auto end() const;
};
inline constexpr prefix_hash_t prefix_hash;

// Usage:
if (prefix_hash.contains("VALUE1")) {
    auto val = prefix_hash["VALUE1"];
}
```

### 7. Stream Operator

```cpp
INLINE std::ostream& operator<<(std::ostream& out, const prefix& p);

// Usage:
std::cout << my_enum_value << std::endl;
```

### 8. std::hash Specialization

```cpp
// In std namespace - enables use with unordered containers
namespace std {
    template<> struct hash<en::prefix> {
        std::size_t operator()(en::prefix e) const noexcept {
            return std::hash<std::uint32_t>{}(e.value);
        }
    };
}

// Usage:
std::unordered_map<en::prefix, double> my_map;
std::unordered_set<en::prefix> my_set;
```

---

## Using Enums in Containers

The generated enums support various container types with full type safety.

### 1. Using with std::map (Ordered Map)

**Works out of the box** - uses `operator<` for ordering:

```cpp
#include "enum/mat.hpp"
#include <map>

using namespace en;

// Create a map with enum keys
std::map<mat, double> tenor_prices;

// Insert values
tenor_prices[mat::_2Y] = 100.0;
tenor_prices[mat::_5Y] = 102.5;
tenor_prices[mat::_10Y] = 105.0;

// Lookup
if (auto it = tenor_prices.find(mat::_5Y); it != tenor_prices.end()) {
    std::cout << "5Y price: " << it->second << std::endl;
}

// Iterate (in order)
for (const auto& [tenor, price] : tenor_prices) {
    std::cout << to_string(tenor) << ": " << price << std::endl;
}
```

### 2. Using with std::unordered_map (Hash Map)

**Fast O(1) lookups** - uses `std::hash<en::mat>` specialization:

```cpp
#include "enum/mat.hpp"
#include <unordered_map>

using namespace en;

// Create an unordered map with enum keys (fast lookups!)
std::unordered_map<mat, double> tenor_stats;

// Insert values
tenor_stats[mat::_2Y] = 1.234;
tenor_stats[mat::_5Y] = 5.678;
tenor_stats[mat::_10Y] = 10.111;

// Fast O(1) lookup
double stat = tenor_stats[mat::_5Y];

// Check if key exists
if (tenor_stats.contains(mat::_7Y)) {  // C++20
    // ...
}

// Or pre-C++20:
if (tenor_stats.find(mat::_7Y) != tenor_stats.end()) {
    // ...
}
```

**Real-world example from RVModel.hpp:**
```cpp
struct ModelParams {
    std::unordered_map<en::mat, double> mean_map;
    std::unordered_map<en::mat, double> std_map;
};

// Usage:
params.mean_map[tenor] = calculated_mean;
params.std_map[tenor] = calculated_std;
```

### 3. Using with mat_array<T> (Type-Safe Fixed Arrays)

**Best for performance** - zero overhead, type-safe indexing:

```cpp
#include "enum/mat.hpp"

using namespace en;

// Create a type-safe array indexed by maturity
mat_array<double> prices;

// Initialize all to zero
prices.data.fill(0.0);

// Or initialize with values
mat_array<double> initialized = {{
    100.0,  // UNI
    98.5,   // 2Y
    99.0,   // 3Y
    99.5,   // 5Y
    100.0,  // 7Y
    100.5,  // 10Y
    101.0,  // 20Y
    101.5   // 30Y
}};

// Type-safe indexing - compiler enforces enum type!
prices[mat::_2Y] = 100.0;
prices[mat::_5Y] = 102.5;
prices[mat::_10Y] = 105.0;

// Bounds-checked access
try {
    double price = prices.at(mat::_5Y);
} catch (const std::out_of_range& e) {
    // Handle error
}

// Iterate over all values
for (auto& price : prices) {
    price *= 1.01;  // Apply 1% increase
}

// Size is known at compile time
static_assert(prices.size() == 8);

// Access underlying array if needed
double* raw_ptr = prices.ptr();
```

**Real-world example from RVModel.hpp:**
```cpp
// Before (old style with raw arrays):
double last_processed_price_[8];  // Easy to index with wrong value!
uint32_t eob_counter_[8];

// After (with mat_array):
mat_array<double> last_processed_price_;  // Type-safe!
mat_array<uint32_t> eob_counter_;

// Usage:
last_processed_price_[mat] = price_mid;  // Compiler checks 'mat' is correct type
eob_counter_[mat]++;
```

### 4. Using with std::unordered_set

```cpp
#include "enum/mat.hpp"
#include <unordered_set>

using namespace en;

// Set of tenors to process
std::unordered_set<mat> active_tenors = {
    mat::_2Y, mat::_5Y, mat::_10Y
};

// Check membership
if (active_tenors.contains(mat::_5Y)) {
    // Process 5Y tenor
}

// Add/remove
active_tenors.insert(mat::_7Y);
active_tenors.erase(mat::_2Y);
```

### 5. Performance Comparison

| Container Type | Lookup Time | Memory | Use Case |
|---------------|-------------|---------|----------|
| `mat_array<T>` | O(1) - fastest | Fixed: `N * sizeof(T)` | Known fixed set of enum values |
| `std::unordered_map<mat, T>` | O(1) average | Dynamic | Sparse data, not all enums used |
| `std::map<mat, T>` | O(log N) | Dynamic | Need ordered iteration |

**Recommendation:**
- Use **`mat_array<T>`** when you have data for ALL or MOST enum values (best performance)
- Use **`std::unordered_map`** when data is sparse or dynamic
- Use **`std::map`** when you need ordered iteration

### 6. Comparison Operations

```cpp
using namespace en;

mat a = mat::_2Y;
mat b = mat::_5Y;

// All comparison operators work
if (a == b) { /* ... */ }
if (a != b) { /* ... */ }
if (a < b) { /* ... */ }   // Compares by enum value (UNI=0, _2Y=1, _3Y=2, etc.)
if (a <= b) { /* ... */ }
if (a > b) { /* ... */ }
if (a >= b) { /* ... */ }

// Sorting
std::vector<mat> tenors = {mat::_10Y, mat::_2Y, mat::_5Y};
std::sort(tenors.begin(), tenors.end());  // Sorts by enum value order
```

---

## Best Practices

### 1. Naming Conventions

**Enum file names**: `lowercase_underscore.enum`
- ✅ `order_type.enum`
- ✅ `buy_sell.enum`
- ❌ `OrderType.enum`

**Prefix names**: Short, lowercase
- ✅ `bs` (buy_sell)
- ✅ `os` (order_side)
- ✅ `mat` (maturity)
- ❌ `BuySell`

**Value names**: UPPERCASE
- ✅ `BUY`, `SELL`, `BOTH`
- ✅ `_2Y`, `_5Y` (leading underscore for numeric identifiers - auto-stripped in strings)
- ✅ `_10s`, `_5min` (leading underscore when starting with digit)
- ❌ `Buy`, `sell`
- ❌ `2Y` (invalid C++ identifier - use `_2Y` instead)

### 2. Ordering

**For persistent enums** (saved to disk/database):
- Add comment: `# do not change order always append at the end`
- ALWAYS append new values at the end
- NEVER reorder existing values
- NEVER remove values (mark deprecated instead)

**Example**:
```
# do not change order always append at the end
x
UNI
CMEMD
CMEILINK1
... existing values ...
NEWEXCH        # ✅ Append here
ANOTHER_NEW    # ✅ And here

```

### 3. Using UNI

Include `UNI` when you need:
- An "undefined" or "invalid" sentinel value
- To distinguish between "not set" vs "set to zero"
- Filtering in validation logic

**Example**:
```
trader
BYHAND
ALGO1
ALGO2
UNI           # Represents "no trader" or "undefined trader"
```

### 4. Code Organization

**Include strategy**:

Option A - Include specific enum:
```cpp
#include "enum/buy_sell.hpp"
using namespace en;
```

Option B - Include all enums:
```cpp
#include "enum/e_names.hpp"  // Includes all enum headers
using namespace en;
```

### 5. Type Safety

The generated enums provide strong type safety:

```cpp
// ✅ Good: Type-safe enum usage
bs side = bs::BUY;
if (side == bs::BUY) { ... }

// ⚠️ Careful: Implicit uint conversion (for backward compatibility)
uint32_t raw_value = side;  // OK, but prefer explicit enum usage

// ✅ Good: Explicit construction from uint
bs from_int(5);  // Requires explicit cast
uint32_t val = 5;
bs from_var(static_cast<uint32_t>(val));
```

---

## Troubleshooting

### Problem: "not found" error when parsing string

```cpp
bs side = bs_index_of("INVALID");  // ERROR: prints "INVALID not found" and calls ERR()
```

**Solution**: Use `try_parse` for safe parsing:
```cpp
auto maybe_side = bs_try_parse("INVALID");
if (maybe_side) {
    bs side = *maybe_side;
} else {
    // Handle invalid input
}
```

### Problem: Generator fails with "No prefix found"

**Cause**: Empty or comment-only enum file

**Solution**: Ensure first non-comment line contains the prefix:
```
# Comments are OK
prefix        # This must be present
VALUE1
```

### Problem: Generator fails with "Enum has duplicate items"

**Cause**: Same value name appears multiple times

**Solution**: Remove duplicate entries:
```
# ❌ Bad
prefix
VALUE1
VALUE2
VALUE1    # Duplicate!

# ✅ Good
prefix
VALUE1
VALUE2
VALUE3
```

### Problem: Compilation error after adding enum value

**Cause**: Stale object files or incomplete rebuild

**Solution**:
```bash
cd /home/vm/m2
make clean
make
```

### Problem: New enum not included in `e_names.hpp`

**Cause**: Generator didn't run or failed silently

**Solution**:
```bash
cd /home/vm/m2/chutil/gencode/enum
./genenum.py 2>&1 | tee generator.log
# Check for errors in output
```

---

## Quick Reference

### Add Value to Existing Enum
```bash
cd /home/vm/m2/chutil/gencode/enum/def
vim existing_enum.enum      # Add value at the end
cd ..
./genenum.py                # Regenerate headers
cd /home/vm/m2
make                        # Rebuild
```

### Create New Enum
```bash
cd /home/vm/m2/chutil/gencode/enum/def
cat > new_enum.enum << 'EOF'
prefix
VALUE1
VALUE2
UNI

EOF
cd ..
./genenum.py
cd /home/vm/m2
make
```

### Verify Generated Code
```bash
# View generated header
cat /home/vm/m2/chutil/include/enum/your_enum.hpp

# Check master include
grep "your_enum" /home/vm/m2/chutil/include/enum/e_names.hpp
```

---

## File Locations Summary

| Item | Location |
|------|----------|
| Generator script | `/home/vm/m2/chutil/gencode/enum/genenum.py` |
| Enum definitions | `/home/vm/m2/chutil/gencode/enum/def/*.enum` |
| Generated headers | `/home/vm/m2/chutil/include/enum/*.hpp` |
| Master include | `/home/vm/m2/chutil/include/enum/e_names.hpp` |

---

## Examples

See existing enums for reference:
- **Simple enum**: [buy_sell.enum](def/buy_sell.enum) - Basic BUY/SEL
- **Sequential values**: [mat.enum](def/mat.enum) - Maturities (_2Y, _3Y, etc.)
- **Large enum**: [alpha.enum](def/alpha.enum) - 50+ trading algorithm parameters
- **Persistent enum**: [exch_code.enum](def/exch_code.enum) - Exchange codes (order preserved)

---

**Generated by**: genenum.py
**Last updated**: 2025-11-22
