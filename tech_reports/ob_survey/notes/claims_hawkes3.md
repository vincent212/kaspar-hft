# Hawkes references, set 3: marks, Markovian control, compound, news, fixed income, reference checks

Compiled 2026-10-03.

**Sources.**
- Crossref DOI records give venue and metadata.
- arXiv PDFs, read via pdftotext, give content.
- Where no PDF could be read, publisher or RePEc abstracts are used.
- The arXiv API was rate-limited (HTTP 429), so arXiv IDs were found by web search and the PDFs downloaded directly.

**Read levels.**
- **FULL TEXT** means the relevant sections of the arXiv version were read. That does not mean every page.
- **ABSTRACT ONLY** and **METADATA ONLY** mean what they say.

Quotes and equation numbers come from the arXiv version named in each entry. Numbering in the published version may differ. Formulas are ASCII transcriptions in each paper's own notation.

BibTeX for new keys is in `bib/refs_hawkes3.bib`. Reused keys (not redefined) are listed in that file's header.

Items already verified in `claims_hawkes2.md` and `claims_classic.md` are not redone. These are `bacry2013`, `bacry2016`, `jaisson2015/2016`, `lu2018` (abstract), `rambaldi2017marked`, `blanc2017qhawkes` and the Da Fonseca–Zaatour trio. Below, those are only re-checked on the specific questions asked.

---

## Topic 1. Marked Hawkes processes and empirical mark effects

### Formal factorisation: ground intensity × mark density, and the likelihood

**rasmussen2018notes** — Rasmussen, "Lecture Notes: Temporal Point Processes and the Conditional Intensity Function", arXiv 1806.00221v1 (1 Jun 2018). FULL TEXT (Secs. 2.4 and 3.1).

- **Sec. 2.4.** The mark κ at time t has conditional density `f*(κ|t) = f(κ | t, H_{t-})`. The marked conditional intensity is defined as
  ```
  λ*(t,κ) = λ*(t) f*(κ|t)
  ```
  where λ*(t) is "called the ground intensity" and may depend on past marks.
  - "Unpredictable mark": f* does not depend on the past.
  - "Independent mark": κ_i is independent of everything else except possibly t_i.
- **Example 2.6 (ETAS).** Ground intensity `λ*(t) = µ + α Σ_{t_i<t} e^{βκ_i} e^{−γ(t−t_i)}` with mark density `f*(κ|t) = δ e^{−δκ}`. This is an exponential mark-impact function.
- **Prop. 3.1.** The marked likelihood is `L = (Π_i λ*(t_i, κ_i)) exp(−Λ*(T))`, with `Λ*(t) = ∫_0^t λ*(s) ds` the integrated *ground* intensity.
- **Likelihood split (my derivation, not stated as such in the text).** Substituting the factorisation gives
  ```
  log L = [Σ_i log λ*(t_i) − Λ*(T)] + Σ_i log f*(κ_i | t_i)
  ```
  The two brackets separate when the ground and mark parameters are disjoint.
- Rasmussen points to Daley & Vere-Jones (2003, 2008) for the measure-theoretic treatment.
- **UNVERIFIED:** the exact section and proposition numbers in `daley2003`. The book itself was not consulted.

**bacry2015** (existing key) — Bacry, Mastromatteo, Muzy, "Hawkes processes in finance", Market Microstructure and Liquidity 1(1):1550005, 2015. FULL TEXT of arXiv 1502.04592.

- **Sec. 2.2.1 "Marked Hawkes processes", Eq. (11).**
  ```
  λ^i_t = µ^i + Σ_{m=1}^M φ^{i,k_m}(t − t_m, ξ_m)
  ```
  - Marks are "typically assumed to be i.i.d. random variables drawn with each event and sampled from a common distribution p(ξ)".
  - "A typical choice for the interaction kernel is the one φ^{ij}(t, ξ) = φ^{ij}(t) χ^{ij}(ξ), in which one assumes a factorized form for the effect of the marks."
  - In finance, marks model trade volumes (refs [30] Fauth–Tudor and [5] Bacry–Iuga–Lasnier–Lehalle) or drawdown intensity ([27] Embrechts et al. and [18] Chavez-Demoulin–McGill).
  - "Multivariate Hawkes processes can also be seen as an example of Hawkes processes with interacting marks [51]."
- **Sec. 4.** This is the survey's description of Fauth–Tudor:
  - "a multiplicative mark (the function χ in Sec. 2.2.1) that corresponds to a power-law function of the volumes: χ(v) = C v^ν".
  - The data are EUR/USD and EUR/GBP, 30-01-2012 to 10-03-2012.
  - "As the transaction volumes increase, the inter-trade durations decrease."
  - **The survey gives no value of ν.**
- **Sec. 5.1** quotes Bacry–Muzy [7], whose 4-D price/trade model "did not account for the volume of the orders nor the price jump sizes".
- The survey does not give a split-likelihood formula for marks.

### Empirical mark estimates

**rambaldi2017marked** (existing key; re-checked) — Rambaldi, Bacry, Lillo, QF 17(7):999–1020, 2017. FULL TEXT of arXiv 1602.07663.

- **Re-check result: the paper does NOT estimate a power of volume.**
- It rejects the factorised form `λ_t = µ + ∫ f(v_s) φ(t−s) dN_s` (Eq. 7) because "under the naive model of Eq. 7 all the kernels φ(j→i) would have the same shape, while empirical data shows large differences among them" (Fig. 1, DAX).
- It uses `λ_t = µ + ∫ φ(t−s; v_s) dN_s` (Eq. 8), implemented by **binning volume into D classes, each a component of a multivariate Hawkes process**, estimated non-parametrically (Wiener–Hopf).
- Footnote 1 states an additivity requirement `φ(t, v1+v2) = lim_{Δt→0} φ(t,v1) + φ(t+Δt, v2)`.
- **Bins (Table 1, six bins).**
  - Bund: 1, 2, 3, (3,7], (7,20], (20,∞) contracts.
  - DAX: 1, 2, 3, (3,5], (5,10], (10,∞).
  - DAX size-1 trades are 64.5% of trades.
- **Sec. 4.1 findings.**
  - "Models where the time dependence is separated from size dependence appear to be inadequate."
  - "The influence of large trades is more intense and more persistent."
  - For lags above about 1 s, the diagonal conditional laws decay as a power law "with exponents smaller than one". These are *time* exponents, not volume exponents.

**fauthtudor2012** — Fauth & Tudor, "Modeling First Line of an Order Book with Multivariate Marked Point Processes", arXiv 1211.4157v1 (17 Nov 2012). FULL TEXT. Journal publication: UNVERIFIED (none found).

- **Model, Eq. (3.6).**
  ```
  λ_i(t, v | F_t) = µ_i + Σ_{j=1}^d ν_ij ∫_{(−∞,t)×R+} h_i(t−s) g_j(v) N_j(ds × dv)
  ```
  - The four components are up/down jumps of the best bid and ask.
  - Candidate impact functions (Eq. 3.7) are `g̃(x) = x^α` and `g̃(x) = exp(αx)`, normalised so that `E[g(V)] = 1` (Eq. 3.8).
  - With exponential mark law `β e^{−βx}`, the power law is chosen (Eq. 5.4): `g(x) = β^α / Γ(α+1) · x^α`.
  - Stationarity requires spectral radius of ν < 1 (Eq. 3.10).
- **Data.** EUR/USD and EUR/GBP in milliseconds, Jan 30 – Mar 9, 2012. That is 3,352,809 and 2,178,009 trades. Exponential fits to volume distributions beat Gaussian fits (Fig. 4).
- **Estimation.** MLE (Eq. 5.3).
- **No numerical estimate of α appears in the arXiv v1 text.** I searched the extracted text for α values and parameter tables and found none.

**leeseo2017marked** — Lee & Seo, "Marked Hawkes process modeling of price dynamics and volatility estimation", J. Empirical Finance 40:174–200, 2017. FULL TEXT of arXiv 1907.12025v1.

- **The mark is the price-jump size in ticks (k ∈ Z+), not volume.**
- **Ground intensity, Eqs. (1)–(2).**
  ```
  λ_gi(t) = µ + Σ_{j=1,2} q_ij ∫ g_ij(k_j) φ_ij(t−u) N_j(du × dk_j)
  ```
  - `φ(t) = β e^{−βt}`.
  - Linear impact function (Assumption 1(ii)): `g(k) = (1 + (k−1)η) / E[1 + (k−1)η]`.
  - The paper states that with the exponential kernel (λ_g1, λ_g2) is Markovian.
  - Mark distribution may depend on λ_gi: `f(k_i | λ_gi(t))`.
- **Data.** NYSE tick data for IBM, GE and CVX, 2008–2011, 10:00–15:30, one-second timestamps.
- **Result (Sec. 5).** "η was estimated around 0.2 in general from 2008 to 2011 … large mark tends to have a large impact for the future intensities. On the other hand, η is less than 1 and this implies that the impact of mark size 2 is generally less than the total impact of the two consecutive unit size jumps." A few negative η are also observed.

**jain2024chp** — Jain, Firoozye, Kochems, Treleaven, Finance Research Letters 69:106157, 2024. FULL TEXT of arXiv 2312.08927v5.

- **Model.** A 12-D compound Hawkes LOB simulator.
  - Order sizes are drawn i.i.d. from calibrated empirical distributions, i.e. sizes are marks that **do not** feed back into intensity.
  - Intensity is separable in time of day: `λ^(i)(Q_t, t, s) = f^(i)(Q_t) × λ^(i)(t, s)` (Eq. 7), with thirteen 30-min bins.
  - Inhibitory kernels are allowed and total excitation is floored at zero (`max(0, ·)`).
  - In-spread intensity scales as `(s−1)^β` in the spread s.
- **Data.** NASDAQ Level-2 for AAPL, plus INTC, TSLA and AMZN in App. C.
- **On marks.** "We show some lack of support for the hypothesis that the order arrival intensities are impacted by the past order sizes" (Intro). App. A is titled "Previous order sizes do not impact future order arrival rates". The evidence is a scatter of next-0.01 s event counts against past order size; the authors themselves call it "weak evidence".
- **Other findings.**
  - The in-spread intensity vs spread exponent is 0.7479 (R² = 0.85), from a log-log regression in App. B.
  - The simulator reproduces a concave market-impact function (abstract).
  - For TSLA and AMZN, the spread and volatility are "severely underestimated" (App. C).

**chavezdemoulin2012** — Chavez-Demoulin & McGill, J. Banking & Finance 36(12):3415–3426, 2012. ABSTRACT ONLY (RePEc).

- A "marked point process model for the excesses of the time series over a high threshold that combines Hawkes processes for the exceedances with a generalized Pareto distribution model for the marks (exceedance sizes)". It is used for instantaneous conditional intraday VaR.
- Backtested against a non-parametric peaks-over-threshold extension.
- MLE is "computationally intensive", with differential-evolution starting values.
- **The marks are return exceedances, not order size.** No volume exponent.
- Per bacry2015's table, the data are 1 year of stock data at 1 ms with a 2-exponential kernel. That is secondary.

**embrechts2011mhp** — Embrechts, Liniger, Lin, J. Appl. Probab. 48A:367–378, 2011. ABSTRACT ONLY (Crossref).

- "We derive the statistical estimation (maximum likelihood estimation) and goodness-of-fit (mainly graphical) for multivariate Hawkes processes with possibly dependent marks. As an application, we analyze two data sets from finance."
- Per bacry2015 Sec. 3.1 (secondary):
  - Extreme hourly returns of Dow Jones, Nasdaq and S&P (14 years) as a Hawkes process with 3-D Gamma marks.
  - Also daily data with a 2-D Hawkes and 1-D marks.
- **The marks are return excesses, not volume.**

**alfonsiblanc2016** (see Topic 2) allows volume-dependent excitation `ϕ_s(v/m1)` and `ϕ_c(v/m1)` but does not estimate them in this paper.

### Claim check: "volume-mark exponent γ ≈ 0.3–0.5 validates the square-root law"

**NOT SUPPORTED. No source found.**

- None of the papers read estimates a volume-mark exponent in a Hawkes intensity:
  - rambaldi2017marked bins volume and rejects factorisation.
  - fauthtudor2012 specifies v^α but reports no estimate.
  - leeseo2017marked uses a linear mark in jump size, with η ≈ 0.2, not volume.
  - jain2024chp finds weak evidence of no size effect.
  - chavezdemoulin2012 and embrechts2011mhp use return-exceedance marks.
- Web and Crossref searches for a Hawkes volume-mark exponent tied to the square-root impact law returned nothing.
- The square-root law concerns price impact of metaorders versus size. The mark function concerns excitation of future event intensity. They are different quantities, and no source read links them.

---

## Topic 2. Markovian state for exponential / sum-of-exponential kernels, and stochastic control

**bacry2015** Prop. 2 (Sec. 2.1), FULL TEXT.

- "Consider a Hawkes process with exponential kernels φ^{ij}(t) = α^{ij} β e^{βt} 1_{t∈R+}. Then the couple (N_t, λ_t) is a Markov process."
  - Note: the sign in the exponent is as printed in arXiv v2 and appears to be a typo for e^{−βt}.
- Eq. (8): `dλ_t = −β λ_t dt + αβ dN_t`.
- The result extends to component-dependent β^{ij} and to "a finite sum of exponentials", at the cost of "an extra set of A auxiliary processes {λ̃^(a)_t}_{a=1}^A, suitably chosen so that the resulting (A+1)-uple (N_t, λ̃^(1)_t, …, λ̃^(A)_t) is Markovian".
- "In the non-exponential case, the Hawkes process cannot be generally mapped to a Markovian process."

**alfonsiblanc2016** — Alfonsi & Blanc, "Dynamic optimal execution in a mixed-market-impact Hawkes price model", Finance and Stochastics 20(1):183–218 (online 6 Nov 2015, issue Jan 2016). FULL TEXT of arXiv 1404.0648v2. **Title and venue verified.**

- **Price model.** `P_t = S_t + D_t`, with
  ```
  dS_t = (ν/q) dN_t
  dD_t = −ρ D_t dt + ((1−ν)/q) dN_t
  ```
  This is Obizhaeva–Wang with linear impact, permanent fraction ν and exponential resilience ρ.
  - With the strategic trader: `dD_t = −ρ D_t dt + ((1−ν)dN_t + (1−ε)dX_t)` (Eq. 3).
  - Here `N = N^+ − N^-` is the signed volume of *other traders'* market orders.
- **MIH model (Sec. 2.3).** (N^+, N^-) is a symmetric 2-D marked Hawkes process.
  - Marks are i.i.d. volumes with law µ and `m_k = ∫ v^k µ(dv)`.
  - The kernel is exponential, "so that (N^+, N^-, κ^+, κ^-) is Markovian" (Eq. 9):
    ```
    dκ^+_t = −β(κ^+_t − κ_∞) dt + ϕ_s(dN^+_t/m1) + ϕ_c(dN^-_t/m1)
    dκ^-_t = −β(κ^-_t − κ_∞) dt + ϕ_c(dN^+_t/m1) + ϕ_s(dN^-_t/m1)
    ```
  - `ι_s = ∫ϕ_s(v/m1) µ(dv)` and `ι_c = ∫ϕ_c(v/m1) µ(dv)`.
  - Stationarity holds iff `ι_s + ι_c < β` (Prop. 2.1).
  - The authors note that a completely monotone kernel can be approximated by a multi-exponential one "while preserving a Markovian framework, at the cost of increasing the dimension of the state space", citing Alfonsi–Schied. This is left to future work.
- **Results (Sec. 3).**
  - The optimal execution strategy is explicit (Thm 4.1).
  - With Poisson order flow, Price Manipulation Strategies (PMS) "necessarily appear" and are robust (Prop. 5.2).
  - In the MIHM equilibrium, `ι_s > ι_c`, `ν < 1` and `β = ρ`. "The self-excitation property of the order flow exactly compensates the price resilience." The price is a martingale, and the optimal strategy reduces to Obizhaeva–Wang.
  - If ι_c = 0, the kernel norm `ι_s/β` should equal 1 − ν.
- **Stated limitations (Remark 2.1, Sec. 3).**
  - Linear impact and exponential decay "are not in accordance with empirical facts".
  - The model is not confronted with data in this paper.

**alfonsiblanc2016ext** — Alfonsi & Blanc, "Extension and Calibration of a Hawkes-Based Optimal Execution Model", Market Microstructure and Liquidity 2(2):1650005, 2016. ABSTRACT ONLY (Crossref).

- The Hawkes parameters and propagator are estimated on CAC40 stocks.
- The propagator decays smoothly with one or two time scales, "but only so after a few seconds".
- They derive the optimal strategy "for a multi-exponential Hawkes kernel" and backtest round trips.
- Round trips are "profitable on average when trading at the midprice … However, in most cases, these profits vanish when we take bid–ask costs into account."

**cartea2014buylow** — Cartea, Jaimungal, Ricci, "Buy Low, Sell High: A High Frequency Trading Perspective", SIAM J. Financial Math. 5(1):415–444, 2014. FULL TEXT of the working-paper PDF (smallake.kr mirror of the WBS version).

- **Market-order intensities (Assumption 1, Eq. 2).**
  ```
  dλ^-_t = β(θ − λ^-_t) dt + η dM̄^-_t + ν dM̄^+_t + η̃ dZ^-_t + ν̃ dZ^+_t
  dλ^+_t = β(θ − λ^+_t) dt + η dM̄^+_t + ν dM̄^-_t + η̃ dZ^+_t + ν̃ dZ^-_t
  ```
  - M̄^± are *influential* market orders. Each order is influential with probability ρ, and non-influential orders do not excite.
  - Z^± are Poisson news processes.
  - Constraint for ergodicity: `β > ρ(η + ν)`.
  - The exponential kernel makes (λ^+, λ^-) Markov state variables for the control problem.
- **Short-term alpha (Eq. 4).**
  ```
  dα_t = −ζ α_t dt + σ_α dB_t + ε^+ dM̄^+_t − ε^- dM̄^-_t + e^+ dZ^+_t − e^- dZ^-_t
  ```
  The midprice follows `dS_t = (υ + α_t) dt + σ dW_t`.
- **Data illustration.** IBM market orders, 1 Feb 2008, 15:30–15:33, a running intensity against a fitted intensity with ρ = 1 (Fig. 2).
- **Fill probabilities.** `h^±(δ; κ_t)`, exponential or power-law class.
- **Headline (abstract).** "HF traders who do not include predictors of short-term-alpha in their strategies are driven out of the market because they are adversely selected by better informed traders."
- The full calibration table was not reviewed.

**cartea2018siamrev** — same authors, "Algorithmic Trading, Stochastic Control, and Mutually Exciting Processes", SIAM Review 60(3):673–703, 2018. FULL TEXT of the first pages (Oxford-Man PDF).

- The first page says: "Published: SIAM Review, 60(3), 673-703. SIGEST award winner, by SIAM for the best paper published during 2013-2017 in SIAM Journal on Financial Mathematics."
- The abstract is essentially the 2014 abstract, with the 2014 working paper's "self-exciting" now "multifactor mutually exciting".
- This is a republication of cartea2014buylow, not a new model. I did not diff the bodies.

**jusselin2021mm** — Jusselin, "Optimal Market Making with Persistent Order Flow", SIAM J. Financial Math. 12(3):1150–1200, 2021. ABSTRACT ONLY.

- "Order flows driven by general Hawkes processes", formulated as stochastic control.
- Existence and uniqueness of a viscosity solution to the HJB equation, plus "a fully consistent numerical method".
- UNVERIFIED: how the non-Markovian kernel is handled (state lifting or otherwise).

**abijaber2019multifactor** — Abi Jaber & El Euch, "Multifactor Approximation of Rough Volatility Models", SIAM J. Financial Math. 10(2):309–349, 2019. ABSTRACT ONLY.

- "We design tractable multifactor stochastic volatility models approximating rough volatility models and enjoying a Markovian structure", applied to rough Heston and fractional Riccati equations.
- This is the sum-of-exponentials Markov lift used in the rough setting. It is not a Hawkes order-flow paper.

**Sum-of-exponentials used to approximate a power law (for Markov or recursive likelihood):**
- **rambaldi2015news, Eq. (7).** `φ_PL(t) = (n/Z){Σ_{k=0}^{M−1} a_k^{−p} e^{−t/a_k} − S e^{−t/a_{−1}}}`, with `a_k = τ0 m^k`, M = 15 and m = 5 "as in [5]". "This approximation … allows the log-likelihood to be computed recursively, reducing the computational cost from O(N²) to O(N)." The exponential cutoff only shows "for t larger than ≈ 10^9 s".
- **blanc2017qhawkes, Sec. 4.1.** With exponential φ and k, "the process is Markovian":
  ```
  λ_t = λ_∞ + H_t + Z_t²
  dH_t = β[−H_t dt + n_H dN_t]
  dZ_t = −ω Z_t dt + k_0 dP_t        (Eq. 19)
  ```
  where `k(t) = √(2 n_Z ω) exp(−ωt)` and `φ(t) = n_H β exp(−βt)`. The √ is garbled in the extracted text; this reading is consistent with `n_Z = ||k²||_1`. The paper says the exponential assumption "is only justified for k".
- **Search note.** Searches for "Gao, Zhou, Zhu optimal execution with Hawkes" found no such paper. That item is NOT FOUND.

---

## Topic 3. Compound Hawkes price models

**swishchuk2020gchp** — Swishchuk & Huffman, "General Compound Hawkes Processes in Limit Order Books", Risks 8(1):28, 2020. FULL TEXT of arXiv 1812.02298v1. The arXiv header says it was submitted to *Mathematics in Science and Industry*; the published venue is Risks per Crossref. An earlier version is arXiv 1706.07459.

- **Hawkes (Def. 2.4).** `λ(t) = λ + ∫_0^t µ(t−s) dN(s)`, with `∫µ < 1`.
- **GCHPnSDO (Def. 3.2, Eq. 13).** `S_t = S_0 + Σ_{i=1}^{N(t)} a(X_i)`.
  - X_k is an ergodic n-state Markov chain independent of N.
  - a(·) is bounded.
  - The paper also covers a non-linear version (NLCHPnSDO, Def. 3.1), a 2-state version and CHPDO with X_k ∈ {−δ, δ}.
- **Results.** LLN and FCLTs. The diffusion limit used empirically is Eq. (59):
  ```
  (S_{nt} − N(nt) a*) / √n  →  σ √(λ / (1 − α/β)) W(t)
  ```
  This is for an exponential kernel `µ(t) = α e^{−βt}`.
- **Data.**
  - LOBSTER level-1 data for AAPL, AMZN, GOOG, MSFT and INTC, 21 June 2012. The first and last 15 min are dropped.
  - MLE via particle swarm.
  - Example (Table 2): AAPL λ = 1.4683, α = 1045.2676, β = 2556.1844. The derived ratio α/β ≈ 0.41 is my computation.
- Empirical E[N[0,1]] matches the MLE value, e.g. AAPL 2.4840 vs 2.4841 (Table 3).
- The comparison of empirical and diffusion-limit volatility at 5/10/20-min windows was not transcribed.

**swishchuk2019chp** — Swishchuk, Remillard, Elliott, Chavez-Casillas, "Compound Hawkes processes in limit order books", in *Financial Mathematics, Volatility and Covariance Modelling* (Routledge, 2019), pp. 191–214. FULL TEXT of arXiv 1712.03106v1. The book's editors were not in Crossref: UNVERIFIED.

- Introduces compound Hawkes (CHP) and regime-switching compound Hawkes (RSCHP) processes for the LOB price, with LLN and FCLT.
- The volatility coefficient is `σ √(λ/(1 − µ̂))` (Eq. 43).
- **Data.** CISCO, 3–7 Nov 2014 (dataset of Cartea et al. 2015).
  - Daily α ranges 401–559 and β ranges 718–1132.
  - `λ̂ = λ/(1−α/β)` ≈ 0.050–0.066.
  - The volatility coefficient is 0.0403–0.0477.
  - The standard-deviation comparison error is "approximately 0.08".

---

## Topic 4. News-driven and time-varying-baseline Hawkes

**rambaldi2015news** — Rambaldi, Pennesi, Lillo, "Modeling foreign exchange market activity around macroeconomic news: Hawkes-process approach", Phys. Rev. E 91:012819, 2015. FULL TEXT of arXiv 1405.6047v2. **Verified.**

- **Data.** EBS Live, 1 Jan – 18 Dec 2012 (353 days), EUR/USD, EUR/JPY and USD/JPY.
  - Events are best-quote changes.
  - News comes from dailyfx.com.
  - EBS raised the tick size fivefold on 23 Sep 2012.
- **Model.**
  - Endogenous kernel: double exponential or the quasi-power-law sum of exponentials (Eq. 7).
  - News kernel `φ_N(t) = α_N e^{−β_N t}` (Eq. 12), in `λ(t) = µ + Σ_{t_i<t} φ(t−t_i) + Σ_{z_j<t} φ_N(t−z_j)` (Eq. 9).
  - Non-causal extension: `φ_N(t) = Θ(t) φ^C_N(t) + Θ(−t) φ^{NC}_N(t)` (Eq. 16), since announcement times are known in advance.
- **Results.**
  - **Table III, unconditional model, n.**
    - Before the tick change: 0.87 (EUR/USD), 0.86 (EUR/JPY), 0.84 (USD/JPY).
    - After: 0.90, 0.91, 0.95.
    - "The criticality parameter n is quite close to 1 and relatively insensitive to the tick size change."
  - The power-law kernel beats the double exponential. Residuals are rejected in 92.8% of days for DE versus 42.2% for PL, aggregated.
  - The news-kernel model outperforms the endogenous-only model.
  - "Once the news term is introduced, the estimate of the criticality parameter n is smaller … a Hawkes model which does not consider news-triggered non stationarity could overestimate n."
- **Stated limitation.** The model "treats equally all the news". The suggested fix is a surprise-dependent α_N(S).

**rambaldi2018bursts** — Rambaldi, **Filimonov**, Lillo, "Detection of intensity bursts using Hawkes processes: An application to high-frequency financial data", Phys. Rev. E 97:032318, 2018. Abstract and Sec. 2 read from arXiv 1610.05383v1.

- **Model, Eq. 2.** `λ(t) = µ + Σ_{j=1}^M φ^S_j(t − z_j) + Σ_{t_i<t} φ(t − t_i)`.
  - The burst "fertility" is `f_j = ∫ φ^S_j`, unrestricted.
  - Burst times and their number are unknown, so they are selected by model selection (BIC).
- **Application.** EBS mid-price changes for EURUSD, EURJPY and USDJPY. Abstract: "these bursts are frequent and … only a relatively small fraction is associated to news arrival". It also reports lead–lag in burst occurrence across FX rates.
- Detailed empirical numbers were not transcribed.

**omi2017tdb** — Omi, Hirata, Aihara, Phys. Rev. E 96:012303, 2017. FULL TEXT of arXiv 1702.04443v4.

- **Model.**
  ```
  λ(t|H_t) = µ(t) + Σ g(t − t_i)
  g(s) = Σ_{i=1}^M α_i β_i e^{−β_i s}        (Eq. 2)
  ```
  - log µ(t) is a linear model in many variable-width cubic basis functions, estimated by Bayesian smoothing (the "BCB" model).
  - The branching ratio is Σα_i.
- **Data.** Nikkei 225 mini, regular session, 4 Jan – 30 Jun 2016. Timestamps have 1-second resolution and are jittered uniformly.
- **Results.**
  - "The branching ratio … estimated by our model is 0.41." This is BCB with 2 exponentials (Table II).
  - "The branching ratio was clearly overestimated for the CONST and PL2h models." In the extracted Table II, the constant-baseline row appears to give 0.57–0.83 across M = 1–4. The row assignment is inferred from the PDF layout: UNVERIFIED.
  - Captures the rapid baseline rise after macro announcements.

---

## Topic 5. Fixed income / Treasury / rates

**"Maniatoff & Reitz (2022), Cross-excitation and lead-lag dynamics in fixed income markets using Hawkes processes, J. Empirical Finance 68, 112–135": NOT FOUND, and the citation is contradicted.**

- A Crossref author search for "Maniatoff" returned zero works.
- A Crossref listing of all J. Empirical Finance vol. 68 (2022) articles shows pp. 104–115 is Jiao, "Decision-based trades…", and pp. 116–132 is Celil, Oh, Selvam, "Natural disasters…". No Hawkes or fixed-income paper is in the volume.
- Web search also found nothing.
- **Treat this as fabricated.**

**"Fleming & Mizrach (2013), The structure of the US Treasury market, J. Economic Perspectives 27(4)": NOT FOUND.**

- Crossref's JEP 2013 issue 4 contents (Bernanke; Reis; Gorton–Metrick; …; Taylor) contain no such paper.
- The real paper is **fleming2018brokertec**: Fleming, Mizrach, Nguyen, "The microstructure of a U.S. Treasury ECN: The BrokerTec platform", J. Financial Markets 40:2–22, 2018. SSRN versions date from 2008/2009, the 2008 one by Mizrach & Fleming only.
  - ABSTRACT ONLY, via a RePEc summary. The tool paraphrased it, so this is not verbatim.
  - It uses a VAR of prices and order flow.
  - Trades and limit orders both affect prices. Limit orders have smaller individual impact, but their variability contributes substantially to price variance.
  - Price responsiveness rises after public announcements.
  - **It is not a Hawkes paper.**
  - UNVERIFIED: sample period and exact wording.

**Verified Hawkes work on rates and bond futures:**
- **bacry2013** (existing): Bund and Bobl futures, 2-D mutually exciting price model, Epps effect (`claims_classic.md`, `claims_fm.md`).
- **bacry2016** and **rambaldi2017marked** (existing): Bund futures (Eurex).
- **bacry2015** Sec. 5.1: Bacry–Muzy [7] used EuroStoxx and Euro-Bund futures, "800 trading days from 2009 to 2012".
- **dafonseca2017leadlag** (existing): multi-asset Hawkes lead–lag on Eurex assets (abstract only).
- **munitoke2011** (existing; FULL TEXT of arXiv 1003.3796v2):
  - Data cover 15 four-hour samples, 10–30 Sep 2009, of BNPP.PA, PEUP.PA, LAGA.PA, FEIZ9 and FFIZ9, described as "stocks, index futures, bond futures".
  - It cites "[2] fits a bivariate Hawkes process to the trade time series of … the 'Bund' and the 'Bobl'". [2] is Bacry, "Modeling microstructure noise using point processes", Fiesta seminar, 2010, a talk and not a paper.
- **hainaut2016bivariate** — Hainaut, "A bivariate Hawkes process for interest rate modeling", Economic Modelling 57:180–196, 2016. ABSTRACT ONLY (RePEc; partly paraphrased by the tool).
  - A continuous-time short-rate model driven by a bivariate mutually exciting process for "global supply and demand for fixed income instruments".
  - Affine bond pricing, caplets and floorlets.
  - Fitted to one-year swap rates 2004–2014.
  - **Not high-frequency or LOB.**
- **Searches for Hawkes on US Treasury futures, BrokerTec or Treasury cash–futures lead–lag** (Crossref and web) returned **no Hawkes paper**. Nothing on Bund/Bobl/Schatz three-way cross-excitation was found beyond the Bund/Bobl items above.

---

## Topic 6. Reference checks

- **large2007**: Large, "Measuring the resiliency of an electronic limit order book", **J. Financial Markets 10(1):1–25, Feb 2007**, DOI 10.1016/j.finmar.2006.09.001. Crossref-verified; the existing key is correct. Content is abstract only (see `claims_classic.md`).
- **"Roughening Heston"** → **eleuch2019roughening**: El Euch, Gatheral, Rosenbaum.
  - Risk.net shows it online 15 Apr 2019. Gatheral's Baruch faculty page lists "Risk Magazine, May 2019. 84-89". SSRN 3116887 exists (Crossref 10.2139/ssrn.3116887, 2018).
  - Standfirst: "Rough volatility models are known to fit the volatility surface with very few parameters. The classical Heston model, however, is highly tractable, allowing for fast calibration."
  - Per a web-search summary only, rough Heston values can be approximated by rescaling the classical Heston vol-of-vol: UNVERIFIED.
  - METADATA ONLY otherwise.
- **eleuch2018leverage**: El Euch, Fukasawa, Rosenbaum, "The microstructural foundations of leverage effect and rough volatility", **Finance and Stochastics 22(2):241–280, 2018**. Verified. FULL TEXT of arXiv 1609.05177v1.
  - **Model (Sec. 2.1).** A 2-D Hawkes with `P_t = N^+_t − N^-_t` and intensity `λ_t = µ(1,1)ᵀ + ∫ φ(t−s) dN_s` (Eq. 1).
    - The kernel matrix is `φ = [[ϕ1, βϕ2],[ϕ2, ϕ1+(β−1)ϕ2]]`, as printed after Eq. (1), "with µ > 0 and β ≥ 1". It derives from `ϕ3 = βϕ2`, introduced with "some β > 1".
    - No-arbitrage: `µ+ = µ−` and `ϕ1+ϕ3 = ϕ2+ϕ4`.
    - β > 1 encodes liquidity asymmetry: "the ask side … less liquid than the bid side".
  - **Thm 2.1 (nearly unstable, light tail).** The limit is Heston with `d⟨W,B⟩_t = (1−β)/√(2(1+β²)) dt`. This is negative for β > 1, i.e. leverage.
  - **Thm 3.1 (heavy tail with α ∈ (1/2,1)).** The limit is rough Heston, with Y Hölder of order `α − 1/2 − ε` and the same correlation. The paper notes "estimated values for α are actually close to 1/2, see [8, 34]".
- **"Rambaldi, Filimonov & Sornette 2017 Phys Rev E 95": NOT FOUND as stated.**
  - The real paper is **Rambaldi, Filimonov, Lillo** (not Sornette), Phys. Rev. E **97**:032318, **2018** (rambaldi2018bursts).
  - Crossref search for Rambaldi/Filimonov/Sornette returned no such PRE 95 paper.
- **lu2018**: Lu & Abergel, "**High-dimensional Hawkes processes for limit order books: modelling, empirical analysis and numerical calibration**", **Quantitative Finance 18(2):249–264, 2018**, DOI 10.1080/14697688.2017.1403142. Crossref-verified. Content is still abstract only; no PDF was accessible.
  - Secondary statements, not verified from the primary:
    - jain2024chp says "Lu and Abergel 2018b propose to floor the total intensity of any element of the Hawkes process to zero".
    - A web-search summary describes a 12-D model (LO/MO/Cancel × moves mid or not × bid/ask). UNVERIFIED.

---

## Topic 7. Saturating / link-function nonlinear Hawkes in finance

No finance paper using sigmoid or softplus links *with reported empirical results* was found in this pass beyond the neural Hawkes items already in the bib (`mei2017`, `lalor2025nhp`; see `claims_dl.md`). What was verified:

- **jain2024chp**: ReLU-type floor, `λ = max(0, f(Q_t)(s−1)^β × (µ + Σ∫φ dN))`, with inhibitory non-parametric kernels, on NASDAQ AAPL (see Topic 1).
- **sfendourakis2020** — Sfendourakis & Muni Toke, "LOB Modeling Using Hawkes Processes with a State-Dependent Factor", Market Microstructure and Liquidity 6(01n04):2050014, 2020. FULL TEXT (Secs. 1–2) of arXiv 2107.12872v2.
  - **Exponential (log-linear) link on the LOB state** (Eq. 2):
    ```
    λ_e(t) = λ_{H,e}(t) exp(⟨θ_e, X_t⟩)
    ```
    λ_{H,e} is a standard exponential-kernel Hawkes. X_t is the state (spread or discretised imbalance).
  - Estimated by direct MLE or EM.
  - **Data.** 36 Euronext Paris stocks, year 2015.
  - **Result (abstract).** State-dependent formulations improve goodness-of-fit.
  - The paper cites Lu & Abergel (2018) for the view that non-linear Hawkes "must be developed" to capture inhibition.
- **mucciante2024** — Mucciante & Sancetta, "Estimation of an Order Book Dependent Hawkes Process for Large Datasets", J. Financial Econometrics 22(4):1098–1129, 2024 (online 2023). ABSTRACT ONLY (arXiv 2307.09077v2).
  - "The intensity is the product of a Hawkes process and high dimensional functions of covariates derived from the order book."
  - Four NYSE stocks.
  - "The out of sample testing procedure suggests that capturing the nonlinearity of the order book information adds value to the self exciting nature of high frequency trading events."
- **Bonnet, Martinez Herrera, Sangnier** (arXiv 2103.05299) is MLE for univariate exponential Hawkes with inhibition. It was checked and **excluded**: it uses simulations only, and its stated applications are neuroscience. No key was written.

---

## Topic 8. QHawkes leverage kernel L (blanc2017qhawkes; FULL TEXT of arXiv 1509.07710v1)

Exact wording and locations:

- **Sec. 2.1, after Eq. (2).** "L : R+ → R is a 'leverage' kernel, coupling linearly price changes to market activity … Although it is necessary to account for the leverage effect on daily time scales, we will find later that on intra-day scales, the kernel L is not significant, so for many applications one can focus on the quadratic kernel only."
- **Sec. 2.3.** "Since the leverage kernel is found empirically negligible in the sequel, we leave this positivity condition for future research."
- **Sec. 3.2.3, "Calibration results".** "Also, the intra-day leverage kernel is found to be close to zero, justifying the fact that we mainly consider L ≡ 0 throughout the paper."
- **Fig. 1 caption** (QARCH on five-minute intraday returns of US stocks, maximum lag 18 bins). "Right: leverage kernel. It is hardly distinct from zero and can be considered as pure noise (as opposed to daily models where it is significantly negative)."

The paper reports no numerical value for L. The statement is qualitative and figure-based.

---

## Synthesis (verified facts only)

1. **Marks.**
   - The factorised form `φ(t)χ(ξ)` with i.i.d. marks is the "typical choice" (bacry2015 Sec. 2.2.1).
   - Writing `λ*(t,κ) = λ*_g(t) f*(κ|t)` makes the log-likelihood a ground-process term plus a mark-density term (rasmussen2018notes Prop. 3.1; the split is algebraic).
   - On futures data, Rambaldi–Bacry–Lillo reject the factorised volume model: kernel *shapes* differ across volume bins. Large trades excite more, and for longer.
   - Fauth–Tudor specify `χ(v) ∝ v^α` but report no α. Lee–Seo estimate a linear jump-size mark slope `η ≈ 0.2` (IBM, 2008–2011). Jain et al. report weak evidence that past order size does not affect arrival rates.
   - **No paper found estimates a volume-mark exponent, and no support was found for "γ ≈ 0.3–0.5 validates the square-root law".**
2. **Markovian control.**
   - Exponential kernels make (N, λ) Markov. Sums of A exponentials need A auxiliary states (bacry2015 Prop. 2).
   - Alfonsi–Blanc (FS 2016) solve optimal execution explicitly with a Markovian marked exponential Hawkes. Price manipulation is unavoidable with Poisson flow and excluded when `β = ρ` and the kernel norm equals the transient-impact fraction.
   - Cartea–Jaimungal–Ricci (SIFIN 2014; SIAM Review 2018 SIGEST republication) use exponential self- and mutually-exciting market-order intensities as state variables in HJB market making, with ergodicity condition `β > ρ(η+ν)`.
   - Power-law kernels are handled in practice by sum-of-exponentials approximations (rambaldi2015news Eq. 7 with M = 15). Rough-volatility analogues are in Abi Jaber–El Euch (2019).
3. **Compound Hawkes.** The price is a sum of Markov-chain-driven jumps at Hawkes event times, with LLN and FCLT giving diffusion coefficient `σ√(λ/(1−α/β))`. Calibrations: LOBSTER level-1 data for 5 stocks on 21 Jun 2012 (Swishchuk–Huffman, Risks 2020), and CISCO 3–7 Nov 2014 (Swishchuk et al., 2019 chapter).
4. **News and baseline.**
   - Adding a pre-scheduled news kernel lowers the estimated branching ratio n (EBS FX 2012, n ≈ 0.84–0.95 without news).
   - A flexible time-varying µ(t) lowers it further: Nikkei 225 mini branching ratio 0.41.
   - Burst detection with unknown burst times: Rambaldi–Filimonov–Lillo, PRE 2018.
5. **Fixed income.**
   - Verified Hawkes LOB or high-frequency work on rates is limited to Eurex Bund/Bobl futures (bacry2013, bacry2016, rambaldi2017marked, Bacry–Muzy 2014 on Euro-Bund) and the Da Fonseca–Zaatour lead–lag paper.
   - **No Hawkes study of US Treasury futures, BrokerTec or Treasury cash–futures lead–lag was found.** The verified BrokerTec microstructure paper is Fleming–Mizrach–Nguyen (JFM 2018), a VAR study.
6. **Reference checks.**
   - large2007 is JFM 10(1):1–25.
   - "Roughening Heston" is El Euch–Gatheral–Rosenbaum, Risk, May 2019 (pp. 84–89 per Gatheral's page).
   - El Euch–Fukasawa–Rosenbaum is FS 22(2):241–280, 2018 (Heston, or rough Heston, with leverage from bid/ask asymmetry β > 1).
   - The real "Rambaldi–Filimonov" paper is with Lillo, PRE 97, 2018.
   - lu2018's title and venue are confirmed.
7. **Nonlinear links.**
   - In finance, the verified forms are ReLU floors (jain2024chp; lu2018 per secondary citation) and an exponential link on LOB state multiplying a linear Hawkes (sfendourakis2020; mucciante2024).
   - No sigmoid or softplus finance application with results was verified in this pass.
8. **QHawkes leverage kernel.** "Hardly distinct from zero and can be considered as pure noise" intraday (Fig. 1). "Significantly negative" in daily models. L ≡ 0 is used throughout.

## UNVERIFIED / NOT FOUND

- **NOT FOUND (likely fabricated):**
  - Maniatoff & Reitz (2022), J. Empirical Finance 68:112–135. The volume's contents contradict it.
  - Fleming & Mizrach (2013), JEP 27(4). Not in that issue. The real related paper is Fleming–Mizrach–Nguyen, JFM 40 (2018).
  - "Rambaldi, Filimonov & Sornette 2017, PRE 95". The real paper is Rambaldi–Filimonov–Lillo, PRE 97 (2018).
- **NOT FOUND:**
  - Any paper estimating a Hawkes volume-mark exponent, or tying one to the square-root law.
  - "Gao, Zhou, Zhu — optimal execution with Hawkes".
  - Any Hawkes study of US Treasury, BrokerTec, or Treasury cash–futures data.
- **UNVERIFIED:**
  - Daley & Vere-Jones section and proposition numbers for the marked factorisation and likelihood.
  - Fauth–Tudor journal publication and any estimated α.
  - Embrechts et al. and Chavez-Demoulin–McGill beyond their abstracts (data details are from bacry2015).
  - Jusselin (2021) treatment of non-Markovian kernels.
  - Alfonsi–Blanc 2016 extension: anything beyond the abstract.
  - Fleming–Mizrach–Nguyen: sample period and verbatim abstract.
  - Hainaut: verbatim abstract beyond its first two sentences.
  - "Roughening Heston": content beyond the standfirst, and the page range (single source).
  - lu2018: model details (dimension, nonlinearity, data).
  - Omi et al. Table II: row labels.
  - Swishchuk et al. 2019 chapter: book editors.
  - Cartea et al.: calibration values beyond Fig. 2.
  - Mucciante–Sancetta: anything beyond the abstract.

---

## Added 2026-10-03: full-text reads for §7.10 and §7.15

**swishchuk2020gchp** — FULL TEXT re-read (arXiv 1812.02298v1) for the empirical section.
- Def. 3.2: S_t = S_0 + Σ_{k=1}^{N(t)} a(X_k), X_k an ergodic Markov chain independent of N; Def. 3.4 (CHPDO): X_k ∈ {−δ, δ}.
- Thm 4.3 (Eq. 34): (S_nt − N(nt) â*)/√n → σ̂* √(λ/(1−µ̂)) W(t), µ̂ = ∫µ; â* = Σ π_i* a(i); σ̂*² = Σ π_i* v(i) (Eq. 36).
- Cor. 3 (Eq. 55–57), two-state ±δ with transition probs (p, p'): σ² = 4δ²[(1−p'+π*(p'−p))/(p+p'−2)² − π*(1−π*)]. For p = p' (symmetric, π* = 1/2) this reduces to σ² = δ² p/(1−p) (our algebra; checked).
- Data: LOBSTER level-1, AAPL/AMZN/GOOG/MSFT/INTC, 21 June 2012, first/last 15 min dropped. δ = half a cent (mid).
- Table 2 (λ, α, β): AAPL 1.4683/1045.27/2556.18; AMZN 0.6443/653.75/1556.17; GOOG 0.4985/865.86/1980.44; MSFT 0.0659/479.35/908.00; INTC 0.0471/399.64/760.50. Our computation α/β: 0.41, 0.42, 0.44, 0.53, 0.53.
- Table 4 (p_dd, p_uu): AAPL .4956/.4933; AMZN .4635/.4576; GOOG .4769/.4461; MSFT .6269/.5827; INTC .6106/.5588.
- Sec. 5.1: one-size model tracks MSFT, INTC; "severely underestimate the variability" for AAPL, AMZN, GOOG. Sec. 5.2: 61%, 53%, 71% of mid changes larger than half a tick for AAPL, AMZN, GOOG; MSFT and INTC all at half tick. Two-size chain (Table 5) improves AAPL and GOOG, not AMZN.
- Sec. 5.3: AMZN 12-state chain MSE 0.0208 → 0.0125; 24 states 0.0123.
- Table 9 (theoretical vs regression coefficient, % error): AAPL 1.42%, AMZN 20.8%, GOOG 4.63%, INTC 3.4%, MSFT 6.4%.
- All fits and checks on the same day (in-sample).

**morariupatrichi2022** — FULL TEXT (arXiv 1809.08060v3, 15 Sep 2021); published Quantitative Finance 22(3), 2022.
- Def. 2.1: λ_e(t) = ν_e + Σ_{e'} ∫ k_{e'e}(t−s, X(s)) dN_{e'}(s) (Eq. 2.1); X piecewise constant, jumps only at events, P(X(T_n)=x | E_n, past) = φ_{E_n}(X(T_n−), x) (Eq. 2.2). d_x = 1 → ordinary linear Hawkes.
- Likelihood separable: transition probabilities estimated independently of the point-process parameters (Sec. 1, Sec. 3). Simulation: Ogata thinning with extra step drawing X_n from φ (Alg. 2.5). Python library `mpoints`.
- Eq. 2.4 alternative: base rates and kernels tracking current state X(t−).
- Data: INTC Nasdaq level-1 (LOBSTER), 1 Jun 2017 – 31 May 2018, 250 days, 12:00–14:30 for estimation. AMD, MU, SNAP, TWTR (Jan–Apr 2018) studied, not reported ("largely representative").
- Event types: bid = buy MOs + level-I buy limit + level-I sell cancels; ask = mirror. N_bid − N_ask proxy of OFI. MOs < 5% of level-I activity.
- ModelS: spread {1, 2+}, 26 params; ModelQI: QI five bins [−1,−0.6), [−0.6,−0.2), [−0.2,0.2), [0.2,0.6), [0.6,1], 92 params. Exponential kernels; daily MLE.
- Findings: self > cross excitation in all states; timescales 0.1–100 ms. Spread 2+: self-excitation magnitude doubles, cross-excitation timescale lengthens drastically. Spread transitions (Fig 4a): 1→1 98%, 1→2+ 1%, 2+→1 6%, 2+→2+ 93%. QI transitions mirror-symmetric between bid and ask; more likely towards neutral. QI: ask self-excitation increases in sell++; in buy+ kernel norm nearly halves. Structural break ~5 Feb 2018 ("return of volatility", VIX +116% to 38).
- Endogeneity (Sec. 4.8): spectral radius ρ(x) higher in disequilibrium states; ρ(2+) systematically above 1; ρ(sell++), ρ(buy++) above 0.9 half the time, exceeding 1 occasionally.
- Fit (Sec. 4.7): Q–Q residuals vs ordinary Hawkes in sample and out of sample (14:30–15:00); improvement extends out of sample but smaller; "meagre" in unconditional Q–Q because models similar in likely states.
- Sec. 4.9: alternative = ordinary Hawkes on d_e·d_x types (210 params) fitted worse in sample than ModelQI (92) on May 2018 (shown day 11 May 2018).
- Extensions (Sec. 5): power-law / sum-of-exponential kernels; state-dependent base rates (nests CTMC and Hawkes); negative kernels with non-linear link.
- NOTE: the survey previously attributed λ̃ = λ·exp(θᵀX) to this paper; that form does not appear in it. Removed 2026-10-03.
