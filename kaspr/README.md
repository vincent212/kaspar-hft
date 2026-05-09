<!--
    Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
    Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
    Licensed under the MIT License. See LICENSE file in the project root.
-->

# Kaspr Trading System

Kaspr is a trading system for CME market data that builds order books for:
- **ES Futures** (Channel 310 - CME Globex Equity)
- **ES Options** (Channel 311 - CME Globex Equity Options)
- **Treasury Futures** (Channel 344 - CBOT Interest Rate Futures): ZN, ZF, ZB, ZT, UB

## Build

```bash
# Full build (all libs + kaspr binary)
KSPRPROJ=~/m2_kaspar make

# Build kaspr only (after libs are built)
cd kaspr/src && KSPRPROJ=~/m2_kaspar make

# With TachBook MBO L3 order books
KSPRPROJ=~/m2_kaspar USE_TACHBOOK=1 make

# Run tests
cd kaspr/tests && KSPRPROJ=~/m2_kaspar make test
```

## Running Kaspr

```bash
cd ~/m2_kaspar/kaspr
./start.sh
```

## Configuration

### Main Config: `config/kaspr.ini`

```ini
kaspr {
    general {
        universe config/universe.csv
        mqport 9001           ; ZMQ monitoring port
    }

    channels {
        chan_310 true         ; ES futures
        chan_311 true         ; ES options
        chan_344 false        ; Treasury futures (ZN, ZF, ZB, ZT, UB)
    }
}
```

### Other Config Files

| File | Purpose |
|------|---------|
| `config/cme.ini` | MDP3 multicast settings per channel |
| `config/ilink.ini` | iLink session config |
| `config/light.ini` | Light (order execution) parameters |
| `config/som.ini` | SOM position/size limits |
| `config/universe.csv` | Instrument definitions |

## Subsystem Instantiation

When kaspr starts, it creates these components in order:

### 1. Order Books (`create_order_books`)

Creates OB actors for each instrument in the universe:

| Channel | Venue | Products | OB Vectors |
|---------|-------|----------|------------|
| 310 | CMEMDFUT | ESH6, ESM6 | `es_order_books` |
| 344 | CMEMDFUT | ZNH6, ZFH6, ZBH6, ZTH6, UBH6 | `zn_order_books`, `zf_order_books`, `zb_order_books`, `zt_order_books`, `ub_order_books` |

### 2. Support Modules (`create_support_modules`)

- **CONS** - Console handler for interactive commands
- **MQ0** - ZMQ server for external monitoring (port from config)
- **Timer** - System timer for scheduled events

### 3. Aggregators

- **ES Aggregator** - combines ES front-month books
- **NQ Aggregator** - combines NQ books (if enabled)
- **Treasury Futures Aggregators** - ZN, ZF, ZB, ZT, UB

### 4. ES Options (`create_option_books`)

Lightweight OBPBook actors (BBO-only) for ~16,698 ES options on channel 311.
Created dynamically by handler_if when ODF messages arrive.

### 5. CVOL Actors (`create_cvol_actors`)

Per-week CvolActors + BestCvolActor per product. Computes CBOE-style
corridor implied volatility from the ES options order books.

### 6. SOM - Simulated Order Manager (`create_som`)

In sim_mode, SOM simulates order fills against the order book instead of sending to iLink.

### 7. Lights (`create_lights`)

Order execution actors - 4 per side per instrument.

### 8. PositionManager (`create_positionman`)

Tracks positions per instrument. Persisted to MySQL across restarts.

### 9. MTD - Monitoring (`create_mtd`)

Subscribes to BBBO from all order books, fills from SOM. Handles console commands.

### 10. MDP3 Market Data (`start_market_data`)

For each enabled channel, creates RecoveryProcessor, MessageProcessor, MsgBuf, and SocketReader A/B feeds.

## Architecture

```
                +-------------+
                |   MDP3      |
                |  Multicast  |
                +------+------+
                       |
          +------------+------------+
          v            v            v
    +----------+ +----------+ +----------+
    | Chan 310 | | Chan 311 | | Chan 344 |
    |   (ES)   | |(ES opts) | |(Treas F) |
    +----+-----+ +----+-----+ +----+-----+
         |            |            |
         v            v            v
    +-------------------------------------+
    |           handler_if                |
    |  (routes data to order books)       |
    +-----------------+-------------------+
                      |
    +-----------------+-----------------+
    v                 v                 v
+--------+      +---------+       +--------+
| OB_ES  |      | OBPBook |       | OB_ZN  |
|  ...   |      | (opts)  |       | OB_ZF  |
+---+----+      +----+----+       +--------+
    |                |
    v                v
+--------+     +-----------+
|ES_Aggr |     |CvolActors |
+---+----+     +-----------+
    |
    v
+--------+     +--------+     +--------+
| Lights |---->|  SOM   |---->|  MTD   |
+--------+     +--------+     +--------+
```

## Console Commands (via MTD)

Connect via ZMQ to port 9001 (use `tools/console_client.py` or `test/test_strategy.py`):

| Command | Description |
|---------|-------------|
| `prices` | Show all instrument prices |
| `bbbo` | Show best bid/offer for all instruments |
| `assets` | List all configured assets |
| `fills` | Show recent fills |
| `get_orders` | Show active orders |
| `order sym=ESH6 sz=10 bs=BUY px=6000 x=CMEMDFUT` | Place order |
| `cancel id=123 x=CMEMDFUT` | Cancel order |

## Key Files

| File | Purpose |
|------|---------|
| `src/kaspr.cpp` | Main implementation |
| `src/kaspr.hpp` | Header with Kaspr class |
| `src/remote_messages.hpp` | Remote actor message serialization |
| `src/queries.hpp` | MySQL queries for position persistence |
| `config/kaspr.ini` | Main configuration |
| `config/universe.csv` | Instrument definitions |
| `generated/python/messages.py` | Auto-generated Python message types |
| `tools/console_client.py` | ZMQ console client |
| `test/test_strategy.py` | Interactive order test harness |
| `messages.schema.json` | Schema driving code generation |
