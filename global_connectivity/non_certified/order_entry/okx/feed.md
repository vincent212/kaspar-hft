# Feed description: OKX V5 order entry (spot and perpetual swaps)

Sources are pages of the OKX V5 API documentation, https://www.okx.com/docs-v5/en/,
cited by anchor, and its changelog (`docs-v5/log_en`); retrieved 2026-11-18.
Nothing here has been checked against OKX itself: no demo keys were available
(README.md).

## 1. Venue and documents

| Document | Version | Date | Applies to |
|---|---|---|---|
| OKX API V5 documentation | V5, single page | retrieved 2026-11-18 | everything below |
| OKX API V5 changelog | as of 2026-11-18 | 2025-11-20: `instIdCode` added to WS place order | `instIdCode` is required on WS `order` and `cancel-order` |

Products: spot (`instType` SPOT, `tdMode` cash) and USDT-margined perpetual swaps
(`instType` SWAP, `tdMode` cross or isolated). Kaspar venue codes `en::x::OKX`
and `en::x::OKXF`.

## 2. Transport

| Item | Value | Source |
|---|---|---|
| Private WebSocket, production | `wss://ws.okx.com:443/ws/v5/private` | #overview-production-trading-services |
| Private WebSocket, demo | `wss://wspap.okx.com/ws/v5/private` | #overview-demo-trading-services |
| REST, production and demo | `https://openapi.okx.com` (recommended since 2026-05-20; `www.okx.com` still works, changelog); demo requests carry the header `x-simulated-trading: 1` | same |
| Keep-alive, connection limits | as market data (`../../market_data/okx/feed.md` section 2) | #overview-websocket-connect |
| Order rate | WS and REST place order: 60 requests per 2 s per user ID and instrument | #order-book-trading-trade-ws-place-order |
| Cancel rate | 60 per 2 s per user ID and instrument | #order-book-trading-trade-ws-cancel-order |

Demo and production share the REST host. Only the WebSocket host and the
header tell them apart, so the handler checks both (section "Logon and session").

## 3. Framing

JSON per WebSocket text frame. Requests `{"id", "op", "args": [ {...} ]}`; `id`
is up to 32 alphanumerics and comes back in the response. Responses
`{"id", "op", "code", "msg", "data": [{"ordId", "clOrdId", "tag", "sCode",
"sMsg", "ts"}], "inTime", "outTime"}`: `code` "0" with `sCode` "0" is success;
`code` "1" with a non-zero `sCode` per argument is a failure.

## 4. Messages used

| Operation | Fields sent | Source |
|---|---|---|
| `login` | `apiKey`, `passphrase`, `timestamp` (Unix seconds), `sign` | #overview-websocket-login |
| `subscribe` `orders` | `instType` ANY | #order-book-trading-trade-ws-order-channel |
| `order` | `instIdCode`, `tdMode`, `clOrdId`, `side`, `ordType` (limit, post_only, ioc, fok), `sz`, `px` | #order-book-trading-trade-ws-place-order |
| `cancel-order` | `instIdCode`, `clOrdId`, `ordId` when known | #order-book-trading-trade-ws-cancel-order |
| REST `GET /api/v5/trade/order` | `instId`, `ordId` or `clOrdId` | #order-book-trading-trade-get-order-details |
| REST `GET /api/v5/trade/fills` | `instId`, `ordId`, `after` (billId), `limit` 100 | #order-book-trading-trade-get-transaction-details-last-3-days |
| REST `POST /api/v5/trade/cancel-all-after` | `timeOut` 10..120 s (0 cancels the timer) | #order-book-trading-trade-post-cancel-all-after |

`orders` channel element fields used: `instId`, `ordId`, `clOrdId`, `state`
(live, partially_filled, filled, canceled, mmp_canceled), `fillSz`, `fillPx`,
`tradeId`, `fillTime`, `accFillSz`, `execType`, `cancelSource`, `amendResult`.

## Logon and session

- **Signature:** `Base64(HMAC-SHA256(secret, timestamp + METHOD + requestPath +
  body))` (#overview-rest-authentication-signature). REST: ISO 8601 UTC with
  milliseconds, rejected more than 30 s from server time (50102). WebSocket
  login: Unix seconds, `GET`, `/users/self/verify`, empty body; it expires 30 s
  after the timestamp (#overview-websocket-login).
- **Keys:** API key, secret, passphrase, created by the owner in the OKX
  account's demo-trading mode. Read at run time from
  `~/.kaspar/keys/okx_demo.json` (README.md).
- **Demo guard:** with `environment = demo` the WebSocket host must be
  `wspap.okx.com` (production hosts `ws.okx.com`, `wsaws.okx.com`, `wsus.okx.com`,
  `wseea.okx.com` are refused) and every REST request and the WebSocket upgrade
  carry `x-simulated-trading: 1`; a request without it is not sent
  (`require_demo_header`). `environment = production` is refused unless
  `allow_production = true`.
- **No sequence numbers** on the private stream; the `orders` channel sends no
  snapshot on subscribe.
- **Cancel-on-disconnect:** none tied to the connection. Cancel All After
  (10-120 s, refreshed every third of the timeout) is optional
  (`cancel_all_after_s`).

## Order identifiers

- `clOrdId`: up to 32 case-sensitive alphanumerics, unique among the account's
  pending orders (#order-book-trading-trade-ws-place-order). The OM uses the
  `BrokerOrderBook` client id in decimal (at most 19 digits), unique for the day
  and not reused after a restart (`ClientIdGen::from_clock`).
- `ordId`: OKX's id, returned in the op result and every `orders` push; used as
  the venue order id in `Ack`.
- Cancels name the order by `clOrdId` (and `ordId` when known); there is no new
  client id per cancel. Request ids: `n<cid>` for a new order, `c<cid>r<k>` for
  the k-th cancel.
- Amend (`amend-order`) keeps `ordId` and `clOrdId`; `newSz` is the new total.
  The OM does not send amends (section "New order").

## New order

| SOM `Order` | OKX |
|---|---|
| LIMIT, DAY or GTC | `ordType` limit (or post_only with `post_only = true`) |
| LIMIT, IOC | `ordType` ioc |
| LIMIT, FOK | `ordType` fok |
| side BUY / SEL | `side` buy / sell |
| `sz` in units | `sz` = units x `qty_unit` (BTC for spot, contracts for swaps) |
| `px` in ticks | `px` = ticks x `tick` |

Refused locally with `Reject`: market, stop and stop-limit orders, other times in
force, short-sale marking, routes other than OKX, broker algorithms,
cancel/replace, sizes that are not whole units, sizes and notionals over the
configured limits, more than `max_orders_per_sec`. No `posSide` is sent, so
swaps need the account in net (one-way) position mode.

## Reports

| OKX | Book call | SOM |
|---|---|---|
| op `order`, `sCode` 0 | `on_accepted(cid, ordId)` | `Ack` |
| op `order`, failure | `on_rejected` | `Reject` (51008 and other balance/size codes: EXCHLIMIT; 50011, 50061, 51034: THROTTLE) |
| `orders` live / partially_filled before the op result | `on_accepted` | `Ack` |
| `orders` with `fillSz` > 0 and a `tradeId` | `on_fill`, once per `tradeId` per order | `Fill` |
| `orders` canceled / mmp_canceled | `on_canceled` | `CancAck` (ours or unsolicited) |
| op `cancel-order` failure | `on_cancel_rejected` | `CancReject` FROMEXCHANGE |
| op `cancel-order` success | none: the `orders` push confirms | |
| `orders` amend results (`fillSz` 0) | none | |

An update carrying the last fill and `state` filled applies the fill first. A
fill after the cancel ack counts. OKX does not bust fills on this channel.

## Throttles and busy states

OKX answers an over-limit request with `sCode` 50011 (rate limit). The OM keeps
its own token bucket (`max_orders_per_sec`) and refuses locally above it. The
session queues at most 512 outbound requests; beyond that it reports Busy and
the OM rejects with THROTTLE until it is available. A new order is never resent
after a timeout.

## Reconnect and replay

The `orders` channel resends nothing. After every login the OM asks the session
to recover each order still live in its book: REST `trade/order` for the state
and `trade/fills` for its fills, delivered as `orders`-shaped updates, fills
first. A fill already applied is recognised by `tradeId` and skipped.

## Risk checks

OKX enforces balance, position and price limits (sCode 51008, 51006 and others).
The OM enforces `max_order_units`, `max_order_notional` (estimated as units x
`qty_unit` x price, which for a swap overstates the notional by 1/`ctVal`) and
`max_orders_per_sec`.

## Instruments

`inst_id_code` and `tick` come from `GET /api/v5/public/instruments`, sent with
`x-simulated-trading: 1` for demo: they differ from production. On 2026-11-18:

| instId | production instIdCode, tickSz | demo instIdCode, tickSz |
|---|---|---|
| BTC-USDT | 3, 0.1 | 3, 0.1 |
| BTC-USDT-SWAP | 10459, 0.1 | 2021032601102993, 0.01 |

The demo swap's code does not fit in 32 bits; `inst_id_code` is 64-bit.

## Pitfalls (`pitfalls.md` section 8)

| Item | How it applies |
|---|---|
| client ids | unique among pending orders at OKX; the OM's are unique per day and not reused after a restart |
| cancel/replace ids | cancel reuses `clOrdId`; amend keeps both ids; replace is refused |
| fill before ack, racing cancel, after cancel ack | each counted once (tests) |
| unsolicited cancels | `orders` canceled with any `cancelSource`, and mmp_canceled, reach the SOM |
| busts and corrections | none on this channel; amend pushes are not fills |
| busy | 50011 and the 512-request queue |
| reconnect | no replay from OKX; REST recovery, deduplicated by `tradeId` |
| orders outside the OM | logged; a fill on one is logged as an error |
| short sales | not applicable to OKX; marking refused |

## Ambiguities

| Question | Reading taken | Alternatives | Resolved by |
|---|---|---|---|
| Does a demo `instIdCode` equal production's? | no: read with the demo header | same code | public REST with and without the header, 2026-11-18 |
| Is `instIdCode` required on WS `cancel-order`? | yes | `instId` | documentation (Required: Yes) |
| Priority on an amend that reduces size | not used | | the OM does not amend |
| Can the documentation's login example be reproduced? | no: its key is masked | | signatures checked against an independent HMAC (Python) and RFC 4231 |
