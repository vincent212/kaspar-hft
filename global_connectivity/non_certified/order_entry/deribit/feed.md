# Feed description: Deribit order entry (JSON-RPC over WebSocket)

Not certified. Citations are to docs.deribit.com pages fetched on 2026-11-18
(copies in `../../market_data/deribit/build/docs/`, downloaded by that
Makefile). Deribit also offers FIX 4.4 and, on Starbase, binary SBE order entry
over TCP from LD4 or AWS PrivateLink; this handler uses JSON-RPC, the only
interface reachable from the internet with a test account.

## 1. Venue and documents

| Document | Date | Applies to |
|---|---|---|
| articles/authentication | fetched 2026-11-18 | `client_signature`, REST `deri-hmac-sha256`, examples |
| api-reference/authentication/public-auth | fetched 2026-11-18 | token scope and lifetime |
| api-reference/trading/private-buy, private-sell, private-cancel, private-cancel_by_label | fetched 2026-11-18 | requests, responses, order and trade objects |
| api-reference/trading/private-get_order_state, -get_order_state_by_label, -get_user_trades_by_order, -get_order_history_by_instrument | fetched 2026-11-18 | recovery and the testnet check |
| subscriptions/user/userorderskindcurrencyraw, usertradeskindcurrencyinterval | fetched 2026-11-18 | order and trade notifications |
| api-reference/session-management/public-set_heartbeat, private-enable_cancel_on_disconnect | fetched 2026-11-18 | keep-alive, cancel on disconnect |
| articles/order-management-best-practices, articles/rate-limits, articles/errors | fetched 2026-11-18 | labels, queueing, limits, error codes |
| articles/deribit-quickstart | fetched 2026-11-18 | test environment `test.deribit.com` |

## 2. Transport, logon and session

- `wss://test.deribit.com/ws/api/v2` (test) and `wss://www.deribit.com/ws/api/v2`
  (production). Separate accounts and keys (deribit-quickstart). The handler
  refuses any host but `test.deribit.com` when `environment = test`, and
  production unless `allow_production = true` (`DeribitConfig.cpp`).
- Logon: `public/auth` with `grant_type = client_signature`: `client_id`,
  `timestamp` (ms), `nonce`, `data`, `signature` = lower-case hex
  HMAC-SHA256(client_secret, `timestamp\nnonce\ndata`) (articles/authentication,
  "Client Signature"). The secret is never sent. A WebSocket token without a
  `session:` scope has `connection` scope and is bound to the connection
  (public-auth); the session re-authenticates at 80% of `expires_in`.
- REST (used for post-trade reconciliation and the testnet check):
  `Authorization: deri-hmac-sha256 id=..,ts=..,nonce=..,sig=..`, sig =
  HMAC-SHA256(secret, `ts\nnonce\nMETHOD\nURI\nbody\n`). A timestamp is
  "valid for 60 seconds since generation".
- Both signature examples in articles/authentication are reproduced in
  `tests/test_deribit_signing_config.cpp`: the WebSocket one
  (`56590594...e9aa1`) and the REST one (`9bfbc51a...276ab`), which the page
  labels as depending on the current time but which is reproduced exactly by
  the WebSocket example's timestamp 1576074319000 and nonce 1iqt2wls.
- After auth: `private/subscribe` to `user.orders.any.any.raw` and
  `user.trades.any.any.raw` (raw needs an authenticated connection);
  `public/set_heartbeat` (interval >= 10 s; the server's `test_request` is
  answered with `public/test`); `private/enable_cancel_on_disconnect` with scope
  `connection` (on by default in the config). Cancel on disconnect fires on TCP
  close, on heartbeat failure and after inactivity, but not on
  `private/logout` (private-enable_cancel_on_disconnect).
- Private requests from one connection "typically go into a queue dedicated to
  your connection" (order-management-best-practices); strict in-order
  processing is not stated.

## 3. Framing

JSON-RPC 2.0, one object per WebSocket text frame: requests
`{jsonrpc, id, method, params}`; responses `{id, result}` or
`{id, error: {code, message, data}}`; notifications
`{method: "subscription", params: {channel, data}}`. On `user.orders.*.raw`
`data` is one order object; on the aggregated intervals and on
`user.trades.*` it is an array.

## 4. Orders

**Identifiers.** `label`: free text, at most 64 characters; Deribit does not
say it must be unique (private-buy). The OM uses the BrokerOrderBook client id
in decimal, unique for the day and not reused after a restart. `order_id` is
Deribit's (a string, e.g. `ETH-584849853` or `31975418392`); Ack's `xordid`
is its trailing digits. The JSON-RPC `id` of each request is the OM's own
counter; the session's own requests use ids from 2^40.

**New order.** `private/buy` or `private/sell`: `instrument_name`, `amount`
(USD for inverse futures and perpetuals, coin for linear and options;
`contracts` may be sent instead), `type` (`limit`; market, stop and other
types are refused locally), `price`, `label`, `time_in_force`
(`good_til_cancelled`, `good_til_day`, `fill_or_kill`,
`immediate_or_cancel`), `post_only` with `reject_post_only` (without
`reject_post_only` a post-only order that would cross is repriced, which
would change the order's price without the SOM knowing; the OM always sends
both). No short-sale marking, routing or broker algorithms exist. SOM DAY maps
to `good_til_day`, GTC to `good_til_cancelled`.

**Cancel.** `private/cancel` with `order_id`. Before the buy/sell response has
given the order an id, `private/cancel_by_label` with `label` and `currency`,
which also removes the order from the connection's queue if it has not reached
the book (order-management-best-practices); its result is the number of
orders cancelled. The SOM has no amend; `private/edit` is not used.

**Reports.**

| Deribit | Book call | SOM |
|---|---|---|
| buy/sell result, `order.order_state` open / filled / cancelled | `on_accepted` | Ack (order_id) |
| ... its `trades` array | `on_fill` per new `trade_id` | Fill |
| ... `order_state` cancelled (IOC/FOK remainder) | `on_canceled` | CancAck |
| ... `order_state` rejected, or an `error` | `on_rejected` | Reject |
| cancel result (the order, cancelled) | `on_canceled` | CancAck |
| cancel_by_label result 0 | `on_cancel_rejected` | CancReject |
| cancel `error` (e.g. 11044 not_open_order) | `on_cancel_rejected` | CancReject |
| `user.orders` open | `on_accepted` | Ack (if not yet) |
| `user.orders` cancelled (ours, cancel on disconnect, MMP, expiry) | `on_canceled` | CancAck |
| `user.orders` rejected | `on_rejected` | Reject |
| `user.orders` filled | none (the trades carry the quantities) | |
| `user.trades` element | `on_fill` per new `trade_id` | Fill |

A trade can arrive in the response, on `user.trades`, and again from recovery;
it is applied once per `trade_id`. The order and trade channels are separate,
so a `filled` or `cancelled` state can arrive before the last trade; a fill
after the cancel ack still counts. Deribit has no bust or correction message
on these channels.

**Errors** (articles/errors): 10028 too_many_requests, 10040 retry and 10041
settlement_in_progress map to THROTTLE; 10009 to EXCHLIMIT (the page lists
10009 as `not_enough_funds` in one table and `invalid_argument` in another);
anything else to UNKNOWN. 11054 post_only_reject, 13009 unauthorized, 11044
not_open_order are logged with their message.

**Throttles.** Matching-engine requests are limited per sub-account by 7-day
volume tier, lowest tier 5 a second with a burst of 20; other requests use a
credit pool of about 20 a second, and exhausting it ends the session with
10028 (articles/rate-limits). The OM's own limit is `max_orders_per_sec`
(default 5); the session's outbound queue holds 512 requests, after which
calls return Busy and orders are rejected with THROTTLE until it drains.

**Reconnect and replay.** The user channels resend nothing on subscribe. After
every logon the OM asks the session to recover each order still live: by
`order_id`, `private/get_user_trades_by_order` then `private/get_order_state`;
with no `order_id` yet (the response was lost), `private/get_order_state_by_label`
first. A new order whose response is lost is never resent. A cancel in flight
when the connection drops is answered CancReject(DISCONNECTED) and the order
stays working until recovery says otherwise.

**Risk checks.** Deribit applies margin and price-band checks. The OM enforces
`max_order_units`, `max_order_notional` (USD; for inverse instruments the USD
amount, for linear amount x price) and `max_orders_per_sec`.

## 5. Pitfalls (`pitfalls.md` section 8)

| Item | How it applies |
|---|---|
| client id uniqueness | `label` uniqueness is not required by Deribit; the OM's ids are unique and increasing across restarts (`ClientIdGen`) |
| cancel/replace ids | no replace; cancel uses `order_id`, or `label` before it is known |
| fill before ack, fill racing cancel, fill after cancel ack | tested |
| unsolicited cancels | `user.orders` cancelled with `cancel_reason` (e.g. cancel_on_disconnect) |
| busts | none on these channels |
| busy | 10028 or a full outbound queue: THROTTLE |
| reconnect | recovery per live order; trades deduplicated by `trade_id` |
| orders outside this OM | labels not issued by this OM are logged and ignored; a trade on such an order is logged as an error |
| short sales | not applicable |

## 6. Ambiguities

| Question | Reading taken | Resolved by |
|---|---|---|
| Is `label` unique? | not assumed; the OM keeps its labels unique | not documented |
| Error 10009 | not_enough_funds -> EXCHLIMIT | articles/errors has both meanings |
| Can a trade arrive after `user.orders` says cancelled? | yes, handled (separate channels) | not documented; the test environment would show it |
| Token lifetime on a WebSocket | re-authenticate at 80% of `expires_in` | public-auth says connection tokens cannot be refreshed |
