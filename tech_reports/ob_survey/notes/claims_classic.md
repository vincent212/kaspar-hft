# Classic LOB references: summaries and claim verification

Compiled 2026-10-02. Sources: Crossref API, arXiv PDFs (text extracted with pdftotext), publisher and author pages.
Unless stated otherwise, quotes come from the arXiv version named in each entry. The published version may differ slightly in wording or numbering.
BibTeX for the new keys is in `bib/refs_classic.bib`. Keys that already exist in `refs.bib` are reused here and not redefined: `cks2014`, `stoikov2018`, `moallemi2017`, `jaisson2015`, `bacry2015`, `filimonovsornette2012`, `filimonovsornette2015`, `hardimanbercotbouchaud2013`.

---

## Part A — Paper summaries

**abergel2016** — Abergel, Anane, Chakraborti, Jedidi, Muni Toke, *Limit Order Books*, CUP 2016 (Physics of Society series), DOI 10.1017/CBO9781316683040.
I only checked the publisher and book-listing descriptions, not the text. According to those descriptions, the book covers the statistical, mathematical and numerical sides of LOBs. It starts with empirical properties drawn from data, moves to mathematical models that reproduce them, and ends with a numerical simulation framework that includes agent-based and Hawkes-based modelling. I extracted no numbers.

**cont2011** — Cont, IEEE Signal Processing Magazine 28(5):16–25, 2011.
This is a review of the empirical properties of high-frequency data and of stochastic models for them. Its main point is that transaction-level dynamics "cannot be characterized solely in terms of the dynamics of a single price". Models must also cover the order flow at the bid, the ask and possibly deeper levels. I read only the abstract and a search summary.

**bouchaud2009** — Bouchaud, Farmer, Lillo, Handbook of Financial Markets (Elsevier), pp. 57–160 (arXiv 0809.0822).
This review covers how supply and demand fluctuations get slowly incorporated into prices. Abstract: "Because revealed market liquidity is extremely low, large orders to buy or sell can only be traded incrementally, over periods of time as long as months. As a result order flow is a highly persistent long-memory process." It then reviews theory on impact, spread, book dynamics and volatility.

**jain2024sim** — Jain, Firoozye, Kochems, Treleaven, arXiv:2402.17359 (27 Feb 2024).
This review of LOB simulation models does four things:
- classifies the models by methodology;
- aggregates the stylized facts used to validate them;
- includes a focused study of how price impact is represented;
- compares the models' quality of fit to empirical data.

**bacry2014** — Bacry and Muzy, Quantitative Finance 14(7):1147–1166 (arXiv 1301.1135).
This paper builds a 4-dimensional Hawkes model of trades and mid-price jumps. Its four kernels cover trade self-excitation, price mean reversion, trade→price impact and price→trade feedback. The model reproduces microstructure stylized facts and the concave, relaxing impact of meta-orders. Diffusive limit (Sec. 3.3): the centred Hawkes process satisfies `(1/√h)(P_{ht} − E P_{ht}) → (I − Φ̂_0)^{-1} Σ^{1/2} W_t`. The price variance is given in closed form in terms of the kernel norms (see Part C).

**bacry2013** — Bacry, Delattre, Hoffmann, Muzy, Quantitative Finance 13(1):65–77 (arXiv 1101.3422).
Upward and downward price jumps are modelled as two *mutually* exciting Hawkes processes. With cross-kernels only, λ1 = μ + ∫φ dN2 and λ2 = μ + ∫φ dN1. This produces microscopic mean reversion (the signature plot) and, in 2D, the Epps effect, "while preserving a standard Brownian diffusion behaviour on large scales". The paper uses Eurex Bund and Bobl futures. Formulas are in Part C.

**bacry2016** — Bacry, Jaisson, Muzy, Quantitative Finance 16(8):1179–1201 (arXiv 1412.7096).
This paper adapts the non-parametric Hawkes estimator to slowly decaying kernels. On simulations it recovers a power-law kernel "over at least 6 decades". It fits an 8-dimensional model: price moves P, trades T, limits L and cancels C that do not move the price, each split into ask and bid. The data are DAX (xFDAX) and Bund (xFGBL) futures. Findings:
- The T, L and C sub-blocks of the kernel-norm matrix are diagonal, meaning events mostly excite events of the same type and sign. They attribute this "mainly … to the splitting of metaorders, see [29] [Lillo, Mike, Farmer 2005], and to a less extent to some herding".
- The P block is anti-diagonal, which means mean reversion.
- The dominant kernels "loosely behave as a power law of exponent slightly higher than one".
- Exogenous limits and cancels have little effect on the mid-price, which is mainly driven by exogenous trades and price jumps.

**jaisson2016** — Jaisson and Rosenbaum, Ann. Appl. Probab. 26(5), 2016 (arXiv 1504.03100).
This paper studies nearly unstable Hawkes processes (‖φ‖₁ close to 1) whose kernel tail is ~x^{−(1+α)}, α ∈ (0,1). For α ∈ (1/2, 1), the rescaled process converges to "a kind of integrated fractional Cox–Ingersoll–Ross process, with associated Hurst parameter H = α − 1/2". With a light-tailed kernel the limit is instead a classical Brownian CIR. The authors offer this as an "agent-based foundation" for rough volatility.

**jaisson2015** (existing key) — Jaisson and Rosenbaum, Ann. Appl. Probab. 25(2):600–631 (arXiv 1310.2033).
Abstract: "in practice, the statistical estimation results seem to show that very often, only nearly unstable Hawkes processes are able to fit the data properly". After rescaling, nearly unstable Hawkes processes "asymptotically behave like integrated Cox–Ingersoll–Ross models". Applied to the Bacry et al. (2013) price model, "under a similar criticality condition, this process converges to a Heston model."

**large2007** — Large, J. Financial Markets 10(1):1–25. I read the abstract only, via search.
The paper defines resiliency through an intensity-based, continuous-time impulse response. It estimates a 10-variate mutually exciting Hawkes model of orders and cancellations for Barclays on the LSE. Findings: the impulse responses have half-lives under 20 s but are slight, implying a resilient response to big trades in under 40% of cases. The resilient response is fast when it happens, but it happens infrequently.

**munitoke2011** — Muni Toke, in *Econophysics of Order-driven Markets* (Springer, New Economic Windows), pp. 49–64 (arXiv 1003.3796).
The paper adds mutually and asymmetrically exciting Hawkes arrival times for limit and market orders to a zero-intelligence order-book simulator. The modelling rests on empirical inter-order times in equities, bond futures and index futures. Result: "this simple feature enables a much more realistic treatment of the bid-ask spread of the simulated order book." The published chapter title is "“Market Making” in an Order Book Model and Its Impact on the Spread".

**contlarrard2013** — Cont and de Larrard, SIAM J. Financial Math. 4(1):1–25 (arXiv 1104.4596). Full detail is in Part C. In short, this is a Markovian queueing model of the best bid and ask queues with closed-form duration law, up-move probability, price autocorrelation and diffusion limit.

**contlarrard2012** — Cont and de Larrard, arXiv:1202.6412.
This paper generalizes contlarrard2013 to high-frequency order flow. It derives a functional CLT and heavy-traffic limit for the bid and ask queues. The limit is a Markovian jump-diffusion in the positive orthant, and the result covers Poisson, self-exciting and ACD-GARCH order flow. It gives a closed-form uptick probability; see Part C.

**cao2009** — Cao, Hansch, Wang, J. Futures Markets 29(1):16–41. Data: Australian Stock Exchange. Abstract: the book beyond the best quotes is "moderately informative — its contribution to price discovery is approximately 22%. The remaining 78% is from the best bid and offer prices on the book and the last transaction price." Order imbalances along the book "are significantly related to future short-term returns, even after controlling for the autocorrelations in return, the inside spread, and the trade imbalance."

**gould2016qi** — Gould and Bonart, Market Microstructure and Liquidity 2(2):1650006 (arXiv 1512.03492).
The paper fits logistic regressions of the next mid-price move direction on queue imbalance for 10 Nasdaq stocks (2014). It finds a "strongly statistically significant relationship" for every stock. Out-of-sample improvements over a null model:
- Binary classification: about 50–60% for large-tick stocks and about 10–30% for small-tick stocks.
- Probabilistic classification: about 20–30% for large-tick and about 2–6% for small-tick.

Large-tick stocks are MSFT, INTC, MU, CSCO and ORCL. Small-tick stocks include GOOG and AMZN.

**xu2019mlofi** — Xu, Gould, Howison, Market Microstructure and Liquidity 4(3–4):1950011 (arXiv 1907.06230). Crossref dates the issue to 2018, but the paper was published online on 8 Jan 2020.
The paper fits a linear relation between multi-level OFI (MLOFI) and contemporaneous mid-price changes for 6 Nasdaq stocks. "For all 6 stocks … the out-of-sample goodness-of-fit of the relationship improves with each additional price level that we include in the MLOFI vector."

**contcucuringu2023** — Cont, Cucuringu, Zhang, Quantitative Finance 23(10):1373–1393 (arXiv 2112.13213). Data: the top 100 S&P 500 components, Nasdaq ITCH via LOBSTER, 2017-01-01 to 2019-12-31. Details are under C8.

**obizhaeva2013** — Obizhaeva and Wang, J. Financial Markets 16(1):1–32 (NBER WP 2005). I read the abstract only, via search.
This is an LOB model with spread, depth and resilience. The optimal execution strategy depends on the book's dynamic property, resilience ("the speed at which supply/demand recovers"), and not on static spread or depth. The optimal strategy combines discrete and continuous trades.

**mei2017** — Mei and Eisner, NIPS 2017 (Advances in NeurIPS 30), pp. 6754–6764 (arXiv 1612.09328).
The paper presents a "neurally self-modulating multivariate point process" whose intensities evolve according to a continuous-time LSTM. It reports competitive likelihood and predictive accuracy on real and synthetic data. The paper is not finance-specific.

**lillo2004** — Lillo and Farmer, Studies in Nonlinear Dynamics & Econometrics 8(3), 2004. Data: LSE. Abstract: "the signs of orders obey a long-memory process. The autocorrelation function decays roughly as a power law with an exponent of 0.6, corresponding to a Hurst exponent H = 0.7." The predictability of signs is offset by "anti-correlated fluctuations in transaction size and liquidity". The abstract does not attribute the long memory to order splitting. That attribution is in **lillo2005** (Lillo, Mike, Farmer, Phys. Rev. E 71:066122, 2005, "Theory for long memory in supply and demand"), which is the reference [29] that bacry2016 cites for metaorder splitting.

**bouchaud2004** — Bouchaud, Gefen, Potters, Wyart, Quantitative Finance 4(2):176–190. Data: Paris Bourse. The random walk of prices results from "a very delicate interplay between two opposite tendencies: long-range correlated market orders that lead to super-diffusion … and mean reverting limit orders that lead to sub-diffusion". The paper introduces the propagator model. The market is "in a precise sense, at a critical point".

**bouchaud2002** — Bouchaud, Mézard, Potters, Quantitative Finance 2(4):251–256. Data: three Paris Bourse stocks. Findings: (i) incoming limit-order prices follow "a power-law around the current price with a diverging mean"; (ii) the "humped shape of the average order book" can be reproduced by a zero-intelligence model.

**smith2003** — Smith, Farmer, Gillemot, Krishnamurthy, Quantitative Finance 3(6):481–514.
This is a zero-intelligence continuous double auction model with IID random order flow, analysed by simulation, dimensional analysis and mean-field theory. It predicts volatility, depth profile, spread, price impact and fill times from order-flow rates. Order size, expressed as a non-dimensional granularity parameter, is "in most cases a more significant determinant of market behavior than tick size". The paper also explains concave impact.

**farmer2005** — Farmer, Patelli, Zovko, PNAS 102(6):2254–2259. The zero-intelligence model is tested on 11 LSE stocks. "The model explains 96% of the variance of the bid-ask spread, and 76% of the variance of the price diffusion rate, with only one free parameter."

**dixon2018** — Dixon, J. Computational Science 24:277–286 (arXiv 1707.05642). An RNN classifies the next price flip from a short sequence of LOB depths and market orders. Data: E-mini S&P 500 Level II, August 2016. The RNN "compares favorably with other classifiers, including a linear Kalman filter". I extracted no numbers.

**eisler2012** (supporting C11) — Eisler, Bouchaud, Kockelkoren, Quantitative Finance 12(9):1395–1419 (arXiv 0904.0900). Event types are MO0/MO′ (market orders that do not / do change the price), LO0/LO′ (limit orders at the best / inside the spread) and CA0/CA′ (cancellations). Bare impacts are permanent for large-tick stocks and history-dependent for small-tick stocks. Table II gives the event frequencies (see C11).

**garriott2025**, **zhang2025oi**, **debie2023** — see C5, C6 and C7.

---

## Part B — Stoikov micro-price (`stoikov2018`, Quantitative Finance 18(12):1959–1966)

**Source caveat.** I could not retrieve the journal PDF or the SSRN PDF (SSRN returned "Content Blocked" and T&F is paywalled). Everything below comes from Stoikov's own materials:
- his "The Micro-Price" talk slides (Gatheral 60 conference, Imperial College site);
- his reference notebook `github.com/sstoikov/microprice` ("Microprice - Big Data Conference.ipynb").

The SSRN abstract (ssrn 2970694) was read via search snippets. The equations match across both sources, but equation numbers and notation in the published paper may differ. Check against the journal PDF before quoting equation numbers.

**SSRN abstract (as indexed):** the micro-price is "the limit of a sequence of expected mid-prices", with "conditions for this limit to exist". It "is a martingale by construction" and "can be considered to be the 'fair' price … conditional on the information in the order book". It "may be expressed as an adjustment to the mid-price that takes into account the bid-ask spread and the imbalance". The paper shows empirically that it "is a better predictor of short term prices than the mid-price or the weighted mid-price".

**Definitions (slides):**
- Mid-price: `M = (P^b + P^a)/2`.
- Imbalance: `I = Q^b/(Q^b + Q^a)`.
- Weighted mid-price: `M^w = I·P^a + (1 − I)·P^b` (attributed to Gatheral and Oomen 2009).
- Spread: in the slides' assumption `S_t = ½(P^a − P^b)`; the notebook uses `S = P^a − P^b`.

**What is wrong with the mid and the weighted mid (slides, verbatim bullets):**
- Mid-price: "Not a martingale (Bid-ask bounce)", "Low frequency signal", "Doesn't use volume at the best bid and ask prices."
- Weighted mid-price: "Not a martingale", "Noisy", "Counter-intuitive examples". The notebook adds that it "Is quite noisy, particularly when the spread widens to two ticks".
- The counter-intuitive example in the slides: Q^b = 9, Q^a = 1, second ask level size 27. When the single ask unit cancels, the ask moves up one tick. The new M^w is lower than before, "The 'fair' price just moved down after an ask order canceled?" The slide's price figures contain typos ($32.17 bid vs "$31.18" ask), so do not quote the dollar figures.

**Definition (slides):**
`P_t^micro = lim_{i→∞} P_t^i`, with `P_t^i = E[M_{τ_i} | F_t]`, where τ_1, …, τ_n are the (random) times at which the mid-price changes.

Assumptions:
1. F_t = (M_t, I_t, S_t) is Markov.
2. The dynamics are independent of the level M_t, i.e. `E[M_{τ1} − M_t | M_t, I_t, S_t] =: g^1(I_t, S_t)`.

**Main result (slides, Theorem):**
`P_t^i = M_t + Σ_{k=1}^{i} g^k(I_t, S_t)`, where
`g^1(I_t,S_t) = E[M_{τ1} − M_t | I_t, S_t]` and
`g^{i+1}(I_t,S_t) = E[g^i(I_{τ1}, S_{τ1}) | I_t, S_t]`.

**Toy cases (slides):**
- Mid independent of imbalance, with symmetric ±1 jumps and S = 1, gives P^micro = M.
- I_t a Brownian motion on [0,1], absorbed at 0 or 1, with the mid jumping or bouncing back with probability 0.5 each, gives `P^micro = M_t + I_t − 1/2`. This is the weighted mid when S = 1.

**Discrete Markov model (slides):**
- Discrete time t ∈ Z+.
- Imbalance discretised to n states, 1 ≤ i_I ≤ n.
- Spread discretised to m states, 1 ≤ i_S ≤ m.
- State X_t = (I_t, S_t) has nm values.
- Mid changes take values in K = {k : 0 < |k| ≤ 2m} (in half-ticks). The notebook uses K = [−0.01, −0.005, 0.005, 0.01] for 1-cent ticks with m = 2.

Transition matrices:
- `Q_ij := P(M_{t+1} − M_t = 0 ∧ X_{t+1} = j | X_t = i)` (transient states, mid does not move).
- `R^1_ik := P(M_{t+1} − M_t = k | X_t = i)` (absorbing states, mid moves).
- `R^2_ik := P(M_{t+1} − M_t ≠ 0 ∧ X_{t+1} = k | X_t = i)`. The slides write `I_{t+1} = k | I_t = i`, but the notebook builds R² over the full (spread, imbalance) state.

Recursion:
- `g^1 = (1 − Q)^{-1} R^1 k`
- `g^{i+1} = (1 − Q)^{-1} R^2 g^i = B g^i`, with `B := (1 − Q)^{-1} R^2`

**Convergence (slides, Theorem):** "If B has strictly positive entries and lim_{k→∞} B^k = W where W is the unique stationary distribution and W g^1 = 0, then the limit lim_i p_t^i = p_t^micro converges."
Spectral form: `p^micro = M_t + Σ_{i=2}^{nm} exp(λ_i) B_i g^1`, where λ_i are the eigenvalues of B and B_i are built from its normalised left and right eigenvectors. The slides write it this way. **The `exp(λ_i)` factor is reproduced as printed; I could not check it against the paper.**
Adjustment: `G* = p^micro − M = Σ_{i≥2} exp(λ_i) B_i g^1`.

**Symmetrisation (slides):** "g^1 … is symmetrized to ensure that g^1(i_I, i_S) = 1 − g^1(n − i_I, i_S)". The "1 −" is as printed; for an antisymmetric price adjustment one would expect a minus sign, so treat it as a likely typo. B is symmetrised so that `B_{(i_I,i_S),(j_I,j_S)} = B_{(n−i_I,i_S),(n−j_I,j_S)}`. "The symmetrizing procedure ensures that B g^1 = 0 [as printed] and that the micro-price converges."
In the notebook, the data are symmetrised before estimation by appending a mirrored copy: `I² = n − I`, `S² = S`, `ΔM² = −ΔM`. In code, `imb_bucket → n_imb−1−imb_bucket`, and dM and mid are negated. The notebook text says "Does the micro-price converge? Yes. But we have to appropriately symmetrize the data."

**Imbalance discretisation (notebook):** `pd.qcut(imb, n_imb)`, i.e. quantile buckets. BAC uses n_imb = 10 and n_spread = 2; CVX uses n_imb = 4 and n_spread = 4. Spreads above n_spread ticks are dropped, and only |ΔM| ≤ 1 tick is kept.
**Truncation:** "In practice, the distant future is well captured by P_t^6", i.e. `p^6 − M = g^1 + Bg^1 + … + B^5 g^1`.
**Data:** BAC (large tick) and CVX (small tick) quotes, March 2011. Out-of-sample checks compare G* with realised average M_{t+60} − M_t (and M_{t+300} − M_t), grouped by (I, S).

---

## Part C — Exact model results

### C-1. Cont and de Larrard 2013 (`contlarrard2013`; quoted from arXiv 1104.4596v1)

**Assumptions (Sec. 2):**
- The spread is fixed at one tick, s^a = s^b + δ. The paper's justification: spread = 1 tick for more than 98% of observations for C, GE and GM on 26 June 2008.
- State: X_t = (s^b_t, q^b_t, q^a_t).
- Arrivals follow independent Poisson processes:
  - market buy (sell) orders at rate **μ**;
  - limit buy (sell) orders at the best bid (ask) at rate **λ**;
  - cancellations at rate **θ**.
- All order sizes equal 1. Each queue changes at total rate λ+θ+μ, with `P[V=+1] = λ/(λ+μ+θ)` and `P[V=−1] = (μ+θ)/(λ+μ+θ)`.
- When the ask (bid) queue depletes, the price moves up (down) one tick. The new (q^b, q^a) is then drawn from a distribution **f** (after an increase) or **f̃** (after a decrease), independent of the past, with the paper assuming `f̃(x,y) = f(y,x)`.
- Empirically λ < μ+θ but close; Table 3 gives λ̂ / (μ̂+θ̂) = 2204/2331 for C, 317/325 for GE and 102/104 for GM, in 100-share batches per second.

**Duration until the next price change (Prop. 1, eqs. 3–4):**
`P[τ > t | q^a_0 = a, q^b_0 = b] = ((μ+θ)/λ)^{(a+b)/2} ψ_{a,λ,θ+μ}(t) ψ_{b,λ,θ+μ}(t)`,
`ψ_{n,λ,θ+μ}(t) = ∫_t^∞ (n/u) I_n(2√(λ(θ+μ)) u) e^{−u(λ+θ+μ)} du`, where I_n is the modified Bessel function of the first kind.
Tails:
- λ < μ+θ: `P[τ > t | x, y] ~ x y (λ+μ+θ)² / (λ²(μ+θ−λ)²) · 1/(4t²)` (eq. 5), i.e. regularly varying with tail index 2.
- λ = μ+θ: `P[σ^a > t | x] ~ x/√(πλ t)`, and `P[τ > t | x, y] ~ x y/(π λ t)` (eq. 6). The tail index is 1, so the duration has infinite mean.

**Probability that the next move is up, balanced case λ = μ+θ (Prop. 2, eq. 7):**
n orders on the bid and p on the ask:
`φ(n,p) = (1/π) ∫_0^π (2 − cos t − √((2 − cos t)² − 1))^p · sin(n t) cos(t/2)/sin(t/2) dt`.
This is the probability that a symmetric 2D random walk from (n,p) hits the x-axis (ask depleted) first. The paper notes it is "independent of the parameters describing the order flow".
**This paper has no x/(x+y) formula.** For comparison, the gambler's-ruin-type ratio b/(a+b) is a heuristic often attributed to this model in secondary sources, but I did not find it here. The closed-form arctan expression is in contlarrard2012 (C-2).

**Price autocorrelation (Prop. 3):** Let `p_cont = P[X2 = δ | X1 = δ]`. Then `Cov(X1, Xk) = (2p_cont − 1)^{k−1}` and `P[X_n = δ | x, y] = (1 + (2p_cont − 1)^{n−1}(2p_1(x,y) − 1))/2`. The first-lag autocorrelation is negative iff `Σ_{i≥1} Σ_{j≥i} f(i,j) > 1/2`; for Citigroup this sum is above 0.7.

**Diffusion limit (Sec. 4):** Define the depth measure `D(f) = Σ_i Σ_j i j f(i,j)` (eq. 9). The paper says √D(f) is the geometric mean of the bid and ask queue sizes after a price change. Section 4 opens with "Assume λ + θ ≤ μ"; this looks like a typo for λ ≤ θ+μ, and Theorems 1–2 use λ = μ+θ and λ < θ+μ. For f symmetric:
- **Theorem 1 (λ = μ+θ):** `(s_{t n log n}/√n, t ≥ 0) ⇒ δ √(πλ/D(f)) W_t`.
- **Eq. 13 (practical form):** `σ = δ √(n π λ / D(f))`, with n chosen so that `n ln n · τ0 = τ2`, where τ0 = 1/λ and τ2 is the observation scale (e.g. 10 min).
- **Theorem 2 (λ < θ+μ):** `(s_{nt}/√n) ⇒ δ W_t / √m(λ, θ+μ, f)`, where `m(λ,θ+μ,f) = Σ_{i,j} m(λ,θ+μ,i,j) f(i,j)` and `m(…,x,y) = ∫_0^∞ dt ∫_t^∞ ψ_{x}(u) du ∫_t^∞ ψ_{y}(u) du`, i.e. the mean duration.
- **Empirical check:** across Dow Jones stocks on 26 June 2008, the standard deviation of 10-minute increments rises roughly proportionally to √(λ/D(f)) (Fig. 5).

### C-2. Cont and de Larrard 2012 (`contlarrard2012`, arXiv 1202.6412), Sec. 5.3

Heavy-traffic limit, no drift (V^a = V^b = 0), bid queue x, ask queue y, correlation ρ between queue increments (Theorem 3, eq. 33):
`p^up(x,y) = 1/2 − arctan( √((1+ρ)/(1−ρ)) · (y/(√λ^a v^a) − x/(√λ^b v^b)) / (y/(√λ^a v^a) + x/(√λ^b v^b)) ) / (2 arctan √((1+ρ)/(1−ρ)))`.
- When √λ^a v^a = √λ^b v^b (eq. 34): `p^up = 1/2 − arctan(√((1+ρ)/(1−ρ)) (y−x)/(y+x)) / (2 arctan √((1+ρ)/(1−ρ)))`.
- **The paper then states, for ρ = 0, `p^up(x,y) = (2/π) arctan(y/x)`.** Evaluating eq. 34 at ρ = 0 gives instead `1 − (2/π) arctan(y/x) = (2/π) arctan(x/y)`. That expression rises with the bid queue x, as an uptick probability should. The printed ρ = 0 simplification appears to swap x and y. Use eq. 34, or `(2/π)arctan(x/y)`, and flag this if citing.

### C-3. Hawkes near-criticality and price volatility

**bacry2013 (Prop. 2.1, exponential cross-kernel φ(t) = α e^{−βt}, ‖φ‖₁ = α/β < 1):**
`C(τ) = Λ (κ² + (1 − κ²)(1 − e^{−γτ})/(γτ))`, where `Λ = 2μ/(1 − ‖φ‖₁)`, `κ = 1/(1 + ‖φ‖₁)` and `γ = α + β`.
- Microstructural variance: `V0 = C(0) = Λ = 2E(λ_i)`.
- Diffusive (macroscopic) variance: `V∞ = Λκ² = 2μ / ((1 − ‖φ‖₁)(1 + ‖φ‖₁)²)`.
- Here the kernel is **cross**-excitation (an up move excites down moves). The diffusive variance relative to the event rate Λ is therefore *reduced* by κ² = 1/(1+‖φ‖)². It is not inflated.
- Sec. 4: a rigorous multivariate Brownian limit is deferred to "a forthcoming paper".
- Illustrative fitted values are μ = 0.016, α = 0.023, β = 0.11 s⁻¹, i.e. ‖φ‖ ≈ 0.21 (computed from the values shown, not stated by the paper).

**bacry2014 (Sec. 3.3):** the general d-dimensional result is `(1/√h)(P_{ht} − E P_{ht}) → (I − Φ̂_0)^{-1} Σ^{1/2} W_t`, with diffusive covariance `(I − Φ̂_0)^{-1} Σ (I − Φ̂_0^†)^{-1}`. For the price X = N⁺ − N⁻ in their 4-kernel model:
`σ_X = √2 · √(Λ_T (Δφ̂^I_0)² + Λ_N (1 − Δφ̂^T_0)²) / ((1 − Δφ̂^T_0)(1 − Δφ̂^N_0) − Δφ̂^I_0 Δφ̂^F_0)`.

**jaisson2015 / jaisson2016:** see Part A. In short:
- Nearly unstable, light-tailed kernels give integrated CIR in the limit, and the Bacry et al. price model gives Heston.
- Nearly unstable heavy-tailed kernels with α ∈ (1/2,1) give an integrated fractional CIR with H = α − 1/2 (rough volatility).

---

## Part D — Claim verdicts

**C1. "Empirical Hawkes calibrations routinely find ρ(Γ) in 0.8–0.95 / 0.9–0.99."**
**Verdict: CONTRADICTED as stated.** The ranges in the sources differ by paper and are contested.
- filimonovsornette2012 (E-mini S&P, 1998–2010): "since 2002, n has been consistently above 0.6 and, since 2007, between 0.7 and 0.8 with spikes at 0.9". Around 0.3 in 1998–2000.
- hardimanbercotbouchaud2013 (E-mini mid-price changes, 1998–2011, power-law kernel): "the Hawkes kernel integrates to unity", i.e. n ≈ 1 throughout.
- filimonovsornette2015 says the n ≈ 1 estimates come from biases: outliers, kernel regularisation, edge effects, regime shifts. Their exhaustive re-optimisation of the HBB setup gives n low until 2002, rising to 2006, "and then later fluctuates between 0.8 and 1.1 approximately". They restate their own E-mini and commodity estimate as "n = 0.7 − 0.8".
- jaisson2015 says qualitatively that "very often, only nearly unstable Hawkes processes are able to fit the data properly".
- bacry2016 reports kernel-norm matrices only as figures. I found no numerical spectral radius in the text.

Neither "0.8–0.95" nor "0.9–0.99" appears in these sources as a stated range. A defensible wording: "estimates range from about 0.7–0.8 (Filimonov–Sornette) to ≈1 (Hardiman et al.), and are sensitive to the choice of kernel and to non-stationarity."

**C2. "Under scaling limits the mid-price converges to BM whose volatility is inflated by 1/(1−ρ)²."**
**Verdict: CONTRADICTED for bacry2013.** In that model, `V∞ = 2μ/((1−‖φ‖)(1+‖φ‖)²)`. The kernel there is cross-exciting, and relative to the event rate Λ = 2μ/(1−‖φ‖) the variance is damped by 1/(1+‖φ‖)².
A 1/(1−ρ)² factor does appear for **self**-exciting counts: the diffusive covariance `(I−Φ̂)^{-1} Σ (I−Φ̂^†)^{-1}` in bacry2014 Sec. 3.3 reduces in 1D to (1−‖φ‖)^{-2} times the mean rate. That factor applies to the variance, not the volatility, and to event counts, not to the mid-price in bacry2013. In the self-exciting case the variance is also ∝ μ/(1−‖φ‖)³ relative to the exogenous rate μ. That cube follows from the bacry2014 formula; it is not quoted from the text.
At criticality, jaisson2015 and jaisson2016 give Heston or rough (fractional CIR) limits, not a Brownian motion with inflated constant volatility.

**C3. Moallemi and Yuan decomposition.**
**Verdict: CONTRADICTED on wording; the rest is VERIFIED.**
- The paper's two components are a "**static** component" and a "**dynamic** component", not an "information component". Abstract: "(i) a static component that relates to the trade-off at an instant of trade execution between earning a spread and incurring adverse selection costs, and incorporates the fact that adverse selection costs are increasing with queue position; (ii) a dynamic component, that captures the optionality associated with the future value that accrues by locking in a given queue position." The model "incorporates both economic (informational) and stochastic modeling (queueing) aspects".
- **Size:** "queue value can be of the same order of magnitude as the bid-ask spread". The front-versus-average-queue difference was "about 0.26 ticks on 8/9/2013 and 0.21 ticks on 8/20/2013, which is comparable to the bid-ask spread". The paper says comparable to the *spread*, not "half the spread".
- **Front versus back:** "the value of orders placed at the front of the queue is always larger than the value of orders placed at the end". The gap is "comparable to the bid ask spread (> 0.1 ticks)" for BAC and CSCO and "< 0.1 ticks" for PFE and PBR. Adverse selection "is increasing with queue length … orders at the end of a large queue are more likely to be executed against a large trade". The back of the queue is more adversely selected.
- The existing summary in the brief is confirmed.

**C4. "Front-of-queue fills have 2–3× the adverse selection of mid/back-queue fills (0.5–1 bp vs 1.5–3 bp)."**
**Verdict: CONTRADICTED.** The direction is reversed. moallemi2017 finds adverse selection *increasing* with queue position, so the back of the queue is worse. I found no academic source for the numbers. The only bps figures I found are from a non-peer-reviewed Substack post ("The Microstructure Lab", OKX BTC-USDT, Feb 2026). It reports the opposite direction: about 1.1 bp with a small queue ahead versus about 3.0 bp with a deep queue ahead.

**C5. "Canadian futures: large-inventory MMs at the back of the queue increase quoted depth by up to 8.4%; unit-inventory effect 31% higher for the 4th MM than the 1st."**
**Verdict: CONTRADICTED for the 8.4% part; UNVERIFIED for the 31% part.**
- Source: Garriott, van Kervel and Zoican (`garriott2025`), J. Financial Markets 75:100982 (2025), using Montréal Exchange data.
- Abstract: "An inventory shock reduces liquidity provision by market makers later in the queue [crowding-out]. … These two results imply a trade-off, as the queuing sequence that optimizes risk sharing **decreases** quoted depth up to 8.4%."
- So 8.4% is a *decrease* in depth under the risk-sharing-optimal queue ordering. It is not an increase by back-of-queue market makers.
- I found the "31% … fourth market maker" figure in no accessible text (abstract and author page only; the full text is paywalled).

**C6. "Zhang and Xie (2025), Chinese market: order imbalance positively predicts returns at 5–30 min, reverses at 60–120 min."**
**Verdict: VERIFIED. Correction: there are three authors.**
- Source: Zhang, Xie, Wang (`zhang2025oi`), "Do order imbalances predict intraday returns? New evidence from the Chinese stock market", Asia-Pacific J. Accounting & Economics, online 19 Dec 2025, DOI 10.1080/16081625.2025.2604824. Crossref lists the authors as Ting Zhang, Chi Xie and Gang-Jin Wang.
- Abstract, from search-indexed text: order imbalances "positively predict stock returns from 5 to 30 minutes, while the predictive relation … reverses to negative from 60 to 120 minutes". It also says reversals "do not exist in large-size stocks, high-turnover stocks, and specially treated stocks".
- I could not access the full text (the publisher returned 403).

**C7. "2015 Ultra T-Bond JPM spoofing: ask-side liquidity costs fell ~10.4 → ~8 bps after spoof placement, recovering to 9–12 bps after cancellation."**
**Verdict: VERIFIED.** Source: Debie et al. (`debie2023`), Eur. Financial Management 29(1):288–326, Sec. 4.3.4, on the Ultra T-Bond September 2015 contract, 30 June 2015. Quote: "Before the spoof order was placed, liquidity costs on the ask side fluctuated between 9.5 and 13 bps. Immediately when the spoof order was placed, ask liquidity costs dropped from 10.38 to 7.97 bps and further decreased to approximately 6 bps right before the spoof order was cancelled. After the spoof order was cancelled, ask liquidity costs fluctuated between 9 and 12 bps." Liquidity costs are measured by APM (Adverse Price Movement).

**C8. Cont, Cucuringu and Zhang (2023).**
**Verdict: CONTRADICTED as drafted, because the draft omits the forecasting result.** It is correct only for *contemporaneous* impact.
- Abstract (verbatim, arXiv final version): "We investigate the impact of order flow imbalance (OFI) on price movements in equity markets in a multi-asset setting. First, we propose a systematic approach for combining OFIs at the top levels of the limit order book into an integrated OFI variable which better explains price impact, compared to the best-level OFI. We show that once the information from multiple levels is integrated into OFI, multi-asset models with cross-impact do not provide additional explanatory power for contemporaneous impact compared to a sparse model without cross-impact terms. On the other hand, we show that lagged cross-asset OFIs do improve the forecasting of future returns. We also establish that this lagged cross-impact mainly manifests at short-term horizons and decays rapidly in time."
- **(a) Integrated OFI.** The first principal component of the multi-level OFIs "can explain over 89% of the total variance among multi-level OFIs". Mean contemporaneous R² (Tables 3 and 5):

  | Model | In-sample R² | Out-of-sample R² |
  |---|---|---|
  | PI[1] (best-level OFI) | 71.16% | 64.64% |
  | PI^I (integrated OFI) | 87.14% | 83.83% |

- **(b) Contemporaneous cross-impact:**

  | Comparison | In-sample R² | Out-of-sample R² |
  |---|---|---|
  | CI[1] vs PI[1] | 73.87 vs 71.16 | 66.03 vs 64.64 (+1.39) |
  | CI^I vs PI^I | 87.85 vs 87.14 | **83.62 vs 83.83** |

  Quote: "the cross-impact model with integrated OFIs cannot provide extra explanatory power to the price impact model with integrated OFIs."
- **(c) Lagged cross-asset OFI, one-minute-ahead forecasts (Table 8, mean OS R²):** FPI[1] −0.37 vs FCI[1] −0.10; FPI^I −0.36 vs FCI^I −0.10; AR −0.36 vs CAR −0.10. The paper: "the cross-impact models exhibit significantly superior performance than the price impact models across all stocks, at the 1% confidence level". All OS R² values are negative, and the paper argues that this does not make the forecasts economically meaningless (Sec. 4.2.2 reports economic gains).
- **Accurate restatement:** "With integrated OFI, cross-impact terms do not improve the out-of-sample fit of a contemporaneous price-impact model; lagged cross-asset OFI does improve short-horizon return forecasts, though out-of-sample R² remains negative."

**C9. Gould and Bonart (2016).**
**Verdict: VERIFIED.** Abstract: "we find that our logistic regression fits provide a considerable improvement in binary and probabilistic classification for large-tick stocks, and provide a moderate improvement in binary and probabilistic classification for small-tick stocks." Introduction: "improve out-of-sample performance of binary classification by about 50–60% for large tick stocks and about 10–30% for small-tick stocks … probabilistic predictions … by about 20–30% for large-tick stocks and about 2–6% for small-tick stocks."

**C10. CKS 2014 (`cks2014`).**
**Verdict: VERIFIED.** Quoted from arXiv 1011.6402v3; the published JFEC version may differ in numbering.
- Abstract: "a linear relation between order flow imbalance and price changes, with a slope inversely proportional to the market depth."
- Introduction: "explains mid-price changes over short time scales in a linear fashion … with an average R² of 65%."
- Depth regression (Table 3): β̂_i = ĉ / AD_i^λ, with λ̂ ≈ 1 for most stocks ("λ = 1 appears to be a good approximation"). The per-stock R² varies widely: for example AMD 78%, BK 93%, APOL 2% and AZO 13%. The paper notes "three stocks with bad fits".
- OFI versus trade imbalance (Table 4, grand means): R² is 65% for OFI, 32% for TI and 67% for both. With both covariates, TI is significant in only 31% of subsamples.
- Abstract: "the relation between price changes and trade volume is found to be noisy and less robust than the one based on order flow imbalance."
- Data: NYSE TAQ, 50 U.S. stocks.

**C11. "A large fraction of price-changing events are aggressive limit orders rather than pure market orders."**
**Verdict: VERIFIED.** The source gives the frequencies; I computed the share.
- eisler2012, Table II, gives unconditional event probabilities for 13 Nasdaq stocks. LO′ is a limit order placed inside the spread, which changes the price.
- Shares of price-changing events (MO′ + CA′ + LO′) that are LO′, computed by me from Table II:

  | Stock | MO′ | CA′ | LO′ | LO′ share |
  |---|---|---|---|---|
  | AMAT | 0.011 | 0.0018 | 0.013 | ≈ 50% |
  | MSFT | 0.0087 | 0.0012 | 0.010 | ≈ 50% |
  | AAPL | 0.076 | 0.077 | 0.16 | ≈ 51% |
  | AMZN | 0.077 | 0.12 | 0.20 | ≈ 50% |

- So roughly half of price-changing events are spread-improving limit orders, both for large-tick and small-tick stocks.
- bacry2016 Remark 4.1 also states that mid-price moves "can correspond to the occurrence of a market order or a cancel order that eats all the available liquidity … or to a limit order placed in the spread", but gives no breakdown.

**C12. "Hawkes kernels have power-law tails consistent with long memory of order flow attributed to order splitting (Bacry–Jaisson–Muzy 2016; Lillo–Farmer)."**
**Verdict: VERIFIED, with an attribution correction.**
- bacry2016: "In all former studies where non parametric estimations of Hawkes kernels involved in the dynamics of order flows were performed [2, 4, 19], power-law kernels with exponents close to 1 have been observed. This property can be directly linked to the strong persistence of the order flow dynamics, see [25], mainly caused by the splitting of large orders and, to a lesser extent, to the herding behavior of agents." It also says the diagonal T, L and C kernels "loosely behave as a power law of exponent slightly higher than one".
- hardimanbercotbouchaud2013 reports a mid-price-change kernel exponent of about −1.15 below roughly 10³ s and about −1.45 from 10³ to 10⁶ s.
- Correction: Lillo and Farmer (2004) establish the long memory (ACF ~ τ^{−0.6}, H ≈ 0.7) but do not attribute it to splitting in their abstract. The splitting explanation is Lillo, Mike and Farmer (2005, `lillo2005`), which is what bacry2016 cites ([29]). bouchaud2009 states the same mechanism ("large orders … can only be traded incrementally … As a result order flow is a highly persistent long-memory process").

---

## Addendum (2026-10-03): Cont–de Larrard tail claim, checked numerically

- Their Prop. 1 gives P[σ^a > t | x] = ((μ+θ)/λ)^{x/2} ∫_t^∞ (x/u) I_x(2√(λ(μ+θ)) u) e^{−u(λ+μ+θ)} du.
  We evaluated this with scipy (normalisation at t=0 verified, value 1.000) and checked it against a Monte Carlo
  of the continuous-time random walk (in=0.6, out=1, x=3): t=5 MC 0.467 / exact 0.470; t=20 0.073 / 0.073;
  t=50 0.0056 / 0.0059; t=100 0.00022 / 0.00020.
- For λ < μ+θ the tail is exponential, with rate of order (√(μ+θ) − √λ)², as expected for a random walk drifting to 0
  (Bessel asymptotics). The paper's eq. (5), a 1/t² Pareto tail, comes from a Tauberian step applied to
  L(s,x) ~ 1 − c·s, which only implies a finite mean. Treat the 1/t² statement as an intermediate-range description,
  not the ultimate tail. The survey text (§6.2) states this.
- Prop. 2 (balanced up-probability φ(n,p)) was evaluated numerically: φ(1,1)=φ(3,3)=0.5, φ(2,1)=0.6977 (arctan 0.7048),
  φ(29,14)=0.7136 (arctan 0.7137). This confirms the arctan form as a close approximation and the bid/ask orientation.
