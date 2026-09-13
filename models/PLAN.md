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
(5) **Two papers, not one** — see below.

---

## Two papers

`tech_reports/shadow_pov.tex` is internal and unpublished, which means the empirical results in
it can simply be replaced rather than corrected in public. They do have to be replaced: they were
produced with cancels paying **zero** modelled latency — the same `payload->ts0 = o.ts` bug fixed
on this branch, still present at `m2_kspr/frame_kaspr/src/SOM.cpp:1935` — so every published
figure is optimistic by an unmeasured amount.

**Paper 1 — Shadow-PPOV, revised.** Same method, but the evaluation becomes a different object:
four placement rates instead of one hardcoded 1.5%; size dependence **measured** rather than a
stylized Gamma Monte Carlo the paper itself disclaims as *"not an empirical claim"*; a **latency
sensitivity curve**, which the current paper does not treat as a parameter at all; twelve fires a
session instead of two, so intraday drift is measured rather than the open cohort discarded; and
realised participation counted per leg, so the order-placement-rate → participation map exists.

The framing shifts with it. Not *"here is our execution method"* but **"here is a characterised
model-free baseline for passive placement"** — fully specified, and now measured across size,
participation and latency, so that someone else can position against it. That is the contribution
that makes shadow usable as a benchmark by anyone other than us.

Publishable as soon as the grid lands. It does not wait on the models.

**Paper 2 — the models.** B0, M0–M3, M4 as the yardstick: predictive scoring, the paired
execution backtest, and the generative-impact work. This is the whole of §1 and §5 of this
document and would be crushed into a subsection of Paper 1.

**Why the split is here and not elsewhere.** The current paper concedes that its benchmark role is
*"proposed rather than demonstrated"*, and Paper 1 cannot fix that: a benchmark with nothing
benchmarked against it never demonstrates anything. What Paper 1 can do is make the baseline
usable — specified and characterised. Paper 2 is the demonstration, and its headline is exactly
the question that motivates this document: does a model beat model-free shadow, and by how much?

**One open question, resolved by data we are about to have.** If the latency curve is close to
flat from 40 µs to 6.4 ms, that is a striking result on its own — passive execution cost is largely
speed-insensitive, which cuts against the arms-race narrative. If it is steep, it quantifies what
colocation is worth for passive flow. Either way the curve does not appear to exist in the
literature, and if it is the standout result it should lead Paper 1 rather than sit in a
sensitivity section.

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

### Prior art for Paper 1 (searched 2026-09)

§0 above was written for the models paper. Paper 1's new contributions — the latency curve,
measured participation-vs-size, and shadowing-by-identifier — need their own check.

**The latency curve still looks unoccupied.** The closest work is theoretical:

- **arXiv:2504.00846, "The effect of latency on optimal order execution policy"** — the title is
  alarming but the content is a stochastic optimal control problem with Brownian price dynamics
  and closed-form approximations. It derives how *optimal limit price* should respond to
  submission latency. It does not measure execution cost against latency on real data.
- **"Optimal execution with stochastic delay"** (Finance & Stochastics, 2022) — likewise
  theoretical, delay as a stochastic control problem.
- **"The good, the bad, and latency"** (Quantitative Finance, 2025) — empirical, but crypto
  (Bybit/Binance) and about exploratory trading, not passive execution cost.

So an **empirical slippage-versus-wire-latency curve for passive execution on futures MBO** does
not appear to exist. Grid C produces exactly that, over 40 µs – 6.4 ms. This strengthens the case
for it leading Paper 1.

**The ½-tick result is the one to engage with directly.** *"The Negative Drift of a Limit Order
Fill"* (arXiv:2407.16527, already cited as `negdrift2024`) argues both theoretically and
empirically — on 10Y Treasury futures — that limit-order fills coincide with adverse moves, and
secondary sources report the magnitude as **close to ½ tick**. Our very first measured session came
in at **mean slip_buy = +0.500 ticks**.

That is either a validation or a coincidence, and it must be resolved rather than quietly enjoyed:
if shadow's measured slippage matches an independent theoretical prediction of the same quantity,
it corroborates the simulator; if it matches *too exactly* it may be an artefact of tick rounding
in a 1-lot experiment. Check the ½-tick figure in the source, and check whether our number moves
off ½ at larger sizes and rates. **Owed before submission.**

**Queue-position value is the theoretical anchor for the latency curve.** Moallemi & Yuan
(`moallemi2016`) decompose queue-position value into a static component — the spread/adverse-
selection trade-off, worsening with position — and a dynamic component, the optionality of holding
a position. Latency degrades execution *through* queue position, so the latency curve is an
empirical measurement of that value along a different axis. Related empirics: orders later in the
queue suffer higher adverse selection because they execute against larger, better-informed trades.
Paper 1 should connect the two explicitly rather than presenting the curve bare.

**Differentiate from imitation learning, which a reviewer will conflate with shadowing.**
FlowHFT (arXiv:2505.05784) and FlowOE (arXiv:2506.05755) use flow-matching imitation learning to
train a policy that mimics an *expert's decisions*. Shadow does not learn, imitate a policy, or
train anything: it attaches to individual live orders by exchange identifier. One sentence of
explicit differentiation is cheap insurance.

**Participation dispersion vs parent size: practitioner-known, academically unmeasured.** POV
algorithms routinely run with realised-participation bounds (e.g. 19–21% for a 20% target) and
deviation is a standard TCA concern, which supports the plan's existing framing that this is a
recognised source of tracking error (`kissell`, `cjp2015`, `bialkowski2008`, `freiwestray2015`).
What does not appear to exist is a measured distribution of realised participation as a function of
parent size on real data — which is precisely what Grid B produces, replacing the paper's stylized
Gamma Monte Carlo.

**For Paper 2's predictive-scoring arm.** KANFormer (arXiv:2512.05734) does survival analysis for
fill probabilities in a LOB — the closest existing methodology for scoring a fill-probability model
directly rather than through execution. Cite it as precedent for the approach, and differentiate on
the label: ours is *filled AND not adversely selected*, not merely filled.

### References to add to Paper 1 (`tech_reports/shadow_pov.tex`)

Format matches the existing `thebibliography` block. Keys chosen to sit alongside the current
ones; `negdrift2024` and `moallemi2016` are already cited and only need *engaging with* rather
than adding.

```latex
\bibitem{latencyexec2025} A.~Kumar and S.~Jaimungal.
  \emph{The Effect of Latency on Optimal Order Execution Policy.}
  arXiv:2504.00846, 2025.

\bibitem{stochdelay2022} Á.~Cartea, S.~Jaimungal, and L.~Sánchez-Betancourt.
  \emph{Optimal Execution with Stochastic Delay.}
  Finance and Stochastics, 26:773--812, 2022.

\bibitem{bybitlatency2025} (authors TBC).
  \emph{The Good, the Bad, and Latency: Exploratory Trading on Bybit and Binance.}
  Quantitative Finance, 2025. DOI 10.1080/14697688.2025.2515933.

\bibitem{flowhft2025} (authors TBC).
  \emph{FlowHFT: Imitation Learning via Flow Matching Policy for Optimal
  High-Frequency Trading under Diverse Market Conditions.}
  arXiv:2505.05784, 2025.

\bibitem{kanformer2025} (authors TBC).
  \emph{KANFormer: Predicting Fill Probabilities via Survival Analysis in
  Limit Order Books.}
  arXiv:2512.05734, 2025.

\bibitem{statefill2024} (authors TBC).
  \emph{Fill Probabilities in a Limit Order Book with State-Dependent
  Stochastic Order Flows.}
  arXiv:2403.02572, 2024.
```

Author lists and exact venues are **unverified** — taken from search result titles, not from the
papers. Fill them in from the sources before submission.

**Where each is used.**

| key | section | what it does for us |
|---|---|---|
| `latencyexec2025` | latency sensitivity | The nearest neighbour, and the one a reviewer will raise. Differentiate on *theoretical optimal limit price under latency* vs *measured cost across a latency range on real MBO*. |
| `stochdelay2022` | latency sensitivity | Establishes delay as a studied control problem, reinforcing that the empirical curve is the gap. |
| `bybitlatency2025` | latency sensitivity | The only empirical latency work found; differentiate on venue (crypto), instrument, and that it studies exploratory trading rather than passive execution cost. |
| `negdrift2024` *(already cited)* | slippage definition / results | Reports a limit-order fill penalty near ½ tick on 10Y futures. **Our first session measured +0.500 exactly.** Engage with this directly — it is either independent corroboration or a rounding artefact. |
| `moallemi2016` *(already cited)* | latency sensitivity | The theoretical anchor: latency degrades execution *through* queue position, so the curve measures queue-position value along a different axis. Currently cited only for per-fill randomness; promote it. |
| `flowhft2025` | related work | One sentence of differentiation: imitation learning mimics an *expert policy*; shadow attaches to individual live orders by identifier and learns nothing. |
| `kanformer2025` | (Paper 2) predictive scoring | Methodological precedent for scoring fill-probability models directly. Differentiate on the label: *filled AND not adversely selected*. |
| `statefill2024` | (Paper 2) predictive scoring | State-dependent fill probability — the quantity B0/M0 estimate. |

### Verification still owed (before submission)
Read in full (only abstracts/excerpts reviewed so far): arXiv:2409.12721 and arXiv:2501.08822.
Run one more targeted sweep on "does statistical LOB realism predict execution performance" to
confirm that exact question is unanswered.

For Paper 1: confirm the ½-tick figure in arXiv:2407.16527 against our measured +0.500, and check
whether ours moves off ½ at larger sizes and rates — a 1-lot experiment on a 1-tick-spread
instrument can produce ½ by rounding alone. Read arXiv:2504.00846 in full to confirm it contains
no empirical latency/cost curve.

---

## 1. Models in scope

The models fall into **three paradigms**, defined by *which question* they answer for a market-making
/ execution algorithm like shadow. shadow makes two distinct decisions, and different paradigms feed
different ones:

- **where to quote and how far to skew for inventory** — fed by **Paradigm 1** (optimal control);
- **keep or cancel a resting order** (the `EV_keep` call) — fed by the fill-probability and toxicity
  signals of **Paradigms 2 and 3**.

**Paradigm 1 — Optimal control & inventory.** Compute the optimal bid/ask quotes by trading off spread
capture against inventory risk, via continuous-time stochastic control (an HJB equation). *Avellaneda–
Stoikov (M5), Guéant et al. (M6).*
**Paradigm 2 — Microstructure & point processes.** Capture temporal clustering, self-excitation and
order-flow toxicity. *Hawkes (M2), CST Markov (M0).*
**Paradigm 3 — Queueing & fill probability.** Your order's queue position and its fill odds on a FIFO
book. *Queue-Reactive (M1)*; the linear imbalance baseline *B0* sits in front as the cheap direction
cue. *DeepLOB (M4)* is a supervised accuracy reference cutting across Paradigms 2–3.

Together they form the standard HFT pipeline — Hawkes → toxicity, Queue-Reactive → fill probability,
Avellaneda–Stoikov/Guéant → inventory-skewed base quotes, combined at a quote/risk gate (see the
pipeline diagram below).

| # | Model | Paradigm | Outputs (fill / dir) | Reference | Class | What it gives shadow |
|---|-------|----------|----------------------|-----------|-------|----------------------|
| B0 | **Queue-Imbalance / OFI** | 3 | **dir ✓** · fill ✗ (proxy only) | Cont, Kukanov & Stoikov (2014), *The price impact of order book events*, J. Fin. Econometrics; Gould & Bonart (2016), *Queue imbalance as a one-tick-ahead price predictor* | Linear one-tick predictor | Near-zero-cost directional gate + fill-side signal. The **"must-beat" baseline** — especially strong in large-tick ES/NQ; if a heavy model can't out-execute it inside shadow, that is itself a finding. |
| M0 | **Cont–Stoikov–Talreja (CST)** | 2 | **fill ✓ · dir ✓** | Cont, Stoikov & Talreja (2010), *A stochastic model for order book dynamics*, Oper. Res. | Zero-intelligence Markov, constant Poisson rates | Baseline fill-probability from constant rates. Deliberately naive floor. |
| M1 | **Queue-Reactive (QR)** | 3 | **fill ✓✓ · dir ✓** | Huang, Lehalle & Rosenbaum (2015), *Simulating and analyzing order book data: the queue-reactive model*, JASA | State-dependent Markov; intensities λ(q) depend on queue size | Queue-position-aware fill prob and level-survival — the canonical model for 1-tick markets like ES/NQ. |
| M2 | **Multivariate Hawkes** | 2 | **fill ✓ · dir ✓** | Bacry, Delattre, Hoffmann & Muzy (2013); Bacry, Mastromatteo & Muzy (2015), *Hawkes processes in finance* | Self/cross-exciting point process | Order-flow clustering + branching ratio → fast/slow regime; cross-excitation toxicity signal for shadow entry/pull. |
| M3 | **Queue-Reactive Hawkes (hybrid)** | 2+3 | **fill ✓✓ · dir ✓** | Morariu-Patrichi & Pakkanen (2019), *Hybrid marked point processes*; (2022) *State-dependent Hawkes* | Hawkes intensities modulated by queue state | Clustering *and* queue-dependence in one fill-prob/pull signal. Superset of M1+M2. |
| M4 | **DeepLOB** | 2+3 | **dir ✓** · fill ✗ | Zhang, Zohren & Roberts (2019), *DeepLOB*, IEEE TSP | Supervised CNN+LSTM classifier | Mid-move direction accuracy ceiling; optional directional gate for shadow. |
| M5 | **Avellaneda–Stoikov** | 1 | neither → **quotes/skew** (takes λ(δ) as *input*) | Avellaneda & Stoikov (2008), *High-frequency trading in a limit order book*, Quant. Finance | Optimal control (HJB); inventory-risk quoting | Reservation-price skew + optimal half-spread: **where** shadow should quote given inventory, vol, horizon. |
| M6 | **Guéant–Lehalle–Fernandez-Tapia** | 1 | neither → **quotes/skew** (takes λ(δ) as *input*) | Guéant, Lehalle & Fernandez-Tapia (2013), *Dealing with the inventory risk*, Math. Fin. Econ. | Optimal control; closed-form/ODE approximation of A–S | Production-grade multi-tier inventory-skewed quotes without HJB numerical instability. |

**Reading the Outputs column — and why it drives the whole design.** The keep/cancel decision is
`EV_keep = P(fill) × E[move|fill]`, so it needs **both** a fill probability and a direction. The models
split three ways on what they can supply:

- **Both fill + direction (M0, M1, M2, M3).** These four can answer keep/cancel *on their own*. Only
  **M1 and M3** produce a *queue-position-aware* `P(fill)` (marked `fill ✓✓`) — a fill probability that
  depends on where your order sits in the FIFO line — which is the entire reason they matter on a
  large-tick market like ES/NQ. M0's and M2's fill probabilities are real but coarser (M0 assumes
  constant rates; M2 knows *when* fills cluster but not *how deep* your queue is).
- **Direction only (B0, M4).** They give `E[move|fill]` but no usable `P(fill)`, so they are either
  **paired** with a fill estimate from M0–M3, or used as a **veto** ("model says down with high
  confidence → cancel the bid, ignore fill odds"). B0's "fill proxy" is a crude
  closeness-to-front heuristic, not a real fill model — do not trust its magnitude.
- **Neither (M5, M6).** The inventory models don't predict fills or direction at all; they answer a
  *different* question — **where to quote and how far to skew for inventory** — and they actually
  *consume* a fill-rate curve `λ(δ)` as an **input**. They sit on shadow's placement/skew lever, not the
  keep/cancel lever, and are meant to run *on top of* one of the fill/direction models above.

Practical consequence: a complete shadow configuration is usually **one fill/direction model (M0–M3)**
`+` optionally **a directional veto (B0 or M4)** `+` optionally **an inventory overlay (M5/M6)** — the
benchmark ablates each slot independently so we can attribute any P&L change to the right piece.

### How the models combine — the HFT pipeline

The three paradigms are complementary, not competitors: in a full stack each feeds a different part of
the quoting decision, exactly as in a production HFT market maker.

```
[ L3 / MBO market-data feed ]
          │
          ├──> [ Hawkes  (M2 / M3) ] ──────────────> toxicity / adverse-selection signal
          │
          ├──> [ Queue-Reactive (M1), CST (M0), B0 ] > fill probability + level survival
          │
          └──> [ Avellaneda–Stoikov (M5) / Guéant (M6) ] > inventory-skewed base quotes
                        │
                        ▼
             [ shadow quote generator + risk gate ]   (combines EV_keep + inventory skew)
                        │
                        ▼
              [ SOM (simulated fills) | iLink (live) ]
```

DeepLOB (M4) plugs in as an optional directional gate alongside the toxicity signal. In the benchmark
we ablate paradigms independently *and* in combination, so we can attribute any shadow-P&L change to a
specific paradigm rather than the stack as a whole.

### Model comparison matrix

| Model | Primary inputs | Key output | Main strength | Main limitation |
|-------|----------------|-----------|---------------|-----------------|
| B0 Imbalance/OFI | best-level sizes, OFI | next-tick direction | near-zero cost; strong in large-tick | direction only; no fill/queue notion |
| M0 CST | level volumes, constant rates | first-passage fill / price-move prob | closed-form, simple | constant rates ⇒ wrong queue-size dist |
| M1 Queue-Reactive | level depth, queue position `k` | queue fill probability | accurate for FIFO matching | needs tick-level L3 / MBO |
| M2 Hawkes | event timestamps (MO/LO/cancel) | dynamic intensities `λ_i(t)` | captures clustering / toxicity | heavy to calibrate online |
| M3 QR-Hawkes | events + queue state | state-dependent intensities | clustering + queue in one | most params; overfit risk |
| M4 DeepLOB | 10-level book snapshots | `P(mid up/flat/down)` | learns nonlinear signal (accuracy ceiling) | black box; data-hungry; no fill model |
| M5 Avellaneda–Stoikov | vol `σ`, risk aversion `γ`, arrival `k`, inventory `q` | reservation price + half-spread | closed-form inventory control | assumes constant depth & continuous fills |
| M6 Guéant et al. | arbitrary `λ(δ)`, inventory limit `Q` | multi-tier skewed quotes | fast ODE solve; multi-tier | ignores microstructural queue priority |

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
| M5/M6 inventory | **mbt_gym** (Jerome, Savani et al. — model-based market-making RL gym) + Cartea–Jaimungal–Penalva reference code | Implements Avellaneda–Stoikov / Guéant inventory-quoting dynamics; reference for the HJB/ODE quoting layer. *Verify exact repo URL before use.* |
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

$$
\text{EV}_{\text{keep}} \;\approx\; P(\text{fill}) \times \mathbb{E}\big[\, \text{mid}_{\text{after fill}} - p \,\big|\, \text{fill} \,\big]
$$

*(chance I actually get filled here) × (which way the market goes once I'm filled).*

The second term is the catch: you tend to get filled *exactly* when an informed seller is running
the price down, so a fill can arrive **with** an adverse move. **Keep the order while EV_keep > 0**
(fills are likely benign and the drift is in your favour); **cancel the moment EV_keep turns
negative** (you'd be filled into a market moving against you). The Paradigm-2/3 models below (B0, M0–
M4) are all just different ways to estimate **P(fill)** and the conditional move **E[·|fill]**.

**The exception — Paradigm 1 (M5, M6).** The inventory models answer a *different* question: not
"keep or cancel this order," but **"given the inventory I already hold, where should I place my quotes
and how far should I skew them?"** They feed shadow's *placement and skew*, not the keep/cancel call —
the inventory-risk overlay that sits on top of the fill/toxicity signal.

---

### B0 — Queue-Imbalance / OFI (linear predictor)

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

---

### M0 — Cont–Stoikov–Talreja (zero-intelligence Markov)

**Core idea.** Model the whole book as a system of queues receiving completely random (Poisson) order
flow at *constant* rates, then read off *probabilities of book events* — will this level empty, will
my order fill, which way will the price move — from the mathematics of that random system. It is
"zero-intelligence" because nothing reacts: the rates never change.

**State space — what one "state" is.**
Picture the price axis as a fixed ladder of tick-spaced slots. *Price is the slot's position on the
ladder, not a number we store*; what we store at each slot is its **queue size** — how many lots are
resting there right now. So one *state* of the model is a snapshot of the whole book: a list giving
the queue size at every slot.

$$
X = (n_1,\, n_2,\, \dots,\, n_K)
$$

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

$$
N_{\text{states}} \;\approx\; (Q_{\max} + 1)^{\,2k}
$$

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

$$
b(n) = \lambda \quad\text{(up-rate: a lot joins)} \qquad\qquad d(n) = \mu + n\,\theta \quad\text{(down-rate: a lot leaves)}
$$

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

$$
P(\text{empty before reaching } N \mid \text{start at } n) \;=\; \frac{r^N - r^n}{r^N - 1}, \qquad r \neq 1
$$

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

$$
h(a, b) = P(\text{ask empties before bid} \mid \text{ask} = a \text{ lots},\ \text{bid} = b \text{ lots}) \quad (=\ P(\text{up-move}))
$$

Memorylessness means `h` at any state equals the rate-weighted average of `h` at the states one jump
away. Collect those equations into one linear system:

$$
Q\,h = 0
$$

with boundary conditions

$$
h = 1 \ \text{ where } a = 0 \ (\text{ask empty} \to \text{up-move}), \qquad h = 0 \ \text{ where } b = 0 \ (\text{bid empty} \to \text{down-move})
$$

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

$$
P(\text{up-move}) \;\approx\; \frac{\text{number of runs where the ask emptied first}}{\text{total runs } M}
$$

where:

- the current state and the rates `λ, μ, θ`.  *(input)*
- `M` — number of simulation runs.            *(input)*
- *output* — a Monte-Carlo estimate whose error shrinks like `1/√M`.

*Monte Carlo* just means "estimate a probability by random simulation and counting." The single fact
under all three methods: for two independent Poisson streams with rates `a` and `b`, the next event
comes from the first with probability

$$
P(\text{next event is } A) = \frac{a}{a + b}
$$

Chaining that one identity across the queue steps is, in the end, what every method computes.

**Calibration — getting λ, μ, θ from data.** These rates are the only unknowns, each fitted by
**maximum-likelihood estimation (MLE)** — the standard recipe of picking the parameter values that make
the observed data most probable. For Poisson rates the MLE reduces to "count events, divide by time":

$$
\hat{\lambda}(i) = \frac{\text{number of limit orders added at level } i}{\text{total time observed}}
$$

$$
\hat{\mu} = \frac{\text{number of market orders}}{\text{total time observed}}
$$

$$
\hat{\theta} = \frac{\text{number of cancellations}}{\text{total lot-seconds resting}}
$$

where *lot-seconds resting* = summed over resting orders, how long each one stayed in the book (each lot
is exposed to cancellation for exactly that long). The hat `λ̂` denotes "the estimate of `λ`."

**Keep/cancel (what shadow gets).** Both EV terms come from the same first-passage: **P(fill)** =
P(market-sells + cancels-ahead drain the queue to my position before the level's price moves), and
**E[move|fill]** from P(which queue empties first). Because the rates are constant, the call is a
**static function of the current queue sizes** — no memory.

**Limitations.** Constant rates give exponential inter-arrival times and near-geometric queue-size
distributions that do **not** match real books (real queues are hump-shaped), plus no clustering and no
reaction to imbalance. That's the point — it's the floor M1–M3 must beat.

---

### M1 — Queue-Reactive (Huang–Lehalle–Rosenbaum)

**Core idea.** Keep CST's queueing picture but fix its worst lie: in real markets the order-arrival and
cancellation rates depend on **how full the queue already is**. Traders pile into thin queues and pull
out of overcrowded ones; big queues get cancelled faster than they trade. Make the rates functions of
the queue size and the model suddenly reproduces the real book.

**State space.** Same picture as CST — a snapshot of queue sizes:

$$
X = (q_{-K},\, \dots,\, q_{-1},\, q_1,\, \dots,\, q_K)
$$

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

$$
\lambda_{\text{limit}}(q) \qquad \lambda_{\text{cancel}}(q) \qquad \lambda_{\text{market}}(q)
$$

(limit-order arrival, cancellation, and market-order rates — each a function of the current size `q`).

where:

- `q` — the queue's current size (lots).
- `λ_limit(q)` — how fast new orders join *when the queue already holds `q` lots* (usually falls as `q`
  grows — nobody wants to join a 20,000-lot queue).
- `λ_cancel(q)` — how fast lots are cancelled at size `q` (usually rises with `q`).
- `λ_market(q)` — how fast market orders eat the level at size `q`.

Group the two ways a lot can leave into one **departure rate**

$$
\mu(q) = \lambda_{\text{cancel}}(q) + \lambda_{\text{market}}(q)
$$

so the queue climbs at rate `λ_limit(q)` and falls at rate `μ(q)`. (In the simplest "Model I" the queues
move independently once `p_ref` is fixed; richer variants — Models II/III — let each queue's rates also
depend on the rest of the book, e.g. on the bid/ask imbalance, which couples them.)

**Computing the probabilities.**
*The headline result — the queue-size distribution.* Run one queue for a long time; what fraction of the
time does it hold exactly `q` lots? That long-run fraction is the **stationary distribution** `π(q)`.
For a birth–death process it has a simple closed form — a running product of up/down rate ratios:

$$
\pi(q) = \pi(0) \cdot \prod_{j=1}^{q} \frac{\lambda_{\text{limit}}(j-1)}{\mu(j)}
$$

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

$$
\mu(q)\,f(q-1) + \lambda_{\text{limit}}(q)\,f(q+1) = \big(\lambda_{\text{limit}}(q) + \mu(q)\big)\,f(q), \qquad f(0) = 1
$$

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

$$
\hat{\lambda}_{\text{limit}}(q) = \frac{\text{number of limit orders added while the queue held } q \text{ lots}}{\text{time the queue spent at size } q}
$$

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

---

### M2 — Multivariate Hawkes (self/cross-exciting point process)

**Core idea.** Neither Markov model has memory: they react to the *current* queue sizes but not to the
fact that a burst just happened. Real order flow **clusters** — a trade makes the next trade more
likely, a cancel triggers more cancels. A Hawkes process encodes exactly that: every event temporarily
raises the intensity of future events.

**State / representation.** A Hawkes model does *not* track queue sizes; it tracks **event times**, and
from them an **intensity** for each kind of event — the instantaneous rate at which that event is about
to happen. There is one intensity per event **type** `m`:

$$
m \in \{\, \text{market-buy},\ \text{market-sell},\ \text{limit-add-bid},\ \text{limit-add-ask},\ \text{cancel-bid},\ \text{cancel-ask},\ \dots \,\}
$$

with `M` types in total (`M ≈ 4–12`). The state is the vector of current rates
`λ(t) = (λ_1(t), …, λ_M(t))`, where an **intensity** `λ_m(t)` means: `λ_m(t) · dt` = probability that a
type-`m` event happens in the next tiny slice of time `dt`.

**Dynamics — the intensity equation.**

$$
\lambda_m(t) \;=\; \mu_m \;+\; \sum_{n=1}^{M} \sum_{t_i^n < t} \alpha_{mn}\, e^{-\beta_{mn}\,(t - t_i^n)}
$$

where:

- `λ_m(t)` — intensity (rate) of event type `m` at time `t`.  *(output)*
- `μ_m` — the calm **baseline** rate of type `m` when nothing has happened recently.  *(param)*
- the double sum — over every past event: `n` ranges over event types, `t_i^n` over the times type-`n`
  events fired before `t`.
- `α_{mn}` — **excitation**: how much one type-`n` event bumps type-`m`'s intensity right after it.  *(param)*
- `β_{mn}` — **decay rate**: how fast that bump fades.  *(param)*
- `e^{−β(t − t_i)}` — the fading factor: 1 the instant the event fires, shrinking toward 0 as time passes.

In words: *the rate of event `m` = its calm baseline + a bump for every recent event, each bump fading
exponentially.* **Self-excitation** is `α_{mm}` (trades beget trades); **cross-excitation** is `α_{mn}`
for `m ≠ n` — a market-buy lifting the ask begets more buys and more ask-side cancels, the math of
"reading the tape."

**Why it's cheap online.** With the exponential kernel you never re-scan history. Keep a running `λ`.
Between events it decays toward its baseline:

$$
\lambda_m(t) = \mu_m + \big(\lambda_m(t_{\text{last}}) - \mu_m\big)\, e^{-\beta\,(t - t_{\text{last}})}
$$

and at each type-`n` event every intensity jumps up (for every `m`):

$$
\lambda_m \;\leftarrow\; \lambda_m + \alpha_{mn}
$$

One multiply + one add per event — **O(1)** — which is what lets it run in shadow's hot path.

**Branching ratio — the fast/slow-market number.** The total excitation of type `m` by one type-`n`
event is the area under its kernel:

$$
\Gamma_{mn} = \int_0^\infty \alpha_{mn}\, e^{-\beta_{mn}\, u}\, du = \frac{\alpha_{mn}}{\beta_{mn}}
$$

= the expected number of type-`m` events *directly* triggered by one type-`n` event. The **branching
ratio** `n*` is the largest eigenvalue (**spectral radius**) of the matrix `Γ`:

$$
n^* = \rho(\Gamma) \quad (\text{spectral radius}), \qquad 0 \le n^* < 1 \ \text{required for stability}
$$

where:

- `n* ≈ 0` — events are mostly independent (exogenous) → **slow, calm market**.
- `n* → 1` — each event nearly triggers another whole event → self-igniting cascades → **fast market**
  (the model is non-stationary / blows up if `n* ≥ 1`).

This one number is shadow's fast/slow regime flag.

**Computing the probabilities.**
*(1) Calibration by likelihood.* The log-likelihood of an observed event stream is

$$
\log L = \sum_i \log \lambda_{m_i}(t_i) \;-\; \sum_{m=1}^{M} \int_0^T \lambda_m(s)\, ds
$$

where:

- first sum — over all observed events `i` (time `t_i`, type `m_i`): reward for putting high intensity
  where events actually occurred.
- second term (the **compensator**) — total intensity integrated over the window `[0, T]`: penalty for
  predicting events that did not happen.
- *output* — a score; maximise it over `{μ, α, β}` to fit the model (this is **MLE**). Both terms have
  O(N) recursions with exponential kernels.

*(2) Fill probability.* For a resting bid, "getting filled" = a market-sell (or a cancel reaching your
spot) fires, so the market-sell intensity *is* your instantaneous **fill hazard**. Over a horizon `h`:

$$
P(\text{fill within } h) = 1 - \exp\!\left( -\int_t^{t+h} \lambda_{\text{sell}}(s)\, ds \right)
$$

Fills therefore cluster — the probability jumps right after a sell burst.

*(3) Direction / toxicity.* Compare expected aggressive-buy vs aggressive-sell intensity over the
horizon; if sell intensity is spiking, a fill on your bid is likely **toxic** (you buy just as sellers
run the price down).

**Calibration (parameters).** Fit `μ` (M numbers), `α` (M×M), `β` (M×M) by MLE — use a **sum-of-two-
exponentials** kernel (two decay rates) for a materially better fit, and the `tick` library rather than
hand-rolling. The ~10:1 add/cancel-to-trade churn is handled by fitting each stream separately and
down-weighting quotes that vanish within milliseconds.

**Keep/cancel (what shadow gets).** **P(fill)** = the market-order fill hazard on your side;
**E[move|fill]** = whether that flow is a directional sweep (via cross-excitation). A spike in
self-excited sell intensity ⇒ "about to be filled *because* a sweep is running me over" ⇒ EV_keep < 0
⇒ cancel; a calm book ⇒ benign fills ⇒ keep.

**Limitations.** Basic form has **no queue size** — it knows *when* events cluster, not *how deep* the
queue is (no true queue position). Kernel/dimension choice matters; churn can inflate excitation. M3
fixes the missing queue state.

---

### M3 — Queue-Reactive Hawkes (hybrid)

**Core idea.** M1 knows the queue state but not time-clustering; M2 knows time-clustering but not the
queue state. M3 is the union: **Hawkes intensities that are also modulated by the current book state.**

**State / representation.** Two things are tracked together:

- the **intensity vector** `λ(t) ∈ ℝ^M` — the recent-burst memory, from M2;
- a discrete **book state** `X(t)` — queue sizes / imbalance bucket, from M1.

They feed back on each other: events fire at rate `λ`; each event changes the queues (updates `X`); and
`X` in turn scales `λ`. With exponential kernels plus a finite set of book states, the pair `(λ, X)` is
still Markov — a "Markov-modulated Hawkes" (hybrid marked point process, Morariu-Patrichi–Pakkanen).

**Dynamics — the intensity equation.**

$$
\lambda_m(t) = \varphi_m\big(X(t)\big) \left[ \mu_m + \sum_n \sum_{t_i^n < t} \alpha_{mn}\, e^{-\beta_{mn}\,(t - t_i^n)} \right]
$$

where:

- the bracket `[ … ]` — the ordinary **M2 Hawkes intensity** (baseline + fading bumps).
- `φ_m(X)` — a **state factor**: a multiplier that speeds up (`φ > 1`) or slows down (`φ < 1`) type-`m`
  events depending on the current book state `X`.
- `X(t)` — the current queue / imbalance state.

In words: *take the Hawkes rate, then multiply it by how likely this event is in the current queue
configuration.* So both "an event just fired, expect more" *and* "behaviour changes because the queue is
nearly empty / very full" live in one intensity.

**Computing the probabilities.** Same likelihood as M2, but `λ` now carries `φ(X(t))`, so you must
**replay the book state `X(t)` along the event path** while scoring — heavier, still O(N). To *simulate*,
use **Ogata thinning**: propose candidate events at a rate `λ̄` that upper-bounds the true intensity,
keep each with probability `λ / λ̄`, and update `X` on every kept event (the standard accept/reject
recipe for point processes). Fill and direction probabilities come out exactly as in M2, now conditioned
on both the burst state and the queue state.

**Calibration.** Initialise `φ(X)` from M1's state-binned rates and `{α, β}` from M2's kernels, then
**joint MLE**. It has the most parameters of any model here, so judge it strictly **out-of-sample**
(BIC / Tier-C / shadow P&L), never on in-sample likelihood — the overfitting risk is real.

**Keep/cancel (what shadow gets).** The richest signal: **P(fill)** and **E[move|fill]** conditioned on
**both** the excitation state and the queue state simultaneously — the most complete EV_keep. Whether
that extra fidelity actually improves shadow's execution over the simpler M1/M2 is precisely the
question the benchmark is built to answer.

**Limitations.** Most parameters, most compute, highest overfitting risk; the online state carries both
the Hawkes intensities and the discretised book state, so its C++ hot-path update is the heaviest here.

---

### M4 — DeepLOB (supervised deep net)

**Core idea.** Drop all queueing / point-process structure and just **learn** the map from recent book
snapshots to the next price move, letting a neural net discover whatever patterns predict direction. It
is not a book model and cannot simulate a book — it's a classifier used as an accuracy yardstick.

**Input.** A snapshot ladder over a short window:

```
input = last T snapshots × top 10 levels × {price, size} on both sides   (≈ T × 40 numbers)
```

with `T ≈ 100` recent book updates. No hand-built features — the raw ladder goes straight in.

**Architecture (what each layer does).**

- **Convolution (CNN) blocks** — small learned filters that first combine price+size *within* a level,
  then combine *across* the 10 levels, building features like imbalance and micro-price automatically.
- **Inception module** — runs several filter sizes in parallel and concatenates them, so the net sees
  patterns at multiple scales at once.
- **LSTM (recurrent layer)** — reads the sequence of those features across the `T` snapshots, capturing
  how the book is evolving over time.
- **Softmax output** — three numbers that sum to 1:

$$
\text{output} = \big(\, P(\text{down}),\ P(\text{flat}),\ P(\text{up}) \,\big) \quad \text{for the mid over a forward horizon } k
$$

**Computing the probability.** A single **forward pass**: the input numbers are multiplied through the
trained network weights to produce the softmax. There is no state machine and no transition rates —
"how it's computed" is just matrix multiplies.

**Calibration / training.** Supervised learning: the labels are the smoothed future mid move bucketed
`up / flat / down` by a threshold `α`; the weights are trained by gradient descent to minimise
**cross-entropy** (the standard classification loss). It needs a lot of data and **strict session-level
train/val/test splits** — consecutive snapshots are highly correlated, so sloppy splitting leaks the
answer and inflates accuracy. Benchmarks: FI-2010, or our own ES/NQ.

**Keep/cancel (what shadow gets).** Supplies only the **E[move|fill]** direction term (a learned
P(up/down)); it has **no fill model**. shadow either combines it with another model's P(fill) or uses
it as a **veto gate** — DeepLOB says "down" with high confidence ⇒ cancel the bid regardless of fill
odds. Its real job: answer *"how much predictable signal are the interpretable models leaving on the
table?"*

**Limitations.** Black box (hard to attribute a decision), data-hungry, leakage-prone, inference
latency in the hot path, and no notion of queue position or of your own order — direction only.

---

### M5 — Avellaneda–Stoikov (optimal-control inventory quoting)

**Core idea.** A different question from every model above: not "will this order fill / which way will
price go," but **"given the inventory I already hold, where should I place my bid and ask to earn the
spread without taking on too much price risk?"** It is a stochastic **optimal-control** problem —
maximise the expected utility of end-of-day wealth, trading spread capture against the variance of
holding inventory.

**Setup / assumptions.** The mid-price is a random walk (arithmetic Brownian motion):

$$
dS_t = \sigma\, dW_t
$$

where:

- `S_t` — mid-price at time `t`.
- `σ`   — volatility (how fast the mid diffuses).
- `W_t` — standard Brownian motion (the random-walk driver); `dW_t` is its increment.

Our quotes sit a distance `δ` from the mid; the farther out, the less often we fill. Fills arrive as a
Poisson process whose rate decays with distance:

$$
\lambda(\delta) = A\, e^{-k\,\delta}
$$

where `λ(δ)` = fill rate at quote distance `δ`, `A` = base fill rate at the touch (`δ = 0`), `k` = how
fast the fill rate falls as you quote farther out.

**Output 1 — reservation price.** The inventory-adjusted "fair value" the maker centres its quotes on.
Holding a long position (`q > 0`) shifts it *down* (you want to sell, so lean cheaper); short shifts it
up:

$$
r(s, q, t) = s - q\,\gamma\,\sigma^2\,(T - t)
$$

where:

- `r` — reservation price (the skewed centre).  *(output)*
- `s` — current mid.               *(input)*
- `q` — current inventory, signed. *(input)*
- `γ` — risk aversion (how much you dislike inventory variance). *(input)*
- `σ` — volatility.                *(input)*
- `(T − t)` — time remaining to the horizon `T`. *(input)*

**Output 2 — optimal total half-spread.** How wide to quote around the reservation price:

$$
\delta_a + \delta_b = \gamma\,\sigma^2\,(T - t) + \frac{2}{\gamma}\,\ln\!\left(1 + \frac{\gamma}{k}\right)
$$

where `δ_a, δ_b` = the ask-side and bid-side distances from `r`; the first term is the inventory/vol
risk premium, the second a fill-rate term (wider when fills are scarce, i.e. small `k`). The quotes are
then placed at `r + ½(δ_a+δ_b)` (ask) and `r − ½(δ_a+δ_b)` (bid).

**How it's solved.** Dynamic programming: the value function obeys a **Hamilton–Jacobi–Bellman (HJB)**
partial differential equation, and A–S give the closed-form approximations above in the small-inventory
/ short-horizon limit.

**Calibration.** Estimate `σ` from mid returns, `A` and `k` from the empirical fill-rate-versus-distance
curve (how often orders resting at distance `δ` actually fill), and pick `γ` as the risk knob (tuned to
hold inventory inside a target band).

**Keep/cancel — what shadow gets.** Feeds shadow's **placement and skew**, *not* the `EV_keep`
toxicity call: it sets where to rest and how far to lean given current inventory — the inventory-risk
overlay on top of the fill/toxicity models.

**Limitations.** Assumes constant book depth and continuous fills (no queue position, no discrete tick
grid), a single price level, and a fixed horizon `T`. Excellent for skew; blind to microstructure.

---

### M6 — Guéant–Lehalle–Fernandez-Tapia (production-grade inventory control)

**Core idea.** Same optimal-control objective as A–S, but made **usable in production**: exact, fast
solutions over realistic (finite) horizons, with hard inventory limits and arbitrary (non-exponential)
fill-intensity functions.

**Key trick — linearise the HJB.** A change of variable collapses the coupled nonlinear HJB equations
into a solvable linear system:

$$
v_q(t) = \exp(-\alpha\, q^2)\, u_q(t)
$$

where:

- `q` — inventory level (bounded, `q ∈ {−Q, …, +Q}`).
- `v_q(t)` — the value function at inventory `q` and time `t`.
- `u_q(t)` — the transformed unknown to solve for.
- `α` — a constant chosen to kill the nonlinear term.

After the substitution the `u_q(t)` obey a **system of linear ODEs** (ordinary differential equations —
equations in the time-derivatives of the `u_q`), which solve exactly and fast. No fragile numerical PDE
solve, so quotes update in real time.

**What it adds over A–S.** Finite inventory bounds `q ∈ {−Q, …, +Q}`; arbitrary fill-intensity `λ(δ)`
(not just `A·e^{−kδ}`); and stable multi-tier quotes.

**Calibration.** Same inputs as A–S (`σ`, the fill-intensity curve, `γ`, inventory band `Q`) plus the
horizon; fit `λ(δ)` non-parametrically instead of assuming the exponential shape.

**Keep/cancel — what shadow gets.** The production inventory-skew engine: real-time skewed multi-tier
base quotes that shadow's risk gate combines with the M0–M3 fill/toxicity signals.

**Limitations.** Still an inventory-control layer — it ignores **microstructural queue priority** (no
notion of your FIFO position), which is exactly what M1/M3 supply. The two are complementary, not
substitutes.

### Glossary

Self-contained: plain-language definitions of every symbol and term used above, so this section can
be read on its own. Symbols first, then concepts by theme.

**Symbols — Greek letters**
- **λ (lambda)** — a *rate* / *intensity*: how many events happen per unit time. Appears subscripted or
  in parentheses by context: `λ(i)` = limit-order rate at level `i` (M0); `λ_limit(q)`, `λ_cancel(q)`,
  `λ_market(q)` = size-dependent rates (M1); `λ_m(t)` = intensity of event type `m` at time `t` (M2/M3);
  `λ(δ) = A·e^{−kδ}` = fill rate as a function of quote distance (M5).
- **μ (mu)** — the market-order arrival rate (M0/M1); in Hawkes, the *baseline* intensity `μ_m` (M2/M3);
  `μ(q)` = a queue's total departure rate = cancellations + market orders (M1).
- **θ (theta)** — the *per-order cancellation rate*: each resting order is cancelled independently at
  rate `θ`, so a level holding `n` lots cancels at total rate `n·θ` (M0).
- **σ (sigma, lower-case)** — volatility: the standard deviation of price returns (M5/M6).
- **γ (gamma)** — risk aversion: how strongly the market maker penalises holding inventory (M5/M6).
- **α (alpha)** — Hawkes *excitation*: the jump one event adds to another's intensity, `α_{mn}` (M2/M3);
  separately, the label threshold for up/flat/down in DeepLOB (M4), and a constant in Guéant's change of
  variable (M6).
- **β (beta)** — Hawkes *decay rate*: how fast an excitation bump fades, `β_{mn}` (M2/M3).
- **δ (delta)** — the distance of a quote from the mid (M5); `δ_a`, `δ_b` are the ask- and bid-side
  half-spreads.
- **φ (phi)** — the *state factor* `φ_m(X)`: a multiplier that scales the Hawkes intensity by the current
  book state (M3).
- **π (pi)** — the *stationary distribution* `π(q)`: the long-run fraction of time a queue holds `q`
  lots; `π(0)` is its normalising constant (M1). (Not the number 3.14 here.)
- **Γ (Gamma, capital)** — the *branching matrix*, `Γ_{mn} = α_{mn}/β_{mn}`: the expected number of
  type-`m` events triggered by one type-`n` event (M2).
- **ρ (rho)** — *spectral radius*: the largest eigenvalue magnitude of a matrix; `ρ(Γ) = n*` is the
  branching ratio.
- **Σ (Sigma, capital)** — summation (add up a series of terms). **Π (Pi, capital)** — product (multiply
  a series of terms).

**Symbols — Latin letters**
- **t** — time; **T** — the trading horizon / total observation window; **t_i** (or `t_i^n`) — the time
  of the i-th (type-`n`) event; **t_last** — the time of the previous event.
- **X** — the *state*: the vector of queue sizes (M0/M1), or the book/queue state that scales the Hawkes
  intensity (M3).
- **n** — a queue size (M0); also an event-type index inside Hawkes sums (M2/M3). **n\*** — the branching
  ratio (see ρ).
- **N** — an upper barrier queue size in the gambler's-ruin formula (M0); also the number of data points
  in BIC.
- **q** — a queue size (M1); *and* the inventory position (M5/M6) — two distinct uses, always clear from
  the model.
- **Q** — the *generator matrix* of the Markov chain (M0); `Q_max` = the queue-size cap; `Q` = the
  inventory bound (M6); `Q_bid`, `Q_ask` = best-level sizes (B0).
- **k** — the number of levels tracked per side (M0); the fill-rate decay constant in `λ(δ)=A·e^{−kδ}`
  (M5); the forward horizon of the DeepLOB label (M4).
- **K** — the number of price levels tracked (per side).
- **i** — a price-level index (distance from the best) and/or an event index.
- **m** — an event type and its index; **M** — the number of event types (M2/M3); `M` is *also* the number
  of Monte-Carlo runs (M0) — clear from context.
- **r** — the down-rate/up-rate ratio in gambler's ruin (M0); *and* the reservation price `r(s,q,t)` (M5).
- **b(n), d(n)** — the birth (up) and death (down) rates of a queue at size `n` (M0).
- **a, b** — generic competing-event rates in `a/(a+b)`; *also* the ask/bid queue sizes in `h(a,b)` (M0).
- **h** — a hitting/harmonic probability `h(a,b)` (M0); *also* the forward horizon in `P(fill within h)`
  (M2). **f(q)** — the probability a level empties starting from size `q` (M1).
- **s** — the mid-price (M5); *also* a dummy integration variable in `∫ λ(s) ds`.
- **p** — the price of your own resting order (in `EV_keep`); **p_ref** — the reference price the price
  ladder is centred on.
- **A** — the base fill rate at the touch in `λ(δ)=A·e^{−kδ}` (M5).
- **W_t** — standard Brownian motion (the random-walk driver of the mid); `dW_t` its increment (M5).
- **v_q(t), u_q(t)** — the value function at inventory `q`, and its transformed unknown (M6).
- **I** — the queue imbalance `(Q_bid − Q_ask)/(Q_bid + Q_ask)` (B0).

**Symbols — operators & notation**
- **∫_a^b … ds** — an integral from `a` to `b`: a continuous sum over the variable `s`.
- **E[· | ·]** (or 𝔼) — a *conditional expectation*: the average value of a quantity given a condition.
- **P(·)** — the probability of an event.
- **e^{x}, exp(x)** — the exponential function; **ln(x)** — the natural logarithm.
- **x̂ (hat)** — an *estimate* of `x` computed from data (e.g. `λ̂` estimates `λ`).
- **∈** — "is a member of"; **{ … }** — a set; **ℝ** — the real numbers; **ℕ** — the non-negative
  integers {0, 1, 2, …}.
- **≈** — approximately equal; **→** — "tends to / maps to"; **←** — "is updated to" (assignment);
  **∞** — infinity; **dt, ds, du** — infinitesimal increments of time or a variable.

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

**Optimal control / inventory**
- **Optimal control** — choosing actions over time (here: where to quote) to maximise an objective
  (expected utility of wealth) under randomness; solved by dynamic programming.
- **Inventory** — the signed position the market maker is currently holding; the core risk in market
  making (an unwanted long/short exposed to price moves).
- **Risk aversion (`γ`)** — a dial for how much you dislike inventory variance; higher `γ` ⇒ tighter
  inventory control, wider/more-skewed quotes.
- **Brownian motion** — the canonical continuous random walk; here the model for the mid-price.
- **Reservation price** — the inventory-adjusted fair value a maker centres its quotes on; skews away
  from the mid as inventory grows.
- **Utility / value function** — the objective being maximised / the best achievable objective from a
  given state; the thing the HJB equation characterises.
- **HJB (Hamilton–Jacobi–Bellman) equation** — the partial differential equation the value function
  satisfies in a continuous-time control problem.
- **PDE / ODE** — Partial / Ordinary Differential Equation: an equation in the derivatives of an
  unknown function (of several / one variable). Guéant et al. reduce the HJB PDE to linear ODEs.
- **Half-spread** — the distance from the reservation price to each quote; total spread = `δ_a + δ_b`.

**This project**
- **shadow** — kaspar's passive execution algorithm (`light22`); the fixed strategy each model plugs
  into. Baseline = shadow as-is.
- **EV_keep** — the keep-vs-cancel expected value `P(fill) × E[mid_after_fill − price | fill]`; keep the
  order while positive, cancel when negative.
- **SOM** — kaspar's Simulated Order Manager; the queue-aware fill simulator used for the backtest.

---

## 2. Data: MBO L3 from kaspar

### How PCAPs are actually processed today (verified in-tree, 2026-09-12)

Short answer to "do the PCAPs go into `.bin` files?" — **that is the intended path, and every
piece of it exists, but no part of it is wired into the `kaspr` binary today.** Both ends
(PCAP → live decode, and `.bin.gz` → replay) are library-only. This is the single biggest
prerequisite for the whole bake-off and is why D0 below comes before D1.

#### The two halves of the pipeline

```
 (A) capture → decode                          (B) record → replay
 ────────────────────────                      ─────────────────────────
 .pcap / .pcap.zst                             .bin.gz  (gzip stream of packed L3 records)
   → PCAPReader          [actor, exists]         → BFA               [actor, exists, NOT wired]
   → MsgBuf              [actor, exists]         → bfile::read_l3    [exists]
   → MessageProcessor    (SBE decode)            → OB / TachBook actors
   → handler_if MBO handlers                     → light22 → SOM → fills
   → { binrec?  →  BinRecorder → bfile::write_l3 → .bin.gz }
   → OB / TachBook
```

Half (A) turns wire packets into decoded L3 events. Half (B) is the **fast replay loop** — once a
session is recorded to `.bin.gz` you never re-parse Ethernet/UDP/SBE again, which is what makes
20+ sessions × N models tractable. The `.bin.gz` file *is* the canonical intermediate.

#### (A) PCAP → decoded L3

`mcast_recv::PCAPReader<seqnumT, N>` (`mcast_recv/include/mcast_recv/act/PCAPReader.hpp`):

- Opens `.pcap` directly via `pcap_open_offline`, or `.pcap.zst` by piping through `zstdcat`
  into `pcap_fopen_offline` — **zstd-compressed captures are read natively, no pre-decompression.**
- Reads **one packet per `actors::msg::Continue`** sent to itself, so replay is a self-clocked
  actor loop, not a blocking read. EOF sends `ShutdownThisActor`.
- Parses Ethernet (incl. 802.1Q VLAN) → IPv4/IPv6 → UDP by hand; the UDP payload is the MDP3
  packet, whose first 4 bytes are the `MsgSeqNum`.
- Optionally strips a **hardware-timestamp trailer** (`TrailerSpec`, e.g. Metamako) appended after
  the UDP payload, and sanity-checks the configured layout against the wire on the first packet —
  a mis-configured trailer fails loudly rather than silently corrupting payloads.
- Emits `msg::ProcessQ` carrying **two timestamps**: `recv_ts` (pcap record header, software) and
  `hw_ts` (trailer, 0 if none) — on top of the `transactTime` / `sendingTime` that come from MDP3
  itself. Worth deciding explicitly which clock each model's inter-arrival times are measured on.
- Hardcodes `buf.chan = 'A'`, with the comment *"Databento data is de-duplicated, always use
  Feed A"* — so the captures this path targets are already A/B-arbitrated. **No A/B feed
  arbitration and no recovery/snapshot replay happens in PCAP mode** (`create_all_mdp3_pcap` passes
  `recovery_processor = nullptr`). Gaps in a capture are therefore *not* repaired.

Assembly is `create_all_mdp3_pcap()` in `interface/mdp3/if/mdp3.hpp` — same `MessageProcessor` and
`handler_if` as live, minus recovery. It accepts an A and a B file but points both at the same
`MsgBuf`.

#### (B) The `.bin.gz` record format

`BinRecorder` (`frame_kaspr/include/frame/mda/act/BinRecorder.hpp`) is a ~60-line actor: `gzopen`
on Start, `bfile::write_l3(outf, m->l3)` per `mda::msg::Data`, `gzflush(Z_FINISH)` on Shutdown.
Note it flushes but deliberately never `gzclose()`s (arena-corruption workaround) — the deflate
state is leaked on exit by design.

The file is a **flat gzip stream of length-implicit, type-tagged packed structs** — no framing, no
index, no footer. Each record is a 1-byte `l3_typ_t` tag followed by the remainder of the
corresponding `[[gnu::packed]]` struct; `bfile::read_l3` reads the tag, then `switch`es to read
`sizeof(rec) - 1` more bytes. Consequences worth planning around: **the format is not
self-delimiting without the type table, it is not seekable, and it is ABI-coupled to the struct
layout in `chutil/include/bfile/r_l3.hpp`.** Any exporter must link that header, not re-implement it.

Record types relevant to us (`en::l3`, from `chutil/gencode/enum/def/l3.enum`):

| Tag | Struct | Carries |
|---|---|---|
| `MBO_V2` | `l3_mbo_v2_packed_t` | the per-order event stream — add/modify/delete |
| `MBOT_V2` | `l3_mbo_trd_v2_packed_t` | trades (order-level) |
| `MBOS` | `l3_mbo_snap_packed_t` | MBO snapshot, incl. chunking fields |
| `FDF` | `l3_fdf_packed_t` | instrument definitions — **BFA needs these to map `securityID` → asset** |
| `GAP_V2`, `CHR_V2` | — | gap / channel-reset markers |

`l3_mbo_v2_packed_t` in full: `typ, venue, transactTime, sendingTime, handlerendtim,
orderUpdateAction, securityID, orderID, priority, pxd (double), displayQty, side, endOfEvent,
lastQuote, recovery, order_flags, visibility_group`. Beyond the fields listed further down, three
matter for modeling: `sendingTime`/`handlerendtim` give a second and third clock; `recovery` marks
events replayed from a snapshot/recovery burst rather than seen live — **these must be filtered
out of any intensity fit**; and `priority` is the CME-assigned queue priority, which is what makes
true queue rank available without reconstruction guesswork.

Naming convention, per `BFA_USAGE.md`: `sec_id.YYYYMMDD.bin.gz` (e.g. `490.20250124.bin.gz`).

#### (B) Replay: BFA

`BFA` (`frame_kaspr/include/frame/mda/act/BFA.hpp`) mirrors PCAPReader's shape: `gzopen` in the
constructor, one `bfile::read_l3` per `Continue`, `manager->terminate()` at EOF. Useful properties
already built in:

- `start_h` / `end_h` hour filters, with early termination once `end_hour` is crossed (the file is
  chronologically ordered) — handy for RTH-only or event-window fits.
- A `time_warp` facility and a `FactoryFunc` hook that creates actors on first sight of an
  instrument's `FDF`/`ODF`.
- **All timing comes from the data**, never wall-clock — `BFA_USAGE.md` is explicit that the Timer
  actor is driven by market-data timestamps. Backtests are therefore deterministic and reproducible.
- Must be added to its Group **last**, after OBs/lights/SOM exist, or it starts pushing data into
  actors that aren't ready.

#### What is wired vs. what exists

| Piece | Status | Evidence |
|---|---|---|
| `PCAPReader` actor | exists, complete; **now has a caller** | `mcast_recv/.../act/PCAPReader.hpp` |
| PCAP → `.bin` conversion | **wired and usable** — this is the D0a path | `dbento_pcap_parse/dbento_pcap_to_bin/` |
| `BinRecorder` + `create_BinRecorder()` | exists; **instantiated by the converter** | `frame_kaspr/src/BinRecorder_if.cpp` |
| `handler_if` record call sites | ~12, all guarded `if (binrec)`; live in the converter's path | `mdp3/include/mdp3/handler_if.hpp` |
| `bfile::write_l3` / `read_l3` | exist, symmetric, cover all record types | `chutil/include/bfile/r_l3.hpp` |
| `create_all_mdp3_pcap()` | exists, **zero callers** | `interface/mdp3/if/mdp3.hpp` |
| `kaspr` PCAP replay mode | **not wired** — `kaspr.cpp` calls `create_all_mdp3()` (live multicast) | `kaspr/src/kaspr.cpp` |
| `kaspr` CLI flags for replay | **none** — `main()` parses only a config path and `--reset-positions` | `kaspr/src/kaspr.cpp` |
| Recording inside `kaspr` | **no** — `handler->binrec = nullptr` (the converter sets its own) | `kaspr/src/kaspr.cpp` |
| `BFA` actor | exists, **never instantiated** — the D0b gap | grep: only its own header |

So the picture has shifted since this section was first written: **PCAP → `.bin` is done**, via
`dbento_pcap_parse/` rather than via flags on `kaspr`. The remaining gap is one-directional —
nothing replays a `.bin` back through the books yet (D0b).

`STRATEGY_SIMULATOR_GUIDE.md` has been corrected on both counts (its mode-1 diagram named
`SocketReader` where it is **PCAPReader**, omitting `MsgBuf`/`MessageProcessor`; and its "Known
Gaps" had the `.bin`/PCAP support backwards).

#### D0 — the Databento parsing pipeline (in-repo as of 2026-09-12)

**Status change:** the PCAP → `.bin` half is no longer a to-do. `dbento_pcap_parse/` was ported
from the internal tree and relicensed MIT; it wraps the same `PCAPReader → MsgBuf →
MessageProcessor → handler_if → BinRecorder` chain the live system uses, so a converted session is
byte-identical to what the live handler would have produced. What remains is the *replay* side.

| Tool (`dbento_pcap_parse/<tool>/src/`) | Role |
|---|---|
| `dbento_pcap_to_bin` | one channel, one day of `.pcap.zst` → one `.bin` |
| `build_universe` | `.bin` → `universe.<chan>.<date>.csv` + `.json.gz` from FDF/ODF/SDF |
| `binstats` | per-`.bin` inventory: definition counts by updateAction, per-securityID MBO activity |
| `merge_bins` / `verify_merged` | merge to one ts-ordered stream; assert monotonicity |
| `pcap_list_ips` | dry-run: what src IPs / dst ports are actually in a capture dir |
| `scripts/extract_futures.sh` | parallel, resume-safe batch over dates × channels |

##### D0a — acquire ES sessions  [DONE 2026-09-12]

**Status: complete, and well past one month.** The full Databento archive on disk was converted:
**363 ES (ch. 310) sessions, 2025-01-01 to 2026-02-27, 73 GB of `.bin`**, zero failures. Wall-clock
was 277 s for the 260 remaining sessions at `NJOBS=59` (all dates run concurrently; a single
session is ~3-77 s and cannot use more than ~2 cores, so across-date parallelism is the only kind
available). Sizing, which the plan flagged as unmeasured: **~200 MB of `.bin` per normal weekday
session** from ~373 MB of compressed pcap.

Two things that fell out of doing it, both now fixed and worth remembering:
- Session size tracks the calendar, not correctness: Sundays are ~2-4 MB (the 18:00 ET open only),
  holidays ~3 MB, shortened sessions 30-60 MB, normal weekdays 120-400 MB. The
  "event count within band of the median" gate **must bucket by session type** or it will flag
  every Sunday as truncated.
- The converter deadlocked at shutdown after migrating the actor framework's own messages to
  `MessageT`. Data was intact (the hang is after the last record is written) but no `.ok` marker
  was produced. The framework messages in `actors/cpp/include/actors/msg/` keep hand-assigned ids.

The corpus spans **five ES quarterly rolls** (Mar/Jun/Sep/Dec 2025), so front-month selection and
roll segmentation is now real work rather than the footnote it was for a single clean month.

##### D0a (original plan) — acquire one month of ES

ES is **channel 310**. One calendar month is ~21–22 trading sessions, which already satisfies the
≥ 20-session floor in *Coverage* below, so this is the right first pull.

```bash
# once: the converter resolves multicast IP/ports from this file (CME SFTP)
cd genconfig && ./genconfig.sh          # writes genconfig/mdp3_prod.info

export KSPRPROJ=~/kaspar-hft
cd dbento_pcap_parse/scripts
SRC=/path/to/databento/pcaps/glbx/futures-xcme NJOBS=16 ./extract_futures.sh 20260202 20260203 ...
```

`extract_futures.sh` iterates channels `{310, 318, 326}`; for an ES-only pull either edit
`CHANNELS=(310)` in a copy, or loop the dates calling `dbento_pcap_to_bin --chan 310` directly.
Output lands in `out/bin/310/310.<date>.databento.bin`, resume-safe behind `.ok` markers.

**Per-session acceptance gate.** A day is only admitted to the study if all four pass:

1. `binstats --datafile <bin>` shows FDF count > 0 **with Add/Modify actions present**. A file whose
   definitions are all Deletes cannot build a symbol table and every consumer silently comes up
   empty — this is the failure mode `binstats` exists to catch.
2. `verify_merged` reports zero out-of-order timestamps.
3. `PCAPReader` did not print its zero-matching-packets warning, and the filtered-drop count at EOF
   is plausible for the layout (near-zero per-channel; ~95% consolidated).
4. Session event count is within a sane band of the month's median — an order-of-magnitude outlier
   means a truncated capture, not a quiet day.

**Open items that must be settled on day 1, not day 21:**

- **Sizing.** Nobody has measured a day of ES 310 MBO through this path yet. Convert one session
  first and record `.bin` size, wall-clock, and peak RSS, then multiply by 22 before committing
  disk. Do not budget from a guess.
- **Contract selection / the roll.** `.bin` files carry every ES expiry. The study needs the front
  month, chosen per session from `build_universe` output plus volume. ES rolls quarterly (Mar /
  Jun / Sep / Dec); **if a roll falls inside the chosen month the series splits**, and fitting
  across it mixes two different liquidity regimes. Either pick a month without a roll, or treat
  pre- and post-roll as separate fitting segments — decide before pulling.
- **No recovery, no A/B arbitration.** PCAP mode passes a null recovery processor and stamps every
  packet feed `'A'`; captures must already be de-duplicated (Databento's are). A gap in the capture
  propagates to the book and is *not* repaired. Gap/`CHR` records in the `.bin` mark where.
- **The `recovery` flag.** `l3_mbo_v2_packed_t.recovery` marks events replayed from a snapshot or
  recovery burst rather than seen live. These **must be filtered out of every intensity fit** — they
  are not real arrivals and will bias λ and the Hawkes branching ratio upward.

##### D0b — what is still unwired

1. **`kaspr` replay mode.** Add `--replay <file.bin.gz>`; construct `BFA` and add it to its Group
   **last** per `BFA_USAGE.md`. This is the loop the bake-off actually runs in, and it is the one
   genuinely missing piece.
2. **`kaspr` PCAP mode** (`--pcap <file>` → `create_all_mdp3_pcap()`). Not needed for the study —
   `dbento_pcap_to_bin` already covers PCAP ingestion — but cheap, and useful for debugging a
   single capture against the live decode path.
3. **`kaspr --record`** (set `handler->binrec` instead of `nullptr`). Also not needed for data
   production any more; keep it for recording *live* sessions.
4. **Round-trip test.** Convert one PCAP to `.bin`, then replay that `.bin` through `--replay` and
   through a direct `--pcap` run of the same capture. Book state and SOM fill sequence must match.
   This is the correctness gate for every number the study reports, and it pins the `r_l3.hpp`
   struct ABI.
5. **Decide the clock.** Fix, once and in writing, which of `transactTime` / `sendingTime` /
   `recv_ts` / `hw_ts` the Hawkes and QR fits use. Exponential-kernel MLE is sensitive to this and
   the four differ by microseconds-to-milliseconds.

> **Dependency note on D1:** `CLAUDE.md` states *"Do not add Arrow/Parquet dependencies — they were
> removed intentionally."* The Parquet exporter proposed in D1 below therefore needs either an
> explicit reversal of that decision or a different target format. The cheap alternative that
> respects the constraint: have D1 emit **CSV or raw little-endian binary on stdout** from a tool
> that links `bfile::read_l3`, and let the Python side (`pandas` / `pyarrow` **outside** the C++
> build) do the columnar conversion. Recommended — it keeps Parquet entirely on the Python side of
> the fence, where it is a research-time dependency rather than a build-time one.

The PCAPs are **MBO** (order-by-order, full L3). kaspar reconstructs them via the MBO path
(`TachBook` / `handler_if` MBO handlers), and `BinRecorder` can dump the per-order event
stream through `bfile::write_l3` — gzip'd `l3_mbo_v2_packed_t` records carrying `transactTime`,
`orderUpdateAction` (add/modify/delete/trade), `securityID`, `orderID`, `priority`, `pxd`,
`displayQty`, `side`, `endOfEvent`. That is exactly the per-order stream every model needs, and it
gives **true queue position** — no MBP approximation anywhere in this project.

### Pipeline
```
Databento .pcap.zst  (ES ch.310 / NQ ch.318, many files per day)
   → dbento_pcap_to_bin  (PCAPReader → MsgBuf → MessageProcessor → handler_if → BinRecorder)
   → <chan>.<date>.databento.bin   gzip'd l3_mbo_v2_packed_t                    [D0a — DONE]
        ├→ build_universe  → universe CSV/JSON (securityID → symbol)               [D0a-2]
        │     └→ merge → master_universe.<chan>.json → BFA securityID map
        └→ binstats / verify_merged → per-session acceptance gate
   → BFA replay into kaspr  (--replay)                                          [D0b — TODO]
   → l3 → columnar exporter                                                     [D1]
   → Python event reader + book re-walker                                       [D2]
```

- **D0a** — pull one month of ES (ch. 310) through `dbento_pcap_parse/`. The converter exists; the
  work is acquisition, the per-session acceptance gate, and the roll/contract decision.
- **D0b** — wire `--replay` (BFA) into `kaspr`. The one genuinely missing piece; blocks the backtest.
- **D1** — small C++ tool linking `bfile::read/write_l3` that streams the gzip record file to a
  columnar format. Reuses the project's own decoder; deterministic. Note the Arrow/Parquet
  constraint in `CLAUDE.md` — emit CSV/raw binary from C++ and convert on the Python side.
- **D2** — Python reader over the Parquet, plus a **book re-walker** that replays the L3 events to
  attach, at each event: `mid`, `spread`, `level_index` (ticks from mid), `queue_size_before`, and
  **per-order queue rank** (position ahead in FIFO priority). This queue rank is the feature that
  makes the whole exercise possible and is available because the data is MBO.

**Canonical event schema:** `ts, instrument, event_type ∈ {LIMIT_ADD, CANCEL, TRADE, MODIFY},
side, price, level_index, size, order_id, priority, queue_size_before, queue_rank, mid, spread`.

### Coverage
- **First pull: one month of ES (ch. 310)** — ~21–22 sessions, which clears the ≥ 20-session floor
  in one acquisition. NQ (ch. 318) later; the same scripts cover it by channel number.
- Split train/val/test by **whole sessions** (no intra-session leakage). With ~22 sessions a
  14 / 4 / 4 split is the natural starting point — thin for M4 (DeepLOB), which is a further
  argument for treating it as a reference rather than a core model at this data scale.
- A month is enough to fit M0/M1/B0 comfortably and M2/M3 adequately; if the Hawkes branching
  ratio proves unstable across sessions, extend to a quarter before blaming the model.
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

### The headline metric: direction-agnostic slippage

Slippage is the number this study lives or dies by, so it is defined here exactly as in
`tech_reports/shadow_pov.pdf` §Experimental Evaluation, and every model is scored on the same
definition. **Do not invent a new one per model.**

Fire the *same* parent both long and short on the same session. For each leg let `mid` be the BBO
midpoint cached at the instant the chunk alarm fires, and `vwap` the size-weighted execution price
for that leg:

```
cost_buy = vwap_buy − mid
cost_sel = mid      − vwap_sel
Slippage = ½(cost_buy + cost_sel) = ½(vwap_buy − vwap_sel)
```

The fire-time `mid` **cancels algebraically** in the second equality, so the reported figure is
exactly *half the gap between what the buyer of a leg paid and what the seller received* —
independent of which mid was captured, and robust to a mis-timed mid snapshot. **Positive = a
loss.** A *negative* value is not a win; it is a diagnostic that the two direction-runs saw
different market states, and that session should be quarantined rather than averaged in.

- **Units.** 1 tick = 0.25 pt. ES: `0.25 × $50 = $12.50` per tick per contract.
- **Reference to beat.** The published shadow-as-is result is ES **+0.45 ticks = +$5.59 per
  contract** at the close, over 31,354 contracts / 137 date-pairs. Any model that does not move
  this number has not earned its complexity.
- **Close cohort only.** The paper fires at 09:29 and 15:57 ET and reports the **close**. At the
  open, price drift inside the 60-second window (100+ NQ points observed, 171 on one date) means
  the two direction-runs sample different price levels and the drift swamps the execution effect.
  Fire at the open for diagnostics if useful, but **the headline is the close.**

### Slippage measurement pipeline

```
.bin session (D0a, on disk: 363 ES sessions 2025-01-01..2026-02-27)
   │
   └─ ONE run per session. SlippageProbe fires every 30 min, 09:30-15:00 ET
      (12 fires); each fire is a round trip (buy then sell back to flat), so
      both directions are measured in the same run and the mid still cancels.
                            │
   BFA --replay  →  OB / TachBook  →  light22 (+ ModelSignal)  →  SOM (sim fills)
                            │                      │
                            │                      └─ fire scheduler: 10 lock-step chunks on
                            │                         Timer alarms at 15:57 ET; caches BBO mid
                            │                         at each alarm  →  mid_fire
                            │
                            └─ SOM Fill → fills CSV row:
                               ts, side, px, sz, symbol, chunk, mid_at_fire
   │
   ├─ per-leg vwap  = Σ(px·sz)/Σ(sz) over fill rows for that leg/direction
   ├─ Slippage      = ½(vwap_buy − vwap_sel)          [mid cancels]
   └─ aggregate across sessions → mean, CI, per-regime split
```

**Why both directions, and why one run is enough.** With a single direction you must trust the
captured `mid` absolutely; any error in *when* it was sampled goes straight into the result.
Measuring both directions makes the metric independent of that snapshot. Upstream got both by
running each session twice; `SlippageProbe` instead makes **each fire a round trip that returns to
flat**, so buy and sell are measured in the same run, in the same market state. One run per
session, 12 samples from it.

**Where the fills come from — decided: CSV.** The sim writes one row per fill via
`--fills-output`: `ts, side, px, sz, symbol, chunk, mid_at_fire`. The fire-time mid rides on every
row, so the two direction-runs pair up from the fills file alone with no second artefact to keep
in sync. The offline aggregator is Python and reads this directly.

`l3_som_t` records carry the same fields (`somcode`, `side`, `px`, `sz`, `symid`,
`som_tim_epoch`) and can be persisted through `bfile::write_l3` for archival or replay
verification, but the **CSV is the source of truth for reported numbers** — a binary path would
need a reader written before any figure comes out. `BFA` also drops a plain-text `som<pid>.out`;
that is a debugging artefact, not an input.

**Per-run acceptance gate** (a run that fails any of these is quarantined, not averaged in):
1. Both legs of the fire completed and filled the full parent (`still_to_be_filled == 0`); a
   fire whose leg timed out is quarantined, and the probe must return to flat before the next fire.
2. Slippage is positive. Negative means the two legs saw materially different market states
   (a fast move between them) — investigate, do not average.
3. Fill count and contract count within band of the corpus median for that fire time.
4. Participation rate ρ stays small (the paper used ρ≈1.5%); see the impact caveat below.

**Secondary metrics** (explain the slippage number, never replace it): fill ratio and
passive-fill share; mark-out at +1s/+5s/+30s after fill (adverse selection); realized fill
queue-rank distribution; cancel efficiency — pulls that avoided a sweep vs pulls that missed a
free fill.

> **Impact caveat, inherited from the paper and applying unchanged to every model here.** The
> simulator does not model the market's reaction to our own orders: each shadow fills against the
> recorded feed as though its presence did not perturb the flow it follows. This is sound at small
> size and breaks once a shadow is a material fraction of resting liquidity. **Report ρ alongside
> every slippage figure, and do not extrapolate to sizes where ρ is large.** A model that "wins"
> by placing far more aggressively has not beaten the baseline; it has left the regime where the
> simulator is valid.

### What D0b must deliver: the `sim` binary

There is **no simulator executable in this repo**. `kaspr` is the only trading binary and it wires
live multicast only. Build one in kaspar-hft: `sim/src/sim.cpp` + `sim/include/`, using the
`mk_kaspr` templates like every other component.

#### Build order

1. **The sim binary skeleton** — Manager + Group + Logger; `--datafile`, `--universe`,
   `--contract`, `--fills-output`. Wires BFA → OB → Timer → light22 → SOM → PositionManager.
2. **Universe at startup** (see below) — the securityID wiring.
3. **`SlippageProbe`** — the firing actor (see below).
4. **Offline aggregator** — Python, reads the fills CSV, emits slippage.

**Explicitly NOT ported:** `Aggregator` (removed from this repo by design; nothing in this path
consumes it), `MarketMaker` (it simulates a counterparty — we fill against recorded flow),
ZMQ/registry/coordinator/Python-MM, and the two-leg spread path.

#### Universe at startup — pre-populate RefData, do not touch the `.bin`

The `.bin` files are correct and must not be rewritten. The securityID wiring is a **startup**
concern:

```
universe.310.<date>.json            (D0a-2, already generated for all 363 dates)
   └─ at sim startup, for each FDF entry:
        RefData::add_future_asset(symbol, venue, securityID, cfiCode,
                                  securityGroup, minPriceIncrement * dispFactor)
   └─ create an OB per contract of interest
   └─ build BFA's dataAdapter directly:   sparse[venue][asset->sec_id] = ob
```

`RefData::add_future_asset` is static and idempotent (returns the existing Asset if the symbol is
already known), so this is safe to run over the whole universe.

> **Do not use the `create_BFA` wrapper in `interface/mda/if/BFA.hpp`.** It converts a dense
> `order_books[venue][asset_id]` into BFA's sparse map by treating *the vector index as the
> securityID*. That is only correct when `asset_id == securityID`, which is false for CME
> (ESH5 is asset id ~1 and securityID 5002). This conflation is what produced
> `out of range: unordered_map::at venue:CMEMD sym: 5002` and zero fills in the smoke test.
> `BFA`'s own constructor already takes the sparse `vector<unordered_map<int32_t, actor_ptr>>`
> keyed by securityID — construct it directly and the problem disappears. No BFA change, no
> universe-map parameter, no `.bin` rewrite.

BFA can also create books from an in-stream FDF via its factory (and will even call
`RefData::add_future_asset` itself for unknown symbols), but relying on that means relying on the
definition happening to be in the capture window — which for the front month it is not. Pre-
populating at startup is deterministic.

#### `SlippageProbe` — the firing actor (NEW, replaces upstream's TradeScheduler design)

**How m2_kspr did it.** `TradeScheduler` subscribes to `BBBOChg` from every OB, keeps a
`bbo_cache_` keyed by `(venue, sym)`, and on each pulse fire writes a `FireRecord`
(`fire_id, time_et, chunks, send_ts_ns, legs[{contract, total_sz, best_bid_at_fire,
best_ask_at_fire, bbo_ts_ns}]`). **No actor computes slippage** — the C++ side only *collects* the
fire-time BBO and the fills; an offline "slippage analyzer" pairs them. We keep that split: C++
collects, Python computes.

What we do differently: upstream fires **twice per session** (09:29 and 15:57 ET) and needs two
runs per session to get both directions. Instead:

**`SlippageProbe` fires every 30 minutes from 09:30 to 15:00 ET — 12 fires per session — and
collects a buy price and a sell price at each fire.**

```
for each fire time in {09:30, 10:00, 10:30, ..., 14:30, 15:00} ET:      # 12 fires
    snapshot mid = (best_bid + best_ask)/2 from the BBO cache          # fire-time reference
    fire LONG  leg:  AddToPos(contract, BUY,  size)  -> PositionManager -> shadow lights
    wait for the parent to fill (chunks drain in lock-step)
    record vwap_buy
    fire SHORT leg:  AddToPos(contract, SEL,  size)  -> returns to flat
    wait for fill
    record vwap_sel
    emit:  Slippage_i = 1/2 (vwap_buy - vwap_sel)
```

Each fire is a **round trip that returns to flat**, so both directions are measured *in the same
run, in the same market state, minutes apart* — the fire-time mid still cancels, and we no longer
need two separate runs per session. Position starts and ends flat at every fire, so fires do not
contaminate each other.

**Why this is better than the paper's design:**

- **~4,356 samples instead of 274.** 12 fires × 363 sessions, versus 2 fires × 137 date-pairs.
  Confidence intervals on the model comparisons get far tighter, which matters because the
  differences between M0-M3 are expected to be small.
- **Intraday coverage.** The paper could only report the close cohort, because the open was
  swamped by drift. Sampling every 30 minutes gives a slippage *profile* across the session
  (open effects, lunchtime illiquidity, close) rather than one number, and makes the RTH/regime
  split in *Coverage* a real analysis rather than an aspiration.
- **One run per session, not two.** Halves the compute and removes the risk that the two
  direction-runs diverge.

**Subscriptions and outputs.** `SlippageProbe` needs `BBBOSub` on the ES book (for the fire-time
mid), `FillSub` on SOM (to know when a leg is done and to record fill prices), and Timer alarms at
the 12 fire times. It writes one CSV row per fill:

```
fire_id, time_et, ts, contract, side, px, sz, chunk, mid_at_fire, bbo_ts_ns
```

**Open design points to settle when building it:**

- **Fill timeout.** A leg that does not fill within the 30-minute window must be abandoned and the
  fire marked incomplete, or the next fire starts from a non-flat position. Abandoned fires are
  quarantined, not averaged.
- **Size per fire.** Must stay small enough that participation rate rho remains in the regime where
  the simulator is valid (the paper used rho ~= 1.5%). Report rho per fire.
- **Sim time, not wall-clock.** The 30-minute cadence is *market* time from the Timer, which is
  driven by data timestamps. Same reason `PositionManager::set_sim_timer` matters.

#### D0a-2 — universe generation (a required pipeline stage, not an afterthought)

A `.bin` is a stream of `securityID`s. Nothing downstream — not the sim, not the models, not the
front-month selection — can act on it without a `securityID -> instrument` map. Producing that map
is a pipeline stage in its own right:

```
<chan>.<date>.databento.bin
   └─ build_universe <bin>
        ├─ universe.<chan>.<date>.<ts>.csv       parsed fields (secID, symbol, asset, tick, type)
        └─ universe.<chan>.<date>.<ts>.json.gz   full FDF/ODF/SDF messages
             └─ merge across dates ─> master_universe.<chan>.json
                  └─ BFA::set_universe_map  ->  books created on MBO ADD for securityIDs
                                                whose FDF never arrived in-band
```

`build_universe` already exists (`dbento_pcap_parse/build_universe/`) and scans a `.bin` for FDF
(futures), ODF (options) and SDF (spread) definitions. What is missing is the **merge step** and
the **BFA wiring** (`create_BFA` here takes no universe map; upstream's does).

Run it per session, as part of the same batch that produces the `.bin` — a session whose universe
cannot be built is not an admissible session, and that check belongs next to `binstats` in the
acceptance gate rather than being discovered later when the sim produces no fills.

##### Root cause and fix: definitions need a SEPARATE PASS

The instrument definitions are present in the captures. We were discarding them.

**Why.** `MessageProcessor::processq_handler` gates on a monotonic sequence number:

```cpp
if (m->buf.seqnum <= qseq_num || stopq) return;   // silently dropped
```

CME's Instrument Replay (IR) stream **loops, restarting its sequence on every cycle** — observed
`last_seqnum` across the seven 2025-01-15 IR windows was 6, 3, 13, 10, 7, 5, 9. Concatenating those
windows into one decode stream means `qseq_num` climbs past them almost immediately and every
later IR packet is dropped as stale. ~71,000 IR packets in, 12 definitions out. The gate is
*correct* for the incremental stream and must not be weakened — it is the same code the live feed
handler uses.

**Measured proof.** Converting each IR window in its own process (fresh `qseq_num = 0`) and
extracting with `build_universe`:

| IR window (UTC) | FDFs recovered |
|---|---|
| 22:00 | 12 |
| 22:10 | 15 |
| 22:20 | 21 |
| **22:30** | **32 — including `5002 ESH5`, `4916 ESM5`, `14160 ESU5`** |
| 22:40 / 22:50 / 23:00 | 11 / 14 / 15 |

The 22:30 window carries the **front month**. No single window carries the whole universe; the
union does. Note the front-month securityIDs are low (`5002`, `4916`, `14160`) while the
rarely-traded contracts are `42xxxxxx` — so "all securityIDs look like 42xxxxxx" is not a valid
sanity check.

**The fix: a definitions pass, separate from the market-data pass.**

```
                 ┌─ pass 1: DEFINITIONS ─────────────────────────────┐
                 │  for each IR snap file (224.0.31.43), separately: │
 <date>/*.pcap.zst  │    fresh MessageProcessor (qseq_num = 0)         │
                 │    decode -> FDF/ODF/SDF                          │
                 │  union across windows                             │
                 └──────────────┬────────────────────────────────────┘
                                │
                                ▼   universe.<chan>.<date>.json   (secID -> symbol, tick, expiry)
                 ┌─ pass 2: MARKET DATA ─────────────────────────────┐
                 │  incrementals (224.0.31.1) as today               │
                 │  -> <chan>.<date>.databento.bin                   │
                 └──────────────┬────────────────────────────────────┘
                                │
                                ▼
                        sim / models: universe resolves securityID -> instrument,
                        front month selected by volume within the ES asset
```

Each IR window is processed **independently** so its sequence space starts clean — the same reason
a separate pass is needed rather than simply prepending all seven files (that was tried; it
changed nothing, because concatenation shares one `qseq_num`).

Implementation options, cheapest first:

1. **`--snap-only` mode in `dbento_pcap_to_bin`**: iterate the IR files, restarting the decode
   context per file, and emit a definitions-only `.bin` (or JSON directly). `build_universe` then
   unions them. No change to the live decode path.
2. **Loop the existing binary once per IR file** from the batch script and union the
   `build_universe` outputs. Zero C++ changes; proven to work (this is how the table above was
   produced). Good enough to unblock, uglier operationally.
3. Reset `qseq_num` between files inside `PcapFileManager` — smaller code, but it touches shared
   decode state used by the live path. Least preferred.

**Merge across dates** into `master_universe.<chan>.json` so a session whose own IR capture is thin
still resolves; definitions are stable across adjacent dates. This also gives the roll calendar
needed for front-month selection across the corpus's five quarterly rolls.

**Acceptance gate addition:** a session is admissible only if its universe resolves the
front-month securityID that carries the session's dominant MBO volume. Catching this at conversion
time is the difference between noticing in seconds and discovering it when the sim produces no
fills.

### Supporting diagnostics (explain the execution result, not the point)
- **Signal accuracy (Tier C):** fill-probability calibration curves, level-break AUC, mid-move
  direction accuracy/AUC incl. M4. Directly ties a model's signal quality to its shadow P&L.
- **Model fit (Tier A/B):** OOS log-likelihood / AIC-BIC and time-rescaling residuals (M0–M3);
  simulated-vs-empirical stylized facts (queue-size dist, signature plot, flow autocorrelation).
  Used to diagnose *why* a model's signal helps or fails — a cheap pre-filter before the expensive
  simulator run.

**Cross-instrument (ES-first):** everything built on ES; NQ added later as replication + ES↔NQ
parameter-transfer (fit ES, run NQ, and vice versa).

### How to compare models — and why the execution backtest is the weakest test

Plugging a model into shadow and measuring slippage is the obvious comparison
and the one this document is built around. It is also the least informative of
the three available, and should not be the only one.

**It is confounded.** A slippage difference between shadow and shadow+model may
come from the model, or from its interaction with the throttle, the cancel
policy and the QCoord/PCoord coordination. Nothing in the output separates them.

**It throws away almost all the evidence.** The grid yields 3,180 fires per
config, and the effect being hunted is perhaps 0.1–0.3 ticks. Each session
meanwhile contains millions of ADD events about which the model made an
implicit prediction. The backtest discards ~99.99% of them.

**It cannot say why.** "M0 beat B0 by 0.15 ticks" offers no mechanism — better
fill probability, better adverse-selection filter, or noise.

#### 1. Predictive scoring (do this first; it is nearly free)

Full MBO means the ground truth for the model's actual question is *derivable*,
with no simulator. For any real ADD, every subsequent add, cancel and trade at
that price is in the capture, so we know whether an order joining that queue at
that position would have filled before the level was abandoned.

| target | label from the `.bin` | score |
|---|---|---|
| fill probability | did a join at this queue rank fill before the level died | log-loss, Brier, AUC, **calibration curve** |
| level survival | time until the level was abandoned | concordance index, Brier over horizons |
| adverse selection | mark-out at +1s/+5s/+30s on the hypothetical fill | AUC of `adverse()` against a toxic-fill label |

Millions of labels per session, decoupled from the algorithm, and diagnostic:
it says *which* of the model's three jobs it does well. Calibration matters as
much as discrimination — a model whose 0.3 fills 30% of the time can be used to
size and to choose between levels; one that only ranks correctly cannot.

**The trap.** A model that predicts *fills* better can make execution worse.
Shadow's edge is not getting filled, it is avoiding being filled when the price
is about to go through. Optimising for fill rate selects for toxic fills. The
label must be **filled AND not adversely selected** — the fill conditioned on
its mark-out — not merely filled.

#### 2. Paired execution backtest (the arbiter of economic value)

"Paired" is a statistical design, not an arm composition: run shadow and
shadow+model over the **same sessions with the same RNG seed**, so the two arms
see an identical placement stream and differ only where the model intervenes.
Day-level noise — that session's volatility, trend, spread regime — is common
to both and cancels in the difference.

    unpaired:  A = 0.50 ± 0.30,  B = 0.45 ± 0.30      overlapping, inconclusive
    paired:    d = A − B = 0.05 ± 0.02                 significant, same data

Pair at the **fire** level (same session, same fire time, same seed), not just
the session, and report the distribution of `d` rather than two means. Because
the model only gates placements, one run can log what it said at every ADD
shadow would have taken, so both arms can often be evaluated from a single
pass.

The seed policy in the grid (seed 1 everywhere, variation supplied by the 265
sessions) exists for this: it is what makes the arms comparable.

#### 3. Stylized-fact comparison (only for generative use)

If a model is used *generatively* — simulating a book rather than emitting a
signal — then the usual check applies: simulate, and compare order-flow, queue
length, spread and return distributions against the real corpus.

Dormant as planned: B0 and M0 both enter shadow *discriminatively*. B0 emits
`I` and `OFI`; M0 is queried as a precomputed `fill_prob[level][queue_rank]`
table. Nothing is ever run forward, so there is no synthetic output to check.
It becomes the gating test the moment we use M0 the other way — see below.

#### What none of these can do — and the one way out

**The simulator does not model the market's reaction to our own orders.** Every
fill is matched against recorded flow as though our presence did not perturb
it. A model that wins by placing more aggressively is partly exploiting that
blind spot, so report volume share per config alongside every result (the probe
measures it per leg now) and treat a config whose share is large as outside the
regime where the number means anything.

This cannot be fixed by replaying harder. A recording cannot respond to us.

**Future work: impact from M0 run generatively.** M0 is not only a lookup
table — Cont–Stoikov–Talreja is a birth–death process per price level, so with
the three rates already being estimated for the discriminative use

```
lambda_limit(i)   adds at level i, per unit time
lambda_cancel(i)  per-order cancel rate at level i
lambda_market     marketable executions at the touch
```

the same fitted object can be run *forward* to generate a book rather than
queried about one. That is the only route we have to an impact estimate: place
an order into the simulated book, let the arrival and cancellation rates
respond to the changed queue state, and measure what the response costs.

Shape of the work, in the order it has to happen:

1. **`ratefit`** — scan the corpus for the three rates per level, per side, per
   regime (RTH/ETH). Already required for M0's discriminative use, so this is
   shared, not extra.
2. **Forward simulator** — drive a book from those rates and confirm it
   reproduces the corpus's stylized facts: fat-tailed returns, volatility
   clustering, long-memory in order signs, the hump-shaped depth profile. This
   is where §3 above stops being dormant. A synthetic book with Gaussian
   returns and no clustering is not a market, and an impact number from it is
   worthless.
3. **Reactive placement** — make the arrival rates state-dependent on our own
   resting size, so adding depth at a level changes the flow into it. The
   crudest version (queue-position-only) already captures the first-order
   effect: our order pushes everyone behind us further back.
4. **Impact curve** — slippage versus participation under the generative model,
   compared against the replay result. Where the two diverge is where the
   replay's zero-impact assumption stops holding, which is the number the
   paper's threats-to-validity section currently has to assert without
   evidence.

The honest framing: this measures impact *under a model*, and a zero-intelligence
birth–death process is a weak model of a market that contains strategic
participants. It bounds the error in the replay result rather than replacing it,
and the bound is only as good as step 2's stylized-fact agreement. That is still
considerably better than the current position, which is a caveat with no number
attached.

#### Sequencing

Predictive scoring first: it is cheap, has enormous N, and kills bad models
before any CPU is spent on them. Then the paired execution backtest on the
survivors, as the test of whether the predictive edge is worth anything after
the algorithm, the queue and the latency have had their say.

### The shadow baseline grid — run this before any model arm

Shadow is the model-free baseline every model in §1 is scored against. Until its cost curve is
measured across size, participation and latency there is nothing to beat, and any claimed
improvement is unfalsifiable. This grid is that measurement. It also **replaces every empirical
number in `tech_reports/shadow_pov.pdf`** — see below for why the published ones cannot stand.

Full working log, with the debugging history behind each item:
`~/.claude/plans/ok-we-still-have-resilient-crab.md` §6.8.

#### Before any sweep: run one session in DEBUG

`PLACES_NO_CHECK_CONSTRAINT` is set in `DEFINES_OPT`, so optimised builds
compile out the `place<>` guards — both the write-once check and, more
importantly, the read-before-write check in `get()`/`getr()`, which is on the
hot path. In an opt build a config field that was never assigned returns
uninitialised memory instead of aborting.

That is an acceptable trade only if the invariant has been *observed* to hold.
So after any change touching a `place<>` field, and before committing CPU to a
sweep:

```bash
KSPRPROJ=~/kaspar-hft make debug
cd sim/src && make debug
./simg --datafile ... --probe-size 10 ...     # one session is enough
```

The guards fire at construction and on first read, so a short run exercises
them. A clean debug session is the evidence that the opt build's silence means
correctness rather than a compiled-out check. This is not hypothetical: an
`all_orders_max` assigned twice was caught exactly this way, and would
otherwise have silently taken the second value.

#### Session inputs: front month and universe

Two upstream facts had to be established from the data before any run is
meaningful. Both were wrong on the first attempt, and both are silent failures
— the sweep completes and produces numbers either way.

**Front month comes from traded volume, not a roll calendar.** A hand-written
roll table was 4–5 days early on every roll, which would have executed part of
the corpus in the illiquid contract:

| roll | hand-written | actual (volume) | front-month volume on the date I would have switched |
|---|---|---|---|
| ESH5 → ESM5 | 20250313 | **20250317** | ESH5 2,057,715 vs ESM5 318,192 |
| ESM5 → ESU5 | 20250612 | **20250616** | ESM5 1,250,123 vs ESU5 191,811 |
| ESU5 → ESZ5 | 20250911 | **20250916** | — |
| ESZ5 → ESH6 | 20251211 | **20251215** | — |

`volstats` now **counts** traded quantity — summing `lastQty` over the trade
records, attributed through its `orderID → securityID` map — rather than
reading CME's `l3_vol` statistic. `l3_vol` only appears when the capture
contains the windows carrying it, so an instrument can look dormant purely
because that message was not captured; every fill is a record we have already
read. Counted volume lands within ~7% of the reported figure, the gap being
trades whose order we never saw an ADD for.

The front month is the most-traded **outright**: ranking on volume alone put
the calendar spread `ESH5-ESM5` (202,930) above the deferred outright `ESM5`
(141,162) on the roll date, and a spread is not executable as the front
contract.

Regenerate with:

```bash
volstats --datafile 310.<date>.databento.bin --universe <universe>.json --out vol.<date>.json
# -> dbento_pcap_parse/scripts/front_month.310.tsv   (date, front, securityID, traded_qty)
```

**The universe is the master, not the per-date file.** 152 of 265 sessions have
a per-date universe that does not contain that session's front month at all.
This is not a roll artefact — it is the IR cycle: CME's instrument-replay
stream loops and restarts its sequence each cycle, so whether the front month's
definition appears depends on which IR windows a given date happened to
capture. Instrument definitions (symbol, securityID, tick) are static, so there
is no reason to source them per date.

The one genuinely daily quantity is the price limit, which is used for exactly
one thing: sizing OB's ladder (`maxpx = high_limit_px / minPriceIncrement`).
The master's merged limit is *below* the widest limit actually seen for every
contract (e.g. ESZ5 master 650,975 vs 737,025 observed), and a ladder that is
too small silently deletes far-resting orders — the bad-price path now removes
an order it cannot place rather than stranding it, so clipping is invisible
rather than fatal. A ladder that is too large costs one pointer per tick per
side.

So `grid_universe.310.json` = master definitions with each contract's limit
widened to the maximum it carried anywhere in 2025:

| contract | high_limit_px | ladder |
|---|---|---|
| ESH5 | 656,450 | 26,258 ticks |
| ESM5 | 662,275 | 26,491 |
| ESU5 | 709,775 | 28,391 |
| ESZ5 | 737,025 | 29,481 |
| ESH6 | 746,750 | 29,870 |

Every session then runs against one universe, so the corpus is not split
between two treatments.

#### Axes

**A. rate × size** — both latencies 500 µs (`--ob-delay-us 500 --ob-cancel-delay-us 500`):

| | 1 lot | 10 | 100 |
|---|---|---|---|
| 0.5% (`--place-rate-bp 50`)  | · | · | · |
| 1%   (`--place-rate-bp 100`) | · | · | · |
| 3%   (`--place-rate-bp 300`) | · | · | · |
| 5%   (`--place-rate-bp 500`) | · | · | · |

12 configs. Yields the slippage surface **and** the order-placement-rate → realised-participation
map. The paper has neither: it reports ρ≈1.5% as a *placement* rate and models participation
separately, never connecting the two.

**B. size depth at 3%** — `Q ∈ {1, 2, 5, 10, 20, 50, 100, 200}` with `ord_sz 1`, so the parent size
*is* the child-fill count and `Q` matches the paper's axis. 8 configs.

**C. latency sensitivity** at 3% / 100 lots — order = cancel latency ∈
`{0, 100, 200, 400, 500, 800, 1600, 3200, 6400}` µs. 9 configs. The 500 µs point is deliberate: it
makes C's curve pass through A's (300bp, 100) cell so the two grids cross-validate. `0` floors at
OB's 40 µs minimum.

**29 configs × 363 sessions = 10,527 runs**; 12 fires per session gives 4,356 samples per config.

#### Cost (measured, not estimated)

One full session to 16:30 ET, `--quiet`, with the probe: **131 s**. So **383 CPU-hours** →
**7.7 h wall at 50-way**. Disk is not a constraint (~3 GB quiet, ~6 TB unquiet, against 74 TB free
on `/vast/home`); `--quiet` exists because the volume buys nothing and the runs share an NFS filer.

#### What this fixes in the paper

1. **Size dependence becomes empirical.** The published section is an exponential/Gamma Monte Carlo
   and says so — *"a scaling law under a stylized model, not an empirical claim."* Its Table 2 is
   simulated, not measured. Grid B replaces it.
2. **One placement rate becomes four.** Everything published rests on the hardcoded
   `std::rand() % 128 >= 2`, i.e. ρ≈1.5%.
3. **Latency gets a section at all.** The paper never treats it as a parameter. Worse,
   `m2_kspr/frame_kaspr/src/SOM.cpp:1935` carries the same `payload->ts0 = o.ts` bug fixed on
   `feat/cancel-latency`, so the published figures were produced with new orders paying 1000 µs and
   **cancels paying zero**. They are optimistic by an unmeasured amount.
4. **Two fires per date becomes twelve.** The paper fires at 09:29 and 15:57 and then *discards*
   the open cohort because intraday drift contaminates it. Twelve fires measure the drift instead.
5. **ρ (our share of volume) is reported per fire**, so the threats-to-validity caveat about the
   zero-impact assumption becomes a measurement. Configs where our share is large must be flagged,
   not averaged — this matters most in the 100-lot row, which is exactly the extrapolation the
   paper warns against.

Not fixed by the grid: there is still **no competing baseline**. The paper concedes the benchmark
role is *"proposed rather than demonstrated"*. That is what M0–M3 in this document are for, which
is the other reason the grid has to land first.

#### Prerequisites (done)

- **Participation is now measured.** `SlippageProbe` subscribes to `TradeNotify` and accumulates
  market volume per leg, emitting `buy_mkt_vol`, `sel_mkt_vol`, `buy_part`, `sel_part`. Without it
  realised participation — our filled quantity over market volume in the window — is unobtainable.
- **Leg times come from the fill,** not from the 1 s clock tick that noticed the leg was done. The
  first real results had every `buy_ns` at 1.00–1.02 s, an artefact of the observer: harmless for
  slippage, fatal for anything per unit time.
- **`Q` semantics.** Child size is clamped by
  `min(ord_sz, lev_orders_max − sz_at_px, diff_from_target, payload_sz)`, so with `ord_sz 5` a
  100-lot parent is ~20+ children and the count is emergent. Grid B pins `ord_sz 1`; elsewhere
  report the measured `n_fills` as the empirical `Q`.

#### Open before launch

- **20250210 still aborts** on a crossed book — OB retains an ask whose DELETE is present in the
  capture (order `6414436033822`, NEW 00:00:13, DELETE 00:24:09, still in the book at 17:59). Not
  the halt gating, not a missing packet, not an unhandled `orderUpdateAction`, not a recovery-gate
  drop — all four were checked and refuted. A session that hits this dies mid-sweep.
- **Only one session is proven clean.** A ~20-session smoke test across the year (Sundays,
  holidays, the 2025-03-09 DST change, the March roll) should precede 10,527 runs.
- **No runner yet**: pinned binary, per-date universe with master fallback, DST-correct 16:30 ET
  cutoff, retry, and CSV aggregation.

#### Open question

The paper evaluates a **two-leg NQ/ES spread** fired in ten lock-step chunks. This grid is **ES
outright**, single parent via `targetpos`. The numbers are therefore a *different experiment*, not
a better-powered version of the published one — either the paper's framing changes, or the probe
grows a spread mode.

---

---

## 6. Phased milestones (ES-first)

- **Phase 0a — One month of ES.** Run `dbento_pcap_to_bin` over ~21–22 ES (ch. 310) sessions
  (D0a). Measure one session first for size/wall-clock/RSS before committing disk. Gate every
  session on `binstats` (FDF Adds present) + `verify_merged` + plausible filtered-drop counts.
  Settle the front-month/roll decision. **Exit:** a month of accepted `.bin` files plus their
  `build_universe` output, and a written note fixing which timestamp field is *the* clock.
- **Phase 0b — Replay path.** Wire `--replay` (BFA) into `kaspr` and pass the round-trip test
  (D0b). **Exit:** a `.bin` replays through the books and SOM deterministically, matching a direct
  PCAP run.
- **Phase 0c — Data spike.** Export one ES session to the columnar format (D1) and build the reader
  + book re-walker with queue rank (D2). Sanity-check event counts, queue-rank distribution,
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
- **Phase 6 — Inventory paradigm (M5/M6).** Add Avellaneda–Stoikov then Guéant et al. as shadow's
  placement/skew layer (reservation-price + optimal half-spread), on top of the best fill/toxicity
  model. Calibrate `σ`, the fill-rate curve `λ(δ)`, and `γ`. **Exit:** does inventory-aware skew
  improve shadow's risk-adjusted P&L (and cut inventory excursions) vs the zero-inventory baseline?
- **Phase 7 — NQ replication + transfer.** Re-run best models on NQ; ES↔NQ transfer tests.
  **Exit:** cross-instrument execution comparison.
- **Phase 8 — Write-up.** Execution tables (baseline vs each model, per regime, ES & NQ), signal
  diagnostics, transfer, limitations. Candidate `tech_reports/` paper alongside `shadow_pov`.
- **Phase 9 — Open-source release.** Land the model implementations in kaspar and tag the release
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

## Come back to this

### 20250509 — a crossed book in the DATA (closed)

Tracked in GitHub issue #58, not here. Short version: the Databento capture for
ESM5 contains a genuinely crossed book at 00:20 ET, proven by an independent
replay that reaches the same `best_bid 22763 > best_ask 22758`. Not our
reconstruction. The crash is accepted; it costs one session per config.


## What the paired number actually measures, and why VWAP replaces it

The probe fires a round trip: buy n, then sell n. `slip_paired` is
`(buy_vwap - sel_vwap)/2`, which is only a like-for-like comparison if the two
legs are comparable. **They are not, and the gap grows with size.**

The two legs run at different times, for different durations, in different
liquidity. At 100 lots a leg takes ~84s, so a round trip spans ~170s -- and
whichever side the book favours fills faster. The slow leg leaves the system
holding inventory for the difference, which is a directional position nobody
asked for. Its P&L lands in the measurement as if it were execution cost.

That is exactly the divergence in the data: at 1 and 10 lots `slip_paired` and
`slip_legsum` agree to within 0.05 ticks, and at 100 lots they are 3.3x apart
(+0.4745 against +1.5631). The mid-based numbers are absorbing drift over a leg
long enough for the price to move.

**So the interval VWAP is the benchmark that means something**, and each leg
gets its own -- the VWAP of everything that traded during THAT leg's interval,
not a shared one. Drift cancels by construction, because our fills and the
benchmark are drawn from the same window. What survives is selection: whether
we were systematically on the wrong side of the trades happening around us.

Three benchmarks now emitted per leg, because they answer three questions:

| benchmark | question | expected sign |
|---|---|---|
| vs mid at arrival | what did the decision cost against the fair price when we committed? | positive |
| vs interval VWAP | how did we do against everyone trading alongside us? | ~0 if only drift; positive if picked off |
| vs touch at arrival | what did patience buy, against just crossing the spread? | **negative** -- that saving is the case for a passive algo |

### The market-making corollary

For a market-making scenario the buy-vs-sell comparison IS the thing that
matters -- the spread you capture is exactly `sell_vwap - buy_vwap`. But that
only holds if the two sides are simultaneous and matched. The moment one side
fills faster you are carrying inventory, and inventory P&L swamps the spread you
were trying to measure.

The consequence is a design constraint, not a measurement detail: **market
making wants small orders and near-zero inventory.** Small clips fill on both
sides at comparable speed, so the paired number stays a spread measurement
rather than a directional bet. Large parents are the opposite -- they guarantee
an inventory imbalance for the duration, which is why the paired number stops
being interpretable at 100 lots.

This also says the 100-lot cells should not be read as market-making results at
all. They are execution-cost results for a directional parent, and the VWAP
benchmark is the only one of the three that treats them fairly.

## Cost tracks drift, not size — and what that means for the paper

Measured on 8,313 completed round trips (config_a, 6 lights, place_rate 0.5%,
sizes 1/10/100). All numbers are the BUY leg against the arrival mid, in ticks.

### Definition: drift

    drift = mid_sell - mid_fire          (ticks, signed)

    mid_fire   the mid at t0, when the parent was released to the lights
    mid_sell   the mid at t1, when the buy leg finished and the sell leg began

Both are recorded by the probe as `(best_bid + best_ask)` and halved on emit, so
the CSV columns are in ticks and the difference is in ticks. ES trades a 1-tick
spread in ~93% of fires, so one unit of drift is roughly one full spread.

Sign: POSITIVE means the mid ROSE while we were buying, which is against us.
For the sell leg the sign convention flips, and unless stated otherwise every
drift figure in this section is over the BUY leg only.

Three things it is NOT, all of which matter for how it is read:

- **Not a trend estimate.** It is the realised move over one specific window,
  not a fitted drift parameter. +1.78 ticks over 88s is ~73 ticks/hour; nothing
  is claiming ES trends at that rate.
- **Not exogenous.** The window's LENGTH is determined by how our own fills
  went, so drift is measured over an interval we selected by finishing when we
  finished. This is the whole reason it reads as selection rather than
  direction, and why conditioning on duration changes the picture.
- **Not our impact.** The sim cannot produce impact -- our fills are additive
  (section 6) -- so drift is the recorded market moving on its own, never the
  market reacting to us. In a live setting part of this quantity WOULD be our
  own footprint, and nothing here can separate the two.

### 1. Against arrival price, cost rises steeply with size

| size | n | buy leg vs arrival mid | sell leg | participation (ours/(ours+mkt)) | leg duration |
|---|---|---|---|---|---|
| 1 | 3014 | +0.4897 | +0.4507 | 1.96% | 0.9s |
| 10 | 3009 | +0.5132 | +0.5376 | 2.36% | 8.1s |
| 100 | 2290 | +1.4400 | +1.2431 | 2.37% | 84.1s |

`slip_paired` hides this completely -- it is flat at +0.47 to +0.50 across all
three -- because it nets the legs against each other and cancels the drift that
IS the cost. Any paper quoting only the paired number reports a size-independent
execution cost, which is an artefact of the metric.

Note ES trades a 1-tick spread in ~93% of fires, so half a tick is the floor for
a spread-crossing round trip. 1 and 10 lots sit essentially ON that floor
(+0.49, +0.51). 100 lots pays +1.44, i.e. the floor plus ~0.94.

### 2. That excess is drift, and drift is not a trend

| size | mean drift over the buy leg | corr(slip, drift) | slip - drift |
|---|---|---|---|
| 1 | -0.058 | +0.994 | +0.547 |
| 10 | +0.103 | +0.895 | +0.410 |
| 100 | +1.776 | +0.906 | -0.336 |

Slippage tracks drift at r ~ 0.9 at every size. And the drift is wildly
superlinear in duration -- +1.78 ticks over 88s would be ~73 ticks/hour, which
no trend produces -- so it is selection, not direction: legs that take a long
time are exactly the ones where the price ran away.

The last column is the result that matters for the algorithm's defence:
**measured against the mid at the moment the leg COMPLETED, the 100-lot
execution is the best of the three (-0.336)**, buying a third of a tick below
the terminal mid while 1 lot pays +0.547 above it. Per-fill execution quality
IMPROVES with size. What degrades is time spent exposed.

### 3. At matched duration, size stops mattering

Buy-leg cost bucketed by duration, across parent sizes:

| duration | 1 lot | 10 lots | 100 lots |
|---|---|---|---|
| 0-1s | +0.43 (drift -0.12) | -0.64 (drift -1.71) | -- |
| 1-5s | +0.68 (drift +0.15) | +0.46 (drift +0.05) | -- |
| 5-20s | +0.65 (drift +0.12) | +0.77 (drift +0.47) | **+1.88 (drift +3.37)** |
| **20-60s** | -- | **+0.91 (drift +0.84)** | **+0.97 (drift +0.77)** |
| 60s+ | -- | +0.64 (drift +0.46, n=26) | +1.65 (drift +2.14) |

A 100-lot leg and a 10-lot leg that both take 20-60s cost the same (+0.97 vs
+0.91) and saw the same drift (+0.77 vs +0.84). **Ten times the size, no extra
cost.**

And duration alone is not the driver either: the most expensive cell in the
table is the FASTEST 100-lot bucket (5-20s, +1.88) because its drift is +3.37 --
legs that finish quickly *because* the market is running, filling our resting
orders on the way past.

So the predictor is drift, everywhere. Size matters only through its correlation
with exposure: more time in the market, and more fills landing in the moments
the market moves.

### 4. The legs are structurally asymmetric

| size | buy leg median | sell leg median | ratio | round-trip P&L | fires profitable |
|---|---|---|---|---|---|
| 10 | 4.83s | 0.39s | 12.9x | -0.948 ticks | 2.1% |
| 100 | 71.10s | 0.58s | 127.4x | -0.907 ticks | 0.1% |

The buy leg is worked; the sell leg is dumped in under a second even at 100
lots. Splitting fires by how lopsided they are shows this does NOT drive the
cost (balanced quartile +0.4599 vs lopsided +0.4597 at 100 lots), but it does
mean the paired number compares a worked execution against something close to a
market order.

**Open question, and it needs answering before the sell-leg numbers are quoted:**
why does a 100-lot sell leg complete in 0.58s? If those orders are marketable on
arrival and filling through `fill_stray_sim_orders` rather than resting, the
sell leg has never measured passive execution.

### 5. Why aggression on the lagging leg is the interesting experiment

Drift cost is unbounded in exposure; crossing the spread is a fixed ~0.5 ticks.
Trading an unbounded cost for a bounded one is the right trade at size, and
capping the slow leg's duration is what does it. The 20-60s row says a
well-behaved 100-lot leg is ALREADY as cheap as a 10-lot one, so the prize is
moving the 60s+ and the drift-heavy 5-20s populations into that regime.

The three benchmarks are what make this measurable rather than arguable: an
aggressive-completion arm should show `slip_vs_touch` going LESS negative (we
gave up spread) against `slip_vs_vwap` improving (we stopped bleeding drift).
Neither can be called from the arrival-mid numbers alone.

### 6. The caveat that limits all of it

Our fills are ADDITIVE, not substitutive. `fill_prev_sim_order` fills our
resting order with the same size as the real execution in front of it, in
addition to that trade rather than instead of it -- so a 5-lot print takes 5
lots from the real order AND gives us 5. We are a ghost in the queue: filled
without anyone losing a fill.

Consequence, measured: fires where our fill exceeds ALL market volume in the
window are 0.1% at 1 lot, 17.9% at 10, and **77.1% at 100**. At 100 lots the
cost number is not an execution measurement -- it is what execution would cost
if we could take liquidity that was never available, with nobody reacting.

This is the hard ceiling on the size axis, and the paper has to state it. The
defensible range on this corpus is 1-10 lots; above that the simulator is
answering a counterfactual that has left the market behind.

### 7. Prior art for the drift result -- it is NOT novel, and the paper must say so

The core relationship is well covered. Searched and found:

- **Rzayev, Sakkas et al. (?), "The Market Maker's Dilemma: Navigating the Fill
  Probability vs. Post-Fill Returns Trade-Off"**, arXiv:2502.18625 --
  https://arxiv.org/html/2502.18625v2
  Documents a negative correlation between a maker order's fill probability and
  its subsequent return CONDITIONED ON FILLING, which they name the **negative
  drift of maker orders**, most pronounced at short timescales. This is our
  selection effect under another name: conditional on being filled, drift is
  adverse. Cite this as the direct antecedent.

- **"Model Predictive Control For Trade Execution"**, arXiv:2603.28898 --
  https://arxiv.org/html/2603.28898v1
  Orders filled at the FRONT of a long, stable queue collect half the spread
  relative to the mid; orders filled at the END of a COLLAPSING queue pay the
  spread. We measure +0.49 on the buy leg, i.e. we PAY it -- so our fills are
  landing in the collapsing-queue regime. That is a diagnosis of our own fill
  path, not just a citation, and it points at the stale-marketable fill
  mechanism (section 4's open question).

- **"Optimal Execution with Passive Market Impact"**, arXiv:2607.28323 --
  https://arxiv.org/abs/2607.28323v1
  Frames passive execution as balancing fill probability, adverse selection and
  opportunity cost; fills arise from quote adjustments. The framework our
  results sit inside.

- **"Target Close and Implementation Shortfall"**, arXiv:1205.3482 --
  https://arxiv.org/pdf/1205.3482
  The IS decomposition. The market-movement component is what we are calling
  drift.

- **"Optimal solution of the liquidation problem under execution risk"**,
  arXiv:2011.02979 -- https://arxiv.org/pdf/2011.02979

- **Almgren & Chriss (2000), "Optimal execution of portfolio transactions"** --
  the canonical impact-vs-timing-risk trade-off. Our "drift cost is unbounded in
  exposure, spread is fixed" argument is their result restated.

- **Perold (1988), "The implementation shortfall: paper versus reality"** --
  origin of the decomposition.

### What is actually ours to claim

Two things, both narrower than "cost is driven by drift":

1. **The matched-duration equivalence.** A 100-lot leg and a 10-lot leg that
   both take 20-60s cost +0.97 and +0.91 with the same drift -- ten times the
   size, no extra cost. Sharper than "cost rises with size", and not something
   we found stated this cleanly elsewhere.

2. **The negative result about the metric.** `slip_paired`, the
   direction-agnostic number a practitioner would naturally reach for, is FLAT
   across a 100x size range and conceals the entire effect. That is a warning
   about measurement, and it is probably the more useful contribution.

### The caveat that has to accompany both

**Cost is not driven by size here because this simulator has NO MARKET IMPACT.**
Fills are additive (section 6): `fill_prev_sim_order` gives our resting order
the same size as the real execution in front of it, in addition to that trade
rather than instead of it. Nobody loses a fill to us and no price moves because
of us.

So "cost tracks drift, not size" is a statement about a world without impact.
In reality the size term returns through exactly the channel we removed, and a
referee will say so first. The honest framing is: *within a no-impact
counterfactual, and over the 1-10 lot range where our fills stay a small
fraction of market volume, execution cost is explained by realised drift over
the execution window rather than by parent size.* Everything above 10 lots is
reporting on a market that could not have absorbed us.

The correlation is also near-definitional -- slip and drift share `mid_fire`,
and our fills track the mid -- so the load-bearing quantity is the RESIDUAL,
`slip - drift = buy_vwap - mid_sell`, which is -0.336 at 100 lots against +0.547
at 1 lot. That residual is the real per-fill execution quality, and it improves
with size.


## 2026-09-13 — the probe was rewritten, and it takes the drift definition with it

**Everything above that describes a 30-minute round trip is history, not the
current design.** Specifically §"ONE run per session" (~L1631), §"`SlippageProbe`
fires every 30 minutes" (~L1749) and the sections on fill timeouts and the
sampling cadence describe an actor that no longer exists. They are kept because
the results they explain were produced by it; they are not a description of what
runs now.

What runs now: both sides quote **simultaneously and continuously** for the
whole session. BUY lights carry `target_pos = +sz`, SEL lights `-sz`, set once
and never revised. A window closes when its minimum has elapsed **and** both
legs have filled their size. Inventory is carried across boundaries, never
unwound.

### Drift had to be redefined, and every drift table above was computed the old way

The old definition was `drift = mid_sell - mid_fire`: the mid at the sell leg's
arrival minus the mid at the buy leg's arrival. It was a real quantity **because
the two legs arrived at different times** — at 100 lots, ~170s apart.

Quoting both sides together removes that gap by construction. `mid_sell` and
`mid_fire` become the same number, so the old expression is identically zero and
`slip_legsum` collapses algebraically onto `slip_paired`. The metric did not get
worse; it stopped existing. Two columns were printing one number with
independent confidence intervals, which is how it was caught.

Drift is now measured **forward from the common arrival**, which is a direction
the new design does have:

```
buy_drift = buy_end_mid - mid_fire     mid when the buy leg finished
sel_drift = sel_end_mid - mid_fire     mid when the sell leg finished
drift     = mid_close   - mid_fire     mid when the window closed
```

`buy_drift` and `sel_drift` differ precisely because the legs finish at
different times — the asymmetry the sequential design confounded with cost is
now the measurement rather than an artefact of it.

**Consequences for the write-up.** The "cost tracks drift, not size" result, the
residual `slip - drift`, and the numbers quoted for them (+0.4745 vs +1.5631 at
100 lots; residual -0.336 at 100 lots against +0.547 at 1 lot) all come from the
sequential probe and the old drift definition. They are not reproducible from a
corpus generated by the current one, and no corpus generated by the current one
exists yet. Either the claim is restated against forward drift and re-measured,
or it is presented explicitly as a result about sequential execution. It cannot
be carried over as-is.
