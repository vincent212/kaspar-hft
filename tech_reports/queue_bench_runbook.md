# Runbook — mailbox queue benchmark on the Linux HFT server

Goal: run the actor-framework mailbox benchmark on the x86-64 Linux server and
report the numbers, to compare against the macOS/Apple-M3 run. This measures the
five message-passing paths: the four mailbox queue types (`BQueue`,
`BQueueBatched`, `ShardedBQueue`, `LockFreeMPSC`) under a 16-deep burst, plus
`fast_send` (the no-queue inline path) as the baseline.

This benchmark is **self-contained**: it builds only `actors/cpp` and links
`libactors.a`. It does **not** need the CME SBE codecs, ZMQ, or the rest of the
tree.

## 0. Prereqs

- The `feat/variant-mailbox` branch checked out.
- A C++20 compiler (g++ 11+ preferred — the dispatch bench relies on
  `[[gnu::noipa]]`, which clang ignores).
- Boost headers (for `boost::circular_buffer`) and Google Test (only if you also
  run the unit tests).
- x86-64 host (the build targets `-march=native`).

```bash
cd <repo>
git fetch origin && git checkout feat/variant-mailbox
export KSPRPROJ=$(pwd)
```

## 1. Build the actor library

```bash
make -C actors/cpp clean
make -C actors/cpp opt        # produces actors/cpp/libactors.a
```

Expect 0 errors, 0 warnings.

## 2. (Recommended) run the queue unit tests

Confirms all four queue types are correct on this platform before trusting the
numbers.

```bash
make -C actors/cpp test        # needs Google Test; runs tests/test_*.cpp
```

Expect `[  PASSED  ] 32 tests` (or more) — in particular the `QueueTest/*`,
`QueueFIFO`, `PopBatch`, and `VariantMailbox` suites.

## 3. Build and run the benchmark

```bash
cd actors/cpp/perf
CXX=g++ make                   # picks up bench_*.cpp, links ../libactors.a

# full comparison — 1,000,000 measured round trips, 20,000 warmup
./bench_pingpong 1000000 20000 all
```

Capture the machine identity alongside the numbers:

```bash
uname -srm
lscpu | grep -E 'Model name|Socket|Core|Thread|MHz'
g++ --version | head -1
```

## 4. What to report back

Paste, verbatim:

1. `uname -srm`, the `lscpu` CPU model line, and the g++ version.
2. The **`batch`** table (the four `burst16 *` rows) — this is the queue-type
   comparison:

   ```
   mode                     p50   p90   p99   p99.9   min   max   mean   amort
   burst16 BQueue          ...
   burst16 BQueueBatched   ...
   burst16 ShardedBQueue   ...
   burst16 LockFreeMPSC    ...
   ```
3. The **`transport`** rows (`send ungrouped`, `send grouped`, `fast_send`) — the
   no-queue / grouped baselines.
4. The `build: MemoryPool ENABLED/DISABLED` line.

## 5. Notes / gotchas

- **amort** (ns/msg) is the throughput number to compare across machines; **p50**
  is the per-message latency under the 16-deep burst. `min`/`max` include
  scheduler noise.
- `fast_send` rows near ~40 ns on macOS are at `steady_clock` resolution; a Linux
  box with a higher-resolution clock may show a truer (lower) number — note the
  clock resolution if it looks quantized.
- If `bench_dispatch` warns about `[[gnu::noipa]]` being ignored, you're on
  clang — rebuild with `CXX=g++` or its dispatch numbers are unreliable (does
  not affect `bench_pingpong`).
- Run on an otherwise-idle box; pin to a core if you can
  (`taskset -c 2 ./bench_pingpong ...`) for a cleaner tail.
