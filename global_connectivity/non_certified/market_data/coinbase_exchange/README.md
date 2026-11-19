# Coinbase Exchange market data (L3 order by order)

**Not certified.** The L3 handler has never received Coinbase Exchange's
order-by-order stream: the `full` and `level3` channels need an API key, and
none was supplied. What it has been checked against is listed below with its
numbers. The feed description, with sources and open questions, is `feed.md`.

## Contents

| Path | What |
|---|---|
| `feed.md` | Feed description: channels, sync rule, messages, mapping to Kaspar records, pitfalls, ambiguities |
| `include/coinbase_exchange/CoinbaseExchangeSync.hpp`, `src/CoinbaseExchangeSync.cpp` | The L3 sync: the book from `full`/`level3` and the REST level-3 snapshot, as Kaspar L3 records for OB (no `Mbp2Mbo`) |
| `include/coinbase_exchange/CoinbaseExchangeL2Sync.hpp`, `src/CoinbaseExchangeL2Sync.cpp` | The no-key fallback: `level2_batch` + `matches` into `sim::Mbp2Mbo`, as the Advanced Trade feed |
| `include/coinbase_exchange/act/CoinbaseExchangeFeed.hpp`, `src/CoinbaseExchangeFeed.cpp` | The WebSocket actor (Boost.Beast, `sim/WsNet.hpp`): connection, signed subscribe, REST snapshots on a helper thread with backoff, the recorder of raw frames, the sandbox guard |
| `include/coinbase_exchange/Rest.hpp`, `src/Rest.cpp` | HTTPS GET, signatures, credentials file, endpoints, `host_allowed` |
| `include/coinbase_exchange/Num.hpp` | Exact decimals, UUIDs, ISO and FIX times |
| `src/cbx_md.cpp`, `src/Verifier.*` | `cbx_md`: `dump` (the monitor), `live` and `replay` (the public check), `check-l3` (the exact L3 check for step 3) |
| `config/products.ini` | Increments, display units, ids |
| `tests/` | `test_cbx_sync.cpp` (39 tests from the documentation's examples, the sync rules, the feed's L3 handler and the step-3 checker), `test_cbx_ob.cpp` (3: the records in a real OB), `test_cbx_recorded.cpp` (2: recorded frames), `data/` |

## Build and run

```bash
export KSPRPROJ=~/kaspar-hft
make test          # 44 tests
make tool          # build/cbx_md

build/cbx_md dump --product BTC-USD                         # public L2 fallback, book once a second
build/cbx_md dump --product BTC-USD --mode full --env sandbox   # L3, needs the key file
build/cbx_md live --product BTC-USD,ETH-USD --minutes 32 --record DIR
build/cbx_md replay --dir DIR/BTC-USD --product BTC-USD
build/cbx_md check-l3 --product BTC-USD --env sandbox --minutes 30   # needs the key file
```

The kaspar libraries must be built with the `COINBASEX` venue code
(`chutil/gencode/enum/def/exch_code.enum`, appended): the venue tables are
`inline constexpr` in the generated header, so libraries built before the
change reject `COINBASEX` (`ASSERT en::is_valid(mkt)` in `Data.hpp`).

In kaspar: `create_CoinbaseExchangeFeed(cfg, spec, ob)` (actor header) gives
the feed actor for one product, sending records to that product's OB; the
asset is registered as bnsim registers crypto assets (units 1, `maxpx` far
above any price, a ring ladder).

## Credentials

The L3 modes read `~/.kaspar/keys/coinbase_exchange_sandbox.json` (or
`key_file`), mode 600, with `environment`, `key`, `secret`, `passphrase`
(`../../order_entry/coinbase_exchange/README.md`, "Credentials"). With
`environment = sandbox` the feed connects only to
`ws-direct.sandbox.exchange.coinbase.com` and `api-public.sandbox.exchange.coinbase.com`
and refuses any other host, and refuses keys whose `environment` differs.

## Checked (2026-11-18)

**Which endpoints are public** (no account): see feed.md section 1. `full`,
`level3` and `level2` need a key on production and in the sandbox;
`level2_batch`, `matches`, `heartbeat` and `ticker` do not; the REST level-1,
-2 and -3 books are public on both.

**Step 1, unit tests**: 44 of 44 pass. They cover every message type of the
`full` channel from the documentation's examples, the `level3` compact form
with its schema, snapshot alignment (discard at or below S, first applied
S+1, a snapshot too old for the buffer refetched, a snapshot newer than the
buffer), duplicates, out-of-order delivery inside the window, a gap beyond it
(gap record, `l3_chr_v2_t` then recovery records, buffered messages applied,
`transactTime` strictly increasing past the recovery), a heartbeat ahead of
the stream for more than 3 s (a gap), a snapshot the sync did not ask for
(ignored by the feed's handler), disconnect, an unknown
maker and an overfill (resync), priority on change, the display-unit cap,
trading status, the step-3 checker's second sync reading the `level3`
schema after the ring rolled past it, the records in a real OB (levels and BBO equal to the sync's
after adds, executions, changes and a resync; a simulated order queued behind
real orders fills only when executions reach it), and two tests from recorded
frames (an L3 snapshot slice plus the public matches against the next
snapshot; 100 `level2_batch` updates against an independent Python
reconstruction).

**Step 2, live public data, no account.** Two runs on production:

- *Recording* (node.js recorder, 42 minutes, 17:27-18:09 UTC): BTC-USD and
  ETH-USD `level2_batch`, `matches`, `heartbeat`, `ticker` on one connection,
  REST level 2 every 10 s and level 3 every 60 s per product; replayed through
  `cbx_md replay`.
- *Live* (`cbx_md live`, 32 minutes, 18:08-18:40 UTC): the C++ feed actor, one
  connection per product, the same REST schedule, the comparisons made as the
  data arrived; raw frames recorded.

| | Recording BTC-USD | Recording ETH-USD | Live BTC-USD | Live ETH-USD |
|---|---|---|---|---|
| Duration | 42 min | 42 min | 32.0 min | 32.0 min |
| Disconnects, reconnects | 0 | 0 | 0 | 0 |
| `level2_batch` updates / public trades | 41,208 / 63,605 | 41,391 / 8,460 | 31,060 / 47,396 | 30,498 / 5,141 |
| Trade ids missing / duplicated | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| Crossed book (L2 states / L3 snapshots) | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| REST level-2 books compared | 251 | 250 | 192 | 189 |
| ... a stream state within +-3 s equal on the best level | 221 (88%) | 194 (78%) | 165 (86%) | 168 (89%) |
| ... equal on the top 10 levels | 196 (78%) | 112 (45%) | 150 (78%) | 122 (65%) |
| ... equal on the top 50 levels | 153 (61%) | 92 (37%) | 122 (64%) | 104 (55%) |
| ... levels of the top 50+50 differing at the nearest state (mean) | 17.6 | 34.6 | 15.4 | 25.1 |
| L3 rounds (snapshot A + matches vs snapshot B, 60 s apart) | 41 | 41 | 31 | 31 |
| Public matches applied / discarded (sequence <= A's) | 62,525 / 550 | 8,289 / 158 | 46,052 / 9 | 4,983 / 1 |
| Orders the matches filled / of them still in B | 4,251 / 0 | 687 / 0 | 3,503 / 0 | 272 / 0 |
| Orders the matches reduced: in B at the predicted size / gone / wrong size | 6 / 58 / 0 | 2 / 9 / 0 | 7 / 59 / 0 | 3 / 22 / 0 |
| Matches on makers opened after A (not checkable) | 57,442 | 7,512 | 41,647 | 4,653 |
| Match price different from the maker's price in A | 57 | 25 | 13 | 5 |
| Untouched orders in both books / size or price changed | 4,157,424 / 360 | 2,068,112 / 220 | 3,149,745 / 330 | 1,566,824 / 219 |
| Levels compared A to B / queue order broken | 1,518,740 / 1 | 764,232 / 1 | 1,151,103 / 10 | 579,050 / 2 |
| Levels with orders new since A / a new order before an old one | 1,273 / 0 | 2,666 / 0 | 1,036 / 1 | 1,985 / 1 |
| OB checks, L2 chain (`Mbp2Mbo` -> OB) / level or BBO mismatches | 251 / 0 | 250 / 0 | 192 / 0 | 189 / 0 |
| OB checks, L3 chain (sync -> OB) / level or BBO mismatches | 83 / 0 | 83 / 0 | 63 / 0 | 63 / 0 |

"Match price different from the maker's price in A" and "size or price
changed" are what `change` messages (modify, STP) do between two snapshots;
the public data has no `change`, so they are counted, not checked. A modify
that increases an order's size moves it behind newer orders, which shows as
"queue order broken" or "a new order before an old one". The live run had two
REST level-2 requests time out (ETH-USD); the recording had none.

What the numbers say:

- The L3 sync applies the documented sync rule correctly on real data as far
  as the public data can show it: of the orders in a level-3 snapshot that the
  public matches filled (8,713), none was in the next snapshot; every order the
  matches reduced and that was still in the next snapshot had exactly the
  predicted size (18 orders, few because orders near the touch rarely last a
  minute); matches at or below the snapshot's sequence were discarded. Orders that
  opened after the snapshot ("unknown maker", most matches) and any `open`,
  `done` or `change` cannot be checked without the `full` channel.
- Orders keep their relative order at a level from one snapshot to the next,
  and new orders come after old ones, except in at most 10 of 1.15 million
  level comparisons; a size-increasing modify would produce exactly these, but
  the public data cannot show whether that is what they were. The REST level-3
  order is taken as queue priority for the recovery records.
- OB, fed the sync's records, held exactly the sync's levels and BBO at every
  check, for both the L3 records and the `Mbp2Mbo` chain.
- `level2_batch` against REST level 2: agreement depends on timing, since a
  batch hides the states inside its 50 ms; BTC-USD matched the top 10 levels
  in most comparisons, ETH-USD in fewer. An independent Python reconstruction
  of the same recording gives the same count for ETH-USD (top 10: 112 of 250),
  so the C++ sync and the reconstruction agree; whether the rest is only timing cannot be shown
  with batched data. No crossed book, and no trade id missing or duplicated
  between the first and the last match.

## Known duplication

- `parse_iso_time` (`include/coinbase_exchange/Num.hpp`) is
  `sim::CoinbaseSync::parse_time` (`sim/src/CoinbaseSync.cpp`) again, as an
  inline on a `string_view`. Sharing it means moving it out of `sim/`.

## Not checked

- The `full` and `level3` channels themselves: `received`, `open`, `done`,
  `change` and `activate` on real data, real sequence gaps and resyncs,
  `ws-direct`, the signed subscribe against the venue. The parsing of those
  messages is checked only against the documentation's examples.
- The lights and the SOM on this feed (bnsim's chain): only OB was run.
- 24-hour behaviour (forced disconnects, memory of a full day's book).
- The FIX market-data route (feed.md section 10).

## Step 3 of the testing ladder (sandbox): what will run

With the key file present (`environment` `sandbox`):

```bash
build/cbx_md check-l3 --product BTC-USD --env sandbox --minutes 30 --record DIR
build/cbx_md check-l3 --product BTC-USD --env sandbox --mode level3 --minutes 30
```

against `wss://ws-direct.sandbox.exchange.coinbase.com` and
`api-public.sandbox.exchange.coinbase.com` only: the signed `full`
subscription; the snapshot alignment on real data; every message type
counted; sequence gaps, duplicates and reordering counted; every minute the
sandbox's REST level-3 book (sequence S) seeded into a second sync, given the
stream's messages after S, and compared with the running book order by order
and level by level at the same sequence (`L3Checker`, tested on a synthetic
stream); the
`level3` channel the same way; and, with the order manager's sandbox orders,
our own orders found in the book under the id the OM reports.
