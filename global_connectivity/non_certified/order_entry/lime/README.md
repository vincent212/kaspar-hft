# Lime C++ Trading API order manager (not certified)

`LimeOM` is a Kaspar order manager for US equities through Lime Trading Corp.
over the Lime C++ Trading API. It registers with the SOM for its venue
(`en::x::LIME`), turns the SOM's `Order` and `Cancel` into `placeOrder`,
`placeAlgoOrder` and `cancelOrder`, and the API's callbacks into `Ack`, `Fill`,
`Reject`, `CancAck` and `CancReject`. Order state is kept in
`frame::som::BrokerOrderBook`.

**It is not certified and has never connected to Lime.** There is no account,
and the Lime SDK (headers and library) is not public, so it has never been
built against the vendor's SDK. It was written from Lime's public
documentation (`feed.md` section 1) and run only against a test fake, an
in-process simulator, and a stand-in for the SDK written from the public
overview.

## What was and was not checked

Checked:

- the actor against `FakeLime` (43 tests), following the list in
  `order_entry.md`: login and registration, bad config, the fields of each side,
  short sale, time in force, route and algo, local refusals and limits, ack,
  partial and full fills, rejects, cancel round trip and cancel reject,
  unsolicited cancel, fill before ack, fill racing a cancel, fill after the
  cancel ack, bust and correction not applied, busy connection, reconnect with
  eventId replay and duplicate skipping, unknown and manual orders, client ids
  across a restart, shutdown with reports in flight;
- the actor against `SimLime` (4 tests);
- `SdkSession` compiled against `sdk_doc/LimeTradingApiDoc.hh` (a transcription
  of the overview's declarations, not the vendor header) with
  `tests/SdkStub.cpp` as the library (6 tests): constructor arguments,
  `placeOrder`/`placeAlgoOrder` arguments, `CallStatus` and exceptions,
  callbacks, and `LimeOM` over it.

Not checked: Lime's behaviour, and that `SdkSession` compiles against the
real SDK (header names and the `ExecType` enumerators are guesses, marked
`GUESS` in `sdk_doc/`). The readings are in `feed.md` section 10 and
`review.md`.

## Contents

| Path | What |
|---|---|
| `feed.md` | Documents, transport, API surface, logon, identifiers, order fields, report mapping, busy and risk, reconnect, ambiguities, pitfalls |
| `review.md` | Decisions needed, settled items, low-impact items |
| `include/lime_om/LimeApi.hpp` | Session interface (`LimeSession`, `SessionEvents`) and Lime types |
| `include/lime_om/act/LimeOM.hpp`, `src/LimeOM.cpp`, `src/LimeConfig.cpp` | The actor, its config, `create_LimeOM` |
| `include/lime_om/msg/LimeEvent.hpp` | Callback thread to actor |
| `src/SdkSession.cpp` | `LimeSession` over `LimeBrokerage::TradingApi`; compiled only with `LIME_SDK_HEADER` |
| `include/lime_om/SimLime.hpp`, `src/SimLime.cpp` | In-process simulator |
| `sdk_doc/LimeTradingApiDoc.hh` | Declarations transcribed from the overview, for tests only |
| `config/lime_om_example.ini` | Example config (credentials in a separate file) |
| `tests/` | gtest: actor against `FakeLime` and `SimLime`; `SdkSession` against `SdkStub` |

## Build and test

```bash
export KSPRPROJ=~/kaspar-hft
make test CXX=clang++ KSPRLIB=/path/to/lib ACTORS_LIB=/path/to/libactors.a
```

With the vendor SDK (never done; the header's file name is not public):

```bash
make LIME_SDK_HEADER='"TradingApi.hh"' LIME_SDK_INC=-I/path/to/sdk/include \
     LIME_SDK_LIB='-L/path/to/sdk/lib -l...' build/run_lime_tests
```

Result on 2026-11-18 (macOS, Apple clang): 53 tests passed (43 actor against
the fake, 4 simulator, 6 `SdkSession` against the stub).

## Live check

Not run: no account, no SDK. The check `order_entry.md` describes (login, one
order far from the market and its cancel, logout) would run only in a Lime test
environment with credentials the owner supplies.

## Use

```cpp
#include "lime_om/act/LimeOM.hpp"

// nullptr, with the reason on stderr, for a bad config, or when the SDK is
// wanted and this build has none.
actors::Actor *om = create_LimeOM("/etc/kaspar/lime_om.ini", som);
if (om)
  manager->add_to_manage_q(om); // registers with the SOM on Start
```

## Comparison with a private reference implementation

Compared afterwards with a private, tested implementation (counts only): of
its 21 actor tests, 19 scenarios are covered here with the same outcome, 2
with a different outcome or a weaker check, 0 not covered; 18 of the 43 actor
tests here have no counterpart there. 17 behaviour differences were found: the
public documents decide 4 (all in this build's favour) and do not decide 13;
none found where this build contradicts the documents.
