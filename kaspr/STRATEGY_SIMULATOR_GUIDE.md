<!--
    Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
    Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
    Licensed under the MIT License. See LICENSE file in the project root.
-->

# Kaspr Strategy Simulator — Developer Guide

Kaspr is a CME market data and execution system for ES and NQ futures. Market data can come from a live multicast feed, a PCAP capture, or a decoded binary record file; orders can be filled by the simulated order manager or routed live over iLink. The two choices are independent.

## Operating Modes

It helps to think of a run as two independent choices: **where market data comes
from**, and **where orders go**. Any data source can be paired with either
execution path.

| | Data source | Reader actor | Clock |
|---|---|---|---|
| **Live multicast** | CME MDP3 groups (310 / 318 / 344) | `SocketReader` → `MsgBuf` | real time |
| **PCAP replay** | `.pcap` / `.pcap.zst` capture of those same groups | `PCAPReader` → `MsgBuf` | timestamps in the capture |
| **Binary replay** | `.bin.gz` of decoded L3 records | `BFA` (straight to the books) | timestamps in the file |

| | Execution |
|---|---|
| **Simulated (`sim_mode=true`)** | SOM matches orders against the reconstructed book |
| **Live** | SOM routes to CME Globex over iLink 3 |

Everything downstream of `MsgBuf` — `MessageProcessor`, `handler_if`, `OB` /
`TachBook`, `light22`, `SOM` — is identical no matter which source feeds it.
That is the point of the design: a strategy cannot tell whether it is being run
on a capture or on the live feed.

### 1. Paper trading — live data, simulated execution

The default, and what `./kaspr config/kaspr.ini` gives you today.

```
CME Multicast → SocketReader → MsgBuf → MessageProcessor → handler_if → OB.cpp → Lights → SOM (sim_mode=true)
```

- Data source: live MDP3 multicast (channels 310, 318, 344)
- Order book: `OB.cpp` (MBP), optionally `TachBook` alongside it
- Execution: SOM simulates fills against the book
- Use case: forward testing, strategy validation under real market conditions

**Simulated execution does not require recorded data.** The `SocketReader` path
is fully supported for simulation — it simply runs on real-time data, so a
session lasts as long as you let it run and is not repeatable. Use it when you
want live market conditions; use PCAP or binary replay when you want the same
session twice.

### 2. Simulation — PCAP replay

Replay a capture of the same multicast groups against the simulated order
manager. Identical decode path, deterministic and repeatable.

```
.pcap / .pcap.zst → PCAPReader → MsgBuf → MessageProcessor → handler_if → OB.cpp → Lights → SOM (sim_mode=true)
```

- Data source: `.pcap` or `.pcap.zst` files captured from CME multicast.
  `.zst` is read natively by piping through `zstdcat` — no need to decompress first.
- Reader: `mcast_recv::PCAPReader` (`mcast_recv/include/mcast_recv/act/PCAPReader.hpp`),
  assembled by `create_all_mdp3_pcap()` in `interface/mdp3/if/mdp3.hpp`
- Order book: `OB.cpp` (MBP), or `TachBook` for order-by-order
- Execution: SOM simulates fills against the book
- Use case: backtesting, strategy development, regression testing

What `PCAPReader` does per packet: parses Ethernet (incl. 802.1Q VLAN) → IPv4/IPv6
→ UDP, optionally strips a hardware-timestamp trailer (`TrailerSpec`, e.g. Metamako —
validated against the wire on the first packet so a misconfiguration fails loudly),
and hands the MDP3 payload to `MsgBuf` carrying both a software capture timestamp
(`recv_ts`, from the pcap record header) and a hardware one (`hw_ts`, from the
trailer; 0 if absent). It reads **one packet per `Continue` message to itself**, so
replay is a self-clocked actor loop rather than a blocking read; EOF triggers
`ShutdownThisActor`.

Two limits worth knowing:

- **No recovery and no A/B arbitration.** `create_all_mdp3_pcap()` passes a null
  recovery processor, and `PCAPReader` stamps every packet as feed `'A'`. Captures
  are expected to be already de-duplicated (as Databento's are). A gap in the
  capture is not repaired — it propagates to the book.
- **Timing comes from the data, never the wall clock.** The `Timer` actor is driven
  by market-data timestamps, which is what makes replay deterministic.

### 3. Simulation — binary (`.bin.gz`) replay

The fast path for repeated backtests. Once a session has been decoded once, the
per-order L3 event stream can be written to a gzip'd record file and replayed
without re-parsing Ethernet/UDP/SBE.

```
.bin.gz → BFA → OB.cpp / TachBook → Lights → SOM (sim_mode=true)
```

- Writer: `BinRecorder` (`frame/mda/act/BinRecorder.hpp`) — set `handler->binrec`
  and it records every L3 event via `bfile::write_l3`
- Reader: `BFA` (`frame/mda/act/BFA.hpp`) — see
  [`BFA_USAGE.md`](../frame_kaspr/include/frame/mda/act/BFA_USAGE.md)
- Format: flat gzip stream of type-tagged packed structs from
  `chutil/include/bfile/r_l3.hpp` (`MBO_V2`, `MBOT_V2`, `MBOS`, `FDF`, …). Not
  seekable, no index; the struct layout is the format.
- Naming convention: `<sec_id>.<YYYYMMDD>.bin.gz`, e.g. `490.20250124.bin.gz`
- `BFA` supports `start_h` / `end_h` hour filtering with early termination, useful
  for RTH-only runs

`BFA` must be added to its `Group` **last**, after the OBs, lights and SOM exist —
otherwise it starts pushing data into actors that are not ready.

### 4. Live trading — iLink

Live multicast for data, iLink 3 for execution. `TachBook` replaces `OB.cpp` for
full MBO book reconstruction.

```
CME Multicast → SocketReader → MDP3 → TachBook → Lights → SOM → iLink → CME
```

- Data source: live MDP3 multicast
- Order book: `TachBook` (MBO L3, order-by-order) — build with `USE_TACHBOOK=1`
- Execution: iLink session to CME Globex
- Requires: iLink credentials (firm ID, session keys, access token)

### Wiring status

The `kaspr` binary currently constructs the **live multicast** path only —
`kaspr.cpp` calls `create_all_mdp3()`, sets `handler->binrec = nullptr`, and
`main()` accepts only a config path and `--reset-positions`. The PCAP and binary
replay paths above are present and complete as libraries but are **not yet
reachable from the command line**; wiring them is three small changes in
`kaspr.cpp` (select `create_all_mdp3_pcap()` on a `--pcap` flag, construct a
`BinRecorder` on a `--record` flag, add `BFA` on a `--replay` flag).

## Build

```bash
# Simulation / paper trading (default)
cd ~/m2_kaspar
KSPRPROJ=~/m2_kaspar make

# Live trading with TachBook MBO
KSPRPROJ=~/m2_kaspar USE_TACHBOOK=1 make
```

## Running

```bash
cd ~/m2_kaspar/kaspr
./kaspr config/kaspr.ini                   # paper trading (live data, sim fills)
./kaspr config/kaspr.ini --reset-positions  # reset positions on startup
```

## Data Flow

```
                CME MDP3 Multicast
                       |
            +----------+----------+
            |                     |
       Channel 310           Channel 318
       (ES futures)          (NQ futures)
            |                     |
       handler_if            handler_if
            |                     |
     +------+------+       +------+------+
     |             |       |             |
   OB_ESM6     OB_ESU6  OB_NQM6     OB_NQU6
     |             |       |             |
     +------+------+       +------+------+
            |                     |
    Lights (4 BUY + 4 SEL)   Lights (4 BUY + 4 SEL)
            |                     |
            +----------+----------+
                       |
                  SOM (sim or iLink)
                       |
                  DB (fill logging)
```

## Strategy Development

### C++ Strategies (Simplest)

Instantiate strategy actors directly in `kaspr.cpp`. They subscribe to order book events and send orders to SOM.

```cpp
// In Kaspr constructor, after create_lights():
auto strategy = new MyStrategy(es_order_books[0], som[en::x::CMEMDFUT]);
add_to_manage_q(strategy);
```

A C++ strategy actor receives `EndOfBurst` messages from OB and sends `Order`/`Cancel` messages to SOM. This is the lowest-latency path — no serialization, no network hops.

### Rust Strategies (via in-process interop)

Strategies can also be written in Rust and run **in the same process** through the C++/Rust FFI
interop (see [`actors/rust/interop/README.md`](../actors/rust/interop/README.md)) — a Rust actor
receives OB events and sends orders to SOM over the C-ABI bridge, no serialization or network hops.
This is in-process only; there is no remote/cross-process strategy transport.

## Order Book: OB.cpp vs TachBook

| | OB.cpp | TachBook |
|---|--------|----------|
| Type | MBP (Market-by-Price) | MBO (Market-by-Order) |
| Depth | Aggregated price levels | Individual orders |
| Build flag | Default | `USE_TACHBOOK=1` |
| Use case | Simulation, paper trading | Live trading |
| Latency | Lower (less processing) | Higher (per-order tracking) |

In simulation mode, OB.cpp is sufficient — SOM matches orders against aggregated price levels. For live trading, TachBook provides the full order-by-order book required for accurate queue position estimation and iLink interaction.

## Execution: Shadow Algorithm

Kaspr uses a **shadow execution algorithm** in `light22`. Rather than continuously quoting, each light observes real order book activity and places orders only when market microstructure signals (ADDs, CANCs) suggest favorable conditions.

Key properties:
- **4 buy + 4 sell lights per instrument** sharing position via PCoord/QCoord
- **Piggybacks on real flow** — places at prices where genuine interest exists
- **Self-managing** — automatic cancellation on position breach, price drift, or attached order execution
- **Configurable throttle** — deterministic (every N events) or stochastic (3% probability)

See [light/SHADOW_ALGORITHM.md](../light/SHADOW_ALGORITHM.md) for the full algorithm description.

## Console Commands

Connect via ZMQ (`tools/console_client.py`) to the MQ0 port (default 7777):

| Command | Description |
|---------|-------------|
| `ping` | Health check |
| `prices` | Show bid/ask for all instruments |
| `bbbo` | Show best bid/offer (32nds format) |
| `assets` | List configured instruments |
| `fills` | Show recent fills |
| `get_orders` | Show working orders |
| `order sym=ESM6 sz=1 bs=BUY px=6000 x=CMEMDFUT` | Place order |
| `cancel id=123 x=CMEMDFUT` | Cancel order |
| `startom` | Enable order matching in SOM |
| `stopom` | Disable order matching in SOM |

## Configuration

### kaspr.ini

```ini
kaspr {
    general {
        universe config/universe.csv
        mqport 7777
        tachbook false           ; set true + USE_TACHBOOK=1 for live
    }
    channels {
        chan_310 true             ; ES futures
        chan_318 true             ; NQ futures
        chan_344 false            ; Treasury futures
    }
}
```

### universe.csv

Defines instruments. Each line: `Type,Symbol,Alias,Mnemonic,Factor,...,Exchange`

```csv
F,ESM6,ES,ES,1.,1,1,100000,1,1,unk,x,0,CMEMDFUT
F,NQM6,NQ,NQ,1.,1,1,200000,1,1,unk,x,0,CMEMDFUT
```

## Known Gaps

- **Replay not reachable from the CLI**: `PCAPReader`, `BinRecorder` and `BFA` are all
  implemented, but `kaspr.cpp` only constructs the live-multicast path — there are no
  `--pcap` / `--record` / `--replay` flags yet. See *Wiring status* above.
- **Producing `.bin.gz` from vendor captures**: `BinRecorder` writes the format, but
  batch conversion of a day of vendor PCAPs (many files per channel, snapshot/IR streams
  for instrument definitions) is handled by separate M2-internal tooling that is not part
  of this repository.
- **iLink configuration**: iLink credential management and session config need to be added for live trading mode.
