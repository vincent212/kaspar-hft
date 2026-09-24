# Paper draft v2 — the pipelining paper

**Working title:** *Thread-Hop Overhead Is a Wash Under Self-Exciting Arrivals: A Closed-Form and Empirical Case for Multi-Actor HFT Pipelines*

**Thesis (one sentence).** The standard objection to actor-based HFT systems is thread-hop overhead; on real CME MDP3 packet-arrival streams that overhead is dwarfed by Hawkes cluster wait, so multi-actor pipelines compress the tail without paying a meaningful median cost — a Pareto win that disappears under a Poisson null with matched rate.

**Contribution.** Empirically-anchored design equation for HFT actor-pipeline stage count, calibrated on 281 sessions of CME MDP3 NQ front-month packet arrivals with Kaspar-HFT's measured busy-poll hop cost h ≈ 90 ns, showing that median-vs-tail Pareto frontier for N stages flips sign between real Hawkes arrivals and a rate-matched Poisson null.

**Prior-art gap already established:** two rounds of fork-based literature search (2026-09) covering foundational, theoretical-bursty-tandem, Hawkes-adjacent, and empirical-applied literatures. Verdict: no direct prior art on either (i) tandem-vs-single-queue as a function of arrival law under self-exciting arrivals or (ii) empirical CME-calibrated tandem sweep with Poisson null. Full reference list under **References** at the end of this outline.

---

## 1. Introduction

### 1.1 The practitioner debate
The single-threaded event-loop vs multi-actor pipeline debate in HFT framework design. Cite the standard objection: every actor hop is a mailbox handoff plus a cache-line transfer, and Δ latency added per stage is real. Motivate the empirical question: does that stage-count cost outweigh the tail-compression benefit under real feed conditions?

### 1.2 What this paper does
- Measures packet-Hawkes on CME MDP3.
- Simulates N-stage tandem Lindley on the observed packet-arrival streams at Kaspar-HFT's calibrated h = 90 ns busy-poll hop.
- Compares against a rate-matched Poisson null on the same arrival count.
- Reports median and p99 latency as a function of N under both regimes.
- Produces a design equation and a specific recommendation.

### 1.3 Contribution and prior-art positioning
One paragraph naming the four adjacent literatures and why each stops short of the specific question.

---

## 2. Data and measurement setup

### 2.1 Corpus
281 sessions of CME MDP3 channel 318 (NQ) front-month pcap-derived tapes, 2025-01 to 2026-02, RTH only, holiday sessions excluded.

### 2.2 Three timestamps
`transactTime`, `sendingTime`, `packet_seq`. Definitions and what the differences physically mean. Note that `handlerendtim` on MBO records carries the pcap wire-arrival time at Databento's colo tap (limitation acknowledged).

### 2.3 Packet grouping
`packet_seq` groups messages into UDP packets; arrival = min `transactTime` per packet_seq. Cross-check that grouping by `sendingTime` gives the same partition (empirically: 1 packet_seq in a million spans two `sendingTime`s).

### 2.4 Per-window panel
30-min RTH windows. Per-window row: n_msg, n_pkt, span_mean, packet-Hawkes fit (λ̄_pkt, n_pkt), utilisation ρ, and the tandem Lindley outputs.

---

## 3. Packet arrivals are Hawkes-clustered

### 3.1 Exp-Hawkes MLE on packet arrivals
Standard Ogata log-likelihood, closed-form compensator. Fit per 30-min window.

### 3.2 5×5 grid on (λ̄_pkt, n_pkt)
Equal-quantile bins. Cell mass check.

### 3.3 Fano and Hurst
Model-free confirmation that packet arrivals are clustered (F(T) > 1 rising with T, H ≈ 0.65–0.75). Poisson null gives F(T) = 1, H = 0.5.

### 3.4 Interarrival-gap diagnostic
Empirical P(gap < mean/10) versus the Poisson benchmark 9.52%. Confirms clustering.

---

## 4. Span is not the story

Short section (2 pages max).

### 4.1 Empirical span distribution
On the NFP-day close-hour window and averaged across the corpus, span_p95 = 1 in 95 %+ of windows, mean span 1.06–1.14. Only max span reaches into the tens.

### 4.2 Span-scaled service is a 2–3 % correction
Two-model Lindley on 5 sessions Mar 3–7, 2025: PACKET-only (constant service) vs SPAN-scaled service (service scales with span). B/A ratio 1.02–1.03 on p99, 1.05–1.07 on p99.9. **Span is not the tail driver on this feed.**

### 4.3 Implication
The paper's simulator uses constant service per packet arrival. Span-scaled service can be retained as a plug-in but is not load-bearing.

---

## 5. Closed-form results

Two theorems and a corollary anchor the paper's simulation. The first is arrival-law-independent and gives a hard upper bound on burst-tail compression. The second uses the cluster representation of Hawkes to obtain a closed-form tail ratio in a batch-arrival approximation of the tandem. The Poisson case falls out as a corollary.

### 5.1 Burst-limit throughput theorem
**Theorem 1 (Burst-limit N-factor speedup).** Consider an isolated cluster of $k$ arrivals that all occur within a time interval shorter than $T$. Let $\tau_k^{(1)}$ denote the exit time of the last arrival under a single-stage deterministic-service queue with service $T$, and let $\tau_k^{(N)}$ denote the exit time under an $N$-stage tandem with per-stage deterministic service $T/N$ and per-stage hop delay $h$. Then

$$
\tau_k^{(1)} = k T, \qquad \tau_k^{(N)} = T + (k-1)\,\frac{T}{N} + (N-1)\,h,
$$

and in particular

$$
\lim_{k \to \infty} \frac{\tau_k^{(1)}}{\tau_k^{(N)}} = N.
$$

*Proof sketch.* Single-stage: service is FIFO, so exit time of the $k$-th arrival is $kT$ regardless of the cluster's inter-arrival structure (as long as all arrivals fall inside the busy period of the first). Tandem: the first message traverses all $N$ stages sequentially, exiting at $T + (N-1)h$. Once stage 1 is free (at $T/N$), the second message enters stage 1 immediately, exits stage 1 at $2T/N$, and so on. By induction the $k$-th arrival exits stage 1 at $kT/N$ and traverses stages $2$ through $N$ back-to-back (assuming pipeline saturation), exiting at $T + (k-1)T/N + (N-1)h$. The ratio is $kN/(N + k - 1 + N(N-1)h/T) \to N$. $\square$

This is the mechanism. It applies to any deterministic-service tandem regardless of the arrival law; the arrival law determines *how often* large-$k$ clusters occur, which is what §5.2 addresses.

### 5.2 Cluster representation and tail-ratio theorem

**Fact (Hawkes–Oakes 1974 cluster representation).** A stationary Hawkes process with background rate $\mu > 0$ and branching ratio $n \in [0, 1)$ is stochastically equivalent to a **Poisson-cluster process**: a rate-$\mu$ Poisson process of immigrant arrivals, each of which independently spawns an offspring cluster whose total size $K$ (including the immigrant) follows the **Borel distribution**

$$
\Pr(K = k) = \frac{(n k)^{k-1} e^{-n k}}{k!}, \qquad k = 1, 2, 3, \ldots
$$

with mean $\mathrm{E}[K] = 1/(1-n)$ and variance $\mathrm{Var}[K] = n/(1-n)^3$. Near criticality, $\Pr(K > k) \sim k^{-1/2}$ (Borel–Tanner).

**Theorem 2 (Tail-ratio in the $M^{[K]}/D/1$ batch approximation).** Approximate the tandem by a batch-arrival $M^{[K]}/D/1$ tandem with immigrant rate $\mu$, batch size $K \sim \mathrm{Borel}(n)$, and per-stage deterministic service $T/N$ (ignoring per-stage hop $h$ in the burst limit). Let $W^{(N)}$ denote the steady-state waiting time of a random message under $N$ stages. Then in the near-critical limit $n \to 1$,

$$
\Pr\!\big(W^{(N)} > x\big) \;\sim\; \Pr\!\Big(K \cdot \tfrac{T}{N} > x\Big) \;\sim\; \Big(\tfrac{N x}{T}\Big)^{-1/2}
$$

as $x \to \infty$, and the tail ratio against single-stage is

$$
\frac{\Pr\!\big(W^{(N)} > x\big)}{\Pr\!\big(W^{(1)} > x\big)} \;\longrightarrow\; N^{-1/2}.
$$

Equivalently, at any fixed high tail quantile $q \in (0.99, 1)$, the tandem's quantile is smaller by a factor of $N$:

$$
W_q^{(N)} \approx W_q^{(1)} / N \qquad (\text{high-}q,\ n \to 1).
$$

*Proof sketch.* Under the batch approximation, the dominant contribution to a large waiting time is a single batch of exceptional size arriving during a busy period. Batch of size $K$ takes $K \cdot T/N$ to clear at each stage in isolation, but pipelining across stages compresses this by Theorem 1 back to $T + (K-1)T/N$, which is $\Theta(K T / N)$ for large $K$. The tail of the waiting time therefore inherits the Borel tail of $K$ multiplied by $T/N$. Substituting $\Pr(K > k) \sim k^{-1/2}$ gives the stated tail. The ratio is exact in the tail limit. $\square$

**Corollary 3 (Poisson null).** Under a rate-matched homogeneous Poisson arrival process (so $K \equiv 1$ almost surely), the tail of $W^{(N)}$ is light (exponential decay under M/D/1), and

$$
\frac{\Pr\!\big(W^{(N)} > x\big)}{\Pr\!\big(W^{(1)} > x\big)} \;\longrightarrow\; 1
$$

as $x \to \infty$. No tail compression from splitting. The only effect of splitting is $+(N-1)h$ added to the median.

### 5.3 What the theorems and the empirical work jointly say
Theorem 1 is a hard upper bound on burst compression, always achievable in the pipeline-saturated limit. Theorem 2 says that under Hawkes arrivals, cluster sizes are heavy-tailed, and the tandem's per-stage batch compression translates directly into a $N^{-1/2}$-power tail ratio versus single-stage. Corollary 3 says the same architecture achieves nothing under Poisson because clusters are trivial. The empirical simulation of §7 tests whether the *quantitative* prediction $W_q^{(N)} \approx W_q^{(1)}/N$ holds on real CME MDP3 packet arrivals at the paper's $(\bar\lambda_{\mathrm{pkt}}, n_{\mathrm{pkt}})$ operating points, with real inter-cluster overlap and real hop cost $h$, and against a shuffled Poisson null.

### 5.4 Scope of the closed forms
- Theorem 1 is exact in the burst-saturation limit; away from it, the pipeline warmup dominates and the speedup is smaller by a factor of $1 + (N-1)/(k-1)$ that vanishes for large $k$.
- Theorem 2 uses the batch-arrival approximation; the full Hawkes tandem waiting-time distribution is not in the literature and is not proved here. The batch approximation is exact in the limit $n \to 1$ where cluster durations shrink to zero relative to inter-cluster gaps.
- Corollary 3 relies on the classical M/D/1 tandem tail being exponential (rate-matched Poisson single-stage has a light tail).
- The role of $h$ is: (i) additive on the median as $(N-1)h$, and (ii) subleading on the tail, contributing $O((N-1)h/T)$ correction to the tail ratio.

## 6. Simulator: N-stage tandem Lindley on packet arrivals

### 6.1 Model
Packet arrivals t₁ < t₂ < … enter stage 1. Each stage k has deterministic service T/N and adds a per-stage hop delay h between stages, where T is the total decode floor and h is the framework's measured mailbox hop cost. Each stage is a single-server FIFO queue with its own independent server (its own actor). Standard Lindley recursion per stage, fed by the previous stage's departures plus h.

### 6.2 Pipelining is what makes multi-stage work
The tandem is a *pipelined* design: each stage's actor runs on its own thread, so at the same wall-clock instant stage 2 can be processing message k−1 while stage 1 is processing message k. The paper's simulator captures this implicitly, because Lindley is applied separately per stage and each stage tracks its own "when did the previous message leave" timeline — the two servers' busy intervals overlap on the wall-clock axis.

**Worked example.** Three packets arriving at t = 0, 1, 2 μs, single-stage total service T = 10 μs, versus a two-stage tandem with per-stage service T/2 = 5 μs and hop h = 3 μs:

| | msg 1 | msg 2 | msg 3 |
|---|---:|---:|---:|
| single-stage arrival | 0 μs | 1 μs | 2 μs |
| single-stage latency | 10 μs | 19 μs | **28 μs** |
| tandem arrive stage 1 | 0 | 1 | 2 |
| tandem wait stage 1   | 0 | 4 | 8 |
| tandem leave stage 1  | 5 | 10 | 15 |
| tandem arrive stage 2 (+h) | 8 | 13 | 18 |
| tandem wait stage 2   | 0 | 0 | 0 |
| tandem leave stage 2  | 13 | 18 | 23 |
| **tandem latency** | **13 μs** | **17 μs** | **21 μs** |

At wall-clock t = 8, stage 1's actor is busy on message 2 while stage 2's actor is busy on message 1: two actors doing useful work at the same time. Msg 1 pays a 3 μs hop cost (latency 10 → 13); msg 3, at the burst tail, drops from 28 μs to 21 μs. Median hurts, tail wins — same message count, better pipeline throughput 2/T instead of 1/T. This is why the two headline curves in §7 (Hawkes vs Poisson) split apart at higher N: under Hawkes there are enough bursts for the pipeline drain effect to matter; under Poisson the queue is rarely full so pipelining wins nothing.

### 6.3 Calibration
- T from fast_send Table 6 (NQ book).
- h from Kaspar-HFT busy-poll async mailbox on isolated cores.
- Cite both.

### 6.4 Determinism and cost
Simulator is deterministic given the tape and (T, h, N). One pass per stage per window; total cost is negligible vs the tape read.

---

## 7. Experimental design

### 7.1 Question
Does the N-stage tandem reduce the p99 latency under real Hawkes packet arrivals? Does the same architecture reduce it under a Poisson null with matched arrival count?

### 7.2 Sweep
N ∈ {1, 2, 4, 8, 12}. Two arrival regimes:
- **Hawkes:** real packet arrivals from the tape.
- **Poisson null:** uniform shuffle of the same n arrivals into [T₀, T₁], equivalent to Poisson conditional on n.

Per (window, N, regime): p50, p95, p99, p99.9, max of end-to-end latency.

### 7.3 Aggregation
Median across sessions inside each (λ̄_pkt, n_pkt) grid cell. Two-panel figure per statistic: Hawkes cell vs Poisson cell.

### 7.4 Conjecture (falsifiable)
Under Hawkes: p99(N)/p99(1) drops toward ~1/N; p50(N) grows at slope << h per stage relative to the p50(1) baseline (dominated by cluster wait, not by hop cost).

Under Poisson: p99(N)/p99(1) stays near 1; p50(N) grows at slope ≈ h per stage in relative terms (hop cost is most of the median).

### 7.5 Falsification
Any of these outcomes kills the conjecture:
- Hawkes p99(N) flat in N (splitting doesn't help under real clustering).
- Poisson p99(N) also drops with N (splitting helps everywhere, not just Hawkes).
- Hawkes p50(N) rises at slope ≈ h per stage (hop cost is not dwarfed).

---

## 8. Results

### 8.1 Tail collapse under Hawkes, not under Poisson
Two curves overlaid: p99(N)/p99(1) versus N. Hawkes drops sharply; Poisson stays flat.

### 8.2 Median cost of splitting is negligible under Hawkes
Two curves overlaid: p50(N) − p50(1) versus N, in μs. Both grow at ~h per stage — but Hawkes p50(1) is many μs (cluster wait), so (N−1)·h is a small fraction; Poisson p50(1) is close to T, so (N−1)·h is comparable to service.

### 8.3 Design equation
$$
p_{99}^{\text{Hawkes}}(N) \approx \frac{p_{99}^{\text{Hawkes}}(1)}{N}, \qquad p_{50}^{\text{Hawkes}}(N) \approx p_{50}^{\text{Hawkes}}(1) + (N-1)\, h
$$
Fitted on the corpus. N* per (λ̄, n) cell for a target tail-vs-median trade-off.

### 8.4 Sensitivity check on h
Sweep h ∈ {30, 90, 300, 1000} ns. Locate the h at which multi-hop stops being free on median.

### 8.5 Grid heatmaps
5×5 median-per-cell heatmaps of p99(N=1), p99(N=4), and the ratio p99(N=4)/p99(N=1), on (λ̄_pkt, n_pkt).

---

## 9. Implementing a multi-actor pipeline

This section maps the abstract N-stage design equation onto concrete framework-design choices, and shows that a shipping implementation — Kaspar-HFT — already hits the numbers the design rule requires. Any actor framework can adopt the same pattern.

### 9.1 What the framework has to provide
An actor framework fit for the design rule of §7 needs to hit h ≈ 90 ns on the hot path. Four primitives are load-bearing:
1. **Lock-free MPSC mailbox** — no per-actor lock on the hot path.
2. **Busy-poll receiver** — spin on the mailbox on a pinned isolated core, avoiding scheduler wake-up cost.
3. **Cache-line-aligned message payload** with padded-single-writer discipline to prevent false sharing between sender and receiver cores.
4. **Per-actor memory pool** — no general-heap allocation while the pipeline is hot.

Frameworks that omit any of these hit h in the 1–10 μs range (wait/notify condition-variables, JVM thread-pool dispatch, general-heap allocation on each send) and cannot support the design rule at N > 2.

### 9.2 Kaspar-HFT as a reference implementation
Kaspar-HFT is a shared-nothing C++20 actor framework that implements all four primitives above. Its `fast_send` mailbox achieves median hop cost h ≈ 90 ns busy-poll on isolated cores (Kaspar-HFT fast_send paper). Actors register under a `Manager`, groups of actors sharing a thread register as `Group`s, and messages flow one-way (`target->send(new Msg(), this)`) with no shared reads/writes. It is the reference implementation used to calibrate h throughout this paper, and every configuration in this section is available in the shipped framework.

### 9.3 Where the pipeline cuts naturally sit on a CME MDP3 handler
Four to five actors, one hop each:
1. **SocketReader** — receives UDP frames, no decode.
2. **MessageProcessor / SBE decoder** — parses MDP3 wire format into typed events.
3. **OB / TachBook** — applies MBP or MBO order-book updates.
4. **Strategy (light22 / Shadow-POV)** — reads book state, emits order intents.
5. **SOM (Simulated Order Manager) or iLink 3** — constructs and dispatches orders.

These are the "natural cut points" referred to throughout the paper's design section. N > 5 requires artificial decomposition (splitting an actor mid-work) and hits diminishing returns.

### 9.4 Applying the design equation to Kaspar-HFT
- Total decode floor T ≈ 7.23 μs (fast_send Table 6 calibration for NQ book).
- Per-stage hop cost h ≈ 90 ns (busy-poll async).
- Predicted median at N stages: p50 = T + (N−1)·h ≈ 7.23 μs + 0.09·(N−1) μs.
- Predicted tail spike under Hawkes: p99 spike shrinks as ~1/N.
- At the paper's N = 4 or 5 operating point: median overhead 270–360 ns on top of ~7 μs floor; tail compression per §7.

### 9.5 Configuration guide
- **Actor-to-core pinning:** every pipeline actor pinned to an isolated core under `isolcpus`. Neighbouring cores hold interrupt handlers, timers, and admin work.
- **Mailbox mode:** `fast_send` busy-poll for every hot-path hop. `wait/notify` acceptable for control-plane / logging actors only.
- **Mailbox layout:** cache-line-aligned message payloads, padded-single-writer between sender and receiver to avoid false sharing.
- **Memory pool:** per-actor object pool, no general-heap allocation on the hot path.
- **IRQ affinity:** NIC and timer IRQs routed off actor cores via `smp_affinity`.
- **Scheduling class:** `SCHED_FIFO` on actor threads.
- **Kernel-bypass NIC:** irrelevant for the actor-to-actor hop but recommended on the ingress path (Solarflare Onload / DPDK / VMA).

### 9.6 What the framework can and cannot do
- **Framework's job:** provide busy-poll async primitives, lock-free MPSC mailboxes, cache-line-aligned payload types, memory-pool APIs, actor-to-core pinning APIs. Kaspar-HFT does all of these.
- **Operator's job (outside framework code):** `isolcpus`, `smp_affinity`, `SCHED_FIFO`, huge pages, kernel-bypass NIC configuration. These are Linux configuration, not framework code.
- **Fundamentally OS/hardware:** scheduler wake-up latency, cache coherence protocol, TLB flush cost. The framework can't touch these; the operator can only steer around them.

### 9.7 Spinning receivers and cache pollution
A spinning receiver has zero wake-up cost but pollutes the L1/L2 caches on its host core and steals memory bandwidth. Kaspar-HFT lets the operator choose spin vs park per actor. The open question — how many stages can be spun before cache-pollution cost exceeds the tail-latency win — is future work and out of scope here. In practice, N ≤ 5 with pinned-core busy-poll runs comfortably.

### 9.8 A worked configuration for a POV strategy under this paper's design rule
Full Kaspar-HFT `.ini` snippet: `kaspr.ini` for a 5-actor pipeline with `fast_send` on the hot path, `isolcpus` layout, memory-pool sizes calibrated to peak burst mass. Small figure showing the pipeline DAG on the actual channels 310 (ES) / 318 (NQ) that Kaspar-HFT serves.

### 9.9 Falsification path for other frameworks
Not every framework will hit h ≈ 90 ns. Wait/notify async gives h ≈ 3.4 μs (fast_send paper); JVM thread-pool dispatch gives h in the 1–2 μs range. Under those hop costs the design equation flips at small N. A one-paragraph corollary: if your framework's measured h > T/N* where N* is your target stage count, don't pipeline — the median cost of a stage eats the entire T budget before the tail win kicks in.

## 10. Discussion

### 10.1 Reinterpretation of the actor-vs-monolithic debate
Thread-hop overhead is not a cost to minimise; it is a fixed budget that becomes free under clustered arrivals. Multi-actor pipelines are the correct architecture for HFT feeds specifically because those feeds are self-exciting.

### 10.2 Limitations
- Single instrument (NQ front-month, chan 318). ES and BTC results are follow-on.
- Tape uses Databento's colo capture; timing offsets from own capture are not measured here.
- Constant service is a simplification; the true decoder has state-dependent variability that we do not model.
- Cache-pollution cost of spinning receivers on many cores is real and only bounded, not measured, in this paper.

### 10.3 Implications for framework designers
- Prefer busy-poll async mailboxes over wait/notify (lower h).
- Prefer isolated-core pinning + IRQ-affinity for the OS-side cost floor.
- Set N to the natural decomposition boundaries of the decode pipeline (SBE decode, book application, strategy, order construction), which is 4–5 stages on CME.
- Beyond that, cache-pollution cost dominates.

### 10.4 Implications for strategy designers
- POV-style algorithms: use large N (tail is what matters for fill quality).
- Race-sensitive algorithms (adverse-selection avoidance, latency arbitrage): use large N (tail compression is the objective).

---

## 11. Future work

- Multi-instrument replication (ES, BTC).
- Cache-pollution empirical measurement.
- Power-law-kernel Hawkes fit vs exponential.
- Overnight-session regime.
- Own-capture pcap validation of the network sub-stage.

---

## 12. Reproducibility

- Code: `arrival_paper/qsim_grid.py` (per-window packet-Hawkes + constant-service Lindley), extended with N-stage tandem sweep and Poisson null.
- Data: 281 message tapes at `/vast/home/vmayeski/out/arrival_paper/tapes/318/message`.
- Companion Kaspar-HFT simulator with two-queue Lindley upgrade (`kaspr/`).

---

## Appendix A — Point-process background (existing)
Kept from previous draft. Kernel definitions, Fano, Hurst, MLE.

## Appendix B — Hop-cost measurement
Detailed methodology of the fast_send / Kaspar-HFT busy-poll measurement, cite fast_send paper.

## Appendix C — Poisson-null validity
Why uniform shuffle of a fixed arrival count is equivalent to a Poisson process conditional on that count. One page.

## Appendix D — Robustness
- h sensitivity table.
- N > 12 extension (does the design equation continue to hold at N = 20, 50?).
- Off-hour (overnight) sanity check.

---

## References

Grouped by category. Two fork-based literature searches (2026-09) confirmed no direct prior art on the paper's specific claim: tandem-vs-single-queue as a function of arrival law under Hawkes-clustered arrivals, empirically calibrated on real market-data feed with measured framework hop cost.

### Foundational tandem / series-queue theory
- Jackson, J. R. (1957). Networks of waiting lines. *Operations Research* 5, 518–521. — closed-form product-form for series/tandem under Poisson + exponential service.
- Burke, P. J. (1956). The output of a queuing system. *Operations Research* 4, 699–704. — output of M/M/1 in steady state is Poisson at same rate; makes M/M/1 tandems trivial.
- Kelly, F. P. (1979). *Reversibility and Stochastic Networks*. Wiley. — canonical tandem product-form.
- Kleinrock, L. (1976). *Queueing Systems, Vol. II: Computer Applications*. Wiley. — tandem/network chapters.
- Whitt, W. (1983). The queueing network analyzer. *Bell System Technical Journal* 62, 2779–2815. — GI/G/1 tandem approximations, standard practitioner formula.

### Tandem theory under bursty / non-Poisson arrivals
- Foss, S., Korshunov, D. (2000). Steady-state asymptotics for tandem, split-match and other feedforward queues with heavy-tailed service. *Queueing Systems* 35, 65–80.
- Baccelli, F., Foss, S., Lelarge, M. (2005). Tail asymptotics for monotone-separable networks. *Journal of Applied Probability* 42(3), 793–812; arXiv:math/0510117.
- Heindl, A. (2001). Decomposition of general tandem queueing networks with MMPP input. *Performance Evaluation* 44(1–4), 5–23.
- Kim, Y. H., Chydzinski, A. (2002). Connection-wise end-to-end performance analysis of queueing networks with MMPP inputs. *Performance Evaluation* 47(2–3), 137–156.
- Dębicki, K., Mandjes, M. (2015). *Queues and Lévy Fluctuation Theory*. Springer. — Lévy-input tandems.
- Konstantopoulos, T., Last, G., Lin, S.-J. (2015). Tandem fluid network with Lévy input in heavy traffic. arXiv:1512.06679.

### Hawkes single-queue and adjacent
- Hawkes, A. G. (1971). Spectra of some self-exciting and mutually exciting point processes. *Biometrika* 58, 83–90.
- Daw, A., Pender, J. (2018a). Queues driven by Hawkes processes. *Stochastic Systems* 8(3), 192–229.
- Daw, A., Pender, J. (2018b). The queue-Hawkes process: ephemeral self-excitement. WSC; arXiv 1811.04282.
- Koops, D. T., Boxma, O. J., Mandjes, M. R. H. (2018). Infinite-server queues with Hawkes arrivals. *Queueing Systems* 92, 27–52.
- Chen, X., Blanchet, J. (2021). Perfect sampling of Hawkes processes and queues with Hawkes arrivals. *Stochastic Systems* 11(3), 264–283; arXiv 2002.06369.
- Sheldon, D., et al. (2023). Steady-state analysis and online learning for queues with Hawkes arrivals. arXiv 2311.02577.
- Gao, X., Zhu, L. (2024). Single-server queues with state-dependent Hawkes arrivals. *Mathematics of Operations Research*.
- Li, B., et al. Hawkes arrivals with feedback. Rice CMOR working paper. — single-server, not tandem.
- Gao, X., Zhu, L., et al. (2024). Heavy-traffic limits for parallel single-server queues with randomly split Hawkes arrivals. — parallel-split, not tandem.
- (JASA 2025). Mutually exciting point processes with latency. — multivariate self-excitation, not tandem.
- Koops, D. T., Boxma, O. J., Mandjes, M. R. H. (2016). Functional CLT for stationary Hawkes → ∞-server queues. arXiv 1607.06624.

### Empirical / applied
- Aquilina, M., Budish, E., O'Neill, P. (2022). Quantifying the high-frequency trading "arms race". *Quarterly Journal of Economics* 137(1), 493–564. — race counts, not tandem sim.
- Byrd, D., et al. (2020). ABIDES: Towards high-fidelity market simulation for AI research. — configurable per-agent latency, no Hawkes-driven tandem.
- Amrouni, S., et al. (2021). ABIDES-Gym / ABIDES-MARL (2024). — RL LOB sims, no packet-Hawkes tandem sweep.
- Frey, S., et al. (2023). JAX-LOB. arXiv 2308.13289. — GPU LOB sim, no Hawkes tandem.
- Deep Hawkes for HFT market making (Springer 2024). — Hawkes as signal, not queueing architecture.
- Dean, J., Barroso, L. A. (2013). The tail at scale. *CACM* 56(2), 74–80. — RPC pipeline tail composition, no Hawkes.
- Zhao, K., et al. (2023). Parsimon: scalable tail latency estimation for data center networks. NSDI 2023. — per-link tail sim, no Hawkes.
- (Datacenter tail-latency crowd: Alizadeh, Kalia, Kapoor, Kaminsky, Andersen — RPC tail composition, no Hawkes.)

### Reference implementation and calibration
- Mayeski, V. Kaspar-HFT: a shared-nothing actor framework for CME futures. — reference implementation.
- Mayeski, V. fast_send: measured busy-poll median hop cost h ≈ 90 ns; wait/notify comparator ≈ 3.4 μs. — h calibration.
- Bacry, E., Mastromatteo, I., Muzy, J.-F. (2015). Hawkes processes in finance. *Market Microstructure and Liquidity* 1(1). — Hawkes in market microstructure review.
- Filimonov, V., Sornette, D. (2012). Quantifying reflexivity in financial markets: Toward a prediction of flash crashes. — near-critical branching ratio on ES.
- Hardiman, S. J., Bercot, N., Bouchaud, J.-P. (2013). Critical reflexivity in financial markets: a Hawkes process analysis. arXiv 1302.1405. — power-law kernel, n ≈ 1.
- Ozaki, T. (1979). Maximum likelihood estimation of Hawkes' self-exciting point processes. *Ann. Inst. Statist. Math.* 31(1), 145–155. — MLE for Hawkes.
- Konishi, K., Makimoto, N. (2001). Optimal slice of a VWAP trade. *Rev. Deriv. Res.* 4, 133–146. — POV / VWAP execution.

### Verdict
Two independent fork searches (spawned 2026-09-19) confirmed no direct prior art on either (i) the theoretical claim that tandem-versus-single-queue architecture recommendation flips sign between Hawkes and Poisson arrivals or (ii) an empirical CME-calibrated tandem sweep against a Poisson null with measured HFT-framework hop cost. Novel on both axes.
