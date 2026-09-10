# Mailbox queue benchmark — x86-64 Linux server results

Follow-up to `queue_types_article.md`, which measured an Apple M3 and closed
with: *"The Linux x86-64 numbers — where more cores mean more producers and the
sharded/lock-free advantage should widen — are coming in a follow-up."*

This is that run. **The prediction holds for fan-in and fails for everything
else**, and one of the article's two headline numbers does not survive.

This document is the comparison against the M3. The standalone write-up of what
this machine says on its own terms — where the headline is thread placement, not
the queue — is `queue_types_article_epyc.md`.

## Machine and method

```
Linux 5.14.0-284.11.1.el9_2.x86_64 x86_64
AMD EPYC 9374F 32-Core Processor   (64 logical, 2 SMT/core)
4 NUMA nodes, 8 L3 instances (256 MiB) -> 4 cores per CCD, 8 per node
g++ (GCC) 15.2.0
build: MemoryPool ENABLED
```

- Branch `feat/variant-mailbox` @ `d15b8a4`, built per `queue_bench_runbook.md`.
- `make -C actors/cpp test` → **32/32 passed** before any timing was trusted.
- **The market-data recorder was stopped for these runs.** Load average 1.4–1.7;
  every raw file records `# kaspr running: 0`.
- **3 runs of everything**; tables below are the median across runs, and every
  ranking claim is checked for per-run stability. Raw files in
  `actors/cpp/perf/results/linux_quiet/`.
- `./bench_pingpong 500000 20000 all` + `./bench_pingpong 300000 10000 grouped`,
  matching the article's parameters.
- **Unpinned**, deliberately: fan-in spawns 32 producer threads, so `taskset -c 2`
  would invalidate the regime under test. Placement is studied separately below.

Two notes on the harness itself:

- The article says fan-in uses `hardware_concurrency − 2`. The code is
  `P = min(32, max(2, hw-2))` — **capped at 32**. On a 64-thread box that is 32
  producers, not 62. Worth correcting in the article text.
- `steady_clock` resolution here quantises to ~10 ns, so every `fast_send` row
  reading 30 ns is resolution-bound, not a measurement. The amortised figures
  are the real ones.

## Transport baseline

| mode | p50 | p99.9 | amort |
|---|---|---|---|
| send ungrouped | 7170 | 9891 | 7208.8 |
| send grouped | 90 | 140 | 114.1 |
| fast_send | 30 | 31 | 56.0 |
| direct call (base) | 20 | 20 | 40.6 |

The article's "no queue at all is 55× faster than the cheapest queued path"
becomes **larger** here, but see the placement section — the denominator is not
a stable quantity on this box.

## Regime 1 — solo (window=1, cross-thread): DOES NOT REPRODUCE

The article reports BQueue winning at 2250 ns with the other three losing by
~1.5×, and builds the "simplicity wins" argument on it. Unpinned, 3 runs:

| mailbox | run 1 | run 2 | run 3 | median |
|---|---|---|---|---|
| BQueue | 6700 | 3610 | 3550 | 3610 |
| BQueueBatched | 6780 | 7160 | 6660 | 6780 |
| ShardedBQueue | 3431 | 7280 | 7010 | 7010 |
| LockFreeMPSC | 7340 | 3640 | 6860 | 6860 |

**The winner changes between runs** (ShardedBQueue, BQueue, BQueue) and single
rows swing 2.1×. Every value is either ≈3,500 or ≈7,000 — bimodal. Reporting a
ranking off one run of this would be reporting noise.

### The solo number measures thread placement, not the queue

Pinning the process to specific CPUs, 3 reps each, `solo1` p50:

| placement | BQueue | Batched | Sharded | LockFree | spread |
|---|---|---|---|---|---|
| one core (`-c 2`) | 1680 | 1700 | **1470** | 1630 | 1.07× |
| SMT pair (`-c 2,34`) | **3240** | 3260 | 3340 | 3230 | 1.03× |
| same CCD (`-c 2,3`) | 3540 | 3530 | 3440 | **3380** | 1.07× |
| cross-CCD (`-c 2,6`) | 6720 | 6830 | 1510 | 1700 | 4.9× (bimodal) |
| cross-NUMA (`-c 2,26`) | 1470 | 1480 | 1520 | 1710 | 5.1× (bimodal) |

Placement moves the number **2.2× (1470 → 3240 → 7000)**. The four queues differ
by **3–7%** within any controlled placement. The placement effect is an order of
magnitude larger than the effect the article is ranking on.

**Putting both threads on one core is the fastest configuration**, which inverts
the usual cache intuition. It should not: a window=1 ping-pong has zero
parallelism — the two threads strictly alternate — so a same-CPU context switch
beats an inter-processor interrupt plus a cache-line transfer between cores. The
bimodality in the unpinned and 2-CPU-cpuset cases is consistent with the
scheduler sometimes co-locating both threads and sometimes not; a 2-CPU cpuset
still lets it choose. (Mechanism not confirmed — `wake_affine` is the obvious
suspect and was not instrumented.)

**And the ranking flips with placement.** ShardedBQueue is stably *best* on one
core (1470 vs BQueue 1680, 3/3 reps) and stably *worst* on an SMT pair (3340 vs
3230). Same machine, same binary, same benchmark.

So: the article's Regime 1 claim is not refuted, but it is **not measurable on
this box**. On a 32-core multi-CCD server, "which mailbox is fastest for a
one-message round trip" is dominated by where the two threads land.

## Same thread, no wakeup (grouped): REPRODUCES

Dedicated `300000 10000 grouped` run, 3 reps, stable:

| mailbox | Linux p50 | M3 p50 (article) |
|---|---|---|
| **LockFreeMPSC** | **70** | **84** |
| BQueue | 90 | 125 |
| BQueueBatched | 90 | 125 |
| ShardedBQueue | 190 | ~200 |

Clean reproduction — same ranking, same shape, Linux modestly faster. This is
the article's strongest result and it travels. With one thread there is no
placement question, which is exactly why it is stable.

## Regime 2 — burst of 16: DOES NOT REPRODUCE

The article: all four within a few percent (223.5–246.4 ns/msg, a 1.10× spread),
concluding "the mailbox type barely matters when only one thread is pushing."

| mailbox | Linux amort | M3 amort (article) |
|---|---|---|
| BQueue | 605.8 | 229.6 |
| BQueueBatched | **584.9** | **226.0** |
| ShardedBQueue | 983.1 | 246.4 |
| LockFreeMPSC | 672.0 | **223.5** |

Spread here is **1.68×**, not 1.10×, and ShardedBQueue is stably worst in all 3
runs — 63% behind BQueueBatched, against 9% on the M3. The convergence claim is
an M3 property, not a general one. (BQueue vs BQueueBatched is a genuine tie:
they trade places between runs and sit within 4%.)

Absolute throughput is also **2.6× worse** than the M3 (585 vs 226 ns/msg), which
is the recurring theme below: this box is worse at cross-thread handoff and
better at contention.

## Regime 3 — fan-in, 32 producers: REPRODUCES, AMPLIFIED, AND ONE CLAIM BREAKS

32 producer threads into one consumer, vs 6 on the M3. Every ranking here was
**stable across all 3 runs**. `p50`/`p99` are push latency; `amort` is throughput.

| mailbox | push p50 | push p99 | p99.9 | amort |
|---|---|---|---|---|
| BQueue | 12180 | 69850 | 102830 | 493.9 |
| BQueueBatched | 14000 | 72441 | 105881 | 581.8 |
| **ShardedBQueue** | **2210** | **6600** | **11980** | **136.7** |
| LockFreeMPSC | 2991 | 16480 | 32670 | 175.0 |

**ShardedBQueue sweeps every column**, by 5.5× on median, 10.6× on p99, 8.6× on
p99.9 and 3.6× on throughput. The article's core thesis — shard the mailbox when
many threads write to it — is not just confirmed but far stronger at 32
producers than at 6. The prediction in the closing paragraph was right.

Two of the article's specific claims do not survive:

**1. "BQueue has the best median — 42 ns."** This is the article's closing line
("*the number to remember: 42 ns median, 24,083 ns p99*") and the rhetorical
spine of the whole piece: the median looks great, the tail kills you. At 32
producers **BQueue has the worst median of the four** — 12,180 ns, 5.5× worse
than ShardedBQueue. It is worst at p50, p99, p99.9 *and* throughput
simultaneously. There is no seductive median left to warn anyone about.

The 42 ns has not vanished, it has moved: BQueue's `min` here is **20 ns** — the
uncontended push is still cheap. With 6 producers the lock is usually free, so
that cost lands on the median. With 32 it is essentially never free, so it lands
on the minimum. **"Medians lie" is true; which statistic does the lying is a
function of your core count.**

**2. "LockFreeMPSC degrades most gracefully deep in the tail (p99.9)."** On the
M3, LockFree's 16,375 ns p99.9 beat ShardedBQueue's 33,625. Here it **inverts**:
ShardedBQueue 11,980 vs LockFreeMPSC 32,670 — almost exactly the same two
numbers with the labels swapped. LockFree is also the least stable row measured
(p99.9 spread 1.8× across runs: 30,540 / 32,670 / 55,270). It is unstable in the
grouped table too — best p50 of the four at 70 ns, worst `max` of the four at
16,060 / 13,950 ns in two of three reps, vs BQueue's 2,350–9,060.

## Summary against the article's table

| regime | article says | this box |
|---|---|---|
| solo (cross-thread, 1 msg) | BQueue, by ~1.5× | **not measurable** — placement swamps it 2.2×, ranking flips |
| grouped (same thread) | LockFreeMPSC | **LockFreeMPSC** ✓ |
| burst throughput | ~tie, all within 1.10× | **not a tie** — 1.68× spread, Sharded stably worst |
| fan-in p99 + throughput | ShardedBQueue | **ShardedBQueue** ✓, by 10.6× and 3.6× |
| fan-in deep tail (p99.9) | LockFreeMPSC | **ShardedBQueue** ✗ inverted |
| "BQueue: best median, awful p99" | the headline | ✗ **worst median of the four** at 32 producers |

The article's thesis — *there is no fastest queue, the ranking inverts with the
regime* — is if anything **strengthened**: it inverts across machines too, and
two of its own five rows flip when the core count changes. What does not survive
is any specific number, and in particular the 42 ns median it closes on.

## What is not controlled here

- `nohz_full=`/`rcu_nocbs=` are empty in `/proc/cmdline` and `chrt` is denied
  (`RLIMIT_RTPRIO=0`), so `max` columns remain scheduler artifacts and are not
  quoted above except where noted as instability.
- The fan-in and burst tables are unpinned. Pinning them is not obviously
  meaningful (32 producers), but NUMA-aware placement of the consumer is
  untested and the solo result suggests it would matter.
- One machine, one compiler, one build. `-march=native` on Zen 4.
