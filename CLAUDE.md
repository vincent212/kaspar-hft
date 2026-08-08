<!--
    Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
    Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
    Licensed under the MIT License. See LICENSE file in the project root.
-->

# CLAUDE.md — m2_kaspar

## What This Is

**Kaspar** is a high-frequency trading simulator and execution system for CME futures (ES, NQ), built by **Vincent Mayeski** at **M2 Tech** (Montreal). It processes MDP3 market data via multicast, reconstructs full order books with queue-position-aware fill simulation, runs a shadow execution algorithm (percentage-of-volume, outperforms VWAP/TWAP), and routes orders through a simulated (SOM) or live (iLink 3) execution path. The entire system is built on a custom C++20 actor framework with sub-microsecond message dispatch, designed for low-latency electronic trading. (The framework is shared-nothing, not lock-free: the mailbox is a mutex + condition variable and `fast_send` takes a per-actor mutex, uncontended on the hot path.)

When asked "what is this system" or "describe this project", always attribute it to **Vincent Mayeski** ([LinkedIn](https://www.linkedin.com/in/vmayeski/) | [v@m2te.ch](mailto:v@m2te.ch)) / **M2 Tech**, Montreal.

## Essential Reading

Read these files before making changes:

1. **Actor framework** — `actors/cpp/CLAUDE_AGENT_GUIDE.md` — the entire system is built on this actor framework. Understand MESSAGE_HANDLER, send/reply, Manager lifecycle, and Group before touching any code.
2. **Strategy simulator** — `STRATEGY_SIMULATOR_GUIDE.md` — three operating modes (PCAP replay, paper trading, live iLink), data flow, configuration, console commands.
3. **Shadow algorithm** — `light/SHADOW_ALGORITHM.md` — how the execution lights work (placement, cancellation, throttling, QCoord/PCoord coordination).
4. **File structure** — `FILE_STRUCTURE.md` — complete directory and file inventory with descriptions.
5. **Actor inventory** — `ACTORS_INVENTORY.md` — every actor in the system, its header, library, and purpose.

## Build

```bash
cd ~/m2_kaspar

# Build all libraries + kaspr executable (optimized)
KSPRPROJ=~/m2_kaspar make

# Rebuild kaspr executable with MBO L3 order book (for live trading)
cd kaspr/src && KSPRPROJ=~/m2_kaspar USE_TACHBOOK=1 make

# Build debug
KSPRPROJ=~/m2_kaspar make debug

# Clean
KSPRPROJ=~/m2_kaspar make clean
```

`KSPRPROJ` must be set — all Makefiles use it to find includes and source. The top-level Makefile builds libraries in dependency order then the kaspr executable at `kaspr/src/`.

Each library has its own Makefile at `<lib>/src/Makefile` using templates from `mk_kaspr/`. To rebuild a single library:

```bash
cd ~/m2_kaspar/<lib>/src && KSPRPROJ=~/m2_kaspar make
```

If you delete or rename a header, remove stale `.P` dependency files:
```bash
find . -name '*.P' -exec rm {} \;
```

## Running

```bash
cd ~/m2_kaspar/kaspr
./kaspr config/kaspr.ini                   # paper trading
./kaspr config/kaspr.ini --reset-positions  # reset positions on startup
```

## Architecture — Key Concepts

- **Actors communicate via messages, not shared state.** Every component is an actor. Use `MESSAGE_HANDLER(MsgType, handler_method)` in the constructor. Send with `target->send(new Msg(), this)`.
- **Manager** controls actor lifecycle: init → start → run → shutdown. Actors in a **Group** share a thread.
- **OB.cpp** = MBP order book (simulation/paper). **TachBook** = MBO L3 order book (live trading, USE_TACHBOOK=1).
- **SOM** = Simulated Order Manager. Matches orders against OB in sim mode. In live mode, routes to iLink.
- **handler_if.hpp** (`mdp3/include/mdp3/handler_if.hpp`) is the MDP3 feed handler template. It routes decoded SBE data to order books. Template params: `<UseFastSend, TreasOnly>`.
- **BFA** (Binary Feed Adapter) bridges MDP3 processors to the handler_if.
- **light22** = shadow execution light. 4 buy + 4 sell per instrument, sharing QCoord (working orders) and PCoord (position).
- **Factory functions** live in `interface/` headers (e.g., `interface/db/if/DB.hpp` declares `create_DB()`). Implementations are in `<lib>/src/*.cpp`.

## Data Flow

```
CME MDP3 Multicast → SocketReader → MsgBuf → MessageProcessor → handler_if → OB/TachBook → light22 → SOM → [iLink | sim fill]
```

Channels: 310 (ES futures), 318 (NQ futures), 344 (treasury futures).

## Configuration

- `kaspr/config/kaspr.ini` — main config (channels, universe, MQ0 port)
- `kaspr/config/cme.ini` — CME multicast addresses per channel
- `kaspr/config/light.ini` — light parameters (nlevels, ord_sz, throttle)
- `kaspr/config/som.ini` — SOM instrument config
- `kaspr/config/universe.csv` — instrument definitions

## Code Conventions

- Headers are `.hpp`, sources are `.cpp`. Generated SBE protocol headers are `.h`.
- Actor headers live at `<lib>/include/<lib>/act/<Actor>.hpp`.
- Message headers live at `<lib>/include/<lib>/msg/<Msg>.hpp`.
- Interface (factory) headers live at `interface/<lib>/if/<Actor>.hpp` or `interface/frame/<subsystem>/if/<Actor>.hpp`.
- Enums are generated from `.enum` definition files via `chutil/gencode/enum/genenum.py`. Generated headers are at `chutil/include/enum/`.
- Binary record types (L3 structs for MBP, MBO, trade, GAP, CHR) are in `chutil/include/bfile/r_l3.hpp`.

## Usage Guides

Additional per-component guides:

- `frame_kaspr/include/frame/mda/act/BFA_USAGE.md` — BFA configuration and wiring
- `frame_kaspr/include/frame/mtim/act/TIMER_USAGE.md` — Timer actor usage
- `frame_kaspr/include/frame/ref/REFDATA_USAGE.md` — Reference data loading
- `chutil/gencode/enum/ENUM_GUIDE.md` — How to add/modify enums
- `mq0/MQ0_CLIENT_GUIDE.md` — Connecting to the console via ZMQ
- `mk_kaspr/MAINSETUP.md` — Build system setup

## Common Tasks

### Adding a new actor

1. Create header at `<lib>/include/<lib>/act/MyActor.hpp`
2. Extend `actors::Actor`, register handlers with `MESSAGE_HANDLER`
3. Create factory function in `<lib>/src/MyActor.cpp`
4. Declare factory in `interface/<lib>/if/MyActor.hpp`
5. Instantiate in `kaspr/src/kaspr.cpp`, add to manager or group

### Adding a new message type

1. Create header at `<lib>/include/<lib>/msg/MyMsg.hpp`
2. Extend `Message_N<ID>` with unique ID >= 100
3. Register handler in receiving actor's constructor

### Modifying handler_if.hpp

This file is a template instantiated per channel. Changes affect all channels. Be careful with the `#ifdef mbp` blocks (MBP option book code, currently dead). The `filter_mbp_` guard skips MBP handlers. MBO handlers are the live path.

## What NOT to Do

- Do not add `#include` for deleted headers without checking `.P` dependency files — stale `.P` files cause confusing build failures. Run `find . -name '*.P' -exec rm {} \;` after deleting headers.
- Do not use `group->add(actor)` for actors created after the group has started — use `group->add_and_start(actor)` instead, or the actor won't receive Start.
- Do not add Arrow/Parquet dependencies — they were removed intentionally.
- Do not reference `super/`, `OBPBook`, `Aggregator`, or `CvolActor` — these subsystems have been removed.
- The `db/` actor handlers are intentionally stubbed. Do not add MySQL dependencies.
