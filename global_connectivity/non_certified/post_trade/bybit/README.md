# Bybit V5 post-trade client (not certified)

`BybitFillsClient` captures the firm's Bybit V5 executions (spot and USDT linear
perpetuals) as `post_trade::msg::TradeCapture`. It reads the private WebSocket topic
`execution` and reconciles over REST (`GET /v5/execution/list`) after every login
and every 5 minutes; a `post_trade::TradeLedger` makes each execution reach the
subscriber once, however many times it is delivered.

**Not certified. It has never connected to Bybit**: no testnet key was supplied.
It has been run against a test fake and the in-process simulator only. With
`testnet = true` (the default) it refuses every host except Bybit's testnet and demo
hosts.

## What was checked, with numbers

| Check | Result |
|---|---|
| Unit tests (`make test`) | 19 of 19 pass |
| Documentation examples | The `execution` stream example and the `/v5/execution/list` response example map to the expected `TradeCapture` (venue, ids, side, quantity units, exact price mantissa, UTC date and time) |
| Stream plus REST copies of one execution | One `TradeCapture` (fake and simulator; in the simulator run every reconciliation re-delivered all executions, and `executions = emitted + duplicates` held) |
| Reconnect | Reconciles from the last window less 60 s; a missed execution from REST then again from the stream is captured once |
| Failed reconciliation | Retried from the same start |
| Day roll | Yesterday's late copy is a duplicate; older executions are dropped and counted |
| `execType` | Trade, AdlTrade, BustTrade, Delivery, BlockTrade, MovePosition, FutureSpread captured; Funding, Settle, unknown not |
| Production refused | Factory returns `nullptr` for `api.bybit.com` or `stream.bybit.com` with `testnet = true` |
| Testnet (ladder step 3) | **Not run**: `~/.kaspar/keys/bybit_testnet.json` does not exist |
| Production | Never |

Not checked: anything Bybit sends (whether `execId` matches between stream and
REST, REST delay, cursor behaviour, rate-limit answers).

## Running step 3 (testnet)

1. The key file `~/.kaspar/keys/bybit_testnet.json`, outside the repository:

   ```json
   {"api_key": "<testnet key>", "api_secret": "<testnet secret>"}
   ```

   A testnet (testnet.bybit.com) or demo-trading HMAC key with read permission on
   Orders (trade history) for Spot and Derivatives; the same key as
   `order_entry/bybit` works.
2. Run the client for 10 minutes while `order_entry/bybit`'s
   `bybit_testnet_check` fills orders on the account:

   ```bash
   make build/bybit_fills_check
   ./build/bybit_fills_check --minutes 10          # --demo for demo trading
   ```

   It captures BTCUSDT spot and linear executions from one hour before the start,
   then fetches `/v5/execution/list` for the same window and checks that every
   position-changing execution was captured exactly once with the same side,
   quantity, price and `orderId`, and that nothing was captured that REST does not
   have. One JSON summary line; exit code 0 only if every check passed.

Without the key file it says what is missing and exits 2; nothing is sent.

## Contents

| Path | What |
|---|---|
| `feed.md` | Sources, transport, replay, `execType` lifecycle, field mapping, duplicates and days, pitfalls, ambiguities |
| `include/bybit_post_trade/BybitFills.hpp`, `src/BybitFills.cpp` | Execution JSON to `TradeCapture` (no network) |
| `include/bybit_post_trade/FillSource.hpp`, `src/RestWsFillSource.cpp` | Source interface; the real source (private stream and REST pages) |
| `include/bybit_post_trade/SimFillSource.hpp`, `src/SimFillSource.cpp` | Simulator for `sim = true` |
| `include/bybit_post_trade/act/BybitFillsClient.hpp`, `src/BybitFillsClient.cpp` | The actor, config, factories `create_BybitFillsClient` |
| `include/bybit_post_trade/msg/FillEvent.hpp` | Source thread to actor message |
| `config/bybit_fills_example.ini` | Example config |
| `tests/` | gtest: mapping from the documentation examples, the actor against `FakeFills` and the simulator |
| `tools/bybit_fills_check.cpp` | Ladder step 3 |

Uses `../common` (`TradeCapture`, `TradeLedger`) and `order_entry/bybit`'s
`BybitAuth.hpp` and `BybitPrivate.hpp` (signing, private stream, host guard).

## Build and test

```bash
export KSPRPROJ=~/kaspar-hft
make test KSPRLIB=/path/to/lib ACTORS_LIB=/path/to/libactors.a
```
