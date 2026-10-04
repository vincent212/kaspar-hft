# Coverage audit: self-supervised LOB learning, uncertainty, information-to-execution (2019–2026)

Date: 2026-10-04. Scope: §10 of `sections/p2_models.tex` ("Pretrained encoders, foundation models and
uncertainty", `\label{sec:foundation}` and the "Confidence and uncertainty" subsubsection; also
`sec:deep-fill`). No `.tex` file was edited. New BibTeX: `bib/refs_coverage.bib`.

Source tags: **[FT]** full text read (arXiv PDF via pdftotext) in this pass; **[ABS]** abstract only;
**[NOTE]** taken from an existing claims file (read in full by an earlier pass), not re-read here;
**[META]** metadata only (Crossref / OpenAlex / arXiv API).

Search method (stated so gaps can be judged): the WebSearch budget of this session was exhausted, so
searches used the arXiv API (abstract field queries), the OpenAlex API and the Semantic Scholar API
(partly rate-limited: 4 of 10 queries returned results). Queries combined "order book"/"LOB"/"order flow" with:
contrastive, self-supervised, masked, autoencoder, representation learning, pretrain/pre-train/foundation
model, embedding, transfer learning, conformal, coverage guarantee, distribution-free, uncertainty,
Bayesian, calibration/calibrated, expected calibration error, reliability, selective, prediction interval,
quantile, Monte Carlo dropout, deep ensemble, fill probability, survival, execution, markout, adverse
selection. Google Scholar, SSRN full-text search and journal sites were not searched directly. Absence
claims below mean "not found by these queries", not "does not exist".

---

## 0. Already covered (not repeated)

Grep of `sections/*.tex`, `notes/claims_*.md`, `refs.bib`, `bib/*.bib`:

| Topic | Keys already in the survey | Cited in .tex? |
|---|---|---|
| Masked message pretraining | `linna2025lobert` | yes (§sec:lobert, detailed) |
| Reconstruction-pretrained snapshot encoder | `li2024simlob` | yes (table row, p2_models.tex:6803 only) |
| Representation benchmark incl. frozen pretrained encoder transfer | `zhong2025lobench` | yes |
| Uncertainty module on pretrained encoder (ANP), calibrated 68% intervals | `manoharan2026uqlob` | yes |
| MC-dropout Bayesian DeepLOB | `zhang2018bdlob` | yes (p2_models.tex:7562) |
| Quantile-regression heads | `zhang2019qr` | yes |
| Bayesian bilinear network | `magris2023bbnn` | yes |
| Uncertainty-based position sizing (Eurodollar, P&L) | `spears2021eurodollar` | yes (multi-asset paragraph) |
| Selective prediction / calibration theory | `chow1970`, `elyaniv2010`, `geifman2017`, `guo2017` | yes |
| Deep fill-probability / survival models | `maglaras2022fill`, `arroyo2024`, `kanformer` | yes (§sec:deep-fill) |
| LOB forecaster repurposed for market impact | `linna2026impact` | **no** — in `bib/refs_deep2.bib` and `claims_deep2.md` only |
| Fill probability vs post-fill returns (live Binance) | `albers2025dilemma` | **no** — bib + `claims_simulators2.md` only |
| Semi-analytic fill probability with state-dependent flow | `lokin2024fill` | **no** — bib + `claims_simulators2.md` only |
| Representation robustness | `wu2021robust`, `barbosa2026lobrep`, `yang2025type` | yes |
| Hand-crafted vs autoencoder features | `nousi2019`, `ntakaris2019access` | yes |
| Generative / foundation (MarketGPT, TradeFM, M3, MarS, Kronos) | `wheeler2024marketgpt`, `tradefm2026`, `m3lob`, `li2025mars`, `kronos2025` | yes |

Item asked to verify — **"Zhang & Zohren Bayesian DeepLOB 2018/2021"**: the Bayesian DeepLOB paper is
BDLOB, Zhang, Zohren & Roberts, arXiv 1811.10041 (Nov 2018), Third Workshop on Bayesian Deep Learning,
NeurIPS 2018 [META, arXiv API + existing bib]. Already in the survey as `zhang2018bdlob`. No 2021 Bayesian
LOB paper by Zhang & Zohren was found; their 2021 LOB paper in the bib (`zhangzohren2021`, arXiv
2105.10430) is on multi-horizon forecasting and IPU hardware, not Bayesian. The 2021 uncertainty paper from
the same group is Spears, Zohren & Roberts (`spears2021eurodollar`), already covered. The only other
Bayesian deep LOB classifier found is `magris2023bbnn` (covered).

---

## 1. Self-supervised learning on LOB data

### 1.1 `lin2025multilevel` — reconstruction pretraining + supervised contrastive learning, manipulation detection [FT]

- **Citation:** Yushi Lin and Peng Yang, "Detecting Multilevel Manipulation from Limit Order Book via
  Cascaded Contrastive Representation Learning", arXiv 2508.17086v2 (10 Oct 2025), q-fin.CP. Southern
  University of Science and Technology. URL https://arxiv.org/abs/2508.17086.
- **Source type:** preprint (no journal reference on arXiv; none found on OpenAlex).
- **What is pretrained:** stage 1, a Transformer "Multilevel LOB Encoder" pretrained *without labels* to
  reconstruct 5-level LOB snapshots (MSE on the 4l price/volume entries), then frozen. Stage 2, its latent is
  concatenated with hand-crafted features and fed to a "Contrastive Fusion Encoder" trained with a hybrid loss
  that includes *supervised* contrastive learning (positive pair = both normal or both anomalous; negative =
  one of each), with anomalies oversampled to a set fraction of each batch. So only stage 1 is
  self-supervised; stage 2 uses labels.
- **Data:** LOBSTER, NASDAQ, CSCO, TSLA, INTC, a single day (2 January 2015). Manipulation is **synthetic**:
  spoofing/layering events injected across all five levels (Appendix A.3). Training 1,239,632 orders with 388
  manipulated (0.03%); validation 324,807 with 74; test 66,524 with 3,350 (5.04%) (Table 6).
- **Downstream task:** binary detection of injected multilevel manipulation, scored by downstream OC-SVM and
  Isolation Forest on the learned representation.
- **Baselines:** the same six encoders (CNN2, LSTM, JFDS, SimLOB, FEDformer, TimesNet) trained in an
  "original" mode (MSE loss, no multilevel encoder); OC-SVM / Isolation Forest on raw inputs.
- **Results (Table 1, AUC-PR, OC-SVM):** JFDS original 0.252 → proposed 0.675; LSTM 0.160 → 0.375; SimLOB
  0.164 → 0.210; CNN2 0.176 → 0.198; raw OC-SVM 0.163. With Isolation Forest, JFDS 0.232 → 0.631. Mixed for
  generic time-series encoders: FEDformer AUC-PR 0.226 → 0.105 (OC-SVM); TimesNet AUROC 0.829 → 0.646 (OC-SVM),
  precision 0.186 → 0.068.
- **Conditions:** one day, three stocks, injected (not real) manipulation, test anomaly ratio (5.04%) far above
  training (0.03%).
- **Execution?** No. Surveillance/detection only; no fills or P&L.

### 1.2 SimLOB publication status update (`li2024simlob`, reuse key) [META]

- Crossref: Yuanzhe Li, Yue Wu, Muyao Zhong, Shengcai Liu, Peng Yang, "SimLOB: Learning Representations of
  Limit Order Book for Financial Market Simulation", *IEEE Transactions on Artificial Intelligence* 7(6):
  3429–3444, June 2026, DOI 10.1109/TAI.2025.3640135. The bib entry `li2024simlob` (bib/refs_deep2.bib) still
  lists it as an arXiv preprint. Content claims unchanged (see `claims_deep2.md`, abstract-only there).
  Noted in `bib/refs_coverage.bib` as a comment; key not redefined.

### 1.3 `solecasale2026tsfm` — generic TS foundation model embeddings on crypto LOB (thesis) [ABS]

- **Citation:** Joel Solé Casalé, "Feature Extraction with Time Series Foundation Models for High-Frequency
  Limit Order Book", thesis, Universitat Politècnica de Catalunya (UPCommons), 2026,
  http://hdl.handle.net/2117/457642. Degree level not stated in the metadata read.
- **Source type:** student thesis, not peer-reviewed; abstract (Catalan) only.
- **What:** frozen Chronos-2 embeddings + PCA + light classifiers (e.g. logistic regression) on Bitcoin futures
  LOB data, compared with end-to-end deep models (TLOB named) and classical temporal classifiers on hand-made
  features. Abstract claims: competitive with transformer architectures using >99.9% fewer trainable
  parameters; better at short horizons and under high class imbalance; TLOB keeps an edge at longer horizons.
  No numbers in the abstract.
- **Execution?** No (prediction only, per abstract).
- Relevance: the only item found that tests a *general* TS foundation model as a frozen feature extractor on
  LOB data; the survey currently says "We found no fully reported study of zero-shot general models on
  intraday or tick-level order-book data" (p2_models.tex, foundation paragraph). This thesis does not change
  that statement much: abstract-only, no numbers, not peer-reviewed.

### 1.4 Not found

- **Contrastive pretraining on LOB snapshots or message streams for price prediction or execution:** none
  found. The only LOB contrastive paper found (1.1) uses *supervised* contrastive loss for anomaly detection.
- **Masked modelling other than LOBERT:** none found on LOB data. (LOBERT and its repurposing
  `linna2026impact` are the only masked-pretraining LOB works found.)
- **Autoencoder pretraining transferred to execution:** none found. Autoencoder/reconstruction pretraining
  appears in SimLOB (simulator calibration), LOBench (prediction transfer), and 1.1 (detection).

Checked and excluded: Kauppinen, Aalto thesis 2026 (JEPA pretraining for PPO) — daily DJIA data, not LOB
[ABS]. Bacalum et al., "LOB-ID", arXiv 2608.13082 — DeepLOB embeddings trained *with* labels, used to score
generators [ABS]. Stillman et al., arXiv 2311.11913 — embedding networks for simulator calibration, not
SSL [ABS]. DeLise, arXiv 2309.00088 — Deep SAD semi-supervised anomaly detection on TMX LOB data, fraud
detection only [ABS, first page of FT].

---

## 2. Uncertainty for LOB predictions

### 2.1 Conformal prediction on LOB / mid-price / fill probability: **none found**

- arXiv abstract queries `"order book" AND (conformal OR "coverage guarantee" OR "distribution-free")`
  returned zero results. OpenAlex/Semantic Scholar queries found no LOB conformal paper.
- Checked and excluded: Irshad & Biswas, "Uncertainty-Aware AI: Conformal Prediction versus Reinforcement
  Learning for Optimal Trade Execution", *Statistics, Optimization & Information Computing* 16(2):1334–1349
  (2026), DOI 10.19139/soic-2310-5070-4159 [ABS via publisher page]. It applies split-conformal prediction to
  next-interval returns to gate execution decisions, reporting 90.2%/90.7% empirical coverage at 90% nominal
  and cost variability falling from 19.1 to 10.0 bps — but on **5-minute bars of 30 US large caps, not LOB
  data**. Outside the survey's scope; mentioned only because it is the closest conformal-execution item found.
- Also excluded: "Coverage-Constrained Selective Prediction for Short-Horizon Cryptocurrency Event Contracts"
  (*Algorithms* 2026, DOI 10.3390/a19080704) — title/metadata only; not identified as LOB [META].

### 2.2 Calibration studies (reliability diagrams, ECE) of LOB classifiers: **none found as a dedicated study**

- No paper found that measures ECE or reliability diagrams across LOB direction classifiers. Calibration
  evidence already in the survey: UQ-LOB interval coverage (`manoharan2026uqlob`); LOBERT selective
  evaluation (`linna2025lobert`); survival-model proper scoring (`arroyo2024`).
- Checked and excluded: Chagas et al., "Inference-Time Decision Calibration for Temporal Classification",
  arXiv 2606.16034 [ABS] — uses FI-2010 among five datasets, but "calibration" there means recombining
  logits of a frozen classifier and an auxiliary branch at decision time, not probability calibration.

### 2.3 Bayesian deep learning for LOB

See §0: BDLOB (2018) and the Bayesian bilinear network (2023) are covered. No further Bayesian deep LOB
work found.

---

## 3. Information-to-execution chain (2023–2026)

### 3.1 `fabre2023interpretable` — neural fill-probability model driving a limit-vs-market decision, impact-adjusted backtest [FT]

- **Citation:** Timothée Fabre and Vincent Ragel, "Interpretable ML for High-Frequency Execution", arXiv
  2307.04863v2 (27 Sep 2024; v1 July 2023), q-fin.TR. CentraleSupélec. URL https://arxiv.org/abs/2307.04863.
- **Source type:** preprint. No journal version found (OpenAlex lists only the arXiv record). An SSRN record
  by the same authors, "Tackling the Problem of State Dependent Execution Probability: Empirical Evidence and
  Order Placement" (DOI 10.2139/ssrn.4509063), appears to be an earlier title — not verified as the same
  paper.
- **What it predicts:** fill probability of a limit order within a fixed horizon T (binary classification),
  trained with inverse-probability-of-censoring weights (IPCW) so that cancelled orders are handled as
  censored, with weights depending on placement distance; plus a second network for the *clean-up cost*
  (expected adverse best-price move if not filled). Model: feed-forward, 3 layers × 32 ReLU units, 25%
  dropout, sigmoid output.
- **Inputs:** hand-crafted, interpretable features — new ones (limit order flow imbalance, aggressiveness
  index, priority volume ahead) plus distance to best, BBO imbalance, size, spread, order-flow imbalances over
  50 events, traded-volume imbalance, trade durations, a high-frequency volatility estimator.
- **Data:** Level-3 (order-by-order) data. Coinbase BTC-USD, ETH-USD, 5 Nov–5 Dec 2022 (7–10 Nov removed for
  extreme volatility); Euronext Paris BNPP and LVMH, Jan–Dec 2017 (BEDOFIH).
- **Execution test:** "impact-adjusted backtest" — uses *real* limit orders that were posted (rather than
  inserting hypothetical ones), computes the model's expected saved cost S, decision d = 1{S>0} (post vs
  cross), and scores against the realised outcome (filled, or unfilled with favourable/unfavourable best-price
  move). Splits: crypto train 5 days / validate 2 / test following week (3 test periods); equity train 8
  months / validate 1.5 / test 2.5. Taker fee 5 bp, maker 0 for crypto.
- **Baselines and results (Table 4, decision precision/recall/F-score):** Model I = exponential fill
  probability fitted to Kaplan–Meier + constant clean-up cost; Model II = neural fill probability + constant
  clean-up cost; Model III = both neural.
  - BTC-USD: I 0.21/0.99/0.34; II 0.27/0.66/0.38; III 0.37/0.81/0.51.
  - BNPP: I 0.11/0.05/0.07; II 0.10/0.21/0.14; III 0.20/0.54/0.30.
  The authors attribute most of the gain to a state-dependent clean-up cost (II → III).
- **Also reported:** with 5 bp taker fee the optimal policy for BTC-USD quotes aggressively inside the spread;
  with zero fees it quotes deeper (heatmaps, Figs. 10–11). A qualitative latency-risk penalty is discussed
  (Sec. 5.2.4).
- **Not reported:** no calibration metric or reliability diagram for the fill-probability network was found
  in the text; no P&L in currency; scores are decision-classification metrics.
- **Execution?** Yes — fills and the post/cross decision; cost is measured through a decision label, not a
  simulated P&L.

### 3.2 `linna2026impact` — pretrained LOB forecaster (LOBERT) repurposed for market impact [NOTE + ABS]

- Already in `bib/refs_deep2.bib` and `claims_deep2.md` (full text read there); **not cited in any .tex**.
- Linna, Baltakys, Manoharan, Iosifidis, Kanniainen, arXiv 2609.16930v1 (Sep 2026), preprint. Injects
  mechanically valid counterfactual messages into the input of a trained Transformer (LOBERT) forecaster and
  reads the change in predictive distribution as short-horizon "model-implied market impact". Abstract
  [ABS, re-read via arXiv API]: scenario rankings Spearman 0.99 and 97.2% directional agreement with realised
  historical outcomes among non-neutral scenarios; "without retraining". Note [NOTE]: NASDAQ L3 data, 7
  stocks, Oct 2025–Feb 2026; the underlying forecaster's F1 is 47.7% (3-class).
- **Execution?** Partly: impact of one's own messages is an execution input; no fills or P&L.

### 3.3 Items in the bib but not in the text (execution-relevant)

- `albers2025dilemma` (Albers, Cucuringu, Howison, Shestopaloff, arXiv 2502.18625v2) [ABS re-read]: live
  Binance BTC-perpetual experiment; negative correlation between maker fill likelihood and post-fill returns;
  viable maker strategies often counter-trade order-book imbalance. Not deep learning, but it is the
  empirical case for why §10's direction forecasts and §deep-fill's fill models must be combined.
  `p2_models.tex` line ~1522 states the same trade-off from the CST model without this live-data citation.
- `lokin2024fill`: semi-analytical, not DL; see `claims_simulators2.md`.

### 3.4 Checked, execution-relevant, but outside §10 (not deep learning)

- Lalor & Swishchuk, "Market Simulation under Adverse Selection", arXiv 2409.12721v3 (2 Jun 2026) [ABS +
  intro]: CME ES, NQ, CL, ZN; argues that simulating price and market orders independently inflates
  short-term strategy performance and that realistic fill probabilities and tracking of adverse fills change
  results. Stochastic-control market maker, no DL. Not in the bib.
- Danait, Zamora, Boier (NVIDIA), "Same Book, Different Fills: Partial Identification of FIFO Execution from
  Aggregate Order Books", arXiv 2609.13597v2 [ABS]: TSE 2025, two instruments, 1,080 five-minute episodes
  each; with L2 data passive-execution backtests depend on the unobserved cancellation-allocation rule; for
  1301.T front vs back cancellation changes completion by 8.01 pp and implementation shortfall by 1.010 bp.
  Not DL; relevant to §sim / queue-position sections.
- Noble, Rosenbaum, Souilmi, "Bridging the Reality Gap in Limit Order Book Simulation", arXiv 2603.24137v1
  [ABS]: interactive large-tick simulator (spread + imbalance state, power-law impact kernel). Not DL.
- Yueshen, "Queuing Uncertainty of Limit Orders", *Management Science* 72(6):4760–4779 (2026), DOI
  10.1287/mnsc.2023.03371 [ABS via OpenAlex]: equilibrium model; end-of-queue limit orders lose money. Theory,
  not DL.
- Moret & Lillo, arXiv 2609.11614 [ABS]: distributional DQN market maker in a zero-intelligence LOB with a
  Bayesian online change-point filter as an auxiliary state. RL section, not §10.

### 3.5 Not found

- A LOB foundation model (LOBERT, M3, TradeFM, MarS, Kronos) evaluated on a fill-probability, execution-cost
  or P&L task with fills: none found, other than `linna2026impact` (impact, no fills).
- A deep LOB model whose *training target* is an execution outcome (fill, markout, post-fill return) beyond
  `maglaras2022fill`, `arroyo2024`, `kanformer` (covered) and `fabre2023interpretable` (3.1): none found.

---

## Recommended additions

Only items that change the information-to-execution picture or fill a gap in §10:

1. **`fabre2023interpretable`** — add to §sec:deep-fill after KANFormer: a small network on interpretable
   features, trained with censoring weights, is the only deep fill-probability study found that turns the
   probability (plus a learned clean-up cost) into a post-or-cross decision and scores that decision on real
   orders (decision F-score 0.51 vs 0.34 for an exponential baseline on BTC-USD; 0.30 vs 0.07 on BNPP).
2. **`linna2026impact`** — add to §sec:foundation, after the LOBERT limitations paragraph: it is the only
   foundation-model result found that is evaluated on an execution-side quantity (the impact of injected
   messages, Spearman 0.99 against realised scenario outcomes) rather than on direction, and is already in the
   bib with a full-text note.
3. **`albers2025dilemma`** — add one sentence in "Confidence and uncertainty", where the text says the
   execution policy needs the confidence: live Binance data show fill likelihood and post-fill return are
   negatively correlated, so a calibrated direction forecast is not sufficient without a joint fill model
   (already in the bib).
4. **Gap statements to add (one sentence each, in "Confidence and uncertainty"):** no conformal-prediction
   study on LOB mid-price or fill-probability forecasts was found, and no dedicated calibration (ECE /
   reliability) study of LOB direction classifiers was found; UQ-LOB's interval coverage is the only reported
   calibration check of a deep LOB forecaster in the survey.
5. **Bib fix (not a text addition):** update `li2024simlob` to the published IEEE TAI 7(6):3429–3444 (2026),
   DOI 10.1109/TAI.2025.3640135.

Not recommended for §10: `lin2025multilevel` (self-supervised + supervised contrastive, but for manipulation
detection on injected events; if cited, it belongs with manipulation/surveillance, and is the one LOB
contrastive-learning paper found); `solecasale2026tsfm` (thesis, abstract only, no numbers); the conformal
execution paper on 5-minute bars (not LOB).

## Summary

- Self-supervised LOB work beyond LOBERT/SimLOB/LOBench is thin: one contrastive paper (supervised
  contrastive on top of a reconstruction-pretrained encoder, for synthetic manipulation detection) and one
  abstract-only thesis using Chronos-2 embeddings. None transfers a self-supervised representation to an
  execution task.
- No conformal-prediction or ECE/reliability study on LOB forecasts was found. Bayesian deep LOB = BDLOB
  (2018, NeurIPS workshop) and the Bayesian bilinear network (2023), both covered; no 2021 Zhang & Zohren
  Bayesian paper found.
- Execution-side additions worth making: Fabre & Ragel (fill probability → post/cross decision on real
  orders) and `linna2026impact` (foundation model → impact), plus citing `albers2025dilemma`, which is
  already in the bib.
