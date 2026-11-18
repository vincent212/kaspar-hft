# Coinbase Exchange order manager (FIX 5.0 SP2)

**Not certified.** `CbxOM` has never connected to Coinbase Exchange, the
sandbox included. It has been tested against a fake session, an in-process
simulator, and a QuickFIX acceptor on 127.0.0.1 playing the venue. The feed
description, with its sources and open questions, is `feed.md`.

## Contents

| Path | What |
|---|---|
| `feed.md` | Feed description: logon, identifiers, orders, reports, throttles, reconnect, pitfalls, ambiguities |
| `include/coinbase_exchange_oe/act/CbxOM.hpp`, `src/CbxOM.cpp` | The order manager actor, on `frame::som::BrokerOrderBook` |
| `include/coinbase_exchange_oe/CbxSession.hpp` | The session interface (as the OUCH OM's `OuchSession`) |
| `include/coinbase_exchange_oe/FixMsg.hpp` | FIX tag=value messages without QuickFIX: the OM's logic is tested without it |
| `src/QfSession.cpp` | The real session: QuickFIX `SSLSocketInitiator`, signed Logon, masked message log |
| `src/CbxSessionUtil.cpp` | Logon signature, log masking (shared with the drop copy) |
| `include/coinbase_exchange_oe/SimCbx.hpp`, `src/SimCbx.cpp` | In-process Coinbase for `--sim` |
| `src/CbxConfig.cpp`, `config/cbx_om_example.ini` | Configuration |
| `fix/FIXT11-prod-sand.xml`, `fix/FIX50-prod-sand.xml` | Coinbase's dictionaries, from `cb_exch_fix_dictionaries-20260612.zip` (SHA-256 of the zip 8dfda183db11f08ab89a1194245283cb50e43612ea6323d1d4bdadadc1483dee) |
| `tests/` | `test_cbx_om.cpp` (35 actor tests and 5 unit tests), `test_sim_cbx.cpp` (3), `test_qf_session.cpp` (5, with QuickFIX), `FakeCbx.hpp`, `TestUtil.hpp` |

The market-data directory provides `coinbase_exchange/Rest.hpp` (signature,
credentials, endpoints, the sandbox guard) and `Num.hpp` (decimals, UUIDs,
times).

## Build and test

```bash
export KSPRPROJ=~/kaspar-hft
make test                                   # without QuickFIX: 43 tests
make test QUICKFIX_PATH=/path/to/quickfix   # with it: 48 tests
```

`QUICKFIX_PATH` must hold `include/quickfix/` and `libquickfix.a` built with
`HAVE_SSL`. QuickFIX is not installed on this Mac; the tests here used a static
library compiled from the vincent212/quickfix sources in a scratch directory
(clang++, `-std=c++17 -DHAVE_SSL=1`, OpenSSL 3 from Homebrew; `config.h`
written by hand with `HAVE_CXX17` and `HAVE_SSL`, `Allocator.h` empty, as
QuickFIX's CMake generates it). The libraries must be built with the
`COINBASEX` venue code (`chutil/gencode/enum/def/exch_code.enum`).

## Checked (2026-11-18, macOS, clang)

- 48 of 48 tests pass with QuickFIX (43 without). They cover the list in
  `order_entry.md`: logon and registration with the SOM; a bad config gives
  `nullptr`; orders before logon or while disconnected are refused; the fields
  of every order type, side and time in force as sent; unsupported orders
  refused locally; local limits; Ack with the exchange order id; partial then
  full fill; reject; business reject; cancel round trip with a new ClOrdID;
  cancel reject leaves the order working; cancel of unknown or filled orders;
  unsolicited cancel; IOC partial fill then expiry; fill before ack (one Ack,
  before the Fill); fill racing a cancel; fill after the cancel ack; restatement
  is not a fill; a fill not in whole units is not guessed; busy connection;
  reconnect delivers a missed fill once (from OrderStatusRequest) and skips the
  venue's resend; replayed reports (same ExecID, or a CumQty already counted)
  applied once; status after reconnect settles a cancel and a never-arrived
  order; unknown reports ignored; ClOrdIDs unique and not reused across a
  restart; shutdown with reports in flight.
- The Logon signature against vectors computed with Python's `hmac`,
  `hashlib` and `base64` (the documentation gives code, no test vectors), and
  the REST/WebSocket signature the same way.
- Coinbase's dictionaries load in QuickFIX and validate the OM's
  NewOrderSingle, OrderCancelRequest and OrderStatusRequest, and an
  ExecutionReport with the fee group.
- End to end over QuickFIX on 127.0.0.1 (plain TCP): the real `QfSession`
  logs on to a QuickFIX acceptor that checks the signature with the test
  secret; NewOrderSingle → New + Trade → `Ack`, `Fill`; OrderCancelRequest →
  Canceled → `CancAck`; a wrong secret is rejected and not retried. The message
  log holds `554=***` and `96=***` and no passphrase.
- The sandbox guard: a sandbox configuration refuses a production host, a
  sandbox host with production credentials, and a look-alike host name; the
  QuickFIX session refuses before connecting.
- TLS handshake only (no FIX message) to `fix-ord.sandbox.exchange.coinbase.com:6121`:
  certificate valid, chain to Amazon Root CA 1, with and without SNI.

## Not checked

- Anything against Coinbase: logon, order, fill, cancel, reject, throttling,
  cancel-on-disconnect, the Saturday logout, resends. No sandbox keys were
  supplied (step 3 below).
- TLS through QuickFIX (the loopback test is plain TCP); QuickFIX does not
  check the host name.
- Whether Coinbase accepts `RawDataLength` (95), which its published FIXT11
  dictionary does not define (feed.md section 10).
- `SendingTime` milliseconds: QuickFIX in this build writes `.000`; the
  signature covers the string sent, so it is consistent, but the value is
  coarse.
- Replace (35=G), batches (U6, U4), TPSL, `CashOrderQty`: not implemented.
- Decisions for the owner: feed.md section 10.

## Credentials

Read at run time from `session.key_file`, by default
`~/.kaspar/keys/coinbase_exchange_sandbox.json`, mode 600 (refused if group or
others can read it):

```json
{
  "environment": "sandbox",
  "key": "<API key>",
  "secret": "<API secret, base64 as Coinbase gives it>",
  "passphrase": "<API passphrase>",
  "oe_key": "<optional: a second key for the FIX order-entry session>",
  "oe_secret": "...", "oe_passphrase": "...",
  "dc_key": "<optional: a third key for the drop copy>",
  "dc_secret": "...", "dc_passphrase": "..."
}
```

Coinbase allows one FIX connection per API key, so a run with the order
manager, the drop copy and the authenticated market data at once needs three
keys (`oe_*`, `dc_*`, and `key` for the WebSocket). Keys are created in the
sandbox web interface, https://public.sandbox.exchange.coinbase.com (profile
menu, API, New API Key), with the **View** and **Trade** permissions; never
**Transfer**. The key file is never copied into the repository or a log.

## Step 3 of the testing ladder (sandbox): what will run

With the key file present, against `fix-ord.sandbox.exchange.coinbase.com:6121`
only (the configuration has `environment = sandbox`; the handler refuses any
other host):

1. Logon with the signed Logon; check the Logon answer and heartbeats.
2. One limit buy of the smallest size on BTC-USD, priced 20% below the
   sandbox's best bid (GET `/products/BTC-USD/book?level=1` on
   `api-public.sandbox.exchange.coinbase.com`), GTC; check New, the Ack's
   exchange id against the order's `OrderID`, and the order in the sandbox's
   `full` channel under the same id.
3. Cancel it; check Canceled and `CancAck`.
4. One IOC far from the market: Expired, `CancAck`.
5. One order that fills against the sandbox book (a marketable limit of the
   minimum size), if the sandbox's liquidity allows; check `Fill` against REST
   `GET /fills?order_id=` with the key.
6. Disconnect and reconnect; check the status reconciliation and that no report
   is applied twice.
7. Logout.

`order_entry.md`'s live check is logon, one order far from the market, its
cancel, logout; steps 4-6 go further and run only if the owner asks.
