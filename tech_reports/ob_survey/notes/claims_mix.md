# Claims & paper notes — combining several models' forecasts (meta-models over classical LOB models)

Scope: methods for combining the outputs of several forecasting models into one forecast (point, probability, density),
with evidence on when each method wins, and finance / high-frequency / LOB applications. Compiled 2026-10-03.
Metadata checked against Crossref (api.crossref.org) or the arXiv API unless stated. BibTeX in `bib/refs_mix.bib`.

Read levels: **FULL TEXT** = the paper (or an author preprint of it) was read; **ABSTRACT ONLY** = only the abstract
(publisher / OpenAlex / arXiv); **METADATA ONLY** = only bibliographic metadata. When the text read is a preprint/working-paper
version, that is stated; numbers are quoted from that version. Anything not checked against a source is marked **UNVERIFIED**.

Reused existing keys (not redefined in refs_mix.bib): see the list at the end of this file.

---

## C. Stacking, and the in-sample vs out-of-fold level-one question

### breiman1996stack (existing key) — Breiman, "Stacked Regressions", *Machine Learning* 24(1):49–64 (1996). **FULL TEXT** (journal PDF)
- Method: combine K predictors v_k(x) as v(x) = sum_k a_k v_k(x). Level-one data z_kn = v_k^(-n)(x_n) (prediction for case n from
  the k-th procedure refit without case n). Choose a = argmin sum_n (y_n - sum_k a_k z_kn)^2 subject to a_k >= 0 (no sum-to-one
  constraint required). Section 8: J-fold (10-fold) CV level-one data instead of leave-one-out.
- On leakage (Sec. 1, verbatim): if the v_k "were constructed using L, and the {a_k} gotten by minimizing squared error over L,
  then the resulting {a_k} will overfit the data - generalization will be poor. This problem can be fixed by using the level-one
  cross-validation data." This is an argument; **the paper reports no experiment that fits the weights on in-sample (resubstitution)
  level-one data**, so it gives no in-sample-vs-CV number.
- Second problem named: the v_k are strongly correlated, so unconstrained LS weights (in-sample or CV) are unstable; ridge
  "better than using the least squares {a_k}, but were not consistent"; non-negativity gave "consistently good results".
- Table 1 (stacking CART subtrees, 100 iterations, 50-case test sets, 10-fold CV): test error Best vs Stacked = 20.9 vs 19.0
  (Boston Housing), 23.9 vs 21.6 (Ozone). About 6.5 / 6.3 subtrees enter the stack on average.
- Table 3 (simulation, 40 inputs, 60 cases; model error ME, leave-one-out [10-fold]): e.g. Stacked Ridge-Subset 33.5[32.3],
  31.2[29.0], 19.8[18.0] for R = -.7, 0, .7; Best of Best 44.3[41.3], 36.0[32.8], 21.3[19.2]. 10-fold level-one data was
  slightly better than leave-one-out ("While the differences are small").
- Table 4: adding sum a_k = 1 changes ME little (Subset 35.5 -> 36.4; Ridge 40.8 -> 40.1; Ridge+Subset 35.5 -> 35.8).
- Stated conclusions: "Stacking never does worse than selecting the single best predictor"; biggest gains when dissimilar predictors
  are stacked (subset + ridge); stacked ridge gains little over CV-selected ridge because neighbouring ridge fits are similar.
  Note "never does worse" is stated about the paper's experiments; no proof ("a general proof is not yet in place").

### wolpert1992 (existing key) — Wolpert, "Stacked Generalization", *Neural Networks* 5(2):241–259 (1992). **METADATA ONLY**
Content known only via Breiman (1996) and Ting & Witten (1999): level-one data built from leave-out (CV) predictions. Breiman
writes that Wolpert "remarks that just how to use level one data to form accurate combinations is 'black art'"; Ting & Witten call the
level-1 generalizer and its inputs issues "considered to be a 'black art'" since 1992.

### leblanc1996 — LeBlanc & Tibshirani, "Combining Estimates in Regression and Classification", *JASA* 91(436):1641–1650 (1996). **ABSTRACT ONLY** (OpenAlex)
- Abstract: framework for combining regression fit vectors (subset regression, ridge, neural nets); examines the CV-based "model mix"
  / "stacking" proposal; derives bootstrap-based and analytic combination methods "and compare[s] them in examples"; applies to
  classification.
- Breiman (1996) reports (of the 1993 tech report) that they "also come to the conclusion that non-negativity constraints lead to the
  most accurate combinations".
- Whether its tables include a combination fitted on in-sample (non-CV) fits, and any numbers: **UNVERIFIED** (full text not accessible:
  JASA/JSTOR closed; no preprint found on Tibshirani's site).

### ting1999 — Ting & Witten, "Issues in Stacked Generalization", *J. Artificial Intelligence Research* 10:271–289 (1999). **FULL TEXT** (arXiv 1105.5466 = JAIR PDF)
- Level-0: C4.5, naive Bayes (NB), IB1 (nearest neighbour). Level-1 data from J = 10-fold CV. Two level-1 input types: predicted
  class labels (M~) vs predicted class probabilities (M~'). Level-1 learners: C4.5, NB, IB1, MLR (multi-response linear
  regression: one LS regression per class on the K models' probabilities for that class; predict argmax).
- 10 datasets (2 artificial, 8 UCI). Table 4: MLR on probabilities beats BestCV (best single model chosen by CV) 9 wins / 0 losses
  ("better than BestCV in nine datasets and equally well in the tenth"); significant in the four largest datasets
  ("stacked generalization is more likely to give significant improvements ... if the volume of data is large").
  Level-1 on class labels: no learner satisfactory.
- Table 6/7: MLR with no constraints / no intercept / non-negativity -> "little difference" in error; non-negativity aids
  interpretability.
- Table 8: MLR vs majority vote 8 wins / 2 losses; majority vote matches BestCV when level-0 models perform comparably (#SE small)
  and is worse when one model is much better (e.g. Vowel: BestCV 2.6, Majority 13.0, MLR 2.5 % error).
- Uses CV level-one data throughout; **no in-sample level-one comparison**.

### tibshirani2002preval — Tibshirani & Efron, "Pre-validation and inference in microarrays", *Stat. Appl. Genet. Mol. Biol.* 1(1) (2002). **FULL TEXT** (author PDF, 26 Jun 2002)
This is the one **empirical in-sample vs out-of-fold comparison of a level-one combination** found. Setting is not stacking by name,
but structurally identical: a learned predictor z (70-gene nearest-centroid classifier on 4918/4936 genes, van't Veer et al. breast
cancer data, n = 78: 44 good / 34 poor prognosis) is combined with 6 clinical predictors in a level-one logistic regression.
The authors note it "is also similar to the method of 'stacking' due to Wolpert (1992)".
- "Re-use" = z computed by the rule built on all 78 cases (in-sample). "Pre-validated" = z~_j from the rule rebuilt without case j's
  fold (K = 13 folds of 6) — i.e. out-of-fold level-one data.
- Table 1 (logistic regression, microarray coefficient): re-use coef 4.096 (SE 1.092, z = 3.753, odds ratio 60.105) vs
  pre-validated coef 1.549 (SE 0.675, z = 2.296, p = 0.011, odds ratio 4.706). Clinical predictors strengthen under
  pre-validation (e.g. angio p 0.069 -> 0.010). Full-model apparent misclassification 0.14 (re-use) vs 0.21 (pre-validated).
- Table 5 (prediction error, threshold 34/78): adding z to the 6 covariates. Naive re-use: 0.269 -> 0.141 (difference 0.128).
  Full cross-validation: 0.295 -> 0.282 (difference 0.013, SE 0.091). Zero-boot: 0.341 -> 0.342 (diff -0.001, SE 0.072).
  .632+ bootstrap: 0.320 -> 0.301 (diff 0.019, SE 0.066). Quote: "Most of the apparent improvement disappears under full
  cross-validation".
- Limitation stated: n = 78 is "too small to detect genuine differences of less than about 10%".
- Follow-up (Höfling & Tibshirani 2008, read as 3 Jul 2007 preprint, not added to bib): the one-degree-of-freedom test on the
  pre-validated predictor is biased; they propose a permutation test.

## A. Point-forecast combination (classical econometrics)

### bates1969 — Bates & Granger, "The Combination of Forecasts", *Operational Research Quarterly* 20(4):451–468 (1969). **ABSTRACT ONLY** (OpenAlex; Crossref lists two DOIs: 10.1057/jors.1969.103 and JSTOR 10.2307/3008764)
- Data: two sets of forecasts of airline passenger data. "The main conclusion is that the composite set of forecasts can yield lower
  mean-square error than either of the original forecasts." Weights are determined from past errors of each forecast; "different methods
  of deriving these weights are examined".
- Two-forecast optimal (variance-minimising) weight, as restated in Timmermann (2006 WP eq. 12) and Wallis (2011): with error variances
  s1^2, s2^2 and covariance s12, w1* = (s2^2 - s12) / (s1^2 + s2^2 - 2 s12), w2* = 1 - w1*; the minimised error variance is
  s1^2 s2^2 (1 - rho^2) / (s1^2 + s2^2 - 2 rho s1 s2) <= min(s1^2, s2^2) (Timmermann eq. 13).
- Per Timmermann (WP sec. 6.5), Bates & Granger used adaptively updated weights and found high discount factors tended to work best.
  Numbers in the original: **UNVERIFIED**.

### granger1984 — Granger & Ramanathan, "Improved methods of combining forecasts", *J. Forecasting* 3(2):197–204 (1984). **ABSTRACT ONLY** (OpenAlex)
- Method: combination as a regression y_t = a0 + sum_k a_k f_kt + e_t estimated by OLS; three variants (no constant + sum-to-one;
  no constant unconstrained; constant + unconstrained). Abstract: "the best method is to add a constant term and not to constrain the
  weights to add to unity". Data: quarterly hog price forecasts, "both within and out of sample".
- Numbers: **UNVERIFIED**. (Breiman 1996 and Diebold & Pauly 1987 later treat this as the regression-based combination.)

### clemen1989 — Clemen, "Combining forecasts: A review and annotated bibliography", *Int. J. Forecasting* 5(4):559–583 (1989). **METADATA ONLY**
- Wallis (2011, full text) states it contains "an annotated bibliography containing over 200 items". Content otherwise **UNVERIFIED**.

### timmermann2006 — Timmermann, "Forecast Combinations", ch. 4 in *Handbook of Economic Forecasting* vol. 1, Elsevier, 135–196 (2006). **FULL TEXT of the working-paper version** (UCSD, 1 Nov 2004; equation symbols garbled by extraction, prose read)
- Abstract (WP): combinations "frequently been found ... to produce better forecasts than methods based on the 'best' individual
  forecasting model"; "simple combinations that ignore correlations between forecast errors often dominate more refined combination
  schemes aimed at estimating the theoretically optimal combination weights"; reasons "poorly understood" — candidates: model
  misspecification, instability (non-stationarity), estimation error when the number of models is large relative to sample size.
- General optimal linear weights under MSE (WP eq. 8–9): w* = Sigma_ff^{-1} sigma_fy (with intercept, per Granger–Ramanathan); weights are
  larger for more accurate forecasts that are less correlated with the others.
- Section 6 summary of empirical findings (WP pp. 53–59): (6.1) simple combinations often dominate estimated-optimal weights, but the
  justification for equal weights "critically depends on the ratio of forecast error variances not being too far away from unity";
  (6.2) choosing the forecast with the best track record is often a bad idea; (6.3) trimming the worst models often required
  (Aiolfi & Favero 2003 found best results keeping the top 20%); (6.4) shrinkage toward equal weights often improves performance
  (Diebold & Pauly 1990; Stock & Watson 2003 — best when shrinkage is strong); (6.5) "The evidence on the value of allowing for
  time-varying combinations ... is somewhat mixed". Also: under asymmetric loss, estimated optimal weights can beat equal weights
  (Elliott & Timmermann 2003 inflation application). These are Timmermann's summaries of other studies; underlying numbers not re-checked.
- Page numbers of the published chapter for any of these statements: **UNVERIFIED** (the WP was read).

### stockwatson2004 — Stock & Watson, "Combination forecasts of output growth in a seven-country data set", *J. Forecasting* 23(6):405–430 (2004). **FULL TEXT of the working-paper version** (January 2003)
- Data: quarterly 1959–1999, 7 countries (G7), up to 73 predictors per country; each individual forecast is the h-step regression
  Y^h_{t+h} = b0 + b1(L) X_t + b2(L) Y_t + u (one candidate predictor X plus own lags; eq. 1) for real GDP or IP growth at 2, 4, 8 quarters;
  benchmark AR. Pseudo out-of-sample.
- Combination methods: mean, median, trimmed mean; discounted-MSFE weights w_i proportional to (sum_s delta^(t-s) e_is^2)^-1 with
  delta = 1, 0.95, 0.9 (delta = 1 = Bates–Granger); "recent best"; shrinkage toward equal weights; time-varying-parameter (TVP)
  combining regressions; principal-component / dynamic-factor forecasts.
- Table 9 (average loss over 39 cases = 13 country-output pairs x 3 horizons; lower is better): mean 0.648; disc. mse(1) 0.653;
  disc. mse(.95) 0.655; trimmed mean 0.659; disc. mse(.9) 0.659; shrink(1) 0.662; median 0.685; random walk 0.698; AR 0.706;
  tvp(.1) 0.735; PC(AIC) 0.805; shrink(.25) 0.814; tvp(.2) 0.842; dfm-PC(2,3) 0.850; tvp(.4) 0.976; recent best 1.048.
- Conclusions (Sec. 5): best-performing combinations "have the least data adaptivity in their weighting schemes"; methods that "heavily
  weight recent performance or allow for substantial time variation in the weights typically performed worse than – sometimes much worse
  than – the simple combination schemes"; least-adaptive combinations were also the most stable across the two halves of the sample.
  Individual-predictor forecasts are unstable and on average worse than AR.

### dieboldpauly1987 — Diebold & Pauly, "Structural change and the combination of forecasts", *J. Forecasting* 6(1):21–40 (1987). **FULL TEXT** (journal scan, OCR)
- Method: regression-based combination with time-varying weights: weighted least squares with geometrically decaying (lambda^t) or
  power (t^lambda) observation weights, optionally with deterministic linear/quadratic time-varying coefficients; compared with OLS
  (Granger–Ramanathan) and restricted variance–covariance (Bates–Granger) combination.
- Evidence is a **simulation** (4 cases of structural change in two biased constituent forecasts; recursive from t = 50). Table 1 MSPE:
  Case 1 (no change) OLS 0.658, var-cov 2.130, forecasts alone 2.018 / 7.569; Case 2 OLS 3.336 vs best time-varying (M5) 1.453;
  Case 3 OLS 3.374 vs M5 1.290; Case 4 OLS 3.009 vs M6 1.244. The paper: OLS combination cuts MSPE by ~60% vs the primary forecasts and
  "our time-varying combination procedures led to substantial further reductions in MSPE in cases 2–4". (OCR of the table read
  carefully; the constituent forecasts are constructed with bias 1.0, which favours an intercept.)

### aiolfi2006 — Aiolfi & Timmermann, "Persistence in forecasting performance and conditional combination strategies", *J. Econometrics* 135(1–2):31–53 (2006). **ABSTRACT of the WP version** (Dec 2003, titled "Persistence in Forecasting Performance")
- G7 output growth, linear and nonlinear models. "strong evidence of persistence among both top and bottom forecasting models, but also
  ... systematic evidence of 'crossings'" (previously good models turning bad), "particularly among linear models".
- Proposed three-stage conditional combination: sort models into clusters by past performance, pool within cluster, estimate optimal
  cluster weights and shrink toward equal weights; "shown to work well empirically in out-of-sample forecasting experiments".
  Numbers **UNVERIFIED** (not extracted).

### genre2013 — Genre, Kenny, Meyler & Timmermann, "Combining expert forecasts: Can anything beat the simple average?", *Int. J. Forecasting* 29(1):108–121 (2013). **ABSTRACT + non-technical summary of the WP version** (ECB WP 1277, Dec 2010)
- ECB Survey of Professional Forecasters (~10 years). Methods: PCA, trimmed means, performance-based weights, least-squares optimal weights,
  Bayesian shrinkage; data-snooping check. "For GDP growth and the unemployment rate, only few of the forecast combination schemes are able
  to outperform the simple equal-weighted average forecast"; for inflation "stronger evidence that more refined combinations can lead to
  improvement", significant "even controlling for data snooping bias". Numbers **UNVERIFIED**.

### smith2009puzzle — Smith & Wallis, "A Simple Explanation of the Forecast Combination Puzzle", *Oxford Bull. Econ. Stat.* 71(3):331–355 (2009). **ABSTRACT ONLY** (OpenAlex)
- Puzzle: "simple combinations of point forecasts are repeatedly found to outperform sophisticated weighted combinations in empirical
  applications". Explanation: "the effect of finite-sample error in estimating the combining weights". Evidence: small Monte Carlo and a
  reappraisal of Stock & Watson (2003, FRB Richmond Economic Quarterly 89/3). Also supports "the popular recommendation to ignore forecast
  error covariances in estimating the weight". Wallis (2011, full text) restates it as: when competing forecast variances are similar "it may
  be more efficient to impose equal weights than to estimate the weights – the squared bias is less than the estimation variance".

### claeskens2016 — Claeskens, Magnus, Vasnev & Wang, "The forecast combination puzzle: A simple theoretical explanation", *Int. J. Forecasting* 32(3):754–762 (2016). **FULL TEXT of the author version** (repository PDF)
- Argument: optimality of combination weights is derived treating weights as fixed; when they are estimated (random), "the forecast
  combination will be biased (even when the original forecasts are unbiased) and its variance is larger than in the fixed-weights case. In
  particular, there is no guarantee that the 'optimal' forecast combination will be better than the equal-weights case or even improve on the
  original forecasts." Theory + special cases + a numerical illustration; no empirical dataset.

### wallis2011 — Wallis, "Combining forecasts – forty years later", *Applied Financial Economics* 21(1–2):33–41 (2011). **FULL TEXT** (Warwick repository author version)
- Linear opinion pool / finite mixture of densities: f_C(y) = sum_j w_j f_j(y), w_j >= 0, sum w_j = 1; mean mu_C = sum w_j mu_j; variance
  sigma_C^2 = sum w_j sigma_j^2 + sum w_j (mu_j - mu_C)^2 (average uncertainty + disagreement).
- Logarithmic opinion pool: f_G(y) = prod_j f_j(y)^w_j / integral(prod_j f_j^w_j); for normal components it is normal with
  1/sigma_G^2 = sum w_j / sigma_j^2 and mu_G/sigma_G^2 = sum w_j mu_j/sigma_j^2 (equal-weight log pool => inverse-variance-weighted mean).
  Log pooling commutes with Bayesian updating ("externally Bayesian").
- Granger (1989) point restated: averaging forecasts made on different private information sets is not the same as pooling the information
  sets; equal-weight (or any sum-to-one) combination does not reach the full-information forecast.

### wang2023review — Wang, Hyndman, Li & Kang, "Forecast combinations: An over 50-year review", *Int. J. Forecasting* 39(4):1518–1547 (2023). **FULL TEXT of arXiv 2205.04216v2** (Sep 2022)
- Review; treats regression-based combinations (Granger–Ramanathan) as "the most simple, common learning algorithm used in stacking";
  recommends rolling/expanding windows and "time series cross-validation ... evaluation on a rolling forecasting origin" for training both
  base and meta-models; notes cross-learning meta-models (M4 top methods; FFORMA) and that Zhao & Feng (2020) used "only the out-of-sample
  forecasts produced by standard individual models as the input" to their neural combiner.

## B. Combining probability and density forecasts

### hallmitchell2007 — Hall & Mitchell, "Combining density forecasts", *Int. J. Forecasting* 23(1):1–13 (2007). **METADATA ONLY** (Crossref; publisher abstract not retrievable — 403; OpenAlex/Semantic Scholar have no abstract)
- A web-search summary of the abstract says: weighted linear combination of density forecasts with weights chosen to minimise the
  Kullback–Leibler information criterion (KLIC) distance to the true density (equivalently, maximise the average log score), applied to
  Bank of England and NIESR "fan chart" UK-inflation densities, finding that "combination can but need not always help". Treat as
  **UNVERIFIED** (not read from the publisher or the paper).

### geweke2011 — Geweke & Amisano, "Optimal prediction pools", *J. Econometrics* 164(1):130–141 (2011). **FULL TEXT of the WP version** (ECB WP 1017, March 2009)
- Method: linear pool p(y) = sum_i w_i p_i(y), w_i >= 0, sum w_i = 1, weights maximise the log predictive score
  f_T(w) = sum_t log( sum_i w_i p(y_t; Y_{t-1}, A_i) ); f_T is concave in w. Unlike Bayesian model averaging (which puts limiting weight 1
  on one model), the optimal pool generally keeps several models with positive weight; a model with positive weight can drop to zero when
  other models are removed.
- Data (finance): daily S&P 500 percent log returns; six models (Gaussian iid, GARCH(1,1), EGARCH, t-GARCH, stochastic volatility (SV),
  hierarchical Markov normal mixture HMNM); rolling 1250-day estimation windows; evaluation 15 Dec 1976 – 16 Dec 2005 (T = 7324).
- Results (WP sec. 3): t-GARCH has the best single log score, 19 above HMNM (and 143 above SV, 232 above EGARCH, 257 above GARCH, 1253 above
  Gaussian). Yet "a number of pools of two models outperform the model that performs best on its own (t-GARCH)"; the best two-model pool
  (HMNM + EGARCH) excludes t-GARCH and "outperforms t-GARCH by 37 points". GARCH + t-GARCH pool: optimum at w = 0.944.
- **Look-ahead in the combination weights, measured:** weights fitted once on all T observations vs re-optimised each day on past
  predictive densities only. t-GARCH + HMNM pool: log score -9284.72 (full-sample weight, HMNM weight 0.289) vs -9287.28 (real-time, average
  HMNM weight 0.307). "in every case the log score is lower when it is determined using only past predictive likelihoods ... But the values
  are, at most, about 3 points lower", although weights can differ markedly (largest contrasts in pools involving EGARCH). Note: here the
  component densities are already out-of-sample (rolling); only the pool weights are in-sample.

### ranjan2010 — Ranjan & Gneiting, "Combining probability forecasts", *JRSS-B* 72(1):71–91 (2010). **FULL TEXT of the tech-report version** (UW Stat TR 543, Oct 2008)
- Theorem 2.1: any linear opinion pool p = sum_i w_i p_i (w_i > 0) of two or more distinct **calibrated** binary-event probability forecasts
  is (a) **uncalibrated**, (b) lacks sharpness (is underconfident: closer to the climatological base rate than its recalibrated version), and
  (c) its recalibration q = P(Y=1 | p) beats it under every strictly proper scoring rule.
- Remedy: beta-transformed linear pool BLP: p = B_{a,b}( sum_i w_i p_i ), B_{a,b} = beta CDF; fitted by maximum likelihood. Nests the linear pool.
- Case study: probability-of-precipitation forecasts (GMOS, EMOS, NMOS statistical + NWS human) for 29 US cities (Baars & Mass 2005 data). Table 4 (test Brier score,
  statistical forecasts only): GMOS 0.0816, EMOS 0.0866, NMOS 0.0932, equal-weight ELP 0.0803, optimal-weight OLP 0.0799, BLP 0.0781
  (reliability component: OLP 0.0021 vs BLP 0.0004). Table 6 (adding NWS): ELP 0.0800, OLP 0.0789, BLP 0.0770. BLP alpha ~ 1.48–1.49.
  "The improvement of the nonlinear BLP method over the linear OLP forecast is about the same as that of the OLP forecast over the best
  individual forecast".

### gneiting2013combining — Gneiting & Ranjan, "Combining predictive distributions", *Electronic J. Statistics* 7:1747–1782 (2013). **FULL TEXT** (arXiv 1106.1638v1)
- Linear pools of CDFs; shows linear combination formulas with positive weights are not coherent; the linear pool increases dispersion (good
  for underdispersed components, harmful for neutrally dispersed ones). Proposes spread-adjusted (SLP) and beta-transformed (BLP) pools.
- Finance case study (Sec. 4.3): S&P 500 daily log returns 3 Jul 1962 – 29 Dec 1995; training to Dec 1978 (4,133 days), test 4,298 one-day
  densities. Components f1 = Student-t GARCH(1,1), f2 = Gaussian MA(1). Table 13 weights on f1: TLP 0.821, SLP 0.756, BLP 0.758. Table 15 mean
  log score (test): f1 3.458, f2 3.247, TLP 3.469, SLP 3.470, BLP 3.470 — "there is little reward for using more elaborate, less parsimonious
  aggregation methods" (consistent with Geweke & Amisano). A single joint MA(1)-t-GARCH(1,1) model that combines the two information sets
  inside one model scores higher still: 3.473 test (3.638 train).
- Discussion: parsimony / bias-variance applies; in data-poor settings simpler pools preferable.

### wallis2011 — see Section A (linear and logarithmic opinion pools).

### yao2018stacking — Yao, Vehtari, Simpson & Gelman, "Using stacking to average Bayesian predictive distributions (with discussion)", *Bayesian Analysis* 13(3):917–1007 (2018). **FULL TEXT** (arXiv 1704.02030v3)
- Stacking of predictive distributions: max_w (1/n) sum_i log( sum_k w_k p(y_i | y_{-i}, M_k) ) s.t. w_k >= 0, sum w_k = 1, using
  leave-one-out predictive densities (computed by Pareto-smoothed importance sampling, PSIS-LOO). Compared with stacking of means, BMA,
  Pseudo-BMA (w_k proportional to exp(elpd_loo_k)) and Pseudo-BMA+ (Bayesian-bootstrap stabilised).
- Claim: BMA is "flawed in the M-open setting" (true DGP not among the candidates) — asymptotically it selects one model; stacking targets
  the best convex combination under the scoring rule. The paper cites LeBlanc & Tibshirani (1996) for using LOO predictors "to avoid
  overfitting" at the meta level.
- Recommendation (Sec. 4.3): "we recommend stacking (of predictive distributions) for the task of combining separately-fit Bayesian posterior
  predictive distributions"; Pseudo-BMA+ as cheaper alternative. Limitation stated: combining separately fitted models "does not pool
  information between the different model fits"; a larger encompassing model is preferable when feasible.

## C (cont.). Super Learner

### vanderlaan2007 — van der Laan, Polley & Hubbard, "Super Learner", *Stat. Appl. Genet. Mol. Biol.* 6(1), Article 25 (2007). **ABSTRACT ONLY** (OpenAlex; bepress full text blocked by Cloudflare)
- "a fast algorithm for constructing a super learner in prediction which uses V-fold cross-validation to select weights to combine an initial
  set of candidate learners", motivated by oracle results for cross-validated selection; "a practical demonstration of the adaptivity"; generalises
  to any parameter defined as a loss minimiser. Whether it reports an in-sample (non-CV) weighting comparison: **UNVERIFIED**.

## D. Model averaging (Bayesian, dynamic)

### hoeting1999 — Hoeting, Madigan, Raftery & Volinsky, "Bayesian model averaging: a tutorial", *Statistical Science* 14(4):382–417 (1999). **FULL TEXT** (journal PDF)
- BMA: p(Delta | D) = sum_k p(Delta | M_k, D) p(M_k | D), with p(M_k | D) proportional to p(D | M_k) p(M_k); implemented via Occam's window or
  MCMC model composition (MC3). Abstract: "In these examples, BMA provides improved out-of-sample predictive performance."
- Evaluation protocol: random split into build/test halves; predictive log score.
- PBC survival example (Sec. 7.1.3): partial predictive score difference 3.6 => "BMA predicts who is at risk 6% more effectively than a
  method which picks the model with the highest posterior model probability (as well as 10% better than the Fleming and Harrington model
  and 2% more effectively than a stepwise method)"; over 20 random splits, BMA was on average 2.7 points (5% per event) better.
- Body-fat example (Table 10): predictive coverage of nominal 90% intervals: BMA 90.8%, stepwise/Cp model 84.4%, adjusted-R^2 model 83.5%.
- Gneiting & Ranjan (2013) cite this Table 10 as a "prominent example" of single selected models being underdispersed.

### raftery2010dma — Raftery, Kárný & Ettler, "Online prediction under model uncertainty via dynamic model averaging: Application to a cold rolling mill", *Technometrics* 52(1):52–66 (2010). **FULL TEXT of the preprint** (revised 3 Jul 2009)
- DMA: each model k is a state-space (dynamic linear) model with parameter forgetting (R_t = Sigma_{t-1} / lambda); model probabilities
  predicted with forgetting, pi_{t|t-1,k} = (pi_{t-1|t-1,k}^alpha + c) / sum_l (pi_{t-1|t-1,l}^alpha + c) (eq. 17, c = 0.001/K), then updated
  pi_{t|t,k} proportional to pi_{t|t-1,k} * p_k(y_t | y^{t-1}). Forecast = sum_k pi_{t|t-1,k} yhat_t^(k). alpha = lambda = 1 gives
  recursive (static) BMA. Used alpha = lambda = 0.99; "relatively insensitive" to this value.
- Data: cold rolling mill strip thickness, 19,058 samples, 24-sample measurement delay. Table 2 MSE (samples 26–200 / 201–19058): best
  physical model M3 77.5 / 20.7; DMA over 3 models 76.1 / 20.7; DMA over 17 models 68.9 / 20.6. DMA "allowed us to avoid paying a price for
  model uncertainty"; gains concentrated in the initial unstable period, where simpler models got more weight.

### koop2012dma — Koop & Korobilis, "Forecasting inflation using dynamic model averaging", *International Economic Review* 53(3):867–886 (2012). **FULL TEXT of the WP version** (Strathclyde DP 11-19; June 2009, revised Nov 2010)
- DMA/DMS (dynamic model selection) over the set of models formed from subsets of Phillips-curve predictors with time-varying coefficients; forgetting factors alpha
  (models) and lambda (coefficients). Quarterly US GDP-deflator and PCE inflation; evaluation from 1970Q1; h = 1, 4, 8.
- Table 1 (GDP deflator, h = 1; MAFE / MSFE / log predictive likelihood): DMA(alpha=lambda=0.99) 38.68 / 14.60 / -45.58;
  DMS(0.95) 36.68 / 13.80 / -36.99; static BMA (alpha=lambda=1) 39.93 / 15.60 / -49.85; TVP-AR(2) 40.55 / 16.65 / -53.65;
  UC-SV 39.90 / 17.20 / -49.85; rolling OLS all predictors 39.34 / 16.65; random walk 40.80 / 19.00. At h = 8: DMS(0.95) 43.91 / 22.09 / -63.23
  vs BMA 54.22 / 29.99 / -102.87 vs rolling OLS all 48.42 / 23.63. (Greek symbols lost in extraction; the parameter labels follow the
  paper's text "alpha = lambda = 0.95".)
- Findings stated: "conventional BMA forecasts poorly"; DMS usually beats DMA on predictive likelihood, less consistently on MSFE/MAFE;
  "simply using a TVP model with all predictors tends to forecast poorly"; best predictors "are changing considerably over time".
- Limitation stated: "we are presenting results for only a single empirical exercise".

## E. Mixture of experts, gating, regime-dependent combination

### jacobs1991 — Jacobs, Jordan, Nowlan & Hinton, "Adaptive Mixtures of Local Experts", *Neural Computation* 3(1):79–87 (1991). **FULL TEXT** (journal scan)
- Architecture: K expert networks o_j(x) and a gating network with softmax outputs p_j(x) = exp(s_j) / sum_i exp(s_i). Trained error
  (eq. 1.3) is the negative log-likelihood of a mixture of Gaussians: E = -log sum_j p_j exp(-||d - o_j||^2 / 2). The gradient weights
  each expert by its posterior responsibility, so experts specialise ("local"); compare eq. 1.2 (gated squared error), which adapts the
  best-fitting expert slowest early in training.
- Experiment: Peterson–Barney vowel formants, 4 classes, 75 speakers (train 50 / test 25), 25 simulations. Table 1: 4 or 8 linear experts
  and backprop nets with 6 or 12 hidden units all reach 88% train / 90% test; epochs to criterion 1124 (4 experts), 1683 (8 experts),
  2209 (BP 6 hidden), 2435 (BP 12 hidden). Gain is training speed, not accuracy, on this task.

### jordan1994hme — Jordan & Jacobs, "Hierarchical Mixtures of Experts and the EM Algorithm", *Neural Computation* 6(2):181–214 (1994). **ABSTRACT (journal, OpenAlex) + FULL TEXT of the 1993 IJCNN conference version**
- Tree of gating networks over GLIM experts; maximum likelihood by EM (each M-step = IRLS fits), plus an online algorithm.
- Conference-version simulation (robot dynamics, 15,000 train / 5,000 test): backprop needed 5,500 passes to reach relative error 0.09;
  HME (16 experts, 15 gates) reached similar error "in only 35 passes"; CART and MARS similar CPU time but "less accurate fits"; online
  HME converged in two passes. Journal-version numbers: **UNVERIFIED**.

### yuksel2012 — Yuksel, Wilson & Gader, "Twenty Years of Mixture of Experts", *IEEE TNNLS* 23(8):1177–1193 (2012). **ABSTRACT ONLY** (OpenAlex)
- Survey: ME regression/classification, EM training, mixtures of Gaussian-process experts, localized ME, variational ME, model selection
  (number of experts, tree depth), statistical properties, applications, software list.

### weigend1995 — Weigend, Mangeas & Srivastava, "Nonlinear gated experts for time series: discovering regimes and avoiding overfitting", *Int. J. Neural Systems* 6(4):373–399 (1995). **ABSTRACT ONLY** (OpenAlex)
- Gated experts: nonlinear gating network gives the probability of each expert from the inputs (contrasted with HMMs, where the switch
  depends on the previous state, and with simple averaging); each expert also learns its own noise width.
- Series: a synthetic switching series, Santa Fe laser data, daily French electricity demand. Stated results: the gate "correctly
  discovers the different regimes"; expert widths matter for segmentation; "less overfitting compared to single networks". No numbers
  extracted.

### weigendshi2000 — Weigend & Shi, "Predicting daily probability distributions of S&P500 returns", *J. Forecasting* 19(4):375–392 (2000). **ABSTRACT ONLY** (OpenAlex)
- "Hidden Markov experts": gate driven by a hidden Markov state; daily S&P 500; out-of-sample vs GARCH and gated experts. "The evaluation of
  the full density shows improvement over all competitors"; point-prediction performance "comparable". Numbers **UNVERIFIED**.

### elliott2005 — Elliott & Timmermann, "Optimal forecast combination under regime switching", *International Economic Review* 46(4):1081–1102 (2005). **FULL TEXT of the preprint** (21 Jun 2004; OCR of a scan)
- Method: latent Markov state S_{t+1} in {1..k} (Hamilton 1989 transition matrix); conditional on the state, (y, forecasts f) jointly
  Gaussian, so E[y_{t+1} | Z_t, S_{t+1}=s] = mu_y,s + sigma_yf,s' Sigma_ff,s^{-1} (f_{t+1} - mu_f,s): state-dependent intercept and weights;
  the combined forecast averages these by the filtered state probabilities.
- Empirical: survey (SPF) + autoregressive forecasts of six US macro series, out-of-sample. Two-state switching combination has lowest MSFE
  for 3 of 6 series (unemployment, inflation, GDP growth); beats rolling-window and Kalman-filter time-varying combinations in 4 of 6 and the
  DGT scheme in 5 of 6. SIC selected two states for all six series.
- Monte Carlo (Table 3): no switching -> expanding-window OLS combination best (switching scheme within a ratio of 1.03); frequent switching
  (p11 = p22 = 0.7) with T = 100 -> simple average best and switching scheme hurt by estimation error, but dominates at T = 500; persistent
  regimes (average duration 20) -> switching scheme "performs far better than the other forecasting methods irrespective of sample size".
- Requirement stated: regime-dependent combination needs changes in relative performance that are "at least mildly persistent".

### guo2018btcvol (existing key) — Guo, Bifet & Antulov-Fantulin, ICDM 2018. **FULL TEXT** (arXiv 1802.04065v3; see claims_meta.md for the full entry)
- Relevant here as a gated combination of a classical autoregressive volatility component and an order-book-feature component (softmax
  gate g_h computed from both histories; Gaussian and log-normal variants TM-G, TM-LOG). Hourly BTC realised volatility from minute-level
  OKCoin order book; TM-G lowest RMSE in 10 of 12 monthly test intervals; adding order-book features linearly (ARIMAX) was worse than ARIMA in
  all 12 intervals (e.g. 0.282 vs 0.194 in interval 2), whereas the gated mixture benefited.

## F. Online / adaptive weighting (prediction with expert advice)

### littlestone1994 — Littlestone & Warmuth, "The Weighted Majority Algorithm", *Information and Computation* 108(2):212–261 (1994). **FULL TEXT** (journal scan, OCR)
- WM: each pool member has a weight (initially 1); predict by weighted vote; on a master mistake, multiply weights of members that
  disagreed with the label by a fixed beta, 0 <= beta < 1 (beta = 0 = Halving algorithm).
- Abstract bound: if some pool member makes at most m mistakes, WM makes at most c (log |A| + m) mistakes (c a fixed constant).
- Shifting target (Sec. 3): WML never lowers a weight below gamma/|A| times the total; if the sequence splits into s segments each with a
  good member (m_i mistakes), WML's mistakes are bounded by a constant times (s log |A| + sum_i m_i), with no prior knowledge of segment
  boundaries. Lemma 3.1 bound: [log(n/(beta gamma)) + m_0 log(1/beta)] / log(1/x) (x a constant depending on beta, gamma; OCR garbled).
- Theory only (mistake bounds), no empirical data.

### freund1997 — Freund & Schapire, "A Decision-Theoretic Generalization of On-Line Learning and an Application to Boosting", *J. Computer and System Sciences* 55(1):119–139 (1997). **FULL TEXT** (journal PDF)
- Hedge(beta): weights w_{t+1,i} = w_{t,i} beta^{l_{t,i}}, allocation p_t = w_t / sum_i w_{t,i}, losses l in [0,1]; a "direct
  generalization" of WM. With beta tuned to a known bound L~ on the best strategy's loss: L_Hedge <= min_i L_i + sqrt(2 L~ ln N) + ln N
  (eq. 11), i.e. average regret O(sqrt(ln N / T)). Second part derives AdaBoost.
- Theory only for the online part.

### cesabianchi2006 — Cesa-Bianchi & Lugosi, *Prediction, Learning, and Games*, Cambridge University Press (2006). **ABSTRACT ONLY** (publisher blurb via OpenAlex)
- Prediction of individual sequences "does not impose any probabilistic assumption on the data-generating mechanism"; algorithms perform
  "nearly as good as the best forecasting strategy in a given reference class"; includes sequential investment. Specific theorems not re-read.

### wintenberger2017 — Wintenberger, "Optimal learning with Bernstein Online Aggregation", *Machine Learning* 106(1):119–141 (2017). **ABSTRACT** (arXiv 1404.1356v5) + update rule as restated by Remlinger et al.
- BOA: exponential weights with a second-order term. Weight update (Remlinger et al. Algorithm 1): with instantaneous linearised loss
  l_{k,t} (expert loss relative to the aggregate), w_{k,t} = w_{k,t-1} exp(-eta l_{k,t} (1 + eta l_{k,t})) / normaliser.
- Claim: first online algorithm achieving the optimal fast rate log(M)/n in deviation for model-selection aggregation (bounded iid, square
  loss); classical exponential weights cannot be optimal in deviation; multiple-learning-rate version optimal up to log log n. Theory.

### devaine2013 — Devaine, Gaillard, Goude & Stoltz, "Forecasting electricity consumption by aggregating specialized experts", *Machine Learning* 90(2):231–260 (2013). **FULL TEXT** (arXiv 1207.1965v1)
- Specialist aggregation (each expert is active only on a subset of rounds, the active set E_t), exponentially weighted average (E_eta), gradient versions, and fixed-share
  rules (Herbster & Warmuth 1998) that track the best sequence of experts. One-day-ahead (half-)hourly electricity load, Slovakia and France.
- Slovak data, Table 2 benchmarks (rmse, MW): uniform sequential rule 31.1; uniform convex weights 30.7; best single expert 30.4; best fixed
  convex weight vector (oracle) 29.2; best compound expert with at most 50 switches 23.1. Sec. 4.2: gradient-based rules "tuned with the
  best parameter eta in hindsight, outperform their comparison oracle, the best convex weight vector (with a relative improvement of 3 % in
  terms of the rmse)". Sec. 4.3 then shows online tuning of eta can approach the hindsight-tuned performance.
- Caveat stated: best constant parameters in hindsight "are far away from the theoretically optimal ones".

### gaillard2015 — Gaillard & Goude, "Forecasting Electricity Consumption by Aggregating Experts; How to Design a Good Set of Experts", in *Modeling and Stochastic Learning for Forecasting in High Dimensions*, Lecture Notes in Statistics, Springer, 95–115 (2015). **METADATA ONLY** (Crossref). Content **UNVERIFIED**.

### remlinger2023 — Remlinger, Alasseur, Brière & Mikael, "Expert aggregation for financial forecasting", *J. Finance and Data Science* 9:100108 (2023). **FULL TEXT** (arXiv 2111.15365v4)
- Finance application of online aggregation. 13 ML return-forecasting models (including OLS+H, RF and neural nets such as NN2, NN4, NN5) on
  CRSP/Compustat, >30,000 US stocks, 94 firm characteristics, 1957–2017 data; each model drives a monthly decile long–short portfolio; BOA aggregates the 13
  expert portfolios monthly (square loss vs. the best possible portfolio return). Test 1987–2016.
- Table 3 (equal-weighted portfolios, annual Sharpe ratio / max monthly loss): best single expert NN2 2.74 / 0.16; uniform mix 2.56 / 0.18;
  best fixed convex combination estimated on the validation set 2.28 / 0.13; best convex combination re-estimated on a 1-year rolling basis
  2.60 / 0.16; BOA 2.77 / 0.08; oracle best convex combination on the test period 2.92 / 0.07. Turnover ~1.22–1.26 for all.
  (Introduction/abstract quote 2.82 and 7% max loss — those are the "Extended BOA" variant, appendix row: SR 2.82, max loss 0.07.)
- Observations stated: expert weights stable 1992–2000, then a regime break in 2001; NN2 and OLS+H take 67% of average weight.
  Note the estimated fixed weights (2.28) underperformed equal weights (2.56) in this data.
- Monthly frequency, not high-frequency.

### berrisch2023crps — Berrisch & Ziel, "CRPS learning", *J. Econometrics* 237(2):105221 (2023). **ABSTRACT** (arXiv 2102.00968v3)
- Combination weights that vary over time **and across quantiles** of the predictive distribution, optimising CRPS (pointwise quantile-loss
  aggregation, smoothed with B-/P-splines); fully adaptive BOA version with optimal convergence; applied to European emission allowance
  (EUA) price distribution forecasts. Numbers **UNVERIFIED** (not extracted).

## G. Residual learning / boosting on top of a structural model

### zhang2003hybrid — Zhang, "Time series forecasting using a hybrid ARIMA and neural network model", *Neurocomputing* 50:159–175 (2003). **METADATA ONLY** (Crossref; publisher abstract not retrievable)
- Commonly described (web-search summary; **UNVERIFIED** wording) as: y_t = L_t + N_t; fit ARIMA for the linear part L_t, then fit a neural
  network to the ARIMA residuals e_t = y_t - Lhat_t using lagged residuals, and forecast yhat = Lhat + Nhat; data: Wolf sunspots, Canadian lynx,
  GBP/USD exchange rate; reported more accurate than either component. The decomposition and all numbers are **UNVERIFIED** (paper not read).

### friedman2001 — Friedman, "Greedy function approximation: A gradient boosting machine", *Annals of Statistics* 29(5):1189–1232 (2001). **ABSTRACT ONLY** (OpenAlex)
- Stagewise additive expansion F_m(x) = F_{m-1}(x) + rho_m h(x; a_m), each h fitted to the negative gradient of the loss (for squared loss: the
  current residuals) — "steepest-descent minimization" in function space; least-squares, LAD, Huber and multiclass logistic losses; TreeBoost.
- Relevance: boosting started from a structural model's prediction as F_0 is residual learning on that model. That use is a design
  inference here, not a claim of the paper (the paper's text was not read; whether it discusses non-constant F_0 is **UNVERIFIED**).

### rahimikia2020ml (existing key) — Rahimikia & Poon, SSRN 3707796 (2020). Not re-read here (OpenAlex returned 404); see claims_meta.md
  (ABSTRACT ONLY there). No claim made in this file.

### christensen2023mlvol — Christensen, Siggaard & Veliyev, "A Machine Learning Approach to Volatility Forecasting", *J. Financial Econometrics* 21(5):1680–1727 (2023). **ABSTRACT** (author accepted manuscript read for abstract and method list)
- Realised variance of DJIA constituents; ML (regularisation, trees, neural nets) vs HAR family. "ML is competitive and beats the HAR lineage,
  even when the only predictors are the daily, weekly, and monthly lags of realized variance"; gains larger at longer horizons. Neural-net
  "ensembles" here are averages over repeated training runs (NN with ensembles of 1 vs 10), not HAR+ML combinations. No HAR-residual model.

### chassot2026hard — Chassot & Audrino, "HARd to beat: The overlooked impact of rolling windows in the era of machine learning", *Int. J. Forecasting* 42(2):330–343 (2026). **ABSTRACT** (arXiv 2406.08041v1)
- 1,445 stocks; "ML models fail to surpass the linear benchmark set by HAR when utilizing a refined fitting approach for the latter"
  (training window / re-estimation frequency); HAR "consistently outperforms its ML counterparts when both rely solely on realized volatility
  and VIX as predictors". (Contradicts christensen2023mlvol in setting; the difference attributed by these authors to the fitting scheme.)

### clements2024harpuzzle — Clements & Vasnev, "Forecast combination puzzle in the HAR model", *J. Forecasting* 43(1):118–137 (2024). **ABSTRACT ONLY** (OpenAlex)
- Views HAR as a forecast combination of three predictors (previous day, previous-week average, previous-month average) whose OLS weights are
  "optimal weights that are known to be problematic"; "an average of simpler HAR-style models, and a simple average forecast often outperforms
  the optimal combination". Data: DJIA index and constituents, commodities, exchange rates, global equity indices. Claimed "dramatic
  improvements in forecast accuracy across all horizons and different time periods"; also good under volatility-timing portfolios. Numbers
  **UNVERIFIED**.

## H. Combination in finance / high-frequency / LOB

### beckerclements2008 — Becker & Clements, "Are combination forecasts of S&P 500 volatility statistically superior?", *Int. J. Forecasting* 24(1):122–133 (2008). **ABSTRACT ONLY** (RePEc)
- Compares model-based volatility forecasts, implied volatility (VIX) and combinations, using "recent econometric advances" (model-confidence-set
  style tests per the WP keywords). "a combination of model based forecasts is the dominant approach"; implied volatility "is in fact an inferior
  forecast of S&P 500 volatility relative to model-based forecasts". Numbers, horizons and test details **UNVERIFIED**.

### rapach2010 — Rapach, Strauss & Zhou, "Out-of-sample equity premium prediction: Combination forecasts and links to the real economy", *Rev. Financial Studies* 23(2):821–862 (2010). **ABSTRACT (RFS, via RePEc) + FULL TEXT of an earlier draft** (Feb 2008, "...Consistently Beating the Historical Average", monthly data; numbers may differ from the published version)
- Abstract (RFS): individual predictors fail out of sample (Welch & Goyal 2008); "Combining delivers statistically and economically significant
  out-of-sample gains relative to the historical average consistently over time"; explanations: combining "incorporates information from numerous
  economic variables while substantially reducing forecast volatility", and combination forecasts are "linked to the real economy".
- Combining schemes (draft eq. 2–4): mean (w = 1/N, N = 14 predictors), median, trimmed mean, DMSPE w_i,t = m_i,t^{-1} / sum_j m_j,t^{-1},
  m_i,t = sum_{s} theta^{t-1-s} (r_{s+1} - rhat_{i,s+1})^2 (theta = 1 is Bates–Granger), cluster C(k, PB), approximate BMA.
- Draft Table 1 (1947:01–2005:12 out-of-sample, unrestricted coefficients): individual predictors' R2_OS mostly negative (e.g. dividend yield
  -0.74%, book-to-market -2.50%); mean combination R2_OS 0.95% (utility gain 1.13% p.a.); median 0.81%; trimmed mean 0.91%; DMSPE theta = 0.9 0.96%;
  cluster C(2,PB) 0.46%. Published-version numbers **UNVERIFIED**.

### pattonsheppard2009 — Patton & Sheppard, "Optimal combinations of realised volatility estimators", *Int. J. Forecasting* 25(2):218–238 (2009). **ABSTRACT ONLY** (RePEc)
- High-frequency IBM data 1996–2008, 32 realised measures from 8 classes: "a simple equally-weighted average of these estimators cannot generally
  be out-performed, in terms of accuracy, by any individual estimator"; "none of the individual estimators encompasses the information in all
  other estimators".

### antulov2021tme — Antulov-Fantulin, Guo & Lillo, "Temporal mixture ensemble models for probabilistic forecasting of intraday cryptocurrency volume", *Decisions in Economics and Finance* 44(2):905–940 (2021). **FULL TEXT** (arXiv 2005.09356v2)
- Gated (mixture-of-experts) combination over S = 4 data sources (own and other-exchange transaction and order-book features, Bitfinex and
  Bitstamp BTC, Jun–Nov 2018): latent z_t in {1..S}; p(y_t | .) = sum_s P(z_t = s | x_<t) p_theta_s(y_t | x_s,<t); per-source log-normal
  components; ensemble of such models. Compared with ARMA-GARCH, ARMAX-GARCH and gradient boosting (GBM).
- Table 2 (1-min volume, Bitfinex; RMSE / MAE / NNLL / interval width IW): ARMA-GARCH 24.547 / 14.227 / 2.660 / 117.348; ARMAX-GARCH 24.629 /
  14.189 / 2.664 / 117.084; GBM 21.026 / 7.978 / NA / NA; TME 20.142 / 10.204 / 2.654 / 44.344. Bitstamp: TME RMSE 11.378 vs GBM 11.740 vs
  ARMA-GARCH 14.587; GBM has the lowest MAE (3.515 vs TME 4.299). Table 3 (5-min, Bitfinex): TME RMSE 63.855 vs ARMA-GARCH 64.999 vs GBM 64.964.
- Stated: gate probabilities show time-varying source contributions; the less liquid market's order book contributes little to the more liquid
  market's forecast. Note GBM wins on MAE in several cases.

### zhanglimzohren2021mbo (existing key) — Zhang, Lim & Zohren, "Deep Learning for Market by Order Data", *Applied Mathematical Finance* 28(1):79–95 (2021). **FULL TEXT** (arXiv 2102.08811v2)
- Data: one year of MBO data for five liquid London Stock Exchange instruments. Equal-weight averaging of classifier outputs (3-class mid-price
  direction at horizons k = 20, 50, 100): Ensemble-MBO (MBO-LSTM
  + MBO-Attention), Ensemble-LOB (LOB-LSTM + LOB-CNN + DeepLOB), Ensemble-MBO-LOB (the two ensembles).
- Table 4 F1 (%): k = 20: DeepLOB 68.40, Ensemble-LOB 68.31, Ensemble-MBO-LOB 69.02; k = 50: DeepLOB 64.79, Ensemble-LOB 65.23,
  Ensemble-MBO-LOB 65.34; k = 100: DeepLOB 61.10, Ensemble-LOB 60.56, Ensemble-MBO-LOB 61.82. Accuracy at k = 20: DeepLOB 68.73 vs Ensemble-LOB
  67.97 vs Ensemble-MBO-LOB 68.95. So the equal-weight ensemble of the three LOB models is not uniformly better than its best member; adding the
  less-correlated MBO signals gives the best row at every horizon (F1 gains over DeepLOB of 0.62, 0.55 and 0.72 points). Authors attribute the benefit to the
  low correlation between MBO and LOB predictive signals (their Fig. 3).

### prata2024, cestari2025hawkes, raffaelli2026mhp (existing keys) — not re-read here; see claims_meta.md. raffaelli2026mhp abstract re-checked
  (OpenAlex): hybrid multivariate-Hawkes (event timing) + continuous-time output-error model (return) "consistently outperforms the pure
  Hawkes-based model in both prediction accuracy and simulated trading profitability" (BTC/USD LOB events). Numbers **UNVERIFIED** (PDF blocked).

### geweke2011, gneiting2013combining, weigendshi2000, remlinger2023 — finance evidence recorded in sections B, E, F above.

## I. Calibration of combined / learned probabilities

### platt1999 — Platt, "Probabilistic outputs for support vector machines and comparisons to regularized likelihood methods", published as "Probabilities for SV Machines" in *Advances in Large Margin Classifiers* (Smola, Bartlett, Schölkopf, Schuurmans eds.), MIT Press, 61–74 (2000). **FULL TEXT of the 26 Mar 1999 preprint** (OCR)
- Sigmoid map P(y = 1 | f) = 1 / (1 + exp(A f + B)), A, B by regularised maximum likelihood with targets y+ = (N+ + 1)/(N+ + 2),
  y- = 1/(N- + 2).
- **Level-one leakage, stated:** fitting the sigmoid on the SVM's own training outputs is biased ("the training of the SVM causes the SVM outputs
  f_i to be a biased estimate of the distribution of f out of sample"); for non-linear SVMs, "fitting a sigmoid to the training set ...
  sometimes leads to disastrously biased fits". Remedies: a hold-out set (typically 30%) or cross-validation ("even better"); all results use
  three-fold CV outputs. Even CV data can overfit when classes are separable, hence the regularised targets. No in-sample-vs-CV number reported.
- Three data-mining data sets: SVM + sigmoid probabilities of "comparable quality" to a regularised-likelihood kernel method, while sparse.

### zadrozny2002 — Zadrozny & Elkan, "Transforming classifier scores into accurate multiclass probability estimates", *Proc. 8th ACM SIGKDD*, 694–699 (2002). **METADATA ONLY** (Crossref)
- Per Niculescu-Mizil & Caruana (2005, full text), isotonic regression is "the method used by Zadrozny and Elkan (2002; 2001) to calibrate
  predictions from boosted naive bayes, SVM, and decision tree models". The multiclass-coupling content is **UNVERIFIED** (not read).
  (The 2001 ICML paper "Obtaining calibrated probability estimates from decision trees and naive Bayesian classifiers" was read only for its
  abstract: binning improves naive Bayes probabilities; m-estimation smoothing and "curtailment" for trees; KDD'98 data. Not added to bib.)

### niculescu2005 — Niculescu-Mizil & Caruana, "Predicting good probabilities with supervised learning", *Proc. 22nd ICML*, 625–632 (2005). **FULL TEXT**
- Ten learners, eight binary problems. **Miscalibrated and how:** maximum-margin methods — **SVMs, boosted trees, boosted stumps** — "push
  probability mass away from 0 and 1", a "characteristic sigmoid shaped distortion"; **naive Bayes** pushes probabilities toward 0 and 1
  (opposite bias); **random forests** "less clear cut": well calibrated on some problems, poorly on LETTER.P2 and not well on HS, COV_TYPE, MEDIS,
  LETTER.P1, showing a milder sigmoid (averaging high-variance trees keeps predictions away from 0/1); **decision trees** high variance.
  Well calibrated before calibration: **neural nets, bagged trees, logistic regression** (and RF on some problems).
- Calibrators: Platt scaling (sigmoid) and isotonic regression (PAV), each fitted on an independent calibration set (1000 cases in main runs)
  because using the model's training set "introduce[s] unwanted bias". With calibration sets smaller than about 200–1000 cases Platt scaling beats
  isotonic for all nine methods (isotonic overfits); with more data isotonic is better or equal. After calibration the best probabilities come from
  boosted trees, random forests and SVMs. Platt scaling can hurt already-calibrated models (moves mass away from 0 and 1).

### ranjan2010 (Section B) — the linear pool of calibrated probability forecasts is itself uncalibrated (underconfident); recalibration (BLP)
  needed after combining.

## H (cont.). Additional finance / HF items found by broad search

### wang2016dmarv — Wang, Ma, Wei & Wu, "Forecasting realized volatility in a changing world: A dynamic model averaging approach", *J. Banking & Finance* 64:136–149 (2016). **ABSTRACT ONLY** (RePEc)
- S&P 500 realised volatility; HAR-RV and extensions (time-varying parameters, volatility of RV) combined by DMA. "DMA can generate more
  accurate forecasts than individual model in both statistical and economic senses"; time-varying-parameter models beat constant ones, also in
  density forecasting. Numbers **UNVERIFIED**.

### ye2026rgresmoe — Ye & Borde, "Regime-Gated Residual Mixture-of-Experts for Cross-Sectional Volatility Forecasting", arXiv 2608.12251v1 (Aug 2026). **ABSTRACT ONLY** (arXiv). Preprint, not peer-reviewed.
- Base predictor forecasts 5-day realised volatility from stock features; a gating network driven by regime state variables routes **residual
  corrections** from experts (regime info enters only the gate). 1,027 US equities, rolling walk-forward, capacity- and tuning-matched comparison;
  independent Japanese panel. Stated: beats a capacity-matched MLP in accuracy and training stability; "appending the same regime variables directly
  to the forecasting input degrades both predictive performance and training stability"; "Hard routing consistently underperforms soft routing".
  Numbers **UNVERIFIED**.

### li2024moeexec — Li, Cucuringu, Sánchez-Betancourt & Willi, "Mixtures of Experts for Scaling up Neural Networks in Order Execution", *Proc. 5th ACM ICAIF*, 669–676 (2024). **ABSTRACT ONLY** (OpenAlex)
- LOB execution (not forecasting): spectral clustering of data, one RL expert per cluster, router assigns an expert per step; Amazon on NASDAQ;
  "improves the profitability of execution tasks by approximately one basis point when compared to a single expert scenario".

### Search outcome (2026-10-03)
- arXiv API searches for mixture-of-experts + order book, stacking + limit order book, forecast combination + high-frequency, and Hawkes +
  neural + order book + hybrid returned no paper that combines the specific classical LOB models of this survey (OFI regression, queue
  imbalance / micro-price, Hawkes intensities, queue-reactive probabilities) in a meta-model with a reported evaluation. The closest verified
  items are guo2018btcvol, antulov2021tme (gated combinations of an econometric/AR component and order-book components),
  zhanglimzohren2021mbo (equal-weight ensemble of deep LOB/MBO classifiers), and raffaelli2026mhp / cestari2025hawkes (Hawkes timing + regression
  on imbalance, pipeline rather than weighting). Absence is a search result, not proof of non-existence.

---

## Synthesis (verified facts only; each point cites the entries above)

### 1. Methods that exist (for combining OFI-regression, queue-imbalance/micro-price, Hawkes and queue-model outputs)
- **Fixed linear combination of point forecasts**: equal weights; inverse-MSE / discounted-MSE weights (Bates–Granger; Stock–Watson DMSPE);
  regression weights with intercept and no sum-to-one (Granger–Ramanathan); shrinkage of estimated weights toward equal weights; trimming.
  (bates1969, granger1984, timmermann2006, stockwatson2004, rapach2010)
- **Stacking**: weights (or a level-1 learner) fitted on cross-validated / out-of-fold level-one predictions; non-negative least squares
  (breiman1996stack); multi-response linear regression on class probabilities (ting1999); V-fold CV weights (vanderlaan2007, abstract);
  stacking of predictive densities by LOO log score (yao2018stacking).
- **Probability / density pools**: linear opinion pool (mixture) and logarithmic pool (wallis2011); log-score-optimal linear pools
  (geweke2011); beta-transformed and spread-adjusted pools (ranjan2010, gneiting2013combining); quantile-wise CRPS combination
  (berrisch2023crps, abstract).
- **Bayesian and dynamic model averaging**: BMA (hoeting1999); DMA/DMS with forgetting factors (raftery2010dma, koop2012dma).
- **State-dependent weights**: mixture of experts / gating on inputs (jacobs1991, jordan1994hme, weigend1995); hidden-Markov regime weights
  (elliott2005, weigendshi2000); gated classical + order-book components (guo2018btcvol, antulov2021tme).
- **Online weights with regret guarantees**: weighted majority, Hedge, fixed-share, BOA (littlestone1994, freund1997, devaine2013,
  wintenberger2017, remlinger2023).
- **Residual learning on a structural model**: boosting (friedman2001, abstract); ARIMA+NN residual hybrid (zhang2003hybrid, unread).
- **Calibration after learning / combining**: Platt sigmoid, isotonic regression (platt1999, niculescu2005); BLP recalibration of pools
  (ranjan2010).

### 2. When each wins, per the evidence read
- **Simple vs estimated weights.** Equal or least-adaptive weights were best on average in stockwatson2004 (Table 9: mean 0.648 vs recent best
  1.048, TVP up to 0.976), are hard to beat for GDP/unemployment in genre2013 (but not inflation), and beat estimated fixed convex weights in
  remlinger2023 (2.56 vs 2.28 Sharpe). The stated mechanism is estimation error in the weights (smith2009puzzle abstract; claeskens2016: estimated
  "optimal" weights make the combination biased with higher variance and carry "no guarantee" of beating equal weights). Equal weights need
  similar error variances (timmermann2006 WP sec. 6.1); when one model is much better, equal weighting loses (ting1999 Vowel: majority vote 13.0%
  vs BestCV 2.6% error). Estimated weights won under asymmetric loss (Elliott–Timmermann 2003 as summarised by timmermann2006), in
  dieboldpauly1987's simulated structural-change cases, and in the stacking studies with CV level-one data (breiman1996stack, ting1999).
- **Stacking and level-one data.** Breiman (1996) and Platt (1999) state that fitting the combiner on in-sample base-model outputs is biased /
  overfits, and use CV outputs; neither reports a measured in-sample-vs-CV comparison. Breiman: 10-fold CV level-one data slightly better than
  leave-one-out (Table 3). Non-negativity constraints matter more than sum-to-one (breiman1996stack Table 4; ting1999 Table 6). Stacking helps
  most when base models are dissimilar (breiman1996stack) and when data are large (ting1999); combining probabilities beats combining labels
  (ting1999).
  **The one measured in-sample vs out-of-fold comparison found** is tibshirani2002preval (level-one logistic regression of a learned classifier +
  6 clinical covariates, n = 78): re-used (in-sample) predictor odds ratio 60.1 vs 4.7 pre-validated; apparent error reduction from adding the
  learned predictor 0.269 -> 0.141 (re-use) vs 0.295 -> 0.282 under full cross-validation (difference 0.128 vs 0.013 +/- 0.091).
  geweke2011 measures a different look-ahead (pool weights fitted on the whole sample vs past-only, with already-out-of-sample component densities,
  S&P 500): log score at most about 3 points worse with real-time weights over T = 7324 days.
  **No study measuring level-one leakage in stacked financial / LOB models was found.**
- **Dynamic weights under regime change.** Evidence is mixed: heavy adaptivity lost in stockwatson2004 (discounting delta = 0.9 no better than
  0.95/1; TVP worse); DMA/DMS beat static BMA and TVP in koop2012dma (e.g. h = 8 MSFE 22.09 DMS vs 29.99 BMA) and helped in the initial unstable
  period in raftery2010dma (MSE 68.9 vs 77.5 best single model, samples 26–200; ~equal afterwards); time-varying WLS weights won in
  dieboldpauly1987's simulated breaks; online aggregation tuned in hindsight beat the best fixed convex combination by ~3% rmse in devaine2013.
  elliott2005: regime-switching weights need persistent regimes; with frequent switching and T = 100 the simple average was best.
- **Gating when the best model depends on state.** Gating gave training-speed gains, not accuracy gains, on the vowel task (jacobs1991:
  90% test for all; 1124 vs 2209 epochs). Input-gated combinations beat linear feature augmentation in guo2018btcvol (ARIMAX worse than ARIMA in
  12/12 intervals; TM-G best in 10/12) and gave lower RMSE and much narrower intervals than ARMA(X)-GARCH in antulov2021tme (but GBM had lower
  MAE). ye2026rgresmoe (abstract, preprint) reports that putting regime variables only in the gate of a residual MoE beats feeding them as
  inputs, and soft beats hard routing. elliott2005: regime weights win when regimes are persistent and samples large enough.
- **Probability combination needs recalibration.** A linear pool of calibrated forecasts is uncalibrated and underconfident (ranjan2010 Thm 2.1);
  BLP improved Brier score over the optimal linear pool by about as much as the pool improved over the best single forecast (0.0781 vs 0.0799 vs
  0.0816). Base learners differ: SVMs and boosted trees/stumps are sigmoid-distorted toward 0.5, naive Bayes toward 0/1, RF mildly
  sigmoid on several problems; neural nets, bagged trees and logistic regression are close to calibrated (niculescu2005). Calibrators must be fit
  on data not used to train the model (platt1999, niculescu2005); Platt scaling beats isotonic below ~200–1000 calibration cases.
- **One bigger model vs combining.** In gneiting2013combining's S&P 500 example, a single MA(1)-t-GARCH model scored 3.473 vs 3.469–3.470 for
  pooling the MA and t-GARCH densities; yao2018stacking also recommends an encompassing model when feasible.

### 3. Evidence specific to finance / high-frequency / LOB
- Daily equity-return densities: optimal pools beat the best single model by up to 37 log-score points over 7324 days (geweke2011); elaborate
  pools add little over the linear pool (gneiting2013combining; geweke2011 per G&R).
- Volatility: combinations of model-based forecasts dominate (beckerclements2008, abstract); DMA over HAR variants beats individual models
  (wang2016dmarv, abstract); equal-weighted realised measures are hard to beat (pattonsheppard2009, abstract); simple averages beat OLS-weighted
  HAR (clements2024harpuzzle, abstract). HAR vs ML is contested (christensen2023mlvol vs chassot2026hard).
- Equity premium: mean combination positive R2_OS where individual predictors are negative (rapach2010; draft 0.95%).
- Cross-sectional ML portfolios: BOA online aggregation 2.77 Sharpe vs 2.74 best expert, max monthly loss 0.08 vs 0.16 (remlinger2023).
- Order book / HF: gated AR + order-book mixture for BTC hourly volatility (guo2018btcvol); gated multi-source mixture for intraday crypto volume
  (antulov2021tme); equal-weight ensemble of MBO and LOB deep classifiers adds 0.55–0.72 F1 points over DeepLOB, while an equal-weight ensemble of
  LOB models alone did not beat DeepLOB at k = 20 and k = 100 (zhanglimzohren2021mbo); Hawkes timing + imbalance regression pipelines
  (cestari2025hawkes, raffaelli2026mhp, numbers unverified); MoE routing for LOB execution, ~1 bp (li2024moeexec, abstract).
- Not found: a published meta-model over OFI regression, queue imbalance / micro-price, Hawkes intensities and queue-reactive probabilities.

---

## UNVERIFIED items (not confirmed against a source read in this pass)
- leblanc1996: all numbers; whether its examples include an in-sample-fitted (non-CV) combination and how it compares.
- vanderlaan2007: any comparison of CV vs non-CV weights; all numbers (full text blocked).
- hallmitchell2007: abstract wording (only a web-search summary) and all numbers.
- zhang2003hybrid: the residual-hybrid specification, datasets and all numbers (paper not read; web-search summary only).
- friedman2001: anything beyond the abstract (e.g. use of a non-constant initial model).
- bates1969, granger1984: all numbers; clemen1989: all content beyond "over 200 items" (via wallis2011).
- timmermann2006: published-chapter page numbers and wording (WP version read); summaries of third-party studies in its sec. 6 not re-checked.
- aiolfi2006, genre2013, smith2009puzzle, clements2024harpuzzle, beckerclements2008, wang2016dmarv, pattonsheppard2009, weigend1995,
  weigendshi2000, berrisch2023crps, yuksel2012, ye2026rgresmoe, li2024moeexec: all numbers (abstract-level only).
- rapach2010: published-version numbers (only a Feb 2008 draft with monthly data was read).
- jordan1994hme: journal-version results (1993 conference version read).
- gaillard2015, cesabianchi2006: content.
- zadrozny2002: content (only metadata; isotonic-regression attribution via niculescu2005).
- raffaelli2026mhp, cestari2025hawkes, prata2024: numbers (see claims_meta.md).
- Any empirical study measuring in-sample vs out-of-fold level-one leakage in financial or LOB stacking: none found.

---

## Keys reused from existing bib files (not redefined in refs_mix.bib)
- breiman1996stack, wolpert1992 (refs.bib / bib files) — stacking.
- guo2018btcvol — gated temporal mixture (BTC volatility).
- rahimikia2020ml — HAR + ML volatility (not re-read).
- zhanglimzohren2021mbo — MBO/LOB ensemble.
- prata2024, cestari2025hawkes, raffaelli2026mhp — LOB stacking / Hawkes hybrids (claims_meta.md).
- gneiting2007 (proper scoring rules), hansen2011mcs (model confidence set) — available for evaluation of combinations; not discussed above.
