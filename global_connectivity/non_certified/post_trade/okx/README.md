# OKX V5 post-trade fills client (spot and perpetual swaps)

**Not certified. Never connected to OKX, demo or production.** No demo keys were
available, so step 3 of the testing ladder did not run. Checked only against the
documentation's examples, a test double and an in-process simulator.

## What it is

| File | What it does |
|---|---|
| `feed.md` | Fills sources, identifiers, mapping to `post_trade::msg::TradeCapture` |
| `include/okx_pt/act/OkxFillsClient.hpp`, `src/OkxFillsClient.cpp` | The actor: fills from the private `orders` channel (or `fills`, VIP4+) and from REST reconciliation into `post_trade::TradeLedger`; one TradeCapture to the subscriber per fill the ledger had not seen |
| `include/okx_pt/OkxFills.hpp`, `src/OkxFills.cpp` | Configuration, the session interface, and the mapping of one OKX fill to TradeCapture |
| `src/OkxFillsWsSession.cpp` | The real session: login, subscription, REST `GET /api/v5/trade/fills` paging |
| `include/okx_pt/SimFills.hpp`, `src/SimFills.cpp` | In-process OKX for `sim = true`: periodic fills, optionally dropping some from the WebSocket so only reconciliation reports them |
| `tests/test_okx_fills.cpp` | 13 tests |

The configuration is the order manager's file (`../../order_entry/okx/config/okx_demo_example.ini`)
with an optional `[post_trade]` section (`channel`, `reconcile_every_s`,
`reconcile_lookback_s`) and an optional `lot_sz` per instrument. The same
environment guard applies: demo only on `wspap.okx.com`, every REST request with
`x-simulated-trading: 1` or not sent, production refused unless
`allow_production = true`.

After each reconciliation pass the client counts `added_by_rest` (REST rows the
WebSocket had not delivered) and `missing_from_rest` (WebSocket fills inside the
pass's time range that REST did not return; logged as an error). A fill that
cannot be mapped is logged and counted, never dropped silently.

## Keys

The same key file as order entry: `~/.kaspar/keys/okx_demo.json`, outside the
repository, `chmod 600`, containing exactly

```json
{"api_key": "<demo API key>", "secret_key": "<demo secret key>", "passphrase": "<the key's passphrase>"}
```

for a demo-trading API key the owner creates in an OKX account (read permission
is enough for this client; the order-entry check needs Trade).

## Build

```bash
export KSPRPROJ=~/kh-cw
make test KSPRLIB=~/kaspar-hft/lib ACTORS_LIB=~/kaspar-hft/actors/cpp/libactors.a
```

## Testing ladder

### Step 1: unit tests (13 tests, all pass)

- Mapping (5): the documentation's REST `trade/fills` example and `orders`
  channel example map field by field; swap sizes in contracts become whole lots;
  non-fills (`fillSz` 0) and unmappable fills (unknown instrument, a size that is
  not a whole number of lots) are refused; decimal parsing.
- Client against a fake session (7): a WebSocket fill reaches the subscriber once;
  reconciliation adds missed fills once and matches the rest; a WebSocket fill
  missing from REST is counted; a reconnect reconciles without duplicates; a
  failed pass is counted, not fatal; an unmappable fill is counted; a bad
  configuration gives `nullptr`.
- SimFills (1): fills dropped from the WebSocket come back through
  reconciliation, each once.

### Step 2

Not applicable: fills are private.

### Step 3: demo trading (NOT RUN: no key file)

Run by `../../order_entry/okx/build/okx_demo_check`, which starts this client
next to the order manager (README there). With `--fill` it makes two fills per
instrument, then checks that every fill OKX's REST history shows for the run's
orders reached the subscriber as exactly one TradeCapture with OKX's price, and
counts TradeCaptures of fills from other activity on the account separately.

### Step 4: production

Never run by the agent.

## Not checked

- Anything against OKX: login, the real shape of `orders` pushes and REST rows,
  REST latency relative to the push, paging over more than 100 fills.
- The `fills` channel (VIP4+), `fills-history`, liquidation and ADL fills.
- Quantity overflow: spot `qty` is in lots of 1e-8 BTC (`int64_t`, no overflow
  for any realistic fill).
