# Feed description: Kraken spot public market data (WebSocket v2 `book`, `trade`, `level3`)

Every entry cites its source. Pages are on docs.kraken.com, read 2026-11-18; the
paths below are relative to `https://docs.kraken.com/`. Items marked **live** were
settled on the live public stream (README.md, "Step 2").

## 1. Venue and documents

| Document | Version | Date | Applies to |
|---|---|---|---|
| `exchange/api-reference/spot-websocket-v2/book` | WebSocket v2 | read 2026-11-18 | price-level book channel |
| `exchange/guides/websockets/book-checksum-v2` | v2 | read 2026-11-18 | book maintenance, CRC32 |
| `exchange/api-reference/spot-websocket-v2/trade` | v2 | read 2026-11-18 | public trades |
| `exchange/api-reference/spot-websocket-v2/level3` | v2 | read 2026-11-18 | order-by-order channel (authenticated) |
| `exchange/guides/websockets/l3-checksum-v2` | v2 | read 2026-11-18 | level3 maintenance, CRC32 with queue order |
| `exchange/guides/general/l3-data` | | read 2026-11-18 | what level3 omits |
| `exchange/guides/general/amends` | | read 2026-11-18 | priority on amend |
| `exchange/guides/websockets/introduction` | | read 2026-11-18 | endpoints, idle timeout, connection rate limit |
| `api-reference/market-data/get-order-book` (REST `Depth`) | REST 0 | read 2026-11-18 | snapshots for the live comparison only |

WebSocket v1 (`wss://ws.kraken.com`, no path) is legacy and not used.

## 2. Transport

- `wss://ws.kraken.com/v2` for `book` and `trade`, no account; `wss://ws-l3.kraken.com/v2`
  for `level3`, with a token from REST `GetWebSocketsToken`, so an API key
  (`level3` page, "Endpoint"; `websockets/introduction`).
- TLS with SNI required (`websockets/introduction`, "General Considerations").
- The server closes a connection after about one minute without traffic; any
  request, for example `{"method":"ping"}`, keeps it open (`websockets/introduction`,
  "Connection times out"). With `book` subscribed Kraken sends a `heartbeat`
  message about once a second (**live**: 59 in the first minute), so the feed
  sends nothing itself; Beast's WebSocket keep-alive pings at half the idle timeout.
- Cloudflare limits connection attempts to about 150 per rolling 10 minutes per IP
  (`websockets/introduction`). The feed waits 1 s before each reconnect.
- `level3` subscriptions are rate-counted by depth (200/s standard, 500/s pro) and
  one depth per symbol (`level3` page, "Rate limits").

## 3. Framing

One JSON object per WebSocket text frame (**live**: no frame contained a newline).
Prices and quantities are JSON numbers whose text carries the precision
(`"qty":0.00006120`); the checksum is computed over that text, so frames are parsed
with `kraken::parse_raw`, which keeps every non-integer number as its text
(`book-checksum-v2`, the tip on decimal parsing).

## 4. Message catalogue

### `book` snapshot and update (channel `book`)

| Field | Type | Meaning |
|---|---|---|
| `channel` | string | `book` |
| `type` | string | `snapshot` (once, after subscribe) or `update` |
| `data[0].symbol` | string | `BTC/USD` |
| `data[0].bids`, `asks` | array of `{price, qty}` | levels; in an update, only the levels that changed |
| `data[0].checksum` | uint32 | CRC32 of the top 10 levels after this message |
| `data[0].timestamp` | RFC 3339 | update time (updates only) |

Subscribe: `{"method":"subscribe","params":{"channel":"book","symbol":["BTC/USD"],"depth":1000}}`;
depth 10, 25, 100, 500 or 1000 (`book` page, "depth").

### `trade` update (channel `trade`)

`data[]` of `{symbol, side, price, qty, ord_type, trade_id, timestamp}`. `side` is
the taker's (`trade` page: "the side of the taker"). Subscribed with
`"snapshot": false`: the snapshot replays the last 50 trades, which would print
twice after a reconnect.

### `level3` snapshot and update (channel `level3`)

| Field | Meaning |
|---|---|
| `data[0].bids/asks[]` | `{order_id, limit_price, order_qty, timestamp}`; in an update also `event`: `add`, `modify`, `delete` |
| `data[0].checksum` | CRC32 of the top 10 levels, order by order in queue order |

### REST `GET /0/public/Depth?pair=XBTUSD&count=100`

`result.XXBTZUSD.bids/asks` of `[price, volume, timestamp]` strings. Quantities have
3 decimals for XBTUSD against the stream's 8, rounded up (**live**: `0.0000612` on
the stream is `"0.001"` in REST). No sequence number.

## 5. Sequencing and recovery

- **Book build:** a WebSocket snapshot on subscribe, then updates on the same
  connection. No REST snapshot and no alignment rule (`book-checksum-v2`,
  "Maintaining the book").
- **Sequence numbers:** none on `book`, `trade` or `level3` (`book` and `level3`
  pages list none; the `level3` page says no sequencing is required). The
  `l3-data` guide lists "sequence numbers for synchronization" as a feature; the
  channel reference has no such field (section 10).
- **Integrity:** the CRC32 `checksum` in every book message, over the top 10 levels
  after all levels of the message are applied and the book is truncated to the
  subscribed depth. A mismatch means the local book is wrong: the feed reconnects,
  which brings a new snapshot.
- **Checksum string** (`book-checksum-v2`, "Algorithm"): for each of the top 10
  asks, lowest first, then each of the top 10 bids, highest first: price text with
  `.` removed and leading zeros removed, then quantity text the same way;
  concatenated asks then bids; CRC32 (zlib), unsigned 32-bit. `level3` is the same
  over every order of each of the top 10 levels in queue order (`l3-checksum-v2`).
- **Trades:** `trade_id` is per symbol and increments by 1 (**live**: 0 gaps in
  1 942 trades in 32 minutes); a gap is counted and logged, not recovered.
- **Reset:** a new connection waits for its snapshot; the snapshot replaces the
  book, and `Mbp2Mbo` sends it to OB as recovery records.

## 6. Semantics

- **Absolute sizes:** an update gives the new total quantity of a level; `qty` 0
  removes it (`book-checksum-v2`).
- **Several updates of one level in one message** are applied in order (**live**:
  seen; the checksum only matches when applied in order).
- **Truncation:** Kraken sends no `qty` 0 for a level pushed out of the subscribed
  depth; the client truncates after each message (`book-checksum-v2`). **Live**:
  126 153 levels truncated in 32 minutes.
- **Depth chosen:** 1000, the deepest public depth.
- **Update rate:** no option; updates arrive as they happen (**live**: about 186 book
  messages per second on BTC/USD).
- **level3:** `add` joins the back of the level; `modify` changes the quantity
  (a quantity decrease keeps priority, an increase or a price change goes to the
  back: `guides/general/amends`); `delete` removes. Hidden iceberg quantity,
  untriggered stops and in-flight orders are not shown (`l3-data`).
- **Trading status:** the `instrument` channel carries pair status; it is not
  subscribed (section 10).

## 7. Mapping to Kaspar

By the rules in `kaspar_mapping.md`.

| Venue message | Kaspar record | Notes |
|---|---|---|
| `book` snapshot | Mbp2Mbo snapshot: recovery adds of chunk orders | through LevelWindow (the best `window` levels, 1000 by default) |
| `book` update | Mbp2Mbo level changes: chunk adds at the back, deletes from the back | absolute target `ceil(qty / chunk)` |
| `trade` | Mbp2Mbo trade: `l3_mbp_trd_t`, then `l3_mbo_trd_v2_t` and delete per executed chunk from the front | aggressor = `side`; the resting side is the other |
| end of each WebSocket message | `endOfEvent` on the last record, then `l3_eob_t` | one message is one event |
| `level3` snapshot | `l3_mbo_t` recovery adds in queue order | KrakenL3Sync |
| `level3` add / modify / delete | `l3_mbo_t` action 0 / 1 / 2 | a modify up or with a new price is sent as delete + add (back of queue) |
| `trade` with `level3` | `l3_mbp_trd_t`, and `l3_mbo_trd_v2_t` before the matching modify-down or delete | section 10 |

- **Priority:** price-level feed: Mbp2Mbo's rule (growth at the back, shrinkage from
  the back, trades from the front). level3: Kraken's amend rule, which is OB's.
- **Identifiers:** Mbp2Mbo numbers its chunks. level3 order ids are 17-character
  text: each live order gets a 64-bit id from a counter; the map is cleared on
  snapshot and reset.
- **Quantity unit:** price-level: one chunk (0.01 BTC by default; `displayQty` 1).
  level3: `ceil(qty / 1e-8)`.
- **Prices:** ticks of 0.1 USD for BTC/USD (`pxd = price / tick`).
- **Venue code:** `en::x::KRAKEN`.

## 8. Instruments

From REST `AssetPairs`: BTC/USD (`XXBTZUSD`, altname `XBTUSD`, wsname `BTC/USD`),
`pair_decimals` 1 (tick 0.1), `lot_decimals` 8. The tick and the chunk are set per
symbol in the feed's ini section (`KrakenFeed::config_from_ini`).

## 9. Pitfalls

One line per item of `pitfalls.md`.

1. Reading the specification: the docs are web pages with OpenAPI blocks; each
   example used in a test is cited in the test.
2. Framing: JSON; decimals as numbers whose text matters (section 3).
3. Sequencing: no sequence numbers; the checksum is the only book integrity check
   (section 5).
4. Book semantics: absolute sizes, zero removes, truncation without deletes,
   several updates of one level per message (section 6).
5. Prices and quantities: exact fixed point with 10 decimals (`kraken::to_fixed`);
   no binary floating point before the checksum.
6. Behaviour the specification omits: REST Depth rounds quantities up to 3
   decimals and lags the stream by up to 4.6 s (median 1.4 s) (**live**, README.md).
7. Post-trade and 8. order entry: in `../../post_trade/kraken` and
   `../../order_entry/kraken`.

## 10. Ambiguities

| Question | Reading taken | Alternatives | Resolved by |
|---|---|---|---|
| Does `level3` carry sequence numbers? | No: the channel reference has no field; the guide's mention is taken as wrong | a field exists but is undocumented | not resolved: level3 not checked live (no key) |
| Book update before the snapshot | ignored (cannot happen on v2) | treat as an error | live: never seen |
| Trade and level3 book change ordering | a trade leaves an execution budget at (side, price) for 1 s; a modify-down or delete there consumes it and becomes an execution; a book change before its trade is a cancel | match on order id (not given) | not resolved: level3 not checked live |
| Trading status | not mapped; the `instrument` channel is not subscribed | subscribe `instrument` and map `status` | owner's decision (review.md) |
| REST Depth for comparison | compared by price and by quantity rounded up to its 3 decimals, against stream states in the 15 s before | none with a sequence number | live |
