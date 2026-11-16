<!--
    Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
    Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
    Licensed under the MIT License. See LICENSE file in the project root.
-->

# bnsim: the simulator on live Binance data

`bnsim` runs the replay simulator's order book, shadow lights and simulated
order manager against Binance's live market data instead of a CME recording.
Spot and USD-M futures, one symbol per process.

```
Binance WebSocket + REST snapshot
   -> BinanceFeed   (own thread; TLS via Boost.Beast and OpenSSL)
   -> BinanceSync   (Binance's local order book procedure)
   -> Mbp2Mbo       (price levels -> per-order records)
   -> OB -> light22 lights -> SOM (simulated fills) -> SlippageProbe CSV
```

## Build

```bash
cd ~/kaspar-hft
KSPRPROJ=$PWD make bnsim       # sim/binance/bnsim
```

`make bnsim` builds only the libraries `bnsim` links. It needs neither the CME
codecs (`make schema`) nor `mdp3` and `ilink`, so it works on a fresh checkout.

Needs OpenSSL (`libssl-dev` on Ubuntu, `brew install openssl@3` on macOS).
Boost.Beast comes with Boost.

## Run

```bash
mkdir -p ~/bnruns/btc && cd ~/bnruns/btc
~/kaspar-hft/sim/binance/bnsim --config ~/kaspar-hft/sim/config_binance \
    --symbol BTCUSDT --probe-size 5 --probe-out probe.csv --quiet
```

| argument | meaning |
|---|---|
| `--config` | directory with `binance.ini`, `lights.ini`, `som.ini` (`sim/config_binance`) |
| `--symbol` | a section of `binance.ini`: `BTCUSDT`, `ETHUSDT` |
| `--futures` | USD-M perpetual instead of spot |
| `--probe-size` | chunks per probe leg. Without it there is no probe and no lights: the book and SOM only, with orders from `--mqport` |
| `--probe-window-min` | minimum length of a probe window (default 10) |
| `--probe-days` | how long the probe keeps opening windows (default 7) |
| `--probe-out` | the probe CSV, one row per window (columns: `sim/README.md`) |
| `--ob-delay-us`, `--ob-feed-delay-us` | the latency model, as in `sim` |
| `--dump` | the feed alone: prints the top of the synthetic book once a second |

`--dump` is the quick check that the host reaches Binance:

```
BinanceFeed_BTCUSDT synced bid 64199.09 x 357 chunks  ask 64199.10 x 114 chunks  events 116 trades 283 gaps 0 ...
```

Market time is Binance's event time, so the latency model and the probe's
windows run on the exchange's clock.

### Unattended: `run.sh`

```bash
~/kaspar-hft/sim/binance/run.sh BTCUSDT &
~/kaspar-hft/sim/binance/run.sh BTCUSDT --futures &
~/kaspar-hft/sim/binance/run.sh ETHUSDT &
~/kaspar-hft/sim/binance/run.sh ETHUSDT --futures &
```

Each start runs in `~/bnruns/<SYMBOL>[_F]/<UTC start time>/` (`BNRUNS` moves
it) with its probe CSV, logs, `out.txt` and `err.txt`. A process that exits is
started again after 5 s in a new directory; nothing carries over. The probe
size is `PROBE_SIZE` chunks a leg (default 5); other arguments go to `bnsim`.

## Orders from Python

`bnsim` takes orders and serves the book and fills over zmq on `--mqport`
(default 7777; `run.sh` uses `MQPORT`, default none). `kaspar_client.py` in
this directory is a client:

```python
from kaspar_client import Kaspar        # pip install pyzmq

k = Kaspar("tcp://localhost:7777")
bid, ask = k.bbo("BTCUSDT")             # ticks, from the book at the time of the request
oid = k.order("BTCUSDT", "BUY", 1, bid, "BINANCE")   # 1 chunk at the bid
k.cancel(oid, "BINANCE")
mine = [f for f in k.fills() if int(f["ID"]) == oid]
```

`python3 kaspar_client.py [tcp://host:port] [SYMBOL] [VENUE]` runs a short
demo: one order resting under the bid, one through the ask, a cancel, then the
orders and fills.

- Prices are in ticks (price / tick) and sizes in chunks. The symbol is
  `BTCUSDT` or `BTCUSDT_F`, the venue `BINANCE` or `BINANCEF`.
- Orders go through SOM and OB like the lights' own: OB's latency model,
  queue position, SOM's limits. A marketable order fills at the best price on
  the other side.
- Requests are request/reply, one at a time: fills are polled, and
  `fills()` returns every fill in the process, the lights' included; keep the
  ids `order()` returned.

## Chunks

OB fills our orders by queue position, which needs individual orders. Binance
publishes price levels. `Mbp2Mbo` cuts every level into synthetic orders of one
size, the chunk, `ceil(quantity / chunk)` of them, kept in a FIFO per level:

| event | effect on the level's queue |
|---|---|
| the level grows | new chunks join the back |
| the level shrinks without a trade | chunks leave from the back (cancels) |
| a trade | chunks leave from the front, each as an execution and then a delete |

All sizes in OB, the lights and the probe are therefore in chunks: a probe leg
of 5 on BTCUSDT spot is 0.05 BTC. Trades smaller than a chunk accumulate per
side until a whole chunk has traded. A level holding less than a chunk counts
as one.

Our order fills the way it does on CME data: when an execution reaches the real
order behind it, or when the market trades through its price.

`binance.ini` sets the tick, the chunk and the ladder per symbol (spot and
futures separately):

| | tick | chunk | ladder |
|---|---|---|---|
| BTCUSDT spot | 0.01 | 0.01 BTC | 524,288 ticks |
| BTCUSDT perp | 0.10 | 0.1 BTC | 524,288 ticks |
| ETHUSDT spot | 0.01 | 0.1 ETH | 131,072 ticks |
| ETHUSDT perp | 0.01 | 1 ETH | 131,072 ticks |

## Prices and the ladder

Prices reach OB as tick counts (`price / tick`): BTC at 64,000 USD on spot is
tick 6,400,000. OB's price ladder is a ring of `ladder` slots per side
(`ladder_ticks`, see `ring_mask` in `frame/ob/act/OB.hpp`). Each side's
window hangs from its best price; a new best price drops the levels that fall
off the far end, and an update deeper than the window is ignored.

## Sync and reconnects

`BinanceSync` follows Binance's procedure for a local book: buffer the diff
stream, fetch a REST snapshot, drop the events it already contains, then check
that every event follows the previous one (`U == prev u + 1` on spot,
`pu == prev u` on futures). A break resynchronises from a new snapshot, which
OB receives as recovery records and rebuilds its book from. A dropped
connection reconnects after a second; Binance closes every connection after 24
hours.

Futures data comes on two connections: depth on `/public`, trades on `/market`
(Binance's routing since 2026).

## Tests

`unit_test/src/test_mbp2mbo.cpp`, `test_binance_sync.cpp` (recorded message
shapes, no network), `test_mbp2mbo_ob.cpp` (Mbp2Mbo into a real OB) and
`test_ob_ring.cpp` (the ring ladder).
