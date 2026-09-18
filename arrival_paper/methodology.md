# Methodology — Hawkes Fitting and Arrival-Process Statistics

*2026-09-18 — v@m2te.ch*

*Companion draft to `outline.md`. This document supplies the fitted-model machinery for §3 of the paper.*

---

## 1. Point-process background — every term defined

This section builds the vocabulary from scratch. If you already know what a σ-algebra is you can skim to §1.4, but the paper is meant to be readable by people who don't.

### Warm-up — the Poisson process in plain English (read this first)

Before defining a "point process" formally, meet its simplest member: the Poisson process. Once you're comfortable with this, the rest of §1 is just formalising it and generalising it.

**The setup.** Imagine you're watching messages arrive from a market-data feed and you write down the time of each arrival:

```
09:30:00.213    ← 1st arrival
09:30:00.451    ← 2nd
09:30:00.802    ← 3rd
09:30:01.014    ← 4th
09:30:01.317    ← 5th
...
```

That list of timestamps is the raw thing we're going to model. Nothing else — no prices, no sides, just when.

**The Poisson idea.** Imagine that every very-tiny fraction of a second, there is some *small independent* chance an arrival happens. Independent means: whether one arrives right now doesn't depend on when the previous one came, or how many happened in the last minute, or anything about the past. It's like flipping a huge number of very-biased coins, one for each tiny sliver of time, and calling an arrival every time one comes up heads.

The **rate** of the process is a single number, usually written `λ` (Greek "lambda", for now think "rate") or `μ` ("mu"), in units of *arrivals per second*. Concretely, if the rate is 300 per second:

$$
\mu \;=\; 300 \text{ arrivals per second}
$$

then on average you get 300 arrivals per second, forever.

Three consequences that follow purely from "each tiny sliver is independent":

**(1) The time between consecutive arrivals ("gap") follows an exponential distribution with mean 1/μ.**

If `μ = 300` per second, the mean gap is:

$$
\text{mean gap} \;=\; \frac{1}{\mu} \;=\; \frac{1}{300 / \text{s}} \;\approx\; 3.3 \text{ milliseconds}
$$

The distribution of gaps has the memoryless property: no matter how long you've already been waiting, the expected time to the next arrival is still 3.3 ms.

The probability the next gap is longer than some time `Δ` is:

$$
\Pr(\text{gap} > \Delta) \;=\; e^{-\mu\,\Delta}
$$

Substituting our example rate gives:

$$
\Pr(\text{gap} > 3.3 \text{ ms}) \;=\; e^{-1} \;\approx\; 36.8\%
$$

$$
\Pr(\text{gap} > 6.6 \text{ ms}) \;=\; e^{-2} \;\approx\; 13.5\%
$$

$$
\Pr(\text{gap} > 10 \text{ ms}) \;=\; e^{-3} \;\approx\; 5.0\%
$$

Conversely, the probability the gap is *shorter* than one-tenth of the mean (i.e., less than 0.33 ms in our example) is:

$$
\Pr\!\Big(\text{gap} < \tfrac{1}{10}\,\text{mean}\Big) \;=\; 1 - e^{-0.1} \;\approx\; 9.52\%
$$

This 9.52% number appears again and again in the rest of the paper — remember it. It's the Poisson benchmark for "how often do two arrivals come nearly back-to-back". If empirical data shows 60-85% instead of 9.52%, we've definitively rejected Poisson.

![Exponential distribution of interarrival gaps](figs/exponential_gaps.png)

*The exponential PDF of interarrival gaps for a rate-300/s Poisson process. The dashed vertical lines mark the p50 (median), p90, and p99 quantiles. Under Poisson, half the gaps are shorter than the median 2.3 ms and 10% are longer than 7.7 ms — a specific shape with a soft right tail. Real CME MDP3 data has a completely different shape: far more mass near 0 (bunched arrivals) and a much heavier right tail (long quiet stretches).*

**(2) The number of arrivals in any window of length `T` follows a `Poisson(μT)` distribution.**

Let `C` be the count of arrivals in a window of length `T`:

$$
\mathrm{E}[C] \;=\; \mu\,T
$$

$$
\mathrm{Var}[C] \;=\; \mu\,T
$$

(Same as the mean — that's the defining property of a Poisson distribution.)

So the ratio of variance to mean in *any* window `T` is:

$$
\frac{\mathrm{Var}[C]}{\mathrm{E}[C]} \;=\; 1
$$

Always. This ratio is called the **Fano factor** (§1.8 will formally define this); Poisson gives Fano = 1 at every window size. Any departure from this is a rejection of Poisson.

**(3) The number of arrivals in *disjoint* windows are independent.**

Whatever happened in `[0, 1s]` tells you nothing about how many arrivals happen in `[1s, 2s]`. No memory, no clustering, no repulsion — just independence.

**Why Poisson is the "null hypothesis" everyone starts from.** Poisson is what happens if arrivals are "as random as possible" — no self-reinforcement, no clustering, no bunching, no memory. It's what a purely mechanical, uncoordinated source would look like. Every departure from Poisson tells us something about the market: bursts of information, coordinated flow, self-reinforcing feedback.

**Two ways real market data rejects Poisson (previewing §1.7).**

- **Bunching**: real market messages tend to arrive in bursts. Many gaps are far shorter than the exponential mean would predict — in ES/NQ we see 60-85% of gaps shorter than mean/10, versus Poisson's 9.52%.
- **Momentum in the intensity**: after a burst starts, more arrivals are likely to keep coming. That's the opposite of Poisson's memoryless assumption.

To model these, we need a class of processes that is Poisson-like locally (arrivals still happen randomly on tiny timescales) but where the *rate* itself changes with the history. That's exactly what a **conditional-intensity point process** is — the topic of the rest of §1.

**Notational note.** Some authors write the Poisson rate as `λ`, others as `μ`. In this document `μ` is reserved for the *background* rate of a Hawkes process (which reduces to plain Poisson when the excitation kernel is zero), and `λ(t)` is reserved for the general time-varying conditional intensity. For a plain Poisson process, all times have the same rate:

$$
\lambda(t) \;=\; \mu \qquad \text{for all } t
$$

That constant is what we've been calling "the rate".

Now we can build up the general theory.

### 1.0 What is a "point process" and what is a "point"?

Start from the plainest words:

- **A "point"** here means a single instant in time when *something happens*. Not a spatial point (x,y), not a decimal point. Just: one instant. In our setting each point is the timestamp of one message arriving from the CME feed. If a message arrives at 09:31:15.723456789 ET, the point is that timestamp. Nothing more.

- **A "process"** is a random object that unfolds in time — you don't know it in advance, and it produces values as time goes on. A coin flipped once is random but static; a stock price ticking through the day is a random *process*. In our case the process produces a random *list of arrival timestamps*: `t₁, t₂, t₃, ...`

Putting the two words together:

- **A "point process"** is a random sequence of points (timestamps) on the real line. It is fully specified by *when* events happen — not by any value attached to those events. The output of a point process is nothing but a set of timestamps.

If each event *also* carries additional information (side, price, size), that's a **marked point process** — the timestamps are the "points" and the additional info per event is called a **mark**. Our arrival streams are naturally marked (each MBO message has side/price/size), but for arrival-process characterization (§3, §5) we mostly ignore the marks and treat only the timestamps as the process.

Alternative names you may see in the literature: "counting process" (emphasises `N(t)`), "temporal point process" (distinguishes from spatial), "renewal process" (a specific subclass — §1.5b), "self-exciting process" (Hawkes — §1.5c). All describe the same class of random-timestamps-in-time object under different assumptions.

**Concrete example.** Between 09:30:00 and 09:30:01 of NQ March-2025 the following four MBO arrivals occur, with timestamps:

```
09:30:00.001234567   Add,  bid 21432.50, size 3
09:30:00.001234571   Add,  ask 21432.75, size 5
09:30:00.017891234   Cancel, order X, bid 21432.25
09:30:00.238491702   Trade, ask 21432.75, size 2
```

The **point process** is just the four timestamps. The **marks** (Add/Cancel/Trade, side, price, size) are additional per-event data. Our theory in §1.1-1.9 is about the timestamps only.

### 1.1 The setup — a probability space

We model market-data arrivals as a **stochastic process**: a family of random variables indexed by time. The underlying machinery is a **probability space** `(Ω, F, Pr)`:

- **`Ω`** ("sample space") — the set of all possible *outcomes* of the whole experiment. Here an outcome is one specific realization of the market's day: the complete list of arrival timestamps, prices, sides, etc. Different days of trading are different outcomes drawn from `Ω`. You never see `Ω` directly; it exists to make everything else well-defined.

- **`F`** ("sigma-algebra" of events, written `Σ` or `F`, pronounced "the σ-algebra"). Plain English:

  Think of a σ-algebra as the **exhaustive list of yes-or-no questions we're allowed to ask about the outcome of the experiment**, and to which a probability can be assigned. Each such question corresponds to a subset of `Ω` — the subset of outcomes for which the answer is "yes". Two examples:

  - Question: "Did at least one message arrive between 09:30:00 and 09:30:01?" → yes/no. The subset of `Ω` where the answer is yes is one event, an element of `F`.
  - Question: "Did the front contract trade above 21500 at some point in the day?" → yes/no. Another event.

  So an "event" is nothing more mysterious than a yes-or-no question about what happened, and `F` is the complete catalog of such questions.

  The two rules a σ-algebra must satisfy just say the catalog is closed under logical combinations:

  1. **Closed under complements.** If "did A happen?" is in the catalog, then "did A *not* happen?" is also in the catalog. (You can always negate a yes/no question.)
  2. **Closed under countable unions.** If you have a list of questions `A₁, A₂, A₃, ...` in the catalog, then "did *at least one* of `A₁, A₂, A₃, ...` happen?" is also in the catalog. (You can OR any countable list of questions together.)

  From those two rules follow all the usual set-theoretic operations: intersections (AND), differences, symmetric differences. So the σ-algebra ends up being every yes/no question you could logically build out of the elementary ones.

  Why is this needed? Because for some sample spaces (uncountably infinite ones like ours) you can't consistently assign probabilities to *every* possible subset of `Ω` — that would break the math. The σ-algebra picks out the "nice" subsets where probability *is* well-defined. In practice for our arrival-timestamp setting, `F` is the standard **Borel σ-algebra** — the natural catalog of yes/no questions on the real line (or on the space of arrival-time lists), generated by the simplest ones like "is the timestamp less than τ?" and closed under negation and countable union as usual — and every event you'd naturally think of ("at least N arrivals in this window", "the k-th arrival was before time τ") lives in it.

  For a small everyday analogy: if `Ω` = {all possible outcomes of a coin flip} = {H, T}, then `F` = { ∅, {H}, {T}, {H,T} } — the four questions "did nothing happen", "was it heads", "was it tails", "was it either" — and each has a probability. That's a σ-algebra. Ours is much bigger but the idea is the same: a complete catalog of yes/no questions closed under negation and countable-OR.

- **`Pr`** (probability measure, sometimes written `P`) — a function that assigns a probability to each event:

  $$
  \Pr : \mathcal{F} \to [0, 1]
  $$

  Takes an event (a subset of `Ω`) and returns a real number in `[0, 1]`. The three axioms are:

  $$
  \Pr(\emptyset) \;=\; 0
  $$

  $$
  \Pr(\Omega) \;=\; 1
  $$

  and countably additive for disjoint events (probabilities of disjoint alternatives sum). Every time you see `Pr(...)` below, read it as "the probability of the event inside the parentheses".

### 1.2 Filtration — how information accumulates over time

At time `t = 0` we know nothing yet. As time passes we observe more arrivals. To formalize this we introduce a **filtration**: an increasing family of σ-algebras indexed by time, with each earlier σ-algebra sitting inside every later one:

$$
\{\mathcal{F}_t\}_{t \geq 0} \qquad\text{with}\qquad \mathcal{F}_s \subset \mathcal{F}_t \quad\text{for all } s \leq t
$$

- **`𝓕ₜ`** — the σ-algebra of everything observable by time `t` (inclusive). If an event is "the third arrival happened at 09:31:15.723", then this event is in `𝓕ₜ` for every `t ≥ 09:31:15.723` (we know it after 9:31:15.723) and not in `𝓕ₜ` for earlier `t` (we don't know it yet).

- **`𝓕ₜ₋`** — everything observable *strictly before* `t`. This distinction matters because whether an arrival happens exactly at time `t` may or may not be included: `𝓕ₜ₋` is the history right up to `t`, exclusive of what happens at `t` itself.

So when a formula says "conditional on `𝓕ₜ₋`", it means "given everything we've seen strictly before time `t`". A **process is adapted to `{𝓕ₜ}`** if for every `t` its value is measurable with respect to `𝓕ₜ` — i.e., knowable from the history up to `t`.

### 1.3 The counting process N(t)

The observable object is a random function of time:

$$
N(t) \;=\; \#\{\, i : t_i \leq t \,\}
$$

- `t₁, t₂, ...` are the (random) arrival times of the point process
- `#{ ... }` means "how many"
- `N(t)` is thus "how many arrivals we've seen by time `t` inclusive"

`N(t)` is a non-decreasing, right-continuous, step-function of `t` that jumps by 1 at each arrival. It starts at zero:

$$
N(0) \;=\; 0
$$

For a fixed `t`, `N(t)` is a random *number* (integer ≥ 0). Varying `t`, `N(·)` is a random *function*. The whole point of point-process theory is to model the *law* of `N(·)`.

Number of arrivals in a half-open interval:

$$
N(t + h) - N(t) \;=\; \text{number of arrivals in } (t,\, t + h]
$$

### 1.4 Conditional intensity — the "current speed" of the process

**The plain-English version first.** At any moment `t`, ask two questions:

1. "What's the probability that at least one new message arrives in the next tiny sliver of time, of length `h` seconds?"
2. "Given all the messages we've already seen up until right now (but not including `t` itself), how does that probability behave as `h` gets very small?"

The **conditional intensity** `λ(t)` is the answer to question 2, divided by `h`, in the limit as `h → 0`. It's the *instantaneous rate* of arrivals at time `t`, conditional on the history so far. If the current rate is 300 per second, then in the next 1 ms the chance of seeing a message is roughly:

$$
\Pr(\text{arrival in the next 1 ms}) \;\approx\; \lambda(t) \cdot h \;=\; 300 \times 0.001 \;=\; 30\%
$$

**The formula.** Written out in symbols this is:

$$
\lambda(t \mid \mathcal{F}_{t^-}) \;=\; \lim_{h \downarrow 0} \frac{1}{h} \; \mathrm{Pr}\!\left(\; \underbrace{N(t+h) - N(t)}_{\substack{\text{number of arrivals}\\\text{in the tiny interval }(t,\,t+h]}} \;\geq\; 1 \;\;\Big|\;\; \underbrace{\mathcal{F}_{t^-}}_{\substack{\text{everything we've}\\\text{observed strictly}\\\text{before time }t}} \;\right)
$$

Piece by piece:

- **`Pr(A | B)`** — read as "the probability of A given B". It's just conditional probability, the same one you saw in grade 12. `Pr(rain | cloudy)` = probability of rain given it's cloudy.
- **`A` = "`N(t+h) − N(t) ≥ 1`"** — the event *"at least one message arrived between `t` and `t+h`"*. `N(t)` was defined as "how many arrivals we've counted so far", so the difference is "how many new ones showed up in that tiny interval". Requiring ≥ 1 just means "at least one".
- **`B` = "`𝓕ₜ₋`"** — the whole history strictly before `t`. All the messages we've already observed, their times, their sides, everything. Formally a σ-algebra; informally "the past."

So the raw probability inside the formula is:

> **"The probability that at least one new message arrives in the next `h` seconds, given the entire history of messages that arrived strictly before now."**

That probability is a number in `[0,1]`. As you shrink `h → 0`, the probability shrinks too (there's less time for something to happen). To get a *rate* (per second) instead of a probability (dimensionless), we divide by `h`. The limit as `h → 0` gives us `λ(t)`, which has units of "arrivals per second".

**Why "conditional".** The rate depends on what came before. If a big burst just happened, `λ(t)` should be temporarily higher (the market is active). If the last message was 2 minutes ago, `λ(t)` might be lower. The conditioning bar `| 𝓕ₜ₋` is what allows the rate to depend on history.

**Contrast with unconditional (Poisson, defined properly in §1.5).** If the intensity equals some constant — call that constant `μ` (Greek "mu", introduced formally in §1.5(a)) — no matter what came before, that's Poisson:

$$
\lambda(t) \;=\; \mu \qquad (\text{Poisson})
$$

`μ` here is just "the background arrival rate", with units 1/time (arrivals per second). If it equals 300, we expect 300 arrivals per second on average, and the history doesn't change that. The conditioning bar `| 𝓕ₜ₋` in the intensity formula carries no information for Poisson because whatever happened before, `λ(t) = μ` anyway.

**Contrast with Hawkes (also defined properly in §1.5).** In a Hawkes process the intensity is that same background `μ` (still the exogenous "would happen anyway" rate, per second) **plus** a sum over all *past* arrival times (those in `𝓕ₜ₋`) of a decaying "excitation" contribution `φ(t − tᵢ)`:

$$
\lambda(t) \;=\; \mu \;+\; \sum_{t_i \,<\, t} \phi(t - t_i) \qquad (\text{Hawkes, preview})
$$

where `tᵢ` is a past arrival and `φ` is a decay function called the kernel. Each past event bumps `λ(t)` up, and the bump decays as time since that event grows. Full formulas below.

**Useful shortcuts** for small `h`, informal:

Probability of an arrival in the next small interval:

$$
\Pr(\text{arrival in }(t,\,t+h]) \;\approx\; \lambda(t)\,h
$$

Expected number of arrivals in a finite interval:

$$
\mathrm{E}\!\left[\,N(b) - N(a)\,\right] \;\approx\; \int_a^b \lambda(s)\, ds
$$

Once we've said what `λ(·)` is (as a function of history), we've said everything about the point process. Different families of `λ` give different models:

### 1.5 Three canonical models

**(a) Homogeneous Poisson.** Constant intensity:

$$
\lambda(t) \;=\; \mu \qquad \mu > 0
$$

- `μ` = the constant background rate (per second). If it's 300, we expect 300 arrivals per second on average, forever.
- Gaps between consecutive arrivals are **i.i.d.** ("independent and identically distributed" — each gap is drawn independently of the others, from the same distribution) and exponential:

$$
t_{i+1} - t_i \;\sim\; \mathrm{Exp}(\mu), \qquad \text{mean gap } = 1/\mu
$$

- Count in any window `[t, t + T]` is Poisson-distributed:

$$
N(t + T) - N(t) \;\sim\; \mathrm{Poisson}(\mu T), \qquad \mathrm{E}[\cdot] = \mathrm{Var}[\cdot] = \mu T
$$

- Every summary statistic we compute reduces to a triviality here. Let `Δ` denote a random interarrival gap. Then:

$$
\text{CV} \;=\; \frac{\sigma(\Delta)}{\mathrm{E}[\Delta]} \;=\; 1 \qquad (\text{coefficient of variation, formally defined in §5.1})
$$

$$
\text{CV}^2 \;=\; \left(\frac{\sigma(\Delta)}{\mathrm{E}[\Delta]}\right)^2 \;=\; 1
$$

$$
F(T) \;=\; 1 \qquad (\text{Fano factor at every window } T\text{; §1.8})
$$

$$
H \;=\; 0.5 \qquad (\text{Hurst exponent; §1.9})
$$

$$
\mathrm{Corr}(\Delta_i, \Delta_{i+k}) \;=\; 0 \qquad (\text{gap autocorrelation at every lag } k)
$$

All these quantities are ways of measuring departure from this baseline.
- This is the null hypothesis every microstructure paper rejects, and we reject it too — but reporting *how much* it's rejected is the ballgame.

**(b) Renewal process.** Interarrival gaps are defined as:

$$
\Delta_i \;=\; t_i - t_{i-1}
$$

They are drawn i.i.d. from some common distribution `G`, not necessarily exponential. (We use `G` rather than `F` for the gap distribution to avoid clash with the σ-algebra `F`.)

Intensity is a function of time since the last arrival:

$$
\lambda(t) \;=\; h(t - t_{\mathrm{last}})
$$

- `t_last` = the last arrival time up to `t`.
- `h(·)` = the **hazard function** of `G`, i.e., the rate at which the next event fires given none has fired yet since `t_last`. For an exponential gap distribution the hazard is constant:

$$
h(u) \;=\; \mu \qquad (\text{if } G = \mathrm{Exp}(\mu))
$$

For other `G` the hazard varies with the elapsed time.

- Marginal distribution of gaps can be arbitrary (heavy-tailed, bi-modal, anything). But **consecutive gaps are independent** by construction.
- Renewal ≠ Poisson unless `G` is exponential.
- Renewal produces high CV (the coefficient of variation introduced above under §1.5a) — defined:

$$
\mathrm{CV} \;=\; \frac{\sigma(\Delta)}{\mathrm{E}[\Delta]}
$$

and high Fano at short `T` — but not sustained clustering over time. Shuffling the gap sequence — using the **Fisher-Yates shuffle**, a standard uniformly-random permutation algorithm (imagine dealing the gaps into a random order like shuffling a deck of cards) — leaves the renewal process statistically identical (i.i.d. under permutation). This is why the Fisher-Yates shuffle test (§5.3) separates renewal from Hawkes: under Hawkes the ordering matters, so shuffling changes the statistics; under renewal the ordering doesn't matter, so shuffling changes nothing.

**(c) Hawkes (self-exciting) process.** Each past arrival transiently raises the intensity of subsequent arrivals:

$$
\lambda(t) \;=\; \mu \;+\; \sum_{t_i \,<\, t} \phi(t - t_i)
$$

- **`μ ≥ 0`** — the **background rate**, the intensity that would exist with zero prior arrivals. "Exogenous" arrivals (news, order-flow uncorrelated with prior events).
- **`φ : (0, ∞) → [0, ∞)`** — the **kernel** (or "excitation function"). Given that an arrival happened `u` seconds ago, `φ(u)` is how much extra intensity that arrival still contributes right now. `φ` decays: recent arrivals contribute more than old ones.

  **What "kernel" means in plain English.** The word "kernel" here has nothing to do with corn or operating systems — it's a math-jargon term for a function that says *"how much a past event still matters at any later time"*. Two everyday metaphors:

  - **Ripples in a pond.** Drop a stone in still water. It creates a ripple that spreads and fades. The kernel is the shape of that fading — big right after the drop, small a few seconds later, zero after long enough. If you drop several stones at different times, the total disturbance at any moment is the sum of the individual ripples still going, each faded by the amount of time since its stone hit.
  - **A bell ringing.** Strike a bell; it rings loudly at first, then decays. The kernel is the decay curve of one strike's sound. If someone hits the bell many times, the total sound right now is the sum of all past strikes, each contributing according to how long ago it was hit.

  In a Hawkes process, every past arrival `tᵢ` is one such "stone drop" or "bell strike". The intensity `λ(t)` right now is the background rate `μ` plus the sum of the *residual echoes* from every past arrival, where the kernel `φ(t − tᵢ)` tells you how loud each echo still is:

  $$
  \lambda(t) \;=\; \underbrace{\mu}_{\text{background}} \;+\; \underbrace{\sum_{t_i \,<\, t} \phi(t - t_i)}_{\substack{\text{sum of residual echoes} \\ \text{from all past arrivals}}}
  $$

  So the kernel is the *shape* of one event's future influence. Two things follow:

  1. **`φ` must decay** as `u` grows — otherwise past events would matter forever, and the process would build up to infinity. Formal condition: the kernel is integrable, i.e., its total area `n = ∫φ` is finite (this integral is the branching ratio from §1.6).
  2. **`φ` must be non-negative** — a past event can only *raise* future intensity in a self-exciting model, never lower it. (Models with negative excitation exist — "self-inhibiting" processes — but they don't fit CME data and we don't use them.)

  Different choices of kernel shape produce different Hawkes flavors. The most common two we fit:

- **`tᵢ`** — the past arrival times (all events with `tᵢ < t`; the sum runs over all of them).

Two standard kernel families:

**Exponential kernel:**

$$
\phi(u) \;=\; \alpha\, e^{-\beta\, u}
$$

Two parameters: `α ≥ 0` is the jump-size (intensity increment immediately after an arrival), `β > 0` is the decay rate (1/time-constant). This is what we fit primarily. Its virtue: **Markovian** — the current intensity summarizes all the information you need from the past, so you only need to update one running number when a new event arrives, rather than re-scanning history. Named after mathematician A. A. Markov. Concretely for the exp-Hawkes, if the current intensity right before a new event is `λ(t)`, then right after that event it becomes `λ(t) + α`, and between events it decays as `μ + (λ(t) − μ)·exp(−β·Δt)`. Nothing else about the past enters. This means intensity updates recursively in `O(1)` per new arrival.

**Power-law kernel:**

$$
\phi(u) \;=\; a \, (u + c)^{-(1+p)} \qquad p > 0
$$

Slower decay; consistent with the empirical Hardiman-Bouchaud finding that CME kernels look power-law over long horizons. Slower to fit; considered in §5 goodness-of-fit follow-up.

### 1.6 Branching ratio n — the most important derived quantity

$$
n \;=\; \int_0^\infty \phi(u)\, du
$$

- `n` = **mean number of direct "offspring" arrivals that a single arrival triggers over its remaining kernel**.
- For the exponential kernel:

$$
n \;=\; \int_0^\infty \alpha\, e^{-\beta u}\, du \;=\; \frac{\alpha}{\beta}
$$

That's a two-line calculation you can do by hand.

- For a general kernel, integrate `φ` over all future time.

Interpretation as a **branching process** (also called a Galton-Watson process, after the 19th-century statisticians who first studied family-surname extinction with it):

Think of each arrival as an "ancestor" that may produce some random number of "offspring" arrivals directly. Those offspring can then produce their own offspring, and so on. The branching ratio `n` is the average number of direct offspring per ancestor. Whether the whole family line dies out or grows to infinity depends entirely on whether `n < 1` or `n ≥ 1`:

- Every arrival is either **exogenous** (spawned by the background `μ`) or **endogenous** (a "child" of some earlier arrival).
- On average each arrival has `n` direct children.
- Expected total cluster size initiated by one exogenous arrival is a geometric series:

$$
\text{expected cluster size} \;=\; 1 + n + n^2 + n^3 + \ldots \;=\; \frac{1}{1 - n} \qquad (\text{for } n < 1)
$$

- When `n < 1` the process is **subcritical / stationary**: expected clusters are finite; the process has a stationary rate:

$$
\text{stationary rate} \;=\; \frac{\mu}{1 - n}
$$

- When `n → 1⁻` the process is **near-critical**: cluster sizes and durations diverge; long-range dependence appears (rough volatility literature builds on this).
- When `n ≥ 1` the process is **supercritical**: with positive probability a single arrival spawns an infinite cascade in finite time, which we don't observe in nature.

Empirically on ES/NQ book feeds the branching ratio sits in a narrow window near 1:

$$
n \;\approx\; 0.85 \text{ to } 0.97 \qquad (\text{ES/NQ, prior literature})
$$

That's the "close to critical" story from Filimonov-Sornette and Hardiman-Bouchaud that we replicate and extend.

### 1.7 Why Hawkes captures CME MDP3 and Poisson doesn't

Three data signatures that a Poisson (or any renewal process) cannot produce, but a Hawkes with `n` close to 1 produces naturally:

**(i) Very short interarrival gaps.** Empirically, on ES/NQ book streams roughly 60-85% of gaps are shorter than one-tenth of the mean gap. For any exponential the corresponding number is fixed:

$$
\Pr\!\Big(\text{gap} < \tfrac{1}{10}\,\text{mean}\Big) \;=\; 1 - e^{-0.1} \;\approx\; 9.52\% \qquad (\text{any exponential})
$$

Hawkes produces the excess of short gaps because each arrival transiently raises `λ`, making the next arrival more likely to come soon.

**(ii) Positive gap autocorrelation.** For a Hawkes process the autocorrelation of consecutive gaps is positive at every lag:

$$
\mathrm{Corr}(\Delta_i, \Delta_{i+k}) \;>\; 0 \qquad \text{for all lags } k
$$

Long gaps cluster with long gaps (quiet periods), short with short (bursty periods). Renewal processes have i.i.d. gaps by definition, so:

$$
\mathrm{Corr}(\Delta_i, \Delta_{i+k}) \;=\; 0 \qquad (\text{renewal, any } k \geq 1)
$$

Only self-excitation can produce positive lag-`k` gap correlation.

**(iii) Persistent burstiness across timescales.** The Fano factor (defined in §1.8 next) rises with the window size `T` on a Hawkes process; on Poisson or any renewal it stays constant. This means clustering isn't a single-timescale phenomenon — it looks self-similar over decades of `T`.

### 1.8 Fano factor — measuring dispersion of counts

For a chosen window length `T`, chop the observation interval `[0, τ_end]` into `K` disjoint bins:

$$
K \;=\; \lfloor \tau_{\mathrm{end}} / T \rfloor
$$

Count arrivals in each bin:

$$
C_k \;=\; N(kT) - N((k-1)T) \qquad k = 1, 2, \ldots, K
$$

Then the Fano factor at window `T` is the sample variance divided by the sample mean of those counts:

$$
F(T) \;=\; \frac{\mathrm{Var}[C_k]}{\mathrm{E}[C_k]}
$$

- Units: dimensionless (variance of a count / mean of a count).
- **Poisson:** variance equals mean at every window, so:

$$
F(T) \;=\; 1 \qquad \text{for every } T \text{ under Poisson}
$$

This is the definitive Poisson signature.

- **Hawkes / clustered:** `F(T) > 1`, and typically `F(T)` grows with `T` because clustering compounds over longer windows.
- **Renewal:** `F(T) → 1` as `T → ∞` (windows contain many i.i.d. gaps and the **Central Limit Theorem**, or CLT — the classic result that averaging many independent things gives a Gaussian bell-curve — kicks in, so the count in a large window looks approximately Gaussian with variance ≈ mean). Renewal can have large `F(T)` at short `T` but `F(T)` stabilizes at large `T`. So *rising* Fano at large `T` is a Hawkes signature, not just a renewal signature.

Origin: Fano (1947) in cosmic-ray physics — same problem, counts of independent events in fixed windows.

**What Fano does visually — same mean rate, four different processes.** All four rows below have the same average rate (~20 arrivals per second), but the *dispersion* of arrivals inside 1-second windows differs by orders of magnitude:

![Rasters at four Fano levels](figs/rasters_by_fano.png)

*Top row (F ≈ 0, highly regular): arrivals march at a near-lattice cadence — every 50 ms like clockwork. Bins all have roughly the same count.*
*Second row (F ≈ 1, Poisson): the reference. Arrivals look "randomly spread" — no big gaps, no big bunches, mildly uneven bin counts.*
*Third row (F ≈ 3, moderate Hawkes): visible clumping. Some bins have 30-40 arrivals, some 5-10. You can see the flow "breathing".*
*Bottom row (F ≈ 14, near-critical Hawkes): pronounced bursts and long quiet stretches. Bin counts range from near-zero to 50+.*

**The same picture as a histogram of counts per 1-s bin:**

![Count histograms at four Fano levels](figs/count_hists_by_fano.png)

*Each panel shows the empirical distribution of arrivals per 1-second bin (colored bars) with the theoretical Poisson pmf at the same mean overlaid as a black curve.*

- **F ≈ 0 (regular):** all bins have counts crowded around the mean; distribution is much *narrower* than Poisson. Sub-Poisson.
- **F ≈ 1 (Poisson):** empirical histogram matches the Poisson pmf almost exactly. Reference case.
- **F > 1 (bursty):** distribution is *wider* than Poisson at the same mean — extra mass in both tails. Some bins are near-empty, some are packed. This is what CME MDP3 looks like — but shifted much further to the right (F(1s) empirically 10-70 on ES/NQ MBO adds).

So *Fano visualises the "shape" of arrivals*: below 1 = too regular to be Poisson (rare in market data), = 1 = Poisson-random, > 1 = bunched. The higher the Fano, the more the arrivals cluster into bursts separated by quiet gaps.

### 1.9 Hurst exponent — how burstiness scales with timescale

For a Hawkes-like process near criticality, the Fano factor scales as a power law in the window size:

$$
F(T) \;\sim\; T^{\, 2H - 1}
$$

where `H` (Hurst exponent) is a number:

$$
H \;\in\; [0.5, 1)
$$

- **`H = 0.5`** — no long-range dependence. Poisson, or any renewal process. `F(T)` is asymptotically constant.
- **`H > 0.5`** — long-range dependence, self-similar clustering across timescales. This is what Hawkes produces near criticality.
- **`H → 1`** — extreme long-memory; each timescale looks like a scaled version of the next.

We estimate `H` empirically by regressing log-Fano on log-window in OLS (**ordinary least squares** — the standard best-fit-line procedure that picks the line minimizing the sum of squared vertical distances between the fitted line and the observed points):

$$
\log F(T) \;=\; (2H - 1)\,\log T \;+\; c \;+\; \varepsilon
$$

The fitted slope equals `2H − 1`, and Hurst is recovered by:

$$
H \;=\; \frac{\text{slope} + 1}{2}
$$

For CME book feeds we typically observe:

$$
H \;\in\; [0.65, 0.75] \qquad (\text{empirical, ES/NQ book feeds})
$$

Higher `H` means more persistent clustering.

**Fano scales differently with window size for each process:**

![Fano factor vs window size](figs/fano_scaling.png)

*Log-log plot of F(T) vs the window size T for the four simulated processes.*

- **Regular (green)**: F(T) stays near zero — the process is nearly deterministic, so bin counts have vanishing variance at every T.
- **Poisson (blue)**: F(T) sticks close to 1 at every T, exactly as theory predicts. That flat horizontal line at F = 1 is the Poisson signature.
- **Moderate Hawkes (orange)**: F(T) is > 1 and rises with T because clustering compounds over longer windows.
- **Near-critical Hawkes (red)**: F(T) rises steeply — larger windows see bigger and bigger bursts because clusters last longer.

The *slope* of the log-log curves gives you the Hurst exponent H. Flat slopes (0) correspond to H = 0.5. Rising slopes correspond to H > 0.5 — the near-critical Hawkes in the red curve has a slope of roughly 0.5, which corresponds to H ≈ 0.75.

Note: shuffling the gap sequence (Fisher-Yates permutation) preserves the marginal distribution of gaps exactly but destroys the ordering, thereby destroying any long-range dependence. Under shuffle:

$$
H_{\mathrm{shuffled}} \;\to\; 0.5
$$

This is our sharpest test for whether clustering lives in the *ordering* (Hawkes) or in the *marginal* (renewal).

### 1.10 Notational summary

| symbol | meaning |
|---|---|
| `Ω` | sample space of all possible market days |
| `F` | σ-algebra of events (subsets of Ω we can assign probabilities to) |
| `Pr(A)` or `P(A)` | probability of event `A ∈ F`, a number in `[0,1]` |
| `𝓕ₜ` | σ-algebra of everything observable by time `t` (inclusive) |
| `𝓕ₜ₋` | σ-algebra of everything observable strictly before `t` |
| `t₁, t₂, ...` | random arrival times (finite in any bounded window) |
| `N(t)` | counting process: number of arrivals with `tᵢ ≤ t` |
| `λ(t)` or `λ(t \| 𝓕ₜ₋)` | conditional intensity at `t` given history before `t`, units 1/time |
| `μ` | Hawkes background rate — intensity with zero prior arrivals |
| `φ(u)` | Hawkes kernel — extra intensity a single event contributes `u` seconds later |
| `α` | exponential-kernel jump size (intensity increment immediately after an arrival) |
| `β` | exponential-kernel decay rate (1/time-constant); half-life `= ln 2 / β` |
| `γ` | marked-Hawkes size exponent — how much larger events excite more |
| `n = ∫φ` | branching ratio; for exponential kernel `n = α/β` |
| `Δᵢ = tᵢ − tᵢ₋₁` | interarrival gap |
| `Cₖ` | number of arrivals in bin `k` under some window `T` |
| `F(T)` | Fano factor = Var[Cₖ] / E[Cₖ] |
| `H` | Hurst exponent — Fano slope in log-log, `F(T) ~ T^{2H-1}` |
| `uᵢ` | compensator increment (`∫_{tᵢ₋₁}^{tᵢ} λ ds`) — "how many events the fitted model expected between these two consecutive events". Used in the time-rescaling GOF (goodness-of-fit) test in §5.5. |

## 2. Data — what we compute from and where it lives

**Corpus:** CME MDP3 pre-decoded L3 record files (`.bin`) for ES (chan 310), NQ (chan 318), and BTC (chan 326, extraction in flight for 2025). Path: `/vast/home/vmayeski/out/bin/{chan}/{chan}.{yyyymmdd}.databento.bin`. Each record carries three ns-precision timestamps that we use throughout the paper:

- **`transactTime`** — CME matching engine event time. The *true* arrival of the market event, stamped by CME's core matching engine and carried unchanged inside the MDP3 message body. This is the "physical" event time — the moment the trade or book update actually happened at the exchange.

- **`sendingTime`** — CME gateway UDP packet header time. Stamped by CME's outbound gateway when it packages one or more matching-engine events into a UDP packet for multicast. The difference `sendingTime − transactTime` therefore measures **CME-internal gateway queueing** — how long the event sat inside CME's own infrastructure between happening and being broadcast.

- **`recv_time`** (on trades) / **`handlerendtim`** (on MBO records) — **the pcap wire-arrival timestamp at Databento's colo capture tap.** Not our own capture — Databento captures pcaps in their CME-colo, stamps each UDP frame with the NIC-arrival time, and ships those pcaps to us. When we run `dbento_pcap_to_bin` on those pcaps, `handler_if.hpp` copies the pcap frame timestamp directly into the `.bin` record's `handlerendtim` field on MBO messages and into `recv_time` on trade messages (verified at `mdp3/include/mdp3/handler_if.hpp:577,691,771,832` — `l3.handlerendtim = recv_time` unconditionally in the offline path). The field name "handlerendtim" is a misnomer inherited from the live-handler code path; in the historical corpus it functions as the pcap wire timestamp. The difference `handlerendtim − sendingTime` therefore measures **CME-multicast transit to Databento's colo tap** — a network-only latency, cleanly separated from CME-internal queueing.

**Important limitation.** The three-timestamp anatomy uses Databento's colo capture time, not our own capture time. So the `handlerendtim − sendingTime` distribution characterises the CME → Databento colo path, which is a stable colo-cross-connect path but is not literally the network we would see if we captured elsewhere. For the arrival-process characterisation (§3) this doesn't matter — the *ordering* and *clustering* of arrivals is invariant to any small fixed offset. For any latency-magnitude claim we call this out explicitly.

**Field-name gotcha.** Older `handler_if` code paths (live handlers) do write a genuine "handler finished processing" timestamp into `handlerendtim`. In the historical pcap pipeline we use, that same field is repurposed to carry the pcap wire timestamp. This is confusing, but it's what the code does — and it's what makes the three-timestamp analysis possible for MBO records (which otherwise have no dedicated `recv_time` field in the current `.bin` schema).

**Restriction to one front contract per session.** For the arrival-process analysis, we track only the front-month futures securityID (highest MBO Adds count in the day, from `binstats`). This isolates the flow that carries the price-forming information. Roll days are dropped (front may straddle two contracts).

**Restriction to RTH.** 09:30 → 16:00 ET (14:30 → 21:00 UTC in winter, 13:30 → 20:00 in summer). Overnight regime is a separate analysis (§future work).

**Message types.** MBO records (`add`, `modify`, `cancel`, `execute`) plus MBO-trade records. Instrument-definition records (FDF/ODF/SDF) and volume/statistic records are excluded — they are exchange bookkeeping, not order-flow arrivals.

## 3. Model classes fit in this paper

We fit three point-process models per (session, stream), in increasing complexity:

### 3.1 Unmarked exponential Hawkes (the workhorse)

$$
\lambda(t) \;=\; \mu \;+\; \sum_{t_i < t} \alpha\, e^{-\beta (t - t_i)}
$$

- Three parameters: `μ ≥ 0` (background), `α ≥ 0` (jump size), `β > 0` (decay rate). Branching ratio `n = α / β`.
- The exponential kernel is Markovian: the intensity can be updated recursively in `O(1)` per arrival, which is what makes the online estimator (§4) practical.
- Prior art warns the true kernel on CME data is closer to a power law (Hardiman-Bouchaud); exponential is a working approximation that fits well over the time-scales we care about (100 ms — 60 s) and is the standard first step. Section §3.5 tests the residual.

### 3.2 Marked exponential Hawkes with size marks

$$
\lambda(t) \;=\; \mu \;+\; \sum_{t_i < t} \alpha\, (1 + s_i)^\gamma\, e^{-\beta (t - t_i)}
$$

- `sᵢ` is a size mark: message size class (0-2 depending on quote/trade size deciles) or trade `lastQty`.
- `γ ≥ 0` measures how much larger events excite subsequent arrivals more than smaller events. LR test against §3.1 verifies whether size marks meaningfully improve fit.
- Motivation: large trades and quote deletions plausibly carry more information than small ones and should trigger more follow-on flow.

### 3.3 Bivariate Hawkes on {bid-updates, ask-updates}

Two intensities, one per side. Let `a ∈ {bid, ask}` index the side of the intensity being modelled, and `b ∈ {bid, ask}` index the side of the past events feeding into it.

$$
\lambda_a(t) \;=\; \mu_a \;+\; \sum_{b \in \{\mathrm{bid,ask}\}} \sum_{t_i^{(b)} < t} \alpha_{ab}\, e^{-\beta_{ab} (t - t_i^{(b)})}
$$

- `tᵢ^{(b)}` are past arrival times on side `b` (i.e., past bid-update times if `b = bid`).
- `α_{ab}` is a 2×2 excitation matrix: `α_{ab}` = how much side-`b` events excite side-`a` intensity. Diagonal terms `α_{bb}, α_{aa}` = self-excitation; off-diagonal `α_{ab}, α_{ba}` = cross-excitation between sides.
- `β_{ab}` is a matching 2×2 decay-rate matrix.
- The 2×2 branching-ratio matrix has entries:

$$
K_{ab} \;=\; \frac{\alpha_{ab}}{\beta_{ab}} \qquad a, b \in \{\text{bid, ask}\}
$$

(for the exponential kernel; we call it `K` here to avoid clash with the counting process `N(t)` from §1.3). Spectral radius of `K` must be strictly less than 1 for the bivariate Hawkes to be stationary.
- Cross-terms `α_{ab}, α_{ba}` measure how much bid-side updates drag ask-side flow and vice versa — the mechanical prediction that "the book updates on both sides at once during price discovery."
- Underpins the directional signed-markout prediction in §7.5 of the paper.

## 4. Maximum-likelihood estimation

### 4.1 Log-likelihood

Two new symbols we use throughout this section:

- **`θ`** (theta) — the vector of parameters we're trying to estimate. For the unmarked exp-Hawkes it's `θ = (μ, α, β)`; for marked it's `θ = (μ, α, β, γ)`; for bivariate it's the six-parameter set `(μ_bid, μ_ask, α_{bb}, α_{ba}, α_{ab}, α_{aa})` plus decay rates. Writing `λ(t; θ)` reminds us the intensity depends on `θ`.
- **`log L(θ)`** — the log-likelihood, a real-valued function of `θ`. Higher `log L(θ)` = the parameter set `θ` explains the observed arrivals better. Maximum-likelihood estimation (MLE) picks `θ̂` that maximizes `log L(θ)`.

For a point process with intensity `λ(·; θ)` observed on `[0, T]` with events `t₁, ..., tₙ`, the log-likelihood is standard (Ozaki 1979):

$$
\log L(\theta) \;=\; \sum_{i=1}^n \log \lambda(t_i;\, \theta) \;-\; \int_0^T \lambda(s;\, \theta)\, ds
$$

- First term: sum over the `n` observed arrivals of the log-intensity *at those arrivals*. This term rewards high intensity at the times events actually happened.
- Second term (subtracted): total expected count over the whole window (this is `∫₀^T λ ds`). This term penalizes models that predict too many events overall.
- Together they trade off. The optimal `θ̂` makes intensity high right when events happened and low elsewhere.

For exponential Hawkes (§3.1) both terms have closed-form recursions:

- **Intensity sum**: introduce the recursion

$$
R_i \;=\; e^{-\beta (t_i - t_{i-1})} \, (1 + R_{i-1}), \qquad R_0 \;\equiv\; 0
$$

Then the intensity at each event is:

$$
\lambda(t_i) \;=\; \mu + \alpha \, R_i
$$

So the log-intensity sum term becomes computable in one pass:

$$
\sum_{i=1}^n \log \lambda(t_i) \;=\; \sum_{i=1}^n \log\!\big(\mu + \alpha R_i\big)
$$

- **Compensator** (the time-integral of `λ`):

$$
\int_0^T \lambda(s)\, ds \;=\; \mu\,T \;+\; \frac{\alpha}{\beta} \sum_{i=1}^n \Big(1 - e^{-\beta (T - t_i)}\Big)
$$

Both are computable in one `O(n)` pass over events. Marked (§3.2) and bivariate (§3.3) generalizations are analogous.

### 4.2 Optimizer

Python `scipy.optimize.minimize` with **L-BFGS-B** — the Limited-memory Broyden–Fletcher–Goldfarb–Shanno algorithm with box constraints, a workhorse quasi-Newton optimizer that iteratively climbs the log-likelihood using local gradient info without needing the full Hessian, and enforces our parameter bounds. Analytic gradient supplied. Bounds: `μ ≥ 0`, `α ≥ 0`, `β > 0`, and (for marked) `γ ≥ 0`. Warm-start from **method-of-moments** initial estimates — a simpler estimator that matches a few sample moments (like mean, variance, Fano) to their theoretical values under the model, giving a rough parameter guess without any optimization. The MoM estimate is fast but imprecise; we use it as a starting point for MLE to avoid local optima. Fit-time per RTH session on the 72-core box: seconds for §3.1, tens of seconds for §3.2, minutes for §3.3.

### 4.3 Numerical stability

- Sub-second precision timestamps must be shifted to a session-relative origin `tᵢ ← tᵢ − t_0` before optimization; otherwise the `exp(-β·tᵢ)` terms underflow for wall-clock ns timestamps.
- On very active sessions (`n > 10⁷`), we sub-sample uniformly to `n ≈ 10⁶` for the fit, then verify the fit on the full sequence. Uniform sub-sampling is not stationary-Hawkes-preserving in the strict sense but produces stable parameter estimates in practice (checked against full-sample fits on smaller days).

## 5. Diagnostic statistics — the per-session panel row (this is the whole point)

**This section is the deliverable.** Everything above sets up the vocabulary and the models. Section 5 says: *for one trading session on one stream, this is exactly what we compute.* Run it 731 days × 3 streams (ES/NQ/BTC) and you get the ~2200-row panel that every downstream result in the paper reads from.

**One session, one stream → one row of the panel.** Each session on each stream produces roughly 25-30 scalars that fit on a single row. The row is written to disk as JSON:

```
/vast/home/vmayeski/out/arrival_paper/fits/{stream}/{yyyymmdd}.hawkes.json
```

Then the 2200 daily rows are concatenated into the aggregate panel:

```
/vast/home/vmayeski/out/arrival_paper/panels/hawkes_panel.parquet
```

That panel IS the paper's empirical evidence base — every regression, every SEM factor, every regime comparison in §7 and §8 pulls from it.

**Two kinds of statistics in §5 — model-free and model-dependent.** Some are pure functions of the raw arrival tape (any point-process reader can reproduce them without fitting anything). Others need the Hawkes fit from §3-4 first:

| subsection | what it computes | model-free or fit-dependent | ~ scalars per session |
|---|---|---|---|
| §5.1 Marginal-gap stats | CV, CV², quantile ratios, P(gap<mean/10), rate ratios | **model-free** | ~10 |
| §5.2 Fano scaling + Hurst | F(T) at 5 window sizes, Hurst H from log-log slope | **model-free** | ~6 |
| §5.3 Gap ACF + shuffle test | ACF at 5 lags, shuffled F(5s) + H, collapse ratio | **model-free** | ~7 |
| §5.4 Branching-ratio recovery | n from MLE fit, n from method-of-moments, their gap | **fit-dependent** | ~3 |
| §5.5 Goodness-of-fit | KS p-value, Ljung-Box p-value at lag 20, QQ plot | **fit-dependent** | ~2 |

Model-free stats (§5.1-5.3) come first as a sanity check — they should reproduce prior-art numbers without any Hawkes assumption. Fit-dependent stats (§5.4-5.5) tell us whether the Hawkes we fit actually explains the data.

**Baseline expectations under a homogeneous Poisson at the same mean rate are given in parentheses throughout this section.** Departures from those baselines are the paper's evidence. Below every diagnostic is a quick reminder of what its Poisson-baseline value is; empirical values that hit the baseline mean "Poisson fits fine here" and values far from the baseline mean "we've rejected Poisson at this session".

### 5.1 Marginal-gap statistics

- **Coefficient of variation** — ratio of the standard deviation of gaps to the mean gap:

$$
\text{CV} \;=\; \frac{\sigma(\Delta)}{\mathrm{E}[\Delta]} \qquad (\text{Poisson: } 1.0)
$$

- **Squared CV** — often reported instead:

$$
\text{CV}^2 \;=\; \left(\frac{\sigma(\Delta)}{\mathrm{E}[\Delta]}\right)^2 \qquad (\text{Poisson: } 1.0)
$$
- **Quantile ratios** — measured `p10, p50, p90, p99, p99.9` gaps against `Exp(same mean)` quantiles `-m·log(1-p)`. Ratio crosses 1 exactly once for a burst process.
- **P(gap < mean/10)** — probability of a near-back-to-back arrival. `= 9.52%` for any exponential; empirically 40-85% for CME book streams.
- **Instantaneous rate** — mean rate vs rate implied by median gap. `1/log2 ≈ 1.44×` for any exponential; empirically 14-120× for CME.

### 5.2 Count-based statistics — Fano scaling and Hurst

Windowed count in the k-th bin (equivalent to `Cₖ` from §1.8):

$$
N_T(k) \;=\; N(kT) - N((k-1)T)
$$

Fano factor at window `T`:

$$
F(T) \;=\; \frac{\mathrm{Var}[N_T]}{\mathrm{E}[N_T]} \qquad (\text{Poisson: } 1 \text{ at every } T)
$$

Compute `F(T)` at `T ∈ {0.1, 1, 5, 60, 300}` seconds. Fit the log-log regression by OLS to extract the Hurst exponent `H`:

$$
\log F(T) \;\approx\; (2H - 1)\,\log T \;+\; c
$$

- `H = 0.5` — Poisson or any renewal process
- `H ∈ (0.5, 1)` — long-range dependence, characteristic of Hawkes / self-exciting
- `H > 1` — non-stationary or diverging variance

### 5.3 Ordering statistics — gap autocorrelation and shuffle test

Renewal processes with heavy-tailed marginals can produce large CV and moderate Fano without any ordering-driven clustering. To separate the two:

- **Gap ACF** (autocorrelation function of the sequence of gaps — literally, the correlation between each gap and the one `k` positions later, computed for `k = 1, 2, 3, ...`) at lags 1, 2, 3, 5, 10. Positive at all lags means long-gap-follows-long-gap.
- **Fisher–Yates shuffle** — seeded random permutation of the gap sequence preserves the marginal exactly and destroys the ordering. Recompute `F(5s)` and `H` on the shuffled sequence. The **collapse ratio** measures how much clustering lives in the ordering vs the marginal:

$$
\text{collapse} \;=\; \frac{F_{\text{original}}(5\text{s})}{F_{\text{shuffled}}(5\text{s})}
$$

A collapse ratio near 1 means the shuffle changed nothing (renewal — clustering was all in the marginal). A collapse ratio much greater than 1 (empirically 3-19× on ES/NQ book streams) means shuffling destroyed most of the clustering, so the clustering lived in the ordering (Hawkes).

The shuffle is the definitive test: renewal processes are invariant under shuffle; Hawkes and near-Hawkes are not.

### 5.4 Branching-ratio recovery

For fitted Hawkes (§3.1), the branching ratio comes directly from the fit:

$$
n \;=\; \frac{\alpha}{\beta}
$$

Sanity check against the method-of-moments estimator, which is model-free and uses only the Fano ceiling:

$$
n_{\mathrm{MoM}} \;\leq\; 1 - \frac{1}{\sqrt{F(T \to \infty)}}
$$

Discrepancy larger than 10% is a red flag for either misspecification (wrong kernel family) or non-stationarity within the session.

### 5.5 Goodness-of-fit — time-rescaling

The **time-rescaling theorem** is a classical result: if we knew the true intensity `λ(·)`, we could integrate it between consecutive events to get numbers called **compensator increments** (each one is "how many events the model would have expected between these two consecutive events"). Under the correct model those numbers are i.i.d. `Exp(1)` random variables. Formally:

$$
u_i \;=\; \int_{t_{i-1}}^{t_i} \lambda(s)\, ds
$$

are i.i.d. `Exp(1)` for the correct model. In practice we transform back to uniform via:

$$
p_i \;=\; 1 - e^{-u_i}
$$

so `pᵢ` should be i.i.d. `U(0,1)` under the correct model. We then apply:

- **Kolmogorov-Smirnov test** (KS test) against `U(0,1)`. The KS test compares an observed distribution against a target — it computes the maximum absolute gap between the empirical CDF (cumulative distribution function of what we saw) and the target CDF (uniform on `[0, 1]`). If that gap is small, the two distributions agree. The p-value tells us how likely we'd see a gap that big by chance if the residuals really were uniform; small p → the model doesn't fit. Report `p_KS` per session.
- **Ljung-Box test** on the `uᵢ` for serial correlation — a joint test that the first several autocorrelations of a sequence are all zero. If residuals are truly i.i.d. (no serial pattern), the Ljung-Box statistic is small; a small p-value flags remaining autocorrelation. Report `p_LB` at lag 20.
- **QQ plot** (quantile-quantile plot) on Exp(1) — plot each residual's rank against its expected Exp(1) quantile; if the model fits, points fall on a straight `y = x` line. Deviations from the line pinpoint which quantiles the model gets wrong. Visual, one representative session per stream in the paper.

Empirical target: `p_KS > 0.05` on ≥ 80% of sessions. Failure at this rate on a given stream would indicate the exponential kernel is too restrictive and we need to move to a power-law kernel (Hardiman-Bouchaud), a mixture-of-exponentials, or a marked model.

### 5.6 So what — what these ~2200 rows actually buy us

The §5 panel isn't the endpoint. It's the raw material that unlocks every headline claim of the paper. Here is what happens *next*, in order:

**Step A — Prove non-Poisson at scale.** Across 731 sessions × 3 streams, every §5 model-free diagnostic rejects Poisson by huge margins (CV in the tens, F(T) in the hundreds or thousands, H well above 0.5, ACF positive at every lag). fast_send showed this on one day; the panel proves it holds every day across three products across three years. Aggregate distribution of each stat over the panel becomes a headline table in §3 of the paper.

**Step B — Fit distributions of Hawkes parameters across regimes.** Every session gives us one (μ, α, β, γ, n). Aggregate 731 fits per stream = a per-stream distribution of `n_day`. The paper then reports:

- **Cross-day stability**: histogram of `n_day` per stream — is the branching ratio a stable property of the market microstructure, or does it swing with VIX, day-of-cycle, or macro events? (Answers the Filimonov/Sornette vs Hardiman-Bouchaud debate on our modern corpus.)
- **Cross-product comparison**: ES vs NQ vs BTC. If BTC's `n_day` distribution differs substantially, that says something about crypto-native market structure.
- **FOMC / open / close vs matched controls**: split the 731 sessions by regime and compare panel-stat distributions. Some regimes should have systematically higher `n`, higher H, worse fit.

**Step C — Feed into the adverse-selection attribution (§7 of the paper).** Every maker fill on that session gets a `λ̂` value (from the online estimator warm-started with that session's fitted parameters). Then per-fill adverse P&L is regressed on `log λ̂` plus controls, and ΔR² of that regression over an arrival-blind baseline is the paper's headline number. **This regression needs the per-session Hawkes fit as an input** — without §5-panel row #d, we can't compute λ̂ on session d.

**Step D — Feed into the unification SEM (§8 of the paper).**

**First, what this step is NOT.** Step D does not make predictions. It does not claim that today's `n` predicts tomorrow's markouts, or that hour t-1's intensity forecasts hour t's latency. Serial autocorrelation of `n` across time is not strong enough for that, and the paper explicitly does not claim it.

**What this step IS.** Step D is a **cause-hunt for the three tails**. We observe three seemingly unrelated tail phenomena on the same days:

- price returns have fat tails (fat-tailed return distribution)
- passive maker fills carry adverse selection (fat-tailed adverse markout)
- decoders occasionally see very high latency (fat-tailed p99 latency)

We want to find the **common driver** — the single physical property of the market on that day (or in that hour) that inflates all three tails together. The candidate driver is **Hawkes criticality (branching ratio `n`)** — how close the message-arrival process is to the near-critical `n → 1` regime.

If a single hidden factor loads on all three observed tails and that factor tracks `n`, we've identified the driver. If not, the three tails have separate causes and the unification claim collapses.

**Nothing is being predicted.** We're testing contemporaneous covariance: within the same day (or hour), do the three tail metrics co-move as if driven by one thing? This is a factor-analysis / structural equation question, not a forecasting question.

**Which "one thing" — criticality (`n`) or intensity (mean `λ`)?** These are *both* summary statistics of the same Hawkes fit, but they play different roles:

| quantity | scale | role in the paper |
|---|---|---|
| `n` (branching ratio) — **criticality** | one number per day (or hour) — the *shape* parameter | **primary hidden-variable candidate in Step D's SEM.** Measures reflexivity independent of raw event count. Theory-motivated by QHawkes / rough-vol scaling limits. |
| `λ̂(t)` — **instantaneous intensity** | one number per event — the *real-time level* | **used in Step E** for per-fill markout attribution, real-time gating decisions, and latency prediction. |
| `mean λ` (session-average intensity) | one number per day (or hour) — the *level* parameter | **robustness alternative** hidden variable in Step D. Mixes exogenous news rate `μ` with reflexivity `n` via `λ̄ = μ / (1-n)`. Less clean as a driver candidate. |

The paper's primary SEM uses `n` as the target. The robustness SEM uses `log(mean λ)`. If `n`-based SEM captures more joint variance than `mean λ`-based SEM, we conclude **reflexivity, not raw volume, drives the three tails** — the theory-driven claim.

**But `n` and mean λ are correlated — how do we know we're isolating criticality and not just activity level?**

This is the real methodological hazard for Step D. Busy days tend to be both high-mean-λ AND high-n (relative to their own baseline). All three tails are also large on busy days simply because busy = more events = more opportunities for large moves, toxicity, and queue buildup. A naive one-factor SEM might find `n` loading on the tails when the true common driver is just activity level, and `n` is riding on the correlation.

Also, at *very low* intensity, `n` cannot be estimated reliably at all — the Hawkes MLE has too few events to distinguish `α` from `β`, so `n_day` on quiet sessions is essentially noise. Including those sessions contaminates the SEM.

**Five ways to disentangle. The paper does #1 as a filter, #2 as the primary analysis, #3 and #4 as robustness.**

**#1 Drop very-low-activity sessions.** Impose a minimum-event-count gate (bright-line: drop bottom 10% of sessions/hours by event count). Below the gate `n` is unreliable AND all tails are small anyway. This is a *scope restriction*, not a disentanglement.

**#2 Partial-out volume before the SEM.** For each of the three tail metrics, first regress on `log(mean λ)` and keep the residuals:

$$
\text{tail\_metric}_{\text{residual}} \;=\; \text{tail\_metric} \;-\; \hat{\beta} \cdot \log(\text{mean } \lambda)
$$

Then run the one-factor SEM on the residuals. What survives is variation orthogonal to activity level. If `n_day` still loads on the residuals, the effect is real. If loadings collapse, mean λ was doing all the work.

**#3 Two-factor SEM (the primary Step D analysis).** Instead of one hidden `θ_day`, fit two hidden factors:

$$
\text{tail}_i \;=\; a_i \cdot \theta^{\text{activity}}_{\text{day}} \;+\; b_i \cdot \theta^{\text{criticality}}_{\text{day}} \;+\; \varepsilon_i \qquad i \in \{\text{latency},\, \text{adv\_pnl},\, \text{return}\}
$$

Where `θ_activity` is identified with `log(mean λ)` and `θ_criticality` with `n`. Both load on the three tails simultaneously. Report the two sets of loadings `(a_i, b_i)` and compare their magnitudes. Interpretation:

- **`b_i` >> `a_i` for all three tails**: criticality drives the tails, activity is secondary. **Theory-driven claim confirmed.**
- **`a_i` >> `b_i`**: activity drives everything, criticality is spurious. **Headline collapses to "big days have big tails".**
- **Both meaningful**: both matter, paper reports the decomposition rather than a single-driver story.

**#4 Stratified analysis (matched on activity).** Bin the panel into deciles by `log(mean λ)`. Within each decile (activity approximately fixed), does `n` still explain variation in the three tails? Regress each tail on `n` within each decile. If the coefficient is stable across deciles → `n` effect is real, not activity-mediated. If it changes sign or shrinks to zero in low-decile → the effect was activity all along.

**#5 Volume-normalised tail metrics.** Instead of absolute `|adv_pnl_top_decile|`, use per-fill *mean* adverse markout. Instead of absolute `p99_lat`, use latency conditional on queue depth (mean wait per unit backlog). This strips activity level out of each tail metric before the SEM sees it. If the SEM factor structure still holds, the effect is per-event and not driven by count.

**Which of these five is the paper's headline?** After further reflection: **not #3 (two-factor SEM)**, but a **five-observed-variables / one-hidden-factor SEM** — which is estimating a specific, named model we call the **MAL-extended Hawkes** (Market Activation Level). Described next.

**MAL — Market Activation Level. A named model extension of standard Hawkes.**

Standard Hawkes fits fixed parameters `(μ, α, β)` per session and doesn't attempt to explain *why* those parameters land where they land session-by-session. **MAL says: they don't just land — they're jointly determined by a latent per-session (or per-hour) state `θ_MAL` that also determines the tail metrics.**

Formally, the Hawkes intensity is parametrised by MAL:

$$
\lambda(t) \;=\; \mu(\theta_{\mathrm{MAL}}) \;+\; \sum_{t_i < t} \alpha(\theta_{\mathrm{MAL}}) \cdot e^{-\beta(\theta_{\mathrm{MAL}})\,(t - t_i)}
$$

And the outcome tails are also functions of MAL:

$$
\log|\mathrm{adv\_pnl\_top\_decile}| \;=\; g_1(\theta_{\mathrm{MAL}}) + \varepsilon_1
$$

$$
1/\nu \;=\; g_2(\theta_{\mathrm{MAL}}) + \varepsilon_2
$$

$$
\log p99_{\mathrm{lat}} \;=\; g_3(\theta_{\mathrm{MAL}}) + \varepsilon_3
$$

The paper takes each `g_i(·)` linear (i.e., linear-factor SEM), and identifies `θ_MAL` by SEM on the five observables `(log mean λ, n, log |adv_pnl|, 1/ν, log p99_lat)`. Nonlinear extensions are natural but not the paper's headline.

**Positioning against prior Hawkes variants (this is what makes MAL a new model, not a repackaging).**

| model | latent state? | acts on `μ` only? | acts on `n` too? | drives outcome tails? | continuous or discrete? |
|---|---|---|---|---|---|
| Standard Hawkes | no | — | — | no | — |
| State-dependent Hawkes (Morariu-Patrichi & Pakkanen 2018) | yes — book queue state | no | no | no | discrete |
| Cox-Hawkes / doubly stochastic (Da Fonseca, Zaatour) | yes — stochastic baseline | yes | no | no | continuous |
| Regime-switching Hawkes (Filimonov-Sornette 2015 flash-crash) | yes | yes | yes | no | discrete |
| Marked Hawkes (Rambaldi, Bacry, Lillo) | no (marks are observed) | — | — | no | — |
| **MAL-extended Hawkes (this paper)** | **yes — continuous latent** | **yes** | **yes** | **yes — three tails jointly** | **continuous** |

MAL sits in an unoccupied cell: **one continuous latent driver of multiple observables (both Hawkes parameters AND outcome tails)**. That's the model-theoretic contribution beyond the empirical unification claim.

**Estimation.** The MAL model IS the five-observed / one-hidden SEM described below. The SEM is the estimator; MAL is the underlying model it estimates. Standard identification convention: `Var(θ_MAL) = 1`; five loadings `a_1..a_5` fit by ML on the 5×5 sample covariance across 731 days × 3 streams.

**Real-time estimation of `θ_MAL(t)`.** For live trading we only need the arrival-side observables — the online `λ̂(t)` from Step E and a rolling estimate of `n̂(t)`. Then `θ_MAL(t)` is a linear combination of those two matched to the fitted loadings. Result: **one real-time market-state number** that a Shadow POV algorithm gates on — a unified signal for adverse-selection risk, current volatility, and expected latency queue.

**Paper's headline, restated in MAL form.**

> "We propose the **Market Activation Level (MAL)** — a continuous latent state that jointly modulates message-arrival Hawkes parameters and the tails of price returns, maker adverse-selection cost, and decoder latency. We fit MAL as a five-observed one-hidden-factor SEM on 731 days × 3 CME futures products (ES, NQ, BTC), find joint-variance explanation of X%, and show that a real-time MAL estimator built from the online intensity estimator serves as a unified toxicity/volatility signal for a Shadow POV execution algorithm."

That is a *stronger* framing than "we found three correlations" — it's a **named model** with theoretical positioning against the existing Hawkes literature, an explicit econometric identification path, and a practical deployment story.

**The cleaner primary SEM: `n` AND `mean λ` are BOTH observed shadows of one deeper hidden factor.**

Instead of choosing between `n` and `mean λ` as the driver, or fighting them against each other in a two-factor model, treat both as *observed proxies* of the same underlying latent market state. That state also drives the three tails. So the SEM has **five observed variables all loading on one hidden factor `θ`**:

$$
\log(\text{mean }\lambda) \;=\; a_1 \cdot \theta \;+\; \varepsilon_1
$$

$$
n \;=\; a_2 \cdot \theta \;+\; \varepsilon_2
$$

$$
\log|\text{adv\_pnl\_top\_decile}| \;=\; a_3 \cdot \theta \;+\; \varepsilon_3
$$

$$
1/\nu \;=\; a_4 \cdot \theta \;+\; \varepsilon_4
$$

$$
\log p99_{\text{lat}} \;=\; a_5 \cdot \theta \;+\; \varepsilon_5
$$

The hidden `θ` is the underlying "market activation state" — we can't observe it directly but we see five shadows of it: two arrival-process summaries (intensity, criticality) and three tail metrics (adverse selection, return, latency).

**Advantages over the two-factor architecture:**

1. **Doesn't force us to disentangle `n` from `mean λ`** — both are just proxies. In empirical panels they're correlated because they reflect the same latent state, and the model recognises that natively.
2. **Cleaner interpretation**: *one* hidden market state manifests as (i) higher intensity, (ii) higher criticality, (iii) fatter return tails, (iv) more adverse maker selection, (v) longer decoder latency queues.
3. **Removes the "which drives which" ambiguity**: no need to argue whether `n` or `mean λ` is causal. Both are shadows.
4. **Bigger set of observed variables → the single factor is more identifiable** (5 loadings on 1 factor vs 3 loadings on 1 factor in the earlier formulation).
5. **Fits the physical intuition** exactly: on genuinely busy days the market is more reflexive, more voluminous, more toxic, and more tail-heavy. All five observations rise together not by coincidence but because there's one underlying activation state driving them.

**Interpretation buckets for the five-observed one-factor SEM:**

- **All five load strongly on `θ`** (say `|a_i|` significant, one-factor R² ≥ 40%) → unification confirmed. One hidden state, five observed shadows. `θ` can be named informally ("market activation state" or "regime intensity") but the SEM doesn't require picking a single microstructure interpretation of which physical quantity is causal.
- **The two arrival summaries load, tails don't**: activity ≠ tail toxicity. Unification collapses. Publishable as a negative result: "activity and tails are decoupled in this data".
- **Tails load, arrival summaries don't**: the tails share a common driver but it isn't the Hawkes-fit we're computing. This would suggest `n` and `mean λ` are inadequate proxies of the underlying state; we'd need a different arrival-side observable.

**Contingency plan.** If the one-factor SEM captures < 30%, try a two-factor model with `θ_activity ~ log mean λ` and `θ_criticality ~ n` (approaches #3 above). If neither works, drop the unification claim from the paper and revert to A/B/C/E/F as standalone results.

**Confirmed: low-intensity gate applies before any SEM fitting.** Drop the bottom 10% of sessions/hours by event count as a preprocessing step. At very low intensity `n` is unreliable AND all three tails are tiny — those windows just add noise. Report the gate explicitly and how much of the panel it drops (likely overnight-like quiet hours if we ran the analysis intraday). This is not a fix for the confound between `n` and `mean λ`; the five-observed one-factor architecture is the fix. The gate is a data-quality prerequisite.

**Why we're looking for a hidden variable in the first place.** Three separate research communities have historically treated three tail phenomena as *unrelated topics*:

1. **Infrastructure engineers** (like the fast_send paper) worry about the **decoder-latency tail** — why does p99 wire-to-book time blow up on some days? They measure it in microseconds.
2. **Execution quants** worry about the **maker-adverse-selection tail** — why does a passive quoter get run over on some fills but not others? They measure it in ticks of markout.
3. **Stochastic-analysis researchers** (Bacry, Bouchaud, Jaisson-Rosenbaum, ...) worry about the **return-fat-tail exponent** — why do daily returns have power-law tails? They measure it as an exponent `ν` on the tail of the return distribution.

Each of these three has its own vocabulary and its own separate literature. **The claim behind Step D is that all three are literally different measurements of the same underlying physical property of the market on that day** — namely, "how close was the Hawkes arrival process to critical branching (n → 1) today". When arrivals cluster hard, packets fatten (→ latency tail), passive quotes get filled by informed aggressors (→ adverse-selection tail), and price moves compound (→ return fat-tail). All three tails inflate together.

**What is `θ_day` a hidden variable *of*?** It represents that per-day "market bursiness state". It is NOT directly observed — we can't just look up a number and say "θ_day = 0.83". Instead we *infer* it from the three observed quantities `(n_day, |adv_pnl_top_decile_day|, 1/ν_day)`. The whole SEM apparatus exists to answer the question: **do the three observed metrics move as if they were driven by one hidden number?**

**What SEM is.** **Structural Equation Modeling** is a statistical framework for asking that exact question. A **one-factor SEM** assumes there is one hidden variable `θ_day` and that each of the three observed quantities is a scaled copy of it plus independent noise:

$$
n_{\mathrm{day}} \;=\; a_n \cdot \theta_{\mathrm{day}} \;+\; \varepsilon_n
$$

$$
\big|\mathrm{adv\_pnl\_top\_decile}_{\mathrm{day}}\big| \;=\; a_p \cdot \theta_{\mathrm{day}} \;+\; \varepsilon_p
$$

$$
\frac{1}{\nu_{\mathrm{day}}} \;=\; a_{\nu} \cdot \theta_{\mathrm{day}} \;+\; \varepsilon_{\nu}
$$

- `a_n, a_p, a_ν` are called **loadings** — how strongly each observed metric is coupled to the hidden variable.
- `ε_n, ε_p, ε_ν` are independent noise terms with variances `σ_n², σ_p², σ_ν²` (each metric has its own idiosyncratic noise the hidden variable doesn't explain).
- `θ_day` is standardised so its variance is 1 (identification convention — otherwise you could absorb a scale into either `θ` or the loadings).
- The paper fits `(a_n, a_p, a_ν, σ_n², σ_p², σ_ν²)` by maximum-likelihood on the 3×3 sample covariance matrix across 731 days per stream.

**The three concrete quantities being compared.** Explicitly:

1. `n_day` = branching ratio from the §5.4 panel column. Range roughly `[0.75, 0.98]` empirically. High `n_day` = bursty day.
2. `|adv_pnl_top_decile_day|` = mean absolute adverse P&L of maker fills in the top λ̂-decile that day, measured in ticks. High value = toxic day.
3. `1/ν_day` = reciprocal of the return tail exponent. `ν_day` is fitted from the Hill estimator on the day's absolute log-return sample; low `ν_day` = fat-tailed day, so `1/ν_day` is high on fat-tailed days.

We take `1/ν_day` rather than `ν_day` so all three metrics point the same direction (bigger = more extreme).

**What we're trying to show / learn.** Three cases and their interpretations:

- **If the one-factor SEM captures 40%+ of the joint variance** across the three metrics: **unification confirmed**. On any given day the three tails move together because one property of the arrival process drives all three. The three literatures are studying the same thing from different angles. Practical consequence: if a trader observes `n_day` from their MDP3 monitoring, they can predict `1/ν_day` (return fat-tailedness) and `|adv_pnl|` (execution toxicity) for that same day. **This is the paper's core empirical claim.**
- **If it captures ~10-15%**: the three tails correlate but the correlation is weak; there are multiple hidden factors, not one. The paper backs off to "arrival intensity is one contributor among several".
- **If it captures ~0-5%**: the three tails are essentially independent on the day-to-day timescale, and the unification claim collapses. The paper still stands as a characterisation of each individual tail, but the "one signal" framing goes away.

**Where §5 comes in**: `n_day` comes from §5.4; `|adv_pnl_top_decile|` needs per-fill `λ̂` (which needs the online estimator initialised from §5.4's fitted params, plus §5.5's validation to know we can trust it); `1/ν_day` comes from a Hill-estimator computation done alongside §5.1-§5.3 on the return time series of the same session. Assembling one row of `(n_day, |adv_pnl|, 1/ν_day)` **requires the full §5 panel row** to have already run for that session.

**Why `n_day` (branching ratio) and not `mean λ_day` (mean intensity) as the hidden-variable target?**

For a stationary Hawkes process the mean intensity is

$$
\overline{\lambda} \;=\; \frac{\mu}{1 - n}
$$

so observed mean-intensity blends the *exogenous news-flow rate* `μ` with the *endogenous reflexivity* `n`. Two very different days can produce the same mean λ:

- **Case A** — high `μ`, low `n` (say 0.3). Many external triggers; each doesn't spawn children. Result: many messages spread out, Poisson-like. **Thin tails everywhere.**
- **Case B** — low `μ`, high `n` (say 0.9). Few external triggers; each spawns a cascade. Result: fewer total messages but bursty. **Fat tails everywhere.**

Same total volume, opposite tail behavior. If we used mean λ as the hidden variable, the SEM would call A and B the same day — the exact opposite of what we want.

**`n` (branching ratio) is the *shape* parameter** — it directly measures how self-exciting the process is, independent of raw event count. It's also the theory-motivated pick: the Jaisson-Rosenbaum rough-vol scaling theorem gives rough vol as `n → 1`, not as `μ → ∞`. Mean-rate has nothing to do with the rough-vol connection.

Concrete predictions:

| hidden variable | predicts fat return tails? | predicts adverse selection? | predicts latency tail? |
|---|---|---|---|
| `n_day` (branching ratio) | **YES** — cluster size ~ 1/(1−n), directly fat-tail-producing | **YES** — bursts concentrate information | **YES** — bursts build queue |
| `mean λ_day` (mean intensity) | maybe — but confounded by news vs reflexivity | maybe — high volume ≠ high toxicity per fill | correlated but confounded with rate |

**What the paper does**:

- **Primary SEM**: three legs load on `θ_day` where `θ_day` is aligned with `n_day` (up to affine).
- **Robustness SEM**: refit the same three legs with `log(mean λ_day)` as the hidden variable. Report ΔR² between the two.

If the `n_day`-based SEM explains more joint variance than the `mean λ_day`-based SEM, the theory wins: **reflexivity, not volume, drives the three tails.** That's a sharper claim than "burstiness matters."

**Could either work?** In principle yes; empirically they're correlated (busy days tend to be both high-`μ` and high-`n`). But the *mechanism* — rough vol via QHawkes, cluster-driven adverse selection, burst-driven queue — needs `n`, not mean λ. If mean λ alone sufficed, the whole rough-vol literature would be about volume, not near-critical Hawkes. It isn't, and that's the reason.

**Sub-daily resolution — hourly SEM likely gives *stronger* contemporaneous results than daily.**

**Important framing — contemporaneous, not predictive.** The SEM claim is a *contemporaneous* one: within a single time window, do the three (or four, if we add latency) tail metrics co-move because they share one hidden driver? It is NOT a claim that previous-hour statistics predict next-hour outcomes.

- **What the hourly SEM measures**: compute `n_hour`, `|adv_pnl|_hour`, `1/ν_hour`, `log p99_lat_hour` all from data *within hour t*. Fit SEM on the tuple `(n_t, |adv_pnl|_t, 1/ν_t, p99_lat_t)` across all 14,000 hours. Same-window co-variation.
- **What the hourly SEM does NOT measure**: cross-time-window prediction, e.g., "`n_{t-1}` predicts `|adv_pnl|_t`". That is a distinct (and weaker) question — serial autocorrelation of hourly `n` is not strong enough to make that work, and the paper does not claim it.

**Why hourly should give sharper contemporaneous results than daily.** Two mechanical reasons:

1. **A daily aggregate washes out burst-vs-calm co-variation within the day.** If the SEM factor is strongest during bursts (which it must be — bursts are the whole story), daily averaging attenuates the signal. Hourly preserves the intraday contemporaneous covariance that a daily aggregate blurs.
2. **More rows = tighter loadings.** 14,000 hours vs 2,200 days → smaller standard errors on the loadings and on the joint-variance-explained scalar.

**Prediction**: if daily SEM captures, say, 40% of joint variance, hourly on the same underlying structure should capture 55-65%. The hourly panel exposes contemporaneous burst dynamics that daily averaging hides.

**Compute the three legs at hourly resolution.**

- **Return tail per hour** — Hill estimator on that hour's returns. Full RTH day has ~10⁴ 1-second returns; one hour has ~3,600 — enough for a stable Hill fit.
- **Hawkes fit per hour** — refit `(μ, α, β)` on each hour's arrivals separately → `n_hour`. Watch for numerical instability on very quiet hours.
- **Adverse selection per hour** — bucket per-fill markouts by `exec_ts` hour.
- **Latency-tail per hour** — Lindley recursion on each hour's arrivals separately.

**Downsides.** Finer window = noisier per-window estimates. Hill estimator, Hawkes MLE, and per-fill markout means each get noisier at ~10× fewer observations per window. On quiet hours the Hawkes MLE may not converge cleanly — flag those hours and drop.

**What hourly gives up.** Cross-day regime effects (FOMC vs matched controls, roll-neighbor days, VIX day-of-cycle) are day-scale, not hour-scale. For those we still need the daily panel.

**Combined plan**: run **both** timescales, both contemporaneous SEMs.

- **Daily SEM (~2,200 rows)** — captures cross-regime day-scale variation. Report as *primary result matching Filimonov-Sornette-era prior art*.
- **Hourly SEM (~14,000 rows)** — captures intraday burst-vs-calm co-variation. Report as *stronger contemporaneous evidence for the one-factor structure*. **This is likely where the paper's biggest ΔR² number comes from.**

If both show a strong one-factor loading, the unification claim is robust across timescales. If daily is strong but hourly weak, the factor lives at day-scale (news / regime) and not within-day (burst dynamics) — informative but suggests a different mechanism than the QHawkes literature predicts. If hourly is strong but daily weak, the factor is a *within-day* structural property that averages out to noise across regimes — also informative and consistent with a microstructure-driven mechanism.

**How to measure the latency tail per session — a queue simulation over the arrival tape.**

The three tails in Step D's unification story include a *decoder-latency tail*. For each session's arrival tape we need a scalar summary of that day's latency-tail severity. We don't have per-day p99 latency directly measured in the historical corpus (fast_send did it on one live day only). What we can do is simulate a simple queue model over the historical arrival tape and report the resulting latency percentiles as the per-session "latency tail" measurement.

**Assumptions.** The simulation isolates *queue-driven* latency, not wire-driven latency. Specifically:

- Socket read is instantaneous — pcap `recv_time` IS the arrival timestamp to the decoder.
- One decoder = one server, FIFO service.
- Service time per message follows the fast_send fit: floor + slope × position-within-packet.
- No packet loss, no retransmission, no gap recovery.

Under those assumptions, the entire latency tail is a function of the arrival process. Bursty arrivals → queue builds → tail inflates. Poisson arrivals at the same mean rate → queue rarely builds → thin tail.

**Lindley recursion for M/G/1.** For an arrival sequence `t_1 < t_2 < ...` with interarrival gaps `A_i = t_{i+1} − t_i`, and service times `S_i` (defined below), the queue wait time `W_i` for message `i` obeys:

$$
W_{i+1} \;=\; \max\!\big(0,\; W_i + S_i - A_i\big), \qquad W_0 = 0
$$

- If `A_i > W_i + S_i` (next arrival comes after the current message finishes serving), the queue empties, `W_{i+1} = 0`.
- If `A_i < W_i + S_i` (next arrival comes while current is still being served), the queue builds, `W_{i+1} = W_i + S_i − A_i`.

Total end-to-end latency for message `i` is then `L_i = W_i + S_i`.

**Service time from fast_send's fit.** Each message's service time follows the linear model established in fast_send.tex:

$$
S_i \;=\; \text{floor} \;+\; \text{slope} \cdot \text{idx}_i
$$

- `floor ≈ 7 µs` — fixed per-message decode cost
- `slope ≈ 0.3 to 0.97 µs per message` (varies by stream; fast_send reports 3.1× spread NQ vs ZN)
- `idx_i` — position of message i within its containing UDP packet (0 for the first message in each packet)

`idx_i` comes directly from the msgtape — it's a per-message field. So the service-time model is fully parameterized by (floor, slope, msgtape).

**One-pass per-session simulation.** In `O(n)`:

```python
W = 0.0
lat = []
for i in range(n):
    A = t[i+1] - t[i]      # interarrival gap
    S = floor + slope * idx_in_packet[i]
    lat.append(W + S)       # end-to-end latency
    W = max(0.0, W + S - A)
```

**What lands in the §5 panel.** Per (session, stream), we save five scalars from the simulated latency distribution:

$$
p50_{\text{lat}}, \; p90_{\text{lat}}, \; p99_{\text{lat}}, \; p999_{\text{lat}}, \; p9999_{\text{lat}}
$$

The paper uses `p99_lat` (or `log p99_lat`) as the third leg of the SEM in Step D:

$$
\log p99_{\text{lat}, \text{day}} \;=\; a_{\ell} \cdot \theta_{\text{day}} \;+\; \varepsilon_{\ell}
$$

alongside `n_day` and `|adv_pnl_top_decile|`. If a single hidden `θ_day` loads all three, the unification holds.

**Robustness — the shuffle test also validates the queue mechanism.** Just like §5.3, run the queue simulation on:
- The original arrival tape → get `p99_lat_original`
- A Fisher-Yates shuffle of the same arrival tape → get `p99_lat_shuffled`

Expected: on the real tape, `p99_lat` inflates because bursts build the queue. On the shuffled tape at the same average rate, `p99_lat` collapses toward the M/M/1 baseline (no bursts, no queue). The **latency collapse ratio** `p99_lat_original / p99_lat_shuffled` is a clean per-session measurement of how much of the day's latency tail was ordering-driven vs marginal-driven. Add it to the panel as a bonus column.

**Free parameter choice for (floor, slope).** The fast_send fit gives per-stream point estimates, but they're one-day estimates. Choices:

- **Fixed at fast_send's point estimates per stream** — simplest; simulation results depend on those constants.
- **Per-session refit** — if we had live latency measurements per session we could refit, but we don't.
- **Sensitivity report** — sweep (floor, slope) over a small grid and confirm the *ordering* of per-session `p99_lat` across the 731-day panel is stable. If it is, the specific values matter less than the *distribution of p99 across days*, which is what feeds Step D's SEM.

The last option is what we do. Report main results with fast_send's point estimates; supplementary appendix with a sensitivity sweep.

**Per-session simulation runtime.** A session has ~10⁷ messages; one Lindley pass is `O(n)` in Python with numpy → seconds per session. Trivial vs the Hawkes MLE fit which is the compute bottleneck.

**Panel column summary — the three legs of the SEM in Step D are now:**

| leg | per-session scalar | source |
|---|---|---|
| latency tail | `log p99_lat_day` | queue simulation above |
| adverse-selection tail | `log |adv_pnl_top_decile_day|` | fill_tape from §6-7 of paper, per-fill `λ̂` from Step E |
| return tail | `1/ν_day` (or `log 1/ν_day`) | Hill estimator on the day's return sample |

Panel columns feed directly into the one-factor SEM.

**Where Step D sits in the dependency chain — and why we do it last.** Step D is NOT a prerequisite for any other step. Steps E and F both use only the per-session Hawkes fit (from B) and the online estimator; neither needs the SEM. The dependency graph is:

```
B (per-session fit) ──► E (online estimator) ──► C (adverse-P&L regression) ──┐
                                             ──► F (passive-quoter frontier)  │
                                                                              ▼
                                                              D (unification SEM)
```

Step D consumes the outputs of B, C, and a separate return-tail computation. If it fails (small joint-variance capture), the paper still stands on A/B/C/E/F. So D is the *ambitious add-on that either upgrades the paper's thesis or gets dropped*, and it runs at the end.

**Why Step D is worth doing (and worth taking the risk of a failed SEM fit).** Three reasons this step lifts the paper from "solid Hawkes replication" to "cross-disciplinary contribution":

1. **Three research communities don't currently talk to each other.** These three tails live in three separate literatures with different vocabularies and journals:
   - **Infrastructure engineers** (fast_send, Paxson & Floyd, network researchers) — SIGCOMM, IEEE Trans. Networking. Care about packet queueing and latency percentiles.
   - **Execution quants** (Kyle 1985, Glosten-Milgrom, Bellia, VPIN, PIN, Aligrithm-style fast-fills-are-bad practitioner work) — J. Finance, Review of Financial Studies. Care about maker adverse-selection cost.
   - **Stochastic-analysis / rough-vol researchers** (Bacry-Muzy, Jaisson-Rosenbaum, Blanc-Donier-Bouchaud, Gatheral) — Quant Finance, Ann. Applied Probability. Care about return-tail exponents and rough volatility.

   They almost never cite each other. A paper that says "these three seemingly-different phenomena are one signal from one arrival process" gets cited from all three communities — that's the mechanical reason it's a "bombshell" in citation terms.

2. **Closes a decade-old theoretical loop between rough-volatility theory and CME microstructure data.** The rough-vol revolution (Gatheral-Jaisson-Rosenbaum 2014-2018) established that SPX realized volatility has Hurst exponent H ≈ 0.1 — surprisingly rough. Their theoretical *mechanism*: near-critical Hawkes microstructure produces rough vol as a scaling limit (Jaisson-Rosenbaum 2015; extended by Blanc-Donier-Bouchaud 2017 QHawkes for fat-tailed returns). This is a **theoretical claim proven mathematically on a stylised model**. Nobody has directly measured the microstructure Hawkes and shown its per-day fit variables track per-day fat-tail exponents at empirical scale — which is exactly what Step D does on 731 days × 3 products. If it works, we've closed the theory-vs-data loop that's been open for a decade.

3. **Gives a real-time signal for something people have historically only measured after the fact.** VPIN, realized volatility, tail-exponent estimation — all backward-looking, all require at least minutes of data to compute. If a single hidden factor `θ_day` explains all three, then the real-time `λ̂(t)` from Step E is a proxy for all three at once — a single O(1)-updateable signal that tracks:
   - Current volatility rate
   - Current adverse-selection cost per fill
   - Current fat-tailedness of returns

   Practically, adding a real-time `λ̂` to a trading system doesn't require rebuilding it around a new metric — it plugs in. **One estimator, three P&L drags monitored simultaneously.**

**What could make it *not* a bombshell.**

- **If prior work already showed this**: it doesn't. Filimonov-Sornette hint at the connection but only measure branching ratio, not the three-way SEM decomposition. Blanc-Donier-Bouchaud prove it theoretically. Nobody has published the empirical SEM at 731-day × 3-product scale with per-fill markouts as one of the three loadings.
- **If the SEM captures small joint variance (< 15%)**: then the three tails correlate but aren't really one signal. Paper reverts to a solid characterisation paper without the unification headline. Still publishable as A/B/C/E/F, just without a big thesis.
- **If it works only on one product**: niche result rather than a general property — the paper narrows its claim to "on ES this unification holds, on NQ/BTC less so".

**Honest calibration of impact.** "Bombshell" here is relative to the field, not to science broadly. Not Nature-tier, not a Nobel; realistic outcome if D works is an arXiv paper cited from three communities → a few hundred citations over 3-5 years, precedent for follow-up work on equities/FX/spot crypto, and a practical `λ̂`-gate in Shadow POV. That's rare enough for a specialist paper to earn the framing.

**Why doing D last minimises the downside.** Because D consumes the outputs of A/B/C/E and doesn't feed them, we can run the entire pipeline and get every §5 panel row, every per-fill markout, every Hawkes fit, every attribution regression, and *only then* try the SEM. If it fails, the failure is contained — we simply omit Step D from the paper and ship the rest. If it works, the paper picks up the unification headline for essentially free at the end. Either way the compute already done is not wasted.

**Step E — Feed the per-session fit into the online estimator, which is what powers everything downstream (§4 of the paper).**

First, what the **online estimator** is and why we need it. Section §4 gives us a batch MLE fit: hand it a session's arrival tape, and after some seconds of computation it returns the three fitted parameters `(μ, α, β)` for that session. That's fine for characterisation, but the paper's real question is about **per-event decisions**: at every maker fill during the day, we need to know `λ̂` (the current message-arrival intensity) *at that exact fill's timestamp*. Doing this naively — recomputing `λ(t) = μ + Σ_{tᵢ < t} α·exp(-β(t - tᵢ))` from scratch at every query time — is `O(n)` per query. With millions of events per session and hundreds of thousands of maker fills, that's `O(n²)` per session, hours per day, infeasible.

The exp-Hawkes kernel is **Markovian** (§3.1) — meaning the current intensity summarises everything you need about the past. Concretely, define one running number `S(t)` that walks forward event-by-event:

$$
S(t) \;=\; \sum_{t_i \,<\, t} \alpha \cdot e^{-\beta (t - t_i)}
$$

Between events, `S(t)` decays exponentially: `S(t + Δ) = S(t) · exp(-β·Δ)`. At each new event, `S` jumps by `α`. So the update rule is `O(1)`:

$$
S(t^+) \;=\; S(t) \cdot e^{-\beta (t - t_{\text{last event}})} \;+\; \alpha
$$

$$
\hat{\lambda}(t) \;=\; \mu + S(t)
$$

That's the **online intensity estimator**. Ship it in Python as `arrival_paper.online.HawkesIntensityEstimator`, initialise with `(μ, α, β)` from that session's batch fit (from §5.4's panel), and stream events through it. Each maker fill on that session gets an `λ̂` tag in real time.

**Directional variant — 1-D vs 2-D `λ̂`.** The single-`λ̂` version above treats all message arrivals as one stream. For a passive quoter resting on *both* sides that's a reasonable first pass — "is the whole book bursty right now?" — but a directional version is stronger. Fit a 2-D Hawkes on `{bid-update, ask-update}` arrivals (§3.3) and run two online estimators in parallel giving `λ̂_bid(t)` and `λ̂_ask(t)`. Then:

- **Bid-side updates cluster with bearish aggression** (sellers hitting the bid): high `λ̂_bid` → bid-side under pressure → resting bid quotes exposed to adverse selection.
- **Ask-side updates cluster with bullish aggression** (buyers lifting the ask): high `λ̂_ask` → ask-side under pressure → resting ask quotes exposed.
- **Directional imbalance `λ̂_ask − λ̂_bid`** = real-time proxy for signed flow pressure.

For a Shadow-POV-style algorithm (always buying, or always selling on the passive side) you gate on **the side you're quoting**, not the aggregate. This is why §3.3 is in the paper — the 2-D Hawkes powers the *directional* signed-markout prediction in §7.5 and gives Shadow POV a directional gate.

**Signed variant — `λ⁺(t)` (market-up) and `λ⁻(t)` (market-down).** The bid/ask split above is a *mechanical* split by book side; it's not the same as up-pressure vs down-pressure. A cancel on the bid updates the bid side (mechanical split says "bid-side event") but is actually bearish (someone is removing a buy quote → less demand). The cleaner signed variant partitions arrivals by *the direction they move price*, not the side of the book they touch.

**Event classification for the signed intensity.** For every MBO / trade record, tag it as `+`, `−`, or neutral:

- **`+` (buy-pressure / market-up)**
  - Trade with buy aggressor (someone lifted the ask)
  - Best-ask tick moves *up* by ≥ 1 tick (ask side thinned or lifted)
  - Best-bid tick moves *up* by ≥ 1 tick (buyers stepping in)
- **`−` (sell-pressure / market-down)**
  - Trade with sell aggressor (someone hit the bid)
  - Best-bid tick moves *down* by ≥ 1 tick (bid side thinned or hit)
  - Best-ask tick moves *down* by ≥ 1 tick (sellers stepping in)
- **Neutral** — everything else: same-price adds/cancels, deep-book updates that don't move BBO. Excluded from the signed streams.

Signed events are exactly the events that produce `Δmid ≠ 0`, so `λ⁺` and `λ⁻` track price-forming flow directly.

**Fit a 2-D Hawkes on the signed streams.** Let the two arrival streams be the `+`-tagged events and `−`-tagged events. Fit the standard bivariate exp-Hawkes (as in §3.3, just with different labels):

$$
\lambda^+(t) \;=\; \mu^+ \;+\; \sum_{t_i^{+} < t} \alpha_{++}\, e^{-\beta_{++} (t - t_i^{+})} \;+\; \sum_{t_i^{-} < t} \alpha_{+-}\, e^{-\beta_{+-} (t - t_i^{-})}
$$

$$
\lambda^-(t) \;=\; \mu^- \;+\; \sum_{t_i^{+} < t} \alpha_{-+}\, e^{-\beta_{-+} (t - t_i^{+})} \;+\; \sum_{t_i^{-} < t} \alpha_{--}\, e^{-\beta_{--} (t - t_i^{-})}
$$

Four excitation parameters: `α_{++}` self-excitation of buy pressure, `α_{--}` self-excitation of sell pressure, and two crosses (`α_{+-}` = does a sell shock lead to buy follow-through? `α_{-+}` = does a buy shock provoke sellers?). The signs of the crosses tell you whether the market is momentum-like (positive self-excitation, weak cross → trends persist) or mean-reverting (large cross-excitation → moves get faded).

**Three quantities the signed intensity gives you.**

- **Total intensity** `λ⁺(t) + λ⁻(t)` — equivalent to the aggregate `λ̂(t)` for price-forming events. This is the "how bursty is the market right now" number.
- **Signed pressure** `λ⁺(t) − λ⁻(t)` — the current directional imbalance. Positive → currently upward pressure; negative → downward pressure; near zero → two-sided flow.
- **Cross-excitation asymmetry** `α_{+-} − α_{-+}` — one-shot session-level number: is sell flow more likely to attract buy follow-through than the reverse? Or vice versa? Interesting per-day summary.

**Online estimator for the signed variant.** Same `O(1)` recursion, run in parallel — track four running accumulators `S_{++}(t), S_{+-}(t), S_{-+}(t), S_{--}(t)`, each updated on the arrival of its corresponding stream:

$$
S_{ab}(t^+) \;=\; S_{ab}(t) \cdot e^{-\beta_{ab} (t - t_{\text{last }b\text{-event}})} \;+\; \alpha_{ab} \cdot \mathbb{1}[\text{new event is type }b]
$$

$$
\hat{\lambda}^+(t) \;=\; \mu^+ + S_{++}(t) + S_{+-}(t)
$$

$$
\hat{\lambda}^-(t) \;=\; \mu^- + S_{-+}(t) + S_{--}(t)
$$

Ship as `arrival_paper.online.SignedHawkesEstimator` alongside the aggregate estimator.

**Trader use of the signed intensity.**

- **Passive quoter on the bid**: adversely selected when `λ⁻` is high (sellers hitting your bid). Gate: `λ⁻(t) < θ_⁻`.
- **Passive quoter on the ask**: adversely selected when `λ⁺` is high (buyers lifting your ask). Gate: `λ⁺(t) < θ_⁺`.
- **Taker who wants to buy**: prefer to aggress when `λ⁺` is low (calm buy-flow ⇒ less likely you're being front-run).
- **Directional-flow signal**: `sign(λ⁺ − λ⁻)` gives instantaneous flow direction; magnitude gives intensity.
- **Shadow POV working a buy program**: quote the bid, gate on `λ⁻`. High `λ⁻` = current selling storm; withhold quote, wait for calmer window.

**Unsigned intensity vs signed intensity — the volatility / momentum decomposition.** The two intensities give a clean split of the two things a trader cares about:

**Unsigned `λ(t) = λ⁺(t) + λ⁻(t)`** — the total rate of price-forming events. Directly a **realized-volatility-rate estimator**:

$$
\mathrm{E}\!\big[\,|\Delta\mathrm{mid}|\text{ per unit time}\,\big] \;\;\propto\;\; \text{tick\_size} \cdot \big(\lambda^+(t) + \lambda^-(t)\big)
$$

$$
\sigma^2(t) \;\;\approx\;\; \text{tick\_size}^2 \cdot \big(\lambda^+(t) + \lambda^-(t)\big) \cdot \mathrm{E}[\text{jump}^2]
$$

High unsigned λ = many price-forming events per second = high realized variance per unit time. This IS the instantaneous rate of quadratic variation of mid.

**Signed `λ⁺(t) − λ⁻(t)`** — the net directional pressure. Directly a **drift / instantaneous-momentum estimator**:

$$
\mathrm{E}\!\big[\,\Delta\mathrm{mid}\text{ per unit time}\,\big] \;\;\propto\;\; \text{tick\_size} \cdot \big(\lambda^+(t) - \lambda^-(t)\big)
$$

Sign gives the current directional bias; magnitude gives its strength.

**Two levels of "momentum" — they aren't the same thing.**

- **Level 1 — instantaneous drift** (from signed `λ`): what direction is the next micro-move likely to go? This is a *one-tick-ahead* signal.
- **Level 2 — persistent momentum** (from the cross-excitation matrix `α_{ab}`): does the current drift continue or get faded over the next `1/β` seconds? This is a *horizon* signal.

Level 2 lives in the four excitation cross-terms:

- If `α_{++} > α_{−+}` → a buy shock triggers *more* buys than sells → **buy momentum is persistent**.
- If `α_{−+} > α_{++}` → a buy shock triggers *more* sells than buys → **buy pressure is mean-reverting**.
- Symmetric for the sell side.

Combining:

| quantity | trader interpretation |
|---|---|
| `λ⁺ + λ⁻` | current volatility rate (rate of quadratic variation of mid) |
| `λ⁺ − λ⁻` | current signed drift (level 1 instantaneous momentum) |
| `α_{++} − α_{−+}` | is upward pressure persistent or mean-reverting? (level 2 up momentum) |
| `α_{--} − α_{+-}` | is downward pressure persistent or mean-reverting? (level 2 down momentum) |
| `n_{++} = α_{++} / β_{++}` | branching ratio of self-exciting buys (near-critical → very long/large bursts) |

**Practical takeaway.**

- Volatility-based strategies (e.g., short vol vs long realized) → use unsigned `λ(t)`
- Directional next-tick strategies → use signed `λ⁺(t) − λ⁻(t)`
- Longer-horizon directional bets (predict next N-second Δmid) → also inspect the cross-excitation matrix to know whether the current signed pressure will persist or fade over the horizon of interest.

**Practical connection to the mid-price**: since `λ⁺` counts events that mechanically move mid up and `λ⁻` counts events that mechanically move mid down, the integral relation `E[Δmid over next window] ≈ tick_size · ∫ (λ⁺(s) − λ⁻(s)) ds` is a first-order price-drift forecast. That's not a novel result — it's essentially the "signed order flow imbalance" measure known to microstructure since Kyle (1985), but the Hawkes machinery gives it a decaying-echo memory that plain OFI doesn't.

**Sanity check on Day 1: does `λ̂(t)` correlate with realized volatility?** If the unification claim of Step D (§8) is to hold, `λ̂(t)` should track short-window volatility on intraday timescales — because high message-arrival intensity means many events per second, many opportunities for mid to move, therefore high realized variance. Concretely, on the pilot day (2025-03-10 NQ):

- Compute `λ̂(t)` at each event using the online estimator with the fitted parameters
- Compute rolling 5-second realized volatility of mid-price at the same timestamps: `σ_5s(t)² = Var[log(mid(s)) − log(mid(s−5s))]`
- Scatter-plot pairs `(λ̂(t), σ_5s(t))`

**Expected shape**: monotone-positive relationship, likely a power law `σ_5s ~ λ̂^α` for some `α > 0`. If we see this on Day 1, the unification claim is on solid ground before we even fit the SEM in Step D. If the relationship is flat or weak, we've caught the problem *early*, and we back off the unification claim before wasting weeks of compute on §8.

**Analogous sanity check: does `λ̂` correlate with the return tail exponent `1/ν_day`?** This is the same idea one level up. Aggregate `λ̂(t)` over a session (e.g., mean `λ̂`, or a high percentile) → get one number per session. Then correlate that number across 731 sessions with `1/ν_day` (tail exponent of returns that same session). Expected: strongly positive. If it holds, `λ̂` is a real-time proxy for both instantaneous volatility AND session fat-tail — which is exactly the "three tails, one signal" claim of Step D restated on the microstructure timescale.

**What "everything downstream" concretely means.** Every result in §7-9 of the paper needs per-fill or per-window `λ̂` tags produced by this estimator:

| downstream result | uses `λ̂` how |
|---|---|
| §7.1 mid-anchored markouts decile-bucketed by `λ̂` | assign every trade a `λ̂` at trade time; bucket; average markout per bucket |
| §7.2 maker-fill markouts decile-bucketed by `λ̂` | same idea, on the fill_tape, using `λ̂` at exec_ts |
| §7.3 attribution regression `adv_pnl ~ log λ̂ + controls` | headline ΔR² number of the paper |
| §7.4 cross-day stability of β₁ coefficient | daily fit → distribution across 731 days |
| §7.5 directional signed markouts | needs 2-D `λ̂_bid`, `λ̂_ask` from §3.3 |
| §7.6 attribution decomposition (arrival vs order-book state) | `λ̂_at_submit`, `λ̂_at_fill`, `λ̂_delta` per fill |
| §7.7 queue-dynamics slices (fast/slow × `λ̂`-decile) | stratify fills by `λ̂` decile then by time-in-queue |
| §8 unification SEM factor construction (Step D above) | daily summaries of `λ̂` feed into `θ_day` factor |
| §9 passive-quoter frontier (Step F below) | per-fill `λ̂` values compared to gating threshold |
| §5 (paper's latency section) | `λ̂ → predicted span → predicted latency percentile` |

Without the online estimator producing `λ̂` on every event, none of these are computable. That's what "everything downstream" means.

**Where §5 comes in**: without §5.4 giving us `(μ, α, β)` for that specific session, we couldn't initialise the online estimator. Without §5.5's KS validation, we wouldn't know whether the `λ̂` values we produce are trustworthy (on sessions where KS fails, the fitted kernel is misspecified and `λ̂` is unreliable). So §5.4 + §5.5 together are what make the downstream `λ̂` values load-bearing.

**Step F — Anchor the practical passive-quoter frontier (§9 of the paper).**

First, what a **passive quoter** is. A market maker who rests limit orders at the best-bid and best-ask, providing liquidity to whoever aggresses. When the market is quiet, that maker earns the spread cheaply. When the market is toxic (someone with a strong information advantage aggresses), the maker gets filled *precisely* because the price is about to move against them. That's **adverse selection** — the bad fills a passive quoter absorbs.

The natural question: can the maker **gate their quoting on `λ̂`**? Specifically, can we set a threshold `θ` such that when `λ̂(t) > θ` the maker temporarily withdraws (skips placing that quote), reducing exposure to toxic bursts at the cost of missing some fills?

We simulate this on the historical maker-fill tape (§7 of the paper describes the maker-fill extraction). For each fitted threshold `θ`, per session:

- For every historical maker fill, look up `λ̂` from the online estimator at that fill's timestamp
- **Keep** the fill if `λ̂ < θ` (would still have placed the quote); **skip** the fill otherwise (would have withheld the quote)
- Aggregate over kept fills:

$$
\text{fill rate}(θ) \;=\; \frac{\text{kept fills}}{\text{total historical maker fills}}
$$

$$
\overline{\text{adv\_markout}}(θ) \;=\; \frac{1}{\text{kept fills}} \sum_{\text{kept}} \text{adverse P\&L}
$$

Sweep `θ` from very high (accept everything, fill rate ≈ 100%, high adverse cost) down to very low (accept only the calmest windows, fill rate small, adverse cost near zero). Plot `\overline{\text{adv\_markout}}(θ)` on the x-axis, `\text{fill rate}(θ)` on the y-axis. That curve is the **passive-quoter frontier**.

The frontier tells a real trader a concrete tradeoff: "skip the top-10%-intensity times → lose about 12% of your fills but save ~0.4 ticks of markout on each remaining fill". They can pick the operating point on the frontier that suits their inventory-vs-toxicity preference.

**Relationship to Kaspar Shadow POV.** The passive quoter in this paper is essentially a stripped-down version of what Kaspar's Shadow POV (percentage-of-volume) execution algo does. Same mechanic: rest orders at the BBO, provide liquidity, worry about adverse selection. Differences:

- **Shadow POV has size ramps, inventory management, participation-rate targets** — the full production algorithm handles inventory drift and hits participation targets while quoting passively.
- **The paper's simulated passive quoter has none of that** — it is a diagnostic tool for measuring the toxicity of *every* historical maker fill, not a production algorithm with size scaling.
- **The paper simulates on the historical maker-fill tape** (real orders that were filled in the market) with a hypothetical `λ̂`-gate overlay; Shadow POV runs on live/replayed market data with a real order book.

**Natural deployment path**: add an `λ̂`-gate as a light-parameter in Shadow POV. Wherever Shadow POV currently decides "should I place / lift / reduce a quote?", add the extra condition "**AND `λ̂_side(t) < θ`**" where `side` is the side Shadow POV is quoting and `θ` is the gate threshold chosen from the paper's frontier. That's a one-parameter addition to the light params. The paper's frontier plot at that θ tells you, quantitatively, how much fill rate Shadow POV loses versus how much toxicity it avoids.

**Directional gate matters here.** Shadow POV is typically directional (working a buy program → quoting the bid). It should gate on `λ̂_bid` from the 2-D estimator, not the aggregate. If `λ̂_ask` is spiking but `λ̂_bid` is quiet, that's *fine* for Shadow POV's passive bid — someone else is doing the aggressive buying, our bid isn't the danger. Ignoring direction and gating on aggregate `λ̂` would withdraw Shadow POV unnecessarily.

**Where §5 comes in**: the whole frontier is powered by session-level `λ̂` values (Step E), which are powered by per-session `(μ, α, β)` from §5.4, whose reliability is validated by §5.5. Skip any of §5 and the frontier is either not computable or not trustworthy.

**In one sentence**: the §5 panel is a 2200 × 30 matrix of numbers; every row of the paper's discussion, every regression, every figure downstream depends on being able to pull a specific column from that matrix for a specific slice of sessions.

**If §5 fails**: if the model-free stats (§5.1-5.3) don't reject Poisson decisively, the paper has no premise. If the fits (§5.4) return nonsense `n_day > 1` on many sessions, we can't build a per-day panel of Hawkes params. If KS (§5.5) fails on most sessions, our online estimator is fundamentally misspecified and every downstream metric that uses `λ̂` inherits that misspecification. Each of §5's five subsections is a load-bearing wall.

**Sanity output on Day 1 of running the pipeline**: we can spot-check three panel columns and know whether the whole enterprise is on rails:

- `H_day > 0.6` on ≥ 90% of sessions per stream — long-range dependence confirmed
- `n_day` between 0.75 and 0.98 on ≥ 90% of sessions per stream — near-critical Hawkes confirmed
- `p_KS > 0.05` on ≥ 60% of sessions per stream — exp kernel adequate (60%, not 80%, because §5.5 already warns about power-law tails at long horizons)

If all three hold, the paper's premise is intact and we proceed to §7-§8 without kernel-family surgery. If any fail on any stream, that's the branch point where we go back and either (a) fit a power-law kernel on that stream or (b) admit the exp fit is a working approximation and quote both.

## 6. Reproducibility

### 6.1 Software stack

- **Tape extraction**: C++ tool `msgtape` (to be built alongside `binstats` in `dbento_pcap_parse/`), emits per-message CSV of `(transactTime_ns, sendingTime_ns, msg_type, side, pxd, size, orderID)` for a given securityID and time window.
- **Statistics**: Python (`numpy`, `pandas`, `scipy`) in the `kaspar_arrival` module. All stats above are pure functions of an arrival timestamp array.
- **Hawkes MLE**: `arrival_paper.hawkes.fit_expo`, `.fit_marked`, `.fit_bivariate` — vectorised numpy with analytic gradients.
- **Online estimator**: `arrival_paper.online.HawkesIntensityEstimator` — `O(1)` recursive update per arrival, used at inference time.

### 6.2 One-command per-session pipeline

```bash
# extract msgtape for a given securityID and session
msgtape --datafile 318.20250310.databento.bin \
        --secid 42288528 \
        --rth --out NQ.20250310.msgtape.csv

# fit + diagnose
python -m arrival_paper.fit \
        --tape NQ.20250310.msgtape.csv \
        --model expo,marked,bivariate \
        --out NQ.20250310.hawkes.json
```

Output JSON carries: fitted `(μ, α, β, γ)` per model, `n_day`, `H_day`, `F(5s)_day`, marginal-gap quantiles, KS and LB p-values, and CPU time.

### 6.3 Session-cache layout

- Raw tapes: `/vast/home/vmayeski/out/arrival_paper/tapes/{stream}/{yyyymmdd}.msgtape.parquet`
- Per-session fits: `/vast/home/vmayeski/out/arrival_paper/fits/{stream}/{yyyymmdd}.hawkes.json`
- Aggregated 731-day panel: `/vast/home/vmayeski/out/arrival_paper/panels/hawkes_panel.parquet`

## 7. What can go wrong and how we'll know

| symptom | interpretation | remedy |
|---|---|---|
| `p_KS < 0.05` on > 30% sessions | exponential kernel misspecified | try power-law kernel (Hardiman-Bouchaud); report both |
| `H` shuffled substantially above 0.5 | finite-sample bias in Hurst estimator | report the *gap* between H and H-shuffled, not the distance from 0.5 |
| `α / β > 0.99` | near-critical fit; MLE may not converge | tighten bounds, report as "n → 1", flag day |
| `n_MoM` and `n_MLE` disagree by > 20% | non-stationary intensity within session | fit on sub-intervals (open / mid / close) separately |
| Rate collapses at roll-time | front contract switched mid-session | drop the two flanking sessions of every roll |
| BTC results very different from ES/NQ | genuine cross-product effect OR early-corpus low liquidity | report time-of-day-normalized statistics, split by 2025 quarter |

## 8. Pilot — what we do first

Before the full 731-day sweep, we validate the pipeline on **one representative day per stream**:

- **NQ**: 2025-03-10 (Monday, secID `42288528` = NQH5 front, 9.1M MBO adds, mid-corpus).
- **ES**: same date, front `ESH5` or successor per binstats.
- **BTC**: first day of 2025 where extraction completes.

For the pilot we produce:

1. Marginal-gap statistics (§5.1) — reproduce fast_send single-day numbers as a regression test.
2. Fano + Hurst (§5.2, §5.3) — same regression test.
3. Fitted exp-Hawkes params (§3.1) and time-rescaling p-values (§5.5).
4. A single reference plot: intensity `λ̂(t)` overlaid on message arrivals for 5-min windows at open, mid-session, close.

Only after the pilot passes do we run the sweep across 731 days.

---

## Appendix A — Log-likelihood derivation for §3.1 exp-Hawkes

Given events `0 < t₁ < ... < tₙ ≤ T` and intensity `λ(t) = μ + α Σ_{tᵢ < t} exp(-β(t - tᵢ))`:

Introduce the recursion:
$$
R_i \;=\; e^{-\beta (t_i - t_{i-1})} \,(1 + R_{i-1}), \qquad R_1 \equiv 0.
$$
Then `λ(tᵢ) = μ + α Rᵢ` for `i ≥ 1` (`λ(t₁) = μ`).

Log-intensity term: `Σ_i log(μ + α Rᵢ)`.

Compensator (time integral of `λ`):
$$
\int_0^T \lambda(s)\, ds \;=\; \mu T \;+\; \frac{\alpha}{\beta}\sum_{i=1}^n \Big(1 - e^{-\beta (T - t_i)}\Big).
$$

Log-likelihood:
$$
\log L(\mu, \alpha, \beta) \;=\; \sum_{i=1}^n \log\!\big(\mu + \alpha R_i\big) \;-\; \mu T \;-\; \frac{\alpha}{\beta}\sum_{i=1}^n \Big(1 - e^{-\beta (T - t_i)}\Big).
$$

Gradients w.r.t. `(μ, α, β)` have closed-form recursions of the same shape as `Rᵢ`. Full derivation follows Ozaki (1979) and Ogata (1981); we replicate here for self-containment when we ship the code.

---

*Draft. Next iteration: expand §3.3 with explicit bivariate MLE recursion, add §4.4 covariance estimator (Hessian at optimum), and add §5.6 stationary-vs-non-stationary sub-interval diagnostics.*
