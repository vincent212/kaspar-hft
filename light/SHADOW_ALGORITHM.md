<!--
    Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
    Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
    Licensed under the MIT License. See LICENSE file in the project root.
-->

# Shadow Algorithm — light22

## Overview

The shadow algorithm is an execution strategy that "shadows" real market activity. Instead of continuously quoting or crossing the spread, a shadow light observes the live order book and places orders only when specific market microstructure signals appear. This makes it efficient because it avoids unnecessary order traffic and only acts when the market structure suggests favorable conditions.

## How It Works

A shadow light subscribes to the order book's `EndOfBurst` stream (one message per MDP3 incremental cycle). On each burst it inspects the most recent book update and decides to **place**, **cancel**, or **do nothing**.

### Placement Logic

A buy (sell) light places an order when all of these are true:

1. **Signal is ADD** — a new order was added to the book on this side
2. **Position below target** — current position has not reached `targetpos`
3. **Not rate-limited** — the deterministic or stochastic throttle allows it
4. **Price within range** — the order price is within `max_dist` ticks of the inside market
5. **Capacity available** — total working order size has not exceeded `all_orders_max` or `diff_from_target`
6. **Level not saturated** — size already resting at this price level is below `lev_orders_max`
7. **Not a duplicate** — the exchange order ID that triggered this signal differs from the last one we attached to

The price is taken directly from the market data message that triggered the EOB — the light places at the same price as the real market participant whose order it is shadowing.

### Cancellation Logic

An existing order is cancelled when any of these are true:

1. **Position at zero** — flat, no reason to hold
2. **Position at/beyond target** — already hedged
3. **Over-hedge risk** — working size exceeds remaining distance to target
4. **Level overcrowded** — too many orders at this price level
5. **Attached order hit** — the exchange order we were shadowing got executed or cancelled (delayed cancel via timer)
6. **Price drifted** — order is more than `max_dist_cancel` ticks from inside

### Throttling

Two modes are supported:

- **Deterministic** (`#ifdef DETERMINISTIC_PLACEMENT`): place after every N EOB ADD messages (`place_after_n_eob`, default 5)
- **Stochastic** (default): 3% probability per EOB ADD message

This prevents over-trading while still reacting to market conditions.

## Why It's Efficient

1. **Zero idle quoting** — no orders are placed until a real market participant acts. This eliminates phantom liquidity and reduces message rates by orders of magnitude compared to a traditional market maker.

2. **Piggyback on real flow** — by shadowing real ADDs, the light naturally places orders at prices where genuine interest exists. This avoids adverse selection from stale quotes.

3. **Automatic position management** — the cancel logic continuously trims exposure. Over-hedge protection, level saturation checks, and attached-order tracking mean the light self-manages without external intervention.

4. **Minimal latency path** — one message handler (`eob_handler`) with a short decision tree. No model evaluation, no network round-trips for decision-making. The critical path is: receive EOB → check flags → send Order to SOM.

5. **Shared-memory coordination** — `QCoord` (queue coordinator) and `PCoord` (position coordinator) coordinate multiple lights per instrument via shared memory guarded by fine-grained mutexes, without message-passing overhead.

## Configuration

From `light.ini`:

| Parameter | Default | Description |
|-----------|---------|-------------|
| `nlevels` | 2 | Number of price levels to work |
| `ord_sz` | 1 | Max order size per placement |
| `lev_orders_max` | 1 | Max working size per price level |
| `place_after_n_eob` | 5 | EOB ADD count between placements (deterministic mode) |

Compile-time constants in `light22.hpp`:

| Constant | Value | Description |
|----------|-------|-------------|
| `max_dist` | 4 | Max ticks from inside to place |
| `max_dist_cancel` | 6 | Max ticks from inside before cancel |

## Architecture

```
                    OB (order book)
                        |
                   EndOfBurst
                        |
              +---------+---------+
              |                   |
         light22<BUY>        light22<SEL>
              |                   |
              +----> QCoord <-----+    (per-instrument working order tracking)
              +----> PCoord <-----+    (per-instrument position)
              |                   |
              +-------> SOM <-----+    (order routing: sim or iLink)
```

Each instrument has 4 buy lights + 4 sell lights sharing a single `QCoord` and `PCoord`. The lights operate independently — no coordination messages between them. `QCoord` provides thread-safe working order accounting; `PCoord` provides shared position state.
