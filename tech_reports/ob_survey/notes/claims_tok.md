# Claims notes: tokenisation / vocabularies for LM-style models of LOB and financial event data

Scope: how discrete vocabularies are built for Transformer / BERT / GPT / SSM models of LOB
messages and other financial series, plus general time-series tokenisation methods.
Read levels: FULL TEXT = PDF text read (arXiv version stated); ABSTRACT ONLY; METADATA ONLY.
Anything not checked against the text is marked UNVERIFIED.

Reused keys (already defined in refs.bib / bib/*.bib, NOT redefined in bib/refs_tok.bib):
linna2025lobert, manoharan2026uqlob, nagy2023, lobbench2025, m3lob, tradefm2026, kronos2025,
chronos2024, timesfm2024, moirai2024, lagllama2023.

---

## 1. LOBERT — `linna2025lobert`
Linna, Baltakys, Iosifidis, Kanniainen, "LOBERT: Generative AI Foundation Model for Limit Order
Book Messages", arXiv:2511.12563 (v2, 8 Sep 2026; v1 also checked). Preprint (no venue stated in PDF).
Read level: FULL TEXT (v2), v1 compared for the token-count claim.

Data: Nasdaq ITCH, AAPL/INTC/MSFT/FB, 80 training days 2015-05-11..2015-09-01, 10 val, 10 test (Sec. 3).
Time stamps only at millisecond resolution in experiments (Sec. 3).

Tokenisation (Sec. 2.1):
- Granularity: ONE composite token per message ("one-token-per-message tokenizer").
- Fields in the token: side (buy/sell), message type (new order, edit, delete, execution, hidden
  order), quantised price difference, quantised volume, volume "round" indicator (Y/N). Fields joined
  into a string "using a colon delimiter" (i.e. vocabulary = set of observed field-combination strings,
  not a full Cartesian product).
- Price: tick distance from the best OPPOSING quote (best bid for sells, best ask for buys);
  quantised to levels (0, 1, 2, 3, 5, 10). Levels "chosen to distribute the values as evenly as
  possible across the most common price movements observed in the training data".
  Execution messages (type 4): price level forced to 0 in the token ("special stringification rule").
- Volume: quantised to levels (0, 50, 100, 200), chosen because volumes concentrate at round values
  ("over 60% of all volume values are exactly 100 units"). Plus binary indicator whether volume is
  exactly on a level or between levels — "proves essential for reconstructing the original volume
  values during inference".
- Time: inter-message time difference is NOT tokenised; continuous only (heavy-tailed, no repeating
  pattern found).
- Order id: not in the token (not mentioned).
- Special tokens: padding, masking, unknown.
- Vocabulary size: 293 distinct message tokens "from the training data" (including specials).
- Continuous side channels: price difference (PLGS, tau_start=10 ticks, tau_max=20, tau_clip=1000
  ticks), volume (PLGS, tau_start=200, tau_max=400, tau_clip=1500 units), time difference (PLGS,
  tau_start=1 ms, tau_max=50, tau_clip=250 ms); all normalised to [0,1]. PLGS (App. B): linear up to
  tau_start, then geometric increments mu^k with mu = 1 - 1/(tau_max - tau_start), so s(x) -> tau_max;
  output s(x)/tau_max.
- Embedding: additive sum of token embedding + continuous price + continuous volume embeddings
  (+ optional gated LOB-snapshot embedding); learned positional embeddings plus continuous RoPE on
  cumulative time differences (Sec. 2.3).
- Snapshot (not tokenised): 40 values (10 levels x price/volume per side); volume 1-exp(-v/2000);
  price as tick distance from opposing best, clipped at Dmax=20, divided by Dmax (Sec. 2.2, App. C).
- Outputs: token classification head (CE) + three regression heads (price, volume, time; MSE) whose
  input concatenates token logits and hidden state.
- Sequence length / context: no numeric context length found in text. Claim: one token per message
  "reduces effective sequence length by roughly an order of magnitude" (v2 Sec. 1); v1 adds
  "(i.e., ≈20× fewer tokens per sequence)". Prior generators described as splitting a message into
  22 tokens (Nagy et al.) and 24 tokens (MarketGPT, Wheeler & Varner) (App. A).

Evidence related to tokenisation:
- Table 1 (next-message accuracy, components of the tokenised message), S5 (1.2M params, no book
  module, re-implemented per Nagy et al.) vs LOBERT (1.1M): type 50.8 vs 60.6%; side 52.2 vs 65.1%;
  price-quantised 18.6 vs 51.8%; volume-quantised 32.1 vs 72.1%; full message 6.1 vs 26.4%;
  LOBERT+Book 61.9/65.4/53.8/72.2/27.8%. NOTE: S5 outputs were "transformed with the same processing
  steps", so accuracies are on LOBERT's quantisation; this is a model comparison, not a pure
  tokeniser ablation (architecture also differs).
- Table 2 (Sec. 3.2) — the closest thing to a tokenisation ablation: inference modes on test split,
  W1 / JSD / TVD (lower better):
  Price: Combined 10.04/0.2111/0.1839; Token only 15.44/0.5566/0.6134; Regressor only 10.85/0.3959/0.4767.
  Volume: Combined 76.86/0.1696/0.1200; Token only 85.36/0.2446/0.1585; Regressor only 86.09/0.6623/0.8395.
  Time (regressor only): 10.658/0.2681/0.3270.
  "Combined" = regressor bounded by the predicted token's bin range; "Token only" = bin-start price,
  bin-centre volume.
- Predicted vs real marginals Pearson corr 0.55 price, 0.37 volume, 0.52 time (Sec. 3.1).
- No ablation over bin edges or vocabulary size reported.

## 2. UQ-LOB — `manoharan2026uqlob`
Edward Manoharan, Linna, Baltakys, Dong, Kanniainen, "UQ-LOB: Uncertainty-Aware Limit Order Book
Mid-Price Forecasting", arXiv:2609.31491 (v2, 28 Sep 2026). Read level: FULL TEXT (local text copy).

Data: Kraken L2/L3 crypto order books (BTC, ETH, SOL, LTC, DOGE, SUI, TAO), Nov 2025 - Feb 2026,
~5.2 billion events over 600 asset-days (Sec. 4.1).

Tokenisation (Sec. 4.2, App. A.6, Table of hyperparameters):
- "Following Linna et al. (2025), each event is encoded as a discrete token from a vocabulary of
  |V| = 439 entries built from five categorical attributes (event type, side, discretised volume,
  discretised price distance from the opposing best quote, and a simultaneity flag); events deeper
  than Lmax = 10 levels are discarded."
- One composite token per event (single discrete feature per event, App. A.6).
- HOW 439 ARISES: NOT STATED in the paper. No bin edges for volume or price distance, no list of
  event types, no definition of the simultaneity flag, and no statement whether 439 is a full product
  or the observed set (as in LOBERT's 293), or whether it includes special tokens. -> UNVERIFIED.
  (Noted difference from LOBERT: a simultaneity flag replaces LOBERT's volume-round indicator in the
  list of attributes; the paper does not say whether the round indicator is dropped.)
- Continuous features per event (7): log inter-event time, log tick distance from opposing best
  quote, log relative volume (these three "follow Linna et al."; note: log transforms, not PLGS as
  in LOBERT), DNFI_t = (dQb1 - dQa1)/(Qb1 + Qa1), cumulative DNFI over preceding 50 and 200 events,
  queue imbalance QI_t = (Qb1 - Qa1)/(Qb1 + Qa1).
- Use of token: encoder is D-TABL (bilinear, not a Transformer); token id embedded via learned table
  E in R^{439x8}, concatenated with the 7 continuous features -> 15 features per event, input
  X in R^{15x512} (App. A.6, eq. 13).
- Context: non-overlapping windows of L = 512 events.
- No tokenisation ablation reported.

## 3. LOBS5 — `nagy2023`
Nagy, Frey, Sapora, Li, Calinescu, Zohren, Foerster, "Generative AI for End-to-End Limit Order Book
Modelling: A Token-Level Autoregressive Generative Model of Message Flow Using a Deep State Space
Network", ICAIF '23, pp. 91-99, arXiv:2309.00638. Read level: FULL TEXT (arXiv).

Data: LOBSTER level-3, GOOG (small tick) and INTC (large tick), 102 training days (2022-07-01..11-11).
Uses 4 message types: new limit order, partial cancel, full delete, visible execution (hidden orders,
halts dropped).

Tokenisation (Sec. 4.1-4.2, Fig. 1):
- Granularity: MANY tokens per message, per-field (and sub-field) — 22 tokens per message.
- Pre-processing: price -> ticks from previous mid-price (rounded down if mid between ticks), clipped
  to [-999, 999]; size clipped at 9999 ("these thresholds affect less than 0.1% of the data").
- Order id: NOT encoded (numeric ids "a poor choice ... arbitrary nature and non-stationarity");
  instead referential messages carry the referenced original order's modified price, size and
  timestamp as extra fields. New limit orders get NA in these reference fields.
- Inter-arrival time dt added as a field.
- Field order (9 fields): event type, direction, price, size, dt, arrival time, then reference price,
  size, time. Ordered so longer/higher-entropy fields come later.
- Per-field encoding: event type, direction, size = 1 token each; price = 2 tokens (sign above/below
  mid; tick distance); arrival time and dt share a vocabulary and are tokenised in groups of 3 digits
  (15-digit nanosecond timestamp -> 5 tokens). 9 fields -> 22 tokens.
- Non-overlapping token ranges per field (same numeric value in different fields -> different tokens).
- Special tokens: MSK (masked target) and HID (fields to the right of the mask); NA for missing refs.
- Vocabulary: 12,011 distinct tokens.
- Book state NOT tokenised: L2 volume image (P volumes + previous mid change) as continuous input.
- Context: n = 500 messages -> 11,000 message tokens + 500 book observations.
- Training: mask a random token in the last message; time tokens of new messages not predicted
  (computed from generated dt). At sampling, distribution restricted to syntactically valid tokens
  for the current field.
- No tokenisation ablation reported.

## 4. LOB-Bench — `lobbench2025`
Nagy, Frey, Li, Sarkar, Vyetrenko, Zohren, Calinescu, Foerster, "LOB-Bench: Benchmarking
Generative AI for Finance -- an Application to Limit Order Book Data", ICML 2025 (PMLR 267),
arXiv:2502.09172. Read level: FULL TEXT (arXiv).

- Benchmark itself does not define a tokenisation (operates on LOBSTER-format messages/books).
- LOBS5 evaluated as in Nagy et al. 2023, scaled to 35M params, trained on all of 2022 (Sec. 6).
- RWKV-4 / RWKV-6 baselines (170M params, initialised from open-source pretrained RWKV): "without any
  data pre-processing and using an off-the-shelf byte-pair tokenizer (Sennrich et al., 2016)";
  message data only, no book state (Sec. 6). App. C: BPE trained on GOOG 2017 messages; 5.5B tokens
  for INTC 2022 (276M messages), 7.5B tokens for GOOG 2022 (380M messages) => about 19.9 and 19.7
  tokens per message (my arithmetic); chunks of 16,384 tokens.
- Outcome (not a clean tokeniser ablation — model, size and book input all differ): "fastest
  divergence exhibited by the RWKV models across most scores"; average L1 distance between real
  and generated impact curves Delta_R = 2.45 for LOBS5 vs Delta_R = 126 for RWKV-6 (Sec. 6). App. E:
  RWKV-6 shortcomings ("wrong price levels, mismatched book volumes etc.") attributed by the authors
  to "tokenization of raw data and missing order book information" (Fig. 16 caption).
- BPE vocabulary size not stated. UNVERIFIED.

## 5. M3 — `m3lob`
Zhang, Ma, Cheng, Li, Duan, "M3: A State-Event Generative Foundation Model for Market Microstructure
Dynamics", arXiv:2608.19227 (2026). Read level: FULL TEXT (arXiv). Venue: not stated in the PDF
text I read (format looks like AAAI style) -> venue UNVERIFIED.

Data: CSI 500 and CSI 300 constituents (China), Jan 2024 - Nov 2025 train, Dec 2025 test;
~31.9 billion order events (App. "Dataset Details"). All limit and market orders kept in arrival order.

Tokenisation (Sec. "Tokenization", App. "Tokenizer Details"):
- Event = 5 attributes (action a in {add, cancel}, side d in {ask, bid}, price, volume, time).
  Limit and market orders not distinguished. Feature vector x_t = [r_open, log(1+V), dtau, a, d].
- Price anchor: relative to DAILY OPEN price, r = (P - P_open)/P_open — explicitly departing from
  mid-relative pricing of MarS (Li et al. 2025) and TradeFM (Kawawa-Beaudan et al. 2026), because
  mid-relative tokens at generation time must use the simulated (reconstructed) mid, causing an
  anchor-feedback error loop.
- Continuous features clipped at 0.5th and 99.5th percentiles before tokenizer training.
- Discretiser: learned VQ-VAE ("Following Shi et al. (2025)" = Kronos) — causal Transformer encoder
  (3 enc + 3 dec layers, 6 heads, d_model 384, d_ff 1536, ~15.2M params), 128-dim bottleneck,
  codebook K = 32,768 entries, nearest-neighbour assignment, k-means init (10 iters), EMA updates
  (decay 0.99), dead-code replacement (EMA cluster size < 2), commitment beta = 0.25, rotation-trick
  gradient estimator. ONE token per event (joint over all 5 attributes). Encoder is causal over the
  window (token depends on x_{<=t}).
- Tokenizer loss: weighted MSE on continuous features (weights e.g. [100,1,1], price prioritised) +
  CE on categorical (action, side) + VQ losses + "Open Tick Loss" (lambda_tick = 0.001, Smooth-L1
  between reconstructed and discrete tick index).
- Codebook usage: 100% cumulative; per-minibatch 22,696 codes (69.3%), perplexity 16,815.
- LOB state NOT tokenised: initial snapshot (10 levels x 2 sides, price/volume/validity) encoded to
  P continuous prefix embeddings via a Transformer + learned-query cross-attention; prepended to
  order tokens ("analogous to a vision-language model").
- AR backbone: LLaMA2-style decoder, vocab 32,768 for all sizes (10M..1.27B), untied embeddings.
- Context: sliding window 1,024 tokens (= events) for tokenizer and AR model.

Tokenisation ablation (App. Table 2, fixed vocabulary K = 32,768):
- Bin tokenizer baseline: factorised 32 price bins x 16 volume bins x 16 time bins x 2 actions x
  2 sides (= 32,768), mid-relative price; decoded by bin median. Two variants: Bin-O (oracle true mid
  anchor; non-causal diagnostic) and Bin-S (simulated mid from replaying decoded events).
- Price reconstruction (Bin-O / Bin-S / VQ): MAE 5.86e-2 / 4.3e-1 / 2.51e-2; exact tick rate
  0.765 / 0.307 / 0.377; within 1 tick 0.846 / 0.548 / 0.662; error P90 4 / 26 / 6 ticks;
  P99 123 / 764 / 28 ticks.
- Other features (Bin vs VQ): relative price MAE 1.119e-3 vs 5.250e-4; log-volume MAE 6.910e-2 vs
  3.530e-2; volume MAE 5.064e2 vs 1.171e2; delta-time MAE 2.380e-2 vs 1.260e-2; event-time MAE
  4.443 vs 1.553; final-time abs err 8.616 vs 2.770.
- NOTE: this ablation confounds two changes (anchor: mid vs open; discretiser: factorised bins vs
  learned VQ). Bin-O beats VQ on exact-tick rate (0.765 vs 0.377) and within-1-tick (0.846 vs 0.662)
  but not on MAE/P99.
- Also (App. Fig. 10): LOB-prefix vs no-LOB validation loss curves; numeric loss reduction only in
  figure (not extracted).

## 6. MarketGPT — key `wheeler2024marketgpt` (new, in refs_tok.bib)
Wheeler & Varner, "MarketGPT: Developing a Pre-trained Transformer (GPT) for Modeling Financial Time
Series", arXiv:2411.16585 (2024). Read level: FULL TEXT (arXiv). Venue: preprint (none found).

Data: Nasdaq TotalView-ITCH 5.0, 8 days (6 train/1 val/1 test per ticker), all price levels; 5 message
types incl. replace and "execute at different price"; pre-training 91,124,274 messages
(2,186,982,576 tokens) across 20 assets, then per-ticker fine-tuning.

Tokenisation:
- Extends Nagy et al. (2023): per-field multi-token encoding; price in ticks from previous mid,
  inter-arrival times added; prices truncated at 999 ticks, sizes at 9999.
- Adds old order ID / old absolute price handling for replace and execute-at-different-price types;
  reference relative price redefined for those types; ticker symbol id appended to each message.
- "converts an 18-element length pre-processed message into a 24-element length tokenized message".
  Price fields = sign token + relative-price token; time fields (dt and timestamp) split into seconds
  and nanoseconds components. Same vocab range reused for variants of the same component (e.g. size).
- Vocabulary: "12,012 + S, where S is the number of tokens reserved for ticker symbol IDs ... S = 98
  tickers for total vocab size of 12,111 tokens." (NOTE: 12,012 + 98 = 12,110, not 12,111 — the
  paper's arithmetic is off by one; report verbatim.)
- Context: max sequence length L = 10,368 tokens (n = 432 messages) in training; at inference context
  reduced to L = 2,688 messages "We did not observe any substantial decrease in model performance".
  Uses a single dedicated attention-sink token + RoPE + rolling KV cache for long generation.
- Sampling restricted to the valid token range for each field.
- Limitation stated by authors: "the model must generate 24 tokens per message, which is expensive".
- No tokenisation ablation.

## 7. MarS / Large Market Model (LMM) — key `li2025mars` (new, in refs_tok.bib)
Li, Liu, Liu, Fang, Wang, Xu, Bian (Microsoft Research Asia), "MarS: A Financial Market Simulation
Engine Powered by Generative Foundation Model", ICLR 2025, arXiv:2409.07486. Read level: FULL TEXT.

Order Model tokenisation (Sec. 2.1, App. B.2.1):
- "we opt to encode each order and its antecedent LOB as a single token."
- Order index = position in tuple (type, price, volume, interval); type in {Ask, Bid, Cancel};
  price and volume discretised into [0, 32), interval into [0, 16) -> index in [0, 49152)
  (3 x 32 x 32 x 16 = 49,152; composite product token).
- Input embedding Emb_i = emb(order_i) + linear_proj(LOB volumes) + emb(LOB mid price), where LOB
  volumes = 10-level ask and bid volumes, "also discretized into [0, 32)", and mid price = number of
  tick changes since market open. Only the order index is predicted; next LOB derived by matching.
- HOW price/volume/interval bins are defined (uniform / quantile / relative to what reference) is NOT
  stated in the text read -> UNVERIFIED. (M3 describes MarS as mid-relative; not confirmed in MarS text.)
- Data: top 500 liquidity Chinese stocks, 2017-2023, "16 billion order tokens"; LLaMA2 backbone;
  sequence length 1024; batch 4096 (4M tokens/step). Fig. 3: order model trained on 32B tokens,
  2M - 1.02B params (scaling).
- Tokenisation ablation (App. B.3, Fig. 11): "Order" vs "Order + LOB" token embedding; validation-loss
  curves (y-axis ~7.0-7.8 over ~3.5e10 tokens) — "integrating the LOB information contributes to an
  enhanced training curve". No numeric gap stated in text.
Order-Batch Model:
- Each minute-level order batch -> "order image" C=3 (order categories) x H=32 (volume slots) x W=32
  (price slots), pixel = count of identical orders, V in [0,100].
- Tokenised with a pre-trained LDM VQGAN (f = 4, vocabulary Z = 8192, code dim 3), fine-tuned on order
  images: 32x32 image -> 8x8 = 64 tokens. 16 batches concatenated -> 1,024-token sequences; LLaMA2.
- Order-batch model trained on 10B tokens, 150M-3B params (Fig. 3).

## 8. TradeFM — `tradefm2026`
Kawawa-Beaudan, Sood, Papasotiriou, Borrajo, Veloso, "TradeFM: A Generative Foundation Model for
Trade-flow and Market Microstructure", arXiv:2602.23784 (2026). Read level: FULL TEXT.

Data: proprietary US equities tick-level transactions, 368 trading days (Feb 2024 - Sep 2025), >9K
equities, >19B tokens, 1.9M date-asset pairs; 10.7B train / 8.7B test tokens. Partial observability
(event stream, no full book).

Tokenisation (Sec. 5.3, 6, App. A.4):
- Event = (dt interarrival seconds, price depth, volume, action, side).
- Scale-invariant features: dt raw seconds; volume log(1+V); price depth (p_order - p_mid)/p_mid with
  p_mid estimated by EW-VWAP (bps); context feature Delta p_t = (p_mid - p_open)/p_open.
- Binning: price-related features — equal-frequency (quantile) bins; log-volume and interarrival time
  — equal-width bins (on log values, i.e. log bins in original space; the text says "For
  log-transformed features, like volume and interarrival time"). Outliers above 99th pct excluded
  (and below 1st pct for price depth and price level) before binning; "special bins" reserved for
  out-of-range values. Tokenizer calibrated on the first 30 days (Feb 2024).
- Composition: mixed-radix single integer, i_trade = i_a*(n_s n_dp n_v n_dt) + i_s*(n_dp n_v n_dt)
  + i_dp*(n_v n_dt) + i_v*n_dt + i_dt, with n_dp = 16, n_v = 16, n_dt = 16, n_s = 2, n_a = 2
  -> vocabulary 16,384 predictable trade tokens. VERIFIED (16*16*16*2*2 = 16,384).
  Worked example (App. A.4): bins dt=11, dp=7, v=7, a=0, s=1 -> i_trade = 6011 (checks:
  4096 + 1792 + 112 + 11 = 6011).
  UNCLEAR: how the reserved "special bins" for out-of-range values fit inside n = 16 per feature
  (the 16,384 figure leaves no extra slots) — not explained in text.
- Non-predicted conditioning inputs: liquidity bin i_l (3 bins by ADV), price-level-change bin
  i_dp_t (32 bins), market/participant indicator I_MP. Input tuple [i_l, I_MP, i_dp_t, i_trade];
  each feature gets its own embedding table, concatenated, linearly projected ("tabular embedding").
- No continuous side channel.
- Model: Llama-style decoder, 524M params, GQA, RoPE; context length 1,024 tokens (App. B.2).
- No tokenisation ablation found.

## 9. Kronos — `kronos2025`
Shi, Fu, Chen, Zhao, Xu, Zhang, Li, "Kronos: A Foundation Model for the Language of Financial
Markets", AAAI 2026 (vol. 40), arXiv:2508.02739. Read level: FULL TEXT (arXiv incl. appendices).
Domain: K-line (OHLCVA candlestick) bars, not LOB messages; >12B observations, 7 frequencies, 45 exchanges.

Tokenisation:
- Preprocessing: per-instance z-score per feature (O, H, L, C, Volume, Amount), clipped to [-5, 5].
- One token per time step (K-line bar), produced by a Transformer autoencoder with Binary Spherical
  Quantization (BSQ; Zhao, Xiong, Kraehenbuehl 2024) -> k-bit code b_t in {-1,1}^k; k = 20
  (vocab 2^20) for all Kronos sizes (Table 1).
- Hierarchical: code split into coarse and fine subtokens, k_c = k_f = k/2 = 10 bits (1,024 entries
  each). Loss L = L_coarse (reconstruct from coarse only) + L_fine (from full) + lambda L_quant.
  AR model predicts coarse subtoken then fine subtoken conditioned on coarse (sequential).
- Context: max 512 tokens.
- Ablations with numbers:
  * Table 2 (prediction space): Kronos_small (discrete, sequential subtokens) vs Kronos-Parallel vs
    Prob-AR vs Direct-AR — Price IC 0.0431 / 0.0345 / 0.0179 / 0.0212; Price RankIC 0.0254 / 0.0226 /
    0.0102 / 0.0149; Return IC 0.0665 / 0.0529 / 0.0356 / 0.0416; Return RankIC 0.0622 / 0.0505 /
    0.0329 / 0.0399; Volatility MAE 0.0384 / 0.0461 / 0.0464 / 0.0565; Vol R2 0.2490 / 0.1784 /
    0.1383 / 0.1608.
  * Fig. 6: vocabulary size 2^14..2^20 — "increasing the vocabulary size improves both reconstruction
    quality and forecasting accuracy" (values only in plot; not extracted).
  * App. Table 9 (tokenizer architecture, vocab 2^18): reconstruction MAE/MSE — Transformer w/
    hierarchical loss 0.0785/0.0203; Transformer w/ standard loss 0.0781/0.0202; CNN 0.0916/0.0251.
    (Hierarchical loss does not improve reconstruction; its purpose is the coarse-to-fine ordering.)
  * App. Table 11: codebook usage coarse 97.66%, fine 85.25% (each 2^10).
  * App. Table 12 (k = 20, Kronos_base): splits n = 1 -> sub-vocab 1,048,576, vocab params 1744.8M,
    total 1842.3M, 1x steps; n = 2 (chosen) -> 1,024, 3.4M, 102.3M, 2x; n = 4 -> 32, 0.2M, 100.5M, 4x;
    n = 5 -> 16, 0.1M, 101.1M, 5x.

## 10. ByteGen — key `li2025bytegen` (new)
Li & Chen (Stevens), "ByteGen: A Tokenizer-Free Generative Model for Orderbook Events in Byte Space",
arXiv:2508.02247 (v2, Aug 2025). Read level: FULL TEXT for the representation section only
(method/data pages skimmed; results not extracted).
- No tokenizer: raw bytes, vocabulary 256 (0x00-0xFF).
- Each L3 event packed from a 64-byte record (ev uint32, order_id uint32, exch_ts int64, local_ts
  int64, px float64, qty float64, bid_px/ask_px float64) into 32 bytes: (order_id << 32 | ev) 8 bytes,
  exch_ts 8, px 8, qty 8. So order id IS kept (unlike Nagy et al.), price and quantity are raw float64.
- Sequence length 10,240 bytes = 320 events; windows aligned to 32-byte boundaries.
- Architecture: H-Net (hybrid Mamba-Transformer with dynamic chunking that learns segmentation).
- Data: CME Bitcoin futures L3 events (per abstract / Sec. 1).
- Results and any comparison vs tokenised models: NOT extracted -> UNVERIFIED.

## 11. Time-series foundation models (contrast cases)
### Chronos — `chronos2024` (TMLR 2024, arXiv:2403.07815). Read level: FULL TEXT.
- Mean scaling: m = 0, s = (1/C) sum_{i<=C} |x_i| (context only) (Sec. 3.1).
- Quantisation: B bin centres; uniform binning chosen over quantile binning "Since the distribution
  of values for unseen downstream datasets can differ significantly from the training distribution".
  Interval [c_1, c_B] = [-15, 15]; |V_ts| = 4096 including PAD and EOS (Sec. 5.2); B = 4094 bins
  (Sec. 5.7). Token spacing in original units 30s/(B-1).
- One token per time step (univariate); no continuous side channel; context 512, prediction 64.
- Regression via classification (categorical CE over bins).
- Vocabulary-size ablation (Sec. 5.6, Fig. 11c, Chronos-T5 Small 46M): "modest improvements in ...
  MASE as the vocabulary size increases. In contrast, the WQL initially improves but deteriorates for
  larger vocabulary sizes." Exact sizes and values only in the figure (not extracted). Changelog F.1:
  an off-by-one bin-decoding bug fixed in v3 "led to changes in the conclusion of the vocabulary size
  experiment".
- Stated limitation (Sec. 5.7, Fig. 16): range capped at [c_1, c_B] so strong trends/sparse spikes
  lose precision; large s relative to variance merges nearby values into one token.

### TimesFM — `timesfm2024` (ICML 2024, arXiv:2310.10688). Read level: FULL TEXT (relevant sections).
- NO discrete vocabulary. Non-overlapping input patches (length p) -> residual MLP block -> "token"
  vector (+ positional encoding); N = floor(L/p) tokens. Output patch can be longer than input patch
  (example: input 32, output 128). Patch masking during training.
- Ablations (Sec. 6): output_patch_len 8..128 -> "monotonic decrease in average MAE" (ETT, 512-step
  horizon, Fig. 3b); input_patch_len 8 -> 32 "increases performance".

### Moirai — `moirai2024` (ICML 2024, arXiv:2402.02592). Read level: FULL TEXT (relevant sections).
- NO discrete vocabulary. Multi-patch-size linear input/output projections; patch size chosen by
  frequency (App. B.1): yearly/quarterly 8; monthly 8,16,32; weekly/daily 16,32; hourly 32,64;
  minute 32,64,128; second 64,128 (five patch sizes total). Output = mixture of parametric
  distributions (Student-t, negative binomial, log-normal, low-variance normal).

### Lag-Llama — `lagllama2023` (arXiv:2310.08278). Read level: FULL TEXT (relevant sections).
- NO discrete vocabulary. "Token" at time t = value x_t plus lag features from a set of lag indices
  plus date-time features; Student-t output head (df, mean, scale); robust standardisation.

## 12. General tokenisation methods
### LLMTime — key `gruver2023llmtime` (new). NeurIPS 2023, arXiv:2310.07820. Read level: FULL TEXT (Sec. 3).
- Values written as digit strings at fixed precision, decimal point dropped; for GPT-3, digits
  separated by spaces (forcing one token per digit) and time steps separated by " ,". Example with
  2 digits: 0.123, 1.23, 12.3, 123.0 -> " 1 2 , 1 2 3 , 1 2 3 0 , 1 2 3 0 0".
- Rationale: BPE splits numbers inconsistently (42235630 -> [422, 35, 630] in GPT-3).
- For LLaMA (digits already individual tokens), adding spaces hurts (Fig. 2; no numbers extracted).
- Rescaling: scale so the alpha-percentile of values is 1, optional offset beta (percentile), tuned
  on validation likelihood.
- Digit-level autoregression = hierarchical softmax -> continuous density via uniform within-bin
  density + change of variables (Sec. 3, Fig. 3).

### BPE — key `sennrich2016bpe` (new). ACL 2016, arXiv:1508.07909. Read level: FULL TEXT (Sec. 3.2).
- Start from character vocabulary (+ end-of-word symbol), iteratively merge most frequent adjacent
  pair; final vocab = initial vocab + number of merges (the only hyperparameter).
- LOB use: LOB-Bench RWKV baselines (Sec. 4 above), BPE trained on GOOG 2017 LOBSTER messages
  (~20 tokens/message by my arithmetic). Criticised for numbers by LLMTime.

### VQ-VAE — key `vandenoord2017vqvae` (new). NIPS 2017, arXiv:1711.00937. Read level: FULL TEXT (Sec. 3).
- Encoder output z_e(x) quantised to nearest codebook vector e_k (k = argmin_j ||z_e - e_j||);
  loss = reconstruction + ||sg[z_e] - e||^2 + beta ||z_e - sg[e]||^2, beta = 0.25 ("quite robust to
  beta ... 0.1 to 2.0"); straight-through gradient. Image experiments K = 512.
- LOB/finance descendants: M3 (VQ, K = 32,768), MarS order-batch model (VQGAN, Z = 8192), Kronos (BSQ,
  a lookup-free binary variant), TOTEM (VQ for time series).

### SAX — keys `lin2003sax` (new), `lin2007sax` (new).
- lin2003sax: DMKD '03 workshop, pp. 2-11. Read level: FULL TEXT (Sec. 3.1-3.2 from author copy at
  cs.ucr.edu/~eamonn/SAX.pdf). Method: z-normalise each series, Piecewise Aggregate Approximation into
  w segments, then map each PAA mean to one of a symbols (the text states "alphabet size is also an
  arbitrary integer a, where a > 2") using breakpoints that make symbols equiprobable under N(0,1)
  ("normalized time series have a Gaussian
  distribution"). Distance on symbols lower-bounds Euclidean distance.
- lin2007sax: DMKD 15(2):107-144, 2007. Read level: METADATA ONLY (Crossref).
- No LOB paper in this set cites SAX (not checked exhaustively) — UNVERIFIED whether any LOB LM
  work uses SAX-style equiprobable Gaussian breakpoints.

### TOTEM — key `talukder2024totem` (new). TMLR 12/2024, arXiv:2402.16412. Read level: FULL TEXT (Secs. 3, 6, App. D).
- VQ-VAE tokenizer over univariate time (sensor dim flattened), strided 1D convs, compression F = 4
  (one token per 4 time steps), RevIN normalisation, codebook K = 256.
- Ablations with numbers (Sec. 6, Table 21): tokens (TOTEM) vs patches (PatchTOTEM), same
  architecture — AvgWins 67.9% vs 39.3% (specialist in-domain), 78.6% vs 23.2% (generalist
  in-domain), 67.5% vs 35.0% (generalist zero-shot); with MLP forecaster 66.1% vs 37.5%.
  Codebook size (Table 21D, reconstruction MSE, "All"): K = 32: 0.0451; K = 256: 0.0192;
  K = 512: 0.0184 — K = 256 chosen as "similar to that of K = 512" and more parsimonious.

### WaveToken — key `masserano2025wavetoken` (new). ICML 2025 (PMLR 267:43248-43275), arXiv:2412.05244.
Read level: FULL TEXT (arXiv v1, Secs. 4.3-4.4). (arXiv lists last author "Yuyang Wang"; PMLR
lists "Bernie Wang" — bib uses PMLR.)
- Pipeline: standardise by context mean/std, single-level DWT (Biorthogonal-2.2 chosen), optional
  thresholding of detail coefficients (No-thresholding chosen in final setting), quantise coefficients
  with bins from the joint empirical distribution, bin size by Freedman-Diaconis rule; PAD and EOS
  special tokens; one shared vocabulary for approximation and detail coefficients.
- Vocabulary 1,024 vs Chronos 4,096 (same T5 architectures).
- Ablation (Sec. 4.4, Fig. 6, WaveToken-Small, 200K steps): "gradual but consistent improvement until
  |V| = 1024 ... For higher vocabulary sizes, WQL and MASE remain flat or worsen". Values only in
  figure (not extracted).

---

## 13. Synthesis (verified facts only)

Granularity: tokens per LOB message
| Model | tokens/message | vocab | fields in token(s) | continuous side channel |
|---|---|---|---|---|
| LOBS5 (nagy2023) | 22 | 12,011 | type, side, price (sign + ticks from mid), size, dt, time (3-digit groups), ref price/size/time | book L2 volume image (not tokenised) |
| MarketGPT (wheeler2024marketgpt) | 24 | 12,111 (stated; 12,012+98) | as LOBS5 + ticker, old id/price for replace; time split s/ns | none |
| LOB-Bench RWKV (lobbench2025) | ~20 (BPE, derived) | not stated | raw LOBSTER text | none |
| ByteGen (li2025bytegen) | 32 bytes | 256 | type, order id, ts, price, qty as binary | none |
| LOBERT (linna2025lobert) | 1 | 293 (observed set + PAD/MASK/UNK) | type, side, price bin (0,1,2,3,5,10 ticks from opposite best), volume bin (0,50,100,200), round flag | PLGS price, volume, time; snapshot |
| UQ-LOB (manoharan2026uqlob) | 1 | 439 | type, side, volume bin, price-distance bin, simultaneity flag (bins not given) | 7 continuous features |
| MarS order model (li2025mars) | 1 | 49,152 = 3x32x32x16 | type (Ask/Bid/Cancel), price, volume, interval | LOB volumes (binned to 32) + mid ticks since open added to embedding |
| TradeFM (tradefm2026) | 1 | 16,384 = 2x2x16x16x16 | action, side, price depth (quantile bins), log-volume, dt (equal-width on log) | none (3 categorical conditioning inputs) |
| M3 (m3lob) | 1 | 32,768 (learned VQ codebook) | action, side, open-relative price, log(1+V), dt | LOB prefix embeddings |

Design choices observed:
1. Per-field multi-token (LOBS5, MarketGPT) vs one composite token per event (LOBERT, UQ-LOB, MarS,
   TradeFM, M3). Composite tokens built either as an observed-string set (LOBERT), a Cartesian
   mixed-radix product (MarS, TradeFM), or a learned codebook (M3). Byte-level (ByteGen) and BPE
   (LOB-Bench RWKV) avoid hand-designed vocabularies.
2. Price reference: previous mid (LOBS5, MarketGPT), opposite best quote (LOBERT, UQ-LOB), EW-VWAP mid
   in relative units (TradeFM), daily open (M3). M3 argues (and shows in its App. Table 2) that mid
   anchors cause feedback errors at generation time.
3. Binning: hand-chosen irregular edges from data (LOBERT), quantile for price + log-equal-width for
   volume/time (TradeFM), uniform on a scaled range (Chronos), Freedman-Diaconis on wavelet coefficients
   (WaveToken), equiprobable Gaussian breakpoints (SAX), learned (VQ: M3, TOTEM; BSQ: Kronos), digits
   (LOBS5 time fields, LLMTime).
4. Continuous retention: LOBERT and UQ-LOB keep continuous features alongside a coarse token; TradeFM,
   MarS, M3, Chronos keep none in the token stream (MarS/M3 add continuous/embedded LOB state).

Reported trade-off evidence (with numbers):
- LOBERT Table 2: token-only vs combined (token-bounded regressor) — price W1 15.44 vs 10.04, JSD
  0.5566 vs 0.2111; volume W1 85.36 vs 76.86. Regressor-only price W1 10.85, JSD 0.3959.
- M3 App. Table 2 (K = 32,768 both): factorised mid-relative bins with simulated anchor vs open-anchored
  VQ — price MAE 4.3e-1 vs 2.51e-2, P99 error 764 vs 28 ticks; oracle-anchored bins still best on
  exact-tick rate (0.765 vs VQ 0.377).
- Kronos Table 2: sequential coarse->fine subtokens vs parallel — price IC 0.0431 vs 0.0345; discrete
  vs continuous regression (Direct-AR 0.0212, Prob-AR 0.0179). Table 12: factorising 2^20 vocab into
  2 x 2^10 cuts total params from 1842.3M to 102.3M at 2x decoding steps.
- TOTEM: discrete VQ tokens beat patches at fixed architecture (AvgWins 67.9 vs 39.3; 78.6 vs 23.2;
  67.5 vs 35.0); codebook 32/256/512 -> MSE 0.0451/0.0192/0.0184.
- Vocabulary size: Kronos — larger (2^14..2^20) better (figure); Chronos — MASE improves modestly,
  WQL non-monotone (figure); WaveToken — improves to 1024 then flat/worse (figure).
- Tokens per message / context: LOBS5 500 messages = 11,000 tokens; MarketGPT 432 messages = 10,368
  tokens; LOBERT claims ~an order of magnitude (v1: ≈20x) fewer tokens than 22-24-token schemes.
- LOB-Bench: BPE-tokenised RWKV-6 impact-curve L1 Delta_R = 126 vs LOBS5 2.45 (confounded by model,
  size, and absence of book input).
- MarS Fig. 11: adding LOB info into the order-token embedding gives lower validation-loss curve (no
  numbers in text).

## UNVERIFIED
- UQ-LOB: how |V| = 439 is composed (bin edges, event-type set, simultaneity flag definition,
  whether specials are included, observed-set vs product). Not in the paper.
- UQ-LOB: whether LOBERT's volume-round flag is retained.
- LOBERT: numeric context length used in experiments (not found in v2 text).
- MarS: how price/volume/interval bins are defined (reference price, uniform vs quantile).
- TradeFM: how the "special bins" for out-of-range values fit inside n = 16 per feature.
- LOB-Bench RWKV BPE vocabulary size.
- MarketGPT: 12,012 + 98 = 12,110 vs stated 12,111 (paper inconsistency; true value unknown).
- Chronos, Kronos, WaveToken vocabulary-size ablation values (figure only, not extracted).
- ByteGen results and any quantitative comparison with tokenised LOB models.
- M3 venue.
- Whether any LOB LM paper uses SAX or LLMTime-style digit encoding beyond LOBS5/MarketGPT's
  3-digit time groups.
- Not searched exhaustively: other 2024-2026 order-flow tokenisation papers beyond those above
  (search found ByteGen; FlowLOB 2608.13096 and "Painting the market" 2509.05107 are flow-matching /
  diffusion models and were not read).
