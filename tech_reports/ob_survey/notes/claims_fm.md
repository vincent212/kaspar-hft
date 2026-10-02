# claims_fm.md — foundation models, TSFMs on finance, cross-asset lead–lag / cross-impact

Verified 2026-10-02. Sources: arXiv PDFs (text extracted with `pdftotext`, copies in the session
scratchpad), arXiv abstract pages, PMLR v235 index, AAAI OJS page, Crossref, OpenAlex.
"VERIFIED" = read in the paper text. "ABSTRACT-ONLY" = only the abstract was read.
"UNVERIFIED" = not confirmed. BibTeX in `bib/refs_fm.bib`.

---

## A. Time-series and financial foundation models

### chronos2024 — Ansari et al., "Chronos: Learning the Language of Time Series"
- Venue: TMLR (PDF header: "Published in Transactions on Machine Learning Research (10/2024)", OpenReview gerNCVqqtR). arXiv 2403.07815 (v3, 4 Nov 2024). AWS AI Labs.
- What it is: a framework that turns real-valued series into tokens and trains off-the-shelf LM architectures with cross-entropy loss. Probabilistic forecasts come from sampling several trajectories.
- Tokenisation (§3.1): **mean scaling**, with m = 0 and s = (1/C) Σ_{i=1..C} |x_i| over the context. Then **uniform binning** into B bins over [c_1, c_B] = [−15, +15] (§5.2). Vocabulary |V_ts| = 4096, including PAD and EOS. The limitations section says B = 4094 bins.
- Architecture: T5 encoder–decoder in four sizes: Mini 20M, Small 46M, Base 200M, Large 710M. A decoder-only GPT-2 base (90M) was also trained. Context length 512, prediction length 64.
- Data (§5.1–5.2): 28 training datasets, "about 890K univariate time series with approximately 84B observations (tokens)". Training used 10M TSMixup augmentations plus 1M KernelSynth synthetic series (Gaussian processes), sampled 9:1. 200K steps, AdamW, 8×A100 (40GB).
- Tasks / evaluation: 42 datasets. Benchmark I has 15 in-domain datasets. Benchmark II has 27 zero-shot datasets. Metrics are WQL (probabilistic) and MASE (point).
- Key results (§5.5): on Benchmark II, Chronos models take the "2nd to 4th spots" on aggregate relative WQL, and Chronos-T5 (Large) places 2nd on MASE. The paper says they "significantly outperform" Moirai-1.0-R, Lag-Llama, LLMTime, ForecastPFN and GPT4TS. It also notes that Moirai pretrained on some Benchmark II datasets.
- Stated limitations (§5.7): the prediction range is restricted to [c_1, c_B], so strong trends are "theoretically infeasible" to model. It struggles with exponential trends and underestimates trend when the context is short. Scale is a problem: very small s against spikes pushes values out of range, and very large s against variance costs precision (tokens are spaced 30s/(B−1) apart). The paper covers univariate series only and has no covariates.
- Finance content: the training and evaluation corpora include only generic finance and economics sets (e.g. Exchange Rate, NN5). The paper does not evaluate on stock returns or HF data.

### timesfm2024 — Das, Kong, Sen, Zhou, "A decoder-only foundation model for time-series forecasting"
- Venue: ICML 2024, PMLR 235:10148–10167 (PMLR page). arXiv 2310.10688. Google Research.
- Architecture: a decoder-only transformer over **patches**. Input patch length 32, **output patch length 128**, so output patches are longer than input patches. The main model has 200M params, 20 layers, 16 heads and model dim 1280 (App. Table). Random patch masking during training covers context lengths 1..512. Loss is **MSE** (point forecasting). Normalisation uses the mean and std of the first input patch.
- Pretraining data (Table 1): "O(100B) timepoints". The largest source is Wikipedia pageviews: hourly 239.1B, daily 115.1B, weekly 16.4B and monthly 3.8B points ("roughly 300B time-points" after cleaning, per §5). Other sources: Google Trends (~0.5B points, ~22k queries), 3M synthetic series of length 2048 (6.144B points), M4, Electricity, Traffic, Weather, Favorita and LibCity. The loader samples 80% real and 20% synthetic. Max context is 512 (256 for weekly, 64 for ≥ monthly).
- Tasks / results: zero-shot point forecasting on Monash (TimesFM is "the top model" on GM of scaled MAE), Darts (within significance of the best, ARIMA and llmtime) and ETT horizons 96/192 (TimesFM and PatchTST are best).
- Stated limitations (App. A.1): no probabilistic loss in the main model, no covariates in pretraining, little hyperparameter tuning, and limited interpretability.

### moirai2024 — Woo et al., "Unified Training of Universal Time Series Forecasting Transformers" (Moirai)
- Venue: ICML 2024, PMLR 235:53140–53164. arXiv 2402.02592. Salesforce.
- Architecture: a **masked encoder** transformer. **Multi patch size** input/output projections give larger patches for higher-frequency data. **Any-variate Attention** flattens multivariate series into one sequence and uses binary attention biases for same/different variate (plus RoPE). The output is a **mixture distribution**: Student-t, negative binomial, log-normal, and a low-variance normal.
- Sizes (Table 4): Small has 6 layers, d_model 384 and 14M params. Base has 12 layers, d_model 768 and 91M. Large has 24 layers, d_model 1024 and 311M.
- Pretraining data: LOTSA, "over 27B observations across nine domains" (Table 2). By domain: Energy 59.17%, Transport 17.73%, Climate 15.15%, CloudOps 5.49%, **Econ/Fin 0.10%** (23 datasets, ~24.9M obs), Healthcare 0.01%. By frequency (Table 3): hourly 71.9%, minute-level 25.4%, second-level 0.054%.
- Tasks / results: in-distribution Monash (beats all Monash baselines at every model size) and zero-shot probabilistic and long-sequence forecasting. The abstract claims "competitive or superior performance as a zero-shot forecaster" compared with full-shot models.
- Stated limitations (§5): little or no hyperparameter tuning. The multi-patch-size approach is "somewhat heuristic". Support for high-dimensional series is limited. LOTSA needs more domain and frequency diversity.

### lagllama2023 — Rasul et al., "Lag-Llama: Towards Foundation Models for Probabilistic Time Series Forecasting"
- Venue: arXiv 2310.08278 (v3, 8 Feb 2024). No journal or conference reference appears on the arXiv page, so the peer-reviewed venue is **UNVERIFIED**.
- Architecture: a decoder-only, LLaMA-style transformer. Each token is the value at time t plus **lag features** (lags at quarterly, monthly, weekly, daily, hourly and second-level frequencies), date-time features and summary statistics. The head is a **Student-t distribution** (df, mean, scale). Values use robust standardisation (median and IQR).
- Size: the final model from hyperparameter search has **2,449,299 parameters**. Selected values (App. D Table 5): 8 layers, 9 heads, 16 embedding dims per head, context length C = 32. The effective window is longer because of the lags.
- Pretraining data: 27 datasets, 7,965 univariate series, "around 352 million data windows (tokens)".
- Tasks / results: zero-shot and few-shot (fine-tuned) univariate probabilistic forecasting on unseen datasets, measured by CRPS and average rank. The paper says Lag-Llama is "state-of-the-art" after fine-tuning and has the best average rank. Exchange-rate is one held-out out-of-domain set.
- Stated limitations / future work (§7): the time-series corpus is small; models need scaling up; multivariate extension is open.

### kronos2025 — Shi, Fu, Chen, Zhao, Xu, Zhang, Li, "Kronos: A Foundation Model for the Language of Financial Markets"
- Venue: AAAI 2026, Proc. AAAI 40(30):25366–25373, doi 10.1609/aaai.v40i30.39730 (AAAI OJS and Crossref). arXiv 2508.02739 v1 (2 Aug 2025). Tsinghua. **All content below is from arXiv v1. Whether the AAAI camera-ready numbers differ is UNVERIFIED.**
- Data type: **K-line / candlestick** bars, OHLCVA (open, high, low, close, volume, amount/turnover). These are bar data, not order-book or message data.
- Tokeniser: a Transformer autoencoder with **Binary Spherical Quantization (BSQ)**, k = 20 bits per token. Each token is factorised into a **coarse and a fine subtoken** of k/2 bits each, trained with L = L_coarse + L_fine + λ·L_quant.
- Model: a decoder-only autoregressive transformer that predicts the coarse subtoken and then the fine subtoken. Sizes (Table 1): small has 8 layers, d_model 512 and 24.7M params. Base has 12 layers, d_model 832 and 102.3M. Large has 18 layers, d_model 1664 and 499.2M. Max context is 512 tokens.
- Pretraining data: "over 12 billion K-line records from 45 global exchanges", 7 sampling frequencies, with a cleaning pipeline for price spikes and illiquid stretches.
- Tasks: price-series forecasting (IC/RankIC), return forecasting, realized-volatility forecasting (MAE), synthetic K-line generation (discriminative score and TSTR IC), and an investment simulation on Chinese A-shares (AER, IR). Evaluation frequencies run from 5-min to daily (Table 8), e.g. 5-min with a 480 look-back and 96 horizon.
- Key numbers (abstract and §Main Results): zero-shot price-series forecasting RankIC is **+93% over the leading TSFM** and **+87% over the best non-pre-trained baseline**. Volatility MAE is **9% lower**. Generative fidelity for synthetic K-lines improves by **22%**. The abstract also says general-purpose TSFMs applied to K-line data are "often underperforming non-pre-trained architectures".
- Stated limitations: the context limit is 512 tokens. The paper has no dedicated limitations section. **UNVERIFIED** whether the AAAI version adds one.

### tradefm2026 — Kawawa-Beaudan, Sood, Papasotiriou, Borrajo, Veloso, "TradeFM: A Generative Foundation Model for Trade-flow and Market Microstructure"
- **Exists.** arXiv 2602.23784 v1 (27 Feb 2026), J.P. Morgan AI Research. arXiv comment: "29 pages, 17 figures, 6 tables. Preprint". **No peer-reviewed venue found (UNVERIFIED).**
- Architecture: a 524M-parameter **decoder-only** transformer (Llama family, GQA, RoPE), sized by Chinchilla scaling. Context length 1,024 tokens, embedding dim 1,024 (App.).
- Tokenisation (§6): one composite token per trade event, built from the bin indices of Δt, price depth, volume, side and action (add/cancel) as a mixed-radix integer: 16 × 16 × 16 × 2 × 2 = **vocabulary 16,384**. Price features use quantile bins. Volume and inter-arrival time use equal-width bins on log values. Conditioning inputs are a liquidity tier (3 bins), a price-level-change bin (32) and a market-vs-participant flag. Features are scale-invariant so the model works across assets.
- Data (§5): proprietary US equities tick data, 368 trading days (Feb 2024 – Sep 2025), >9K equities, "over 19 billion tokens across 1.9 million date-asset pairs". Train is 10.7B tokens; test (Jan 2025 onward) is 8.7B. The OOD set is Jan 2025 data for Japan and China.
- Tasks / results: closed-loop generation with a deterministic matching simulator. Rollouts reproduce heavy tails, volatility clustering and the absence of return autocorrelation. Table 2 gives log-return K-S at Δt = 10 s: TradeFM 0.013 vs Compound Hawkes 0.039 vs zero-intelligence 0.376 ("2–3× lower K-S distance than Compound Hawkes"). Table 3 compares order-flow distributions: TradeFM is best on most quantities, but **Compound Hawkes has lower K-S on spreads** (0.218 vs 0.238). Zero-shot transfer to APAC shows "moderate perplexity degradation".
- Stated limitations: model quality and simulator fidelity are entangled in the closed-loop evaluation ("Full disentanglement remains future work"). Spread fidelity is weaker than Hawkes. Downstream utility (agents, stress tests) is "a key priority" but not yet validated.

### Evidence: general TSFMs on financial returns and volatility
1. **rahimikia2025tsfm** — Rahimikia, Ni, Wang, "Re(Visiting) Time Series Foundation Models in Finance", arXiv 2511.18578 (VERIFIED from intro).
   - Data: **daily** excess returns, 34 years, 94 countries, ~2 billion observations. Tests run 2001–2023.
   - Zero-shot results: "off-the-shelf pre-trained TSFMs perform weakly in zero-shot forecasting of daily excess returns, underperforming strong ensemble models such as CatBoost and LightGBM". Chronos (large) with a 512 window gets out-of-sample R² **−1.37%** and directional accuracy just above 51%. TimesFM (500M) gets R² **−2.80%** and directional accuracy just below 50%. Benchmarks for all U.S. stocks, averaged over windows: linear R² −0.47%, CatBoost −0.10%.
   - Fine-tuning gives limited gains. Pre-training from scratch on financial data gives substantial gains, e.g. Chronos (small) at window 5 goes from R² −77.07% to −3.18%. Even so, TSFMs pre-trained from scratch remain behind the benchmarks on goodness-of-fit. Twelve more TSFM architectures give "generally similar results".
   - Limitation for this survey: the data is **daily**, not high-frequency.
2. **brini2026rv** — Brini, arXiv 2607.05291 (ABSTRACT-ONLY). Nine zero-shot TSFMs against eight econometric (HAR-family) realized-volatility models, across 50 assets in equities, FX and futures. Pooled losses favour TSFMs, but the advantage is concentrated in a few outlier assets. Averaging per-asset loss ratios against Log-HAR, only TTM beats the benchmark at every horizon, "by a narrow margin".
3. **alonso2026tsfm** — Noguer i Alonso & Franklin, arXiv 2606.27100 (ABSTRACT-ONLY). Five U.S. equities, linear and log returns. Pretrained TSFMs win 8 of 10 tasks against train-from-scratch nets, but "gains over the random-walk benchmark are small and sparse". Diebold–Mariano rejects equal accuracy only for Chronos on AMZN and Moirai-2.0 on GOOG.
- **High-frequency (intraday / tick) zero-shot evidence: no peer-reviewed or fully read study found — UNVERIFIED.** The only candidate seen is arXiv 2608.08825 (Dewage et al.), which corrects frozen TimesFM on opening-hour returns for 10 tech stocks. Only its abstract was read, and it is not included in the bib.

---

## B. LOBERT — linna2025lobert (arXiv 2511.12563)

- Versions: v1 is 16 Nov 2025 and v2 is 8 Sep 2026. arXiv comment: "Submission for NeurIPS 2025 GenAI in Finance Workshop". **Acceptance is UNVERIFIED.** Table 1–3 and Appendix D numbers are identical in v1 and v2; v2's introduction was rewritten.
- What it is: an encoder-only, BERT-style foundation model for LOB **messages** (Level-3 / ITCH), fine-tuned with task-specific heads.
- **Tokenisation (§2.1): one token per message.** Price is the tick distance from the best opposing quote, quantised to (0, 1, 2, 3, 5, 10). Volume is quantised to (0, 50, 100, 200) and carries a "round volume" Y/N flag; "over 60% of all volume values are exactly 100 units". These are combined with side and type (new, edit, delete, execution, hidden) into **293 distinct message tokens**, including special tokens. Continuous copies of price, volume and Δt are kept through Piecewise Linear-Geometric Scaling (PLGS). Price uses τ_start = 10 ticks, τ_max = 20, τ_clip = 1000. Volume uses 200 / 400 / 1500. Time uses 1 ms / 50 / 250 ms. v1 states this gives "≈20× fewer tokens per sequence"; v2 says "roughly an order of magnitude".
- Snapshots (§2.2): the optional 10-level book snapshot is 40 values. Volume is mapped as 1 − e^{−v/2000}. Prices are tick distances clipped at 20 ticks.
- Architecture (§2.3): BERT encoder with GELU and dropout 0.1. The embedding is additive over message token, continuous price, continuous volume and optional gated snapshot. Position uses learned positional embeddings plus **continuous-time RoPE** on cumulative Δt (App. E). The output has a token classification head (cross-entropy) and three regression heads (MSE) that take token logits and hidden state. "Combined" inference bounds each regressor by its token's bin.
- **Pretraining objective (§2.4): Masked Message Modeling (MMM)**, "analogous to BERT's Masked Language Modeling". Snapshots are also masked at 90% of positions to avoid leaking the masked message.
- **Model size:** "1.1M parameters" for the LOBERT used in the next-message comparison (vs S5 at 1.2M) (§3.1). **Layer count, hidden size, heads and the mid-price model's size are not stated — UNVERIFIED.**
- **Context length:** sequences of **512 messages** (§3).
- **Dataset (§3):** Nasdaq ITCH for AAPL, INTC, MSFT and FB. Train is 80 days (2015-05-11 to 2015-09-01), validation 10 days (2015-09-02 to 09-16), test 10 days (2015-09-17 to 09-30). That is 470M messages in 919k non-overlapping 512-message sequences. Time resolution was **millisecond only**, because of data-access issues. Optimiser AdamW with lr 5e-5 and wd 0.01, cosine warm restarts, 10 epochs, batch 32.
- **Downstream tasks and headline results:**
  - Next-message prediction (Table 1, full-message accuracy): S5 6.1%, LOBERT 26.4%, LOBERT with Book Module 27.8%. Component accuracies for LOBERT+Book: type 61.9%, side 65.4%, price 53.8%, volume 72.2%. Pearson correlations of the predicted vs real marginals are price 0.55, volume 0.37, time 0.52.
  - Output modes (Table 2): Combined has the lowest W1/JSD/TVD. Price W1 is 10.04 vs 15.44 (token only) vs 10.85 (regressor only).
  - Mid-price direction (§3.3, Table 3): 3 classes, the target is the average of the next h mid-prices, H ∈ {10, 50, 100} **messages**, and τ(h) = round(100·log2(h/10))/1000. Metrics are selective macro-F1 and coverage at confidence τ_c, averaged over 4 stocks. The baseline is DeepLOB retrained on the same data.

### Claim verdicts
| Claim | Verdict | Exact text / location |
|---|---|---|
| "F1 0.88 at 10% coverage vs DeepLOB 0.61 at 28% (horizon 100, confidence 0.9)" | **VERIFIED** | Table 3, row τ_c = 0.9, H = 100: LOBERT F1 **0.88**, Cover. **0.10**; DeepLOB F1 **0.61**, Cover. **0.28**. §3.3 text: "H = 100: 0.55 → 0.88 … coverage decreases (1.00 → 0.10)". The F1 is *selective* macro-F1 on the confident subset, averaged over FB/INTC/MSFT/AAPL. The two models are compared at different coverage. At τ_c = 0.3 (full coverage) H = 100 is a tie: 0.55 vs 0.55. |
| "about 53% slower" | **VERIFIED** | §3.3: "LOBERT's inference is currently slower than DeepLOB's by 53%." |
| "47% of DeepLOB throughput" | **VERIFIED** | App. D: "On a single NVIDIA V100 GPU, the model attains a throughput of 281.87 predictions per second, corresponding to 47% of DeepLOB's throughput under the same evaluation conditions with batch size of 1." |

Other Table 3 rows at τ_c = 0.9: H = 10 is LOBERT 0.82 / 0.29 vs DeepLOB 0.48 / 0.50; H = 50 is 0.84 / 0.14 vs 0.60 / 0.40.

### Stated limitations (§4)
- The model's ability to produce realistic long sequences is not established.
- The breadth of tasks it adapts to has not been studied.
- It may struggle to track order size and location when price levels move, and it does not model individual order IDs.
- Transformer compute and inference time are non-trivial.
- The time dimension is only partly covered because the data has ms resolution (§3).

---

## C. Cross-asset / lead–lag / cross-impact

### Futures ↔ cash/ETF lead–lag (S&P 500)
- **laughlin2014** — Laughlin, Aguirre, Grundfest, Financial Review 49(2):283–312 (Crossref). Read from arXiv 1302.5966 preprint; the published version may differ.
  - Measured with "millisecond-resolution tick data" on near-month E-mini S&P 500 (CME) against SPY and other NJ-traded equities.
  - Chicago→New Jersey response latency: "7.25–7.95 ms" in April 2010, then "approximately 6.65 ms" after Spread Networks fiber (Aug 2010). Overall it fell by 3 ms between 27 Apr 2010 and 17 Aug 2012.
  - Microwave networks were estimated at "4.2–5.2 ms", against a speed-of-light minimum of 3.93 ms and an expected ~4.03 ms.
  - "20% of all trading in the highly liquid SPY ETF can be specifically identified to occur in direct response to changes in the traded price of the near-month E-mini S&P 500 futures contract."
- **dobrev2017** — Dobrev & Schaumburg, "High-Frequency Cross-Market Trading: Model Free Measurement and Applications", Federal Reserve Board working paper, version of 15 Oct 2017 (AEA 2018 preliminary program PDF). **Journal publication UNVERIFIED.**
  - Method: co-activity of trade timestamps at ms offsets.
  - CME (Aurora) to BrokerTec (Secaucus) is "roughly 4.7 milliseconds … at the speed of light". The fiber lower bound is "roughly 7 ms".
  - 10-year Treasury, cash vs futures: spikes at "+/-5 milliseconds", meaning "sometimes Treasury futures lead cash Treasuries (+5 millisecond offset) and other times the cash market leads futures (-5 millisecond offset)".
  - S&P 500 (SPY vs E-mini): there is a "pronounced asymmetry of the spike … at +5 milliseconds", consistent with the futures' dominant role in price discovery.
  - Cross-asset (10y cash Treasury vs E-mini): "the E-mini market leads the cash Treasury market (+5 millisecond offset) but not vice versa".
  - Abstract: price discovery in US Treasury, equity and EUR/USD FX "primarily takes place in futures rather than cash markets".
- **garrison2019** — Garrison, Jain, Paddrik, OFR Working Paper 19-04 (2019). SPY and E-mini order flow in 10 ms and 1 s buckets, 553.73M messages over 23.4M 10 ms intervals.
  - Order-flow linkage between the two markets is "positive, but short-lived". Lead/lag correlations at 1 s are negative, and beyond 1 s "the economic significance of correlation coefficients is very small".
  - At 10 ms there is "some feedback from futures to ETF in the volatile periods".
  - A related later paper by the same authors is Crossref-listed as J. Futures Markets 44(9):1508–1542 (2024), "Cross-Asset Tandem Trading and Extraordinary Volatility". Its content was not read.
- **hasbrouck2003** — Hasbrouck, JF 58(6):2375–2400 (ABSTRACT-ONLY, via OpenAlex). "For the S&P 500 and Nasdaq-100 indexes, most of the price discovery occurs in the E-mini market." For the S&P 400 MidCap, price discovery is shared between the regular futures and the ETF. **The numeric information shares are UNVERIFIED.**
- **huth2014** — Huth & Abergel, J. Empirical Finance 26:41–58 (read from arXiv 1111.7103). This is European data, not ES: tick data for 2010-03-01 to 2010-05-31 using the Hayashi–Yoshida cross-correlation.
  - "the future leads the stock, by an average time of 0.6 seconds" (CAC40 future FCE vs TOTF.PA).
  - Using only the leader's past, they get 60% accuracy on the lagger's next mid-quote move. A naive market-order strategy cannot profit because of the spread.

### Cross-impact / order flow across the rates curve
- **brandt2004** — Brandt & Kavajecz, JF 59(6):2623–2654 (Crossref). Numbers are from NBER WP 9529 (Feb 2003); **the published-version numbers are UNVERIFIED.**
  - GovPX inter-dealer data, Jan 1992 – Dec 1999.
  - Order-flow imbalances "account for up to 26 percent of the day-to-day variation of yields on days without major macroeconomic announcements". A 1-sd imbalance moves yields by more than 2.5 bp, and by more than 3.3 bp when liquidity is low. The effect is permanent over two weeks.
  - Cross-maturity structure: net order flow has a common factor explaining ~32% of its cross-maturity variation, and "upward of 80 percent" of the order-flow effect on yields is due to that common factor. "excess buying or selling across the whole curve is much more informative about the level of yields than excess buying or selling of a particular maturity range."
  - Frequency is daily, not intraday.
- **tomas2022** — Tomas, Mastromatteo, Benzaquen, Quantitative Finance 22(6):1017–1036 (read from arXiv 2004.01624 v3).
  - Sets axioms for cross-impact models and tests them on three datasets: NYMEX crude calendar contracts; "Bonds and indices" (10y US T-note futures and E-mini S&P 500, first two maturities each, CME, Jan 2016 – Dec 2017, 160 days, 1-min bins); and 393 stocks.
  - Best fit comes from the r-el, kyle and ml models. Only the **kyle** model satisfies all axioms. In Table 4 (Bonds and indices, out-of-sample R²(I_σ)), kyle scores 0.38, ml 0.40 and direct −0.11.
  - **This is not a full Treasury-curve study.** It covers only 10y note futures plus ES.
- Not found or verified: a peer-reviewed intraday cross-impact study across the full Treasury futures curve (2y/5y/10y/30y) — **UNVERIFIED**. A Fed note, "Order Flow Imbalances and Amplification of Price Movements: Evidence from U.S. Treasury Markets" (FEDS Notes, 2025-11-03), turned up in search but was not read.
- Already in the bib: **contcucuringu2023** (Cont, Cucuringu, Zhang, QF 2023, cross-impact of OFI in equities), so it is not repeated.

### Multivariate Hawkes for lead–lag
- **bacry2013** (already in `bib/refs_classic.bib`; read from arXiv 1101.3422). This is a 2-asset mutually exciting point process with closed-form signature plot and cross-correlation. It reproduces microstructure noise and the **Epps effect**, fitted on Euro-Bund and Euro-Bobl futures. The paper notes that lead–lag can be deduced from the model's second-order properties.
- **dafonseca2017** — Da Fonseca & Zaatour, J. Futures Markets 37(3):260–285 (ABSTRACT-ONLY, via OpenAlex). A multi-asset Hawkes model with a diffusive-limit covariance that links HF and LF parameters, illustrated on Eurex assets, where it "captures the lead–lag relationship". **Specific lags and estimates are UNVERIFIED.**

---

## UNVERIFIED list
- Lag-Llama peer-reviewed venue.
- Kronos: whether the AAAI camera-ready numbers match arXiv v1.
- TradeFM peer-reviewed venue (arXiv preprint only).
- LOBERT workshop acceptance. LOBERT layer count, hidden size and heads, and the mid-price model's parameter count (not stated in the paper).
- High-frequency (intraday/tick) zero-shot evidence for general TSFMs (no fully read study).
- brini2026rv and alonso2026tsfm: abstract-only.
- Hasbrouck 2003 numeric information shares (abstract-only).
- Brandt & Kavajecz published-version numbers (WP numbers quoted).
- Dobrev & Schaumburg journal publication.
- Da Fonseca & Zaatour empirical numbers (abstract-only).
- An intraday cross-impact study across the full Treasury futures curve.
