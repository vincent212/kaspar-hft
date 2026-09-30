# Serial vs parallel decode — wire-to-book latency

## STATUS: PARALLEL DECODE REMOVED

**As of 2026-09-30, parallel decode has been removed from the codebase.** In the
main comparison (§2, book series) serial was 2.0–2.2× faster at p50 and 1.5–3.2×
faster at p99; against the best parallel configuration measured (§8.4b, all six
series) it was 1.7–2.1× faster at p50 and 1.5–3.3× faster at p99. The parallel
path was not validated for correctness, and CME packets carry 1.06–1.10 messages,
so a fan-out has almost nothing to divide. Serial decode is the only path. The
rest of this report is the record of the measurement, as written at the time; its
status notes and production guidance (`cme_decode_workers`) are superseded by the
removal.

---

## Status at the time of measurement (historical, superseded by the removal above)

**Where the code lives.** This report and the code it measures are on
**`mdp3/uniform-decode`** (PR **#127**, open, unmerged). `main` is at `4b6d889`
("restore serial decode as the default, parallel behind a switch", PR #120) and
does **not** contain: the uniform decode path, the treasury asset-lookup fix,
pooled `ParsedMsg` entries, the inline fan-out bypass, or the correctness fixes
from two review rounds. Every file path and line number below refers to this
branch, not `main`.

One set of numbers comes from somewhere else again: **§8.4b** was measured on a
local throwaway branch (`perf/parallel-experiment`, never pushed, now deleted)
carrying an async decode hand-off and `INLINE_MAX_MSGS = 1`. **Recovery was
deliberately broken on it** — a critical decode failure did not trigger recovery,
so the book would diverge silently. Those numbers are a latency measurement only,
and that code is not in PR #127.

**The verdict is not final, and this is not a finished evaluation.** §8 records a
measurement defect affecting two of the four windows, a regime where the parallel
path does win (§8.4b), a hypothesis about `fast_send` that the data did not
support, and five things that would have to happen before any of it settles. The
only conclusion stable enough to act on is the production guidance:
`cme_decode_workers 0`.

> **The parallel decoder is RESEARCH ONLY and must not be used in production.**
> It is slower than serial at every book percentile measured here, and it is not
> correctness-validated: no dual-path replay has confirmed its output matches
> serial, and some message types are not routed on it (stats/volume flush empty
> batches; option and spread definitions are unhandled). Production runs
> `cme_decode_workers 0`. This report exists to record why.

**Measured 2026-09-25, 10:21–11:47 ET, CME MDP3 production, live market.**
Three 900 s windows on one box, one binary, one universe. Serial decode wins on
every book series at every percentile measured. One trade series is the
exception and is called out below.

---

## 1. What the two paths are

`cme_decode_workers` in `cme.ini` selects the path, per channel
(`kaspr.cpp:509`).

**SERIAL (`cme_decode_workers 0`, the default).** `DataDecoder::on_decode_packet`
calls `mbo_data()` inline on the MessageProcessor thread, straight into
`handler_if`. No copy, no allocation, one thread, one orderID map.

**PARALLEL (`cme_decode_workers N`, N a power of two).** Every SBE message is
dispatched to a warm `DecodeWorker` fleet with a dense `order_seq`; each worker
builds `l3` entries into a `ParsedMsg` and sends it to a single `Reconstructor`,
which resequences by `order_seq` and applies in wire order.

Serial is the path the latency article measured. Parallel is opt-in and, per its
own wiring comment, "not yet validated vs serial".

---

## 2. Headline result

Book, full 900 s windows, recovery excluded (see §5). All figures µs.

| series | path | msgs | p10 | p50 | p90 | p99 | p999 | mean |
|---|---|---|---|---|---|---|---|---|
| ES book | **SERIAL** | 446,081 | **4.65** | **6.65** | **10.47** | **19.10** | **39.25** | **7.30** |
| ES book | PAR w=8 | 368,068 | 10.22 | 13.71 | 24.50 | 49.18 | 103.10 | 16.26 |
| NQ book | **SERIAL** | 695,724 | **4.29** | **6.36** | **8.85** | **12.35** | **18.94** | **6.50** |
| NQ book | PAR w=8 | 476,570 | 11.06 | 14.09 | 20.58 | 39.45 | 56.50 | 15.74 |
| ZN book | **SERIAL** | 157,699 | **5.04** | **7.11** | **11.81** | **63.30** | **273.69** | **9.95** |
| ZN book | PAR w=8 | 150,751 | 11.69 | 14.25 | 22.65 | 91.80 | 337.81 | 18.39 |

**Serial is 2.0–2.2× faster at the median and 1.5–3.2× at p99** (ZN book 1.45×, ES book 2.6×, NQ book 3.2×). The serial
medians (6.36–7.11 µs) land on the 6.72/7.29/7.03 µs `qlen==0 AND idx==0`
intercept published in `kaspr/perf/RESULT_qlen_vs_latency.md`, so serial is
behaving exactly as previously measured; parallel is the slow path.

Hot path only (`qlen==0 AND idx==0`), serial:

| | n | p10 | p50 | p99 | p999 |
|---|---|---|---|---|---|
| ES | 382,047 | 4.63 | 6.51 | 14.59 | 21.71 |
| NQ | 544,832 | 4.29 | 6.34 | 11.56 | 15.33 |
| ZN | 122,500 | 5.09 | 6.99 | 15.46 | 26.37 |

### The one exception

**ZN trade p99: serial 268.04 µs vs parallel 236.63 µs — parallel is better.**
It is the only percentile on any series where parallel beats serial, and it does
not generalise: at p999 the order flips back (310.13 serial vs 323.03 parallel),
and ZN trade is the thinnest, most erratic series measured on either path
(serial mean 38.27 µs sits above its own p75). Treat it as one crossover in a
noisy series, not as a case for parallel.

So: **serial wins across the board on book, and on 5 of 6 series overall.**

Full trade table:

| series | path | p10 | p50 | p90 | p99 | p999 | mean |
|---|---|---|---|---|---|---|---|
| ES trade | **SERIAL** | **5.76** | **8.87** | **16.11** | **33.62** | **67.65** | **10.36** |
| ES trade | PAR w=8 | 13.01 | 19.44 | 36.33 | 154.92 | 281.60 | 24.84 |
| NQ trade | **SERIAL** | **4.96** | **7.33** | **11.10** | **22.15** | **42.47** | **8.01** |
| NQ trade | PAR w=8 | 13.02 | 16.69 | 28.92 | 60.74 | 102.22 | 19.73 |
| ZN trade | **SERIAL** | **6.77** | **17.19** | **95.61** | 268.04 | **310.13** | **38.27** |
| ZN trade | PAR w=8 | 14.68 | 31.50 | 104.28 | **236.63** | 323.03 | 48.59 |

---

## 3. Why parallel is slower

**CME packets carry ~1.1 messages.** Measured from the `.arr` per-packet log over
1.31 M packets:

| | packets | msgs/pkt mean | p50 | p90 | p99 | max | ≤8 msgs |
|---|---|---|---|---|---|---|---|
| ES | 441,446 | 1.09 | 1 | 1 | 4 | 34 | 99.611% |
| NQ | 715,959 | 1.06 | 1 | 1 | 3 | 40 | 99.128% |
| ZN | 155,890 | 1.10 | 1 | 1 | 2 | 42 | 99.577% |

`dispatch()` round-robins messages across workers with `i & worker_mask_`. At one
message per packet, **every packet dispatches to exactly one worker** — there is
no fan-out, only its cost. Per packet or per message, parallel pays:

| cost | site | per |
|---|---|---|
| 1,500-byte packet memcpy | `DataDecoder.hpp:641` | packet |
| `std::map` insert + erase, node embeds a 1.5 KB `message_buffer` | `DataDecoder.hpp:725` | packet |
| second `std::map` for resequencing | `Reconstructor.hpp:234` | out-of-order msg |
| `std::vector` heap allocation for entries | `ParsedMsg.hpp` (pre-fix) | message |
| two extra actor hops + mailbox contention across N workers | — | message |

Serial pays none of it. Fan-out can only win when per-message decode cost
exceeds coordination cost; CME SBE decode is well under 1 µs while coordination
measured ~7 µs. **On this feed shape the arithmetic cannot work.**

---

## 4. Two optimisations, and what they did

Implemented on `pr-127` and measured as the third window.

**Fix A — pooled entries.** `ParsedMsg::entries` was a `std::vector` that
`DecodeSink::flush()` `swap()`'d into. The swap handed the sink's `reserve(64)`'d
storage away and took back an empty vector, so the *next* message's first
`emplace_back` reallocated from zero: one malloc + one free per message. Entries
now live inline in the pooled block (`l3_t inl[8]` + overflow vector + count),
sized from the p99 of 2–4 with overflow for the max-40 case.

**Fix B — inline bypass.** A packet with ≤ 8 messages is decoded on the
decoder's own thread and its entries sent straight to the Reconstructor with the
same dense `order_seq`, skipping the memcpy, the `pending_` slot, the worker hop
and the `DecodeDone` reply. Ordering is unchanged — the Reconstructor still
applies strictly by `order_seq`, so inline and fanned-out packets cannot
overtake. Coverage is 99.1–99.6% of packets.

Bypass confirmed firing by per-thread CPU: chan 310 and 344 worker threads at
**0 jiffies** while their Reconstructors accumulated time. Chan 318 (NQ) workers
stayed busy, matching its 0.872% of >8-message packets.

**Result — the body improved, the tail did not:**

| series | p50 before → after | p99 before → after | p999 before → after |
|---|---|---|---|
| ES book | 13.71 → **10.28** | 49.18 → 46.55 | 103.10 → **122.80** |
| NQ book | 14.09 → **9.93** | 39.45 → 39.40 | 56.50 → **67.85** |
| ZN book | 14.25 → **10.13** | 91.80 → **103.48** | 337.81 → 274.41 |
| ES trade | 19.44 → **14.24** | 154.92 → **279.97** | 281.60 → **415.81** |
| NQ trade | 16.69 → **12.05** | 60.74 → **149.33** | 102.22 → **310.35** |
| ZN trade | 31.50 → **26.56** | 236.63 → **469.80** | 323.03 → **766.06** |

**p50 improved 25–30% and p10 by 35–40% on every series. p99/p999 got worse on
five of six.** The fixes recovered roughly half the median gap to serial
(14.1 → 9.9 against a 6.4 target) and moved the tail the wrong way.

Two candidate mechanisms, never separated — see §8.1. **Bimodality:** ~99.2% of
packets now take a fast inline path and ~0.8% still fan out, so the tail is drawn
from a different population than the body, where before every packet paid the
same ~14 µs. **Footprint:** this build carried `INLINE_CAP = 8`, a 2,880-byte
inline block on every message, since cut to 2. Trade series suffer most, which
fits bimodality (their fat packets are the ones exceeding the threshold) but does
not distinguish the two. **The cause was not established; an earlier draft of
this section wrongly asserted bimodality alone.**

**Neither fix makes parallel competitive.** Serial still wins every book
percentile by 1.5–2.3×.

### Worker count does not matter

A fourth window ran the same fixed build with `cme_decode_workers 64` (192 worker
threads on 32 physical cores), full 900 s:

| series | w=8 p50 | **w=64 p50** | w=8 p99 | **w=64 p99** |
|---|---|---|---|---|
| ES book | 10.28 | **10.85** | 46.55 | **47.68** |
| NQ book | 9.93 | **10.64** | 39.40 | **40.42** |
| ZN book | 10.13 | **10.63** | 103.48 | **98.00** |

**8× the workers moved the median by under 0.8 µs** — and slightly the wrong
way. Expected: the inline bypass takes 99.1–99.6% of packets before any worker is
involved, so worker count can only touch the ~0.8% that fan out. It does confirm
that 192 threads on 32 cores do not wreck the inline path through scheduler
pressure, which was not obvious beforehand.

The useful conclusion: **the remaining ~3.5 µs gap to serial is not worker
capacity.** It is the two actor hops and the two `std::map`s, and no amount of
parallelism addresses them.

---

## 5. How to reproduce

### Build

```bash
cd /home/vincent/kaspar-hft
./build.sh install USE_TACHBOOK=1      # libs; USE_TACHBOOK is REQUIRED
./build.sh -C kaspr/src USE_TACHBOOK=1 # the binary: the default target skips it
```

`USE_TACHBOOK=1` is not optional — without it `create_probes()` is not compiled
in and the run produces no samples. The default `build.sh` target builds the
libraries but **not** the kaspr binary; build `kaspr/src` explicitly.

If `genschema` cannot reach `sftpng.cmegroup.com`, the pinned MDP3 v12 / iLink v8
codecs can be copied from another tree into `mdp3_sbe/` and `ilink3_sbe/`.
Confirm `SBE_SCHEMA_VERSION = 12` before trusting a build — CME's current
templates are v13 and regenerating would break the pin.

### Configure

Two config directories that differ in exactly one key. `kaspr.cpp:445` resolves
`cme.ini` by fixed name against the main config's own directory, so the variants
must be separate **directories** — a renamed `cme_parallel.ini` is silently
ignored and you measure serial while believing otherwise.

```
kaspr/config_ser127/     cme_decode_workers 0   perf_csv_dir .../mdperf_ser127
kaspr/config_pr127/      cme_decode_workers 8   perf_csv_dir .../mdperf_pr127
```

Each holds `md_perf.ini`, `cme.ini`, `som.ini`, `light.ini`. Required in
`md_perf.ini`: `perf_probe true`, `tachbook true`, `perf_route_tachbook true`,
`mqport 7778`, `perf_bin_ms 100`. Point `universe` and `cme_ini` at **absolute**
paths; a relative `genconfig/mdp3_prod.info` breaks on CWD.

### Run

```bash
KHPROJ=/home/vincent/kaspar-hft \
PROBE_CFG=../config_ser127/md_perf.ini \
OUTDIR=/home/vincent/perf/mdperf_ser127 \
  kaspr/run_probe.sh --no-restart -t 900
```

Solo mode stops the live recorder for the window. `--no-restart` leaves it down
(otherwise a `trap EXIT` restarts it). Swap `config_ser127` → `config_pr127` for
parallel. **Use a distinct `OUTDIR` per variant** or the runs overwrite.

Verify the path actually taken before trusting any number:

```
Kaspr: chan 310 SERIAL decode (inline)
Kaspr: chan 310 PARALLEL decode, 8 workers
```

### Analyse

```bash
cd kaspr/perf
MDPERF_DIR=/home/vincent/perf/mdperf_ser127 python3 kh_msg.py [HH:MM:SS HH:MM:SS]
```

`kh_msg.py` reads the per-message `.msg` log (16 B records: `t1`, `l1_ns`,
`qlen`, `idx`) and pairs qlen with latency **exactly**. Do not use the binned
`.csv` for percentiles — a "p99" of bin maxima overstates the real per-message
p99 by ~1.7× (measured: 68.48 vs 39.81 µs). This is the same ecological fallacy
documented at the top of `RESULT_qlen_vs_latency.md`.

### Excluding recovery — mandatory

Instrument recovery replays snapshots back to back and its latencies are
milliseconds. Include them and NQ book reads **mean 11,408 µs, p99 386 ms**
instead of 15.2 / 37.6 µs. Three methods, best first:

1. **`drop_startup()`** (`kh_report.py:78`) — the codified rule. Reads the
   `.arr` file, finds the leading run of identical `t0` (`startup_pkts()`), drops
   bins until that many packets have closed. Measured bursts: ES 4,498, NQ
   20,079, ZN 6,082 packets. **Only the serial path writes `.arr`**, so this is
   unavailable for parallel runs.
2. **Latency threshold** — drop the leading contiguous run of `l1 ≥ 1 ms`.
   Self-calibrating, works on both paths. Used for every figure in this report,
   plus a 60 s settling cut.
3. **Manual time cut** — `kh_msg.py HH:MM:SS HH:MM:SS`.

---

## 6. Caveats

- **qlen was idle.** 88–94% of messages at `qlen == 0`, mean 0.07–0.16. The
  per-message fit gives r² = 0.008 and the queue term contributes 0.14 µs of a
  15.56 µs mean. **These windows say nothing about the qlen→latency slope.**
  Answering that needs a busy window.
- **Windows are ~800 s of steady state each, one box, one afternoon.** Not a
  multi-session result.
- The ZN book `max` of 3,046 µs sits an order of magnitude past its p999 of
  273.69. Isolated mid-run spikes are not removed by method 2, so a few may be
  residual recovery rather than real tail events. Unresolved.
- Parallel writes no `.arr`, so interarrival analysis and `drop_startup` are
  serial-only.
- The probe process **hangs in teardown** after flushing — the window elapses,
  TachBooks report out, the process never exits. Data is complete at that point;
  it was reaped with `-9`.

---

## 7. Conclusion (provisional — see §8)

**This section states what the four windows showed. It is not a finished
evaluation, and §8.4b partly contradicts the last paragraph — read both.**

**Keep `cme_decode_workers 0` in production. The parallel path is for research
only.** Beyond being slower, it is not correctness-validated against serial, so a
production run on it risks silent book divergence, not just latency.

Serial decode is faster on every book series at
every percentile, by 2.1–2.2× at the median and 2.4–3.2× at p99, and its medians
match the published article intercept. The single crossover (ZN trade p99) does
not survive to p999 and sits in the noisiest series measured.

Parallel decode's cost is structural on THIS feed **at the median**, not
incidental: at 1.06–1.10 messages per packet there is no fan-out to amortise the
coordination over. The two optimisations here recovered half the median gap.

**They also appeared to widen the tail — and that turned out to be wrong.** A
later window (§8.4b) that pushed *more* traffic onto the fan-out path improved
p999 by 32–66% on five of six series, and the improvement held at p9999 and max.
The earlier "widened the tail" reading came from a build carrying a 2,880-byte
inline block per message (§8.1), which is a defect, not a property of fan-out.
So the honest summary is: **parallel loses the body and can win the far tail.**
Whether that is worth having depends on which one you are optimising, and the
correctness question (§8.5, item 1) has to be settled either way.

## 8. THE JURY IS STILL OUT — read this before citing section 2

**This report measures a quiet market, and at least one of its parallel
configurations was very likely measured wrong. Do not read it as "parallel decode
does not work." Read it as "parallel decode lost under these conditions, on a
build with a known defect, and the comparison deserves a rerun."**

**And one regime has now been measured where it wins.** §8.4b: with the inline
threshold cut to single-message packets, the parallel path beats its own previous
build from roughly p95–p99.5 onward, and by **−40% to −66% at p999** on the trade
series — an effect that holds at p9999 and max. It still loses at p50 on every
series, and serial still wins everywhere. But the far tail is a real regime, and
it is the regime a market-data handler is judged on.

Four reasons to hold the verdict open.

### 8.1 The +fix runs carry a build defect that inflates the tail

Both "PAR +fix" windows ran with `ParsedMsg::INLINE_CAP = 8`. `sizeof(bfile::l3_t)`
is **360 bytes** — it is a variant over ~30 record types, sized by its largest
member — so the inline block was **2,880 bytes on every message**, including the
p50 single-entry case, replacing a 24-byte vector handle. Against the
`MemoryPool<ParsedMsg,32,32,4096>` that is roughly **11.8 MB resident, well past
L2**, on the hot path. A second effect compounds it: the inline bypass allocates
and frees `ParsedMsg` on the DataDecoder thread, making it a new contender on
`MemoryPool`'s static per-instantiation mutex, where previously only worker
threads touched it.

Cache footprint and lock contention hit the **tail**, not the median — which is
exactly the shape measured (p50 improved 25–30%, p99/p999 worsened on five of six
series). `INLINE_CAP` has since been cut to **2** (720 bytes, still covering
p90 = 1). **That change is unmeasured.** Every p99/p999 figure for a "+fix" run in
section 4 should therefore be treated as an upper bound on a build that no longer
exists.

Correcting an overclaim in section 4: it attributed the tail regression to
bimodality between the inline and fan-out populations. Bimodality is real, but
footprint is a second credible mechanism and the two were never separated. The
honest statement is that the cause was not established.

### 8.2 The windows were quiet, and the thesis is about load

Across every window, **88–94% of messages saw `qlen == 0`**, mean depth
0.07–0.34, per-message fit r² = 0.008, and the queue term contributed 0.14 µs of
a 15.56 µs mean. This is an **idle-queue measurement**. It says almost nothing
about the regime where fan-out would be expected to pay:

- Coordination cost is roughly **fixed per packet**; decode work scales with
  message count. The measured 1.06–1.10 messages/packet is a *quiet-market*
  figure. Under a burst — an economic print, a Fed decision, an option-expiry
  sweep — packets get fatter and the ratio moves toward fan-out. The `.arr`
  census already shows 0.4–0.9% of packets carrying more than 8 messages, up to
  **40–42**; a regime where that fraction is materially larger is a different
  measurement, not an extrapolation of this one.
- Serial decode is a **single thread**. It cannot exceed one core, so it has a
  hard ceiling that parallel does not. Nothing here probed that ceiling: at
  `qlen ≈ 0` serial was never close to saturated. **The interesting question is
  what happens when it is** — and that is precisely when a market-data handler
  matters.
- The tail is where queueing lives, and the tail is the part of this measurement
  the 8.1 defect most plausibly distorted.

**Plausible, untested hypothesis: under sustained high message rates — deep
queues, fat packets, serial pinned at one core — parallel decode could beat
serial.** This report neither demonstrates nor refutes that. It was not designed
to.

### 8.3 Known optimisations were deliberately not done

The per-packet coordination cost still includes work that is removable, and was
left in scope-limited:

| cost | site | status |
|---|---|---|
| `pending_` `std::map` — malloc + rebalance per packet, node embeds a 1.5 KB `message_buffer` | `DataDecoder.hpp` | **not done** — dense monotonic key, a fixed ring indexed by `pid & mask` is a drop-in |
| resequence `std::map` | `Reconstructor.hpp` | **not done** — same shape, `order_seq & mask` |
| 1,500-byte packet memcpy | `DataDecoder.hpp` | **not done** (the inline bypass sidesteps it for ≤99% of packets rather than removing it) |
| `MemoryPool` static mutex now shared with the DataDecoder thread | `MemoryPool.hpp` | **not done** |
| two extra actor hops per message | — | inherent to the design |

Both `std::map`s have dense, monotonic keys, so both are mechanical replacements
with the house circular-buffer-plus-overflow pattern (`BQueue`, `HybridBuffer`).
Neither was attempted here.

### 8.4 `fast_send` may be throttling the fan-out by design

`MessageProcessor::processq` hands each packet to the decoder with
**`decoder->fast_send(&dp, this)`** (`MessageProcessor.hpp:332`) and then blocks
on the reply to read `rc` and `is_channel_reset`. `Actor::fast_send` takes
`fast_send_mutex` as a `lock_guard` and **holds it across the entire handler**
(`actors/cpp/src/Actor.cpp:121`). On the parallel path that handler is
`on_decode_packet`, so the packet copy, the `pending_` map insert, the whole
`dispatch()` loop and every `DecodeReq` send all run **inside that lock**, on the
MessageProcessor's own thread, while it waits.

Two consequences, and they cut against the whole point of a worker fleet:

1. **The producer stalls instead of running ahead.** MessageProcessor cannot
   start framing packet N+1 until packet N's dispatch has finished and replied.
   Fan-out is supposed to decouple arrival from decode; a synchronous
   `fast_send` re-couples them at the front. The workers can only ever be as far
   ahead as one packet, so there is no depth for them to work through — which is
   also why an 8→ 64 worker change measured flat (§4): the fleet was never the
   constraint.
2. **`on_decode_done` contends for the same mutex.** Worker completions arrive
   through `process_message_internal`, which takes `fast_send_mutex` too
   (`Actor.cpp:101`). So every completion must wait for any in-flight dispatch,
   and every dispatch waits behind any completion being processed. With N workers
   reporting per packet, that is N acquisitions of a lock the producer also needs.

This is *correct* — it is exactly the serialization that makes `order_seq_` and
`pending_` safe without their own lock, and the review confirmed the inline
bypass relies on it. But correctness here was bought with throughput, and on the
serial path the cost is invisible because the decode genuinely is inline work
that has to happen on that thread anyway.

**Hypothesis worth testing: replace the synchronous `fast_send` with an
asynchronous hand-off on the parallel path, so MessageProcessor keeps framing and
the workers stay busy.** It is not a small change and it is not free:

- `rc` and `is_channel_reset` are consumed **synchronously** at
  `MessageProcessor.hpp:333-345` to drive `do_data_recovery()`. An async hand-off
  has to deliver failure out of band — the fan-out path already has the machinery
  (`DecodeDone.ok` → `pp.failed` → `TriggerRecovery`), so the reply value may be
  redundant there, but that needs proving before it is removed.
- The comment at `MessageProcessor.hpp:327` notes `fast_send` runs the handler
  **before `msg_q[sn]` is erased**, which is what makes the zero-copy
  `DecodePacket` borrow safe. Async means the packet must be owned — either
  copied (which the parallel path already does into `pending_`) or reference
  counted.
- Dropping the lock means `order_seq_` and `pending_` need their own
  synchronisation, and the inline bypass loses the guarantee it currently rests
  on.

So: plausible, possibly significant, and interacts with both correctness
invariants this path depends on. It belongs on the list above §8.2's load test,
because a producer that cannot run ahead will look the same as a fleet that is too
small — and this measurement cannot tell those two apart.

### 8.4b MEASURED: the best parallel configuration, against serial

A fourth window ran the parallel path with two changes, workers = 8, 900 s, same
config and hours as the serial control:

- **async hand-off** — MessageProcessor stops *consuming* the decode reply, so the
  producer frames packet N+1 without waiting on N's dispatch.
- **`INLINE_MAX_MSGS` 8 → 1** — only genuinely single-message packets go inline;
  everything with real work to divide fans out.

This is the fastest parallel configuration measured. Comparing it to the earlier
`INLINE_CAP = 8` build would be comparing one defect to another (§8.1), so the only
comparison below is **against serial**.

| series | n | p50 | p90 | p99 | p999 | p9999 | max |
|---|---|---|---|---|---|---|---|
| **ES book** serial | 446,081 | **6.65** | **10.47** | **19.10** | **39.25** | **67.90** | **1202.78** |
| ES book parallel | 249,500 | 11.54 | 25.47 | 48.71 | 82.93 | 175.51 | 1993.13 |
| **ES trade** serial | 35,519 | **8.87** | **16.11** | **33.62** | **67.65** | **86.46** | **90.00** |
| ES trade parallel | 31,639 | 17.60 | 33.74 | 93.18 | 148.39 | 200.62 | 202.88 |
| **NQ book** serial | 695,724 | **6.36** | **8.85** | **12.35** | **18.94** | **55.64** | **454.22** |
| NQ book parallel | 279,239 | 13.47 | 19.51 | 36.47 | 71.86 | 1641.22 | 3965.85 |
| **NQ trade** serial | 15,548 | **7.33** | **11.10** | **22.15** | **42.47** | **57.46** | **94.97** |
| NQ trade parallel | 8,064 | 13.84 | 24.04 | 73.47 | 103.92 | 245.59 | 2313.26 |
| **ZN book** serial | 157,699 | **7.11** | **11.81** | **63.30** | 273.69 | **342.48** | 3046.64 |
| ZN book parallel | 86,591 | 14.31 | 25.60 | 151.20 | **248.08** | 434.30 | **1238.19** |
| **ZN trade** serial | 16,250 | **17.19** | **95.61** | **268.04** | **310.13** | **318.32** | **319.05** |
| ZN trade parallel | 9,546 | 30.64 | 140.84 | 387.78 | 460.25 | 465.28 | 466.18 |

**Serial wins 28 of 30 tail comparisons** (6 series × p90, p99, p999, p9999, max).
The two exceptions are both ZN book: **p999 248.08 vs 273.69** (−9%) and
**max 1238.19 vs 3046.64** (2.5× better). Everywhere else serial leads, and the
margin is widest exactly where a fan-out was supposed to help — the trade series
at p999, where serial is 1.5–2.4× faster (ES 67.65 vs 148.39, NQ 42.47 vs 103.92,
ZN 310.13 vs 460.25).

At the median serial is 1.7–2.1× faster on every series.

**What this establishes.** The best parallel configuration measured still loses to
serial almost everywhere, including the far tail it was expected to win. The one
real exception — ZN book, the thinnest book on the slowest channel — is a single
series out of six and does not generalise.

**What it does not establish.** The two changes were measured together, so the
contribution of each is unknown. And the §8.4 `fast_send` hypothesis is **not
supported**: if the synchronous reply had been the binding constraint, p50 should
have improved once the producer stopped waiting. It got worse on all six series.
Either the constraint is elsewhere, or the threshold change masked the benefit.

**Caveat on the sample.** The parallel window carries fewer messages than the
serial control (250–279k vs 446–696k book; 8–32 samples above p999 on the trade
series), so the deep-tail figures carry real uncertainty. The direction is
consistent across all six series, which the sample size does not explain away.

Recovery was deliberately broken on the branch that produced these numbers
(`perf/parallel-experiment`, never merged, since deleted), so this is a latency
measurement only — not a correctness comparison.

### 8.5 What would actually settle it

1. **Correctness first.** A dual-path replay of one `.bin` through serial and
   parallel, diffing final book state. This has never been run, and the
   correctness caveat in `kaspr.cpp` still stands. Two silent-divergence bugs were
   found by review in this path — one of them reintroduced by a commit written
   specifically to keep the two paths consistent. **No performance number from
   this path means anything until that replay passes.**
2. **Rerun the +fix windows with `INLINE_CAP = 2`**, matched duration, same hours.
3. **Measure under load, not at `qlen ≈ 0`** — an FOMC or CPI window, or a
   synthetic replay at an accelerated rate, so the queue term is actually
   exercised. The `.arr` log makes packet fatness measurable directly.
4. **Separate the two changes measured together in §8.4b.** Run async-only with
   `INLINE_MAX_MSGS` back at 8, and threshold-only without the async hand-off.
   The tail win is real but unattributed, and the evidence points at the
   threshold rather than the hand-off — p50 got *worse*, which is the opposite of
   what a freed producer should do.
5. **Then remove the two `std::map`s** and re-measure, so the comparison is
   against a parallel path that has actually been optimised.

Until these are done, the defensible claim is narrow: **on a quiet CME session, at
idle queue depth, serial decode is 2.1–2.2× faster at the median and parallel is
not correctness-validated — so production stays on serial.** Whether parallel wins
under load is **open**, and this report should not be cited as having closed it.
