# Kraken order entry (spot WebSocket v2; Kraken Futures REST v3 demo)

**Not certified.** This order manager has never sent an order to Kraken, on any
environment: no demo keys were supplied. What was checked is listed below with
its numbers.

## What it is

`KrakenOM` (`include/kraken_oe/act/KrakenOM.hpp`) registers with the SOM for
`en::x::KRAKEN` (spot) or `en::x::KRAKENF` (futures), turns `Order` and `Cancel`
into the venue's requests, and the venue's reports into `Ack`, `Fill`, `Reject`,
`CancAck` and `CancReject`, with state in `frame::som::BrokerOrderBook`. The venue
connection is behind `KrakenSession`:

| Session | Product | Environment | Run against Kraken? |
|---|---|---|---|
| `spot_session_factory` (`src/KrakenSpotSession.cpp`) | spot: WebSocket v2 `add_order`, `cancel_order`, `executions` on `ws-auth.kraken.com` | **production only** (Kraken has no spot testnet) | never |
| `futures_session_factory` (`src/KrakenFuturesSession.cpp`) | futures: REST `sendorder`, `cancelorder`, `fills`, `orders/status` | `demo-futures.kraken.com` | never (no keys) |
| `SimKraken` | in-process venue for `--sim` runs and tests | none | n/a |

The spot path is spot-only and untested live: its messages are tested on the
documentation's examples, its session code is compiled and never run. The futures
path is what step 3 runs on the demo when keys exist; Kraken Futures has its own
order API, so step 3 tests the OM, BrokerOrderBook handling and the futures
session, not the spot session.

`feed.md` describes both APIs: signing, identifiers, order types, the report
mapping, throttles, reconnect.

## Build

```bash
export KSPRPROJ=~/kh-cw          # the tree with this directory
make CXX=clang++ KSPRLIB=/Users/vm/kaspar-hft/lib \
     ACTORS_LIB=/Users/vm/kaspar-hft/actors/cpp/libactors.a test     # unit tests
make ... tools                   # build/kraken_demo_check (step 3)
```

libframe is compiled from this tree into `build/` (BrokerOrderBook and the
KRAKEN/KRAKENF codes are newer than the installed library).

## Configuration and keys

`config/kraken_futures_demo_example.ini` (`[kraken]` section, read by
`load_settings`). With `testnet = true` only `demo-futures.kraken.com` is accepted;
any other host, and `product = spot` with `testnet = true`, is refused before a
connection is made, and the futures session checks the host again before every
request.

Keys are read at run time from `key_file`, by default
`~/.kaspar/keys/kraken_futures_demo.json`, never from the repository:

```json
{"api_key": "<public key from demo-futures.kraken.com, Settings > API keys>",
 "api_secret": "<the base64 private key shown once at creation>"}
```

The demo key needs the "General API" read and "Trading" permissions (futures
permission names; withdrawals are not needed). The file is read only by the
session thread; neither the key nor the secret is logged (`load_keys` errors name
the file, never its content; tested).

## Testing ladder

### Step 1: unit tests (64 tests, all pass)

`make test`, 2026-11-18, macOS, clang:

| File | Tests | What |
|---|---|---|
| `test_auth_config.cpp` | 9 | spot `API-Sign` equals the documented example (`rest/authentication`); futures challenge signature equals the documented example (`futures/websockets`); futures `Authent` equals an independent Python computation (the page has no usable example); nonces increase; key-file errors never contain the key; the testnet guard (production host, look-alike host, spot testnet, demo host with `testnet = false`); the example ini |
| `test_codecs.cpp` | 12 | spot `add_order`/`cancel_order` requests; the documented `add_order` response and error, `executions` "Live Order" and "Execution" examples, snapshot open orders, every `exec_type`; futures `sendorder` bodies for each order type; the documented `PlacedOrderExample`, `RejectedOrderExample`, `ExecutedOrderExample`, `insufficientAvailableFunds`, `apiLimitExceeded`; the `cancelorder` example and `filled`/`notFound`; the `fills` example; `orders/status` mapping |
| `test_kraken_om.cpp` | 31 | `order_entry.md`'s list against `FakeKraken`: logon and registration, bad config gives `nullptr`, orders before logon and while disconnected, logon failure not retried; fields as sent; DAY/IOC/FOK/market/post-only; stops, algorithms, routes, short-sale marks, AT_OPEN, fractional sizes, size and notional limits, cancel/replace refused locally; order-rate throttle; ack with the venue id hash; partial then full fill; venue reject reasons; cancel by venue id and by client id; cancel reject leaves the order working; cancel of unknown and filled orders; unsolicited cancel; fill before ack; fill racing a cancel and after the cancel ack; reports held until the venue id is paired; overfill and unreadable quantity not forwarded; busy; reconnect delivers a missed fill once and does not re-ack; sequence gap reconnects; unknown reports ignored; client ids increase and are not reused after a restart; shutdown with a report in flight |
| `test_futures_session.cpp` | 9 | the futures session against a fake Kraken Futures REST API that checks every request's host and `Authent`: production host refused with no request sent; `authenticationError` is a logon failure; place, fill, cancel with every request signed correctly and sent only to the demo host; a fill reported once over repeated polls; reject statuses; unsolicited cancel (`DEAD_MAN_SWITCH`) and full fill from status polling, fill before `Filled`; an order absent from `orders/status` ended after 3 polls; account history before the process not reported; a fill made while disconnected reported once by the next session; then KrakenOM end to end on this session: `Ack`, `Fill`, `CancAck`, exactly once each |
| `test_sim_kraken.cpp` | 3 | KrakenOM on SimKraken: ack and partial fills, resting order cancel and cancel reject, rejected symbol, a fill made while the link is down arriving once after reconnect |

### Step 2: live public market data

In `../../market_data/kraken/README.md`.

### Step 3: Kraken Futures demo — NOT RUN

`/Users/vm/.kaspar/keys/kraken_futures_demo.json` does not exist, so nothing was
sent to Kraken. `build/kraken_demo_check --config config/kraken_futures_demo_example.ini`
was run and stopped as designed (exit 3, "no key file"); with a production
configuration it refuses (exit 2).

With the key file in place, the same command runs, on `demo-futures.kraken.com`
only:

1. logon of KrakenOM and of the post-trade `KrakenFillsClient`;
2. a limit buy of one unit (0.0001 BTC, PF_XBTUSD) 20% below the bid: `Ack`
   expected; its cancel: `CancAck` expected;
3. an IOC buy of one unit 1% through the ask: `Fill` expected;
4. an IOC sell of one unit 1% through the bid (flat again): `Fill` expected;
5. the OM shut down and a new one started on the same session factory
   (reconnect, new client ids): logon expected;
6. a signed `GET /fills`: for the orders of steps 3 and 4, the venue's fill
   quantities against the SOM `Fill` messages, and every venue `fill_id` against
   the `TradeCapture` messages (each exactly once).

It prints a JSON report and exits 0 only if every check passes. Its requests (three
`sendorder` and one `cancelorder` at 10 cost units each, polling at about 3 units
per second per client) stay within Kraken Futures' 500 units per 10 s.

### Step 4: production — never

Not run, and not to be run by an agent. The spot session can only be checked on
production; that is for the owner, after review.

## Not checked

- Any request to Kraken: signing is checked against the documentation's examples
  and an independent computation, not against Kraken's servers.
- The spot WebSocket session's behaviour (token, subscription, `sequence`,
  `cancel_all_orders_after`): production only.
- Whether futures `orders/status` reports an order that ended more than 5 s
  before a poll (the session's 3-poll absence rule covers it; not observed).
- Futures `cliOrdId` uniqueness across days: set `clid_prefix` to the date in a
  real configuration.
