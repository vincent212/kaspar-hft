<!--
    Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
    Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
    Licensed under the MIT License. See LICENSE file in the project root.
-->

# Runbook: re-run the actor benchmarks on an HFT server and update the docs

**Audience:** an engineer or a Claude Code instance running on the target
low-latency Linux server. **Goal:** replace the current *indicative* Apple
Silicon / macOS numbers with real, pinned x86-64 Linux numbers, and update the
three places that quote them.

The current numbers were taken on Apple Silicon / macOS with **no CPU pinning**,
so they are explicitly labeled "indicative; ratios are the point." On a quiesced,
pinned HFT box the *absolute* numbers will change (usually lower and far tighter
tails) and the ratios firm up. This runbook produces those numbers rigorously and
propagates them.

---

## 0. Ground rules (do not skip)

- **Never commit to `main`.** Branch, push, open a PR, get it reviewed
  (`/code-review`), then merge. This is a hard repo rule.
- **Do not fabricate or round-trip guess any number.** Every figure that lands in
  a doc must come from a run you actually did on this machine, pasted from stdout.
  If a run looks wrong, investigate — do not "adjust" it.
- **Record the exact machine + method** in the docs alongside the numbers (CPU
  model, kernel, compiler+version, isolated cores, governor state). Numbers
  without provenance are not useful.
- If the expected ordering (`fast_send` < grouped `send` < ungrouped `send`) does
  **not** hold, stop and diagnose — it means pinning, build flags, or the run is
  wrong. Report it; do not publish it.

---

## 1. Machine preparation (for stable, low-variance numbers)

Capture the machine identity first — you will paste this into the docs:

```bash
lscpu | egrep 'Model name|Socket|Core|Thread|CPU max|Flags' | head
uname -r
g++ --version | head -1
```

Quiesce the cores you will pin to (pick 2 isolated physical cores on the same
NUMA node, SMT siblings avoided). Ideally the cores are already `isolcpus=` /
`nohz_full=` at boot. At minimum, for the run:

```bash
# performance governor (kills frequency scaling jitter); requires root
sudo cpupower frequency-set -g performance   || true
# disable turbo so numbers are repeatable (intel_pstate shown; amd differs)
echo 1 | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo 2>/dev/null || true
```

Note whichever of these you could/couldn't set — it goes in the "method" note.

---

## 2. Build

```bash
cd <repo-root>                 # kaspar-hft checkout, on main (or this branch)
export KSPRPROJ=$(pwd)

# actors library (the benches link it) -- build FRESH so you measure current code
make -C actors/cpp KSPRPROJ=$KSPRPROJ

# the benches: builds bench_pingpong (pool ON) and bench_pingpong_nopool (pool OFF)
make -C actors/cpp/perf KSPRPROJ=$KSPRPROJ
```

If `libactors.a` was already present the perf Makefile will NOT rebuild it (it
has no dependency on the actor sources) — run the explicit `make -C actors/cpp`
above first, or `make -C actors/cpp/perf lib`, so you are not benchmarking a
stale library.

---

## 3. Run (pinned, warmed, repeated)

The bench is `bench_pingpong [N] [warmup] [section]`. Pin it to an isolated core
with `taskset`, and raise scheduling priority with `chrt` so it is not preempted.
Run each configuration **at least 3 times** and take the **median** of the
medians; latency micro-numbers are noisy.

```bash
CORE=3            # an isolated physical core
N=5000000         # more samples than the macOS runs (1e6); bump if variance is high
W=50000           # warmup

run() {  # run() <binary> <section>
  sudo chrt -f 80 taskset -c $CORE "$1" $N $W "$2"
}

# pool ON: all sections
run ./actors/cpp/perf/bench_pingpong all
# pool OFF: the allocation section is the one that differs
run ./actors/cpp/perf/bench_pingpong_nopool alloc

# fast_send section alone prints the "fast_send vs direct call" line -- capture it
run ./actors/cpp/perf/bench_pingpong fastsend
```

(Drop `sudo chrt -f 80` if you lack `CAP_SYS_NICE`; still use `taskset`.)

Save the raw stdout of every run (you will cite it in the PR):

```bash
mkdir -p /tmp/kaspar_bench
for i in 1 2 3; do
  run ./actors/cpp/perf/bench_pingpong all      > /tmp/kaspar_bench/pool_on_$i.txt  2>/dev/null
  run ./actors/cpp/perf/bench_pingpong_nopool all> /tmp/kaspar_bench/pool_off_$i.txt 2>/dev/null
done
```

---

## 4. What to collect

From the printed table (`p50 p90 p99 p99.9 min max mean amort`):

| Group | Rows to record |
|---|---|
| **A. Transport** (p50 round-trip) | `send ungrouped`, `send grouped`, `fast_send` |
| **B. Allocation** (amortized) | `grouped plain-new`, `grouped pooled` — **pool ON and pool OFF** (the OFF binary) |
| **C. fast_send variants** (amortized) | `fs heap+reply`, `fs heap noreply`, `fs pooled+reply`, `fs pooled noreply`, `fs stack+reply`, `fs stack noreply`, `direct call (base)` |
| **D. fast_send vs call** | the `fast_send vs direct function call (clean amortized …)` line — record `direct call`, `fast_send`, the `+X ns` delta |

Which metric to quote (unchanged from the macOS methodology):
- **Transport rows:** use `p50` (well above clock resolution).
- **Allocation + fast_send rows:** use `amort` (per-sample quantizes to the clock tick).
- **fast_send-vs-call:** quote the **absolute delta** (`+X ns`), not the ratio —
  the ~1 ns baseline makes the ratio noisy. On Linux with a higher-resolution
  clock (`clock_gettime` ~20 ns granularity is typical), the per-sample rows may
  resolve better than macOS's ~40 ns tick — if so, note it and you may quote p50
  for fast_send too.

Sanity gate before publishing: `fast_send` < grouped `send` < ungrouped `send`;
pool ON `grouped pooled` < pool OFF (which should ≈ plain-new). If not, stop.

---

## 5. Update the docs (branch → PR)

```bash
git checkout main && git pull
git checkout -b perf/hft-server-numbers
```

Three files quote the numbers. Update **all three** consistently, and change the
provenance line from "Apple Silicon / macOS, no pinning" to this server's spec.

### 5a. `actors/cpp/perf/README.md`

- **Section "Results" preamble:** replace the "Apple Silicon (arm64), macOS, …,
  no CPU pinning — indicative" line with the real spec: CPU model, kernel,
  compiler, "pinned to core N with `taskset`/`chrt`, performance governor, turbo
  off", and N.
- **Table A (transport):** new p50s.
- **Table B (allocation, pool ON vs OFF):** new amortized ns and the `max` tail
  figures (the tail is the headline for the pool — report both machines' tails if
  useful, but at least this one's).
- **Section C table + the backed-out costs** (base dispatch, global new+delete ns,
  pooled alloc ns): recompute from the new rows.
- **Section D (fast_send vs bare call):** new `~X ns` call, `~Y ns` fast_send,
  `+Z ns` delta.
- **Caveats:** keep the clock-resolution note but update it if the Linux clock
  resolves finer; the numbers are now pinned, so soften "no CPU pinning."

### 5b. `README.md` (top-level, "Performance Characteristics → Measured")

- The **round-trip table** (ungrouped / grouped / fast_send).
- The **`fast_send` vs bare function call** table and the "+~7 ns" sentence.
- The **Allocation** table (pool ON/OFF, tail).
- The provenance line ("Apple Silicon / macOS … indicative; ratios are the
  point") → the real machine; you can drop or soften "indicative" now that it is
  a pinned measurement, but keep the machine spec explicit.
- Also check the intro "Why actors?" line (`~tens of ns round trip`) and the
  Actor Framework `fast_send()` bullet (`~24 ns round trip`) — update the `~24 ns`
  there if the server number differs.

### 5c. `tech_reports/fast_send.pdf` (compiled from `tech_reports/fast_send.tex`)

The PDF is generated from the `.tex`. Edit the `.tex`, then recompile.

Numbers in `fast_send.tex` to update (grep for them):
- The provenance sentence: "All figures below were taken on Apple Silicon
  (arm64), macOS, … without CPU pinning, over $N=10^6$ samples … indicative
  rather than a specification" → this server's spec + N.
- **Table `tab:transport`:** the `2250` / `125` / `~36` p50s and the `18x` / `~62x`
  speedups (recompute speedups from the new p50s: ungrouped/grouped,
  ungrouped/fast_send).
- The prose figures that repeat them: `2250`~ns, `125`~ns, `~36`~ns, `62x`, `18x`,
  `3.5x`, the fast_send variants table (`35.9`~ns floor, `~15`~ns global alloc,
  `2`--`3`~ns pooled), and the "macOS has a fast small-object allocator …
  $124.5$~ns" sentence (line ~1133) — on Linux the global allocator is typically
  slower, so this comparison may flip; rewrite it to match what you measure.
- The paragraph noting the numbers "were taken on Apple Silicon, not the x86-64
  Linux target" (~line 1171) — this is now the x86-64 Linux target, so update or
  remove that caveat.

Recompile:

```bash
cd tech_reports
latexmk -pdf fast_send.tex   # or: pdflatex fast_send.tex (x2 for refs/bib)
# confirm fast_send.pdf regenerated; commit BOTH fast_send.tex and fast_send.pdf
```

If LaTeX is not installed: `sudo apt-get install -y texlive-latex-base
texlive-latex-extra texlive-fonts-recommended latexmk`. If you cannot build LaTeX
on the box, update `fast_send.tex` anyway and note in the PR that the PDF needs a
recompile elsewhere — do **not** hand-edit the binary PDF.

---

## 6. Open the PR

```bash
git add actors/cpp/perf/README.md README.md tech_reports/fast_send.tex tech_reports/fast_send.pdf
git commit   # message: "perf: real HFT-server numbers (pinned x86-64 Linux)"
git push -u origin perf/hft-server-numbers
gh pr create --title "perf: real HFT-server benchmark numbers" --body "..."
```

In the PR body, paste:
- the machine spec (`lscpu`, kernel, `g++ --version`, pinning/governor state),
- the raw stdout of one representative run per section,
- a note on run-to-run variance (min/median/max of the 3 runs for the headline
  figures),
- confirmation the sanity ordering held.

Then run `/code-review` on the PR before merging.

---

## 7. One-line kickoff (paste to the other instance)

> You are on the target HFT Linux server, in the `kaspar-hft` checkout on `main`.
> Read `actors/cpp/perf/BENCH_ON_HFT_SERVER.md` and follow it end to end: prep the
> machine, build fresh, run the pinned benchmarks, then update
> `actors/cpp/perf/README.md`, the top-level `README.md`, and
> `tech_reports/fast_send.tex` (recompile `fast_send.pdf`) with the real numbers,
> on a branch, and open a PR with the raw run output pasted in. Do not commit to
> `main`; do not invent numbers.
