# OKX V5 order entry (spot and perpetual swaps)

**Not certified. Never connected to OKX, demo or production.** No demo keys were
available (`~/.kaspar/keys/okx_demo.json` does not exist), so step 3 of the
testing ladder did not run. Everything below was checked only against the
documentation, a test double and an in-process simulator.

## What it is

| File | What it does |
|---|---|
| `feed.md` | Order-entry description: transport, signing, identifiers, order mapping, reports, throttles, reconnect, instruments |
| `include/okx_oe/act/OkxOM.hpp`, `src/OkxOM.cpp` | The order manager: registers with the SOM for `en::x::OKX` and `OKXF`, turns `Order`/`Cancel` into WS `order`/`cancel-order`, and OKX's results and `orders` pushes into `Ack`, `Fill`, `Reject`, `CancAck`, `CancReject`; state in `frame::som::BrokerOrderBook` |
| `include/okx_oe/OkxSession.hpp`, `src/OkxWsSession.cpp` | The session interface and the real session: private WebSocket, login, `orders` subscription, REST recovery after a reconnect, optional Cancel All After |
| `include/okx_oe/OkxAuth.hpp`, `src/OkxAuth.cpp` | HMAC-SHA256 signing (REST and WebSocket login) and the key file |
| `include/okx_oe/OkxConfig.hpp`, `src/OkxConfig.cpp` | Configuration and the environment guard |
| `include/okx_oe/SimOkx.hpp`, `src/SimOkx.cpp` | In-process OKX for `sim = true` runs |
| `tests/FakeOkx.hpp`, `tests/*.cpp` | Test double and 43 tests |
| `tools/okx_demo_check.cpp` | Step 3 of the testing ladder, demo only (below) |
| `config/okx_demo_example.ini` | Demo configuration for BTC-USDT and BTC-USDT-SWAP |

## Environment guard

- `environment = demo` (the default): the WebSocket host must be `wspap.okx.com`;
  `ws.okx.com`, `wsaws.okx.com`, `wsus.okx.com`, `wseea.okx.com` and any other
  host are refused. Every REST request and the WebSocket upgrade carry
  `x-simulated-trading: 1`; a request without it is refused before it is sent.
  OKX's demo and production share the REST host, so the header is what keeps a
  demo REST request off production.
- `environment = production` is refused unless `allow_production = true`, which
  only the owner sets after review.
- `create_OkxOM` returns `nullptr` (OKX unrouted, kaspar keeps running) for a
  refused environment or a bad configuration.

## Demo keys

Step 3 needs a demo-trading API key, created by the owner in an OKX account:
Trade -> Demo Trading -> Personal Center -> Demo Trading API -> Create Demo
Trading API Key, with the Trade permission. The agent does not create accounts.

The key file is `~/.kaspar/keys/okx_demo.json` (or `key_file` in the
configuration), outside the repository, readable only by the owner
(`chmod 600`), with exactly:

```json
{"api_key": "<demo API key>", "secret_key": "<demo secret key>", "passphrase": "<the passphrase chosen for the key>"}
```

Nothing in the handler prints, logs or copies these values.

The demo account must be in a mode that trades spot and USDT swaps (Spot and
futures mode or above) and in net (one-way) position mode, since the OM sends no
`posSide`. Before a run, read `inst_id_code` and `tick` for each instrument from
`GET /api/v5/public/instruments` **with** `x-simulated-trading: 1`; the demo
swap's values differ from production (`feed.md`, "Instruments") and the example
configuration holds the 2026-11-18 values.

## Build and run

```bash
export KSPRPROJ=~/kh-cw
make test KSPRLIB=~/kaspar-hft/lib ACTORS_LIB=~/kaspar-hft/actors/cpp/libactors.a
make tools KSPRLIB=~/kaspar-hft/lib ACTORS_LIB=~/kaspar-hft/actors/cpp/libactors.a
build/okx_demo_check --config config/okx_demo_example.ini           # no fills
build/okx_demo_check --config config/okx_demo_example.ini --fill    # adds two fills per instrument
```

## Testing ladder

### Step 1: unit tests (43 tests, all pass)

- Signing (6): HMAC-SHA256/Base64 equal to RFC 4231 test case 2; the
  pre-hash string in the documentation's two examples (GET with a query, POST
  with a body); REST and WebSocket-login signatures with the documentation's
  example secret equal to an independent Python `hmac` computation; ISO
  timestamps with milliseconds and `Z` (the documentation's example); key-file
  errors never contain the file's content. The documentation gives no signature
  with a known secret (its login example masks the key), so no test compares
  with an OKX-published signature.
- Environment and configuration (5): demo requires `wspap.okx.com`; demo
  requests carry the header or are refused; production refused unless allowed
  and never on the demo host; the example file parses and validates (including
  the 16-digit demo `instIdCode`); decimal scaling of sizes and prices.
- OkxOM against FakeOkx (28), covering the list in `order_entry.md`: logon and
  registration for both venue codes; bad configuration gives `nullptr`; orders
  before login and while disconnected rejected; the fields of spot and swap
  orders; DAY/GTC/IOC/FOK to `ordType`; unsupported orders and local risk
  limits refused; Ack with `ordId`, partial and full fills; rejects mapped from
  `sCode`; cancel round trip by `clOrdId` and `ordId`; cancel reject leaves the
  order working; cancel of an unknown or filled order; unsolicited and MMP
  cancels; fill before ack, fill racing a cancel, fill after the cancel ack;
  amend pushes and zero fills are not fills; one fill per `tradeId`; busy
  connection throttles until available; reconnect recovers a missed fill
  exactly once and a cancel made while down; orders outside the OM logged and
  ignored; client ids unique, increasing and not reused after a restart;
  shutdown with reports in flight.
- SimOkx (4): ack then fill at the order's price; cancel of a resting order;
  cancel after a fill refused with sCode 51401; oversize rejected with 51008.

### Step 2

Not applicable: order entry has no public data.

### Step 3: demo trading (NOT RUN: no key file)

`build/okx_demo_check` refuses to start without the key file, or with any
configuration other than `environment = demo` on `wspap.okx.com` (checked: both
refusals exit with status 2 and send nothing). With the key file it will:

1. log in on `wss://wspap.okx.com/ws/v5/private` and subscribe to `orders`,
   together with the post-trade client (`../../post_trade/okx`);
2. per instrument, from the demo ticker: a DAY limit buy of 1 unit 10% below the
   best bid (Ack expected), then its cancel (CancAck); an IOC and a FOK buy at
   the same price (OKX cancels both unfilled); with `--fill`, an IOC buy 0.2%
   through the best ask and an IOC sell 0.2% through the best bid, which trade
   against the demo book and leave the position where it was. One unit is
   0.0001 BTC on both instruments;
3. fetch OKX's record of every order (`GET /api/v5/trade/order`) and its fills
   (`GET /api/v5/trade/fills`), at most about 7 REST requests per second, and
   check that the Ack's venue order id is OKX's `ordId`, the last SOM message
   matches OKX's state, the Fill messages' sizes and prices equal OKX's fills,
   and every OKX fill reached the post-trade subscriber as one TradeCapture with
   the same price and no fill is missing;
4. print each order's SOM messages and OKX state, the TradeCapture counts, and
   PASSED or FAILED.

Not covered by the tool: amend (the OM refuses replace), a reconnect (there is
no way to drop the session from outside; the recovery path is tested only
against FakeOkx), `post_only` (a configuration switch), Cancel All After.

### Step 4: production

Never run by the agent. The handler refuses production unless
`allow_production = true`.

## Not checked

- Anything against OKX: login, signature acceptance, the shape of real
  responses and `orders` pushes, error codes, rate-limit behaviour, recovery
  after a real reconnect.
- That OKX accepts `clOrdId` values of 19 digits and the request ids used.
- Amend, market orders, `posSide` (long/short mode), isolated margin, other
  instruments.
