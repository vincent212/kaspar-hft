# Verified claims: Lalor & Swishchuk, neural Hawkes LOB simulator + deep-RL market making

Source: FULL TEXT, arXiv 2502.17417v1 (24 Feb 2025), read in full via pdftotext on 2026-10-03.
Published version: Applied Mathematical Finance, DOI 10.1080/1350486X.2025.2548448 (Crossref). Volume/pages for the
published version are taken from the existing bib entry `lalor2025nhp` (32(2):128–155), which was checked earlier.
Authors: Luca Lalor and Anatoliy Swishchuk, University of Calgary.

## Event types (Sec. 2.1)

There are 12 event types. The superscript gives the effect on the mid: +, −, or 0.
- Aggressive (move the mid):
  - LB+ — limit buy above the best bid;
  - LS− — limit sell below the best ask;
  - MB+ — market buy that depletes at least one ask queue;
  - MS− — market sell that depletes a bid queue;
  - BC− — buy cancel that empties the best bid;
  - SC+ — sell cancel that empties the best ask.
- Non-aggressive (leave the mid unchanged): LB0, LS0, MB0, MS0, BC0, SC0.
- Sets used later: O_u = {MB+, LB+, SC+}, O_d = {MS−, LS−, BC−} (printed as "MC−", a typo), and O_n = the six 0-events.

## Data

- LOBSTER free sample: one day, 21 June 2012, 9:30–16:30 (as printed), 10 levels.
- Stocks: AAPL, AMZN, GOOG, INTC, MSFT.
- Table 1 gives event counts per stock. Pooled probabilities: LB0 .221, LS0 .236, BC0 .212, SC0 .225; MB+ .005, MS− .006, …
- Model split: 60% train, 20% validation, 20% test (Sec. 3.1).

## Classical structure (Sec. 2.2)

- Nonlinear multivariate Hawkes, Eq. (1)–(2): λ_i(t) = φ_i(λ̄_i + ∫ Σ_j α_ij e^{−β_ij(t−s)} dN_j(s)), with m = 12.
- Mid process, Eq. (3): V(t+Δt) = V(t) + (Δ/2)[Σ_{k∈O_u} I_k a(X_k) − Σ_{l∈O_d} I_l b(X_l)], where Δ is the tick/spread.
- This generalises Lu & Abergel (2018), which uses unit jumps (Eq. 4).

## Neural Hawkes (Sec. 3.1)

- Follows Mei & Eisner (2017) and Shi & Cartlidge (2022): m = 12 continuous-time LSTM units stacked, one per event type, "the hidden state parameters ... specifically computed for each of our LOB event types".
- Input: one-hot event type plus a market state x ∈ {0, 1, 2}.
  - x comes from the volume imbalance I = (v_b − v_a)/(v_b + v_a) at the best quotes, with θ = 0.4 (as in Shi & Cartlidge).
  - Eq. (6) prints the intervals as [−1, θ], [−θ, θ], [−θ, 1]: overlapping, so evidently a typo.
- Cell, Eq. (7a–g): gates i, f, o; target g_t = f⊙c_{t−1} + i⊙tanh(…); decay δ_t = exp(W_δ[x, h] + b_δ); c_t = g_t + (c_{t−1} − g_t)⊙exp(−δ_t Δt); h_t = o⊙tanh(c_t).
- Intensity: λ_i(t) = ln(1 + e^{h_i(t)}) (softplus). The paper numbers this Eq. (7) a second time.
- What can now be negative, per the paper: "α_ij and λ_i ... can now be negative", allowing inhibition. Past effects are not additive and decay is learned.
- Training:
  - 20 epochs, batch 256, rolling windows of sequence length 100, RMSprop with lr 0.002.
  - Loss Eq. (8) is printed as Σ_j Σ_i [log λ_i(t_{j+1}) − ∫ λ_i ds] and called the negative log-likelihood.
  - As printed it has the sign of a log-likelihood, and it sums log-intensities over all types rather than only the type that occurred. This differs from the standard point-process likelihood. It is likely a typo; the code was not checked.
- Table 2: loss and next-event-type accuracy ("probability of correctly predicting the next event in the batch").

  | Stock | Train loss | Train acc | Test loss | Test acc |
  |---|---|---|---|---|
  | AAPL | 1.7739 | .4588 | 1.5426 | .4054 |
  | AMZN | 1.6952 | .4401 | 1.5662 | .3904 |
  | GOOG | 2.6181 | .3778 | 2.76 | .3153 |
  | INTC | −0.4972 | .5675 | −0.2038 | .4969 |
  | MSFT | −0.6151 | .5489 | −0.5639 | .5124 |

- No classical Hawkes baseline is fitted in this paper. It cites Shi & Cartlidge (2022) for neural Hawkes beating classical Hawkes.

## Simulation (Sec. 3.1–3.2)

- Events by Ogata thinning: Δt ~ Exp(Λ_t), Λ = Σ λ_i; P(type i) = λ_i/Λ (Eq. 9).
- "we ran 200 simulations over each batch". Table 3 gives simulated counts; totals ≈ 51,200 per stock.
- Mid, Eq. (10): V(t+Δt) = V(t) + sgn·|ΔV|.
  - The sign comes from the event's category: +1 for O_u, −1 for O_d, 0 for O_n.
  - The jump size comes from a discrete empirical distribution, Eq. (11).
  - Number of distinct jump sizes (Table 4): AAPL 50, AMZN 47, GOOG 91, INTC 2, MSFT 3.
- Table 4, real vs sim log-return statistics:

  | Statistic | AAPL | AMZN | GOOG | INTC | MSFT |
  |---|---|---|---|---|---|
  | Volatility, real | .00005 | .0001 | .00011 | .00018 | .00016 |
  | Volatility, sim | .00005 | .0001 | .0002 | .0001 | .0002 |
  | Absolute skewness, real | .0685 | .1276 | .0267 | .1014 | .0908 |
  | Absolute skewness, sim | .1688 | .3134 | 1.6717 | .0144 | .0555 |
  | Excess kurtosis, real | 6.69 | 17.71 | 35.40 | −1.98 | −1.92 |
  | Excess kurtosis, sim | 6.06 | 13.34 | 33.87 | −1.99 | −1.94 |
  | Hurst, real | .3929 | .3428 | .3415 | .1874 | .2846 |
  | Hurst, sim | .5759 | .6521 | .5707 | .3274 | .5583 |

- Authors' reading: it captures volatility and kurtosis, but is "less accurate in finer details, such as matching the observed price range and distinguishing between trending and mean-reverting market behavior."
- Authors on event mix: "the simulation still exhibits a higher proportion of aggressive events relative to non-aggressive events compared to the real data".

### OUR COMPUTATION from their Tables 1 and 3: share of the six aggressive types among all events

| Stock | Real | Sim |
|---|---|---|
| AAPL | 0.161 | 0.316 |
| AMZN | 0.102 | 0.200 |
| GOOG | 0.163 | 0.312 |
| INTC | 0.005 | 0.223 |
| MSFT | 0.006 | 0.270 |

### OUR COMPUTATION: share of the most frequent type in the full day (Table 1)

AAPL .227, AMZN .228, GOOG .208, INTC .260, MSFT .260. This is a rough reference for always guessing the most frequent type. It uses the full day, not their test split.

## Market making (Sec. 4)

- State S_t = (V_t, Q_t), where Q is inventory.
- Fills happen only on market-order events:
  - Adverse fills: MB+ / MS− always fill the agent's quote at the touch (Eqs. 17–18).
  - Non-adverse fills: MB0 / MS0 fill with probability p = 0.2 (Eqs. 14–16). The value 0.2 is from their earlier empirical work on CME futures (Lalor & Swishchuk 2024b).
- Actions (Eq. 19): {−1, 0, 1}, restricted to {0, 1} at Q = −q and to {−1, 0} at Q = q, with q = 5. Described as choosing "whether or not to post limit orders at the best bid/ask".
- Reward (Eq. 20): E[W_T − ψ ∫|Q_t| dt], with W = QV + C and ψ = 0.001. Trade size 1.
- Algorithm: Soft Actor-Critic via Stable Baselines3.
- Train on the first 5,000 events, test on the next 2,500, separately for simulated and real data, per stock. 100 test episodes.
- Results:
  - Terminal rewards are "mostly negative for both sets of data" (Figs. 6–7, histograms only, no numbers in text).
  - Most fills come from aggressive market orders, in both sim and real data.
  - Adverse : non-adverse fill ratio (Table 6), real vs sim: AAPL 3.21 / 3.09, AMZN 3.42 / 3.46, GOOG 3.23 / 3.42, INTC 2.00 / 1.97, MSFT 1.95 / 1.61.
- Authors: "a simple strategy ... is unlikely to be profitable on its own"; "the main point here is to show how our event-based LOB model can be applied".

## Limitations stated by the authors

- Finer details are not matched (skewness, Hurst exponent, price range).
- Aggressive vs non-aggressive events need to be distinguished better.
- Future work: extend beyond 12 events.
