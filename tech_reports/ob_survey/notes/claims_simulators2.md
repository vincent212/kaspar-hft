# Claims: more order-book simulators and backtesters (supplement to claims_simulators.md)

Checks done **2026-10-03/04**. This file covers simulators/backtesters that are NOT in `claims_simulators.md` (ABIDES, ABIDES-Gym, MAXE, PyMarketSim, JAX-LOB, mbt_gym, hftbacktest, NautilusTrader, MarS, TRADES/DeepMarket, queue-reactive). Those are referenced here only for comparison.

Method:
- Repos: shallow `git clone --depth 1` (HEAD on the check date) plus `gh api repos/...` for license, stars and push date. Code was read and grepped; **nothing was built or run**. Fill and queue behaviour therefore comes from reading code and docs.
- Papers: arXiv PDFs via pdftotext; arXiv API, Crossref and OpenAlex metadata; IFAAMAS proceedings PDFs.
- Vendor sites: WebFetch of official docs; web.archive.org snapshots where the live page geo-redirected or is gone.
- Not reachable: dl.acm.org (Cloudflare), the MultiCharts wiki (Cloudflare), rithmic.com (client-rendered, no text), cmegroup.com (blocked as scraping), the QuantOffice knowledge base (login), epam.com (403), and Databento's queue-position docs page (JS-rendered). WebSearch quota ran out partway through, so later repo discovery used `gh search repos` and the arXiv/Crossref APIs only.

Source-kind tags: **[FT]** full text, **[ABS]** abstract, **[META]** arXiv/Crossref/OpenAlex metadata, **[DOCS]** official documentation, **[VENDOR]** vendor marketing/product page, **[ARCHIVE]** official page read via web.archive.org, **[REPO]** repo files/code.
"VERIFIED" = seen in the cited source. "UNVERIFIED" = not confirmed. "Silent" = the pages/files read do not address the point. An absence ("none found") means the stated grep/search found nothing; it is not proof of absence.

## Bib keys

New keys are in `bib/refs_sim2.bib`. Reused existing keys (not repeated there):

| Key | File | Used for |
|---|---|---|
| moallemi2017 | refs.bib | Moallemi and Yuan, queue position valuation (§B2) |
| arroyo2024 | bib/refs_dl.bib | fill-probability survival analysis (§B9) |
| maglaras2022fill | bib/refs_deep2.bib | fill conditions source cited by Arroyo et al. (not re-read) |
| li2024simlob | bib/refs_deep2.bib | SimLOB (§B10) |
| prata2024 | refs.bib | LOBCAST benchmark (§D) |
| hftbacktest, nautilustrader | bib/refs_sim.bib | comparison only |
| byrd2020abides, amrouni2021abidesgym | bib/refs_dl.bib, bib/refs_sim.bib | comparison only |
| lobbench2025 | refs.bib | not re-checked here |

Key clash noted: the existing `dixon2018` (bib/refs_classic.bib) is Dixon's *Journal of Computational Science* RNN paper. Dixon's *High Frequency* trade-execution paper used here is a different work and gets the new key `dixon2018exec`.

## Corrections / additions to claims_simulators.md

- §12 said lobsim and PyLOB were "not checked". They are now checked (§A6, §A10 below). "lobsim" is several unrelated repos; the relevant one is kpetridis24/lobsim.
- Balch et al. 2019 (§B3) and Karpe et al. 2020 (§B5) are further uses of the ABIDES MarketReplayAgent. Balch et al. confirm that the replayed opening book was "recreated" from the LOBSTER orderbook file by submitting synthetic limit orders.
- NautilusTrader: `liquidity_consumption=True` "estimates **available size**, not **order priority**" [REPO: docs/concepts/backtesting/fill-prices-and-matching.md]. It is a mechanical consumption tracker, not a market-impact model. This agrees with the existing "no impact model found (U)".
- No other correction found.

---

# A. Open-source backtesters and engines

## A1. QuantConnect LEAN (key `lean`)
- URL: https://github.com/QuantConnect/Lean. C# with a Python API, Apache-2.0, pushed 2026-10-02. [REPO]
- **Type:** event-driven historical backtester plus live trading. README: "event-driven ... Out-of-the-box alternative data and live-trading support"; `lean live`. [REPO: README.md]
- **Data level: bars and L1 ticks only.** `enum TickType { Trade, Quote, OpenInterest }` [REPO: Common/Global.cs:512]; ticks carry `BidPrice/AskPrice/BidSize/AskSize` [REPO: Common/Data/Market/Tick.cs]. `grep 'OrderBook\|MarketDepth\|Level2'` over `Common/Data` found no depth type.
- **Fills:** `ImmediateFillModel` ("the default fill model"), `EquityFillModel`, `FutureFillModel`, `LatestPriceFillModel`. In the base limit fill, a buy fills `if (prices.Low < limitPrice)`, with the comment "assume the order completely filled" [REPO: Common/Orders/Fills/FillModel.cs:705-757]. Docs: "the pre-built fill models assume orders completely fill". [DOCS: https://www.quantconnect.com/docs/v2/writing-algorithms/reality-modeling/trade-fills/key-concepts]
- **Queue position:** none (grep `QueuePosition|queue position` found nothing).
- **Latency:** no latency model class. The only latency value is `latency = 0.075d` ("Time between order submitted and filled, in seconds"), an input to the slippage formula in `MarketImpactSlippageModel`. [REPO: Common/Orders/Slippage/MarketImpactSlippageModel.cs:60,68]
- **Impact:** through slippage models only (constant, volume-share, Almgren-based `MarketImpactSlippageModel`).
- **Live, same code:** yes (README).
- **Verdict:** bar/L1 backtester. Out of scope for order-book-level comparison.

## A2. Barter (key `barter`)
- `barter-rs/barter` does not exist (gh api 404). The real repo is https://github.com/barter-rs/barter-rs: Rust, MIT, pushed 2026-08-24. [REPO]
- **Type:** live, paper and backtest framework. It uses "mock MarketStream or Execution components to enable back-testing on a near-identical trading system as live-trading". [REPO: README.md]
- **Data:** `OrderBooksL1/L2/L3` subscription types exist [REPO: barter-data/src/subscription/book.rs], but no exchange connector implements L3. Exchanges are all crypto: binance, bitfinex, bitmex, bybit, coinbase, gateio, kraken, okx.
- **Backtest fills:** the mock exchange accepts **Market orders only** and fills the full quantity at the price written on the order request. There is no book walk and no queue. [REPO: barter-execution/src/exchange/mock/mod.rs:367-390]
- **Latency:** a constant `latency_ms`, applied with wall-clock `tokio::time::sleep`. [REPO: mock/mod.rs:42]
- **Determinism:** not claimed. The wall-clock sleep suggests runs are not time-deterministic (inference, not tested).
- **Verdict:** live-first framework. Its backtest is not order-book-level.

## A3. Hummingbot (key `hummingbot`)
- URL: https://github.com/hummingbot/hummingbot. Python/Cython, Apache-2.0. [REPO]
- **Backtesting (strategy_v2) is candle-based:** `CandlesFactory`; the simulator uses `df['close']`, `df['low'] <= sl_price` and `df['high']`. [REPO: hummingbot/strategy_v2/backtesting/, executors_simulator/position_executor_simulator.py]
- **Paper trade runs against live books, not history:** "simulates trading against live Binance market data" [REPO: README.md:55].
  - Market orders wait `TRADE_EXECUTION_DELAY = 5.0` s, then fill at the VWAP of walking the live L2 book.
  - Limit orders fill in full at their limit when the book crosses them, or when a public trade prints strictly through them. `trade_quantity` is read but never used. [REPO: hummingbot/connector/exchange/paper_trade/paper_trade_exchange.pyx:144, 452-469, 836, 895-905]
- **Queue position:** none.
- **Verdict:** out of scope (bar-level backtest; the L2 paper mode is live, not historical).

## A4. tardis-machine (key `tardismachine`) and Cryptofeed (key `cryptofeed`)
- **tardis-machine** (https://github.com/tardis-dev/tardis-machine, TypeScript, MPL-2.0) offers "tick-by-tick historical replay in exchange-native format via HTTP and WebSocket" and "customizable order book snapshots" [REPO: README.md:11-17]. It is a data replay server with **no fill simulation** (grep `fill|matching|backtest` over `src` found only a session-metadata identifier).
- **Cryptofeed** (https://github.com/bmoscon/cryptofeed, Python, AGPL-3.0 per the LICENSE file) is a "Cryptocurrency Exchange Feed Handler" with L2_BOOK and L3_BOOK channels. It is a live feed handler only (grep `backtest|simulat` found nothing). [REPO]
- **Verdict:** data infrastructure, not simulators. Both are used as data sources by simulators (hftbacktest has a Tardis converter, per claims_simulators.md §6).

## A5. Matching engines / book builders without a strategy-replay harness
- **OrderBook-rs** (https://github.com/joaquinbejar/OrderBook-rs, Rust, MIT): a "limit order book ... comprehensive order matching engine". "Market Simulation: Tool for back-testing" appears only as a listed use case. Its replay re-executes the engine's own journal, not a market-data feed. grep `backtest` over `src` found nothing. [REPO]
- **PyLOB** (https://github.com/DrAshBooth/PyLOB, Python, MIT text in LICENSE.md): "A limit order book for simulation research ... price-time priority ... Its chief simplifying assumption is zero latency". `replay` re-issues its own session log. Companion agent-based **PyLOBsim** (last push 2013; description only, not cloned). [REPO]
- **SimpleOrderbook** (https://github.com/jeog/SimpleOrderbook, C++11/Python, GPL-3.0): "financial market orderbook and matching engine"; grep `backtest` found nothing. [REPO]
- **itch-order-book** (https://github.com/charles-cooper/itch-order-book, C++, license NOASSERTION): an ITCH 5.0 book builder that "only calculates the aggregate quantities at each price and does not track the queue depth for each order". [REPO: README]
- **matching-engine-rs** (https://github.com/amankrx/matching-engine-rs, Rust, MIT): ITCH book processing at "11.3 Million messages per second" (README claim, not reproduced); no backtest. [REPO]
- **Verdict:** components, not backtesters. Listed for completeness, not in the comparison table.

## A6. lobsim, kpetridis24 (key `lobsim`)
- URL: https://github.com/kpetridis24/lobsim. C++20 with Python bindings, Apache-2.0, 27 stars, pushed 2026-02-03. [REPO]
- **Type:** "fast, deterministic **L3 limit order book replay + paper execution simulator**". Historical events need a stable `order_id`. [REPO: README.md]
- **Queue position: exact, given L3 data.** "FIFO queueing at a price level is preserved using order arrival order ... you get full L3 behavior: queue priority". The code computes `queueAhead = aheadMarket + aheadPaper` from Fenwick trees of historical quantity placed before the paper order's `placement_seq`. [REPO: cpp/src/paper_trading_simulator.cpp, `apply_paper_trade_at_level`]
  - Without order IDs (an L2 feed), the README says it "does **not** represent true L3 queue position".
- **Impact:** none. A paper order "does not alter historical liquidity"; for `AGGRESSIVE_TRADE`, "The historical book is not mutated". [REPO: README.md]
- **Latency:** strategy-event injection "with optional latency" (`latency=1_000  # microseconds`) in `MultiBookSimulator`. No feed-latency model or per-message-type latencies were found (UNVERIFIED absence).
- **Determinism:** claimed ("deterministic") in the README. Not tested.
- **Data/venues:** examples use a Coinbase BTC-USDT parquet source and LOBSTER AMZN [REPO: README.md:326]. No CME MDP3 or ITCH decoder found.
- **Live path:** none.
- **Author name:** only the handle `kpetridis24` was seen; the full name is UNVERIFIED.

## A7. Backtesting-Engine, chasemetoyer (key `chasemetoyer_bte`)
- URL: https://github.com/chasemetoyer/Backtesting-Engine. Rust with Python bindings, MIT, 7 stars, pushed 2026-03-08. [REPO]
- **Type:** "L3 parquet ingestion", "limit order book and matching engine", "deterministic replay/backtesting", "local CoinAPI LIMITBOOK conversion". [REPO: README.md]
- **Queue position: exact FIFO.** Strategy orders are submitted into the same `L3MatchingEngine` that replays history (`state.engine.process_event(EngineEvent::Submit(order))`). `queue_ahead_qty` is computed in `src/lob/book.rs:414-440`. [REPO: src/backtest/mod.rs]
- **Impact:** because the strategy order sits in the replayed book, it alters later matching of replayed events. This is an inference from the code path; the README does not say so.
- **Latency:** none found (grep `latency` over `src/backtest`).
- **Live:** `src/live/` defines an adapter trait; no venue implementation was checked (UNVERIFIED).
- **Data:** CoinAPI crypto L3. No CME or ITCH decoder found.

## A8. MarketMakingMLAlgo, koteyevlev (key `koteyevlev_mm`)
- URL: https://github.com/koteyevlev/MarketMakingMLAlgo. Python notebooks/scripts, MIT, last commit 2021-09-12. [REPO]
- **Type:** re-matching of a full order log. "Backtest was build on Full order log" (data `OrderLog20151010`; default ticker `LKOH`; the README calls it an HSE diploma project). The exchange is not named in the README. [REPO: README]
- **Queue position: exact FIFO against historical orders.** `Matching_Engine` keeps `bid_FIFO[price]` lists of order numbers. Agent orders get IDs `"myorder"+n` and go through the same `check_add` path into the FIFO lists. [REPO: backtest_py/algo_classes.py L67-85, L330-372]
- **Impact:** mechanical only. Historical trade records are dropped (`fol = fol[fol["ACTION"] != 2]`), so trades are regenerated by the engine, and agent fills remove historical resting volume. Historical participants do not react. [REPO: backtest_period.py L19]
- **Latency:** none found. **Live:** none.
- **Author names** in the bib entry come from the handle and README and are not verified.

## A9. hft-backtesting, evgerher (key `evgerher_hft`)
- URL: https://github.com/evgerher/hft-backtesting. Python, Apache-2.0, last commit 2020-05-27. BitMEX XBTUSD/ETHUSD L2 snapshots plus trades. [REPO]
- **Queue position: estimated.** The initial position is set by `order_position_policy='tail'|'head'|'random'` (seed 1337) times the level volume. Trades consume volume ahead. Level depletions are applied deterministically, or with probability `order_volume_before / level_volume`. [REPO: hft/backtesting/backtest.py L24-32, L158-201, L348]
- **Latency:** a single `delay` on orders and statuses. The unit is inconsistent: the docstring says "delay in microseconds !" but the code uses `timedelta(milliseconds=self.delay)` (L369), and the README says "millisec".
- **Impact:** none. **Live:** none.

## A10. Smaller replay repos found (one line each; [REPO] unless noted)
- **Tribeca** (michaelgrosner/tribeca, TypeScript, ISC, last commit 2018-02-26): a crypto market-making bot with a backtester. Order ack/cancel after a fixed simulated 3 ms. Fills happen only when the market strictly crosses the order price, at the snapshot size; the book is not depleted, and there is no queue (`src/service/backtest.ts` L104-197). **The same quoting engine runs live**; only the gateway is swapped (`src/service/main.ts` L120-135).
- **mmssss/hft-market-making** (Python, no license, 2023): Binance perpetual L2 plus trades. Separate execution and market-data latencies ("Latency in nanoseconds"). An order fills whole when the price crosses or touches; no queue (`simulator/simulator.py` L680-720). The README says the simulator actually used is a fork of `dolmatovas/HFT` (not inspected).
- **singhnam23/market-making-simulator** (Python, MIT, 2024): Databento data. `# TODO: Add queue position logic` (simulator.py L73), and `process_trade` is a stub (`pass`). Latency in ms on the action queue.
- **ksemianov/TradingGym** (Python, Apache-2.0, 2018): MOEX Plaza II level II. Fills only on crossing. README: "still assumes no market impact".
- **DJ824/hft-backtester** (C++, no license, 2025): described as an "orderbook based backtesting suite using MBO data from Databento"; README says "UNDER REFACTOR". Queue handling UNVERIFIED.
- **Not inspected (description only, UNVERIFIED):** abhinavavvaru-code/marketmaker-backtester ("Queue-aware limit order book backtester ... order-level (L3) book reconstruction", 0 stars); nkaz001/market-making-backtest (earlier BitMEX work by hftbacktest's author); SaadSouilmi/Queue-Reactive (model-based C++ engine); SamanvayMS/market-making-simulation (describes an "MBO replay engine", but the clone is empty).
- **Searches with zero results** (`gh search repos`): "mdp3 backtest", "itch backtest", "market by order backtest", "mbo backtest", "L3 backtest", "queue position backtest", "databento queue". "mdp3" returned only CME feed handlers/simulators (epam/java-cme-mdp3-handler, vincent212/CME-Market-Data-Handler, usa4148/cme-mdp3-sim "feed simulator"), none of them backtesters.

---

# A'. RL environments

## A'1. rl_markets / Spooner et al. 2018 (keys `spooner2018`, `rl_markets`)
- **Paper:** AAMAS 2018, Stockholm, pp. 434-442. [FT: https://www.ifaamas.org/Proceedings/aamas2018/pdfs/p434.pdf; arXiv 1804.04216]
- **Type:** historical replay. "We have developed a simulator of a financial market via direct reconstruction of the limit order book from historical data". The data is 10 European equities (tickers such as HSBA.L, GSK.L, VOD.L, ING.AS, SAN.MC), January-August 2010. [FT §3]
- **Data level:** MBP. The simulator "tracks the top 5 price levels", plus transactions.
- **Impact:** none. "simulated orders placed by an agent cannot impact the market". [FT §3]
- **Queue position: estimated.** The agent's order joins at the back. For cancellations, "Our solution is to assume that cancellations are distributed uniformly throughout the queue", so the probability that a cancel is ahead is "proportional to the amount of volume ahead". [FT §3]
- **Code:** https://github.com/tspooner/rl_markets (C++, BSD-3-Clause, last commit 2019-11-04).
  - New orders are created as `Order(price, size, volume(price))`, i.e. behind all visible volume [REPO: src/market/book.cpp L257].
  - `doCancellation` implements the "Uniform scheme": `q_head -= ceil(volume*q_head/total); q_tail -= floor(volume*q_tail/total)` [REPO: src/market/order.cpp].
  - Code-reading observation, not run: on a volume increase a negative `vol_diff` is passed to `addVolumeBehind`, which shrinks the volume behind. This looks like a sign error. [REPO: book.cpp L127-137]
- **Latency:** the paper lists latency only as future work. The repo has fixed/normal/lognormal latency samplers that set `ref_time` in `DoAction` [REPO: src/environment/intraday.cpp L178]. `grep -rn ref_time` over `src/` and `include/` finds only assignments (L132, L178) and the declaration, never a read. **VERIFIED: the released code has no effective order latency.**
- **Live:** none.

## A'2. crypto-rl / Sadighian 2019 (keys `sadighian2019`, `cryptorl`)
- **Paper:** arXiv 1911.08647. Coinbase "level-3 tick data" is recorded "and replayed to fully reconstruct the LOB", then sampled into snapshots "every 1-seconds". [FT]
- **Queue:** "If the agent posts a new order at the same price as existing resting orders, the new order price jumps a one-tick increment ahead of the queue by default". [FT]
- **Code** (https://github.com/sadighian/crypto-rl, Python, **no LICENSE file**, last commit 2021-11-30; the paper does not link the repo):
  - `queue_ahead` is the snapshot notional at the level when the order is placed. It is reduced only by opposite-side trade notional; others' cancellations are not modelled. [REPO: gym_trading/envs/market_maker.py L158-168; gym_trading/utils/position.py L70-100]
  - 1 s snapshots, 15 book rows. [REPO: configurations.py]
- **Latency:** none (0 hits in the paper; none in the code). **Impact:** not modelled. **Live:** README says "Research only: there is no capability for live-trading".
- **Data level:** L3 recorded, but the environment sees 1 s L2 snapshots.

## A'3. Other RL environments (out of scope or not order-book-level)
- **Yvictor/TradingGym** (MIT): tick replay with long/short/flat targets, filled at the next tick's deal price (`training_v1.py` L231-232). No book, queue or latency. An Interactive Brokers live env is only "in the future" per the README. [REPO]
- **gym-anytrading** (AminHP, MIT): bar-level, uses `Close` (`stocks_env.py` L18), Buy/Sell only, percentage fees. [REPO]
- **FinRL-Meta** (MIT; NeurIPS 2022 Datasets and Benchmarks, arXiv 2211.03107): grep for order-book terms finds no order-level fill environment. [REPO]
  - The futures env uses 500 ms L1 snapshots and books trades at `close_price = self.px0[self.t]`.
  - `env_market_impact` uses parametric (Almgren-Chriss-type) cost models.
  - `env_execution_optimizing` is copied from Microsoft qlib's high-frequency execution examples (not inspected further).
- **FinRL-X** (AI4Finance-Foundation/FinRL-Trading, Apache-2.0, arXiv 2603.21330): no order-book code found. [REPO]
- **"Gym-LOB"**: there is no canonical project. jiseokcube/gym-lob has an empty `step()` (the body is `###`). matt-quant-heads-io/gym-lob uses "Simplified fill logic: if market traded at or below our bid price", with no queue. Tejasv-Singh/lob_gym uses a synthetic OU generator and walk-the-book fills. [REPO]
- **RL4Market**, **simlob/Sim-LOB**, **mm-sim/MarketSimulator**, **Mango**: no matching project found (`gh search repos`; arXiv for RL4Market). **PyMarket** (kiedanski/pymarket) simulates auction mechanisms, not an LOB.
- **abides-jpmc-public**: the public home of ABIDES-Gym (BSD-3, archived 2023-12-13). Already covered in claims_simulators.md §2.

---

# B. Academic replay methodologies and related papers

## B1. Rigtorp 2013, queue position estimation (key `rigtorp2013queue`)
- Blog post, not a simulator. [DOCS: https://rigtorp.se/2013/06/08/estimating-order-queue-position.html]
- With MBO feeds the problem is "theoretically trivial"; futures "usually provide market data through a market-by-level feed", so the position must be estimated.
- **Initial estimate:** the level size Q(t0) when the order is sent (or at acknowledgement, or the average of the two). If a size increase equal to the order size S appears within δ, re-anchor; "δ is chosen to be a multiple of the delay between sending an order and observing it on the market data feed". This is the only place latency enters.
- **Decreases:** a fill decrease counts fully against the volume ahead. Otherwise only a fraction p = f(V̂)/(f(V̂)+f(max(Q−S−V̂,0))) counts, with f = "ln(1+x) or the identity function". The identity gives the proportional rule used by Spooner et al.
- hftbacktest's `ProbQueueModel` cites this post (claims_simulators.md §6).

## B2. Moallemi and Yuan backtest (key `moallemi2017`, reused)
- [FT: https://moallemi.com/ciamac/papers/queue-value-2016.pdf, revision June 2017; SSRN DOI 10.2139/ssrn.2996221]
- Section 5 is a backtest built on NASDAQ ITCH (market-by-order): "With full information on historical order/trade data, we were able to construct a simulator to backtest our proposed valuation model".
- **Queue position: exact relative to historical orders.** Artificial "Regular" orders join the back of the queue; "Touch" orders go "at the very front of the queue". Fill rule: "If a limit order at the same price that has arrived after the artificial order is filled, we will assume that the artificial order is also filled."
- **Impact:** none. Artificial orders are "of infinitesimal size and hence have no market impact".
- **Latency:** not modelled (orders inserted instantly at random times).
- **Sample length is inconsistent in the paper:** Table 2 says "21 trading days of August 2013"; the text says "30 trading days".
- **Code:** none mentioned.

## B3. Balch et al. 2019, replay vs interactive simulation (key `balch2019replay`)
- arXiv 1906.12010; "Presented at the 2019 ICML Workshop on AI in Finance". [META]
- Built in ABIDES with "a market replay agent that provides liquidity by replaying historical orders" of a LOBSTER stream (09:30-10:30, 86,615 events). The opening book is recreated by submitting synthetic limit orders; executions are handled by cancelling the corresponding resting orders. [FT]
- **Impact:** mechanical only. Market orders of 50-1000% of the best size walk the replayed book, and the mid then reverts. The key weakness of replay is that "the simulated market does not substantially adapt to or respond to the presence of" the strategy. [FT]
- **Queue:** the word "queue" does not occur in the paper. **Latency:** mentioned only as an ABIDES capability.

## B4. Vyetrenko and Xu 2019, replay with simulated market response (key `vyetrenko2019`)
- arXiv 1906.02312. The arXiv comment says ICML 2019 proceedings, but the paper is not among the 773 papers in PMLR v97 (checked). The venue is UNVERIFIED, so it is cited as arXiv. [META]
- **Type:** MBP replay. "a market simulator that replays the LOB using prices and volumes at multiple LOB levels and completed trade data provided by the exchange". [FT §2.1]
- **Queue position: estimated.** A new passive order is "placed at the back of the queue". "We model queue cancellations according to a predefined input distribution (e.g., from the back of the queue, from the front of the queue, uniformly at random, etc.)". Passive orders "are executed whenever historical trades are". [FT §2.1]
- **Latency:** "we model latency between the agent's placement decisions and the time these decisions reach exchange ... we use historical latency profiles". [FT §2.1]
- **Impact:** synthesised adverse price moves after the agent's aggressive orders, so the "simulated LOB time series needs to diverge from historical". [FT §2.2]
- Simulator parameters (matching rules, cancellation assumption, latency) are calibrated so that "simulation output resembles real execution data".
- **Data:** unnamed liquid futures. **Code:** none.

## B5. Karpe et al. 2020 (key `karpe2020`)
- ICAIF 2020, DOI 10.1145/3383455.3422570; arXiv 2006.05574. [META]
- Uses "ABIDES market replay" with historical LOB data to train a DDQL execution agent. Nothing on queue position or latency. [FT]

## B6. Giegrich, Oomen and Reisinger 2024, K-NN resampling (key `giegrich2024knn`)
- arXiv 2409.06514. No published version found in Crossref. [META]
- **Type:** data-driven simulator that resamples historical LOB transitions by K-nearest neighbours, so the path diverges from history. "synthetic trading within the simulation leads to a market impact in line with the corresponding literature". [FT]
- **Data:** CME 3-month SOFR futures, "daily level 2 LOB data from CME ... does not allow us to follow individual orders", 5 levels plus trades. [FT]
- **Queue:** fills are attributed using "CME's Allocation mechanism" (pro-rata-type). For FIFO, "one needs to track all other limit orders that are currently ahead in the queue"; this is left to "a parametric or non-parametric model". [FT]
- **Latency:** 0 mentions. **Code:** none found.

## B7. Dixon 2018, CME trade execution model (key `dixon2018exec`)
- *High Frequency* 1(1):32-52, DOI 10.1002/hf2.10016 [META]; arXiv 1710.03870 [FT].
- **Data:** CME E-mini ES (ESU6), August 2016, an "archived Chicago Mercantile Exchange (CME) FIX format message feed", Level II.
- **Method:** "Using exchange matching engine rules, this trade execution model estimates the queue position of a reference order and determines whether it is filled".
- **Queue position: estimated (MBP), exact with MBO.** An unknown parameter ω ∈ [0,1] sets how many cancellations advance the order ("ω = 1 represents the most favorable scenario"). With market-by-order data, "no approximation is needed" (Remark 3.3.2). Covers FIFO and pro-rata. The backtest uses ω = 0, "the most conservative queue position estimate".
- **Latency:** the backtest uses "0ms latency". Remark 4.0.6: "a trade decision must be made at time t0 − l for it to be received at time t0".
- **Code:** none mentioned.

## B8. RL market making on historical or calibrated LOBs
- **Kumar 2020** (key `kumar2020drl`): AAMAS 2020 extended abstract, 3 pages, pp. 1892-1894. [FT: https://www.ifaamas.org/Proceedings/aamas2020/pdfs/p1892.pdf]
  - Agent-based ("single market making agents with multiple market-takers"), not historical replay.
  - "The simulation framework takes account of the agent's latency", with no detail given. Queue handling is not described.
  - Citation inconsistency: Kumar's 2021 arXiv paper cites it as the "20th" AAMAS, pp. 1892-1895; the PDF says 19th.
- **Kumar 2021** (key `kumar2021hawkes`, arXiv 2109.15110): a multi-agent simulator "from scratch" with a FIX-style interface and price-time matching. Order flow comes from a deep Hawkes model fitted to a "Nasdaq TotalView-ITCH 5.0 data feed sample". Queue position is exact inside the synthetic book and valued with Moallemi-Yuan. Generative/agent-based, not replay. No code link. [FT]
- **Kumar 2023** (key `kumar2023marl`): AAMAS 2023 extended abstract, pp. 2409-2411. The abstract describes a "multi-market simulation framework ... with a realistic market design and matching engine" and says "We investigate the effect of latency". [ABS via Crossref] Full text not read.
- **Guo, Lin and Huang 2023** (key `guo2023mm`; IJCNN 2023, DOI 10.1109/ijcnn54540.2023.10191123; arXiv 2305.15821) [FT]
  - Shenzhen Stock Exchange event-by-event data.
  - The simulator "executes the agent's order only when the real historical order arrives", with crossing-only fills, so there is no queue position.
  - Impact is ignored ("Since the volume of agent's quote is small").
  - Includes a latency sweep; the unit is not stated in the passage read.
- **Zhao and Linetsky 2021** (key `zhao2021ber`; ICAIF 2021): "three years of limit order book data on Chicago Mercantile Exchange (CME) S&P 500 and 10-year Treasury note futures". [ABS] Closed access; simulator, queue and latency details UNVERIFIED.
- **Gašperov and Kostanjčar 2022** (key `gasperov2022`; IEEE Control Systems Letters 6:2485-2490): trained on "a weakly consistent, multivariate Hawkes process-based limit order book simulator". Model-based, not replay. [FT]
- **Hambly, Xu and Yang 2023** (key `hambly2023`; Mathematical Finance 33(3):437-503) [FT, arXiv v4]
  - No dedicated discussion of LOB simulators or backtesting.
  - On Spooner: "since the market is reconstructed from historical data, simulated orders placed by an agent cannot impact the market".
  - Calls for "developing good market simulators that could generate (unlimited) realistic market scenarios".

## B9. Fill-probability / queue papers using hypothetical orders (not simulators)
- **Arroyo et al. 2024** (key `arroyo2024`, reused): LOBSTER MBO. Hypothetical one-share orders are "placed last in the queue", with fill conditions "following a similar approach to that of Maglaras et al. (2021)". "hypothetical limit orders do not have market impact". No latency (0 hits). [FT arXiv 2306.05479]
- **Lokin and Yu** (key `lokin2024fill`, arXiv 2403.02572): semi-analytical. The own order's queue position is a "pure-death process"; validated on LMAX EUR/USD data. Not a simulator; no latency. [FT]
- **Albers et al. 2025** (key `albers2025dilemma`, arXiv 2502.18625): a live Binance BTC-perpetual experiment. [FT]
  - Argues that synthetic-order replay "cannot guarantee that a fill is realizable in the real world, and with Binance's L2 data, accurately tracking an order's queue position—and thus its outcome—is impossible".
  - Rates methods on "whether latency effects are accounted for", yet treats latency as negligible in its own analysis.

## B10. Latency papers (none provides a replay simulator)
- **Moallemi and Sağlam 2013** (key `moallemi2013latency`; Operations Research 61(5):1070-1086): a "closed-form expression for the cost of latency", calibrated on TAQ data. [FT author PDF]
- **Cartea, Jaimungal and Sánchez-Betancourt 2021** (key `cartea2021latliq`; IJTAF 24(06n07)): Monte Carlo runs of their own model ("We perform 10,000 simulations"). [FT arXiv 1908.03281]
- **Cartea and Sánchez-Betancourt 2021** (key `cartea2021shadow`; SIAM J. Financial Math. 12(1):254-294): uses a "proprietary data set of foreign exchange". [ABS] No simulator mentioned.
- **Cartea and Sánchez-Betancourt 2023** (key `cartea2023delay`; Finance and Stochastics 27(1):1-47): "We use foreign exchange data to implement the random-latency-optimal strategy". [ABS] Whether that implementation is a replay backtest is UNVERIFIED.
- **SimLOB** (`li2024simlob`, reused): representation learning, not a simulator. A Transformer autoencoder learns LOB representations used to calibrate agent-based models (PGPS). [FT arXiv 2406.19396]

---

# C. Commercial / industry

Only tools whose documentation describes order-book-level historical replay with simulated fills are included. All are closed source and commercial. None of the docs read describe a determinism guarantee.

## C1. Trading Technologies, TT Backtesting for ADL algos (keys `tt_backtesting`, `tt_piq`)
- **What it does:** "TT Backtesting lets users execute ADL algos in a simulated matching environment while replaying historical market data." [DOCS: https://library.tradingtechnologies.com/tt-backtesting/tt-backtesting-overview/introduction-2/]
- **Matching:** "Matches orders placed by the algo instances against the historical market data stream using the same simulation matching engine used in the TT production simulation environment." [DOCS: .../how-tt-backtesting-works/]
- **Data:** "actual book data consisting of the bids / offers as well as the trades", back to about late 2018. MBP vs MBO: silent. [DOCS: .../tt-backtesting-considerations/]
- **Fidelity limits:** at high replay speeds the "Algo Server will conflate" updates, and "your ADL algo may not behave the same in a backtest as it does in production if you set the replay speed too high". One trading day per backtest at most. [DOCS: considerations; running-a-backtest]
- **Queue position:** the backtesting pages are silent. TT's general PIQ page says actual PIQ comes from MBO feeds ("CME (Market by Order)"); otherwise "Estimated PIQ ... is a conservative estimate" driven by trades in front of the order; and "In the Simulation environment, PIQ is estimated for all markets." Whether the simulated matcher *fills* according to that estimate: silent. [DOCS: https://library.tradingtechnologies.com/trade/viewing-market-data/position-in-queue-piq/position-in-queue-piq/]
- **Latency, impact:** silent.
- **Same code live:** the backtest runs the ADL algo itself, the same object that runs in production.

## C2. Deltix (EPAM) QuantOffice + TimeBase (key `deltix_quantoffice`)
- [VENDOR: https://www.deltixlab.com/quantoffice] "The Universal Strategy Runner uses a variety of simulators, from coarse bar-based to substantially more precise L2 (MBP and MBO) simulators."
- Strategies can "Go live on the execution simulator". Paper trading runs "with real time, or streaming historical data from TimeBase". Strategies are "switched from Paper trading to Live trading using QuantOffice trading connectors activated on TradeHub (Ember)". Languages: C# or Python.
- The execution-server page lists an "Exchange simulator" and a "Matching engine with FIX API". [VENDOR]
- **Queue position, latency, impact, determinism:** UNVERIFIED. The documentation portal (kb.quantoffice.cloud) needs a login.

## C3. RCM-X Strategy Studio (key `rcmx_strategy_studio`)
- [ARCHIVE vendor page, snapshot 2021-01-25; the live page now returns 403] "Full tick-by-tick Backtesting, Live Simulation, and Production Trading"; "Fill Simulator for back testing and live simulation"; "order book building".
- Back-test data readers include OneTick and TickData.com. Execution handlers include "CME iLink", CQG and TT.
- **Queue position, latency, impact:** silent. **Current availability:** UNVERIFIED.

## C4. Retail platforms with depth replay (borderline)
- **NinjaTrader 8 Playback with Market Replay** (key `ninjatrader_playback`) [ARCHIVE DOCS, 2025 snapshots of the official help guide: playback.htm, simulation.htm]
  - Replay files hold "both level I and level II (market depth) data". However, "Ask and Bid Volume during playback ... will be simulated and set to '1'".
  - Playback orders "are processed immediately and synchronously. This enables reproducible results"; "simulated internet latency delay simulation is not present in playback".
  - The live Simulator uses "time (to simulate order queue position)"; whether Playback uses the same queue model is silent.
  - The Strategy Analyzer is bar-based ("three virtual bars").
  - Same NinjaScript (C#) code runs on sim and live connections.
- **Bookmap** (key `bookmap_kb`) [DOCS]
  - Replay of recorded market depth, with simulated orders through the Bookmap simulator.
  - It displays a queue position that "approximates the position in the queue according to a FIFO matching algorithm using a more pessimistic scenario relating to canceled orders". Whether simulated fills use this approximation is silent.
  - "We do not currently support partial fill". Feeds include CME futures, Nasdaq TotalView and crypto [VENDOR].
- **Sierra Chart** (key `sierrachart_tradesim`) [DOCS]
  - Market depth is replayed, but "Estimated Position in Queue Tracking ... only applies to non-replaying charts".
  - Replay fills come from the High/Low/Last values. **Replay fills are not queue-aware.**
- **MultiCharts, ATAS** [VENDOR]: both state that they replay Level II/DOM in simulation. Fill rules and queue handling: silent.

## C5. Commercial exclusions (one line each)
| Tool | Reason | Source |
|---|---|---|
| OneTick | Only a vendor line that it "can be ... an exchange simulator"; no fill/queue model documented; the onetick-py API index shows no simulator functions | [VENDOR] onetick.com/video-shorts/onetick-back-testing; [DOCS] onetick-py genindex |
| KX kdb+/Insights | "Reconstruct ... order books", "Replay historical data to simulate real-time feeds"; no matching/fill simulator described | [VENDOR] https://kx.com/use-cases/backtesting/ |
| Databento | Data vendor (MBO incl. GLBX.MDP3); no simulator. Its blog says MBO lets you "track the exact queue position of an order", and L2 "market replay" platforms "have been overly optimistic" | [VENDOR blog, key `databento_queue`] https://databento.com/blog/getting-queue-position-from-l2-and-order-book-data |
| LSEG Tick History PCAP | Data product ("reconstruction and replay of events"); no fill simulator; the page read does not mention MayStreet | [VENDOR] lseg.com tick-history-pcap |
| QuantHouse, Exegy, Algoseek, Kinetick, Tick Data | Data/feed/hardware vendors; no backtester or fill simulator described | [VENDOR] home/product pages |
| Quantower | Market Replay on "Tick, 1 minute, 1 day" data; fills on Last or Bid/Ask/Last | [DOCS] help.quantower.com market-replay |
| MotiveWave | "Replay using historical Time & Sales data" | [VENDOR] motivewave.com/products.htm |
| QuantRocket | Zipline/Moonshot on daily/minute data | [VENDOR] quantrocket.com |
| Exactpro th2 | Test automation for exchanges and trading systems, not strategy backtesting | [VENDOR] exactpro.com/test-tools/th2 |
| CQG, Rithmic, CME New Release/certification, Nasdaq test facility, Jigsaw | UNVERIFIED: pages unreadable or silent | see method notes |
| Quantitative Brokers, Abel Noser, Vela/Exegy SuperFeed replay, Pico Redline, Corvil, Orc, TradeStation, TT Algo SE | Not fetched; no claim made | none |

---

# D. Excluded as out of scope (bar-level or not order-book-level)

- **Backtrader, Zipline, VectorBT:** not re-checked in this pass. Their exclusion as bar-level is UNVERIFIED here.
- **LEAN:** bars plus L1 ticks; range-touch full fills; no depth data type (§A1).
- **Jesse:** 1-minute candles; a limit fills if the candle range contains its price (`candle_service.py:121-122`) [REPO]. Live trading via a licensed plugin [DOCS: docs.jesse.trade/docs/livetrade.html].
- **Hummingbot backtesting:** candles (§A3).
- **Barter backtest:** market orders only, filled at the requested price (§A2).
- **gym-anytrading:** close-price bars (§A'3).
- **Yvictor/TradingGym:** next-tick fills, no book (§A'3).
- **FinRL-Meta / FinRL-X:** bars, L1 snapshots, parametric impact (§A'3).
- **QuantRocket, Quantower, MotiveWave:** bar/tick/T&S replay (§C5).
- **LOBCAST** (`prata2024`): a forecasting benchmark on FI-2010, not a simulator [REPO README; META Crossref].
- **SimLOB:** representation learning (§B10).

---

# Comparison table

Legend: **V** = VERIFIED in the section cited; **U** = UNVERIFIED; "silent" = docs/code read do not say; "none (V)" = searched and not found. Kaspar's row is the user-provided description, not independently verified here.

| Simulator | § | Type | Data level | Own-order queue position | Latency model | Agent affects replayed market | Determinism | Same-code live | Language | License | Feeds / venues |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Kaspar (user description) | — | historical replay | L3 MBO (MDP3 PCAP or decoded) | exact: inserted into real FIFO queue | order, cancel, feed latencies | no impact | single thread, market-time clock | replay/paper/live (iLink 3) | C++20 | — | CME MDP3 |
| lobsim (kpetridis24) | A6 | L3 replay + paper execution (V) | L3 (V) | exact count of historical qty ahead, needs order IDs (V) | optional injection latency, µs (V) | no (V) | claimed "deterministic" (V claim, not tested) | none (V) | C++20 + Python (V) | Apache-2.0 (V) | Coinbase parquet, LOBSTER (V) |
| Backtesting-Engine (chasemetoyer) | A7 | L3 replay in matching engine (V) | L3 (V) | exact FIFO, own orders in replayed book (V) | none found (V) | yes, mechanically (inference from code) | claimed "deterministic" (V claim) | live trait only; venues U | Rust + Python (V) | MIT (V) | CoinAPI crypto L3 (V) |
| MarketMakingMLAlgo (koteyevlev) | A8 | full order-log re-matching (V) | L3 order log (V) | exact FIFO vs historical orders (V) | none found (V) | yes, mechanical; historical trades regenerated (V) | silent | none (V) | Python (V) | MIT (V) | exchange unnamed; ticker LKOH (V) |
| hft-backtesting (evgerher) | A9 | L2 replay (V) | L2 snapshots + trades (V) | estimated: tail/head/random + probabilistic depletion (V) | single delay, unit inconsistent (V) | no (V) | seeded RNG 1337 (V) | none (V) | Python (V) | Apache-2.0 (V) | BitMEX (V) |
| Tribeca | A10 | live MM bot + backtester (V) | L2 snapshots + trades (V) | none: strict-cross fills (V) | fixed 3 ms ack (V) | no, book not depleted (V) | silent | yes, gateway swap (V) | TypeScript (V) | ISC (V) | crypto (Coinbase, HitBTC) (V) |
| mmssss/hft-market-making | A10 | L2 replay (V) | L2 + trades (V) | none (V) | separate exec and MD latency (V) | no (V) | silent | none (V) | Python (V) | none (V) | Binance perp (V) |
| rl_markets (Spooner 2018) | A'1 | MBP replay RL env (V) | L2, 5 levels + trades (V) | estimated: back of queue, proportional cancels (V) | sampler present but unused (V) | no (V) | replay deterministic; seeded samplers (V code reading) | none (V) | C++ (V) | BSD-3 (V) | European equities 2010 (V) |
| crypto-rl (Sadighian) | A'2 | L3 recorded, 1 s L2 snapshot replay (V) | L2 snapshots (V) | estimated: notional ahead minus trades; no cancels (V) | none (V) | no (V) | silent | none, "Research only" (V) | Python (V) | no license file (V) | Coinbase, Bitfinex (V) |
| Moallemi & Yuan backtest | B2 | replay methodology (V) | L3 ITCH (V) | exact: filled if a later order at that price fills (V) | none (V) | no (V) | n/a | n/a | no code (V) | n/a | Nasdaq ITCH (V) |
| Dixon 2018 | B7 | replay execution model (V) | L2 CME FIX (V) | estimated via ω; exact with MBO (V) | 0 ms in backtest; t0 − l remark (V) | no (U) | n/a | n/a | no code (V) | n/a | CME ES (V) |
| Vyetrenko & Xu 2019 | B4 | MBP replay + synthetic response (V) | L2 + trades (V) | estimated: back of queue, cancel distribution (V) | historical latency profiles (V) | yes, synthetic adverse moves (V) | silent | none (V) | no code (V) | n/a | unnamed futures (V) |
| Balch et al. 2019 (ABIDES replay) | B3 | ABIDES replay agent (V) | L3 LOBSTER (V) | silent ("queue" absent) (V) | ABIDES capability only (V) | mechanical only (V) | ABIDES seed | none | Python | BSD-3 (ABIDES) | Nasdaq LOBSTER (V) |
| Giegrich et al. 2024 | B6 | K-NN resampling of history (V) | L2, 5 levels (V) | CME allocation (pro-rata-type); FIFO not handled (V) | none (V) | yes, endogenous (V) | silent | none | no code (V) | n/a | CME SOFR futures (V) |
| TT Backtesting | C1 | historical replay + TT sim matcher (V) | book + trades; MBP/MBO silent (V) | silent; PIQ "estimated for all markets" in sim (V) | silent | silent | conflation at speed (V) | yes, ADL algo (V) | ADL (V) | commercial | TT-connected exchanges (U) |
| Deltix QuantOffice | C2 | bar to L2 MBP/MBO simulators (VENDOR) | MBP, MBO (VENDOR) | U | U | U | U | yes, paper → live via TradeHub (VENDOR) | C#, Python (VENDOR) | commercial | "100+" venues (VENDOR); feeds U |
| RCM-X Strategy Studio | C3 | tick backtest + fill simulator (ARCHIVE) | tick + book building (ARCHIVE) | silent | silent | silent | silent | implied (ARCHIVE) | U | commercial | readers OneTick, TickData; CME iLink exec (ARCHIVE) |
| NinjaTrader Playback | C4 | L1/L2 replay sim (ARCHIVE DOCS) | L2 MBP; bid/ask volume set to 1 (V) | silent for playback (V) | none in playback (V) | silent | "reproducible results" (V) | yes, NinjaScript (V) | C# (V) | commercial | provider-dependent |
| Bookmap replay | C4 | depth replay + simulator (V) | depth; MBO use silent | displayed FIFO approximation; fill use silent (V) | silent | silent | silent | yes, sim/live platform (U) | Java/Python API (U) | commercial | CME, Nasdaq TotalView, crypto (VENDOR) |
| Sierra Chart replay | C4 | depth replay, sim fills (V) | depth displayed; fills from H/L/Last (V) | none in replay (V) | silent | silent | silent | U | ACSIL (U) | commercial | provider-dependent |

---

# Summary: closest to Kaspar

Kaspar's comparison point: order-level (MBO) historical replay, own orders placed in the real FIFO queue (exact position, no impact), modelled order/cancel/feed latency, deterministic single-threaded market-time execution, the same strategy code live, and native CME MDP3.

1. **hftbacktest** (claims_simulators.md §6) is still the closest open-source tool. It has an L3 FIFO model against historical per-order queues, no impact, feed and order latency models (including latency interpolated from recorded data), and same-code live trading. Gaps relative to Kaspar: live trading is crypto only (Binance Futures, Bybit); CME is reached only through Databento MBO files; no native MDP3 decoder; no determinism claim found.
2. **kpetridis24/lobsim** (§A6) is the closest new find. It does L3 replay, counts exact historical quantity ahead of each paper order, does not mutate the historical book, has optional order-injection latency and claims determinism. It has no live path, no CME/ITCH decoder (Coinbase/LOBSTER examples) and no feed-latency model found. It is a small project (27 stars).
3. **NautilusTrader** (claims_simulators.md §7) has a deterministic runtime and same-code live trading, and reads CME data through Databento `GLBX.MDP3`. Its queue position is an *estimate* of displayed quantity ahead (advanced by per-order deletes under MBO); the simulated order is not inserted into the replayed queue.
4. **Exact queue but the agent mutates the replayed book:** chasemetoyer/Backtesting-Engine (§A7) and koteyevlev/MarketMakingMLAlgo (§A8). Neither models latency or has a verified live path.
5. **Academic methodologies:**
   - Moallemi & Yuan (§B2) is the closest: exact queue on ITCH, filled when a later order at that price fills, no impact, but no latency.
   - Dixon (§B7) uses CME data with an estimated queue, notes that MBO makes it exact, and remarks on latency.
   - Vyetrenko & Xu (§B4) model latency from historical profiles, but on MBP with an estimated queue and synthetic impact.
6. **Commercial:** TT Backtesting (§C1) is the most similar documented product. It replays history through TT's production simulation matcher, and the same ADL algo runs live. Its documentation is silent on queue position in fills (TT's PIQ is "estimated" in the sim environment), on latency, and on whether the data is MBP or MBO, and it warns of conflation at high replay speed. Deltix QuantOffice claims MBO simulators and paper → live, but its details are behind a login.
7. **No native MDP3 decoder found:** no open-source or documented commercial backtester decodes CME MDP 3.0 packet captures itself. CME access in the open-source tools is through Databento-normalised MBO.

## UNVERIFIED items
- Determinism of lobsim and Backtesting-Engine was claimed in their READMEs but not tested.
- Backtesting-Engine's live venues.
- DJ824/hft-backtester's queue handling.
- The repos listed "not inspected" in §A10.
- Vyetrenko & Xu's venue.
- Zhao & Linetsky's simulator details (closed access).
- Kumar 2023 full text.
- The QuantOffice queue/latency model.
- RCM-X Strategy Studio's current availability.
- Bookmap/NinjaTrader playback fill rules with respect to queue position.
- CQG, Rithmic and the CME/Nasdaq test environments.
- The bar-level status of Backtrader, Zipline and VectorBT (not re-checked here).
- Author full names for lobsim (handle `kpetridis24`) and koteyevlev_mm.
