# Feed description: Coinbase Exchange market data (WebSocket `full`/`level3`, REST level 3)

Coinbase Exchange is Coinbase's institutional spot exchange (formerly Coinbase
Pro). It is a different venue and API from Coinbase Advanced Trade, which
`sim/CoinbaseFeed.cpp` already handles (venue code `COINBASE`); this handler
uses the new code `COINBASEX`.

Every entry cites its source. The documentation is a set of web pages without
version numbers; each was fetched as Markdown (`<url>.md`) on 2026-11-18.
Abbreviations:

- **CH**: Exchange WebSocket Channels, https://docs.cdp.coinbase.com/exchange/websocket-feed/channels
- **OV**: Exchange WebSocket Overview, https://docs.cdp.coinbase.com/exchange/websocket-feed/overview
- **WSRL**: Exchange WebSocket Rate Limits, https://docs.cdp.coinbase.com/exchange/websocket-feed/rate-limits
- **WSAUTH**: Exchange WebSocket Authentication, https://docs.cdp.coinbase.com/exchange/websocket-feed/authentication
- **BOOK**: Get product book (REST, with its OpenAPI excerpt), https://docs.cdp.coinbase.com/api-reference/exchange-api/rest-api/products/get-product-book
- **PROD**: Get single product, https://docs.cdp.coinbase.com/api-reference/exchange-api/rest-api/products/get-single-product
- **RRL**: REST Rate Limits, https://docs.cdp.coinbase.com/exchange/rest-api/rate-limits
- **RAUTH**: REST Authentication, https://docs.cdp.coinbase.com/exchange/rest-api/authentication
- **SBX**: Exchange Sandbox, https://docs.cdp.coinbase.com/exchange/introduction/sandbox
- **CL**: Exchange Changelog, https://docs.cdp.coinbase.com/exchange/changes/changelog (latest entry 2026-Feb-02)
- **Live**: what the public endpoints answered on 2026-11-18 (section 1, "Probed").

## 1. Venue and documents

| Document | Version | Date | Applies to |
|---|---|---|---|
| CH | none | fetched 2026-11-18 | Every channel; the `full` channel's sync procedure and messages; `level3`; `level2_batch`; `matches`; `heartbeat`; `status` |
| OV | none | fetched 2026-11-18 | Endpoints, subscribe, sequence numbers |
| WSRL, WSAUTH | none | fetched 2026-11-18 | Limits; signing the subscribe |
| BOOK, PROD | OpenAPI `info.version` 1.0 | fetched 2026-11-18 | REST snapshot (level 1, 2, 3); product increments and status |
| RRL, RAUTH | none | fetched 2026-11-18 | REST limits; the signature |
| SBX | none | fetched 2026-11-18 | Sandbox hosts |
| CL | entries to 2026-Feb-02 | fetched 2026-11-18 | Authentication enforced on `full`, `level2`, `level3` (2023-AUG-01, CL); `level3` channel added; `change` `reason`, `new_price`, `old_price` added |

The FIX 5.0 market-data gateway (`fix-md.exchange.coinbase.com`, L3 with
`RptSeq`) is a second route to the same data; it is not built here (section 10).

**Probed** (2026-11-18, no account, from this Mac):

| Endpoint | Result |
|---|---|
| `wss://ws-feed.exchange.coinbase.com`, `full`, `level3`, `level2` | `{"type":"error","message":"Failed to subscribe","reason":"level2, level3, and full channels now require authentication. ..."}` |
| same host, `level2_batch`, `matches`, `heartbeat`, `ticker` | public; data flows |
| `wss://ws-feed-public.sandbox.exchange.coinbase.com`, `full`, `level3`, `level2` | the same authentication error |
| sandbox `heartbeat`, `matches` | public |
| `GET https://api.exchange.coinbase.com/products/BTC-USD/book?level=1,2,3` | public, HTTP 200. Level 3: 6.25 MB, 101,736 orders, 2.97 s; `cache-control: public, max-age=1` (Cloudflare) |
| `GET https://api-public.sandbox.exchange.coinbase.com/products/BTC-USD/book?level=3` | public; the sandbox has 13 products |

## 2. Transport

- WebSocket JSON. Production `wss://ws-feed.exchange.coinbase.com` (public
  channels; the authenticated ones once signed) and `wss://ws-direct.exchange.coinbase.com`
  ("direct access", authentication required) (OV, "Exchange WebSocket
  Overview"). Sandbox `wss://ws-feed-public.sandbox.exchange.coinbase.com` and
  `wss://ws-direct.sandbox.exchange.coinbase.com` (SBX, "Sandbox URLs"). The
  handler uses `ws-direct` for the signed L3 channels and `ws-feed` for the
  public fallback.
- Subscribe within 5 seconds of connecting, or the server disconnects; the
  answer is a `subscriptions` message (OV, "Subscribe"). Product ids at the root
  of the subscribe apply to every channel in it (OV, "Specifying Product IDs").
- Authentication: the subscribe carries `signature`, `key`, `passphrase` and
  `timestamp`, signed as `GET /users/self/verify` with the REST scheme:
  base64(HMAC-SHA256(base64decode(secret), timestamp + "GET" + "/users/self/verify")),
  timestamp in seconds, within 30 s of the server's time (WSAUTH; RAUTH,
  "Signing a Message", "Selecting a Timestamp").
- Compression: `permessage-deflate` may be offered; not used (OV,
  "Websocket Compression Extension").
- Keep-alive: the `heartbeat` channel sends one message per second per product
  (CH, "Heartbeat Channel"). The handler's idle timeout is 30 s; Beast pings at
  half of it.
- Limits (WSRL): 8 requests/s per IP (burst 20); 100 client messages/s per IP;
  inbound messages 10/s (burst 1000); at most 10 subscriptions per product per
  channel per account. REST public endpoints: 10 requests/s per IP, burst 15
  (RRL). BOOK warns that "abuse of Level 3 via polling can cause your access to
  be limited or blocked" (BOOK, quoted in the survey; see section 9).
- TLS 1.2 or higher on exchange.coinbase.com since 2025-10-20 (CL, 2025-OCT-20).

## 3. Framing

- One JSON object per WebSocket text frame on the `full` channel; one JSON
  array per frame on `level3` after a one-time schema object (CH, "Level3
  Channel", "L3 Schema"). Live: every frame held one message.
- Prices and sizes are decimal strings ("400.23", "5.23512"); `sequence` is a
  JSON number on `full` and a string inside the `level3` arrays ("57560479456")
  (CH examples). The handler parses both.
- Times are ISO 8601 UTC with up to 9 fraction digits (REST level 3 `time`:
  "2026-11-18T17:25:47.637647565Z"; `full` messages: 6 digits) (CH, BOOK, Live).
- Order ids are UUIDs (CH examples).

## 4. Message catalogue

Fields as CH lists them; "→" gives what the handler reads. JSON has no offsets;
the tables give the field, its JSON type, and its meaning.

### Subscribe (client → server)

| Field | Type | Meaning |
|---|---|---|
| `type` | string | `"subscribe"` |
| `product_ids` | array of string | e.g. `["BTC-USD"]` |
| `channels` | array | `"full"` or `"level3"`, and `"heartbeat"` (L3); `"level2_batch"`, `"matches"`, `"heartbeat"` (fallback) |
| `signature`, `key`, `passphrase`, `timestamp` | string | authentication (section 2) |

### `received` (full)

`type`, `time`, `product_id`, `sequence`, `order_id`, `side`, `order_type`
(`limit`: `size`, `price`; `market`: optional `funds`), `client_oid` (own
orders only). A valid order reached the matching engine; it is not on the book
(CH, "Received"). → sequence only.

### `open` (full)

| Field | Type | Meaning |
|---|---|---|
| `type` | string | `"open"` |
| `time` | string | ISO 8601 |
| `product_id` | string | |
| `sequence` | number | per product |
| `order_id` | string (UUID) | |
| `price` | string | |
| `remaining_size` | string | size going on the book |
| `side` | string | `buy` / `sell` |

"The order is now open on the order book"; sent only for orders not fully
filled at once, never for market orders (CH, "Open").

### `done` (full)

`type`, `time`, `product_id`, `sequence`, `price`, `order_id`, `reason`
(`filled` or `canceled`), `side`, `remaining_size` (0 for filled); market
orders have no `price` or `remaining_size`. Sent for every received order; no
more messages for the order after it. "`done` messages for orders that are not
on the book should be ignored" (CH, "Done"). `cancel_reason` (101-107) only on
authenticated own orders (CH, "Cancel Reason").

### `match` (full; also the `matches` channel)

| Field | Type | Meaning |
|---|---|---|
| `type` | string | `"match"` (`"last_match"`: the first message of the `matches` channel) |
| `trade_id` | number | per product |
| `sequence` | number | per product, shared with the other messages |
| `maker_order_id`, `taker_order_id` | string (UUID) | |
| `time`, `product_id` | string | |
| `size`, `price` | string | |
| `side` | string | **the maker's side**: `sell` is an up-tick (the aggressor bought) |

(CH, "Match", "Matches Channel".)

### `change` (full)

Two forms (CH, "Change"):

- self-trade prevention: `reason` `"STP"`, `old_size`, `new_size`, `price`
  (`null` for a market order), `side`, `order_id`, `sequence`, `time`,
  `product_id`; size only decreases;
- modify order (FIX OrderCancelReplaceRequest): `reason` `"modify_order"`,
  `old_size`, `new_size`, `old_price`, `new_price`.

Sent for open and for received-but-not-open orders; the latter "can" be
ignored (CH).

### `activate` (full)

A stop order placed: `type`, `product_id`, `timestamp` (epoch seconds as a
string), `order_id`, `stop_type`, `side`, `stop_price`, `size`, `funds`,
`private`; no `sequence` in the example (CH, "Activate"). → ignored.

### `level3` channel

A schema message `{"type":"level3","schema":{...}}` first, then arrays in the
schema's order (CH, "L3 Schema"):

| Type | Array |
|---|---|
| `open` | type, product_id, sequence, order_id, side, price, size, time |
| `change` | type, product_id, sequence, order_id, price, size, time |
| `match` | type, product_id, sequence, maker_order_id, taker_order_id, price, size, time |
| `done` | type, product_id, sequence, order_id, time |
| `noop` | type, product_id, sequence, time |

"While the schema should not change, it may" (CH): the handler reads the
schema and refuses arrays before one (counted as `bad_message`). `received`
and `activate` have no compact form; `match` carries no side (the maker's side
is taken from the book).

### `heartbeat`

`type`, `sequence` (the product's latest), `last_trade_id`, `product_id`,
`time` (CH, "Heartbeat Channel").

### `status`

`type`, `products` (array of product objects: `id`, `status`, `cancel_only`,
`limit_only`, `post_only`, `trading_disabled`, `auction_mode`, ...), `currencies`
(CH, "Status Channel").

### `level2_batch` (public fallback)

`snapshot`: `product_id`, `bids`, `asks` (arrays of `[price, size]`, the whole
book). `l2update`: `product_id`, `time` (of the newest change in the batch),
`changes` (arrays of `[side, price, size]`, size absolute, `"0"` removes the
level). Batches every 50 ms. No sequence number (CH, "Level2 Channel", "Level2
Batch Channel"; Live).

### REST `GET /products/{id}/book?level=3`

`bids`, `asks`: arrays of `[price, size, order_id]`; `sequence` (number);
`auction_mode` (bool); `auction` (object or null); `time` (BOOK). Level 3 "returns
the entire order book", non-aggregated. Level 2 rows are `[price, size,
num_orders]`.

### REST `GET /products/{id}`

`quote_increment`, `base_increment`, `status`, `cancel_only`, `limit_only`,
`post_only`, `trading_disabled`, `auction_mode`, ... (PROD; Live:
BTC-USD and ETH-USD `quote_increment` "0.01", `base_increment` "0.00000001").

## 5. Sequencing and recovery

- **Scope**: per product; each message is exactly one greater than the one
  before (OV, "Sequence Numbers"). The public `matches`, `ticker` and
  `heartbeat` messages carry a `sequence` too. That it is the same sequence as
  the REST book's is supported by the public check: applying only the matches
  above a book's sequence and discarding the rest gave no wrong prediction of
  the next book (README.md, "Checked").
- **Gap rule** (OV): a jump of more than one means a message was dropped; a
  number lower than the previous "can be ignored or represent a message that has
  arrived out of order". The handler: at or below the last applied, a duplicate
  (ignored); above the next expected, held for up to `reorder_window` (64)
  messages; if the missing one does not arrive, a gap.
- **Sync procedure** (CH, "Full Channel", steps 1-6): subscribe, queue the
  stream, fetch the REST level-3 book, replay the queue "discarding sequence
  numbers before or equal to the snapshot sequence number", apply the rest, then
  the live stream. The handler also requires the first message applied after a
  snapshot at S to be S+1, and the buffer to be contiguous from there; if not,
  the snapshot is too old (or the buffer has a hole) and another is fetched,
  with backoff 2, 4, 8, ... 60 s (rate limits, and the level-3 polling warning).
- **Recovery**: a gap (or a disconnect, or a match on an order not in the book,
  or a match larger than the order) sends `l3_gap_v2_t` to OB, buffers the
  stream, and fetches a snapshot; the snapshot goes to OB as `l3_chr_v2_t`
  followed by recovery adds. There is no retransmission service.
- **Heartbeat**: a heartbeat whose `sequence` is above the last applied is
  counted (`hb_ahead`): the message after the last applied is missing. If the
  stream has not advanced 3 s after the first such heartbeat
  (`heartbeat_gap_ns`), that is a gap and the book is rebuilt; whether a
  heartbeat can overtake messages on the same connection is not documented,
  so a heartbeat alone never triggers a resync.
- **Resets**: none documented. A new connection is a new stream: the book is
  rebuilt from a snapshot.

## 6. Semantics

- Order by order (MBO), the whole book. Only `open` adds an order; `match`
  reduces the maker's size; `done` removes an order on the book; `change`
  changes size or price; "All `open` and `match` messages always result in a
  change to the order book. Not all `done` or `change` messages result in
  changing the order book" (CH, "Full Channel").
- **Queue priority on change**: "If order quantity is increased or order price
  is modified, queue priority is lost. Queue priority is maintained when order
  quantity is decreased" (FIX OrderCancelReplaceRequest,
  https://docs.cdp.coinbase.com/exchange/fix-api/order-entry-messages/order-entry-messages5;
  also exchange/fix-api/best-practices, "Modify Order Requests"). STP changes only
  decrease size. This is OB's rule.
- **Queue order in the snapshot**: BOOK does not say whether orders at one
  price are listed in priority order. Measured (section 9): yes.
- **Trades**: each `match` is one trade, against one maker; `side` is the
  maker's (CH, "Match"). Trades are not also sent as book changes: the maker's
  reduction is implied by the match. Matches always precede the taker's `open`
  (an order that rests after taking).
- **Trading states**: products can be in auction mode (`auction_mode`,
  `auction_state` collection/opening/complete); during collection "orders do not
  match and the book may appear crossed" (BOOK, "Auction Details"). Other
  states: `cancel_only`, `limit_only`, `post_only`, `trading_disabled`,
  `status` (CH, "Status Channel"; PROD).

## 7. Mapping to Kaspar

| Venue message | Kaspar record | Notes |
|---|---|---|
| `open` | `l3_mbo_v2_t`, action 0 | `pxd` = price / quote_increment (ticks); `displayQty` = ceil(size / qty_unit), at most 9,999,999 |
| `done`, order on the book | `l3_mbo_v2_t`, action 2 | |
| `done`, order not on the book | none | counted (`ignored_done`) |
| `change`, order on the book | `l3_mbo_v2_t`, action 1 | price and/or size; OB's priority rule is Coinbase's |
| `change`, order not on the book | none | counted (`ignored_change`) |
| `match` | `l3_mbp_trd_t` (`pxd` trade price, `sz` display units, `aggresorSide` 1 buy / 2 sell = opposite of `side`), then `l3_mbo_trd_v2_t` (maker, `lastQty` = display units before minus after), then action 1 with the size left or action 2 if filled | one event; the maker's later `done` is then ignored. A fill below one display unit sends no `l3_mbo_trd_v2_t` (counted `zero_execs`) |
| `received`, `activate`, `noop`, `heartbeat`, `subscriptions` | none | sequence only |
| REST level-3 snapshot | `l3_mbo_v2_t` action 0, `recovery` true, in the book's order (bids, then asks) | preceded by `l3_chr_v2_t` on every snapshot after the first: OB clears on the first recovery record only when it is not already recovering, which it still is if no live record came since the last recovery (found in testing, `test_cbx_ob.cpp`) |
| gap, disconnect | `l3_gap_v2_t` | |
| product status | `l3_sst_t` | below |
| end of each event | `l3_eop_t` (`msgSeqNum` = low 32 bits of `sequence`), then `l3_eob_t` | |

- **Event boundaries**: Coinbase marks no transactions. Each venue message is
  one event: its records share one `transactTime` (the message's `time`, made
  strictly greater than the previous event's), `endOfEvent` is set on its last
  book record. A match's three records are one event, as in `Mbp2Mbo` and the
  CME handler.
- **Order ids**: `orderID` = the UUID's high 64 bits with the top bit cleared
  (`order_id64`); if two live orders share it, the second gets a synthetic id
  with the top bit set (counted `id_collisions`; 0 seen). The order manager
  reports the same id in `Ack.xordid`.
- **Priority field**: an increasing counter per add, renewed when an order loses
  priority.
- **Trading status** (CME `SecurityTradingStatus` values): `status` not
  `online` → 4 (closed); `trading_disabled` or `cancel_only` → 2 (halted);
  `auction_mode` → 21 (pre-open); otherwise 17 (ready to trade), including
  `limit_only` and `post_only`. Sent from GET /products/{id} at connect, from the
  `status` channel if subscribed, and 17 (or 21 with `auction_mode`) with the first
  snapshot if nothing else was received.
- **Prints and volume**: one `l3_mbp_trd_t` per `match`; OB counts volume from
  prints only under `USEMBP` (frame_kaspr/src/OB.cpp).

## 8. Instruments

- Definitions: GET /products/{id} (PROD) for `quote_increment` and
  `base_increment`; `config/products.ini` holds them, and `cbx_md` checks them
  against the venue at start.
- Symbology: Coinbase product ids (`BTC-USD`). `securityID` = `sec_id` from
  `products.ini`, the Kaspar asset's exchange id.
- Prices: ticks of `quote_increment`, as the other crypto feeds; the asset's
  units are 1. A price that is not a whole number of increments is refused
  (counted `bad_message`), never rounded.
- Quantities: kept exactly in the sync as integers of `base_increment`
  (1e-8). OB holds `displayQty` in units of `qty_unit`: 1e-5 BTC for BTC-USD
  (largest order within 5% of the touch on 2026-11-18: 56.5 BTC; 3.3% of orders
  are below one unit and show as 1), 1e-4 ETH for ETH-USD (largest within 5%:
  743.8 ETH; 1.8% below one unit). OB refuses an order of
  `CHOPIN_MAX_ORD_SZ` (10,000,000) units or more (`frame_ref/.../mda/msg/Data.hpp`,
  `frame_kaspr/.../ob/Order.hpp`), so larger orders (BTC orders of 100 to 3,359
  BTC rest at prices such as 0.01 and 89,000) are sent as 9,999,999 (counted
  `saturated`).
- OB's ring ladder must cover the levels used; orders far from the touch fall
  outside it and are kept in OB's order map only (frame_kaspr/include/frame/ob/act/OB.hpp).
  `cbx_md` uses a ring of 2^20 ticks (10,485.76 USD).

## 9. Pitfalls

**1. Reading the specification**
- Several documents: the WebSocket pages, the REST OpenAPI and the changelog;
  no conflicts found on the L3 rules. The survey's notes agree.
- Errata and changelog: CL records authentication being enforced on `full`,
  `level2`, `level3` (2023-AUG-01) and the `change` fields; both applied.
- Tables across page breaks: not applicable (web pages).
- Offsets, lengths, padding: not applicable (JSON).
- Enumerations elsewhere: `cancel_reason` codes (CH, "Cancel Reason"); not used.
- Names that differ: `remaining_size` (`open`, `done`) versus `size` (`level3`
  arrays); `new_price` versus `price` on the two `change` forms. Both read.
- Examples that contradict tables: the `received` example's `client_oid`
  "d50ec974-76a2-454b-66f135b1ea8c" is not a UUID (32 hex digits in 4 groups);
  three examples share `sequence` 10; the `activate` example has no `sequence`.
  Recorded in the tests.
- Two protocol layers: not applicable.
- Repeated names: `match` and `last_match` (same fields).
- Variable-length fields: not applicable.
- Odd-width integers: not applicable.

**2. Framing and encoding**
- Endianness, signedness: not applicable.
- Fixed-point: decimal strings; the product's increments give the scale.
  Parsed exactly (`coinbase_exchange/Num.hpp`).
- Nulls: `price` is `null` on an STP change of a market order (CH).
- Strings: UUIDs lowercase in every message seen.
- Several messages per frame: never seen; the handler would see one JSON value
  per frame and does not split.
- Timestamps: ISO 8601 UTC, 6 to 9 fraction digits; `activate` uses epoch
  seconds.

**3. Sequencing and recovery**
- Scope: per product, shared by every channel of the product (OV; Live). Unlike
  Advanced Trade, whose `sequence_num` counts per connection.
- A/B arbitration: not applicable.
- Gap rule: section 5. Out-of-order delivery is documented (OV); handled by the
  reorder window.
- Recovery: REST level-3 snapshot only.
- Snapshot alignment: discard `<=` S (CH); the first applied must be S+1.
- Snapshot too old: refetched with backoff.
- State to clear: the order map, id set and level sums on every snapshot; the
  stream buffer and the `level3` schema on a disconnect (#122's lesson).
- Recovery paths exercised: gap, out-of-order, duplicate, too-old snapshot,
  disconnect, unknown maker, overfill (unit tests); real resyncs not seen live
  (the `full` channel was not available).
- Session behaviour: subscribe within 5 s; no logon.

**4. Book semantics**
- MBO, whole book.
- Level-index updates: not applicable.
- Depth limits: none; the REST book is the whole book.
- Priority on modify: documented, same as OB (section 6).
- Implied, iceberg, hidden: icebergs were removed on 2025-05-27 (CL); hidden
  orders not documented.
- Trades also as book deltas: no; the match is the only record of the maker's
  reduction.
- Trade side: the maker's (CH). Inverted for `aggresorSide`.
- Trading states: auction mode can cross the book (BOOK); mapped to 21.
- Book clears: none documented.

**5. Prices, quantities, instruments**
- Price or yield: price only.
- Quantity multipliers: none; `qty_unit` chosen per product (section 8).
- Sentinels: none seen.
- Price-dependent ticks: no; `quote_increment` per product.
- Definitions: REST (PROD).
- Lifecycle: `status` (`online`, `offline`, `delisted`) (CH, "Status Channel").

**6. Behaviour the specification omits** (probed)
- Update rate: `full`, `level3` and `level2` are real time, `level2_batch` 50 ms
  (CH). The REST level-3 book is served with `cache-control: public, max-age=1`
  through Cloudflare, so two fetches within a second can return the same book
  (Live); the verifier skips a snapshot that is not newer.
- History on subscribe: `matches` sends one `last_match` first (CH; Live).
- Whole-book snapshots: the REST level-3 book of BTC-USD is about 6 MB, 100,000
  orders, about 3 s to fetch (Live).
- Order within an event: matches before the taker's `open` (CH, "Received").
- Connection limits: subscribe within 5 s (OV). No forced 24-hour disconnect
  documented; none seen in 32-minute runs.

**7, 8. Post-trade, order entry**: not applicable here (see
`../../post_trade/coinbase_exchange/feed.md`, `../../order_entry/coinbase_exchange/feed.md`).

**Measured on the public REST level-3 books** (`cbx_md replay` of the
42-minute recording; README.md, "Checked"): orders present in two consecutive
snapshots (60 s apart) keep their relative order at their price level in all
but 1 of 1,518,740 level comparisons on BTC-USD and 1 of 764,232 on ETH-USD,
and no order new since the earlier snapshot was listed before an older one
(1,273 and 2,666 levels with new orders); in the 32-minute live run, 10 of
1,151,103 and 2 of 579,050, and 1 new-before-old on each product. A modify
that increases an order's size moves it to the back, which shows as exactly
such a violation; the public data cannot tell which these were. The
snapshot's order is taken as queue priority.

## 10. Ambiguities

| Question | Reading taken | Alternatives | Resolved by |
|---|---|---|---|
| Is the REST level-3 list at one price in queue priority? | Yes; recovery adds go to OB in that order | Arbitrary order | Measured: at most 10 violations in 1,151,103 level comparisons (section 9), each explainable by a size-up modify. Not documented. |
| Does a same-size, same-price `modify_order` lose priority? | It changes nothing and is not sent | Coinbase may requeue it | Open: needs the authenticated `full` channel |
| May a heartbeat overtake messages on the same connection? | Not assumed: counted, and a gap only if the stream stays behind it for 3 s | Treat `heartbeat.sequence > last` as a gap at once | Open |
| Does `done` follow a `match` that filled the maker? | Yes (CH: "done ... for all orders for which there was a received message"); the delete is sent on the match and the `done` ignored | Wait for `done` to delete | Documentation; checked live only on the public `matches` data (orders the matches filled were never in the next snapshot: 0 of 7,754 on BTC-USD and 0 of 959 on ETH-USD, both runs) |
| Polling the REST level-3 book "can cause your access to be limited" (BOOK). How often is too often? | Snapshots only to (re)synchronise, with backoff; the public check fetched one per product per 60 s | Every 10 s, as crypto.md asks for the comparison | **Owner decision**: the level-3 comparison interval (section "Checked" of README.md used 60 s) |
| `displayQty` unit: OB refuses 10,000,000 units or more, so a unit fine enough for small orders (1e-8) cannot hold large ones | 1e-5 BTC, 1e-4 ETH; ceil; saturate above the cap | Other units per product | **Owner decision** (config/products.ini) |
| FIX market data (L3 over FIX with `RptSeq`, one connection per key) instead of WebSocket | WebSocket `full`/`level3`, as asked | FIX MD, which has snapshots on port 6121 and a documented per-symbol `RptSeq` | **Owner decision**, after sandbox tests of both |
| `ws-direct` or `ws-feed` for the signed channels | `ws-direct` (lower latency per OV), `ws-feed` as configurable fallback | | Owner, after the sandbox run |
| Trading status when `status` is `online` but `post_only` | 17: nothing crosses in post-only mode | 21 | Reading of CH; open |
