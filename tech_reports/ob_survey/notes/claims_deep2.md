# Claims & paper notes — deep LOB forecasting, part 2 (single-asset, multi-asset, benchmarks)

Verified 2026-10-03. Sources: arXiv API + arXiv PDFs (text-extracted with pdftotext), Crossref API
metadata. Read levels: **FULL TEXT** = the arXiv PDF was read (the version is stated; the journal
version was not read unless said so); **ABSTRACT ONLY** = abstract from arXiv or Crossref;
**METADATA ONLY** = Crossref bibliographic record only. Numbers are copied from the stated table or
section. Anything not confirmed is marked UNVERIFIED. Nothing is filled in from memory.

Items already covered in `notes/claims_dl.md` / `notes/claims_meta.md` (DeepLOB, TransLOB, TABL, DAIN,
DeepLOB-Attention/Seq2Seq, LOBCAST, LOBFrame, Lucchese, Kolm, Sirignano–Cont, Axial-LOB, TLOB/MLPLOB,
LiT, HLOB, OF-MATNet, LOBERT, UQ-LOB, etc.) are not repeated; they are referenced by key where
relevant. New BibTeX keys are in `bib/refs_deep2.bib`.

---

## Part 1 — Paper notes

### C. Benchmarks, re-tests, and Transformer-vs-simpler comparisons

**zhong2025lobench — LOBench** (Zhong, Lin, Yang, arXiv 2505.02139v1, 4 May 2025). **Preprint.** **FULL TEXT (arXiv v1).**
- Existence: VERIFIED (the task named "Representation Learning of Limit Order Book: A Comprehensive Study and Benchmarking", arXiv 2505.02139, is real).
- What it is: a benchmark for LOB *representation learning* (encoder → 256-d latent → simple decoder/head), not only for forecasting. Tasks: reconstruction, mid-price trend classification, imputation (§3.4).
- Data: Shenzhen Stock Exchange, full year 2019; 10-level snapshots sampled every 3 s; continuous-auction hours only. Five stocks: sz000001, sz000002, sz000858, sz002415, sz300147 (Table 2). Released data are "desensitized LOB snapshot sequences" (§3.1).
- Models (Table 3): CNN2, LSTM, Transformer, iTransformer, TimesNet, TimeMixer, DeepLOB, TransLOB, SimLOB. All re-implemented in PyTorch Lightning with extra FC layers so that every latent is 256-d; 100 epochs, Adam (§4). Parameter counts in Table 3 are 15–49 M (e.g. DeepLOB 15.42 M, TransLOB 49.44 M), i.e. far larger than the original DeepLOB (~60 k parameters per claims_dl.md) — the re-implementations are not the original-size models.
- Normalisation: argues feature-wise z-score breaks the price-level ordering; proposes a per-snapshot "global" z-score over all prices / all volumes (§3.2.3, Figs 3–4).
- Prediction target: 3-class smoothed mid-price trend with Δt = 5 and δ = 0.0001; train **and test** sets are class-balanced by down-sampling (§4.2.1).
- Results: reconstruction — "models incorporating Transformer architectures consistently outperform"; TimesNet best overall (Table 5). Prediction/imputation — "DeepLOB and LSTM consistently exhibit the poorest performance" (§4.2.2, shown only as training curves, Figs 8–9; no test-accuracy table for the single-stock prediction task).
- Transfer (Table 6): train on sz000001, test on the other four stocks. Mean recall / mean precision: DeepLOB 0.6814 / 0.7078, iTransformer 0.6682 / 0.7034, TransLOB 0.6568 / 0.6873, LSTM 0.6611 / 0.6725, CNN2 0.6048 / 0.6289; SimLOB with frozen encoder and decoder fine-tuned on ~20% of target data: 0.7260 / 0.7332. Note: in this transfer table DeepLOB has the best mean among end-to-end models, which sits awkwardly with the §4.2.2 statement that DeepLOB performs worst on prediction.
- Limitations stated: only five stocks; raw order flow cannot be released; dataset to be expanded later (§3.1).
- Independent re-tests: none found.

### B. Multi-asset / cross-asset models

**chen2022gtnvf — GTN-VF, Graph Transformer Network for volatility** (Chen & Robert, ICAIF '22, pp. 156–164; arXiv 2112.09015v2). **Peer-reviewed (ACM ICAIF).** **FULL TEXT (arXiv v2).**
- Task: multivariate short-term realized volatility (RV) forecasting over the next ΔT ∈ {600, 1200, 1800} s; loss and metric RMSPE.
- Input: NYSE Daily TAQ, sampled at 1 s; **level-1 only** (best bid/ask price and size, plus trade count/volume/price; features in its Appendix A). So "LOB data" here means top-of-book quotes.
- Multi-asset mechanism: a graph whose nodes are (stock, time) pairs; edges from (i) temporal feature correlation, (ii) cross-sectional feature correlation, (iii) GICS sector/industry, (iv) FactSet supply-chain links. A 3-layer, 8-head Graph Transformer (128 channels) passes messages over these edges; weights are shared across all stocks (one model for the universe).
- Data: ~494 S&P 500 stocks; train Jan-2017–Dec-2019, val Jan–Dec 2020, test Jan–Oct 2021 (Table 2; 2.14 M / 0.74 M / 0.61 M buckets at ΔT=600).
- Headline (Table 4, test RMSPE, ΔT = 600/1200/1800): Naïve 0.2834/0.2628/0.2364; HAR-RV 0.2612/0.2061/0.1939; LightGBM 0.2492/0.2035/0.1963; MLP 0.2514/0.2308/0.1999; TabNet 0.2478/0.1996/0.2019; Vanilla GTN-VF (no relations) 0.2498/0.2251/0.2160; full GTN-VF 0.2287/0.1892/0.1798. Text: "on average, we gain 6% in RMSPE compared with Naïve Guess and 2% compared with the best baseline model TabNet on test set" (§5.4).
- Cross-asset evidence: relations help relative to the same model without them — e.g. ΔT=600 test RMSPE 0.2498 (vanilla) → 0.2287 (all relations). The single most useful relation is *temporal* feature correlation (0.2358 alone), not a cross-sectional one; cross-sectional feature correlation alone gives 0.2382, sector 0.2422, supply chain 0.2411 (Table 4). Note: at ΔT=1200 HAR-RV (0.2061) and TabNet (0.1996) beat Vanilla GTN-VF (0.2251), so the graph model without relations is not better than simple baselines.
- Ablations: gains larger for more liquid stocks ("around 8% for more liquid stocks and 2% for less liquid stocks" vs naïve, §5.5.1); more-connected nodes have ~2% better RMSPE (§5.5.2).
- Limitations: Sector-level graph did not fit in memory (Table 5). No significance tests reported.
- Independent re-tests: none found.

**bilokon2023transformers — Transformers vs LSTMs** (Bilokon & Qiu, arXiv 2309.11400v1, 2023; SSRN 4577922). **Preprint** (no journal/proceedings version found on Crossref). **FULL TEXT (arXiv v1).**
- Data: Binance crypto LOB via cryptofeed. Task 1 (mid-price level): one day BTC-USDT, 2022-07-15. Task 2 (mid-price difference): four days BTC-USDT. Task 3 (mid-price movement, 3-class, smoothed labels à la Tsantekidis/DeepLOB): 12 days ETH-USDT 2022-07-03…07-14, 10,255,144 ticks; first 6 days train, last 3 test (§5.3.3). Single asset per task.
- Models (task 3): MLP, LSTM, DeepLOB, DeepLOB-Seq2Seq, DeepLOB-Attention, Transformer, Reformer, Informer, Autoformer, FEDformer, and the authors' DLSTM (Autoformer-style trend/remainder decomposition + two LSTMs).
- Headline: Task 1 — Transformer-based methods have "around 10%−25% prediction error less" than LSTM but "insufficient for trading"; Task 2 — LSTM better, best out-of-sample R² "around 11.5%" (§1). Task 3 accuracy (Tables 3–4), k = 20/30/50/100: DLSTM 73.10/70.61/67.45/63.73; DeepLOB 70.29/67.23/63.32/58.12; DeepLOB-Attention 70.04/67.21/64.05/59.16; best Transformer-family model 68.89 (Autoformer, k=20), 67.93 (Autoformer, k=30), 63.46 (FEDformer, k=50), 59.18 (Autoformer, k=100); vanilla Transformer 67.80/64.25/59.51/55.42; MLP 61.58/59.19/55.65/57.03.
- Trading sim (Tables 5–6): executed at mid, no impact; with 0.002% cost "LSTM-based methods' performance is generally better than Transformer-based methods", and Autoformer gives negative CPR/SR at k=50 and 100.
- Data-quality flag: the LSTM row at k=50 (62.77/62.91/62.77/62.78) is identical to the LSTM row at k=20 in Table 3 — likely a copy error; not explained in the paper.
- Limitations stated: simulation "not … a fully developed high-frequency trading strategy"; Transformer models are large and hard to tune.
- Bearing on "Transformer vs simpler": on this one crypto pair, generic time-series Transformers did not beat DeepLOB-family CNN-LSTMs on direction classification; a decomposition+LSTM model was best.

**shabani2023abn — Augmented Bilinear Network, cross-stock transfer** (Shabani, Tran, Kanniainen, Iosifidis, Pattern Recognition 141:109604, 2023; arXiv 2207.11577v1). **Peer-reviewed.** Crossref also lists a Corrigendum (Pattern Recognit., doi 10.1016/j.patcog.2025.113026; not read). **FULL TEXT (arXiv v1).**
- Question: adapt a model pre-trained on some stocks to new stocks without access to the old data. Method: freeze the pre-trained TABL (or 1-D CNN) weights and add low-rank auxiliary connections trained on the new stock ("aTABL", "aCNN").
- Data: FI-2010 (5 Helsinki stocks, 1–14 June 2010), 10 most recent events × 40 features (10 levels), horizon H = 10 events, first 7 days train / last 3 days test; metric F1 (§4.1).
- Setup 1 (leave-one-stock-out, Table 2, TABL family, average over the 5 target stocks, F1 %): TABL trained from scratch on all 5 stocks 66.27 ± 3.50; **TABL-base trained on the other 4 stocks only (zero-shot on target) 65.62 ± 4.90**; TABL-fine-tune 68.05 ± 2.40; aTABL-IS1 69.10 ± 2.70. For stocks 1 and 2 the zero-shot model beats the pooled-from-scratch model (63.98 vs 62.08; 70.31 vs 69.43); for stock 5 it is much worse (57.81 vs 63.39).
- CNN family (Table 3, average F1): CNN (all stocks) 63.19; CNN-base 62.24; CNN-fine-tune 61.30; aCNN 63.60. Plain fine-tuning *degraded* the CNN on stocks 3 and 5.
- Multi-asset reading: assets interact only through shared weights learned on other stocks (transfer), not through cross-asset inputs. Evidence: on FI-2010, a TABL trained on 4 stocks is within ~0.7 F1 points on average of one trained on all 5 including the target; low-rank adaptation adds ~3 points.
- Limitations: FI-2010 only (10 days, 5 stocks, 2010). Independent re-tests: none found.

### A. Single-asset architectures (CNN / LSTM / attention / bilinear / Transformer / other)

**makinen2019jump — CNN-LSTM-Attention for jump arrivals** (Mäkinen, Kanniainen, Gabbouj, Iosifidis, Quantitative Finance 19(12):2033–2050, 2019; arXiv 1810.10845v1). **Peer-reviewed.** **FULL TEXT (arXiv v1).**
- Target: binary — does a return jump (Lee–Mykland nonparametric test on mid-price) occur in the next one-minute interval. Also a separate jump-direction experiment (Table 12).
- Input: NASDAQ TotalView-ITCH, 5 stocks (AAPL, FB, GOOG, MSFT, INTC), 360 trading days in 2014–2015; features over the past 120 minutes (LOB prices/volumes, order-flow intensities, time of day; Table 4).
- Architecture: CNN → LSTM → attention over time steps (CNN-LSTM-A). One model per stock (single-asset).
- Protocol: expanding window — train on 50, 100, …, 350 days and test on the next 10 days (7 test sets, Table 2). Jump samples are duplicated (shifted by seconds) to increase their number "in all datasets" (§2.1) — the random classifier's precision of 0.24 in Table 5 shows the evaluated sets are far from the natural ~3 jumps/day/stock rate.
- Headline (Table 5, overall): CNN-LSTM-A precision 0.66, recall 0.80, **F1 0.72, Cohen's κ 0.62**; LSTM F1 0.69 (κ 0.60); CNN 0.66 (0.55); MLP 0.53 (0.44); random 0.32 (0.00). **Time-of-day-only CNN-LSTM ("v10") F1 0.66, κ 0.54** — i.e. most of the skill comes from intraday seasonality of jumps (they cluster in the first half hour); LOB features add 0.06 F1. Average F1 across sets 0.71; INTC best (0.78), MSFT ~10 points lower (§4).
- Stated conclusion: LOB data improves jump prediction "either clearly or marginally, depending on the underlying stock"; direction of jumps is "much more difficult" to predict.
- Independent re-tests: none found.

**tran2021bin / tran2021binext — BiN (Bilinear Input Normalization) and BiN-C(TABL)** (Tran, Kanniainen, Gabbouj, Iosifidis; ICPR 2020 proceedings, pp. 7287–7292, published Jan 2021 = arXiv 2003.00598; extended preprint arXiv 2109.00983, 2021). **ICPR version peer-reviewed; extended version preprint.** **FULL TEXT of both arXiv versions.**
- Architecture: a learnable input-normalization layer that normalises along both the temporal and the feature axes of the D×H input (bilinear), placed in front of the C(TABL) temporal-attention bilinear network (tran2019tabl) or DeepLOB. This is the "BiN-CTABL" model benchmarked in LOBCAST (prata2024).
- Data: FI-2010 (10 events × 40 features, H ∈ {10, 20, 50}, 7 days train / 3 days test). The extended version adds NASDAQ TotalView-ITCH data for Amazon and Google, 22 Sep–5 Oct 2015 (~13 M events, 10 days; 7/3 split).
- Headline (FI-2010, Table I, F1 %): BiN-C(TABL) 81.04 (H=10), 71.22 (H=20), 88.06 (H=50); C(TABL) with z-score 77.63 / 66.93 / 78.44; DeepLOB 83.40 / 72.82 / 80.35 (as reported by DeepLOB). Text: BiN improves C(TABL) at H=50 "from 78.44% to 88.06%". BiN-B(TABL) (one hidden layer, +102 parameters) matches BiN-C(TABL) (Table II). Same FI-2010 numbers appear in both versions.
- Protocol flag (extended version only, §IV-B): "we used no validation set for FI-2010, and simply used the F1 score measured on the train set for validation purposes." The ICPR text does not contain this sentence.
- US-data results (extended version, Tables IV–V): reported, but the PDF table text did not extract cleanly; individual numbers UNVERIFIED here.
- Independent re-tests: LOBCAST (prata2024) re-ran BiN-CTABL on FI-2010 and on LOBSTER stocks — see claims_dl.md D3/D6 for those numbers (not repeated here).

**shabani2022mtabl — MTABL, multi-head temporal attention bilinear layer** (Shabani, Tran, Magris, Kanniainen, Iosifidis, EUSIPCO 2022, pp. 1487–1491; arXiv 2201.05459v1). **Peer-reviewed (conference).** **FULL TEXT (arXiv v1).**
- Architecture: replaces the single temporal attention of TABL with K = 2…5 attention heads (outputs concatenated or averaged).
- Data/protocol: FI-2010, first 40 features (10 levels), H = 10 only; TABL protocol (7/3 days); mean ± std over 4 runs.
- Headline (Table 1, F1 %): topology A (one TABL layer) 54.25 → 60.90 with 5 heads; topology B 69.10 → 69.16 (best, K=5); topology C (two BL + TABL) 76.01 → 76.42 (best, K=4). Authors: in B and C "the improvement is not significant".
- Bearing: multi-head attention helps the smallest network; for the strongest topology the gain is 0.4 F1 points on one horizon of FI-2010.

**tran2022atnbof — 2D-Attention Neural Bag-of-Features (ATNBoF)** (Tran, Passalis, Tefas, Gabbouj, Iosifidis, IEEE Access 10:45542–45552, 2022; arXiv 2005.12250v1). **Peer-reviewed.** **FULL TEXT (arXiv v1).** This is the "ATNBoF" model benchmarked in LOBCAST.
- Architecture: (temporal) neural bag-of-features quantisation with a plug-in 2-D attention block (codeword attention "CA" or temporal attention "TA"); optional 1-D convolution front end. Also evaluated on audio and medical data.
- Data/protocol: FI-2010, H ∈ {10, 20, 50}, 7/3 days; averaged F1.
- Headline (Table I, F1 % with conv front end): H=10 best NBoF-TA 67.98 (GRU 62.21); H=20 best NBoF-TA 60.10 (GRU 53.83); H=50 best TNBoF-CA 73.77 (GRU 65.93). Without conv layers, GRU beats every NBoF variant (e.g. H=10 GRU 60.92 vs best 45.97).
- Note: LOBCAST lists ATNBoF as the largest model it re-ran (~1.3·10⁷ parameters, per claims_dl.md).

**passalis2020tlonbof — Temporal Logistic Neural BoF (TLoNBoF)** (Passalis, Tefas, Kanniainen, Gabbouj, Iosifidis, Pattern Recognition Letters 136:183–189, 2020; arXiv 1901.08280v1). **Peer-reviewed.** **FULL TEXT (arXiv v1).** The "TLONBoF" model in LOBCAST. (A related conference paper, ICASSP 2019, doi 10.1109/ICASSP.2019.8682297, and the journal "Temporal Bag-of-Features Learning…", IEEE TETCI 4(6):774–785, 2020, doi 10.1109/TETCI.2018.2872598, exist — METADATA ONLY, not added.)
- Architecture: Conv1D(256, k=5) → logistic-kernel neural BoF with 3 temporal segments → FC(512) → FC(3) (Table 1).
- Data/protocol: FI-2010 hand-crafted 144-d features (453,975 vectors), **anchored day-by-day** setup (train on days 1..d, test on day d+1), H = 10.
- Headline (Table 3, macro-F1 %, κ): TLo-NBoF 52.98 ± 2.37 (κ 0.290); GRU 50.55 (0.256); LSTM 49.51 (0.240); CNN 47.19 (0.219); MLP 36.91 (0.128). These anchored-setup F1s (~50%) are much lower than the ~80% F1s reported under the 7/3-day setup on the same dataset — the protocol, not only the model, drives FI-2010 numbers.

**zhang2018bdlob — BDLOB** (Zhang, Zohren, Roberts, arXiv 1811.10041v1; Third Workshop on Bayesian Deep Learning, NeurIPS 2018 per arXiv comment). **Workshop paper (not archival proceedings).** **FULL TEXT (arXiv v1).**
- Architecture: DeepLOB-style CNN with MC-dropout variational inference; predictive uncertainty used for position sizing.
- Data: LSE, LLOY/BARC/TSCO/BT/VOD, all of 2017 (>134 M quotes); 6 months train / 3 val / 3 test. Prediction horizon: not found in the text — UNVERIFIED.
- Headline (Table 1): precision / recall / F1 / AUC — CNN 0.52 / 0.52 / 0.48 / 0.672; DeepLOB5 0.60 / 0.60 / 0.58 / 0.803; BDLOB 0.60 / 0.61 / 0.60 / 0.811. Trading sim at mid, no transaction costs.

**zhang2019qr — DeepLOB-QR, quantile regression** (Zhang, Zohren, Roberts, arXiv 1906.04404v1; Time Series Workshop, ICML 2019). **Workshop paper.** **FULL TEXT (arXiv v1).**
- Target: returns that *include the spread* — separate long-position (buy ask, sell bid) and short-position returns at horizon k; quantiles 0.25/0.5/0.75, combined into a point forecast ("QR(C)").
- Data: same LSE 5 stocks, 2017, 6/3/3 months.
- Results (Table 2, k = 50/100/200): the PDF table did not extract with reliable column alignment; the column consistent with R² holds values 0.010–0.044 (UNVERIFIED alignment). Stated: performance worsens as k increases; combining quantiles helps.
- Bearing: one of few deep-LOB papers whose target is a spread-crossing (tradable) return rather than mid-price direction.

**spears2021eurodollar — multivariate CNN-LSTM for the Eurodollar futures curve** (Spears, Zohren, Roberts, J. Financial Data Science 3(1):57–73, 2021; arXiv 2007.15982v1). **Peer-reviewed.** **FULL TEXT (arXiv v1).**
- Multi-asset by construction: input and output are the joint microprice moves of 9 CME Eurodollar contracts EDc7–EDc15 (level-1 only), aligned and down-sampled whenever any contract's microprice moves ≥ 0.1 bp; data = every trading day of 2018 (Thomson Reuters).
- Architecture: DeepLOB's CNN-LSTM-Inception ("CNN-LSTM Inc", ~100 k parameters) vs a 2-layer MLP (~133 k), both with a second head that outputs a full covariance (Cholesky-parameterised, "aleatoric") plus MC-dropout ("epistemic") uncertainty. Assets interact through a shared network on the joint input and a learned output covariance.
- Protocol: expanding window by month — train months 1..M−1, validate M, test M+1, M ∈ {7..11}.
- Headline: CNN-LSTM Inc beats MLP on MSE in 3 of 5 months (one-tailed t-test p ≤ 0.0001); "On average across all months, the CNN-LSTM Inc MSE is 2.5% smaller" (§3). Uncertainty-scaled sizing improves out-of-sample Sharpe vs unscaled (Table 3; cumulative 5-month Sharpe values 1.25–1.42 across strategies, column alignment from text extraction UNVERIFIED).
- Limitations stated: only 9 contracts, level 1 only; "one could also include deeper order book data".
- No single-asset vs multi-asset ablation is reported, so the paper does not show that the cross-contract input helps.

**magris2023bbnn — Bayesian bilinear network (B-TABL)** (Magris, Shabani, Iosifidis, J. Forecasting 42(6):1407–1428, 2023; arXiv 2203.03613v2). **Peer-reviewed.** **FULL TEXT (arXiv v2).**
- Architecture: TABL trained as a Bayesian network with the VOGN second-order variational optimiser; compared with ADAM, MC-dropout and SGD.
- Data: FI-2010 144-d features, H = 10, last 3 days test (150,418 samples).
- Headline: VOGN and ADAM reach nearly the same point performance (Table II values ≈ 0.774 vs 0.772 in the leading column; exact column labels not recoverable from the text extraction — UNVERIFIED); MC-dropout and SGD are much worse. The contribution is calibrated predictive distributions (Table IV: AUROC, ECE), not higher accuracy.

**lee2024predictability — what DeepLOB's accuracy is made of** (K. Lee, Applied Economics Letters 33(8):1105–1111, issue dated 2026 per Crossref, online 2024; arXiv 2409.14157v1). **Peer-reviewed.** **FULL TEXT (arXiv v1).** Key re-test.
- Model: DeepLOB exactly as in zhang2019 (Table 1), plus a smaller variant for level-1 input (Table 4). Data: AAPL, NASDAQ TotalView-ITCH, all of 2022, regular hours (~3 M observations); for each test day train on the previous 20 days.
- Finding 1 — label leakage in the smoothed target: with the DeepLOB/Tsantekidis target r₂₀ = (mean of next 20 mid-prices − mean of last 20)/mean of last 20, DeepLOB reaches **65.9%** daily-average accuracy, but a **naive forecast that uses the last observable value of the same smoothed series reaches 64.8%** (Table 2). "r20 inherently contains future information"; overlapping windows make the target "naturally predictable".
- Finding 2 — with a target that starts from the current price, r₁,₂₀ = (mean of next 20 − current)/current, accuracy falls to **54.6%** (full 10-level LOB, Table 3); **level-1 only gives 53.6%** (Table 5) — "LOB data beyond Level 1 has little impact on mid-price prediction".
- Finding 3 — decomposition: of the 53.6%, "volatility accuracy" (STABLE vs not) is 69.4% and "directional accuracy" within correctly-flagged moves is 71.1%. With level-1 *prices only*, directional accuracy is ~50% (Fig. 3); volatility accuracy 67.5%. Volume (imbalance) is what carries direction.
- Single asset, one stock, one year. No trading-cost analysis.
- Bearing: a large part of the published 3-class accuracy on smoothed labels can be reproduced by a naive persistence forecast; depth beyond level 1 added ~1 point on AAPL.

**yang2025siamese — Siamese bid/ask weight sharing; OFI vs raw LOB** (Yang, Fang, Zhang, Zhou, arXiv 2505.22678v1, 2025). **Preprint** (an earlier SSRN posting, doi 10.2139/ssrn.4476414, 2023, exists — METADATA ONLY). **FULL TEXT (arXiv v1).**
- Architecture: process the ask side and the bid side with the same sub-network (shared parameters), applied to MLP, LSTM, MLP-LSTM, CNN-LSTM and an LSTM with multi-head attention (LSTM-MHA). Regression target; metrics MAE and R².
- Data: 14 "military industry" stocks, Chinese A-share market; results are counted as "win times" over 149 test sets (Tables 2–3). Exact period: not extracted — UNVERIFIED.
- Headline: (i) OFI features beat raw LOB input on almost every test set for the recurrent models — e.g. horizon 10, LSTM: OFI better on 146 of 149 sets, raw LOB on 3 (Table 2, original networks). (ii) Siamese sharing beats the original network "in over 75% of cases" excluding MLP (abstract; Table 3, e.g. LSTM with LOB input at horizon 10: Siamese better on 107 vs 29). (iii) MHA helps "particularly over shorter forecasting horizons".
- Bearing: input representation (OFI) matters more than architecture, consistent with the wu2021robust / kolm2023 line.

**hedges2026frontier — inference-compute frontier; FastBiNLOB** (C. E. Hedges, arXiv 2606.25986v1, 2026; SSRN 6991059). **Preprint.** **FULL TEXT (arXiv v1)** read for setup and conclusions; the main result tables (Tables 5–6) did not extract — their numbers UNVERIFIED here.
- Question: is there a scaling-law-like frontier of predictive loss vs per-observation "structural forward work" across very different LOB predictors (single trees, boosted trees, random-convolution logistic, neural LOB models)?
- Data: FI-2010 only; a "scaling lane" over CF1–CF9 folds and 5 horizons; a "deployment lane" with window 128, train days 1–7, test days 8–10 (Table 1).
- Headline (abstract and Conclusion): holding out the MLPLOB family, a power law fitted to the non-MLPLOB frontier predicts the MLPLOB frontier with **R² = 0.941** (0.948 for a subset; 0.838 for the full target pool). In latency space the frontier is "substantially weaker". FastBiNLOB (axis-separable dense mixer, no attention) "exceeds the published y10 and y100 macro-F1 targets" of TLOB/MLPLOB at lower batch-one latency (five seeds). Note: FastBiNLOB uses multi-task training, per-horizon fine-tuning and validation-tuned class-bias calibration that the published baselines did not use.
- Bearing: on FI-2010, accuracy tracks compute across families; attention is not required to match TLOB-level F1.

**lensky2024volimage — order-flow images → CNN for short-term volatility** (Lensky & Hao, IEEE CAI 2024, pp. 817–822; arXiv 2304.02472v2). **Peer-reviewed (conference).** **FULL TEXT (arXiv v2).**
- Target: short-term realized volatility of Binance BTC/USDT perpetual futures. Input: 4-minute windows (refreshed every 10 s; 8,616 images/day) rendered as images — LOB (20 levels) and trades mapped to colour channels; optional hand-crafted features. Models: 3-layer CNN, ResNet-18, ConvMixer; baselines GARCH, MLP on raw data, naive (current vol).
- Data: January 2021; chronological 3:1:1 split.
- Headline (abstract): CNN + aggregated features RMSPE **0.85 ± 1.1**; CNN without features 1.0 ± 1.4; **naive 1.4 ± 3.0**. Note the standard deviations exceed the means; no significance test.
- Single asset, one month of data.

**maglaras2022fill — RNN for limit-order time-to-fill distribution** (Maglaras, Moallemi, Wang, Quantitative Finance 22(11):1989–2003, 2022; SSRN 3897438). **Peer-reviewed.** **ABSTRACT ONLY** (OpenAlex).
- Abstract: a recurrent neural network estimates the distribution of time-to-fill of a limit order conditional on current market conditions; "superiority … to several benchmark techniques" on a historical dataset; "significant cost reductions" in a prototypical trading problem. Data, horizons, numbers: UNVERIFIED. Related to arroyo2024 and kanformer (fill/survival targets, in claims_dl.md).

**guo2023dla — DLA, dual-stage temporal attention** (Guo & Chen, Arabian J. Sci. Eng. 48(8):9597–9618, 2023). **Peer-reviewed.** **METADATA ONLY** (Crossref; OpenAlex has no abstract). This is the "DLA" model in LOBCAST (prata2024). Architecture details and results: UNVERIFIED here.

**ye2024imaging — LOB as images for trend prediction** (Ye, Yang, Chen, Int. J. Forecasting 40(3):1189–1205, 2024). **Peer-reviewed.** **METADATA ONLY** (no abstract on Crossref/OpenAlex). Content UNVERIFIED.

**wu2024lstf — Long short-term temporal fusion Transformer, Chinese LOB** (Wu, Wang, Fu, Applied Intelligence 54(24):12979–13000, 2024). **Peer-reviewed.** **METADATA ONLY.** Same authors have mWDN-Transformer (ACM CNIOT 2024, pp. 175–181, doi 10.1145/3670105.3670134; METADATA ONLY, not added). Content UNVERIFIED.

**abbasimehr2026multiscale — temporal multi-scale attention for mid-price movement** (Abbasimehr, Abri, Shakouri, Paki, Annals of Data Science, online 23 Sep 2026). **Peer-reviewed.** **METADATA ONLY.** Content UNVERIFIED.

### A (cont.). LLM-style, state-space and pretrained/foundation approaches

**llmlob2026 (existing key) — "Do LLMs Understand Limit Order Book Dynamics?"** (J. Chen & P. Glasserman, arXiv 2608.23706v1, 24 Aug 2026). **Preprint.** **FULL TEXT (arXiv v1).**
- Existence of "LLM applied to LOB data": VERIFIED, but in this paper the "LLM" is a GPT-style causal decoder (12 layers, d=768, 12 heads, ~86.4 M parameters; also a 1.5 B variant) **trained from scratch on synthetic LOB event sequences** from a small simulated book — not a pretrained text LLM.
- Findings: near-perfect "valid sequence" and goal-directed traversal scores (Table 1), yet the model's implicit world model "fails to learn the state of the LOB"; new kernel-level and history-level total-variation tests and a regression test show **biased forecasts and "spurious predictability"** — "the LLM finds predictability where none exists". An empirical-kernel baseline does not show the same errors.
- Limitation stated: small LOB settings; "the issues we document would likely be more severe with a larger state space".

**wheeler2024marketgpt — MarketGPT** (Wheeler & Varner, arXiv 2411.16585v1, 2024). **Preprint.** **FULL TEXT (arXiv v1).**
- Decoder-only Transformer (768-d, 12 layers, 12 heads, ~100 M parameters) trained on tokenised NASDAQ TotalView-ITCH 5.0 messages (tokenisation from nagy2023; 24 tokens per message; a ticker symbol token is appended so multiple stocks share one model). 8 days of data: 6 train / 1 val / 1 test, days vary by ticker.
- Use: message generator inside a discrete-event simulator; evaluated on stylized facts, not on forecasting accuracy. Limitation stated: inference is slow (24 tokens per message, quadratic attention).
- Bearing: generative "GPT for LOB" exists; it is not a forecasting benchmark result.

**popov2026ssm (existing key) — hardware-aware state-space model for crypto order books** (Popov & Huber, Computer Science Bulletin 9(1):177–187, 2026, doi 10.71465/csb211). **ABSTRACT ONLY** (Crossref).
- Abstract: diagonal-plus-low-rank SSM with quantization-aware training on FPGA (Stratix V); Bitcoin and Ethereum tick data 2022–2024; **62.7% directional accuracy** for next-tick movement; 247 µs end-to-end latency; 15.2× throughput vs GPU LSTM. No comparison of accuracy against non-SSM baselines is given in the abstract. Venue is a small journal; numbers UNVERIFIED beyond the abstract.
- Mamba/S4 search: arXiv queries for "limit order book" + mamba / "state space" found no other SSM *forecaster* on LOB data (hits were LOBS5 generative work = nagy2023, ByteGen arXiv 2508.02247 [generative, byte-level], and a cross-sectional SSM on intraday bars, arXiv 2608.28060, which does not use LOB data). "LOBMamba" remains UNVERIFIED (as in claims_dl.md).

**linna2026impact — repurposing a pretrained LOB forecaster (LOBERT) for market impact** (Linna, Baltakys, Manoharan, Iosifidis, Kanniainen, arXiv 2609.16930v1, Sep 2026). **Preprint.** **FULL TEXT (arXiv v1).**
- Model: LOBERT (linna2025lobert), input L = 512 messages, 3-class target on average mid-price over the next h = 100 messages vs current mid, threshold τ = 1 tick.
- Data: NASDAQ L3 messages in LOBSTER format for 7 assets (AAPL, AMD, AMZN, ASML, GOOG, MSFT, PLTR); train 1 Oct 2025–16 Jan 2026 (795 M messages), val 20–30 Jan 2026 (99 M), test 2–13 Feb 2026 (156 M). Whether one pooled model or per-asset models were used: UNVERIFIED from the extracted text.
- Forecast quality reported: **F1 47.7%, balanced accuracy 47.2%**, NLL 1.012 (§4.1) — modest on a balanced-ish 3-class task.
- Main claim: injecting counterfactual messages and comparing predictive distributions ranks impact scenarios with Spearman 0.99 and 97.2% directional agreement with realised historical outcomes (non-neutral scenarios).

### B (cont.). Universal / pooled models and cross-asset transfer

**sirignano2019 (existing key) — universal model** (Sirignano & Cont, QF 19(9) 2019; **FULL TEXT of arXiv 1803.06917v1, March 2018** — the journal version was not read and may differ).
- Data: NASDAQ order book reconstructed from LOBSTER for ~1,000 stocks, 1 Jan 2014 – 31 Mar 2017 (§2). Target: direction of the **next mid-price move** (event time, binary).
- Architecture: 3-layer LSTM + ReLU feed-forward + softmax (§2). Stock-specific models trained on ~500 GPU nodes; the universal model trained on pooled data from all training stocks with asynchronous SGD on 25 GPU nodes. Pooled "without any specific normalization".
- Headline results: (i) stock-specific deep model vs stock-specific linear (VAR/probit) model: "increase in accuracy, between 5% to 10% for most stocks" (Fig. 4 left); universal deep vs stock-specific linear: "around 10%" (Fig. 4 right). (ii) **Table 1**: a universal model trained on stocks 1–464 (Jan 2014–May 2015) and tested on 25 unseen stocks 465–489 (Jun–Aug 2015) beats the stock-specific models on **25/25** stocks, average **+1.45%** accuracy; vs the universal model trained on all 489 stocks it wins on only 4/25, average **−0.15%** (i.e. roughly equal). (iii) Table 2: training on the full 19 months beats 1/3/6-month windows on 100% of 50 stocks, average gains 7.2% / 3.7% / 1.6%. (iv) Normalising by volatility/spread/volume or partitioning by sector or tick size "do not improve training results". (v) The universal model's advantage is largest for stocks with less data (Fig. 7).
- Absolute accuracy levels are shown only in figures (Figs 6, 9, 10) — exact values UNVERIFIED here.
- Interaction of assets: shared weights only (pooled training); no cross-asset inputs.
- Limitations: target is the next mid-price move — no costs, no tradability analysis in the read version.

**lucchese2024 (existing key) — universal vs stock-specific, §4.4** (IJF 40(4) 2024; FULL TEXT arXiv 2211.13777v3; general notes in claims_dl.md).
- Setup: train on 5 "in-sample" stocks {QRTEA, CHTR, EXC, WBA, AAPL}; test on those and on 5 unseen stocks {LILAK, XRAY, PCAR, AAL, ATVI}; same deepLOB/deepOF/deepVOL backbone; MCS test against an "unpredictive benchmark".
- Result (Table 8, MCS p-values of the benchmark, universal models): for unseen **AAL and ATVI** p ≈ 0.00 at h = 10…100 (predictability found "without ever learning from the stock's past order book data"); for unseen **LILAK, PCAR** p = 1.00 at h ≥ 20 and XRAY p = 1.00 at h ≥ 20 (no predictability detected). For in-sample CHTR and AAPL the universal model finds no predictability at h ≥ 20 although stock-specific models did — "universal models may be picking up different order book dynamics".
- Table 9 (share of cases in the 99%/95% MCS when predictability exists): deepLOB(L1, universal) 0%/0%; deepOF(L1) 0%/0%; deepLOB(L2) 8%/24%; deepOF(L2) 42%/62%; deepVOL(L2) 71%/81%; **deepVOL(L3) 92%/100%**. "Some predictive universal patterns can be extracted only from the most granular data", unlike stock-specific models where order flow suffices.
- Bearing: universal-model generalisation to unseen stocks is real for some names and absent for others; it depends on input granularity.

**shi2021lobrm — LOB Recreation Model (TAQ → deeper levels), cross-stock transfer** (Shi, Chen, Cartlidge, AAAI-21, 35(1):548–556; arXiv 2103.01670v1). **Peer-reviewed.** **FULL TEXT (arXiv v1).**
- Task: predict volumes at levels 2–5 of small-tick stocks from trades-and-quotes (level-1) history only. Model: GRU "history compiler" + ODE-RNN "event simulator" + learned weighting.
- Data: LOBSTER intraday data for MSFT and INTC (two datasets). **Protocol flag: samples are shuffled and split 80/20 at random** (§Experiments) — not a chronological split.
- Headline (Table 1, test R² bid/ask): LOBRM (ODE-RNN) 0.773 / 0.753; RF and SLFN lower test performance (test loss 19.45/15.98 and 18.62/16.43 vs 13.61/11.56); ridge/SVR R² ≈ 0.14–0.17. Transfer: model trained on MSFT, fine-tuned on 30% of INTC data → INTC test R² 0.685 / 0.649 (Table 3), "approximately equivalent to a fully trained model using discrete RNNs".
- Downstream (Table 4): next-event mid-price direction with a small CNN — test accuracy top-of-book only 75.17%, real 5-level LOB 81.14%, recreated LOB 79.75%.
- Limitations stated: only two intraday datasets; plan to try "a universal LOBRM".

**yu2025asymgen — cross-market transfer of orderbook models (electricity)** (Yu, Wu, Han, Cremer, arXiv 2510.12685v2, Feb 2026; "Accepted to PSCC 2026" per arXiv). **Accepted conference paper (proceedings not yet on Crossref).** **FULL TEXT (arXiv v2).** Out-of-domain for equities but a clean transfer experiment on orderbook features.
- Data: continuous intraday electricity orderbooks, Germany (DE) and Austria (AT), 60-min and 15-min products; 384 orderbook features, feature-selected; quantile models (linear QR, KNN, LightGBM, XGBoost, MLP, KAN, etc.); target ID3 price index.
- Headline (§V–VI): "models trained on more liquid markets or products transfer well to less liquid ones, whereas the reverse transfer leads to substantial performance degradation." Direct transfer from AT-trained models raises the average quantile loss "from 3.30 to 23.84 for the 60-min product and from 6.56 to 80.15 for the 15-min product"; DE-trained models "maintain similar performance" on AT. Joint training is best or equal for the less liquid AT target; separate training is best for DE.

### A (cont.). Other single-asset items (normalisation, crypto, efficiency, representation)

**september2024edain — EDAIN; independent FI-2010 re-test of BiN and DAIN** (September, Sanna Passino, Goldmann, Hinel, AISTATS 2024, PMLR 238:1891–1899; arXiv 2310.14720v2). **Peer-reviewed.** **FULL TEXT (arXiv v2).**
- Adaptive input-normalisation layer (outlier mitigation, shift, scale, power transform); local-aware and global-aware variants. Tested on synthetic data, Amex default prediction, and FI-2010.
- FI-2010 protocol: 144-d features, H = 10, 0.01% stationarity threshold, **anchored CV, 9 folds** (train on days 1..d, test day d+1), GRU model.
- Table 3 (Cohen's κ / macro-F1, mean ± 95% CI): z-score 0.2777 / 0.5052; Winsorize+z 0.2928 / 0.5166; CDF inversion 0.3618 / 0.5798; **BIN 0.3670 / 0.5889**; **DAIN 0.3588 / 0.5776**; **EDAIN (local-aware) 0.3836 / 0.5946**; EDAIN (global-aware) 0.2820 / 0.5111. Paired sign test EDAIN vs second best p = 0.00097. "Most of the variability … arises from the folds themselves."
- Bearing: an independent group confirms that learned input normalisation (BiN/DAIN/EDAIN) adds ~7–9 macro-F1 points over z-score on FI-2010 under anchored CV; absolute F1 ≈ 0.51–0.59 under this protocol vs ≈ 0.8 under the 7/3-day split.

**jha2020digital — temporal CNN on Coinbase BTC** (Jha, De Paepe, Holt, West, Ng, arXiv 2010.01241v1, 2020; SSRN 3704098). **Preprint.** **FULL TEXT (arXiv v1)** (9 pages).
- Abstract: temporal CNN predicts bitcoin spot direction from Coinbase LOB data; "71% walk-forward accuracy" at a 2-second horizon. The classification report in the paper shows "Accuracy 0.76", which differs from the abstract's 71% — not reconciled in the text. The paper's own "stylized facts" include: "we don't explicitly validate" that deep models beat traditional ones on crypto order books. Weak evidence; included only as an early crypto example.

**sangadiev2020deepfolio — DeepFolio** (Sangadiev et al., arXiv 2008.12152v1, 2020). **Preprint.** **FULL TEXT (arXiv v1).**
- ResCNN+GRU variant of DeepLOB; FI-2010 and Binance 10-level LOB for BTC, LTC, ETH (one year from 27 Feb 2019, 5-minute snapshots / hourly resolution per §IV); second stage allocates a crypto portfolio with Sharpe-ratio or min-volatility loss.
- Multi-asset aspect: a "transfer"/pooled setup that concatenates the training sets of BTC, LTC and ETH and tests each separately (Table III). Assets interact only via pooled training, plus the portfolio layer.
- Numbers: the result tables mix decimal commas and points and the DeepLOB baseline rows show recall 33.33% (a collapsed classifier); values not reliable enough to quote — UNVERIFIED.

**makinde2026tkan — Temporal KAN** (A. Makinde, arXiv 2601.02310v2, 2026; arXiv journal-ref "BILT Student Research Journal, Issue 7, 2026" — not found on Crossref). **Student-journal / preprint.** **FULL TEXT (arXiv v2).**
- LSTM with learnable B-spline (KAN) activations; FI-2010, "strict chronological, non-overlapping train/test split".
- Headline: k = 100 macro-F1 **0.3995 vs 0.3354** for the author's DeepLOB replication (+19.1% relative); backtest 132.48% vs −82.76% (DeepLOB) at 1.0 bp cost. Note that the replicated DeepLOB F1 (0.335) is far below DeepLOB's published FI-2010 figures (~0.76–0.80); the author attributes this to the chronological split. Single author, small-venue; treat as weak evidence.

**ntakaris2024alpe — RL "adaptive learning policy engine" for mid-price level** (Ntakaris & Ibikunle, arXiv 2412.19372v2, 2024). **Preprint.** **FULL TEXT (arXiv v2)** skimmed.
- Level-1 NASDAQ data for 100 S&P 500 stocks, 1 Sep–30 Nov 2022 (LSEG); target is the **mid-price level** (regression; RMSE and a relative RMSE). Baselines naive, ARIMA, MLP, CNN, LSTM, GRU, RBFNN.
- Flag: in Table 3 (Amazon) the naive regressor has RMSE 0.602, above ARIMA 0.364 and every neural model (ALPE 0.0559) — unusual for a mid-price *level* target, where persistence is normally hard to beat; the naive definition was not checked. Per-stock models.

**li2024simlob — SimLOB** (Li, Wu, Zhong, Liu, Yang, arXiv 2406.19396v4). **Preprint.** **ABSTRACT ONLY.** Transformer autoencoder that learns vector representations of LOB snapshots for simulator calibration; the "SimLOB" baseline in zhong2025lobench (whose frozen encoder transferred best across stocks, Table 6 there).

**hong2026lobin — LOBIN, in-switch LOB construction and inference** (Hong, Zheng, Lilley, Zohren, Zilberman, arXiv 2608.02424v1, Aug 2026). **Preprint.** **ABSTRACT ONLY.** Builds LOBs and runs ML inference on programmable switches; ">10% reduction in latency compared to the NASDAQ order-matching server benchmark"; hybrid switch+server handles ~45% of traffic in-switch with ~3% average change in error rate vs server-only. Model accuracy figures UNVERIFIED.

### C (cont.). Other benchmark / survey items

**neogi2026prequential — prequential benchmark on BTC perpetual ticks** (A. Neogi, SSRN 7273201, 2026). **Preprint (SSRN).** **ABSTRACT ONLY** (Crossref).
- Abstract: 10 baseline models × 10 horizons (1–500 ticks), 11,918,929 BTC perpetual-futures ticks, test-then-train (zero look-ahead). Reports lag-1 ACF 0.046 and kurtosis 1,282. Claims the author's "Sticker" model reaches 92.92% non-zero directional accuracy at h = 1, 14 µs latency, "maximum commercial signal edge of 0.10 bps" and an "annualized Sharpe ratio of 3,913". These figures are extraordinary and unreplicated; the list of the 10 baselines is not in the abstract. Do not cite numbers without reading the paper — UNVERIFIED.

**barbosa2026lobrep — critical survey of LOB representations** (A. F. Barbosa De Oliveira, SSRN 7438404, 2026). **Preprint (SSRN).** **ABSTRACT ONLY.** Abstract: published score gaps "rarely isolate representation because operator, capacity, features, and protocol often change together"; depth-aware designs show gains "in some controlled or semi-controlled settings", and "the reviewed evidence does not identify a universally preferred architecture."

---

## Part 2 — Synthesis (verified facts only; each statement points to an entry above or in claims_dl.md)

### 2.1 Architecture families and what each adds

| Family | Representative keys | What the verified evidence shows |
|---|---|---|
| CNN / CNN-LSTM (DeepLOB line) | zhang2019, zhang2018bdlob, zhang2019qr, spears2021eurodollar | Convolutions over price levels then LSTM; Bayesian (MC-dropout) and quantile heads add uncertainty, not much accuracy (BDLOB F1 0.60 vs DeepLOB5 0.58). Re-tests: on AAPL 2022 DeepLOB's 65.9% on the smoothed r₂₀ target is matched by a naive persistence forecast at 64.8%; on a target anchored at the current price it gets 54.6% with 10 levels vs 53.6% with level 1 only (lee2024predictability). |
| Bilinear + temporal attention (TABL line) | tran2019tabl, shabani2022mtabl, magris2023bbnn, tran2021bin | Very small models (~10⁴ parameters). Multi-head attention: +0.4 F1 on the strongest topology (76.01 → 76.42, FI-2010 H=10). Learned bilinear input normalisation (BiN) is the large lever: C(TABL) 78.44 → 88.06 F1 at H=50; an independent re-test (september2024edain) confirms BIN/DAIN beat z-score by ~7–8 macro-F1 under anchored CV (0.5889 / 0.5776 vs 0.5052). |
| Neural bag-of-features | passalis2020tlonbof, tran2022atnbof | FI-2010 only. 2-D attention lifts NBoF; without a conv front end GRU beats all NBoF variants. |
| Attention on CNN-LSTM | makinen2019jump, arxiv240902277 (claims_dl) | For jump arrivals, CNN-LSTM-Attention F1 0.72 vs LSTM 0.69 vs a time-of-day-only model 0.66 — most skill is intraday seasonality. |
| Transformers (LOB-specific and generic) | wallbridge2020, berti2025tlob, lit2025, kisiel2022axial (claims_dl); bilokon2023transformers, zhong2025lobench, hedges2026frontier | On one crypto pair, generic time-series Transformers (Autoformer, Informer, FEDformer, Reformer, vanilla) did not beat DeepLOB-family CNN-LSTMs on direction (e.g. k=20: best Transformer 68.89 vs DeepLOB 70.29) and lost more under costs. In LOBench, Transformer-based encoders reconstruct best, but in cross-stock transfer the end-to-end means are close (DeepLOB 0.6814, iTransformer 0.6682, TransLOB 0.6568 mean recall). On FI-2010 an attention-free axis-separable mixer is reported to exceed TLOB/MLPLOB's y10/y100 F1 at lower latency (hedges2026frontier; table values not extracted). |
| State-space models | popov2026ssm (abstract), nagy2023 (generative) | No peer-reviewed S4/Mamba LOB *forecaster* with baseline comparisons was found. popov2026ssm reports 62.7% next-tick directional accuracy (abstract only, no baseline accuracy given). |
| Graph networks | chen2022gtnvf (over assets), briola2025hlob (over levels, claims_dl) | Graph over (stock, time) nodes helps volatility forecasting (below). No other graph-over-price-levels forecaster was found beyond HLOB. |
| LLM-style models | llmlob2026, wheeler2024marketgpt | Verified papers train GPT-style decoders **from scratch** on LOB message tokens; none found that applies a pretrained text LLM to LOB forecasting with reported accuracy. llmlob2026: valid-sequence scores near perfect, but the implicit world model is wrong and yields "spurious predictability". |
| Pretrained / representation encoders | linna2025lobert, linna2026impact, li2024simlob, zhong2025lobench | A frozen reconstruction-pretrained encoder (SimLOB) + small fine-tuned head transferred best across stocks in LOBench (mean recall 0.7260 vs ≤ 0.6814 for end-to-end models). LOBERT-based forecaster on 7 NASDAQ names reaches F1 47.7% / balanced accuracy 47.2% on a 3-class task. |
| Input representation (cross-cutting) | yang2025siamese, lucchese2024, kolm2023, wu2021robust, lee2024predictability | OFI inputs beat raw LOB on 146 of 149 test sets for an LSTM (yang2025siamese); order-flow/volume representations beat raw levels (lucchese2024); level-1 volume imbalance carries the directional signal on AAPL (lee2024predictability). Across studies, the input representation moves results more than the architecture does. |

### 2.2 Single-asset vs multi-asset evidence

Ways assets interact in the verified papers:
1. **Shared weights / pooled training (universal model), no cross-asset inputs.** sirignano2019: a universal LSTM trained on stocks 1–464 beats stock-specific models on 25/25 unseen stocks (+1.45% average accuracy, next mid-price move), and is about equal (−0.15%) to a universal model that also saw those stocks. Longer pooled history helps (19 months vs 1 month: +7.2%). lucchese2024: universal models find predictability for unseen AAL and ATVI (MCS p ≈ 0 up to h = 100) but not for unseen LILAK, PCAR, XRAY beyond h = 10; universal models need the most granular input (deepVOL-L3 in the MCS 92%/100% vs deepLOB-L1 0%). shabani2023abn: on FI-2010 a TABL trained on 4 stocks, applied zero-shot to the 5th, averages 65.62 F1 vs 66.27 for one trained on all 5; low-rank adaptation reaches 69.10. zhong2025lobench: models trained on one Shenzhen stock and applied to four others get mean recall 0.60–0.68; a frozen pretrained encoder with a fine-tuned head gets 0.726.
2. **Transfer by fine-tuning.** shi2021lobrm: MSFT → INTC with 30% of INTC data gives test R² 0.685/0.649 vs 0.773/0.753 in-domain (random shuffled split). yu2025asymgen (electricity orderbooks): transfer from the more liquid market works; the reverse raises average quantile loss from 3.30 to 23.84 (60-min) and 6.56 to 80.15 (15-min).
3. **Cross-asset inputs / attention / graphs.** chen2022gtnvf (volatility, level-1 TAQ, ~494 S&P 500 stocks): adding relations lowers test RMSPE at ΔT=600 s from 0.2498 (same model, no relations) to 0.2287; the largest single gain is from the *temporal* relation (0.2358), cross-sectional relations alone give 0.2382–0.2422; the relation-free graph model is worse than HAR-RV at ΔT=1200 s (0.2251 vs 0.2061). ofmatnet (claims_dl; abstract only): attention over time, asset and level on multi-asset OFI, claims "R2 improvements in over 90% of cases" vs single-asset OF-SATNet on 110 assets — numbers UNVERIFIED. spears2021eurodollar: joint model of 9 Eurodollar contracts with a learned output covariance; no single-asset ablation. wheeler2024marketgpt and linna2026impact: one model over several tickers via a symbol token / pooled data; no single- vs multi-asset ablation.
- **Net verified position:** pooling across assets (shared weights) is supported by numbers for next-move direction (sirignano2019) and partially for longer horizons (lucchese2024: works for some unseen stocks, fails for others). For **cross-asset inputs improving mid-price forecasts**, no verified numeric evidence was found; the only verified numbers are for **realized volatility** (chen2022gtnvf), where the gain over a relation-free version of the same model is ~8% RMSPE at 600 s and much of it comes from a non-cross-sectional (temporal) relation. m3lob lists cross-asset information as future work (claims_dl).

### 2.3 What re-tests and protocol checks show

- Published FI-2010 numbers depend heavily on protocol: the same dataset gives macro-F1 ≈ 0.5–0.6 under anchored day-by-day CV (passalis2020tlonbof 52.98; september2024edain 0.51–0.59) vs ≈ 0.76–0.88 under the 7-days/3-days split (tran2021bin, zhang2019); a chronological replication of DeepLOB at k=100 got 0.335 (makinde2026tkan, weak source).
- Protocol choices that inflate or blur results, found in the papers read: smoothed labels that share information with the inputs (lee2024predictability: persistence 64.8% vs DeepLOB 65.9%); model selection on training-set F1 with no validation set (tran2021binext); randomly shuffled train/test split (shi2021lobrm); duplicated positive samples in evaluation sets (makinen2019jump); class-balanced test sets (zhong2025lobench).
- LOBCAST (prata2024, claims_dl): all 15 models drop on new LOBSTER data (F1 48–61%). LOBench (zhong2025lobench): with 15–49 M-parameter re-implementations, DeepLOB and LSTM train worst on the prediction task, yet DeepLOB has the best end-to-end transfer mean.
- Transformer vs simpler: bilokon2023transformers (crypto, one pair): DeepLOB-family and a decomposition-LSTM beat generic Transformers; shabani2022mtabl: multi-head attention adds 0.4 F1 to the best TABL; hedges2026frontier: FI-2010 loss tracks compute across families and an attention-free mixer matches TLOB-level F1 (values not extracted); claims_dl D6/briola2020 and wang2025cryptolob (claims_meta) report simple models close to deep ones.
- Independent re-tests found for: DeepLOB (lee2024predictability, LOBCAST, LOBench, bilokon2023transformers, makinde2026tkan), BiN/DAIN (september2024edain, LOBCAST), TransLOB (LOBench, LOBCAST). None found for GTN-VF, MTABL, ATNBoF (beyond LOBCAST), ABN, Mäkinen's jump model, LOBRM, OF-MATNet.

### 2.4 UNVERIFIED list (do not cite these specifics)

- ofmatnet: horizons, R² values, limitations (paywalled; abstract only).
- tran2021binext: US-data (Amazon/Google) result values (table text garbled).
- zhang2018bdlob: prediction horizon.
- zhang2019qr: column alignment of Table 2 (R² ≈ 0.010–0.044 column assignment).
- spears2021eurodollar: per-strategy cumulative Sharpe values (1.25–1.42) column alignment.
- magris2023bbnn: exact metric labels for the ≈0.774 / 0.772 values.
- hedges2026frontier: FastBiNLOB F1 and latency table values.
- yang2025siamese: data period.
- linna2026impact: whether one pooled model or per-asset models were used.
- sirignano2019: absolute accuracy levels (figures only); whether the QF 2019 version matches arXiv v1.
- sangadiev2020deepfolio: all numeric results.
- jha2020digital: 71% (abstract) vs 0.76 (report) discrepancy.
- maglaras2022fill, guo2023dla, ye2024imaging, wu2024lstf, abbasimehr2026multiscale: architecture details and results (metadata/abstract only).
- popov2026ssm, neogi2026prequential, barbosa2026lobrep, li2024simlob, hong2026lobin: abstract-only claims; neogi2026prequential's Sharpe 3,913 and 92.92% accuracy especially.
- makinde2026tkan: journal status (DOI not on Crossref).
- berti2025tlob venue (from claims_dl): still UNVERIFIED.
- "LOBMamba" or any Mamba/S4 LOB forecaster with baselines: not found.
- A paper applying a *pretrained text* LLM (e.g. GPT-4/Llama) to LOB forecasting with reported accuracy: not found.
- A verified numeric demonstration that cross-asset LOB inputs improve mid-price direction/return forecasts: not found (only the OF-MATNet abstract claim).
