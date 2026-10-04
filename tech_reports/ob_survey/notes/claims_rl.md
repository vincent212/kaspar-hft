# Claims: reinforcement learning on limit order books (for "Prediction versus policy learning")

Checks done **2026-10-04**. Supplements `claims_simulators.md` (§2 ABIDES-Gym, §3 JAX-LOB, §4 mbt_gym), `claims_simulators2.md` (§A'1 Spooner/rl_markets, §A'2 Sadighian/crypto-rl, §B3 Balch, §B4 Vyetrenko & Xu, §B5 Karpe, §B8 Kumar/Guo/Zhao/Gašperov 2022/Hambly) and `claims_lalor.md`. Facts already recorded there are not repeated unless they are needed for an RL-specific point.

Method:
- arXiv PDFs fetched with curl and read with pdftotext (sections grepped, then key passages read in full). Non-arXiv PDFs: Nevmyvaka et al. (author PDF, cis.upenn.edu), Lin & Beling (ijcai.org), Gašperov et al. 2021 survey (mdpi-res.com PDF), Gašperov & Kostanjčar 2021 (IEEE Access OA PDF via OpenAlex link).
- Venue/pages/DOI from the Crossref API and arXiv API. Web search quota was exhausted; discovery used the arXiv API (title queries), the Crossref API and the reference lists of the papers read.
- Nothing was run.

Tags: **[FT]** full text read (the passages cited were read; long papers were not read line by line), **[ABS]** abstract only, **[META]** Crossref/arXiv metadata, **[SEC]** stated by a secondary source only (named). "UNVERIFIED" = not confirmed. "Silent" = the text read does not address the point. "Peer-reviewed" = journal or archival conference proceedings; workshop papers and arXiv-only items are marked as such.

## Bib keys

New entries: `bib/refs_rl.bib`. Reused existing keys (not repeated there):

| Key | File | Used for |
|---|---|---|
| hambly2023 | bib/refs_sim2.bib | survey (§1.1) |
| spooner2018, rl_markets | bib/refs_sim2.bib | MM on MBP replay (§3.1) |
| sadighian2019, cryptorl | bib/refs_sim2.bib | crypto MM (§3.4) |
| kumar2020drl, kumar2021hawkes, kumar2023marl | bib/refs_sim2.bib | MM, agent-based (§3.8) |
| guo2023mm, zhao2021ber, gasperov2022 | bib/refs_sim2.bib | MM (§3.8) |
| karpe2020 | bib/refs_sim2.bib | execution in ABIDES replay (§2.5) |
| balch2019replay, vyetrenko2019 | bib/refs_sim2.bib | replay vs interactive simulation (§4) |
| lalor2025nhp | bib/refs_dl.bib | neural Hawkes sim + SAC MM (§3.8) |
| frey2023 | refs.bib (duplicate `jaxlob2023` in bib/refs_eval.bib) | JAX-LOB execution env (§2.7) |
| amrouni2021abidesgym | bib/refs_sim.bib | ABIDES-Gym execution env (§2.7) |
| jerome2023mbtgym | bib/refs_sim.bib | model-based gym (§3.7) |
| byrd2020abides, coletta2022 | bib/refs_dl.bib | simulators (§4) |
| bailey2014pbo, bailey2014 | refs.bib | backtest overfitting (§4; not re-read here) |
| avellaneda2008, almgren2001 | refs.bib | baselines named by RL papers |

New keys: `nevmyvaka2006`, `ning2021ddqn`, `lin2020ppo`, `hendricks2014`, `gueant2019bonds`, `sadighian2020`, `ganesh2019dealer`, `gasperov2021survey`, `gasperov2021signals`, `sun2023qtsurvey`, `vyetrenko2020getreal`, `coletta2023cgan`, `ragel2025timetravel`, `niu2024imm`, `fang2021opd`, `zhang2023generalizable`, `roavicens2019irl`.

---

# 1. Surveys

## 1.1 hambly2023 — Hambly, Xu & Yang, "Recent advances in reinforcement learning in finance", Mathematical Finance 33(3):437–503, 2023, DOI 10.1111/mafi.12382. **Peer-reviewed.** [FT arXiv 2112.04553v4]
- Sections 4.2 (optimal execution) and 4.5 (market making) survey RL papers; §4.1 covers microstructure background.
- Execution benchmarks it names: "Time-Weighted Average Price (TWAP) and Volume-Weighted Average Price (VWAP) as well as the Submit and Leave (SnL) policy". Typical state: time, (mid-)price/spread, inventory, past returns; typical control: market-order quantity and/or limit price. [FT §4.2]
- On Spooner et al.: "since the market is reconstructed from historical data, simulated orders placed by an agent cannot impact the market". [FT §4.5]
- **Discrepancy:** Hambly et al. describe Spooner's reward as "the sum of a symmetrically dampened PnL and an inventory cost"; Spooner et al.'s consolidated agent "uses the asymmetrically dampened reward function" (Spooner FT §5.4). Cite Spooner directly.
- Future directions (§5): general RL algorithms "tend to overfit"; "Online learning ... is impractical for many financial decision-making problems, especially in the high-frequency regime"; offline/batch RL methods exist but "focus on general methodologies without being specifically tailored to financial applications"; "Financial time series are known to be non-stationary ... historical data that are further away in time may not be helpful", hence a need for "good market simulators that could generate (unlimited) realistic market scenarios".

## 1.2 gasperov2021survey — Gašperov, Begušić, Posedel Šimović & Kostanjčar, "Reinforcement learning approaches to optimal market making", Mathematics 9(21):2689, 2021, DOI 10.3390/math9212689. **Peer-reviewed (MDPI).** [FT, 23 pp.]
- Search: Scopus, Google Scholar, Web of Science with "market making" AND "reinforcement learning"; 23 references, 22 analysed in §4; covers 2001–2021; "20/23 or 87%" from 2018 on. Includes grey literature (a thesis, arXiv preprints).
- **Environment (§4.4):** "13/22 (59%) of the considered papers ... employed simulators, primarily based on analytical MM models. Among them, 3/22 (14%) were based on agent-based models and 3/22 (14%) on the AS model. Only 4/13 (31%) papers employed simulators calibrated with historical data." "single-agent market replay ... of historical data was utilized in 9/22 (41%) papers ... this can easily result in overfitting, i.e., the inability of the RL agent to generalize out-of-time."
- **Rewards (§4.7):** "All considered papers employed PnL-based rewards, either with (11/22 or 50%) or without (11/22 or 50%) some form of inventory penalization, mostly running (9/11 or 82%)." 3/22 used CARA utility.
- **Benchmarks (§4.10):** "Fixed-offset strategies (4/22 or 18%) and AS/GLFT approximations (4/22 or 18%) represent the most frequent benchmarks"; "Promisingly, all 22 references reported positive results. However, due to the vast diversity of used datasets, experimental setups, and implementation details, side-to-side performancewise comparison of the existing approaches remains a daunting task", "further exacerbated by the crisis of reproducibility ... in DRL".
- **Non-stationarity (§5):** "Virtually all current approaches assume stationarity ... the issue of the nonstationarity of financial markets still goes unaddressed."
- Model-free dominates: 20/22 (91%). Discrete actions in 15/22. Function approximation in 16/22.
- Live trading: the words "live"/"deployment" do not occur in the text (grep). Silent.

## 1.3 sun2023qtsurvey — Sun, Wang & An, "Reinforcement learning for quantitative trading", ACM TIST 14(3):1–29, 2023, DOI 10.1145/3582560. **Peer-reviewed.** [FT of arXiv 2109.13851v1 (2021); the 2023 journal version was not read]
- Covers algorithmic trading, portfolio management, order execution, market making (Tables 6, 7 summarise execution and MM papers).
- Execution limitations: "most of algorithms are only tested on stock data"; "the execution time window (e.g., 1 day) is too long, which makes the task easier".
- MM: "one major obstacle is the lack of high-fidelity micro-level market simulator. At present, there is still no reasonable way to simulate the ubiquitous market impact."
- Challenges: "severe distribution shift of financial market makes RL-based methods exhibit poor generalization"; "learning through directly interacting with the real market is risky and impractical. RL-based QT normally use historical data to learn a policy, which fits in offline RL settings."
- "simulation with only historical market data is not enough". **Discrepancy:** it says "Spooner et al. tried to take market impact into consideration for MM with event-level data"; Spooner et al. state their simulated orders "cannot impact the market" (Spooner FT §3).
- **A survey dedicated only to RL for optimal execution was not found** (arXiv title queries "execution AND reinforcement AND (survey OR review)" returned nothing; Crossref queries returned no such survey). UNVERIFIED that none exists.

---

# 2. RL for optimal execution

## 2.1 nevmyvaka2006 — Nevmyvaka, Feng & Kearns, "Reinforcement learning for optimized trade execution", ICML 2006, pp. 673–680, DOI 10.1145/1143844.1143929. **Peer-reviewed.** [FT author PDF]
- **Environment: historical order-flow replay with agent orders inserted.** "historical records from the INET Electronic Communication Network"; "a simulator ... that combines the real world INET order flow with artificial orders generated by our execution strategies. It executes orders and maintains priorities in order books". 1.5 years of NASDAQ data, AMZN, NVDA, QCOM; 12 months train / 6 months test.
- **Impact:** training assumes "our actions do not affect market state variables"; but "all test set order book simulations maintain the impacts of any policy actions" (mechanical impact within the replayed book; no reaction of other traders: "we do not explicitly model strategic order submission by other traders").
- **State:** private variables elapsed time t and remaining inventory i (resolutions T, I), plus discretised market variables (spread, bid–ask volume imbalance, immediate market-order cost, signed transaction volume over 15 s).
- **Action:** reprice all remaining shares as one limit order at ask − a (sell) relative to the touch; remainder executed at market at horizon end.
- **Reward:** cash proceeds of executions; performance measured as trading cost in bps vs the mid at episode start.
- **Algorithm:** Q-learning/dynamic programming hybrid, backward in time, exploiting the private/market-variable independence.
- **Baseline:** optimized submit-and-leave (S&L); market order.
- **Result:** private variables only: "Relative improvement over S&L ranges from 27.16% to 35.50%"; with market variables, "improvement in execution of 50% or more over S&L". Table 1: adding spread + immediate cost + signed volume gives a further 12.85% cost reduction; volume imbalance only 0.13%. Conditions: V = 5,000 or 10,000 shares, H = 2 or 8 min.
- **Latency/fees:** "we assume that commissions and exchange fees are negligible"; "we do not account for possible order arrival delays".
- Queue details of the simulator beyond "maintains priorities" are not given.

## 2.2 ning2021ddqn — Ning, Lin & Jaimungal, "Double deep Q-learning for optimal execution", Applied Mathematical Finance 28(4):361–380, 2021 (Crossref; DOI 10.1080/1350486X.2022.2077783). **Peer-reviewed.** [FT arXiv 1812.06600v2]
- **Environment:** historical mid-prices, no order-book simulation. "We use the full limit order book information to extract the midprice at the end of each second"; "we approximate all execution prices by the mid-price and ignore the spread"; walking the book replaced by a quadratic penalty.
- **Impact:** "we assume the trader's actions do not directly effect the price process during training".
- **Data:** 9 tickers (AAPL, AMZN, FB, GOOG, INTC, MSFT, NTAP, SMH, VOD), 2 Jan 2017 – 30 Mar 2018, hours 11–13 analysed separately.
- **State:** time, inventory, transformed price, quadratic variation (TIP / TIPQV feature sets).
- **Action:** number of shares to sell by market order in each of N = 5 periods of a 1-hour horizon (executed evenly each second). Market orders only.
- **Reward (Eq. 3.1):** q(p_{t+1} − p_t) − a(x/M)², a = 0.01.
- **Baseline:** TWAP. Metric: P&L improvement over TWAP in bps per hour.
- **Result (Table 1):** mean ΔP&L from −0.28 (AMZN, TIP) to 15.43 bps (VOD, TIPQV); P(ΔP&L > 0) from 45.7% (GOOG) to 97.8% (INTC). Conclusion: "outperforms TWAP on seven out of the nine stocks".
- **Train/test split:** not stated in v2 (grep for "test"/"out-of-sample" finds no split). UNVERIFIED whether Table 1 is out of sample.
- Limitation stated: "restriction to market orders is sub-optimal".

## 2.3 lin2020ppo — Lin & Beling, "An end-to-end optimal trade execution framework based on proximal policy optimization", IJCAI 2020, pp. 4548–4554, DOI 10.24963/ijcai.2020/627. **Peer-reviewed.** [FT]
- **Data/environment:** NYSE millisecond TAQ (WRDS) 2018, 14 stocks, "reconstruct it into the LOB"; "Only the top 5 price levels ... are kept and aggregated at 5 seconds". Train Jan–Sep, test Oct–Dec. Horizon 1 minute, 5-s steps. Hyperparameters tuned on FB only.
- **State:** top-5 bid/ask prices and volumes (stacked or LSTM) plus remaining inventory and elapsed time.
- **Action:** shares to trade, range 0 to 2×TWAP volume; quadratic penalty "if the trading volume exceeds the available volumes of the top 5 bids". Remainder liquidated at the last step.
- **Reward:** sparse, only at t = T−1: compares the agent's IS to TWAP's IS (values −1/0/1 per printed f(x); the printed sign convention looks inverted relative to the text's "higher IS" = better; UNVERIFIED).
- **Baselines:** TWAP, VWAP, Almgren–Chriss (permanent impact set to 0), DDQN (Ning), DQN (Lin 2019), PPO with shaped reward.
- **Result:** PPO-LSTM/Stack "outperform the other models most of time" (Table 2, ΔIS in USD). Per-stock counts not extracted (table garbled in text extraction).
- **Stated assumptions:** "actions ... have only a temporary market impact, and the market is resilient"; "we are training and testing on the historical data and cannot account for the permanent market impact"; "no order arrival delays"; fees ignored. They say a "high-fidelity market simulation environment or data collected by implementing the DRL algorithm in the real market" would be needed to relax this.
- Excluded Nevmyvaka and Hendricks from comparison because "market microstructure have already changed dramatically".

## 2.4 hendricks2014 — Hendricks & Wilcox, "A reinforcement learning extension to the Almgren–Chriss framework for optimal trade execution", IEEE CIFEr 2014, pp. 457–464, DOI 10.1109/CIFEr.2014.6924109. **Peer-reviewed.** [FT arXiv 1403.2229v1; the arXiv title says "model", the published title "framework"]
- **Data:** Thomson Reuters Tick History, 166 JSE (South Africa) stocks, 2012, 5 levels of depth, "aggregated into 5-minute intervals showing average level prices and volumes". Results shown for SBK, AGL, SAB.
- **State:** time, inventory, spread percentile, volume percentile (tabular).
- **Action:** multiplier β ∈ [0, 2] (step 0.25) on the AC child-order volume; child orders executed as market orders walking the 5-level book.
- **Reward:** implementation shortfall.
- **Baseline:** Almgren–Chriss trajectory.
- **Result:** "improve post-trade implementation shortfall by up to 10.3% on average compared to the base model". Table I contains negative cells (down to about −26%) for some hours/parameters.
- **Stated assumption:** "our trading activity does not affect the market attributes ... we assume the limit order book is resilient"; "The validity of this assumption however will be tested in future research".

## 2.5 karpe2020 — Karpe, Fang, Ma & Wang, ICAIF 2020. **Peer-reviewed.** [FT arXiv 2006.05574v2] (simulator facts: claims_simulators2 §B5)
- **Environment:** ABIDES with ExchangeAgent, MarketReplayAgent ("accurately replays all market and limit orders recorded in the historical LOB data"), six MomentumAgents, a TWAP agent and the DDQL agent. LOBSTER-format NASDAQ data, CSCO, IBM, INTC, MSFT, YHOO, 13 Jan – 6 Feb 2003; 9 days train, next 9 days test.
- **State:** time remaining, quantity remaining, spread, volume imbalance, 1-period and t-period log returns. ΔT = 30 s, 660 periods.
- **Action:** quantity a·N_TWAP, a ∈ {0.1, 0.5, …, 2.5}, × placement (market order, or limit orders split over the top 1/2/3 levels).
- **Reward:** 1 − |P_fill − P_arrival|/P_arrival · λ N_t/N.
- **Result:** "our RL agent converges to the TWAP strategy after 9 consecutive days of training regardless of the stock chosen." Authors: "Our experience does not currently allow us to clearly distinguish the difference between our agents and the benchmark."

## 2.6 zhang2023generalizable — Zhang, Duan, Chen, Chen, Li & Zhao, "Towards generalizable reinforcement learning for trade execution", IJCAI 2023, pp. 4975–4983, DOI 10.24963/ijcai.2023/553. **Peer-reviewed.** [FT arXiv 2307.11685v1]
- **Core claim:** "many existing RL methods exhibit considerable overfitting which prevents them from real deployment". They frame execution as "offline RL with dynamic context (ORDC)", the context being "market variables that cannot be influenced by the trading policy"; overfitting "is caused by large context space and limited context samples in the offline setting".
- **Simulator:** LOB snapshots of the 100 most liquid China A-shares; train Apr–Jun 2022, validation Jul, test Aug. Sell 0.5% of previous-day volume in a random 30-min window, decisions each minute. Market orders execute on the snapshot at τ + Δτ (Δτ = 3 s) with temporary impact. A limit order fills fully if a trade prints through its price; at equal price "may be partially filled", with "additional trading limits" because "the LO may be at the end of the queue". Permanent impact assumed linear and ignored.
- **Reward:** r = n_t p̄_t + β (v_t − v_t,TWAP)².
- **Baselines:** TWAP, momentum, re-implementations of Nevmyvaka, Ning, Lin & Beling (2020, 2021), Dabérius et al., Fang et al. (OPD), tuned DQN/PPO.
- **Result (Table 2, cost vs TWAP in bp, lower is better):** PPO+CATE test −4.91 (0.30), PPO+CASH −4.58 (0.21); re-implemented prior methods have positive test cost (OUR READING of the text-extracted table, rows assigned in printed order: Nevmyvaka 9.12, Ning 9.41, Lin & Beling 2020 12.71, Fang/OPD 11.87) and train–test gaps up to 16.78 (OPD). Mean (std) over the last 100 of 1000 evaluations, 5 seeds. Values for the TWAP row could not be recovered from the extraction.

## 2.7 Execution environments with RL examples (no performance claim vs baselines)
- **frey2023 (JAX-LOB):** recurrent PPO execution env; state includes time, initial mid, drift, task size, executed quantity, L1 imbalance; continuous action = sizes at far touch / mid / near touch / passive price; reward Σ Q_j(P_j − P_VWAP) + λ Σ Q_j(P_VWAP − P_init); one month of LOBSTER AMZN (April 2021). The paper reports only training throughput; its conclusion: "we provide an example use case of training an RL agent for the execution task on a single day of message data. We plan to extend this work by following a rigorous RL training pipeline ... including out-of-sample testing". [FT §5.2, §6, §7]
- **amrouni2021abidesgym (ABIDES-Gym):** ExecutionEnv: buy 20,000 shares in 4 h, child orders of 50, actions MARKET / LIMIT at near touch / DO NOTHING, reward from entry-price slippage plus penalty 100 per unexecuted share; DQN via RLlib. Reports training curves only, with "positive spikes explained by the rare unrealistic behaviors of the synthetic market". [FT §4.2, §5.2]

## 2.8 fang2021opd — Fang et al., "Universal trading for order execution with oracle policy distillation", AAAI 2021, 35(1):107–115, DOI 10.1609/aaai.v35i1.16083. **Peer-reviewed.** [FT arXiv 2103.10860v1]
- Not order-book level: "minute-level price-volume market information" of China A-shares 2017–2019 (Zhang et al. 2023 call it "bar-level simulation").
- Teacher–student: a teacher with "perfect information" (future data) is trained and the student imitates it. Baselines TWAP, VWAP, AC, DDQN, PPO. Listed here because it is imitation of an oracle policy, not of historical execution logs.

---

# 3. RL for market making

## 3.1 spooner2018 — Spooner, Fearnley, Savani & Koukorinis, AAMAS 2018, pp. 434–442. **Peer-reviewed.** [FT arXiv 1804.04216] (simulator: claims_simulators2 §A'1)
- **Environment:** MBP replay, 10 European equities, Jan–Aug 2010, top 5 levels; estimated queue (back of queue, uniform cancellations); no impact; chronological train/test with separate validation.
- **State:** agent-state (inventory, active quoting distances) and market-state (spread, mid move, book/queue imbalance, signed volume, volatility, RSI); linear combination of tile codings (LCTC).
- **Action:** 10 actions: 9 bid/ask offset pairs plus a market order to clear inventory.
- **Reward:** PnL Ψ = ψ_a + ψ_b + Inv·Δm; symmetric dampening Ψ − η Inv Δm; asymmetric Ψ − max(0, η Inv Δm). Consolidated agent: asymmetric dampening, LCTC, SARSA.
- **Baselines:** Abernethy & Kale MMMW (online learning); fixed symmetric offsets θ = 1…5; random.
- **Result (Table 6, out-of-sample ND-PnL ×10⁴, mean ± sd):** e.g. HSBA.L 15.43 ± 13.01 (agent) vs 2.80 ± 10.30 (fixed θ=5) vs 1.66 ± 22.48 (MMMW). OUR COUNT from Table 6: the agent's mean ND-PnL exceeds fixed θ=5 on 7 of 10 stocks (lower on GASI.MI, ING.AS, NOK1V.HE). Agent MAP 1–229 units vs 205–3021 (fixed) and 4684–8865 (MMMW).
- **Stated:** MMMW did worse than in its original paper, "attributed to the use of a less realistic market simulation"; future work includes "market frictions such as rebates, fees and latency" and "better order book reconstruction and estimation of order queues".

## 3.2 gueant2019bonds — Guéant & Manziuk, "Deep reinforcement learning for market making in corporate bonds: beating the curse of dimensionality", Applied Mathematical Finance 26(5):387–452, 2019, DOI 10.1080/1350486X.2020.1714455. **Peer-reviewed.** [FT arXiv 1910.13205v1]
- **Not an LOB:** OTC corporate-bond dealer market with RFQs; Avellaneda–Stoikov-type model; "model-based actor-critic-like algorithm"; scales to 20 bonds; validated against a finite-difference PDE solution in low dimension.
- Relevant statement: model-free optimisation without parameter estimation "is true only when one has gigantic data sets ... Perhaps in the case of limit order book data on stock markets"; "There is of course no such simulator of real financial data, and one has instead to build a simulator using a model".

## 3.3 ganesh2019dealer — Ganesh, Vadori, Xu, Zheng, Reddy & Veloso (JPMorgan), "Reinforcement learning for market making in a multi-agent dealer market", arXiv 1911.05892, 2019. **Preprint** (no venue in arXiv metadata). [FT]
- **Not an LOB:** dealer market; investors trade with dealers; "trade arrivals ... from a statistical model calibrated to real market data" (details in an appendix not read).
- **Actions:** pricing parameters for buy/sell spreads in [−1, 1] and fraction of inventory to hedge; PPO; reward Total PnL (spread PnL + inventory PnL − hedge cost), plus risk-averse variants.
- **Baselines:** random, persistent and an adaptive dealer. "The RL agent was able to outperform the random and persistent agents in all experiments"; vs adaptive agent it wins when the adaptive agent is risk-averse (γ = 2) but "the adaptive agent is able to close the gap/outperform at lower risk aversion". 5 seeds.
- A naive variance penalty "results in the RL agent choosing not to trade at all by setting a high spread".

## 3.4 sadighian2019, sadighian2020 — Sadighian, crypto market making. **Preprints (arXiv only).** [FT both]
- **2019 (1911.08647):** Coinbase BTC/ETH/LTC; L3 replayed into 1-s snapshots (claims_simulators2 §A'2). A2C and PPO; 17 actions (symmetric/asymmetric quotes, flatten); rewards "positional PnL" and "trade completion" (clipped goal-based). Train 8 days (27 Sep – 4 Oct 2019), test 2 days (1–2 Nov 2019). Market-order fee 0.20% per side, limit fees ignored. **No baseline strategy**; agents compared with each other. "The agents achieved positive returns ... on the testing data sets."
- **2020 (2004.06985):** BitMEX XBTUSD L2 snapshots recorded at 1 s; price-based vs time-based event steps; 7 reward functions × 6 feature sets × 2 algorithms. Fill rule: notional ahead at placement "only reduced when there are buy (sell) transactions at or above (below)" the price; modification resets priority. Maker rebate 0.025%, taker fee 0.075%; fixed 0.01% slippage on flatten. Train 8 days, test 30 days (Jan 4 – Feb 3 2020). **Baseline:** buy-and-hold, 16.25%; best agent (A2C, trade-completion reward, feature set 3) 17.61%, i.e. the best of many configurations. All experiments lost 5–10% on 19 Jan 2020 (a >5% drop in <200 s).
- 2020 critiques prior work: tick-based approaches "make assumptions about latency and executions, which may impact the capability of a model to translate simulated results into real-world performance".

## 3.5 gasperov2021signals — Gašperov & Kostanjčar, "Market making with signals through deep reinforcement learning", IEEE Access 9:61611–61622, 2021, DOI 10.1109/ACCESS.2021.3074782. **Peer-reviewed (OA).** [FT]
- **Data:** Bitstamp BTC/USD, Sept 2020, tick trades + top-of-book quotes from WebSocket feeds; 64/16/20 chronological split. One MM period = 19 top-of-book changes (~16.22 s).
- **Fill model: no queue.** "a first-passage time (FPT) ... execution model is assumed, according to which a bid (ask) limit order is executed as soon as a sell (buy) side trade takes place at a price that is lower (higher) or equal". No fees or rebates. Unit order size.
- **State:** inventory + outputs of supervised "signal generating units" (price-range and trend predictions). **Action:** continuous tick offsets from best bid/ask. **Reward:** spread-capture terms minus λ|inventory| ("symmetrically dampened PnL ... with maximum dampening" plus absolute inventory penalty). Trained by neuroevolution with an adversary that displaces quotes.
- **Baselines:** fixed offset with inventory constraints (FOIC), linear-in-inventory (LIIC), GLFT. The GLFT risk aversion γ was selected as "the value of γ that results in the maximum PnLMAP(T) value on the testing dataset" (benchmark tuned on test data).
- **Result:** "more than 30% higher terminal wealth than the FOIC(0,0,2) while being exposed to less than 60% of its inventory risk" (single 20% test segment).

## 3.6 niu2024imm — Niu, Li, Zheng, Lin, An, Li & Guo, "IMM: an imitative reinforcement learning approach with predictive representation learning for automatic market making", IJCAI 2024, pp. 5999–6007, DOI 10.24963/ijcai.2024/663. **Peer-reviewed.** [FT arXiv 2308.08918v1]
- **Data:** Shanghai Futures Exchange FU, RB, CU, AG; "5-depth LOB and aggregated trades ... 500-milliseconds"; train Jul 2021 – Mar 2022 (20% validation), test Apr–Jul 2022.
- **Environment:** "Match the orders in market-replay simulator according to the price-time priority" — on 5-level 500-ms snapshots, so own-queue position must be estimated; the estimation rule is in supplementary material, not read (UNVERIFIED). State includes a queue-position feature (volume ahead / level volume).
- **Imitation:** a rule-based "suboptimal signal-based" expert (LTIIC) supplies an expert dataset; TD3-style RL plus imitation loss. Not imitation of historical human/market-maker data.
- **Baselines:** FOIC, LIIC, LTIIC, and two RL methods. "IMM significantly outperforms the benchmarks"; hyperparameters chosen by max PnL/MAP on validation.

## 3.7 jerome2023mbtgym — mbt_gym (facts in claims_simulators §4). [FT arXiv 2209.07823]
- PPO (SB3) on the Cartea–Jaimungal MM model whose optimal policy is known; Fig. 3 compares learnt vs true bid/ask depths. With n = 10 parallel trajectories "learning is unstable". Model-based, no historical data, no queue.
- Its related-work list of HFT papers that "train agents using a market replay simulator": Nevmyvaka (Nasdaq), Jerome et al. 2022 (Nasdaq), Spooner (LSE), Zhong et al. 2020 (CME futures), Xu et al. 2022 (XSHE), Patel 2018, Sadighian 2019, Gašperov & Kostanjčar 2021 (crypto). The Zhong, Xu, Patel and Jerome 2022 papers were not read (UNVERIFIED beyond this list).

## 3.8 Other MM entries already in notes (summary only)
- **kumar2020drl / kumar2021hawkes / kumar2023marl:** agent-based / deep-Hawkes generative simulators; exact queue inside the synthetic book (2021); 2023 abstract only. (claims_simulators2 §B8)
- **guo2023mm:** SZSE event data, crossing-only fills, no queue, impact ignored. (§B8)
- **zhao2021ber:** CME ES and 10-y note futures, 3 years of LOB data; deep policy-based RL; [ABS] only; simulator/queue UNVERIFIED. Hambly et al. [SEC]: BER "allows the algorithm to avoid large losses due to adverse selection".
- **gasperov2022:** trained on a Hawkes-based LOB simulator (model-based). (§B8)
- **lalor2025nhp:** neural-Hawkes event simulator; SAC; fills only on market-order events (adverse always, non-adverse with p = 0.2); terminal rewards "mostly negative" on both simulated and real data; authors say a simple strategy "is unlikely to be profitable on its own". (claims_lalor)

---

# 4. Problems specific to RL on order books (sourced)

**4.1 Historical replay cannot react to the agent (no strategic impact).**
- Spooner: simulated orders "cannot impact the market" [FT §3].
- Nevmyvaka: training assumes actions do not affect market variables; test keeps only mechanical impact; no strategic responses modelled [FT §3].
- Lin & Beling: "training and testing on the historical data and cannot account for the permanent market impact" [FT §2.2].
- Frey et al.: "only direct market impact is accounted for. Such historical messages have no strategic behavior that may react to an RL agent's actions" [FT §3].
- Balch et al.: replay "does not substantially adapt to or respond to the presence of" the strategy (claims_simulators2 §B3).
- Vyetrenko et al. 2020: "market response to them will not be reflected in historical data. Therefore, simple market replay of historical data is not sufficient for back testing or strategy construction" [FT §1.1].
- Coletta et al. 2023: with market replay "one implicitly makes the assumption that the financial market does not react to the presence of the trading agent ... Needless to say, this is an unrealistic assumption" [FT §1].
- Ragel & Challet: naive insertion of agent events is only a "good scheme for systems which are weakly" reactive; "up to now, there is no good way to account remotely realistically for event-by-event impact of the RL agent with historical data" [FT §2].

**4.2 Simulator fidelity / sim-to-real gap, and RL exploiting simulator artefacts.**
- Vyetrenko et al. 2020: simulations "lack fidelity"; "there are still significant discrepancies between simulated markets and real ones" [FT abstract].
- Coletta et al. 2023 (LOBGAN, a conditional GAN simulator): a hand-crafted market maker "can make consistently large profits, albeit with an extremely unsophisticated strategy"; an RL MM agent trained in it learned a policy that exploits the generator, "rather than learning a real profitable policy"; RL "would likely try and exploit the features of the model to make profits, whilst being potentially unprofitable in real markets" [FT §1 and the RL subsection "Learning liquidity provision with reinforcement learning"].
- Amrouni et al.: positive reward spikes "explained by the rare unrealistic behaviors of the synthetic market" [FT §5.2].
- Gašperov et al. 2021 survey: agent-based/model simulators raise "whether simulator-based approaches can replicate stylized empirical facts" and "serious calibration issues" [FT §4.4].
- Sun et al.: "lack of high-fidelity micro-level market simulator ... This unignorable gap between simulation and real market limits the usage of RL in market making" [FT].
- Lalor & Swishchuk: their neural-Hawkes simulator has roughly double the real share of aggressive events (our computation, claims_lalor).

**4.3 Fill and queue modelling in replay.** Most LOB RL environments estimate or ignore queue position: back-of-queue with proportional cancels (Spooner), one tick ahead of the queue (Sadighian 2019), notional ahead reduced only by trades (Sadighian 2020), first-passage/touch fills (Gašperov & Kostanjčar 2021), crossing-only (Guo 2023), pro-rata share of the level with "no queue priority" (Ragel & Challet, despite order-by-order data), probabilistic fills p = 0.2 (Lalor & Swishchuk), snapshot fills with ad-hoc limits (Zhang 2023). Market-order-only formulations avoid the issue (Ning; Hendricks & Wilcox; Lin & Beling). See §5.

**4.4 Reward design.**
- Spooner: the "natural" PnL reward leads to "instability during learning and unsatisfactory out-of-sample performance"; the inventory term "is also the main source of instability"; dampening (η ≥ 0.1) improves consistency [FT §4.2, §5.3].
- Gašperov et al. survey: all 22 MM papers use PnL-based rewards; 11/22 add inventory penalties [FT §4.7].
- Ganesh et al.: a variance penalty "results in the RL agent choosing not to trade at all by setting a high spread" [FT].
- Lin & Beling: shaped rewards are "time-consuming and ha[ve] a risk of overfitting"; they use a sparse reward vs TWAP; "DQN does not work well with the sparse rewards" [FT §2.2, §3.4].
- Zhang et al. 2023: "the design of the reward function has a significant impact on the performance" [FT §5].
- Sadighian 2020: across 7 rewards "no clear trends emerged for the best observation space combination, or reward function (other than what does not work)" [FT §7].

**4.5 Non-stationarity and limited data.**
- Hambly et al.: non-stationary series; older data "may not be helpful" [FT §5].
- Gašperov et al. survey: "Virtually all current approaches assume stationarity" [FT §5].
- Sun et al.: "severe distribution shift" → poor generalization [FT §6].
- Zhang et al. 2023: limited context sequences + large context space → overfitting, shown theoretically and empirically [FT abstract, §3].
- Sadighian 2020: one volatile day caused 5–10% losses in all experiments; train/test split "selected based on data availability and not empirically" [FT §7].

**4.6 Overfitting to replayed paths / backtest overfitting.**
- Gašperov et al. survey: single-path market replay "can easily result in overfitting, i.e., the inability of the RL agent to generalize out-of-time" [FT §4.4].
- Zhang et al. 2023: "many existing RL methods exhibit considerable overfitting which prevents them from real deployment"; re-implemented prior methods show train–test gaps up to 16.78 bp (our reading of Table 2).
- Lin & Beling and Ning: hyperparameters "cannot be tuned with cross-validation" (Ning) / tuned on one stock (Lin & Beling).
- Selection of the best of many configurations is reported as the headline result in Sadighian 2020 (best of 7 rewards × 6 feature sets × 2 algorithms, in time- and price-based environments, vs buy-and-hold). Generic backtest-overfitting references: bailey2014pbo, bailey2014 (not re-read here).

**4.7 Weak or inconsistent baselines.**
- Common baselines: TWAP, VWAP, AC, S&L for execution (Hambly §4.2); fixed offsets and AS/GLFT for MM (Gašperov survey §4.10).
- Karpe et al.: the agent converged to TWAP and could not be distinguished from it.
- Sadighian 2019: no baseline; Sadighian 2020: buy-and-hold only.
- Gašperov & Kostanjčar 2021: GLFT tuned on the test set (strengthens the benchmark, but mixes test data into benchmark selection).
- Ganesh et al.: the adaptive dealer matches or beats RL at low risk aversion.
- Gašperov survey: "all 22 references reported positive results", and comparison across papers "remains a daunting task".

**4.8 Latency mostly ignored.** Nevmyvaka ("do not account for possible order arrival delays"), Lin & Beling ("no order arrival delays"), Spooner (future work; released code has no effective latency, claims_simulators2 §A'1), JAX-LOB (default 0 ns), mbt_gym (not implemented). Exceptions: Zhang 2023 (3 s market-order delay), Kumar 2020/2023 (latency modelled, details not given), Ragel & Challet (discuss inbound/processing/outbound latency).

**4.9 Online/live learning is impractical; offline RL is the realistic setting** (Hambly §5; Sun §6). See §6.

---

# 5. Queue-exact historical replay and live testing

**Papers read that place simulated orders in the recorded order-by-order FIFO queue:**
- **nevmyvaka2006:** INET order flow with artificial orders; the simulator "executes orders and maintains priorities in order books". This is the only RL paper read whose text describes priority maintained against real historical orders. The paper gives no further detail on cancellations, hidden orders or initial-book reconstruction.
- **karpe2020:** ABIDES MarketReplayAgent replays all LOBSTER orders into a FIFO exchange agent; the opening book is recreated with synthetic orders (claims_simulators2 §B3/§B5), so the agent's position relative to pre-existing orders is approximate. The paper does not discuss queues.
- **frey2023 (JAX-LOB):** price-time priority on LOBSTER messages; initial L2 levels are synthetic single orders, and the execution env cancels and resubmits agent orders each step (claims_simulators §3). No out-of-sample result reported.
- **ragel2025timetravel:** order-by-order BEDOFIH data (Euronext Paris, TotalEnergies, 4 days, Jan 2016) but explicitly pro-rata fills, "no queue priority".
- **niu2024imm:** claims "price-time priority" matching on 5-level 500-ms snapshots; queue estimation rule not read.

**Live/paper trading:** none of the RL papers read in full (Nevmyvaka, Ning, Lin & Beling, Hendricks & Wilcox, Karpe, Zhang 2023, Fang 2021, Spooner, Sadighian 2019/2020, Ganesh, Guéant & Manziuk, Gašperov & Kostanjčar 2021, Niu 2024, Ragel & Challet, Lalor & Swishchuk, Guo 2023, Gašperov & Kostanjčar 2022, JAX-LOB, ABIDES-Gym, mbt_gym) reports a live or paper-trading test. OUR COUNT: 0 of 20. Sadighian's repo README says "Research only: there is no capability for live-trading" (claims_simulators2 §A'2). Ning et al. state the network could "perform online learning in real-time" but report no such test. Zhao & Linetsky 2021 not checked (closed access).

---

# 6. Offline RL / imitation from historical data

- **zhang2023generalizable:** frames execution on historical LOB data as "offline RL with dynamic context"; notes it "is also different from the canonical offline RL setting" and "offline RL algorithms do not apply to our setting" because the agent is evaluated on unseen contexts. This is learning from a data-driven simulator, not from logged execution decisions.
- **ragel2025timetravel:** "consistent data time travel for offline RL": after an agent action, jump to another historical time whose state and subsequent effective events match the action's influence. Tabular ε-greedy Q-learning MM, 5 actions; TotalEnergies, 4 days, 48 agents per calibration day. Result: agents trained and tested with time travel have larger average gain than with naive sequential data; the authors report "a better signal-to-noise ratio and a more consistent loss of gains between the test and the train periods" (tested on other days, time-travel agents gain "significantly smaller" amounts than on the training day; sequential-data agents "do not learn much specific to each day"); "the difficulty of this task for RL may have been overestimated". No baseline strategy. **Preprint** (arXiv 2408.02322v2; Authorea and SSRN copies; no peer-reviewed version found).
- **niu2024imm:** imitation of a rule-based expert's actions (expert dataset generated by the rule), combined with RL. [FT]
- **fang2021opd:** distillation from an oracle teacher with future information, minute bars (not LOB). [FT]
- **roavicens2019irl:** Roa-Vicens et al., "Towards inverse reinforcement learning for limit order book dynamics", ICML 2019 workshop on AI in Finance (arXiv 1906.04813). **Workshop paper.** [FT] IRL (MaxEnt, GP-IRL, Bayesian-NN IRL) recovers an expert's reward in a synthetic one-level LOB with stochastic agents; "only the GP-based and our proposed BNN methods are able to discover the non-linear reward case". Demonstrations are simulated, not historical.
- **No verified source found that learns a policy offline (offline RL or behaviour cloning) from logged historical execution or market-making decisions of real traders on LOB data.** Ragel & Challet note public order data are anonymous: "Academics only rarely have access to trader-resolved data". UNVERIFIED that none exists (web search unavailable).

---

# 7. Summary table

Env type: R = historical replay, R-MBP = replay of aggregated levels/snapshots, A = agent-based, M = model-based/parametric, D = dealer/OTC model. Queue: E = exact FIFO vs historical orders, Ea = exact in engine but approximate start, Es = estimated, N = none/touch/crossing, P = pro-rata.

| Key | Task | Env | Queue | Data | Algo | Baseline | Out-of-sample | Headline (conditions) | Live |
|---|---|---|---|---|---|---|---|---|---|
| nevmyvaka2006 | exec | R (INET) | E (as stated) | NASDAQ 3 stocks, 18 mo | Q-learning/DP | S&L, market order | 6-mo test | ≥50% cost cut vs S&L | no |
| ning2021ddqn | exec | mid-price only | n/a (MO at mid) | 9 US stocks 2017–18 | DDQN | TWAP | split not stated | beats TWAP 7/9 stocks | no |
| lin2020ppo | exec | R-MBP (TAQ top-5, 5 s) | n/a (MO) | 14 US stocks 2018 | PPO | TWAP, VWAP, AC, DQN | Oct–Dec test | best "most of time" | no |
| hendricks2014 | exec | R-MBP (5-min avg) | n/a (MO) | JSE 2012 | Q-learning | AC | yes | IS up to 10.3% better | no |
| karpe2020 | exec | R + A (ABIDES) | Ea | NASDAQ 2003, 5 stocks | DDQL | TWAP | 9 days | converges to TWAP | no |
| zhang2023generalizable | exec | R-MBP snapshots | Es (conservative) | 100 A-shares 2022 | PPO/DQN + context | TWAP, prior RL | Aug 2022 | −4.9 bp vs TWAP | no |
| fang2021opd | exec | minute bars | n/a | A-shares 2017–19 | PPO + distillation | TWAP, VWAP, AC, RL | yes | beats baselines | no |
| frey2023 | exec | R (LOBSTER) | Ea | AMZN Apr 2021 | RNN-PPO | none | no | speed only | no |
| amrouni2021abidesgym | exec | A (synthetic) | E (in sim) | synthetic | DQN | none | n/a | training curves | no |
| spooner2018 | MM | R-MBP | Es | 10 EU equities 2010 | SARSA + LCTC | fixed, MMMW | yes | beats fixed θ=5 on 7/10 (our count) | no |
| sadighian2019 | MM | R (1-s snapshots of L3) | Es | Coinbase 3 pairs | A2C, PPO | none | 2 days | positive returns | no |
| sadighian2020 | MM | R-MBP (1-s L2) | Es (trades only) | BitMEX XBTUSD | A2C, PPO | buy-and-hold | 30 days | best 17.61% vs 16.25% | no |
| gasperov2021signals | MM | R top-of-book | N (touch) | Bitstamp BTC Sep 2020 | neuroevolution + adversary | FOIC, LIIC, GLFT | 20% | +30% wealth vs FOIC | no |
| niu2024imm | MM | R-MBP (5-lvl, 500 ms) | Es (UNVERIFIED rule) | SHFE 4 futures | TD3 + imitation | FOIC, LIIC, expert, RL | 60 days | beats benchmarks | no |
| guo2023mm | MM | R (event) | N (crossing) | SZSE | DRL | (claims_simulators2) | — | — | no |
| ragel2025timetravel | MM | R + time travel | P | Euronext Paris, 4 days | Q-learning | none (seq. vs jump) | cross-day | larger gain with time travel | no |
| lalor2025nhp | MM | M (neural Hawkes) + real | N (p = 0.2) | LOBSTER 1 day | SAC | none | test events | mostly negative | no |
| gasperov2022 | MM | M (Hawkes) | — | — | DRL | (claims_simulators2) | — | — | no |
| ganesh2019dealer | MM | D (multi-agent) | n/a | calibrated synthetic | PPO | random, persistent, adaptive | n/a | beats simple; mixed vs adaptive | no |
| gueant2019bonds | MM | D (RFQ model) | n/a | model | actor-critic | PDE solution | n/a | scales to 20 bonds | no |
| jerome2023mbtgym | MM | M | none | model | PPO | known optimum | n/a | approaches optimum | no |
| kumar2021hawkes | MM | A (deep Hawkes) | E (synthetic) | ITCH-fitted | DRL | — | — | (claims_simulators2) | no |

---

# 8. Short summary

- Two surveys read in full support the main problems: Gašperov et al. 2021 (22 MM papers; 41% single-path replay with overfitting risk; 59% simulators, only 4/13 calibrated to data; all report positive results; stationarity assumed) and Hambly et al. 2023; Sun et al. (arXiv v1 of the TIST 2023 paper) adds the simulator/market-impact gap and the offline-RL framing. No dedicated survey of RL for optimal execution was found.
- Execution RL on LOB data mostly uses market-order-only formulations on aggregated levels (Ning, Lin & Beling, Hendricks & Wilcox) and beats TWAP/AC under no-permanent-impact, no-latency assumptions. Karpe et al. found convergence to TWAP. Zhang et al. 2023 show prior methods overfit when re-implemented on one simulator.
- MM RL on LOBs uses estimated or absent queue models. Of 20 RL papers read, only Nevmyvaka et al. (2006) describe inserting agent orders into recorded order flow with priorities maintained, and none reports a live test.
- Coletta et al. 2023 give direct evidence that an RL agent learns to exploit a learned (CGAN) simulator rather than a real strategy.
- Verified offline-RL work for LOBs is limited to Zhang et al. 2023 (offline RL with dynamic context) and the preprint by Ragel & Challet (data time travel). No verified source learns from logged decisions of real traders.
- Discrepancies found: Hambly et al. and Sun et al. each misdescribe one aspect of Spooner et al. (reward dampening; market impact). Cite Spooner directly.
