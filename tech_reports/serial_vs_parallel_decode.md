# Serial vs parallel decode — wire-to-book latency

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

**Serial is 2.1–2.2× faster at the median and 2.4–3.2× at p99.** The serial
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

The mechanism is bimodality: ~99.2% of packets now take a fast inline path and
~0.8% still fan out, so the tail is drawn from a different population than the
body. Before, every packet paid the same ~14 µs. Trade series suffer most —
their fat packets are the ones that exceed the threshold.

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

## 7. Conclusion

**Keep `cme_decode_workers 0` in production. The parallel path is for research
only.** Beyond being slower, it is not correctness-validated against serial, so a
production run on it risks silent book divergence, not just latency.

Serial decode is faster on every book series at
every percentile, by 2.1–2.2× at the median and 2.4–3.2× at p99, and its medians
match the published article intercept. The single crossover (ZN trade p99) does
not survive to p999 and sits in the noisiest series measured.

Parallel decode's cost is structural, not incidental: at 1.06–1.10 messages per
packet there is no fan-out to amortise the coordination over. The two
optimisations here recovered half the median gap but widened the tail, which is
the opposite of what was wanted. Closing the rest would mean removing the two
`std::map`s and the two actor hops — and even then the arithmetic only works on
a feed with materially fatter packets than CME MDP3 delivers.
