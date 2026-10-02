# Claims & paper notes — deep learning / generative LOB models

Verified 2026-10-02 against arXiv PDFs (text-extracted), Crossref, PMLR, publisher and
Semantic Scholar pages. "arXiv vN" means the numbers come from that arXiv version; the
journal version was not read unless stated. Nothing below is filled in from memory —
fields that could not be confirmed are marked UNVERIFIED.

BibTeX: new keys in `bib/refs_dl.bib`; existing keys (in `refs.bib`) are reused here.

---

## Part 1 — Paper notes

### Existing keys (in refs.bib; not re-added)

**zhang2019 — DeepLOB** (Zhang, Zohren, Roberts, IEEE TSP 67(11) 2019; read arXiv 1808.03668v6)
- Input: the last 100 LOB snapshots × 40 features (10 levels × bid/ask × price/volume). Model: convolutional
  blocks (1×2 filters combine price and volume per level, then across bid/ask, then across levels), then an
  Inception module (parallel 1×1, 3×1, 5×1 temporal convolutions plus max-pooling), then an LSTM with 64 units
  and a softmax over {down, stationary, up}. Labels use smoothed mid-price returns with threshold α.
- Data: FI-2010 (Setup 1 anchored, Setup 2 7 days train / 3 days test) and one year of LSE data
  (3 Jan – 24 Dec 2017, 08:30–16:00) for LLOY, BARC, TSCO, BT, VOD, with a 3-month test period. Transfer test on HSBC, GLEN, CNA, BP, ITV.
  ">134 million samples"; ~150,000 events/day/stock.
- Results: FI-2010 Setup 2 F1 83.40 (k=10), 72.82 (k=20), 80.35 (k=50); Setup 1 F1 77.66 / 74.96 / 76.58 (k=10/50/100).
  LSE F1 70.15 / 63.49 / 60.65 (k=20/50/100); transfer stocks 68.48 / 62.84 / 60.77. Forward pass 0.253 ms, 60k parameters (Table III).
- Limitations stated: FI-2010 "is far too short, downsampled and taken from a less liquid market". The trading
  simulation is "not … a fully developed, stand-alone trading strategy" and uses "mid-prices without transaction costs".

**tsantekidis2017** (in refs.bib: Tsantekidis et al., "Forecasting stock prices from the LOB using CNNs", IEEE CBI 2017)
— this is a different paper from tsantekidis2017lstm below. Not re-read here. Its FI-2010 numbers as quoted in
DeepLOB Table II ("CNN-I"): F1 55.21 / 59.17 / 59.44 at k=10/20/50.

**wallbridge2020 — TransLOB** (Wallbridge, arXiv 2003.00130v1, 2020)
- Input: [100, 40] FI-2010 window. Five dilated causal 1-D convolution layers (14 features, kernel 2, dilation 1,2,4,8,16),
  then two transformer blocks with masked (causal) multi-head self-attention, layer norm and feed-forward layers, then a
  3-class output. Trained 150 epochs; first 7 days train / last 3 test.
- Results (FI-2010): accuracy / F1 = 87.66 / 88.66 (k=10), 78.78 / 80.65 (k=20), 88.12 / 88.20 (k=50), 91.62 / 91.61 (k=100).
  At k=100 the only baseline is the author's own CNN reproduction (63.06 / 62.97).
- Limitations stated: "Due to the limited nature of the FI-2010 dataset, significant time was spend tuning hyperparameters
  … to negate overfitting"; "our architecture was notably sensitive to the initialization"; tests on larger LOB data are left to "future work".
- Independent replication: LOBCAST (prata2024) measured FI-2010 F1 59.4±2.6 against a claimed 87.3±4.0 (robustness score 69.9, the
  lowest of 15 models), and 57.7 / 50.4 on LOB-2021 / LOB-2022.

**kolm2023 — Deep OFI** (Kolm, Turiel, Westray, Mathematical Finance 33(4):1044–1081, 2023)
- Full text not accessible (Wiley/SSRN returned 403). Only the abstract was verified (EconPapers/RePEc).
- Abstract: "forecasting high-frequency returns at multiple horizons for 115 stocks traded on Nasdaq using order book information at
  the most granular level … we achieve state-of-the-art predictive accuracy by training simpler 'off-the-shelf' artificial neural
  networks on stationary inputs derived from the order book. Specifically, models trained on order flow significantly outperform
  most models trained directly on order books … the effective horizon of stock specific forecasts is approximately two average price changes."
- From Lucchese et al. (arXiv v3) describing Kolm et al.: the data is LOBSTER, 115 Nasdaq tickers, 2 Jan 2019 – 31 Jan 2020. The original deepOF
  is a multi-horizon regression (outputs in R), not a classification. Features are standardized by training mean/std.
- From a secondary source (hasgeek page, not the paper): the models compared are MLP, LSTM, LSTM-MLP, stacked LSTM, CNN-LSTM.
- Horizons, the benchmark set (ARX/linear) and R² values: UNVERIFIED (paper not read).

**prata2024 — LOBCAST** (Prata et al., Artificial Intelligence Review 57(5), 2024; read arXiv 2308.01915v2)
- Re-implements 15 models (MLP, LSTM, CNN1, CTABL, DeepLOB, DAIN, CNNLSTM, CNN2, TransLOB, TLONBoF, BiNCTABL, DeepLOBAtt, DLA,
  ATNBoF, AxialLOB) plus two ensembles. Tests on FI-2010 (robustness) and on LOBSTER "LOB-2021/2022" data (generalizability). Ternary labels,
  horizons K = {1,2,3,5,10}, 5 seeds, F1 is the main metric. The robustness/generalizability score is 100 − (|A| + S), where A is the mean
  difference between the claimed and measured F1.
- Headline: "all models exhibit a significant performance drop when exposed to new data". The LOB-2021/2022 F1 range is 48–61%. BiNCTABL
  is best (FI-2010 F1 82.6; LOB-2021 61.2; "average decrease of approximately 19.6%"). "Five of the best six models incorporate attention".
  Models "diverged (F1-score ⩽ 33%) during the hyperparameters search for about half of the runs." Ensembles do not beat the best single model.
  A trading simulation on LOB-2021 shows profitability "is far from guaranteed".
- Parameter counts (Table 1) range from 1.1·10⁴ (CTABL, BiNCTABL) to 1.3·10⁷ (ATNBoF). The authors observe that the "average number of parameters is
  very low compared to other classical fields" and conjecture that current systems are "inadequate".
- Limitations stated: the grid hyperparameter search is not exhaustive. Code and hyperparameters were missing for several original models.

**sirignano2019** (in refs.bib = Sirignano & Cont, "Universal features of price formation…", QF 19(9) 2019). Not re-read here.
Note: this key is NOT the "Deep learning for limit order books" paper. That paper is added as `sirignano2019dl`.

**ntakaris2018 — FI-2010** (J. Forecasting 37(8) 2018). Not re-read. From DeepLOB and Tsantekidis: 5 Helsinki stocks, 10 trading days
(1–14 June 2010), ~4.5 million messages, 10 levels, normalized and downsampled (blocks of 10 events).

**linna2025lobert, manoharan2026uqlob, nagy2023, lobbench2025, frey2023, gu2024mamba, vaswani2017** — already in refs.bib.
They are outside this task's claim set and were not re-verified here.

### New keys (in bib/refs_dl.bib)

**tsantekidis2017lstm** (EUSIPCO 2017, pp. 2511–2515; read the Zenodo open-access PDF)
- An LSTM with 40 hidden units plus a Leaky-ReLU feed-forward layer, trained on the last 100 LOB depth samples. A "temporally aware"
  normalization of prices/volumes. Targets are mid-price direction at k = 10, 20, 50. FI-2010 (5 stocks, 10 days, 4.5M messages), 7 days train / 3 days test.
  Baselines: linear SVM (trained with SGD) and MLP.
- Results (columns as printed, "Mean Recall / Mean Prec. / Mean F1 / κ"): LSTM 60.77 / 75.92 / 66.33 / 0.500 (k=10);
  59.60 / 70.52 / 62.37 / 0.430 (k=20); 60.03 / 68.50 / 61.43 / 0.411 (k=50). MLP F1 48.27 / 51.12 / 55.95; SVM 35.88 / 43.20 / 49.42.
  (DeepLOB, TransLOB and Zhang & Zohren 2021 quote these numbers with the precision and recall columns swapped.)
- Stated: 32–64 hidden units avoid over-/under-fitting. No limitations section.

**tran2019tabl — TABL** (IEEE TNNLS 30(5) 2019; read arXiv 1712.00975)
- Bilinear layers act on the (features × time) matrix separately along the feature and temporal modes. A TABL layer adds a learned
  temporal attention mask. A(TABL), B(TABL) and C(TABL) have 0, 1 and 2 hidden bilinear layers. Evaluated on FI-2010 (Setup 1 anchored), H = 10, 50, 100.
- Results: C(TABL) accuracy / F1 = 78.01 / 72.84 (H=10), 74.81 / 74.32 (H=50), 74.07 / 73.52 (H=100). The abstract says the 2-hidden-layer
  network "significantly outperformed existing deep-learning baselines while requiring substantially less computational resources".
- LOBCAST: CTABL 1.1·10⁴ parameters; FI-2010 F1 69.6 (claimed 74.3); LOB-2021 59.7.

**passalis2020dain — DAIN** (IEEE TNNLS 31(9) 2020; read arXiv 1902.07892)
- A learnable three-stage input normalization layer (adaptive shift, adaptive scaling, gating) placed in front of an MLP or RNN and trained
  end-to-end. Evaluated on FI-2010 (anchored day-wise splits) with horizons 10 and 20, and on a household-power dataset.
- Results (FI-2010, macro-F1 / κ): MLP+DAIN 68.26 / 0.5145 vs z-score 54.65 / 0.3206 (h=10); RNN+DAIN 65.13 / 0.4660 vs z-score 53.85 / 0.3018 (h=10).
- LOBCAST: DAIN FI-2010 F1 55.6 (claimed 66.8); LOB-2021 55.9.

**sirignano2019dl — spatial neural network** (QF 19(4):549–570; online 2018; read arXiv 1601.01987v7)
- Models the joint distribution of the best bid and best ask at a future time, conditional on the current LOB state. The "spatial neural network"
  exploits the spatial (price-level) structure to use information deep in the book with a low-dimensional parameterization.
- Data: 489 stocks (mostly S&P 500 / NASDAQ-100), 1 Jan 2014 – 31 Aug 2015, trained on a 50-GPU cluster.
- Abstract: it "outperforms … the naive empirical model, logistic regression (with nonlinear features), and a standard neural network architecture.
  Both neural networks strongly outperform the logistic regression model". It is especially better "in the tail of the distribution". Numeric results not extracted.

**briola2020** (arXiv 2007.07319v3)
- Random, naive, multinomial logistic regression, MLP, shallow LSTM, self-attention LSTM and CNN-LSTM are compared on identical features: LOBSTER
  INTC (large-tick). Train 4 Feb – 31 May 2019; test 3–28 Jun 2019. Labels are return quantiles at horizons H ∈ {10, 50, 100} price changes.
- Results (Table 7, test). MLP balanced accuracy 0.56 / 0.59 / 0.61 and MCC 0.34 / 0.36 / 0.38. CNN-LSTM 0.57 / 0.56 / 0.55 and MCC 0.34 / 0.34 / 0.32.
  Logistic regression 0.46 / 0.47 / 0.53 and MCC 0.24 / 0.25 / 0.30.
  Abstract: "simpler multilayer perceptrons achieved performance comparable to or exceeding CNN-LSTM architectures".
- Limitations stated: one large-tick stock only. Future work should address small-tick stocks and "real time" horizons.

**briola2024 — LOBFrame** (QF 25(7):1101–1131, 2025; arXiv 2403.09267, v4 titled "Deep Limit Order Book Forecasting"; numbers from v4)
- DeepLOB only, chosen because of code availability etc. It is trained on 15 NASDAQ stocks (LOBSTER, 2017–2019): 6 small-tick, 3 medium-tick (AAPL, ABBV, PM)
  and 6 large-tick (BAC, CSCO, KO, ORCL, PFE, VZ). Horizons H ∈ {10, 50, 100} LOB updates. Metrics are MCC, F1 and the new transaction-level p_T.
  Code: LOBFrame (open source).
- Results: see D1/D2. p_T at H10 ranges 0.04–0.15 across stocks with the least restrictive threshold. The small-tick group splits into two clusters with average p_T 0.06 and 0.12.
- Limitations stated: backtesting "using historical data-only is not possible" (execution speed, costs, impact); "high forecasting power does not necessarily
  correspond to actionable trading signals".

**lucchese2024** (IJF 40(4):1587–1621, 2024; read arXiv 2211.13777v3)
- deepLOB (raw L1/L2 levels), deepOF (multi-level order flow, Kolm et al.) and deepVOL are compared. deepVOL is a new "volume representation" (volumes on a
  fixed tick grid around the mid, L2 and L3 variants). All three share the same CNN–Inception–LSTM backbone; seq2seq/attention decoders are used for
  multi-horizon. The architecture is held fixed and only the representation varies.
- Data: 10 of the 115 Nasdaq stocks of Kolm et al. (LOBSTER, 2 Jan 2019 – 31 Jan 2020). Ternary labels. h ∈ {10,…,1000} order-book updates.
  Comparison uses Model Confidence Sets (Hansen et al. 2011). Inputs for deepLOB/deepOF use a 5-day rolling-window z-score. deepVOL uses a different (image-like) normalization.
- Results: predictability is found "up to 50 order book events ahead at the 99% confidence level" for most stocks. Conclusions: "persist up to 50-300 order book
  updates ahead". "Models based on the basic order book level representation are considerably outperformed by those with order flow or volume inputs".
  L2 beats L1; L3 helps "only universal models".
- Limitations stated: mid-to-mid returns "are not tradable in practice"; the order-book clock makes horizons stochastic; latency, own-trade impact and
  crowding; "might not be directly tradable through a standalone strategy"; Nasdaq only; hyper-parameter tuning left to future work; 10 stocks only for compute reasons.

**zhangzohren2021** (arXiv 2105.10430v2)
- Multi-horizon forecasting. A DeepLOB encoder feeds a Seq2Seq or Luong-attention decoder (single 64-unit LSTM) that emits a path of
  class predictions (k = 10, 20, 30, 50, 100). Data: FI-2010 and one year of LSE data (5 training stocks; transfer to 20 other stocks). Training is benchmarked on a Graphcore IPU vs RTX 2080 GPU.
- Results: FI-2010 F1 for DeepLOB-Attention is 82.37 (k=10) and 81.49 (k=100) vs DeepLOB 83.40 and 76.76. LSE F1 at k=20 / 50 / 100 is 68.09 / 65.63 / 62.46 (DeepLOB
  68.40 / 64.79 / 61.10). LSE MLP F1 is 46.89 / 47.25 / 43.66 and linear model (LM) F1 is 42.38 / 41.13 / 41.80.
  The IPU "only takes about 15% of the corresponding GPU wall-clock time" to train an encoder-decoder.

**berti2025tlob — TLOB / MLPLOB** (arXiv 2502.15757v3; journal/conference version not confirmed — a search snippet said ICAIF '25, which Crossref and DBLP did not confirm. UNVERIFIED)
- MLPLOB is an MLP-Mixer-style model: feature-mixing and temporal-mixing MLPs with a bilinear-normalization input layer. TLOB alternates temporal self-attention
  (across snapshots) and spatial/feature self-attention (across LOB features) in each block, with an MLPLOB feed-forward. A new labelling decouples the
  smoothing window k from the horizon h ("removing the horizon bias").
- Data: FI-2010, TSLA and INTC (LOBSTER, January 2015; INTC is the small-tick stock per the paper), and BTC perpetual from Binance (2023, Kaggle). Horizons are 10, 20, 50, 100. The metric is F1.
- Results: FI-2010 F1 TLOB 81.55 / 82.68 / 90.03 / 92.81 and MLPLOB 81.64 / 84.88 / 91.39 / 92.62 (BiNCTABL 81.1 / 71.5 / 87.7 / 92.1 — baselines taken from LOBCAST).
  TSLA TLOB 60.50 / 49.74 / 43.48 / 39.84. INTC TLOB 80.15 / 72.75 / 62.07 / 50.14. BTC TLOB 74.7 / 61.74 / 48.54 / 41.49.
  INTC h=50: 2012 day 66.87 vs 2015 60.19 ("−6.68"). Parameters: MLPLOB 6.3·10⁷.
- Limitations stated: "the proposed methodologies are not sufficiently mature for practical deployment in live trading environments". Spread-based thresholds
  "significantly impact model evaluation and potential profitability". Profitability analysis/backtesting is future work.

**lit2025 — LiT** (Frontiers in AI 8:1616485, 2025; read the PMC full text)
- Authors: Yue Xiao, Carmine Ventre, Yuhan Wang, Haochen Li, Yuxi Huan, Buhong Liu. The LOB is a 2-channel (price, volume) H×W image with 20 levels per side (80 features)
  and a 64-snapshot window. Structured patches: height = one side of the book, width = a temporal window. Then a linear projection with positional embeddings,
  transformer layers, LSTM layers and a softmax trend classifier.
- Data: Binance crypto; training on September 2024 (1,077,057 snapshots); evaluation on October–December 2024. The horizons are intervals: 300–500, 300–700, 300–1,000 and 500–1,000 ms.
- Results: F1 / accuracy 58.99 / 59.03, 63.65 / 64.58, 66.40 / 68.34 and 64.32 / 66.37 on the four horizons. Baselines: Ridge, RF, SVM, MLP, LSTM, ViT, DeepLOB, TransLOB.
- Limitations stated: it "relies solely on raw price and volume data", and the order-imbalance features are future work.

**arxiv240902277** = Jung & Lee, "Attention-based reading, highlighting, and forecasting of the limit order book" (QF 25(7):1015–1027, 2025; read arXiv 2409.02277v2)
- Seq2seq transformer forecasting the whole multi-level LOB (prices and volumes, 5 levels). A "compound multivariate embedding" tokenizes by
  level × side × feature × stock. A structure loss penalizes violations of the LOB's ordinal price structure.
- Data: LOBSTER sample day 21 June 2012 for AAPL, GOOG, INTC, MSFT and AMZN, resampled to 5 s (100 dims). Split 6:2:2. A 120-step context (10 min) predicts 24 steps (2 min).
- Results (Table 2, scaled). Mid-price MSE: Compound 0.0120 vs Linear 0.0125, LSTM 0.0124, Informer-style "Temporal" 0.0124 and Spacetimeformer 0.0122.
  Structure loss: 0.1480 vs 0.2430 (Linear). Total loss: 0.0080 vs 0.0090. Forecasting loss: Linear 0.0065 vs Compound 0.0066.
- Limitations stated: top 5 levels only (compute). The model cannot identify the cause of fluctuations (new orders vs cancellations vs executions).

**zaznov2022** (Mathematics 10(8):1234, 2022; abstract via Semantic Scholar — MDPI page returned 403)
- A survey of LOB-based price-movement prediction. "Some researchers claim accuracy … well in excess of 80%". The "ultimate conclusion … even the state-of-art models can not
  guarantee a consistent profit in active trading". DL models "are also more prone to over-fitting". It recommends trading simulation with "transactions costs, bid–ask spreads, and market impact".

**wu2021robust** (arXiv 2110.05479v2; also SSRN 10.2139/ssrn.4295991)
- Argues that level-based ("compressed") LOB inputs violate the assumptions of deep models. It proposes moving-window and market-depth (price-grid) representations and tests adversarial
  perturbations (orders added in empty ticks). Models: logistic regression, MLP (100–50), LSTM (20 units), DeepLOB (compressed only) and TCN (new representations). Data: FI-2010.
- Results (Table 1, no perturbation, accuracy / F-score):
  - Linear: 52.98 / 38.50 → 59.57 / 53.66 (moving-window).
  - MLP: 60.14 / 53.96 → 71.27 / 69.59 (market-depth).
  - LSTM: 70.74 / 68.45 → 77.46 / 76.18 (market-depth).
  - DeepLOB: 77.30 / 77.23.
  - TCN market-depth: 78.81 / 77.29.
  - Under "both" perturbation, DeepLOB falls to 51.35 / 39.59.
- Inconsistencies in the paper: the prose gives the LSTM numbers as identical to the MLP numbers, and gives DeepLOB as "47.5% … 22.2%"; neither matches Table 1.

**kolm_westray** — Kolm & Westray, "Deep learning alpha signals from limit order books: practical insights and lessons learned", Risk.net Cutting Edge, 7 Aug 2025 (paywalled; online supplement SSRN 5379395)
- This is the award paper. Risk Awards **2026** "Buy-side quants of the year: Petter Kolm and Nicholas Westray" (risk.net/awards/7962610). It is not a 2024 paper. A related SSRN working paper exists:
  "Improving Deep Learning of Alpha Term Structures from the Order Book" (SSRN 4770476, March 2024, not read).
- Per the award article (secondary): four DNN types on 115 stocks. Quotes: "Some of these fancy, very deep models, for this prediction task on order books, don't really help".
  Time-stamping order-book updates is "probably significantly more important than getting the latest and greatest deep neural network". A 1,000-update lookback did worse than a 500-update one.
  Hand-made stationary transforms beat letting models learn them. The article body was not read (paywall).

**arroyo2024** (QF 24(1):35–57, 2024; read arXiv 2306.05479v1)
- Survival analysis of limit-order time-to-fill. Encoder: a convolutional-Transformer (causal convolution producing Q/K/V, then self-attention) over a lookback window of
  T = 50, 500 or 1000 trades. Decoder: a monotonic neural network outputting the survival function S(t|x). Cancellations are right-censoring.
- Inputs: time of day, volatility, volume imbalance, microprice, and prices/volumes of the top 5 levels. Two datasets: observed orders placed by participants,
  and hypothetical 1-share orders pegged to the best level and "placed last in the queue". Nine Nasdaq stocks (AAPL, AMZN, BIDU, COST, CSCO, DELL, GOOG,
  INTC, MSFT). Training 1 Sep – 26 Dec 2022, 100 orders per day.
- Results: the MN-Conv-Trans average improvement over the MN-MLP encoder in RCLL (observed orders) is 86.01% (T=50), 85.71% (T=500) and 85.22% (T=1000). DeepSurv and DeepHit improve 16.66% and 19.73%.
- Limitations stated: Brier and C-index rejected as evaluation scores (see D10). Future work: whether fill probabilities improve LOB modelling/execution.

**coletta2021** (ICAIF '21, pp. 1–9; arXiv 2110.13287)
- A CGAN "world agent" trained on LOBSTER market data generates order streams conditioned on the recent market state inside ABIDES, so the simulated market responds to
  experimental agents. Evaluated by stylized facts and responsiveness. Claim: "outperforms previous work in terms of stylized facts reflecting market responsiveness and realism."

**coletta2022** (ICAIF '22, pp. 428–436; arXiv 2210.09897)
- Improved CGAN world agent plus an alternative explicit mixture-of-parametric-distributions world agent. Evaluated on stylized facts (e.g. AVXL) and on market-impact
  responsiveness. Claim: both "consistently outperform previous work, providing more realism and responsiveness."

**byrd2020abides** (SIGSIM-PADS '20, pp. 11–22) — An agent-based interactive discrete-event market simulator following NASDAQ ITCH/OUCH. It supports tens of thousands of agents with configurable latencies, and is demonstrated with market-impact experiments.

**lim2021tft** (IJF 37(4):1748–1764) — Temporal Fusion Transformer: an LSTM encoder-decoder for local processing, interpretable multi-head attention for long-range dependencies,
variable-selection networks and gating, and static covariate encoders. Multi-horizon quantile forecasts. Not an LOB paper.

**gu2022s4** (ICLR 2022, Outstanding Paper Honorable Mention per arXiv comments) — S4: a structured state-space sequence model with a normal-plus-low-rank state matrix,
which reduces the kernel computation to a Cauchy kernel. It solves Path-X (length 16k) and generates "60× faster" than Transformers.

**zuo2020thp** (ICML 2020, PMLR 119:11692–11702) — Transformer Hawkes Process. Self-attention over event history parameterizes a continuous-time conditional intensity (softplus).
Evaluated on likelihood and event prediction on datasets including financial transactions.

**zhang2020sahp** (ICML 2020, PMLR 119:11183–11193) — Self-Attentive Hawkes Process. Self-attention with time intervals encoded as phase shifts of sinusoidal positional encodings.
The authors claim interpretability of inter-type influence.

**shchur2021** (IJCAI-21, pp. 4585–4593) — Survey of neural TPPs: history encoders (RNN/attention), conditional intensity/density parameterizations, and likelihood-based training.

### Searched-for items (all found; metadata checked on Crossref or arXiv)

**ofmatnet**: Bandealinaeini, Sharifkhani, Salavati, "Attention-Based Multi-Asset Order Flow Networks for Enhanced Mid-Price Prediction", ICAIF '25, pp. 525–533.
- The name is confirmed by the abstract, via Semantic Scholar: "We propose OF-MATNet".
- Inputs are multi-level OFI features from several Nasdaq assets. A Transformer applies attention along the time, asset and level axes. Peer assets are chosen by rolling Granger-causality tests. The target is mid-price regression, scored by R².
- Abstract claims, tested on 110 assets: it beats OF-SATNet (single-asset), DeepLOB, BiNCTABL and TLOB, with "R2 improvements in over 90% of cases".
- Full text paywalled: horizons, R² values and stated limitations are UNVERIFIED.

**kanformer**: Zhong, Bacry, Guilloux, Muzy, arXiv 2512.05734 (Dec 2025). No journal version found.
- Architecture: dilated causal convolutions, then a Transformer encoder with Kolmogorov–Arnold Network layers. It outputs a survival curve for time-to-fill.
- Inputs: LOB snapshots, agent action-type embeddings and agent features, and the order's **queue position**.
- Data: Euronext CAC 40 front-month futures.
- Metrics: RCLL and IBS for calibration; time-dependent AUC and C-index for discrimination.
- Results (per fork read of Table 2, 30 runs):

  | Model | RCLL | IBS | IAUC | C-index |
  |---|---|---|---|---|
  | KANFormer | 0.53 ± 0.03 | 0.027 | 0.76 | 0.72 |
  | Arroyo ConvTrans | 1.18 | — | 0.47 | 0.44 |
  | ConvTrans++ (adds agent features and queue position) | 0.93 | — | 0.54 | 0.49 |

- Limitations stated: only one futures contract; equities untested.

**diffvolume**: Z. Wang, C. Ventre, "DiffVolume", ICAIF '25, pp. 587–595; arXiv 2508.08698.
- Method: a conditional diffusion model for future LOB volume snapshots. It conditions on past volumes and time of day, and optionally on a target liquidity profile for counterfactuals.
- Data: LOBSTER at one snapshot per second. Stocks: MU (big tick), AAPL (medium tick), ADBE and ZM (small tick), 16 days each.
- Evaluation: compared with WGAN-GP on marginals, spatial correlation and autocorrelation; it "outperform[s] … in all four stocks". Numbers not extracted.

**m3lob**: Y. Zhang, Y. Ma, Y. Cheng, J. Li, Y. Duan, "M3: A State-Event Generative Foundation Model for Market Microstructure Dynamics", arXiv 2608.19227 (29 Jul 2026).
- Model: autoregressive over a joint sequence of VQ-tokenized order events and an LOB-state encoding. Generation runs closed-loop through a simulated matching engine.
- Data: 800 CSI 300 and CSI 500 stocks, about 31.9B order events (Jan 2024 – Nov 2025); held-out test month Dec 2025.
- Results: a scaling law over five model sizes, reproduction of stylized facts, stress tests, and a square-root impact check with TWAP injections.
- Limitations stated (as future work): cross-asset information and context length.

**deepqr**: Bodor & Carlier, "Deep Learning Meets Queue-Reactive: A Framework for Realistic Limit Order Book Simulation", arXiv 2501.08822 (Jan 2025).
- MDQR extends Huang et al. 2015 in three ways: it drops queue independence, adds market features to the state, and models order sizes. Neural networks parameterize the state-dependent event intensities, so it is also a neural/point-process hybrid.
- Data: Euro-Bund futures, Mar–Jun 2022. Results are qualitative: it reproduces the square-root impact law, order-size distributions and cross-queue correlations.

**LOBMamba**: UNVERIFIED. No paper by that name was found, and no Mamba/SSM LOB forecaster was added. The closest hit (Graph-Mamba, arXiv 2410.03707) uses daily prices, not LOB data.

### Extra: spatial-temporal / structured forecasters

**kisiel2022axial**: Kisiel & Gorse, "Axial-LOB", IEEE SSCI 2022, pp. 1327–1333.
- Fully attentional: gated, position-sensitive axial attention along the time and feature axes.
- FI-2010 F1 is 85.14 / 75.78 / 80.08 / 83.27 / 85.93 at k = 10 / 20 / 30 / 50 / 100, with 9,615 parameters vs DeepLOB's 142,435 (per the paper).
- LOBCAST measured AxialLOB at F1 73.4 on FI-2010 (claimed 82.0) and 59.5 on LOB-2021.

**briola2025hlob**: Briola, Bartolucci & Aste, "HLOB", ESWA 266:126078, 2025.
- Method: a TMFG graph over volume levels, combined with a homological CNN.
- Evaluation: against 9 state-of-the-art models on 15 NASDAQ stocks (3 datasets), using F1, MCC and p_T.
- Abstract claim: best in 4 of 6 small-tick, 3 of 3 medium-tick and 4 of 6 large-tick stocks.

OF-MATNet (above) is the multi-asset, multi-axis attention example.

### Extra: diffusion LOB generation

**berti2025trades**: Berti, Prenkaj & Velardi, "TRADES", ECAI 2025 (FAIA, doi 10.3233/FAIA251249; volume and pages not confirmed on Crossref, so omitted).
- Method: a transformer DDPM that generates order flow conditioned on the LOB state.
- Data: LOBSTER TSLA and INTC, January 2015.
- Predictive-score MAE: Tesla 1.213 vs market replay 0.923, IABS 1.870, CGAN 3.453; Intel 0.307 vs 0.149, 1.866, 0.699.
- The headline "×3.27 / ×3.48" figures are ratios of excess MAE over market replay, not of raw MAE.
- Limitations stated: one model per stock; "viable … in controlled environments".

**wang2026difflob**: Z. Wang & C. Ventre, "DiffLOB", IJCAI-26, pp. 6564–6572.
- A diffusion model conditioned on a future regime (trend, volatility, liquidity, OFI) for counterfactual LOB generation.
- Only the abstract was read; no numbers.

**diffvolume** (above).

### Extra: Hawkes / point-process structure inside a neural LOB model

**shi2022sdpnhp**: Shi & Cartlidge, KDD '22, pp. 1607–1615.
- Method: a separate intensity per event type, computed by parallel continuous-time LSTMs, plus an event–state interaction.
- Data: LOBSTER INTC, MSFT and JPM, 5 days each.
- Results: best NLL, time loss and type accuracy against stochastic and neural TPP baselines, including SAHP and CT-LSTM.
- Limitations stated: simplistic simulator settings (market-order occurrence, the relative-price distribution of arriving orders).

**lalor2025nhp**: Lalor & Swishchuk, Applied Mathematical Finance 32(2):128–155, 2025.
- A 12-event-type LOB driven by an LSTM-based neural Hawkes process, used as the environment for a deep-RL market maker. Only the abstract was read.

**deepqr** (above) is the third such hybrid: neural networks inside queue-reactive (point-process) intensities.

---

## Part 2 — Claim verdicts

### D1 — Briola et al. p_T; "DeepLOB-class models with >60% accuracy have p_T ≈ 0.51–0.54 vs 0.50 null"
**CONTRADICTED.**
- Definition (Briola, Bartolucci & Aste, arXiv 2403.09267v4, Eq. 3): **p_T = CT / (PT + TT − CT)**.
  - PT = number of potential transactions counted on the targets set.
  - TT = number of executed transactions counted on the predictions set.
  - CT = number of correctly executed transactions (the intersection).
  - "a transaction happens when one is able to open a position and then close it".
  It is a Jaccard-type overlap, not a hit rate with a 0.5 null.
- Reported values (Table 8, DeepLOB, 15 NASDAQ stocks), lowest threshold column (0.3; the paper highlights the untouched-signal case):
  - H10: 0.04 (GOOG) to 0.15 (AAPL).
  - H50: 0.01 (NVDA) to 0.17 (CSCO).
  - H100: 0.03 (IBM, NVDA) to 0.19 (BAC).
  - At threshold ≥0.5, p_T for small-tick stocks is ≈0.00–0.04 ("the probability of correctly executing a trade at a threshold larger than 0.5 is zero").
- No null/random p_T value is given. No ">60% accuracy" figure is used: the paper reports MCC and F1. F1 in Table 8 ranges up to 0.79 (PFE, H50, threshold 0.9) while p_T there is 0.00.
- The "0.51–0.54 vs 0.50" numbers appear nowhere in the paper.

### D2 — spread-to-tick thresholds (≈3 high predictability, <1.5 noise)
**CONTRADICTED** (the thresholds exist, but the claim gets their meaning and direction wrong).
- Exact text: "if (i) ⟨σ⟩ ≳ 3θ, we are dealing with a small-tick stock; if (ii) ⟨σ⟩ ≲ 1.5θ, we are dealing with a large-tick stock; if (iii) 1.5θ ≲ ⟨σ⟩ ≲ 3θ, we are dealing with a medium-tick stock."
  (σ is the average spread, θ the tick size.)
- These are tick-size class boundaries, not predictability thresholds. Predictability goes the opposite way to the claim. p_T "increases moving from small-tick stocks to large-tick stocks", and so does MCC.
  "large-tick stocks demonstrate to offer higher probabilities to actually operate trading in a fully automated way compared to small-tick stocks."
  So spread ≲ 1.5 ticks (large-tick) is the *more* predictable regime, and spread ≳ 3 ticks (small-tick) the least.
  Medium-tick stocks are "structurally more similar to large-tick stocks".

### D3 — LOBCAST: all 15 models degrade on new data; degradation positively correlated with parameter count
- Part 1, "all degrade significantly": **VERIFIED as the authors' own claim, with caveats.** Abstract: "all models exhibit a significant performance drop when exposed to new data".
  - On LOB-2021 the F1 of 4 models (MLP, CNNLSTM, CNN2, TLONBoF) is *above* their originally claimed FI-2010 F1 (↑ arrows in Table 2).
  - Every model's LOB-2021/2022 F1 (48–61) is below the best FI-2010 results.
  - The drop is large for the FI-2010 leaders, e.g. BiNCTABL −19.6% on average.
- Part 2, "larger models degrade most / positive correlation with parameter count": **CONTRADICTED / not in source.**
  - The paper makes no such statement. Its only parameter remark is that counts are "very low compared to" CV/NLP.
  - My own computation from Tables 1–2: Spearman ρ between parameter count and generalizability score (higher = less degradation) is +0.34 (LOB-2021) and +0.30 (LOB-2022).
    The sign is opposite to the claim. With 15 models this is weak and not significant.
  - Examples: ATNBoF has the most parameters (1.3·10⁷), generalizability 80.9. BiNCTABL/CTABL have the fewest (1.1·10⁴), generalizability 73.5 / 78.4.
- Source: arXiv 2308.01915v2. The AIR journal page could not be fetched.

### D4 — Lucchese et al. 2024
- (a) "representation matters more than architecture": **UNVERIFIED as phrased.** The paper holds the CNN–Inception–LSTM architecture fixed and varies only the input, so it does not compare architectures.
  What it does say: "The performance of the deep learning models is strongly dependent on the choice of order book representation". Also:
  "models based on the basic order book level representation are considerably outperformed by those with order flow or volume inputs" (abstract and conclusions, arXiv v3).
- (b) "rolling 5-day z-score beats global": **CONTRADICTED / not shown.** A 5-day rolling-window z-score *is used*: "the features are standardized using a 5-day rolling window".
  Kolm et al. used training mean/std, which is noted as a "slight difference". But no experiment compares rolling vs global normalization, and no superiority claim is made.
- (c) Quote "Before investing resources in implementing complex Transformers...": **UNVERIFIED — not found.**
  - Not in arXiv 2211.13777v3, whose text contains no occurrence of "Transformer" at all.
  - Not in Briola 2020/2024, LOBCAST, TLOB, Wu 2021 or Zhang & Zohren 2021 (all grepped).
  - A web search for the exact phrase found nothing.
  - The IJF published version was not read, so it may differ from v3.

### D5 — Kolm et al. 2023
- 115 Nasdaq stocks: **VERIFIED** (abstract: "115 stocks traded on Nasdaq"). Period 2 Jan 2019 – 31 Jan 2020 (per Lucchese et al., who reuse the universe).
- OFI inputs beat raw order-book inputs: **VERIFIED**. Abstract: "models trained on order flow significantly outperform most models trained directly on order books". Note the word "most".
- Multiple horizons: **VERIFIED** in general ("at multiple horizons"; "effective horizon … approximately two average price changes"). The exact horizon grid is **UNVERIFIED**.
- Results vs linear/ARX baselines: **UNVERIFIED** — full text inaccessible (Wiley/SSRN 403). Do not cite R² numbers or ARX comparisons until the paper is read.

### D6 — "simple MLPs / linear models are competitive with deep models when features are well engineered"
**Split:** the MLP part is supported by specific papers. The linear part is mostly contradicted.
- Supporting evidence:
  - **briola2020**: the MLP matches or beats CNN-LSTM on INTC. Balanced accuracy is 0.56 / 0.59 / 0.61 vs 0.57 / 0.56 / 0.55, and MCC 0.34 / 0.36 / 0.38 vs 0.34 / 0.34 / 0.32 (H10/50/100). Raw LOB inputs, not engineered features.
  - **berti2025tlob**: the MLP-based MLPLOB beats every prior model on FI-2010 and matches TLOB (81.64 / 84.88 / 91.39 / 92.62). It is best on INTC at h=10/20.
    But MLPLOB has 6.3·10⁷ parameters, so it is "simple" in architecture, not in size.
  - **wu2021robust**: with a better representation an LSTM reaches 77.46 / 76.18 (market-depth) ≈ DeepLOB 77.30 / 77.23. But the MLP stays well behind (71.27 / 69.59) and the linear model far behind (52.98–59.57).
  - **kolm2023** abstract: "simpler 'off-the-shelf' artificial neural networks on stationary inputs". **kolm_westray** (via the Risk award article): "fancy, very deep models … don't really help".
  - **arxiv240902277**: a linear AR seq2seq ties or beats the attention models on the forecasting-loss component (0.0065 vs 0.0066) and on volume MSE (0.0105 vs 0.0106). It loses on mid-price MSE and structure loss.
- Contradicting evidence (linear and MLP models clearly worse):
  - **briola2020**: logistic regression MCC 0.24–0.30 vs 0.32–0.38.
  - **zhangzohren2021** LSE: linear F1 41–42 and MLP F1 44–47 vs DeepLOB 61–68.
  - **tsantekidis2017lstm**: MLP F1 48–56 vs LSTM 61–66.
  - **prata2024**: MLP is ranked 14th on FI-2010 and LOB-2021, but it degrades least (generalizability 95.0).

### D7 — TransLOB: causal convolutions + masked self-attention; FI-2010 results
**VERIFIED.**
- Abstract: "uses a causal convolutional network for feature extraction in combination with masked self-attention".
- Architecture: five dilated causal conv layers (dilation 1–16), then two transformer blocks with masked multi-head self-attention.
- FI-2010 F1: 88.66 (k=10), 80.65 (k=20), 88.20 (k=50), 91.61 (k=100). Accuracy: 87.66 / 78.78 / 88.12 / 91.62.
- Caveat: the k=100 comparison is against a CNN baseline only. LOBCAST's independent replication gets FI-2010 F1 59.4 ± 2.6 vs the claimed 87.3.

### D8 — DeepLOB: CNN + Inception + LSTM; LSE + FI-2010; simple trading simulation
**VERIFIED.**
- Architecture: convolutional layers, then an Inception module, then a 64-unit LSTM.
- Data:
  - FI-2010 (two setups).
  - One year of LSE data for LLOY, BARC, TSCO, BT and VOD (3 Jan – 24 Dec 2017), 3-month test.
  - Transfer test on GLEN, HSBC, CNA, BP, ITV.
- Trading simulation (Sec. IV-D): µ = 1 share. Signals −1/0/+1 map to sell/wait/buy, executed with a 5-step delay. Results are reported as normalized daily profits.
- Transaction costs are **not included**: "we use mid-prices without transaction costs". The authors concede this "is not a reasonable assumption for a standalone strategy".
  They argue that passive entry plus aggressive exit is "effectively equivalent to a mid-mid trade".

### D9 — TLOB
- Dual spatial and temporal attention: **VERIFIED.** Each block alternates temporal self-attention across snapshots and spatial self-attention across features. In the ablation (Table 9), full TLOB beats "w/o SA" and "w/o TA" at all horizons.
- Datasets: **VERIFIED**: FI-2010, TSLA and INTC (LOBSTER, January 2015), and BTC (Binance perpetual, 2023, Kaggle).
- Performance falls with horizon: **VERIFIED for the NASDAQ and BTC data**. "As expected, the longer the horizon, the more difficult to forecast". INTC TLOB F1 drops 80.15 → 50.14 and BTC 74.7 → 41.49.
  On FI-2010 F1 *rises* with horizon (81.55 → 92.81) under the original FI-2010 labels.
- Economic evaluation: **partial, no backtest**. Sec. 7.5 sets the label threshold θ to the average spread (TSLA only). F1 falls to 41.39 / 36.48 / 30.82 at h = 50 / 100 / 200.
  Conclusions: "an extensive profitability analysis, based on backtesting … would be very interesting" (future work). Also: "not sufficiently mature for practical deployment".
  The paper also shows a predictability decline over time (INTC 2012 vs 2015, −6.68 F1).

### D10 — Arroyo et al.: queue position as input? metric?
- Queue position as an input feature: **CONTRADICTED.**
  - Inputs listed: "time of day, volatility, volume imbalance, microprice, and prices and volumes of the best five levels".
  - Queue position enters only through the data design. The hypothetical orders are 1-share orders "placed last in the queue", and the fill conditions track the "orders in front of the hypothetical limit order in the queue" (Appendix A).
  - The text does not list queue position or volume-ahead as a model feature.
  - Independent corroboration: KANFormer (arXiv 2512.05734) describes ConvTrans as using LOB snapshots only. It adds queue position in a "ConvTrans++" variant, and credits that gain to "the importance of including agent-level information and queue position".
- Metric: **VERIFIED: right-censored log-likelihood (RCLL)** for both training and evaluation, chosen because it is a proper scoring rule (Rindt et al. 2022).
  The authors reject time-dependent concordance and Brier as improper or as relying on assumptions that fail here. Brier "is a proper scoring rule under the assumption of independence between censoring and covariates … These assumptions do not hold in the context of limit order executions".
