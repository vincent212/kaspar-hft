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

Apple Silicon (arm64), macOS, `-O3 -march=native`, native (not emulated), no CPU
pinning, N = 1,000,000, warmup = 10,000. **Indicative, not a spec** — absolute
numbers move with hardware, allocator, and turbo state; the *ratios* are the
point. macOS has a fast small-object allocator, so the pool's absolute win here
is a lower bound on what a busier/Linux allocator would show.

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

| variant | amort (ns) | what it isolates |
|---|---:|---|
| stack input, **no reply** | 35.9 | base dispatch, zero allocations |
| pooled input + reply      | 41.3 | 2 pooled allocs |
| stack input + reply       | 51.1 | 1 global alloc (the Pong reply) |
| heap input + reply        | 65.8 | 2 global allocs |

Backing out the costs: **base fast_send dispatch ≈ 36 ns**; a global `new`+`delete`
of a small message ≈ **15 ns each**; a pooled alloc ≈ **2–3 ns**. With the pool
off, `pooled+reply` rises to 65.6 ns — identical to `heap+reply`, as expected.

Takeaways for callers on the hot path: prefer `fast_send`; keep the request
message on the **stack** when you can (saves an alloc/free outright); and for
messages that must be heap-lived, use the **MemoryPool** to turn a ~15 ns global
alloc into a ~2 ns pooled one and to cut the allocator tail.

## Caveats

- **Clock resolution.** `steady_clock` ticks at ~40 ns here, so per-sample
  `fast_send` percentiles are quantized — read `amort` for those rows.
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
