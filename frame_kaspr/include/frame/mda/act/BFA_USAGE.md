<!--
    Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
    Licensed under the MIT License. See LICENSE file in the project root.
-->

# BFA Actor Usage (Binary File Adapter)

BFA reads historical market data from binary files and replays it through the system.

## Location
`frame_kaspr/include/frame/mda/act/BFA.hpp`

## Purpose

BFA (Binary File Adapter) reads gzip-compressed L3 market data files and sends
the data to Order Book (OB) actors, simulating a live market data feed.

## Data Flow

```
[BFA] reads file.bin.gz
   |
   v
[OB] receives L3 messages (add/modify/delete/trade)
   |
   v
[Aggregator] aggregates across OBs, calculates BBBO
   |
   v
[Lights] receive BBBO updates, make trading decisions
   |
   v
[SOM] receives orders, simulates fills (in sim_mode)
```

## Timing

**CRITICAL**: All timestamps come from the market data in the file.

- BFA extracts timestamps from each L3 record
- Timer actor uses these timestamps to schedule alarms
- DO NOT use `chutil::Time::get_ts()` or calculate current time
- Just schedule alarms at absolute times (e.g., 9:00 AM)
- The Timer will fire when market data timestamps reach those times

## File Format

BFA reads files containing:
- `l3_fdf_t`: Symbol definition messages (security ID, units, etc.)
- `l3_mbo_v2_t`: Order add/modify/delete (MBO = Market By Order)
- `l3_mbo_trd_v2_t`: Trade messages

## Construction

```cpp
// order_books: 2D vector [venue][asset_id] of OB pointers
// data_file: path to gzip-compressed data file
// manager: pointer to the Manager
auto bfa = new frame::mda::act::BFA(order_books, data_file, manager);
```

## Usage in Simulation

BFA must be added to the Group LAST, after all other actors are set up.
This ensures OBs are ready to receive data when BFA starts reading.

```cpp
// Create other actors first
create_order_books();
create_timer();
create_aggregator();
create_som();
create_lights();

// BFA last - starts data flow
create_bfa();  // Must be last!
```

## Example Data Files

Treasury futures data in `/home/data/treas/data/`:
- `490.20250124.bin.gz` - ZN (10-year) futures for Jan 24, 2025
- Format: sec_id.YYYYMMDD.bin.gz
