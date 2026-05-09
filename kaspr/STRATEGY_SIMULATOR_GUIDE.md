<!--
    Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
    Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
    Licensed under the MIT License. See LICENSE file in the project root.
-->

# Kaspr Strategy Simulator — Developer Guide

Kaspr is a CME market data and execution system for ES and NQ futures. It supports three operating modes: backtesting with recorded data, paper trading with live data, and live trading with iLink.

## Operating Modes

### 1. Simulation — PCAP Replay

Replay recorded multicast data against the simulated order manager (SOM). Fills are simulated by matching orders against the reconstructed order book.

```
PCAP file → SocketReader → MDP3 → OB.cpp → Lights → SOM (sim_mode=true)
```

- Data source: `.pcap` files captured from CME multicast
- Order book: `OB.cpp` (MBP, market-by-price)
- Execution: SOM simulates fills against OB
- Use case: backtesting, strategy development

### 2. Paper Trading — Live Data, Simulated Execution

Connect to live CME multicast feeds. Orders execute against the simulated order manager, not the exchange.

```
CME Multicast → SocketReader → MDP3 → OB.cpp → Lights → SOM (sim_mode=true)
```

- Data source: live MDP3 multicast (channels 310, 318, 344)
- Order book: `OB.cpp` (MBP)
- Execution: SOM simulates fills against OB
- Use case: forward testing, strategy validation with real market conditions

### 3. Live Trading — iLink

Connect to live CME multicast for data and iLink for order execution. TachBook replaces OB.cpp for full MBO (market-by-order) book reconstruction.

```
CME Multicast → SocketReader → MDP3 → TachBook → Lights → SOM → iLink → CME
```

- Data source: live MDP3 multicast
- Order book: `TachBook` (MBO L3, order-by-order) — build with `USE_TACHBOOK=1`
- Execution: iLink session to CME Globex
- Requires: iLink credentials (firm ID, session keys, access token)

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

### Python / Java Strategies (via Coordinator)

External strategies connect via ZMQ using the actor framework's remote messaging:

1. **Coordinator** bridges C++ and external actors
2. Strategy discovers actors via `GlobalRegistry`
3. Strategy subscribes to OB events (serialized via ZMQ)
4. Strategy sends orders to SOM (routed back via ZMQ)

```
Python Strategy ←→ ZMQ ←→ ZmqReceiver ←→ SOM / OB
```

Enable remote messaging in `kaspr.ini`:
```ini
kaspr {
    remote {
        registry tcp://localhost:12020
        zmq_port 12558
    }
}
```

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

- **Binary file reader**: `.bin` replay (BinRecorder format) is not yet implemented in this repo. PCAP replay is supported.
- **iLink configuration**: iLink credential management and session config need to be added for live trading mode.
