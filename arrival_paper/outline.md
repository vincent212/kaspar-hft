# Modelling Real-World Latency Tails in a CME Futures Order-Book Simulator

**Paper outline — v@m2te.ch — scope frozen 2026-09-18, reframed 2026-09-18**

## SCOPE (frozen — do not expand)

This is a paper on **order-book simulator methodology**, not a descriptive paper on tails.

**Thesis.** Treating execution latency as constant in an LOB simulator materially misprices market-making and passive-execution algorithms. On CME MDP3 message data we (a) measure that the four other tails (matching-engine latency, send-to-handler latency, maker adverse selection, return) all move together and load on the same Hawkes arrival intensity + branching ratio; (b) upgrade the Kaspar simulator's latency model from constant delay to a two-queue system (an exchange-side queue absorbing matching-engine bursts + a local system queue with deterministic service driven by the same arrival stream); (c) re-run the shadow POV execution algorithm under constant-delay and tail-aware latency and quantify the P&L / adverse-selection / fill-rate delta as a **correction to prior shadow-POV numbers**.

**Corpus for measurement.** **CME NQ front-month only** (chan 318). 2025-01-01 → 2026-02-27, up to ~281 sessions (as many as complete under a ≤24 h compute budget), ~10 30-min windows/session ≈ ~2800 windows. ES and BTC deferred to cross-product follow-on.

**Simulator upgrade.** Kaspar's current simulator uses a constant per-message delay. New model:
- **Exchange-side queue** — matching-engine time draws from the empirical `sendingTime − transactTime` distribution conditioned on the current window's Hawkes state.
- **System-side queue** — deterministic service of X μs (default 7 μs) fed by real handlerendtim arrivals, giving the modelled p50/p99 receive latency the fifth tail already reports.
- Compose the two: total order-round-trip = exchange-side wait + system-side service.
- Baseline for comparison: current constant-delay setup.

**Shadow re-run.** Same shadow POV configuration as prior article, run twice on the same tape:
- Baseline: constant delay (previous article's number).
- Corrected: tail-aware two-queue delay.
- Report P&L, adverse-selection cost per fill, fill rate.

**What the fat-tail measurement contributes.** It supplies the driving distributions the simulator needs — not a standalone claim. The five tails still get correlations vs (n, λ̄) reported (grid heatmaps and 22–30 Spearman cells) because the correlation structure is what motivates the two-queue design: latency and adverse selection co-move under the same Hawkes driver, so a simulator that fixes latency without repricing adverse selection would be internally inconsistent.

**Working title.** "Modelling Real-World Latency Tails in a CME Futures Order-Book Simulator — and What They Do to a Shadow POV Execution Algorithm"

**Claim.** On this ~4000-window NQ panel, **five tails move together** and all five track the same driver. The tail set:

- **Matching-engine tail** — p99 of `sendingTime − transactTime` (how long CME's matching engine took to publish an event through its gateway)
- **Send-to-handler tail** — p99 of `handlerendtim − sendingTime` (databento wire → handler-end; the software-side decode / dispatch time under the same message-arrival load)
- **Modelled receive-queue latency tail — the HFT-system centrepiece** — for the sequence of message arrivals at `sendingTime` we simulate a single-server G/D/1 queue with a fixed **1 μs receive overhead** and deterministic **service time swept from 1 μs to 20 μs**; report p50 and p99 of per-message system time (wait + service) as a function of service time. On the operating point of the deployed decoder (default 7 μs), track the per-window p99 as `log_p99_qlen`. This is the tail an HFT system architect actually cares about — it says "for a decoder budgeted at X μs per message, this is the p99 wire-to-book latency your resting book will see under real CME NQ arrival bursts."
- **Maker-adverse-selection tail** — p95 of |markout| on real historical passive fills
- **Return fat-tail** — Hill / Fréchet tail exponent 1/ν on window mid-quote returns

All five correlate — marginally and after partialling — with the two Hawkes summaries fitted per window: **λ̄ (mean intensity)** and **n = α/β (branching ratio)**. High λ̄ and high n → simultaneously fat tails in all five domains.

**Correlations reported.** 10 pairwise cross-tail Spearman correlations on the NQ panel + 20 marginal + partial arrival-side attribution cells (5 tails × 4 correlations {ρ(n), ρ(λ̄), ρ(n|λ̄), ρ(λ̄|n)}) = **30 correlations total**.

The three arrival-side latency-stage tails (matching-engine, send-to-handler, modelled receive-queue) are separate physical mechanisms sharing the same *arrival-side driver* — a stronger empirical statement than "one end-to-end latency tail correlates," because the effect reaches into every queue in the chain, one after another. In particular the modelled receive-queue tail is a design-time knob: system-architecture readers get the p99-vs-service-time curve for their own decoder budget, computed on real NQ traffic (not synthetic Poisson).

**First look at the modelled-queue tail** (NQH5, 2025-01-02, 17.3 M arrivals, receive = 1 μs):

| service (μs) | p50 sys time (μs) | p99 sys time (μs) |
|---|---|---|
| 1 | 1.0 | 5.0 |
| 5 | 5.0 | 30.0 |
| 7 | 7.0 | 42.0 |
| 10 | 10.0 | 70.0 |
| 15 | 15.0 | 130.6 |
| 20 | 20.0 | 201.5 |

p50 tracks the service floor 1:1 (the median arrival finds the queue empty), but p99 grows super-linearly with service budget — at 20 μs the burstiest 1% of arrivals see 10× the service time by the time they clear. Chart: `arrival_paper/figs/qlen_service_sweep_20250102.png`.

Note on what the tape can't split. The .bin data schema reserves a `recv_time` field (pcap kernel timestamp) that would let us physically split the send-to-handler stage into network-transit vs software-decoder. In the current databento .bin conversion pipeline `recv_time` is zero-initialized (no pcap timestamps are propagated), so a *direct* two-way physical split of that stage is not computable from this tape. The modelled-queue tail is exactly the substitute: instead of measuring one specific decoder, we simulate the queue behaviour parametrically so the reader can pick their own service-time operating point.

**Novelty (lit-search verified 2026-09).** The pairwise legs are separately published (return × Hawkes: Hardiman-Bercot-Bouchaud 2013, Filimonov-Sornette 2012, Wehrli-Wheatley-Sornette 2021; adverse-selection × Hawkes: Cartea-Jaimungal-Ricci 2014, Rambaldi-Bacry-Lillo 2017; end-to-end HFT latency at panel scale: essentially unpublished for CME futures; separating matching-engine latency from downstream decode latency inside one Hawkes attribution framework: unpublished). What is NOT published anywhere the two-agent lit search could find is the **joint five-way panel** with cross-tail correlations + joint Hawkes attribution + a **modelled-queue-latency curve on real CME arrivals** that HFT system architects can use to size their own decoder — computed on ~416 NQ sessions of raw MDP3, not synthetic Poisson.

**Deployment.** Shipping the online O(1) intensity estimator (`arrival_paper.online.HawkesEstimator`, MIT) that a market-making system can gate on in real time. Backtest: Shadow POV λ̂-gating on the paper's fill_tape.

**What this paper does NOT do — deferred to follow-on papers.**

- **No latent-factor model.** No one-factor SEM, no MAL-extended Hawkes. The "why do the tails share this arrival-side signature" question is a follow-on (MAL paper).
- **No signed λ.** Only total λ̂ = λ̂⁺ + λ̂⁻. The signed variant λ̂⁺ − λ̂⁻ as a directional alpha is its own follow-on (signed-Hawkes-vs-CKS-OFI paper).
- **No cross-product replication.** NQ only. ES and BTC are deferred to a cross-product follow-on paper — same methodology, different streams. The 3-product replication was Tier-2 novelty (N-D) and its absence weakens the "not-an-NQ-artifact" claim slightly; the follow-on closes that gap.
- **No cross-market or cross-asset excitation.** Fit one Hawkes per session independently. NQ↔MNQ, ES↔MES, NQ↔ES cross-excitation is another follow-on.

**Filters applied.**
- Drop bottom 10% of windows by event count (low-intensity gate; n unreliable there, tails tiny anyway).
- Drop volstats-vs-DB roll-day mismatches ± 1 day around each (contract-roll churn contaminates both n and λ̄).
- 2025-01-01 → 2026-02-27 corpus (2024 skipped for now; BTC deferred if master_universe.326 doesn't materialise).

---

**Working title:** "Long Latency Tails in HFT Systems Have the Same Signature as Fat Return Tails and Adverse-Selection Tails — Evidence from CME MDP3 NQ Futures, with Implications for Market-Making Systems"

**Alternative title candidates** (pick one on final draft):
- "Long Latency Tails in HFT Systems Have the Same Signature as Fat Return Tails and Adverse-Selection Tails: Implications for Market-Making Systems" (current — punchy, honest about "signature" not "cause", flags the deployment angle)
- "Three Correlated Tails in HFT: Latency, Returns, and Adverse Selection Track the Message-Arrival Hawkes, and What That Means for Market Makers"
- "The Common Arrival-Process Signature Behind Latency, Return, and Adverse-Selection Tails in CME Futures — with Implications for Market Making"

"Same cause" would be the MAL claim we deferred; "same signature" states the empirical fact this paper actually delivers. The "implications for market making" clause promises the paper's deployment story: the online λ̂ estimator + Shadow POV gating result.

**The pitch (verbatim to appear in the abstract):**

> *Five tails from a modern HFT system — three physical latency stages (matching-engine → gateway, gateway → pcap capture, decoder wire-to-book), plus maker-adverse-selection markouts and the return fat-tail — move together, and all five track the same driver: how bursty and how near-critical the CME MDP3 message-arrival process is on the window under measurement. Prior work has separately linked the fat-tailed return distribution to Hawkes self-excitation (Hardiman-Bercot-Bouchaud 2013; Filimonov-Sornette 2012; Wehrli-Wheatley-Sornette 2021), and separately linked adverse-selection cost to mutually-exciting order flow in stochastic-control models (Cartea-Jaimungal-Ricci 2014) and event-clustering estimates (Rambaldi-Bacry-Lillo 2017); we extend that chain to three engineering-side tails — decomposed via the three-timestamp anatomy (`transactTime`, `sendingTime`, `recv_time`) that public MDP3 raw pcaps make available — and show that on a per-session panel of the NQ E-mini front month all five tail metrics co-move and load on the same Hawkes-arrival factors $(\bar\lambda, n)$, which no prior empirical paper reports jointly. We fit exponential Hawkes per 30-min window on ~416 NQ sessions (2025-01 → 2026-02, ~4000 windows) and report: (i) 10 pairwise cross-tail Spearman correlations; (ii) 20 marginal + partial correlations of each tail against $(n, \bar\lambda)$ (5 tails × 4 correlations); (iii) an online O(1) intensity estimator (`arrival_paper.online.HawkesEstimator`, MIT) that a market-making system can gate on in real time. Cross-product replication on ES and BTC, and latent-factor unification (MAL), are deferred to follow-on papers.*

**Central thesis (extended):** the near-critical Hawkes clustering that fattens the packet-decoder latency tail, produces the toxic maker fills (§7), and generates the fat-tailed return distribution (§8) is one arrival-process phenomenon showing up in three domains at once. This paper's job is to demonstrate that the three tails **move together across the ~5000-window panel** and that each tracks the two Hawkes summaries $(n, \bar\lambda)$ — the empirical proof of shared causation without needing a latent-factor model. Concretely:

(a) **Cross-tail co-movement** — pairwise Spearman across (latency tail, adverse-P&L tail, return tail): three correlations per stream × 3 streams = 9 correlations. This is the "they move together" evidence.

(b) **Arrival-side attribution** — for each of the three tails, marginal + partial Spearman against $(n, \bar\lambda)$: 6 correlations per stream × 3 streams = 18. This is the "the driver is the arrival process" evidence.

(c) **Working artifact** — the online O(1) intensity estimator + a Shadow POV λ̂-gating backtest that a market-making system can deploy.

We deliberately do **not** fit a one-factor SEM or a latent-factor Hawkes extension — those belong to a follow-up (see Future Work: MAL). The empirical evidence that the three tails move together *and* share the same arrival-side correlates is the paper's contribution; whether one names the shared cause a latent variable is theoretical decoration on top of that empirical result.

**Scope decisions (locked with author):**

1. **Marked Hawkes** — condition α on packet size / order size / trade size. One extra parameter, better fit.
2. **Both anchors for markouts** — compute markouts anchored on (a) trades and (b) all book-update events. Trades give clean signs and fewer observations; book events give ~100× the sample.
3. **Standalone paper.** Self-contained empirical result on the arrival-process signature that ties three tails together; not a sequel to any prior work.
4. **Corpus: 731 trading days**, 2023-01 to 2026-02, three streams — ES (chan 310), NQ (chan 318), and **BTC (chan 326, CME Crypto Futures)**. ES and NQ are already decoded to `/vast/home/vmayeski/out/bin/{310,318}/` (1.3 TB, .ok markers). BTC PCAPs live at `/vast/vendor/databento/pcaps/glbx/futures-xcme/YYYYMMDD/` on `224.0.33.240:14326` (legacy IP, ~158 files/day × 313 days) and still need `.bin` conversion via `dbento_pcap_to_bin --chan 326`; task #78 is the pending pipeline step. BTC is added as a *third stream* — same methodology across three products.
5. **Message-level arrival process, not packet-level.** The natural unit for both info content and latency work is the individual SBE *message* (book add/modify/cancel/execute or trade). This paper characterises messages *within* packets and treats packet-span as a downstream summary. A packet with 45 messages is 45 arrivals of information — the message-level view captures clustering the packet-level view flattens.
6. **Three timestamps per message.** The .bin schema (`r_l3.hpp`) carries all three: `transactTime` (matching-engine time), `sendingTime` (CME gateway packet header), and `recv_time` (pcap kernel timestamp). Their differences are treated as first-class objects in the paper.
7. **Adverse selection is measured on real historical passive fills, not a simulated quoter.** MBO L3 carries every order lifecycle: add → modify → cancel → execute. Every trade in the tape carries the maker order_id, and we can trace back to that order's Add event to recover submission time, submission price, side, and time-in-queue. Compute markouts after each *real* maker fill. This is stronger than any simulator: no fill model, no queue assumption, no synthetic quote placement — the sample is every passive fill that actually happened in ES and NQ across 731 days. Mid markouts on all trades stay as a wider robustness comparison.
8. **Regime splits are first-class in every result.**
   - **Open (09:30–09:45 ET)** and **Close (15:45–16:00 ET)** — vol-scalper windows, disproportionate share of daily activity and toxicity
   - **FOMC days separately** — split those 20+ days from the corpus and report their own panel
   - **Roll-neighbor days** and **CPI/NFP days** as further robustness slices

---

## What's genuinely new

### Novelty stack — three tiers

The paper has three tiers of contributions. Be honest about which is which up front.

#### TIER 1 — GENUINELY NOVEL (the paper's core contributions)

**★ N-A. Adverse selection attribution to arrival intensity, at per-fill resolution, on public MBO L3 across years.**

This is the paper's central novel result. To our knowledge, no prior work has published:

- Real-time Hawkes intensity λ̂ correlated with **per-fill maker adverse P&L** (not aggregate trader-level, not VPIN aggregate)
- On **public** MDP3 order-level data (not privileged CFTC/CME account-level like Kirilenko / Baron / Bellia)
- At **corpus scale** (731 days × 3 streams — Kirilenko-style work is episodic, Bellia-style is thousands of fills, we have hundreds of millions)
- With the **attribution regression** framing: ΔR² of an arrival-augmented model over an arrival-blind order-book-state baseline is the money number and it hasn't been published anywhere I can find

The **individual pieces** exist in prior art:
- Per-order MBO tracking of makers → markouts: Kirilenko (with account attribution); Aligrithm (blog scale)
- Hawkes on CME arrivals: Filimonov, Hardiman-Bouchaud
- Coarse aggregate flow toxicity ↔ HFT: VPIN, Bellia

The **combination** — real-time online λ̂ × per-fill maker adverse P&L on public order-level MBO across years — is what's new. If the paper stands or falls on one claim it stands or falls on N-A.

**★ N-B. Three-timestamp anatomy of CME MDP3 (transact / send / recv).**

Every prior Hawkes-on-CME paper uses one timestamp (either exchange transact or their local capture, depending on data source). None decomposes into `t_match` (matching engine) / `t_send` (gateway header) / `t_recv` (pcap kernel). The `t_send − t_match` distribution — CME's exchange-gateway queueing signature — is, as far as I can find, unpublished. Independent of the trading result this is a first-class empirical contribution.

**★ N-C. Message-level (not packet-level) arrival characterization AND its side-by-side comparison with packet-level.**

Academic literature is all at message/event level (they buy Databento or TAQ book data; no raw packets). Any prior packet-level HFT work is single-day and doesn't put the two side-by-side. Presenting both on the same corpus, showing the packet-level view understates message-level clustering by the mean-span factor, is a specific empirical angle available only to authors with raw pcap access.

#### TIER 2 — REPLICATION AT NEW SCOPE (worth stating, not the headline)

**N-D. Cross-product replication: ES + NQ + BTC on same methodology.**
Literature over-samples ES. NQ Hawkes fits are scarce. CME BTC futures Hawkes fits are essentially absent — crypto Hawkes lit is on spot venues (Coinbase/Binance), not CME MDP3. Matched-methodology results on all three across 731 days.

**N-E. Multi-year stability across 731 days per stream.**
Filimonov did 12 years but ES-only, mid-price only. Achab EUREX single-year. Bellia specific-episode datasets. Nobody has published per-day Hawkes parameter distributions at this cross-day granularity.

**N-F. Regime splits at corpus scale (open / close / FOMC vs matched controls).**
Kirilenko is episodic (2010 flash crash). Filimonov detected precursors at 10-min windows. Nobody publishes Hawkes params × per-fill markouts distributions across ~20 FOMC 14:00–14:30 windows vs matched controls.

**N-G. Five-way co-movement + shared arrival-side attribution (verified novel 2026-09).**

The pairwise legs are published to very different depths:
- **Return-tail × Hawkes** — Hardiman, Bercot & Bouchaud (2013, *Eur. Phys. J. B*, arXiv:1302.1405); Filimonov & Sornette (2012, *Phys. Rev. E*, arXiv:1201.3572); Wehrli, Wheatley & Sornette (2021, *Quant. Finance*).
- **Adverse-selection × Hawkes** — Cartea, Jaimungal & Ricci (2014, *SIAM J. Financial Math.*, SSRN 3306158) as a stochastic-control model; Rambaldi, Bacry & Lillo (2017, *Quant. Finance*, arXiv:1602.07663) as an event-clustering estimate; Kirilenko-Kyle-Samadi-Tuzun (2017, *J. Finance*) and Easley-López de Prado-O'Hara VPIN as single-episode empirical anchors.
- **Three latency subtails × Hawkes** — essentially absent. Theoretical strand (Daw & Pender 2018 *Stochastic Systems*, Koops et al. Hawkes/G/1 queues) has no market-data component. Empirical HFT-latency literature (Aquilina, Budish & O'Neill 2022, *QJE*) measures **race-margin** latency as a market-quality quantity, not per-stage decomposition of exchange-side + wire + decoder queueing times. **The three-timestamp decomposition itself is our N-B contribution.**

**What no prior paper reports** (targeted lit-search, 2026-09, arxiv + Semantic Scholar + SSRN + Google Scholar):
- All five tail metrics on the *same* panel of many sessions,
- Latency decomposed into three physical stages via matching-engine / gateway / pcap timestamps,
- With pairwise cross-tail correlations reported (10 pairs),
- Plus joint marginal + partial correlations against Hawkes `(n, λ̄)`.

**Why decomposing the latency chain matters for the paper's story.** A single end-to-end latency tail could be dismissed as artifact of one specific queue. Showing that **three separate physical stages** (CME matching engine → CME gateway; CME gateway → pcap capture; pcap capture → our decoder) all show the same tail-vs-arrival-intensity relationship makes the effect much harder to attribute to a single infrastructural quirk. **The arrival-process signature reaches into every queue in the chain, one after another.**

**Blunt readable framing.** When the arrival process spikes, *every* queue in the ecosystem congests at the same time — the exchange's matching engine, the exchange's egress gateway, the network wire, every recipient's decoder, and (indirectly, through those bottlenecks) every passive quote resting in every book. Nobody in the chain is exempt from the same driver; nobody's queue clears while another's fills. **Every HFT is flying blind at the same time.** That is the paper's one-line takeaway, and it is why arrival-side monitoring — the online O(1) intensity estimator we ship — is a system-wide risk signal, not just a decoder-buffer sizing tool.

This paper's N-G contribution is exactly that five-way joint. Modest but empirically real. Whether the co-movement reflects a single hidden driver is a follow-up (see Future Work: MAL) and not claimed here.

**Pre-emptive review positioning (two cautions from the lit search).** The paper's related-work section will explicitly:
1. Cite Hardiman-Bercot-Bouchaud (2013) and Filimonov-Sornette (2012) *first* when introducing the return-tail leg — they establish that the panel-scale link exists and pre-empt "this has been done" reviewer objections on that leg. Our extension is to the *other two* tails on the same panel, not to the return-tail leg itself.
2. Cite Aquilina-Budish-O'Neill (2022) for panel-scale latency-related empirical work, then explicitly draw the distinction: theirs measures market-quality latency-arbitrage (a public-good quantity, sub-microsecond race-margin quantile); ours measures **decoder wire-to-book decode time** (a per-firm engineering quantity, 99th percentile of per-message serialisation + book-update time). Different measurement, different unit, different use.

#### TIER 3 — DELIVERABLES (not novel research, but useful practitioner output)

**N-H. Online O(1) intensity estimator packaged as public Python module** (`kaspar_arrival`, MIT). No widely-adopted open module exists as of our search.

**N-I. Reproducible pipeline from raw MDP3 PCAP to results.** Academic papers rarely publish the parser; we do.

**N-J. Latency prediction model** — `λ̂(t) → predicted span → predicted latency percentile`. Useful engineering artifact tied to N-C.

**N-K. Marked / 2-D Hawkes MLE codes** — standard tech but reproducible in the module.

### Where the paper is NOT novel — be up-front

- Rejecting Poisson on CME arrivals: **done many times** (Filimonov, Hardiman, Bacry).
- Multivariate Hawkes on LOB with bid/ask cross-excitation: **done** (Achab et al. on EUREX, 2018).
- Maker-side markouts from order-level data with account attribution: **done** (Kirilenko, Baron).
- QHawkes → fat-tailed returns: **done and textbook** (Blanc-Donier-Bouchaud 2017, Bacry-Muzy 2015).
- Mid markouts on all trades: **30 years of microstructure literature**.
- "Fast fills are bad fills" intuition: **practitioner folklore** (Aligrithm blog etc.).

### The pitch line

> This paper's headline contribution is **N-A**: to attribute per-fill adverse selection to the message arrival process, at scale, on public MDP3. Everything else — the multi-product corpus (N-D), the multi-year stability (N-E), the regime splits (N-F), the three-way marginal-correlation panel (N-G) — supports N-A. **N-B** (three-timestamp anatomy) and **N-C** (message vs packet) ship regardless of whether N-A holds. The public Python module (N-H) makes N-A reproducible outside our stack. The one-factor / latent-driver unification is deferred to a follow-up (see Future Work: MAL).

---

## Paper outline

### Abstract

Limit-order-book simulators treat per-message latency as a constant scalar or an i.i.d. draw from a supplied distribution, with no coupling to the arrival process. This paper shows that assumption is materially wrong for CME NQ and provides a minimal correction. We measure five tails on ~281 sessions of NQ front-month MDP3 (matching-engine latency, send-to-handler latency, modelled queue latency, adverse-selection markout, return proxy), fit exponential Hawkes per 30-min window, and report their behaviour across the (λ̄, n) plane on a 5×5 equal-quantile grid. We then upgrade the Kaspar simulator's constant per-message delay to a two-queue G/D/1 model — one system-side queue with the operator's own service time, one exchange-side queue with a service time estimated from public MDP3 by taking a low quantile of `sendingTime − transactTime`. The recursion is a one-line change to Kaspar's existing `pub_q` and `del_q` release rule, guarded by `#ifdef OB_TAIL_DELAY` so the constant path stays bit-identical. Heavy-tailed system latency emerges naturally: the Hawkes clustering lives in the tape's arrival timestamps, and a G/D/1 with the right service time reads it off for free. We A/B a shadow POV execution algorithm on the same tape under {constant delay, tail-aware delay} and quantify the P&L, fill-rate, and adverse-selection delta as a correction to prior shadow-POV results. Companion open-source repo ships the msgtape parser, per-window panel builder, calibration script for exchange service time, and the Kaspar OB upgrade.

### 1. Introduction

LOB simulators are the standard tool for evaluating market-making and execution algorithms in academic and industrial research: ABIDES (Byrd et al. 2020), ABIDES-Markets, MarketSim (Wellman group), JAX-LOB (Frey et al. 2023), CoinTossX (Jericevich et al. 2022), Kaspar. Every one of them treats latency as a constant scalar or an i.i.d. draw from an exogenous distribution, decoupled from the arrival process. On real CME MDP3 data this assumption is wrong — arrivals are Hawkes-clustered, and the same clustering that produces fat return tails and adverse-selection tails also produces heavy-tailed system latency through any downstream queue.

**LOB simulator landscape.**

| Simulator | Substrate | Latency model | Book type | Tape replay | Agents | Reproducibility |
|---|---|---|---|---|---|---|
| ABIDES (Byrd 2020) | ns discrete-event, Python | Per-link scalar or i.i.d. distribution | MBP synth | No | ZI + MM + HFT | Seeded |
| ABIDES-Markets | ABIDES + ARL | Same as ABIDES | Same | No | RL + strategic | Seeded |
| MarketSim (Wellman) | Strategic ABM, Java | Fixed cross-venue delay | MBP synth | No | Strategic MM / arb | Deterministic |
| JAX-LOB (Frey 2023) | GPU parallel, JAX | None | MBP | Yes | None | Deterministic |
| CoinTossX (Jericevich 2022) | Real matching engine, Go | Real network | MBP | Yes | External clients | Not deterministic |
| LOBSTER | Reconstruction only | N/A | MBO | Yes (playback) | None | Deterministic |
| Kaspar (this paper) | Actor framework, C++20 | Constant scalar (baseline); **G/D/1 (new)** | MBP or MBO | Yes (MDP3 .bin) | Shadow POV | Deterministic (market time) |

None of the seven has a queue-based latency model driven by the arrival stream. That is the gap this paper closes.

**Contributions.**

1. **Measurement.** ~281 sessions of NQ front-month MDP3 (~2800 30-min windows), per-window Hawkes fit + five tail metrics + paired absolute-and-relative 5×5 heatmaps on the (λ̄, n) plane.
2. **A minimal simulator upgrade.** One line of arithmetic in Kaspar's OB release rule flips the model from constant-delay to two-queue G/D/1. Two scalar knobs: `service_us_inbound` (operator profiles their own decoder), `service_us_outbound` (estimated from public MDP3). Regression-safe under `#ifdef`.
3. **An exchange service-time recipe.** A practitioner running any LOB simulator can take public MDP3, compute `sendingTime − transactTime` per message, and use a low quantile of that distribution as the exchange service time. That's the paper's core deliverable to the broader simulator user community — they already have their own inbound service time from profiling their own code.
4. **Shadow POV A/B.** Same tape, same algo, two latency models. Reports P&L / fill-rate / adverse-selection delta as a correction to prior shadow-POV results.

**Why the tails come out for free.** Section 3 walks through the intuition: given a G/D/1 with any scalar service time, quiet arrivals emerge with system_time ≈ service_us and bursty arrivals inherit the residual service time of the message ahead. Cluster N messages inside one service_us and the Nth pays ~N × service_us. The tail *is* the cluster-size distribution, and cluster sizes come straight from the tape's Hawkes-structured arrivals — no fitted latency distribution needed.

**Positioning.** This is the empirical/engineering realization of Daw & Pender's (2018) queue-Hawkes theory inside a real trading simulator, validated by a controlled A/B on a shadow POV algorithm calibrated on years of CME NQ MDP3 data.

### 1.5 Prior art and what this paper actually adds

This paper sits at the intersection of four literatures: (i) LOB simulators for HFT algorithm evaluation, (ii) exchange-round-trip latency measurement, (iii) queue-Hawkes theory, and (iv) latency-aware market-making backtests. The prior-art survey below was run 2026-09-18 across arXiv, SSRN, Google Scholar, Semantic Scholar, and the ACM DL.

**A. LOB / market-microstructure simulators — how they treat latency**

The dominant published simulators treat per-message latency as **constant or i.i.d. from a supplied distribution, with no state coupling to arrival intensity**:

- **Byrd, Hybinette, Balch (2020)** *ABIDES: Towards High-Fidelity Market Simulation for AI Research* (ACM SIGSIM-PADS; arXiv 1904.12066). Discrete-event nanosecond simulator modelled on NASDAQ ITCH/OUCH. Per-link agent↔exchange latencies are pairwise scalars — constant or drawn from a supplied distribution. **Directly the class of simulator this paper upgrades.**
- **Vyetrenko, Byrd, Petosa, Mahfouz, Dervovic, Veloso, Balch (2020)** *Get Real: Realism Metrics for Robust LOB Market Simulations* (ICAIF). Stylized-fact realism agenda for ABIDES-style sims — latency handled implicitly via inter-arrival stylized facts, not modelled per message.
- **Wah & Wellman (2016)** *Latency arbitrage in fragmented markets* (Algorithmic Finance 5). Two-venue ABM; latency is a fixed cross-venue delay used only to define an arbitrage window.
- **Wang, Hoang, Vorobeychik, Wellman (2021 / MarketSim @ ICAIF 2024)** — strategic ABM; latency not modelled per message.
- **Jericevich, Chang, Gebbie (2022)** *CoinTossX* (SoftwareX). Production-grade research matching engine; measures latency but does not model client-side stochastic delay.
- **Cliff (2019)** *A Cloud-Native Globally Distributed Financial Exchange Simulator* (arXiv 1909.12926). Geographically-varying per-link latency; still deterministic per link.
- **Frey et al. (2023)** *JAX-LOB* (ICAIF) and **Shi et al. (2024)** *TRADES* — GPU / neural-generative sims; latency ignored.
- **arXiv 2510.08085 (2025)** *A Deterministic LOB Simulator with Hawkes-Driven Order Flow* — Hawkes on the *order flow*, **not** on the *latency*. Closest hit on Hawkes-inside-a-sim; the latency angle is unclaimed.
- **Rosenbaum & Souilmi (2026)** *Bridging the Reality Gap in LOB Simulation* (arXiv 2603.24137). Discusses exchange round-trip latency as an emergent inter-event clustering scale but does not model the stack.
- **Oxford-Man (2020)** *Fast Agent-Based Simulation of Pro-Rata LOBs with Study of Latency Effects*. Constant per-agent latency knob; sweeps its value, does not model its distribution.

**B. Decomposition of exchange round-trip latency inside a simulator**

Very thin literature. The main exchange-side latency measurement paper is:

- **Aquilina, Budish, O'Neill (2022)** *Quantifying the HFT "Arms Race"* (QJE 137(1)). Measures the *race margin* (public microsecond gap between a marketable message and the trailing losers) from LSE INET logs. This is an exchange-side quantity — public race margin, not a per-firm engineering decomposition, and it is not embedded in a simulator.
- **Stoikov & co-authors (2020)** *The Importance of Low Latency to Order Book Imbalance Strategies* (arXiv 2006.08682). Empirical latency-sensitivity study, no decomposition.

**No prior work found** for a peer-reviewed simulator that separately parameterises matching-engine service time, network transit, and decoder time from MDP3-class data.

**C. Queue-Hawkes theory — Hawkes arrivals feeding a service queue**

The theoretical machinery exists but has not been coupled to trading-system execution latency:

- **Daw & Pender (2018)** *Queues Driven by Hawkes Processes* (Stochastic Systems), and *The Queue-Hawkes Process: Ephemeral Self-Excitement* (arXiv 1811.04282 / WSC 2018). Canonical result: heavy-tailed queue-length under heavy-tailed intensity jumps. **The theoretical foundation this paper builds on.**
- **Koops et al. (2018/2022)** *Infinite-server queues with Hawkes arrivals* (Queueing Systems). Analytical, non-financial.
- **Gao & Zhu (2024)** *Single-Server Queues with State-Dependent Hawkes Arrivals* (Math. of OR) and *Steady-State Analysis and Online Learning for Queues with Hawkes Arrivals* (arXiv 2311.02577). Theoretical extensions, still non-financial.

The financial-Hawkes literature works on order-flow arrivals, not on service queues producing execution delay:

- **Rambaldi, Bacry, Lillo (2017)** [arXiv 1602.07663] — Hawkes on order-book events; no queue model of latency.
- **Bacry, Mastromatteo, Muzy (2015)** *Hawkes Processes in Finance* (Market Microstructure and Liquidity). Foundational review of event arrivals, not service queues.
- **Cartea, Jaimungal, Ricci (2014)** *Buy Low Sell High* (SIAM J. Fin. Math. 5). Multivariate mutually-exciting order arrivals in market-making, no exogenous latency queue.
- **Bacry, Gaïffas, Muzy (2015) queue-reactive Hawkes**, and *State-Dependent Hawkes for LOB Modelling* (arXiv 1809.08060). Hawkes coupled to book **state**, not to a service queue producing message-processing delay.

**Nothing found** that couples a Hawkes cluster arrival stream to a *service queue whose backlog is the execution-side latency*. That is the specific theoretical gap this paper's two-queue simulator fills.

**D. Latency-model swap on a market-making backtest — P&L / adverse-selection / fill-rate delta**

The closest prior work reports latency-sensitivity, but not a controlled simulator-model swap:

- **Cartea & Sánchez-Betancourt (2021)** *The Shadow Price of Latency: Improving Intraday Fill Ratios in FX* (SIAM J. Fin. Math.; SSRN 3190961). Empirical FX study of how latency degrades fill ratios; derives what a taker would pay to reduce latency. Closest existing "swap latency, measure P&L" work — but latency is treated as an exogenous scalar or empirical distribution against a static book, not swapped between simulator models.
- **Moallemi & Sağlam (2013)** *The Cost of Latency in HFT* (Operations Research 61(5)). Closed-form cost of a *constant* latency for a representative execution agent. No simulator, no tail case.
- **Cartea & Sánchez-Betancourt (2022)** *Optimal Execution with Stochastic Delay* (Finance & Stochastics). Analytical stochastic-delay execution; no simulator sweep.
- **Cartea, Jaimungal, Sánchez-Betancourt (2021)** *Latency and Liquidity Risk* (IJTAF; arXiv 1908.03281). Compares fill / adverse-selection outcomes under latency; analytical, not simulator-based.
- **Bergault, Drissi, Guéant (2020)** *The Cost of Latency for MM under Latency* (Quantitative Finance 20(9); arXiv 1806.05849). Optimal MM with a fixed latency parameter, sensitivity to that parameter reported.
- **arXiv 2504.00846 (2025)** *The effect of latency on optimal order execution policy* — RL policies under varying latency; assumes constant delay.
- **arXiv 2505.12465 (2025)** *Resolving Latency and Inventory Risk in MM with RL* — RL agent robustness under latency perturbation; still parametric constant delay.
- **Bonart & Gould (2017)** *Latency and Liquidity Provision in a LOB* (Quantitative Finance; arXiv 1511.04116). Empirical inter-arrival phases around market orders; not a simulator-swap experiment.
- **Sun, Bipin et al. (2024)** *Market Simulation under Adverse Selection* (arXiv 2409.12721). Adverse-selection–aware sim; latency held constant.

**Novelty verdict — by angle**

1. **LOB simulators with tail-aware, arrival-driven latency:** *Novel.* All surveyed simulators (ABIDES, ABIDES-Markets, MarketSim, CoinTossX, JAX-LOB, Oxford-Man pro-rata sim, arXiv 2510.08085) treat latency as constant or i.i.d. draws with no state coupling. Two-queue Hawkes-driven latency inside a discrete-event replay is unclaimed.
2. **Decomposition of round-trip latency into matching / transit / decoder inside a simulator on MDP3 data:** *Novel.* Aquilina-Budish-O'Neill measure a related public quantity but do not decompose it or embed it in a simulator; nothing else found.
3. **Coupling Hawkes arrivals to a service queue that *is* execution latency:** *Novel application; partial theoretical replication.* Daw-Pender and Gao-Zhu supply the queue-Hawkes theory; no prior work applies it to per-firm HFT execution latency inside a trading simulator.
4. **Constant-delay vs tail-delay simulator swap on a market-making / execution algo, reporting P&L, adverse selection, fill rate delta:** *Partial prior art.* Cartea & Sánchez-Betancourt's shadow-price papers report latency-P&L sensitivity in FX but not a simulator-model swap; Bergault-Drissi-Guéant sweep a constant-latency parameter. The specific controlled A/B — *same book, same tape, same algo, only the latency model changes* — is unclaimed.

**Paper's positioning.** The empirical/engineering realization of Daw & Pender's queue-Hawkes theory inside a real trading simulator, validated by the constant-vs-tail A/B on a shadow POV algorithm calibrated on years of CME NQ MDP3 data. Cite Daw & Pender 2018 and Gao & Zhu 2024 as the queue-Hawkes foundation; cite Byrd/Balch/Hybinette 2020 as the ABIDES-class simulator this work upgrades; cite Aquilina-Budish-O'Neill 2022 as the public-good latency measurement whose per-firm engineering complement we supply.

**Weakness to acknowledge upfront.** The queue-Hawkes theory (Daw & Pender) is not ours; what is ours is the empirical distributions driving the two queues, the two-queue architecture inside a discrete-event replay, and the controlled A/B on a shadow POV. If a reviewer asks "why not use ABIDES?" — the answer is that ABIDES models arrival events but constant latency; layering a two-queue Hawkes-driven latency model on top of it would produce essentially the same paper we write here, just with ABIDES as the substrate. The Kaspar simulator is the substrate we own and can measure against real MDP3 recordings; the ABIDES port is a follow-on.

### 2. Data and measurement

#### 2.1 Corpus

- **CME NQ front-month, chan 318, RTH-only.** 2025-01-01 → 2026-02-27, target ~281 sessions × ~10 30-min windows/session ≈ ~2800 windows.
- Per-session tapes emitted by the parser stack (`dbento_pcap_parse` + `arrival_paper.fill_tape` + `arrival_paper.packet_tape`):
  - `msgtape.csv` — per-securityID, RTH-only, three per-message timestamps (`transactTime`, `sendingTime`, `handlerendtim`) plus action / pxd / sz / orderID.
  - `fill_tape.parquet` — every historical passive maker fill, with mid-anchored markouts at multiple horizons.
  - `bbbochg.csv.gz` — channel-wide BBBO change stream.
  - `packet_tape.parquet` — per-UDP-packet aggregate.
- Front-contract picker: `front_contract.py` (kaspar.db volume/OI + master universe, roll-day exclusion ±1 day).

#### 2.2 Per-window panel (`hourly_panel.py`)

One row per 30-min window. Columns:

- **Hawkes fit.** `mu`, `alpha`, `beta`, `n_branch`, `lambda_bar`, `converged`, `log_mean_lambda`.
- **Five tails** — p50 alongside p99 (or p95 for markout / return) so `p99/p50` ratios are available:
  - `p50_lat_me_ns`, `p99_lat_me_ns` — matching-engine latency, `sendingTime − transactTime`.
  - `p50_lat_handler_ns`, `p99_lat_handler_ns` — send-to-handler latency, `handlerendtim − sendingTime`.
  - `p50_qsim_ns`, `p99_qsim_ns` — **modelled queue latency** from a G/D/1 sim on real `handlerendtim` arrivals at deterministic service `service_us` (default 7 μs). This is the same recursion the Kaspar simulator will use; the paper's grid is thus both a measurement and a ground truth for the sim.
  - `p50_absmark`, `p95_absmark` — adverse-selection markout on maker fills.
  - `p50_absret`, `p95_absret` — return fat-tail proxy from fill exec-price stride.
- **Filters.** Drop `kept=False` windows (below `min_events` intensity gate); drop volstats-vs-DB roll-day mismatches ±1 day.

#### 2.3 5×5 grid analysis (`grid_scan.py`)

Every kept + converged window is one point (`lambda_bar`, `n_branch`). Bin both axes into equal-quantile 5-bin edges → 25 cells. For each cell, per tail, report:

- **Absolute grid** — median across the cell's windows of the tail's raw p99 (in μs for latencies, price units for markout/return).
- **Relative grid** — median across the cell's windows of `p99 / p50` (dimensionless, "how much fatter than the median").

Plus a **cell mass** heatmap (# windows per cell). 5 tails × 2 grids + 1 mass = **11 heatmaps** as the paper's central figure block.

The grid is a reporting object: it shows how each tail behaves across the (λ̄, n) plane and provides the ground truth against which the sim's per-cell p99 is compared for calibration.

### 3. Why heavy tails emerge from a plain G/D/1 with the arrival stream

The paper's core insight: given any scalar service time `s`, feeding real CME MDP3 arrivals into a G/D/1 produces heavy-tailed system latency **for free** — no fitted latency distribution, no CDF sampler, no Hawkes-state variable at draw time.

#### 3.1 The recursion

For message `i` with arrival time `arrival[i]` (using the tape's `handlerendtim` for the system-side queue, or `transactTime` for measurement), release time is:

```
release[i]  = max(arrival[i], release[i−1]) + s
sys_time[i] = release[i] − arrival[i]
            = max(0, release[i−1] − arrival[i]) + s
```

That's it. One scalar knob.

#### 3.2 Why bursts fatten the tail

In a quiet stretch, `release[i−1] < arrival[i]` — the server was idle waiting — so `sys_time[i] = s` exactly. In a bursty stretch, `arrival[i] − arrival[i−1] < s`, so `release[i−1] > arrival[i]` and message `i` inherits the previous message's residual service time on top of its own service. Cluster `N` messages inside one service_us, and the `N`th message pays roughly `N × s`. The tail *is* the distribution of cluster sizes, and cluster sizes come straight from the tape's Hawkes-structured arrivals.

Empirically on our corpus, at `s = 7 μs`: the median arrival's `sys_time` is 7 μs (quiet moment, queue empty), the p99 is 40–50 μs (arrived at the tail of a 6–7-message cluster).

#### 3.3 Same recursion in two homes

- **Python (paper's analysis).** `arrival_paper.hourly_panel.qsim_p50p99_in_window` runs the recursion per 30-min window on real `handlerendtim` arrivals; reports p50/p99 as one of the five tails.
- **C++ (Kaspar simulator upgrade).** `OB.cpp`'s existing `pub_q` (inbound) and `del_q` (outbound) FIFOs replace their current constant-lag release rule with the G/D/1 recursion above.

The reader can verify the sim is correct by comparing per-cell p99 from the Python `qsim` (paper table) to the sim's per-cell p99 running on the same tape with the same `s`. They must agree to within rounding.

### 4. Simulator upgrade

Implementation is minimal, guarded by a compile-time flag so the constant-delay baseline stays bit-identical.

- **Two G/D/1 queues**, one per side, both already present in `OB.hpp` as `pub_q` (inbound) and `del_q` (outbound). The change is a one-line arithmetic swap in each queue's release rule: current `release = arrival + feed_delay` becomes `release = max(arrival, release_prev) + service_us`.
- **Two scalar knobs.** `service_us_inbound` = operator's own decoder / handler cost (they profile their own code). `service_us_outbound` = CME's matching-engine service time (estimated from public MDP3 by §6's recipe).
- **`#ifdef OB_TAIL_DELAY`** guards both edits. Undefined → constant-delay path unchanged, all existing tests continue to pass. Defined + service knobs set → G/D/1 path.
- **No new queues, no new actors, no new messages.** Deterministic under market time (no wall-clock timers) — same replay produces the same P&L.

Full C++ diff and unit tests: see `frame_kaspr/` in the companion repo. This paper does not reproduce the diff.

**Operator config (kaspr.ini):**

| Knob | What |
|---|---|
| `service_us_inbound` | Own decoder service time (μs) |
| `service_us_outbound` | Exchange service time (μs), calibrated per §6 |
| `OB_TAIL_DELAY` (compile) | Whether to use the G/D/1 path or the constant path |

### 5. A/B: shadow POV under constant vs tail-aware latency

Same tape, same instrument config, same shadow POV parameters. Twice on the same NQ corpus:

- **Baseline.** `OB_TAIL_DELAY` undefined. Constant `delay = X μs`, `feed_delay = Y μs` matching the previously published shadow-POV runs.
- **Corrected.** `OB_TAIL_DELAY` defined. `service_us_inbound = 7`, `service_us_outbound = <recipe output>` from §6.

Both runs feed the same P&L / fill-rate / adverse-selection collector.

**Reported deltas** (bootstrap CI by session):

1. Cumulative P&L (baseline − corrected) and its distribution across sessions.
2. Fill rate (fills / posted quotes) per regime bin (bottom / middle / top of the (λ̄, n) grid).
3. Adverse-selection cost per fill — mean and p95 |markout| under each latency model.
4. Order round-trip p50/p99 distribution — sanity check that the sim's simulated latencies match the empirical grid from §2.3.

**Framing.** This is a correction to the prior article's shadow-POV numbers, not a new algo. If the delta is small, the prior conclusions stand. If it is large, subsequent shadow work must adopt the tail-aware sim.

### 6. Practical guidance for LOB-simulator users

**Two audiences, two service times.**

| Audience | Service time they need | How they get it |
|---|---|---|
| Their own decoder / handler | `service_us_inbound` | Profile their own code. They wrote it, they can measure it. |
| CME's matching-engine + gateway | `service_us_outbound` | **They don't know it. This paper's recipe extracts it from public MDP3.** |

The paper's central practitioner deliverable is the exchange-side recipe:

#### 6.1 Recipe for estimating exchange service time from public MDP3

1. Get a public MDP3 corpus (Databento or similar) covering the target instrument.
2. Build a msgtape (parser shipped in `arrival_paper/`) emitting `transactTime` and `sendingTime` per message.
3. Compute per-message `sendingTime − transactTime` in microseconds.
4. Report the p1 across the whole corpus (or p1 per 30-min window, corpus mean of those p1s if you want to be conservative). Under G/D/1, the fastest 1% of arrivals see an empty server, so their system time collapses to the service time itself.
5. Use that value as `service_us_outbound`. On our NQ corpus, this comes out to ~X μs (final number pending full-corpus completion).

The `arrival_paper.calibrate_service_time` script automates steps 3–4.

#### 6.2 Which tails matter most

On our corpus:

- **Matching-engine latency dominates** — a 3.4 ms p99 median vs 20 μs for the receive-side handler tail.
- **Modelled queue at 7 μs service** — 49 μs p99. Meaningful for downstream decoders, but small next to the ME side.
- **Adverse selection and return fat-tail** — the co-moving tails a market-making algo must actually hedge against.

#### 6.3 Where NOT to trust a constant-delay sim

- **Bursty regimes.** FOMC / CPI / macro-event windows have inter-arrival tightening that makes queue backup dominate.
- **Near-critical n.** Even outside macro events, n > 0.9 windows produce clustering that a constant-delay sim can't reproduce.
- **Passive-fill P&L attribution.** Adverse-selection concentration in the fastest fills is a queue-backup phenomenon; constant-delay sims under-cost it.

### 7. Discussion & limitations

- **Corpus scope.** NQ only. ES and BTC are deferred; the recipe generalizes but calibration numbers are NQ-specific.
- **Interactive fills ignored.** Our orders don't perturb the market data in this replay setup. Real interactive fills would need a full agent-based extension.
- **Race margin vs. per-firm latency.** Aquilina-Budish-O'Neill (2022) measures a public-good race-margin quantity from LSE INET logs. We measure per-firm engineering latency from CME MDP3. Different objects — do not conflate.
- **Model class.** We fit exponential Hawkes for tractability. Power-law kernels (Hardiman-Bercot-Bouchaud 2013) would fit the intraday-decay curve better; using them would push our n estimates modestly upward but not change the sim recursion.
- **G/D/1 assumes deterministic service.** Real decoders have jitter; adding a small service-time noise ε in the sim is a trivial extension left to future work.

### 8. Future work — Hawkes-aware Shadow POV

The five tails this paper reports co-move under a single Hawkes-driven arrival state (λ̄, n). The current shadow POV is oblivious to that state; it participates at a target rate against measured trade volume without conditioning on burstiness. The natural follow-on paper extends shadow POV to be Hawkes-aware:

1. Ingest the online (λ̄, n) estimator (`arrival_paper.online.HawkesEstimator`).
2. Condition participation rate on the current grid cell — throttle in near-critical cells, participate normally in quiet cells.
3. Condition adverse-selection budget on the cell's markout distribution — stand down when the p95 |markout| exceeds a threshold.
4. Condition passive-quote width on the cell's return fat-tail.
5. Re-run the A/B: constant-delay + non-Hawkes shadow vs tail-aware delay + Hawkes-aware shadow. Report the incremental P&L / fill-rate / adverse-selection delta attributable specifically to the Hawkes conditioning.

**Why this paper first.** A Hawkes-aware shadow evaluated in a constant-delay sim would show phantom edge because the sim under-costs bursts. The simulator upgrade in this paper is the prerequisite that makes the Hawkes-aware follow-on honest.

### 9. Conclusion

LOB simulators are the default tool for evaluating market-making and execution algorithms. Every published one uses constant or i.i.d. latency, decoupled from the arrival process. On CME NQ, the arrival process is Hawkes-clustered and the resulting service-queue latency is heavy-tailed — you cannot see that in a constant-delay sim.

The correction is small: one line of arithmetic per queue and one exchange-side service time estimated from public MDP3. The tails come out for free because the arrival stream already carries the Hawkes state. We provide the recipe, the code, and the corrected shadow-POV numbers.

### Appendices

- A. Exponential-Hawkes MLE derivation, Ogata thinning simulation, and the recursion used in `hawkes_smoke.py`.
- B. The G/D/1 recursion, its Python implementation (`qsim_p50p99_in_window`), and unit tests.
- C. Full 5×5 grid heatmaps for all 5 tails × {absolute, ratio}, plus the cell-mass heatmap.
- D. C++ `OB.cpp` diff for the two-queue upgrade, `#ifdef OB_TAIL_DELAY` guards, and the regression-test proof that the constant path is bit-identical.
- E. `arrival_paper` Python module API + reproducibility runbook (msgtape parser → panel builder → grid_scan → calibrate_service_time).

---

## Corpus and infrastructure — concrete

### Data at hand

```
/vast/home/vmayeski/out/bin/310/          ES  — 1094 dates, 731 .ok
/vast/home/vmayeski/out/bin/318/          NQ  — 1094 dates, 731 .ok
Range: 2023-01-03 → 2026-02-27
Size:  1.3 TB combined
```

### Pipeline

```
.bin  →  bin_reader.py  →  per-day per-stream tapes:
                              arrival_tape.parquet   (~500 MB / day / stream)
                              event_tape.parquet     (~1 GB / day / stream)
                              bbo_tape.parquet       (~200 MB / day / stream)
                              trade_tape.parquet     (~50 MB / day / stream)

Per-day features:
  hawkes_fit.json    (μ, α, β, γ, n, LL, KS_p)
  markout_bins.parquet     (per event: λ̂, mrkt at each τ, side, controls)
  daily_stats.json         (CV, Fano, H, mean_rate, ...)

Aggregation:
  panel_arrival_process.parquet   (731 days × 2 streams × K stats)
```

### Compute budget (rough)

- **Bin decode**: 1.3 TB → tapes = one-time ~24-48 hours on the 72-core box using existing `binstats.cpp` extended
- **Hawkes MLE per day**: minutes each (~10M events) — 731 × 2 = <8 hours on 72 cores
- **Markouts per day**: minutes each — negligible
- **Regressions**: negligible
- **Total** first-run: ~2-3 days of compute; iterations are cheap since tapes cache

### Repo layout (proposed)

```
kaspar-hft/
└── arrival_paper/
    ├── src/
    │   ├── bin_reader.py       # .bin → tapes
    │   ├── hawkes.py           # MLE (unmarked/marked/2-D)
    │   ├── online.py           # O(1) intensity
    │   ├── markout.py          # per-anchor markouts
    │   ├── panel.py            # aggregate daily → panel
    │   └── plots.py            # figures
    ├── scripts/
    │   ├── build_tapes.sh      # driver: bin → tapes for all days
    │   ├── fit_hawkes.sh       # driver: tape → hawkes_fit per day
    │   ├── compute_markouts.sh # driver: tape → markouts per day
    │   └── build_panel.sh      # driver: aggregate → panel
    ├── figures/
    ├── tables/
    ├── notebook/
    │   └── explore_panel.ipynb # sanity + iteration
    └── arrival_paper.tex       # the writeup
```

Store derived tapes in `/vast/home/vmayeski/out/arrival_paper/tapes/{stream}/{date}/`.

---

## Task list (ranked)

**A. Corpus infra**

1. **BTC .bin conversion (task #78 pending)** — run `dbento_pcap_to_bin --chan 326` across all 731 days for the BTC feed. Produces `/vast/home/vmayeski/out/bin/326/326.{yyyymmdd}.databento.bin`. ~24 hrs on 72 cores.
2. Extend `binstats.cpp` (or add sibling `bin_to_tapes.cpp`) to emit `message_tape`, `packet_tape`, `bbo_tape`, `trade_tape` per session. C++ for speed on the combined ~2 TB corpus (ES + NQ + BTC).
3. Wire into `scripts/build_tapes.sh` — parallel across days × streams.
4. Validate one day end-to-end (spot-check tapes vs raw bin).

**B. Statistics infra (per-day features)**

4. Python `arrival_paper.stats`: CV, CV², Fano(T), Hurst estimator on arrival_tape.
5. Reproduce fast_send single-day numbers as a regression test.
6. Run across 731 days, emit `daily_stats.json` per (stream, day).

**C. Hawkes fitting**

7. Unmarked exp-Hawkes MLE (scipy) with analytic gradient.
8. Marked Hawkes with size marks (scope decision 1).
9. 2-D Hawkes on bid/ask arrivals.
10. Time-rescaling residual test (KS on Exp(1) residuals).
11. Run all three per day; emit `hawkes_fit.json`.

**D. Online estimator**

12. `arrival_paper.online` — O(1) `update(t, mark)` → λ̂ recursive.
13. Sanity check on simulated Hawkes (recover params).

**E. Latency prediction**

14. Refit `floor + slope · idx` per stream per session from fast_send-style measurements.
15. Fit span-given-λ̂ distributions.
16. Cross-validate latency-percentile predictor.

**F. Mid-anchored markouts**

17. Trade-anchored markouts at {100ms, 1s, 10s, 30s, 100 evts, 500 evts}.
18. Book-event-anchored markouts (same horizons).
19. Distributions per stream, per anchor, per τ.

**G. Fill-tape construction from MBO (scope decision 7)**

20. One-pass builder: read `message_tape`, maintain per-price-level book state and `order_id → (add_ts, add_price, side, size, last_modify_ts, position_in_queue)` hashmap. On each Execute with a maker_order_id, emit a fill_tape row and update state.
21. Handle edge cases: multi-fill executions, iceberg orders, order modifies mid-queue (price-time priority reset), cancels-that-become-executes, hidden liquidity.
22. Attach queue-at-submit and queue-at-fill features: `size_ahead_at_submit`, `n_orders_ahead_at_submit`, `size_remaining_at_fill`, `n_orders_behind_at_fill`.
23. Attach arrival-process features: `λ̂_at_submit`, `λ̂_at_fill`, `λ̂_delta`, `dq_ahead_rate` (mean size-decrement rate on our side ahead of us during `[submit_ts, exec_ts]`).
24. Compute per-fill adverse P&L at τ ∈ {100ms, 1s, 10s, 30s, 100 evts, 500 evts} using `bbo_tape` mid.
25. Run across 731 days × 2 streams in parallel (72 cores).

**H. Three-timestamp anatomy (scope decision 6)**

24. Distribution of `t_send − t_match` per stream and per regime.
25. Distribution of `t_recv − t_send` per stream (network transit).
26. Batching pattern: within-packet position × `t_send − t_match`.
27. Fit Hawkes separately on t_match series vs t_send series vs t_recv series; compare params.

**I. Regime splits (scope decision 8)**

28. Slice `message_tape` and simulator-fill records by regime: {full RTH, open 15m, close 15m, FOMC 14:00-14:30, non-FOMC control 14:00-14:30, roll-neighbor}.
29. Re-run §3, §6, §7 per regime.
30. Build the regime-summary table.

**J. Main result — intensity × toxicity**

31. Compute λ̂ (message-level) at each anchor event and at each maker fill.
32. Decile-bucketed mid markouts (§7.1).
33. Decile-bucketed real maker fills from MBO (§7.2).
34. Regressions with controls, clustered SE (§7.3).
35. Cross-day stability (§7.4).
36. Directional Hawkes → signed markout (§7.5).
37. **Attribution regression: baseline vs arrival-augmented model, ΔR² is the headline (§7.6).**
38. Queue-dynamics slices — fast/slow, cleared/standing, alone/crowded (§7.7).

**J′. Unification: fat tails × branching × adverse selection (§8)**

39. Per-day return tail exponent ν_day (Hill + Fréchet) at multiple Δ, per stream.
40. Assemble (n_day, ν_day, adv_pnl_top_decile_day) panel — 731 × 3 × 3 (streams).
41. Cross-sectional correlations and regressions per stream; test the three-way link.
42. Report the 18-correlation panel (3 tails × 2 Hawkes summaries × 3 streams) marginal + partial. No SEM fit in this paper; the latent-factor question is a follow-up.
43. Message vs trade-level subordination test (Ané-Geman comparison + Hawkes compensator).
44. Cross-product replication: does the factor structure hold on ES + NQ + BTC?

**K. Application**

45. Fill-rate vs adverse-P&L frontier from `fill_tape`: for each λ̂ decile, report per-fill mean adverse P&L and share of daily fills. Skipping the top decile saves X ticks of adverse selection at the cost of Y% of fills.
46. Order-lifetime slice: does λ̂ predict *which* orders get filled fast vs sit? Fast-fill (< 1 s) orders should carry more adverse P&L in top-decile λ̂.

**L. Writeup**

47. Draft LaTeX (reuse fast_send.tex style/preamble).
48. Figures 1-13 (see below).
49. Iterate; reviewer pass.

### Milestones

- **Week 1**: A + B done; single-day validation against fast_send single-day numbers.
- **Week 2**: C + D done; per-day Hawkes fits across 731 days; message-level vs packet-level comparison.
- **Week 3**: F done; H done (three-timestamp anatomy stands as its own §).
- **Week 4**: G done — fill_tape built across all 731 days.
- **Week 5**: I + J done — regime splits and main result.
- **Week 6**: E + K + L — latency prediction, application, drafting.

---

## Key figures planned

| # | figure | shows |
|---|---|---|
| 1 | Fano(5s) and H over 731 days per stream, event annotations | multi-year stability + macro events |
| 2 | Message-level vs packet-level CV / Fano / H side-by-side | why the message-level view is the right one |
| 3 | Distribution of Hawkes branching ratio n across 731 days (marked vs unmarked) | how near-critical, and how stable |
| 4 | λ̂(t) time series over 30-min RTH window with trades overlaid | intuition for online estimator |
| 5 | Hawkes time-rescaling QQ (Exp(1)) — one day + KS-p distribution across days | goodness-of-fit |
| 6 | `t_send − t_match` and `t_recv − t_send` distributions per stream | three-timestamp anatomy |
| 7 | Intraday intensity profile λ̂(t) averaged across 731 days, with open / close / FOMC highlighted | intraday regime |
| 8 | Median \|markout\| and per-fill adverse P&L by λ̂-decile × τ, faceted per stream — **the money figure** | dose-response |
| 9 | Fill-rate vs adverse-P&L frontier (real maker fills, λ̂ gates) | practical value |
| 10 | FOMC-day intensity profile vs matched control days, minute-resolution around 14:00 | FOMC as info event |
| 11 | Attribution: ΔR² of arrival-augmented model over baseline, per stream and per regime | **the headline: how much of adverse selection the arrival process explains** |
| 12 | Fast/slow × λ̂-decile × adverse-P&L heatmap; queue-cleared vs queue-standing markout distributions | queue-dynamics answers Q1-Q3 (§7.7) |
| 13 | Three-panel scatter across ~5000 windows per stream: (a) n_window vs 1/ν_window, (b) n_window vs adv_pnl_p95, (c) 1/ν_window vs adv_pnl_p95 — Spearman ρ + partial-ρ annotated | **the three-tail correlation figure** |

---

## Future work (deferred; not in v1)

Scope for v1 is the NQ front-month simulator upgrade + shadow POV A/B. The following extensions are deferred; each has enough distinct content to be its own paper.

- **Hawkes-aware shadow POV (the natural direct follow-on).** See §8 in the paper outline. This paper's simulator upgrade is the prerequisite: a Hawkes-aware algo evaluated in a constant-delay sim would show phantom edge because the sim under-costs bursts. Once the sim mirrors real-market latency, extend shadow POV to (i) ingest an online (λ̄, n) estimator, (ii) throttle participation in near-critical cells, (iii) condition adverse-selection budget on the cell's markout distribution, (iv) condition passive-quote width on the cell's return fat-tail. Re-run the A/B: tail-aware sim × Hawkes-aware shadow vs tail-aware sim × non-Hawkes shadow. Report incremental P&L / fill-rate / adverse-selection delta attributable to the Hawkes conditioning.
- **Cross-product replication.** Extend to CME ES front (chan 310) and CME BTC front (chan 326). The G/D/1 recursion and the exchange-service-time recipe are product-agnostic; the only new work is running the calibration on the other corpora and verifying that the 5×5 grid tells a consistent story.
- **Interactive fills.** Current sim replays market data; our simulated orders don't perturb the book. Extending to interactive fills means simulated fills feed back into the arrival process (partial-fill dynamics, our own quotes influencing the queue) — a full agent-based extension.
- **ABIDES port.** Ship the two-queue G/D/1 recursion as an ABIDES plug-in so the broader research community can use it without adopting Kaspar. The port is one edit to ABIDES's per-link latency draw plus a config exposing the two service times.
- **Cross-instrument latency coupling.** NQ↔ES on colo have correlated latencies (shared infrastructure, cross-hedging flow). Extend the sampler to draw jointly from a bivariate distribution across two channels' outbound queues.
- **Full pcap rebuild for the network-transit tail.** `recv_time` is zero-initialised in the databento .bin pipeline. A raw-pcap re-parse would split the send-to-handler stage into network transit + software decoder, adding a sixth measurable tail.
- **Non-deterministic service time.** Real decoders have jitter; the trivial extension is `service_us + ε` with ε from a jitter distribution the operator measures on their own hardware.
- **Signed-Hawkes directional alpha.** Its own paper, lit-search-verified novel in 2026-09 (see reference memory). This paper uses total λ̂ only; the signed variant λ̂⁺ − λ̂⁻ as directional alpha is a separate research program.

---

## Publication notes

- **Nothing proprietary in the .bin format schema goes in the paper.** Same discipline as fast_send: aggregate statistics and Python only.
- **License / branding:** M2 Tech / Vincent Mayeski, MIT for the Python module.
- **Companion companion .md article** (like `md_latency_article.md`) with the empirical numbers spelled out plain-text.
- **arXiv target:** q-fin.TR (trading and market microstructure), with cross to stat.AP.
- **Length:** aim ~25-30 pages. Denser than fast_send because there are more results to fit.

---

## Open questions to resolve before drafting

- **Which subset of 731 days to fit Hawkes on for the paper?** Recommend all 731 with per-day parameters; then present the *distribution* across days as the headline. If MLE is slow on the fattest days, subsample events (uniform thinning is not Hawkes-preserving; use random subsampling with variance correction).
- **Book-event anchor definition** — how do we assign a "side" to an ambiguous cancel? Recommend: side = the side whose top-of-book size decreased. Skip if both sides changed at the same ns.
- **Tick-normalization** — report markouts in ticks or in bp of price? Recommend ticks primary, bp secondary for cross-stream comparability.
- **Handling of overnight regime** — RTH only for the main analysis; briefly report on the Globex regime as robustness.
- **Reproducibility for external readers who don't have the bin archive** — publish one sample day's derived tapes as CSVs alongside the arXiv submission.

---

## Bottom line

We have exactly what's needed: 731 days of pre-decoded MDP3 for both ES and NQ. The gaps between fast_send and the new paper are (a) multi-year, (b) fitted marked Hawkes, (c) online estimator, (d) markouts × intensity — and the corpus supports all four with room for tissue.

Next action: build the .bin → tapes step (task A1). That unlocks everything downstream.
