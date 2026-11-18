# Feed description: Bybit V5 executions (post-trade)

Not certified. Sources: Bybit V5 documentation as fetched on 2026-11-18, cited by
page path. Nothing here has been checked against Bybit: no testnet key was supplied.

## 1. Venue and documents

| Document | Applies to |
|---|---|
| `v5/websocket/private/execution` | live executions, topic `execution` |
| `v5/websocket/private/fast-execution` | `execution.fast` (fewer fields, Trade only; not used) |
| `v5/order/execution` (Get Trade History) | `GET /v5/execution/list`, reconciliation |
| `v5/enum` (`execType`) | which executions are trades |
| `v5/ws/connect`, `v5/guide` | hosts, signing (shared with `order_entry/bybit`) |
| `v5/rate-limit` | request limits |

Bybit has no FIX drop copy or trade-capture session in V5 (FIX order entry is
spot only and by whitelist, `fix-api/guide`). The post-trade feed is the private
stream plus REST history.

## 2. Transport

Private stream `wss://stream-testnet.bybit.com/v5/private` (demo:
`stream-demo.bybit.com`); REST `https://api-testnet.bybit.com` (demo:
`api-demo.bybit.com`). Authentication, ping and the host guard are those of
`order_entry/bybit/feed.md` sections 2 to 4. The client subscribes to `execution`
(all categories); `execution.spot` and `execution.linear` cannot be mixed with it in
one request.

## 3. Subscription and replay

- The stream has no replay and no sequence for gap detection (`seq` is a cross
  sequence shared by simultaneous fills and across symbols).
- Recovery is `GET /v5/execution/list?category=&startTime=&endTime=&limit=100&cursor=`:
  at most 7 days per request, two years back, newest first, cursor paging. The
  `nextPageCursor` comes back URL-encoded and is sent back unchanged, so the signed
  query string is the one sent. A REST element has no `category` field: the
  client takes the request's.
- The client reconciles each configured category after every login, from the start
  of the UTC day on the first login (or `since_ms`), then from the end of the last
  successful window less `margin_s` (60 s); and every `reconcile_every_s` (300 s).
  A failed page leaves the window start where it was, so the next pass retries it.
  Pages are 250 ms apart (4 requests a second, under Bybit's per-UID limits).

## 4. Report lifecycle

| `execType` | Meaning (`v5/enum`) | Captured |
|---|---|---|
| Trade | an execution of an order | New |
| AdlTrade | auto-deleveraging | New |
| BustTrade | takeover liquidation (not a trade bust) | New |
| Delivery | futures delivery, delisting close | New |
| BlockTrade, MovePosition, FutureSpread | position transfers and spread legs | New |
| Funding, Settle | cash | not captured, logged |
| UNKNOWN, anything else | | not captured, logged |

Bybit V5 sends no amendment, cancellation or bust of an execution, so the client
emits only `Event::New`. The stable identifier is `execId`: the stream copy and the
REST copy of one execution carry the same `execId`.

## 5. Mapping to `post_trade::msg::TradeCapture`

| Field | From |
|---|---|
| `venue` | `category`: spot `BYBIT`, linear `BYBITF` |
| `trade_id`, `report_id` | `execId` |
| `report_seq` | 0 (arrival order) |
| `event` | New |
| `venue_order_id` | `orderId` |
| `symbol`, `asset` | `symbol`; asset from the config by (category, symbol) |
| `side` | `side` Buy / Sell (the firm's side) |
| `qty` | `execQty` / `qty_unit`; not a whole number of units: not captured, error |
| `price_mantissa`, `price_decimals` | `execPrice` as sent |
| `trade_date`, `trade_time_ns` | `execTime` (ms) in UTC |
| `account` | `orderLinkId` (the OM's `k<date>-<client id>`; empty for orders placed elsewhere) |
| `poss_dup` | true for a REST copy |
| `raw` | the execution element |
| `settle_date`, `firm`, `counterparty`, `yield` | not given |

Executions in instruments not in the config are not captured and are logged as
errors ("CHECK POSITIONS"). Fees (`execFee`, `feeCurrency`) are in `raw` only.

## 6. Duplicates and days

The client applies every execution to a `post_trade::TradeLedger`; a second copy
(stream then REST, or overlapping REST windows) is a `Duplicate` and is not sent.
One ledger holds one UTC day; when an execution of a later day arrives, the ledger
becomes the previous day's and a new one starts, so a late copy of yesterday's
execution is still recognised. Executions older than both ledgers are logged and
dropped.

## 7. Pitfalls (post-trade)

| Item | How it applies |
|---|---|
| FIX version, dictionary, credentials in Logon, Reject from a missing field | Not applicable: JSON over WebSocket and REST |
| Requests before a session-status message | Not applicable |
| Subscription and replay from start of day | REST reconciliation from the UTC day start |
| Lifecycle and stable identifier | New only; `execId` |
| Duplicates on reconnect | `execId` in the ledger |
| Price vs yield, accrued, settlement | Not applicable |
| Daily sequence reset | No sequence; ledgers by UTC day |

## 8. Ambiguities

| Question | Reading taken | Resolved by |
|---|---|---|
| Is `execId` the same on the stream and in REST? | Yes | Documentation names the same field; not observed |
| Does REST include executions that the stream delivered seconds earlier? | Yes, with a short delay | Not observed; the 60 s margin covers a delay up to that |
| Should liquidation (BustTrade) and ADL executions reach position keeping? | Yes: they change the position | Documentation of `execType` |
