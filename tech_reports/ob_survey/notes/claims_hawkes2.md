# Hawkes and queue-reactive references: verified content notes (set 2)

Compiled 2026-10-02. Sources: Crossref DOI records, the arXiv API and PDFs (text extracted with pdftotext), JMLR and NeurIPS pages, and publisher abstracts where these could be reached.

Unless stated otherwise, quotes and equation numbers come from the **arXiv/preprint version named in each entry**. Numbering and wording in the published version may differ.

BibTeX for the new keys is in `bib/refs_hawkes2.bib`. Existing keys are reused here and not redefined: `huang2015`, `bacry2015`, `daley2003`, `ozaki1979`, `hardimanbercotbouchaud2013`, `filimonovsornette2012`.

Notation: everything below is transcribed in each paper's own notation. ASCII renderings stand in for the typeset formulas.

---

## huang2015 — Huang, Lehalle, Rosenbaum, "Simulating and analyzing order book data: the queue-reactive model", JASA 110(509):107–122, 2015 (arXiv 1312.0563v2, 3 Sep 2014)

**State (Sec. 2.1).**
- The LOB is a 2K-dimensional vector centred on a reference price `p_ref`.
- The bid side is `[Q_{-i}: i=1..K]` and the ask side is `[Q_i: i=1..K]`. `Q_{±i}` is the limit at distance `i − 0.5` ticks from `p_ref`, and `q_i` is the number of orders at `Q_i`.
- Queue sizes are measured in units of the average event size at that limit, `AES_i`. The paper says AES is a better choice than average trade size (fn. 5). The queue size is "approximated by the smallest integer that is larger than or equal to the volume available at the queue, divided by ... AES_i" (Sec. 2.3.2).
- `X(t) = (q_{-K}(t),...,q_{-1}(t),q_1(t),...,q_K(t))` is a continuous-time Markov jump process on `Ω = N^{2K}` with unit jumps.
- Generator: `Q_{q,q+e_i} = f_i(q)`, `Q_{q,q−e_i} = g_i(q)`, `Q_{q,q} = −Σ_{p≠q} Q_{q,p}`, and 0 otherwise.
- Ergodicity (Thm 2.1) holds under two assumptions:
  - Assumption 1 (negative individual drift): there exist `C_bound` and `δ>0` such that if `q_i > C_bound` then `f_i(q) − g_i(q) < −δ`.
  - Assumption 2 (bounded inflow): `Σ_i f_i(q) ≤ H`.

**Reference price (Sec. 2.2.2).**
- `p_ref = (p_1 + p_{-1})/2`.
- If the spread is odd (in ticks), `p_ref = p_mid`.
- If the spread is even, `p_ref = p_mid ± tick/2`, choosing whichever is closer to the previous `p_ref`.

**Model I: independent queues (Sec. 2.3).**
- Intensities depend only on the target queue size: `λ^L_i(n)`, `λ^C_i(n)`, `λ^M_i(n)` when `q_i = n`.
- Bid/ask symmetry is assumed: `λ^·_i = λ^·_{-i}`.
- `f_i(q) = λ^L_i(q_i)` and `g_i(q) = λ^C_i(q_i) + λ^M_i(q_i)`.
- Each queue is a birth–death process. Market orders sent to `Q_i` consume `Q_i` directly.
- The paper uses `K = 3`. Its reason: the dynamics and distributions at `Q_{±4}` and `Q_{±5}` are "quite similar" to those at `Q_{±3}`.

**Estimator (Sec. 2.3.2), described as "the maximum likelihood method".**
- For each event ω at queue `Q_i`, record:
  - `Δt_i(ω)`, the waiting time in seconds since the previous event at `Q_i`;
  - the type `T_i(ω)`: `E^+` for a limit insertion, `E^−` for a cancellation, `E^t` for a market order;
  - `q_i(ω)`, the queue size *before* the event.
- Recording restarts whenever `p_ref` changes.
- The estimator is:
  ```
  Λ̂_i(n)    = [ mean( Δt_i(ω) | q_i(ω) = n ) ]^{-1}
  λ̂^L_i(n)  = Λ̂_i(n) · #{T_i(ω) ∈ E^+, q_i(ω)=n} / #{q_i(ω)=n}
  λ̂^C_i(n)  = Λ̂_i(n) · #{T_i(ω) ∈ E^−, q_i(ω)=n} / #{q_i(ω)=n}
  λ̂^M_i(n)  = Λ̂_i(n) · #{T_i(ω) ∈ E^t, q_i(ω)=n} / #{q_i(ω)=n}
  ```
- "Mean" is the empirical mean and `#A` is the cardinality of A.
- Algebraically, `λ̂^L_i(n) = #{type-L events with q=n} / Σ_{ω: q_i(ω)=n} Δt_i(ω)`. That is, the event count in state n divided by the total time spent in state n. This rewriting is our own algebra and is not stated in the paper.
- Data at `Q_i` and `Q_{-i}` are pooled. Confidence intervals come from CLT approximations (appendix).

**Empirical shapes (Fig. 2, France Telecom).**
- Insertion at `Q_{±1}` is roughly constant in queue size, with a significantly smaller value at 0. At `Q_{±2}` and `Q_{±3}` it decreases with queue size.
- Cancellation at `Q_{±1}` is "increasing concave ... between 0 and 25" and then flat or slightly decreasing. The paper contrasts this with the linear cancellation rate of Cont–Stoikov–Talreja.
- Market orders at `Q_{±1}`: "The rate decreases exponentially with the available volume."

**Model I invariant law (Sec. 2.3.3).**
- `ρ_i(n) = λ^L_i(n) / (λ^C_i(n+1) + λ^M_i(n+1))`.
- `π_i(n) = π_i(0) Π_{j=1}^{n} ρ_i(j−1)`, with `π_i(0) = (1 + Σ_{n≥1} Π_{j=1}^{n} ρ_i(j−1))^{-1}`.
- Empirical queue distributions are sampled every 30 s. The model's invariant distribution "approximate[s] very well" the empirical one, whereas a Poisson model with linear cancellation fits worse (Fig. 3).

**Model II: dependent case (Sec. 2.4).**
- Market orders hit the best non-empty queue:
  - `f_i(q) = λ^L_i(q)`;
  - `g_i(q) = λ^C_i(q) + λ^M_buy(q)·1_{bestask(q)=i}` for i>0, and the analogue with `λ^M_sell` and bestbid for i<0.
- **Model IIa** (Sec. 2.4.1): at `Q_{±2}`, `λ^L` and `λ^C` are functions of `q_{±2}` and `1_{q_{±1}>0}`. Market orders go only to `Q_{±1}` and `Q_{±2}`. The market order rate is a function of `q_{±1}` if `q_{±1}>0`, and of `q_{±2}` otherwise. `(q_1,q_2)` is a quasi-birth-and-death process with a matrix-geometric invariant law.
- **Model IIb** (Sec. 2.4.4): bid–ask dependence. Rates at `Q_{±1}` are functions of `q_{±1}` and `S_{m,l}(q_{∓1})`.
  - `S_{m,l}` buckets the opposite queue into empty, small `(0,m]`, usual `(m,l]` and large `>l`.
  - m and l are the 33% lower and upper quantiles of `q_{±1}` given positive. Fig. 6 uses m=4 and l=9 AES_1.
- Model IIb findings:
  - insertion decreases with the opposite queue size and is much larger when the opposite queue is empty;
  - cancellation decreases with opposite-side liquidity;
  - market orders increase with opposite-side liquidity (with a special case when the opposite queue is empty).

**Model III: the queue-reactive model (Sec. 3.1).**
- When `p_mid` moves up or down and `q_{±1}=0` at that moment, `p_ref` moves by δ (one tick) **with probability θ**.
- The triggers are:
  - insertion inside the spread while the opposite best queue is empty;
  - cancellation of the last order at a best queue;
  - a market order consuming the last order at a best queue.
- On a `p_ref` shift, the queues relabel to their neighbours (renormalised by the AES ratio). The new `q_{±3}` is drawn from its invariant law.
- **With probability θ_reinit**, the whole LOB state is redrawn from its invariant distribution around the new `p_ref`. The paper reads this as "the percentage of price changes due to exogenous information". Cont–de Larrard corresponds to `θ_reinit = 1`.
- The state is `X̃(t) = (X(t), p_ref(t))` on `N^{2K} × δN`. Model I is used within constant-`p_ref` periods; per the paper, IIa and IIb give very similar results.
- θ and θ_reinit are calibrated to the 10-min volatility of `p_mid` and to the mean-reversion ratio `η = N_c/(2N_a)` (continuations over alternations, from Robert–Rosenbaum 2011).
- Calibrated values for France Telecom (Sec. 3.2.1): **θ = 0.7, θ_reinit = 0.85**.

**Maximal mechanical volatility (Sec. 3.1.2).**
- With `θ_reinit = 0` and `θ = 1`, the maximum attainable volatility is **5 bps against 14 bps empirical** for France Telecom.
- In that setting **η = 0.08, against 0.39 empirical**. The paper attributes the gap to strong mean reversion from the "often reversed bid-ask imbalance immediately after a change of p_ref".

**Data (Sec. 2.2.1, Table 1).**
- Cheuvreux LOB database, Euronext Paris, January 2010 to March 2012, five best limits.
- The first and last trading hours are removed.
- Stocks:
  - France Telecom: 159,250 orders/day, 7,282 trades/day, average spread 1.43 ticks. This is the main example.
  - Alcatel-Lucent: 129,400 orders/day, 8,626 trades/day, average spread 1.99 ticks. Results are in the appendix.
- The framework targets large-tick assets, which fn. 1 defines as average spread < 2.5 ticks.

**Applications.**
- Sec. 2.5 computes execution probability under Assumptions 3–4. The cancellation rate seen by the tagged order is `λ^C_1(q_{-1})·(q_{-1}−n_0)/q_{-1}`. Models I–III give similar values; the Poisson model overestimates.
- Sec. 3.2 analyses order placement tactics in simulation: T1 "fire and forget" and T2 "pegging", with schedules S1 linear and S2 exponential, `n_total = 60 AES_1`, M = 20 slices of 10 min.

---

## blanc2017qhawkes — Blanc, Donier, Bouchaud, QF 17(2):171–188, 2017 (arXiv 1509.07710v1)

**General QHawkes intensity (Eq. 2, Sec. 2.1):**
```
λ_t = λ_∞ + (1/ψ) ∫_{-∞}^{t} L(t−s) dP_s + (1/ψ²) ∫_{-∞}^{t}∫_{-∞}^{t} K(t−s, t−u) dP_s dP_u
```
- P is a pure-jump price with i.i.d. centred jumps ξ, where `E ξ² = ψ²` (e.g. ξ = ±ψ, with ψ the tick).
- L is the "leverage" kernel and K is the symmetric, positive quadratic feedback kernel.
- The intraday leverage kernel is found ≈ 0, so the paper mostly uses `λ_t = λ_∞ + ψ^{-2}∬K dP dP` (Eq. 3).
- Ordinary Hawkes is the special case `K(t,s) = φ(t)δ_{t−s}`.

**ZHawkes (Sec. 2.2, Eq. 4):**
- `K(t,s) = φ(t)δ_{t−s} + k(t)k(s)`, which gives `λ_t = λ_∞ + H_t + Z_t²`.
- `H_t = ∫φ(t−s)dN_s` and `Z_t = ψ^{-1}∫k(t−s)dP_s`, a moving average of past signed returns.
- Price moves in the same direction raise future activity more than compensated moves.

**Stationarity (Sec. 2.4, Eqs. 6–7).**
- The necessary condition is `λ_∞ > 0` and `Tr(K) < 1`, or `λ_∞ = 0` and `Tr(K) = 1`. Then `λ̄ = λ_∞/(1−Tr K)`.
- For ZHawkes, `Tr K = n_H + n_Z`, where `n_H = ||φ||_1` is the "Hawkes norm" and `n_Z = ||k²||_1` the "Zumbach norm".

**Zumbach effect (Sec. 1).**
- "Past large scale realized volatilities are more correlated with future small scale realized volatilities than vice-versa."
- In the formulation the paper attributes to [14] (Chicheportiche–Bouchaud), `⟨r_t² σ²_{t+τ}⟩ > ⟨r²_{t+τ} σ_t²⟩`, with r a daily return and σ a 5-min-based volatility.
- The effect is invariant under r → −r and therefore distinct from leverage.
- QHawkes/ZHawkes is time-reversal asymmetric. Per the paper, continuous-time stochastic-volatility models (Heston, MRW, fractional CIR) are time-reversal symmetric by construction.

**Data and calibration (Sec. 3).**
- 133 NYSE stocks traded without interruption from 1 Jan 2000 to 31 Dec 2009, giving 2,499 days of intraday **5-minute bins**.
- A QARCH model is calibrated (GMM, then MLE with Student-t residuals). The result is a clear off-diagonal structure, unlike the daily calibration.
- Fit: `K(τ,τ') ≈ φ(τ)δ_{ττ'} + k(τ)k(τ')` with `φ(τ) = g τ^{−α}` and `k(τ) = k_0 e^{−ωτ}`, where `g=0.09`, `α=0.60`, `k_0=0.14`, `ω=0.15`. ω = 0.15 means roughly 30 min decay.
- With a longer lag (60 bins): `g'=0.09`, `α'=0.76`.
- Residual Student-t has `ν≈7.9`.

**Tails (Sec. 4.3).**
- With `n_H=0`, the activity tail gives a cumulative return-tail exponent `ν = 1 + 1/n_Z`.
- The calibrated `n_Z ≈ 0.06` gives ν ≈ 18, which is too thin.
- With Hawkes, `ν = 1 + (1−n_H)/n_Z ≈ 4` for `n_Z=0.06` and `n_H≈0.8`.
- Simulations use `n_H=0.8`, `n_Z=0.1`, `Tr K=0.9`.
- The abstract says long memory can arise without criticality.

---

## zumbach2009 — Zumbach, QF 9(5):505–515, 2009 (arXiv 0708.4022v1, dated Jan 2007)

**Volatility definitions (Sec. 2).**
- Historical volatility uses only the past: `σ_h²[δt_σ, δt_r](t) = (1year/δt_r)(1/n) Σ_{t−δt_σ+δt_r ≤ t' ≤ t} r²[δt_r](t')`.
- Realized volatility uses only the future: `σ_r[δt_σ,δt_r](t) = σ_h[δt_σ,δt_r](t+δt_σ)`.

**Three time-reversal-invariance statistics, each zero under TRI (Sec. 3).**
1. `a_p(Δσ) = p(Δσ) − p(−Δσ)`, where `Δσ = σ_r − σ_h`.
2. `a_σ(δt_σ, δt'_σ) = ρ_σ(δt_σ,δt'_σ) − ρ_σ(δt'_σ,δt_σ)`, where `ρ_σ` is the correlation of past historical volatility at horizon `δt_σ` with future realized volatility at `δt'_σ`. Its maximum is 6–12%, at `δt_σ ≈ 1 week` and `δt'_σ ≈ 6 h`.
3. Granularity asymmetry `a_gr(δt_r, δt'_r) = ρ_gr(δt_r,δt'_r) − ρ_gr(δt'_r,δt_r)`, where `ρ_gr = ρ(σ_h[δt_σ,δt_r], σ_r[δt_σ,δt'_r])`. It is 12–18% at `δt_σ ≈ 2 days`.

Statistic 3 (coarse-grained past volatility predicting fine-grained future volatility better than the reverse) is the form later called the "Zumbach effect" (see blanc2017qhawkes).

**Data.** High-frequency FX (CHF/USD, DKK/USD, JPY/USD, USD/GBP, XAU/USD), sampled every 3 min in business time, starting 1 Jan 1990.

**Findings (abstract).**
- Empirical FX is not TRI.
- Only multi-timescale ARCH processes reproduce all three measures. GARCH(1,1) gives some asymmetry, but none on the third measure.
- "All the stochastic volatility type processes are time reversal invariant."

---

## hardiman2014branching — Hardiman & Bouchaud, Phys. Rev. E 90:062807, 2014 (arXiv 1403.5227v3)

**Model.**
- `λ(t) = µ + ∫_{-∞}^{t} φ(t−s)dN(s)` (Eq. 1), with `Λ = µ/(1−n)` and `n = ∫φ`.

**Derivation (Sec. III).**
- `ν̂(ω) = Λ / |1 − φ̂(ω)|²` (Eq. 3). Setting ω=0 gives `∫ν = Λ/(1−n)²` (Eq. 4).
- Count variance in a window W: `σ_W² = ∫_{−W}^{W} ν(τ)(W−|τ|)dτ ≤ W∫ν` (Eq. 5).

**Assumptions.**
1. `ν(t) → 0` for `|t| > R`.
2. `W ≫ R`.

Under these, `σ_W² ≈ W∫ν` (Eq. 6), and the estimator is (**Eq. 7**):
```
n ≈ 1 − sqrt( µ_W / σ_W² ) := ñ,     µ_W = ΛW
```

**Sample version (Eqs. 8–9).**
- Use m non-overlapping windows over `T = mW`: `µ̃_W = (W/T)N_T`, and `σ̃_W² = (1/(m−1))Σ(N_W(i)−µ̃_W)²`.

**Properties.**
- Biased low for finite W, and exact only as W → ∞.
- The mean of ñ over realisations is −∞ for any finite m, because `σ̃² = 0` has positive probability. The authors therefore report the **median**.
- Simulations: exponential kernel with β=1, `µ=1−n`, `T=10^5`, W=20, 100 runs (Fig. 1).
- Power-law near-critical case (`n=0.99`, ε=0.35): `1−ñ ~ W^{−0.35}`.

**Empirical (Sec. V).**
- E-mini S&P mid-price changes, including a re-run of the Filimonov–Sornette flash-crash analysis: 10-min periods, with 60 windows of W=10 s.
- With fixed W, ñ rises over time, as Filimonov–Sornette found.
- If W halves every 18 months, the estimate is roughly constant. The paper reads this as consistent with criticality (n=1) as argued in `hardimanbercotbouchaud2013`.

---

## achab2018cumulants — Achab, Bacry, Gaïffas, Mastromatteo, Muzy, JMLR 18(192):1–28, 2018 (method name: NPHC)

**Estimand.**
- The matrix of integrated kernels `G = [g^{ij}]`, where `g^{ij} = ∫φ^{ij}`. No kernel-shape model is needed.
- `g^{ij}` is the average number of type-i events whose direct ancestor is a given type-j event. `E[dN^{i←j}_t] = g^{ij}Λ^j dt` (Eq. 2).
- `φ^{ij} ≡ 0` is equivalent to Granger non-causality (Def. 2).
- Stability requires `||G|| < 1` (spectral norm).

**Integrated cumulants (Eqs. 3–5) and their relation to `R = (I−G)^{-1}` (Eqs. 6–9):**
```
Λ^i    = Σ_m R^{im} µ^m
C^{ij} = Σ_m Λ^m R^{im} R^{jm}
K^{ijk}= Σ_m ( R^{im}R^{jm}C^{km} + R^{im}C^{jm}R^{km} + C^{im}R^{jm}R^{km} − 2Λ^m R^{im}R^{jm}R^{km} )
```
- The case d=1 with `M={C^{11}}` is Hardiman–Bouchaud.
- For d>1, C alone is invariant under `R → OR` with O orthogonal, so the system is under-determined. Third-order cumulants fix G.

**Estimator (Eq. 10).**
- `R̂ ∈ argmin_R (1−κ)||K^c(R) − K̂^c||²_2 + κ||C(R) − Ĉ||²_2`, where `K^c = {K^{iij}}` (d² components) and κ balances the two terms by the empirical norms.
- The loss is called "a peculiar Generalized Method of Moments".
- "The objective to minimize ... is **non-convex**." It is optimised with **AdaGrad**, initialised from `C^{1/2} O L^{-1/2}`.
- Then `Ĝ = I − R̂^{-1}`.
- Complexity is `O(nd² + N_iter d³)`. Consistency is Theorem 3.

**Data.** Simulated (rectangular, exponential and power-law kernels), MemeTracker (100/200 sites), and the 8-dimensional order book model of Bacry et al. 2016 on DAX futures, 01/01/2014–03/01/2014 (QuantHouse).

---

## veen2008em — Veen & Schoenberg, JASA 103(482):614–624, 2008 (author preprint, stat.ucla.edu/~frederic/papers/em.pdf)

**Approach.**
- Estimating branching (ETAS) models is treated as an incomplete-data problem.
- A latent `u_i` identifies the parent: `u_i = 0` if event i is background, and `u_i = j` if it was triggered by event j.
- The E step computes `prob(u_i = j)` and `prob(u_i = 0)` from the current parameters. The M step maximises the expected complete-data log-likelihood, which separates into background and triggering parts.

**Motivation (abstract).** Direct numerical MLE "can be unstable and computationally intensive", partly because the incomplete-data log-likelihood is flat (Figs. 2–3). The EM version is "extremely robust and accurate".

**Application.** Space–time ETAS on seismicity.

The explicit temporal-exponential form of these EM steps is given in lewis2011em, Eqs. 6–10 (below).

---

## lewis2011em — Lewis & Mohler, "A nonparametric EM algorithm for multiscale Hawkes processes", preprint dated May 9, 2011

**Venue: UNVERIFIED.**
- No Crossref record was found.
- tick (JMLR 2018) cites it as "preprint, pages 1–16, 2011". Bacry et al. 2015 cite "preprint, 2011". Bacry–Muzy cite "Preprint, 2010".
- Achab et al. 2018 cite "Journal of Nonparametric Statistics, 2011", but no such record was found.

**Parametric EM (Sec. 2).**
- Model: `λ(t) = µ + Σ αω e^{−ω(t−t_j)}`.
- E step: `p_ij = αωe^{−ω(t_i−t_j)}/λ(t_i)` and `p_ii = µ/λ(t_i)`.
- M step: `µ = Σp_ii/T`, `α = Σ_{i>j}p_ij/n`, `ω = Σ_{i>j}p_ij / Σ_{i>j}(t_i−t_j)p_ij` (Eqs. 6–10).
- The paper shows this is a projected gradient step, e.g. `µ^{k+1}−µ^k = (µ^k/T)∂l/∂µ`.
- Convergence depends on how well the time scales of µ and g are separated.

**Method: MPLE (Sec. 3).**
- Objective: `L = Σ log λ(t_i) − ∫_0^T λ − α_1 R(µ) − α_2 R(g)` (Eq. 22), where R is a roughness penalty. Good's penalty `||(µ^{1/2})'||²` is used for a time-varying µ.
- It is solved inside EM iterations. Without penalty, the EM reduces to MISD (Marsan–Lengliné), which is a histogram estimator.
- Application: Iraq violent civilian deaths 2003–2007.

---

## ogata1981 — Ogata, IEEE Trans. Inf. Theory 27(1):23–31, 1981

**Abstract (via Semantic Scholar).** "A simple and efficient method of simulation is discussed for point processes that are specified by their conditional intensities. The method is based on the thinning algorithm which was introduced recently by Lewis and Shedler for the simulation of nonhomogeneous Poisson processes. Algorithms are given for past dependent point processes containing multivariate processes."

**Algorithm.** As summarised by Bacry et al. 2015 (App. B), for decreasing kernels:
1. Draw a candidate `Δt ~ Exp(λ^{tot}_t)`, where `λ^{tot}_t = Σ_k λ^k_t`.
2. Draw `U ~ Unif[0, λ^{tot}_t]`.
3. Reject if `U < λ^{tot}_t − λ^{tot}_{t+Δt}`.
4. Otherwise assign the event to a component by where U falls.

The primary paper text was not read; the details above come from the abstract and the secondary summary.

---

## ogata1988 — Ogata, JASA 83(401):9–27, 1988

The primary text and abstract were not accessible (T&F returned 403).

From secondary sources:
- **Residual analysis.** Transform event times by the fitted compensator, `τ_i = ∫_0^{t_i} λ̂(s)ds`. If the model is correct, `{τ_i}` is a unit-rate Poisson process, so standard Poisson diagnostics apply on the transformed scale. Sources: a WebSearch snippet, and R. Peng's paraphrase, which credits the underlying result to Papangelou.
- The temporal ETAS model, as given by Veen–Schoenberg, Sec. 2:
  ```
  λ(t|H_t) = µ + Σ_{t_i<t} g(t−t_i, m_i)
  g(τ, m) = K_0 e^{a(m−M_0)} / (τ+c)^{1+ω}
  ```
  The temporal part is the modified Omori–Utsu law.

The exact equation numbers and sections in Ogata 1988 are UNVERIFIED.

---

## bacry2018tick — Bacry, Bompaire, **Deegan**, Gaïffas, Poulsen, JMLR 18(214):1–5, 2018

**Title and authors.**
- The JMLR title is "tick: a Python Library for Statistical Learning, with an emphasis on Hawkes Processes and Time-Dependent Models".
- arXiv 1707.03003 has a different title ("...with a particular emphasis on time-dependent modelling") and lists 4 authors, without Deegan. **The published version has 5 authors.**

**Content.**
- A Python 3 library with a C++ core.
- Modules: `tick.hawkes`, `tick.linear_model`, `tick.robust`, `tick.survival`, plus `prox` and `solver`.
- Hawkes estimators (Table 2):
  - non-parametric: EM (Lewis & Mohler 2011), basis kernels (Zhou et al. 2013), Wiener–Hopf (Bacry & Muzy), NPHC (Achab et al.);
  - parametric: single exponential, sum of exponentials, sum of Gaussians, ADM4.

---

## bacrymuzy2016wh — Bacry & Muzy, IEEE Trans. Inf. Theory 62(4):2184–2202, 2016

The arXiv 1401.0903v2 version is titled "Second order statistics characterization of Hawkes processes and non-parametric estimation".

**Conditional law.**
- `g^{ij}(t)dt = E(dN^i_t | dN^j_0 = 1) − ε^{ij}δ(t) − Λ^i dt` (Eq. 7).
- `ν(t) = Σ g^T(t)`, with Σ = diag(Λ) (Eq. 8).

**Proposition 2 (Eq. 18), the D²-dimensional Wiener–Hopf system:**
```
g(t) = Φ(t) + Φ ⋆ g(t),   ∀ t > 0
```
- Theorem 1: Φ is the unique causal L¹ solution.
- Corollary 1: a stationary multivariate Hawkes process is uniquely determined by its first- and second-order statistics.

**Estimation.**
- Estimate g empirically, then solve the WH system with a Nyström method using Q Gaussian quadrature points. Section III extends this to marked processes with piecewise-constant mark functions.
- Applications: high-frequency trading events and earthquakes.

---

## bochud2007 — Bochud & Challet, QF 7(6):585–589, 2007

The arXiv version, physics/0605149v2, is titled "Optimal approximations of power-laws with exponentials".

**Setup.** Approximate `f(x) = x^{−α}` on `[1, 10^k]` by `g(x) = Σ_{i=0}^{N} w_i e^{−λ_i x}`.

**Ansatz.**
- The i-th exponential matches f and f′ at `x_i = β^i`, which gives `λ_i = αβ^{−i}` and `w_i = (e/β^i)^α` (Eqs. 2–3).
- Corrected form: `g(x) = Σ c_i β^{−iα} e^{α} e^{−α x/β^i}` (Eq. 4). The `c_i` solve the N+1 linear equations `g(β^j)=f(β^j)`.
- Uniform `c_i = c` with `g(1)=f(1)` gives Eq. 5.

**Results.**
- With `β^N = 10^k`, the per-decade error C has a minimum at `N_m(k)`. For α=2, `N_m ≈ 1.7k`, i.e. `β ≈ 10^{1/1.7} ≈ 3.87`.
- A second-order refinement gives `λ_i = sqrt(α(α+1)) β^{−i}`.

---

## bremaud1996 — Brémaud & Massoulié, Ann. Probab. 24(3):1563–1588, 1996

**Abstract (Project Euclid).** "...convergence to equilibrium of a general class of point processes, containing, in particular, the nonlinear mutually exciting point processes ... give general conditions guaranteeing the existence of a stationary version and the convergence to equilibrium of a nonstationary version, both in distribution and in variation."

**Nonlinear intensity.** `λ_t = φ(∫_{(−∞,t)} h(t−s) N(ds))` per Costa et al. (arXiv 1801.04645), which writes `Λ^{h,φ}(t)`. Bacry et al. 2015 write `λ^i_t = h(µ^i + Σ_j ∫dN^j φ^{ij}(t−t'))`.

**Main condition (Theorem 1).** As restated by Zhu (arXiv 1204.1067): "under the assumption that λ(·) is α-Lipschitz with α‖h‖_{L1} < 1, there exists a unique stationary and ergodic version". h may be signed. This permits inhibition.

The primary PDF was not read. The theorem number is per Costa et al.'s citation "[5, Theorem 1]".

---

## lu2018 — Lu & Abergel, QF 18(2):249–264, 2018

Only the abstract was verified, via RePEc. HAL hal-01686122 was blocked by a bot wall.

**Abstract.** "High-dimensional Hawkes processes with exponential kernels are used to describe limit order books... The observation of inhibition effects is particularly interesting, and leads us to the use of non-linear Hawkes processes. A specific attention is devoted to the calibration problem, in order to account for the high dimensionality of the problem and the very poor convexity properties of the MLE."

**UNVERIFIED:** the specific event types, the dimension, the dataset, which flows inhibit which, and the nonlinearity used.

---

## rambaldi2017marked — Rambaldi, Bacry, Lillo, QF 17(7):999–1020, 2017 (arXiv 1602.07663v1)

**Model forms.**
- The factorised ("naive") mark model `λ_t = µ + ∫ f(v_s)φ(t−s)dN_s` (Eq. 7) is rejected: the estimated `φ(j→i)` across volume bins have different shapes (Fig. 1, DAX).
- The general form is `λ_t = µ + ∫φ(t−s; v_s)dN_s` (Eq. 8). It is implemented by **binning the volume into D classes and treating each class as one component of a multivariate Hawkes process**.

**Estimation.**
- Non-parametric Wiener–Hopf (Bacry–Muzy), with `Λ = (I − ||φ||_1)^{-1}µ` (Eq. 6).
- Moderate inhibition shows up as negative conditional laws `g^{ij}`.

**Data.**
- Eurex Bund and DAX futures, July 2013–November 2014 (QuantHouse), microsecond timestamps.
- Bund is described as large-tick, DAX as small-tick.
- There is a ≈300 µs peak in the duration distribution, linked to the Eurex round trip.

**Findings.**
- Self-excitation dominates, followed by excitation from large volumes.
- Large trades excite opposite-side limit orders, more so for DAX.
- There is mutual excitation between limit and cancel orders on opposite sides, and **inhibition** `L^x_i → L^y_j` and `C^x_i → C^y_j` for x≠y (same type, opposite side). The paper reads this as a fair-price effect.
- "Large orders [are] more exogenous than small ones": µ_i/Λ_i is higher for large orders.

---

## wang2012mmhp — Wang, Bebbington, Harte, Ann. Inst. Stat. Math. 64(3):521–544, 2012 (online 2010)

**Abstract (via WebSearch summary).**
- The Markov-modulated Hawkes process with stepwise decay (MMHPSD) switches among hidden states. Each state has its own background rate and decay rate.
- The states capture main shocks, large aftershocks, secondary aftershocks and quiescence.
- Estimation uses EM. Data: the Landers–Hector Mine sequence.

Fabre & Muni Toke (arXiv 2502.04027) add that the stepwise rather than continuous decay "allowed the authors to design an EM algorithm". Their own model "extends the MMHPSD by allowing the intensity to decrease between event times", which implies the MMHPSD intensity is piecewise constant between events.

The exact MMHPSD intensity formula is UNVERIFIED.

---

## wu2019qrhawkes — Wu, Rambaldi, Muzy, Bacry, arXiv 1901.08938 (2019)

**General QRH form (Eq. 2).** `λ^ℓ(t) = µ^ℓ(q(t)) + Σ_m ∫φ^{ℓm}(t−s, q(t))dN^m_s`.

**QRH-I (Eq. 4).** One queue at the best bid or best ask:
- `λ^ℓ(t) = µ^ℓ(q(t−)) + Σ_m ∫_0^t φ^{ℓm}(t−s)dN^m_s` for ℓ, m ∈ {L, C, M}.
- `q(t) = q(0) + N^L − N^M − N^C`.
- `λ^{M,C} = 0` when q=0.
- The Hawkes memory resets at each `p_ref` change, as in the HLR periods.
- Kernels are sums of exponentials, `φ^{ℓm}(t) = Σ_u α^{ℓm}_u β_u e^{−β_u t}`, with β_u fixed hyperparameters, so the log-likelihood is convex in (µ, α).

**QRH-II (Eq. 17).**
- `λ^ℓ(t) = f^ℓ(q_a(t), q_b(t)) (µ^ℓ + Σ_m ∫φ^{ℓm}(t−s)dN^m_s)` over the 8 event types {P±, L^{a,b}, C^{a,b}, M^{a,b}} of Bacry et al.
- The state dependence multiplies both the baseline and the Hawkes term. Calibration is by MLE or least squares.

**Data.**
- Eurex Bund and DAX L1 data, 1 Oct 2013–30 Sep 2014.
- 1,207,099 constant-price periods for Bund and 417,581 for DAX.
- Fixed decay rates include β_3 = 5500 s⁻¹ (Bund), and β = 40, 2100, 5200 s⁻¹ (DAX).

**Findings (abstract).**
- The Hawkes term "dramatically improves" the pure QR model, both for inter-event times and for queue distributions. This is measured by log-likelihood, AIC and BIC (Table 2) and by QQ plots.
- Market-order and mid-price-change rates are mainly functions of volume imbalance; limit and cancel rates are not.

---

## Multivariate Hawkes MLE: separability of the log-likelihood

**Wu et al. 2019, Eq. 5.** "The log-likelihood L of a D-dimensional point process where the components do not share any parameters has the following general form (see [10], page 21)":
```
L(θ) = Σ_{ℓ=1}^{D} L_ℓ(θ),   L_ℓ(θ) = ∫_0^T log λ^ℓ(t;θ|F_t) dN^ℓ_t − ∫_0^T λ^ℓ(t;θ|F_t) dt
```
- Their [10] is Daley (& Vere-Jones), *An Introduction to the Theory of Point Processes* vol. 1, Springer (cited as "2008"). This corresponds to our key `daley2003`. Page 21 was not checked by us.

**Yang et al. 2017, Eq. 4.** The negative log-likelihood is written `L_t(λ) = Σ_{i=1}^{p} L_{i,t}(λ_i)`, i.e. a sum of per-dimension terms each depending only on `λ_i`.

**Bacry et al. 2015 (key `bacry2015`), App. C.1, Eq. 70.**
- `log L(µ,Φ) = −Σ_i ∫_0^T λ^i_t dt + Σ_m log λ^{k_m}_{t_m}`.
- Likelihood computation is `O(M²D)` in general, and `O(MD)` for exponential kernels.

**Conclusion.** Because `λ^i` depends only on `(µ^i, {φ^{ij}}_j)`, the i-th term contains only the i-th row's parameters. The MLE therefore splits into D independent problems, provided no parameters are shared across rows (e.g. a common β). This last step is our inference from the stated form. Wu et al. state the "components do not share any parameters" condition explicitly.

---

## yang2017online — Yang, Etesami, He, Kiyavash, NIPS 30, 2017

The authors were confirmed on the NeurIPS proceedings page. **They are not "Pentyala, Mohan, Feng".** No page numbers are given in the NeurIPS BibTeX.

**Method (abstract).**
- Nonparametric online learning of triggering functions `f_{i,j}(t)` approximated in an RKHS.
- It maximises "a time-discretized version of the log-likelihood, with Tikhonov regularization".
- **O(log T) regret.** The algorithm is called NPOLE-MHP (Algorithm 1), with projected gradient steps.

---

## gao2024mhp — Gao, Dai, Hu, "Mamba Hawkes Process", arXiv 2407.05302v1 (7 Jul 2024)

The authors were confirmed: Anningzhe Gao, Shan Dai (Shenzhen Research Institute of Big Data) and Yan Hu (CUHK-Shenzhen).

**Model.**
- Event history is encoded with a Mamba SSM, using time-dependent discretisation with `Δ_i = Softplus(S_Δ(x̂_i))`.
- Intensity, in the neural Hawkes style: `λ(t) = Σ_k λ_k(t)` and `λ_k = f_k(α_k (t−t_j) + w_k^T h(t_j) + b_k)`, with `f_k` a scaled softplus (Eqs. 12–13).
- The paper also introduces MHP-E, a Mamba + Transformer variant.

**Datasets.** Synthetic, Financial Transactions (a day of stock buy/sell events, mean length 2,074), StackOverflow, Retweet and MIMIC-II.

---

## gatheral2018rough — Gatheral, Jaisson, Rosenbaum, QF 18(6):933–949, 2018 (arXiv 1410.3394v1)

**Estimator.**
- `m(q,Δ) = (1/N)Σ|log σ_{kΔ} − log σ_{(k−1)Δ}|^q`. The scaling `E|log σ_Δ − log σ_0|^q = K_q Δ^{ζ_q}` is found empirically, with `ζ_q ≈ Hq`.
- **H = 0.125 for DAX and 0.082 for Bund** (Fig. 2.3). The S&P and NASDAQ indices (Oxford-Man realized variance) give similar results.
- Abstract: "log-volatility behaves essentially as a fractional Brownian motion with Hurst exponent H of order 0.1".

**RFSV model.**
- `σ_t = exp(X_t)`, with `dX_t = ν dW^H_t − α(X_t − m)dt` and H < 1/2 (Eqs. 3.3–3.4).

---

## hoffmann2013leadlag — Hoffmann, Rosenbaum, Yoshida, Bernoulli 19(2):426–461, 2013

**Definition.** `(X, Y)` has lead–lag ϑ if `(X_t, Y_{t+ϑ})` is a semimartingale with respect to a suitable filtration.

**Estimator (Sec. 3.2, Eq. 9).** Shifted Hayashi–Yoshida contrast over the observation intervals I (of X) and J (of Y):
```
U^n(ϑ̃) = 1_{ϑ̃≥0} Σ_{I,J: I≤T} X(I)Y(J) 1{I ∩ J_{−ϑ̃} ≠ ∅} + 1_{ϑ̃<0} Σ_{I,J: J≤T} X(I)Y(J) 1{J ∩ I_{ϑ̃} ≠ ∅}
ϑ̂_n solves |U^n(ϑ̂_n)| = max_{ϑ̃ ∈ G^n} |U^n(ϑ̃)|
```
The estimator is consistent, with a rate governed by the sparsity of the sampling design.

---

## Da Fonseca & Zaatour: three papers exist (all J. Futures Markets)

- **dafonseca2014**, "Hawkes Process: Fast Calibration, Application to Trade Clustering, and Diffusive Limit", JFM 34(6):548–579, 2014.
  - Gives explicit moments and the autocorrelation of jump counts over an interval, using the affine property.
  - Method-of-moments calibration on trade arrival times of major stocks, rolled over time.
  - Diffusive limit linking HF parameters to daily volatility.
- **dafonseca2015**, "Clustering and Mean Reversion in a Hawkes Microstructure Model", JFM 35(9):813–838, 2015.
  - Multivariate moments.
  - Unifies the mean-reverting price model of Bacry et al. 2013 with the clustering model of dafonseca2014.
  - Diffusive limit, and explicit price impulse response to a buy or sell trade.
- **dafonseca2017leadlag**, "Correlation and Lead–Lag Relationships in a Hawkes Microstructure Model", JFM 37(3):260–285, 2017.
  - Multi-asset Hawkes model with the covariance of the diffusive limit.
  - Lead–lag illustrated on Eurex assets.
  - **This is the cross-asset lead–lag paper.**

All three entries are based on abstracts only (Crossref); the papers were not read.

---

## UNVERIFIED summary

- Venue of `lewis2011em`. Treated as a preprint; the JNS 2011 citation found in Achab et al. could not be confirmed.
- `lu2018`: everything beyond the abstract (event types, data, which flows inhibit which).
- `wang2012mmhp`: the exact intensity formula.
- `ogata1988`: exact statements, equation numbers and sections. The time-rescaling description comes from secondary sources.
- `ogata1981`: algorithm details are from Bacry et al. 2015, not from the primary text.
- `bremaud1996`: the exact theorem statement. The Lipschitz condition `α‖h‖_1 < 1` is as restated by Zhu (2012) and Costa et al.
- `daley2003` p. 21, as cited by Wu et al. for the likelihood form: page not checked.
- Da Fonseca papers: abstracts only.
- Huang et al.: the published JASA version was not compared with arXiv v2.
