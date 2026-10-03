# Claims & paper notes — attention / Transformers used as COMBINERS of several models' forecasts

Pass date: 2026-10-03. Bib: `bib/refs_attncomb.bib`. Existing keys reused, not re-entered: zhao2020for2for, mafildes2021,
felici2026mtl, montero2020fforma, prata2024, zhanglimzohren2021mbo, jacobs1991, jordan1994hme, guo2018btcvol, antulov2021tme,
wolpert1992, chronos2024, timesfm2024, moirai2024, vaswani2017, li2024moeexec, ye2026rgresmoe. claims_nncomb.md recorded "No
attention-based ... neural combiner of pre-fitted forecasts was verified"; this pass covers that gap.

Search: arXiv API (~45 field queries: attention/transformer × ensemble / forecast combination / stacking / meta-learner / mixture of
experts / dynamic ensemble / ensemble members / foundation-model routing / limit order book / stock / volatility / cryptocurrency),
Semantic Scholar API (several queries rate-limited and returned nothing; retried with backoff), OpenAlex full-text and title search
(~25 queries), Crossref and OpenAlex for metadata and abstracts. No general web search engine was used. Elsevier/ScienceDirect pages
were blocked (bot wall), so Elsevier-only papers are abstract or metadata level.

Read levels: **FULL TEXT** = the arXiv PDF was read (text extraction; equations partly garbled). **ABSTRACT ONLY** = Crossref /
OpenAlex / Semantic Scholar abstract. **METADATA ONLY** = title, authors, venue.

Field key used in each entry: (a) what attention runs over; (b) Q / K / V; (c) output (weights or direct forecast; do weights
vary by instance/time); (d) component models; (e) data and frequency; (f) training protocol; (g) baselines (simple average, linear
stacking, MLP, FFORMA, best single); (h) headline results; (i) stated limitations.

---

## A. Attention used as the combiner of several models' forecasts (the question asked)

### bourgin2021maes — Bourgin, Bica & van der Schaar, "Model-Attentive Ensemble Learning for Sequence Modeling", arXiv 2102.11500v1 (Feb 2021). Preprint (no venue in arXiv metadata). **FULL TEXT**
- (a) **Across models.** Soft attention over the M experts at every prediction step t of every sequence. The paper says this differs
  from NMT attention: "here, attention is computed at the model level for aggregating base learner predictions in an ensemble" (§4.2.1).
- (b) Table 2 caption: "In MAES, the context is the query and the expert encodings are the keys." Query = c_{n,t}, the hidden state of a
  single-layer RNN run over the instance history H_{n,1:t}. Key = u_m, a learned v-dimensional vector per expert ("representing the
  context c_t for which model m's prediction is most useful"). Values = the experts' predictions f̂(H_{n,1:t}; θ_m). Scoring options
  additive / concatenation / dot / general; best was additive. The score must be permutation-invariant over experts (§4.2.2).
- (c) Outputs weights α_{t,m} (softmax over experts, Eq. 11) and the weighted sum of expert predictions (Eq. 10). Weights vary by
  instance and by time step (Fig. 3: "the expert weights depend on both the specific sequence features and the prediction step";
  text: "mainly on time due to the predominance of TCS").
- (d) LSTMs with different hidden sizes (20 sampled in [100,1100]; final MAES used M = 5 sampled at random from the 20).
- (e) Synthetic binary sequence classification simulating "temporal conditional shift" (motivated by ICU data); d = 3, T = 48,
  5,000 train / 1,000 test sequences per shift level δ ∈ {0, …, 0.4}. Not a forecasting benchmark; no real data.
- (f) **Not separately trained.** Experts, context RNN and expert keys are trained jointly end-to-end with the Jacobs et al. (1991)
  mixture-of-experts likelihood (Eq. 12) to force specialisation. Baseline stacking meta-models were fitted on validation data.
- (g) Individual LSTMs; post-hoc step-wise selection (best-validation LSTM per step); **average ensemble**; **global linear stacking**;
  **step-wise linear stacking** (Krstanovic & Paulheim 2017). No MLP combiner, no FFORMA.
- (h) Results only in Fig. 2 (APR vs δ); no numeric table — **numbers UNVERIFIED**. Text: stacking beat individual LSTMs; "The average
  ensemble achieved lower APR values than the best base models"; baseline differences "relatively limited"; MAES "significantly more
  robust to TCS than all baselines" (significance at p = 0.2 (*) and p = 0.05 (**), Monte Carlo permutation test). Independently
  trained LSTMs gave highly correlated predictions (Fig. 4), which the authors give as the reason baseline ensembles gained little.
- (i) Future work: real data, base-learner selection (experts were sampled at random), heterogeneous expert architectures.

### patel2023attnpool — Patel & Wikner, "Attention-Based Ensemble Pooling for Time Series Forecasting", arXiv 2310.16231v1 (Oct 2023). Preprint (arXiv comment "9 pages, 5 figures"; no venue). **FULL TEXT**
- (a) **Across models**, recomputed at every time step; the query and keys are time-delay embeddings over the last l steps (attention
  across models, conditioned on a short window of past states and past model errors). Explicitly for the case where the component models
  are fixed: "we do not have the ability to re-tune or update the candidate models themselves", which excludes jointly trained methods.
- (b) §2: "the weights are chosen adaptively by an attention mechanism based on a representation of the current state of the system (the
  query) and the set of representations of the candidate models' forecasts (the keys and the values)". Lorenz case: q_j = u_{j−1} (last
  true state); key_i = F^{(i)}_{j−1} − u_{j−1} (model i's last one-step error); values = F^{(i)}_j (model i's current forecast). COVID case:
  q = last observed weekly deaths; key_i = model i's last median error plus its other 20 quantiles. Additive (Bahdanau) scoring; also a
  multi-head variant whose heads are mixed by a linear layer.
- (c) Softmax weight a_{i,j} per model per time step; output is the weighted average of model forecasts (Eq. 2). Weights vary with time
  (Fig. 3 shows the top-weighted model's ρ tracking the true ρ(t)).
- (d) Lorenz: 11 stationary Lorenz-63 models with ρ ∈ {28, 30, …, 48}. COVID: the 9 COVID-19 Forecast Hub teams with the most complete
  weekly death forecasts (May 2020 – Oct 2022), 21 quantiles each.
- (e) Synthetic non-stationary Lorenz-63 (ρ(t) = 38 − 10 cos(2πt/T), Δt = 0.1); weekly US COVID-19 deaths, all states + DC.
- (f) Components fixed in advance (forecasts given). Combiner trained on one-step forecasts in "open loop"; tested in open loop (COVID)
  or closed loop multi-step (Lorenz). COVID: for each of 4 validation periods, trained on all data outside that period (so training
  data include periods after the test period). Loss: MSE (Lorenz), mean WIS (COVID).
- (g) **Linear regression on the component forecasts** (linear stacking); a feed-forward NN on past states only (Lorenz); "best initial
  model" (highest initial attention weight) and "fixed attention" (weights frozen at forecast start); COVID-19 Forecast Hub ensemble
  (weighted by past 12-week WIS, drawing on up to 124 models). No simple average, no MLP combiner of forecasts, no FFORMA.
- (h) Lorenz (Fig. 2, median valid time over 200 trajectories): linear regression 0.20, FF NN 0.40, additive attention 0.30 with l = 1
  and 2.60 with l = 5; fixed-attention and best-initial-model variants did not improve from l = 1 to l = 5. COVID (Table 1, mean WIS,
  periods from 08/29/2020, 06/05/2021, 12/18/2021, 07/09/2022): Hub ensemble 35.12 / 22.13 / 32.86 / 12.60; linear regression
  35.75 / 20.42 / 35.98 / 13.80; additive attention 38.27 / 20.72 / 35.51 / 12.92; multi-head attention 36.88 / 20.86 / 34.74 / 12.94
  (column assignment reconstructed from the extracted table and checked against the text: linear regression best in period 2, Hub best
  in periods 1, 3 and 4). Abstract: "it does not consistently perform better than the existing ensemble pooling when forecasting
  COVID-19 weekly incident deaths."
- (i) Hub used far more models; gap filling may bias; reporting errors in ground truth; method "assumes that the biases of the
  candidate models are not changing, or do not change much, over time", while Hub models were updated continually.

### chen2025e3former — Chen, He, Ye, Jiang, Zhang, Chen & Gao, "Online Ensemble Transformer for Accurate Cloud Workload Forecasting in Predictive Auto-Scaling" (E3Former), arXiv 2508.12773v1 (Aug 2025). Preprint. **FULL TEXT**
- (a) **Across models.** The "Online Scaling" ensembler applies multi-head self-attention to a set of d tokens, one per sub-network
  forecast; each token stacks a sub-network's forecast with the latest observed ground truth, embedded from horizon length H to D.
- (b) Self-attention: Q, K, V are all projections of the per-model tokens (W_Q, W_K, W_V are additionally adapted online by an
  EMA-of-gradients "Adapter").
- (c) Outputs weights: s = SoftMax(Linear(Flatten(MHSA(F))) + w), where w are Exponentiated-Gradient-Descent weights updated from
  each expert's recent squared error; final forecast f = sᵀF. Weights change online over time (Fig. 9). An alternative ensembler,
  Follow-The-Perturbed-Leader (E3Former-FTPL), has no attention.
- (d) The d "experts" are sub-networks of one parameter-shared Transformer, each fed a different patch size (8/16/32/64) of the same
  input ("d independent subnetworks with a part of shared parameters"). They are not separately designed models.
- (e) ByteDance cloud workloads (FaaS queries per second; IaaS CPU usage), minute-level; plus ETTh1/ETTh2/ETTm1/ETTm2, Electricity,
  Weather. Online forecasting.
- (f) Offline: "each output is used independently to train the corresponding sub-networks"; online: ensembler integrates outputs and
  the Adapter updates parameters.
- (g) ETS, Seasonal ARIMA, STL, DLinear, TimesNet, iTransformer, PatchTST, GPT4TS, FSNet, Time-FSNet, OneNet. No simple average,
  linear stacking, MLP combiner or FFORMA baseline.
- (h) "E3Former-OS reduces MSE by 13.9%, MAE by 11.7%, and WMAPE by 19.3% on average across seven datasets compared to the best
  baseline, OneNet (E3Former-FTPL exceeded it by 9.2%/8.5%/13.3%)" (§5). Abstract/intro give 19.1% for WMAPE (inconsistent with 19.3%
  in §5). Ablation (Table 7 text): removing multi-resolution patching (so no ensemble) raises MSE/MAE/WMAPE by 15.0/8.2/13.4%; removing
  the online adaptor by 29.4/14.3/13.5%. No ablation isolating attention-based scaling against EGD weights alone was found in the text.
- (i) No limitations section found.

### yao2026asde — Yao & Koprinska, "Attention score-based dynamic ensemble for time series forecasting with an importance greedy diversity model selection" (IGDS-ASDE), *Information Fusion* 137:104656 (Crossref issue date Jan 2027). DOI 10.1016/j.inffus.2026.104656. Peer-reviewed. **ABSTRACT ONLY** (OpenAlex; ScienceDirect blocked)
- Abstract: two-stage ensemble; IGDS removes "redundant and underperforming ensemble members"; then "ASDE uses attention mechanism in a
  meta-learning framework to adaptively compute ensemble member weights for the final prediction based on historical and predicted
  future performance". Seven time-series datasets; "significant improvements over advanced ensemble and individual baselines".
- (a) across ensemble members, using past and predicted future performance — Q/K/V definitions, component models, data, frequency,
  baselines and numbers **UNVERIFIED**.
- Related item by the same authors, not read: Yao & Koprinska, "Dynamic Meta-learning Ensemble with Pairwise Comparisons and
  Hierarchical Clustering for Financial Time Series Forecasting", CCIS, Springer, pp. 111–126 (2026), DOI 10.1007/978-981-95-6786-7_8
  (**METADATA ONLY**; whether it uses attention is UNVERIFIED; no bib entry written).

### chen2024attnensload — Chen, Li, Li, Sun, Meng & Su, "Ensemble Learning with Additive Attention Mechanism for Short-Term Load Forecasting", *Proc. 43rd Chinese Control Conference (CCC 2024)*, IEEE, 7113–7118. DOI 10.23919/CCC63176.2024.10661985. Peer-reviewed conference. **ABSTRACT ONLY** (Semantic Scholar)
- Additive attention combines two models, LSTM and Prophet, with "dynamic weights". Abstract: better than the individual models and
  "the ensemble algorithms with static weights", and better than SVR, Ridge, XGBoost and ANN "as ensemble learning algorithms" (i.e.
  linear-stacking (Ridge) and MLP-type (ANN) combiners). Q/K/V, data frequency, training protocol and numbers **UNVERIFIED**.

### liu2025windattnens — Liu, Leng, Chen, Zhang, Liu & Shen, "Medium-term wind speed ensemble forecasting using ICEEMDAN and dual attention mechanism", *Proc. SPIE*, CVAA 2024 (publ. 2025). DOI 10.1117/12.3055723. Conference. **ABSTRACT ONLY** (OpenAlex)
- Base models: attention-LSTM, GRU and BiLSTM on ICEEMDAN modes (wind power, despite "wind speed" in the title). "Finally, the
  attention mechanism is combined to dynamically weight the three models to achieve ensemble predictions." Q/K/V, baselines, numbers
  **UNVERIFIED**.

### liu2020taxiens — Liu, Liu, Lyu & Ye, "Attention-Based Deep Ensemble Net for Large-Scale Online Taxi-Hailing Demand Prediction", *IEEE T-ITS* 21(11):4798–4807 (2020). DOI 10.1109/TITS.2019.2947145. Peer-reviewed. **ABSTRACT ONLY** (OpenAlex)
- Ensemble of base models for city-wide spatio-temporal demand; "three attention blocks to model the inter-channel relationship,
  inter-spatial relationship and position relationship of the feature maps", attention maps multiplied into the input feature map;
  "superior to the existing ensemble strategy". Whether the channels are base-model outputs (i.e. attention across models) is
  **UNVERIFIED**; numbers UNVERIFIED.

### ren2025llmcomb — Ren & Wang, "Can LLM Improve for Expert Forecast Combination? Evidence from the European Central Bank Survey", arXiv 2506.23154v1 (Jun 2025). Preprint. **FULL TEXT** (read in part: setup, Table 3, conclusion)
- A pretrained Transformer LLM (Qwen; Deepseek as robustness check) used zero-shot as the combiner, via prompting, of ECB Survey of
  Professional Forecasters forecasts (GDP growth, HICP inflation, unemployment; 1- and 2-year horizons). Prompt components: historical
  accuracy weighting, lag compensation, trend enhancement; input is a fixed window of each expert's past forecasts and errors.
- (a)/(b) No learned attention combiner; the internal attention of the LLM over the prompt is not analysed. (c) The LLM outputs
  combination weights / combined forecast per round. (f) No training. (g) Baseline: simple average only.
- (h) Regression of log errors on method dummies: GDP growth h = 1, Qwen coefficient −0.264 (SE 0.114, p = 0.03); h = 2, 0.106
  (p = 0.402) (Table 3). Text: advantage weakens and often loses significance at 2-year horizon; for HICP "neither method demonstrates a
  clear edge". Conclusion claims superiority over simple averaging "particularly for one-year-ahead forecasts".
- Note: the arXiv PDF still contains Elsevier template placeholder text in the abstract/keywords fields.

### Abstract/metadata-level items where attention's role as combiner is unclear
- **gao2025transfusion** — Gao et al., "Load forecasting method based on Transformer multi-model fusion", *J. Computational Methods in
  Sciences and Engineering* (2025), DOI 10.1177/14727978251341480. **ABSTRACT ONLY**. Stacking of Transformer, XGBoost, GBDT; abstract
  does not state whether the Transformer is a base model or the meta-learner. Role **UNVERIFIED**.
- **pang2024windsa** — Pang & Dong, *Energy Conversion and Management* 307:118343 (2024). **METADATA ONLY** (no abstract retrievable).
  Title says AI models "optimized by self-attention mechanism"; whether self-attention combines the models is **UNVERIFIED**.
- **shobha2025attnmeta** — Shobha & Narasimhaiah, *IJEECS* 40(3):1325–1336 (2025). **ABSTRACT ONLY**. Attention + gated selection
  meta-model over RF, XGBoost, NN, SVM outputs — but for **classification** ("explicit content"), not forecasting.
- Not given entries (title/abstract only, not attention combiners or role unknown): "Load forecasting with Attention-Gated Hybrid
  Stacking Ensemble ... vs conventional Ridge stacking" (ResearchGate preprint, DOI 10.13140/rg.2.2.27468.17281, **METADATA ONLY**);
  Díaz de León-Hicks et al., "Addressing the Algorithm Selection Problem through an Attention-Based Meta-Learner Approach", *Applied
  Sciences* 13(7):4601 (2023) — attention meta-learner for algorithm **selection** on vehicle-routing instances, not forecasting
  (ABSTRACT ONLY).

---

## B. Adjacent: attention across the members of one NWP ensemble (post-processing)
These attend over ensemble members, but the members are perturbed runs of a single numerical weather model, not separately built
forecasting models, and the network corrects members or outputs a distribution rather than learning per-model combination weights.

### finn2021saet — Finn, "Self-Attentive Ensemble Transformer", arXiv 2106.13924v2; ICML 2021 workshop "Tackling Climate Change with ML". Workshop paper. **FULL TEXT**
- (a) Across ensemble members (k = 50 ECMWF IFS-EPS members), attention weights computed by dot product over the spatial grid.
  (b) Q, K, V are linear (1×1-conv) projections of each member's latent field; output for member i = v_i + Σ_j w_{ij}(v_j − v̄), a
  convex combination of value perturbations (ensemble-Kalman-filter analogy). (c) Outputs post-processed members directly, not
  per-model weights. (e) Global 2 m temperature, 48 h lead, 32×64 grid, 2017–18 train, 2019 test, ERA5 target. (f) Trained with
  Gaussian CRPS. (g) Raw IFS-EPS, climatology, member-wise "Direct" NN, parametric PPNN (Rasp & Lerch).
- (h) Table 2 (CRPS / ensemble-mean RMSE K / spread K): IFS-EPS raw 0.52 / 1.12 / 0.73; PPNN(5) 0.42 / 0.93 / 0.87; Direct(5) 0.45 /
  0.96 / 0.70; Transformer(5) 0.41 / 0.90 / 0.90. Cost "scales quadratically with the number of members".

### benbouallegue2024poet — Ben Bouallègue et al., "Improving Medium-Range Ensemble Weather Forecasts with Hierarchical Ensemble Transformers" (PoET), *AIES* 3(1) (2024). DOI 10.1175/AIES-D-23-0027.1. Peer-reviewed. **FULL TEXT of arXiv v3 (Oct 2023), skimmed**
- Finn-style self-attention across ensemble members inside a U-Net; ensemble-size agnostic (trained on 11-member reforecasts 2000–2016,
  applied to the 51-member 2021 operational ensemble). Abstract: "up to 20% improvement in skill globally for 2m temperature and 2% for
  precipitation"; beats the statistical member-by-member (MBM) benchmark; better than other DL methods on most ENS10 parameters; for
  Z500 with 5 members it "fails to beat the LeNet approach".

### hohlein2024perminv — Höhlein, Schulz, Westermann & Lerch, "Postprocessing of Ensemble Weather Forecasts Using Permutation-Invariant Neural Networks", *AIES* 3(1) (2024). DOI 10.1175/AIES-D-23-0070.1. Peer-reviewed. **FULL TEXT (arXiv v2), skimmed**
- Set transformer (three set-attention blocks, 8 heads, attention-based pooling) and set-pooling networks over unordered ensemble
  members; output distribution parameters (DRN) or quantile-function coefficients (BQN). Data: 20-member COSMO-DE-EPS wind gusts at 175
  stations; EUPPBench surface temperature. Conclusion: all permutation-invariant models beat raw ensemble and EMOS "by a large margin",
  but differences to existing NN post-processing (DRN/BQN on summary statistics) "are very minor and can mostly be attributed to
  differences in the distribution parameterization"; most information sits in a few ensemble-internal degrees of freedom.

### vanpoecke2025saet — Van Poecke et al., "Self-Attentive Transformer for Fast and Accurate Postprocessing of Temperature and Wind Speed Forecasts", *AIES* 4(3) (2025). DOI 10.1175/AIES-D-24-0127.1. Peer-reviewed. **FULL TEXT (arXiv v2), skimmed**
- Self-attention across the ensemble dimension, with information exchange across variables, space and 20 lead times; EUPPBench
  (11-member reforecasts, train 1997–2015, val 2016, test 2017). Abstract: CRPS improvement over raw forecasts 16.5% (2 m temperature),
  10% (10 m wind), 9% (100 m wind), beating classical MBM (e.g. 10% vs 8% for 10 m wind); up to six times faster.

---

## C. Attention-gated or routed mixtures of experts (jointly trained; not combiners of separately trained forecasts)
- **schwab2019ame** — Schwab, Miladinovic & Karlen, "Granger-causal Attentive Mixtures of Experts", *AAAI* 33:4846–4853 (2019). FULL
  TEXT (method section read). One expert per input feature group; per-expert "attentive gating networks" attend to the combined hidden
  state and output attention factors a_i that weight expert contributions; trained jointly with a Granger-causal objective; purpose is
  feature importance. MAES (bourgin2021maes) takes its learned per-expert key vectors from this paper.
- **yemets2025gatets** — GateTS, arXiv 2508.17515 (2025), preprint. FULL TEXT (intro read). Sparse MoE inside a univariate forecaster
  with an "attention-inspired gating mechanism that replaces the traditional one-layer softmax router"; experts are internal
  sub-networks trained jointly. Not a forecast combiner.
- **shi2025timemoe** (Time-MoE, ICLR 2025) and **liu2024moiraimoe** (Moirai-MoE, arXiv 2410.10469): MoE layers replace the Transformer
  feed-forward blocks; the router picks FFN experts per token. Experts are not forecasting models; not forecast combination.
- Existing keys (see claims_mix.md / claims_nncomb.md): jacobs1991, jordan1994hme (softmax gates), guo2018btcvol, antulov2021tme
  (temporal mixture gates on BTC order-book data) — softmax gating, not attention.

---

## D. Non-attention combiners of time-series foundation models (contrast; 2025–2026)
- **ning2026timerouter** — TimeRouter, arXiv 2606.11625 (Jun 2026), preprint, FULL TEXT. One-vs-all **XGBoost** router over 4 frozen
  TSFMs (Chronos-2, FlowState, PatchTST-FM, Sundial), features = context statistics + per-FM context-tail CV scores + downsampled per-FM
  forecasts (stacking features); selective gate defers to a CV-inverse-weighted average. GIFT-Eval LB MASE 0.6765 vs best single FM
  Chronos-2 0.6978 and TSOrchestra (fine-tuned LLM orchestrator) 0.6768 (Table 1). Head ablation: MLP 0.6787, logistic regression
  0.6836 (Table 3). Its related-work section names LLM-based combiners/selectors: TSOrchestra ("R1-style fine-tuned LLM for ensemble
  orchestration"), MoiraiAgent (fine-tuned Qwen-2.5-3B per-series selection), TimeCopilot (LLM agent) — not read; content UNVERIFIED.
- **das2025synapse** — Synapse, arXiv 2511.05460 (Nov 2025), preprint, FULL TEXT (method read). Weights ∝ 1/CRPS over a rolling
  window, updated by forward simulation; mixture by quantile sampling. No attention.

---

## E. Frequently mistaken for attention combiners (attention sits inside the members; combiner is something else)
- **ali2021seaice** — "Sea Ice Forecasting using Attention-based Ensemble LSTM", arXiv 2108.00853 (ICML 2021 CCAI workshop), FULL TEXT.
  Attention is a self-attention layer over LSTM hidden states inside each of the two members (daily→monthly and monthly→monthly LSTMs);
  the member outputs are concatenated and a fully connected layer "learns weights to assign to individual models". Table 2: EA-LSTM
  %RMSE 4.11 (R² 0.982) vs E-LSTM (no attention) 4.60, m-LSTM 4.21, Random Forest 4.63.
- **bui2025haelt** — HAELT, arXiv 2506.13981 (Jun 2025), preprint, FULL TEXT. Hourly AAPL up/down. Attention is temporal self-attention
  plus a Transformer branch inside the model; the "dynamic ensemble" weights are softmax(−L_i(t)/τ) of each pathway's recent
  validation loss (Eqs. 2–4), not attention. Ablation: full model F1 0.6421 (Table 3).
- **li2024masaat** — MASAAT portfolio optimisation, arXiv 2404.08935, preprint, FULL TEXT (method read). Attention for cross-asset and
  cross-time analysis inside each trading agent; agents' portfolios are "merged"; future work proposes weighting agents by historical
  performance.
- **iqbal2025abhef** — ABHEF, *IEEE Access* 13:7986–8010 (2025), ABSTRACT ONLY. Base models include TFT and a Dual Attention
  Transformer; meta-models compared: Gradient Boosting, LGBM, Ridge, CatBoost; CatBoost chosen.
- Din, Ahmed & Khan, CAB-XDE, arXiv 2401.11621 / *PLOS ONE* (2025), DOI 10.1371/journal.pone.0320089, and Din & Khan, TFT-ACB-XML,
  arXiv 2602.12380 — ABSTRACT ONLY; daily BTC; attention/TFT are base learners, error-reciprocal weights then **XGBoost** meta-learner.
  No bib entries written.
- Pandey et al., flare prediction ensemble, *Frontiers in Astronomy and Space Sciences* (2022), DOI 10.3389/fspas.2022.897301 —
  arXiv 2209.07406 read: logistic-regression meta-model, no attention.
- **zhanglimzohren2021mbo** (existing key; claims_meta.md): MBO-Attention is a base model; ensembles there are not attention-weighted.

---

## Synthesis (verified facts only)

1. **Do attention combiners exist?** Yes, but few, and almost all are recent preprints, workshop papers or low-detail conference /
   journal abstracts. Full-text-verified attention combiners of multiple models' forecasts: bourgin2021maes (jointly trained experts),
   patel2023attnpool (fixed, separately produced forecasts), chen2025e3former (sub-networks of one shared Transformer). Abstract-level
   only: yao2026asde (Information Fusion; the only peer-reviewed journal item found that explicitly describes attention computing
   ensemble-member weights for forecasting), chen2024attnensload (CCC), liu2025windattnens (SPIE), liu2020taxiens (T-ITS; role of
   attention across models unclear). One zero-shot LLM-as-combiner study (ren2025llmcomb).

2. **What is attended over?**
   - Across models in all three full-text items: the softmax runs over the candidate models (or sub-networks).
   - The query is the state or recent history of the series: an RNN encoding of the instance history (MAES); the last l true states
     (Patel & Wikner); the latest ground truth stacked into each token (E3Former).
   - Keys: learned per-model vectors (MAES); each model's recent forecast errors over the last l steps (Patel & Wikner); the models'
     forecasts themselves (E3Former self-attention).
   - Values: the models' current forecasts in all three.
   - Patel & Wikner found the time-delay window essential: valid time 0.30 at l = 1 vs 2.60 at l = 5 (Lorenz). Weights frozen at forecast
     start gave no gain.
   - NWP post-processing transformers (section B) attend across ensemble members of one model and output corrected members or
     distribution parameters, not model weights.

3. **Instance- and time-varying weights?** Yes in all three full-text items. Weights are recomputed per sequence and per step (MAES,
   Fig. 3), per time step (Patel & Wikner, Fig. 3) and online (E3Former, Fig. 9).

4. **Training protocol.** MAES trains experts and gate jointly (mixture-of-experts loss), so it is not stacking of pre-fitted
   forecasts. Patel & Wikner use fixed, externally produced forecasts. Their COVID combiner was trained on data from outside each
   validation period, including later dates. E3Former trains sub-networks offline and the combiner online. None of the full-text items
   use cross-fitted (out-of-fold) level-one forecasts in the stacking sense.

5. **Domains and frequencies.** Synthetic sequences (MAES; Lorenz-63 at Δt = 0.1), weekly COVID-19 deaths, minute-level cloud
   workloads plus ETT/Electricity/Weather, short-term electric load (CCC), wind (SPIE), taxi demand. NWP 2 m temperature, wind and
   precipitation (section B). No full-text attention combiner on financial data was found.

6. **Evidence against standard baselines.**
   - Simple average: only MAES (average ensemble below the best base model; MAES best, numbers only in a figure) and ren2025llmcomb
     (LLM beats simple average at 1-year horizon for GDP growth; not at 2 years; no clear edge for HICP).
   - Linear stacking:
     - patel2023attnpool, Lorenz: attention beat linear regression clearly (2.60 vs 0.20 median valid time).
     - patel2023attnpool, COVID: mixed. Linear regression was better in the first two periods and attention in the last two. The
       Hub's performance-weighted ensemble was best in 3 of 4 periods.
     - MAES beat global and step-wise linear stacking (figure only).
     - chen2024attnensload (abstract) reports beating Ridge and ANN combiners.
   - MLP combiner: chen2024attnensload abstract only.
   - FFORMA: no attention combiner paper found compares against FFORMA.
   - Best single model: MAES and E3Former report gains. Patel & Wikner's "best initial model" was worse than re-computed attention.

7. **Financial / order-book data.** No attention-based combiner of separately trained forecasts was found for limit-order-book data.
   No full-text-verified one was found for any financial series. Finance papers with "attention" and "ensemble" in the title put
   attention inside the base models and combine with inverse-loss softmax weights (HAELT, hourly AAPL), XGBoost (CAB-XDE, TFT-ACB-XML,
   daily BTC), CatBoost (ABHEF, energy) or plain merging (MASAAT). For a learned neural combiner on LOB data, the closest verified items
   remain prata2024 (MLP over 15 LOB models' class probabilities; not attention) and softmax-gated mixtures (guo2018btcvol,
   antulov2021tme).

8. **MoE inside Transformers.** Time-MoE, Moirai-MoE and GateTS route tokens to internal FFN experts. They are not combinations of
   expert forecasts. The 2025–26 combiners of time-series foundation models that were read (TimeRouter, Synapse) use XGBoost routing or
   inverse-CRPS weights, not attention.

### UNVERIFIED / NOT FOUND
- yao2026asde: Q/K/V design, component models, datasets and frequencies, baselines, all numbers (abstract only).
- chen2024attnensload, liu2025windattnens, liu2020taxiens: architectures beyond the abstract and all numbers. For liu2020taxiens, whether
  attention runs across base models at all.
- gao2025transfusion (Transformer role), pang2024windsa (whether self-attention combines models), the Attention-Gated Hybrid Stacking
  preprint (content), the Yao & Koprinska CCIS 2026 paper (content).
- bourgin2021maes: all APR numbers (figure only).
- TSOrchestra, MoiraiAgent, TimeCopilot, ZooCast (LLM / embedding-based TSFM selectors cited by TimeRouter): not read.
- OneNet (Wen et al., arXiv 2309.12659): PDF download failed twice; its combination mechanism was not checked here.
- NOT FOUND:
  - an attention combiner evaluated against FFORMA or on M4/M5;
  - an attention combiner with cross-fitted level-one forecasts;
  - any attention or Transformer combiner on limit-order-book data;
  - a full-text-verified attention combiner on any financial series;
  - a peer-reviewed full-text study of attention across separately trained forecasting models with a simple-average baseline and
    tabulated numbers.
