# Currenex US Treasury OUCH order manager (not certified)

`CurrenexOM` is a Kaspar order manager for US Treasuries on Currenex over
its OUCH 2.0 binary protocol (Logon Version 200). It registers with the SOM
for its venue, turns the SOM's `Order` and `Cancel` into NewOrder and
OrderCancelRequest messages, and Currenex's reports into `Ack`, `Fill`,
`Reject`, `CancAck` and `CancReject`. Order state is kept in
`frame::som::BrokerOrderBook`.

**It is not certified and has never connected to Currenex**, in production
or in a test environment. It has been run only against the in-process
simulator, the test fake, and a TCP server on 127.0.0.1 inside the tests.
Before any use with Currenex it needs Currenex's connection details, answers
to the questions in `feed.md` section 10, a Currenex code in
`chutil/gencode/enum/def/exch_code.enum` (there is none; the venue is a
config value), and conformance testing with Currenex.

## Specifications

Public, from currenex.com; not in the repository.

| Document | Version | URL |
|---|---|---|
| Ouch 2.0 US Treasury Specification | revision 1, 21 February 2022 (cover 10 February 2022), Version 200 | https://www.currenex.com/support/documentation/ustreasury/UST%20Ouch%20Specification.pdf |

The Firm LP specification (Ouch 3.0, version 300) and the FX OUCH
specification do not apply; `feed.md` section 1 says why.

## Contents

| Path | What |
|---|---|
| `feed.md` | Feed description: documents, transport, framing, the message catalogue, logon, identifiers, order fields, report mapping, throttles, reconnect, risk checks, pitfalls, ambiguities |
| `review.md` | What was checked and how, and what was not |
| `currenex_ust_ouch.tbl` | Field table of the header and the 27 messages (10 to Currenex, 17 from Currenex), checked with `fieldgen.py --check` |
| `include/currenex_ust/CxCodec.hpp`, `src/CxCodec.cpp` | SOH/ETX framing and the header, units, error and logout code texts |
| `include/currenex_ust/CxSession.hpp` | Session interface: logon, new order, cancel, instrument and routing requests, resend, logout; events back |
| `include/currenex_ust/TcpSession.hpp`, `src/TcpSession.cpp` | The real session over TCP with Boost.Asio: framing, outbound numbering, heartbeat answers, logout answers |
| `include/currenex_ust/SimCurrenex.hpp`, `src/SimCurrenex.cpp` | In-process simulated Currenex on a `TimedRunner`: instruments, acks, rejects, trades (with or without pending fills), cancels, cancel on disconnect, persistence and resend with GapFills, heartbeats |
| `include/currenex_ust/act/CurrenexOM.hpp`, `src/CurrenexOM.cpp` | The actor, its config, ClOrderID generation, and the factories `create_CurrenexOM` |
| `include/currenex_ust/msg/CxReport.hpp` | The message from the session thread to the actor |
| `src/CxConfig.cpp`, `config/currenex_ust_example.ini` | Config file reading and checks; example config |
| `tests/` | gtest: codec, actor against `FakeCurrenex`, actor against `SimCurrenex`, `TcpSession` against a loopback server |
| `Makefile` | Generates the header, builds and runs the tests; not part of the top-level build |

The message header is generated into `build/include` from
`currenex_ust_ouch.tbl`; it is not committed.

## Use

```cpp
#include "currenex_ust/act/CurrenexOM.hpp"

// nullptr, with the reason on stderr, if the file is missing or bad.
actors::Actor *om = create_CurrenexOM("/etc/kaspar/currenex_ust.ini", som);
if (om)
  manager->add_to_manage_q(om); // registers with the SOM on Start
```

With `sim = true` in the config, `create_CurrenexOM` uses `SimCurrenex`
instead of a socket. Credentials are read from the file named by
`credentials`, outside the repository. See `config/currenex_ust_example.ini`.

## Build and test

Needs the Kaspar libraries (`libframe`, `liblogger`, `libmq0`, `libchutil`,
`libactors`), Boost, ZeroMQ and Google Test. `BrokerOrderBook.cpp` is
compiled into the test binary.

```bash
export KSPRPROJ=~/kaspar-hft            # source tree: headers, table, fieldgen
make test                               # generate the header, build, run
# libraries elsewhere:
make KSPRLIB=/path/to/lib ACTORS_LIB=/path/to/libactors.a CXX=clang++ test
make syntax GXX=g++-15                  # g++ -fsyntax-only on every source
make check                              # fieldgen --check on the table
```

96 tests, run with clang on macOS: framing, encode of the 10 requests and
decode of the 17 reports against the specification's offsets; the actor
against the fake (logon and registration, refused and transient logouts,
order fields, time in force, instrument mapping by CUSIP, local refusals and
limits, on-behalf-of key lookup, ack, partial, full and fractional fills,
rejects, pending fill workflow, cancel round trip, cancel reject, unsolicited
cancels, residual cancel, IOC, fill before ack, fill racing a cancel, fill
after the cancel ack, unrequested replace acks, busy connection, heartbeats
and GapFills in the sequence, reconnect with replay delivering a missed fill
once, over-replay, duplicate ExecutionIDs, numbering restart without
persistence, instrument indexes per session, orders while disconnected,
unknown reports, id uniqueness across a restart, shutdown with reports in
flight); the actor against the simulator; the TCP session against a
loopback server.

## Comparison

Compared against a private reference implementation after this handler was
written: 254 of 256 fields identical in name, offset, length and type (the
other 2 differ in name only, 0 in layout); 24 of 24 message types in both,
none in only one; 504 of 504 static_asserts on struct sizes, field offsets
and member sizes pass with both headers in one translation unit.
