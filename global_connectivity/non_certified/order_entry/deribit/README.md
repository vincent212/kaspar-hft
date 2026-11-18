# Deribit order manager (not certified)

`DeribitOM` connects the SOM to Deribit futures and perpetuals over the JSON-RPC
WebSocket. **Not certified.** It has never connected to Deribit, test or
production: no test-environment keys were supplied. Everything below was checked
against the documentation, a fake venue and a simulator only. The feed
description is `feed.md`.

## Files

| File | Contents |
|---|---|
| `include/deribit_oe/act/DeribitOM.hpp`, `src/DeribitOM.cpp` | the actor: SOM `Order`/`Cancel` to `private/buy`, `private/sell`, `private/cancel`, `private/cancel_by_label`; responses and `user.orders`/`user.trades` to Ack, Fill, Reject, CancAck, CancReject on `frame::som::BrokerOrderBook` |
| `include/deribit_oe/DeribitSession.hpp` | the session interface the OM, the simulator and the fake implement |
| `src/DeribitWsSession.cpp` | the real session: auth, subscriptions, heartbeat, cancel on disconnect, recovery calls |
| `include/deribit_oe/Signing.hpp`, `src/Signing.cpp` | `client_signature` and `deri-hmac-sha256` (HMAC-SHA256) |
| `include/deribit_oe/DeribitConfig.hpp`, `src/DeribitConfig.cpp` | configuration, the environment guard, the key file |
| `include/deribit_oe/RpcWs.hpp`, `src/RpcWs.cpp` | WebSocket client and HTTPS request (Boost.Beast, on `sim/WsNet.hpp`) |
| `include/deribit_oe/SimDeribit.hpp`, `src/SimDeribit.cpp` | in-process venue for `sim = true` runs |
| `tests/FakeDeribit.hpp`, `tests/test_*.cpp` | the fake venue and 48 tests |
| `tools/deribit_testnet_check.cpp` | testing ladder step 3 (not run) |
| `config/deribit_test_example.ini` | test-environment configuration (BTC-PERPETUAL) |

## Build and run

```bash
export KSPRPROJ=~/kaspar-hft
make test                      # 48 tests
make tools                     # build/deribit_testnet_check
# on this Mac, with the libraries of the main checkout:
make KSPRLIB=~/kaspar-hft/lib ACTORS_LIB=~/kaspar-hft/actors/cpp/libactors.a test
```

`create_DeribitOM("config/deribit_test_example.ini", som)` returns the actor,
or `nullptr` (reason on stderr) for a bad configuration or a refused
environment.

## Keys

Read at run time from `key_file`, by default `~/.kaspar/keys/deribit_test.json`,
outside the repository:

```json
{"client_id": "...", "client_secret": "..."}
```

The key must be created on **test.deribit.com** (a separate account from
production) with scope `trade:read_write`. Nothing logs, prints or copies the
secret; `public/auth` sends only the HMAC signature.

## Environment guard

- `environment = test` (the default) requires `host = test.deribit.com`; any
  other host, `www.deribit.com` included, is refused by the factory, by the
  session before it connects, and by the testnet check.
- `environment = production` is refused unless `allow_production = true`, which
  only the owner sets after review.

## Testing ladder

**Step 1, documentation examples and unit tests: 48 tests, all pass.**
- Signing: both of the documentation's signature examples reproduced exactly
  (WebSocket `56590594...e9aa1`; REST `9bfbc51a...276ab`, which the page says
  depends on the current time but which the WebSocket example's timestamp and
  nonce reproduce); the secret never appears in the auth request.
- Environment guard (2 tests), key file (content never in errors), INI parsing
  (2 tests), decimal conversion: 10 signing and configuration tests in all.
- OM against the fake (33 tests, the list in `order_entry.md`): logon and
  registration; bad config gives `nullptr`; orders before logon or while
  disconnected; fields for an inverse (BTC-PERPETUAL, USD amount) and a linear
  (ETH_USDC-PERPETUAL, coin amount) instrument; TIF and post-only; nine
  unsupported orders refused locally; size, notional and rate limits; Ack with
  `order_id`, partial and full fills; immediate fills in the response counted
  once with the notification; IOC remainder; error codes; cancel by `order_id`
  and by `label`; cancel_by_label that cancels nothing; cancel reject; cancel of
  unknown or filled orders; unsolicited cancel; rejected state; fill before ack;
  fill racing cancel; fill after cancel ack; edits and untriggered states are
  not fills; duplicate `trade_id`; busy; reconnect recovering a missed fill once;
  reconnect recovering an order whose response was lost (by label); cancel in
  flight at a disconnect; orders outside the OM; client ids across a restart;
  shutdown with reports in flight; and one test of the `order_id` to `xordid`
  conversion.
- OM against `SimDeribit` (4 tests): fill, cancel, cancel after fill,
  insufficient funds.

**Step 2** is market data only (`../../market_data/deribit/README.md`).

**Step 3, test environment: not run.** `~/.kaspar/keys/deribit_test.json` does
not exist. With that file in place, `build/deribit_testnet_check` runs, on
test.deribit.com only, with BTC-PERPETUAL and one contract (10 USD) per order:
logon of the OM and the fills client; a GTC buy 30% below the best bid and its
cancel; IOC and FOK buys 30% below the bid; a DAY sell 30% above the ask, a
dropped connection, the reconnect and the cancel; an IOC buy 1% through the ask
(fills if the test book has liquidity) and an IOC sell 1% through the bid to
flatten. It then fetches `private/get_order_history_by_instrument` and
`private/get_user_trades_by_instrument_and_time` over signed REST and checks
each order's final SOM message against Deribit's `order_state`, the sum of its
Fills against `filled_amount`, each Fill against a Deribit trade of that order,
and the fills client's TradeCaptures against Deribit's trade list. It exits 0
only when all match. Without the key file it prints what the file must contain
and exits 3; with a production host it exits 2 (both checked). On 2026-11-18 the
public ticker on test.deribit.com reported BTC-PERPETUAL `state: locked`; the
check would fail at step 2 if the test instrument is still locked.

**Step 4, production: never** by the agent.

## Not checked

- Any message from Deribit's servers on the private API: logon, the shape of
  live responses and notifications, error texts, token lifetime, cancel on
  disconnect, heartbeat behaviour.
- Rate-limit behaviour under load; the order of `user.orders` and
  `user.trades` notifications relative to each other and to the response.
- Ed25519 and RSA keys (only HMAC keys are implemented); FIX and Starbase SBE
  order entry; `private/edit`.
