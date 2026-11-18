# LMAX Broker Trading order manager (not certified)

`LmaxOM` is a Kaspar order manager for LMAX Exchange spot FX over the Broker
Trading FIX 4.4 API. It registers with the SOM for its venue, turns the SOM's
`Order` and `Cancel` into NewOrderSingle, OrderCancelReplaceRequest and
OrderCancelRequest, and LMAX's ExecutionReports and OrderCancelRejects into
`Ack`, `Fill`, `Reject`, `CancAck` and `CancReject`. Order state is kept in
`frame::som::BrokerOrderBook`.

**It is not certified and has never connected to LMAX**, in production or in
the demo environment. There was no demo key file when it was built, so the live
check was not run. It has been run only against a test fake and against
`SimLmaxGateway`, an in-process FIX acceptor on 127.0.0.1 that validates every
request with LMAX's dictionary.

## What was and was not checked

Checked:

- every request as built validates against the dictionary generated from LMAX's
  `fix-spec.json` and has the spec's field order; reports written from the
  spec's field lists parse;
- the actor against the fake (38 tests in all, below), following the list in
  `order_entry.md`;
- the actor over QuickFIX against the simulator: an order far from the market,
  amend, cancel and a marketable order that fills (the live-check scenario),
  partial fills, cancel on disconnect reported after the reconnect, an order
  with ExecInst H surviving a disconnect, a wrong password, and that the
  password reaches neither the FIX log nor the message store.

Not checked: LMAX's behaviour. Which ExecType LMAX uses for an expired IOC, the
size unit, what OrderStatus answers look like, how LMAX resends after a
reconnect, and whether the demo gateway speaks FIX 4.4 are readings
(`feed.md` section 11, `review.md`); the simulator implements the same
readings.

## Contents

| Path | What |
|---|---|
| `feed.md` | Feed description: documents, transport, framing, message catalogue (generated), logon, identifiers, order fields, report mapping, throttles, reconnect, risk checks, pitfalls, ambiguities |
| `review.md` | Decisions needed, settled items, low-impact items |
| `dict/LMAX-FIX44-Trading.xml` | QuickFIX dictionary generated from LMAX's Broker Trading `fix-spec.json` (FIX.4.4) by `../../market_data/lmax/tools/fixspec2qf.py` |
| `include/lmax_om/ExecParse.hpp` | Request builders (D, F, G, H, AD) and report parsers (8, 9, AE, AQ) |
| `include/lmax_om/LmaxSession.hpp`, `src/QfLmaxSession.cpp` | Session interface and the QuickFIX implementation |
| `include/lmax_om/act/LmaxOM.hpp`, `src/LmaxOM.cpp`, `src/LmaxOMConfig.cpp` | The actor, its config, `create_LmaxOM` |
| `include/lmax_om/msg/LmaxReport.hpp` | Session thread to actor; `ExecCopy` to the post-trade client |
| `tools/lmax_om_check.cpp` | Live check (demo or UAT) and `--sim` |
| `config/lmax_om_example.ini` | Example config |
| `tests/` | gtest: parsers and dictionary, actor against `FakeLmax`, actor over QuickFIX against `SimLmaxGateway` |

The shared FIX code (environment guard, key file, redacting log and store,
initiator, simulator, instrument file) is in `../../market_data/lmax`.

## Build and test

Same dependencies as `../../market_data/lmax/README.md`.

```bash
export KSPRPROJ=~/kaspar-hft
make test QF_DIR=/path/to/qfbuild CXX=clang++
make lmax_om_check QF_DIR=/path/to/qfbuild CXX=clang++
./build/lmax_om_check --sim
```

Result on 2026-11-18 (macOS, Apple clang): 38 tests passed (6 dictionary and
parser, 26 actor against the fake, 6 over QuickFIX against the simulator).
`lmax_om_check --sim`: every SOM message matched the simulator's reports, PASS.

## Live check

Not run: `~/.kaspar/keys/lmax_demo.json` did not exist. Its format is in
`../../market_data/lmax/README.md` (it needs `trade_target_comp_id`, which comes
with the demo account).

```bash
./build/lmax_om_check OM.ini MD.ini SEC_ID
```

With `environment = demo` in both configs, the tool connects only to
`fix-order.london-demo.lmax.com:443` and `fix-marketdata.london-demo.lmax.com:443`
(any other host, and any production host, is refused before connecting). It
reads the best bid and offer of the instrument from market data, then:

1. buys one unit (0.1 contract = 1,000 base currency) at 2% below the bid: Ack;
2. amends it to two units one tick higher: CancAck for the old id, Ack for the new;
3. cancels it: CancAck;
4. buys one unit IOC five ticks above the offer: Ack and Fill (or CancAck if LMAX
   expired it).

Every SOM message is printed next to the ExecutionReports LMAX sent, and the fill
is compared with LMAX's Trade report (quantity in contracts, price in ticks). The
tool prints PASS or FAIL. The fill in step 4 is a real demo trade of 1,000 units
of the base currency.

## Use

```cpp
#include "lmax_om/act/LmaxOM.hpp"

// nullptr, with the reason on stderr, for a bad config or a refused host.
actors::Actor *om = create_LmaxOM("/etc/kaspar/lmax_om.ini", som, post_trade_client);
if (om)
  manager->add_to_manage_q(om); // registers with the SOM on Start
```

LMAX has no `en::x` code yet; the config names one (`venue`), see
`../../market_data/lmax/review.md` decision 7.
