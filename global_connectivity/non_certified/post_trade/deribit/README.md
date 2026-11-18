# Deribit post-trade client (not certified)

`DeribitFillsClient` takes the account's trades from Deribit's private
`user.trades.any.any.raw` channel into `post_trade::TradeLedger` and sends one
`post_trade::msg::TradeCapture` per trade to its subscriber, reconciling against
signed REST `private/get_user_trades_by_currency_and_time` at logon and every
`reconcile_every_s`. **Not certified.** It has never connected to Deribit: no
test-environment keys were supplied. `feed.md` describes the feed.

## Files

| File | Contents |
|---|---|
| `include/deribit_pt/DeribitFills.hpp`, `src/DeribitFills.cpp` | configuration, Deribit trade to TradeCapture, session interface |
| `include/deribit_pt/act/DeribitFillsClient.hpp`, `src/DeribitFillsClient.cpp` | the actor, the ledger, reconciliation counts |
| `src/DeribitFillsWsSession.cpp` | the real session (WebSocket and signed REST) |
| `include/deribit_pt/SimFills.hpp`, `src/SimFills.cpp` | simulator: periodic trades, some withheld from the WebSocket |
| `tests/test_deribit_fills.cpp` | 13 tests |

Uses `../../order_entry/deribit` (WebSocket client, signing, environment guard,
configuration file) and `../common` (TradeLedger).

## Build and run

```bash
export KSPRPROJ=~/kaspar-hft
make test
make KSPRLIB=~/kaspar-hft/lib ACTORS_LIB=~/kaspar-hft/actors/cpp/libactors.a test
```

`FillsConfig::from_file` reads the order manager's INI
(`../../order_entry/deribit/config/deribit_test_example.ini`) and its
`[post_trade]` section.

## Keys and environment

The same key file and guard as the order manager:
`~/.kaspar/keys/deribit_test.json` holding
`{"client_id": "...", "client_secret": "..."}` of a key created on
test.deribit.com (scope `trade:read`, or `trade:read_write` shared with the OM);
`environment = test` accepts only `host = test.deribit.com`.

## Testing ladder

**Step 1: 13 tests, all pass.**
- Mapping from the documentation's examples: the
  `get_user_trades_by_currency_and_time` response row (BTC-26JUN20, 10 USD =
  1 contract, price 9681.5, 2020-05-19T21:21:51.862Z) and the `private/buy`
  response trade (ETH-PERPETUAL, `ETH-2696083`, label `market0000234`), field
  by field; unmappable trades (unknown instrument, half a contract, no
  `trade_id`); decimal parsing; the example configuration file.
- Client against a fake session: duplicates dropped; trades missed on the
  WebSocket added once by reconciliation; a WebSocket trade missing from REST
  counted; reconnect without duplicates; a failed pass counted, not fatal; an
  unmappable trade counted; a production host or empty configuration refused.
- Client against `SimFills` with every third trade withheld from the
  WebSocket: every trade reaches the ledger exactly once.

**Step 2** is market data only.

**Step 3: not run.** `~/.kaspar/keys/deribit_test.json` does not exist. With it,
`../../order_entry/deribit/build/deribit_testnet_check` runs this client next to
the order manager on test.deribit.com and checks that its TradeCaptures are
exactly the trades `private/get_user_trades_by_instrument_and_time` returns for
the run (README of the order manager, step 3).

**Step 4: never.**

## Not checked

Any live message: the shape of `user.trades` notifications and of the REST
response on Deribit's servers, `has_more` paging, REST lag behind the
WebSocket, and whether trade ids from the two sources agree.
