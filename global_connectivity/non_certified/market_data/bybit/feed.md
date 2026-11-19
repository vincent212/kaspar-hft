# Feed description: Bybit V5 public market data (spot and linear perpetuals)

Not certified. Sources are Bybit's V5 API documentation pages as fetched on
2026-11-18 (Docusaurus source, `bybit-exchange.github.io/docs`, changelog head
2026-11-17), cited by page path below, and Bybit's public stream and archive,
probed on 2026-11-18 ("observed").

## 1. Venue and documents

| Document | Version | Date | Applies to |
|---|---|---|---|
| `v5/websocket/public/full-ob` (Full Orderbook) | V5 web docs | fetched 2026-11-18 | `orderbook.full.{symbol}`, the topic this handler uses |
| `v5/market/full-ob` (Get Full Orderbook) | V5 | 2026-11-18 | the REST snapshot that starts the full book |
| `v5/websocket/public/orderbook` | V5 | 2026-11-18 | `orderbook.{1,50,200,1000}.{symbol}` (DEPTH mode, the archive) |
| `v5/websocket/public/trade` | V5 | 2026-11-18 | `publicTrade.{symbol}` |
| `v5/websocket/public/orderbook-rpi` | V5 | 2026-11-18 | RPI book (not used) |
| `v5/ws/connect` | V5 | 2026-11-18 | hosts, ping, idle cut-off |
| `v5/market/instrument` | V5 | 2026-11-18 | tick and lot sizes |
| `changelog/v5` | head 2026-11-17 | 2026-11-18 | full book for spot from 2025-12-16, futures from 2026-08-11; depth 500 removed 2025-09-11 |
| Order book archive `quote-saver.bycsi.com/orderbook/{spot,linear}/{SYM}/{DATE}_{SYM}_ob200.data.zip` | undocumented | 2026-11-17 files | replay (observed format) |
| Trade archive `public.bybit.com/{trading,spot}/{SYM}/` | undocumented | 2026-11-17 files | replay trades (observed columns) |

The SBE market data (`ob.50.sbe`) is on the Market Maker WebSocket only and is
not used.

## 2. Transport

- WebSocket, TLS: `wss://stream.bybit.com/v5/public/spot` and `/v5/public/linear`
  (`v5/ws/connect`). One connection per category carries the book and trade
  topics of a symbol.
- Subscribe: `{"op":"subscribe","args":["orderbook.full.BTCUSDT","publicTrade.BTCUSDT"]}`.
- Heartbeat: the client sends `{"op":"ping"}`; Bybit recommends every 20 s and
  cuts a connection with no ping-pong and no data for 10 minutes (`v5/ws/connect`).
  The handler pings every 20 s.
- REST snapshot: `GET https://api.bybit.com/v5/market/full_orderbook?category=&symbol=`.
  IP limit 600 requests per 5 s (`v5/rate-limit`). The handler fetches one at start,
  after a gap and after `u = 1`, at most one a second; the verify tool adds one
  every 10 s.

## 3. Framing

JSON text frames, one message each. Prices and sizes are decimal strings. Book
message: `{"topic","type":"delta","ts","data":{"s","b":[[px,sz]...],"a":[...],"u","seq"},"cts"}`.
Field order differs by product and depth (`ts` before or after `type`,
`v5/websocket/public/orderbook` reminder); the parser does not depend on order.

## 4. Message catalogue

### `orderbook.full.{symbol}` (delta only)

| Field | Type | Meaning |
|---|---|---|
| `type` | string | always `delta` |
| `ts` | ms | system time |
| `cts` | ms | matching-engine time; correlates with trade `T` |
| `data.s` | string | symbol |
| `data.b`, `data.a` | [[price, size]] | absolute sizes; `"0"` deletes the level |
| `data.u` | int | update id, +1 per message; `1` = re-initialise |
| `data.seq` | int | cross sequence: increasing, not consecutive |

### `GET /v5/market/full_orderbook` result

`s`, `b`, `a` (as above, whole book), `ts`, `u`, `seq`, `cts`. `u` "corresponds
to `u` in the WebSocket full orderbook stream" (`v5/market/full-ob`).

### `orderbook.{depth}.{symbol}`

`type` `snapshot` or `delta`; same `data` fields. Level 1 has snapshots only and
repeats one every 3 s with the same `u`.

### `publicTrade.{symbol}`

| Field | Meaning |
|---|---|
| `T` | fill time, ms |
| `S` | **taker** side, `Buy` or `Sell` |
| `v`, `p` | size, price |
| `i` | trade id |
| `BT` | block trade |
| `RPI` | trade against a Retail Price Improvement order |
| `seq` | cross sequence |

## 5. Sequencing and recovery

Full book (`v5/websocket/public/full-ob`, "Full Order Book Synchronization
Procedure" and "Order Book Update Procedure"):

1. Subscribe; buffer deltas. A break in `u` restarts the buffer; a delta whose
   `seq` is lower than the previous one is dropped.
2. Fetch the REST snapshot (update id L, sequence S).
3. Alignment: the snapshot contains every delta with `u <= L`. The first delta
   applied must have `u = L + 1`. If the oldest buffered delta has `u > L + 1` the
   snapshot is older than the stream: fetch another. If a buffered delta has
   `u = L` and a different `seq`, fetch another. Deltas with `u <= L` are dropped.
4. Afterwards: `u` must be the previous `u + 1`. `u <=` previous: ignore. `u >`
   previous + 1: gap; send `l3_gap_v2_t`, start again from step 1. `u = 1`:
   re-initialise (service restart, delisting, before an auction, auction to
   continuous trading, tick or lot change).

The documentation states step 4 of the procedure in terms of `seq` ("snapshot's
`seq` strictly less than the first buffered delta's `seq`"); the handler uses the
equivalent test on `u`, which is consecutive, plus the `seq` check at equal `u`.
The procedure's numbering skips step 6.

Scope: `u` and `seq` are per symbol and topic, not per connection (observed: the
REST snapshot, fetched over separate HTTPS connections, carries the stream's `u`
and matches the stream's book at that `u`). No WebSocket reconnect happened in the
live runs, so continuity of `u` across a reconnect was not observed directly; the
handler resynchronises from REST after every reconnect regardless.

Depth topics: a `snapshot` message replaces the book; deltas follow with `u + 1`.
A gap cannot be repaired from REST at that depth; the handler resubscribes,
which yields a new snapshot.

Archive: the recorded `orderbook.200` stream. A file starts with a snapshot and
contains further snapshots (observed: 2 mid-file snapshots in linear 2026-11-17,
1 in spot; one with `u = 1`).

## 6. Semantics

- Price-level book, keyed by price. Full depth on `orderbook.full` (observed up
  to 20,062 bid and 40,806 ask levels on linear BTCUSDT; 10,318 per side on spot).
- Sizes are absolute; `0` deletes.
- No checksum (none documented for any Bybit book topic).
- RPI orders are not in the book topics or the REST snapshot.
- Trades: `S` is the taker side. Trades are not also reported as separate book
  changes; the book delta that follows reflects them.
- Trading states: none in the market-data stream. The instrument's `status`
  (`v5/market/instrument`) gives PreLaunch / Trading / Delivering / Closed.

## 7. Mapping to Kaspar

| Venue message | Kaspar record | Notes |
|---|---|---|
| book delta / snapshot | `sim::LevelWindow` -> `sim::Mbp2Mbo` -> `l3_mbo_v2_t` add / change / delete | the best `window` levels per side (default 1000) go to OB, cut into orders of `chunk` |
| `publicTrade` (not RPI, not BT) | `l3_mbp_trd_t` print, and `Mbp2Mbo::trade` (`l3_mbo_trd_v2_t` against the resting chunk orders) | `aggresorSide` 1 when `S = Buy`, 2 when `S = Sell` |
| `publicTrade` with `RPI` or `BT` | `l3_mbp_trd_t` only | RPI orders and block trades are not in the public book |
| gap in `u` | `l3_gap_v2_t` | then a new REST snapshot (full) or resubscribe (depth) |
| `u = 1` with an empty book | `l3_chr_v2_t` | delisting or pre-auction: OB cleared |
| snapshot | `Mbp2Mbo` recovery (its resync) | |

- Priority: price levels have no order priority. `Mbp2Mbo` puts added size at the
  back of the level and removes reduced size from the back, as for the other
  crypto feeds.
- Identifiers: synthetic order ids from `Mbp2Mbo`; `securityID` from configuration.
- Quantity unit: `chunk` per OB order (configured; linear BTCUSDT 0.1 BTC, spot
  0.01 BTC in the tests); prints in `print_unit` (the instrument's `qtyStep` or
  `basePrecision`: 0.001 linear, 0.000001 spot).
- `endOfEvent`: on the last record of each WebSocket message (each delta, each
  trade).
- Time: `cts` (matching engine) for book records, `T` for trades, in ns.
- Trading status: no `l3_sst_t` is sent (known deviation, section 10).
- Venue codes: `en::x::BYBIT` (spot), `en::x::BYBITF` (linear).

## 8. Instruments

From `GET /v5/market/instruments-info`: `priceFilter.tickSize` (BTCUSDT 0.1 on
both), `lotSizeFilter.qtyStep` (linear 0.001) or `basePrecision` (spot 0.000001).
Prices are converted to ticks by rounding `price / tick`. Instruments with zero or
negative prices do not exist in spot or linear.

## 9. Pitfalls

| Item | How it applies |
|---|---|
| Several documents | Depth topics and the full-book topic have different sync rules; the full-book page is the authority for this handler. |
| Errata / changelog | Depth 500 removed; level-1000 interval 300 ms -> 200 ms (2025-11-25); full book for futures only since 2026-08-11. |
| Offsets, lengths, padding | Not applicable (JSON). |
| Examples vs tables | The REST page says at most 10,000 levels per side; the live REST answer had 40,806 ask levels (section 10, item 3). |
| Endianness, fixed point | Decimal strings; prices to ticks by rounding. |
| Timestamps | ms since epoch: `ts` (system), `cts` (engine), trade `T`. |
| Sequence scope | Per symbol and topic. |
| A/B arbitration | Not applicable. |
| Gap rule | `u` + 1; `u = 1` resets; `seq` only orders. |
| Snapshot alignment | Snapshot contains `u <= L`; first delta `L + 1`. Exactly checked live, section README. |
| Snapshot too old | Refetch, at most one a second. |
| State on reset | Book and buffer cleared; CHR on an empty reset. |
| Injection | Gap, duplicate, `u = 1`, seq regression and stale snapshots are unit-tested. |
| MBO / level | Level by price. |
| Depth-limited books | Full topic: no. The `window` cut is the handler's, handled by `LevelWindow`. |
| Priority on modify | Not applicable to levels; `Mbp2Mbo`'s rule. |
| Implied, hidden | RPI is hidden from these topics. |
| Trades also as book deltas | Yes, implicitly: the next delta reflects the trade. `Mbp2Mbo`'s late-trade rule prevents double counting. |
| Trade side | Taker (`S`). |
| Trading states | Not in the stream (section 10). |
| Book clears | `u = 1` with empty sides. |
| Price vs yield, multipliers | Not applicable. |
| Ticks that depend on price | No; fixed per instrument, but a tick change produces `u = 1`. |
| Subscription options that change rate | Depth chosen: full (200 ms) over 1000 (200 ms) and 50 (20 ms). |
| History on subscribe | None for books; `publicTrade` sends no history. |
| Whole book snapshot | Yes: REST full book, tens of thousands of levels (about 3 MB for linear BTCUSDT). |
| Order within an event | Trades and book deltas arrive as separate messages; trades usually before the delta covering them. |
| Connection limits | 20 s ping, 10 min idle cut. No 24 h disconnect observed in 32 minutes. |

## 10. Ambiguities

| Question | Reading taken | Alternatives | Resolved by |
|---|---|---|---|
| Does the REST full book's `u` equal a stream delta's `u` (so a comparison at exactly that update is possible)? | Yes | REST `u` from a separate counter | Live: 748 of 748 REST snapshots (two 31-32 minute runs per product) matched the book rebuilt at that `u` exactly |
| Is `u` per connection? | No, per symbol and topic | per connection | Live: REST snapshots (separate connections) carry the stream's `u`. A WebSocket reconnect was not observed |
| REST depth limit of 10,000 per side | The answer is the whole book | 10,000 only | Live: 19,168 bids / 40,806 asks returned; the verify tool compares only the price range both books cover |
| Taker or maker side in `publicTrade` | Taker | maker | Documentation (`S`: "Side of taker"); archive assumed the same |
| Do RPI and block trades consume public book levels? | No: print only | consume | Documentation: RPI orders are not in the book; block trades are off book. Not measured |
| Trading status | Not sent (no `l3_sst_t`); OB stays in its default state | map instrument `status` from REST | Open: review item 3 |
| Archive trade-file side column | Taker, as on the WebSocket | maker | Not documented; replay showed no crossed books either way |
