# Review of `paper_v2.tex` — v3

Reviewer: Claude Opus 5. Full re-read of the current draft (41 pages, 1065 lines,
4 parts + 3 appendices). Supersedes `REVIEW_v2.md`, which was written against the
pre-Theorem-2 draft and is now obsolete.

---

## Summary

The paper is in substantially better shape than at v2. **All 23 findings from v2
are resolved.** The two structural changes since then are both improvements:

- **Conjecture 2 → Theorem 2.** The tail result is now a *pathwise* reduction —
  `W_j^(N) = w_j(s_max) + (N-1)h` on every sample path, arbitrary arrivals, no
  arrival-law assumption — proved from Friedman's order-invariance plus Lindley
  monotonicity. This was the paper's weakest link and is now its strongest.
- **The Borel error is fixed, and fixed better than v2 recommended.** Appendix A
  derives the Stirling asymptotic, establishes `k* = (1-n)^-2`, and states
  explicitly that the subexponential machinery (Asmussen–Foss,
  Baccelli–Foss–Lelarge) does *not* apply at fixed `n < 1` — then observes it is
  not needed, because Theorem 2 applies `1/N` to whatever `W_q^(1)` the feed
  produces. §1.5 reports the episode candidly as a data point on model-assisted
  proof drafting.

### Numbers independently re-derived — all correct

Recomputed from the tables, not taken on trust:

| check | result |
|---|---|
| `p50 = T + (N-1)h`, all 20 cells | exact |
| `Δ(N)` from Table 1 → Table 2, all 20 | exact |
| **Δ depends only on `T/N`** — all 8 diagonals single-valued | exact |
| `γ` least-squares, 5 rows | matches to 0.01 |
| ratio ≤ `1/N` bound | zero violations |
| `κ` breakpoints, 15 values | match |
| `N*` at `κ=1` vs raw `p99` minima | both give 1,2,4,4,8 |
| abstract "0.28–0.34" (N=2, T≥4) | 0.284–0.340 |
| conclusion "4 to 130× hop cost" | 4.0–131.0 |
| conclusion "p99.9 excess twice that again" | 2.09–2.33× |
| Appendix B ratio + bound columns | match |
| Appendix C: `7+2+1` vs `7+3` differ by exactly `h` | 1.70 at all three quantiles |
| Appendix C ratios vs `s_max/T` bound | all below bound |

The diagonal result is the strongest empirical content in the paper: Δ taking a
single value per `T/N` across up to four different `(T,N)` pairs is Theorem 2's
reduction identity appearing directly in real data, with no fitting.

Build is clean: no LaTeX warnings, no undefined refs or citations, no
TODO/placeholder markers, figure present, three appendices complete.

**Verdict: submittable.** The findings below are correctness/consistency nits and
one factual error, not structural problems.

---

## Findings

### 1. Shadow-POV is used with no citation — violates a standing project rule

`\shadowpov{}` appears at line 91 ("the \kasparhft{} actor chain that carries the
\shadowpov{} algorithm") and in the conclusion. There is **no bibitem for it**
(`grep` for a shadow-pov reference returns 0).

The project's standing convention is that Shadow-POV is a named algorithm and the
introducing publication must be cited on first mention, never re-derived
descriptively. Add the reference, or drop the name.

**Severity: must fix** (explicit standing rule).

### 2. §6 (`sec:kaspar`, line 627) misstates what Part II runs on

> "It is at once a production system ... and, with the same actor code driven from
> recorded packet captures, **the simulator this paper's Part~II runs on**."

Part II does **not** run on Kaspar-HFT. Per the Reproducibility section, Part II
runs on `qsim_prep.py` (packet extraction + Hawkes MLE) and `qsim_run.py` (a
numba tandem-Lindley recursion over extracted timestamps). Kaspar-HFT's actor
code is not in the loop anywhere in Part II; only the *tapes* derive from pcaps.

This overclaims the link between the framework and the experiment, and a careful
reader will catch it. Suggested fix: "…and, with the same actor code driven from
recorded packet captures, a replay simulator in its own right; Part II's
measurements are made on a standalone tandem-Lindley model of that pipeline
rather than on the framework itself."

**Severity: must fix** (factual).

### 3. "281-session corpus" in three places where the analysed corpus is 276

§3.1 correctly explains that 276 of 281 tapes yield at least one complete RTH
window, giving 3512 windows. But three other places still say 281:

- line 90 (§1.3): "each 30-minute RTH window of the 281-session corpus"
- line 102 (§1.5): "the tandem-Lindley simulator on the 281-session corpus"
- line 406 (§3.2): "on the 281-session NQ front-month corpus" — then immediately
  "across the 3512 windows", which is the 276-session number. Internally
  inconsistent within one sentence.

The abstract and conclusion correctly say 276/3512.

**Severity: should fix.**

### 4. Appendix A understates its own range: "1.7–7 T" should be "1.0–7 T"

Line 1017: "against the $1.7$–$7\,T$ tail excess observed at the corpus-median
$p_{99}$".

Computed `Δ(1)/T` across the sweep: **1.00, 1.72, 2.52, 3.95, 6.96** at
`T = 2, 4, 8, 16, 32`. The stated lower bound of 1.7 silently drops the `T = 2`
row. Either write "1.0–7 T" or scope it to `T ≥ 4 μs`.

**Severity: minor.**

### 5. Algorithm 1 describes a Poisson null the code does not implement

Algorithm 1 line 7: `arr_P ← sort(Uniform(T_0, T_1)^{n_pkt})` — uniform over the
**nominal 30-minute window**. §3.5 repeats this ("rate `n_pkt/(T_1-T_0)`").

`qsim_prep.py:130-133` actually draws uniform over **`[first packet, last
packet]`**:

```python
rng.integers(low=int(pkt_arr[0]), high=int(pkt_arr[-1]) + 1, size=n_packets)
```

For a busy window these coincide; for a sparse one they do not. The code's choice
is arguably the better null (rate-matched to the observed packet span rather than
to wall-clock), but the paper documents a different construction than the one
that produced its numbers.

**Severity: should fix** — change the paper to match the code, since the code's
choice is defensible.

### 6. The released `rho` column is computed at a service time not in the sweep

Reproducibility states "the cached per-window metadata are released with the
paper." That metadata contains a `rho` column computed in `qsim_prep.py:111` as

```python
rho = float(n_packets * FLOOR_NS) / max(1, window_span_ns)   # FLOOR_NS = 7_230
```

i.e. at the **legacy 7.23 μs floor**, which the paper dropped entirely. The ρ
values quoted in the text (0.0036 at T=8, 0.0145 at T=32) are computed correctly
from λ̄ and T, so the *paper* is right — but a reader who opens the released
metadata gets a ρ that matches nothing in the paper.

Fix is free: `window_span_ns` is now stored, so `rho(T) = n_packets·T /
window_span_ns` is computable per scenario in `qsim_run.py`. Either do that and
drop the stale column, or rename it `rho_at_7p23us`.

**Severity: should fix** (reproducibility).

### 7. §1.3's Shadow-POV promise is only partly delivered

§1.3 promises to "map the recommended cut points onto the \kasparhft{} actor
chain that carries the \shadowpov{} algorithm (Section~\ref{sec:implementation})."

§6 describes the pipeline qualitatively and §7 gives the measurement workflow,
but no per-stage service times for the Shadow-POV chain are given, so no concrete
`s_max`, partition, or `N*` is worked out for it. The reader is given the method
but not the worked instance the intro promises.

Either soften §1.3 to "map the design rule onto the Kaspar-HFT actor chain", or
add a short worked partition using the `~7 μs` tick-to-book figure already cited
at line 629 — the Table 2 diagonals make this a five-line calculation.

**Severity: should fix** (a promise the paper does not keep).

### 8. `p95` is recorded everywhere and reported nowhere

Algorithm 1 records `p95`, `quantiles_us()` returns it, and it is in the output
parquet — but no table or figure in the paper shows it. Not an error; a free
strengthening of §4.4 ("Across windows") if wanted.

**Severity: cosmetic.**

### 9. `T = 10 μs` is used in exposition but is not a sweep value

The "Reading Equation (1)" paragraph (line 184) and the worked example (line 425)
both use `T = 10 μs`, which is not in `{2, 4, 8, 16, 32}`. Line 188 does say "a
round-number ballpark chosen for exposition", so this is deliberate — but a
reader moving from the worked example to Table 1 finds no `T = 10` row. Using
`T = 8` in the worked example would remove the seam at no cost.

**Severity: cosmetic.**

---

## Not findings — checked and correct

- Theorem 1's burst hypothesis is now explicit in the statement (`j`-th member
  arrives no later than `(j-1)T/N`), and the proof's use of it is valid for both
  the single-stage and tandem halves.
- Corollary 3 now has a real proof (PASTA + Theorem 2 two-sided), not an
  assertion, and its `ρ/N < 1-q` condition is checked cell-by-cell against
  Table 1 in §5.
- `friedman1965` and `lindley1952` are both present in the bibliography.
- §3.2's branching-ratio numbers (median 0.794, 5–95 range 0.757–0.827) match the
  cache, note that every fit converged, cite the L-BFGS-B cross-check, and
  explain the gap against the literature's `n ≈ 0.9` (packet arrivals aggregate
  several book events, lowering fitted self-excitation). This is a correct and
  well-handled treatment of what was v2's finding #3.
- §3.2 correctly states the optimizer as Nelder–Mead, matching the code. v2's
  finding #22 (paper claimed L-BFGS-B) is resolved.
- The §9/§11 busy-poll contradiction from v2 (#14, #15) is gone: the conclusion
  now says "measure … rather than assume it wins".

---

## Suggested order

**Before submission:** 1 (Shadow-POV citation), 2 (Part II simulator claim),
3 (281 vs 276), 7 (Shadow-POV worked instance or soften the promise).

**Housekeeping:** 4, 5, 6.

**Optional:** 8, 9.

Nothing here requires re-running the sweep.
