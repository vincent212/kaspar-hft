# Feed description: Kraken fills for post-trade (spot executions and TradesHistory; Kraken Futures fills)

Sources on docs.kraken.com, read 2026-11-18 (paths relative to
`https://docs.kraken.com/`). Not checked against Kraken: no keys (README.md).
Signing, hosts and the testnet guard are those of `../../order_entry/kraken/feed.md`
section 2.

## 1. Venue and documents

| Document | Applies to |
|---|---|
| `exchange/api-reference/spot-websocket-v2/executions` | spot fills as they happen; `snap_trades` |
| `api-reference/account-data/get-trades-history` | spot REST fill history |
| `api-reference/historical-data/get-your-fills` | futures fills |
| `exchange/guides/futures/ratelimits` | futures cost budget |

Kraken offers no trade-capture FIX session for spot or futures in these
documents; the US Perps FIX drop copy (`exchange/api-reference/drop-copy`) is for
another product and not used.

## 2. Transport

| Product | Stream | Reconciliation | Environment |
|---|---|---|---|
| spot | WebSocket v2 `executions` on `ws-auth.kraken.com`, `snap_trades: true`, `snap_orders: false` | `POST /0/private/TradesHistory`, `start` = now − lookback, paged with `ofs` (50 rows per page) | production only; never run |
| futures | `GET /derivatives/api/v3/fills` (newest 100) polled every `poll_ms` | the same endpoint with `lastFillTime`, paged back until before the start | `demo-futures.kraken.com` |

Futures costs: the poll 2, a reconciliation page 25, out of 500 per 10 s
(`futures/ratelimits`).

## 3. Report lifecycle and identifiers

- Every report is a new fill (`Event::New`): Kraken sends no amendment,
  cancellation or bust of a fill through these APIs.
- Stable identifier, used as `trade_id` and `report_id`:
  - spot: the trade txid. The WebSocket gives it as `exec_id`
    (`TGBB7L-HT5LX-J3BZ4A` in the documentation's example); TradesHistory gives
    it as the key of each row (`THVRQM-33VKH-UCI7BS`). That the two are the same
    identifier is the reading taken (both have the trade-txid format; the
    documentation does not say so): not checked live.
  - futures: `fill_id` (UUID). `sequence_id` (per-market trade sequence) goes to
    `report_seq` when present.
- `venue_order_id`: spot `order_id` / `ordertxid`; futures `order_id`.
- Duplicates: the same fill from the stream and from REST, or again after a
  reconnect, has the same `report_id` and is dropped by `TradeLedger`.

## 4. Fields

| TradeCapture | spot WebSocket | spot REST | futures |
|---|---|---|---|
| symbol | `symbol` (`BTC/USD`) | `pair` (`XXBTZUSD`), mapped to the configured symbol | `symbol` (`PF_XBTUSD`) |
| side | `side` | `type` | `side` |
| qty (lots) | `last_qty` / lot | `vol` / lot | `size` / lot |
| price | `last_price` text | `price` text | `price` text |
| time | `timestamp` (RFC 3339) | `time` (epoch seconds with a fraction) | `fillTime` (RFC 3339) |
| trade_date, trade_time_ns | UTC date and ns since UTC midnight (Kraken has no trading day) | | |
| poss_dup | false | true (a REST copy) | false (poll), true (reconciliation) |
| raw | the fill as received | `{txid: row}` | the fill |

A fill whose quantity is not a whole number of the configured lot, or whose
symbol is not configured, is refused with a reason and counted (`unmapped`).

## 5. Mapping to Kaspar

`KrakenFillsClient` applies each fill to `post_trade::TradeLedger` and sends a
`post_trade::msg::TradeCapture` to its subscriber only when the ledger added it.
After each reconciliation pass it counts `added_by_rest` (REST rows the stream had
not delivered) and `missing_from_rest` (stream fills older than 5 s inside the
pass's range that REST did not return; logged as errors).

## 6. Ambiguities

| Question | Reading taken | Resolved by |
|---|---|---|
| spot `exec_id` = TradesHistory txid | yes | not resolved (production only) |
| TradesHistory `start` is exclusive, by txid or time | a time in seconds; the ledger drops overlap | not resolved |
| futures `lastFillTime` format | RFC 3339 with milliseconds, url-encoded | not resolved (no keys) |
| trading day | UTC calendar day | no venue definition |
