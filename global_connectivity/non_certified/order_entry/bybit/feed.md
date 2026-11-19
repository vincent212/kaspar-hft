# Feed description: Bybit V5 order entry (spot and linear perpetuals)

Not certified. Sources: Bybit V5 documentation as fetched on 2026-11-18
(`bybit-exchange.github.io/docs`, changelog head 2026-11-17), cited by page path.
Nothing here has been checked against Bybit: no testnet key was supplied.

## 1. Venue and documents

| Document | Applies to |
|---|---|
| `v5/guide` (Integration Guidance, "Create A Request") | hosts, HMAC signing, timestamp rule |
| `v5/ws/connect` (WebSocket Connect) | private stream auth, ping, `max_active_time` |
| `v5/order/create-order`, `cancel-order`, `amend-order`, `open-order`, `order-list` | requests and order queries |
| `v5/order/execution` | `GET /v5/execution/list` |
| `v5/websocket/private/order`, `.../execution` | reports |
| `v5/enum` | `orderStatus`, `cancelType`, `execType`, `rejectReason` |
| `v5/error` | `retCode` values |
| `v5/rate-limit` | limits |
| `v5/demo` | demo trading hosts and restrictions |
| `v5/websocket/trade/guideline` | WebSocket order entry (not used) |
| `fix-api/guide` | FIX 4.4, spot only, whitelist, RSA (not used) |

## 2. Transport

| Environment | REST | Private stream | Order entry used |
|---|---|---|---|
| Testnet (default) | `https://api-testnet.bybit.com` | `wss://stream-testnet.bybit.com/v5/private` | REST |
| Demo trading | `https://api-demo.bybit.com` | `wss://stream-demo.bybit.com/v5/private` | REST (demo has no WebSocket trade, `v5/demo`) |
| Production | `api.bybit.com`, `stream.bybit.com` | | refused while `testnet = true` |

The OM uses REST for requests because both test environments accept it (demo has no
WebSocket trade API). With `testnet = true` the host guard
(`BybitPrivate.hpp: guard`) accepts only the four hosts above; any other host makes
the factory return `nullptr` and the session constructor throw, before any
connection.

## 3. Authentication

- REST (`v5/guide`): `X-BAPI-API-KEY`, `X-BAPI-TIMESTAMP` (ms),
  `X-BAPI-RECV-WINDOW` (5000), `X-BAPI-SIGN` = lower-case hex
  HMAC-SHA256(secret, timestamp + api_key + recv_window + payload), where payload is
  the query string (GET) or the JSON body (POST) exactly as sent. Accepted when
  `server_time - recv_window <= timestamp < server_time + 1000`; error 10002
  otherwise, 10004 for a bad signature.
- Private stream (`v5/ws/connect`): `{"op":"auth","args":[api_key, expires_ms, sign]}`,
  sign = HMAC-SHA256(secret, "GET/realtime" + expires). The OM uses expires = now +
  10 s.
- Keys: `~/.kaspar/keys/bybit_testnet.json`, `{"api_key": "...", "api_secret": "..."}`.
  Never logged.
- RSA keys are supported by Bybit and not by this OM.

## 4. Logon and session

There is no session layer: each REST request stands alone. "Logged in" means the
private stream authenticated and its `order` and `execution` subscriptions were
acknowledged. There are no sequence numbers and no replay. Bybit cuts a connection
with no ping for 10 minutes; the stream writes `{"op":"ping"}` every 20 s from a
timer while it waits for data (checked live on a public connection: pongs every
3 s at a 3 s setting). Cancel-on-disconnect (DCP) is for institutional accounts
only (`v5/order/dcp`) and is not used: orders survive a disconnect.

## 5. Order identifiers

- `orderLinkId`: at most 36 characters of `[0-9A-Za-z_-]`, must be unique
  (`v5/order/create-order`); the uniqueness window is not stated.
- The OM sends `k<YYYYMMDD>-<client id>` (UTC date; client id from
  `BrokerOrderBook`'s `ClientIdGen`, clock-based so a restart does not reuse one).
- Cancel names the order by `orderLinkId`. Reports carry `orderId` and
  `orderLinkId`. `Ack.xordid` is `orderId` when it is decimal (spot), else its
  FNV-1a 64-bit hash (linear `orderId`s are UUIDs).

## 6. New order

| SOM field | Bybit | Supported |
|---|---|---|
| side BUY / SEL | `side` Buy / Sell | yes |
| LIMIT | `orderType` Limit, `price` | yes |
| MARKET | `orderType` Market | linear only; a spot market buy is refused (its `qty` is in quote currency by default) |
| STOP, STOP_LIMIT | conditional orders | refused locally |
| DAY | none at Bybit | sent as GTC when `day_as_gtc`, else refused |
| GTC, IOC, FOK | `timeInForce` | yes |
| strategy "POSTONLY" | `timeInForce` PostOnly | yes |
| AT_OPEN, AT_CLOSE, EXTENDED_DAY, short-sale flags, other strategies | | refused locally |
| cancel/replace (amend) | `POST /v5/order/amend` | not implemented: refused |
| linear position mode | `positionIdx` 0 (one-way) | hedge mode not supported |

`qty` and `price` are sent as decimal strings with the instrument's step decimals.
Local limits: `max_order_qty`, `max_order_notional`, `max_orders_per_sec`
(token bucket; refused with `THROTTLE`).

## 7. Reports

| Bybit | Book call | SOM |
|---|---|---|
| REST create `retCode != 0` | `on_rejected` | `Reject` (10006 -> `THROTTLE`) |
| REST create, no answer | none; `GET /v5/order/realtime` by `orderLinkId` | never resent |
| `order` New / PartiallyFilled / Untriggered | `on_accepted` | `Ack`, once |
| `execution` execType Trade | `on_fill` | `Fill`, once per `execId` |
| `order` Filled | `on_accepted` if no Ack yet | (fills come from executions) |
| `order` Cancelled / PartiallyFilledCanceled / Deactivated | `on_canceled` after fills up to `cumExecQty` have arrived (else fetched from `/v5/execution/list` first) | `CancAck` |
| `order` Cancelled, never acked, nothing filled, `rejectReason` set | `on_rejected` | `Reject` (PostOnly that would cross) |
| `order` Rejected | `on_rejected` | `Reject` |
| REST cancel `retCode != 0` | `on_cancel_rejected` | `CancReject` |
| `execution` AdlTrade, BustTrade (takeover liquidation), Delivery, BlockTrade, MovePosition | none | logged as error: the position changed outside the SOM |
| `execution` Funding, Settle | none | logged |
| reports for `orderLinkId`s the OM did not make | none | logged; a Trade execution as an error |

Bybit V5 has no bust or correction of an execution. Executions of one order can
arrive before its `order` update; a fill before the ack produces the Ack first.

## 8. Throttles

`v5/rate-limit`: HTTP 600 requests per 5 s per IP; per UID, create and cancel 10 to
20 per second by product and account level; error 10006 when exceeded. The OM
throttles to `max_orders_per_sec` (default 10) and treats 10006 as busy until the
queue drains.

## 9. Reconnect and replay

The private stream has no replay. After every login the OM fetches, for each order
still live in its book, `GET /v5/order/realtime` and `GET /v5/execution/list` by
`orderLinkId`; executions are applied once by `execId`, so a fill missed while
disconnected is delivered exactly once.

## 10. Pitfalls (order entry)

| Item | How it applies |
|---|---|
| Client id uniqueness | "Always unique", window not stated; date plus clock-based id. |
| Cancel and replace ids | Cancel reuses the order's `orderLinkId`; amend not implemented. |
| Fill before ack, racing a cancel, after the cancel ack | Unit-tested (`test_bybit_om.cpp`). |
| Unsolicited cancels | `cancelType` other than CancelByUser reaches the SOM as `CancAck`. |
| Busts and corrections | None exist in V5; BustTrade is a liquidation, not a bust. |
| Busy / throttled | 10006 and local token bucket. |
| Reconnect duplicates | `execId` set per order. |
| Orders placed elsewhere | Logged; not forwarded. |
| Short sales | Not applicable to crypto spot (no borrow without margin); short flags refused. |

## 11. Ambiguities

| Question | Reading taken | Resolved by |
|---|---|---|
| Uniqueness window of `orderLinkId` | Forever; ids include the date | Open |
| Queue priority after a REST amend | Not used | Open (FIX amend keeps priority on a size decrease) |
| Signature examples in `v5/guide` | The strings to sign are checked exactly; the example signatures cannot be, because the guide does not give the secret (12 placeholder secrets tried, none reproduce `410e0f38...`) | Open: the first testnet request settles it |
| Does a PostOnly that would cross come back as `Rejected` or `Cancelled` with `rejectReason`? | Either is a `Reject` when nothing filled and no Ack was sent | Testnet run (not done) |
| Spot `Cancelled` with a partial fill (spot uses `PartiallyFilledCanceled`) | Both are `CancAck` after the fills | Documentation |
