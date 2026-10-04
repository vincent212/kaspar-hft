# Structural hybrids used for PREDICTION on LOB / high-frequency data — verified claims

Scope: single models trained end to end whose architecture is constrained by a classical
microstructure / point-process model, evaluated on prediction (next event type/time, price move,
duration) on real data. Source tags: **FULL TEXT** = whole paper read via pdftotext;
**abstract only** = only abstract/metadata read.

Search method (2026-10-03): arXiv listing search + arXiv abs pages, OpenAlex title/abstract boolean
search, Crossref and Semantic Scholar metadata. General web search was NOT available in this
session (budget exhausted), so Google Scholar / SSRN full-text search was not done. ACM DL and
Springer pages were blocked by bot challenges, so ACM/Springer-only papers could not be read in full.

---

## 1. shi2022sdpnhp — Shi & Cartlidge, KDD '22 — FULL TEXT (Bristol accepted manuscript)

Source read: University of Bristol AAM (research-information.bris.ac.uk/files/332120717/kdd2022.aam.pdf),
"Peer reviewed version", 9 pp. Page numbers 1607–1615 and DOI 10.1145/3534678.3539462 confirmed via
OpenAlex/Crossref and the AAM coversheet.

**Fit to "structural hybrid for prediction":** good on structure, partial on purpose. One network,
trained end to end on a point-process log-likelihood, whose intensities follow a neural-Hawkes form
(exponential decay of a latent state between events). The prediction target is the **next event type
(4 classes) and next inter-event time**, NOT price direction. The paper's second purpose is simulation
(Sec. 3.5, 4.2).

### 1.1 Classical structure built in (Sec. 2.3, 3.2–3.3; Eqs. 2, 3–15, 16–17, 20)
- Reference classical model: exponential-kernel Hawkes intensity λ(t) = μ + Σ_{h: t_h<t} α·exp(−δ(t − t_h)) (Eq. 2).
- Neural-Hawkes (Mei & Eisner CT-LSTM) form: at each event j, gates computed from
  h̃⁻_j = concat((ỹ_j + x̃_j), h⁻_j) (Eq. 3; ỹ, x̃ = embeddings of event type and state), standard LSTM gates
  i, ī, f, f̄, o, z (Eqs. 4–9), cell c_j and decay target c̄_j (Eqs. 10–11), decay rate
  δ_j = softplus(LC_d(...)) (Eq. 12). Between events:
  c(t) = c̄_j + (c_j − c̄_j)·exp(−δ_j (t − t_j)) (Eq. 13); h(t) = o_j ⊙ tanh(c(t)) (Eq. 14);
  λ_k(t) = softplus(LC_λ(h(t))[k]) (Eq. 15). So the "Hawkes" exponential decay acts on the latent state,
  not directly on the intensity (stated explicitly in Sec. 3.2).
- "Parallel" (PCT-LSTM, Sec. 3.3): K CT-LSTM units, one per event type, λ_k(t) = D_k(CTLSTM_k(S, X, t)) (Eq. 20)
  vs. single-unit λ_k(t) = D(CTLSTM(S,X,t))[k] (Eq. 19). A shared linear layer mixes the K hidden states at
  each event (Eq. 18); the authors say this shared layer gave "approximately 2%" higher type accuracy than
  without it (Sec. 3.3; no table for this).
- State dependence (from Morariu-Patrichi & Pakkanen's state-dependent Hawkes, ref [22]): after event e_j,
  the market state follows P(x_j | y_j, F⁻) ~ φ_{y_j, x⁻_j} (Eq. 16), a learned transition matrix
  φ_{K×W×W}. State = 3-level discretisation of top-of-book queue imbalance
  I = (v_b(1) − v_a(1))/(v_b(1) + v_a(1)) with thresholds ±θ (Eq. 17). **The value of θ is not given.**

### 1.2 Network, input, output (Sec. 3.1, 4.1.2)
- Input: sequence of J−1 = 49 previous events (type y ∈ {0: bid submission, 1: bid cancellation,
  2: ask submission, 3: ask cancellation}, time t) plus state sequence x (Sec. 3.1; J = 50, Sec. 4.1.1).
  Market orders/executions are not a separate event type in the 4-class scheme.
- Sizes (Sec. 4.1.2, applies to all models): linear layers in LSTM units 2 layers × 16 units, Tanh
  (Softplus for decay layer); embedding layers 2 × 16 Tanh; intensity decoder 1 layer × 16 units Softplus;
  SAHP 4 heads. Optimiser RMSprop, lr 2e-3, "200 iterations".
- Output: K intensity functions λ_k(t); predicted time t̂_j = ∫ t·p_j(t) dt (expected time under Eq. 21),
  predicted type argmax_k λ_k(t_j)/λ(t_j) if time known, or argmax_k ∫ λ_k/λ · p_j(t) dt if not (Sec. 3.4).

### 1.3 Prediction target (Sec. 3.1, 4.1.2)
- Next event type (4 classes) and next event time. Metrics: (1) negative log-likelihood per event;
  (2) "time loss" = absolute difference between real and predicted time after common (base-10) log;
  (3) type accuracy (%) "when the next event time is known and not known" — Table 1 reports these as
  "a / b"; the paper does not say explicitly which number is which (presumably known / unknown, in the
  order given in the text — this is my inference, not stated).
- No price-direction target.

### 1.4 Training (Sec. 3.4, 4.1)
- Loss (Eq. 25): L = L1 − η1·L2 + η2·L3, maximised. L1 = point-process log-likelihood ("Hawkes loss",
  Eq. 22); L2 = cross-entropy of next type (Eq. 23); L3 = log-likelihood of the state transitions (Eq. 24),
  which "can be separated ... and trained separately".
- The headline PCT-LSTM row apparently uses no cross-entropy term: the row "PCT-LSTM (η1 = 1)" is described
  as the variant "trained with a mixed loss function that contains a cross-entropy term" and the authors
  conclude the CE term "does not help" PCT-LSTM (Sec. 4.1.3). SAHP results do include the CE term.
- Data (Sec. 4.1.1): LOBSTER, INTC, MSFT, JPM, "five days' length" each; ~0.5 million event updates per
  day per stock; rolling windows J = 50, step 1 → "approximately 4 million samples".
  **Dates of the five days are not given. Train/validation/test split is not described anywhere in the
  paper.** No standard deviations, repeated runs, or significance tests are reported.

### 1.5 Baselines and results (Table 1)
Neg log prob (lower better) | time loss | type acc. (%) "a / b":

| Model | MSFT | INTC | JPM |
|---|---|---|---|
| Hawkes (state-dependent, exp. kernel) | 0.67, 1.25, 42.67/43.12 | 0.80, 1.24, 40.10/39.48 | −0.41, 1.04, 48.20/48.14 |
| LSTM (no intensity) | –, –, 47.64 | –, –, 46.04 | –, –, 58.05 |
| SAHP | −0.60, 1.14, 47.73/46.92 | −0.36, 1.08, 46.87/45.72 | −1.72, 1.06, 59.15/57.24 |
| CT-LSTM (Mei–Eisner) | −0.81, 1.03, 46.81/46.64 | −0.52, 1.00, 44.22/42.99 | −2.03, 0.91, 59.62/58.38 |
| **PCT-LSTM** | −0.87, 1.03, 49.66/48.98 | −0.61, 0.97, 48.38/46.88 | −2.09, 0.87, 60.97/59.47 |
| PCT-LSTM (non-sd) | −0.81, 1.03, 48.79/48.12 | −0.60, 0.98, 48.16/46.43 | −2.07, 0.88, 60.43/58.89 |
| PCT-LSTM (η1 = 1) | −0.53, 1.04, 49.11/47.90 | −0.31, 1.04, 48.31/46.48 | −1.57, 0.95, 60.33/57.29 |

- vs plain classical model (state-dependent Hawkes): PCT-LSTM better on all metrics, all stocks
  (e.g. MSFT type acc. 49.66 vs 42.67).
- vs plain network (LSTM with same layer-size settings, "naïve LSTM model, with no modelling of event
  intensity rates"): type acc. MSFT 49.66 vs 47.64; INTC 48.38 vs 46.04; JPM 60.97 vs 58.05 (+2.0 to +2.9
  points). LSTM cannot produce time predictions or likelihoods. Parameter counts are not reported.
- Ablations: removing state (non-sd) costs 0.2–0.9 points of accuracy; parallel vs single CT-LSTM
  (CT-LSTM row) is the ablation of the parallel structure. **There is no ablation that keeps the network
  and removes the Hawkes/decay structure other than the separate LSTM baseline.**
- Learned state-transition matrix (Eq. 27): >90% probability the imbalance state persists; asymmetric
  transitions (e.g. 0.07 vs 0.02) depending on whether the event was a bid or ask submission.

### 1.6 Limitations stated by the authors (Sec. 5)
- "only validated on the task of LOB event prediction"; simulator settings simplistic (occurrence of
  market orders, relative price distribution of arriving orders).
- Simulation (Sec. 4.2): fails to reproduce the negative volatility–return correlation (Fig. 3f).

### 1.7 Inconsistencies / gaps I noticed (my reading, not stated by authors)
- Eq. 22 indexes the log-intensity term by the state, λ_{x_{j+1}}(t_{j+1}), where the event type y_{j+1}
  is meant; and that term sits inside the sum over k, which taken literally counts it K+1 times.
- Sums and Algorithm 1 run k = 0..K while there are K types indexed 0..3 (off by one).
- Eq. 9 writes z_j = tanh(sigmoid(LC_z(·))) — unusual (standard LSTM is tanh of the linear map); may be a typo.
- No data split, no dates, no θ, no variance across runs → the ~2-point margins over LSTM cannot be
  assessed for significance from the paper alone.

### 1.8 Independent re-run of PCT-LSTM by a third party (LOBDIF paper, arXiv 2412.09631v1) — FULL TEXT
Zheng, Li, Ouyang, Liang, Shao re-ran the baselines "using open-source code with optimal settings"
with the same layer settings as Shi & Cartlidge (Sec. VI-A.3), 5 runs averaged; an 80/10/10 split within each day (they say "dividing each day into 80% for training, 10% for validation, and 10% for
testing"; ordering not stated). Datasets: MSFT-1, MSFT-2 (LOBSTER, two different days, 4 types),
Pingan-1/2 (Shenzhen) and Telecom-1/2 (Shanghai) from CSMAR (3 types). Table III type accuracy
(fraction; time known/unknown not separated):

| | MSFT1 | MSFT2 | PINGAN1 | PINGAN2 | TELE1 | TELE2 |
|---|---|---|---|---|---|---|
| Hawkes | 0.33 | 0.37 | 0.36 | 0.36 | 0.35 | 0.37 |
| LSTM | 0.37 | 0.34 | 0.41 | 0.39 | 0.41 | 0.44 |
| CT-LSTM | 0.45 | 0.42 | 0.37 | 0.43 | 0.42 | 0.44 |
| PCT-LSTM | 0.47 | 0.44 | 0.38 | 0.46 | 0.46 | 0.45 |
| LOBDIF | 0.46 | 0.44 | 0.52 | 0.50 | 0.47 | 0.50 |

MAE (log-time) PCT-LSTM: 1.07, 1.16, 2.23, 1.91, 2.29, 2.59; Hawkes 1.72, 1.58, 2.73, 1.99, 2.33, 2.10.
- Here PCT-LSTM beats plain LSTM on 5 of 6 datasets but **loses on PINGAN1 (0.38 vs 0.41)**. On time MAE it beats the classical
  Hawkes on 5 of 6 datasets but is **worse on TELE2 (2.59 vs 2.10)**.
- Inconsistency in LOBDIF: it reports Wilcoxon signed-rank p-values on 5 paired runs as low as 6.10e-4;
  with n = 5 pairs the smallest attainable exact Wilcoxon p-value is 1/32 = 0.031 (one-sided). It also
  reports p = 1.52e-3 on MSFT1 accuracy where LOBDIF (0.46) is *below* PCT-LSTM (0.47).
- LOBDIF itself is a diffusion model, not a structural hybrid.

---

## 2. gyotoku2025deeppp — Gyotoku, Muni Toke & Yoshida, arXiv 2504.15944v1 (math.ST, 22 Apr 2025) — FULL TEXT

Preprint (no journal version found via arXiv/OpenAlex/Crossref).

**Fit:** structurally the cleanest "classical model whose functions are output by a network": a Cox-type
**(marked) ratio model** (Muni Toke & Yoshida, `munitoke2020ratio` and their 2022 marked-ratio paper)
where the covariate-dependent factors are deep ReLU networks, trained by the model's own
quasi-likelihood. **But on real data it reports only an in-sample fit figure — no out-of-sample
prediction metric and no baselines.** The paper's main contribution is a theoretical risk bound.

- Structure (Sec. 4, Eq. 4.1): λ^{i,k_i}(X_t, Y_t) = λ_0(t) λ^i(X_t) p_i^{k_i}(Y_t), Σ_k p_i^k = 1;
  λ_0 an unobserved baseline intensity that cancels in ratios (handles intraday non-stationarity).
  Ratio loss (Example 2.2; Eq. 4.8): −∫ Σ_{i,k} log[exp(l^{i,k}(X,Y)) / Σ_{j,k'} exp(l^{j,k'}(X,Y))] dN^{i,k},
  i.e. a softmax over event types evaluated at event times; the network outputs the log-ratios l.
  "One-step" estimation: one network for all (i,k) ratios (Sec. 4.1.2). "Two-step" (Sec. 4.1.3): one
  network for the unmarked ratios λ^i, then separate networks for the mark probabilities p_i^k —
  i.e. the multiplicative structure is imposed.
- Network (Sec. 4.1.2): dense feed-forward, LeakyReLU, n^L inner layers × n^N neurons. Real-data run:
  n^L = 8 and n^N = 64 for all networks (Sec. 4.2).
- Theory (Thm 3.1): risk R_T ≤ C φ_T L (log T)^4 for sparse ReLU nets (Schmidt-Hieber-type), under
  periodic stationarity and α-mixing; minimax-optimal up to log factors (Remark 3.2).
- Simulation (Sec. 4.1.5, Fig. 6; 20 samples per horizon): both methods converge at rate ≈ −1/3 (L2, L∞)
  and ≈ −2/3 (risk); "The superiority of the two-step estimation method, which takes into account the
  multiplicative structure of the model, is clear." → an ablation of structure, **on simulated data only**.
- Real data (Sec. 4.2): TotalEnergies (FR0000120171), Euronext Paris, 22 trading days 2–31 Jan 2017,
  09:05–17:25; >1,750,000 market (and marketable) orders. Events: side i ∈ {sell MO, buy MO} × mark
  k ∈ {does not change mid, changes mid} (4 types). Covariates: level-1 queue imbalance
  (q_B − q_A)/(q_B + q_A), last trade sign, spread in ticks capped at 3.
  Result: Fig. 7 compares fitted vs empirical joint probabilities p^{i,k}(imbalance, last sign, spread);
  authors: "both estimation methods give excellent fitting results"; exponential parametric forms
  "have been tested and not shown here, because of poor results". **No train/test split, no accuracy,
  no baseline numbers for the real data.**
- Prediction claim for the classical (non-neural) ratio model — that it predicts trade signs better than
  Hawkes on Paris data — is cited from [17, 18], not shown in this paper.

---

## 3. hwang2025shockbiased — Hwang, Lee, Kim, Lee & Kim, ICAIF '25, pp. 915–923 — ABSTRACT ONLY

DOI 10.1145/3768292.3770431 (Crossref: Proceedings of the 6th ACM International Conference on AI in
Finance, 14 Nov 2025; authors Sukmin Hwang, Sungho Lee, Chanyeong Kim, Yongjae Lee, Woo Chang Kim).
OpenAlex/Semantic Scholar list it as gold OA, CC-BY, but the ACM PDF was blocked (HTTP 403 bot
challenge) and no arXiv/repository copy was found. **Only the abstract was read.**
- Abstract: a Transformer Hawkes Process whose "attention matrix" is augmented "with a domain-informed bias
  term, whose amplitude is a function of both the event mark and the elapsed time with exponential temporal
  decay"; evaluated on "real-world datasets from financial markets" in "event prediction tasks", reported to
  outperform existing methods, with "interpretable parameters".
- This is the closest match to "attention biased by a Hawkes kernel" found, but datasets, targets,
  baselines and numbers are UNVERIFIED.

## 4. chung2024nmhp — Chung, Lee & Kim, "Neural Marked Hawkes Process for Limit Order Book Modeling", PAKDD 2024, LNCS, pp. 197–209 — METADATA ONLY

DOI 10.1007/978-981-97-2259-4_15 (Crossref; in "Advances in Knowledge Discovery and Data Mining").
Closed access; Crossref/OpenAlex/Semantic Scholar have no abstract (publisher elided it); Springer page
blocked. **Content unverified** — only the title indicates a neural marked Hawkes model of the LOB.

## 5. lee2025crossasset — Lee, Hwang, Kim, Yang, Lee & Kim, ICAIF '25, pp. 665–673 — ABSTRACT ONLY

DOI 10.1145/3768292.3770432. Abstract: multi-stage framework for intraday KOSPI 200 index direction:
(i) select 30 constituents, (ii) forecast each constituent's future OFI sequence, (iii) fuse predicted OFI
tensor with a "Stock-Time Attention" block to classify short-term index direction. OFI is a hand-defined
target/input, not a structural layer → boundary case (multi-stage). Numbers unverified.

---

## 6. Boundary / out-of-scope items checked (with reason)

- **fabre2025spoof** (existing key) — Fabre & Challet, arXiv 2504.15908v1 — FULL TEXT. Hawkes-*inspired*
  features: L_t(ϕ) = ∫ ϕ(t − s, v_s, δ_s) dN_s with ϕ_{β,η}(t,v,δ) = e^{−βt} f(v) e^{−ηδ} (Eqs. 5–12), but β ∈
  {10,100,1000} s⁻¹ and η ∈ {0.001,0.1,1,10} bp⁻¹ are **fixed grids, not learned** (Sec. 2.2.3) → 31
  hand-made features into a 1-hidden-layer, 64-neuron MLP that outputs skew-Gaussian parameters of the
  1-s mid-price move (Eq. 20), trained by NLL. Purpose: spoofing detection. Data: Coinbase BTC-USD,
  ETH-USD L3; training 1,000,000 + validation 1,000,000 orders sampled from 1–3 Dec 2022 (Sec. 2.2.3).
  No baselines for prediction accuracy. Inconsistencies: figures report results "over the test set"
  (Figs. 1–5) but Sec. 2.2.3 defines only training and validation sets; the data section says Dec 1–7, 2022,
  while detection runs on 88,327,661 orders of Dec 4–7, **2024** (Sec. 3). → boundary (fixed-kernel features).
- **cestari2025hawkes** (existing key) — arXiv 2312.16190 — FULL TEXT skimmed. Two-stage: Hawkes model
  forecasts next event time, then a continuous-output-error (COE) system-identification model forecasts
  return sign; no neural network; retrained on rolling windows (Hawkes T_train = 20 min, COE 50 min,
  Table 3). Out of scope (not a network; not end-to-end).
- **zheng2024lobdif** — LOBDIF (arXiv 2412.09631v1) — FULL TEXT. Diffusion model for next event
  type/time; no microstructure structure. Useful only as an independent re-run of PCT-LSTM (Sec. 1.8).
  Journal version: Data Science and Engineering 11(3):706–723 (2026), DOI 10.1007/s41019-025-00328-4,
  whose Crossref author list has 4 authors (no D. Ouyang) vs 5 on arXiv; I read only the arXiv v1.
- **joseph2022snh** — Shallow Neural Hawkes, J. Comput. Sci. 63:101754 (2022); arXiv 2006.02460 —
  FULL TEXT (arXiv). One-hidden-layer network as non-parametric Hawkes kernel; real data = 120,000
  Binance BTC market orders, 8 May 2020 (Sec. 5.2); reports in-sample total NLL −40143 (SNH) vs −33127
  (EM) vs −29698 (Wiener–Hopf), plus k-fold CV. Estimation, not prediction of price/events → out of scope.
- **fabre2025neuralhawkes** — Fabre & Muni Toke, QF 25(5):671–698 (2025); arXiv 2401.09361v3 — abstract
  and contents read. Uses physics-informed neural networks to solve the Hawkes moment (Fredholm)
  equation for kernel estimation; applications are kernel/causality analysis on crypto. Not prediction.
  (Only "physics-informed" + order-book hit found.)
- **lee2023rnnhawkes** — K. Lee, FRL 55:103922 (2023); arXiv 2304.11883 — abstract plus keyword-searched passages of the full text (method and training description) read.
  3-layer LSTM trained on simulated Hawkes paths to output Hawkes parameters (amortised MLE), used for
  real-time volatility. Network outputs classical parameters, but the target is parameter estimation, not
  forecasting → boundary.
- **lee2026srpp** (arXiv 2604.00346) — duration forecasting with a self-exciting flexible-residual point
  process, AAPL Dec 2022, out-of-sample tests incl. Diebold–Mariano (Tables 3–4). No neural network → out.
- **hu2021nhstock** — Hu, Ji, Xie, Yu, IEEE ICICN 2021, pp. 436–439 — ABSTRACT ONLY. Neural Hawkes for
  stock price "movement" events; abstract reports sequence log-likelihood −0.6358 / −2.3878 (5-/10-day)
  vs Hawkes −4.3243 / −4.5841 and inhibition Hawkes −11.353 / −24.8147. Horizon in days → not HF/LOB;
  data and splits unverified.
- **tan2026hawkesattn** — "From Hawkes Processes to Attention", arXiv 2601.09220 — datasets checked:
  StackOverflow, Amazon, Taxi, Taobao etc.; no financial data → out of scope for this survey.
- Blakely, arXiv 2411.13594 — partially read: Stoikov micro-price plus an error-correction term from a
  hyperdimensional-vector **Tsetlin machine** (not a neural network); two-stage → out.
- Rahman & Upadhye, arXiv 2411.08382 — abstract only: VAR then FNN on VAR residuals to forecast OFI
  (two-stage stacking) → boundary/out.
- Kumar, "Deep Hawkes process for high-frequency market making", J. Banking & Financial Technology
  8(1):11–28 (2024) — abstract only: simulation + market-making agent, not prediction → out.
- Lalor & Swishchuk (lalor2025nhp, existing) and Shi & Cartlidge NS-ABM (ISAFM 31(2), 2024) — simulation.
- **kolm2023** (existing key) — Deep OFI: OFI features fed to standard networks — boundary per task
  definition; not re-read here.

## 7. Searches that returned nothing relevant
arXiv listing search and OpenAlex title/abstract search for: "physics-informed"/"finance-informed"/
"theory-guided" + order book (prediction); microprice + neural; queue imbalance + neural; Kyle/Glosten +
neural + order book; "order flow imbalance" + "end-to-end"; "order book" + "inductive bias";
Hawkes + attention + stock (only text/social-media models, e.g. AAAI 2021 hypergraph attention, not LOB).
No paper was found that builds an OFI/queue/micro-price/Kyle layer *inside* an end-to-end network for LOB
prediction. This is a negative result of a limited search (no Google Scholar/SSRN full-text search was
possible in this session), not proof of absence.

## 8. Ranking (fit to "structural hybrid, used for prediction, full text read")
1. shi2022sdpnhp — end-to-end neural-Hawkes, real LOBSTER data, prediction metrics vs classical
   state-dependent Hawkes and vs plain LSTM, ablations of state and of the parallel structure; full text.
   Weakness: target is next event type/time (not price); no split/dates/θ/variance reported.
2. gyotoku2025deeppp — classical ratio-model structure with network-parameterised factors, structure
   ablation (one-step vs two-step), full text; but real-data evidence is an in-sample fit figure only.
3. hwang2025shockbiased — best conceptual match (Hawkes-decay bias inside attention, financial event
   prediction) but abstract only.
4. chung2024nmhp — metadata only.
Boundary: fabre2025spoof (fixed-kernel Hawkes features → MLP), kolm2023 (OFI features → nets),
lee2023rnnhawkes (network outputs Hawkes params, estimation not forecasting), lee2025crossasset,
Rahman & Upadhye (VAR + FNN residual stacking), cestari2025hawkes (Hawkes + COE, no network).
