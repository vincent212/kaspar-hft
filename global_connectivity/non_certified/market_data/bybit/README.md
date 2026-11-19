# Bybit V5 public market data, spot and linear perpetuals (not certified)

`BybitSync` turns Bybit's public order book and trade streams for one symbol into
Kaspar records: the book goes through `sim::LevelWindow` and `sim::Mbp2Mbo` to OB as
synthetic orders, trades as prints. `BybitFeed` is the actor that owns the WebSocket.
The default topic is `orderbook.full.{symbol}` (the whole book, deltas every 200 ms,
started from `GET /v5/market/full_orderbook`); depth topics
(`orderbook.{1,50,200,1000}`) and Bybit's free order-book archive are also read.
Venue codes: `en::x::BYBIT` (spot), `en::x::BYBITF` (linear).

**Not certified.** It has been checked against Bybit's live public stream and archive
as below, never in production use.

## What was checked, with numbers

### 1. Unit tests: 22 of 22 pass

Documentation examples (full-book delta, public trade), synthetic sequences for every
alignment and recovery rule (snapshot alignment on `u`, stale snapshot, `seq`
mismatch at equal `u`, buffer restart, gap, duplicate, `u = 1`, empty reset ->
clear, reconnect -> GAP, absolute sizes, taker side, RPI and block prints), frames
recorded from the live stream on 2026-11-18 (`tests/data`: spot SOLBTC full book
with 4 REST snapshots, every one matched exactly; linear BTCUSDT `orderbook.50`,
290 frames, replayed and with a gap injected), the chain through a real OB on both
recordings, and the feed config.

### 2. Live public data, no account (2026-11-18)

Two runs per product on BTCUSDT, `orderbook.full` plus `publicTrade`, a REST full
book every 10 s compared with the sync's book at exactly the snapshot's `u` (deltas
after it undone from an undo log) over every level in the price range both books
cover, and the chain sync -> LevelWindow (1000 levels) -> Mbp2Mbo -> OB checked after
each comparison (OB's 1000 levels per side and BBO against the sync's book).

| | linear run 1 | linear run 2 | spot run 1 | spot run 2 |
|---|---|---|---|---|
| minutes | 32 | 31 | 32 | 31 |
| frames | 27,116 | 16,977 | 19,122 | 12,760 |
| book deltas applied | 9,591 | 9,292 | 9,590 | 9,292 |
| trades applied / print only (RPI, block) | 47,043 / 0 | 17,604 / 0 | 19,970 / 2 | 7,998 / 13 |
| REST comparisons, exact over the full common range | 190 / 190 | 184 / 184 | 190 / 190 | 184 / 184 |
| levels compared per comparison | about 59,700 | about 59,900 | about 20,000 | about 20,000 |
| comparisons exact over the best 200 levels per side | 190 / 190 | 184 / 184 | 190 / 190 | 184 / 184 |
| OB checks passing (levels checked) | 190 / 190 (380,000) | 184 / 184 (368,000) | 190 / 190 (380,000) | 184 / 184 (368,000) |
| gaps / resyncs / `u = 1` / snapshots refused | 0 / 0 / 0 / 0 | 0 / 0 / 0 / 0 | 0 / 0 / 0 / 0 | 0 / 0 / 0 / 0 |
| crossed books | 0 | 0 | 0 | 0 |
| WebSocket reconnects | 0 | 0 | 0 | 0 |
| REST errors (connect timeout, retried) | 1 | 1 | 1 | 1 |
| stream ahead of the snapshot, updates (mean / max) | 5.0 / 11 | 5.0 / 12 | 3.8 / 9 | 3.8 / 8 |
| largest book, bid / ask levels | 20,062 / 40,243 | 19,436 / 40,819 | 10,303 / 10,318 | 10,266 / 10,250 |

Each run synchronised once, from its first REST snapshot. REST use: one request per
10 s per run, against Bybit's 600 per 5 s IP limit. Run 2 used the code as it is
now except the reconnect GAP record (added after; reconnects did not occur).

Not seen live, so checked only by unit tests: gaps, `u = 1`, resynchronisation after
a reconnect, refused snapshots, empty-book resets.

### 3. Archive replay (2026-11-17, whole day)

`bybit_replay` on `quote-saver.bycsi.com` `ob200` files (the recorded
`orderbook.200` stream, 100 ms) merged with the day's `public.bybit.com` trade file,
through BybitSync (DEPTH mode) -> Mbp2Mbo -> OB, OB checked every 1000 book events.

| | linear BTCUSDT | spot BTCUSDT |
|---|---|---|
| hours covered | 24.0 | 24.0 |
| book messages / deltas | 863,299 / 863,296 | 841,593 / 841,591 |
| snapshots (mid-file; with `u = 1`) | 3 (2; 1) | 2 (1; 0) |
| gaps / `seq` regressions / crossed books | 0 / 0 / 0 | 0 / 0 / 0 |
| trades fed / applied / RPI print only / before first snapshot | 2,287,314 / 2,287,281 / 0 / 33 | 1,056,951 / 1,056,168 / 759 / 24 |
| OB checks (levels) / bad | 863 (345,200) / 0 | 841 (336,400) / 0 |

### 4. Not checked

- The lights and SOM downstream of OB on live data.
- `orderbook.1`, `orderbook.200` and `orderbook.1000` live (DEPTH mode was checked
  live only through the recorded `orderbook.50` frames and on the archive).
- Symbols other than BTCUSDT live (SOLBTC spot only in the recorded test data).
- Runs longer than 32 minutes; Bybit's forced disconnects.

## Contents

| Path | What |
|---|---|
| `feed.md` | Feed description: documents, transport, messages, sync rule, semantics, mapping, pitfalls, ambiguities |
| `review.md` | Decisions for the owner |
| `include/bybit_md/BybitSync.hpp`, `src/BybitSync.cpp` | The sync class (no network) |
| `include/bybit_md/act/BybitFeed.hpp`, `src/BybitFeed.cpp` | The actor |
| `include/bybit_md/BybitNet.hpp` | WebSocket (the application ping goes out from a timer while a read waits, `read_with_ping`, shared with `order_entry/bybit`'s private stream), REST GET over `crypto_common::https_request` (`../crypto_common`), raw-frame recorder |
| `tools/bybit_record.cpp` | Records raw frames and REST books |
| `tools/bybit_verify.cpp` | Live check (ladder step 2) |
| `tools/bybit_replay.cpp` | Archive replay |
| `tools/ChainOB.hpp` | Sync -> Mbp2Mbo -> real OB in one thread, and its check |
| `tests/` | gtest and recorded frames |

## Running

```bash
export KSPRPROJ=~/kaspar-hft
make test KSPRLIB=/path/to/lib ACTORS_LIB=/path/to/libactors.a
make tools
./build/bybit_verify --category linear --symbol BTCUSDT --tick 0.1 --unit 0.001 \
    --chunk 0.1 --window 1000 --minutes 31 --ob
./build/bybit_verify --category spot --symbol BTCUSDT --tick 0.1 --unit 0.000001 \
    --chunk 0.01 --window 1000 --minutes 31 --ob
./build/bybit_record --category spot --symbol SOLBTC --topic full --seconds 20 --out f.rec
./build/bybit_replay --book 2026-11-17_BTCUSDT_ob200.data.zip --trades BTCUSDT_2026-11-17.csv.gz \
    --category spot --tick 0.1 --unit 0.000001 --chunk 0.01 --ob-every 1000
```

`libframe` is compiled from this checkout into `build/kl/` because a `libframe`
built before the BYBIT and BYBITF codes rejects them; `LOCAL_FRAME=0` links the
installed one. No account and no keys are needed.
