# Feed description: Kraken order entry (spot WebSocket v2; Kraken Futures REST v3 demo)

Every entry cites its source on docs.kraken.com (read 2026-11-18; paths relative
to `https://docs.kraken.com/`). Nothing here was checked against Kraken itself:
no keys were supplied (README.md).

Two order APIs are covered, because Kraken has no spot test environment
(`exchange/guides/fix/introduction`; `exchange/guides/futures/introduction`):

| Product | API | Environment | Status |
|---|---|---|---|
| spot | WebSocket v2 `add_order`, `cancel_order`, `executions` | production only | built and unit-tested; never run |
| futures | REST v3 `sendorder`, `cancelorder`, `fills`, `orders/status` | `demo-futures.kraken.com` | built and tested against a fake; runs on the demo when keys exist |

## 1. Venue and documents

| Document | Applies to |
|---|---|
| `exchange/guides/rest/authentication` | spot REST signing (API-Sign), with a worked example |
| `api-reference/websocket-authentication/get-websockets-token` | spot WebSocket token |
| `exchange/guides/websockets/authentication` | spot WebSocket authentication |
| `exchange/api-reference/spot-websocket-v2/add_order`, `cancel_order`, `cancel_after`, `executions` | spot order messages |
| `exchange/guides/general/clordid`, `amends`, `ratelimits` | client ids, amend priority, spot rate counter |
| `exchange/guides/futures/introduction`, `futures/rest`, `futures/ratelimits`, `futures/websockets` | futures hosts, Authent, cost budget, challenge signing |
| `api-reference/order-management/send-order`, `cancel-order`, `get-specific-orders-status`, `dead-mans-switch` | futures order endpoints |
| `api-reference/historical-data/get-your-fills` | futures fills |

## 2. Transport and authentication

**Spot.** REST `https://api.kraken.com`; WebSocket `wss://ws-auth.kraken.com/v2`.
1. `POST /0/private/GetWebSocketsToken`, body `nonce=<ms>`, headers `API-Key` and
   `API-Sign = base64(HMAC-SHA512(base64dec(secret), uri_path + SHA256(nonce + body)))`
   (`rest/authentication`; the documented example is a unit test).
2. Connect the WebSocket and send every request with `token`. The token must be
   used within 15 minutes; it stays valid while the connection is open
   (`get-websockets-token`).
3. Nonce: strictly increasing per key (`rest/authentication`); milliseconds,
   bumped by one when two requests share a millisecond.

**Futures.** REST `https://demo-futures.kraken.com/derivatives/api/v3/` (demo) or
`https://futures.kraken.com/...` (production) (`futures/introduction`).
Headers `APIKey`, `Nonce` (optional), `Authent = base64(HMAC-SHA512(base64dec(secret),
SHA256(postData + nonce + endpointPath)))` where `endpointPath` is `/api/v3/<name>`
without `/derivatives`, and `postData` is the url-encoded form as sent
(`futures/rest`). The page gives no complete Authent example (its example secret is
87 characters, not valid base64); the unit test checks Authent against an
independent Python computation and checks the WebSocket challenge signature against
the documented example (`futures/websockets`, "Sign challenge").

**Environment guard.** `testnet = true` accepts only `demo-futures.kraken.com`;
`product = spot` with `testnet = true` is refused (no spot testnet). The check runs
when the configuration is read and again before every futures request
(`KrakenConfig.hpp`).

## 3. Logon and session

- Spot: logged on when the `executions` subscription (with `snap_orders` and
  `snap_trades`) is acknowledged. The snapshot gives open orders and the last 50
  trades. `executions` messages carry `sequence`; a gap ends the session and the
  next one starts from a fresh snapshot (`executions` page).
- Futures: no session; logged on when an authenticated `GET /fills` succeeds. An
  `authenticationError` or HTTP 401/403 is a logon failure (not retried).
- Keep-alive: spot sends `{"method":"ping"}` after 20 s without a frame
  (`websockets/introduction`: idle connections close after about a minute).
- Cancel on disconnect: spot `cancel_all_orders_after` and futures
  `cancelallordersafter`, renewed every timeout/3 when `cancel_after_s` > 0.
  FIX cancel-on-disconnect is not used.

## 4. Order identifiers

- Client id: BrokerOrderBook's 64-bit id in base 36 (at most 13 characters)
  after `clid_prefix`. Spot: free text of at most 18 ASCII characters, unique among
  open orders (`guides/general/clordid`). Futures `cliOrdId`: at most 100
  characters, "globally unique" (`send-order`): ClientIdGen ids do not repeat
  across restarts on the same day; across days they could, so the futures example
  configuration should set a date `clid_prefix` (README.md).
- Venue id: spot `OTCFZG-YOE2Q-LQKNM3`; futures a UUID. Text; `Ack.xordid` is its
  64-bit FNV-1a hash.
- Cancel uses the venue id once known, otherwise the client id (both APIs accept
  either). Amend is not used (cancel/replace is refused locally).

## 5. New order

| SOM | Spot `add_order` | Futures `sendorder` |
|---|---|---|
| LIMIT, GTC | `limit`, `gtc` | `lmt` |
| LIMIT, IOC | `limit`, `ioc` | `ioc` |
| LIMIT, FOK | `limit`, `fok` | `fok` |
| LIMIT, DAY | GTC when `day_as_gtc` (default), else refused | same |
| MARKET | `market` | `mkt` (IOC with 1% price protection) |
| post-only (config) | `post_only: true` | `post` |
| STOP, STOP_LIMIT, AT_OPEN, AT_CLOSE, EXTENDED_DAY, short-sale marks, algorithms, routes other than KRAKEN, cancel/replace | refused locally | refused locally |

Quantity: SOM units times `qty_unit` (default 1e-8 BTC), sent as exact decimal
text; price: SOM ticks times the asset's tick. Spot sends them as JSON numbers
with that text. Local limits: `max_order_units`, `max_order_notional`,
`max_orders_per_sec`.

## 6. Reports

| Venue | Report | Book call → SOM |
|---|---|---|
| spot `add_order` success / futures `sendorder` status `placed`, `partiallyFilled`, `filled` | Accepted | `on_accepted` → `Ack` |
| spot `add_order` error / futures any other `sendorder` status, `result: error` | Rejected | `on_rejected` → `Reject` (`Insufficient` → EXCHLIMIT; rate limit, `apiLimitExceeded` → THROTTLE) |
| spot `executions` `exec_type: trade` / futures `GET /fills` row | Trade | `on_fill` → `Fill`, once per `exec_id` / `fill_id` |
| spot `exec_type: canceled`, `expired` / futures `cancelorder` `cancelled`, `orders/status` `CANCELLED` | Canceled | `on_canceled` → `CancAck` |
| spot `cancel_order` error / futures `cancelorder` `filled`, `notFound`, error | CancelRejected | `on_cancel_rejected` → `CancReject` |
| spot `exec_type: filled` / futures `FULLY_EXECUTED` | Filled | checked against the book; a missing fill is logged |
| spot `amended`, `restated` | Restated | logged |
| futures `orders/status` `REJECTED` | Rejected | `on_rejected` |

- **Futures, one source per fact:** executions only from `GET /fills` (the
  `EXECUTION` events inside a `sendorder` answer are not used, so a fill cannot be
  counted twice under two ids); terminal states from `orders/status`, which returns
  open orders and those that ended in the last 5 s (`get-specific-orders-status`).
  An order absent from it for 3 polls is reported as cancelled (or rejected if it
  never got a venue id); its executions are those already read from `/fills`.
- **Fill before ack, fill racing a cancel, fill after the cancel ack:**
  BrokerOrderBook counts them (tests).
- **Reports naming only the venue id** (spot executions after the first carry
  `order_id` only) are held until a report pairs it with the client id.
- **Busts and corrections:** none in these APIs.

## 7. Throttles

- Spot: a per-pair rate counter shared by REST, WebSocket and FIX
  (`guides/general/ratelimits`). Rejections with "Rate limit" map to THROTTLE.
- Futures: 500 cost units per 10 s; `sendorder` and `cancelorder` 10,
  `fills` 2, `orders/status` 1, `cancelallordersafter` 25 (`futures/ratelimits`).
  Polling every second spends 30 per 10 s.
- The session queue holds 64 requests; beyond that the OM is told it is busy and
  rejects with THROTTLE until it drains. A new order is never resent after a
  timeout.

## 8. Reconnect and replay

- Spot: the new subscription's snapshot has open orders (Accepted, not re-acked)
  and the last 50 trades (skipped by `exec_id` if applied). A fill older than the
  last 50 trades would be missed: the post-trade client's REST reconciliation
  covers it.
- Futures: the factory's tracker survives the session, so a new session goes on
  polling the same orders and reports each `fill_id` once; fills before the
  process started are history and not reported.

## 9. Risk checks

The venue enforces margin and position limits (`insufficientAvailableFunds`,
`maxPositionViolation`). The OM enforces size, notional and order rate limits
before sending.

## 10. Ambiguities

| Question | Reading taken | Resolved by |
|---|---|---|
| Futures: is `executionId` in `sendorder` the `fill_id` in `/fills`? | not relied on: executions come only from `/fills` | design |
| Futures `cliOrdId` uniqueness "globally" | prefix with the date in production configs | not resolved (no keys) |
| Spot: does every `executions` message carry `sequence`? | gap-check when present | not resolved (production only) |
| Futures `/fills` page size | 100 newest without `lastFillTime` (`get-your-fills`) | documentation |
