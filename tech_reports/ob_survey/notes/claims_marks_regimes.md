# Marked Hawkes and regime-switching Hawkes in LOB / high-frequency modelling: verified notes

Compiled 2026-10-03. Companion bib: `bib/refs_marks_regimes.bib` (new keys only; reused keys are listed in its header).

**Read levels.** FULL TEXT = the relevant sections of the named version were read via pdftotext (not necessarily every page). ABSTRACT ONLY / METADATA ONLY mean what they say. Formulas are ASCII transcriptions in the paper's own notation. Equation/table numbers refer to the version named.

**Already verified elsewhere (not redone; see `claims_hawkes2.md`, `claims_hawkes3.md`, `claims_classic.md`):** rambaldi2017marked, fauthtudor2012, leeseo2017marked, jain2024chp, bacry2015 (Sec. 2.2.1 marks), rasmussen2018notes, chavezdemoulin2012 (abstract), embrechts2011mhp (abstract), alfonsiblanc2016, cartea2014buylow, cartea2018siamrev, swishchuk2019chp, swishchuk2020gchp, morariupatrichi2022, sfendourakis2020, mucciante2024 (abstract), rambaldi2015news, rambaldi2018bursts, omi2017tdb, filimonovsornette2015 (via claims_classic), wang2012mmhp (abstract via search summary), large2007 (abstract).

**Access note.** HAL (hal.science) serves a proof-of-work bot wall to curl. Older HAL PDFs were retrieved through Wayback Machine snapshots (`web.archive.org/web/<ts>id_/<hal url>`), which are byte copies of the HAL deposit.

---

# PART 1 — Marked Hawkes in LOB / high-frequency modelling

## lu2018 — Lu & Abergel, "High-dimensional Hawkes processes for limit order books: modelling, empirical analysis and numerical calibration", Quantitative Finance 18(2):249–264, 2018. FULL TEXT (HAL hal-01686122 author version dated 29 Oct 2017, via Wayback snapshot 2018-07-27).

(Upgrades the ABSTRACT ONLY entry in claims_hawkes2.md.)

- **Data (Sec. 2.1).** The 30 DAX stocks on XETRA, Feb–Apr 2016, tick-by-tick, 1 µs resolution. Anomalies < 3% of data.
- **Events (Sec. 2.2, Table 1).** Only changes to the best limits (level-1). 12 types: {L, C, M} × {buy, sell} × {0 = does not change mid, 1 = changes mid}.
  - For market orders the 0/1 split is a size-relative-to-queue criterion: M^0 = "order quantity < best ask/bid available quantity", M^1 = "order quantity ≥ best ask/bid available quantity".
  - For limit orders 1 = price inside the spread; for cancellations 1 = total cancellation of the best limit.
- **Marks explicitly not modelled (Sec. 3).** "N is actually a 12-variate counting process, and the marks determining the price jump when an event of type 1 occurs are not modelled." Jump assumed to be one tick; "the average jump size of the best bid and ask prices is 1.08 ticks" (footnote: below 1.01 for some large-tick stocks).
  - So aggressiveness enters only as a **discrete type**, not as a mark with its own distribution.
- **Models.** 1-exponential and 2-exponential linear Hawkes, 12×12 kernel matrix (Sec. 3.1); nonlinear model `λ(t) = (µ + Φ ⋆ dN)^+` (Eq. 3) with negative kernels `−Σ_p α_mnp exp(−β_mnp t)`.
- **Evaluation.** Q–Q plots of time-rescaled durations per dimension (Fig. 2, 4), signature plots of simulated mid-price (Fig. 3), conditional event-transition probabilities (Tables 2, 3, 6).
- **Findings.**
  - Poisson fails; 2-exp beats 1-exp for L^0 and C^0; neither linear model fits C^1 (Sec. 3.2.1).
  - Linear models reproduce the signature-plot shape but "the long-term volatility level is too high" (Sec. 3.2.2).
  - In the linear calibration, kernels for inhibitory pairs are forced to 0 (Table 5). In the nonlinear model they become negative, e.g. medians −0.0319 … −0.1908 (Table 7).
  - Table 8 (median optimal log-likelihood per type): identical for type-0 events; improves for type-1 events, e.g. L^1_buy −1415.8 → −962.6, C^1_buy −1025.0 → −638.6. These are in-sample values; no out-of-sample test was found in the sections read.

## bacry2014 (existing key) — Bacry & Muzy, "Hawkes model for price and trades high-frequency dynamics", Quantitative Finance 14(7):1147–1166, 2014. FULL TEXT (arXiv 1301.1135; Secs. 2.1, 7, 8 checked).

- 4-D model: market orders at ask/bid (T^+, T^-) and mid-price up/down moves (N^+, N^-).
- **Volumes explicitly excluded (Sec. 2.1):** "we will not take into account the volumes associated to each market orders. Though this can be basically done within the framework of marked the point processes, it would necessitate cumbersome notations and make the estimation much more difficult."
- Sec. 8 proposes as future work i.i.d. volume marks entering as `λ^N_t = Φ^N ⋆ dN_t + Φ^I ⋆ f(v_t) dT_t`, with f "what is generally referred into the literature by the 'instantaneous impact function'". Not estimated in this paper.
- Data (Sec. 7): EuroStoxx (FSXE) and Euro-Bund (FGBL) futures, trades at best bid/ask (QuantHouse), 800 trading days from 2009 (to 2012 per bacry2015).

## abergeljedidi2015 — Abergel & Jedidi, "Long-time behavior of a Hawkes process-based limit order book", SIAM J. Financial Math. 6(1):1026–1043, 2015. FULL TEXT (HAL hal-01121711v5, 17 Jul 2015, via Wayback).

- Theory only; no estimation on data in the text read.
- Market orders M^± and limit orders L^±_i **at each level i (1..K ticks from the opposite best quote)** are driven by a multivariate exponential Hawkes process (Sec. 3.1.3). Price level enters as a **discrete event type**, not a mark.
- All orders have a fixed size q (the lot size; Sec. 3.1.1; Eq. 5–6 use q dM, q dL). Cancellations are Cox processes with intensity proportional to queue size, λ^C_i a_i.
- Results: stationarity/ergodicity under ρ(A) < 1 with A_ij = α_ji/β_ji (Prop. 2.2); long-time diffusive price limit with a volatility formula averaged under the stationary law (abstract, Sec. 5).

## zheng2014constrained — Zheng, Roueff, Abergel, "Modelling bid and ask prices using constrained Hawkes processes: ergodicity and scaling limit", SIAM J. Financial Math. 5(1):99–136, 2014. FULL TEXT (arXiv 1301.5007v2).

- Four events: best ask up/down one tick, best bid up/down one tick. "The arrival of these events is described by a marked point process N" — **the mark is the event type in {1,…,p}** (Sec. 2.1).
- Constraint: intensity of mark i is 0 if the spread state S(t−) ∈ A_i (Eq. 2.1); e.g. events 2, 3 impossible when S = 1. S jumps by J(i) at each event (Eq. 2.2). So the state (spread) gates the intensity.
- Exponential fertility → Markov chain; ergodicity and scaling limit (bid, ask, mid behave as co-integrated random walks).
- Illustration (Sec. 4): Eni SpA and Total, 1–10 April 2011. β = 1.65 and 1.79 (half-lives 0.6 s, 0.55 s); spectral radii of the fertility matrix 0.5723 and 0.6221.

## munitokepomponio2012 — Muni Toke & Pomponio, "Modelling trades-through in a limit order book using Hawkes processes", Economics: The Open-Access, Open-Assessment E-Journal 6(2012-22), 2012. FULL TEXT (HAL hal-00745554v1, via Wayback).

- **Aggressiveness by thinning, not by mark.** A "trade-through" is a trade that consumes at least one share at the 2nd limit (Sec. 2.2). Only these events are modelled.
- Data (Sec. 2.3): Thomson-Reuters tick data, BNP Paribas (Euronext Paris), 1 Jun – 29 Oct 2010, 109 days; estimation window 9:30–11:30. About 8% of transactions are trades-through in the illustrative sample (Sec. 4.2).
- Model (19): bivariate exponential Hawkes (ask, bid); MLE.
  - Median half-lives AA, AB, BA, BB: 145, 521, 474, 35 ms.
  - Ratios of average α/β: AB 0.017, BA 0.038 vs AA 0.111, BB 0.120 → "cross-excitation … much weaker than the self-excitation".
  - "Very large variations in the results of the numerical maximization of the likelihood" across days.
- **Time-varying baseline (relevant to Part 2).** Model (20) without cross terms, with λ0 piecewise-linear on 30-min knots (following Bowsher 2007).
  - Table 8 (109 days; four tests = two Ljung–Box + two KS on rescaled durations), share passing all 4 at 1% level: full model const λ0 76.1%; no-cross const λ0 70.6%; full model piecewise-linear λ0 86.2%; no-cross piecewise-linear λ0 87.2%.
  - Average α/β is about the same with constant vs piecewise-linear baseline (Table 5: 6.02/47.6 ≈ 0.126 ask; Table 6: 6.62/56.4 ≈ 0.117 ask; my computation from table averages). The paper itself does not discuss branching-ratio bias.

## richards2025scoretest — Richards, Dunsmuir, Peters, "Score test for marks in Hawkes processes", International Journal of Data Science and Analytics 20(3):3037–3052, 2025 (online 10 Oct 2024; Crossref). FULL TEXT of the Research Square preprint rs-3898697/v1 (30 Jan 2024).

- **Literature statement (Sec. 1), directly relevant to the "widely used" question:** "Despite the well-recognized advantages of incorporating marks into the Hawkes process, the simplest form of a Hawkes process, the unmarked process, remains the most widely used in financial market research. The limited number of studies to date that include marks in a financial application are Kirchner [19], Alfonsi and Blanc [1], Rambaldi et al. [26], Fauth and Tudor [13], Chavez-Demoulin and McGill [7], Embrechts et al. [12] and Liniger [21]. Incorporation of marks remains prohibitive due to the construction of marks and fitting challenges".
  - [19] is Kirchner's 2017 ETH PhD thesis "Perspective on Hawkes Processes"; [21] Liniger is a thesis (not checked).
- **Model.** Univariate Hawkes with vector marks and a normalised boost function `g(X) = h(X;ψ)/E[h(X;ψ)]` multiplying the kernel; H0: ψ = 0 (marks do not boost intensity). The score statistic needs only the unmarked fit; asymptotically χ²(r). Marks may be serially dependent (an unadjusted statistic is inflated under serial dependence; Sec. 6.1).
- **Application (Sec. 7).** Thomson-Reuters Tick History LOB matched to trades on a 1 ms grid; silver futures (COMEX), final 10 trading days of July 2015 (Fig. 1 caption: 20–31 Jul 2015), bid side, events = any LO/MO/C at levels 1–5; 60,691 events on 31 Jul 2015. 21 candidate marks (depth-, volume-, price-, count-based; Table 1). Tested on segments of 1,000–10,000 events because a whole day "is not appropriate with a single stationary model".
  - Highest share of 1,000-event segments where the mark is significant at 1%: Imb 55.1%, B.opp.depth 54.6%, Rel.Imb 53.9%, B.MOLOCV 53.4%, B.CountMOLOC 53.4%, B.MOLOV 51.8%, B.depth 50.6%. "These are all volume-based."
  - So **roughly half** of segments reject "no mark effect" for the best marks; no marked model is then fitted or compared out of sample in this paper.

## clinet2021scoretest — Clinet, Dunsmuir, Peters, Richards, "Asymptotic distribution of the score test for detecting marks in Hawkes processes", Statistical Inference for Stochastic Processes 24(3):635–668, 2021. FULL TEXT of Sec. 1–2 (arXiv 1904.13147v1).

- Theory: score statistic for H0 "marks do not impact intensity" is χ² under the null and non-central χ² under local alternatives; existence of stationary marked Hawkes with serially dependent marks.
- Intensity (Eq. 1), following Liniger (2009) and Embrechts et al. (2011): `λ_g(t) = η + ϑ ∫ w(t−s; α) g(x; φ, ψ) N_g(ds × dx)`, with `E_φ[g] = 1` for stationarity.
- Intro: "Increasingly marked Hawkes processes, in which marks attached to past event times influence future intensities, are being considered", citing Richards et al. for LOB futures data. No data analysis in this paper.

## josephjain2024marked — Joseph & Jain, "Non-parametric estimation of multi-dimensional marked Hawkes processes", arXiv 2402.04740v1 (7 Feb 2024). FULL TEXT of Sec. 1 and 4.4. Journal publication: not found (OpenAlex lists only arXiv/RePEc).

- Shallow neural-network kernels φ_dj(t, m) taking time and a continuous mark (SNH with marks; NNNH with marks for non-linear).
- **Data (Sec. 4.4.1).** Binance market orders, BTC-USD and ETH-USD, buy/sell (4-D), **one 30-min window**, 16 Jul 2022 02:00–02:30 UTC, 30,388 events (Table 1). Mark = traded volume.
- **Findings (qualitative, Fig. 9–11).** Self-excitation constant in volume for sell BTC, decreasing for buy BTC, increasing for buy and sell ETH. QQ plots indicate a volume dependency for sell BTC-USD and buy ETH-USD, "while there is no observable dependency for the volume of trade in the other two dimensions". No likelihood-ratio or out-of-sample test reported in the sections read.

## fabre2025neuralhawkes (existing key) — Fabre & Muni Toke, QF 25(5):671–698, 2025. FULL TEXT of Sec. 2.1 and 4.1–4.2 (arXiv 2401.09361v3, 4 Nov 2024).

- Marked kernel form (Eq. 2): `λ^i_t = µ^i + Σ_j ∫ ϕ^ij(t−s, ξ^j_s) dN^j_s`, marks i.i.d.; branching ratio = spectral radius of mark-averaged norms (Eq. 3–4). Non-parametric (PINN solution of the Fredholm moment equation); mark domain discretised.
- **Data (Sec. 4.1).** Coinbase trades, 15 pairs, 1–30 Dec 2023 (SUN ZU Lab), µs timestamps; BTC-USD 5,196,281 events (Table 8).
- **Volume-mark result (Sec. 4.2).** BTC-USD, D = 1, M = 15 log-spaced volume bins from 100 to 100,000 USD (Table 9; 46% of trades ≤ 100 USD). "The aggregated kernel is a concave non-decreasing function of the volume and reaches a plateau for sizes that are larger than 50,000 USD" (Fig. 15). Kernels of large trades show extra latency peaks near 70 µs and 200 µs. Consistent with Rambaldi et al. (2017) "in the sense that the shape of the kernel depends on the traded volume".
- No test against an unmarked model reported in that section.

## chevalier2023disorder — Chevalier, Hafsi, Ly Vath, "Uncovering market disorder and liquidity trends detection", arXiv 2310.09273v1 (13 Oct 2023). FULL TEXT of Sec. 2 and 4.1–4.3. Journal publication: not found (OpenAlex lists arXiv/RePEc/HAL only).

- **Genuinely marked (volume) model with a power-law boost.** Trades-through (Def. 2.1: (type, depth, volume)) on bid/ask as a 2-D marked Hawkes. Ground intensity (Eq. 5–6): `λ^A_g(t) = µ^A(t) + ∫ α_AA e^{−β_AA(t−u)} g_A(v) N^A(du×dv) + ∫ α_AB e^{−β_AB(t−u)} g_B(v) N^B(du×dv)`, factorised kernel, `g_i(v) = v^{η_i}/E[v^{η_i}] = β_i^{η_i}/Γ(1+η_i) · v^{η_i}` with exponential volume law `f_i(v) = β_i e^{−β_i v}` (Sec. 4.2).
- Baselines µ^A, µ^B piecewise constant on 14 half-hour intervals (Sec. 4.2).
- Log-likelihood splits into ground-intensity and mark-density terms with no shared parameters (Prop. 4.1, Remark 4.3).
- **Data (Sec. 4.1).** Refinitiv tick data, BNP Paribas (Euronext Paris), all of 2022, 9:30–17:00; trades-through limits 1–4. 4.89% of transactions produce at least one trade-through; beyond limit 4 only 0.0126%.
- **Results (Sec. 4.3).** Fitted volume-law parameters β_A, β_B in 0.009–0.013. Mutual excitation weak vs self-excitation. "Average branching ratio of 0.83." η_i are shown only as boxplots (Fig. 6); **no numerical η is stated in the text**. May 2022 calibrations pass KS 50.2% at 5%, 60.8% at 2.5%, 82.5% at 1%; Ljung–Box passed up to lag 20 at 5%.
- **No comparison against the unmarked model (g ≡ 1) is reported**; Remark 4.2 notes only that g ≡ 1 recovers Muni Toke–Pomponio.
- The paper's main purpose is CUSUM-type quickest detection of a change in trade-through intensity (liquidity regime change), Sec. 3, 4.4 (relevant to Part 2; detection results not transcribed).

## chevalier2024incomplete — Chevalier, Hafsi, Ly Vath, "Optimal execution under incomplete information", arXiv 2411.04616v2 (12 Aug 2026). FULL TEXT of Sec. 1–2 and 6 (setup). Journal publication: not found.

- Marked (volume) Hawkes buy/sell order flow **modulated by a hidden Markov chain I (liquidity regime)**: in regime i, `dλ^{i,±} = −β_i(λ^{i,±} − λ^i_∞)dt + ∫ φ^i_s(v/m_{1,i}) n^±(dt,dv) + ∫ φ^i_c(v/m_{1,i}) n^∓(dt,dv)` (Eq. 1), regime-dependent volume law ν_i. Filtering equations for I; impulse-control liquidation.
- Numerics only (Sec. 6): two regimes, exponential volumes, boost `ζ^η/Γ(1+η) v^η`, regime 2 = regime 1 with buy/sell intensities swapped; η = 0.05 is a chosen default (Table 1), **not an estimate**. Remark 6.1: parameters "could be estimated jointly with the transition rate matrix … as described in Wu et al." (Wu, Ward, Curley, Zheng, AoAS 16(2):1171–1190, 2022, a social-interaction MMHP paper). No calibration to market data.

## clinet2022qla — Clinet, "Quasi-likelihood analysis for marked point processes and application to marked Hawkes processes", Statistical Inference for Stochastic Processes 25(2):189–225, 2022 (online 2021). FULL TEXT of Sec. 3 (arXiv 2001.11624v2).

- Theory (QLA, ergodicity) for "generalized exponential marked Hawkes processes" with multiplicative boost g_αβ(x) on the kernel.
- States that Richards et al. (2019 version) "fits the above model on financial limit order book data, linear and quadratic shapes have been proposed for the boost function … with several mark processes ranging from trade volumes, price transformations, market imbalance, to transformations of the counting process N itself" (Sec. 3, after Eq. 3.2). Secondary for that claim.
- Worked example (Eq. 3.10–3.12): a **queue-reactive Hawkes** with the queue size X as the mark process (L adds 1, M and C remove 1), cancellations at rate ν_C x. Illustration only; no data.

## bacrymuzy2016wh (existing key) — Bacry & Muzy, IEEE Trans. Inf. Theory 62(4):2184–2202, 2016. FULL TEXT of Sec. I, III (arXiv 1401.0903v2).

- Sec. III-B: with piecewise-constant mark functions f^ij (M pieces), "the process N basically corresponds to a non-marked Hawkes process of dimension DM". This is the formal equivalence "binned marks = more event types" used by rambaldi2017marked.
- Applications (Sec. I, V): the **financial** application is a 1-D *unmarked* Hawkes for market orders on EuroStoxx and Euro-Bund futures (1000 days, May 2009 – Sep 2013), kernel "strikingly well fitted by a power-law function with an exponent very close to 1". The **marked** application is earthquakes (NCEC magnitudes), confirming ETAS's exponential mark function.

## deschatre2022electricity — Deschatre & Gruet, "Electricity intraday price modelling with marked Hawkes processes", Applied Mathematical Finance 29(4):227–260, 2022 (Crossref). FULL TEXT of Sec. 2–3 (arXiv 2103.07407).

- Mid-price of German EPEX Spot intraday hourly products (deliveries 18h, 19h, 20h), Jul–Sep 2017, 1-s grid, from 9 h to 1 h before delivery.
- Bivariate (up/down) Hawkes whose excitation is **linear in the price-jump size mark**: `λ_t = µ(t/T)·(1,1)ᵀ + ∫ ϕ(t−s) (J_s dN^+_s, J_s dN^-_s)ᵀ` (Eq. 1), null diagonal in ϕ (cross-excitation only); **time-dependent baseline** `µ0 e^{κ t/T}` (increasing activity toward delivery).
- MLE; jump sizes not modelled parametrically (first two moments only). Table 2: κ ≈ 3.5 for all three maturities; E(J) = 0.13 €/MWh.
- Evaluation is by reproduction of moments and signature plots (Figs. 8–10). **No test against an unmarked or constant-baseline model** in the sections read.

## Other Part-1 items (lower read level)

- **kirchnervetter2022** — Kirchner & Vetter, "Hawkes model specification for limit order books", European Journal of Finance 28(7):642–662, 2022 (online 25 Jun 2020; Crossref). ABSTRACT ONLY (Semantic Scholar). "We model the flow of market orders, limit orders, and cancelations by a self- and crossexciting multitype marked Hawkes process with state-dependent baseline intensities. The marks carry the order sizes and the state of the book is summarized by the 'limit-order-book imbalance'. … we select the non-zero excitements (the 'Hawkes skeleton'), the shape of the decay kernels, and the shape of the impact functions in a nonparametric manner." Imbalance explains the probability of a bid (vs ask) market order "in a perfectly linear manner". MLE. Data, size-impact estimates and any marked-vs-unmarked comparison: UNVERIFIED (full text not accessed; Kirchner's 2017 ETH thesis "Perspectives on Hawkes processes" also not accessed).
- **hawkes2018review** — A. G. Hawkes, "Hawkes processes and their applications to finance: a review", Quantitative Finance 18(2):193–198, 2018. METADATA ONLY (Crossref; closed; no abstract available via Crossref/Semantic Scholar). Content on marks: UNVERIFIED.
- **large2007** (existing key) — abstract only (see claims_classic.md: 10-variate Hawkes, Barclays on LSE). Whether the 10 types split by aggressiveness: UNVERIFIED from the primary; Muni Toke–Pomponio (Sec. 5) refer to "the dynamics of aggressive market orders described e.g. in Large (2007)" (secondary).
- **chung2024nmhp** (existing key) — still METADATA ONLY (no accessible abstract or text).

---

# PART 2 — Regime-switching / Markov-modulated / time-varying-baseline Hawkes

## fabremunitoke2026mmhp — Fabre & Muni Toke, "High-frequency market manipulation detection with a Markov-modulated Hawkes process", European Journal of Finance 32(3):309–341, 2026 (OpenAlex). FULL TEXT of Sec. 1–2 and 4 (arXiv 2502.04027v1, 6 Feb 2025).

- **Model (Eq. 3–5).** Hidden M-state CTMC S_t; `λ_t = Σ_i 1{S_t=i} (µ^i + Σ_{t_k<t} ϕ^i(t − t_k))` — **both baseline and kernel switch**. To get a tractable EM, the kernel is made piecewise constant between events on a grid of step δ ("MMHP-δ", Def. 2.3); δ = 100 s is described as a proxy of Wang et al.'s MMHPSD. Viterbi for state decoding (historical and online).
- **Literature statement (Sec. 1.1):** "the Markov-modulated Hawkes process (MMHP) has received much less attention due to the multiple challenges that arise in their estimation"; cites Wang (2010), Wang et al. (2012) (stepwise decay → EM) and Wu et al. (2022) (Bayesian, one Poisson + one Hawkes regime, "small data sets"). "To the best of our knowledge, our work is the first to propose an application of Markov-modulated point processes to the detection of suspicious events in a trading environment."
- **Data (Sec. 4.1–4.2).** Coinbase SEI-USD trades, 1 Dec 2023 – 31 Mar 2024, µs timestamps; only zero-price-return trades used; intraday seasonality removed first. Daily fits 1 Dec 2023 – 24 Mar 2024; goodness-of-fit **out of sample** on the same weekday of the following week.
- **Results.**
  - Table 1 (median AIC rank among 12 models, M ∈ {2,3,4} × δ ∈ {1,10,100} s plus MMPP): MMHP beats MMPP at each M; more regimes and smaller δ improve AIC (M = 4, δ = 10: median rank 2; MMPP M = 2: rank 12).
  - Fig. 7: out-of-sample QQ plots better for MMHP-δ (δ = 1 or 10) than MMPP.
  - With M = 3, δ = 1 s: stationary distribution 90.31% / 9.55% / 0.14% (normal / high / extreme burst). Extreme-burst state = 24.20% of buy and 21.39% of sell volume (216M USD) vs 25% / 24.30% (233M USD) under MMPP — "the introduction of a Hawkes component not only fits the data better but also makes the detection more conservative."
  - Compute: one day takes ≈ 15 min at δ = 100 s and ≈ 10 h at δ = 1 s; M > 4 not attempted.
  - Branching ratios by regime: not transcribed (states are ordered by α only).
- Comparison is MMHP vs MMPP, **not** MMHP vs a single-regime Hawkes.

## swishchuk2019chp (existing key) — regime-switching part. FULL TEXT re-checked (arXiv 1712.03106v1).

- Def. 6 (one-dimensional regime-switching Hawkes): `λ_t = <λ, Y_t> + ∫_0^t <µ(t−s), Y_s> dN_s`, with Y an N-state Markov chain (standard-basis representation, Eq. 12). **Both baseline and kernel switch.** Def. 7: RSCHP = compound sum over this N.
- Theory: LLN and diffusion limit for RSCHP (Thm 2).
- Data use is a heuristic only ("Remark on Regime-switching Case", Sec. 4): two states defined as intensity above / below its average; transition matrix from relative frequencies; for CISCO 5 days λ1 = 0.03238898, λ2 = 0.02545533, (p1, p2) = (0.2, 0.8), λ̂ = 0.02688, "the error does not exceed 0.0055". **No regime-switching Hawkes is estimated by likelihood/filtering in this paper.**
- Intro cites "Cohen et al. [2014] derived an explicit filter for Markov modulated Hawkes process" (reference list: Cohen, S. and Elliott, R. (2014), "Filters and smoothness for self-exciting Markov modulated counting process", IEEE Trans. Aut. Control) and "Vinkovskaya [2014] considered a regime-switching Hawkes process to model its dependency on the bid-ask spread". The Cohen–Elliott paper could not be found in Crossref under that title: **NOT VERIFIED**.

## vinkovskaya2014thesis — Vinkovskaya, "A Point Process Model for the Dynamics of Limit Order Books", PhD thesis, Columbia University, 2014, DOI 10.7916/D88913WW (DataCite). ABSTRACT ONLY (DataCite; Academic Commons is behind a bot wall).

- Order-flow events (market, limit, cancel) show "clustering in time, cross-correlation across event types and dependence of the order flow on the bid-ask spread"; modelled by "a multivariate self-exciting point process with multiple regimes that reflect changes in the bid-ask spread".
- MLE on TAQ data for US stocks. The model "may be used to obtain predictions of order flow and … its predictive performance beats the Poisson model as well as Moving Average and Auto Regressive time series models."
- Note: the regime is the **observed** spread, so this is a state-dependent model (like morariupatrichi2022), not a hidden-Markov one. Stocks, dates, numbers: UNVERIFIED.

## chenhall2013seppvb — Chen & Hall, "Inference for a nonstationary self-exciting point process with an application in ultra-high frequency financial data modeling", J. Applied Probability 50(4):1006–1024, 2013. FULL TEXT (Cambridge OA PDF).

- SEPPVB: constant baseline ν replaced by ν(t); asymptotic theory for MLE; Wald/score/LR tests asymptotically χ².
- **Data (Sec. 5.2).** All trades of ANZ (Australian Stock Exchange), December 2008, 21 days, 10:00–16:00 AEST; same-timestamp trades merged (1,493–5,821 per day, mean 3,399). Baseline = B-spline with knots at each trading hour; kernel exponential `γ1 e^{−γ2 t}` or polynomial `γ1/(1+t)^{1+γ2}`; fitted per day.
- Results: SEPPVB fits "considerably better" than GGACD(1,1) (Table 2, KS p-values of Rosenblatt residuals per day; e.g. 12-02: 0.43 vs 0.0011). "The estimated integral of the excitation function varies in the range 0.10–0.66" (Table 3). Baseline peaks near open/close and locally around 11:00 AEST (other Asian markets opening).
- **No comparison against a constant-baseline Hawkes** in the text read.

## clinetpotiron2018 — Clinet & Potiron, "Statistical inference for the doubly stochastic self-exciting process", Bernoulli 24(4B), 2018. FULL TEXT of Sec. 1, 8 (arXiv 1607.05831).

- Exponential Hawkes with **all three parameters time-varying** (ν_t, a_t, b_t; Eq. 5.3 `λ(t) = ν_t + ∫ a_s e^{−b_s(t−s)} dN_s`), target = integrated parameter. Naive block-wise local MLE averaging has an exploding bias; a first-order bias-corrected estimator with CLT is given.
- **Data (Sec. 8).** AAPL trades on NASDAQ, 2015, 251 days, 9:30–15:30; ~15,000 trades/day on average. With 30-min local blocks, "given how volatile the estimates are with respect to their own confidence interval, it is clear that neither the parametric model nor the seasonal component model can be satisfactory"; a* oscillates around a seasonal path, b* "can really go far off … with no specific pattern". Daily estimates average roughly (ν, a, b) = (0.56, 11, 40) with s.d. (0.24, 2, 8). (a/b ≈ 0.28 is my arithmetic on these averages, not a reported branching ratio.)

## roueffvonsachs2019 — Roueff & von Sachs, "Time-frequency analysis of locally stationary Hawkes processes", Bernoulli 25(2), 2019 (OpenAlex). FULL TEXT of Sec. 1, 4 (arXiv 1704.01437v3). Builds on roueff2016lshp: Roueff, von Sachs & Sansonnet, "Locally stationary Hawkes processes", SPA 126(6):1710–1743, 2016 (METADATA ONLY).

- Locally stationary Hawkes: baseline **and fertility function** vary slowly in rescaled time.
- Data (Sec. 4): transaction times of ESSI.PA and TOTF.PA (Paris), 61 days in Feb, Jun, Nov 2013. Nonparametric local mean density and local Bartlett spectrum (bandwidth ≈ 1 h 16 min 30 s).
- Findings are descriptive: spectrum shape varies along the day; Poisson-normalised spectra highest around the lunch break and always > 1; the authors conclude locally stationary Hawkes models are better adapted "not only because the local Bartlett spectrum is not constant along the frequencies but also because its shape varies along the time". No branching-ratio numbers.

## deschatre2025nonlocal — Deschatre, Gruet, Lotz, "A non-local estimator for locally stationary Hawkes processes", arXiv 2506.02631v1 (3 Jun 2025). FULL TEXT of abstract and Sec. 4–5. Journal publication: not found.

- Hawkes with time-dependent baseline µ(t/T) **and reproduction rate g(t/T)** multiplying the kernel; global MLE; consistent likelihood-ratio test of H0: g constant.
- Data (Sec. 4): German intraday power market LOB, hourly product delivering 18:00–19:00, 21 sessions in March 2023; 4-D (limit/market × ask/bid), 17 h per session; µ and g are degree-4 Bernstein polynomials.
- Result: "The null hypothesis that g is constant is rejected at the 95% confidence level for all but one of the trajectories" (exception 14 Mar). The endogeneity rate tends to rise, peak a few hours before delivery, then fall (Fig. 4).

## Time-varying baseline and the branching-ratio bias (consolidated)

- **wehrli2021scale** — Wehrli, Wheatley, Sornette, "Scale-, time- and asset-dependence of Hawkes process estimates on high frequency price changes", Quantitative Finance 21(5):729–752, 2021 (Crossref). ABSTRACT ONLY (Semantic Scholar). EBS EUR/USD and CME E-mini S&P 500 mid-price changes. With immigration intensity "either piecewise constant or adaptive logspline, estimated using an expectation maximization (EM) algorithm" chosen by information criteria, "the estimated branching ratio depends little upon window size and is usually far from criticality". "The (positive) bias incurred by keeping the immigration intensity constant is small for time scales up to two hours, but can become as high as 0.3 for windows spanning days." Branching ratio shows intraday seasonality; E-mini more endogenous than spot FX; criticality also rejected with INAR models at month scales.
- **wheatley2019endoexo** — Wheatley, Wehrli, Sornette, "The endo–exo problem in high frequency financial price fluctuations and rejecting criticality", Quantitative Finance 19(7):1165–1178, 2019 (Crossref). ABSTRACT ONLY (Semantic Scholar/OpenAlex). EM with BIC-selected flexibility of the deterministic background intensity; "we strongly reject the hypothesis that the considered financial markets are critical at univariate and bivariate microstructural levels." Data and numbers: UNVERIFIED.
- **rambaldi2015news** (existing; claims_hawkes3.md): adding a news kernel lowers n; "a Hawkes model which does not consider news-triggered non stationarity could overestimate n". Unconditional n 0.84–0.95 (EBS FX 2012, Table III).
- **omi2017tdb** (existing; claims_hawkes3.md): flexible µ(t) gives branching ratio 0.41 (Nikkei 225 mini, 2016); "clearly overestimated for the CONST and PL2h models" (constant-baseline row ≈ 0.57–0.83 per extracted Table II; row assignment UNVERIFIED).
- **filimonovsornette2015** (existing; claims_classic.md): n ≈ 1 estimates attributed to biases incl. "regime shifts"; own E-mini/commodity estimates n = 0.7–0.8.
- **munitokepomponio2012** (Part 1): piecewise-linear baseline raises the share of days passing all four tests from 70.6–76.1% to 86.2–87.2% (Table 8), with α/β roughly unchanged (my computation).
- **chevalier2023disorder** (Part 1): 14 half-hour piecewise-constant baselines; average branching ratio 0.83 (BNP trades-through, 2022); no constant-baseline comparison.

---

# SYNTHESIS (verified facts only)

## Part 1 verdict: "marked Hawkes is widely used in LOB modelling"

**Not supported as stated. Supported only if "mark" means a discrete event type.**

(a) **Event type as a discrete mark — very common.** Almost every Hawkes LOB model splits events into types, often by side, order type, aggressiveness or price level, and a binned mark is formally just a higher-dimensional unmarked Hawkes (bacrymuzy2016wh Sec. III-B: piecewise-constant mark functions ≡ DM-dimensional unmarked process). Verified examples: lu2018 (12 types; 0/1 = does/does not move the mid, for market orders = size ≥ best-queue size), munitokepomponio2012 (trades-through only), abergeljedidi2015 (limit orders typed by level i, fixed size q), zheng2014constrained (event type is called the "mark"), morariupatrichi2022 (state as mark), rambaldi2017marked (volume binned into 6 classes).

(b) **Continuous / size mark that changes the excitation — a small literature.**
- Explicit statement: Richards–Dunsmuir–Peters (richards2025scoretest, Sec. 1): "the unmarked process, remains the most widely used in financial market research. The limited number of studies to date that include marks in a financial application are" seven (Kirchner; Alfonsi–Blanc; Rambaldi et al.; Fauth–Tudor; Chavez-Demoulin–McGill; Embrechts et al.; Liniger). Fabre–Muni Toke (2026, Sec. 1.1) say the same for Markov-modulated Hawkes.
- Bacry & Muzy (2014, Sec. 2.1) deliberately leave out volumes because it "would … make the estimation much more difficult". Lu & Abergel (2018, Sec. 3) leave price-jump marks out because the mean jump is 1.08 ticks.
- Verified empirical marked fits on HF financial data, with the mark and the effect found:
  - **Volume, binned:** Rambaldi–Bacry–Lillo (Bund and DAX futures). Large trades excite more and for longer; the factorised φ(t)f(v) form is rejected.
  - **Volume, non-parametric:** Fabre–Muni Toke (2025, Coinbase BTC-USD, Dec 2023). The aggregated mark kernel is concave and non-decreasing, with a plateau above 50,000 USD.
  - **Volume, neural:** Joseph–Jain (arXiv, Binance, one 30-min window). The volume effect is visible in 2 of 4 dimensions.
  - **Volume, power law:** Fauth–Tudor (FX, arXiv; no exponent reported) and Chevalier–Hafsi–Ly Vath (arXiv, BNP 2022; η shown only in a boxplot).
  - **Price-jump size:** Lee–Seo (NYSE stocks, η ≈ 0.2) and Deschatre–Gruet (German intraday power, linear in jump size).
  - **Many candidate marks, score-tested:** Richards et al. (silver futures). About half of the 1,000-event segments reject "no mark effect" for the best marks (Imb 55.1%).
  - **Order sizes, abstract only:** Kirchner–Vetter.
  - **Return exceedances (extremes, not LOB events):** Chavez-Demoulin–McGill and Embrechts et al., abstracts only.
- Counter-evidence: Jain et al. (2024, NASDAQ) report "some lack of support" that past order sizes affect arrival rates. Their own description of this evidence is "weak evidence".
- **Tests against the unmarked model are rare.**
  - Formal: the score test (Richards et al.) and the factorisation rejection (Rambaldi et al.).
  - Informal: QQ-plot comparisons (Joseph–Jain).
  - Chevalier et al., Deschatre–Gruet, Fauth–Tudor and Lee–Seo report no comparison with g ≡ 1 in the sections read.
  - **No out-of-sample marked-vs-unmarked forecast comparison was found.**
- No volume-mark exponent tied to the square-root law was found (see claims_hawkes3.md; still NOT FOUND).

## Part 2 verdict: regime-switching / Markov-modulated Hawkes in finance

- **Hidden-Markov (MMHP) Hawkes estimated on real HF financial data: one verified paper.** fabremunitoke2026mmhp (Coinbase SEI-USD, EM with a stepwise kernel, AIC and out-of-sample QQ against MMPP). That paper says MMHP "has received much less attention due to the multiple challenges … in their estimation".
- Other verified MMHP/RS-Hawkes work is theory, simulation or non-finance:
  - swishchuk2019chp RSHP: theory plus a two-state frequency heuristic on CISCO.
  - chevalier2024incomplete: hidden-Markov marked Hawkes with filtering for execution; numerics only.
  - wang2012mmhp: earthquakes.
  - wu2022mmhp: email and animal behaviour.
  - vinkovskaya2014thesis: regimes = **observed** spread, TAQ; abstract only.
- **Time-varying baseline is common and the bias result is verified.**
  - Deterministic or flexible µ(t):
    - chenhall2013seppvb: ASX ANZ, B-spline µ(t); branching 0.10–0.66.
    - munitokepomponio2012: piecewise-linear µ(t).
    - omi2017tdb: branching ratio 0.41 vs overestimated with a constant baseline.
    - rambaldi2015news: news kernel lowers n.
    - wheatley2019endoexo and wehrli2021scale: EM with BIC/IC-selected baselines. **Positive bias from a constant immigration intensity "can become as high as 0.3 for windows spanning days"**, small up to about 2 h. Criticality rejected.
    - filimonovsornette2015.
    - chevalier2023disorder.
    - jain2024chp: time-of-day factor.
  - Time-varying kernel or reproduction rate as well:
    - clinetpotiron2018: AAPL 2015; parameters vary intraday beyond any seasonal model.
    - roueffvonsachs2019: Paris stocks 2013; spectrum shape changes during the day.
    - deschatre2025nonlocal: German power; constant reproduction rate rejected in 20 of 21 sessions.
- **Comparison against a single-regime Hawkes:**
  - Out-of-sample: none found for hidden-regime models. Fabre–Muni Toke compare against MMPP, not a single-regime Hawkes.
  - In-sample: the baseline-flexibility papers (Omi, Wehrli et al., Rambaldi) compare against constant-baseline Hawkes.

# UNVERIFIED / NOT FOUND (this file)

- **NOT FOUND:**
  - Cohen & Elliott (2013/2014), filters for self-exciting Markov-modulated counting processes. It is cited by swishchuk2019chp as IEEE TAC 2014, but Crossref title and author searches returned no match.
  - Any regime-switching Hawkes fitted to bitcoin/crypto data other than fabremunitoke2026mmhp.
  - Any out-of-sample comparison of a marked vs an unmarked Hawkes, or of a hidden-regime vs a single-regime Hawkes, on LOB data.
  - Any estimated power-law exponent for volume in a Hawkes intensity on LOB data with a reported number (Fauth–Tudor and Chevalier et al. specify v^η, but no number appears in the text).
  - A Swishchuk paper titled "Hawkes processes in limit order books with regime switching": not found. The regime-switching material is inside swishchuk2019chp.
  - "Chen, Hall et al. marked Hawkes for order flow": not found. The verified Chen–Hall paper (JAP 2013) is about a time-varying baseline, unmarked.
- **ACCESS-LIMITED:**
  - kirchnervetter2022, wheatley2019endoexo, wehrli2021scale and vinkovskaya2014thesis: abstract only.
  - hawkes2018review, roueff2016lshp, wang2012mmhp (beyond the search summary), chung2024nmhp: metadata only.
  - large2007: abstract only.
  - Kirchner's ETH thesis and Richards' UNSW thesis: not accessed.
  - Luo & Krishnamurthy (arXiv 2308.06769; Hawkes change-point via Fréchet statistic, "cryptocurrency datasets"): abstract seen only, not added.
- **UNVERIFIED details:**
  - η values in Chevalier et al. (boxplot only).
  - Regime-specific branching ratios in fabremunitoke2026mmhp (not transcribed).
  - Data, sample and size-impact shape in kirchnervetter2022.
  - Datasets in wheatley2019endoexo.
  - Liniger (2009) thesis content.
  - Whether Large (2007) types events by aggressiveness.
