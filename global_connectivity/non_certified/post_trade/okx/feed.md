# Feed description: OKX V5 fills for post-trade

Sources: the OKX V5 documentation, https://www.okx.com/docs-v5/en/, cited by
anchor; retrieved 2026-11-18. Nothing here has been checked against OKX (no demo
keys).

## 1. Venue and documents

As `../../order_entry/okx/feed.md`. Products: spot and USDT perpetual swaps;
venue codes `en::x::OKX` and `OKXF`.

## 2. Transport and session

| Item | Value | Source |
|---|---|---|
| Live fills | private WebSocket (`wss://ws.okx.com/ws/v5/private`; demo `wss://wspap.okx.com/ws/v5/private`), channel `orders` (`instType` ANY); channel `fills` only for VIP4 and above | #order-book-trading-trade-ws-order-channel, #order-book-trading-trade-ws-fills-channel |
| Reconciliation | REST `GET /api/v5/trade/fills` (last 3 days), `begin` in ms, 100 rows per page, paged with `after` = `billId`; 60 requests per 2 s | #order-book-trading-trade-get-transaction-details-last-3-days |
| Older history | `GET /api/v5/trade/fills-history` (3 months); not used | #order-book-trading-trade-get-transaction-details-last-3-months |
| Login, signing, demo header, keep-alive | as order entry | |

The `orders` channel sends no snapshot on subscribe and nothing is replayed after
a reconnect: missed fills come only from REST.

## 3. Reports

| OKX | Meaning | TradeCapture |
|---|---|---|
| `orders` element with `fillSz` > 0 and a `tradeId` | one fill of one of the account's orders | `Event::New` |
| `orders` element with `fillSz` 0 (ack, amend, cancel) | not a fill | none |
| REST `trade/fills` row | one fill | `Event::New`, `poss_dup` = true |

OKX sends no amendment, cancellation or bust of a fill on these sources.
Liquidation and auto-deleveraging fills are treated as fills like any other; their
shape was not checked.

## 4. Identifiers and duplicates

`tradeId` is per instrument and is the same on the private channel, in REST and
on the public trade feed. `trade_id` = `report_id` = `instId:tradeId`, so the
same fill from the WebSocket, from REST, or again after a reconnect is one
report; `post_trade::TradeLedger` drops the second copy.

## 5. Mapping to TradeCapture

| Field | From |
|---|---|
| `venue` | OKX (SPOT) or OKXF (SWAP), from the configured instrument |
| `trade_id`, `report_id` | `instId:tradeId` |
| `venue_order_id` | `ordId` |
| `symbol`, `asset` | `instId`, the configured Kaspar asset (-1 if none) |
| `side` | `side` (the account's side) |
| `qty` | `fillSz` / `lotSz`, exact integer (spot lot 0.00000001 BTC; swap lot 0.01 contract); a size that is not a whole number of lots is refused and counted |
| `price_mantissa`, `price_decimals` | `fillPx` as sent |
| `trade_date`, `trade_time_ns` | `fillTime` (ms), UTC date and time since UTC midnight (OKX trades around the clock) |
| `account` | empty: OKX's fills name no account |
| `poss_dup` | true for REST rows |
| `raw` | the element as received |

## 6. Pitfalls (`pitfalls.md` section 7)

| Item | How it applies |
|---|---|
| FIX version, dictionary, credentials in Logon | not applicable: JSON over WebSocket and REST, HMAC login |
| requests before the session is open | subscription only after the login event succeeds |
| subscription and replay | no replay on subscribe; REST from `now - reconcile_lookback_s` at each logon and every `reconcile_every_s` |
| lifecycle | new only |
| duplicates on reconnect | keyed by `instId:tradeId` |
| price versus yield, accrued interest, settlement | price only; no settlement date |
| daily sequence reset | none: no sequence numbers |

## 7. Ambiguities

| Question | Reading taken | Alternatives | Resolved by |
|---|---|---|---|
| Does the `orders` channel carry every fill, one element per `tradeId`? | yes | fills aggregated in one push | documentation (`fillSz`, `tradeId` are the latest fill); not checked live |
| Does REST `trade/fills` return a fill as soon as it is pushed? | not assumed: a WebSocket fill missing from REST is counted, not dropped | | not checked live |
