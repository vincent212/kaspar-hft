# Deribit market data handler (not certified)

Two Deribit market-data paths into Kaspar's OB. **Not certified.** What was and
was not checked is listed below, with numbers. The feed description is
`feed.md`; the decisions waiting for review are in `review.md`.

- **Starbase** (order-by-order SBE multicast, new matching engine):
  `StarbaseBook`, decoder generated from Deribit's SBE schema. Starbase is
  reachable only from LD4 or AWS PrivateLink, so it was checked on Deribit's
  sample PCAP, not live.
- **JSON-RPC book** (public WebSocket, price levels): `DeribitBookSync` and the
  `DeribitFeed` actor, through `sim::LevelWindow` and `sim::Mbp2Mbo` to OB.
  Checked live for 30 minutes on BTC-PERPETUAL, without an account.

## Files

| File | Contents |
|---|---|
| `include/deribit_md/Starbase.hpp`, `src/Starbase.cpp` | packet framing (hand-written: `PacketHeader` is not generated) and message decoding with the generated codecs |
| `include/deribit_md/StarbaseBook.hpp`, `src/StarbaseBook.cpp` | per-channel sequencing, snapshot synchronisation, order-by-order books, invariant counters, Kaspar L3 records |
| `include/deribit_md/DeribitBookSync.hpp`, `src/DeribitBookSync.cpp` | the JSON book and trades for one instrument (no network) |
| `include/deribit_md/act/DeribitFeed.hpp`, `src/DeribitFeed.cpp` | the WebSocket actor (own thread, reconnect, heartbeat, optional recording) |
| `include/deribit_md/DeribitWs.hpp` | JSON-RPC requests and the heartbeat answer |
| `include/deribit_md/FrameRecorder.hpp` | raw-frame recorder (receive time, length, frame) |
| `tools/starbase_pcap.cpp` | PCAP decoder and book checker; `--dump` for the SDK comparison, `--ob` for the chain into OB |
| `tools/sdk_dump.py` | the same dump from Deribit's own Python SDK |
| `tools/deribit_live.cpp` | the live check and the replay verifier (`--replay`) |
| `tests/*.cpp`, `tests/data/btc_perpetual_100ms.frames` | 37 tests; 246 frames recorded from the live public stream |

## Build and run

```bash
export KSPRPROJ=~/kaspar-hft
make docs          # schema, sample PCAP (5.3 MB) into build/docs (not committed)
make test          # generate the SBE codecs, build, 37 tests
make pcap-check    # the sample PCAP through StarbaseBook and OB
make sdk-check     # the same PCAP through Deribit's Python SDK; dumps must be equal
make live          # 30 minutes of the public BTC-PERPETUAL book (no account)
# on this Mac, with the libraries of the main checkout:
make KSPRLIB=~/kaspar-hft/lib ACTORS_LIB=~/kaspar-hft/actors/cpp/libactors.a test tools
```

The SBE codecs are generated into `build/gen` by the real-logic SBE tool
(`sbe-all` 1.30.0, the jar `genschema/` uses), from
`deribit-sbe-market-data-api.xml` (schema id 2102, version 2). `genschema.py`
itself was not used: it fetches CME templates and post-processes CME
specifics; this Makefile runs the same SBE tool directly on Deribit's XML.

`DeribitFeed` configuration (one INI section per instrument): `interval`
(`100ms`; `raw` needs an authenticated connection), `host`, `depth` (50),
`tick` (0.5 for BTC-PERPETUAL), `chunk` (USD per synthetic order), `sec_id`,
`record` (path for raw frames).

## Testing ladder

### Step 1: documentation examples and recorded frames: 37 tests, all pass

- Starbase decoding (7): a Buy Put laid out byte by byte from the
  documentation; trades and flags; InstrumentDefinition with repeating groups;
  malformed packets rejected; heartbeat; unknown template skipped by length;
  first packet of the sample capture.
- StarbaseBook (17): snapshot alignment with buffered incrementals; recovery
  adds in `sortOrderId` order; Put with a new `sortOrderId` as delete then add;
  Put with the same one as a change; reduce and delete; execution placed before
  the maker's book change (both orders of arrival); inconsistent fill counted;
  implied fill as a print only; gap then resync from a later snapshot; gap
  revealed by a heartbeat; duplicates dropped; later snapshot compared with the
  rebuilt book; invariants counted; status mapping; end-of-packet records;
  unrouted instruments emit nothing.
- JSON sync (9): the documentation's snapshot; absolute changes and delete;
  broken `change_id` chain is a gap; inconsistent actions counted; crossed
  book counted; trade direction is the taker's; book at a given `change_id`;
  heartbeat answer; recorded live frames replay with no gap.
- Chain into OB (4): Starbase records into OB hold what the book holds; a
  simulated order fills when executions reach it; a priority reset moves an
  order behind a simulated one; recorded JSON frames into OB.

### Step 2a: Starbase sample PCAP (decoder generated from the published schema)

`starbase-market-data.pcap` (5.3 MB, packets from 2026-04-29):

| Check | Result |
|---|---|
| packets decoded / malformed / unknown templates | 13,306 / 0 / 0 |
| messages (37,257 snapshot, 488 incremental), heartbeats | 37,745, 86 |
| identical to Deribit's Python SDK 2.0 (`make sdk-check`) | yes: 51,051 dump lines, byte-for-byte equal |
| instruments with a definition / synchronised | 1,849 / 1,849 |
| gaps, duplicates, missing, resyncs | 0, 0, 0, 0 |
| crossed books after any transaction | 0 |
| delete, reduce or fill of an unknown order | 0 |
| new order joining ahead of its level (`sortOrderId` not the largest) | 0 |
| reduce that did not reduce; quantity off step; price off tick | 0; 0; 0 |
| later snapshots compared with the rebuilt book / equal | 8,259 / 8,259 (4 after incremental changes) |
| snapshot levels not listed in `sortOrderId` order | 135 (review.md 2) |
| Puts of orders already on the book / with a new `sortOrderId` | 283 / 283 (review.md 1) |
| chain into OB (`--ob`): books, records, BBO checks, level checks, mismatches | 105, 2,486, 42,210, 213,863, 0 |

The sample has no trades (templates 30, 31, 33 do not occur): trade handling is
checked only by unit tests. It has 488 incremental messages, so the
synchronisation and priority rules saw little live-like traffic.

### Step 2b: live public JSON book, BTC-PERPETUAL, 30 minutes (2026-11-18 23:13-23:43 CET)

`book.BTC-PERPETUAL.100ms` and `trades.BTC-PERPETUAL.100ms` from
www.deribit.com, no account; REST `public/get_order_book` (depth 20) every 10 s.

| Check | Result |
|---|---|
| frames, notifications, level updates, trades | 6,098; 5,430; 151,844; 3,848 |
| `change_id` gaps, resyncs, reconnects | 0, 0, 0 |
| crossed book after a notification; inconsistent actions | 0; 0 |
| REST snapshots fetched / failed (connect timeouts) | 165 / 2 |
| REST `change_id` delivered by the stream: compared exactly, top 20 levels per side | 18 snapshots, 18 equal, 0 level mismatches |
| REST `change_id` inside a 100 ms aggregate | 147 snapshots, 5,880 levels: 4,387 (74.6%) equal the stream book just before; 5,262 (89.5%) equal the book just before or just after |
| chain DeribitBookSync -> LevelWindow -> Mbp2Mbo -> OB: BBO checks, level checks (50 per side), mismatches | 5,431; 543,100; 0 (3,116,523 OB records) |

The recording (`build/live_frames.bin`, 10,312 frames: this run plus 4,214
frames of an earlier, interrupted run) replays through `deribit_live --replay`
with 2 snapshots, 0 gaps, 0 crossed books, and 856,200 OB level checks with 0
mismatches. Exact comparison of every snapshot needs the `raw` interval, which
needs an account (review.md 6).

### Step 3: not applicable

Market data needs no account; order entry and fills are in
`../../order_entry/deribit` and `../../post_trade/deribit`.

### Step 4: never.

## Not checked

- The live Starbase feed, its Retransmit Gateway, A/B arbitration (not
  reachable), and Starbase trade messages on real data (none in the sample).
- Whether the live Starbase feed still gives a known order a new `sortOrderId`
  (review.md 1).
- The JSON `raw` interval; options and linear instruments on the JSON path;
  runs longer than 30 minutes (Deribit's forced disconnects, if any).
- `DeribitFeed` as a running actor inside kaspar: the live check runs the same
  classes in process, not the actor.
