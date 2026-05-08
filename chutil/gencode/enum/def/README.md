<!--
    Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
    Licensed under the MIT License. See LICENSE file in the project root.
-->

# Enum Definition Files

This directory contains enum definition files (`.enum`) that are processed by `genenum.py` to generate C++ enum headers.

## Quick Start

**To add a new value to an existing enum:**
1. Edit the `.enum` file (e.g., `mat.enum`)
2. Add your value at the END of the file (before the final blank line)
3. Run: `cd .. && ./genenum.py`

**To create a new enum:**
1. Create a new `.enum` file in this directory
2. Follow the format shown in existing files
3. Run: `cd .. && ./genenum.py`

## File Format

```
prefix_name         # Line 1: C++ struct name (lowercase)
VALUE1             # Line 2+: Enum values (UPPERCASE)
VALUE2
VALUE3
UNI                # Optional: "undefined" marker

```

**Note**: Always end with a blank line.

## Important Rules

⚠️ **For enums marked "do not change order":**
- ALWAYS add new values at the END
- NEVER reorder existing values
- NEVER remove values (they may be in serialized data)

## Examples

**Simple enum** (`buy_sell.enum`):
```
bs
BUY
SEL
UNK
UNI

```

**Maturity enum** (`mat.enum`):
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

## What You Get

After running `genenum.py`, your enum will have:

✅ **Type-safe enum struct** with implicit uint conversion
✅ **Constexpr lookup tables** for zero-overhead string conversion
✅ **Hash support** - use with `std::unordered_map<en::mat, T>`
✅ **Array wrapper** - type-safe `mat_array<T>` indexed by enum
✅ **Comparison operators** - full support for `==`, `<`, etc.
✅ **String parsing** - convert "2Y" ↔ `mat::_2Y`

**Example usage:**
```cpp
#include "enum/mat.hpp"
using namespace en;

// Unordered map (O(1) lookups)
std::unordered_map<mat, double> prices;
prices[mat::_5Y] = 100.0;

// Type-safe array
mat_array<double> data;
data[mat::_2Y] = 98.5;

// String conversion
std::cout << to_string(mat::_10Y);  // Prints "10Y"
mat m = mat_index_of("5Y");         // Parses to mat::_5Y
```

## More Information

See `../ENUM_GUIDE.md` for complete documentation.
