<!--
    Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
    Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
    Licensed under the MIT License. See LICENSE file in the project root.
-->

# actors/cpp/perf — actor-framework microbenchmarks

Latency microbenchmarks for the actor framework's message-passing paths. Each
`bench_*.cpp` builds to its own binary and links `libactors.a`; shared timing and
percentile helpers live in `bench_common.hpp`.

```bash
# build all benches (also builds a pool-OFF variant, see below)
KSPRPROJ=/path/to/kaspar-hft make

# run everything
KSPRPROJ=/path/to/kaspar-hft make run

# or run one bench with custom knobs:  N (measured) warmup section
./bench_pingpong 1000000 10000 all
./bench_pingpong 500000  5000  transport      # just A
./bench_pingpong 500000  5000  alloc          # just B
./bench_pingpong 500000  5000  fastsend       # just C
```

## `bench_pingpong` — round-trip latency

Measures a **sequential** ping → pong → reply round trip (one outstanding
message at a time), so every sample is a clean round-trip latency, not saturated
throughput. Three sections:

- **A. Transport** — `send` on separate threads (ungrouped), `send` in one
  `Group` (grouped), and `fast_send`; heap-allocated messages, with a reply.
- **B. Allocation** — grouped send with plain (`new`/`delete`) vs MemoryPool
  messages; isolates the per-message allocation cost.
- **C. fast_send variants** — heap vs pooled vs stack message, and with vs
  without a reply.

### Metrics

`p50/p90/p99` and `mean` are per-sample: each round trip is bracketed by two
`steady_clock` reads. `amort(ns)` is the measured phase's total wall time divided
by the sample count, captured with a **single** clock-read pair — it is free of
per-sample clock quantization and resolves costs below the clock tick.

**Read the right column:**
- **Transport (async `send`)** — use `p50`/`p99`. The values (µs / ~125 ns) are
  well above the clock floor, so the distribution is meaningful.
- **Allocation and fast_send** — use `amort`. `fast_send` is so cheap that
  per-sample timing quantizes to the ~40 ns `steady_clock` tick (hence the
  identical `p50 ≈ 41` rows); `amort` sees through that.

## Results

Apple M3 (8-core, arm64), macOS, `-O3 -march=native`, native (not emulated), no CPU
pinning, N = 1,000,000, warmup = 10,000. **Indicative, not a spec** — absolute
numbers move with hardware, allocator, and turbo state; the *ratios* are the
point. A second run on x86-64 Linux (AMD EPYC 9374F) is in
[§ Second data point](#second-data-point-x86-64-linux) — the ratios reproduce,
but the allocator and clock-resolution absolutes differ materially, so read that
before quoting any single figure. **Note on the allocator:** glibc's small-object
allocator (tcache) is *faster* than macOS's on this pattern (a global `new`+`delete`
is ~3.6 ns on the Linux box vs ~15 ns here), so the pool's *median* win is
**smaller** on Linux, not larger — its durable benefit is the allocator **tail**,
which a quiesced box is needed to measure.

### A. Transport (p50 round-trip)

| mode | p50 (ns) | p99 (ns) | vs previous |
|---|---:|---:|---|
| `send` ungrouped (2 threads) | 2250 | 6750 | — |
| `send` grouped (1 thread)    | 125  | 125  | **18× faster** |
| `fast_send` (inline)         | 41\* | 42\* | ~3× faster again |

\* at `steady_clock` resolution; the real cost is the ~24 ns per-sample mean /
~36 ns amortized. One-way ≈ round-trip / 2 for the symmetric handler.

The ordering is the framework's design showing through: ungrouped pays a
cross-core mailbox wakeup (mutex + condvar) twice per round trip; grouping puts
both actors on one thread+queue so there is no wakeup, only queue push/pop; and
`fast_send` skips the queue entirely, running the handler inline.

### B. Allocation — MemoryPool ON vs OFF (grouped send, amortized ns/round-trip)

The pool is a **compile-time** switch (`DISABLE_MEMORY_POOL`); `make` builds
`bench_pingpong` (pool on) and `bench_pingpong_nopool` (`-DDISABLE_MEMORY_POOL`).
Same pooled message types, both binaries:

| row | pool ON | pool OFF | note |
|---|---:|---:|---|
| grouped, plain `new`/`delete` | 124.5 | 128.1 | global allocator (baseline) |
| grouped, MemoryPool messages  | **92.8** | 128.2 | pool OFF ⇒ collapses to plain |

With the pool **off**, the pooled types cost the same as plain `new` (128 ns),
confirming the define is the only thing changing. With the pool **on**, the two
per-round-trip allocations (Ping + Pong) drop the cost to 92.8 ns — and, in a
separate run, the **tail** dropped from `max` 5.5 ms → 33 µs, because the pool avoids the
occasional global-allocator slow path. That tail is the real point for
low-latency: not the median, the p99.9+.

### C. fast_send variants (amortized ns/op, pool ON)

Each input kind (stack / pooled-heap / global-heap) is measured **with and
without a reply**; the two rows of a pair have identical input handling, so their
difference is exactly the reply (its Pong allocation + the `reply()`/`unique_ptr`
plumbing).

| variant | amort (ns) | reply cost (Δ) |
|---|---:|---:|
| stack input,  no reply | 36 | — |
| stack input + reply    | 51 | **~15** |
| pooled input, no reply | 38 | — |
| pooled input + reply   | 43 | **~4** |
| heap input,   no reply | 52 | — |
| heap input + reply     | 66–78 | ~15–25 |

Backing out the costs:
- **base `fast_send` dispatch ≈ 36 ns** (stack input, no reply — zero allocations).
- **reply() plumbing alone ≈ 2–4 ns** — the *pooled* pair isolates it (its Pong
  alloc is only ~3 ns), so the ~4 ns delta is almost all plumbing.
- **a reply that allocates its Pong from the global heap ≈ 15 ns on macOS** (stack
  pair delta) — i.e. the reply's real cost is dominated by the *allocation*, not the
  `reply()` mechanism. **This figure is allocator-specific:** on the x86-64 Linux
  box (glibc tcache) the same global `new`+`delete` is only ~3.6 ns.
- global `new`+`delete` of a small message ≈ **15 ns (macOS) / ~3.6 ns (glibc)**;
  a pooled alloc ≈ **2–3 ns** on both.

With the pool off, `pooled+reply` rises to match `heap+reply` (its Pong alloc goes
back to the global heap), as expected.

Takeaways for callers on the hot path: prefer `fast_send`; keep the request
message on the **stack** when you can (saves an alloc/free outright); the reply
*mechanism* is nearly free (~3 ns) — its cost is the reply message's allocation,
so allocate replies from the **MemoryPool** to turn a ~15 ns global alloc into a
~3 ns pooled one and to cut the allocator tail.

### D. fast_send vs a bare function call

How much does the actor machinery cost over just calling a function? The bench
times both cleanly (one clock-read pair around a tight loop — no per-iteration
clock reads, so neither number carries the ~28 ns clock overhead the `amort`
column does), doing identical trivial work on a stack input:

| | per op |
|---|---:|
| direct function call (`noinline`) | ~1 ns |
| `fast_send` (dispatch, no reply) | ~8 ns |

So **`fast_send` adds ~7 ns over a bare call** — that ~7 ns is the uncontended
mutex lock/unlock, the message field writes, the `handler_cache[id]`
pointer-to-member dispatch, and the reply-`unique_ptr` wrapping. The ratio prints
as ~4–10× only because the ~1 ns baseline is so small that its measurement is
noisy; the **+7 ns absolute delta is the stable, meaningful figure**. For
context, that ~7 ns is ~1/18 of a same-thread `send` (~125 ns) and ~1/300 of a
cross-thread `send` (~2250 ns) — the framework tax on the fast path is single-digit
nanoseconds.

## Caveats

- **Clock resolution is platform-specific.** `steady_clock` ticks at ~40 ns on
  this macOS box (~9–10 ns on the x86-64 Linux box), so per-sample `fast_send`
  percentiles are quantized — read `amort` for those rows. Don't hardcode the
  tick; the bench prints the measured value.
- **No CPU pinning.** The ungrouped cross-thread number especially will tighten
  with pinned cores and a quiet machine; `add_to_manage_q(actor, {core})` can pin
  (a soft hint on macOS).
- **Sequential, not saturated.** These are latencies with one message in flight,
  not throughput under load.
- **Amortized includes the loop's own two clock reads per iteration**, so its
  absolute value is inflated by that fixed overhead; the *differences* between
  rows are the real allocation costs (the overhead cancels).
- Numbers vary run-to-run (~10–30 % on the mean, driven by the tail); the `p50`
  and the cross-row ratios are stable.

## Second data point: x86-64 Linux

The same benches were run on **AMD EPYC 9374F, RHEL 9.2, g++ 15.2, `-O3
-march=native`**, `taskset` to two physical cores, N = 5,000,000, over two passes:
one with a co-resident market-data recorder running, and one with it stopped (box
settled to loadavg ~2). The box still lacks isolated cores (`nohz_full`/`rcu_nocbs`
are unset boot params) and `CAP_SYS_NICE`, so a full quiesce wasn't possible. What
that means for each metric, from the two passes:

- **`p50`, `p99.9`, and the amortized column are solid.** The `amort` column was
  never contaminated — every row moved ≤ 0.5 ns between passes (e.g. `fast_send`
  57.6 → 57.3 ns). With the recorder stopped, every `fast_send` `p99.9` tightened
  to a flat **31 ns** against a `p50` of 30 (one clock tick, not a preemption
  artifact). So the amort-based figures below and the dispatch result are
  publishable as-is.
- **`max` still is not.** Even quiet, one worst sample in 5 M still catches a
  scheduler stall (3.9–11.7 µs) — that needs isolated cores, a boot-level change.
  The pool's tail-latency win therefore can't be confirmed on this box yet.

Full run, both passes, raw output, and methodology are in
[`BENCH_RESULTS_HFT_SERVER.md`](BENCH_RESULTS_HFT_SERVER.md); the runbook is
[`BENCH_ON_HFT_SERVER.md`](BENCH_ON_HFT_SERVER.md).

**What reproduces** (the point of the exercise — the *shape* of the claims holds
on a different ISA, OS, and allocator):

| quantity | macOS (Apple, unpinned) | Linux (EPYC, pinned) |
|---|---:|---:|
| `send` ungrouped p50 | 2250 ns | 3370 ns |
| `send` grouped p50 | 125 ns | 90 ns |
| `fast_send` p50 | ~41 ns\* | ~30 ns\* |
| `fast_send` p99.9 | — | 31 ns (quiet pass) |
| grouped ÷ ungrouped | 18× | 37× |
| `fast_send` over a bare call (Δ) | +7 ns | +9 ns |
| pooled alloc | 2–3 ns | ~2 ns |
| pool OFF ⇒ pooled collapses to plain | 128.1 ≈ 128.2 | 111.5 ≈ 111.4 |

\* clock floor on both, not a measurement.

**What does *not* travel** (why you can't quote one machine's absolutes as the
spec):

- **`steady_clock` tick:** ~40 ns (macOS) vs **~9–10 ns** (Linux). The bench
  prints the measured value; don't hardcode it.
- **Global `new`+`delete`:** ~15 ns (macOS) vs **~3.6 ns** (glibc tcache) — so the
  pool's *median* win shrinks from ~26 % to ~7.5 % on Linux. The pool's durable
  benefit is the allocator **tail**, which needs isolated cores to measure and is
  not yet confirmed on this box.

**A stronger framing for the fast path** came out of the Linux run's dispatch
sweep (`bench_dispatch`): `fast_send` costs **about one polymorphic virtual call**
(9.1 ns vs a 7.5–9.3 ns band for an unpredictable virtual dispatch) — a
like-for-like comparison against the thing a caller would otherwise write, both
measured the same way in the same process. Prefer that to any "N× faster than
`send`" ratio, which mixes metrics (see below).

## Adding a benchmark

Drop a `bench_<name>.cpp` in this directory; the Makefile globs `bench_*.cpp` and
builds each against `libactors.a`. Reuse `perf::now_ns()`, `perf::LatencyStats`,
and `perf::print_header/print_row` from `bench_common.hpp` so output stays
comparable.

Planned / candidate benches:

- **throughput** — messages/sec under saturation (many in flight), vs the
  sequential latencies here.
- **contention** — N producers into one actor's mailbox; scaling of the
  `BQueue` (and the `ShardedBQueue` / `LockFreeMPSC` mailboxes from PR #20) as
  producers increase.
- **fan-out / fan-in** — one actor to N, and N to one.
- **allocation stress** — MemoryPool vs global under bursty allocation with a
  busier allocator, to size the tail benefit seen in section B.
