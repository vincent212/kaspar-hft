# Handoff: run the two new simulator arms and fill the blanks in `paper_v2.tex`

Branch: `research/arrival-paper`. Everything below needs the per-window arrival
cache `arrival_paper/figs/qsim_cache/arrivals/*.npz`, which exists only on the
server (not in git, not on the laptop). The code has been tested end-to-end on a
synthetic cache; it has NOT been run on the real corpus.

## 0. FIRST: the paper has two reasons to exist, and both must be asked and answered explicitly

This is the raison d'être of the paper. Both question sets below must be posed
**explicitly in the abstract, explicitly in the introduction, and answered
explicitly in the conclusions**, one paragraph per answer. If the reader cannot
find the questions and their answers in those three places, there is no paper.

### Question set A: what causes the queues?

Reading the paper end to end, it is not clear what it concludes causes the
queues. The abstract and title say "clustering"; the mechanism section (4.6) says
the onset is the tight-gap floor; the live section (8.3) says the far tail is the
number of messages per packet. A reader cannot tell which. Pose the three
candidates and answer each:

1. **Is it the arrival rate of packets (how busy the market is)?**
   Current answer: **no**, at HFT service times. Utilisation is ~0.005–0.015 at
   T ≤ 32 µs; a window with 4–5× the packet rate has the same tail relative to T
   (Section 4.6). Rate only matters at T ≥ 64 µs, where ordinary utilisation
   queueing takes over.
2. **Is it the clustering of packets (arrivals in runs)?**
   Current answer: **this is what the paper claims, and it is the one not yet
   shown.** The tight-gap floor (~7.5 µs, 1 % of gaps) sets *where* a tail
   switches on; the claim is that the Hawkes ordering bunches tight gaps into runs
   and that sets *how large* the tail gets. The only control so far (uniform
   shuffle) destroys the gap floor and the ordering together, so it cannot
   separate the two. **The gap-shuffle arm (§4 below) decides this.** Do not
   finalise the abstract or the title until it has run.
3. **Is it the number of messages per packet (span)?**
   Current answer: **yes, as a second, separate mechanism.** Decode time grows
   ~1 µs per message, so a large packet (a) delays its own later messages
   (serialisation, not removed by a stage cut) and (b) holds the thread for many
   times the floor, so the stream queues behind it. That queue is what the live ZN
   far tail is (8.3). In the simulator it dominates at T ≤ 8 µs and adds a few
   percent above 16 µs; it does not compound with clustering (6.6).

### Question set B: are two queues better than one?

This is the title question and it must be posed as plainly as that: given a
servicing chain of length T on one thread, does cutting it into two (or N)
stages, each its own queue on its own thread, give a lower latency tail, and
when? Current answer, to be restated once the runs are in:

- **Yes, above a threshold; no, below it.** Below the tight-gap floor (~7.5 µs)
  there is no queue to shorten and a cut only adds one hop (~1.7 µs) to every
  message. Above it, one balanced cut removes most of the p99 excess for one hop
  on the median; the split pays on essentially every window from about 10 µs
  (Section 4.5, 4.4), and the best stage count rises with T (1 at ≤ 8 µs, 2 at
  16, 4 at 32, 8 at 64–128; Section 4.3).
- **Only the slowest stage matters** (Theorem 1 / the boxed principle): a cut
  that does not shorten the largest stage buys nothing.
- **At the production operating point (~7 µs) the answer is no** — do not split
  (Section 7); the tail the production chain does carry comes from large
  packets (8.3), which a cut addresses only through the queue such a packet
  opens, not through its in-packet serialisation.
- **Subject to two tests now added and not yet run:** the equal-core comparison
  (Section 4.3: is N stages the right way to spend N cores, against sharding
  and M/D/N dispatch?) and the null arms (Section 4.2). The "yes" must be
  restated with those results.

Required edits once the runs are in:
- **Abstract:** pose both question sets in plain words (what causes the queues
  — rate, clustering, or messages per packet? — and are two queues better than
  one?), then give the answers in two or three sentences, with the clustering
  answer written from the gap-shuffle result and the two-queues answer stated
  with its threshold.
- **Introduction (1.1 or 1.5 "What this paper does"):** both question sets as
  numbered lists, each question with a pointer to the section that answers it
  (A: 4.6; 4.2 + 4.1; 6.5–6.6 + 8.3. B: 4.1, 4.3, 4.5, 7, 4.2–4.3).
- **Conclusions:** this is where it matters most. One paragraph per question,
  in the same order, each opening by restating the question and closing with
  the answer and the number that supports it. The existing "Conclusion 1 /
  Conclusion 2" paragraphs are to be reorganised around these questions, not
  left beside them.
- **Title:** keep "Under Hawkes Arrivals" only if the gap shuffle collapses the
  tail. If the tail survives the gap shuffle, the mechanism is the gap floor and
  the title must change.

**Heading rule: no titles of the form "Why …", "What …", "Where …", "Which …",
"How …".** Headings state what the section establishes, declaratively; the
questions belong in the abstract, introduction and conclusions as written
sentences, not in section titles. Every existing heading of that form has been
renamed (e.g. "What this paper does" → "Plan of the paper"; "Why measurement, not
local reasoning, must decide" → "Measurement, not local reasoning, decides";
"Where the tail switches on" → "The tail onset"; "Which null the tail survives"
→ "The tail against three nulls"; "What agrees" → "Statistics that agree"; the
paragraph heads "What follows." → "Consequences.", "Why this needs measurement."
→ "This needs measurement.", etc.). Keep any new heading to the same standard.

Two ways a queue forms, which is the sentence the whole paper should hang on: a
*run* of packets arrives closer together than the service time (clustering,
threshold set by the gap floor), or a *single* many-message packet holds the
thread far longer than the floor and the stream backs up behind it (span). Rate
matters for neither at HFT service times. Splitting shortens the first kind of
queue; it does not shorten in-packet serialisation; whether it helps the queue
behind a large packet is the open regime.

## 1. What changed in the code

- `arrival_paper/qsim_run.py` — same CLI, same output file. It now also computes,
  per window and per (T, N) cell:
  - regime `G`: **gap shuffle** (Fisher–Yates permutation of each window's
    interarrival gaps; seeded from `md5(session|window|gap)`). Same packet count,
    same gap distribution, same tight-gap floor, ordering destroyed. This is the
    renewal null already used for the Fano diagnostic, now run through the sweep.
  - regime `B`: **1 s-binned Poisson** (each 1 s bin keeps its own real packet
    count, arrivals redrawn uniformly inside the bin; seed `…|bin1s`). Keeps
    second-scale rate variation, destroys microsecond clustering.
  - an **M/D/N dispatch** arm on the real arrivals: one FIFO queue, N servers,
    whole-packet service T, no hops (columns `<T>_H_MDN{2,4,8}_{p50,p95,p99,p999,max}_us`).
  - `H` and `P` are computed exactly as before; the bound check still runs on all
    four regimes. Runtime roughly 3× the old run. Needs numba (pure-python
    fallback exists but is slow).
- `arrival_paper/make_tables.py` — additionally prints:
  - `% ===== Table (tab:nulls)` rows: T, N, p99 under H / P / G / B.
  - `[prose] share of the single-stage Hawkes p99 EXCESS that survives each null`
    (excess = p99 − T, corpus medians, per T) — **this is the key number**.
  - `% ===== Table (tab:equal-core)` rows: T, N, tandem p50/p99 vs M/D/N p50/p99.
- `arrival_paper/make_zn_tail_figure.py` — generator for `figs/zn_serial_tail.pdf`
  (Section 8.3 figure), previously untracked.

## 2. Run

```bash
cd ~/kaspar-hft && git checkout research/arrival-paper && git pull
python3 -m arrival_paper.qsim_run \
    --cache-dir arrival_paper/figs/qsim_cache \
    --out-dir   arrival_paper/figs/qsim_grid
python3 -m arrival_paper.make_tables \
    --grid arrival_paper/figs/qsim_grid/qsim_grid.parquet > /tmp/tables.tex 2> /tmp/prose.txt
```

Sanity before pasting anything:
- `[run] Thm 2 bound check: 0 violating messages` on stderr.
- The `Table 1 (tab:main-corpus)` and `Table 2 (tab:delta)` blocks in
  `/tmp/tables.tex` must be identical to what is in the paper now (the new arms do
  not touch H or P). If they differ, stop — the cache or the seed changed.

## 3. Blanks to fill in `paper_v2.tex`

Every blank is a `\blank{...}` macro that renders as a boxed **[FILL: …]**.
`grep -n '\\blank{' paper_v2.tex` lists them (five). In order:

1. **Abstract** — one sentence after "…attributable to clustering." saying
   whether the tail survives the gap-shuffle and 1 s-binned nulls (see §4 below
   for the wording rule).
2. **Section 4.2, `tab:nulls`** — replace the `\multicolumn{6}{c}{\blank{…}}` row
   with the `tab:nulls` rows from `/tmp/tables.tex` (28 rows, `\midrule` between
   T blocks as printed).
3. **Section 4.2, prose** — two or three sentences from the `[prose] … survives
   each null` block, at T = 8, 16, 32 µs, per the rule in §4.
4. **Section 4.3, `tab:equal-core`** — replace the blank row with the
   `tab:equal-core` rows (28 rows).
5. **Section 4.3, prose** — one or two sentences reading that table (§5 below).

(The two empty tables currently render with their header columns spread across
the full page width; that is the wide blank cell, and it goes away once real
rows replace it. Nothing to fix.)

Then: delete the line `\newcommand{\blank}[1]{...}` from the preamble,
`latexmk -pdf paper_v2.tex`, and check `grep -c Overfull paper_v2.log` is 0 and
`grep -c '\\blank' paper_v2.tex` is 0.

## 4. How to read the null result (the key test)

The number is: at each T, the fraction of the single-stage Hawkes p99 excess
(p99 − T) that remains under each null. Read T = 16 and 32 µs first (T = 8 sits
at the floor and is noisier).

- **Gap shuffle keeps most of it (say > 50 %)** → the tail is set by the gap
  *distribution* (the 7.5 µs floor and how many gaps sit near it), not by the
  ordering of packets into clusters. Then the attribution in the paper is wrong
  as written: the abstract sentence "so the tail reduction in that regime is
  attributable to clustering", the 4.1 preamble ("it is the bursts … that create
  the tail"), the Conclusion, and the title's "Under Hawkes Arrivals" all need
  to say "under a floored gap distribution" / "the tight-gap floor". The design
  rule itself is unaffected (it is stated in terms of the floor already).
- **Gap shuffle collapses it (say < 20 %) and the binned null also collapses
  it** → clustering is confirmed against the stronger null. Fill: "the tail
  survives neither the gap shuffle nor the 1 s-binned null, so it is the ordering
  of tight gaps into runs, not the gap distribution or the second-scale rate,
  that produces it; the tight-gap floor sets where the tail switches on, the
  clustering sets how large it grows."
- **Gap shuffle collapses it but the binned null keeps it** → the tail comes
  from second-scale rate variation (busy stretches), not microsecond
  self-excitation. Say so; "Hawkes" is then the wrong word for the mechanism.
- Mixed across T: report it by T. It is fine for the gap marginal to carry the
  tail at T = 8 (just above the floor) while clustering carries it at 16–32.

Expected (not measured): collapse. A deterministic server queues on *runs* of
tight gaps; a renewal process makes a run of two with probability ~0.01², a
clustered one makes them in streaks. But it has to be shown.

## 5. How to read the equal-core table

M/D/N has the same capacity N/T as the tandem and no hops, with ordering
ignored, so it is a lower bound on dispatch. Expected: M/D/N p99 ≈ tandem p99
(same capacity) with M/D/N p50 = T against the tandem's T + (N−1)h. Write that,
and that dispatch on this feed would need a resequencing stage (a hop) which the
table does not charge; the tandem's advantage is that it keeps order without one.
If M/D/N beats the tandem at p99 too, say the tandem's case rests on ordering,
not on latency.

## 6. Other items that need the author or the raw data (not fixable from here)

- **Six-stream figure (8.3, `fig:tails-six` / `tab:tails-six`).** Built from the
  serial rows of the report's section 2 (ES/NQ/ZN × book/trade, one 900 s window
  each) plus span p99 and packet counts from the 53-min capture (6.5) and the
  per-message slopes (6.3). Two gaps in the source: the report gives no message
  counts for the trade streams and no p1 for any stream. If the trade windows'
  message counts are wanted in the table, add them from the `.msg` logs.
- **p99.9/p1 on ZN (Section 8.3).** The report has no p1 (lowest is p10). The
  text says "fifty-four times the fastest tenth". If you want p99.9/p1, add
  `np.quantile(lat, 0.01)` to `kaspr/perf/kh_msg.py`, rerun on the ZN `.msg`
  log, and replace that sentence.
- **"8.29 million messages" (Section 6.1, twice)** vs the span table, which sums
  to 6.81 M. Reconcile or say what the other 1.5 M are.
- **96.1 % singleton packets / mean span 1.073 / 93.2 % singleton messages
  (Section 6.6)** cannot all hold if pooled (0.961/1.073 = 89.6 %). Say which are
  per-window medians, or fix one.
- **Literature list (1.5):** "Bo Li" and "ABIDES-MARL" were removed because they
  have no bibliography entry; add entries and restore if wanted. Everything else
  in that paragraph is now `\citep`'d; Hawkes (1971) and Ozaki (1979) are cited
  in 3.3.
- **Contribution list (1.5 "What this paper does")** has no bullet for the live
  cross-validation (Section 6) or the ZN tail analysis (8.3). Add one.
- **Hawkes fit:** no goodness-of-fit test, exponential kernel only, and Appendix E
  shows the fitted model misplaces the tight end of the gap distribution. Either
  add a time-rescaling/KS residual test or one sentence in 3.2 saying the fit is
  descriptive and misspecified at the floor.
- **Reproducibility:** state whether the live per-message `.msg` records are
  released; add the commit/tag, Python/numba versions, MLE initialisation, and the
  live box's hardware/kernel/NIC.
- **Hot-path residual (8.3):** with an empty queue and first-in-packet, p99 is
  15.5 and p99.9 26.4 µs against a 7 µs floor. The text now says this is
  unresolved. If the `.msg` records show what it is, say so.
- **Remarks are now numbered** (`\newtheorem{remark}{Remark}[section]`); the
  "Remark 2.2" phantom is gone. Check "Remark 2.3"/"2.4" read right in context.

## 7. What was already fixed in this pass (for orientation)

- Serial-vs-parallel comparison removed from 8.3; 8.3 is now the ZN serial tail:
  a few large packets → linear service increase → queue build-up (non-linear).
- 6.6, Section 7, the Conclusion and the abstract brought in line with that
  mechanism (no more "not a queue / only lever / drained by servers in parallel").
- Pipelining de-emphasised: 3.4 retitled "Stages run concurrently", the
  "Pipelining is necessary" paragraph cut, "pipelining rule" → "splitting rule".
- New 4.2 (nulls) and 4.3 (equal cores) with the blanks above; new assumption
  paragraph in 2.3 (equal stages, no cross-core cost beyond h).
- Abstract: NQ/channel-318 scope, "three instruments, one of them the corpus's
  own", 11–47× (recomputed from Table 1), "code is in the repo; captures are not
  redistributable", "three significant figures" claim dropped.
- Numeric fixes: Section 7 hop cost 1.7 → 3.4; Δ(6) attributed to the 4.5 sweep;
  Conclusion "tail excess" → "p99 excess plus hop cost"; floor-table caption;
  crossing table "at or above"; Poisson p99.9 at N=8; Appendix D 55–130 / 1.9–2.6
  and the Poisson row; "halving h" sentence; forty → forty-five messages.
- Threshold stated as two levels (tail appears ~7.5 µs; split pays from ~10 µs).
- Live-measurement limits and session nesting added to the caveats; "sharing
  nothing but the binary" / "no common term" softened; the p99-vs-p1-gap
  agreement flagged as near-definitional.
- Language: British spellings in 8.5, trade-off, p99.9 notation, possessives on
  citations, step 6→7, subject–verb, the Appendix E sentence, Part I retitled
  "Theory", duplicated preamble/opener paragraphs in 4.6/6.3/6.4/6.5/6.6 removed.
