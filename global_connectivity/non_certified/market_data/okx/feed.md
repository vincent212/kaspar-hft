# Feed description: OKX V5 public market data (spot and perpetual swaps)

Sources are pages of the OKX V5 API documentation, one HTML page at
https://www.okx.com/docs-v5/en/, cited by anchor; retrieved 2026-11-18. "Live"
marks a fact settled by probing the public stream on 2026-11-18 (recordings in
`tests/data/` and `build/rec/`).

## 1. Venue and documents

| Document | Version | Date | Applies to |
|---|---|---|---|
| OKX API V5 documentation (`docs-v5/en`) | V5, single page | retrieved 2026-11-18 | everything below |
| OKX API V5 changelog (`docs-v5/log_en`) | as of 2026-11-18 | entries 2025-11-20 to 2026-11 | the deprecations it lists override the body of older pages |
| OKX API V5 documentation, 2025 edition (archive copy) | V5 | 2025 | only for the checksum string format, removed from the current page |

Products covered: spot `BTC-USDT` (`instType` SPOT) and the USDT-margined perpetual
`BTC-USDT-SWAP` (`instType` SWAP). Kaspar venue codes: `en::x::OKX` for spot,
`en::x::OKXF` for swaps.

## 2. Transport

| Item | Value | Source |
|---|---|---|
| Public WebSocket | `wss://ws.okx.com:443/ws/v5/public`, TLS only | #overview-production-trading-services |
| Demo public WebSocket | `wss://wspap.okx.com/ws/v5/public` (the demo books, not production's) | #overview-demo-trading-services |
| REST | `https://www.okx.com` (the tools use it; `openapi.okx.com` is the recommended domain since 2026-05-20 and is identical) | same |
| Connection limit | 3 connection requests per second per IP | #overview-websocket-connect |
| Subscription limit | 480 `subscribe`/`unsubscribe`/`login` requests per connection per hour | same |
| Idle timeout | the server closes a connection with no subscription or no data pushed for 30 s | same |
| Keep-alive | client sends the text `ping` after N < 30 s without a message, expects `pong` within N s; the handler uses N = 20 | same |
| Forced disconnects | `event: notice`, code 64008, before a service upgrade: reconnect | #overview-websocket-notification |
| REST order book rate limit | `GET /api/v5/market/books`: 40 requests per 2 s per IP | #order-book-trading-market-data-get-order-book |

No account or key is needed for public data.

## 3. Framing

One JSON object per WebSocket text frame. Exceptions: the literal `pong`. A push
is `{"arg": {"channel", "instId"}, "action"?, "data": [ ... ]}`; events are
`{"event": "subscribe" | "error" | "notice" | "channel-conn-count", ...}`. Prices
and sizes are decimal strings; `ts` is a string of Unix milliseconds; `seqId`
and `prevSeqId` are JSON integers. One frame carried one element in `data` in
every frame recorded (live).

## 4. Message catalogue

### `books` push (channel `books`, 400 levels, every 100 ms)

| Field | Type | Meaning |
|---|---|---|
| `arg.channel`, `arg.instId` | string | `books`, the instrument |
| `action` | string | `snapshot` (full 400 levels) or `update` (changed levels) |
| `data[].asks`, `data[].bids` | array of `[px, sz, "0", count]` | price, size at the price, a deprecated field always `"0"`, number of orders |
| `data[].ts` | string ms | book generation time |
| `data[].checksum` | integer | **deprecated, fixed at 0** (changelog 2026-06-23) |
| `data[].prevSeqId` | integer | `seqId` of the previous push; -1 in a snapshot |
| `data[].seqId` | integer | sequence id of this push |

Source: #order-book-trading-market-data-ws-order-book-channel.

### `trades` push (channel `trades`)

| Field | Type | Meaning |
|---|---|---|
| `instId` | string | instrument |
| `tradeId` | string | id of the last trade aggregated |
| `px`, `sz` | string | price and total size |
| `side` | string | `buy` or `sell`: the **taker's** side |
| `count` | string | number of maker fills aggregated |
| `source` | string | `0` normal order, `1` enhanced liquidity program order |
| `ts` | string ms | trade time |
| `seqId` | integer | the book sequence id the trade belongs to (present live; not in the field table) |

One push per taker order per fill price (#order-book-trading-market-data-ws-trades-channel).
The `trades-all` channel gives one trade per push; not used.

### `GET /api/v5/market/books?instId=..&sz=400` (REST)

`data[0]` has `asks`, `bids` (same level format), `ts` and `seqId` (live: present,
and equal to a `seqId` of the stream in 98 of 192 snapshots for spot, 91 of 192
for the swap). Cache refreshed every 50 ms (#order-book-trading-market-data-get-order-book).

## 5. Sequencing and recovery

- **Book build:** the stream sends a full snapshot on subscribe (`action`
  snapshot, `prevSeqId` -1, 400 levels per side), then updates. No REST snapshot
  is needed and none is used for sync.
- **Scope:** `seqId` is per instrument and the same on every connection
  (#order-book-trading-market-data-ws-order-book-channel, "Sequence ID").
- **Gap rule:** each update's `prevSeqId` must equal the `seqId` of the message
  before it. Anything else is a gap: the handler stops applying, resubscribes
  (a new connection) and the new snapshot reaches OB as recovery records.
- **Heartbeat:** with no book change, an update with empty `asks` and `bids`,
  `seqId == prevSeqId`; the rule above still holds.
- **Reset:** after maintenance `seqId` can be smaller than `prevSeqId`; the
  `prevSeqId` rule still holds and later messages follow it.
- **Checksum:** none. The `checksum` field is fixed at 0 since 2026-06-23 on
  `books`, `books-l2-tbt` and `books50-l2-tbt` (changelog). Live, 37 627 book
  messages over 32 minutes all carried 0; the public spread channel
  `sprd-books-l2-tbt`, the only book channel whose page still describes a
  checksum, also sent 0 in 12 messages probed. The handler still checks the
  field: a non-zero value is compared with CRC32 over the top 25 levels in the
  2025 documented format, `bid1px:bid1sz:ask1px:ask1sz:...` (a side that runs
  out is skipped), as a signed 32-bit integer; a mismatch is treated as a gap.
- **Trades:** `tradeId` increases by `count` per push (live: 0 breaks in 43 303
  trade pushes). A trade push is not sequenced with the book by the transport;
  its `seqId` places it (section 7).

## 6. Semantics

- Price-level book, by price (not by index), depth 400 per side.
- Updates carry **absolute** sizes per price. Size `"0"` deletes the level. A
  price not in the book is inserted in price order.
- Depth limit: a level leaving the top 400 arrives as a delete, and a level
  entering it arrives as an insert, so the book never holds more than 400 per
  side (live: maximum 400/400 bids/asks, never more; no delete of an unknown
  price in the probe).
- Order count per level is sent; individual orders are not (no L3 on OKX).
- Trade side: the taker's (#order-book-trading-market-data-ws-trades-channel).
- Trades are also reflected in the next book update (the level shrinks); they
  are not book deltas themselves.
- Trading states: none are published on these channels.
- Crossed books: none observed live (0 in 37 627 book messages).

## 7. Mapping to Kaspar

`OkxSync` keeps the 400-level book and feeds `sim::LevelWindow` (best
`window_depth` levels, default 50) into `sim::Mbp2Mbo`, which turns level sizes
into synthetic orders of `chunk` units (BTC 0.01 for spot, 1 contract for the
swap) for OB, as for Binance, Coinbase and Hyperliquid.

| Venue message | Kaspar record or message | Notes |
|---|---|---|
| `books` snapshot | `LevelWindow::resync` -> Mbp2Mbo recovery records (clear, then adds) | the first one also emits trading status 17 (`Mbp2Mbo::start`) |
| `books` update | `LevelWindow::flush` -> Mbp2Mbo adds, size changes, deletes | priority: Mbp2Mbo's rule (a smaller level loses chunks from the back) |
| heartbeat update | nothing | counted |
| `trades` | `Mbp2Mbo::trade` -> print (`l3_mbp_trd_t`) and executions against the front chunks | aggressor = `side`; `buyer_is_maker` = (`side` == sell) |

Trade ordering: a trade whose `seqId` is above the last book `seqId` applied is
not yet in the book, so it is stamped after the last book event and Mbp2Mbo
executes it from the front of the queue, the next update settling the level.
One at or below it is already in the book and gets transactTime 0, so Mbp2Mbo
replaces the chunks it executes. Live, all 43 303 trade pushes were ahead of the
book.

Priority: OKX publishes levels, not orders; OB's rule is assumed (section 10).
`endOfEvent` is set after each push (`Mbp2Mbo::end_event`). Instrument
identifier: the Kaspar asset's `securityID` (`sec_id` in the configuration).
Quantities are in chunks; sizes fit `uint32_t` for these instruments.

## 8. Instruments

Definitions from `GET /api/v5/public/instruments?instType=SPOT|SWAP`
(#public-data-rest-api-get-instruments), configured by hand in
`config/okx_example.ini`:

| instId | tickSz | lotSz | ctVal | chunk used |
|---|---|---|---|---|
| BTC-USDT | 0.1 | 0.00000001 BTC | - | 0.01 BTC |
| BTC-USDT-SWAP | 0.1 | 0.01 contract | 0.01 BTC | 1 contract |

The demo environment's instruments differ (BTC-USDT-SWAP: tickSz 0.01,
`instIdCode` 2021032601102993 in demo against 10459 in production, checked
2026-11-18); its books are not production's. Ticks do not depend on price.
Prices are always positive for these instruments.

## 9. Pitfalls

| Item | How it applies |
|---|---|
| 1. several documents, errata | the changelog overrides the body: checksum deprecation (2026-06-23), WebSocket port change (8443 -> 443) |
| 1. tables across pages, offsets, padding | not applicable: JSON |
| 1. examples contradicting tables | trades carry `seqId` live, absent from the field table |
| 2. endianness, fixed point, sentinels | decimal strings, parsed as received; `prevSeqId` -1 in snapshots |
| 2. several messages per frame | `data` is an array; every element is applied in order |
| 2. timestamps | Unix ms strings |
| 3. sequence scope | per instrument, identical across connections |
| 3. A/B arbitration | not applicable |
| 3. gap rule | `prevSeqId` == previous `seqId`; heartbeats and resets obey it |
| 3. recovery | resubscribe; the snapshot is pushed on subscribe |
| 3. snapshot alignment | not needed: the snapshot is in the stream |
| 3. state to clear | `OkxSync::reset()` on every new connection: book, seq, last tradeId |
| 3. recovery tested | injected gap and duplicate in `test_okx_sync.cpp` |
| 3. session behaviour | ping/pong, 30 s idle close, notice 64008 |
| 4. MBO or level | level, by price |
| 4. depth-limited | yes, 400; falls off as deletes (live) |
| 4. priority on modify | not published; OB's rule via Mbp2Mbo |
| 4. implied, iceberg | none published |
| 4. trades double counted | prints from `trades` only; book updates are not executions |
| 4. trade side | taker |
| 4. trading states | none |
| 4. book clears | on each snapshot |
| 5. price or yield, multipliers | price; swap size in contracts of 0.01 BTC |
| 5. price-dependent ticks | no |
| 5. instrument lifecycle | swaps do not expire; `instIdCode` changes on relist (changelog) |
| 6. update-rate options | `books` 100 ms; `books-l2-tbt` 10 ms needs VIP4+ |
| 6. history on subscribe | `trades` sends none (live: in 3 recordings the first trade was 182 to 1248 ms after the subscription) |
| 6. whole-book snapshot | 400 levels, not the whole book |
| 6. message order | trades and books are separate pushes; ordered by `seqId` |
| 6. connection limits | 30 s idle, 3 connects/s, 480 subscriptions/h |

## 10. Ambiguities

| Question | Reading taken | Alternatives | Resolved by |
|---|---|---|---|
| Is the checksum still sent? | no: always 0; checked anyway when non-zero | rely on it | changelog 2026-06-23; live: 37 627 zeros, 0 non-zero |
| Do levels beyond 400 come back as inserts? | yes | stale levels kept | live: book never above 400 per side; REST at the same `seqId` matched over 400 levels |
| Does a trade's `seqId` place it relative to the book? | yes | trade time | live: every trade `seqId` exceeded the last applied book `seqId` |
| Queue priority within a level | OB's rule (smaller keeps, larger loses) through Mbp2Mbo chunks | none expressible | not published by OKX: assumed |
