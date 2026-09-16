# Queue length (qlen) vs leg-1 latency — Fed day, 2026-09-16

> **CORRECTION, added after the per-message log went live.** The slopes in this
> document are **bin-level** and overstate the queue effect by **6–17×**. Per
> message, the same quantity is 0.37 / 1.47 / 3.79 µs per unit qlen, not 10.19
> / 4.59 / 13.66. The bin-level regression is an ecological fallacy: bursts
> raise ring qlen *and* packet size together, and it hands the packet-size
> effect to the queue. See `RESULT_per_message.md`.
>
> What **does** survive, and is confirmed by the per-message data:
> - the intercepts. 6.18 / 6.73 / 6.84 µs here; 6.72 / 7.29 / 7.03 µs measured
>   directly at `qlen==0 AND idx==0`.
> - `floor@b~1` flat across a 6.5–9.8× packet-rate change.
> - context switching ruled out.
> - the arrival-process results.
> - the 14:01:59.7 stall.
>
> The Fed-day *data* is preserved at `/home/vincent/perf/mdperf_fedday_20260916`.

Captured live. Preserved verbatim before the per-message instrumentation change
and process restart, because the restart resets every accumulator and this
window (FOMC, 14:00 local) is not reproducible.

## Provenance

- host process: `md_perf_meter`, pid 2850235, started 13:30:37 local
- data: `/home/vincent/perf/mdperf/lat_{ESZ6,NQZ6,ZNZ6}.csv`, 100 ms bins
- script: `kh_scat.py "14:00:00"` (post-cut only)
- startup backlog excluded via the `.arr` leading-run rule (`drop_startup`)
- recoveries during the window: **0**. All 39 recovery lines in the log are
  from startup, 17:30:37–17:30:54 UTC. Nothing was held and released mid-window,
  so no leg 1 here is a gap-close duration.

## What the two axes are

Both are means over **the same messages**, from the same bin:

```
x = qlen_sum / l1_n      mean MsgBuf qlen seen by a message in the bin
y = l1_sum_ns / l1_n     mean leg-1 latency of a message in the bin
```

Matched numerator set, matched denominator. This is the reason the table below
works and the earlier `qlen_max` grouping did not — see "Superseded" at the end.

## Dose-response

```
  mean qlen     ES book   NQ book   ZN book  |  ES trade  NQ trade  ZN trade
  0.00-0.01       7.2us     7.3us     8.1us  |    10.2us     9.5us    20.0us
  0.01-0.05       7.0us     6.9us     7.2us  |    12.3us    10.1us    16.9us
  0.05-0.10       7.4us     7.3us     7.3us  |    19.1us        -     15.0us
  0.10-0.25       8.2us     7.5us     7.9us  |    23.3us    41.6us    41.6us
  0.25-0.50       9.4us     8.2us    15.4us  |    34.0us        -     75.2us
  0.50-1.00           -         -    25.3us  |         -         -    33.1us
  1.00+               -    16.1us    18.6us  |         -         -   130.5us
```

Weighted fits (weight = messages in bin):

```
  ES book    lat =  6.73 + 10.19*q    r2=0.002
  NQ book    lat =  6.84 +  4.59*q    r2=0.033
  ZN book    lat =  6.18 + 13.66*q    r2=0.087
  ES trade   lat = 10.35 + 74.95*q    r2=0.179
  NQ trade   lat =  9.39 + 77.55*q    r2=0.210
  ZN trade   lat = 19.49 + 66.74*q    r2=0.313
```

Denominators behind the fits: ES book 8,314 bins / 604,241 msgs; NQ book 8,322 /
723,373; ZN book 6,949 / 399,439; ES trade 5,338 / 55,333; NQ trade 5,319 /
22,930; ZN trade 1,244 / 36,730.

## What it shows

**The three book intercepts agree: 6.18, 6.73, 6.84 µs.** Three instruments,
three channels, three independent fits, spread 0.66 µs. That is the hot path
extrapolated to zero queue.

**Trade slopes are 5–16× the book slopes** (67–78 vs 4.6–13.7 µs per unit
qlen) while the intercepts differ by only 1.4–3×. A queued packet costs far
more on trade because it carries ~3 messages behind one arrival stamp instead
of ~1.1. Queue length and batch multiply.

**Low r² on book is a finding, not a weakness.** qlen explains 0.2–8.7% of
bin-to-bin variance on book, where mean qlen barely leaves [0, 0.3] — short
lever, steep slope. On trade, where qlen reaches 1.0+, it explains 18–31%.
The conditional mean is monotone; the residual scatter is batch and cache.

## Corroborating measurements from the same window

**Fed event, before/after 14:00** (`kh_event.py`). The control column is
`floor@b~1`: mean leg 1 over bins with batch/pkt < 1.05 AND qlen_max ≤ 1 —
messages that arrived alone and waited for nothing.

```
sym  pop    when     pkt/s     msgs     mean   batch   qmean   floor@b~1
ES   book   before    97.5   197937    8.3us    1.14   0.079   8.4us/90582
ES   book   after    634.7   213538    7.9us    1.14   0.089   7.1us/49744
NQ   book   before   168.2   311330    7.1us    1.06   0.103   7.1us/173576
NQ   book   after    771.9   248578    7.8us    1.09   0.122   7.5us/60636
ZN   book   before    59.3   122500   12.4us    1.17   0.165   7.6us/56476
ZN   book   after    581.3   196869    7.7us    1.15   0.141   7.0us/43847
```

Packet rate rose **6.5× / 4.6× / 9.8×**. The unqueued floor moved by at most
+0.4 µs and fell on two of three. The hot path is not rate-sensitive. Two
independent methods — this floor and the fitted intercept above — land on the
same 6.2–7.5 µs.

ZN got *faster* (12.4 → 7.7 µs) at ten times the rate. Read as mean minus
floor: 4.8 µs of composition before, 0.7 µs after. The burst arrived as more,
smaller packets — batch flat 1.17→1.15, qmean *fell* 0.165→0.141. Pre-Fed ZN
was slow because it was idle.

**Context switching is ruled out**, including under load. `/proc/<pid>/task/*/schedstat`
field 2 (runqueue wait — time runnable but denied a core), 10 s window during
the event:

```
voluntary     44,028 -> 272,190 per 10s   (6.2x)
nonvoluntary     512 ->   1,056 per 10s   (2.1x)

2850318  cpu=9634.8ms  rqwait=3.283ms  slices=  2193  wait%=0.03%
2850323  cpu=4952.4ms  rqwait=0.067ms  slices=  1041  wait%=0.00%
2850316  cpu= 417.8ms  rqwait=0.254ms  slices=139345  wait%=0.06%
2850321  cpu= 125.7ms  rqwait=1.885ms  slices= 43679  wait%=1.48%
```

Switch count jumped 6×; hot-thread runqueue wait stayed at 0.03%. Pre-Fed total
was 2.585 ms out of 10,000 ms = 0.026%.

Caveat: schedstat measures being **off core**. It cannot see an SMT sibling
stealing issue slots — 65 threads on 8 physical cores. Flat rqwait rules out
preemption; it does not rule out SMT contention, which needs perf counters.

**Arrival process is not Poisson** (`kh_proc.py`, same day). Gap
autocorrelation positive at all 7 lags on book; Fano factor collapses 6–19×
under a seeded shuffle that preserves the marginal exactly and destroys only
ordering; Hurst H = 0.62–0.70 against a shuffled null of ~0.55; implied Hawkes
branching ratio ≤ 0.88–0.95. So: not renewal, not merely heavy-tailed, but
long-range dependent / self-exciting. Measured c_a² = 13.9 / 17.1 / 28.9
against 1 for Poisson. Kingman's E[W] ≈ (ρ/(1−ρ))·((c_a²+c_s²)/2)·E[S] therefore
*understates* here, because the shuffle proves correlation contributes beyond
the marginal.

## One counterexample, and it is not queueing

ESZ6 book, bin 14:01:59.7:

```
14:01:59.6    176 msgs  batch 1.17  qmax 2    mean  10.1us   max    20.0us
14:01:59.7     69 msgs  batch 1.19  qmax 1    mean 918.9us   max 12237.1us
14:01:59.8     92 msgs  batch 1.48  qmax 1    mean   9.3us   max    21.3us
```

A 63 ms smear over 69 messages in one 100 ms bin, at **qlen 1**, in a
*quiet* bin. Neighbours normal both sides. Not a recovery (count flat at 39).
Nothing in qlen or batch explains it — it is a stall, not a queue. 1 bin in
~5,000, 69 messages in 213,538. It moves no mean above. It belongs in the
writeup as the residual the queueing model does not cover.

## Superseded — do not reuse

The earlier tables grouped bins by `qlen_max` and are **not interpretable**.
The group was selected on the *deepest* message in the bin but averaged over
*all* of them, and msgs/bin itself rises with qlen (ZN book: 6.7 at qlen 0,
479.6 at qlen 3-4 — a 72× swing). The effect was divided by a denominator that
grew with the effect, which flattened the ladder to ~1.2 µs and hid the result.

Affected: `kh_qlat.py`, `kh_batch.py` Test 2 qlen column, `/tmp/kh_exc.py`.
`kh_scat.py` replaces them.

A max and a sum do not identify a joint distribution. Once 166 messages collapse
to `(qlen_max, l1_sum, n)`, no arithmetic recovers which message was deep — only
a bound between "+6.5 µs on all 310" and "2.0 ms on one".

## Still bin-level

Everything above is an **ecological** relation. A slope here says "bins where
messages saw longer queues had higher mean latency", NOT "a message at qlen d
costs a + b·d". Closing that gap is what the `MsgRec` per-message log
(`LatencyProbe.hpp`) is for: one 16-byte record per admitted message carrying
`(t1, l1_ns, qlen, idx)`, emitted at the same point as the bin accumulators so
its population is exactly the row's `l1_n`.
