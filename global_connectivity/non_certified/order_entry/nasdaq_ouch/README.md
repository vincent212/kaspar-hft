# Nasdaq OUCH 5.0 order manager (not certified)

`OuchOM` is a Kaspar order manager for Nasdaq U.S. equities over OUCH 5.0 and
SoupBinTCP 3.0. It registers with the SOM for `en::x::NASDAQ`, turns the SOM's
`Order` and `Cancel` into OUCH Enter Order and Cancel Order requests, and
Nasdaq's reports into `Ack`, `Fill`, `Reject`, `CancAck` and `CancReject`.
Order state is kept in `frame::som::BrokerOrderBook`.

**It is not certified and has never connected to Nasdaq**, in production or in
a test environment. It has been run only against the in-process simulator,
the test fake, and a SoupBinTCP server on 127.0.0.1 inside the tests. Before
any use with Nasdaq it needs Nasdaq's port details, the decisions listed in
`feed.md` section 10, and certification in Nasdaq's test environment.

## Specifications

Public, from nasdaqtrader.com; not in the repository.

| Document | Version | URL |
|---|---|---|
| OUCH 5.0 Order Entry Specification | revision 1.05, October 2025 | https://www.nasdaqtrader.com/content/technicalsupport/specifications/TradingProducts/OUCH5.0.pdf |
| SoupBinTCP | 3.00, 02/22/2017 | https://www.nasdaqtrader.com/content/technicalsupport/specifications/dataproducts/soupbintcp.pdf |

## Contents

| Path | What |
|---|---|
| `feed.md` | Feed description: documents, transport, framing, the message catalogue, logon, identifiers, order fields, report mapping, throttles, reconnect, risk checks, pitfalls, ambiguities |
| `ouch50.tbl` | Field table of the 25 OUCH 5.0 messages (8 to Nasdaq, 17 from Nasdaq), checked with `fieldgen.py --check` |
| `include/nasdaq_ouch/OuchSession.hpp` | Session interface: logon, enter, cancel, account query, logout; events back |
| `include/nasdaq_ouch/SoupSession.hpp`, `src/SoupSession.cpp` | The real session: SoupBinTCP over TCP with Boost.Asio |
| `include/nasdaq_ouch/SimOuch.hpp`, `src/SimOuch.cpp` | In-process simulated OUCH port on a `TimedRunner`: accepts, rejects configured symbols, fills after a delay in one or more executions, cancels, answers Account Query, replays on login |
| `include/nasdaq_ouch/act/OuchOM.hpp`, `src/OuchOM.cpp` | The actor and its config |
| `include/nasdaq_ouch/if/OuchOM.hpp` | The factories `create_OuchOM`; `nullptr` on a bad configuration |
| `include/nasdaq_ouch/OuchCodec.hpp`, `src/OuchCodec.cpp` | Optional Appendage (TagValue) building and lookup, reason texts, Eastern midnight (from `post_trade/common`'s `EasternTime`, shared with the DROP client) |
| `include/nasdaq_ouch/msg/OuchReport.hpp` | The message from the session thread to the actor |
| `src/OuchConfig.cpp`, `config/ouch_example.ini` | Config file reading and checks; example config |
| `tests/` | gtest: codec, actor against `FakeOuch`, actor against `SimOuch`, `SoupSession` against a loopback server |
| `Makefile` | Generates the headers, builds and runs the tests; not part of the top-level build |

The OUCH and SoupBinTCP headers are generated into `build/include` from
`ouch50.tbl` and `global_connectivity/non_certified/market_data/nasdaq/soupbintcp.tbl`; they are not committed.

## Known duplication

`SoupSession` here (Boost.Asio) and
`post_trade/nasdaq/include/nasdaq_post_trade/SoupSession.hpp` (one thread,
`poll`-based) are two SoupBinTCP 3.00 client implementations. A shared
SoupBinTCP session, used by both, is planned; until then a SoupBinTCP fix
must be applied to both.

## Use

```cpp
#include "nasdaq_ouch/if/OuchOM.hpp"

// nullptr, with the reason on stderr, if the file is missing or bad.
actors::Actor *om = create_OuchOM("/etc/kaspar/ouch.ini", som);
if (om)
  manager->add_to_manage_q(om); // registers with the SOM on Start
```

With `sim = true` in the config, `create_OuchOM` uses `SimOuch` instead of a
socket. Credentials are read from the file named by `credentials`, outside
the repository. See `config/ouch_example.ini`.

## Build and test

Needs the Kaspar libraries (`libframe`, `liblogger`, `libmq0`, `libchutil`,
`libactors`), Boost, ZeroMQ and Google Test. `BrokerOrderBook.cpp` is
compiled into the test binary.

```bash
export KSPRPROJ=~/kaspar-hft            # source tree: headers, tables, fieldgen
make test                               # generate headers, build, run
# libraries elsewhere:
make KSPRLIB=/path/to/lib ACTORS_LIB=/path/to/libactors.a CXX=clang++ test
make syntax GXX=g++-15                  # g++ -fsyntax-only on every source
make check                              # fieldgen --check on ouch50.tbl
```

84 tests: encode of the 8 requests and decode of the 17 reports against the
specification's offsets; the actor against the fake (logon and
registration, config checks, order fields per type, side and time in force,
local refusals and limits, ack, partial and full fills, rejects, cancel
round trip, cancel reject, cancel pending, unsolicited and partial cancels,
fills after a partial cancel, fill before ack, fill racing a cancel, fill
after the cancel ack, busts, busy connection, reconnect with a missed fill
delivered once, a cancel in flight when the link drops, replay and
duplicate match numbers, orders while disconnected, end of session and its
purge of done orders, unknown reports, id uniqueness across a restart,
shutdown with reports in flight); the actor against the simulator; the
SoupBinTCP session against a loopback server.
