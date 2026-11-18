# LMAX post-trade client (not certified)

`LmaxTradeClient` captures LMAX Exchange trades for position keeping and STP. It
maps LMAX's TradeCaptureReports (requested on a Broker Trading session) and the
order manager's copies of Trade, TradeCorrect and TradeCancel ExecutionReports to
`post_trade::msg::TradeCapture`, applies them to a `post_trade::TradeLedger`
(`../common`), and sends its subscriber one message per change to the ledger.

**It is not certified and has never connected to LMAX.** There was no demo key
file, so nothing was run against LMAX. It has been run against a test fake and
against `SimLmaxGateway` on 127.0.0.1.

## What was and was not checked

Checked: the mapping of AE and ExecutionReports (trade, correction, cancel,
refusals), the trading-day boundary across both 2026 DST changes, the actor
against the fake (request on logon, a fill seen on both inputs reaching the
subscriber once, resends, the day's replay after a reconnect, a cancel removing
a trade, a rejected request, a new trading day), and end to end over QuickFIX:
an `LmaxOM` fill reaching the ledger through `ExecCopy`, and the client's own
session receiving the day's trade as an AE after a TradeCaptureReportRequest.

Not checked: whether LMAX identifies a fill the same way in an AE and in an
ExecutionReport, how LMAX reports a corrected or busted trade, and whether a
second session is allowed (`review.md` decisions 1-3). The simulator follows the
same readings.

## Contents

| Path | What |
|---|---|
| `feed.md` | Feed description: sources, fields, recovery, semantics, mapping to TradeCapture, pitfalls, ambiguities |
| `review.md` | Decisions needed, settled items, low-impact items |
| `include/lmax_post_trade/TradeMap.hpp`, `src/TradeMap.cpp` | AE and ExecutionReport to TradeCapture; trading-day start |
| `include/lmax_post_trade/act/LmaxTradeClient.hpp`, `src/LmaxTradeClient.cpp` | The actor |
| `tests/` | gtest: mapping, actor against `FakeLmax`, loopback against `SimLmaxGateway` |

Sessions, parsers and the dictionary come from `../../order_entry/lmax`; the
ledger and the message from `../common`.

## Build and test

```bash
export KSPRPROJ=~/kaspar-hft
make test QF_DIR=/path/to/qfbuild CXX=clang++   # builds ../../order_entry/lmax's library first
```

Result on 2026-11-18 (macOS, Apple clang): 16 tests passed (7 mapping, 7 actor
against the fake, 2 over QuickFIX against the simulator).

## Live check

Not run (no `~/.kaspar/keys/lmax_demo.json`; format in
`../../market_data/lmax/README.md`). The order-entry live check
(`../../order_entry/lmax/tools/lmax_om_check.cpp`) produces one demo fill. The
post-trade check for it is: run `LmaxTradeClient` with its own session after that
fill and compare the AE with the ExecutionReport printed by the order-entry check
(ExecID, SecondaryExecID, OrderID, LastQty, LastPx), which settles `review.md`
decision 1. No tool for this was written.

## Use

```cpp
#include "lmax_post_trade/act/LmaxTradeClient.hpp"

lmax::pt::TradeClientConfig cfg;               // venue, instruments, account
auto *client = new lmax::pt::LmaxTradeClient(cfg, {}, positions);  // ExecCopy input only
actors::Actor *om = create_LmaxOM("/etc/kaspar/lmax_om.ini", som, client);
```

Pass `lmax::om::qf_session_factory(...)` instead of `{}` to give the client its
own session.
