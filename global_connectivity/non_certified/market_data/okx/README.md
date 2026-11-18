# OKX V5 public market data (spot and perpetual swaps)

**Not certified.** Built and tested by an agent with the `build-feed-handler`
skill on 2026-11-18. The checks below were run, with the numbers given; nothing
else has been checked.

## What it is

| File | What it does |
|---|---|
| `feed.md` | Feed description: transport, messages, sequencing, semantics, mapping to Kaspar, pitfalls, ambiguities |
| `review.md` | Decisions for the owner |
| `include/okx_md/OkxSync.hpp`, `src/OkxSync.cpp` | One instrument's `books` (400 levels) and `trades` into `sim::LevelWindow` and `sim::Mbp2Mbo`; `seqId`/`prevSeqId` gap rule; checksum field check; no network |
| `include/okx_md/OkxWs.hpp`, `src/OkxWs.cpp` | TLS WebSocket client with OKX's text ping/pong keep-alive, and one-shot HTTPS; shared with order entry and post-trade |
| `include/okx_md/act/OkxFeed.hpp`, `src/OkxFeed.cpp` | The feed actor: connection, subscription, reconnect on a gap or error, records to OB (or a monitor print with no OB), optional raw-frame recording |
| `tools/okx_record.cpp` | Raw-frame recorder: WebSocket frames as received, with REST snapshots every 10 s interleaved |
| `tools/okx_verify.cpp` | Live or replayed verification (below) |
| `tools/okx_feed_monitor.cpp` | Runs the `OkxFeed` actor itself under a Manager and prints the top of book |
| `config/okx_example.ini` | Instruments for `OkxFeed` and the monitor |
| `tests/test_okx_sync.cpp`, `tests/test_okx_ob.cpp`, `tests/data/` | Unit tests; a 6-second recording of both instruments with 6 REST snapshots |

Venue codes: `en::x::OKX` (spot), `en::x::OKXF` (swaps).

## Build and run

Not part of the top-level build. On macOS with the libraries installed in
`~/kaspar-hft`:

```bash
export KSPRPROJ=~/kh-cw
make test  KSPRLIB=~/kaspar-hft/lib ACTORS_LIB=~/kaspar-hft/actors/cpp/libactors.a
make tools KSPRLIB=~/kaspar-hft/lib ACTORS_LIB=~/kaspar-hft/actors/cpp/libactors.a

build/okx_verify --inst BTC-USDT --minutes 30 --ob --record build/rec/spot.rec
build/okx_verify --inst BTC-USDT --replay build/rec/spot.rec --ob
build/okx_record --inst BTC-USDT --inst BTC-USDT-SWAP --out build/rec/x.rec --seconds 60
build/okx_feed_monitor --seconds 60
```

The Makefile compiles libframe from this tree into `build/` (review.md item 4).
No account or key is used.

## Testing ladder

### Step 1: unit tests (16 tests, all pass)

- Checksum: the 2025 documentation's string format (two examples), CRC32 equal to
  zlib's (the CRC-32 check value and the documentation's example string, as a
  signed integer), top 25 levels only, strings as received; a non-zero checksum is
  verified and a wrong one is a gap.
- Books: snapshot then the documentation's update rules (absolute size, size 0
  deletes, insert in price order); the documentation's five-step sequence example
  (snapshot, update, heartbeat, reset, update); a gap stops the book until a new
  snapshot; other instruments, channels and events ignored.
- Trades: taker side; a trade ahead of the book executes from the front; a trade
  already in the book keeps the level size; `tradeId` continuity with `count`.
- Recorded frames: spot and swap replay with 0 gaps, 0 crossed books and 400
  levels per side; the book equals the REST snapshot at the same `seqId` over
  all 400 levels per side (the tests require at least 1 of the 3 spot snapshots
  and 3 of the 3 swap snapshots to have a `seqId` the stream also has); an
  injected gap and a duplicate are detected.
- Full chain on the recording: OkxSync -> Mbp2Mbo -> a real OB; after every book
  message OB's size at each of the best 20 levels and its best bid and ask equal
  the sync class's book (spot and swap).

### Step 2: live public data, no account

`okx_verify --ob` on `wss://ws.okx.com/ws/v5/public`, `books` (400 levels) and
`trades`, 1918 s (32 minutes) per instrument, run on 2026-11-18 and replayed
through the final build with identical results:

| | BTC-USDT | BTC-USDT-SWAP |
|---|---|---|
| book messages (1 snapshot + updates) | 18 537 | 19 090 |
| gaps (`prevSeqId` != previous `seqId`), resyncs, reconnects | 0, 0, 0 | 0, 0, 0 |
| heartbeats, sequence resets | 0, 0 | 0, 0 |
| checksum field: zero / non-zero / mismatch | 18 537 / 0 / 0 | 19 090 / 0 / 0 |
| crossed books after an update | 0 | 0 |
| maximum levels per side (bids/asks) | 400/400 | 400/400 |
| trade pushes, `tradeId` breaks | 7 296, 0 | 36 007, 0 |
| REST snapshots (every 10 s), errors | 192, 0 | 192, 0 |
| REST `seqId` also in the stream: compared over 400 levels per side | 98 snapshots, 78 400 levels, 0 mismatches | 91 snapshots, 72 800 levels, 0 mismatches |
| REST `seqId` not in the stream: top 5 levels equal to the stream's book just before or just after | 61 of 94 | 33 of 101 |
| OB checks (once a second): levels compared, mismatches | 1 806: 180 600, 0 | 1 811: 181 100, 0 |
| OB best bid/ask mismatches | 0 | 0 |

**The CRC32 check did not run on any live message**: OKX has sent 0 in the
checksum field since 2026-06-23 (`feed.md` section 5). A probe of the spread
channel `sprd-books-l2-tbt` also gave 0 in 12 messages. Integrity rests on
`seqId` continuity and the REST comparison at equal `seqId`.

The REST snapshots whose `seqId` is not in the stream (REST is cached every
50 ms, the stream batches 100 ms) are compared only on the top 5 levels against
the neighbouring stream books; the shares above are what those comparisons give,
and a lower share means the book moved between the two, not that it is wrong.

The `OkxFeed` actor itself (connection loop, keep-alive, monitor mode) ran under
a Manager for 600 s on both instruments: see "Feed actor run" below.

Not run: the chain beyond OB (lights and SOM, as `bnsim` does for Binance).
`sim/BnSimKaspr` has no OKX feed, and this run was limited to the OKX
directories.

### Feed actor run

`okx_feed_monitor --seconds 600`, both instruments, one connection each, no OB
(monitor mode), 2026-11-18:

| | BTC-USDT | BTC-USDT-SWAP |
|---|---|---|
| connections | 1 | 1 |
| book messages (1 snapshot + updates) | 5 108 | 5 877 |
| gaps, crossed books | 0, 0 | 0, 0 |
| checksum field zero / mismatch | 5 108 / 0 | 5 877 / 0 |
| trades | 964 | 4 627 |

This shows the actor's connection loop and keep-alive held for 10 minutes; it
does not exercise its reconnect path (no disconnect occurred).

### Steps 3 and 4

Not applicable to public market data. Production order entry is never run by the
agent.

## Not checked

- `books-l2-tbt` and `books50-l2-tbt` (VIP4+), the SBE stream, `books5`, `bbo-tbt`.
- A real gap, reset, heartbeat or notice 64008 from OKX: none occurred live;
  those paths are tested only with the documentation's examples and injected
  data.
- Reconnect of `OkxFeed` after a dropped connection (none occurred live).
- Instruments other than BTC-USDT and BTC-USDT-SWAP; dated futures and options.
- The demo environment's public books.
