# Claims & paper notes — neural networks as forecast COMBINERS (neural stacking / learned ensemble weighting)

Question: have neural networks been used to combine the forecasts of several models, which architectures, and do they beat the
simple average and linear combinations?

Read levels: FULL TEXT = journal or author/preprint PDF read; ABSTRACT ONLY = abstract from OpenAlex/Crossref/arXiv/publisher;
METADATA ONLY = bibliographic record only. Anything not read is marked UNVERIFIED.
Items already verified in claims_mix.md / claims_meta.md (breiman1996stack, wolpert1992, ting1999, jacobs1991, jordan1994hme,
bates1969, stockwatson2004, claeskens2016, guo2018btcvol, antulov2021tme, zhanglimzohren2021mbo, prata2024, wang2023review ...)
are referenced, not re-verified, except where noted.

Search date: 2026-10-03.

## A. 1990s ANN forecast combiners (finance and economics)

### donaldson1996annc — Donaldson & Kamstra, "Forecast combining with neural networks", *J. Forecasting* 15(1):49–61 (1996). DOI 10.1002/(SICI)1099-131X(199601)15:1<49::AID-FOR604>3.0.CO;2-2. **FULL TEXT** (journal scan from markkamstra.com, OCR)
- **Combiner:** single-hidden-layer feed-forward ANN, eq. (1): F_t = b0 + sum_{j<=k} b_j f_{j,t} + sum_{i<=p} d_i * logistic(z_t' g_i),
  inputs z_{j,t} = (f_{j,t} - mean)/sd of the two component forecasts only (no extra features). Output = the combined forecast
  directly (not weights). k in {0,2} linear terms, p in {0,1,2,3} logistic nodes; hidden weights g drawn uniformly in [-1,1]
  (10 random draws), only b and d estimated; 61 specifications incl. the pure linear one; choice by ten-fold cross-validation on
  the in-sample combining window. Selected: SP500 (0,1), NIKKEI (0,2), TSEC (1,1), FTSE (0,1).
- **Components:** two daily volatility forecasts: MA-variance model (MAV, window by Schwarz) and GARCH(1,1).
- **Data:** daily returns, S&P 500, Nikkei, TSE Composite, FTSE; 1969–Sep 1987 (stopped before the Oct 1987 crash).
- **Protocol:** component forecasts are one-step-ahead OUT-OF-SAMPLE (rolling fixed-length window, re-estimated daily from
  Jan 1980). Combiner weights estimated on component forecasts 1 Jan 1980 – 21 Jun 1983, then recursively updated daily;
  evaluation 22 Jun 1983 – 30 Sep 1987, out-of-sample for the combiner.
- **Baselines:** simple average (AVE), OLS linear combination, MAD (least-absolute-deviation) linear combination, both components.
- **Results (Table II, RMSE x1e-4, out-of-sample):** SP500 MAV 1.36, GAR 1.35, AVE 1.35, OLS 1.35, MAD 1.42, ANN 1.35;
  NIKKEI 1.17/1.16/1.16/1.16/1.23/1.17; FTSE 1.30/1.24/1.25/1.23/1.31/1.24; TSEC 0.66/0.77/0.68/0.67/0.67/0.67.
  i.e. RMSE of the ANN equals OLS on SP500 and TSEC and is 0.01 worse than OLS on NIKKEI and FTSE; text: "the OLS and ANN combined
  forecasts share common performance characteristics". RMAE: MAD lowest everywhere; ANN = OLS on SP500 and NIKKEI.
- **Their claim of superiority rests on encompassing tests (Table III, Chong–Hendry, 1% level):** "ANN is the only model whose
  forecast is not encompassed by at least one other forecast in at least one index"; ANN encompasses AVE in SP500, FTSE, TSEC;
  encompasses OLS in TSEC; encompasses MAD in NIKKEI.
- Explanation offered (Fig. 2, TSEC): ANN surface lies above OLS when both inputs are small or either very large, below OLS where
  the two forecasts agree; most points sit where the two surfaces intersect.
- Stated caveat (fn. 8): in practice one combines only when the forecasters' information sets are unavailable; here they are
  available, so the exercise "should therefore be viewed primarily as an exercise to compare the combining methods".

### donaldson1999interact — Donaldson & Kamstra, "Neural network forecast combining with interaction effects", *J. Franklin Institute* 336(2):227–236 (1999). DOI 10.1016/S0016-0032(98)00018-0. **FULL TEXT** (author-site scan, OCR)
- No new experiment: same ANN combiner (eqs. 2–4, two standardized forecasts in, combined forecast out) and the same S&P 500
  MAV/GARCH volatility data as donaldson1996annc / harrald1997; explains graphically why the ANN differs from OLS.
- Its own summary of the two earlier papers (Sec. 4): "all the models attain a satisfactory level of performance both in- and
  out-of-sample, that the RMSFE and MAFE do not give a clear ranking of the models, and that the encompassing tests clearly favour
  the ANN models."
- Mechanism claimed: interaction effects — the ANN can "turn off" one forecast in favour of the other in a state-dependent way
  (Fig. 1 surface fitted on 1969–1979; Fig. 2, Oct 1974: ANN combined forecast 6.5 on 9 Oct vs OLS 4.5, falls to ~3 by 16 Oct while
  MAV, GARCH and OLS decline over several weeks). Footnote 10: superiority "cannot ... be resolved with inspection of such figures".

### harrald1997 — Harrald & Kamstra, "Evolving artificial neural networks to combine financial forecasts", *IEEE Trans. Evolutionary Computation* 1(1):40–52 (1997). DOI 10.1109/4235.585891. **FULL TEXT** (author-site PDF; Tables I–III are images, numbers not extractable — table values **UNVERIFIED**)
- **Combiner:** the Donaldson–Kamstra ANN form (their eqs. 2–4): two standardized component forecasts as the only inputs, a hidden layer
  of three sigmoid nodes plus direct input-to-output links and a bias; output = combined forecast. Hidden weights found by
  evolutionary programming (EP-NN: 29 independent 1000-generation runs; best/median/worst in-sample MSE reported as EP-NN(B/M/W)) or
  self-adaptive EP (SEP-NN); output-layer coefficients by OLS inside the EP loop.
- **Components:** MAV (moving-average variance) and GARCH(1,1) one-step volatility forecasts, S&P 500 daily, Apr 1969 – Sep 1987.
- **Protocol:** combiners (and kernel window width) trained "once and once only" on 1969–1979 using the *in-sample* MAV and GARCH
  forecasts; out-of-sample period Jan 1980 – Sep 1987 with rolling re-estimated components. So the level-one training data are
  in-sample fitted component forecasts, not out-of-sample ones.
- **Baselines:** simple average, OLS combination, Gaussian-kernel nonparametric combination, the two components.
- **Results (text, Sec. VII-B):** out of sample, RMSFE of the EP-NN/SEP-NN combiners is "in the middle of the pack, with lower
  RMSFE's than the MAV method, the worst performer, and not much higher RMSFE's than the best performer GARCH"; kernel "has the best
  MAFE measure on the out-of-sample data". I.e. on squared error the single GARCH model beat every combiner.
- **Encompassing (Table III, text):** SEP-NN out-of-sample forecasts encompass all linear methods and the kernel "at the 5% level of
  significance or better" (coefficient significant at 0.1% vs MAV and vs average, 1.3% vs GARCH, 1.7% vs kernel, 4.9% vs OLS),
  never itself encompassed. OLS encompasses five of eight alternatives at 5%; average encompasses none. EP-NN(B) (best in-sample
  MSE) did "somewhat worse" out of sample — authors: "some problems with overfitting even if the neural network architecture includes
  only three nodes".
- Stated caveat (fn. 11): with access to the forecasters' information sets, combining is inefficient; exercise compares combining methods.

### shi1996annc — Shi, Xu & Liu, "Applications of artificial neural networks to the nonlinear combination of forecasts", *Expert Systems* 13(3):195–201 (1996). DOI 10.1111/j.1468-0394.1996.tb00119.x. **ABSTRACT ONLY** (Crossref/OpenAlex)
- Abstract: ANN "as a tool for nonlinear combination of forecasts"; "three forecasting models are used for individual forecasts, and
  then two linear combining methods are used to compare with the ANN combining method"; "the prediction by the ANN method
  outperforms those by linear combining methods" on "real-world data".
- Architecture, data, whether simple average was a baseline, numbers: **UNVERIFIED**.
- Precursor: Shi & Liu, "Nonlinear combination of forecasts with neural networks", Proc. IJCNN-93 Nagoya, vol. 1, 959–962 (1993),
  DOI 10.1109/IJCNN.1993.714070 — ABSTRACT ONLY: "the nonlinear combination of forecasts with neural networks is an effective way for
  combining forecasts". No details. (No bib entry written for the 1993 paper.)

### palit2000 — Palit & Popović, "Nonlinear combination of forecasts using artificial neural network, fuzzy logic and neuro-fuzzy approaches", *Proc. 9th IEEE Int. Conf. Fuzzy Systems (FUZZ-IEEE 2000)*, vol. 2, 566–571. DOI 10.1109/FUZZY.2000.839055. **ABSTRACT ONLY** (OpenAlex)
- Abstract: nonlinear combination of time series forecasts with neural networks, fuzzy logic and neuro-fuzzy systems; "on some
  practical examples" the nonlinear combination "is capable of producing a single better forecast than any individual forecasts
  involved in the combination". Comparison to simple average / linear combination: **UNVERIFIED** (not stated in abstract).

### babikir2016 — Babikir & Mwambi, "Evaluating the combined forecasts of the dynamic factor model and the artificial neural network model using linear and nonlinear combining methods", *Empirical Economics* 51(4):1541–1556 (2016). DOI 10.1007/s00181-015-1049-1. **METADATA ONLY** (Crossref; Springer page behind redirect, no abstract in OpenAlex)
- Described in wang2023review (FULL TEXT, arXiv 2205.04216v2, Sec. 2.3) together with Krasnopolsky & Lin (2012) as employing "neural
  network approaches with various activation functions to approximate the nonlinear dependence of individual forecasts". Own content
  **UNVERIFIED**.

### wang2023review (existing key) — what it says about NN combiners (FULL TEXT of arXiv 2205.04216v2, Sec. 2.3 "Nonlinear combinations" and 2.4)
- "Neural networks are often employed to estimate the nonlinear mapping ... The design of a neural network model is nevertheless
  time-consuming, and sometimes leads to overfitting and poor forecasting performance as more parameters need to be estimated."
- On Donaldson–Kamstra 1996, Harrald–Kamstra 1997 ("only using a single time series"), Krasnopolsky & Lin 2012, Babikir & Mwambi 2016:
  "The empirical results of nonlinear combinations from these studies generally dominate those from traditional linear combination
  strategies, such as simple average, OLS weights, and performance-based weights. However, the empirical evidence provided is based on
  fewer than ten time series, possibly hand-picked to lead to this result." Also lists drawbacks: neglect of error correlations,
  unstable parameter estimates, multicollinearity; "the performance of nonlinear combinations relative to linear combinations needs
  further investigation."
- Sec. 2.4 lists ANN / wavelet NN / SVR / LASSO as series-by-series stacking meta-models; cross-learning NN combiners: Zhao & Feng
  2020 (For2For), Ma & Fildes 2021.

## B. Cross-learning neural meta-learners over many series (M4 / retail)

### montero2020fforma — Montero-Manso, Athanasopoulos, Hyndman & Talagala, "FFORMA: Feature-based forecast model averaging", *Int. J. Forecasting* 36(1):86–92 (2020). DOI 10.1016/j.ijforecast.2019.02.011. **FULL TEXT of the working-paper version** (Monash EBS WP 19/18, 16 Jan 2019). CONTRAST: the combiner is gradient-boosted trees, NOT a neural network.
- **Combiner:** xgboost on 42 tsfeatures features of each series (Table 1; domain labels deliberately not used) -> one score per
  method -> softmax -> combination weights. Custom objective: weighted average of the per-method OWA losses, L_bar_n = sum_m w_m L_nm
  (i.e. expected loss if a method were drawn at random with probabilities w; Sec. 4 notes this differs from the final weighted-average
  forecast). Hyper-parameters by Bayesian optimisation on a 10% holdout.
- **Components (9):** naive, rwf with drift, snaive, theta, auto.arima, ets, tbats, stlm-ar, nnetar.
- **Protocol:** each M4 series split into a training period and a test period of length h; methods fitted on the training period and
  their errors over the test period form the meta-data -> level-one data OUT-OF-SAMPLE (temporal holdout).
- **Results (Sec. 4, text):** second place in M4 (point forecasts and intervals). Model selection with the same features/pool/xgboost
  (cross-entropy) had 10% larger average OWA; "simple averaging ... produces a 14% increase in error for the same pool of methods".
  Weight clusters: ~40% of series get a near-equal-weight profile, ~60% one dominant method. Removing any one method raised error
  (max +1%, removing rwf-drift).

### mafildes2021 — Ma & Fildes, "Retail sales forecasting with meta-learning", *European J. Operational Research* 288(1):111–128 (2021). DOI 10.1016/j.ejor.2020.05.038. **FULL TEXT** (Lancaster eprint, author version Jan 2020 rev. Mar 2020)
- **Combiner (M0):** "Double Channel Convolutional Neural Network" (DCCNN). Channel 1 = raw sales series, channel 2 = multivariate
  series of influential factors (price, display, feature, calendar) incl. the future horizon; each channel 3 temporal conv blocks
  (filters 64/128/64, ReLU, squeeze-and-excite on the first two), global average pooling, concatenation, dropout 0.8, dense softmax
  -> M combination weights (sum to 1). Output = weights; combined forecast = sum_m w_m * yhat_m (eq. 6). Inputs: raw series + factors
  (NOT the base forecasts themselves; these enter only in the weighted sum). Loss: scaled MSE of the combined forecast (eq. 7–8),
  i.e. trained end-to-end on combined-forecast error (they contrast this with FFORMA, which minimises combined *errors* of bases).
  Adam, batch 4096, 50 epochs, Keras/TensorFlow.
- **Components (9):** individual-series ETS, ADL-1, ARX-1, ELM-1, SVM-1; pooled ADLP-3, RF-7, GBRT-7, ELMP-7.
- **Data:** IRI weekly scanner data, 6 categories, 100 stores; 153 weeks; 15 rolling slots of 55 weeks (48 fit + 7 forecast); 10 slots
  train (83,944 series) / 5 test (36,194 series); horizon h = 1–7 weeks.
- **Protocol:** in each slot the base forecasters are fitted on 48 weeks and the meta-learner's targets are their forecasts of the next 7
  weeks, so level-one inputs are OUT-OF-SAMPLE base forecasts; meta-learner trained on 8 slots, validated on 2, tested on 5 later slots.
- **Baselines:** E1 equal-weight average of the 9; E2 softmax performance weights (Cerqueira et al. 2017); E3 equal-weight top-4;
  FFORMA1/2 (xgboost, hand features, same pool and SMSE loss); M1 (3-layer FC NN on hand features -> softmax weights); M2 (sales-only
  DCCNN); M3/M4 (individual-only / pooled-only pools); M5 (DCCNN selecting single best, cross-entropy); M6 (predict abs errors ->
  softmax(-e) weights). "OLS and constrained regression weights were also examined but performed poorly" (no numbers given).
- **Results (Table 8, test, h = 1–7; AvgRelMAE relative to best single base GBRT-7):** M0 sMAPE 16.849 / AvgRelMAE 0.968; M1 16.865 /
  0.970; M2 16.870 / 0.970; FFORMA2 16.928 / 0.974; FFORMA1 16.975 / 0.977; E2 16.994 / 0.978; M6 17.006 / 0.979; M4 17.053 / 0.982;
  E1 (simple average) 17.078 / 0.986; M3 17.231 / 0.995; E3 17.247 / 0.996; M5 (selection) 18.135 / 1.050. Best single base GBRT-7
  test sMAPE 17.301 (Table 7). So the CNN combiner beat the simple average by ~1.8% AvgRelMAE and the best single model by ~3.2%.
- Nemenyi test (Fig. 9): M0 not significantly better than M1 and M2; M0, M1, M2 significantly better than all others.
- Abstract-stated limitations: "accuracy gains over some more sophisticated meta ensemble benchmarks are modest and the learnt features
  lack interpretability". Selecting a single model (M5) was worse than several base forecasters; mixed pools (individual + pooled)
  mattered (M3, M4 worse).

### zhao2020for2for — Zhao & Feng, "For2For: Learning to forecast from forecasts", arXiv 2001.04601v1 (Jan 2020). **FULL TEXT**. Preprint; no journal version found.
- **Combiner:** NN whose only inputs are the h x M matrix of base forecasts (no series features; domain type deliberately not used).
  (a) CNN: linear 1 x M "combination" path plus a ResNet-like residual path (four 3x3 conv layers, sigmoid, then FC) -> h outputs;
  (b) LSTM run over the forecast horizon, one step per horizon point, state carries earlier horizon points -> linear -> scalar.
  Output = the forecast directly (no explicit weights). 8 independently trained instances averaged.
- **Components (8):** rwf with drift, snaive, theta, auto.arima, ets, tbats, stlm-ar, nnetar (FFORMA pool minus naive).
- **Data:** M4 (100,000 series; yearly, quarterly, monthly, weekly, daily, hourly). MAE loss on log, last-observation-normalised values.
- **Protocol:** last h observations of each series held out; base models fitted on the truncated series; their forecasts of the
  held-out h points are the NN's training inputs -> level-one data OUT-OF-SAMPLE (temporal holdout); one training sample per series.
- **Results (Tables 2–4, OWA):** single RNN for all frequencies: total OWA 0.8345 vs FFORMA 0.838 and M4 winner 0.821 -> "would rank 2nd";
  quarterly 0.8414 (best submission 0.847); monthly 0.8492 (winner 0.836); weekly 0.8916 vs FFORMA 0.796; hourly 0.6501 vs FFORMA 0.484;
  daily 0.9968 vs FFORMA 1.019.
- Simple-average baseline: the introduction states the method "is more accurate than a simple arithmetic combination of the base models",
  but no table reports the simple average of these 8 bases — that number is **UNVERIFIED** in this paper.
- Stated limitation: on weekly (359 series) and hourly (414 series) "a simple linear model works better as it is less prone to
  overfitting ... a large sample size is crucial for NN models to work well".

### felici2026mtl — Felici & Sudoso, "Optimizing accuracy and diversity: a multi-task approach to forecast combinations", *Annals of Operations Research* 362(1–3):493–520 (2026; Crossref). DOI 10.1007/s10479-026-07291-x. **FULL TEXT** (arXiv 2310.20545v3, author version marked "Published at" that DOI)
- **Combiner (DNN-MTL):** two CNN subnetworks with the Ma & Fildes (2021) block design (3 temporal conv blocks, 64/128/64 filters,
  kernels 2/4/8, squeeze-and-excite, global average pooling) on the raw training-period series. Regression branch -> unnormalised
  weights; classification branch (sigmoid) -> which methods belong to an "accurate and diverse" subset (labels from a small QP using the
  error-correlation matrix); softmax(element-wise product) -> convex combination weights. Inputs: raw series (base forecasts enter only
  in the weighted sum). Output: weights. Loss: MAE of the combined forecast scaled by the MAE of the simple average (eq. 2) + lambda x
  binary cross-entropy.
- **Components (9):** auto.arima, ets, nnetar, tbats, stlm-ar, rwf-drift, theta, naive, snaive.
- **Data:** M4 yearly, quarterly, monthly; LargeST road-traffic daily and hourly.
- **Protocol:** temporal holdout inside the in-sample period (Fig. 2): bases fitted on a training part, forecasts of the next H points
  are the meta-data -> level-one data OUT-OF-SAMPLE; final out-of-sample period never used for tuning, labels or training.
- **Baselines:** AVERAGE (equal weights), CLS-REG (constrained least squares, Conflitti et al. 2015), FFORMA, FFORMA-DIV (Kang et al.
  2022), ablations DNN-REG, DNN-REG-DIV, DNN-MTL(lambda=0).
- **Results (Tables 4–5, OWA):** M4 yearly AVERAGE 0.949, CLS-REG 0.946, FFORMA 0.799, FFORMA-DIV 0.798, DNN-REG 0.801, DNN-MTL 0.794;
  quarterly 0.916 / 0.912 / 0.847 / 0.842 / 0.845 / 0.833; monthly 0.911 / 0.909 / 0.858 / 0.851 / 0.862 / 0.846. LargeST daily
  1.267 / 1.017 / 0.804 / 0.803 / 0.792 / 0.771; hourly 0.975 / 0.507 / 0.483 / 0.478 / 0.422 / 0.411.
  Note: the regression-only CNN combiner (DNN-REG) was slightly WORSE than FFORMA/FFORMA-DIV on M4 yearly and monthly OWA and only
  matched it on quarterly; it was better on LargeST. Text: "the constrained least squares regression approach performs slightly better
  than the simple average"; MCB-Nemenyi: DNN-MTL significantly different from the other meta-learners (Fig. 3–4).

### lemke2010 — Lemke & Gabrys, "Meta-learning for time series forecasting and forecast combination", *Neurocomputing* 73(10–12):2006–2016 (2010). DOI 10.1016/j.neucom.2009.09.020. **FULL TEXT** (Bournemouth accepted manuscript, Nov 2009)
- NN role: a feed-forward NN (1 hidden layer, 30 nodes) is one of three meta-learners used as a CLASSIFIER choosing one of four
  candidates (structural model, direct NN, variance-based pooling, regression combination) from series + diversity features. It is not
  a combiner. The combining meta-learner is "zoomed ranking" (k-NN style ranking -> convex rank-based weights over four methods).
- The paper itself considers only linear combinations: "Literature in the area of nonlinear forecast combination is quite sparse,
  which is probably due to the lack of evidence of success as stated in [43 = Timmermann 2006]."
- Data: NN3 (monthly, 111 series) and NN5 (daily cash-machine withdrawals). Table 3 (SMAPE, NN3 / NN5): simple average 17.5 / 32.2,
  trimmed 17.4 / 31.8, regression combination 20.4 / 27.5, pooling(3) 16.8 / 25.7. Table 8: NN meta-learner (selection) 17.0 / 27.3,
  decision tree 18.0 / 26.5, SVM 18.0 / 26.5, zoomed ranking best method 17.5 / 26.6, zoomed ranking combination 15.5 / 23.8.
  Caveat stated: Table 8 numbers "cannot be compared to the competition results, as the whole time series were used in the training set
  for building the models" (a look-ahead); under competition conditions (NN5 only) zoomed-ranking combination SMAPE 23.8.

## C. Finance: neural stacking / neural combination of volatility and return forecasts

### ramosperez2019 — Ramos-Pérez, Alonso-González & Núñez-Velázquez, "Forecasting volatility with a stacked model based on a hybridized Artificial Neural Network", *Expert Systems with Applications* 129:1–9 (2019). DOI 10.1016/j.eswa.2019.03.046. **FULL TEXT** (arXiv 2006.16383v2)
- **Combiner (Stacked-ANN):** feed-forward ANN, 2 hidden layers (20, 10 sigmoid units), linear output, Adam, RMSE loss, L2 penalty.
  Inputs = the three level-one forecasts (RF, gradient boosting, SVM) PLUS the last 30 realised 5-day volatilities (33 inputs). Output =
  the volatility forecast directly.
- **Components:** random forest, gradient-boosted trees, RBF SVM, each fed the last 30 5-day volatilities.
- **Data:** S&P 500 daily; target = 5-day forward realised volatility ("TRV"); five windows 2000–07, 2001–08, 2002–09, 2009–16, 2010–17,
  each evaluated on the following year (2008, 2009, 2010, 2017, 2018).
- **Protocol:** within each window, first 25% fits level-one models, next 50% trains the ANN on level-one forecasts made on data the
  level-one models did not see (blocked holdout -> OUT-OF-SAMPLE level-one data), last 25% used for hyper-parameter choice; comparison
  year is separate.
- **Baselines:** plain ANN, ANN-GARCH(1,1), ANN-EGARCH(1,1), Heston. NO simple average of RF/GB/SVM, NO linear stacking, and NO RMSE of
  the individual RF/GB/SVM components are reported.
- **Results (Table 6, RMSE):** Stacked-ANN 0.01192 / 0.00534 / 0.00494 / 0.00254 / 0.00544 (2008/09/10/17/18) vs best benchmark
  ANN-EGARCH or ANN-GARCH 0.01332 / 0.00584 / 0.00537 / 0.00263 / 0.00571. Only Stacked-ANN passed Kupiec, AS1, AS2 (95%) in every
  year (Table 7). Whether the gain comes from combining or from the extra lagged inputs is not separable from the reported tables.

### kimwon2018 — Kim & Won, "Forecasting the volatility of stock price index: A hybrid model integrating LSTM with multiple GARCH-type models", *Expert Systems with Applications* 103:25–37 (2018). DOI 10.1016/j.eswa.2018.03.002. **ABSTRACT ONLY** (Semantic Scholar API)
- Hybrid, not a pure combiner: LSTM whose inputs include outputs/parameters of one to three GARCH-type models (GARCH, EGARCH, EWMA)
  together with other variables (exact input list **UNVERIFIED**). KOSPI 200.
- Abstract: GEW-LSTM (LSTM + three GARCH-type models) "has the lowest prediction errors in terms of MAE, MSE, HMAE, and HMSE"; MAE 0.0107,
  "37.2% less than that of the E-DFN (0.017)" (best existing); MSE, HMAE, HMSE smaller by 57.3%, 24.7%, 48%.
- Comparison to a simple average or linear combination of the GARCH forecasts: not mentioned in abstract — **UNVERIFIED**.

### barnwal2019stack — Barnwal, Bharti, Ali & Singh, "Stacking with Neural network for Cryptocurrency investment", arXiv 1902.07855v2 (Feb 2019). **FULL TEXT**. Preprint, not peer-reviewed.
- **Combiner:** feed-forward NN with one hidden layer of 6 nodes, log-loss, taking the 0/1 predictions of the level-0 classifiers
  (text: "Level 0 has 7 models"; Fig. 2 lists XGBoost, LightGBM, elastic-net logit, RF, naive Bayes, LDA, QDA; abstract says 3 generative +
  6 discriminative). Inputs = component predictions only. Output = direction probability.
- **Data:** Bitcoin daily close-to-close direction, Aug 2017 – Jul 2018; technical indicators + sentiment. Level-0 trained Aug 2017 –
  Mar 2018; level-1 data created Apr–May 2018 (out-of-sample for level 0; purged walk-forward CV, 1-week purge); test Jun–Jul 2018
  (roughly two months of daily observations).
- **Results (test Jun–Jul 2018):** stacked NN accuracy 0.54, AUC 0.50, F1 0.55; best single QDA accuracy 0.52, AUC 0.55, F1 0.67;
  XGBoost accuracy 0.46. The abstract promises a stacking-vs-blending comparison; no blending or simple-vote numbers appear in the text read.
- Takeaway limited to: a 1-hidden-layer NN meta-learner on ~40 daily test points; AUC 0.50 = chance.

## D. Neural combiners outside finance (cited for the architecture, not the domain)

### krasnopolsky2012 — Krasnopolsky & Lin, "A Neural Network Nonlinear Multimodel Ensemble to Improve Precipitation Forecasts over Continental US", *Advances in Meteorology* 2012:649450 (2012). DOI 10.1155/2012/649450. **ABSTRACT ONLY** (OpenAlex)
- NN maps the members of a multi-model NWP ensemble to a "nonlinear NN ensemble mean" of 24-h precipitation; compared with the
  "conservative" (equal-weight) multi-model ensemble, multiple-linear-regression ensembles and human forecasters.
- Abstract: the NN ensemble "improves upon conservative multi-model ensemble and multiple linear regression ensemble"; reduces high bias
  at low precipitation and low bias at high precipitation, sharpens features; "performs at least as well as human forecasters supplied with
  the same information". Numbers **UNVERIFIED**.

## E. Gated / mixture-of-experts combiners (existing keys; verified in claims_mix.md / claims_meta.md, not re-read)
- jacobs1991, jordan1994hme: gating network (softmax over inputs) weights expert outputs; experts and gate trained jointly — the
  combiner and the components are fit together, so this is not stacking of pre-fitted forecasts.
- weigend1995 (gated experts for time series, ABSTRACT ONLY) and weigendshi2000 (S&P 500 daily densities, ABSTRACT ONLY).
- guo2018btcvol: softmax gate over an autoregressive volatility component and an order-book-feature component (hourly BTC realised
  volatility from minute OKCoin LOB); TM-G lowest RMSE in 10 of 12 monthly test intervals; components trained jointly with the gate.
- antulov2021tme: temporal mixture ensemble, gate over 4 data-source experts (1-min / 5-min BTC volume, Bitfinex/Bitstamp LOB and trades);
  beats ARMA-GARCH on RMSE; GBM wins on MAE in several cases.

## F. LOB-specific neural combiner (existing key)

### prata2024 (existing key; FULL TEXT verified in claims_meta.md) — LOBCAST METALOB
- MLP (two layers) over the 3-class probability outputs of 15 DL LOB models (45 inputs); trained on 70% of the base models' test split
  (out-of-sample level-one data), validated 15%, tested 15%.
- F1 (Table 2): FI-2010 METALOB 82.2 ± 7.3 vs F1-weighted majority 60.0 ± 12.7 vs best single BINCTABL 82.6 ± 7.0; LOB-2021 55.9 vs
  BINCTABL 61.2; LOB-2022 53.2 vs DeepLOB 59.5. Paper: ensembles "do not exceed the performance of the top-performing models, which is
  probably due to the relatively high agreement rate among systems" (METALOB agreed with BINCTABL 82.8% on FI-2010).
- No other LOB / high-frequency neural combiner of pre-fitted forecasts was found in this search (bileki2022 in claims_meta.md stacks CNN
  features into CatBoost — tree combiner, ABSTRACT ONLY).

## G. Not combiners (checked, recorded to avoid mis-citation)

### smyl2020esrnn — Smyl, "A hybrid method of exponential smoothing and recurrent neural networks for time series forecasting", *Int. J. Forecasting* 36(1):75–85 (2020). DOI 10.1016/j.ijforecast.2019.03.017. **ABSTRACT ONLY** (Semantic Scholar)
- M4 winner. Abstract: "a dynamic computational graph neural network system that enables a standard exponential smoothing model to be
  mixed with advanced long short term memory networks into a common framework. The result is a hybrid and hierarchical forecasting method."
  ES and LSTM are fitted jointly inside one model; it is a hybrid, not a combiner of separately produced forecasts. (zhao2020for2for
  notes Smyl averaged multiple trained instances — an ensemble of runs, not a learned combiner.)
- lemke2010: NN used to select, not combine (above). hansen1997 (Hansen & Nelson, IEEE TNN 8(4):863–873, 1997, ABSTRACT ONLY): neural
  networks used alongside traditional time-series methods for Utah tax-revenue forecasts ("portfolio of forecasts"); the abstract does
  not describe an NN that takes other models' forecasts as inputs — not counted as a neural combiner; no bib entry written.

---

## Synthesis (verified facts only)

### 1. Yes — neural networks have been used as forecast combiners since the 1990s. Architectures found
| Architecture of the combiner | Inputs | Output | Paper(s) (read level) |
|---|---|---|---|
| Single-hidden-layer MLP, 1–3 logistic nodes + linear skip terms, random hidden weights | 2 standardized component forecasts only | combined forecast | donaldson1996annc (FULL), donaldson1999interact (FULL) |
| Same MLP, hidden weights by evolutionary programming | 2 component forecasts only | combined forecast | harrald1997 (FULL) |
| MLP (details unknown) | component forecasts | combined forecast | shi1996annc, palit2000 (ABSTRACT); krasnopolsky2012 (ABSTRACT, NWP ensemble) |
| 2-hidden-layer MLP (20, 10) | 3 ML forecasts + 30 lagged volatilities | combined forecast | ramosperez2019 (FULL) |
| 1-hidden-layer MLP (6 nodes) | 0/1 predictions of 7 classifiers | direction probability | barnwal2019stack (FULL, preprint) |
| 2-layer MLP | class probabilities of 15 DL LOB models | class probabilities | prata2024 (FULL, existing) |
| CNN (ResNet-like, linear skip path) or LSTM over horizon | h x M matrix of base forecasts only | combined forecast | zhao2020for2for (FULL, preprint) |
| Two-channel temporal CNN + squeeze-excite -> softmax | raw series + exogenous drivers (not the forecasts) | convex weights | mafildes2021 (FULL) |
| Two CNN branches (regression + selection), product -> softmax | raw series | convex weights | felici2026mtl (FULL) |
| FC net (128/64/32) on hand-crafted features -> softmax | 27 tsfeatures | convex weights | mafildes2021 benchmark M1 (FULL) |
| Gating network / mixture of experts (jointly trained with experts) | inputs / histories | gate probabilities over experts | jacobs1991, jordan1994hme, guo2018btcvol, antulov2021tme (existing, verified elsewhere) |
| CONTRAST: gradient-boosted trees -> softmax weights | 42 series features | convex weights | montero2020fforma (FULL WP) |
| CONTRAST: hybrid, not a combiner | — | — | smyl2020esrnn (ABSTRACT), kimwon2018 (ABSTRACT) |
No attention-based or mixture-density neural combiner of pre-fitted forecasts was verified in this search.

### 2. Domains and frequencies
- Finance: daily index volatility (S&P 500, Nikkei, TSE, FTSE; 1980–1987) — donaldson1996annc, harrald1997, donaldson1999interact;
  5-day S&P 500 realised volatility — ramosperez2019; daily BTC direction — barnwal2019stack; hourly / minute BTC volatility and volume
  via gating — guo2018btcvol, antulov2021tme; LOB mid-price direction (FI-2010, LOBSTER 2021/2022) — prata2024.
- Non-finance: M4 yearly–hourly (zhao2020for2for, felici2026mtl; FFORMA as tree contrast), weekly retail SKU sales (mafildes2021), road
  traffic (felici2026mtl), 24-h precipitation (krasnopolsky2012).

### 3. Do they beat simple averages and linear combinations? (as reported)
- **1990s finance (two volatility forecasts):** on RMSE the ANN was not better than OLS combination (donaldson1996annc Table II: equal on
  SP500 and TSEC, 0.01 worse on NIKKEI and FTSE, x1e-4); in harrald1997 the single GARCH model had the lowest out-of-sample RMSFE and the
  NN combiners were "in the middle of the pack". The claimed superiority rests on forecast-encompassing tests (ANN never encompassed;
  ANN encompasses AVE in 3 of 4 indices). donaldson1999interact: "RMSFE and MAFE do not give a clear ranking". wang2023review: this
  evidence "is based on fewer than ten time series, possibly hand-picked".
- **Cross-learning over many series:** NN combiners beat the simple average clearly when trained across tens of thousands of series:
  mafildes2021 AvgRelMAE 0.968 vs simple average 0.986 vs best single 1.000 (≈1.8% better than the average); felici2026mtl M4 OWA
  0.794–0.846 vs AVERAGE 0.911–0.949 and CLS-REG 0.909–0.946. But they beat the tree-based FFORMA only modestly or not at all:
  mafildes2021 (0.968 vs 0.974, abstract: gains over sophisticated meta-ensembles "modest"); felici2026mtl DNN-REG (pure regression CNN)
  was worse than FFORMA on M4 yearly and monthly OWA; For2For RNN total OWA 0.8345 vs FFORMA 0.838.
- **High-frequency / LOB:** the only neural stacker found (prata2024 METALOB) did NOT beat the best single model on any of 3 datasets
  (FI-2010 82.2 vs 82.6; LOB-2021 55.9 vs 61.2; LOB-2022 53.2 vs 59.5), though it beat an F1-weighted majority vote on FI-2010 (82.2 vs 60.0).
  No simple-average or linear-stacking baseline was reported there.
- **Recent finance stacking papers** (ramosperez2019, barnwal2019stack, kimwon2018) report no simple-average or linear-stacking baseline,
  so they do not answer the combination-puzzle question. barnwal2019stack test AUC 0.50.

### 4. Conditions that matter (stated or shown in the papers read)
- **Data size:** For2For: NN combiner lost to a simple linear model on M4 weekly (359 series) and hourly (414) — "a large sample size is
  crucial for NN models to work well". Successful NN combiners (mafildes2021, felici2026mtl) were trained on 10^4–10^5 series.
  harrald1997: the best-in-sample-MSE network did worse out of sample even with 3 hidden nodes ("problems with overfitting").
- **Out-of-sample level-one data:** donaldson1996annc, mafildes2021, zhao2020for2for, felici2026mtl, montero2020fforma, ramosperez2019,
  barnwal2019stack and prata2024 train the combiner on component forecasts not seen by the components (rolling OOS / temporal holdout /
  separate split). harrald1997 trained on in-sample fitted component forecasts and found the single GARCH model best on RMSFE.
- **Dissimilar components:** mafildes2021 — pools mixing individual and pooled models beat single-type pools (M3, M4 ablations; E3
  top-4-pooled average ≈ best single); felici2026mtl — explicit diversity term/labels improved the CNN combiner; prata2024 attributes
  METALOB's failure to "high agreement rate among systems"; Donaldson–Kamstra motivate combining by "partially non-overlapping information sets".
- **Combination vs selection:** NN or tree meta-learners that output weights beat the same meta-learner used for selection
  (mafildes2021 M5 1.050 vs M0 0.968; FFORMA: selection +10% OWA).
- **What the NN adds over linear weights:** Donaldson–Kamstra: state-dependent interaction ("turn off" one forecast); cross-learning
  papers: weights conditioned on series features / raw history (a function of the input), not a fixed weight vector.

### UNVERIFIED (not read or not found)
- shi1996annc, Shi & Liu 1993 IJCNN, palit2000, krasnopolsky2012: architectures, data and numbers beyond abstracts.
- babikir2016: content (only described second-hand by wang2023review).
- kimwon2018: exact LSTM input list; no comparison with averaging of GARCH forecasts known.
- Sermpinis, Dunis, Laws & Stasinakis (2012, Decision Support Systems 54(1):316–329, DOI 10.1016/j.dss.2012.05.039) and Sermpinis,
  Stasinakis & Dunis (2014, J. Int. Fin. Markets Inst. Money 30:21–54): abstracts not retrieved (SSRN blocked); a web-search summary says
  NN forecasts of EUR/USD were combined by Kalman filter, simple average, Bayesian average, Granger–Ramanathan and LASSO, i.e. NNs as
  components, not as the combiner. No bib entries written.
- Nti, Adekoya & Weyori (2020, J. Big Data 7, DOI 10.1186/s40537-020-00299-5): abstract reports stacking accuracies 90–100% on stock data;
  meta-learner type and protocol not read. Not included.
- Any attention-based dynamic ensemble weighting for time series, any mixture-density neural combiner, any NN meta-learner in M5, and any
  LOB / tick-level neural combiner other than METALOB: not found in this search (web-search budget exhausted partway; arXiv/OpenAlex
  keyword searches returned nothing relevant). Absence is not established.
