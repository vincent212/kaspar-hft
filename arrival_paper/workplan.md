# Arrival-Paper Work Plan

*2026-09-18 — v@m2te.ch*

*Companion to `outline.md` and `methodology.md`. This document is the execution schedule — tasks, durations, milestones, cut-lines. Not for the article.*

---

## Target

Ship an arXiv paper (working title: *"Long Latency Tails in HFT Systems Have the Same Signature as Fat Return Tails and Adverse-Selection Tails — Evidence from CME MDP3 on ES, NQ, and BTC, with Implications for Market-Making Systems"*).

**Scope frozen 2026-09-18 (see `outline.md` §SCOPE).** Deliver: (i) three tail metrics on a ~5000-window panel co-move; (ii) all three track (n, λ̄) via marginal + partial Spearman correlations; (iii) working online O(1) intensity estimator + Shadow POV λ̂-gating backtest. NO latent-factor model (MAL is a follow-on). NO signed λ (that's another follow-on).

## Corpus

- ES chan 310, NQ chan 318 — `.bin` already extracted, `/vast/home/vmayeski/out/bin/{310,318}/`, 731 trading days 2023-01 to 2026-02.
- BTC chan 326 — `.bin` conversion running in background for 2025 only (313 days). ETA hours.

## Realistic total effort

**4-6 months of focused solo work**; **6-9 months elapsed** if day-job trading work competes for time. `"Weeks"` was the earlier lowball; ignore it.

## Milestone timeline (target dates assume focused-effort baseline)

| week | milestone | gate |
|---|---|---|
| **Week 1** | BTC decode done. `bin_to_tapes` C++ tool built and running for 3 streams. | One session (2025-03-10 NQ) end-to-end from `.bin` → `message_tape` + `packet_tape` + `bbo_tape` + `trade_tape` all present. |
| **Week 2-3** | Full-corpus tape extraction complete. Model-free stats computed per session. | Panel of daily CV / Fano / Hurst / ACF / shuffle for 2,200 sessions. Reproduces fast_send single-day numbers on the pilot day. |
| **Week 4-5** | Hawkes MLE — unmarked, marked, 2-D signed. Fitted per session. Time-rescaling KS. | Per-session JSON with `(μ, α, β, γ, n, p_KS)`. Sanity checks: `H > 0.6` on ≥90% days, `n ∈ [0.75, 0.98]` on ≥90%, `p_KS > 0.05` on ≥60%. |
| **Week 6** | Online estimator + Lindley latency simulation shipped. | Python `kaspar_arrival` module. Latency-tail scalar per session. λ̂-vs-realized-vol scatter on pilot day. |
| **Week 7-9** | Fill-tape construction from MBO L3. Attach queue features + λ̂. Per-fill markouts at 6 horizons. | fill_tape parquet per session. Validation vs a hand-checked sample of ~200 fills. |
| **Week 10** | Return-tail Hill estimation per session. | `1/ν_day` and `1/ν_hour` in the panel. |
| **Week 11** | Full per-window panel assembled — ~5000 rows × 3 streams, five tail metrics per row. **Result 1** (30 cross-tail Spearman correlations, 10 pairs × 3 streams) computed. **Result 2** (60-cell arrival-side attribution) computed. Robustness across window sizes. | Numerical result: signs + magnitudes of all 90 correlations. |
| **Week 12** | Cross-product analysis (ES/NQ/BTC). Regime splits (open/close/FOMC). Passive-quoter frontier. | All figures 1-11 drafted. |
| **Week 13-16** | LaTeX drafting. Companion `.md` article (like `md_latency_article.md`). Internal review. arXiv submission. | Paper submitted. |

If elapsed to Week 20 with no submission → cut line applies (see below).

---

## Task decomposition (with realistic hours)

### Group A — Corpus infrastructure

| # | task | est hrs | parallel? | risk |
|---|---|---|---|---|
| A1 | BTC .bin conversion (2025 only) | 8 (running) | yes | low |
| A2 | Extend `binstats.cpp` → `bin_to_tapes.cpp` — emit `message_tape` with (t_match, t_send, t_recv, msg_type, side, price, size, orderID, idx_in_packet) | 40 | no | medium — C++ record layout |
| A3 | **BBO reconstruction** — maintain top-of-book state through MBO events; emit `bbo_tape` aligned with events. This is the hardest single piece — a proper order-book state machine in C++. | 60-80 | no | high — bugs are subtle |
| A4 | `trade_tape` extraction with maker order_id lookup | 12 | no | low |
| A5 | Full-corpus batch driver (parallel across days × streams) | 8 | no | low |
| A6 | Verification: run pilot day end-to-end, reconcile with fast_send single-day stats | 16 | no | medium |
| **A subtotal** | | **~150-170 hrs (~4 weeks)** | | |

### Group B — Model-free session stats

| # | task | est hrs | parallel? | risk |
|---|---|---|---|---|
| B1 | Python module `arrival_paper.stats`: CV, CV², P(gap<mean/10), Fano, Hurst, ACF | 16 | no | low |
| B2 | Fisher-Yates shuffle collapse test | 8 | no | low |
| B3 | Run across 2,200 sessions in parallel; emit `daily_stats.json` per session | 8 | yes | low |
| B4 | Reproduce fast_send numbers on the pilot day as regression test | 8 | no | low |
| **B subtotal** | | **~40 hrs (~1 week)** | | |

### Group C — Hawkes MLE

| # | task | est hrs | parallel? | risk |
|---|---|---|---|---|
| C1 | Unmarked exp-Hawkes MLE with analytic gradient (`arrival_paper.hawkes.fit_expo`) | 24 | no | low |
| C2 | Marked Hawkes with size γ | 20 | no | low |
| C3 | 2-D signed (buy/sell pressure) Hawkes with 2×2 excitation | 32 | no | medium — 6-parameter MLE, potential local optima |
| C4 | Time-rescaling KS + Ljung-Box residual tests | 12 | no | low |
| C5 | Warm-start from Fano-scaling method-of-moments | 8 | no | low |
| C6 | Run all three variants across 2,200 sessions × 3 streams | 16 | yes | medium — quiet sessions may fail to converge; need retry/flag logic |
| **C subtotal** | | **~110 hrs (~2.5 weeks)** | | |

### Group D — Online estimators

| # | task | est hrs | parallel? | risk |
|---|---|---|---|---|
| D1 | 1-D online intensity estimator (`arrival_paper.online.HawkesIntensityEstimator`) with `O(1)` update | 12 | no | low |
| D2 | 2-D signed online estimator | 20 | no | low |
| D3 | Sanity check on synthetic Hawkes (recover fit params) | 8 | no | low |
| **D subtotal** | | **~40 hrs (~1 week)** | | |

### Group E — Latency simulation

| # | task | est hrs | parallel? | risk |
|---|---|---|---|---|
| E1 | Lindley recursion Python implementation | 8 | no | low |
| E2 | Fast_send (floor, slope) point estimates per stream | 4 | no | low |
| E3 | Run Lindley per session across corpus → `p50/p90/p99/p999/p9999_lat` per session | 8 | yes | low |
| E4 | Shuffle robustness — Lindley on shuffled arrival tape → `latency collapse ratio` per session | 4 | yes | low |
| E5 | Sensitivity sweep on (floor, slope) — appendix | 8 | no | low |
| **E subtotal** | | **~32 hrs (~1 week)** | | |

### Group F — Fill tape (hardest, most novel)

| # | task | est hrs | parallel? | risk |
|---|---|---|---|---|
| F1 | One-pass Python (or C++) builder: `order_id → (add_ts, add_price, side, size, position_in_queue)` hashmap over `message_tape` | 40 | no | medium |
| F2 | Handle edge cases: multi-fill executes, iceberg reveals, modify-mid-queue (priority reset), cancel-that-becomes-execute, hidden liquidity | 40 | no | high — CME semantics are fiddly, easy to miscount |
| F3 | Attach `size_ahead_at_submit`, `n_orders_ahead_at_submit`, `size_remaining_at_fill`, `n_orders_behind_at_fill` from bbo/book_state | 24 | no | medium |
| F4 | Attach `λ̂_at_submit`, `λ̂_at_fill`, `λ̂_delta`, `dq_ahead_rate` per fill via online estimator | 16 | no | low |
| F5 | Per-fill adverse P&L at τ ∈ {100ms, 1s, 10s, 30s, 100 evts, 500 evts} using bbo_tape mid | 16 | no | low |
| F6 | Validate against hand-checked ~200-fill sample from a single 5-minute NQ window | 20 | no | medium |
| F7 | Run across 2,200 sessions × 3 streams in parallel | 12 | yes | medium |
| **F subtotal** | | **~170 hrs (~4 weeks)** | | |

### Group G — Return-tail estimation

| # | task | est hrs | parallel? | risk |
|---|---|---|---|---|
| G1 | Reconstruct 1-second and 5-second mid-price returns per session from bbo_tape | 12 | no | low |
| G2 | Hill estimator on |log-returns| upper tail; Fréchet fit on block-maxima | 16 | no | low |
| G3 | Sensitivity to Hill `k` and Fréchet block-size choices | 8 | no | low |
| G4 | Run across corpus | 4 | yes | low |
| **G subtotal** | | **~40 hrs (~1 week)** | | |

### Group H — Panel + correlations (no SEM, no MAL — see scope in outline)

| # | task | est hrs | parallel? | risk |
|---|---|---|---|---|
| H1 | Assemble per-window panel: ~5000 rows × 12 columns per stream (window_id, session, n, λ̄, five tail metrics — ME p99, send-recv p99, decoder p99, log\|adv_pnl\|_p95, 1/ν —  event_count, gate_flag) | 14 | no | low |
| H2 | Activity gate: drop bottom 10% by event count; sensitivity at 5% and 20% | 4 | no | low |
| H3 | Roll-day drop: exclude ± 1 day around volstats-vs-DB mismatch flags | 2 | no | low |
| H4 | **Result 1 — cross-tail co-movement**: 30 pairwise Spearman correlations (10 tail pairs × 3 streams) + 5×5 cross-tail correlation heat-map per stream | 10 | no | low |
| H5 | **Result 2 — arrival-side attribution**: 60-cell table of marginal + partial Spearman for each of (5 tails × {n, λ̄}) × 3 streams | 10 | no | low |
| H6 | Robustness: rerun H4 + H5 at 5-min, 15-min, 30-min, 60-min window sizes; report stability of signs and rough magnitudes | 12 | no | low |
| H7 | Cross-product comparison: sign and magnitude consistency of the 90 correlations across ES / NQ / BTC | 8 | no | low |
| H8 | Regime splits (open / close / FOMC vs matched controls) — same 90 correlations per regime | 20 | no | medium |
| **H subtotal** | | **~80 hrs (~2 weeks)** | | |

*Deleted from earlier plan: MAL SEM fit, two-factor SEM, partial-out-volume + re-fit SEM residuals, stratified within-decile SEM regressions. These are follow-on paper tasks per the outline's SCOPE section.*

### Group I — Application (passive-quoter frontier)

| # | task | est hrs | parallel? | risk |
|---|---|---|---|---|
| I1 | Compute fill-rate + avg adverse-markout curve as function of `λ̂` threshold `θ` — **total intensity only** | 12 | no | low |
| I2 | Cross-product comparison ES / NQ / BTC of the fill-rate-vs-toxicity frontier | 8 | no | low |
| I3 | Shadow POV λ̂-gate backtest: replay a POV algo on the fill_tape with and without a λ̂ > θ block-quote gate; report ticks-saved-per-fill × slippage | 12 | no | low |
| **I subtotal** | | **~32 hrs (~1 week)** | | |

*Deleted from earlier plan: signed-λ̂ directional variant (I2 old). That's the follow-on signed-Hawkes-alpha paper.*

### Group J — Writing

| # | task | est hrs | parallel? | risk |
|---|---|---|---|---|
| J1 | LaTeX draft (reuse fast_send preamble/style) | 60 | no | low |
| J2 | Companion `md_arrival_article.md` (fast_send-style plain-text) | 30 | no | low |
| J3 | Figures 1-12: matplotlib scripts + saved PNGs | 40 | partial | low |
| J4 | Prior-art review write-up | 20 | no | low |
| J5 | Internal review + iteration + arXiv submission | 40 | no | low |
| **J subtotal** | | **~190 hrs (~5 weeks)** | | |

### Grand total

| group | hours |
|---|---|
| A — corpus infra | ~160 |
| B — model-free stats | ~40 |
| C — Hawkes MLE | ~110 |
| D — online estimators | ~40 |
| E — latency sim | ~32 |
| F — fill tape | ~170 |
| G — return tail | ~40 |
| H — panel + correlations (no SEM) | ~80 |
| I — application | ~32 |
| J — writing | ~190 |
| **Total** | **~890 hrs** |

At **35 hrs/week focused** → **~26 weeks = 6 months** elapsed.
At **20 hrs/week competing with day-job** → **~45 weeks = 10 months** elapsed.

---

## Parallelization opportunities

- **A2 (msgtape) can run while A3 (BBO) is in development** — msgtape needs no BBO state.
- **B (model-free stats) can run as soon as A2 finishes** — doesn't need BBO or fits.
- **C (Hawkes MLE) can run in parallel with B** on the message_tape.
- **D (online estimator)** can be written and unit-tested independently of the corpus; only integration needs C's outputs.
- **E (Lindley)** independent of C, D, F.
- **F (fill tape)** needs A3 (BBO) — this is the critical path.
- **G (return tail)** needs A3 (BBO) for mid-price reconstruction, otherwise independent.
- **H (panel + correlations)** is downstream of everything.
- **J (writing)** starts in parallel with C/D/E once the introduction and methodology sections are drafted.

Critical path: **A1 → A2 → A3 → F → H → J**. That's the tightest sequential chain and defines the schedule.

## Delegation candidates (subagent or fork)

- **A3 BBO reconstruction** — clean C++ engineering task with well-defined semantics. Hand to a coding subagent with the r_l3.hpp record layout and CME order-book conventions.
- **B model-free stats** — pure-function Python. Can be handed to a fresh agent with the panel schema.
- **G Hill estimator + Fréchet fit** — well-scoped statistical task. Delegate.
- **J3 figure scripts** — one-day-per-figure sub-tasks. Delegate to a data-viz-capable agent one at a time.

Everything else — MLE numerics, fill-tape semantics, correlation-panel interpretation, cross-tail figure design — stays with you because it's judgment-heavy.

## Risks + mitigations

| risk | probability | impact | mitigation |
|---|---|---|---|
| Hawkes MLE fails to converge on many sessions | medium | medium | use MoM warm-start; retry with bounds; flag failed sessions rather than drop |
| BBO reconstruction has subtle bugs | medium | high | hand-check a 5-min window against a Databento reference; unit-test edge cases |
| Fill-tape semantics wrong on iceberg/modify | high | high | validate against a small hand-checked sample; consult CME MDP3 semantics doc |
| Cross-tail correlations weak or one-sign-flipped on a stream | medium | medium | report the null honestly; the paper still stands on the pairwise-established priors (Hardiman-Bercot-Bouchaud, Filimonov-Sornette, etc.); the "three-way joint on same panel + 3 products" contribution survives even if one cell is flat |
| Decoder-latency-tail measurement noisy (handlerendtim quantization, replay-mode artifacts) | medium | high | use a hand-checked pilot day; report the noise level and the SNR; if too noisy, revert to using packet-span instead of decode time |
| BTC results very different from ES/NQ | medium | medium | report as regime effect, don't force one story |
| Compute overrun (multi-day sessions or 731-day scale takes weeks) | low | medium | already have 72-core box + terabytes of scratch; monitor |
| Day-job trading competes for hours | high | high | reserve fixed weekly hours (say Mon/Wed/Fri mornings); protect them |
| Scope creep (like adding MAL after Step D was already scoped) | high | medium | freeze scope at end of Week 3; new ideas go to a "v2" list |

## Cut lines (in order of what to drop if timeline slips)

If elapsed hits Week 20 with no submission and no Week-16 milestone hit:

1. **Drop BTC** — ES + NQ only. Saves ~2 weeks of the corpus infra and analysis.
2. **Drop marked Hawkes and 2-D signed Hawkes** — unmarked only. Saves ~1.5 weeks.
3. **Drop MAL SEM** — publish A/B/C/E/F as *"Non-Poisson arrival processes and adverse selection in CME futures"* — a solid microstructure paper without the unification headline. Saves ~3 weeks. MAL becomes a follow-up paper.
4. **Drop hourly SEM** — daily-only. Saves ~1 week but weakens the paper substantially.
5. **Drop latency sim** — reference fast_send's numbers and don't try to simulate. Removes the third leg of the SEM entirely; effectively kills the "three tails" framing. Do this only as a last resort.

## Definition of done

- arXiv upload with a compilable PDF
- Companion `md_arrival_article.md` in `tech_reports/`
- `kaspar_arrival/` Python module published under MIT with a README and one-command reproduction of the pilot day
- All figures reproducible from data in `/vast/home/vmayeski/out/arrival_paper/`

## What to do next (this week)

1. **Verify BTC .bin conversion completes** — check daily. Should be done in hours.
2. **Draft `bin_to_tapes.cpp` schema** — decide on tape record layouts before writing C++.
3. **Kick off A2** (msgtape extension) as this week's first coding task.
4. **Delegate A3 BBO reconstruction to a subagent** — spec + prior-art C++ reference from `frame_kaspr/src/OB.cpp` or `TachBook`; ask the agent to build a minimal `bbo_emit.hpp` that maintains state and emits per-event BBO records.
5. **Freeze scope** — this document is the scope. Any new ideas go to a `v2_ideas.md` sidecar.

## Cadence

- **Daily**: 10-minute standup with self — what shipped yesterday, what's next, what's blocked.
- **Weekly**: check milestone table above; slip any milestone by more than 3 days → escalate (either cut scope or add hours).
- **Every 4 weeks**: reread this document and update dates if scope has drifted. Commit the delta to git.

---

*This work plan is a scope-and-time commitment. If you find yourself wanting to add something (another product, a fancier Hawkes variant, a different SEM architecture), it goes to `v2_ideas.md`, not into this scope. Scope creep is the #1 risk after Week 4.*
