# Feed description: Deribit post-trade (private trades over JSON-RPC)

Not certified. Deribit's documented FIX 5.0 SP2 drop copy covers only Starbase
direct-access orders (starbase/fix-drop-copy-api) and needs LD4 or AWS
PrivateLink; whether a FIX trade-capture service exists for JSON-RPC accounts was
not found in the documentation. This client takes the account's trades from the
JSON-RPC API. Documents as in
`../../order_entry/deribit/feed.md`, plus
subscriptions/user/usertradeskindcurrencyinterval,
api-reference/trading/private-get_user_trades_by_currency_and_time and
articles/accessing-historical-trades-orders (fetched 2026-11-18).

## Transport and logon

The same WebSocket, keys, signing and environment guard as the order manager
(`../../order_entry/deribit/feed.md` section 2): `public/auth`
(`client_signature`), `private/subscribe` to `user.trades.any.any.raw`,
`public/set_heartbeat`. A second, separate connection from the OM's.

## Subscription and replay

- `user.trades.(kind).(currency).(interval)`: `raw` "selects unaggregated
  updates and is available only to authorized users"; `data` is an array of
  trades. Nothing is resent on subscribe.
- Reconciliation over signed REST (`deri-hmac-sha256`):
  `GET /api/v2/private/get_user_trades_by_currency_and_time` with `currency`,
  `start_timestamp`, `end_timestamp`, `count` (at most 1000),
  `sorting=asc`; the result has `trades` and `has_more`. Without
  `historical=true` it returns "recent records" (trades of the last 24 hours,
  articles/accessing-historical-trades-orders); the default lookback is one
  hour. Pages continue from the last trade's millisecond; at most 20 pages per
  currency per pass, 100 ms apart.

## Report lifecycle and identifiers

- One trade object per fill: `trade_id` ("unique per currency"), `trade_seq`
  (per instrument), `order_id`, `label`, `instrument_name`, `direction` (the
  account's side), `amount`, `price`, `timestamp` (ms), `liquidity` (M/T),
  `fee`, `fee_currency`.
- No amendment, cancellation or bust message exists on these channels: every
  report is `Event::New`.
- The documentation's examples show `trade_id` both with a currency prefix
  (`ETH-2696083`) and without (`47958936`, BTC); the client prefixes the
  configured currency (`BTC:47958936`) so ids are unique across currencies.

## Mapping to TradeCapture

| TradeCapture | Deribit |
|---|---|
| venue | `en::x::DERIBIT` |
| trade_id, report_id | `<currency>:<trade_id>` |
| report_seq | 0 (arrival order) |
| event | New |
| venue_order_id | `order_id` |
| symbol, asset | `instrument_name`, the configured Kaspar asset |
| side | `direction` buy/sell |
| qty | `amount` / `qty_unit` (exact, else the trade is refused and counted) |
| price_mantissa, price_decimals | `price` as printed (shortest round-trip) |
| trade_date, trade_time_ns | `timestamp`, UTC date and ns since UTC midnight |
| account | `label` |
| poss_dup | true when it came from REST |
| raw | the trade object |

## Duplicates

The ledger keys by `<currency>:<trade_id>`: a trade from both the WebSocket
and REST, or again after a reconnect, is applied once.

## Ambiguities

| Question | Reading taken |
|---|---|
| Is `trade_id` unique across currencies? | no ("unique per currency"); prefixed |
| Can a trade be busted or corrected? | no message for it on these channels |
| Does REST lag the WebSocket? | WebSocket trades in the last 5 s of a pass are not counted as missing from REST |
