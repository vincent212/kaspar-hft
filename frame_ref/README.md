<!--
    Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
    Licensed under the MIT License. See LICENSE file in the project root.
-->

# frame_ref — Reference Data (Headers Only)

Shared reference data types used across kaspr. Headers only — no compiled library.

## Files

| File | Purpose |
|------|---------|
| `Asset.hpp` | Instrument definition (name, mnemonic, venue, multiplier) |
| `Price.hpp` | Price type with tick ↔ display conversion |
| `RefData.hpp` | Singleton registry of all instruments, loaded from `universe.csv` |
