# Nasdaq DROP post-trade client

A drop-copy client for the Nasdaq Stock Market: it logs in to a Nasdaq DROP
host over SoupBinTCP, receives the day's DROP lines (DROP 2.20), and turns
executions, trade corrections and breaks into normalized
`post_trade::msg::TradeCapture` messages for a subscriber actor (position
keeping, an STP publisher, a recorder). Reports are applied idempotently, so a
replay of the day or an overlap after a reconnect never reaches the subscriber
twice.

**Not certified.** This client has never connected to Nasdaq, in test or
production. It was written from the public specifications and has run only
against the in-process simulator and the test fake in this directory. Before
any use, the ambiguities in `feed.md`, section 10, must be settled with Nasdaq
and the client checked against a real DROP port.

## Contents

| Path | What it is |
|---|---|
| `feed.md` | feed description: documents, transport, framing, message catalogue, lifecycle, mapping, pitfalls, ambiguities |
| `nasdaq_drop.tbl` | field table for SoupBinTCP 3.00 and the DROP line; input to `fieldgen.py` |
| `include/nasdaq_post_trade/drop_msgs.hpp` | generated from the table by the Makefile; do not edit |
| `include/nasdaq_post_trade/DropLine.hpp`, `src/DropLine.cpp` | DROP line parsing and formatting, mapping to `TradeCapture` |
| `include/nasdaq_post_trade/DropSession.hpp` | the session interface the actor uses |
| `include/nasdaq_post_trade/SoupSession.hpp`, `src/SoupSession.cpp` | the real session: SoupBinTCP over TCP, heartbeats, reconnect at the next sequence number |
| `include/nasdaq_post_trade/SimDropServer.hpp`, `src/SimDropServer.cpp` | in-process DROP host on 127.0.0.1, with a trading generator, for `--sim` runs and tests |
| `include/nasdaq_post_trade/act/NasdaqDropClient.hpp`, `src/NasdaqDropClient.cpp` | the actor, and the factory |
| `include/nasdaq_post_trade/if/NasdaqDropClient.hpp` | `create_NasdaqDropClient(config, subscriber)`; returns `nullptr` on a bad configuration |
| `tests/` | gtest: ledger, codecs, the actor against `FakeDropSession`, the real session against the simulator |
| `../common/include/post_trade/msg/TradeCapture.hpp` | the normalized trade-capture message, shared by every venue |
| `../common/include/post_trade/TradeLedger.hpp`, `../common/src/TradeLedger.cpp` | the idempotent ledger, shared by every venue |
| `../common/include/post_trade/EasternTime.hpp`, `../common/src/EasternTime.cpp` | US Eastern date and midnight from UTC, shared with the OUCH order manager |

## Known duplication

`SoupSession` here (one thread, `poll`-based) and
`order_entry/nasdaq_ouch/include/nasdaq_ouch/SoupSession.hpp` (Boost.Asio)
are two SoupBinTCP 3.00 client implementations. A shared SoupBinTCP session,
used by both, is planned; until then a SoupBinTCP fix must be applied to
both.

## Build and run the tests

The Makefile is not part of the top-level build. It needs the Kaspar libraries
(`frame`, `logger`, `mq0`, `chutil`, `libactors.a`), Boost and Google Test.

```bash
cd global_connectivity/non_certified/post_trade/nasdaq
export KSPRPROJ=~/kaspar-hft
make test
```

When the libraries were built in another checkout, or gtest is elsewhere:

```bash
make test KSPRPROJ=/Users/vm/kh-gc \
    KSPRLIB=/Users/vm/kaspar-hft/lib \
    ACTORS_LIB=/Users/vm/kaspar-hft/actors/cpp/libactors.a \
    GTEST_PATH=/opt/homebrew CXX=clang++
```

`make check-table` runs `fieldgen.py --check` on the table; `make catalogue`
prints the message catalogue as markdown. The tests open TCP connections on
127.0.0.1 only, between the session and the simulator in the same process.

## Configure

A boost INFO file with a `nasdaq_drop` section (the keys and defaults are in
`include/nasdaq_post_trade/if/NasdaqDropClient.hpp`):

```
nasdaq_drop
{
    host               <DROP host from Nasdaq>
    port               <port>
    credentials        /secure/nasdaq_drop_credentials.info
    replay_from_start  true
    assets             { INTC 101 MSFT 102 }
}
```

The credentials file holds `username` (at most 6 characters) and `password` (at
most 10), and is supplied at deployment, never committed. With
`sim { enabled true trade_ms 500 symbols "INTC MSFT" }` the client starts
`SimDropServer` on 127.0.0.1 and connects to it instead; host, port and
credentials are then not read.

```cpp
#include "nasdaq_post_trade/if/NasdaqDropClient.hpp"

actors::Actor *drop = create_NasdaqDropClient("config/nasdaq_drop.info", position_keeper);
if (drop)
    manager.add_to_manage_q(drop);   // the session starts on Start
```

## What the client sends

A Login Request, Client Heartbeats, and a Logout Request on shutdown. DROP has
no acknowledgement and takes no other input (`feed.md`, section 6).
