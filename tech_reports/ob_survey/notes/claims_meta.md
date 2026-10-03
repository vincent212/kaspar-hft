# Claims & paper notes — meta-models / engineered-feature learners for LOB prediction

Verified 2026-10-03. Metadata comes from the Crossref API (arXiv API where an arXiv id is
given). Text was extracted with `pdftotext` from arXiv PDFs, the NBER PDF, an SSRN PDF and
a Berkeley technical-report PDF. Abstracts of paywalled papers come from OpenAlex, EconPapers
or institutional repositories.

Read levels:
- **FULL TEXT**: the PDF was read. The version is stated, and it is often the arXiv or working-paper
  version rather than the journal version.
- **ABSTRACT ONLY**: only the abstract was read.
- **METADATA ONLY**: only the bibliographic record was checked.

Nothing below was filled in from memory. Anything I could not confirm is marked **UNVERIFIED**.

BibTeX: new keys are in `bib/refs_meta.bib`. Existing keys (in `refs.bib` or other `bib/*.bib`) are
reused and not redefined: `kolm2023`, `lucchese2024`, `contcucuringu2023`, `lopezdeprado2018`,
`prata2024`, `briola2020`, `briola2024`, `gould2016qi`, `stoikov2018`, `xu2019mlofi`, `ntakaris2018`,
`tsantekidis2017`, `sirignano2019`, `zhang2019`, `kolm_westray`, `bailey2014pbo`.

---

## Part 1 — Engineered/handcrafted LOB features fed to learners

### kercheval2015 — Kercheval & Zhang, *Quantitative Finance* 15(8):1315–1329 (2015). **ABSTRACT ONLY**
- doi 10.1080/14697688.2015.1032546. The paper is closed access (Semantic Scholar and OpenAlex mark it CLOSED). The abstract was read via EconPapers/OpenAlex.
- Abstract: "machine learning framework … automate real-time prediction of metrics such as mid-price
  movement and price spread crossing. By characterizing each entry in a limit order book with a vector of
  attributes such as price and volume at different levels, the proposed framework builds a learning model for
  each metric with the help of multi-class support vector machines. Experiments with real data establish that
  features selected by the proposed framework are effective for short-term price movement forecasts."
- Feature set (second-hand, from nousi2019 §4.1, which cites it as [15]):
  - basic: prices and volumes at every level;
  - "time-insensitive": spread, mid-price, price differences, accumulated differences;
  - "time-sensitive": average intensities of trades, orders, cancellations and executions, and their derivatives ("limit activity acceleration").
  The same feature set later underlies FI-2010 (ntakaris2018).
- Data, horizons and accuracy numbers: **UNVERIFIED** (full text not read).

### ntakaris2020plos — Ntakaris, Kanniainen, Gabbouj, Iosifidis, *PLOS ONE* 15(6):e0234107 (2020). **FULL TEXT (arXiv 1907.09452v1)**
- **Inputs:** 273 handcrafted features in three pools:
  - 135 "first pool" LOB features, after Kercheval–Zhang and FI-2010;
  - 83 technical indicators;
  - 55 quantitative indicators.

  One quantitative feature is the output of an *online adaptive logistic regression*. It is fitted on blocks of 10 messages and gives the probability of a best-ask (bid) change at the 10th event. It is a model output used as a feature, i.e. a stacking-style meta-feature.
- **Feature selection and learners:** wrapper feature selection ranked by entropy, LMS (two variants) and LDA (two variants). The classifiers are LMS, LDA and an RBF network (an ELM-type MLP).
- **Data and labels:** Nasdaq Nordic ITCH, 5 stocks, 10 days. This is the FI-2010 data, with 4,581,250 events and 458,125 feature vectors. Labels are ternary, from the percent change of a smoothed mid-price (span 9) with threshold γ = 0.002.
- **Horizons and protocol:** horizons are the next 10th, 20th and 30th events. The protocol is anchored day-by-day CV with 9 folds. Normalization is a rolling z-score, with the stated aim of avoiding look-ahead bias.
- **Headline numbers (Table 4, full pool, macro-F1):**
  - T=10: best 0.444 ± 0.011 (LMS2-LMS). The range across the 12 sorting/classifier pairs is 0.397–0.444.
  - T=20: best 0.462.
  - T=30: best 0.472 ± 0.015 (LDA2-LMS).
  - Technical-only pool (Table 6): best F1 0.365 / 0.391 / 0.403.
  - Quantitative-only pool (Table 7): best 0.384 / 0.435 / 0.449.
  - Table 8: several sorting/classifier pairs plateau at about 5 features. LMS2-RBFN gets 0.403 with 5 features and 0.442 with all 273.
- **Stated findings:**
  - "sorting methods and classifiers can be used in such a way that one can reach the best performance with a combination of only very few advanced hand-crafted features".
  - The adaptive-logistic-regression feature is "selected first among most of the five sorting methods".
- **Stated limitations:**
  - Only 5 stocks and 10 days; the authors "intend to test our experimental protocol on a longer trading period".
  - More advanced classifiers (CNN/RNN) are out of scope.
- **Caveat:** the numbers come from arXiv v1 and the PLOS version was not read. The arXiv text calls the data both "Nordic" and "FI-2010".

### ntakaris2019access — Ntakaris, Mirone, Kanniainen, Gabbouj, Iosifidis, *IEEE Access* 7:82390–82412 (2019). **FULL TEXT (arXiv 1904.05384v3)**
- **Inputs:** three handcrafted sets plus an automated set:
  - "Econ": a new set of econometric features from the message book and the LOB;
  - "Tech-Quant": from ntakaris2020plos;
  - "LOB": from Kercheval–Zhang and FI-2010;
  - features from an LSTM autoencoder ("fully automated").
- **Learners:** nine networks (5 MLPs, 2 CNNs, 2 LSTMs), each trained jointly on two tasks: classifying the next mid-price direction and regressing the number of events until the change.
- **Data:** TotalView-ITCH, 10 business days each.
  - US: AMZN and GOOG, 22.09.15–05.10.15, about 13M events.
  - Nordic: 5 stocks, about 4M events.
- **Headline numbers (Protocol I, F1):**
  - Nordic joint: best F1 53% (Econ) and 56% (Tech-Quant), MLP 3.
  - US joint: LSTM 2 gets 59% F1.
  - US Google stock-specific: LSTM 1 with Tech-Quant gets 58%.
- **Headline numbers (Protocol II, F1):**
  - US joint: 65% (MLP 4, Tech-Quant, balanced).
  - Kesko: 63%.
- **Handcrafted vs automated features:** LSTM-AE features score mostly F1 0.27–0.43 in Tables 9–10. Outliers reach 0.46 and 0.53 (US, LSTM 2, unbalanced) and up to 0.49 (Nordic).
- **Conclusion:** "showed superiority of the suggested handcrafted feature sets against the fully automated process derived from an LSTM AE … extensive analysis of the input signal leads to high forecasting performance even with simpler neural network architects like shallow MLPs".
- **Limitations:**
  - Only 2 US and 5 Nordic stocks; extension "for future research".
  - Feature and model choice "should be differentiated for liquid and illiquid stocks".
- **Caveat:** the comparison is against an *autoencoder* representation, not against end-to-end raw-LOB deep models such as DeepLOB.

### nousi2019 — Nousi et al., *IEEE Access* 7:64722–64736 (2019). **FULL TEXT (arXiv 1809.07861v2)**
- **Inputs:** the 144-dimensional Kercheval–Zhang/FI-2010 handcrafted vector, computed every 10 events. It is combined over a sliding window of 5 in four ways:
  - `last` (144-d);
  - `mean` (144-d);
  - `last⊕mean` (288-d);
  - `concat` (720-d).

  These are compared with unsupervised learned features (autoencoder, 24-d; bag-of-features) and with concatenations of the two.
- **Learners:** linear SVM (SGD), a single-layer feedforward network (SLFN) and an MLP (512-512-3).
- **Data:** FI-2010 (5 Nordic stocks, 1–14 June 2010, 10 days, 453,975 vectors).
- **Labels:** the mean of the next Nα ∈ {1, 5, 10} smoothed mid-prices, with γ = 0.0001/0.0002/0.0003.
- **Protocols:** anchored walk-forward (9 folds) and leave-one-stock-out.
- **Headline numbers (Table 5, MLP, anchored, macro-F):**
  - `concat` 48.75 / 56.06 / 52.03 for Nα = 1 / 5 / 10.
  - `AE` 31.79 / 30.11 / 29.22.
  - `BoF` 33.77 / 35.78 / 36.17.
  - SVM `concat`, Nα = 1: 45.44.

  Handcrafted (windowed) representations beat the learned low-dimensional ones. Combining learned with handcrafted (e.g. last⊕BoF) beats learned alone.
- **Conclusion:** "prediction results are improved when combining the extracted feature representations with the handcrafted ones".
- **Data-quality flag:** Table 7 gives the stationary class precision 79.35, recall 89.47 and F-score 93.98. An F-score above both precision and recall is arithmetically impossible, so this row contains an error.
- Limitations are not explicitly stated beyond class imbalance and noise at short horizons.

### tsantekidis2020stationary — Tsantekidis et al., *Applied Soft Computing* 93:106401 (2020). **FULL TEXT (arXiv 1810.09965v1)**
- **Inputs:** "stationary features":
  - level prices as the percentage difference to the current mid-price;
  - the mid-price percent change since the previous event;
  - cumulative depth per side;
  - each z-scored.

  They are compared with raw prices and volumes z-scored with the previous day's statistics.
- **Learners:** linear SVM, MLP (128-64-32), CNN, LSTM and CNN-LSTM.
- **Data:** Nasdaq Nordic, 5 Finnish stocks, 10 days (first 7 days train, last 3 test).
- **Horizons:** k ∈ {10, 50, 100, 200} events.
- **Headline numbers (Table 3, mean F1 and Cohen's κ, raw → stationary):**
  - LSTM: k=10 0.35 → 0.42 (κ 0.12 → 0.18); k=100 0.34 → 0.44.
  - CNN: k=100 0.37 → 0.44.
  - MLP: k=100 0.26 → 0.39.
  - CNN-LSTM (stationary only): 0.44 / 0.47 / 0.48 / 0.49 for k = 10 / 50 / 100 / 200 (κ 0.21–0.25).
  - SVM barely changes: 0.33 → 0.35 at k=100.
- **Claim:** "proposed stationary price features significantly outperform the raw price features for all the tested models".
- **Protocol flag:** Table 3 reports "the mean of each metric for the last 20 training epochs". The text does not describe a separate validation set for stopping or model selection.
- **Stated limitation:** more data is needed for bigger models; overfitting was observed.

### zheng2013jump — Zheng, Moulines, Abergel, *Journal of Mathematical Finance* 3(2):242–255 (2013). **FULL TEXT (arXiv 1204.1381v1)**
- **Inputs:** lagged limit-order volumes, price gaps, market-order sign and size, and limit-order event counts.
- **Learner:** logistic regression and LASSO-logistic (λ chosen by CV).
- **Target:** an inter-trade "price jump", meaning the next market order executes beyond the best quote that was present just after the previous market order.
- **Data:** the 40 CAC40 stocks, April 2011, with morning and afternoon subsets.
- **Headline numbers:**
  - Out-of-sample AUC "around 0.80 … consistently high over all datasets and all stocks" (Fig. 5).
  - The conditional probability of the next trade sign "reaches 0.80 in average when the liquidity on the best limit prices is quite unbalanced".
- **Protocol:** the train/test split is **UNVERIFIED** (not described in the arXiv v1 text I read).
- **Venue note:** the journal is a SCIRP open-access journal.

### palguna2016 — Palguna & Pollak, *IEEE J. Selected Topics in Signal Processing* 10(6):1083–1092 (2016). **ABSTRACT ONLY**
- Nonparametric mid-price predictors built on features from the current and recent order book. They are evaluated inside an order-execution task on NASDAQ data over five months (2013–2014).
- Abstract: for the two best features "the trading cost improvement is on the order of one basis point".
- Which features were used, and the learner details: **UNVERIFIED**.

### tashiro2019 — Tashiro, Matsushima, Izumi, Sakaji, *Quantitative Finance* 19(9):1499–1506 (2019). **ABSTRACT ONLY**
- The CNN is fed order-based (message) encodings instead of LOB snapshots.
- Abstract: "smoothing filters which we propose to employ rather than embedding features of orders improve accuracy". An investment simulation is included.
- Numbers: **UNVERIFIED**.

---

## Part 2 — Classical microstructure measures as learner inputs

### easley2021 — Easley, López de Prado, O'Hara, Zhang, *Review of Financial Studies* 34(7):3316–3363 (2021). **FULL TEXT (SSRN 3345183, Feb 2019 version)**; the RFS abstract was read via OpenAlex
- **Inputs:** six classical microstructure measures:
  - Roll measure;
  - Roll impact;
  - a volatility measure (VIX proxy);
  - Kyle's λ;
  - Amihud;
  - VPIN.

  They are computed on dollar-volume bars (about 50/day) with look-back windows of 25–2000 bars.
- **Learner:** random forest (sklearn defaults: 100 trees, max_features = int(√6) = 2, balanced class weights, unregularized). A regularized forest and logistic regression are used as robustness checks.
- **Targets:** binary sign of the change in six quantities over h = 250 bars (≈ one week; h = 50 as robustness): Corwin–Schultz bid–ask spread, realized volatility, Jarque–Bera statistic, first-order autocorrelation, |skewness| and kurtosis.
- **Data:** 87 liquid futures across asset classes, 5 years of tick data.
- **Protocol:** 10-fold *purged* CV, which "purge[s] approximately one-week of data from the training set to remove observations that could contained leaked information". An embargo is not mentioned in this version.
- **Feature importance:** MDI (in-sample) vs MDA (out-of-sample, permutation).
- **Headline numbers and findings:**
  - Out-of-sample accuracy, excluding the bid-ask spread target, "reach highs ranging from 0.54 to 0.61 (depending on the lookback windows)". Spread prediction accuracy is "not as good".
  - In-sample (MDI), Amihud, VIX and VPIN are most important.
  - Out-of-sample (MDA), "VPIN is the most important predictor for five variables, with the Roll measure dominating for the sixth".
  - "Some microstructure features with apparent high explanatory power exhibit low predictive power, and vice versa."
- **Logistic regression vs random forest (§5.5):**
  - MDA importances are correlated above 0.6.
  - "Prediction accuracy with the logistic is also similar … with the logistic approach typically being slightly more accurate."
  - "the random forest sets a non-parametric benchmark that a classical model can beat by injecting structural information".
- **Limitations stated:**
  - Spread is imputed (Corwin–Schultz), so spread prediction is weak.
  - Accuracy is used rather than cost-weighted metrics.
  - The study is "not … tied to a particular investment strategy".
- **Caveat:** the numbers are from the 2019 SSRN draft. The RFS abstract adds "cross-asset effects", which are not in the draft.

### aitsahalia2026 — Aït-Sahalia, Fan, Xue, Zhou/Zhu, *Management Science* 72(9):7792–7815 (2026). **FULL TEXT (NBER WP 30366, Aug 2022)**; the MS abstract was read via OpenAlex
- **Author discrepancy:** the 4th author is "Yifeng Zhou" on NBER and SSRN 4196310, and "Xiaonan Zhu" on Crossref (MS) and SSRN 4095405. The bib uses the Crossref MS author list.
- **Inputs:** 13 TAQ-derived predictors over 9 look-back windows, 117 in total. They include:
  - LobImbalance (top-of-book depth imbalance);
  - TxnImbalance (Lee–Ready signed volume);
  - PastReturn;
  - Lambda (price range / volume);
  - volume statistics;
  - durations.
- **Learners:**
  - main: LASSO and random forest;
  - robustness: OLS, ridge, FarmPredict, gradient-boosted trees, and feed-forward NNs with 1–2 layers.
- **Data:** 101 S&P 100 stocks, Jan 2019–Dec 2020, 505 days.
- **Protocol:** rolling. Each model is trained on the previous 5 days and predicts the next day. Hyperparameters are re-tuned every 20 days on a separate 20-day tuning block.
- **Horizons:** calendar (5 s, 30 s), trade (10 and 200 trades) and volume (1,000 and 20,000 shares) clocks.
- **Headline numbers:**
  - Median out-of-sample R² for 5-s returns is about 10% (10.5% in the intro), and R² > 0 for every stock. For 30-s returns the median is about 4%.
  - Direction accuracy is about 64% for the next 5 s / 10 trades / 1,000 shares.
  - Median OS R² for the duration to the next 10 trades is 9.8%.
  - With a simulated imperfect "peek" at incoming order flow, 5-s R² goes from 14.0% to 27.1% and direction accuracy from 68.3% to 79.0%.
  - About 80% of predictability comes from the most recent 10 ms / 10 transactions.
- **Feature importance (LASSO selection frequency):** TxnImbalance and PastReturn come first, then LobImbalance. The volume variables are "consistently not predictive". Quote: "variables derived from the state and evolution of the limit order book tend to be less useful for predicting future returns than variables derived from the transactions record".
- **Learner comparison (§7.1):**
  - "all methods have very similar performance, except OLS".
  - RF and GBT gains over LASSO "are limited for most cases".
  - OLS fails "due to heavy overfitting".
  - RF is "slightly better" than LASSO for returns, with "substantially the same performance" for direction.
- **Stated limitations:**
  - Only TAQ data, so the result is a "lower bound" on achievable predictability.
  - Latency is not modelled in the base results.
  - The prediction is a theoretical exercise, not a trading backtest.

### rahimikia2020ml — Rahimikia & Poon, SSRN WP 3707796 (2020). **ABSTRACT ONLY** (via the University of Manchester repository)
- **Abstract:** ML models for daily realised volatility using "HAR model variables, limit order book (LOB) data, and news sentiment".
  - "nearly seven million ML models".
  - "high-dimensional ML models outperform HAR models in 90% of the out-of-sample period, except during extreme volatility".
  - "incorporating ML into ensemble frameworks enhances HAR model performance, though caution is needed when using ML models as direct substitutes".
- This is a classical-model (HAR) + ML ensemble. Not peer-reviewed as far as I could verify.

### rahimikia2026altdata — Rahimikia & Poon, *International Journal of Finance & Economics* (online 8 Jun 2026). **ABSTRACT ONLY**
- The published companion is *linear*: LOB and news variables are added to HAR-family models, "requiring minimal changes to the benchmark specification and no auxiliary models".
- Abstract: "within the order book, depth measures contribute more to forecasting performance than slope measures". Gains are "strongest on high volatility days".

### guo2018btcvol — Guo, Bifet, Antulov-Fantulin, *IEEE ICDM 2018*, 989–994. **FULL TEXT (arXiv 1802.04065v3)**
- **Target:** hourly realised volatility of BTC.
- **Data:** OKCoin, about 1 year of minute-level order book.
- **Inputs:** volatility history plus order-book features (spread, depths, volumes, their differences, weighted spread, bid/ask slope), over 30 minutes of history.
- **Model:** a "temporal mixture" (TM-G, TM-LOG). A gate mixes an autoregressive volatility component with an order-book-feature component, which makes it a learned combination of a classical model and features.
- **Baselines:** EWMA, GARCH, BEGARCH, STR, ARIMA, ARIMAX, STRX, RF, XGBoost, elastic net, GP and LSTM.
- **Protocol:** 12 monthly test intervals with a 3-month rolling train/validation window.
- **Headline numbers (Table I, RMSE):**
  - TM-G has the lowest RMSE in 10 of 12 intervals. Exceptions: interval 3, where LSTMs win (0.258 vs TM-G 0.259), and interval 4, where TM-LOG wins (0.172 vs TM-G 0.189).
  - Interval 1: TM-G 0.075, XGT 0.076, RF 0.082, LSTMs 0.081, GARCH 0.136.
  - Adding the order-book features linearly (ARIMAX) has higher RMSE than plain ARIMA in all 12 intervals (e.g. interval 2: 0.282 vs 0.194).
- **Limitation stated:** deep temporal-mixture variants were not attempted, for interpretability reasons.

### bieganowski2026 — Bieganowski & Ślepaczuk, arXiv 2602.00776 (2026; SSRN 6159346). **FULL TEXT (arXiv v1)**. Preprint, not peer-reviewed
- **Inputs:** engineered features: top-of-book metrics, order-flow/trade imbalance, and VWAP-to-mid deviations. Deep levels are deliberately omitted.
- **Learner and target:** CatBoost with MSE and the direction-aware GMADL loss. The target is the 3-s log mid return.
- **Data:** Binance Futures perpetuals (BTC, LTC, ETC, ENJ, ROSE), 1-s frequency, 1 Jan 2022–12 Oct 2025.
- **Protocol:** walk-forward CV with a purge gap and inner time-series CV for tuning.
- **Claims:**
  - SHAP rankings and dependence shapes are stable across assets.
  - The imbalance SHAP magnitude rises with relative tick size.
- **Backtest table flag:** the taker backtest (Table 1) reports ARC 0.13–7.00 and IR* up to 8.97, while the "IR**" row reaches 101.21 and 213.58. I did not reconcile the IR** definition. **Do not cite these numbers.** Latency is not modelled ("upper bound").

### wang2025cryptolob — Wang, arXiv 2506.05764 (2025). **FULL TEXT (arXiv v2)**. Preprint, not peer-reviewed
- **Data:** Bybit BTC/USDT 100 ms snapshots from a **single day** (2025-01-30). About 100,000 snapshots are used for most tables (80/20 split) and 1,000,000 for Table 4.
- **Models:** logistic regression, XGBoost, CatBoost, CNN+XGBoost, CNN+CatBoost (CNN embeddings fed to a GBDT, i.e. a two-stage stack), DeepLOB and CNN+LSTM.
- **Inputs:** a flattened raw LOB (T×F), with Kalman or Savitzky–Golay filtering.
- **Headline numbers:**
  - Binary, 500 ms, 40 levels, SG-filtered (Table 2): logistic 0.7284, XGBoost 0.7281, DeepLOB 0.7189, CNN+XGB 0.7130, CatBoost 0.6260.
  - Conclusion: simpler models "marginally outperform our more complex neural networks by 1–2%".
- **Leakage flag:** the Savitzky–Golay filter is written as a *centred* window. Eq. 5 fits v_{t+j} for j = −10…10, and Eq. 7 gives v̂_t = Σ_{j=−10}^{10} c_j v_{t+j}. If it is implemented as written, the smoothed inputs contain up to 10 future snapshots (1 s at 100 ms). This is the same scale as the prediction horizons (100–1000 ms). The paper does not state that a causal one-sided version was used.
- **Stated limitations:**
  - Single day only; "further testing on additional days … is needed".
  - Offline only.
  - The Kalman filter is under-tuned.

---

## Part 3 — Point-process (Hawkes/Cox) intensities as predictor inputs

### munitoke2020ratio — Muni Toke & Yoshida, *Quantitative Finance* 20(1):81–98 (2020). **FULL TEXT (arXiv 1805.06682v3)**
- **Model:** a ratio model of Cox-type intensities, multinomial-logit-like in covariates. Its inputs are book-state covariates (imbalance i, spread s, last trade sign) and optionally Hawkes-kernel history covariates (H_B, H_A), which are exponentially-weighted counts of past bid/ask market orders using parameters from a fitted Hawkes model.
- **Target:** the sign of the next market order. The model is calibrated on the previous trading day.
- **Data:** 36 Paris-listed stocks, 2015, about 54M trades.
- **Headline numbers (Fig. 8, all trades):**
  - "Last" sign gets >80%.
  - "Imbalance" gets about 70–80%.
  - Hawkes Full gets 73–78% and Hawkes NoCross about 75–80%.
  - Ratio(i, last sign, s) gets about 85%.
  - Ratio(H_B, H_A, i, last sign, s) is best, "improving … by a bit more than 2.5% in average".
- **Headline numbers (Fig. 9, sign changes only, about 20% of trades):**
  - The no-history ratio model gets about 40%.
  - Both Hawkes models are worse than imbalance alone.
  - The combined Hawkes + state ratio model beats the best Hawkes model by "more than 13%".
- **Limitation stated:** it is "a theoretical exercise" that predicts with all information just before the trade, "not taking into account latency, information delays, reaction times".

### fabre2025spoof — Fabre & Challet, arXiv 2504.15908 (2025). **FULL TEXT (arXiv v1)**. Preprint
- **Inputs:** multi-scale exponential-kernel ("Hawkes-inspired") order-flow variables, weighted by order size and by posting distance from the best quote.
- **Model:** a probabilistic NN that outputs the parameters of the distribution of the 1-s mid move.
- **Data:** Coinbase L3 BTC-USD and ETH-USD, 1–7 Dec 2022, with a time-ordered train/validation split of 1M + 1M orders.
- **Application:** spoofing detection. The abstract says "31% of large orders could spoof the market" for 2024-12-04 to 2024-12-07; that period differs from the training data dates.
- **No ablation:** no quantitative comparison of Hawkes-feature inputs vs plain-LOB inputs was found in the text. The claim that posting distance is "critical" rests on fitted-model diagnostics, not on an ablation table.

### cestari2025hawkes — Cestari, Barchi, Busetto, Marazzina, Formentin, *ECC 2025*, 1943–1948 (arXiv 2312.16190). **FULL TEXT (arXiv v1)**
- **Pipeline:** a Hawkes model predicts the *time* of the next LOB event. A continuous-time output-error (COE) model uses base imbalance as a regressor and predicts the return sign at that time.
- **Benchmarks:** Oracle (true next-event time), Naive (+1 s) and moving average.
- **Data:** USDT/USD, 50 validation scenarios of 2 min each. No transaction costs. Zero-return events are removed.
- **Results:** accuracy and profit are shown **only in figures** (Figs. 5–6). The ordering is Oracle > Hawkes > MA > Naive. Exact numbers are **UNVERIFIED** (not in the text).

### raffaelli2026mhp — Raffaelli, Cestari, Marazzina, Formentin, *Decisions in Economics and Finance* (online 17 Apr 2026). **ABSTRACT ONLY**
- BTC/USD LOB event streams. A multivariate Hawkes model combined with COE ("hybrid MHP–COE") "consistently outperforms the pure Hawkes-based model in both prediction accuracy and simulated trading profitability".
- Numbers: **UNVERIFIED**.

### yadav2026hawkes — Yadav, Nagarjuna, Lal, *AIAI 2026* (IFIP AICT, Springer), 223–236. **METADATA ONLY** (Crossref)
- A search-engine snippet (not the paper) describes a cross-attention Transformer fusing LOB state with Hawkes intensity features, evaluated by a factorial ablation on 18 days of NVIDIA tick data. It reports that "Hawkes content adds incremental accuracy".
- The abstract could not be retrieved from Springer (redirect to login), so the content is **UNVERIFIED** and no numbers are given.

---

## Part 4 — Ensembles / stacking of LOB models

### zhanglimzohren2021mbo — Zhang, Lim, Zohren, *Applied Mathematical Finance* 28(1):79–95 (2021). **FULL TEXT (arXiv 2102.08811v2)**
- **Inputs:** MBO messages (normalized) vs 10-level LOB snapshots, both derived from the same LSE feed.
- **Data:** 5 LSE stocks (LLOY, BARC, TSCO, BT, VOD), all of 2018. Split: 6 months train, 3 validation, 3 test (>46M test observations).
- **Horizons:** k ∈ {20, 50, 100}.
- **Ensembles:** equal-weight averages, not a learned meta-model:
  - Ensemble-MBO = MBO-LSTM + MBO-Attention;
  - Ensemble-LOB = LOB-LSTM + CNN + DeepLOB;
  - Ensemble-MBO-LOB = both.
- **Headline numbers (Table 4, F1 %):**

  | Model | k=20 | k=50 | k=100 |
  |---|---|---|---|
  | LOB-DeepLOB | 68.40 | 64.79 | 61.10 |
  | Ensemble-LOB | 68.31 | 65.23 | 60.56 |
  | Ensemble-MBO-LOB | 69.02 | 65.34 | 61.82 |
  | linear (LOB-LM) | 42.38 | 41.13 | 41.80 |

  The best single MBO model is weaker (k=20: MBO-LSTM 61.75 / MBO-Attention 61.73).
- **Mechanism claimed:** MBO and LOB signals are less correlated (Fig. 3), so the combination diversifies. The gain over DeepLOB alone is +0.62 / +0.55 / +0.72 F1 points for k = 20 / 50 / 100.

### prata2024 — LOBCAST (existing key), METALOB stacking result. **FULL TEXT (arXiv 2308.01915v2)**
- **Meta-learner:** METALOB is an MLP with two layers, fed the 3-class probability outputs of all 15 base DL models (45 inputs).
- **Out-of-fold check:** the meta-learner is trained on 70% of the base models' *test* split, validated on 15% and tested on the remaining 15%. Its training data is therefore out-of-sample for the base models.
- **Comparison baseline:** MAJORITY is an F1-weighted vote.
- **Headline numbers (Table 2, F1):**

  | Dataset | METALOB | MAJORITY | Best single |
  |---|---|---|---|
  | FI-2010 | 82.2 ± 7.3 | 60.0 ± 12.7 | BINCTABL 82.6 ± 7.0 |
  | LOB-2021 | 55.9 ± 2.6 | — | BINCTABL 61.2 |
  | LOB-2022 | 53.2 ± 1.5 | — | DeepLOB 59.5 |

- **Paper's conclusion:** "ensemble models … do not exceed the performance of the top-performing models, which is probably due to the relatively high agreement rate among systems". On FI-2010, METALOB agreed with BINCTABL 82.8% of the time.
- **Caveat:** METALOB is scored on a 15% subset of the test period, not the same set as the base models.

### bileki2022 — Bileki, Barboza, Silva, Bonato, *Applied Soft Computing* 116:108274 (2022). **ABSTRACT ONLY**
- **Pipeline:** a two-stage stack. A CNN extracts features from a price-aggregated order book (B3 Brazil, from MBO data). CatBoost then combines the CNN features with Times-and-Trades events that include broker IDs.
- **Abstract:** "improving accuracy by 8% when compared to a common CNN, where 5% is only due to the adoption of CatBoost and another 3% is due to the combination of features from CNN with TTinfo". Only CatBoost needs retraining.
- Whether "8%" is absolute or relative, plus horizon and protocol: **UNVERIFIED**.

---

## Part 5 — OFI-input vs raw-input comparisons (existing keys; adds to claims_dl.md)

### kolm2023 — **ABSTRACT ONLY** (as in claims_dl.md; Wiley/SSRN full text still not accessible on 2026-10-03)
- Abstract: "models trained on order flow significantly outperform most models trained directly on order books". "Off-the-shelf" networks on "stationary inputs".
- **Exact OFI-vs-raw numbers: UNVERIFIED.**

### lucchese2024 — **FULL TEXT (arXiv 2211.13777v3)**
Same CNN–Inception–LSTM backbone throughout; only the representation changes. Results are the % of (stock, horizon) cells where the model is in the Model Confidence Set, at α = 0.01 / 0.05.

| Setting (table) | deepLOB(L1) | deepOF(L1) | deepLOB(L2) | deepOF(L2) | deepVOL(L2) | deepVOL(L3) |
|---|---|---|---|---|---|---|
| Stock-specific (Table 5) | 11 / 7 | 22 / 23 | 11 / 5 | **89 / 88** | 84 / 65 | 86 / 77 |
| Universal (Table 9) | 0 / 0 | 0 / 0 | 24 / 8 | 62 / 42 | 81 / 71 | **100 / 92** |

- **Seq2seq (Table 7):** deepOF(L2) 97% (α = 0.01) vs deepLOB(L2) 39%.
- **Simple AR baseline (Table 11, α = 0.01):** a 3×3 empirical transition-matrix AR model on past return labels is in the MCS 28% of the time, vs deepOF(L2) 81%. Table 11 lists "deepOF(L1)" twice (12% and 26%); one row is presumably deepLOB(L1), which is a typo in the source.
- **Authors' caveat:** "a more in-depth study comparing these specifications to the models in Aït-Sahalia et al. (2022) is required to shed some light on … whether the expressivity of deep learning techniques is the key … or if careful feature engineering can be as effective."

### contcucuringu2023 — model class **VERIFIED (arXiv 2112.13213v4)**
- Integrated OFI and cross-asset OFI enter **linear** models: OLS for price impact and ARs, LASSO for cross-impact.
- There are no nonlinear ML learners. The paper cites Aït-Sahalia et al. for LASSO/tree methods, but does not use them. Numbers are in claims_classic.md (C8).

---

## Part 6 — Stacking and time-series validation protocol

### wolpert1992 — *Neural Networks* 5(2):241–259. **METADATA ONLY**
Its content is described here only through Breiman (1996). Breiman credits it as the origin of stacking.

### breiman1996stack — *Machine Learning* 24(1):49–64. **FULL TEXT (Berkeley tech-report PDF of the journal article)**
- Stacking uses "cross-validation data and least squares under non-negativity constraints".
- On leakage: if the base predictors are built on the learning set and the combination weights are also fitted on it, "the resulting {a_k} will overfit the data — generalization will be poor. This problem can be fixed by using the level-one cross-validation data."
- 10-fold CV level-one data was "more effective than the leave-one-out level one data".
- "Stacking never does worse than selecting the single best predictor." The biggest gains come when the stacked predictors are dissimilar.

### burman1994 — *Biometrika* 81(2):351–358. **ABSTRACT ONLY**
h-block CV for stationary dependent data: it removes "the h observations preceding and following the observation in the test set".

### racine2000 — *Journal of Econometrics* 99(1):39–61. **METADATA ONLY**
hv-block CV, according to its title. Content is **UNVERIFIED**.

### bergmeir2012 — *Information Sciences* 191:192–213. **METADATA ONLY**
Content is **UNVERIFIED**.

### cerqueira2020 — *Machine Learning* 109(11):1997–2028. **ABSTRACT (arXiv 1905.11744v1)**
- 62 real and 3 synthetic series.
- "cross-validation approaches can be applied to stationary time series. However, in real-world scenarios, when different sources of non-stationary variation are at play, the most accurate estimates are produced by out-of-sample methods that preserve the temporal order of observations."

### lopezdeprado2018 (existing key)
Purged k-fold CV and MDI/MDA are applied in easley2021 as described above. The book itself was not re-read here.

---

## Part 7 — Synthesis (verified facts only)

1. **Engineered/classical inputs vs learned-from-raw inputs, same learner.** Every head-to-head I could verify favours engineered or transformed inputs. The magnitudes are modest and the data are small.
   - **tsantekidis2020stationary:** stationary price-difference features beat raw prices for every DL model. LSTM F1 at k=100 goes 0.34 → 0.44, and MLP 0.26 → 0.39. Data: 5 Nordic stocks, 10 days.
   - **ntakaris2019access, nousi2019:** handcrafted sets beat autoencoder or BoF representations. Nousi MLP F 56.06 (`concat`) vs 30.11 (AE) at Nα=5.
   - **lucchese2024:** with the architecture held fixed, order-flow (deepOF) and volume (deepVOL) inputs are in the 99% MCS far more often than raw-level inputs (89% vs 11% for L2 stock-specific models).
   - **kolm2023:** abstract only; its numbers remain UNVERIFIED.
2. **Given good features, the learner matters little.**
   - **aitsahalia2026:** LASSO, RF, GBT, ridge and NNs give "very similar performance". OLS fails.
   - **easley2021:** logistic regression on six microstructure measures is "typically … slightly more accurate" than a random forest.
   - **wang2025cryptolob** (preprint, single day, possible look-ahead in the smoothing): logistic and XGBoost are within 1–2% of DeepLOB.
   - **briola2020** (claims_dl.md): an MLP matches CNN-LSTM, but logistic regression trails.
3. **Point-process intensities as inputs.** The strongest verified evidence is **munitoke2020ratio**:
   - Hawkes history covariates plus book-state covariates in one logit-type model beat both Hawkes-only and state-only predictors of next-trade sign.
   - The gain is about +2.5% overall and >13% on sign changes vs the best Hawkes model.
   - The setting is idealized: no latency.

   Other Hawkes-feature papers (fabre2025spoof, cestari2025hawkes, raffaelli2026mhp, yadav2026hawkes) give no extractable ablation numbers, or I could only read their abstracts.
4. **Classical model + learned component.**
   - **guo2018btcvol:** a gated mixture of an AR volatility model and an order-book-feature model has the lowest RMSE in 10 of 12 months, against baselines including GARCH, ARIMAX, RF, XGBoost and LSTM. Adding the same features *linearly* (ARIMAX) raised ARIMA's RMSE in all 12 months.
   - **rahimikia2020ml** (abstract): ML-in-ensemble improves HAR, while ML used as a direct substitute is unreliable in extreme-volatility periods.
5. **Stacking deep LOB models gives little.**
   - **prata2024:** a learned meta-MLP (METALOB) never beat the best base model, and lost by 5.3 (LOB-2021) and 6.3 (LOB-2022) F1 points out of FI-2010. The authors attribute this to high agreement among base models.
   - **zhanglimzohren2021mbo:** equal-weight averaging of *dissimilar* inputs (MBO + LOB) gave +0.55 to +0.72 F1 over DeepLOB.
   - **bileki2022** (abstract): a CNN → CatBoost two-stage stack reports +8% accuracy.

   This is consistent with breiman1996stack: gains come from dissimilar base predictors.
6. **Protocol.** Several papers use leakage-aware protocols:
   - easley2021: 10-fold purged CV;
   - aitsahalia2026: rolling 5-day train with a separate tuning block;
   - bieganowski2026: purged walk-forward;
   - prata2024: meta-learner trained on base-model test data.

   Breiman (1996) states the general requirement: combination weights must be fitted on cross-validated (level-one) predictions, or the combination overfits.

   Specific protocol issues found:
   - tsantekidis2020stationary reports test metrics averaged over the last 20 training epochs, with no stated validation set;
   - wang2025cryptolob's smoothing is written as non-causal;
   - nousi2019 Table 7 contains an impossible F-score.
7. **Caution on transferability.**
   - aitsahalia2026 finds transaction-derived variables more useful than LOB-state variables for return prediction.
   - easley2021 finds MDI (in-sample) and MDA (out-of-sample) rankings disagree.

   Feature-importance claims therefore depend on the evaluation protocol.

---

## Part 8 — UNVERIFIED items

- **kolm2023:** all numeric OFI-vs-raw comparisons, horizon grid and baselines (full text inaccessible).
- **kercheval2015:** data, horizons, accuracy numbers and exact feature list. The feature list is known only second-hand via nousi2019.
- **zheng2013jump:** train/test split for the reported out-of-sample AUC ≈ 0.80.
- **palguna2016:** which features and predictors were used.
- **tashiro2019:** all numbers.
- **bileki2022:** whether "+8%" is absolute or relative; horizon and protocol.
- **cestari2025hawkes:** numeric accuracy and profit (figures only).
- **raffaelli2026mhp:** all numbers.
- **yadav2026hawkes:** entire content. Only a search-engine snippet was seen; the abstract was not retrieved.
- **rahimikia2020ml:** methods and numbers beyond the abstract. Its peer-review status is unknown (SSRN WP).
- **racine2000, bergmeir2012, wolpert1992:** content (metadata only).
- **aitsahalia2026:** whether the Management Science version's numbers match NBER WP 30366 (only the WP was read), and the 4th-author discrepancy (Zhou vs Zhu).
- **easley2021:** whether the RFS version's numbers match the 2019 SSRN draft.
- **bieganowski2026:** the IR** metric definition and the backtest figures (not reconciled; do not cite).
- **Akyildirim, Sensoy, Gulay, Corbet, Salari (2021),** "Big data analytics, order imbalance and the predictability of stock returns", *J. Multinational Financial Management* 62:100717, doi 10.1016/j.mulfin.2021.100717. Metadata was confirmed on Crossref. No abstract or full text could be retrieved, so it was **not added to the bib**.
- **Kolm & Westray (2024),** "Improving Deep Learning of Alpha Term Structures from the Order Book", SSRN 4770476. Metadata only and no abstract was retrievable, so it was **not added to the bib**.
- I found **no** peer-reviewed paper that feeds queue-model outputs (Cont–Stoikov–Talreja, Cont–de Larrard or queue-reactive probabilities) into an ML classifier as features. This is absence of evidence from my searches (arXiv API, Crossref, web search), not proof that none exists.
