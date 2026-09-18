# Arrival Process, Latency, and Toxicity in CME ES / NQ

**Paper outline — 2026-09-17 — v@m2te.ch**

**Working title:** "Long Latency Tails in HFT Systems Have the Same Signature as Fat Return Tails and Adverse-Selection Tails — Evidence from CME MDP3 on ES, NQ, and BTC, with Implications for Market-Making Systems"

**Alternative title candidates** (pick one on final draft):
- "Long Latency Tails in HFT Systems Have the Same Signature as Fat Return Tails and Adverse-Selection Tails: Implications for Market-Making Systems" (current — punchy, honest about "signature" not "cause", flags the deployment angle)
- "Three Correlated Tails in HFT: Latency, Returns, and Adverse Selection Track the Message-Arrival Hawkes, and What That Means for Market Makers"
- "The Common Arrival-Process Signature Behind Latency, Return, and Adverse-Selection Tails in CME Futures — with Implications for Market Making"

"Same cause" would be the MAL claim we deferred; "same signature" states the empirical fact this paper actually delivers. The "implications for market making" clause promises the paper's deployment story: the online λ̂ estimator + Shadow POV gating result.

**The pitch (verbatim to appear in the abstract):**

> *Three tails in three domains — the decoder-latency tail, the maker-adverse-selection tail, and the return-fat-tail — are each linked to the intensity and criticality of the CME MDP3 message-arrival process. We measure all three at scale on 730 sessions across ES, NQ, and BTC, fit exponential Hawkes per window, and report per-stream marginal and partial correlations between each tail and both Hawkes summaries `(n, λ̄)`. Whether the three tails share a single hidden driver — a Market Activation Level (MAL) — we raise as a future-paper question and do not fit here.*

**Central thesis (extended):** the near-critical Hawkes clustering that fattens the packet-decoder latency tail (fast_send extended), produces the toxic maker fills (§7), and generates the fat-tailed return distribution (§8) shows up in all three domains. This paper's job is to (a) demonstrate the empirical link at per-fill resolution on public MBO L3 across 3 products × 730 sessions, (b) report the marginal and partial correlations for each of `(n × 3 tails)` and `(λ̄ × 3 tails)` across the corpus, and (c) publish the online-λ̂ estimator that turns arrival-side monitoring into a runtime signal for a Shadow POV execution algorithm. We deliberately do **not** fit a one-factor SEM or a latent-factor Hawkes extension — those belong to a follow-up (see Future Work: MAL).

**Scope decisions (locked with author):**

1. **Marked Hawkes** — condition α on packet size / order size / trade size. One extra parameter, better fit.
2. **Both anchors for markouts** — compute markouts anchored on (a) trades and (b) all book-update events. Trades give clean signs and fewer observations; book events give ~100× the sample.
3. **This is a standalone paper**, not an extension of fast_send. fast_send analyzed a single day and rejected Poisson; this paper fits generative models across a multi-year corpus and adds the P&L bridge.
4. **Corpus: 731 trading days**, 2023-01 to 2026-02, three streams — ES (chan 310), NQ (chan 318), and **BTC (chan 326, CME Crypto Futures)**. ES and NQ are already decoded to `/vast/home/vmayeski/out/bin/{310,318}/` (1.3 TB, .ok markers). BTC PCAPs live at `/vast/vendor/databento/pcaps/glbx/futures-xcme/YYYYMMDD/` on `224.0.33.240:14326` (legacy IP, ~158 files/day × 313 days) and still need `.bin` conversion via `dbento_pcap_to_bin --chan 326`; task #78 is the pending pipeline step. BTC is added as a *third stream* — same methodology across three products.
5. **Message-level arrival process, not packet-level.** The natural unit for both info content and latency work is the individual SBE *message* (book add/modify/cancel/execute or trade). fast_send characterized packet arrivals; this paper characterizes messages *within* packets and treats packet-span as a downstream summary. A packet with 45 messages is 45 arrivals of information.
6. **Three timestamps per message.** The .bin schema (`r_l3.hpp`) carries all three: `transactTime` (matching-engine time), `sendingTime` (CME gateway packet header), and `recv_time` (pcap kernel timestamp). Their differences are treated as first-class objects in the paper.
7. **Adverse selection is measured on real historical passive fills, not a simulated quoter.** MBO L3 carries every order lifecycle: add → modify → cancel → execute. Every trade in the tape carries the maker order_id, and we can trace back to that order's Add event to recover submission time, submission price, side, and time-in-queue. Compute markouts after each *real* maker fill. This is stronger than any simulator: no fill model, no queue assumption, no synthetic quote placement — the sample is every passive fill that actually happened in ES and NQ across 731 days. Mid markouts on all trades stay as a wider robustness comparison.
8. **Regime splits are first-class in every result.**
   - **Open (09:30–09:45 ET)** and **Close (15:45–16:00 ET)** — vol-scalper windows, disproportionate share of daily activity and toxicity
   - **FOMC days separately** — split those 20+ days from the corpus and report their own panel
   - **Roll-neighbor days** and **CPI/NFP days** as further robustness slices

---

## What's genuinely new vs. fast_send

fast_send established, on one day of ES/NQ/ZN book+trade:

- Marginal not exponential (CV 4-29, P(gap<mean/10) 40-85%)
- Order not independent (Fano(5s) 1024-1260, collapses under Fisher-Yates shuffle)
- Hurst 0.64-0.74, Hawkes-consistent (branching ratio upper bound 0.85-0.97)

Everything in fast_send was a *rejection* of Poisson on a *single day*, and it was computed on **packet** arrivals.

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

Academic literature is all at message/event level (they buy Databento/TAQ; no packets). fast_send is all at packet level. Presenting both, on the same corpus, showing the packet-level view understates message-level clustering by the mean-span factor — that's our specific empirical angle.

#### TIER 2 — REPLICATION AT NEW SCOPE (worth stating, not the headline)

**N-D. Cross-product replication: ES + NQ + BTC on same methodology.**
Literature over-samples ES. NQ Hawkes fits are scarce. CME BTC futures Hawkes fits are essentially absent — crypto Hawkes lit is on spot venues (Coinbase/Binance), not CME MDP3. Matched-methodology results on all three across 731 days.

**N-E. Multi-year stability across 731 days per stream.**
Filimonov did 12 years but ES-only, mid-price only. Achab EUREX single-year. Bellia specific-episode datasets. Nobody has published per-day Hawkes parameter distributions at this cross-day granularity.

**N-F. Regime splits at corpus scale (open / close / FOMC vs matched controls).**
Kirilenko is episodic (2010 flash crash). Filimonov detected precursors at 10-min windows. Nobody publishes Hawkes params × per-fill markouts distributions across ~20 FOMC 14:00–14:30 windows vs matched controls.

**N-G. Unification (fat tails + adverse selection + latency ← one λ̂).**
The individual bilateral links are all replications (Bacry-Muzy / Blanc-Bouchaud on fat tails; fast_send on latency; N-A on adverse selection). The **three-way cross-domain correlations** — reported jointly across a 730-session × 3-product corpus at per-window resolution — have not been published together. Modest but real. Whether they share a single hidden driver is a follow-up (see Future Work: MAL) and not claimed in this paper.

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

Three tails in three domains — the decoder-latency tail, the maker-adverse-selection tail, and the return-fat-tail — are each linked to the CME MDP3 message-arrival process. We measure all three at scale on ~730 trading days across CME ES, NQ, and BTC futures book feeds (chan 310/318/326) using raw MDP3 with three per-message timestamps (matching-engine, gateway-send, capture-recv). We fit exponential Hawkes per 30-min window (mean branching ratio ~ 0.85 ES, ~ 0.92 NQ, ~ ? BTC), track every historical passive maker order via MBO L3 order_id to compute per-fill adverse P&L, and estimate return-tail exponents ν_window. We then report per-stream marginal and partial correlations of each of the three tails against the two Hawkes summaries `(n_window, λ̄_window)` — six correlations per stream × 3 streams. As applications we (i) predict next-100 ms packet spans and decoder-latency percentiles from real-time λ̂ (RMSE ≤ Y µs), and (ii) show that a passive quoter gating on λ̂ saves **Z ticks per fill** of adverse selection at **W%** loss in fill rate. Companion Python module `kaspar_arrival` (MIT) ships the online O(1) intensity estimator and the full analysis pipeline. Whether the three tails share a single hidden driver — a Market Activation Level (MAL) — is raised as a follow-up question and not fit here.

### 1. Introduction

**The three tails.** A trading system experiences three quite different pathologies, each in its own domain:

1. **The decoder-latency tail** — the p99 wire-to-book time is dominated by rare bursts when many messages arrive inside one decode interval, converting into oversized UDP packets and inflated per-message serialisation cost (fast_send, 2026).
2. **The maker-adverse-selection tail** — the worst adverse markouts on passive fills are concentrated in a small share of fills, disproportionately fast fills that occur near the peak of activity bursts (Kirilenko 2011, aligrithm 2026).
3. **The return-fat-tail** — the unconditional distribution of high-frequency price returns has power-law tails much heavier than a Gaussian (Bacry-Muzy 2015, Blanc-Donier-Bouchaud 2017, and 30 years of stylized facts).

Each of these has a substantial literature and each is treated in its own community — infrastructure engineers, execution quants, and stochastic-analysis researchers rarely read the same papers.

**The claim.** They are the same phenomenon. The near-critical Hawkes clustering of the underlying message arrival process is the common driver. When λ̂(t) — the local message-arrival intensity — spikes, packet spans grow, passive quotes get filled by informed aggressors while the price walks against them, and the marginal return distribution over that window is heavy-tailed. On days where the fitted branching ratio `n_day` sits closer to 1, all three tails fatten together. On days where it sits further from 1, all three tails are more moderate.

**What this paper does:**

- Fits marked exponential Hawkes to raw MDP3 message tapes across 731 days × 3 products (ES chan 310, NQ chan 318, BTC chan 326), producing per-session parameter distributions with time-rescaling goodness-of-fit tests.
- Computes per-fill adverse P&L on every historical maker fill in public MBO L3 (via order_id tracking), and correlates it with real-time λ̂.
- Estimates return tail exponents ν_day at multiple horizons per session.
- Assembles the per-window triple (n_window, |adv_pnl_p95|, 1/ν_window) and reports its marginal correlations against (n_window, λ̄_window). The full 18-correlation panel is the headline result. No SEM is fit in this paper.
- Publishes an online O(1) intensity estimator as an MIT Python module and demonstrates two applications: a latency predictor (§5) and a λ̂-gated passive quoter (§9).

**Why three products.** ES and NQ share clientele and correlate closely; if the correlations hold only on those it's a CME-equity artifact. BTC (CME chan 326 crypto futures) has a distinctly different clientele and liquidity profile — if the same tail-vs-Hawkes correlation signs and magnitudes hold across all three, that's evidence for a general property of near-critical financial arrivals, not an ES/NQ quirk.

### 1.5 Prior art and what this paper actually adds

The four component pieces of this paper — Hawkes on financial arrivals, multivariate Hawkes on LOB, Hawkes for market making, and adverse-selection markouts — each have substantial prior literatures. The specific combination and the corpus scale are what is new.

**Closest prior work, by thread:**

**A. Hawkes on E-mini S&P (direct competition on the ES side)**
- **Filimonov & Sornette (2012, 2015)** [arXiv 1302.1405 and follow-ups] — apply Hawkes to E-mini S&P mid-price changes 1998-2010, argue "reflexivity" (branching ratio → 1) has increased over time. This is the seminal work on the exact contract we study.
- **Hardiman, Bercot & Bouchaud (2013)** — "Critical reflexivity in financial markets" — challenge Filimonov & Sornette on two fronts: (i) branching ratio is close to critical *throughout* 1998-2012 (constant, not rising), and (ii) the Hawkes kernel decays as a *power-law*, not exponential. Also concludes the market is near-critical.
- **Da Fonseca & Zaatour, Hardiman & Bouchaud** — goodness-of-fit machinery for Hawkes on financial data.

**B. Multivariate Hawkes on LOB (direct competition on the framework)**
- **Bacry & Muzy (2015)** review "Hawkes Processes in Finance" — foundational.
- **Achab, Bacry, Muzy, Rambaldi (2018)** [arXiv 1706.03411, Quant Finance] — nonparametric branching-ratio matrix on EUREX 12-dim event process. This is the state-of-the-art for the bid/ask cross-excitation framework.
- **Morariu-Patrichi & Pakkanen (2018)** — state-dependent Hawkes for LOB (queue-reactive).
- **Rambaldi, Bacry, Lillo** — volume-marked Hawkes.

**C. Hawkes for market making / adverse-selection modeling**
- **Toke & Pomponio (2012)** — bivariate Hawkes for trades-through.
- **Kumar (2021)** [arXiv 2109.15110] — Deep Hawkes for HFT market making.
- **Bellia (2017)** [SSRN 3074313] — HFT market making, liquidity provision, adverse selection.
- **Easley, López de Prado, O'Hara** — VPIN and flow toxicity (the coarse aggregate proxy for what we compute per-fill).

**D. Empirical adverse-selection markouts on maker fills**
- **Aligrithm blog (2026)** "Fast-fills-are-bad-fills" — reports market-order avg −0.72 tick markout, sub-minute limit fills −0.31, ten-minute+ limit fills +0.43. The exact intuition we quantify, but in blog form on a small sample, without linking to Hawkes intensity.
- Practitioner literature broadly agrees that fast fills are toxic; academic quantification with MBO L3 across years is scarce.

**Debate to be aware of** — Filimonov/Sornette vs Hardiman/Bouchaud on whether branching ratio has risen and whether the kernel is exponential vs power-law. Our multi-year corpus 2023-2026 is in a position to weigh in on both.

**What this paper adds that the above literature does not:**

1. **Three-timestamp anatomy (transact / send / recv) — genuinely novel.** Every prior Hawkes-on-CME paper uses a single timestamp (either exchange transact time or their capture time, depending on the data feed they had). None of the four threads above decompose the arrival process into matching-engine, gateway-send, and capture-recv sub-processes. The `t_send − t_match` distribution — CME's gateway queueing signature — is, as far as I can find, unpublished. This is a first-class contribution.

2. **Message-level vs packet-level side-by-side.** All academic Hawkes-on-LOB work is at the *event* level (implicitly message-level, since they buy Databento or TAQ book data). No academic paper we can find explicitly contrasts packet-level and message-level arrival statistics — that's the fast_send angle and it's ours by default. This paper reports both and shows how much the packet-level view understates message-level clustering.

3. **Multi-year (731 days), ES + NQ + BTC, same corpus, same methodology.** Filimonov 12 years but ES-only and mid-price only; Achab EUREX single-year; Bellia specific-episode datasets. Nobody has published a matched-methodology study of E-mini S&P (ES), E-mini NASDAQ-100 (NQ), *and* CME BTC futures (chan 326) with per-day Hawkes parameter distributions. The NQ and BTC sides are each a contribution on their own — the literature over-samples ES, and CME BTC futures arrival processes are virtually unpublished (crypto Hawkes work is on spot venues like Coinbase/Binance, not CME MDP3). If the framework holds across three products with very different clienteles and liquidity profiles, that's evidence the story is a general property of near-critical financial arrivals, not an ES/NQ quirk.

4. **Explicit λ̂-decile × per-fill markout on real MBO L3 maker fills at years' scale.** The Achab paper connects branching-ratio matrix to book flows but doesn't compute markouts. The Bellia and Easley VPIN papers compute adverse selection but not conditional on real-time Hawkes intensity. The Aligrithm-style fast-fill studies compute markouts but not conditional on Hawkes intensity. **The λ̂ × real-maker-fill markout cross-product on years of MBO L3 is, as far as we can tell, unpublished.** This is the paper's headline result.

5. **Online O(1) intensity estimator packaged as a public Python module.** The academic Hawkes literature focuses on batch MLE. Real-time intensity is straightforward once you write the recursion, but no widely-adopted open module exists (as of the search). Making `kaspar_arrival` an MIT-licensed practitioner deliverable fills that gap.

6. **Regime splits (open / close / FOMC) with fitted Hawkes + markouts per regime.** Kirilenko et al. studied the 2010 flash crash episodically; Filimonov detected precursors at 10-min windows. Nobody publishes a corpus-scale Hawkes parameter distribution across ~20 FOMC days vs matched controls. The FOMC 14:00-14:30 intensity/markout profile alone is a novel empirical result.

7. **Reproducible pipeline from raw MDP3 PCAP to results.** Academic papers cite Databento or TAQ but rarely publish the parser. `dbento_pcap_parse` + `bin_to_tapes` + `kaspar_arrival` is the full stack, which is unusual for the microstructure literature.

**Weakness / risk to acknowledge upfront** — the mid markout side of our analysis is not novel; that's why we lean on the fill-conditional (real maker MBO fill) measurement as the money result. The Hawkes-fit numbers will slot into a 15-year-old debate and we should be candid about which side we come down on (early hypothesis: Hardiman/Bouchaud is closer, branching ratio near-critical and roughly stable, kernel closer to power-law than pure exponential).

### 2. Data and preprocessing

#### 2.1 The .bin archive
- **ES (chan 310) and NQ (chan 318)**: 731 trading days each, 2023-01-03 → 2026-02-27, pre-decoded L3 record files at `/vast/home/vmayeski/out/bin/{chan}/{chan}.{yyyymmdd}.databento.bin` (1.3 TB combined, .ok markers).
- **BTC (chan 326, CME Crypto Futures)**: same date range in raw PCAPs at `/vast/vendor/databento/pcaps/glbx/futures-xcme/YYYYMMDD/`, on `224.0.33.240:14326` (legacy IP; the chan re-IP'd to `224.4.70.16` in production but archived pcaps use the old address). **Requires `dbento_pcap_to_bin --chan 326` to produce chan-326 .bin files** — task #78 in the working list. Compute budget: ~24 hrs on 72 cores for the full 3-year backfill.
- All three streams carry MBO records (add/modify/cancel/execute), trades, best-bid/ask reconstruction, and packet-level receive timestamps.

**Cross-product caveat for BTC**: CME BTC futures liquidity has ramped substantially through 2023-2026; the early corpus will have lower message rates than late corpus. Report the intraday-normalized statistics (per-message and per-second scaled by daily volume) alongside raw rates so cross-product comparisons stay honest.

#### 2.2 Windowing
- **RTH only**: 09:30 → 16:00 ET (14:30-21:00 UTC in winter, 13:30-20:00 in summer)
- **Front-month only**: match against MDP3 InstrumentDefinition + volume ranking
- **Roll days skipped**: exclude the two days flanking the roll to avoid mixed-contract stream artifacts
- Net expected: 700 clean trading days × 6.5 hours × 2 streams

#### 2.3 Derived tapes (one per session per stream)
- `packet_tape`: (packet_recv_ts, packet_send_ts, span, first_msg_type)
- `message_tape` (the primary tape for arrival-process work): (t_match, t_send, t_recv, msg_type ∈ {book_add, book_mod, book_cxl, trade}, side, price_ticks, size, order_id, level, packet_seq, idx_within_packet)
- `bbo_tape`: (event_ts_ns, best_bid_ticks, best_ask_ticks, best_bid_size, best_ask_size) — reconstructed from message_tape
- `trade_tape`: (t_match, t_send, t_recv, aggressor_side ∈ {+1,−1}, price_ticks, size, maker_order_id, aggressor_order_id) — subset of message_tape with `msg_type = trade`
- `fill_tape` (per-maker-fill): built by joining trade_tape.maker_order_id back to its Add/Modify in message_tape. Columns:
  - **Identity**: order_id, side, submit_ts, submit_price, submit_size, exec_ts, exec_price, exec_size
  - **Time**: `time_in_queue = exec_ts − last_modify_ts`, `lifetime = exec_ts − add_ts`
  - **Queue-at-submit**: `size_ahead_at_submit` (total size at same price level on same side, minus our own), `n_orders_ahead_at_submit`, `bbo_size_opposite_at_submit`, `spread_at_submit`
  - **Queue-at-fill**: `size_remaining_at_fill` (same-side same-price total AFTER our fill — what's left of the queue at our price), `n_orders_behind_at_fill` (how many orders queued *after* us that are still resting at our price), `bbo_size_opposite_at_fill`, `spread_at_fill`
  - **Aggressor**: `aggressor_side`, `aggressor_size` (their trade size), `aggressor_order_id`
  - **Arrival-process**: `λ̂_at_submit`, `λ̂_at_fill`, `λ̂_delta = λ̂_at_fill − λ̂_at_submit`, `dq_ahead_rate` (rate at which same-price same-side size ahead of us depleted during `[submit_ts, exec_ts]`)
  - **Markout target**: `mid_at_fill`, `mid_at_fill + τ` for each τ ∈ {100ms, 1s, 10s, 30s, 100 evts, 500 evts}

The `message_tape` is the master; `fill_tape` is the primary tape for the adverse-selection analysis. Every downstream analysis reads from `message_tape` (arrival-process work) or `fill_tape` (adverse-selection work).

### 3. Arrival-process characterization across 731 days

**Two arrival series per stream per day** — analyzed side-by-side in every subsection below:

- **Packet-level series** (`packet_recv_ts`) — matches fast_send methodology; useful for latency work
- **Message-level series** (per-SBE-message `transactTime`) — the *true* arrival of information events; strictly finer-grained than packet-level

Ratio n_msgs / n_packets = mean span. Packet-level CV is bounded above by message-level CV; Fano and H are typically higher on messages than packets because within-packet clustering is invisible at packet level.

#### 3.1 Stability of the non-Poisson fingerprint (per-day CV, Fano, H)
- **Table:** distribution of {CV, Fano(5s), H} across days, by stream — mean / median / p10 / p90
- **Figure:** time series of Fano(5s) and H over 731 days, with major events annotated (Fed days, CPI, election, roll dates)
- Test: does Fano scale with mean rate? Regress log(Fano) ~ log(rate) with day fixed effects
- Test: does H depend on day-of-cycle (rollovers, month-ends, ETF-rebalance days)?

#### 3.2 Fitted exponential Hawkes (unmarked) per session
- Model: `λ(t) = μ + Σ_i α · exp(-β·(t - t_i)) 1{t_i < t}`
- Per-session MLE via `scipy.optimize.minimize` on the log-likelihood
  ```
  LL(θ) = Σ_i log(λ(t_i)) − ∫_0^T λ(s) ds
  ```
- **Table:** distribution of {μ, α, β, n = α/β} across 731 days per stream
- **Time-rescaling test:** compensator `Λ(t_i) = ∫_0^{t_i} λ(s)ds` should give i.i.d. Exp(1) inter-event times on {Λ(t_i)−Λ(t_{i−1})}. Report KS p-value per day, and fraction of days with p > 0.05.

#### 3.3 Marked Hawkes with size marks (scope decision 1)
- Model: `λ(t) = μ + Σ_i α · (1 + s_i)^γ · exp(-β·(t - t_i))`
  - `s_i` = packet span (arrival marks) OR trade size (trade marks)
  - `γ` = size-sensitivity exponent
- Fit per session
- Compare LL vs unmarked Hawkes (LR test)
- **Result to expect:** γ > 0, meaning larger packets/trades trigger more subsequent arrivals — the classical "big trade attracts follow-on flow" story

#### 3.4 Two-dimensional Hawkes: bid-side vs ask-side excitation
- Fit joint Hawkes on `{bid-update, ask-update}` arrivals
- Estimate 2×2 excitation matrix `A = [[α_{bb}, α_{ba}], [α_{ab}, α_{aa}]]`
- Cross-excitation `α_{ab}, α_{ba}` measures how much one side's updates drag the other

#### 3.5 Three-timestamp anatomy (scope decision 6)

For every SBE message we have `t_match` (matching-engine `transactTime`), `t_send` (packet-header `sendingTime`), `t_recv` (pcap kernel timestamp).

- **Match → Send** (CME-internal queueing): distribution of `t_send − t_match` per message. This is CME's exchange-gateway service time and is a private property of the CME cluster.
- **Send → Recv** (network transit and our capture): distribution of `t_recv − t_send` — dominated by network path (colo cross-connect + NIC + kernel).
- **Batching signature**: for messages with identical `t_send` but different `t_match` values (they were coalesced into one packet), plot `t_send − t_match` as a function of position within packet.

**Table (planned) — per stream, aggregated:**

| metric | p50 (µs) | p90 (µs) | p99 (µs) | share with delta = 0 |
|---|---|---|---|---|
| t_send − t_match | | | | |
| t_recv − t_send | | | | |
| t_recv − t_match | | | | |

The three arrival processes give three different Hawkes fits — arguably the *match* process is the physically meaningful one (this is what the market is doing), while the *send* process is what any downstream latency work has to plan against.

### 4. Python module + online intensity estimator

Package: `kaspar_arrival/` (public — no proprietary internals).

- `arrival_paper.mle`: batch fitter for {unmarked, marked, 2-D} exp-Hawkes on numpy arrays
- `arrival_paper.online`: O(1) recursive λ̂(t) update
  ```python
  def update(self, t_new, mark=1.0):
      dt = t_new - self.t_last
      self.s = self.s * np.exp(-self.beta * dt) + self.alpha * mark
      self.t_last = t_new
      return self.mu + self.s      # current intensity estimate
  ```
- `arrival_paper.markout`: markout(τ) computer for trade and book-event anchors
- `arrival_paper.plot`: standard figures

### 5. Latency prediction

- Reuse fast_send's `latency = floor + slope · idx + queue_penalty` fit; refit per stream per session
- Fit `E[span | λ̂-decile] → P(idx = k | λ̂)`
- Emit `predicted_latency_percentile(q, λ̂)`
- CV: 90/10 split within-day, then cross-day

**Metric:** p99 latency prediction RMSE, mean absolute quantile error at {p50, p90, p99}.

### 6. Adverse selection — from mid-anchored markouts to simulator fills

#### 6.1 Mid-anchored markouts (robustness / fast approximation)
For each anchor event at time `t_i` (either a trade or a book event), assign a side:
- **Trade anchor**: `s_i = +1` if trade lifted the ask, `−1` if hit the bid (aggressor side from execute + best-quote lookup)
- **Book-event anchor**: `s_i = +1` if best-bid tick moved up or best-ask size grew; `−1` if best-ask tick moved down or best-bid size grew. Ambiguous events skipped.

Markout at horizon τ:
```
markout(τ)_i = s_i · (mid(t_i + τ) − mid(t_i)) / tick_size
```
- τ ∈ {100ms, 1s, 10s, 30s}
- Event-based τ ∈ {50 evts, 100 evts, 500 evts} (measured in `event_tape` events)

Negative markout = adverse: price moved against the aggressor side.

#### 6.2 Real maker fills from MBO L3 (scope decision 7 — the actual measurement)

Mid-anchored markouts assume infinite depth and instant fill for everyone. That is not how a passive quoter is filled — they wait in queue and get filled selectively, precisely when there is an aggressor on the other side. Instead of simulating this, we use **every real historical maker fill in the market**.

**The mechanic:** MDP3 MBO is L3 order-by-order. Every Execute message carries the maker order_id being filled. We track order lifecycles:

- **Add** — `(add_ts, add_price, side, size, order_id)`
- **Modify** — new price/size for same order_id
- **Cancel** — removes order_id
- **Execute** — trade against order_id, `(exec_ts, exec_price, exec_size)`

**Fill tape construction:** for every Execute message on a resting order, join back to the most recent Add/Modify of that order_id to recover:

```
(submit_ts, submit_price, side, submit_size, order_id,
 exec_ts, exec_price, exec_size,
 time_in_queue = exec_ts − last_modify_ts,
 lifetime      = exec_ts − add_ts,
 aggressor_side = ¬side,
 aggressor_size, aggressor_order_id)
```

Then at `exec_ts` compute mid drift at each horizon τ. The maker's realized adverse P&L per fill is:

```
adv_pnl(τ) = side · (mid(exec_ts + τ) − exec_price) · tick_value
```

Negative = adverse (price moved against the maker after they were filled).

**Also record at exec_ts:**
- Local intensity λ̂(exec_ts⁻) — message-level, from online estimator
- Book state: BBO spread, top-level size (bid + ask), imbalance
- Trade-flow burst: number of trades in the preceding 100 ms / 500 ms

**Why this is stronger than a simulator:**

- **Every real passive fill in ES + NQ for 731 days** — millions of maker fills without any modeling assumption
- **No queue model needed** — we already have the maker's *actual* time-in-queue and lifetime from MBO
- **No fill model needed** — the fill is what happened; we just anchor markouts on it
- **Cross-sectionally rich** — condition on maker size (retail vs pro), time-in-queue (fast cancels vs stale quotes), submission distance from BBO, etc.

**Two headline metrics:**

1. **Fill-conditional adverse P&L** — mean signed P&L per fill at τ, sliced by λ̂-decile
2. **Adverse-fill intensity elasticity** — how much |adverse_pnl| rises per unit of log(λ̂)

**Robustness checks:**

- **Filter out short-lived (< X ms) orders** — noise from pings and fleet cancels
- **Filter out ISO / cross fills** where possible (they're a different phenomenon)
- **Split by maker size tier** — small vs large orders may face different toxicity
- **Mid-anchored markouts on ALL trades** (§6.1) as the wider comparison

**Compute:** the fill-tape is a subset of `message_tape` (every Execute with its maker_add lookup). Building it is one pass through `message_tape` per day with an order-id → open-order hashmap. Trivial on 72 cores.

### 7. Main result — arrival intensity × toxicity

Two parallel result tracks: (a) mid-anchored markouts on all anchor events, (b) simulator fills.

#### 7.1 Bucketing by intensity — mid markouts
- Compute λ̂(t_i^−) just before each anchor event (message-level intensity)
- Sort events into deciles by λ̂
- **Table:** median |markout|, mean signed markout, share adverse — per decile × per τ × per anchor-type × per stream
- **Figure:** heatmap: decile × τ, cell value = median |markout|

**Expected shape:** monotone increase in |markout| with decile, top decile ~ 2-5× median decile.

#### 7.2 Bucketing by intensity — real maker fills from MBO (the money result)
- Group `fill_tape` rows by λ̂ at exec_ts (also try λ̂ at submit_ts; report both — they answer different questions)
- **Table:** for each decile, per-fill mean adverse P&L at each τ, fill count, mean time-in-queue, mean lifetime, aggressor-size distribution
- **Figure:** decile-conditioned adverse-P&L curve, faceted by stream × τ

**Expected shape:** top-decile fills carry substantially more adverse selection than median-decile fills — often by a larger multiple than the mid-anchored markouts (§7.1) show, because the fill event selects the adverse subset of price movements. In §7.1 we average markouts over all events; in §7.2 we average only over the events where a maker actually got filled.

#### 7.3 Regression with controls
```
adverse_pnl(τ)_i ~ β₀ + β₁ · log(λ̂_i^−) + β₂ · spread_i + β₃ · |book_imbalance|_i
                 + β₄ · queue_position_i + β₅ · time_of_day_i + day_FE_i
```
- Cluster SE on trading day
- Report β₁ (the intensity slope), t-stat, R²
- Interaction: `β₁ × 1{VIX > 25}` — does the intensity-toxicity link strengthen in high vol?
- Run once on mid markouts, once on simulator fill P&L

#### 7.4 Cross-day stability
- Regress at the daily level → distribution of β₁ across 731 days
- **Result to look for:** β₁ significantly positive on ≥ 90% of days, mean t-stat > 3

#### 7.5 Directional prediction (2-D Hawkes)
- Signed markout ~ (bid-side λ̂ − ask-side λ̂)
- Directional 2-D excitation matrix predicts fill P&L sign

#### 7.6 Attribution — arrival-process share of adverse selection

This is the central-thesis test. Decompose the variance / mean of per-fill adverse P&L into contributions from arrival-process features vs "everything else":

**Baseline model** (arrival-process-blind):
```
adv_pnl(τ) ~ α + β_1·spread + β_2·imbalance + β_3·log(order_size) + day_FE + time_of_day_FE
```

**Arrival-augmented model** (add the intensity features from fill_tape):
```
adv_pnl(τ) ~ [baseline] + γ_1·log(λ̂_at_fill) + γ_2·log(λ̂_delta_positive)
           + γ_3·log(1/time_in_queue) + γ_4·log(dq_ahead_rate)
```

- ΔR² between the two models is the share of adverse-P&L variance the arrival process explains
- F-test on the joint significance of {γ_1, γ_2, γ_3, γ_4}
- Report per stream and per regime
- **Headline number**: "The arrival process explains X% of the per-fill adverse-P&L variance beyond order-book state alone."

#### 7.7 Queue dynamics through the arrival-process lens

Queue-microstructure work as a full topic is a separate paper. This section stays tight: three questions, each answered via the arrival-process framing.

**Q1. Fast fills vs slow fills — is the toxicity story just the fast-fill story?**

Split fills by `time_in_queue` into buckets: {<100ms, 100ms-1s, 1s-10s, 10s-1min, >1min}.
- For each bucket, mean adverse markout at each τ (replicates Aligrithm-style result on our corpus)
- Within each bucket, further split by λ̂-decile
- **The test:** does λ̂ still explain adverse P&L *within* the fast-fill bucket, or is time-in-queue a sufficient statistic? If λ̂ still matters within the same time-in-queue bin, the arrival-process story is not just a rebranding of "fast fills are bad."

**Q2. Queue drained vs queue standing — did the aggressor blow through us or just clip us?**

At exec_ts, look at `size_remaining_at_fill` (same-side same-price total size after our fill). Bucket:
- `size_remaining = 0` — aggressor cleared the entire price level (walked the book). Highest toxicity expected.
- `size_remaining > 0` but small — partial clear.
- `size_remaining` large — aggressor just clipped the front of a deep queue and we happened to be it.

Cross with λ̂: does a high-λ̂ fill more often clear the whole level?

**Q3. Alone vs in a crowd — were we at a thinly-populated price or in a long queue?**

At exec_ts, look at `n_orders_behind_at_fill`:
- 0 — we were the only order at our price (or all behind us have already cancelled)
- Small (1-3) — thin queue
- Large — deep queue behind us; others were willing to join

Interpretation: if `n_orders_behind` is large, other quoters agreed with the price; if it's 0, our order was the outlier. Expected: deep queue behind = more informed flow willing to lean the same way = less adverse markout (we're in a crowd of similarly-informed makers, not a "sitting duck"). Or the reverse — deep queues may indicate a stale consensus that a fresh aggressor exploits. Test empirically.

Cross with λ̂: does the *arrival-rate* explain queue depth better than pure size, and does that interact with markout?

**These three questions justify the queue features on the fill_tape without opening a queue-microstructure paper.**

#### 7.6 Regime splits (scope decision 8) — three separate panels

Each of §7.1–7.5 is re-run on each of these regime subsets and the results compared:

**A. Open window (09:30–09:45 ET)** — the first 15 minutes carry disproportionate flow and vol-scalper activity. Hypothesis: highest λ̂ across the day, largest β₁, largest per-fill adverse P&L. This is where the vol-scalper gets paid or gets run over.

**B. Close window (15:45–16:00 ET)** — last 15 minutes; MOC imbalance flow builds through 15:50. Hypothesis: rising λ̂ curve, distinct microstructure (MOC-driven).

**C. FOMC-day 14:00–14:30 ET** — 20+ Fed announcement days across the corpus, isolated from the rest. Hypothesis: a *massive* intensity spike at 14:00 (statement release) with elevated β₁ and adverse P&L in the immediate post-release window. Sub-analysis: does the *pre-release* intensity profile change (leaks, positioning) vs a control day matched on VIX and volume?

**D. Roll-neighbor days (D-2 to D-1 of contract roll)** and **CPI/NFP days** — robustness slices reported in the appendix.

**Reporting** — one summary table across regimes:

| regime | days | events/day | mean λ̂ | β₁ (intensity slope) | top-decile per-fill P&L (ticks) |
|---|---|---|---|---|---|
| Full RTH ex-events | | | | | |
| Open 15 min | | | | | |
| Close 15 min | | | | | |
| FOMC 14:00–14:30 | | | | | |
| Non-FOMC control 14:00–14:30 | | | | | |
| Roll-neighbor | | | | | |

### 8. Unification — one arrival process governs three tails

**The point of this section is not novelty. The link Hawkes → fat-tailed returns is well-established** (Blanc, Donier & Bouchaud 2017 QHawkes; Bacry & Muzy 2015; Jaisson & Rosenbaum 2015-16; Hardiman & Bouchaud 2014 on E-mini S&P specifically; 2025 Econ Letters on trading intensity and extreme returns). We cite these heavily.

**The empirical contribution here is three-domain joint measurement.** The same near-critical Hawkes intensity λ̂ that we characterize in §3, that predicts the toxic per-fill markouts of §7, *also* correlates with the fat-tailed return distribution over the same window. We report all three correlations. Whether they trace back to a single hidden driver is left for a follow-up paper. The three domains:

| domain | metric | where in this paper | where in prior art |
|---|---|---|---|
| Infrastructure | packet-span, decoder latency p99 | fast_send (§Arrival) | fast_send |
| Execution | per-fill maker adverse P&L | §6-7 of this paper | Kirilenko, Baron, Bellia, Aligrithm |
| Price | return tail exponent, kurtosis | **§8 (this section)** | Blanc-Bouchaud, Hardiman-Bouchaud |

Nobody has published the three-way cross-check on a single corpus. Doing it across ES + NQ + BTC × 731 days is our specific contribution.

#### 8.1 Return-tail characterization per session per stream

For each (session, stream):
- Sample mid-price returns at fixed message-index Δ (Δ ∈ {50 msgs, 500 msgs, 5000 msgs, 5 s, 1 min, 5 min, 30 min})
- Estimate tail exponent `ν_day` via Hill estimator on |r| upper tail; also fit Fréchet to block maxima
- Estimate return kurtosis (raw and robust to outliers)
- Report all three per (session, stream, Δ)

#### 8.2 Cross-day panel — n × ν × adverse-P&L triangle

Build a 731 × 3 panel with columns: `n_day` (Hawkes branching ratio from §3), `ν_day` (return tail exponent from §8.1), `adv_pnl_top_decile_day` (mean per-fill adverse P&L in the top λ̂-decile from §7).

**Cross-sectional tests (per stream):**

- `corr(n_day, 1/ν_day)` — QHawkes predicts monotone: higher n → fatter tail (smaller ν)
- `corr(n_day, |adv_pnl_top_decile|)` — higher n → more toxic top-decile fills
- `corr(1/ν_day, |adv_pnl_top_decile|)` — the direct three-way link

**Regression:** `|adv_pnl_top_decile_day| ~ a · (1/ν_day) + b · X_controls + FE`. A significant `a` means fat-tail days *are* toxic-fill days beyond order-book controls.

**What we measure in this paper (no latent-factor fit).** For each stream (ES, NQ, BTC) and each 30-min window we compute the arrival-side pair `(n_window, λ̄_window)` and the three tail metrics. We report the 6-cell marginal-correlation table (each of 3 tails × each of {n, λ̄}) plus partial correlations to test whether `n` and `λ̄` are redundant proxies of each other. Whether all five variables share a single per-window latent driver `θ` (a Market Activation Level) is the question of a follow-up paper (see Future Work: MAL) — this paper builds the panel and reports the correlations that a future MAL paper would fit.

#### 8.3 Message-level vs trade-level subordination

Prior subordination literature (Clark 1973, Ané & Geman 2000) uses trade or volume arrivals as the subordinator that normalizes returns to Gaussian. We test:

- Subordinate returns by cumulative *message* count (Add+Modify+Cancel+Execute) vs by cumulative *trade* count
- Compare goodness-of-Gaussian on the subordinated sequence — QQ deviation, kurtosis
- Compare against subordinating by `∫ λ̂(s) ds` (the Hawkes compensator)

**Claim to test:** the message-level or compensator subordination out-performs trade-level in explaining return non-Gaussianity, because messages carry the pre-trade information that trades don't. This is a genuinely new empirical test in the subordination literature.

#### 8.4 Cross-product test — ES vs NQ vs BTC

- Report `(n_day, ν_day, adv_pnl_top_decile_day)` distributions per stream
- Compare shapes: does BTC's higher return kurtosis map to higher branching ratio, or to fatter marginal step sizes (marked Hawkes γ)?
- Test whether the same `θ_day` factor structure holds on all three products

**If it holds on all three products of very different clienteles, that's the paper's most robust claim: the arrival-process-governs-everything story is a property of near-critical financial arrivals in general, not a CME-equity-index artifact.**

#### 8.5 Explicit prior-art acknowledgment

We do NOT claim to have discovered "arrival process governs fat tails." That's Blanc-Bouchaud, Bacry-Muzy, Hardiman-Bouchaud, Jaisson-Rosenbaum. We (i) confirm their prediction on our corpus, (ii) extend to NQ and BTC where it hasn't been tested, and (iii) report per-window correlations tying the same arrival-side signals to per-fill adverse selection and decoder latency. **The three-domain joint measurement across ES / NQ / BTC × 730 sessions is the contribution; the individual bilateral links are replications.** Whether the three correlations trace back to a single latent driver is left for a follow-up paper.

### 9. Application — passive quoter with intensity gate

- Simulated passive-quote strategy: rest at BBO ± 1 tick
- Skip placement when λ̂ > θ_q (q-th percentile threshold)
- Sweep q ∈ {50, 70, 80, 90, 95, 99}
- Metrics: fill rate, mean signed markout per fill, net edge per fill, hit rate on informed side
- **Figure:** frontier — fill-rate vs avg |markout| across q values
- Optionally combined with a taker mode: aggress when λ̂ > θ' AND book_imbalance favorable

### 10. Discussion
- What kind of Hawkes describes CME MDP3? (near-critical, size-marked, cross-exciting bid/ask)
- Comparison with Bacry-Muzy microstructure Hawkes literature
- Comparison with informed-trading models (Kyle, PIN, VPIN) — VPIN is a coarser proxy of the same signal
- Limitations: unmarked H may miss stealth mid-size flow; RTH-only misses overnight regime
- Open questions:
  - Can the same estimator generalize to non-CME venues (options, spot FX, equities)?
  - Does the intensity-markout link narrow around Fed/CPI (i.e., are those events "priced in" faster)?
  - Best kernel — exp vs power-law vs sum-of-exps?

### 11. Conclusion
- CME MDP3 book feeds are near-critically Hawkes, and this property is stable across 3+ years
- Real-time intensity is a first-order predictor of adverse-selection cost
- A drop-in Python estimator enables passive quoters to skip toxic bursts at a cost measured in fill rate
- Reproducible pipeline: `.bin` → derived tapes → figures + tables + Python module

### Appendices
- A. Exp-Hawkes MLE derivation and code
- B. Marked-Hawkes gradient derivation
- C. Time-rescaling residual test details
- D. Multi-year table (731 days) — arrival stats, Hawkes params, markout decile-lifts
- E. Python module API + reproduction runbook

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

Scope for v1 is one outright front-month per stream (ES front, NQ front, BTC front). The following extensions are deliberately out of scope; each has enough interesting content to be its own paper.

- **Market Activation Level (MAL) — the latent-factor unification.** This paper measures each of the three tails and reports their marginal + partial correlations with `(n, λ̄)`. The natural follow-up is: are all five variables (the three tails + n + λ̄) shadows of a single hidden per-window market state θ_MAL? Formal statement: fit a five-observed one-hidden-factor SEM (or a MAL-augmented Hawkes with θ_MAL entering both the intensity kernel and the tail-thickness likelihoods) on the ~5000-window panel we build here. That is its own paper — it requires careful identification, sensitivity to window size, and a defensible econometric strategy for coupling arrival-side and tail-side likelihoods. The empirical panel this paper delivers is exactly what such a MAL paper would need as input.
- **Cross-excitation between related outrights on the same channel.** Chan 318 carries NQ (E-mini) alongside MNQ (Micro NQ) — a strong prior says a burst on one excites the other, both directly (arb bots cross-hitting) and through common information. Fit a bivariate marked Hawkes on {NQ, MNQ} and estimate the off-diagonal α terms; do the same on chan 310 for {ES, MES}.
- **Cross-market excitation between channels.** NQ ↔ ES is the natural pair (equity-index co-movement, common-factor risk). Same bivariate Hawkes formalism but the two streams live on different channels with different `handlerendtim` origins, so alignment needs care. Only worth doing after the within-channel micro↔full result is up.
- **Options overlay** — the CME MDP3 options feeds sit adjacent to the underlying futures feeds. Options-market activity is an obvious upstream driver of underlying-futures arrivals via delta-hedging flow. Requires a distinct feed handler.
- **Latency-tail causal experiment** — swap the decoder's memory allocator (jemalloc vs pool allocator) and re-fit; expect only the exogenous piece of the latency tail to move, not the arrival-driven queueing piece. Backs the paper's causal claim about which tail component we're measuring.
- **Cross-venue microstructure** (CME vs ICE for the same product family) — different matching engines, different orderbook rules, similar underlyings. Tests whether the arrival regularities we find are venue-specific or product-specific.

Signal we're getting good enough within-scope results to defer these: the 18-correlation marginal panel is uniformly signed and significant across streams, and per-stream Hill exponents cluster tightly by regime.

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
