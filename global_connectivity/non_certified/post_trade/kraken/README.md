# Kraken post-trade fills client (spot; Kraken Futures demo)

**Not certified.** This client has never connected to Kraken: no demo keys were
supplied, and the spot side can only run on production. What was checked is listed
below with its numbers.

## What it is

`KrakenFillsClient` (`include/kraken_pt/act/KrakenFillsClient.hpp`; factory in
`include/kraken_pt/if/KrakenFillsClient.hpp`) reads the
account's fills, applies each to `post_trade::TradeLedger` (`../common`), and sends
one `post_trade::msg::TradeCapture` per new fill to its subscriber. A stream and a
REST reconciliation feed the same ledger, so a fill the stream missed is recovered
and a fill seen twice is emitted once (`feed.md`).

| Session | Stream | Reconciliation | Environment | Run against Kraken? |
|---|---|---|---|---|
| `spot_fills_factory` | WebSocket v2 `executions` | REST `TradesHistory` | production only | never |
| `futures_fills_factory` | `GET /fills` polled | `GET /fills?lastFillTime=` paged | `demo-futures.kraken.com` | never (no keys) |
| `SimFills` | in-process | in-process | none | n/a |

The spot sessions are spot-only and untested live; the futures sessions are what
step 3 runs.

## Build

```bash
export KSPRPROJ=~/kh-cw
make CXX=clang++ KSPRLIB=/Users/vm/kaspar-hft/lib \
     ACTORS_LIB=/Users/vm/kaspar-hft/actors/cpp/libactors.a test
```

The order-entry sources (signing, endpoint guard) and libframe are compiled into
`build/`.

## Configuration and keys

`config/kraken_fills_futures_demo_example.ini` (`[kraken_fills]`). The same guard
as order entry: with `testnet = true` only `demo-futures.kraken.com` is accepted,
and `product = spot` with `testnet = true` is refused. The key file is the order
manager's, `~/.kaspar/keys/kraken_futures_demo.json`:

```json
{"api_key": "<public key from demo-futures.kraken.com>", "api_secret": "<base64 private key>"}
```

The fills client needs only read permission ("General API" read on the demo).

## Testing ladder

### Step 1: unit tests (12 tests, all pass)

`make test`, 2026-11-18, macOS, clang:

| Suite | Tests | What |
|---|---|---|
| `KrakenFills` | 6 | the documentation's spot `executions` "Execution" example → TradeCapture (id, side, 500 000 lots of 1e-8, price 26599.9 as mantissa 265999 and 1 decimal, UTC date and time); the documentation's `TradesHistory` row (pair `XXBTZUSD` mapped to `BTC/USD`, epoch-seconds time); the documentation's futures `fills` example; unconfigured symbol, fraction of a lot and non-trade refused with a reason; one trade from the WebSocket and its TradesHistory copy reach the ledger once; the example ini and the testnet guard |
| `KrakenFillsClient` | 4 | on SimFills: stream fills emitted once although every reconciliation pass returns them again; a fill the stream dropped recovered by the next pass (`added_by_rest` = 1); a fill made while disconnected arrives once after the reconnect; an unmappable fill counted, not emitted |
| `KrakenFuturesFills` | 2 | against a fake Kraken Futures REST API that checks host and `Authent`: production host refused with no request; 150 fills: the poll reports the newest 100 once over repeated polls and a new one once, fills before the process start are not reported, and a reconciliation pages back through all 151 with `lastFillTime`; every request signed correctly and sent to the demo host |

### Step 2: live public market data

In `../../market_data/kraken/README.md`.

### Step 3: Kraken Futures demo — NOT RUN

`/Users/vm/.kaspar/keys/kraken_futures_demo.json` does not exist. With it,
`../../order_entry/kraken/build/kraken_demo_check --config
../../order_entry/kraken/config/kraken_futures_demo_example.ini --fills-config
config/kraken_fills_futures_demo_example.ini` runs this client next to KrakenOM on
the demo, makes two fills (an IOC buy and an IOC sell of 0.0001 BTC on PF_XBTUSD),
reconnects the order manager, and checks every `fill_id` the venue's signed
`GET /fills` returns against the TradeCaptures emitted (each exactly once) and the
`added_by_rest` and `missing_from_rest` counters
(`../../order_entry/kraken/README.md`, "Step 3").

### Step 4: production — never

The spot fills session (WebSocket `executions`, `TradesHistory`) can only run on
production; not run, for the owner after review.

## Not checked

- Any request to Kraken.
- That the spot WebSocket `exec_id` equals the `TradesHistory` txid (the ledger's
  deduplication across the two spot sources depends on it).
- The futures `lastFillTime` format and paging against the real API.
