# Claims: limit-order-book simulators and backtesters

Checks done **2026-10-03**. Sources: arXiv PDFs (read via pdftotext), the arXiv API, Crossref and OpenAlex metadata, the OpenReview API, GitHub repo files (`gh api` and shallow clones), and official docs.
Not reachable: dl.acm.org (403 / Cloudflare), dblp.org, lobsterdata.com (client-rendered). Published proceedings full texts were therefore NOT read; arXiv versions were.

Source-kind tags: **[FT]** full text, **[ABS]** abstract, **[META]** publisher/Crossref metadata, **[DOCS]** official docs, **[REPO]** repo files/code.
"VERIFIED" = seen in the cited source. "UNVERIFIED" = not confirmed. An absence ("none found") means a grep/tree search of the default branch plus the paper text found nothing. It is not proof of absence.

Bib keys: new keys are in `bib/refs_sim.bib`. Reused existing keys:

| Key | File |
|---|---|
| byrd2020abides | bib/refs_dl.bib |
| frey2023 | refs.bib (note: a duplicate entry `jaxlob2023` exists in bib/refs_eval.bib for the same work) |
| li2025mars | bib/refs_tok.bib |
| berti2025trades | bib/refs_dl.bib |
| huang2015 | refs.bib |
| deepqr | bib/refs_dl.bib |
| jain2024sim | bib/refs_classic.bib |
| gould2013 | refs.bib |
| lobbench2025 | refs.bib |
| lobster | refs.bib (its content was NOT re-verified here) |
| coletta2021, coletta2022 | bib/refs_dl.bib (not re-checked here) |

New keys: `amrouni2021abidesgym`, `belcak2022maxe`, `mascioli2024pymarketsim`, `jerome2023mbtgym`, `hftbacktest`, `nautilustrader`, `axtell2025abm`.

Bib fixes found for existing entries (not applied; the other files were left untouched):
- `berti2025trades`: arXiv journal_ref gives "ECAI 2025. Volume 413: Pages 3703 - 3710", DOI 10.3233/FAIA251249. The existing entry has no pages. [META: https://export.arxiv.org/api/query?id_list=2502.07071]
- `byrd2020abides`: the arXiv preprint 1904.12066 has a different title, "ABIDES: Towards High-Fidelity Market Simulation for AI Research". The SIGSIM-PADS title in the existing entry matches Crossref. [META: https://api.crossref.org/works/10.1145/3384441.3395986]
- `li2025mars`: OpenReview lists it as "ICLR 2025 Poster", forum Yqk7EyT52H. It has no pages or DOI. The repo README's own BibTeX still cites the arXiv 2024 version. [META: https://openreview.net/forum?id=Yqk7EyT52H]

---

## 1. ABIDES (byrd2020abides)

**Citation and repo**
- Byrd, Hybinette, Balch. SIGSIM-PADS '20, pp. 11–22, DOI 10.1145/3384441.3395986; arXiv 1904.12066. [META: https://api.crossref.org/works/10.1145/3384441.3395986]
- Repo: github.com/abides-sim/abides.

**Type: agent-based discrete-event simulation, with an optional replay agent**
- "Agent-Based Interactive Discrete Event Simulation environment". [ABS: https://arxiv.org/abs/1904.12066]
- The repo has `agent/examples/MarketReplayAgent.py` and `config/marketreplay.py`, which feed historical orders to the ExchangeAgent. [REPO: https://github.com/abides-sim/abides/blob/master/agent/examples/MarketReplayAgent.py]
- In the paper's experiments, historical data enters only through a fundamental-value oracle: background agents get "a noisy observation of the most recent historical trade". [FT §5: https://arxiv.org/pdf/1904.12066]

**Protocol**
- "modeled after NASDAQ's published equity trading protocols ITCH and OUCH". [ABS]
- "The exchange does not yet implement the opening cross auction". [FT §3.4]

**Data level: L3 per-order (replay agent)**
- `L3OrdersProcessor` reads columns `TIMESTAMP, ORDER_ID, PRICE, SIZE, BUY_SELL_FLAG`. [REPO: config/marketreplay.py, MarketReplayAgent.py]
- The data vendor/format is UNVERIFIED; the code path is `/efs/data/DOW30/...` and names no vendor.

**Queue position: exact per-order FIFO in the simulated book**
- "In the case of multiple orders at the same price, the oldest order is selected". [FT §3.5]
- In the code, each price level is a list ("oldest at index 0"), new orders are appended, and matching takes `book[0].pop(0)`. [REPO: util/OrderBook.py]
- Replayed and agent orders share one book, so an agent order is queued by arrival time. This is an inference from the code; the paper does not state it.
- Code observation: `modifyOrder` locates the order at index `mi` but writes `book[i][0] = new_order`, overwriting the queue head. [REPO: util/OrderBook.py ~l.350–360]

**Latency**
- "pairwise agent latency matrix and a latency noise model which are applied to all messages", plus a "computation delay per agent". [FT §3.1]
- Cubic jitter model `min_latency + a/x^3`. [REPO: model/LatencyModel.py]
- ExchangeAgent has `pipeline_delay = 40000` (ns), "delay added only to order activity". [REPO: agent/ExchangeAgent.py]

**Market impact: yes, agents react**
- "large market orders impact simulated prices not just immediately, but for a significant period after". [FT §7]

**Determinism**
- "the entire simulation to be guaranteed identical when the same seed is initialized within the same experimental configuration"; each agent has its own PRNG. [FT §3.1]
- Caveat in the same section: "Events that occur simultaneously (in the same nanosecond) will be executed in arbitrary order". [FT §3.1]

**Live/paper trading:** none found. [FT, REPO tree]

**Language:** Python 3.6, NumPy, Pandas. [FT §4]

**License:** BSD 3-Clause, Georgia Tech Research Corporation 2019. [REPO: LICENSE.txt; FT §8]

**Feeds:** no exchange feed decoder found; protocol messages are ITCH/OUCH-like.

**Speed:** "tens of thousands of trading agents" [ABS], with no timing or hardware stated.

## 2. ABIDES-Gym / ABIDES-Markets (amrouni2021abidesgym)

**Citation and repo**
- Amrouni, Moulin, Vann, Vyetrenko, Balch, Veloso. ICAIF '21, pp. 1–9, DOI 10.1145/3490354.3494433; arXiv 2110.14771. [META: https://api.crossref.org/works/10.1145/3490354.3494433]
- The arXiv PDF prints a placeholder DOI (10.1145/1122445.1122456); do not use it.
- Repo: github.com/jpmorganchase/abides-jpmc-public. It is archived. [REPO: GitHub API]

**Type: agent-based simulation wrapped as an OpenAI Gym environment**
- An interruptible kernel lets a Gym agent pause the simulation. [FT §3.3: https://arxiv.org/pdf/2110.14771]

**Exchange model**
- "NASDAQ equity exchange … regular hours continuous trading session … instructions similar to the OUCH protocol … price/time priority". [FT §2.2]

**Data**
- The public repo has no replay agent. Its only oracles are mean-reverting and sparse mean-reverting. [REPO: tree]
- The background configs are synthetic, e.g. "RMSC04: 1 Exchange Agent, 2 Market Maker Agents, 102 Value Agents, 12 Momentum Agents, 1000 Noise Agents". [REPO: README.md]
- The paper argues against pure replay because it assumes the agent's "trades … [do] not impact future prices". [FT §6.4]
- The book exports L3 data via `get_l3_bid_data` and `get_l3_itch`. [REPO: abides_markets/order_book.py]

**Queue position: exact per-order FIFO inside the simulation**
- Each level has separate visible and hidden queues.
- A size decrease keeps queue position; an increase sends the order to the back. [REPO: abides_markets/price_level.py]

**Latency**
- "An optional latency model is applied to the messaging system". [FT §2.1]
- Two options: `cubic` or `deterministic`, with pairwise minimum latency in ns. [REPO: abides-core/abides_core/latency_model.py]

**Market impact:** yes: "a complex interactive market behavior response to the experimental agent's action". [ABS]

**Determinism**
- The kernel takes "a pseudo-random seed". [FT §2.1.1]
- Examples use `env.seed(0)`. [FT Listing 1]

**Live/paper trading:** none found.

**Language:** Python. [REPO]

**License:** BSD 3-Clause, J.P. Morgan Chase 2021 (Georgia Tech's BSD file is also present). [REPO: LICENSE]

**Speed:** none stated.

## 3. JAX-LOB (frey2023)

**Citation and repo**
- Frey, Li, Nagy, Sapora, Lu, Zohren, Foerster, Calinescu. ICAIF '23, pp. 583–591, DOI 10.1145/3604237.3626880; arXiv 2308.13289. [META: Crossref]
- Repos: github.com/KangOxford/jax-lob and KangOxford/AlphaTrade.

**Type: historical replay of LOBSTER messages on a GPU order-book engine; agent orders injected**
- "the exclusive use of historical LOB data means that only direct market impact is accounted for. Such historical messages have no strategic behavior that may react to an RL agent's actions". [FT §3: https://arxiv.org/pdf/2308.13289]

**Data level: L3 message flow; the book is initialised from L2**
- "the order book state (Level-2) of the first ten levels … is used to initialize the book … assuming that there is exactly one order per price level containing the sum of the listed volumes". [FT §5.1.1]
- The loader reads LOBSTER `Flow_10` and `Book_10` files. [REPO: gymnax_exchange/jaxen/base_env.py]

**Queue position: price-time priority by order timestamp in a fixed-size array**
- "If multiple orders share this price, the one with the earliest arrival time is considered". [FT §4.1]
- The initial L2 levels are synthetic orders with IDs ≤ −9000. A cancel for an unknown ID decrements the synthetic order at the same price (`get_init_id_match`). Before the initial liquidity turns over, the agent's position relative to real pre-existing orders is therefore approximate. [REPO: gymnax_exchange/jaxob/JaxOrderBookArrays.py]
- The execution env cancels and resubmits all agent orders every step. The code comment says: "#TODO avoid being sent to the back of the queue every time". [REPO: gymnax_exchange/jaxen/exec_env.py]

**Latency:** a single `time_delay_obs_act` parameter, defaulting to 0 ns. [REPO: exec_env.py] The paper does not discuss latency.

**Market impact:** direct/mechanical only. [FT §3]

**Determinism:** the code uses JAX PRNG keys. The paper makes no reproducibility claim. [REPO]

**Live/paper trading:** none.

**Language:** JAX/Python on GPU. [FT §4]

**License:** no LICENSE file in either repo (`/license` returns 404). UNVERIFIED / none declared. [REPO]

**Feeds:** LOBSTER (Nasdaq). [FT §5.1]

**Speed (with stated hardware)**
- Per message, N=100 orders, 1000 books in parallel: 2.6 µs on an Nvidia 2080 Ti. A CPU book on one Apple M1 core takes 3.6–7 µs. [FT Table 5]
- Env step: 0.46 ms with 10,000 envs on the 2080 Ti, vs 3.5 ms for the CPU env. [FT Table 6]
- RL training: "550 versus 74 steps per second" on an Nvidia A40 vs a 32-core AMD EPYC 7513. [FT §1, §6]
- Worst-case message processing time is over 2 s. [FT §4.3, Table 4]

## 4. mbt_gym (jerome2023mbtgym)

**Citation and repo**
- Jerome, Sánchez-Betancourt, Savani, Herdegen. ICAIF '23, pp. 619–627, DOI 10.1145/3604237.3626873; arXiv 2209.07823. The v1 title was "Model-based gym environments for limit order book trading". [META: Crossref; ABS]
- Repo: github.com/JJJerome/mbt_gym.

**Type: model-based stochastic**
- "model-based approaches, a category in which this paper sits". [FT §1.1.2: https://arxiv.org/pdf/2209.07823]

**Data level: none; no order book is kept**
- The state is cash, inventory, time, mid-price and arrival/fill state.
- Mid-price models: Brownian, GBM, OU, jumps; the repo adds Heston and CEV.
- Arrivals are Poisson or Hawkes.
- Fills depend on depth only, e.g. P[fill|δ] = e^{−κδ}. [FT §2.1–2.3; REPO: mbt_gym/stochastic_processes/]

**Queue position:** none. "queue" does not appear in any `.py` file. [REPO]

**Latency:** not implemented. The paper lists it as future work via a `LatencyProcess` class. [FT §5; REPO grep]

**Market impact:** parametric only (permanent, temporary and transient impact classes). [FT §2.2; REPO: price_impact_models.py]

**Determinism:** a `seed` argument feeds `np.random.default_rng(seed)`. [REPO: mbt_gym/gym/TradingEnvironment.py]

**Live/paper trading:** none.

**Language:** Python/NumPy. [FT §2.7]

**License:** BSD 3-Clause. [REPO: LICENSE]

**Speed:** 1000 trajectories take 0.2 s vectorised vs 5 min 30 s with multiprocessing, on an AMD Ryzen 7 3800X (8 cores / 16 threads, 64 GB RAM). [FT §2.7, footnote 2]

## 5. MAXE (belcak2022maxe)

**Citation and repo**
- Belcak, Calliess, Zohren. "Fast Agent-Based Simulation Framework with Applications to Reinforcement Learning and the Study of Trading Latency Effects", Multi-Agent-Based Simulation XXII, LNCS, Springer 2022, pp. 42–56, DOI 10.1007/978-3-030-94548-0_4; arXiv 2008.07871. [META: Crossref]
- The arXiv v1 title differed. The LNCS volume number and the editors are UNVERIFIED.
- Repo: github.com/maxe-team/maxe.

**Type: agent-based, message-driven** [FT §1: https://arxiv.org/pdf/2008.07871]

**Data level: simulated book only**
- The agents include Bouchaud zero-intelligence, impact, random-walk market maker and Python agents.
- Loggers record L1, by-order and by-trade data. [REPO: README]
- No historical-data input found (grep for ifstream/replay/historical). [REPO]

**Queue position: exact per-order queues; several matching rules**
- Each tick is a `std::list` of limit orders. `PriceTimeBook` matches the front order.
- `PureProRataBook`, `PriorityProRataBook` and `TimeProRataBook` are also available. [REPO: TheSimulator/Book.h, PriceTimeBook.cpp; FT §2]

**Latency**
- Messages can be delivered with "a non-negative delay which can be used to, for example, model latency". [FT §3]
- Latency effects are studied through a processing delay d, which "includes the two-way latency between the agent and the exchange". [FT §4.1]
- The exchange has a configurable `processingDelay`. [REPO: ExchangeAgent.cpp]

**Market impact:** yes; agents react, with "fall, overreaction, and settlement" phases. [FT §4.2]

**Determinism:** NOT seeded. `std::mt19937` is seeded from `std::random_device`, and "seed" appears nowhere in the C++ sources. [REPO: Simulation.cpp]

**Live/paper trading:** none.

**Language:** C++ core with a Python API (pybind11) and XML configs. [FT §3; REPO]

**License:** MIT. [REPO: LICENSE]

**Speed/memory**
- In the comparison, ABIDES stopped at 416 agents when it exceeded "the 16GiB of memory available in our small workstation". MAXE ran "100,022 agents … into 100MiB". [FT §6]
- The CPU is not stated.

## 6. hftbacktest (hftbacktest)

**Repo**
- The real repo is **github.com/nkaz001/hftbacktest**; "nkaujanga" is incorrect. Created 2022-08-25. [REPO: gh api]
- Clone read at master @ 5f3ec40 (2025-12-23).

**Type: market-data replay backtester; no market impact**
- "HftBacktest is a market-data replay-based backtesting tool, which means your order cannot make any changes to the simulated market, no market impact is considered". [DOCS: https://github.com/nkaz001/hftbacktest/blob/master/docs/order_fill.rst]

**Data level: L2 MBP and L3 MBO**
- "Full order book reconstruction based on Level-2 Market-By-Price and Level-3 Market-By-Order feeds". [REPO: README.rst]

**Queue position: two kinds of model** [REPO: hftbacktest/src/backtest/models/queue.rs]
- L2 (estimated):
  - `RiskAdverseQueueModel`: the position advances "only when trades occur at the same price level".
  - `ProbQueueModel`: the position also advances when the level quantity decreases, with probability from the relative position.
  - Probability functions: `PowerProbQueueFunc` (and variants 2 and 3), `LogProbQueueFunc` and `LogProbQueueFunc2`.
  - The code comments cite rigtorp.se (2013) and quant.stackexchange Q3782.
- L3 (exact under the FIFO assumption): `L3FIFOQueueModel`. In the code, "all orders, including backtest orders, are managed in a FIFO queue based on price-time priority". Backtest orders fill when a market order executes the order behind them.
  - The doc comment warns that venues may use pro-rata matching, so the model must be chosen carefully.
  - The struct keeps `bid_queue: HashMap<i64, VecDeque<Order>>` (spot-checked locally).

**Latency** [DOCS: docs/latency_models.rst; REPO: models/latency.rs]
- The docs name feed latency, order entry latency and order response latency.
- Feed latency comes from the data's two timestamps (local and exchange).
- `ConstantLatency`.
- `IntpOrderLatency` interpolates "actual historical order latency data". It is collected by submitting unexecutable orders and stored as rows of req/exch/resp timestamps.
- Artificial order latency can be generated from feed latency (a utility, not a separate model).
- Custom models plug in through the `LatencyModel` trait.

**Fills**
- `NoPartialFillExchange` (the default) and `PartialFillExchange`.
- Taker orders "will be fully executed at the best" regardless of quantity. [DOCS: order_fill.rst]

**Live trading:** "using the same algorithm code: currently for Binance Futures and Bybit. (Rust-only)". Connectors: binancefutures, binancespot, bybit. [REPO: README.rst, connector/src/]

**Feeds**
- Collectors: Binance, Bybit, Hyperliquid.
- Converters: Binance, Bybit, Databento, Hyperliquid, MEXC, Tardis.
- CME is reached only through Databento MBO files: "DataBento's historical data includes a Start-of-Day (SOD) snapshot for CME data"; the tutorial input is `glbx-mdp3-*.mbo.dbn.zst`. There is no native MDP3 decoder. [REPO: py-hftbacktest/hftbacktest/data/utils/databento.py; examples/Level-3 Backtesting.ipynb]
- No ITCH/Nasdaq mentions found. [REPO grep]

**Language:** Rust core; Python bindings (maturin) usable from Numba JIT functions. [REPO: pyproject.toml, README]

**License:** MIT, 2022. [REPO: LICENSE]

**Determinism:** no explicit claim found (UNVERIFIED).

**Speed:** no numeric claims in the README or docs (the "Accelerated Backtesting" tutorial was not read).

**Citation:** no paper, CITATION.cff or DOI. The author's real name does not appear in the files read.

## 7. NautilusTrader (nautilustrader)

**Repo**
- github.com/nautechsystems/nautilus_trader, created 2018-06-25.
- Clone read at develop @ ad9d283, version v2.0.0rc6.

**Type: event-driven backtester (historical replay) plus live trading platform**

**Backtest/live parity**
- "The same strategy and execution-algorithm code can run across backtest and live systems".
- The README also cautions that live execution "introduces venue, transport, timing … behavior that a simulation may not reproduce". [REPO: README.md]

**Determinism**
- "deterministic event-driven runtime for both research and live execution". [REPO: README.md]
- Fill models: "Set `random_seed` when a run must reproduce the model's random draws". [DOCS: docs/concepts/backtesting/fill-models.md]

**Data levels:** `L3_MBO`, `L2_MBP` and `L1_MBP` books, plus trade- and bar-driven execution. [DOCS: docs/concepts/order_book.md, backtesting/data-and-venues.md]

**Queue position: estimated from displayed size**
- "Set `queue_position=True` with `trade_execution=True` to track displayed quantity ahead of each LIMIT order". The order snapshots same-side displayed size at its price, and same-side trades reduce it.
- L2: an UPDATE caps the quantity ahead at the new level size.
- L3 MBO: "A per-order DELETE advances the queue by that order's remaining tracked size".
- Stated limits: limit orders only; "Historical data cannot reveal hidden orders or every venue-specific priority rule"; NO_AGGRESSOR trades reduce both sides ("optimistic"). [DOCS: docs/concepts/backtesting/trade-execution.md]
- Default fill: `prob_fill_on_limit` (default 1.0).
- `liquidity_consumption=True` tracks consumed size per level. "Consumption tracking estimates **available size**, not **order priority**". [DOCS: fill-models.md, fill-prices-and-matching.md]

**Latency**
- `StaticLatencyModel` (base plus insert/update/cancel delays) is the only built-in model; Rust users can implement the trait. [DOCS: behavioral_models.md]
- Commands enter the venue's inflight queue with an arrival timestamp. [DOCS: execution-flow.md]
- In the sandbox, venue-generated events are not delayed (inbound leg only).
- No feed-latency model was found (UNVERIFIED absence).

**Market impact:** none found as a modelled effect. Replay with optional liquidity consumption is the only related mechanism (UNVERIFIED that no impact model exists).

**Language:** Rust core, Python control plane via PyO3 (v2). The v1 Cython package is legacy. [REPO: README.md, MIGRATION_V2.md]

**License:** LGPL-3.0. [REPO: LICENSE]

**Feeds/adapters (README "stable")**
- Exchanges and brokers: AX Exchange, Betfair, Binance, Bybit, Coinbase, Deribit, Derive, dYdX, Hyperliquid, Interactive Brokers, Kraken, Lighter, OKX, Polymarket.
- Data vendors: Databento, Tardis.
- CME: via Databento `GLBX.MDP3`. The MBO schema is described as "Per-order events for queue position modeling and exact book reconstruction". [DOCS: docs/integrations/databento.md]
- No native MDP3 decoder was checked in the source (UNVERIFIED either way).

**Speed:** qualitative only ("Rust core with the mimalloc allocator…", "fast enough to train AI trading agents"). No numeric figure or hardware. [REPO: README.md]

**Citation:** no paper or CITATION.cff; cite as software.

## 8. MarS (li2025mars)

**Citation and repo**
- Li, Liu, Liu, Fang, Wang, Xu, Bian (MSRA). ICLR 2025 (poster); arXiv 2409.07486v2. [FT title page: https://arxiv.org/pdf/2409.07486; META: OpenReview]
- Repo: github.com/microsoft/MarS.

**Type: generative (order-level foundation model "LMM"), combined with a matching engine and injectable agents**
- "At the core of MarS is the simulated clearing house, which matches both generated and interactive orders in real-time". [FT §3]
- An order model (2M–1.02B parameters) and an order-batch model (150M–3B parameters) are combined. [FT Fig. 3, App. C]

**Data**
- "top 500 liquidity stocks in the Chinese stock market … 2017 to 2023 … 16 billion order tokens"; LLaMA2-based. [FT App. B.3]
- The README says "production-grade applications require complete order-level historical data"; the demos use noise agents. [REPO: README]

**Queue position: exact within the simulated book**
- `Level` keeps a dict of orders and walks them in insertion order (FIFO); cancels name a `cancel_id`. [REPO: mlib/core/level.py]
- Queue position in the *real* market is not modelled.

**Latency**
- The README says states and actions are distributed "considering network and computational latency". [REPO: README]
- Events are scheduled at `wakeup_time + computation_delay + communication_delay`. [REPO: mlib/core/engine.py]

**Market impact: yes**
- "inject their own orders … observe how these actions impact market dynamics in real-time". [FT §3]
- The model "naturally learns immediate market impact". [FT]
- Example: `market_simulation/examples/market_impact.py`. [REPO]

**Determinism**
- Sampling is stochastic (`torch.multinomial` with temperature).
- Example scripts take seeds. Bit-exactness on GPU is UNVERIFIED. [REPO]

**Live/paper trading:** none found.

**Language:** Python/PyTorch; Ray Serve; models of 2M–10M parameters are released on HuggingFace. [REPO]

**License:** MIT. [REPO]

**Speed:** none in the paper. The README says "we utilized 128 GPUs running parallel simulations" but does not name the GPU model. [REPO: README]

## 9. DeepMarket / TRADES (berti2025trades)

**Citation and repo**
- Berti, Prenkaj, Velardi. ECAI 2025, FAIA vol. 413, pp. 3703–3710, DOI 10.3233/FAIA251249; arXiv 2502.07071. [META: arXiv API]
- No separate "DeepMarket" paper was found. DeepMarket is the open-source framework introduced in the TRADES paper ("the first open-source Python framework for LOB market simulation with deep learning"). [FT: https://arxiv.org/pdf/2502.07071; REPO: github.com/LeonardoBerti00/DeepMarket]

**Type: generative (diffusion), run as a "world agent" inside an extended ABIDES**
- The paper calls this "a hybrid approach between deep learning-based and agent-based simulations". [FT §4, §7]

**Data**
- LOBSTER Nasdaq data, TSLA and INTC, January 2015, ~24M samples.
- Conditioning uses an L-level LOB snapshot; the released dataset uses the top 10 levels. [FT §6.1, §7]
- Each generated order is (price, quantity, direction, depth, time offset, type).

**Queue position: exact within the simulated book (ABIDES FIFO)**
- Generated cancels are mapped to the resting order at that price "with the quantity closer to the quantity generated". [REPO: ABIDES/agent/WorldAgent.py, util/OrderBook.py]

**Latency**
- The shipped config runs `kernel.runner` without a latency model and with `defaultComputationDelay = 0`.
- The kernel defaults are `defaultLatency = 1`, `latencyNoise = [1.0]`.
- `LatencyModel.py` is present but not wired in. [REPO: ABIDES/config/world_agent_sim.py, Kernel.py]

**Market impact: yes**
- In a POV-agent experiment, historical replay shows "only instantaneous impact" while the diffusion simulation shifts the price "permanently". [FT §7]

**Determinism**
- A `-seed` flag seeds torch, numpy and the kernel. [REPO]
- DDIM sampling with η=0 "is deterministic". [FT §7.2]

**Live/paper trading:** none found.

**Language:** Python, PyTorch Lightning. **License:** MIT. [REPO]

**Speed**
- "each hour of market simulation required six hours of computation on an RTX 3090".
- One-step DDIM gives a "100-fold increase" in speed at a cost in quality. [FT §7, §7.2]

## 10. PyMarketSim (mascioli2024pymarketsim), Wellman group

**Citation**
- Mascioli, Gu, Wang, Chakraborty, Wellman. ICAIF '24, pp. 117–125, DOI 10.1145/3677052.3698639. [META: Crossref]
- Abstract (via OpenAlex): "agent-based environment incorporates key elements such as private valuations, asymmetric information, and a flexible limit order book mechanism". [ABS: https://api.openalex.org/works/doi:10.1145/3677052.3698639]
- Full text NOT read (ACM 403). All paper claims beyond the abstract are UNVERIFIED.

**Repo**
- github.com/dipplestix/pymarketsim; the owner's profile name is Chris Mascioli; Python; MIT.
- That this is the paper's repo is UNVERIFIED (the paper was not read). [REPO]

**Type: agent-based, synthetic**
- Background agents: ZI, market maker, informed, noise, spoofer, HBL.
- The fundamental is a mean-reverting Gaussian process. [REPO: marketsim/agent, marketsim/fundamental]

**Book**
- A "four-heap order book that respects price-time priority", with `(price, order_id)` heap keys.
- Time steps are discrete. The code comment reads "TODO Need to figure out how to handle ties for price and time".
- Agents withdraw all their orders on each arrival. [REPO: marketsim/fourheap/order_queue.py, market/market.py, simulator/simulator.py]
- Queue position of a persistent agent order is therefore not meaningful in the shipped agents. This is an inference from the code.

**Latency:** none. [REPO: event_queue.py]

**Determinism**
- `EventQueue(rand_seed)` builds a seeded RNG, but `step()` uses the global `random.shuffle`, and `Simulator.step` uses global `random.random()`.
- So a seed alone does not control the shuffle unless the global RNG is also seeded. This is an inference from the code. [REPO]

**Live/paper trading:** none. **Data feeds:** none (synthetic).

**Older egtaonline/market-sim:** Apache-2.0. The default branch holds only LICENSE and README, and the README's tags are absent, so its content is UNVERIFIED. [REPO]

## 11. Queue-reactive model (huang2015) and MDQR (deepqr)

**Queue-reactive model: citation**
- Huang, Lehalle, Rosenbaum. JASA 110(509):107–122, 2015, DOI 10.1080/01621459.2014.982278; preprint arXiv 1312.0563v2. [META: Crossref; FT: https://arxiv.org/pdf/1312.0563v2]

**Type: model-based stochastic**
- Queue sizes X(t) = (q−K…qK) form "a continuous-time Markov jump process"; intensities "only depend on the current state of the order book"; K=3.
- The reference price changes with probability θ when a best queue depletes; the book is re-initialised with probability θ_reinit. [FT §2.1, §3.1]

**Data level: MBP**
- Queue sizes are in units of average event size.
- Calibration data: Cheuvreux database, Euronext Paris, Jan 2010–Mar 2012, up to 5 levels; France Telecom and Alcatel-Lucent. [FT §2.1–2.2]

**Own-order queue position: modelled under assumptions**
- §2.5 computes the execution probability of a tagged limit order. Earlier orders have priority, and cancels fall uniformly at random on orders other than the tagged one, which "is never canceled" (Assumptions 3–4).
- The authors note that exact treatment "would need more detailed market data keeping records of the identifiers", and that "execution probabilities might be slightly overestimated". [FT §2.5]

**Market impact: yes**
- "the limit orders change the queue sizes and therefore modify the behaviors of the order flows. Consequently they generate market impact". The impact is concave in time and volume over 2000 simulations. [FT §3.2]

**Not in the paper:** latency (no mention found) and code release.

**MDQR (deepqr)**
- Bodor, Carlier, arXiv 2501.08822v1 (Université Paris 1 / BNP Paribas), no journal_ref.
- Relaxes queue independence, adds market features, and models order sizes; intensities come from an MLP. [ABS]
- Data: Euro-Bund futures (FGBL, Eurex), Mar–Jun 2022, 5 levels (MBP). [FT §3.2]
- Claims to reproduce "the square-root law of market impact" (TWAP agent experiment). [ABS; FT Fig. 7]
- Code, latency and speed: none found (UNVERIFIED).

## 12. Other replay tools

- **LOBSTER (key `lobster`):** UNVERIFIED in this pass. lobsterdata.com is client-rendered and returned no text, and SSRN did not load. Its description as Nasdaq ITCH reconstruction was not re-confirmed here. JAX-LOB and TRADES both consume LOBSTER files (VERIFIED, see §3 and §9).
- **lobsim, PyLOB:** not checked (the agent's search budget ran out). UNVERIFIED; omitted.

## 13. Surveys and overviews

- **jain2024sim:** Jain, Firoozye, Kochems, Treleaven, "Limit Order Book Simulations: A Review", arXiv 2402.17359 (v2, 1 Mar 2024); SSRN DOI 10.2139/ssrn.4745587; no journal version found.
  - Scope: classifies models by methodology, collects stylized facts, studies price impact and empirical fit. [ABS: arXiv API; META: Crossref]
- **axtell2025abm (new):** Axtell, Farmer, "Agent-Based Modeling in Economics and Finance: Past, Present, and Future", JEL 63(1):197–287, 2025, DOI 10.1257/jel.20221319. [META: Crossref; ABS: OpenAlex] Full text not read.
- **gould2013 (reused):** "Limit order books", Quantitative Finance 13(11):1709–1742. A review of LOBs, not of simulators. [META: Crossref]
- **lobbench2025 (reused):** a benchmark for generative LOB models (ICML 2025, PMLR 267). Not a survey. [META: arXiv API]

---

## Comparison table

Legend: **V** = VERIFIED from the cited source in the sections above; **U** = UNVERIFIED; "none (V)" = absence by repo/paper search.

| Simulator | Type | Data level | Own-order queue position | Latency model | Simulated order affects market | Determinism | Live/paper path | Language | License | Feeds / data | Speed claim (hardware) |
|---|---|---|---|---|---|---|---|---|---|---|---|
| ABIDES | agent-based DES + optional L3 replay agent (V) | L3 per-order (V) | exact FIFO in simulated book (V) | pairwise network + noise, compute delay, exchange pipeline delay (V) | yes, agents react (V) | single seed → identical runs claimed; same-ns events arbitrary order (V) | none (V) | Python (V) | BSD-3 (V) | ITCH/OUCH-style messages; replay file vendor U | agent count only, no timing (V) |
| ABIDES-Gym / Markets | agent-based, Gym wrapper (V) | simulated L3; no replay in public repo (V) | exact FIFO, visible + hidden queues (V) | cubic or deterministic pairwise (V) | yes (V) | seeded kernel (V) | none (V) | Python (V) | BSD-3 (V) | synthetic (oracles) (V) | none (V) |
| JAX-LOB | historical replay + injected agent orders, GPU (V) | L3 messages; init from L2 as one synthetic order/level (V) | price-time by timestamp; approximate vs. pre-existing initial liquidity; agent orders re-queued each step (V) | single obs-to-action delay, default 0 (V) | direct/mechanical only (V) | PRNG keys; no claim (V) | none (V) | JAX/Python (V) | no license file (V) / license U | LOBSTER Nasdaq (V) | 2.6 µs/msg on 2080 Ti; 550 vs 74 steps/s, A40 vs EPYC 7513 (V) |
| mbt_gym | model-based stochastic (V) | no book (V) | none; depth-based fill prob. (V) | none, future work (V) | parametric impact models (V) | seed → numpy rng (V) | none (V) | Python/NumPy (V) | BSD-3 (V) | none, synthetic (V) | 1000 traj. in 0.2 s, Ryzen 7 3800X (V) |
| MAXE | agent-based (V) | simulated book; L1/by-order logs (V) | exact; price-time and pro-rata variants (V) | message delay + exchange processing delay (V) | yes (V) | not seeded, random_device (V) | none (V) | C++ + Python (V) | MIT (V) | none, synthetic (V) | 100,022 agents in 100 MiB; CPU not stated (V) |
| hftbacktest | historical replay, no impact (V) | L2 MBP and L3 MBO (V) | L2: estimated (risk-averse / probabilistic); L3: exact FIFO assumption (V) | constant, interpolated historical order latency, feed latency from timestamps (V) | no (V) | no claim found (U) | live, same code: Binance Futures, Bybit, Rust-only (V) | Rust + Python/Numba (V) | MIT (V) | Binance, Bybit, Hyperliquid, MEXC, Tardis, Databento (CME MBO via files) (V); no ITCH (V) | none numeric (V) |
| NautilusTrader | event-driven replay backtester + live platform (V) | L1/L2 MBP, L3 MBO, trades, bars (V) | estimated displayed-qty-ahead tracking; L3 deletes advance (V) | static insert/update/cancel delays; inbound only (V); feed latency none found (U) | no impact model found (U) | "deterministic event-driven runtime"; fill-model random_seed (V) | live and sandbox, same strategy code (V) | Rust + Python (PyO3) (V) | LGPL-3.0 (V) | crypto venues, IB, Betfair, Databento incl. GLBX.MDP3, Tardis (V) | qualitative only (V) |
| MarS | generative foundation model + matching engine (V) | order-level; trained on Chinese equities (V) | exact FIFO in simulated book (V) | computation + communication delays (V) | yes, model conditions on injected orders (V) | stochastic sampling, seeds in examples; bit-exactness U | none (V) | Python/PyTorch (V) | MIT (V) | Chinese A-share order data (paper) (V) | 128 GPUs, model unnamed (V) |
| DeepMarket / TRADES | generative diffusion world-agent in ABIDES (V) | generated order events; conditioning on top-10 levels (V) | exact FIFO in simulated book (V) | none in shipped config (V) | yes, permanent impact vs replay (V) | seed flag; DDIM η=0 deterministic (V) | none (V) | Python (V) | MIT (V) | LOBSTER Nasdaq TSLA, INTC (V) | 1 h sim = 6 h compute, RTX 3090 (V) |
| PyMarketSim | agent-based, synthetic (abstract V; details from repo V; paper body U) | simulated four-heap book (V) | price-time book, but agents withdraw orders on arrival (V) | none (V) | yes, agents (V) | partial: global RNG used (V) | none (V) | Python (V) | MIT (V); repo-paper link U | none (V) | none found (U) |
| Queue-reactive (huang2015) | model-based stochastic Markov queues (V) | MBP, 5 levels (V) | tagged order under uniform-cancel assumptions (V) | none (V) | yes, queue-size feedback (V) | n/a | n/a | no code (V) | n/a | Euronext Paris, Cheuvreux (V) | none (V) |
| MDQR (deepqr) | model-based, neural intensities (V) | MBP, 5 levels (V) | U | U | yes, square-root impact (V) | U | U | U | U | Eurex FGBL (V) | U |

## Summary

- Twelve simulators/models are covered. The four simulators that keep an exact per-order FIFO queue (ABIDES, ABIDES-Markets, MAXE, MarS; TRADES runs inside ABIDES) do so in a *simulated* market, where the agent's position is exact by construction. Among the replay tools:
  - hftbacktest is the only one with an exact L3 FIFO model (`L3FIFOQueueModel`) against reconstructed historical per-order queues.
  - JAX-LOB uses price-time priority, but its initial L2 snapshot is collapsed to one synthetic order per level.
  - NautilusTrader and hftbacktest's L2 models *estimate* the quantity ahead.
- Latency:
  - ABIDES (pairwise network, compute and exchange delays) and hftbacktest (entry/response latency interpolated from recorded live data, plus feed latency from timestamps) have the most detailed models.
  - mbt_gym, PyMarketSim and the shipped TRADES config have none.
- A live path from the same code was verified only for hftbacktest (Binance Futures, Bybit) and NautilusTrader (many venues).
- CME data reaches hftbacktest and NautilusTrader only through Databento `GLBX.MDP3` files/adapters; no native MDP3 decoder was verified in any of them. No ITCH decoder was found in hftbacktest.
- Determinism claims:
  - Explicit: ABIDES (seed → identical runs, except that same-nanosecond events run in arbitrary order) and NautilusTrader (deterministic runtime, fill-model seed).
  - Not seeded: MAXE.
  - Partially seeded: PyMarketSim.
- UNVERIFIED items:
  - PyMarketSim paper body.
  - LOBSTER details.
  - JAX-LOB license (no file).
  - MAXE LNCS volume and editors.
  - hftbacktest author's real name.
  - lobsim and PyLOB (not checked).
