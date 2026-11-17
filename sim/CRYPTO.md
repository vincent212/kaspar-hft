<!--
    Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
    Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
    Licensed under the MIT License. See LICENSE file in the project root.
-->

# bnsim on Coinbase and Hyperliquid

`bnsim` runs on three exchanges' live data: Binance (spot and USD-M futures,
[BINANCE.md](BINANCE.md)), Coinbase (spot) and Hyperliquid (perpetuals). The
order book, lights, SOM, probe and zmq console are the same for all three;
only the feed differs. Pick it with `--exchange`.

```
Coinbase   wss://advanced-trade-ws.coinbase.com   level2, market_trades, heartbeats
   -> CoinbaseFeed -> CoinbaseSync -> LevelWindow -> Mbp2Mbo -> OB ...

Hyperliquid  wss://api.hyperliquid.xyz/ws         l2Book (fast), trades
   -> HyperliquidFeed -> HyperliquidSync -> LevelWindow -> Mbp2Mbo -> OB ...
```

Neither needs an API key.

## Run

```bash
cd ~/bnruns/cb && ~/kaspar-hft/sim/binance/bnsim --exchange coinbase \
    --config ~/kaspar-hft/sim/config_binance --symbol BTC-USD \
    --probe-size 5 --probe-out probe.csv --quiet

cd ~/bnruns/hl && ~/kaspar-hft/sim/binance/bnsim --exchange hyperliquid \
    --config ~/kaspar-hft/sim/config_binance --symbol BTC \
    --probe-size 5 --probe-out probe.csv --quiet
```

`--dump` prints the top of the synthetic book once a second, with nothing else
running. `run.sh` takes the exchange from `EXCHANGE`:

```bash
EXCHANGE=coinbase    sim/binance/run.sh BTC-USD
EXCHANGE=hyperliquid sim/binance/run.sh BTC
```

Every other option, the output files and the zmq client are as in
[BINANCE.md](BINANCE.md).

## Configuration

`sim/config_binance/coinbase.ini` and `hyperliquid.ini`, one section per
product or coin: `tick`, `chunk` (the size of one synthetic order), `ladder`
(OB's ring of price slots), `sec_id`, and for Coinbase `depth` (levels per
side kept). `som.ini` carries the limits for all three exchanges.

## How each feed becomes a book

**Coinbase** sends its whole book on subscribe, tens of thousands of levels,
then absolute level updates. `LevelWindow` keeps all of it and passes only the
best `depth` levels (1000) to `Mbp2Mbo`: a level that changes, enters the
window or leaves it. Every message carries `sequence_num`, one more than the
last across all channels; a jump reconnects, and the new snapshot reaches OB as
recovery records. Trades are applied oldest first. Coinbase's `side` on a trade
is the maker's: `SELL` prints at the ask.

**Hyperliquid** sends the best 20 levels per side, whole, about twice a second
with `"fast": true` (once every several seconds without). Each book is diffed
against the last, so an unchanged level keeps its queue. A level that drops
below the 20 leaves the book and rejoins at the back of the queue if it comes
back. Trades' `side` is the taker's: `A` sold into the bid. The trades replayed
on subscribe are skipped. Hyperliquid drops a connection that sends nothing for
60 s, so the feed sends `{"method":"ping"}` every 30 s.

Hyperliquid prices have at most five significant figures, so the tick depends
on the price: 1 for BTC above 10,000, 0.1 for ETH between 1,000 and 10,000.
`hyperliquid.ini` must change if a price crosses a power of ten.
