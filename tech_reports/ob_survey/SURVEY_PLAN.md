# Survey plan — Order Book Information, Execution, and Model Discovery

Working title: *From Limit-Order-Book Information to Executable Decisions: Signals, Queues,
Simulation, and LLM-Assisted Model Discovery*

Status: planning document. Every section below lists what it covers, one worked example, and its
sources. Numbers marked **(illustrative)** are made up to show the arithmetic and must not appear
in the paper as results. Numbers with a `file:line` come from the Kaspar repo and were checked
against the code on 2026-09-30.

**Scope: theoretical and methodological paper. No new simulations or experiments are run for it.**
- Examples are worked arithmetic on stylised numbers, labelled illustrative.
- Tables with empty numeric columns are *reporting templates the paper recommends*, not results to
  be filled in.
- Proposed experiments (ablations, null tests, determinism checks) are stated as recommendations
  or open problems.
- Kaspar appears as an architecture and a method: how the code works, cited by file. The only
  empirical numbers quoted are ones already in existing reports (`md_latency_article.md`,
  `shadow_pov.tex`), attributed to them.
- LAMD is described as a design. There are no LAMD results.

---

## 0. Thesis

> How does information contained in the limit order book become an executable trading decision,
> and how should prediction, queue dynamics, simulation, and execution be evaluated together?

The spine that every section hangs on:

```
LOB state → representation → prediction → decision → queue → execution → P&L
```

Each arrow can destroy information or add uncertainty. The paper's claim is not that simple models
beat deep ones. It is that a model is only as good as what survives the full chain, measured on
real market data, against a null that reflects how many candidates were searched.

Three fixed positions:

1. **The market is observed, not generated.** Real exchange data is ground truth. The simulator
   replays and reconstructs it. It does not invent market behaviour. It only produces
   counterfactual *execution* outcomes for our own orders.
2. **LLMs generate hypotheses, not data.** In LAMD the LLM proposes candidate models. Fitting,
   replay, validation and certification are deterministic and outside the LLM.
3. **Complexity is an empirical hypothesis.** Each added layer (model capacity, hardware, search
   breadth) must earn incremental out-of-sample economic value.

Name decision: **LAMD — LLM-Assisted Model Discovery** (replaces LATSS).

## 0.1 Master diagrams

```
                 LIMIT ORDER BOOK
                        │
          ┌─────────────┴─────────────┐
     REPRESENTATION                EVENT FLOW
   OFI / imbalance             Hawkes / queues
   snapshots                    first passage
   embeddings                   cancellations
          └─────────────┬─────────────┘
                  PREDICTIVE MODEL
       ┌────────────────┼────────────────┐
    direction        fill prob.      adverse selection
       └────────────────┼────────────────┘
                  TRADING DECISION
                        │
                 QUEUE POSITION
                        │
                  EXECUTION MODEL
              ┌─────────┴─────────┐
          SIMULATION             LIVE
         replay of recorded     CME MDP3
         MBO, reconstructed     iLink 3
         order book
              └─────────┬─────────┘
                       P&L
```

```
   HUMAN-SPECIFIED VOCABULARY (OFI, QI, queue, trades, Hawkes, regime, cross-asset)
                             │
                        LLM proposes model
                             │
                 deterministic compiler → parameter fit
                             │
             Kaspar replay on REAL recorded data (queue-exact fills)
                             │
            walk-forward OOS → statistical certification (N_trials)
                             │
                        model archive ──► mutate / iterate
```

---

## Part I — The problem

### 1. Introduction: the LOB as a dynamic market state
- **Covers:** the L-level book L_t, mid and spread, and what they throw away. Event-driven data,
  high dimension, non-stationarity.
- **Example:** two ES books with identical mid (5000.125) and spread (1 tick) but different
  depth, 12 vs 900 lots at the best ask. Mid and spread are identical, yet the cost of lifting
  100 lots differs by several ticks. **(illustrative)**

### 2. Prediction versus tradeability
- **Covers:** P(Δm > 0 | X) is not P&L. Aggressive orders pay the spread. Passive orders may not
  fill, and the fill itself carries information.
- **Example (illustrative).** ES, tick = 0.25 pt. A classifier is 55% right on the sign of the
  next mid move, and the typical move is half a tick (0.125 pt).
  - Expected move per trade: (0.55 − 0.45) × 0.125 = **0.0125 pt**
  - Aggressive entry cost: half spread = **0.125 pt**, plus fees
  - Net: a loss of about 10× the edge. The accuracy is real and the trade is untradeable.
  - The same signal used to decide *which resting passive order to keep or cancel* has no spread
    to pay. Whether it helps is an execution question (Part III), not an accuracy question.
- **Literature hook: selective prediction (UQ-LOB, Manoharan, Linna, Baltakys, Dong & Kanniainen,
  arXiv:2609.31491, 2026).** A model that says *when* to trust its forecast changes the
  tradeability arithmetic. Trading only the most confident tier raises the edge per trade, and the
  number of trades falls. UQ-LOB reports that restricting to the most confident 10% raises
  directional macro F1 by 0.11–0.15 (UQ-regression) on 7 crypto assets at 5/10/15 s horizons.
  - The illustrative arithmetic to add: if the confident tier moves 55% accuracy to, say, 70% on
    10% of signals, the edge per trade quadruples (0.10 → 0.40 of |Δm|). Whether that clears the
    half spread still depends on |Δm| for those events.
  - UQ-LOB's result on "large, economically meaningful moves" speaks to exactly this.
  - Read the full paper before stating whether it includes a cost-aware evaluation. The abstract
    reports F1 and coverage only.

### 3. Prediction, execution, and simulation are different problems
- **Covers:** P(Y_{t+H} | X) versus P(Fill | X, Q_ahead, a) versus P(X_{t+1:t+H} | X).
- **Example.** One event stream (a day of ES MBO), three models, three metrics:

  | Model | Target | Right metric | Wrong metric |
  |---|---|---|---|
  | Hawkes, 4 event types | event intensity | log-likelihood, residual KS test | directional accuracy |
  | Survival model | P(fill ≤ 1 s \| q) | survival likelihood, calibration | mid-move AUC |
  | Classifier | P(Δm_H > 0) | log loss, AUC, then P&L | fill rate |

### 4. Positioning and taxonomy
- **Covers:** three traditions (econometric, point-process/queueing, ML); four axes
  (representation × model class × objective × economic role); Class 4 / Class 5 shorthand.
- **Example:** place four papers on the four axes in a table (fill in once the bibliography is
  verified). DeepLOB goes under snapshot × CNN-LSTM × direction × aggressive. A queue-reactive
  model goes under queue state × Markov queue × depletion × simulation/execution.

### 5. Survey methodology
- **Covers:** scope; evidence classes (established / synthesis / recommendation / open
  hypothesis); what makes two LOB studies comparable (the 15-field list from the draft, §2.3).
- **Example:** two hypothetical "65% accuracy" results. One is on FI-2010 (5 Nasdaq Nordic stocks,
  10 days, horizon k = 10 events, 3-class). The other is on BTC-USDT at a 1 s calendar clock,
  2-class. The table shows they share almost no comparability fields. **(illustrative)**

---

## Part II — Models of the LOB

### 6. Order-flow imbalance and queue imbalance
- **Covers:** OFI (the draft form, and the full Cont–Kukanov–Stoikov form that handles price-level
  changes), Δm = β·OFI, β versus depth, QI and multi-level variants.
- **Example: OFI by hand (illustrative, best level unchanged throughout).**

  | n | event | Δv_bid | Δv_ask | e_n = Δv_bid − Δv_ask |
  |---|---|---|---|---|
  | 1 | bid add 20 | +20 | 0 | +20 |
  | 2 | ask cancel 15 | 0 | −15 | +15 |
  | 3 | buy MO hits ask for 10 | 0 | −10 | +10 |
  | 4 | ask add 30 | 0 | +30 | −30 |
  | 5 | bid cancel 5 | −5 | 0 | −5 |
  |   | **OFI** | | | **+10** |

  Events 2 and 3 contribute +15 and +10 through the same arithmetic, yet one is a cancel and one is
  an aggressive trade. This leads into §7 (event taxonomy). β example: +10 lots of OFI moves a
  12-lot thin book far more than ES's typical hundreds of lots, so β scales roughly with
  1/depth. Cite CKS for the empirical form.
- **Example: QI.** Bid 300, ask 100 → QI = (300 − 100)/(300 + 100) = **0.5**. Book A reached
  ask 100 from 400 via a 300-lot aggressive buy. Book B reached it via 300 lots of cancels. Same
  QI, different information. A snapshot model cannot tell them apart.
- **Kaspar note:** there is no OFI, QI, or microprice computation in the Kaspar C++ today. These
  are survey content, not system features.

### 7. Event taxonomy and why events beat snapshots
- **Covers:** add / execute / cancel. The same Δv from different causes. MBO (L3) versus MBP.
- **Example:** the Book A / Book B pair above, shown as the two message sequences an MBO feed
  delivers (CME MDP3 template 47 order-book updates versus template 48 trade summaries).
- **Literature hook: LOBERT (Linna, Baltakys, Iosifidis & Kanniainen, arXiv:2511.12563, NeurIPS
  2025 GenAI in Finance workshop).** It is the direct deep-learning form of the "events beat
  snapshots" argument. Each complete multi-dimensional message is one token, with continuous
  price, volume and time. It reports leading performance on mid-price movement and next-message
  prediction with shorter required context than earlier methods. In the paper, pair it with the
  Book A / Book B example: a message-level encoder sees the difference between a cancel and an
  execution, and a snapshot encoder does not.

### 8. Queue and first-passage models
- **Covers:** the depletion time τ = inf{u : Q_{t+u} ≤ 0}; constant-rate, state-dependent and
  queue-reactive intensities.
- **Example (illustrative).** Best-ask queue 200 lots. Net depletion (executions + cancels −
  adds) runs at 40 lots/s, so naive E[τ] = 200/40 = **5 s**. Condition the rate on OFI: with a
  positive OFI decile the rate is 90 lots/s and E[τ] ≈ 2.2 s; with a negative decile it is 15
  lots/s and E[τ] ≈ 13 s. Same queue, a 6× range, which is why state dependence matters.

### 9. Hawkes processes
- **Covers:** multivariate intensity, branching matrix Γ, stationarity ρ(Γ) < 1, mean intensity,
  near-criticality (stated within its asymptotic assumptions), state-dependent extension
  λ_i^H · exp(γ_iᵀ X).
- **Example (illustrative).** Two types (buy MO, sell MO), μ = (1, 1) events/s,
  Γ = [[0.6, 0.2], [0.2, 0.6]].
  - The eigenvalues are 0.8 and 0.4, so ρ(Γ) = **0.8** < 1, which is stationary.
  - I − Γ = [[0.4, −0.2], [−0.2, 0.4]], det = 0.16 − 0.04 = 0.12.
  - (I − Γ)⁻¹ = (1/0.12)·[[0.4, 0.2], [0.2, 0.4]].
  - λ̄ = (I − Γ)⁻¹μ = (0.6/0.12, 0.6/0.12) = **(5, 5) events/s**.
  - So 80% of activity is endogenous (1 of 5 events is exogenous). This is good for simulation and
    says nothing about direction by itself.
- **Kaspar pointer:** `arrival_paper/hawkes_correlations.py` (branch `research/arrival-paper`)
  relates Hawkes intensity to |markout_1s|. That relationship is an execution use, not a direction use.

### 10. Deep learning: CNN → LSTM → Transformer → spatial-temporal → state-space
- **Covers:** the architectural progression (not monotone improvement); representation beats
  backbone; target zoo (P(Δm > 0), E[Δm], P(Δm > θ), P(T_move ≤ H), P(T_fill ≤ H), E[Π]).
- **Foundation encoders and uncertainty (the Tampere line of work, Kanniainen group).** Present
  these as two layers:
  - LOBERT is a pretrained, fine-tunable message-level encoder.
  - UQ-LOB is an encoder-agnostic module that attaches to a pretrained LOB encoder and outputs a
    distribution rather than a point forecast. UQ-regression gives a Gaussian over tick
    displacement; UQ-classification gives down/up/stationary probabilities.

  This moves the target from "direction" to "direction plus confidence", which is the input an
  execution policy needs (§13–§15). Check in the full text whether UQ-LOB uses LOBERT as its
  encoder before saying so.
- **Example: a baseline reporting template** the paper recommends every deep-model study fill in
  (template only; this paper does not fill it in):

  | Model | Input | Params | OOS log loss | OOS net P&L | Latency |
  |---|---|---|---|---|---|
  | Logistic | OFI_1..5, QI | ~10 | | | ns |
  | DeepLOB-style CNN-LSTM | 10-level snapshots × 100 | ~10⁵ | | | µs–ms |
  | Transformer | raw snapshots | ~10⁶ | | | ms |
  | Transformer | event stream + OFI | ~10⁶ | | | ms |

  The comparison that matters is row 3 versus row 1, not row 3 versus row 2.

### 11. Hybrid models
- **Covers:** OFI plus embeddings, Hawkes intensities as inputs, statistical baseline plus neural
  residual.
- **Example: an ablation template.** M0(X_t) versus M1(X_t, Z_t) with Z_t = Hawkes intensity
  computed from the same events already in X_t, using identical splits, seeds, and economic
  evaluation. Stated as a recommended experimental design, with an argued (not tested) prediction:
  ΔV ≈ 0 when Z is a deterministic function of X and the network has enough context. Studies
  should report ΔV with a confidence interval, not a single number.

### 12. Cross-asset models
- **Covers:** Δp_i = Σ_j β_ij OFI_j; futures/cash, ES/NQ, ETF/constituents; the multiple-testing
  burden.
- **Example (illustrative).** Δp_NQ = β₁·OFI_NQ + β₂·OFI_ES. Scale it to 10 related
  instruments × 10 predictors × 5 lags = **500 coefficients**. At a 5% level, about 25 are
  "significant" under the null alone. Report OOS improvement, economic value, added latency, and
  regime stability for each.

---

## Part III — From prediction to execution

### 13. Passive versus aggressive
- **Covers:** Π_agg = Δm − spread − fees − impact; Π_pass = 1_fill·(Δm + rebate − adverse).
- **Example:** continues from §2. With the same signal, the aggressive expectation is negative. The
  passive expectation is P(Fill)·E[Π | Fill], which needs §14–§15.

### 14. Queue position and fill probability
- **Covers:** residual queue Q_ahead, price-time priority, P_F(q, H) versus P(Δm > 0).
- **Example (illustrative).** Two resting bids at the same price, one with 20 lots ahead and one
  with 480. Net depletion is 40 lots/s. Naive time-to-front: **0.5 s** versus **12 s**. Both have
  identical P(Δm > 0 | X). With H = 5 s the first almost surely fills and the second almost surely
  does not.

### 15. Adverse selection
- **Covers:** R^fill = m_{t+H} − p_t; AS(q, H) = −E[R | Fill, Q_ahead = q]; conditioning on the
  fill is essential; queue position as an information variable.
- **Example (illustrative), E[Π] = P(Fill)·E[Π | Fill], in ticks:**

  | Position | P(Fill, 10 s) | spread capture | markout given fill | E[Π \| Fill] | E[Π] |
  |---|---|---|---|---|---|
  | Front | 0.80 | +0.5 | −0.20 | +0.30 | **+0.24** |
  | Back | 0.25 | +0.5 | −0.45 | +0.05 | **+0.0125** |

  This table shows the decomposition. The *sign* of the relationship between queue rank and
  markout is an empirical question. The draft claims a front fill "can be more informative". That
  is an open hypothesis, and the literature on queue-position value (e.g. Moallemi–Yuan) must be
  checked before the claim goes in.
- **Kaspar source:**
  - The markout definition is `markout = maker_side * (fwd_mid_native - exec_price_native)` in
    `arrival_paper/fill_tape.py` (branch `research/arrival-paper`, lines ~399 and ~412), at fixed
    times and after k book events.
  - The mark-out table is in `tech_reports/shadow_pov.tex:1103ff`.

### 16. Latency and signal decay
- **Covers:** T_lat = T_signal + T_decision + T_network + T_exchange; the relevant object is
  E[Π | X, T_lat, policy].
- **Example (illustrative):** signal value decays as exp(−t/τ) with half-life 2 ms, so
  τ = 2 ms/ln 2 ≈ 2.9 ms. At 100 µs total latency, 97% of the value remains. At 5 ms, 18% remains.
  The same accuracy yields very different economics.

### 17. Replay and deterministic simulation (the method)
- **Covers:** why "signal → fill at next mid" is not a backtest of a passive strategy; what a
  queue-exact replay requires (MBO data, order-level book, latencies, an explicit fill rule, and a
  stated no-impact assumption).
- **Example: two backtests of the same passive signal.**

  ```
  naive:   signal at t → assume filled at bid → P&L = m_{t+H} − bid
  replay:  signal at t → order leaves at t → arrives t + feed + order delay
           → appended at tail of the real FIFO → filled only when recorded
             executions reach an order behind it → P&L from actual fill time
  ```

  The naive version fills every order, including the ones the market would never have reached. It
  is biased exactly toward the adverse-selection-free fills that do not exist.

---

## Part IV — Kaspar HFT: an integrated research and production architecture (case study)

All claims in Part IV are code-backed. Line numbers are from `main` as of 2026-09-30.

### 18. Architecture overview
- **Covers:** MDP3 multicast → SocketReader → MsgBuf → MessageProcessor → handler_if → OB/TachBook
  → light22 → SOM → [iLink | sim fill]; the actor framework (message passing, Groups, fast_send);
  reference to `tech_reports/fast_send.tex`.
- **Example:** the data-flow line above as a figure, with one ES add message traced through it.

### 19. Queue-exact simulated fills
- **Covers:** how a simulated order enters the *actual recorded queue* and when it fills.
- **How the code does it:**
  - There is no estimated queue-ahead variable. Each price level is an `OrderQ`
    (`frame_kaspr/include/frame/ob/OrderQ.hpp`) holding every real and simulated order
    individually, in arrival order. Queue position *is* list position.
  - **Placement.** In sim mode, SOM turns the new order into a book ADD tagged venue `SIM` and
    sends it to the OB actor (`frame_kaspr/src/SOM.cpp:1345ff`).
  - **Latency.** OB holds it until its modelled arrival (`frame_kaspr/src/OB.cpp:2444-2448`):
    `order_leave_time + feed_delay + max(40, eff_delay)` µs, with a 40 µs floor. Order, cancel,
    and feed delays are independent flags (`sim/src/main.cpp:163-187`).
  - **Insertion.** At the tail, behind all resting depth (`OrderQ.hpp:195-199`). Simulated orders
    are not counted in real book size (`OrderQ.hpp:214-218`).
  - **Fill rule.** A fill happens when a recorded execution hits the first *real* order in the queue
    and a simulated order sits ahead of it (`OB.cpp:1235-1251`, `get_head_no_sim`,
    `OrderQ.hpp:54-64`). Leftover size rolls to earlier simulated orders (`OB.cpp:1312-1343`).
    `shadow_pov.tex:756-761` states the rule: aggression that reached an order *behind* the
    shadow must have passed through it.
  - **No self-reaction.** Simulated orders are excluded from the BBO, so a strategy cannot react to
    its own quote (`OB.cpp:184-193`).
  - **No impact.** Stated explicitly in `shadow_pov.tex:763-767`.
- **Worked trace (illustrative sizes):**

  ```
  t0  queue @ 5000.00 bid:  R1(10) R2(5) R3(20)
  t1  our SIM(3) arrives:   R1(10) R2(5) R3(20) S(3)          ← tail
  t2  R2 cancels:           R1(10) R3(20) S(3)                ← S moves up
  t3  exec 10 vs R1:        R3(20) S(3)     R1 is head real; nothing sim ahead → no fill
  t4  exec 20 vs R3:        S(3)            R3 head real; S is behind it → no fill
  t5  R4(8) joins:          S(3) R4(8)
  t6  exec 5 vs R4:         R4 is head real, S is ahead of it → S filled 3 @ 5000.00
  ```

  At t6, aggression reached R4, which is behind S, so it must have passed through S. Note that at
  t3–t4 the naive "touch = fill" backtest would already have filled S.

### 20. Determinism
- **Covers:** one Group thread runs book, timer, SOM, lights, position manager, and file reader in
  one message sequence. Timers run on market time. The RNG is seeded.
- **Sources:** `sim/src/SimKaspr.cpp` (the `sim_group` is built at line 79; actors are added
  through line 569, reader last); `STRATEGY_SIMULATOR_GUIDE.md:84-85` ("The `Timer` actor is
  driven by market-data timestamps, which is what makes replay deterministic"); per-light
  `rng_seed` (`light/include/light/act/light22.hpp:206-216`, `sim/config_a/lights.ini`:
  `rng_seed 1`).
- **Example:** state the determinism property and the test that would check it: replaying the same
  `.bin.gz` twice with the same config should produce byte-identical fill tapes. The paper
  argues this from the design (one thread, market-time clock, seeded RNG). It does not claim the
  test was run for this paper.

### 21. Replay / paper / live equivalence
- **Covers:** everything downstream of MsgBuf is identical regardless of source; one branch in SOM
  decides sim fill versus exchange.
- **Source:** `if (sim_mode)` at `SOM.cpp:1345`; the real-order path to the order manager (iLink)
  is at `SOM.cpp:1378-1397`. Both paths share the same reject checks (`internal_reject`,
  `SOM.cpp:1405-1515`), except one live-only rule for cancels on unacked orders.
- **Must be stated in the paper:**
  - The shipped `kaspr` binary passes `true` for sim_mode (`kaspr/src/kaspr.cpp:356`), so it runs
    paper trading. iLink session and credential setup is still to do.
  - PCAP replay is not reachable from the `kaspr` command line. The working replay path is `sim/`
    over `.bin.gz` files.

### 22. Shadow-PPOV as a model-free execution benchmark
- **Covers:** the light places at the price of a third-party ADD, records that order's exchange
  ID, and cancels when that order leaves. There is no prediction of book evolution. It is the
  bottom rung of the hierarchy model-free → predictive placement → full predictive policy.
- **Sources:**
  - Shadowed order ID: `light22.hpp:535`. Bank-wide dedup: `:543-548`.
  - Stochastic placement gate (`place_rate_bp`, default 300 = 3% of ADDs): `light22.hpp:571-581`.
  - Grace period `delayed_cancel_events` / `delayed_cancel_ms`: `light22.hpp:49-64`. The comment
    there reads: "cancel too quickly and we give up fills... too slowly and we wear the adverse
    selection".
- **Example config (from `sim/config_a/lights.ini`):**

  ```
  nlevels 5   lev_orders_max 10   ord_sz 10   nlights_per_side 10
  place_rate_bp 300   delayed_cancel_events 5   max_dist 10   max_dist_cancel 12
  rng_seed 1
  ```

- **Why it belongs here:** it is the null model for passive placement. Any predictive placement
  policy in §2 or §13 must beat Shadow-PPOV's slippage and markouts *in the same replay* to claim
  value.

### 23. Measurement
- **Covers:** what is measured and what is not.
- **Quotable, sourced numbers:**
  - **Socket-to-book**, median **8.0 µs**, live CME (`tech_reports/md_latency_article.md:5-7`).
    The article itself says: "This is not wire-to-book. It is socket-to-book." (`:44-45`)
  - Book p50: ES 7.1 µs, NQ 7.3 µs, ZN 7.1 µs. Book p99: ES 18.5, NQ 13.6, ZN 57.0 µs
    (`md_latency_article.md:54-61`).
  - Book-latency intercepts of 6.18 / 6.73 / 6.84 µs (`kaspr/perf/RESULT_qlen_vs_latency.md:76`).
- **Slippage probe:** slippage is measured against mid, interval VWAP, same-side passive peers,
  and the touch (`sim/src/SlippageProbe.cpp:659-713`, definitions in
  `sim/include/sim/act/SlippageProbe.hpp:97-146`). The VWAP comparison separates drift from
  adverse selection.

---

## Part V — LLM-Assisted Model Discovery (LAMD)

### 24. Three different uses of generative models (keep them apart)

| Use | What is generated | Example | In this paper |
|---|---|---|---|
| LOB generation | synthetic order/message streams | Nagy et al. (S5 token model), LOB-Bench | adjacent work only; not used |
| LOB modelling | predictions / representations | Transformers, SSMs, LOBERT (encoder-only; its next-message prediction is a modelling task, not used here to generate data) | Part II |
| Model generation | candidate models and hypotheses | **LAMD** | this part |

### 25. The LAMD loop
- **Covers:** a fixed human vocabulary; the LLM as proposal engine; a deterministic compiler; fits;
  Kaspar replay on real data; walk-forward; certification; archive and mutation.
- **Example: one hypothetical iteration with 3 candidates.** All values are invented to show the
  bookkeeping. Nothing here was run.

  | id | LLM proposal | compiled form | fit | hypothetical replay OOS net (ticks/fill) | N_trials so far |
  |---|---|---|---|---|---|
  | c1 | "keep passive bid only when short-horizon OFI agrees" | cancel if OFI_5 < θ | θ by grid on train | +0.04 | 1 |
  | c2 | "OFI times queue imbalance" | score = OFI_5 × QI | logistic | +0.01 | 2 |
  | c3 | "cancel faster when sell-MO intensity spikes" | cancel if λ_sell > k·λ̄ | k on train | −0.02 | 3 |

  Every candidate's baseline is Shadow-PPOV in the same replay. The archive records the prompt,
  compiled model, fit, data split, and N_trials. c1 is **not** "discovered" until it clears the
  §27 null for the total trial count.
- **Source status:** no LAMD code exists in this repo. The description comes from the author's
  Substack (URL to add). The paper must describe it as a method and design, not report results
  that do not exist.

### 26. Hypothesis generation is not validation
- **Covers:** an LLM candidate has no stronger evidentiary status than a human one; syntactic
  validity is not mechanism.
- **Cautionary citation (to verify):** "Do LLMs Understand Limit Order Book Dynamics?" (2026). It
  reports valid-looking event sequences with wrong state dynamics and spurious predictability.

### 27. Search complexity and multiple testing
- **Covers:** performance + model complexity + **search complexity**; deflated Sharpe; the null
  max.
- **Example (illustrative).** For N independent null strategies, the expected maximum of the
  standardised Sharpe estimate ≈ √(2 ln N) (a rough upper approximation). With 1 year of data
  (SE of an annualised SR ≈ 1):

  | N_trials | null max SR (1 yr) | null max SR (4 yr) |
  |---|---|---|
  | 1 | 0 | 0 |
  | 100 | ≈ 3.0 | ≈ 1.5 |
  | 50,000 | ≈ 4.65 | ≈ 2.3 |

  A Sharpe of 2 from one prespecified test is evidence. A Sharpe of 2 as the best of 50,000
  LLM variants on 4 years is *below* the null max. Final version: replace this with the proper
  Bailey–López de Prado expected-max formula.

---

## Part VI — Evaluation

### 28. Statistical metrics by target

| Target | Metrics |
|---|---|
| direction | log loss, AUC, balanced accuracy, calibration |
| direction with confidence (selective prediction) | risk–coverage curve, F1 by confidence tier, interval coverage vs nominal (UQ-LOB reports near-nominal 68% coverage) |
| return | MSE/MAE, CRPS |
| event time / fill | survival likelihood, concordance, calibration by q bucket |
| intensity | point-process log-likelihood, time-rescaling residual test |

### 29. Economic metrics
- **Covers:** Π_agg, Π_pass, E[Π] = P(Fill)·E[Π | Fill], p_T and its limits (impact, capacity,
  inventory, latency distribution).
- **Example:** reuse the §15 table, and show that p_T can rank Front and Back wrongly when it
  ignores P(Fill).

### 30. Temporal validation
- **Covers:** chronological split, walk-forward, purging, embargo sized from look-back + horizon.
- **Example (illustrative):** features use a 60 s look-back and labels a 10 s horizon, so the
  embargo is ≥ 70 s at each train/test boundary. Walk-forward uses 20 sessions train, 5 validate,
  5 test, then rolls by 5.

### 31. Parsimony
- **Covers:** C(M) = (params, latency, calibration, monitoring) reported as separate columns; prefer
  M1 over M0 only if ΔV is credible and large relative to ΔC.
- **Example: a reporting template** the paper recommends (not filled in here):

  | Model | OOS Sharpe | DSR | Params | Latency | Calibration | Fill model |
  |---|---|---|---|---|---|---|
  | Shadow-PPOV (no model) | | | ~10 config | µs | none | queue-exact replay |
  | OFI baseline | | | ~10 | ns–µs | low | queue-exact replay |
  | Shallow NN | | | 10³–10⁴ | µs | medium | queue-exact replay |
  | Transformer | | | 10⁶ | ms | high | queue-exact replay |
  | Hybrid | | | 10⁶ | ms | high | queue-exact replay |

### 32. Benchmarking standard and datasets
- **Covers:** the reporting checklist (data / prediction / model / validation / execution /
  results); FI-2010, LOBSTER, ITCH, crypto feeds, and their limits.
- **Example:** show what the checklist would contain for a Kaspar replay study (ES, MDP3 MBO, L3,
  event clock, the three latency flags, fill rule §19, no-impact assumption, N_trials). The
  numeric fields stay blank because no run is made. The point is to show which fields a
  queue-exact replay can supply that a snapshot backtest cannot.

---

## Part VII — Hardware and outlook

### 33. Hardware acceleration and the infrastructure constraint
- **Covers:** determinism as risk control; FPGA as the deterministic hot path; GPU for
  simulation and training; mid-tier accelerators; Transformer-on-FPGA reality; workload placement;
  the bound that hardware cannot extend signal lifetime. Use the strengthened (v2) text and
  separate book-building latency from inference latency.
- **Example (illustrative):** OFI plus a logistic score takes about 20 multiply-adds and fits in a few
  FPGA cycles. A spatial-temporal Transformer needs a GPU. If the signal half-life is 5 ms,
  50 µs GPU inference versus 500 ns FPGA inference keeps 99.3% versus ~100% of the value, a
  second-order difference. Hardware removes engineering latency. It cannot add signal lifetime.
- **Workload placement table:** reuse the v2 table unchanged.
- **Link to Part V:** GPU-speed simulation multiplies N_trials. Certification must scale with it.

### 34. Failure modes
- Regime change versus state invalidation; spoofing/layering (it changes the OFI → price mapping,
  it does not "falsify" OFI); icebergs; feed and timestamp errors.
- **Example:** a CME sequence gap. A book reconstructed across the gap is wrong until snapshot
  recovery. Any model trained on those rows learns reconstruction error, not market behaviour.

### 35. Research agenda and conclusion
- Execution-aware objectives E[Π | a, X]; queue-aware benchmarks; cross-asset after costs;
  structural-change tests; LAMD as a controlled statistical search process.
- Closing claim: report what information a model extracts, what decision it changes, how that
  decision is executed, and whether the incremental value clears a properly calibrated null.

---

## Evidence status

| Claim | Class |
|---|---|
| OFI explains short-horizon mid changes, β ~ 1/depth | established (CKS), to cite |
| Hawkes stationarity ρ(Γ) < 1 and λ̄ = (I − Γ)⁻¹μ | established (math) |
| Representation matters more than backbone | synthesis; needs ≥ 3 cited comparisons |
| Front-of-queue fills are *more* informative | **open hypothesis**; check against queue-value literature |
| Hybrid inputs add little when derived from the same stream | open hypothesis (§11 ablation) |
| Queue-exact replay changes backtest conclusions versus naive fills | synthesis; argued from the fill rule (§17, §19); prior results in `shadow_pov.tex` may be cited as-is |
| LLM search raises the multiple-testing burden | established in principle (DSR); no LAMD-specific evidence, and none produced by this paper |

## Citation verification list

| Citation | Status |
|---|---|
| JAX-LOB, Frey et al., arXiv:2308.13289 | real |
| **UQ-LOB**, Manoharan, Linna, Baltakys, Dong, Kanniainen, arXiv:2609.31491 (25 Sep 2026) | verified on arXiv (abstract); **must cite**; read full text for encoder and cost evaluation |
| **LOBERT**, Linna, Baltakys, Iosifidis, Kanniainen, arXiv:2511.12563 (NeurIPS 2025 GenAI in Finance workshop) | verified on arXiv (abstract); **must cite**; read full text for dataset |
| Cont, Kukanov, Stoikov (OFI) | real; exact reference to add |
| Bailey & López de Prado (deflated Sharpe) | real; exact reference to add |
| DeepLOB (Zhang, Zohren, Roberts) | real; exact reference to add |
| Moallemi & Yuan (queue position value) | to verify |
| Nagy et al. 2023 (generative LOB) | to verify |
| LOB-Bench | to verify |
| "Do LLMs Understand LOB Dynamics?" (2026) | to verify |
| Liu et al. 2026, 57 µs INT8 CNN on KU040 | **unverified; do not cite numbers** |
| "pauliano22" 444 ns MoE engine | **unverified; hedge or drop** |
| Alveo U50 risk system, 518/530 ns | to verify |
| Stratix V hardware-aware SSM, 247 µs | to verify |
| Corsair (IEEE Micro 2025), LLM inference, not HFT | to verify |
| Exegy + AMD UL3524 STAC-T0 | to verify |
| Old-draft placeholders ("various", "related papers") | replace all |
| LAMD Substack post | URL to add |

## Doc/code contradictions (do not copy these into the paper)

- `STRATEGY_SIMULATOR_GUIDE.md:212` says "SOM matches orders against aggregated price levels", and
  `:42` calls OB.cpp MBP. The code keeps and fills per order (§19). `shadow_pov.tex:749-752` is
  correct.
- `light/SHADOW_ALGORITHM.md:44-45` describes `#ifdef DETERMINISTIC_PLACEMENT`. It was replaced by
  runtime `place_rate_bp` (`light22.hpp:550-556`).
- `SHADOW_ALGORITHM.md` calls `max_dist` / `max_dist_cancel` compile-time. They are config now.
- The header comment in `sim/config_a/lights.ini` says arm A uses `nlights_per_side 6`,
  `lev_orders_max 5`, `ord_sz 3`. The values in the file are 10 / 10 / 10.

## Next steps

1. Agree this outline and the examples.
2. LaTeX skeleton (one file per Part), with a bibliography containing only verified entries.
3. Keep the §10, §31 and §32 tables as recommended reporting templates, and say so in the text.
4. Write Part IV first, since it is fully sourced from code and existing reports. Then Parts III
   and V, then the literature Parts II and VI.
