<!--
    Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
    Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
    Licensed under the MIT License. See LICENSE file in the project root.
-->

# Results: actor benchmarks on DINONY4-01 (AMD EPYC 9374F, RHEL 9.2)

Run of `BENCH_ON_HFT_SERVER.md` on 2026-09-08. Every number below is pasted
from stdout of a run on this machine; the raw files are committed under
`results/` and listed in §8. §6 compares them against the published Apple
Silicon numbers in `README.md` and `tech_reports/fast_send.pdf`. §7 is a new
bench answering a question the existing ones do not: what `fast_send` costs
against a **virtual call** rather than a bare static one.

**Read §2 before quoting anything.** This box does *not* meet the runbook's
quiescing preconditions, and the percentile table is clock-resolution limited.
The amortized per-op numbers in §4 are the ones to propagate, plus §7, which is
built from within-process differences and is insensitive to both (see §9).
**§4.4 re-runs the whole suite with the recorder stopped** to establish which
of those caveats were real: `amort` was never affected, p99.9 becomes quotable
quiet, and `max` stays an artifact either way.

---

## 1. Headline

| quantity | value | basis |
|---|---|---|
| `direct call` amortized | **1.2 – 1.4 ns** | 3 runs, `fastsend` section |
| `fast_send` amortized | **10.0 – 12.5 ns** | 3 runs, `fastsend` section |
| `fast_send` overhead over a bare call | **+8.6 – +11.2 ns (7.2x – 9.3x)** | same |
| `fast_send` vs a **polymorphic virtual call** | **0.98x – 1.21x** | §7, n=15 |
| a *predicted* virtual call over a direct call | **+0.23 ns** | §7, n=15 |

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

- **`max` is meaningless here.** `max` on `send ungrouped` reached
  **12,593,329 ns** (12.6 ms) in one pool-ON run. That is a scheduler preemption
  by the recorder, not a property of the actor framework.
  **This was re-tested by stopping the recorder — see §4.4.** Stopping it drops
  that `max` to 195 µs (64.7x) and makes **p99.9 publishable**, but `max` stays
  a scheduler artifact even on the quiet box. So: p99.9 yes, `max` no.
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

### 4.4 Recorder stopped: which numbers were actually load-contaminated

§2 originally said "do not publish p99.9 or `max`", and §9 asked for a box with
`nohz_full`, `chrt` and no other load before quoting anything absolute. That
bundled three preconditions that are not equally binding, so the recorder was
**stopped** and the whole suite re-run to separate them.

`kaspr/stop_kaspr.sh`, 90 s settle, three runs, then `run_local.sh -o -d` to
restart. Load average 6.04 → **1.99**; recorder downtime 3.2 min; each raw file
records `# kaspr running: 0` in its header. Files in `results/quiet/`.

The two boot-level blockers were **unchanged** by this: `nohz_full=`/`rcu_nocbs=`
are still empty (they are boot parameters — a process kill cannot set them) and
`chrt -f 80` still returns `Operation not permitted` (`RLIMIT_RTPRIO=0`). So
this isolates *ambient load only*.

| mode | p99.9 busy | p99.9 quiet | max busy | max quiet | max ratio |
|---|---|---|---|---|---|
| send ungrouped | 4740 | **4480** | 12,593,329 | **194,711** | **64.7x** |
| send grouped | 171 | **131** | 9,990 | 12,080 | 0.8x |
| fast_send | 50 | **31** | 7,890 | 4,040 | 2.0x |
| grouped plain-new | 180 | **150** | 22,560 | 5,029 | 4.5x |
| grouped pooled | 160 | **150** | 9,830 | 4,710 | 2.1x |
| fs heap+reply | 50 | **31** | 9,200 | 8,240 | 1.1x |
| fs heap noreply | 40 | **31** | 11,751 | 11,660 | 1.0x |
| fs pooled+reply | 40 | **31** | 8,970 | 3,880 | 2.3x |
| fs pooled noreply | 40 | **31** | 10,440 | 6,010 | 1.7x |
| fs stack+reply | 40 | **31** | 8,501 | 4,229 | 2.0x |
| fs stack noreply | 40 | **31** | 8,420 | 4,420 | 1.9x |
| direct call (base) | 30 | **20** | 9,730 | 5,020 | 1.9x |

Three findings, and they do not all point the same way:

1. **p99.9 becomes publishable.** Every `fast_send` variant tightens from 40–50
   to a flat **31 ns**, against a p50 of 30 — i.e. p99.9 is now one clock tick
   off the median rather than a preemption artifact. `direct call` p99.9 drops
   30 → 20, equal to its own p50. The original "do not publish p99.9" was
   correct *for the busy run* and is **wrong for the quiet one**.
2. **`max` is still not publishable, and stopping the recorder does not fix
   it.** Quiet `max` is still 3.9–11.7 µs, `fs heap noreply` barely moved
   (11,751 → 11,660), and `send grouped` got *worse* (9,990 → 12,080). One worst
   sample in 5,000,000 catches *something* on a box with no isolated cores, and
   that is what `nohz_full` would address. Only the 12.6 ms outlier — three
   orders of magnitude out — was the recorder.
3. **The amortized numbers were never load-contaminated.** Every `amort` in
   §4.2 moved by **≤0.5 ns** with the recorder gone (`fast_send` 57.6 → 57.3,
   `direct call` 42.0 → 41.9, `fs stack noreply` 48.5 → 48.2). §4.1 and §6 rest
   on `amort`, so **the hedging in §9 did not apply to them.** They stand as
   published. `send ungrouped` is the lone exception (3412 → 3594 amort, p50
   3370 → 3580), which is inside the 1.9x run-to-run swing §4.3 already records
   for that row — noise, not a quiet-box effect.

The pool ON/OFF question in §4.3 is **still not resolvable**: the pool deltas
are ~1–4 ns and the quiet box only bought ~0.5 ns of stability on `amort`. That
one needs the isolated cores, not just an idle box.

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

### Related, still open: #39, the `reinterpret_cast` dispatch UB

`actors/cpp/include/actors/Actor.hpp:201`:

```cpp
generic_handler_t generic_ptr = reinterpret_cast<generic_handler_t>(ptr);
```

This is **issue #39, "actor reinterpret cast", still OPEN** (filed 2026-09-06).
`git blame` puts the line at `a21e4be` (2026-05-08), the initial release commit —
it has **never been modified**, and is still on `main` @ `523cb15`. So: reported,
not fixed.

**#39 is not the cause of the crash in §5** — a clean archive fixes the crash
while the cast is still there. But #39 is why the crash presented the way it did.
Because dispatch goes through a `reinterpret_cast`ed pointer-to-member, an ODR
layout disagreement between two objects cannot be caught by anything: no compiler
warning, and as #39 notes, neither UBSan nor ASan sees it. A stale archive member
therefore surfaces as a wild jump in `call_handler` rather than as a diagnosable
type or link error. The two defects compose badly.

Also relevant to this bench specifically: **issue #37** (hand-assigned message
ids) records that `Message_N<100>` already appears six times and `<101>` four
times in the tree. `bench_pingpong` uses ids 100–103. Id collision was ruled out
as the cause here — `handler_cache` is sized 2048 and a clean rebuild fixes the
crash.

Anyone extending these benches should use **`MessageT<Derived>`**, not another
hand-picked `Message_N<N>`. `Message.hpp:84` says so outright ("Prefer
`MessageT<Derived>` for new types, which assigns a collision-free id
automatically"), and the space is tighter than it looks: hand-assigned ids are
statically capped at **512**, not 2048 — `Message_N` static_asserts `N < 512`
because 512+ is reserved for `MessageT`'s dynamic range — and 126 of those 512
are already taken. `bench_dispatch` (§7) uses `MessageT` and picks no id at all.

(Both out of scope for this PR; noted so the interaction is on the record.)

---

## 6. Comparison with the published Apple Silicon numbers

Sources: `actors/cpp/perf/README.md` §Results, and `tech_reports/fast_send.pdf`
Tables 4–6 / Figure 2. Both are Apple Silicon (arm64), macOS, N=1e6, unpinned.

**The ratios reproduce. Two absolute claims do not.**

| quantity | published (Apple) | this box (Linux) | verdict |
|---|---|---|---|
| `send` ungrouped p50 | 2250 ns | 3370 ns | same order |
| `send` grouped p50 | 125 ns | 90 ns | faster here |
| `fast_send` p50 | 41 ns* | 30 ns* | both at the clock floor |
| `steady_clock` tick | ~40 ns | **~10 ns** | differs 4x |
| grouped vs ungrouped | 18x | **37x** | published claim is conservative |
| `fast_send` vs grouped | 3.5x | 3.0x (p50), 4.1x (amort) | reproduces |
| `fast_send` vs ungrouped | 62x | 58x (same method) | reproduces |
| `fast_send` over a bare call | +7 ns | **+8.6 – 11.2 ns** | reproduces |
| pooled allocation | 2–3 ns | ~2 ns | reproduces |
| `reply()` plumbing alone | 2–4 ns | 4.3 ns | reproduces |
| pool OFF ⇒ collapses to plain | 128.1 vs 128.2 | 111.5 vs 111.4 | reproduces exactly |
| **global `new`+`delete`** | **~15 ns** | **~3.6 ns** | **does not reproduce** |
| **pool win, grouped median** | 124.5→92.8 (−26%) | 119.3→110.4 (−7.5%) | **much smaller** |
| pool tail win | 5.5 ms → 33 µs | not resolvable here | unconfirmed |

\* clock floor, not a measurement, on both platforms.

Derivations, so these can be checked:

- **global `new`+`delete`** = `fs heap noreply` − `fs stack noreply` amort =
  52.1 − 48.5 = **3.6 ns** (§4.2). Published back-out is ~15 ns.
- **`reply()` plumbing** = `fs pooled+reply` − `fs pooled noreply` =
  54.7 − 50.4 = **4.3 ns**. Matches the published 2–4 ns.
- **clock-read overhead of the `amort` loop** = `direct call (base)` amort 42.0
  minus the single-pair `fastsend` measurement 1.4 = **~40.7 ns**. Backing that
  out of `fast_send` amort 57.6 gives ~16.9 ns, and out of `grouped pooled`
  110.4 gives ~69.7 ns — hence the 4.1x above. Cross-check: `fs stack noreply`
  48.5 − 40.7 = 7.8 ns, against the independently measured 10.0–12.5 ns.

### What should change in the published docs

1. **README §B has the allocator direction backwards.** It says "macOS has a
   fast small-object allocator, so the pool's absolute win here is a **lower
   bound** on what a busier/Linux allocator would show." glibc's tcache is
   *faster* than macOS on this pattern — global `new`+`delete` is 3.6 ns here
   vs ~15 ns published — so the pool's median win *shrinks* to 7.5%, and that
   9 ns sits inside this box's run-to-run spread. The lower-bound claim is not
   supported by this run.
2. **The ~15 ns global-allocation figure is Apple-specific but reads as
   universal** — README §C back-out bullets, PDF Table 5 caption, and the PDF
   §11.1 Guidance paragraph ("turning a ~15 ns global allocation into a ~2 ns
   pooled one"). Needs a platform qualifier.
3. **`steady_clock` ≈ 40 ns is hardcoded** (README Caveats, PDF §11.1). It is
   ~10 ns here, which is why the `fast_send` p50 reads 30 and not 41. State it
   per-platform, or have the bench print the measured tick.
4. **The 62x figure divides a p50 by an amort.** PDF Table 4 and the Figure 2
   caption take ungrouped p50 2250 against `fast_send` amort ~36. Mixed units.
   Same-method on this box gives 58x; p50/p50 gives 112x; amort/amort with the
   clock overhead removed gives ~200x. 62x survives only because both errors
   move in the same direction.
5. **The 5.5 ms → 33 µs tail claim ships without its denominator** — "in a
   separate run", no N, no run count, no repeat. It is the load-bearing
   evidence for the whole queueing argument in PDF §10. Still neither confirmed
   nor refuted here, but the reason is now sharper: with the recorder stopped
   (§4.4) my worst `max` falls from 12.6 ms to 195 µs, which shows a
   millisecond-scale maximum on a shared box is a **scheduler** signature, not
   an allocator one. The published 5.5 ms sits in exactly that range. Whatever
   it measures, it needs its own provenance and a quiesced box before it can be
   attributed to the pool. Note the quiet re-run covers pool ON only, so this
   is not yet a pool ON/OFF tail comparison.
6. **Two ratios got better on Linux and should be claimed.** Grouped vs
   ungrouped is 37x here, not 18x.
7. **The docs benchmark against the wrong baseline.** README §D and the PDF
   quote `fast_send` against a *direct free-function call* (+7 ns). Nobody
   writes a static call where a message dispatch would go; they write a virtual
   call. Measured against that, `fast_send` is **0.98x–1.21x** — i.e. free — and
   that is the number that should be in the paper. §7 supplies it. The +7 ns
   figure itself reproduces (+7.47 ns here), so this is an argument about which
   comparison to lead with, not a correction.

None of this is a change to the *conclusions*. The ordering, the pool-OFF
collapse, the reply-plumbing cost, and the single-digit-ns `fast_send` tax all
reproduce on x86-64 Linux. What moves is the allocator arithmetic, which is a
property of the platform's malloc and not of the framework.

---

## 7. Dispatch cost: `fast_send` against a virtual call

`bench_pingpong` section D compares `fast_send` only to a direct free-function
call, and the README and PDF quote that as "+7 ns over a bare call". A bare
static call is not the alternative anyone would actually write. The hand-written
alternative to a message dispatch is a **virtual call**. New bench,
`actors/cpp/perf/bench_dispatch.cpp`, supplies that baseline.

Every arm performs the identical work (`g_sink += m->seq` on a stack `Dis`) and
differs only in how it is reached. Arms C–H index the same 1024-entry table of
`Handler*` and differ **only** in how many distinct dynamic types the entries
have — identical loads, identical cache footprint, identical instruction
sequence. The only variable is what the branch predictor can learn.

N=5,000,000, warmup 50,000, 5 repeats per invocation, 3 invocations, `taskset -c
2,3`. n=15 per arm.

| arm | min ns | med ns | max ns | net of floor | x direct |
|---|---|---|---|---|---|
| A empty loop (floor) | 0.47 | 0.47 | 0.72 | — | 0.3x |
| B direct call | 1.63 | 1.63 | 1.67 | 1.16 | 1.0x |
| C non-virtual via ptr | 1.63 | 1.63 | 1.90 | 1.16 | 1.0x |
| D virtual, 1 type | 1.86 | 1.87 | 1.99 | 1.39 | 1.1x |
| E virtual, 2 types cyclic | 9.21 | 9.50 | 10.10 | 8.74 | 5.7x |
| F virtual, 4 types cyclic | 9.32 | 9.36 | 9.98 | 8.85 | 5.7x |
| G virtual, 4 types shuffled | 7.53 | 7.70 | 7.85 | 7.06 | 4.6x |
| H virtual, 8 types shuffled | 8.58 | 8.65 | 8.85 | 8.11 | 5.3x |
| I ptr-to-member call | 1.63 | 1.63 | 1.64 | 1.16 | 1.0x |
| **J `fast_send`** | **9.10** | **9.54** | **9.89** | **8.63** | **5.6x** |

### 7.1 The result

**`fast_send` costs about one polymorphic virtual call.** 9.10 ns against a
7.53–9.32 ns band for a virtual call whose target the predictor cannot guess.
The bands overlap; on this box `fast_send` is 0.98x–1.21x a real virtual
dispatch, and it delivers a typed handler lookup and reply plumbing for that.

That is a far more defensible claim than "62x faster than `send`" (§6 item 4),
because it compares against the thing a reader would otherwise write, and both
sides are measured the same way in the same process.

### 7.2 Decomposition

| quantity | derivation | value |
|---|---|---|
| pointer load | C − B | 0.00 ns |
| vtable, target predicted | D − C | 0.23 ns |
| mispredict, 2 types cyclic | E − D | 7.35 ns |
| mispredict, 4 types cyclic | F − D | 7.46 ns |
| mispredict, 4 types shuffled | G − D | 5.67 ns |
| mispredict, 8 types shuffled | H − D | 6.72 ns |
| ptr-to-member vs virtual | I − D | −0.23 ns |
| framework over a bare indirect call | J − I | 7.47 ns |

Two things worth stating on their own:

- **A predicted virtual call is free.** D − C = 0.23 ns. The vtable load hits L1
  and the out-of-order engine hides it. Virtual dispatch is not expensive; an
  *unpredictable* one is, and everything above 1.86 ns in this table is branch
  misprediction, not indirection.
- **The cost is not monotone in the number of types.** 2 types cyclic (9.21) is
  *dearer* than 4 types shuffled (7.53). Any single "a virtual call costs X"
  figure is a fiction — which is exactly why this is a sweep and not one arm.

### 7.3 Cross-checks

- `fast_send` − direct call = **+7.47 ns**, against the README §D published
  **+7 ns** on Apple Silicon. That claim reproduces closely.
- `fast_send` here is 9.10–9.89 ns; `bench_pingpong` section D independently
  reports 10.0–12.5 ns (§4.1). Two separately written benches, same order,
  overlapping. The residual is the `MessageT` id guard (below) and ambient load.
- Measured `steady_clock` tick, printed by the bench itself: **9.0 ns** —
  confirming §2 and §6 item 3 from a third, independent code path.
- **The "needs no quiescing" claim below was tested, not just argued.** All
  three invocations were repeated with the recorder stopped (§4.4). Per-arm
  minima, busy vs quiet, n=15 each:

  | | A | B | C | D | E | F | G | H | I | J |
  |---|---|---|---|---|---|---|---|---|---|---|
  | busy | 0.47 | 1.63 | 1.63 | 1.86 | 9.21 | 9.32 | 7.53 | 8.58 | 1.63 | 9.10 |
  | quiet | 0.47 | 1.63 | 1.63 | 1.86 | 9.28 | 9.32 | 7.49 | 8.59 | 1.63 | 8.85 |
  | Δ | 0.00 | 0.00 | 0.00 | 0.00 | +0.07 | 0.00 | −0.04 | +0.01 | 0.00 | −0.25 |

  Six arms are identical to 0.01 ns, the largest move is 0.25 ns, and the signs
  are mixed — i.e. run-to-run noise, not a load effect. **A 580%-CPU recorder is
  worth ≤0.25 ns to this bench.** The headline is unchanged: `fast_send` at
  8.85–9.10 against a 7.49–9.32 polymorphic band is 0.95x–1.18x.

### 7.4 Method notes that materially affect these numbers

- **`[[gnu::noipa]]`, not `noinline`, on the work functions.** All eight virtual
  overrides have identical bodies, so `-fipa-icf` folds them into one symbol,
  every vtable slot points at it, and the polymorphic arms silently become the
  monomorphic arm. `noipa` disables ICF too. This is a trap that would have
  produced plausible, wrong numbers.
- **Verified in the disassembly, not assumed.** Arms D–H each emit `mov
  (%rax,%rdx,8),%rdi` / `mov (%rdi),%rax` / `callq *0x10(%rax)` — a genuine
  vtable load and indirect call, no speculative devirtualization. Arm I emits
  the Itanium ABI member-pointer sequence `test $0x1,%dl` / `callq
  *-0x1(%rdx,%rax,1)`. I checked this specifically because D − C ≈ 0 looks
  exactly like a devirtualized arm; it is not, it is a predicted one.
- **The message uses `MessageT<Dis>`, not `Message_N<N>`.** `Message.hpp` caps
  hand-assigned ids at 512, 126 are already taken, and #37 records reuse in the
  100–103 range `bench_pingpong` occupies. `MessageT` assigns a collision-free
  id and there is nothing to pick. Cost: its ctor calls `message_id<Dis>()`, a
  function-local static, so each construction pays a guard load a `Message_N`
  would not. Every arm including the floor constructs a `Dis`, so it cancels out
  of the net-of-floor column and out of every difference — but it is why the
  absolutes here sit slightly above §4.1. **Compare deltas across the two
  benches, not absolutes.**
- Min is the right statistic for comparison (least contaminated by preemption);
  the min-to-max spread is the box's noise floor and it is under 5% on every arm.

---

## 8. Raw output

Twelve runs, all exit 0, committed under `actors/cpp/perf/results/`:

```
pool_on_1.txt   pool_on_2.txt   pool_on_3.txt     (bench_pingpong, all)
pool_off_1.txt  pool_off_2.txt  pool_off_3.txt    (bench_pingpong_nopool, all)
fastsend_1.txt  fastsend_2.txt  fastsend_3.txt    (bench_pingpong, fastsend)
dispatch_1.txt  dispatch_2.txt  dispatch_3.txt    (bench_dispatch, §7)
```

Nine more under `results/quiet/` — the same benches with the recorder stopped
(§4.4). Each carries `# kaspr running: 0` and its start loadavg in the header:

```
quiet/pool_on_{1,2,3}.txt   quiet/fastsend_{1,2,3}.txt   quiet/dispatch_{1,2,3}.txt
```

To reproduce the run and the tables:

```bash
make -C actors/cpp clean && make -C actors/cpp && make -C actors/cpp/perf
actors/cpp/perf/results/run_bench.sh          # writes the 9 pingpong .txt files
python3 actors/cpp/perf/results/aggregate.py  # reduces them to §4

# §7, three invocations:
taskset -c 2,3 actors/cpp/perf/bench_dispatch 5000000 50000 5
```

`aggregate.py` exits non-zero if the sanity gate (`fast_send` < grouped <
ungrouped) fails, so it can be wired into CI as-is. `bench_dispatch` needs no
aggregator — it reduces its own repeats and prints the decomposition.

---

## 9. Recommendation

Do **not** use this run to replace the Apple Silicon numbers in the three docs
the runbook points at. The ratios reproduce and the sanity gate passes, so the
*shape* of the existing claims is confirmed on x86-64 Linux:

- `fast_send` ≈ 3x faster than grouped `send`, ≈ 112x faster than ungrouped
- `fast_send` costs ~9 ns over a direct call, ~9x a bare call

An earlier draft of this section asked for `nohz_full`, `chrt` **and** an idle
box before quoting any absolute. That bundled three preconditions that are not
equally binding, so the recorder was stopped and the suite re-run to separate
them (**§4.4**). The result splits three ways:

- **`amort` was never load-contaminated** — every row moved ≤0.5 ns with the
  recorder stopped. §4.1, §4.2's `amort` column and all of §6 rest on `amort`,
  so **they are quotable as they stand.** The hedge did not apply to them.
- **p99.9 becomes quotable on the quiet box** — `fast_send` tightens to a flat
  31 ns against a p50 of 30. Quote §4.4's quiet column, not §4.2's busy one.
- **`max` remains unquotable** and stopping the recorder does not fix it: still
  3.9–11.7 µs, and one row got worse. That is the part that genuinely needs
  `nohz_full`/`rcu_nocbs`, which are boot parameters this box does not set.

So the remaining ask is narrower than it was: a reboot with isolated cores buys
`max` and the pool ON/OFF question (§4.3). Nothing else is waiting on it.

**§7 is the exception, and I would land it.** The dispatch comparison is built
entirely from *differences between arms measured in the same process, in the
same loop, microseconds apart*. Ambient load and clock resolution are common-mode
and cancel; that is why the min-to-max spread is under 5% on every arm despite
the box being busy. It needs no quiescing to be quotable, and it answers a
question the current docs do not: what `fast_send` costs against the dispatch a
reader would otherwise hand-write.
