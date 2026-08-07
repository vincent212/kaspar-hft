# Matching engine (example)

A single-actor, per-symbol **spot limit-order-book matching engine** with **price-time (FIFO)
priority** and first-class partial fills — ships as
[`examples/matching_engine.rs`](examples/matching_engine.rs).

It demonstrates the intended shape of an exchange matching engine on `actors2`:

- **Core** (`OrderBook`) — a plain, framework-agnostic, **deterministic** struct: `place` /
  `cancel` / `replace` mutate an in-memory book and **return the resulting events**. This is
  where correctness lives (see the tests).
- **Actor** (`MatchingEngine`) — a thin adapter that owns the book (its mailbox makes it the
  deterministic **single writer** for its symbol) and routes each event to the right `ActorRef`.

## Run it

```sh
cargo test  --example matching_engine                    # 15 tests
cargo run   --example matching_engine                    # demo: build a book, cross an order, cancel, replace
cargo run --release --example matching_engine -- bench   # throughput benchmark
```

In-memory only; no server/container. To run as a standalone binary, copy the single file into a
`cargo new` project's `src/main.rs` with `actors2` as a dependency.

## Messages

**Inbound (order entry)** — modelled on FIX/iLink; integer prices/quantities:

| Message | Fields | Meaning |
|---|---|---|
| `PlaceOrder` | `order_id, side, price, qty` | new limit order: match what crosses, rest the remainder |
| `CancelOrder` | `order_id` | remove a resting order |
| `ReplaceOrder` | `order_id, new_price, new_qty` | cancel-replace (re-queues at tail → loses time priority) |

**Outbound (execution reports)** — `Ack`, `FillMsg` (emitted to **both** sides of a trade, at the
maker's price), `Canceled`, `Replaced`, `Rejected`, `BboUpdate`. On the async path these are
**pooled** (`MsgBox::pooled`); inbound orders arrive via `fast_send` (on the stack). Rejects (bad
price/qty, unknown/duplicate id) never panic — they emit `Rejected` and leave the book unchanged.

## Data structure (O(1) hot ops)

Carried from the C++ `OB`/`OrderQ` design:

- price → **level** in a `BTreeMap` (best bid = max key, best ask = min key; O(log L));
- each level is an **intrusive doubly-linked FIFO list** over a shared **slab** of order nodes;
- a global `order_id → node` index.

So **rest, match-at-head, cancel, and replace are all O(1)** (no linear scan, no mid-array
shift), and best-of-book aggregate size is O(1) via a per-level running total. Integer
prices/qty give exact partial-fill accounting.

## Matching algorithm (price-time FIFO)

Price priority (best price first) then time priority (earliest at a level). A marketable order
sweeps opposite levels generating fills **at the maker's price** (taker price improvement),
resting any non-marketable remainder. Cancel unlinks in O(1). Cancel-replace = remove old (emit
`Replaced`) + place new (re-queues at the tail, may immediately match).

## Tests (15)

Run `cargo test --example matching_engine`:

- **Core** — price-time priority, partial fill, price improvement, cancel, cancel-replace loses
  priority, reject cases.
- **Book integrity** — a 20k-op random `place`/`cancel`/`replace` fuzz that asserts invariants
  after **every** op (in particular: the book is **never left crossed**; index ↔ node ↔ level
  totals stay consistent), plus an aggressive multi-level sweep.
- **Actor-level** — drive the `MatchingEngine` actor end-to-end and assert the exact
  execution-report sequence.

## Performance (measured, release, single core, unpinned dev box)

| Workload | ns/op | throughput |
|---|---|---|
| insert (rest, no cross), book at 2M live orders | ~165 ns | ~6.1 M/s |
| match (each order fully crosses → a trade) | ~115 ns | ~8.7 M/s |
| cancel from a 1M-deep single level (reverse order) | ~115 ns | ~8.7 M/s |

A single core comfortably clears a 3M msg/s target; throughput scales by running one engine
actor per symbol across cores. (Matching only — excludes network / serialization at the edges.)

## Design origin & further reading

Data-structure design carried (design, not code) from the C++ `OB` actor in this repo:
`../../frame_kaspr/include/frame/ob/act/OB.hpp`, `.../ob/OrderQ.hpp`, `.../ob/Order.hpp`,
`../../frame_kaspr/src/OB.cpp`. That `OB` is a feed-driven book *reconstructor*; here we reuse its
structures and drive them from **order entry** with explicit crossing.

- The Actor Model for Low-Latency Software —
  <https://vincentmayeski.substack.com/p/the-actor-model-for-low-latency-software>
