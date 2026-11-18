# Kraken spot public market data (WebSocket v2 `book`, `trade`, `level3`)

**Not certified.** What was and was not checked is listed below with its numbers.

## What it is

| Part | File | Role |
|---|---|---|
| `KrakenBookSync` | `include/kraken_md/KrakenBookSync.hpp` | `book` + `trade` for one symbol, no network: snapshot, absolute updates, truncation to the subscribed depth, CRC32 on every message, `trade_id` continuity; levels into `sim::LevelWindow` and `sim::Mbp2Mbo` |
| `KrakenL3Sync` | `include/kraken_md/KrakenL3Sync.hpp` | `level3` (order by order), no network: queue order, Kraken's amend priority rule, truncation, queue-order CRC32; MBO records directly. **Not checked live** (needs an API key) |
| `KrakenFeed` | `include/kraken_md/act/KrakenFeed.hpp` | the actor: `wss://ws.kraken.com/v2`, reconnect on checksum mismatch or error, records to OB |
| `FrameRecorder` / `FrameReader` | `include/kraken_md/FrameRecorder.hpp` | raw frames to a file (`<recv ns> <frame>` per line) and back |
| `kraken_verify` | `tools/kraken_verify.cpp` | live or replayed check: checksum, REST comparison, OB chain |
| `kraken_feed` | `tools/kraken_feed.cpp` | runs `KrakenFeed` under a Manager (monitor mode), optional recording |

`feed.md` is the feed description; `review.md` lists the decisions for the owner.

## Build and run

```bash
export KSPRPROJ=~/kh-cw
make CXX=clang++ KSPRLIB=/Users/vm/kaspar-hft/lib \
     ACTORS_LIB=/Users/vm/kaspar-hft/actors/cpp/libactors.a test    # unit tests
make ... tools
build/kraken_verify --minutes 32 --ob --record FILE --report FILE.json   # live, no account
build/kraken_verify --replay FILE --ob                                   # a recording
build/kraken_feed --seconds 300 --record FILE                            # the actor
```

No account or key is needed for `book` and `trade`.

## Testing ladder

### Step 1: unit tests (25 tests, all pass)

| Suite | Tests | What |
|---|---|---|
| `KrakenJson` | 6 | decimal text kept through parsing; exact fixed point; checksum tokens and CRC32 strings from the book checksum guide; RFC 3339 |
| `KrakenBookSync` | 9 | the guide's snapshot gives the documented checksum 3310070434; a mismatch is reported; updates in order, zero removes; truncation to depth; other symbols and method responses ignored; malformed level is an error; trade side is the taker's and `trade_id` gaps are counted; the snapshot reaches Mbp2Mbo as recovery records; 2 000 recorded live frames (depth 1000, with trades): every checksum matches, 1000 levels per side, no crossed book |
| `KrakenBookOB` | 1 | the recorded frames through Mbp2Mbo into a real OB: OB's levels equal Mbp2Mbo's |
| `KrakenL3Sync` (`Fixture`) | 9 | the level3 checksum guide's snapshot gives the documented 1063832831; the same with two orders of one level swapped fails (queue order is checked); add joins the back; modify down keeps the place, up or a price change goes to the back; delete and unknown orders; truncation deletes orders pushed out of depth; a trade before the book change becomes an execution; a new snapshot replaces the book and ids; transactTime strictly increases |

The level3 API reference's own snapshot example is abridged (3 orders per side)
and its checksum cannot be reproduced from it; the guide's full example is used.

### Step 2: live public data, no account (BTC/USD, depth 1000)

`kraken_verify --minutes 32 --ob`, 2026-11-18 23:13:45 to 23:45:45 CET, 1 925 s:

| Check | Result |
|---|---|
| frames / book messages | 361 971 / 358 839 (1 snapshot, 358 838 updates; about 186 per second) |
| CRC32 checksum | **358 839 of 358 839 match, 0 mismatches**, 0 missing |
| crossed book after a message | 0 |
| resubscriptions | 0 (no mismatch, error or dropped connection) |
| levels applied / truncated below depth 1000 | 487 111 / 126 153 |
| trades / `trade_id` gaps | 1 942 / 0 |
| heartbeats | 1 918 |

REST comparison: `GET /0/public/Depth?pair=XBTUSD&count=100` every 10 s, in a
second thread (6 requests a minute; Kraken's public REST limit is far above
that). REST Depth has no sequence number, gives quantities with 3 decimals
rounded up (the stream has 8), and is served from a cache behind the stream, so
each snapshot's top 25 levels per side (50 levels) are compared with the stream's
books of the 15 s before the response, by price and by quantity rounded up to the
REST precision:

| Comparison | Result |
|---|---|
| REST snapshots | 191 ok, 1 failed (TCP connect timeout) |
| snapshot equal to **some** stream book in the 15 s before it, all 50 levels | **191 of 191** |
| how old that stream book was (REST response time minus stream time) | median 1 447 ms, p90 3 258 ms, max 4 623 ms, min −158 ms |
| equal to a stream book within ±1 s of the request | 66 of 191 |
| equal to the stream book nearest the request time (within 108 ms) | 9 of 191 |
| levels of the nearest book matching by price / by quantity | 7 330 / 6 364 of 9 550 |

So no REST snapshot disagreed with the stream: each is a past state of the
stream's book, on average 1.4 s old.

OB chain: in the same process, every record went through `Mbp2Mbo` (tick 0.1,
chunk 0.01 BTC, 1000 levels) into a real `frame::ob::act::OB` (venue KRAKEN), and
every 10 s the OB was queried:

| Check | Result |
|---|---|
| records sent to OB | 25 734 757 |
| checks / levels compared (best 20 per side) | 191 / 7 640 |
| OB size at the level ≠ Mbp2Mbo's chunks | 0 |
| Mbp2Mbo's chunks ≠ `ceil(qty / chunk)` of the sync's book | 0 |
| OB best bid/ask ≠ Mbp2Mbo's | 0 of 191 |

The run was recorded (59 MB of frames); replaying the recording with
`--replay --ob` reproduces the same counts (358 839 checksums, 0 mismatches,
25 734 757 records, 0 OB mismatches in 192 checks).

`KrakenFeed` actor (`kraken_feed --seconds 300 --record`), at the same time on a
second connection: 59 244 book messages, 59 244 checksums ok, 0 bad, 357 trades,
0 `trade_id` gaps, 0 reconnects, 4.1 million records; 59 753 frames recorded.

Not part of step 2: the lights and the SOM. The chain checked is sync → LevelWindow
→ Mbp2Mbo → OB, driven synchronously in `kraken_verify`; no `bnsim`-style run with
lights and SOM on Kraken data was made.

**level3 was not checked live.** It needs an API token (`ws-l3.kraken.com`), and
no key was supplied. `KrakenL3Sync` was built from the documentation and tested on
the checksum guide's example and on synthetic sequences only: its handling of real
`level3` traffic, its CRC32 against Kraken's, and the trade-to-book-change matching
(`review.md` item 2) are unverified.

### Steps 3 and 4

Order entry and fills: `../../order_entry/kraken/README.md` and
`../../post_trade/kraken/README.md`. Production: never.

## Not checked

- `level3` on live data (above).
- Symbols other than BTC/USD; depths other than 1000.
- Behaviour across a Kraken maintenance window or a forced disconnect (none
  occurred in 32 minutes; reconnect is exercised only by the code path).
- Trading status (`instrument` channel): not implemented (`review.md` item 3).
