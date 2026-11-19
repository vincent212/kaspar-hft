# Bybit V5 order manager, spot and linear perpetuals (not certified)

`BybitOM` is a Kaspar order manager for Bybit V5 spot (`en::x::BYBIT`) and USDT
linear perpetuals (`en::x::BYBITF`), one OM per venue code. It turns the SOM's
`Order` and `Cancel` into signed REST requests (`POST /v5/order/create`,
`/v5/order/cancel`) and the private stream's `order` and `execution` updates into
`Ack`, `Fill`, `Reject`, `CancAck` and `CancReject`. Order state is kept in
`frame::som::BrokerOrderBook`.

**Not certified. It has never connected to Bybit**, in production or on the
testnet: no testnet key was supplied. It has been run against the test fake and
the in-process simulator only. With `testnet = true` (the default) it refuses
every host except Bybit's testnet and demo hosts, before connecting.

## What was checked, with numbers

| Check | Result |
|---|---|
| Unit tests (`make test`) | 39 of 39 pass: 8 signing, body, host guard and key file; 27 actor tests against `FakeBybit` (the `order_entry.md` list); 4 `SimBybit` |
| Signing against the documentation | The two strings to sign in `v5/guide` match byte for byte. The two example signatures cannot be reproduced because the guide does not give the secret; the HMAC is checked against RFC 4231 case 2 and Python's `hmac` on the guide's strings |
| Ping while the private stream is silent | `read_with_ping` on a live public connection with no subscription: 4 of 4 frames were pongs, 3.0 to 3.3 s apart at a 3 s setting |
| Production refused | `HostGuardRefusesProduction`, `BadConfigGivesNullNotCrash`: `api.bybit.com`, `stream.bybit.com`, `api.bytick.com`, `api.bybit.kz` refused with `testnet = true` |
| Testnet order entry (ladder step 3) | **Not run**: `~/.kaspar/keys/bybit_testnet.json` does not exist |
| Production (ladder step 4) | Never |

Not checked: anything Bybit actually answers (retCodes, report order, PostOnly
reject form, `orderLinkId` reuse rules, rate-limit behaviour), amend, and RSA keys.

## Running step 3 (testnet)

1. Create, by hand, a Bybit testnet account at testnet.bybit.com (or a demo
   trading key in a mainnet account's Demo mode) and an API key with system-generated
   HMAC, Read-Write on Orders for Spot and Derivatives (USDT perpetual), unified
   trading account, one-way position mode. Fund it from the testnet faucet.
2. Write `~/.kaspar/keys/bybit_testnet.json`, outside the repository, mode 600:

   ```json
   {"api_key": "<testnet key>", "api_secret": "<testnet secret>"}
   ```

3. Run, for each category:

   ```bash
   make build/bybit_testnet_check
   ./build/bybit_testnet_check --category linear --symbol BTCUSDT   # add --demo for demo trading
   ./build/bybit_testnet_check --category spot --symbol BTCUSDT
   ```

   It logs in; places a GTC limit buy at 80 % of the bid and cancels it, the same as
   PostOnly, an IOC at 80 % (expects `CancAck`), a PostOnly at the ask (expects
   `Reject`), an IOC buy through the ask and an IOC sell back through the bid
   (expects `Fill`s), and an order left working across an OM restart (cancelled over
   REST). It then fetches `/v5/order/history` and `/v5/execution/list` for every
   `orderLinkId` and checks each SOM message against them: `Ack.xordid` against
   `orderId`, one `Fill` per Trade execution, fill total against `cumExecQty`,
   `CancAck` and `Reject` against `orderStatus`. Exit code 0 only if every check
   passes; one JSON summary line. Run `post_trade/bybit`'s `bybit_fills_check` at the
   same time to check trade capture against the same fills.

Without the key file the tools print what is missing and exit 2; nothing is sent.

## Contents

| Path | What |
|---|---|
| `feed.md` | Hosts, signing, logon, identifiers, order fields, report mapping, throttles, reconnect, pitfalls, ambiguities |
| `include/bybit_oe/BybitAuth.hpp` | HMAC-SHA256 signing (REST, WebSocket auth), key file |
| `include/bybit_oe/BybitPrivate.hpp` | Signed `RestClient`, `PrivateWs` (auth, subscribe, ping while waiting), host guard; shared with `post_trade/bybit` |
| `include/bybit_oe/BybitSession.hpp`, `src/RestWsSession.cpp` | Session interface and the real session (stream thread, REST thread) |
| `include/bybit_oe/act/BybitOM.hpp`, `src/BybitOM.cpp`, `src/BybitConfig.cpp` | The actor and its config |
| `include/bybit_oe/if/BybitOM.hpp` | The factories `create_BybitOM` |
| `../common/include/oe_common/OmSupport.hpp` | Shared by the four crypto order managers: reconnect timer, order-rate token bucket, local rejects, what a lost connection does to the book |
| `include/bybit_oe/SimBybit.hpp`, `src/SimBybit.cpp` | In-process venue for `sim = true` |
| `include/bybit_oe/msg/BybitReport.hpp` | Session thread to actor message |
| `config/bybit_om_example.ini` | Example config (testnet hosts) |
| `tests/` | gtest: signing, `BybitOM` against `FakeBybit`, `SimBybit` |
| `tools/bybit_testnet_check.cpp` | Ladder step 3 (testnet or demo only) |

## Use

```cpp
#include "bybit_oe/if/BybitOM.hpp"
// nullptr, with the reason on stderr, for a bad config or a production host
// with testnet = true.
actors::Actor *om = create_BybitOM("/etc/kaspar/bybit_om.ini", som, en::x::BYBITF);
```

## Build and test

```bash
export KSPRPROJ=~/kaspar-hft
make test KSPRLIB=/path/to/lib ACTORS_LIB=/path/to/libactors.a
```

Needs the Kaspar libraries, Boost (Beast), OpenSSL 3, nlohmann/json, ZeroMQ and
Google Test. `libframe` is compiled from this checkout into `build/kl/` (a
`libframe` built before the BYBIT and BYBITF codes rejects them); `LOCAL_FRAME=0`
links the installed one.
