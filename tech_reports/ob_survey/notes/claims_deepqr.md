# Verified claims: Bodor & Carlier, deep queue-reactive (MDQR)

Source: FULL TEXT, arXiv 2501.08822v1 (15 Jan 2025), downloaded 2026-10-03 and read in full via pdftotext.
Bib key: `deepqr` (bib/refs_dl.bib).
Authors: Hamza Bodor (Univ. Paris 1 / BNP Paribas), Laurent Carlier (BNP Paribas Global Markets Data & AI Lab).

## Model chain (Sec. 3–4)

- **QR (Huang et al. 2015).** Intensities λ^η(q), η ∈ {L, C, M}, depend on the queue size q.
  - Total rate Λ(q) = λ^L + λ^C + λ^M.
  - Likelihood: Π e^{−Λ(q_k)Δt_k} λ^{η_k}(q_k).
  - The MLE is closed form, Eq. (1): λ̂^η(n) = [#events of type η at q = n / #events at q = n] × [1 / mean Δt at q = n].
- **SAQR (Bodor & Carlier 2024, arXiv 2405.18594).** Adds sizes: λ̂^{η,s}(n), Eq. (2).
- **DQR (Sec. 3.1).** Replaces q_k with a state vector x_k and parameterises the intensities λ^η_θ(x_k) with a neural network.
  - The likelihood has the same form. Training minimises the NLL: Σ Λ_θ(x_k)Δt_k − log λ^{η_k}_θ(x_k).
  - Applies to a single queue.
- **MDQR (Sec. 4).** Each event is e_k = (η_k, ℓ_k, Δt_k, s_k, x_k), with ℓ ∈ {−K…−1, 1…K}.
  - The likelihood factorises, Eq. (3), into p(η, ℓ, t | x) × p(s | η, ℓ, t, x).
  - The first factor is a conditional Poisson: ∝ e^{−ΛΔt} λ^{(η,ℓ)}(x), with Λ = Σ_η Σ_ℓ λ^{(η,ℓ)}.
  - Losses: NLL Eq. (4); sizes by categorical cross-entropy, Eq. (5), with C = 200 classes (one per integer size 1..200), because >99.9% of sizes are < 200.
  - The size factor "is independent of the timing and category-level intensities, allowing it to be modeled separately".
  - Δt in MDQR is measured between all book events, not per queue (Sec. 4.2).

## Data (Sec. 3.2)

- Euro-Bund futures FGBLM2: active days March–June 2022, 9:00–18:00, 5 levels per side.
- Table 1 (events per level):
  - level 1: #L 32.9M, #C 30.1M, #M 2.12M (212 ×10^4), AES 6.25, AIT 57.3 ms;
  - level 2: #M 0.51 ×10^4;
  - levels 3–5: #M = 0.
- Queue sizes normalised as ⌈q / AES⌉. Segments of constant reference price; inter-event times are reset when it changes.
- New levels are initialised from their empirical distributions after a reference-price change.

## Architecture / training

- **DQR.** MLP with hidden layers [128, 32], tanh, and an output of dim 3 with ReLU (intensities L, C, M).
  - Batch norm between dense layers; categorical embeddings of dim 2.
  - Split 80/20 train/validation; Adam with cyclic LR 1e-5..1e-3; early stop after 10 epochs without improvement.
- **MDQR (Table 4).**
  - Intensity net: input 25, hidden [256, 64] tanh, output 30 (3 types × 10 levels) with ReLU, loss Eq. 4.
  - Size net: input 27, hidden [256, 64] tanh, output 200 softmax, loss Eq. 5.
  - Training stopped around epoch 85 (Fig. 5).
- **Features (Table 3).**
  - Queue sizes q_i (log-transformed).
  - Spread in ticks.
  - Trade imbalance TI_τ = (V^b − V^a)/(V^b + V^a) over τ ∈ {20 s, 1 min, 5 min, 15 min}.
  - Last event type e_i per level (embedding 2, cardinality 3).
  - Hour (embedding 2, cardinality 9).

## Results

- **DQR excitation (Fig. 1, transition matrix, rows = previous type, cols = cancel/limit/trade).**

  | Previous | Real | SAQR/QR | DQR |
  |---|---|---|---|
  | cancel | .73 .24 .03 | .44 .53 .03 | .66 .30 .04 |
  | limit | .22 .76 .02 | .44 .53 .03 | .24 .74 .02 |
  | trade | .51 .19 .30 | .42 .54 .04 | .46 .29 .25 |

  "SAQR model: Rows are uniform". State x_k = [q_k, η_{k−1}].
- **DQR ablation (Fig. 3).** Vanilla q → +hour → +last event → both. The paper says both features give the highest log-likelihood and balanced accuracy and the lowest relative time error. Exact bar values were not extracted.
- **Impact (Figs. 7–8).** TWAP agent: 100% of average 5-min volume, 10 child orders, one every 30 s.
  - Without TI features: linear rise, then a plateau.
  - With TI: a concave rise, then relaxation that stabilises ~10 min after execution, at about a 20% reduction ("market-specificity cannot be verified without meta-trade data").
  - Max impact vs inventory: power-law fit x^0.55, R² = 0.89. Average 5-min volume ≈ 4,000 lots.
- **Cross-side transition matrix at the best prices (Fig. 9).** MDQR is close to real; QR/SAQR rows are similar.
- **Mid-price prediction (Sec. 4.6, Table 6).** Horizon k = 500 events, threshold Δr = 20 bp, 3 classes.
  - Simulators generate 500 forward events from each observed state.
  - DeepLOB: one month of data, 2-day validation, 3-day test.
  - Balanced accuracy / F1: DeepLOB 0.54±0.01 / 0.56±0.01; QR 0.56±0.01 / 0.55±0.02; SAQR 0.58±0.03 / 0.58±0.03; MDQR 0.63±0.02 / 0.62±0.02.
  - The test period for the simulator predictions is not stated separately.
- **Queue-size gamma fit, best ask (Table 7), α and 1/β.**

  | Model | α | 1/β |
  |---|---|---|
  | Real | 1.35±0.18 | 183.44±31.79 |
  | QR | 3.08±0.13 | 83.43±3.06 |
  | SAQR | 1.91±0.06 | 110.49±3.14 |
  | MDQR | 1.30±0.13 | 172.00±12.26 |

- **Queue-volume correlations (Fig. 12), best bid vs best ask.** Real −0.54, QR −0.22, SAQR −0.43, MDQR −0.54.
  - Real same-side correlations are "typically 0.3 to 0.5".
  - QR's are mostly near 0; MDQR's are similar to real.
- **Returns (1-min) and event counts/volumes per 5 min (Figs. 13–15).** MDQR is closer to real, especially in tails and variability. QR/SAQR std is "considerably smaller".
- **Speed (Table 8).** Per-event inference: MDQR 0.037±0.001 ms; LOBGAN 0.217; RNN (Hultin) 1.152; WGAN 0.144 ms.
  - Daily generation (1.5M events): 0.92 / 5.43 / 28.85 / 1.95 min.
  - INCONSISTENCY: the text says MDQR takes "1.95 minutes", but its table says 0.92 (1.95 is the WGAN row).
  - Times exclude the matching engine. Hardware: AMD EPYC 7413.
- **Fill ratio study (Sec. 4.8).**
  - Grid: q_order ∈ {1, 2, 5, 10, 20, 50, 60}, τ ∈ {1, 5, 10, 30, 60, 300} s, level ∈ {0..4}, about 2,500 orders per cell.
  - Correlation with fill ratio: level −0.79, period 0.32, quantity −0.10.
  - Feature importance: Random Forest 0.728 / 0.252 / 0.020; SHAP 0.197 / 0.080 / 0.022.

## Limitations stated by the authors

- Not yet validated for small-tick assets (Sec. 5, Sec. 6).
- The state space is hand-crafted. It "required several iterations before incorporating trade imbalance features crucial for accurate market impact profiles" (Sec. 5).
- Impact relaxation magnitude: "market-specificity cannot be verified without meta-trade data".
