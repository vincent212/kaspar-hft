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

**State space — what one "state" is.**
Picture the price axis as a fixed ladder of tick-spaced slots. *Price is the slot's position on the
ladder, not a number we store*; what we store at each slot is its **queue size** — how many lots are
resting there right now. So one *state* of the model is a snapshot of the whole book: a list giving
the queue size at every slot.

```
X = (n_1, n_2, …, n_K)
```

where:

- `X`   — one complete book configuration (one "state").
- `K`   — how many price levels we track on each side (e.g. 5 or 10).
- `n_i` — the number of lots resting at the i-th price level; `n_i` is 0, 1, 2, …
- `i`   — the level index. **This is where price lives:** `i = 1` is the best price, `i = 2` is one
  tick behind it, and so on, all measured relative to a moving reference price `p_ref`.

So **sizes are the values `n_i`; prices are the positions `i`.** Example: best bid holding 40 lots, one
tick behind it 120 lots, best ask 12 lots — those are three of the numbers in `X`. A market buy that
takes all 12 ask lots sets that entry `12 → 0`, the level is gone, and the ladder shifts up one slot
(the reference price moved).

*How many states are there?* Each `n_i` can be any non-negative integer, so the exact set of states is
infinite. To actually compute, cap each queue at a maximum size `Q_max` and track `k` levels a side.
The number of distinct states is then

```
N_states ≈ (Q_max + 1) ^ (2k)
```

where:

- `Q_max`    — the largest queue size we allow (e.g. 500 lots).
- `k`        — levels tracked per side; `2k` — total tracked levels (both sides).
- `N_states` — the number of possible book configurations.

Even small numbers explode: `Q_max = 100`, `k = 4` gives `101^8 ≈ 10^16` states — far too many to list
one-by-one. That is why we never enumerate the chain; we use its structure (below) to get the single
probability we want without touching every state.

**Dynamics — how the book changes over time.**
Only three things ever happen to a queue, and each is a **Poisson process** — a stream of random events
arriving at a steady average rate, the timing otherwise memoryless ("rate `μ`" means, on average, `μ`
events per second):

1. A **limit order** joins a level → that queue grows by one lot. Arrival rate `λ(i)` (may depend on
   the level `i`).
2. A **market order** hits the best level on the opposite side → that best queue shrinks by one lot.
   Arrival rate `μ`.
3. A **cancellation** removes a resting order → the queue shrinks by one lot. Each resting order is
   cancelled independently at rate `θ`, so a level holding `n` lots is cancelling at total rate
   `n × θ` (more orders resting ⇒ cancellations happen faster).

Track a single queue: it steps **up** by one when a limit order arrives, **down** by one when a market
order or a cancellation removes a lot. Its up-rate and down-rate are

```
up-rate   (a lot joins)  :  b(n) = λ
down-rate (a lot leaves) :  d(n) = μ + n·θ
```

where:

- `n` — current queue size (lots).
- `λ` — limit-order arrival rate.
- `μ` — market-order rate.
- `θ` — per-order cancellation rate.

A process that only steps up or down by one, with an up-rate and a down-rate, is a **birth–death
process** (births = orders arriving, deaths = orders leaving). Stack all the queues together and the
whole book is a **continuous-time Markov chain (CTMC)**: *Markov* = the future depends only on the
current state `X`, not on the path that led there; *continuous-time* = events can occur at any instant.
The table listing every jump rate between states is the **generator matrix** `Q` (entry `Q[X → X']` =
the rate of jumping from configuration `X` to `X'`).

One rule links the queues: **when a best queue reaches 0, that price level is empty, the best price
rolls one tick, and `p_ref` updates.** A queue hitting zero is exactly what turns queue dynamics into
*price* moves.

**Computing the probabilities — the formulas, with inputs and outputs.**
Everything shadow needs is a **first-passage probability** (a.k.a. hitting probability): the chance the
system reaches one target before another — e.g. "does the ask queue empty before the bid queue?" (an
up-move). Three ways to get it, simplest first.

*Method 1 — one queue, closed form (gambler's ruin).* Starting from size `n`, does the queue hit `0`
(empty) before it grows to a barrier `N`? For a birth–death process this is the classic **gambler's-
ruin** formula. Let `r = d / b` be the down-rate-to-up-rate ratio (rates taken roughly constant). Then

```
P(empty before reaching N | start at n)  =  (r^N − r^n) / (r^N − 1),    for r ≠ 1
```

where:

- `n` — starting queue size.  *(input)*
- `N` — upper barrier size.   *(input)*
- `r = d/b` — down/up rate ratio; `r > 1` means the queue tends to drain.  *(input)*
- *output* — the probability the queue empties before it ever reaches `N`.

Sanity check: `n = 0` gives probability `1` (already empty); `n = N` gives `0`. The matching *fill-time*
question ("how long until the queue ahead of my order drains?") uses the same birth–death first-passage
math via a **Laplace transform** (a standard integral transform that turns a timing question into
algebra). With cancellations the down-rate `d = μ + nθ` depends on `n`, so the exact answer replaces the
single ratio `r` with the general birth–death first-passage sum; gambler's ruin is the clean constant-
rate illustration.

*Method 2 — two queues, linear solve.* Use bid size and ask size together. Define

```
h(a, b) = P(ask empties before bid | ask has a lots, bid has b lots)     # = P(up-move)
```

Memorylessness means `h` at any state equals the rate-weighted average of `h` at the states one jump
away. Collect those equations into one linear system:

```
Q · h = 0,   with   h = 1 on every state where a = 0   (ask empty → up-move happened)
                    h = 0 on every state where b = 0   (bid empty → down-move happened)
```

where:

- `Q` — the generator matrix built from `λ, μ, θ`.  *(input)*
- the two boundary conditions above.                *(input)*
- `h(a, b)` — the up-move probability for every current `(a, b)`.  *(output)*

This is a **system of linear equations** on the truncated `a`–`b` grid (solved like any sparse linear
system). A function satisfying `Q·h = 0` is called **harmonic**: its value at each point is the average
of its neighbours' — the same math as "which wall does a random walk hit first."

*Method 3 — Monte Carlo (simulation).* When the grid is too large to solve exactly, **simulate**: from
the current state draw the time to the next event (exponential, rate = sum of all active rates), pick
which event fired (an event of rate `a` fires with probability `a / Σrates`), update the queues, repeat
until a best queue empties. Then

```
P(up-move) ≈ (number of runs where the ask emptied first) / (total runs M)
```

where:

- the current state and the rates `λ, μ, θ`.  *(input)*
- `M` — number of simulation runs.            *(input)*
- *output* — a Monte-Carlo estimate whose error shrinks like `1/√M`.

*Monte Carlo* just means "estimate a probability by random simulation and counting." The single fact
under all three methods: for two independent Poisson streams with rates `a` and `b`, the next event
comes from the first with probability

```
P(next event is A) = a / (a + b)
```

Chaining that one identity across the queue steps is, in the end, what every method computes.

**Calibration — getting λ, μ, θ from data.** These rates are the only unknowns, each fitted by
**maximum-likelihood estimation (MLE)** — the standard recipe of picking the parameter values that make
the observed data most probable. For Poisson rates the MLE reduces to "count events, divide by time":

```
λ̂(i) = (number of limit orders added at level i) / (total time observed)
μ̂    = (number of market orders)                 / (total time observed)
θ̂    = (number of cancellations)                 / (total lot-seconds resting)
```

where *lot-seconds resting* = summed over resting orders, how long each one stayed in the book (each lot
is exposed to cancellation for exactly that long). The hat `λ̂` denotes "the estimate of `λ`."

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

**State space.** Same picture as CST — a snapshot of queue sizes:

```
X = (q_{−K}, …, q_{−1}, q_1, …, q_K)
```

where:

- `q_i`   — lots resting at level `i`; `q_i` runs `0 … Q_max`.
- negative `i` — bid-side levels; positive `i` — ask-side levels.
- `p_ref` — the **reference price** the ladder is measured from.

What's new is a **two-timescale** design:

- *fast timescale* — with `p_ref` held fixed, each queue fills and drains (the dynamics below);
- *slow timescale* — when a best queue empties (or the book goes one-sided), `p_ref` moves one tick and
  the whole ladder is re-centred on the new mid.

Re-centring is what keeps the model **stationary** (its statistics don't drift over time) and the state
space bounded.

**Dynamics — rates that depend on the queue size.** As in CST each queue moves up (a limit order joins)
or down (a market order or cancellation removes a lot), so it is still a **birth–death process**. The
one change from CST: the three rates are no longer constants but **functions of the current size `q`**:

```
limit-order arrival rate at size q :  λ_limit(q)
cancellation rate at size q        :  λ_cancel(q)
market-order rate at size q        :  λ_market(q)
```

where:

- `q` — the queue's current size (lots).
- `λ_limit(q)` — how fast new orders join *when the queue already holds `q` lots* (usually falls as `q`
  grows — nobody wants to join a 20,000-lot queue).
- `λ_cancel(q)` — how fast lots are cancelled at size `q` (usually rises with `q`).
- `λ_market(q)` — how fast market orders eat the level at size `q`.

Group the two ways a lot can leave into one **departure rate**

```
μ(q) = λ_cancel(q) + λ_market(q)
```

so the queue climbs at rate `λ_limit(q)` and falls at rate `μ(q)`. (In the simplest "Model I" the queues
move independently once `p_ref` is fixed; richer variants — Models II/III — let each queue's rates also
depend on the rest of the book, e.g. on the bid/ask imbalance, which couples them.)

**Computing the probabilities.**
*The headline result — the queue-size distribution.* Run one queue for a long time; what fraction of the
time does it hold exactly `q` lots? That long-run fraction is the **stationary distribution** `π(q)`.
For a birth–death process it has a simple closed form — a running product of up/down rate ratios:

```
π(q) = π(0) · Π_{j=1}^{q}  λ_limit(j−1) / μ(j)
```

where:

- `π(q)` — long-run probability the queue holds `q` lots.  *(output)*
- `π(0)` — a normalising constant, fixed by making all the `π(q)` sum to 1.
- `Π_{j=1}^{q}` — "multiply the following term for `j = 1, 2, …, q`" (a product).
- `λ_limit(j−1)` — arrival rate when the queue holds `j−1` lots.  *(input)*
- `μ(j)` — departure rate when the queue holds `j` lots.          *(input)*

Because the rates vary with size, this product comes out **hump-shaped** (rises, then falls) — exactly
the shape real ES/NQ queue-size histograms have, and precisely what CST's constant-rate version (a plain
geometric decay) *cannot* reproduce. Matching this histogram is the paper's central validation.

*Fill and level-survival probabilities.* Same **first-passage** question as CST — will the queue drain to
my position / to zero before it grows — but now solved with the size-dependent rates. Let `f(q)` be the
probability the level empties starting from size `q`; it satisfies one balance equation per size:

```
μ(q)·f(q−1) + λ_limit(q)·f(q+1) = (λ_limit(q) + μ(q))·f(q),    with   f(0) = 1
```

where:

- `f(q)` — probability of reaching size 0 (level empties) starting from size `q`.  *(output)*
- `f(0) = 1` — boundary condition (already empty).
- `λ_limit(q), μ(q)` — the calibrated size-dependent rates.  *(input)*

Each equation links only `f(q−1), f(q), f(q+1)`, so this is a **tridiagonal linear system** (fast to
solve). *Price-move* probability then combines these per-queue survival odds with the reference-price
rule (which best queue empties first).

**Calibration — non-parametric binning.** You do not assume a formula for the rate curves; you read
them off the data. For each queue size `q`, count the events that happened while the queue held exactly
`q` lots and divide by the time spent at that size:

```
λ̂_limit(q) = (# limit orders added while the queue held q lots) / (time the queue spent at size q)
```

and likewise `λ̂_cancel(q)` and `λ̂_market(q)`. That gives the whole rate-vs-size *curve* directly from
the L3 event stream. Then plug the curves into the `π(q)` formula above and check it against the
empirical queue-size histogram as a fit test.

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

### Glossary

Plain-language definitions of every technical term used above, grouped by theme.

**Order-book / market-microstructure**
- **Limit order** — an order to buy/sell at a set price that *rests* in the book until matched. Adds
  depth (liquidity) to a price level.
- **Market order** — an order that executes immediately against the best resting orders. Removes depth;
  the *aggressor* in a trade.
- **Cancellation** — removal of a resting limit order before it trades.
- **Queue / queue size** — the lots resting at one price level, waiting to trade, ordered by arrival.
- **Queue position / queue rank (FIFO)** — how many lots sit *ahead* of your order in the first-in-
  first-out priority line at a level; you fill only after they do. FIFO = first-in, first-out.
- **Best bid / best ask (top of book)** — the highest buy price and lowest sell price currently resting.
- **Mid / mid-price** — the average of best bid and best ask.
- **Spread** — best ask minus best bid, usually measured in ticks.
- **Tick** — the minimum price increment (ES = 0.25 index points; a "1-tick" or "large-tick" market
  usually has the spread pinned at one tick).
- **Reference price (`p_ref`)** — a slowly-updated anchor the model measures level positions from, so the
  price ladder "moves with" the market.
- **Imbalance** — `(bid size − ask size)/(bid size + ask size)`; how lopsided the top of book is.
- **OFI (order-flow imbalance)** — running signed change in top-of-book size; net buying pressure at the
  touch (Cont–Kukanov–Stoikov).
- **Adverse selection / toxic fill** — getting filled right before the price moves against you (e.g. your
  bid fills just as the market drops). The core risk shadow manages.
- **Mark-out** — the mid-price change measured a fixed time after a fill (+1s, +5s…); the standard way to
  score whether a fill was benign or toxic.
- **MBO / MBP** — Market-By-Order (every individual order visible, needed for true queue position) vs
  Market-By-Price (only aggregated size per level). Our data is MBO.
- **L1 / L2 / L3** — market-data depth: L1 = best bid/ask only; L2 = aggregated depth per level; L3 =
  every individual order (same detail as MBO).

**Probability / stochastic-process**
- **Poisson process** — a stream of random events arriving at a steady average **rate**; gaps between
  events are memoryless (exponentially distributed).
- **Rate / intensity (`λ`)** — expected number of events per unit time. "Intensity" is used when the rate
  can itself change over time or with state.
- **Markov property / Markov chain** — the future depends only on the *current* state, not on how you got
  there. A **CTMC (continuous-time Markov chain)** is a Markov chain whose jumps can happen at any instant.
- **Generator matrix (`Q`)** — the table of all state-to-state jump rates that defines a CTMC.
- **Birth–death process** — a chain whose state only steps up by one ("birth") or down by one ("death"),
  each with its own rate; a single queue is one.
- **First-passage / hitting probability** — the chance the process reaches one target state before
  another (e.g. ask queue empties before bid queue).
- **Gambler's ruin** — the classic first-passage formula for a birth–death walk hitting `0` before a
  barrier `N`.
- **Harmonic function** — a function whose value at each state equals the (rate-weighted) average of its
  neighbours'; hitting probabilities are harmonic, solving `Q·h = 0`.
- **Stationary / invariant distribution (`π`)** — the long-run fraction of time the process spends in each
  state, once it has settled.
- **Tridiagonal system** — a linear system where each equation touches only its immediate neighbours
  (`f(q−1), f(q), f(q+1)`); very fast to solve.
- **Laplace transform** — an integral transform that turns questions about *timing* into algebra.
- **Monte Carlo** — estimating a probability by simulating the process many times and counting outcomes;
  error shrinks like `1/√M` for `M` runs.

**Point-process / Hawkes**
- **Point process** — a random model of *event times* on a timeline (rather than of queue sizes).
- **Hawkes process** — a point process where each event temporarily raises the intensity of future
  events (self- and cross-excitation) — captures clustering.
- **Kernel (`φ`)** — the function giving how much, and for how long, one event lifts future intensity
  (here `α·e^{−β·Δt}`, an exponential kernel).
- **Self- / cross-excitation** — an event raising the rate of the *same* type (self) or of *other* types
  (cross).
- **Branching ratio (`n*`)** — expected number of "child" events triggered per event; `n* < 1` required
  for stability; `n*→1` = near-critical / fast market. Equals the **spectral radius** (largest eigenvalue
  magnitude) of the excitation matrix.
- **Compensator** — the integrated intensity `∫λ dt`; appears in the likelihood and in fill-probability
  `1 − e^{−∫λ}`.
- **Ogata thinning** — a standard way to *simulate* a point process: propose events at an upper-bound rate
  and randomly accept them with probability `λ/λ̄`.

**Estimation / evaluation / ML**
- **MLE (maximum-likelihood estimation)** — choose the parameter values that make the observed data most
  probable; for Poisson rates this is just "count events / time."
- **Likelihood / log-likelihood** — how probable the data is under a given parameter set; models are fit
  by maximising it and compared by its out-of-sample value.
- **AIC / BIC** — information criteria that score fit *minus* a penalty for the number of parameters
  (guards against overfitting); lower is better.
- **OLS (ordinary least squares)** — fit a linear model by minimising squared errors.
- **Logistic regression** — a linear model whose output is squashed to a probability by the logistic
  function `σ`.
- **Softmax / cross-entropy** — softmax turns network outputs into class probabilities; cross-entropy is
  the loss used to train classifiers against labels.
- **CNN / LSTM** — Convolutional Neural Network (learns spatial patterns, here across book levels) /
  Long Short-Term Memory (a recurrent net that learns temporal patterns across snapshots).
- **Out-of-sample / train-val-test split** — fit on one set of sessions, tune on a second, judge on a
  third never seen during fitting; prevents crediting a model for memorising its training data.

**This project**
- **shadow** — kaspar's passive execution algorithm (`light22`); the fixed strategy each model plugs
  into. Baseline = shadow as-is.
- **EV_keep** — the keep-vs-cancel expected value `P(fill) × E[mid_after_fill − price | fill]`; keep the
  order while positive, cancel when negative.
- **SOM** — kaspar's Simulated Order Manager; the queue-aware fill simulator used for the backtest.

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
