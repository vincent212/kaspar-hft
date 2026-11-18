# LMAX Broker Market Data handler (not certified)

`LmaxMdFeed` turns LMAX Exchange's FIX 4.4 market data (Broker Market Data API)
into Kaspar records for OB: each MarketDataSnapshotFullRefresh (W) is diffed
against the previous one and sent through `sim::Mbp2Mbo` as synthetic orders, as
`sim/` does for price-level crypto feeds.

**It is not certified and has never connected to LMAX**, in production or in the
demo environment. There was no demo key file when it was built
(`~/.kaspar/keys/lmax_demo.json`), so the live check below was not run. What was
checked is listed under Tests. The open questions are in `review.md`.

## What was and was not checked

Checked, on this Mac with clang, against LMAX's published JSON specification and
an in-process simulator:

- the QuickFIX dictionary generated from LMAX's `fix-spec.json` parses and
  validates every message LMAX's spec defines; a MarketDataRequest as the handler
  builds it validates and has the spec's field order;
- the snapshot parser, the request id format, the environment guard (demo hosts
  only for a demo config; production refused), time parsing;
- `LmaxMdSync` on constructed snapshots and on 3,000 random snapshots with trades:
  after every W the book rebuilt from the records equals LMAX's book, and is never
  crossed;
- the full feed over QuickFIX against `SimLmaxGateway` on 127.0.0.1: logon with
  username, password and 141=Y, subscription, books checked after every W, a gap
  the gateway fills, a gap it does not fill (reconnect and resync), a disconnect,
  a MarketDataRequestReject, a wrong password.

Not checked: anything about LMAX's real behaviour. In particular the size unit,
whether trades repeat across snapshots, LMAX's answer to a ResendRequest, the
update rate, and the FIX version the demo gateway speaks are readings
(`feed.md` section 10). The simulator implements the same readings, so the
loopback tests do not test them.

## Contents

| Path | What |
|---|---|
| `feed.md` | Feed description: documents, transport, framing, message catalogue (generated from LMAX's JSON), sequencing, semantics, mapping, instruments, pitfalls, ambiguities |
| `review.md` | Decisions needed, settled items, low-impact items |
| `dict/LMAX-FIX44-MarketData.xml` | QuickFIX dictionary, generated from LMAX's Broker Market Data `fix-spec.json` by `tools/fixspec2qf.py`; not edited by hand |
| `tools/fixspec2qf.py` | LMAX `fix-spec.json` to QuickFIX XML (`-o`), validity check (`--check`), message tables for `feed.md` (`--markdown`) |
| `include/lmax_fix/` and `src/` (`LmaxFix`, `RedactLog`, `FixInitiator`, `SimLmaxGateway`) | Shared by the three LMAX directories: environment guard, key file, QuickFIX settings in memory, log and store that redact the password, the initiator, the simulator |
| `include/lmax_md/MdParse.hpp` | V builder, W and Y parsers, instrument file |
| `include/lmax_md/LmaxMdSync.hpp` | One instrument's W stream into Mbp2Mbo, no network |
| `include/lmax_md/BookCheck.hpp` | Rebuilds a book from the records and compares it with LMAX's |
| `include/lmax_md/act/LmaxMdFeed.hpp` | The actor; `create_LmaxMdFeed(config, obs)` |
| `tools/lmax_md_check.cpp` | Live check (demo or UAT) and `--sim` run |
| `config/` | Example config, instrument file for EUR/USD and USD/JPY |
| `tests/` | gtest: dictionary and parsers, sync, feed against the simulator |

## Build and test

Needs the Kaspar libraries (`libframe`, `liblogger`, `libmq0`, `libchutil`,
`libactors`), Boost, ZeroMQ, OpenSSL, Google Test, and a static QuickFIX built
with `HAVE_SSL` (`QF_DIR/include/quickfix/*.h`, `QF_DIR/libquickfix.a`).
`sim/src/Mbp2Mbo.cpp` and `LevelWindow.cpp` are compiled in.

```bash
export KSPRPROJ=~/kaspar-hft
make test QF_DIR=/path/to/qfbuild CXX=clang++
make lmax_md_check QF_DIR=/path/to/qfbuild CXX=clang++
./build/lmax_md_check --sim 1          # one minute against the simulator
make dict SPEC=/path/to/broker-market-data.fix-spec.json   # regenerate the dictionary and diff
```

Result on 2026-11-18 (macOS, Apple clang, QuickFIX from vincent212/quickfix):
26 tests passed, three runs in a row. `lmax_md_check --sim 0.25`: 560 snapshots
per instrument, 0 mismatches, 0 crossed, 0 sequence breaks, PASS.

## Live check

Not run: `~/.kaspar/keys/lmax_demo.json` did not exist.

The key file (never in the repository) is JSON:

```json
{
  "environment": "demo",
  "username": "<LMAX demo username>",
  "password": "<LMAX demo password>",
  "md_sender_comp_id": "<optional; defaults to the username>",
  "md_target_comp_id": "<TargetCompID LMAX gives for the demo market-data gateway>",
  "trade_sender_comp_id": "<optional; defaults to the username>",
  "trade_target_comp_id": "<TargetCompID for the demo trading gateway>",
  "account": "<optional LMAX account id>"
}
```

LMAX's documents do not give the demo TargetCompIDs; they come with the demo
account. `environment` must be `demo` (or `uat`), and must match the config.

With the file in place:

```bash
sed 's#/path/to#'$PWD'/../../../..#' config/lmax_md_example.ini > /tmp/lmax_md.ini   # set key_file too
./build/lmax_md_check /tmp/lmax_md.ini 30
```

The run connects only to `fix-marketdata.london-demo.lmax.com:443` (the guard
refuses any other host for a demo config, and any production host), logs on with
141=Y, subscribes EUR/USD and USD/JPY at depth 20 with trades, and for 30 minutes
checks after every W that the book rebuilt from the records equals LMAX's book
(snapshot against the records produced from it), that the rebuilt book is never
crossed, and that every MsgSeqNum QuickFIX delivers follows the previous one
(gaps show as ResendRequests). It prints the first two W of each instrument (the
size unit and the trade entries, `review.md` decisions 1 and 4), a summary each
minute, and PASS or FAIL. The FIX log, if `log_path` is set, has the password
replaced by `***`.

## Use

```cpp
#include "lmax_md/act/LmaxMdFeed.hpp"

// obs[i] is the OB of the i-th instrument in the config's instrument file.
actors::Actor *feed = create_LmaxMdFeed("/etc/kaspar/lmax_md.ini", obs);
if (feed)
  manager->add_to_manage_q(feed);
```

`create_LmaxMdFeed` returns `nullptr`, with the reason on stderr, for a bad
config or a refused host. LMAX has no `en::x` code yet (`review.md` decision 7).
