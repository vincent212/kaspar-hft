# Clear Street order manager (not certified)

`ClearStreetOM` is a Kaspar order manager for US common stocks through Clear
Street over its FIX 4.2 order-entry API. It registers with the SOM for
`en::x::CLEARSTREET`, turns the SOM's `Order` and `Cancel` into
NewOrderSingle, OrderCancelReplaceRequest and OrderCancelRequest, and Clear
Street's ExecutionReports, OrderCancelRejects and session Rejects into `Ack`,
`Fill`, `Reject`, `CancAck` and `CancReject`. Order state is kept in
`frame::som::BrokerOrderBook`.

It was written from Clear Street's public FIX API guide
(`docs.clearstreet.com/guides/fix-api`, retrieved 2026-11-18) and changelog;
`feed.md` section 1 lists the documents.

**It is not certified and has never connected to Clear Street.** There is no
account, no API key, and Clear Street publishes no test environment. It has
been run only against a test fake, against `SimClearStreet` (an in-process
simulated session), and over QuickFIX against a local acceptor on 127.0.0.1
that plays Clear Street as the guide describes.

## What was and was not checked

Checked:

- every request as built matches the guide's examples field for field and
  validates against `build/ClearStreet-FIX42.xml` (QuickFIX's FIX 4.2 plus the
  guide's additions, `tools/mkdict.py`); the guide's example reports parse;
- the actor against the fake, following the test list in `order_entry.md`
  (below);
- the actor over QuickFIX against the local acceptor: Logon with 553/554, an
  order acked and filled, a wrong API key that never logs on, and a session
  Reject (unauthorized account) matched to its order by RefSeqNum.

Not checked: Clear Street's behaviour. The format of the account list in the
Logon, the session schedule's time zone, whether 150=8 follows 150=0, cancel
on disconnect, and how short sales marked with OpenClose (77) are treated are
readings (`feed.md` section 10, `review.md`); the simulator and the acceptor
implement the same readings.

## Contents

| Path | What |
|---|---|
| `feed.md` | Feed description: documents, transport, dictionary, messages, logon, identifiers, report mapping, throttles, reconnect, risk checks, pitfalls, ambiguities |
| `review.md` | Decisions needed, settled items, low-impact items |
| `tools/mkdict.py` | Writes the QuickFIX dictionary from QuickFIX's `FIX42.xml` |
| `include/clearstreet_om/CsFix.hpp`, `src/CsFix.cpp` | Request builders (D, F, G) and report parsers (8, 9, 3) |
| `include/clearstreet_om/CsSession.hpp`, `src/QfCsSession.cpp` | Session interface and the QuickFIX implementation |
| `include/clearstreet_om/SimClearStreet.hpp`, `src/SimClearStreet.cpp` | Simulated session for `environment = sim` |
| `include/clearstreet_om/act/ClearStreetOM.hpp`, `src/ClearStreetOM.cpp`, `src/ClearStreetOMConfig.cpp` | The actor, its config, `create_ClearStreetOM` |
| `include/clearstreet_om/msg/CsReport.hpp` | Session thread to actor |
| `config/` | Example config and instrument file |
| `tests/` | gtest: builders and parsers, actor against `FakeClearStreet`, simulator, QuickFIX against a local acceptor |

## Build and test

Not part of the top-level build. Needs the kaspar libraries (frame, logger,
mq0, chutil, actors), Boost, ZeroMQ, OpenSSL, Google Test, a static QuickFIX
built with SSL, and QuickFIX's `FIX42.xml`.

```bash
export KSPRPROJ=~/kaspar-hft
make test CXX=clang++ QF_DIR=/path/to/qfbuild QF_SPEC=/path/to/quickfix/spec/FIX42.xml
```

Result on 2026-11-18 (macOS, Apple clang): 49 tests passed (9 builders,
parsers and dictionary; 32 actor against the fake; 5 simulator; 3 over
QuickFIX against the local acceptor).

## Live check

Not run, and not possible yet: Clear Street names no UAT or certification
environment, and there are no credentials. The config refuses
`environment = production` unless `production_ok = yes`. When a test session
exists, the check is the one in `order_entry.md`: logon, one order far from
the market and its cancel, logout.

## Use

```cpp
#include "clearstreet_om/act/ClearStreetOM.hpp"

// nullptr, with the reason on stderr, for a bad config.
actors::Actor *om = create_ClearStreetOM("/etc/kaspar/clearstreet_om.ini", som);
if (om)
  manager->add_to_manage_q(om); // registers with the SOM on Start
```

The API key is read from the file `key_file` names, outside the repository.

## Comparison with a private implementation

After this order manager was frozen, it was compared with a private, tested
Clear Street order manager. Of that implementation's 19 test scenarios, this
one covers 19 (2 only partly) and expects the same result in 16. This one has
16 actor-test scenarios the other does not. Of 27 differences in fields and
behaviour, the public documentation supports this implementation in 5, the
other in 0, and decides 21 neither way; in 1 this implementation is wrong by
its own reading.
