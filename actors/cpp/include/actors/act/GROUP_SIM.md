<!--
    Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
    Licensed under the MIT License. See LICENSE file in the project root.
-->

# Group Actor for Simulation

The Group actor runs multiple actors in a single thread, providing deterministic
execution for simulation and backtesting.

## Location
`actors/cpp/include/actors/act/Group.hpp`

## Purpose

In production, each actor runs in its own thread for parallel processing.
In simulation, we want **deterministic, serialized execution** so that:
- Results are reproducible
- Timing is controlled by market data timestamps
- No race conditions or timing issues

## How It Works

1. Create a Group actor
2. Add all simulation actors to the Group via `group->add(actor)`
3. Only manage the Group with the Manager (not individual actors)
4. All actors in the Group share one thread and process messages sequentially

## Architecture

```
[Manager] (SimKaspr)
   |
   | manage(group)   <-- ONLY the Group is managed
   v
[Group] "sim_group" (single thread)
   |
   | add(actor)  <-- all actors added to group
   v
   +--[Timer]
   +--[BFA]
   +--[OB]
   +--[Aggregator]
   +--[SOM]
   +--[PositionManager]
   +--[MarketMaker]
   +--[Lights...]
```

## Usage

```cpp
// In your simulation manager constructor:

void create_simulation() {
  // 1. Create the Group - this is the ONLY actor that gets managed
  group = new actors::Group("sim_group");
  manage(group);  // <-- ONLY the group is managed

  // 2. Create all other actors and add them to the group
  timer = new frame::mtim::act::Timer(ob);
  group->add(timer);  // <-- NOT manage(), use add()

  bfa = new frame::mda::act::BFA(order_books, data_file, this);
  group->add(bfa);  // <-- NOT manage(), use add()

  // ... add all other actors to group
}
```

## Key Rules

1. **Only the Group is managed**: `manager->manage(group)`
2. **All simulation actors are added to Group**: `group->add(actor)`
3. **BFA must be added last**: It starts the data flow
4. **Never manage individual actors**: They would run in separate threads

## Why This Matters

- **Deterministic**: Same input always produces same output
- **Reproducible**: Bugs can be debugged by replaying same data
- **No race conditions**: Single thread means no synchronization issues
- **Timing from data**: All timestamps come from market data, not system clock

## Common Mistakes

❌ **WRONG**: Managing individual actors
```cpp
manage(timer);  // WRONG - runs in separate thread
manage(bfa);    // WRONG - runs in separate thread
```

✅ **CORRECT**: Only manage the Group
```cpp
group = new actors::Group("sim_group");
manage(group);  // CORRECT - only group is managed
group->add(timer);  // CORRECT - added to group
group->add(bfa);    // CORRECT - added to group
```
