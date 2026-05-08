<!--
    Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
    Licensed under the MIT License. See LICENSE file in the project root.
-->

# OB.cpp — Order Book Simulation Reference

**Scope:** `frame_kaspr/src/OB.cpp` and `frame_kaspr/include/frame/ob/act/OB.hpp`.
This document describes what the order book actor does in simulation mode:
how it builds per-price-level queues from MBO messages, how it decides when
and how sim orders fill against those queues, and what latency / queue-
position knobs exist today.

**Last updated:** 2026-04-18

---

## 1. Sim vs. prod modes

There is **no boolean `sim_mode` flag** on the OB constructor. Sim vs. prod
is decided per-order at runtime based on the exchange id embedded in the
order id:

```cpp
OB.cpp:248   mda::OrderID::exchid(id) == en::x::SIM
```

That check is the primary switch inside the main message handlers. A few
constructor parameters do shape behaviour globally:

| Parameter | File:line | Effect |
|-----------|-----------|--------|
| `do_cross_check` | `OB.cpp:48,64` | Enables the end-of-event anti-crossing validator (§5). |
| `delay` (µs, default 1000) | `OB.hpp:257` | Per-venue latency applied when a sim Order enters the book (§4). |
| `debug` | `OB.cpp:75` | Opens `ob.txt` and enables `debug_print_book_()`. |

Sim-specific code paths inside message handlers:

- `fill_sim_on_arrival()` (`OB.cpp:328`) — immediate aggressive fill for a
  newly-arriving sim order.
- `fill_stray_sim_orders()` (`OB.cpp:462`) — when a non-sim (market)
  order arrives, fills any resting sim order it crosses or meets.
- Sim-aware helpers in `OrderQ`: `get_sim_in_book()` vs
  `get_orders_in_book()` (`OrderQ.hpp:100,76`), `get_head_no_sim()`
  (`OrderQ.hpp:47`) — the BBBO is computed skipping sim orders so the
  market-maker doesn't chase its own orders.

No `#ifdef SIM` / `#ifdef PROD` blocks. One conditional flag worth noting:
`#ifdef ALERTLOCKEDBOOK` on `OB.cpp:2867` toggles a warning for locked
(bid == ask) books.

---

## 2. Order queue construction

### Data structure

Each price level owns an `OrderQ` (`OrderQ.hpp`). The queue is a doubly-
linked list of `Order*` with an index map for fast lookup:

```cpp
OrderQ.hpp:28   head, tail                              // list endpoints
OrderQ.hpp:33   std::flat_map<uint64_t, Order*> qordermap  // id → node
```

Insertion is at the **tail** — strict FIFO with respect to arrival
time. `append()` (`OrderQ.hpp:168`) sets the new order's `prev` to the
current tail; `remove_order()` (`OrderQ.hpp:218`) unlinks and erases
from `qordermap`.

Per-level counts are maintained separately for total orders vs. sim-only
orders so that public BBBO-style queries can skip sim volume:

- `get_orders_in_book()` — total orders at level (`OrderQ.hpp:76`)
- `get_sim_in_book()` — sim subset (`OrderQ.hpp:100`)
- `get_head_no_sim()` — first non-sim order at head (`OrderQ.hpp:47`)

### MBO actions

MBO messages arrive via the `data_handler` and dispatch by
`orderUpdateAction` / `md_event`:

| Action | File:line | What happens |
|--------|-----------|--------------|
| ADD | `OB.cpp:2524-2562 → add() OB.cpp:236` | Insert at tail of the level's `OrderQ` |
| MOD (CANC / EXEC) | `OB.cpp:2569-2587 → mod() OB.cpp:660-1012` | CANC decrements size / removes; EXEC is handled via `exec_slate` and fires fills when the matching CANC arrives |
| CANCEL | `OB.cpp:1689-1825` | `canc_notify()` (`OrderQ.hpp:298`) adjusts `size_of_book_cnt` and unlinks |
| CHANNEL_RESET | see `md_event == RST` paths | Clears the whole side's queues |

### Self-placed sim orders (from Lights via SOM)

Sim orders arrive the same way as exchange MBOs (through `data_handler`)
but are tagged by exchange id so they are not confused with real orders.
They are appended at the tail of their price level just like any other
order — **there is no re-ordering to a different queue position**.

What *is* different: on arrival the OB immediately checks whether the
sim order crosses the current book (aggressive) and fires a fill for the
crossable size (§3). Whatever does not immediately fill rests at the
tail.

---

## 3. Fill generation in sim

Fills are driven by three paths, all ultimately creating a
`frame::som::msg::Fill` via `OrderQ::fill_notify_s()`:

### 3.1 Aggressive fill on arrival

`fill_sim_on_arrival()` (`OB.cpp:328`) runs when a sim order lands:

- BUY sim with `px ≥ best_ask` → fill up to sim order's size from the
  best-ask queue (`OB.cpp:381-408`)
- SEL sim with `px ≤ best_bid` → symmetric
- The fill price is the resting counter-side's price, **not** the
  incoming order's price — standard taker pricing.

Partial fills work naturally: `fill()` (`OB.cpp:1015`) uses
`fillsz = std::min(order.sz, sz)`. Whatever remains stays resting and
can fill later as more liquidity crosses it.

### 3.2 Passive fill when real flow crosses

When a non-sim MBO arrives that touches levels with resting sim orders,
`fill_stray_sim_orders()` (`OB.cpp:462`) walks from the far side toward
the sim order, filling any sim order the market would have crossed on
its way in. This covers the case where our order was already resting
and real flow came and lifted it.

### 3.3 Exec-driven fill (via `exec_slate`)

CME MDP3 emits MOD+EXEC pairs when an order trades. The OB parks the
EXEC expectation on an `exec_slate` queue (`OB.cpp:1684-1687`) and pops
it when the corresponding CANC arrives. This keeps fills aligned with
the real exchange's trade report rather than inferring from price
crosses alone.

### 3.4 Queue-position fill inference — the key mechanism

The sim doesn't watch the book and guess "I'm 42 contracts deep at this
level, did 42 contracts trade through yet?" Instead it uses a much
stronger signal: **when a real exchange order that was queued behind
us fills, every sim order queued ahead of that real order would already
have filled at the exchange** (because the exchange matches strict
FIFO). That's the moment we materialise the sim fills.

Two pieces of code realise this:

**The gate** (`OB.cpp:943`):

```cpp
if (o == q->get_head_no_sim()) {
    fill_prev_sim_order();
    ...
}
```

- `o` is the real order that just executed (`ASSERT(!o->issim())` at `OB.cpp:852`).
- `q->get_head_no_sim()` returns the first **non-sim** order in the queue
  (`OrderQ.hpp:47`).
- The gate fires only when `o` is the earliest real order still resting
  — i.e. the only things that could be ahead of `o` in the queue are
  our own sim orders (or nothing). That's the proof of "no real flow
  has been skipped"; any sim orders ahead of `o` **must** have been in
  front of this real order on the actual exchange too.

**The walk-backwards** — `fill_prev_sim_order()` at `OB.cpp:854` plus
the recursion inside `fill()` at `OB.cpp:1044-1045`:

```cpp
// fill_prev_sim_order() — peels one step back
auto prev_order = static_cast<Order *>(o->prev);
if (prev_order && prev_order->issim())
    fill(q, prev_order, sz, px, txtim);     // sz = real EXEC size

// fill() — recurses through consecutive sim orders
if ((sz - fillsz > 0) && prev && prev->issim())
    fill(q, prev, sz - fillsz, px, tim);
```

So the algorithm is:

> Take the size `o` just executed. Walk backwards through consecutive
> sim orders at the same price level. Each sim order fills against that
> remaining size (full fill if small, partial if its size exceeds the
> remainder). Stop when the remaining size hits zero OR the next node
> backward is a real order (would have been matched earlier) or null.

### 3.5 Worked example

Queue at best-ask 6971.75 before any EXEC (head → tail):

```text
SIM_2  →  SIM_3  →  SIM_4  →  REAL_5  →  REAL_20
```

(`SIM_X` = sim order for X contracts owned by this process;
`REAL_X` = real exchange order from MBO.)

Because `get_head_no_sim() == REAL_5`, the only real order that the
FIFO-fill-forward logic cares about next is `REAL_5`.

Now a real trade arrives (MDP3 MOD+EXEC) against `REAL_5` for 5
contracts:

1. `o == REAL_5`, `sz == 5`. Gate passes (`o == q->get_head_no_sim()`).
2. `fill_prev_sim_order` looks at `o->prev == SIM_4`, sim → call
   `fill(SIM_4, sz=5)`.
3. `fill`: `SIM_4.sz(4) <= 5`, so `SIM_4` fills entirely →
   `Fill{ sz=4, still_to_be_filled=0 }` emitted. Remaining = 1.
4. Recurse: `prev == SIM_3`, sim → `fill(SIM_3, sz=1)`.
   `SIM_3.sz(3) > 1`, partial fill → `SIM_3` now has `sz=2`,
   `Fill{ sz=1, still_to_be_filled=2 }` emitted. No recursion (partial
   path doesn't recurse).
5. `REAL_5` itself is consumed by the MBO handler's normal path (not
   inside `do_exec`, outside the sim code).

Queue after (head → tail):

```text
SIM_2  →  SIM_3 (sz=2)  →  REAL_20
```

Note the outcome: we got a full fill on `SIM_4`, a partial of 1 on
`SIM_3`, and `SIM_2` didn't fill because the real execution's 5
contracts were already consumed. That's what you'd actually see on the
exchange: the real order behind them would have lifted sim volume in
FIFO order.

When the next real trade comes (say `REAL_20` EXECs for 20), the gate
re-evaluates `get_head_no_sim()`, which now returns `REAL_20` (since
`REAL_5` is gone). `fill_prev_sim_order` walks back: `SIM_3` partial,
then `SIM_2`, then stops at whatever precedes `SIM_2`.

### 3.6 What the mechanism does NOT do

- **No queue-position estimate at arrival.** When a sim order is
  appended to a level that already has 100 real contracts resting, we
  don't guess "100 more lots of real flow must trade through us first".
  We wait for an actual real EXEC behind our order.
- **Head-of-queue sim orders never fill from this path.** If a sim
  order is the very first order at a level and no real order ever sits
  behind it, `get_head_no_sim()` never returns anything from that
  level, so the gate never fires. In practice the order would already
  have been matched by `fill_sim_on_arrival()` if it was aggressive, or
  would get picked up by `fill_stray_sim_orders` in the legacy path
  (currently disabled — §3.7).
- **FIFO within the sim-ahead chain is walked tail→head by
  recursion**, not head→tail. The end state is identical to head→tail
  processing (same sim orders, same fill sizes) because `fill` allocates
  min(sim.sz, remaining) at each step, just in reverse traversal
  order.
- **No queue-position aging.** There is no notion of "my order has
  been sitting here for X minutes, so queue-position decay / slippage
  should adjust my estimate". Fills are strictly tied to actual EXEC
  events in the feed.

### 3.7 Disabled / dead paths worth knowing

`fill_stray_sim_orders_buy` and `fill_stray_sim_orders_sel`
(`OB.cpp:882-936`) are guarded by `#ifdef DONOTODOTHISIFDIFFERENTPRICE`
(typo preserved from source). They are **not** compiled in. They would
have walked sim orders at **neighbouring** price levels when a trade
happened — i.e. if a trade goes through at 6971.50 but you have a sim
order at 6971.75 (better price from the taker's perspective), fill the
stray. With this disabled, sim orders at prices strictly better than
the trade price do **not** get swept — only the exact-price queue is
worked via the FIFO inference above.

This is a conservative choice (you only get filled when the exact level
you rested at trades), but it under-fills aggressive resting orders
that a real exchange would have matched first.

### Fill message construction

`OrderQ::fill_notify_s()` (`OQ.hpp:375`) emits a `frame::som::msg::Fill`
to the order's owner actor. Fields: `id`, `sym`, `side`, `sz` (filled
size this time), `px` (price as integer ticks), `still_to_be_filled`
(remaining after this fill — 0 on full fill), `tim` (event ns). The
owner is the Light that placed the order; SOM's `fill_handler`
(`frame_kaspr/src/SOM.cpp:932`) then broadcasts to all `FillSub`
subscribers (including TradeScheduler).

---

## 4. Order-arrival delay / latency simulation

The OB has **one** latency knob: the `delay` member (`OB.hpp:257`,
default 1000 µs). On every sim-order insertion:

```cpp
OB.cpp:1991   order_engine_arrive_time = order_leave_time + std::max(40, delay) * 1000
OB.cpp:2012   if (order_engine_arrive_time < current MBO time) drop
```

Semantics:

- `delay` models the **one-way latency from Light → exchange matcher**.
  The order lands at `ts + delay` in sim-time.
- If the MBO stream has already advanced past that arrival instant, the
  order is **dropped** (assumes the state of the book at the arrival
  instant is not reconstructable from the forward stream).
- There is **no queue-position estimation** — if the order "arrives"
  before the cursor, it is appended to the tail of the live queue with
  no notion of how many contracts landed between `ts` and `ts + delay`.
- No separate `ack` latency, no round-trip latency on cancels, no
  variable delay / jitter.

Other latency-looking fields:

- `latency` (`OB.hpp:63`, filled at `OB.cpp:1351`) records
  `now - MBO.sendingTime` purely for diagnostics.
- `time_warp` machinery exists on BFA but is not consumed by OB.

### Known gaps (arrival-delay side)

1. No distribution (single deterministic µs value).
2. No account for queue accumulation during the delay window.
3. Dropping orders whose target instant is past the cursor is
   convenient but is a silent bias — worth a counter.
4. No separate cancel latency; cancels are effectively instant.

---

## 5. Cross-check / invariants

`cross_check()` (`OB.cpp:2791-2916`) runs at end-of-event when
`do_cross_check` is on:

- Detects inverted market `best_bid > best_ask` (`OB.cpp:2815`) →
  log-errors, calls `canc_notify_all()` (`OB.cpp:2809`) to wipe the
  stale book, and records the event on `crossed_data` history.
- Warns on locked market `best_bid == best_ask` (`OB.cpp:2867`, only
  if `ALERTLOCKEDBOOK` is defined).
- Clears the crossed-history counter when the book recovers
  (`OB.cpp:2914`).

No retransmit / recovery attempt inside OB — recovery of book state
after a detected inversion is upstream concern (MDP3 snapshot feed).

---

## 6. Statistics / diagnostics

Counters and buffers used for logging / audit:

| Field | File:line | Purpose |
|-------|-----------|---------|
| `num_exec`, `num_add` | `OB.hpp:54-56` | Printed on shutdown (`OB.cpp:2787-2788`) |
| `tx_px_3`, `tx_sz_5`, … | `OB.hpp:272-294` | Circular buffers of recent trade px / sz |
| `latency` | `OB.hpp:63` | Rolling MBO-ingest latency samples |
| `recent_dat`, `recent_tx` | `OB.hpp:266-267` | Recent data events / trades |

Output paths:

- `ob.txt` — full book dump, opened if `debug=true` (`OB.cpp:75`)
- `debug_print_book_()` (`OB.cpp:2291-2336`) — prints levels + BBBO to
  the `OBFILE` stream (stderr)
- Stats block on shutdown (`OB.cpp:2273-2287`) — toggled by the `Set`
  handler

---

## 7. TODOs / hazards found in the source

Direct citations from the code as it stands today:

| File:line | Comment |
|-----------|---------|
| `OrderQ.hpp:198` | `TODO: should not allow double insertions` — duplicate-id inserts are detected but not prevented. |
| `OB.cpp:2077` | `TODO: change these places to arrays of double` — float / int precision risk on price maths. |
| `OB.cpp:884, 914` | `#ifdef DONOTODOTHISIFDIFFERENTPRICE` guards around stray-fill at a price different from match px — currently disabled. |
| `OB.cpp:1684-1685` | Comment questioning `exec_slate` semantics: "not sure if queue is necessary or 1:1 exec-to-canc correspondence". |
| `Order.cpp:116` | `ASSERT(fillsz == _sz)` — flagged as needing relaxation for non-CME venues. |

None of these are blocking for the CME-ES sim path but all would bite a
port to a different venue or a more adversarial backtest.

---

## 8. Operational cheat sheet

- Want to change Light → exchange latency: override `delay` on the OB
  constructor (`OB.hpp:257`). Default 1000 µs.
- Want the BBBO to reflect real market only (not our orders):
  `OrderQ::get_head_no_sim()` is already being used — look for
  `get_best_bid_px_no_sim()` / equivalent at the consumer.
- Want to detect when the sim drops a late-arriving sim order: no
  counter today; hook one at `OB.cpp:2012`.
- Want fills that ignore FIFO (optimistic / worst-case fills): the
  `fill_prev_sim_order()` logic at `OB.cpp:854` is the hook. A
  `--pessimistic-fill` mode that forced us to wait for real flow past
  our qty-ahead would live here.
