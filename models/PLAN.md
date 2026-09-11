# Order-Book Models in Shadow — ES & NQ

**Goal:** implement the major *published* limit-order-book models, calibrate each on real CME
**MBO** order-flow data, plug each into the **shadow execution algorithm**, and backtest through
kaspar's **order-book simulator** to see which model makes shadow execute better.

The primary result is execution performance in the simulator. The statistical model-fit
diagnostics exist only to explain *why* a model helps or hurts shadow — they are not the point.

Status: **plan only.** Data (MBO PCAPs) lives on a separate machine; this repo
(`/Users/vm/obmodels`) holds the pipeline, calibration, integration, and report — portable there.

**Locked decisions:** (1) **ES first**, NQ added once the pipeline is stable (NQ = replication +
ES↔NQ transfer test). (2) **Shadow-execution backtest is the primary deliverable.**
(3) **DeepLOB (M4) included** as a signal-accuracy yardstick.
(4) **Open-source the model implementations in kaspar** (MIT, alongside the existing code) as the
reproducibility artifact backing the arXiv paper.

---

## 0. Prior art & positioning (arXiv, searched 2026-09)

**Verdict:** the exact contribution here — *hold the execution algorithm fixed, treat the
published order-book model as the ablated variable, and rank models by execution-P&L delta on
real MBO ES/NQ* — does not appear to have been published. Every component is published; the
head-to-head benchmark is the gap. This is an **empirical/benchmark** contribution, not a
theoretical one, so the framing must carry the novelty (see "Positioning" below).

### Closest prior work (must cite and differentiate)
- **Market Simulation under Adverse Selection** — arXiv:2409.12721 (Rosenbaum/Dauphine group).
  *The paper a reviewer will point at.* Same instruments (real CME MBO futures **ES, NQ**, CL, ZN,
  Apr 2024); same method skeleton (backtest a strategy, show the assumed fill/book model changes
  measured performance — perfect fills ρ=1 vs adverse-selection-aware ρ=0.2 → terminal wealth
  drops sharply). **Delta:** it ablates *two versions of one framework* (Cartea et al. SOC), not a
  lineup of published book models. We must adopt its adverse/non-adverse fill discipline and
  differentiate on **breadth of models compared + the realism-vs-execution decoupling result**.

### Landscape (grouped)
- **Book models judged on statistical realism, not execution** (the majority): Queue-Reactive
  (arXiv:1312.0563), Queue-Reactive Hawkes (arXiv:1901.08938), Compound Hawkes (arXiv:2312.08927),
  DeepLOB-meets-Queue-Reactive (arXiv:2501.08822), Bridging the Reality Gap (arXiv:2603.24137),
  order-sizes QR (arXiv:2405.18594). All evaluate by stylized-fact reproduction — none by
  execution P&L.
- **Execution/RL papers that evaluate execution but don't ablate the book model**: Optimal
  Execution with RL (arXiv:2411.06389), effect of latency (arXiv:2504.00846). They swap the
  *strategy* (TWAP/VWAP/POV/RL) inside a *fixed* environment — opposite of our design.
- **Fill-probability-under-a-model** (the mechanism we use, not the benchmark): state-dependent
  fill probabilities (arXiv:2403.02572), KANFormer survival fill-prob (arXiv:2512.05734),
  Cont–Kukanov optimal placement (arXiv:1210.1625).
- **Theory unifying model families** (no execution eval): multi-dimensional queue-reactive /
  signal-driven unified framework (arXiv:2506.11843).

### Positioning — the citable finding
Frame the paper around one question: **does statistical model realism actually translate into
execution edge?** The result is publishable if we can show the ranking of models by stylized-fact
fit **decouples** from their ranking by shadow P&L (e.g. the most "realistic" model, QR-Hawkes,
does not necessarily produce the best execution, or vice versa). That decoupling — not the models
themselves — is the contribution. Novelty is thin if framed as "we assembled known models";
strong if framed as "realism ≠ execution edge, measured head-to-head on real MBO futures".

### Verification still owed (before submission)
Read in full (only abstracts/excerpts reviewed so far): arXiv:2409.12721 and arXiv:2501.08822.
Run one more targeted sweep on "does statistical LOB realism predict execution performance" to
confirm that exact question is unanswered.

---

## 1. Models in scope

Four published book/order-flow models feed shadow's placement + fill-prob logic, over a
near-free linear baseline every heavier model must beat. One supervised ML model is added as an
accuracy reference for the directional signal.

| # | Model | Reference | Class | What it gives shadow |
|---|-------|-----------|-------|----------------------|
| B0 | **Queue-Imbalance / OFI** | Cont, Kukanov & Stoikov (2014), *The price impact of order book events*, J. Fin. Econometrics; Gould & Bonart (2016), *Queue imbalance as a one-tick-ahead price predictor* | Linear one-tick predictor | Near-zero-cost directional gate + fill-side signal. The **"must-beat" baseline** — especially strong in large-tick ES/NQ; if a heavy model can't out-execute it inside shadow, that is itself a finding. |
| M0 | **Cont–Stoikov–Talreja (CST)** | Cont, Stoikov & Talreja (2010), *A stochastic model for order book dynamics*, Oper. Res. | Zero-intelligence Markov, constant Poisson rates | Baseline fill-probability from constant rates. Deliberately naive floor. |
| M1 | **Queue-Reactive (QR)** | Huang, Lehalle & Rosenbaum (2015), *Simulating and analyzing order book data: the queue-reactive model*, JASA | State-dependent Markov; intensities λ(q) depend on queue size | Queue-position-aware fill prob and level-survival — the canonical model for 1-tick markets like ES/NQ. |
| M2 | **Multivariate Hawkes** | Bacry, Delattre, Hoffmann & Muzy (2013); Bacry & Muzy (2014) | Self/cross-exciting point process | Order-flow clustering + branching ratio → fast/slow regime; cross-excitation imbalance signal for shadow entry/pull. |
| M3 | **Queue-Reactive Hawkes (hybrid)** | Morariu-Patrichi & Pakkanen (2019), *Hybrid marked point processes*; (2022) *State-dependent Hawkes* | Hawkes intensities modulated by queue state | Clustering *and* queue-dependence in one fill-prob/pull signal. Superset of M1+M2. |
| M4 | **DeepLOB** | Zhang, Zohren & Roberts (2019), *DeepLOB*, IEEE TSP | Supervised CNN+LSTM classifier | Mid-move direction accuracy ceiling; optional directional gate for shadow. |

**Excluded** (can add later): Cont–de Larrard diffusion-limit queueing model; modern generative
LOB models (autoregressive S5 arXiv:2309.00638, diffusion arXiv:2509.05107) — heavy training,
overlap DeepLOB; compound/order-size QR (arXiv:2405.18594) — better as an order-size *variant* of
M1/M2 than a standalone model; agent-based / SantaFe; rough-vol book models; neural-Hawkes. Note
**LOB-Bench (arXiv:2502.09172)** as the standard realism benchmark — cite in related work.

### Reference implementations (open source)

Existing GitHub code to reuse or study per model. Most are academic/small repos, not production —
calibrate against them, don't depend on them. Reuse where noted; implement B0 and CST ourselves.

| Model | Repo | Reuse note |
|-------|------|-----------|
| M1 QR | [jvallikivi/lobsim](https://github.com/jvallikivi/lobsim) | Experimental LOB sim built on Huang–Lehalle–Rosenbaum. Closest reference impl. |
| M1 QR | [TomasEspana/qrm_optimal_execution](https://github.com/TomasEspana/qrm_optimal_execution) | Implements QR "Model I" + RL execution — relevant since it also does execution. |
| M2 Hawkes | [X-DataInitiative/tick](https://github.com/X-DataInitiative/tick) | **Use directly** for multivariate Hawkes MLE + simulation — don't hand-roll the estimator. |
| M2/M3 Hawkes | [sohaibelkarmi/High-Frequency-Trading-Simulator](https://github.com/sohaibelkarmi/High-Frequency-Trading-Simulator) | C++ LOB engine + marked multivariate Hawkes generator (C++ **and** Python). Code for arXiv:2510.08085. **Architecturally closest to kaspar** — study it. |
| M2 Hawkes | [ZhangMian-CentraleSupelec/…-Limit-Order-Book](https://github.com/ZhangMian-CentraleSupelec/High-Frequency-Data-and-Limit-Order-Book) | Smaller academic Hawkes-sim + LOB analysis. |
| M4 DeepLOB | [zcakhaa/DeepLOB-…](https://github.com/zcakhaa/DeepLOB-Deep-Convolutional-Neural-Networks-for-Limit-Order-Books) | **Authors' official** PyTorch/TF impl — use directly. |
| M4 DeepLOB | [Jeonghwan-Cheon/lob-deep-learning](https://github.com/Jeonghwan-Cheon/lob-deep-learning) | Clean reimpls of DeepLOB, TransLOB, DeepFolio in one repo. |
| B0, M0 | — | No canonical repo; implement ourselves (few lines each). |

**LOB simulators / Tier-D environment references** (kaspar's own SOM is our simulator, but these
are the standard comparators): [ABIDES](https://github.com/abides-sim/abides) (agent-based,
widely cited), [DrAshBooth/PyLOB](https://github.com/DrAshBooth/PyLOB) (fast Python matching
engine), [JAX-LOB](https://arxiv.org/pdf/2308.13289) (GPU, for large-scale RL),
[LeonardoBerti00/DeepMarket](https://github.com/LeonardoBerti00/DeepMarket) (generative/diffusion
on ABIDES — the excluded generative family).

**Not found on GitHub:** any implementation of the plan's actual contribution — published models
ablated *inside a fixed execution algorithm*, ranked by execution P&L on real MBO futures. The
components exist; the benchmark does not.

### Model primers — how each one works

Read this first if the models are unfamiliar. Each primer covers the core idea, the **state space**
(how many states), the **dynamics / rates**, **how the probabilities are actually computed**,
**calibration**, and what shadow gets. They are ordered simplest → most complex.

**The one job every model has.** shadow rests a passive order at a price and must decide, tick by
tick: *keep it here, or cancel it?* Every model answers the **same** question — the expected value
of holding the order — and differs only in how it estimates the pieces. For a resting **buy** at
price p:

> **EV_keep ≈ P(fill) × E[ mid_after_fill − p | fill ]**
> — *(chance I actually get filled here) × (which way the market goes once I'm filled)*.

The second term is the catch: you tend to get filled *exactly* when an informed seller is running
the price down, so a fill can arrive **with** an adverse move. **Keep the order while EV_keep > 0**
(fills are likely benign and the drift is in your favour); **cancel the moment EV_keep turns
negative** (you'd be filled into a market moving against you). Everything below is just a different
way to estimate **P(fill)** and the conditional move **E[·|fill]** — that is the entire point of
plugging a model into shadow.

#### B0 — Queue-Imbalance / OFI (linear predictor)

**Core idea.** The cheapest signal that works: the sizes resting at the top of book already tell you
which way the next tick is likely to go. If far more size is queued on the bid than the ask, buyers
outnumber sellers at the touch and the mid tends to tick up; the reverse for an ask-heavy book.
There is no state machine and no dynamics — just a regression from a couple of instantaneous
features to the next move.

**Features.** Two, both read straight off the book:
- **Queue imbalance** `I = (Q_bid − Q_ask) / (Q_bid + Q_ask) ∈ [−1, 1]` at the best level (optionally
  a depth-weighted version over the top few levels).
- **Order-flow imbalance (OFI)** — the running signed change in best-level size over a short window,
  the Cont–Kukanov–Stoikov event-flow variable. Each L1 update contributes `+q` when size is added to
  the bid (or the bid ticks up), `−q` when the bid is consumed/cancelled/ticks down, and the mirror
  with opposite sign on the ask. Summed over the window, OFI is *net buying pressure at the touch*,
  and the mid change over that window is very nearly **linear** in it (intraday R² can reach ~0.6–0.7
  in large-tick names).

**The model.** `E[ΔMid over horizon h] ≈ β_I·I + β_OFI·OFI (+ intercept)`, or, for a direction/veto,
a logistic `P(up) = σ(w₀ + w₁·I + w₂·OFI)`. That is the whole model — two coefficients. "How the
probability is computed" is just plugging the current `I, OFI` into the fitted line/logit.

**Calibration.** Ordinary least squares (or logistic regression) of the realised next-`h` mid move on
`(I, OFI)`, fit on training events and **refit per regime** (RTH / ETH / high-vol) because the slope
changes. No iterative estimation, no state space — seconds to fit.

**Why it's strong on ES/NQ, and its ceiling.** Imbalance predicts best exactly when the tick is large
relative to volatility, so the price is "pinned" at the touch and the queues carry the information —
the ES/NQ regime. But it is memoryless beyond the window, linear, and has **no notion of your queue
position or of getting filled**: it only knows direction.

**Keep/cancel (what shadow gets).** B0 supplies the **E[move|fill]** term directly (the predicted mid
move, signed to your side) and essentially nothing for **P(fill)**. So inside shadow it is a fast
**toxicity veto**: bid-heavy (I>0) ⇒ up-drift ⇒ resting on the bid is benign ⇒ keep; ask-heavy (I<0)
⇒ down-drift ⇒ a bid fill would be adverse ⇒ cancel. It is the baseline every model below must beat.

#### M0 — Cont–Stoikov–Talreja (zero-intelligence Markov)

**Core idea.** Model the whole book as a system of queues receiving completely random (Poisson) order
flow at *constant* rates, then read off *probabilities of book events* — will this level empty, will
my order fill, which way will the price move — from the mathematics of that random system. It is
"zero-intelligence" because nothing reacts: the rates never change.

**State space.** The state is the vector of queue sizes at each price level, `X = (n₁, …, n_K)` for the
`K` tracked levels on each side (measured relative to the best/reference). Each `n_i ∈ {0,1,2,…}`, so
the raw state space is `ℕ^{2K}` — countably **infinite**. In practice you truncate each queue at `Q_max`
and track `k` levels a side, giving on the order of `(Q_max+1)^{2k}` states — which explodes
combinatorially (e.g. 100 lots × 4 levels a side ≈ 100⁸). That blow-up is exactly why you do **not**
brute-force the full chain but exploit its structure below.

**Dynamics / rates.** Three independent Poisson streams act on each level `i` (distance from the
opposite best):
- **limit order** at rate `λ(i)` → `n_i → n_i + 1`;
- **market order** at rate `μ` hits the opposite best → best queue `− 1`;
- **cancellation**: each resting order leaves at rate `θ(i)`, so a level holding `n_i` orders cancels
  at total rate `n_i·θ(i)` → `n_i → n_i − 1`.
Every rate is constant or linear in `n_i`, so each queue is a **birth–death process** (birth `λ`, death
`μ + n·θ`) and the full book is a continuous-time Markov chain (CTMC) with generator `Q`. A **price
move** happens when a best queue hits 0 (that level is exhausted, the best rolls one tick) — the event
that couples the queues.

**Computing the probabilities.** Everything shadow needs is a **first-passage / hitting probability** of
this CTMC, obtainable three ways (increasing generality):
1. **Single queue, closed form.** One queue is an M/M/1-type birth–death chain; the time to hit 0 from
   size `n`, and the probability it grows vs shrinks, have analytic Laplace-transform expressions (the
   CST results). An order's fill time is the time for the queue ahead of it to drain.
2. **Coupled top-of-book, linear solve.** "P(ask empties before bid)" (= P(up move)) is a **harmonic
   function** `h` of the generator: solve `Q·h = 0` with `h = 1` on `{Q_ask = 0}` and `h = 0` on
   `{Q_bid = 0}`. That's a sparse linear system on the truncated 2-D grid — a discrete Dirichlet
   problem, the continuous analogue of "which axis does the random walk hit first."
3. **Monte Carlo.** Simulate the CTMC forward (exponential inter-event times; pick the event with
   probability proportional to its rate) and count outcomes — used when the exact routes are too big.
The atom under all three: for competing Poisson streams with rates `a, b`, the next event is type A
with probability `a/(a+b)`; chaining that over the birth–death steps gives the hitting probabilities.

**Calibration.** Trivial MLE — each rate is `#events / exposure-time`: limit adds at level `i` over
time for `λ(i)`, market orders over total time for `μ`, cancels over (time × size exposed) for `θ`.

**Keep/cancel (what shadow gets).** Both EV terms come from the same first-passage: **P(fill)** =
P(market-sells + cancels-ahead drain the queue to my position before the level's price moves), and
**E[move|fill]** from P(which queue empties first). Because the rates are constant, the call is a
**static function of the current queue sizes** — no memory.

**Limitations.** Constant rates give exponential inter-arrival times and near-geometric queue-size
distributions that do **not** match real books (real queues are hump-shaped), plus no clustering and no
reaction to imbalance. That's the point — it's the floor M1–M3 must beat.

#### M1 — Queue-Reactive (Huang–Lehalle–Rosenbaum)

**Core idea.** Keep CST's queueing picture but fix its worst lie: in real markets the order-arrival and
cancellation rates depend on **how full the queue already is**. Traders pile into thin queues and pull
out of overcrowded ones; big queues get cancelled faster than they trade. Make the rates functions of
the queue size and the model suddenly reproduces the real book.

**State space.** Same shape as CST — a vector of queue sizes `X = (q_{−K}, …, q_{−1}, q_1, …, q_K)` at
`K` levels each side around a **reference price** `p_ref`, each `q_i ∈ {0,…,Q_max}`. What's new is a
**two-timescale** structure: (i) *fast* — the queues evolve with `p_ref` held fixed; (ii) *slow* — when
the best queue depletes / the book is one-sided, `p_ref` updates by a tick and the state is re-centred.
Centring is what keeps the model stationary and the state space bounded.

**Dynamics / rates.** Each queue `i` is again a birth–death chain, but with **state-dependent**
intensities `λ_limit,i(q)`, `λ_cancel,i(q)`, `λ_market,i(q)` — arbitrary functions of the current size
`q`, not constants. In "Model I" the queues are independent given `p_ref`; Models II/III let the
intensities depend on the whole book (e.g. on imbalance), coupling the queues.

**Computing the probabilities.** The headline object is the **stationary distribution of a single
queue**, which for a birth–death chain is the closed-form product `π(q) ∝ Π_{j=1}^{q} λ(j−1)/μ(j)`,
with `λ(·)` the arrival intensity and `μ(·) =` cancel+market departure intensity. Because `λ, μ` depend
on `q`, `π` comes out **hump-shaped**, matching the empirical queue-size histogram — the thing CST's
geometric distribution cannot do, and their central validation. Fill and level-survival probabilities
are again first-passage on the birth–death chain, now with the `q`-dependent rates: the same hitting-
time recursions with `λ(q), μ(q)` in place of constants (a tractable tridiagonal linear system per
queue). Price-move probability comes from the reference-price mechanism plus which best queue reaches 0.

**Calibration.** **Non-parametric binning.** For each queue size `q`, estimate
`λ_limit(q) = (# limit adds that occurred while the queue held q lots) / (time the queue spent at q)`,
and likewise for cancels and market orders — the rate-vs-size *curves* straight from the L3 stream.
Then check the implied `π(q)` against the empirical histogram.

**Keep/cancel (what shadow gets).** Genuinely **queue-position-aware**: **P(fill)** and level-survival
use the size-dependent drain rates (a deep queue that cancels fast has a very different fill profile
than CST assumes), and **E[move|fill]** comes from the reference-price / depletion mechanics. shadow
holds orders in queues that statistically hold and fill benignly, cancels in queues about to deplete
underneath it. The workhorse model for a 1-tick market.

**Limitations.** Still Markovian in the *queue state* — **no time-clustering / self-excitation** (a
burst and a lull with the same queue sizes look identical), and Model I assumes independent queues. M2
adds the time dimension; M3 adds both.

#### M2 — Multivariate Hawkes (self/cross-exciting point process)

**Core idea.** Neither Markov model has memory: they react to the *current* queue sizes but not to the
fact that a burst just happened. Real order flow **clusters** — a trade makes the next trade more
likely, a cancel triggers more cancels. A Hawkes process encodes exactly that: every event temporarily
raises the intensity of future events.

**State / representation.** Not a discrete queue chain — a **point process** on event times, one
"dimension" per event type `m ∈ {market-buy, market-sell, limit-add-bid, limit-add-ask, cancel-bid,
cancel-ask, …}` (`M ≈ 4–12`). The tracked object is the **intensity vector** `λ(t) ∈ ℝ^M`; with
exponential kernels this vector is itself **Markov** — a *continuous* state, not a finite one.

**Dynamics / equations.** `λ_m(t) = μ_m + Σ_n Σ_{t_i^n < t} α_{mn}·e^{−β_{mn}(t − t_i^n)}`. Read:
type-`m` intensity = baseline `μ_m` + a decaying bump `α_{mn}` for every past type-`n` event, fading at
rate `β_{mn}`. The exponential kernel gives the crucial **O(1) online recursion**: between events each
`λ_m` decays toward `μ_m` (`dλ_m/dt = −β(λ_m − μ_m)`), and at a type-`n` event every `λ_m` jumps by
`α_{mn}`. So the whole state updates with a decay-multiply and an add per event — no history re-scan,
which is what makes it viable in shadow's hot path. **Cross-excitation** (`α_{mn}`, `m≠n`) is the
tape-reading part: a market-buy lifting the ask excites more buying and more ask-side cancels.

**Branching ratio / regime.** Let `Γ_{mn} = ∫_0^∞ φ_{mn} = α_{mn}/β_{mn}`. The **branching ratio
`n* = spectral radius(Γ)`** is the expected number of child events per parent; stationarity needs
`n* < 1`. `n*→1` = near-critical, self-igniting, **fast market**; small `n*` = mostly exogenous, **slow
market**. This single number is shadow's fast/slow regime flag.

**Computing the probabilities.** (1) **Likelihood** for calibration:
`log L = Σ_i log λ_{m_i}(t_i) − Σ_m ∫_0^T λ_m(s) ds`; with exponential kernels both the sum and the
compensator integral have O(N) recursions — maximise over `{μ, α, β}`. (2) **Fill probability**: for a
resting bid, the event "a market-sell (or a cancel that reaches me) fires" *is* the fill, so the
market-sell intensity is the instantaneous **fill hazard** and
`P(fill in [t, t+h]) = 1 − exp(−∫_t^{t+h} E[λ_sell(s)] ds)`. (3) **Direction**: compare aggressive-buy
vs aggressive-sell expected intensities over the horizon; a fill coinciding with a self-excited sell
burst is a toxic fill.

**Calibration.** MLE of `μ` (M), `α` (M²), `β` (M²) — use **sum-of-two-exponentials** kernels for a
materially better fit, and the `tick` library rather than hand-rolling. Handle the ~10:1
add/cancel-to-trade churn by fitting each stream separately and survival-weighting ephemeral quotes.

**Keep/cancel (what shadow gets).** **P(fill)** = the market-order fill hazard on your side;
**E[move|fill]** = whether that flow is a directional sweep (via cross-excitation). A spike in
self-excited sell intensity ⇒ "about to be filled *because* a sweep is running me over" ⇒ EV_keep < 0
⇒ cancel; a calm book ⇒ benign fills ⇒ keep.

**Limitations.** Basic form has **no queue size** — it knows *when* events cluster, not *how deep* the
queue is (no true queue position). Kernel/dimension choice matters; churn can inflate excitation. M3
fixes the missing queue state.

#### M3 — Queue-Reactive Hawkes (hybrid)

**Core idea.** M1 knows the queue state but not time-clustering; M2 knows time-clustering but not the
queue state. M3 is the union: **Hawkes intensities that are also modulated by the current book state.**

**State / representation.** A coupled system: the **intensity vector** `λ(t) ∈ ℝ^M` (as in M2) *and* a
discrete **book/queue state** `X(t)` (queue sizes / imbalance bucket, as in M1). The two interact both
ways — events fire according to `λ`, each event moves the queues (updates `X`), and `X` in turn
modulates `λ`. With exponential kernels + a finite discretisation of `X`, the pair is still Markov (a
"Markov-modulated Hawkes" / hybrid marked point process, Morariu-Patrichi–Pakkanen).

**Dynamics / equations.** `λ_m(t) = φ_m(X(t))·[ μ_m + Σ α_{mn} e^{−β_{mn}(t − t_i^n)} ]` — the M2
intensity scaled by a **state factor** `φ_m(X)` that says "in this queue regime, type-`m` events are
faster/slower" (additive state terms are an alternative parametrisation). So both "an event just fired,
expect more" *and* "behaviour changes because the queue is nearly empty / very full" live in one
intensity.

**Computing the probabilities.** Same likelihood form as M2, but `λ` now carries `φ(X(t))`, so you must
**replay `X(t)` along the event path** while evaluating the compensator — heavier but still O(N).
Simulation is by Ogata **thinning** (propose at the current intensity upper bound, accept with prob
`λ/λ̄`, update `X` on each accepted event). Fill and direction probabilities are read off exactly as in
M2, but conditioned on the live queue state as well as the excitation.

**Calibration.** Initialise `φ(X)` from M1's state-binned rates and `{α, β}` from M2's kernels, then
**joint MLE**. It has the most parameters of any model here, so judge it strictly **out-of-sample**
(BIC / Tier-C / shadow P&L), never on in-sample likelihood — the overfitting risk is real.

**Keep/cancel (what shadow gets).** The richest signal: **P(fill)** and **E[move|fill]** conditioned on
**both** the excitation state and the queue state simultaneously — the most complete EV_keep. Whether
that extra fidelity actually improves shadow's execution over the simpler M1/M2 is precisely the
question the benchmark is built to answer.

**Limitations.** Most parameters, most compute, highest overfitting risk; the online state carries both
the Hawkes intensities and the discretised book state, so its C++ hot-path update is the heaviest here.

#### M4 — DeepLOB (supervised deep net)

**Core idea.** Drop all queueing / point-process structure and just **learn** the map from recent book
snapshots to the next price move, letting a neural net discover whatever patterns predict direction. It
is not a book model and cannot simulate a book — it's a classifier used as an accuracy yardstick.

**Input / representation.** A tensor of the last `T` book snapshots (typically `T ≈ 100`) × top **10
levels** × {price, size} on both sides — i.e. a `T × 40` "image" of the order book over a short window.
No hand-built features; the raw ladder is the input.

**Architecture.** Stacked **convolution** blocks first combine price+size within a level, then aggregate
across the 10 levels (learning imbalance-like and micro-price-like features); an **Inception** module
mixes several receptive-field sizes; an **LSTM** models how those features evolve across the `T`
snapshots; a final softmax outputs `P(down), P(flat), P(up)` for the mid over a forward horizon `k`.

**Computing the probability.** A **forward pass** — no state machine, no transition rates. The softmax
*is* the probability; "how it's computed" is matrix multiplies through the trained weights.

**Calibration / training.** Supervised cross-entropy against labels from the smoothed future mid move
(up/flat/down by a threshold `α`), trained by SGD. Needs a lot of data and **strict session-level
train/val/test splits** — snapshot autocorrelation makes leakage easy and inflates accuracy.
Benchmarks: FI-2010, or our own ES/NQ.

**Keep/cancel (what shadow gets).** Supplies only the **E[move|fill]** direction term (a learned
P(up/down)); it has **no fill model**. shadow either combines it with another model's P(fill) or uses
it as a **veto gate** — DeepLOB says "down" with high confidence ⇒ cancel the bid regardless of fill
odds. Its real job: answer *"how much predictable signal are the interpretable models leaving on the
table?"*

**Limitations.** Black box (hard to attribute a decision), data-hungry, leakage-prone, inference
latency in the hot path, and no notion of queue position or of your own order — direction only.

---

## 2. Data: MBO L3 from kaspar

The PCAPs are **MBO** (order-by-order, full L3). kaspar reconstructs them via the MBO path
(`TachBook` / `handler_if` MBO handlers) and `BinRecorder` already dumps the per-order event
stream through `bfile::write_l3` — gzip'd `l3_mbo_v2_packed_t` records carrying `transactTime`,
`orderUpdateAction` (add/modify/delete/trade), `securityID`, `orderID`, `priority`, `pxd`,
`displayQty`, `side`, `endOfEvent`. That is exactly the per-order stream every model needs, and it
gives **true queue position** — no MBP approximation anywhere in this project.

### Pipeline
```
MBO PCAP (ES ch.310 / NQ ch.318)
   → kaspar replay (MDP3 → handler_if MBO → book)
   → BinRecorder (bfile::write_l3, gzip'd l3_mbo_v2_packed_t)      [exists]
   → NEW: l3 → columnar exporter (Parquet)                        [D1]
   → NEW: Python event reader + book re-walker                    [D2]
```

- **D1** — small C++ tool linking `bfile::read/write_l3` that streams the gzip record file to
  Parquet. Reuses the project's own decoder; deterministic.
- **D2** — Python reader over the Parquet, plus a **book re-walker** that replays the L3 events to
  attach, at each event: `mid`, `spread`, `level_index` (ticks from mid), `queue_size_before`, and
  **per-order queue rank** (position ahead in FIFO priority). This queue rank is the feature that
  makes the whole exercise possible and is available because the data is MBO.

**Canonical event schema:** `ts, instrument, event_type ∈ {LIMIT_ADD, CANCEL, TRADE, MODIFY},
side, price, level_index, size, order_id, priority, queue_size_before, queue_rank, mid, spread`.

### Coverage
- ≥ 20 sessions ES first (NQ later). Split train/val/test by **whole sessions** (no intra-session
  leakage).
- Fit and evaluate **RTH and ETH/overnight separately** — intensities and branching ratios differ.
- Include ≥ 1 high-vol day (CPI/FOMC) for the fast-market regime.

---

## 3. Integrating each model into shadow (the core work)

shadow (`light22`) already places passive orders on real flow and pulls them on
position/drift/attached-execution triggers. Each model plugs in through a **common signal
interface** the light queries per book event:

```
ModelSignal {
    fill_prob(side, level, queue_rank, book_state) -> p      // will my passive order fill first?
    level_survival(side, level, horizon) -> p                // will this level hold / break?
    imbalance_pull(book_state) -> bool                       // toxic flow -> cancel now
}
```

- shadow's **placement** decision gates on `fill_prob` / `level_survival` (only rest where the
  model says the queue reinforces and fill is likely-benign).
- shadow's **cancellation** trigger adds `imbalance_pull` (Hawkes cross-excitation / QR depletion)
  on top of the existing position/drift triggers.
- Each model implements the same interface, so swapping B0→M0→M1→M2→M3(→M4 gate) is a config switch.
  Two baselines: **shadow as-is** (no model / current heuristics, from `tech_reports/shadow_pov`)
  and **B0** (linear queue-imbalance/OFI) — the cheap signal every heavier model must beat.

Modeling code is Python for calibration; the online signal used inside the light is ported to
**C++** (in the light directly) so it runs in the simulator on the same replay path. No Rust.

---

## 4. Calibration methodology

| Model | Estimation | Key parameters | Gotchas |
|-------|-----------|----------------|---------|
| **B0 OFI/imbalance** | Linear/logit regression of one-tick mid-move on best-level OFI and queue imbalance | regression coeffs; lookback | Near-free. Watch tick-clustering; use as the "must-beat" reference, calibrate per regime. |
| **M0 CST** | Closed-form: rate = count / time, per event type per level | λ_limit(i), λ_cancel(i), λ_market, mean sizes | Trivial floor. |
| **M1 QR** | Non-parametric: bin events by queue size q per level; estimate λ_limit(q), λ_cancel(q), λ_market(q); reference-price layer (HLR §3–4) | Intensity curves per level; invariant queue dist.; θ | Validate the invariant queue distribution vs empirical (their headline result). Multi-level coupling. |
| **M2 Hawkes** | MLE, exponential then sum-of-exp kernels (or Bacry–Muzy Wiener–Hopf). 4-dim {market±,limit±} → 8–12-dim incl. cancels | μ, α, β matrices; **branching ratio n\*** | Sum-of-2-exp materially better. ~10:1 add/cancel:trade churn — fit per-stream, survival-weight ephemeral quotes. n\* = fast/slow regime metric. |
| **M3 QR-Hawkes** | State-dependent MLE: Hawkes intensity × queue-state factor (Morariu-Patrichi–Pakkanen `mpoints`) | Hawkes kernels + per-state multipliers φ(q) | Init from M1 states + M2 kernels. Superset — gate on OOS BIC to avoid overfit. |
| **M4 DeepLOB** | Supervised CNN+LSTM on 10-level book snapshots, label = mid move over horizon h | net weights | Session-level split. Accuracy reference / optional directional gate, not a fill model. |

Fit on train, tune on val, freeze, evaluate once on test. Log config + git SHA per run.

---

## 5. Evaluation — shadow execution is the headline

### Primary: shadow execution backtest (simulator)
For each model (and the baseline), run shadow through the kaspar order-book simulator (SOM +
queue-aware fills) on the same replayed ES sessions. Report, with confidence intervals across
sessions:
- **P&L** (per contract, per session) and Sharpe of the execution edge,
- **fill ratio** and **passive-fill share** (spread captured vs spread paid),
- **adverse selection**: mark-out at +1s/+5s/+30s after fill,
- **queue outcomes**: realized fill queue-rank distribution, toxic-fill rate,
- **cancel efficiency**: pulls that avoided a sweep vs pulls that missed free fills.

Head-to-head table: baseline vs M0–M3 (M4 as optional directional gate), per regime (RTH/ETH).

### Supporting diagnostics (explain the execution result, not the point)
- **Signal accuracy (Tier C):** fill-probability calibration curves, level-break AUC, mid-move
  direction accuracy/AUC incl. M4. Directly ties a model's signal quality to its shadow P&L.
- **Model fit (Tier A/B):** OOS log-likelihood / AIC-BIC and time-rescaling residuals (M0–M3);
  simulated-vs-empirical stylized facts (queue-size dist, signature plot, flow autocorrelation).
  Used to diagnose *why* a model's signal helps or fails — a cheap pre-filter before the expensive
  simulator run.

**Cross-instrument (ES-first):** everything built on ES; NQ added later as replication + ES↔NQ
parameter-transfer (fit ES, run NQ, and vice versa).

---

## 6. Phased milestones (ES-first)

- **Phase 0 — Data spike.** Export one ES session MBO PCAP → Parquet (D1) and build the reader +
  book re-walker with queue rank (D2). Sanity-check event counts, queue-rank distribution,
  add/cancel/trade ratios. **Exit:** clean canonical ES event file with per-order queue rank.
- **Phase 1 — Signal interface + shadow harness + B0/CST.** Define the `ModelSignal` interface, wire
  the shadow simulator backtest, and run **shadow-as-is vs B0 (queue-imbalance/OFI) vs M0 (CST)** —
  both cheap models land here and establish the two baselines. **Exit:** end-to-end execution P&L
  table (shadow-as-is vs B0 vs CST) on ES.
- **Phase 2 — M1 Queue-Reactive.** Calibrate λ(q), validate invariant queue dist., plug into shadow.
  **Exit:** QR vs baseline vs CST execution comparison + signal-accuracy diagnostics.
- **Phase 3 — M2 Hawkes.** Kernels + branching ratio + `imbalance_pull`, plug into shadow.
  **Exit:** Hawkes vs QR execution comparison on ES.
- **Phase 4 — M3 Hybrid.** Only if QR and Hawkes each help shadow in different regimes. Plug in.
  **Exit:** does the superset actually improve shadow P&L OOS (BIC-honest)?
- **Phase 5 — M4 directional gate.** Add DeepLOB as an optional entry gate + as the Tier-C accuracy
  ceiling. **Exit:** does a learned directional filter add to the best book model's shadow P&L?
- **Phase 6 — NQ replication + transfer.** Re-run best models on NQ; ES↔NQ transfer tests.
  **Exit:** cross-instrument execution comparison.
- **Phase 7 — Write-up.** Execution tables (baseline vs each model, per regime, ES & NQ), signal
  diagnostics, transfer, limitations. Candidate `tech_reports/` paper alongside `shadow_pov`.
- **Phase 8 — Open-source release.** Land the model implementations in kaspar and tag the release
  referenced by the arXiv paper (see §7).

---

## 7. Open-source deliverable

The model implementations ship in **kaspar** (already MIT, © Vincent Mayeski / M2 Tech) as the
reproducibility artifact behind the paper. What goes public:

- **C++ `ModelSignal` implementations** in the light (CST, QR, Hawkes, QR-Hawkes) + the shadow
  integration — the online path that runs in the simulator.
- **Python calibration + evaluation harness** (this `obmodels` repo, or a `kaspar/research/` subdir)
  — fitters, the Tier A/B/C diagnostics, and the shadow-execution backtest driver.
- **The L3→Parquet exporter (D1) + reader/book-re-walker (D2)** so others can reproduce the event
  stream from their own MBO captures.
- **Configs + a runbook**: session lists, train/val/test splits, one-command repro of the headline
  execution table. Pin the exact git SHA the paper's numbers come from.

Out of scope for release: raw CME MBO PCAPs (licensed data — ship the schema and a synthetic or
tiny sample instead), iLink credentials, any live-trading config.

**Checklist:** MIT headers on new files (match existing kaspar convention); no proprietary data in
the repo or its history; README pointing paper→code→data-schema; a tagged release + archival DOI
(e.g. Zenodo) cited in the arXiv submission.

---

## 8. Risks & open items

- **Fill-prob ground truth / self-impact.** shadow's own orders perturb the queue; the simulator's
  queue-aware SOM must handle self-impact so the counterfactual fill is honest. Specify precisely
  before trusting Phase 1 P&L.
- **Non-stationarity / regime.** One parameter set won't hold across RTH/ETH/high-vol — fit and
  report per regime; pooled numbers hide it.
- **Overfitting M3 (and M4).** More parameters ≠ better shadow P&L. Gate on OOS, not in-sample fit.
- **Add/cancel churn (~10:1).** Ephemeral quotes distort intensities; survival-weight in calibration.
- **Online cost inside the light.** The signal runs per book event in the sim's hot path — Hawkes
  state must be O(1) incremental (decay-multiply + add), not a history re-sum.

The online signal is **C++**, implemented directly in the light. No Rust anywhere in the project.
