<!--
    Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
    Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
    Licensed under the MIT License. See LICENSE file in the project root.
-->

# Results: actor benchmarks on DINONY4-01 (AMD EPYC 9374F, RHEL 9.2)

Run of `BENCH_ON_HFT_SERVER.md` on 2026-09-08. Every number below is pasted
from stdout of a run on this machine; raw files are listed in §6.

**Read §2 before quoting anything.** This box does *not* meet the runbook's
quiescing preconditions, and the percentile table is clock-resolution limited.
Only the amortized per-op numbers in §4 are worth propagating.

---

## 1. Headline

| quantity | value | basis |
|---|---|---|
| `direct call` amortized | **1.2 – 1.4 ns** | 3 runs, `fastsend` section |
| `fast_send` amortized | **10.0 – 12.5 ns** | 3 runs, `fastsend` section |
| `fast_send` overhead over a bare call | **+8.6 – +11.2 ns (7.2x – 9.3x)** | same |

Sanity gate from the runbook — `fast_send` < grouped `send` < ungrouped `send`
— **PASSES** on all three pool-ON runs (p50, ns):

```
pool_on_1: fast_send=30  grouped=90  ungrouped=3360  -> PASS
pool_on_2: fast_send=30  grouped=90  ungrouped=3370  -> PASS
pool_on_3: fast_send=30  grouped=90  ungrouped=3410  -> PASS
```

Ratio, median of medians: grouped `send` is **3x** `fast_send`; ungrouped
`send` is **~112x** `fast_send`.

---

## 2. What this machine could NOT do (read before quoting)

The runbook asks for a quiesced, pinned, isolated box. This one is a **live
market-data recorder**. At run time `kaspr` had 1d16h uptime, 21.8 GB RSS, and
threads resident on all 64 logical CPUs. Load average was 3.96.

| runbook step | status | evidence |
|---|---|---|
| isolated cores (`nohz_full=`, `rcu_nocbs=`) | **not available** | both are *empty* in `/proc/cmdline`; `tuned.non_isolcpus=ffffffff,ffffffff` |
| `cpupower frequency-set -g performance` | **not available** | `intel_pstate=disable` on an AMD box ⇒ no `cpufreq` sysfs at all (`/sys/devices/system/cpu/cpu3/cpufreq/scaling_governor: No such file or directory`) |
| disable turbo | **not available** | same — no `intel_pstate` node |
| `chrt -f 80` (RT priority) | **denied** | `chrt: failed to set pid 0's policy: Operation not permitted` (RLIMIT_RTPRIO=0, sudo needs a password) |
| `taskset` pinning | **works** — no root needed | verified all 3 bench threads on cores 2,3 (see §3) |

Consequences, stated plainly:

- **Tails are meaningless here.** `max` on `send ungrouped` reached
  **8,107,511 ns** (8.1 ms) in one pool-ON run. That is a scheduler preemption
  by the recorder, not a property of the actor framework. Do not publish p99.9
  or `max` from this run.
- **p50/p90/p99 are quantized.** The bench measures with `steady_clock`, whose
  resolution here is ~10 ns. Every `fast_send` row reports p50=30, p90=30 —
  that is 2–3 clock ticks, not a measurement. The bench prints this caveat
  itself. The `amort` column (total elapsed / N) is the only sub-10-ns-resolvable
  figure, which is why §1 quotes it.
- Absolute numbers here are an **upper bound**. On a genuinely isolated core
  with RT priority they should come down and tighten.

---

## 3. Method

```
host        DINONY4-01
CPU         AMD EPYC 9374F 32-Core, 1 socket, 32C/64T, 4 NUMA nodes
            node0 0-7,32-39   node1 8-15,40-47   node2 16-23,48-55   node3 24-31,56-63
            SMT siblings are (n, n+32)
kernel      5.14.0-284.11.1.el9_2.x86_64
compiler    g++ (GCC) 15.2.0  (/usr/local; needs LD_LIBRARY_PATH to its own libstdc++,
            the system /lib64/libstdc++.so.6 lacks GLIBCXX_3.4.32)
repo        branch docs/hft-server-bench-runbook @ a6a49ad, on top of main @ 523cb15
lib flags   -std=c++20 -O3 -march=native -fPIC
bench flags -std=c++20 -O3 -march=native -W -Wall -Wextra
N=5000000  warmup=50000  3 runs per configuration
```

Pinned with `taskset -c 2,3` — **two physical cores on NUMA node0, not SMT
siblings of each other**. This deviates from the runbook's single `CORE=3`
deliberately: the bench spawns real threads (PongActor, DriverActor, bench_group),
and confining a producer and consumer to one core serializes them and measures
context-switch cost instead of transport cost. Verified:

```
pid 2431126's current affinity list: 2,3
  tid 2431126: 2,3
  tid 2431127: 2,3
  tid 2431128: 2,3
```

`sudo chrt -f 80` was dropped — not available (§2).

Aggregation is median-of-medians across the 3 runs, per the runbook.

---

## 4. Numbers

### 4.1 Amortized per-op, `fastsend` section (the figures to quote)

| run | direct call | fast_send | delta | ratio |
|---|---|---|---|---|
| 1 | 1.2 ns | 10.2 ns | +9.0 ns | 8.8x |
| 2 | 1.4 ns | 10.0 ns | +8.6 ns | 7.2x |
| 3 | 1.4 ns | 12.5 ns | +11.2 ns | 9.3x |

Median: direct call **1.4 ns**, fast_send **10.2 ns**, **+9.0 ns / 8.8x**.

### 4.2 Round-trip latency, pool ON (ns; median of 3 run-medians)

| mode | p50 | p90 | p99 | min | mean | amort |
|---|---|---|---|---|---|---|
| send ungrouped | 3370 | 3510 | 3800 | 1380 | 3388.0 | 3412.4 |
| send grouped | 90 | 91 | 100 | 80 | 91.0 | 112.9 |
| fast_send | 30 | 30 | 31 | 20 | 28.3 | 57.6 |
| grouped plain-new | 90 | 120 | 170 | 80 | 97.3 | 119.3 |
| grouped pooled | 90 | 91 | 140 | 80 | 88.5 | 110.4 |
| fs heap+reply | 30 | 30 | 40 | 20 | 28.4 | 57.8 |
| fs heap noreply | 30 | 30 | 31 | 20 | 26.0 | 52.1 |
| fs pooled+reply | 30 | 30 | 40 | 20 | 28.2 | 54.7 |
| fs pooled noreply | 30 | 30 | 31 | 20 | 26.0 | 50.4 |
| fs stack+reply | 30 | 30 | 31 | 20 | 28.4 | 53.6 |
| fs stack noreply | 30 | 30 | 31 | 20 | 26.1 | 48.5 |
| direct call (base) | 20 | 20 | 20 | 9 | 20.1 | 42.0 |

p50 was identical across all 3 runs for every row except `send ungrouped`
(3360–3410).

### 4.3 Round-trip latency, pool OFF (`-DDISABLE_MEMORY_POOL`)

| mode | p50 | p90 | p99 | min | mean | amort |
|---|---|---|---|---|---|---|
| send ungrouped | 3290 | 3440 | 3730 | 1290 | 3317.6 | 3340.5 |
| send grouped | 90 | 91 | 130 | 80 | 91.7 | 113.7 |
| fast_send | 30 | 31 | 50 | 20 | 29.5 | 60.2 |
| grouped plain-new | 90 | 90 | 100 | 80 | 89.7 | 111.5 |
| grouped pooled | 90 | 90 | 100 | 80 | 89.5 | 111.4 |
| fs heap+reply | 30 | 30 | 49 | 20 | 28.7 | 58.3 |
| fs pooled+reply | 30 | 30 | 50 | 20 | 29.0 | 58.7 |
| fs stack noreply | 30 | 30 | 40 | 20 | 26.3 | 48.8 |
| direct call (base) | 20 | 20 | 30 | 9 | 20.2 | 42.2 |

**Pool ON vs OFF is not resolvable on this box.** Every p50 is identical; the
`amort` differences (e.g. fast_send 57.6 vs 60.2) are smaller than the
run-to-run spread. One pool-OFF run had a `send ungrouped` p50 of 1750 vs 3360
in the other two — a 1.9x swing from ambient load alone. Any claim about the
memory pool needs a quiesced box.

---

## 5. Build blocker found: `libactors.a` accumulates stale members

The first build produced binaries that **segfaulted in every section**, pool ON
and OFF, exit 139. Backtrace:

```
#0  actors::Actor::call_handler(actors::Message const*)
#1  actors::Actor::fast_send(actors::Message const*, actors::Actor*)
#2  run_fastsend_heap<Ping, Pong>(...)
#3  main
```

Faulting instruction, from the core in `/home/core`:

```
=> 0x416ac1 <call_handler+65>: mov (%rsi),%rdx     # rsi=0x5da03b0, unmapped
   0x416ac4                    test %rdx,%rdx
   0x416ac9                    add  0x8(%rsi),%rbx  # Itanium ABI ptr-to-member {ptr, adj}
```

It is loading the `{ptr, adj}` member-function-pointer pair out of
`handler_cache[id]`, and the slot address itself is unmapped — i.e. the
`handler_cache` vector was read at the wrong offset in the `Actor` object.

**Root cause: `ar rcs` inserts and replaces, but never removes.**

```
$ ar tv actors/cpp/libactors.a
... Actor.cpp.o          Aug  7 21:49 2026     <-- stale, CMake naming
... Manager.cpp.o        Aug  7 21:49 2026
... (6 more *.cpp.o from Aug 7)
... Actor.o              Sep  8 19:19 2026     <-- current, Makefile naming
... (7 more *.o from Sep 8)
```

Sixteen members where the Makefile lists eight. The `*.cpp.o` set came from an
older `actors/cpp/build/` CMake tree; the `*.o` set from `make`. Different
object names means `ar r` never replaced them, so both generations persist. The
linker resolves `call_handler` from `Actor.cpp.o` — compiled **2026-08-07,
against pre-pull headers** — while `bench_pingpong.cpp` compiled today against
current headers. Different `Actor` layout in the two objects, so the offset of
`handler_cache` disagrees. Classic ODR violation, and it lands on the
`reinterpret_cast` dispatch path where nothing is type-checked.

**Fix:** `make -C actors/cpp clean` (plus `rm -rf actors/cpp/build`) before
building. After that the archive has exactly 8 members and all four sections
run clean.

**This defeated the runbook's own warning.** §2 says:

> If `libactors.a` was already present the perf Makefile will NOT rebuild it
> ... run the explicit `make -C actors/cpp` above first

That is not sufficient. `make -C actors/cpp` *did* run and *did* recompile every
object — the archive was still poisoned, because the stale members have
different filenames and nothing removes them. Suggested runbook/Makefile changes:

1. Runbook §2 should say `make -C actors/cpp clean && make -C actors/cpp`.
2. `actors/cpp/Makefile` should build the archive with `ar Dcrs` after `rm -f
   $(LIB)`, so the archive is always exactly its prerequisites.
3. `clean` should also `rm -rf build` (the CMake tree it currently ignores).
4. Separately: the `$(OBJDIRO)/%.o: %.cpp` rule has **no header dependency
   tracking**. Editing a header does not trigger a rebuild. Add `-MMD -MP` and
   `-include $(OBJS:.o=.d)`. This did not cause the crash above, but it is the
   same failure mode waiting to happen.

Hypotheses tested and eliminated before landing on the above, so nobody repeats
them: message-id overflow of `handler_cache` (ids are 100–103, cap is 2048);
optimization level (`-O0/-O1/-O2/-O3` all fine); `-march=native` / `znver4` /
AVX-512 (all fine); separate translation units, `-fPIC`, and static-archive
linkage as such (all fine — a clean archive built with make's exact
`CXXFLAGS_OPT` works).

### Related, not fixed: the dispatch path is UB by construction

`actors/cpp/include/actors/Actor.hpp:201`:

```cpp
generic_handler_t generic_ptr = reinterpret_cast<generic_handler_t>(ptr);
```

This converts `void (ActorT::*)(const MsgT*)` to `void (Actor::*)(const Message*)`
and later calls through it. That is undefined behaviour; it happens to work only
when `Actor` is a primary base at offset 0 and `Message` likewise for `MsgT`, so
the ABI's `this`-adjustment is zero. `git blame` puts this line at `a21e4be`
(2026-05-08), the initial release commit — it has **never been modified**, and is
still on `main` @ `523cb15`. It is not the cause of the crash in §5, but it is
why the crash presented as a wild jump instead of a diagnosable type error.
(Out of scope for this PR; noted so it is on the record.)

---

## 6. Raw output

Nine runs, all exit 0, saved on the box at `/tmp/kaspar_bench/`:

```
pool_on_1.txt   pool_on_2.txt   pool_on_3.txt     (bench_pingpong, all)
pool_off_1.txt  pool_off_2.txt  pool_off_3.txt    (bench_pingpong_nopool, all)
fastsend_1.txt  fastsend_2.txt  fastsend_3.txt    (bench_pingpong, fastsend)
```

`/tmp` is not durable. If these need to be retained, say so and I will commit
them under `actors/cpp/perf/results/`.

---

## 7. Recommendation

Do **not** use this run to replace the Apple Silicon numbers in the three docs
the runbook points at. The ratios reproduce and the sanity gate passes, so the
*shape* of the existing claims is confirmed on x86-64 Linux:

- `fast_send` ≈ 3x faster than grouped `send`, ≈ 112x faster than ungrouped
- `fast_send` costs ~9 ns over a direct call, ~9x a bare call

But the absolute latencies carry the load of a busy 64-core recorder and the
percentiles are at clock resolution. To get publishable absolutes this needs a
box with `nohz_full`/`rcu_nocbs` set at boot, `CAP_SYS_NICE` for `chrt`, and
nothing else running. Happy to re-run there.
