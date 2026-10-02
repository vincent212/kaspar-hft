# Median vs tail in an HFT market-data path: inline vs hand-off, and why parallel decode loses

*Draft for an arXiv paper. Experimental branch `md-latency-experiments`; not production code.*
*Data: live CME MDP3, 2026-10-01, 08:56-13:00 ET (pre-open and RTH), kaspr on one AMD EPYC 9374F host.*

## Abstract

We measure socket-to-book latency on a live CME MDP3 feed, message by message,
across 14 configurations of one C++ actor-based market-data path.

1. **Thread hand-offs, not decoding, set the median.** SBE decode plus
   book-event handling costs about 0.3-0.9 us. Each hop into a sleeping thread
   costs 2-4 us on a sparse feed. Removing every hop between socket read and
   book publish cuts the median from 6.3 to 1.3 us, and p1 from 3.6 to 0.5 us,
   with no packet loss.
2. **Inlining book work trades tail for median on some instruments.** Running
   the book on the decode thread halves the median. It lengthens p999 by up to
   2x on books with heavy per-event work (ZN, ES), not on NQ.
3. **Parallel decode loses on both median and tail.** Packets carry about one
   message each, so fanning out adds two hops and a record/replay cost per
   packet for nothing. More threads also means more chances to be descheduled.
4. **Pinning busy-polling threads without CPU isolation is worse than not
   pinning.** Device interrupt work on the pinned CPUs stalls them for
   milliseconds.


## Configuration labels

Every configuration is the measurement base config below plus the listed keys,
nothing else. The exact files are in `md_paper_data/configs/<label>/`.

**Common to all** (`md_perf.ini`, `kaspr.general`):

- `tachbook true`, `perf_probe true`, `perf_route_tachbook true`: market data
  goes to TachBook, the MBO L3 book, and the latency probe subscribes to it.
- `perf_bin_ms 100`, `mqport 7778`.
- Channels 310 (ES), 318 (NQ) and 344 (ZN/ZF/ZB/ZT/UB) on, with feeds A and B.
- Universe: Z6 and H7 contracts.
- Run with Onload (`EF_POLL_USEC=3000`, `EF_INT_DRIVEN=0`) by `run_probe.sh` in
  solo mode, with the recorder stopped.

**The path being changed:**

```
socket reader A/B -> [hop 1] -> MsgBuf -> MessageProcessor -> decode -> handler -> [hop 2] -> TachBook (t1)
```

`MsgBuf -> MessageProcessor -> decode -> handler` is inline (fast_send) in every
serial config. "Hop" means a `send` into another thread's mailbox.

| label | keys added to base | hops in t0..t1 | what it tests |
|---|---|---|---|
| `base` | none | 2. Hop 1 into a sleeping MsgBuf (BQueue condvar), hop 2 into a sleeping TachBook. | the production path |
| `fastsend` | `book_fast_send true` (`kaspr.general`) | 1. Hop 2 removed: handler_if<UseFastSend=true> runs the book update inline on the MsgBuf thread. | book inline vs hand-off |
| `mbspin` | `cme_msgbuf_mailbox lockfree_spin` (per channel) | 2. MsgBuf is a LockFreeMPSC whose consumer busy-polls, so hop 1 has no wakeup. | cost of the wakeup on hop 1 |
| `fastsend_mbspin` | `fastsend` + `mbspin` keys | 1, no wakeup | both together |
| `rfs` | `book_fast_send true` + `cme_reader_fast_send true` (per channel) | 0. The socket reader fast_sends into MsgBuf, so the whole path runs on the reader thread. Readers A and B serialize on MsgBuf's mutex. | no hops at all |
| `p4s` | `cme_decode_workers 4` + `cme_decode_spin true` (per channel) | 4: MsgBuf, worker, HandlerIfActor, book. Hop 1 into a sleeping MsgBuf; workers and HandlerIfActor busy-poll. | parallel decode, 4 workers per channel |
| `fsmb_pin` | `fastsend_mbspin` + `cme_cpus` per channel + `tachbook_cpus 54x6`; run under `taskset -c 0-15,24-47,56-63` | 1 | pinning busy threads on NUMA node 2 (not isolated) |
| `rfs_pin` | `rfs` + `cme_cpus` per channel + `tachbook_cpus 54x6`; same taskset | 0 | pinning the zero-hop path |
| `<label>_A` | `<label>` + `cme_feed_b false` (per channel) | same as `<label>` | feed A only: socket reader B never started, so no A/B arbitration |

**`cme_cpus` order:** recovery, MessageProcessor, MsgBuf, sock A, sock B.

| | ES | NQ | ZN |
|---|---|---|---|
| `fsmb_pin` | 22,22,16,17,49 | 22,22,18,19,51 | 22,22,20,21,53 |
| `rfs_pin` | 22,22,22,16,17 | 22,22,22,18,19 | 22,22,22,20,21 |

- `fsmb_pin`: sock A and B on the two SMT siblings of one core, MsgBuf alone on
  its own core.
- `rfs_pin`: each socket reader alone on its own core.
- Siblings on this host are (N, N+32).

**Short names used in the text:**

| short name | label |
|---|---|
| base | `base` |
| fastsend, "fast_send to book" | `fastsend` |
| mbspin, "spinning MsgBuf" | `mbspin` |
| "reader fast_send", "no hops" | `rfs` |
| p4s, "parallel" | `p4s` |
| "pinned" | `*_pin` |
| "feed A only" | `*_A` |

## Results as of 2026-10-01 13:00 (all passes pooled)

26 valid runs, 08:56-13:00 ET, Onload solo mode, 8-10 minutes each, first
120 s of each run dropped. One run (`x1_rfs`) is invalid: a stale library meant
the feature was not active. It is excluded and listed in `runs.log`.

Full tables: `md_paper_data/`.

### F1. End-to-end book latency, t1 - t0 (us)

Each cell is p1 / p10 / p50 / p90 / p99 / p999. `runs` = runs pooled.

| config | runs | ES Z6 book | NQ Z6 book | ZN Z6 book |
|---|---|---|---|---|
| base: serial, send to book, sleeping MsgBuf (production) | 4 | 3.9 / 4.7 / 6.6 / 11.0 / 23.1 / 65.6 | 3.6 / 4.3 / 6.3 / 8.9 / 12.6 / 21.9 | 3.9 / 4.6 / 7.3 / 12.0 / 63.0 / 138.0 |
| fastsend: book inline | 2 | 2.2 / 2.9 / 4.9 / 8.1 / 19.6 / 80.1 | 2.0 / 2.7 / 4.7 / 6.8 / 10.3 / 20.8 | 1.9 / 2.7 / 4.7 / 7.8 / 54.0 / 179.3 |
| mbspin: spinning MsgBuf | 2 | 2.7 / 3.8 / 5.3 / 8.1 / 16.6 / 34.6 | 3.2 / 3.6 / 5.3 / 7.0 / 10.0 / 17.2 | 3.2 / 3.7 / 4.9 / 8.8 / 44.0 / 153.6 |
| fastsend + spinning MsgBuf | 4 | 1.4 / 1.8 / 2.5 / 5.3 / 14.4 / 29.9 | 0.9 / 1.6 / 2.2 / 3.9 / 7.2 / 15.0 | 0.9 / 1.5 / 2.4 / 5.3 / 65.8 / 290.2 |
| rfs: reader -> MsgBuf -> decode -> book all inline (no hops) | 1 | 0.6 / 0.9 / 1.6 / 4.5 / 13.8 / 28.6 | 0.5 / 0.8 / 1.3 / 3.1 / 6.6 / 12.9 | 0.6 / 0.9 / 1.4 / 4.1 / 53.9 / 162.5 |
| p4s: parallel, 4 spinning workers | 2 | 7.8 / 9.6 / 12.3 / 18.1 / 35.8 / 85.3 | 6.5 / 8.2 / 11.0 / 15.6 / 22.6 / 346.8 | 7.8 / 10.0 / 13.7 / 20.8 / 90.9 / 264.8 |
| fastsend+mbspin, pinned (node 2, not isolated) | 2 | 0.9 / 1.1 / 1.7 / 5.4 / 51.0 / 739.0 | 0.8 / 1.0 / 1.4 / 3.5 / 29.6 / 312.0 | 0.9 / 1.2 / 1.9 / 13.9 / 331.8 / 1282.6 |
| rfs, pinned | 2 | 0.6 / 0.8 / 1.3 / 4.4 / 31.9 / 253.5 | 0.6 / 0.8 / 1.1 / 3.0 / 8.6 / 84.0 | 0.7 / 0.9 / 1.3 / 4.4 / 104.6 / 361.5 |
| base, feed A only | 1 | 4.0 / 4.6 / 6.6 / 9.6 / 16.9 / 37.1 | 3.8 / 4.1 / 5.0 / 7.5 / 10.4 / 17.6 | 3.9 / 4.4 / 6.6 / 10.9 / 66.0 / 266.3 |
| fastsend, feed A only | 1 | 2.3 / 2.9 / 4.4 / 6.6 / 14.4 / 29.6 | 2.1 / 2.3 / 2.8 / 4.7 / 7.5 / 12.6 | 2.4 / 2.9 / 4.7 / 6.8 / 44.6 / 196.4 |
| mbspin, feed A only | 1 | 3.0 / 3.4 / 4.0 / 6.7 / 13.5 / 26.3 | 3.1 / 3.5 / 4.1 / 5.8 / 8.4 / 12.9 | 3.4 / 3.8 / 5.5 / 8.0 / 46.2 / 85.4 |
| fastsend+mbspin, feed A only | 1 | 1.5 / 1.8 / 2.3 / 3.9 / 11.1 / 23.2 | 1.4 / 1.7 / 2.1 / 3.1 / 5.8 / 9.1 | 1.7 / 2.0 / 2.5 / 4.0 / 46.7 / 109.3 |
| rfs pinned, feed A only | 2 | 0.6 / 0.8 / 1.2 / 4.0 / 16.6 / 42.8 | 0.6 / 0.7 / 1.1 / 2.6 / 6.2 / 15.7 | 0.6 / 0.8 / 1.2 / 3.3 / 36.8 / 100.5 |
| p4s, feed A only | 1 | 6.6 / 8.0 / 10.3 / 14.9 / 25.6 / 49.8 | 7.5 / 8.7 / 11.2 / 14.9 / 20.8 / 31.3 | 8.6 / 10.4 / 13.5 / 19.4 / 58.1 / 163.6 |

### F2. Main findings

1. **Hops, not decode, set the median.**
   - Decode plus handler is 0.3-0.9 us at p50 on every serial config.
   - Each thread hop into a sleeping thread costs 2-4 us: a futex wakeup on a
     sparse feed of about 1 packet per ms.
   - Removing hops step by step moves the NQ book median:

     | path | NQ book p50 |
     |---|---|
     | base, 2 hops, both receivers sleeping | 6.3 us |
     | spinning MsgBuf | 5.3 us |
     | fastsend: book hop removed | 4.7 us |
     | both | 2.2 us |
     | reader fast_send (`rfs`): zero hops | **1.3 us** |

   - p1 falls from 3.6 to 0.5 us.
2. **Median vs tail, inline vs hand-off: real but modest, and not universal.**
   - fastsend vs base, pooled:
     - ES p999 80 vs 66 us
     - ZN p999 179 vs 138 us
     - NQ p999 21 vs 22 us
   - fastsend + mbspin vs mbspin on ZN: p999 290 vs 154 us, while p50 halves
     (2.4 vs 4.9 us).
   - The penalty shows on the books with heavier per-event book work (ZN, ES),
     not on NQ.
   - Mechanism (pass 1, P2): packets queue behind inline book work in bursts.
   - Unpinned, the zero-hop `rfs` path was **not** worse than base at p999 on
     ES/NQ (28.6 vs 65.6, 12.9 vs 21.9). On ZN it was comparable (162 vs 138).
     That is one run; it needs repeats.
3. **Parallel decode (p4s) is the slowest and has the longest tails.**
   - p50 11-14 us, about 2x base.
   - p999 85-347 us.
   - The fixed hand-off cost (two extra hops plus recording and replay) and
     more threads exposed to descheduling explain it (P4).
   - 61 of 84 p4s buckets had a book message over 100 us, against 48 of 156
     for base. Every one coincided with a hot thread waiting for a CPU.
4. **Pinning without isolation makes the tail far worse.**
   - fsmb_pin ZN p999 is 1,283 us, against 290 us unpinned. rfs_pin ES p999 is
     254 us, against 29 us unpinned.
   - The pinned hot threads waited 22-51 ms per 10 s in the run queue at p50,
     max 272 ms. Unpinned: about 0.01-0.03 ms p50.
   - CPUs 17-22 service the storage controller's interrupts (`mpi3mr0` msix
     18-23, about 120 M each). A pinned spinner cannot move off when softirq or
     kernel work lands on its CPU; an unpinned one migrates.
   - Pinning needs isolation: `isolcpus`/`nohz_full`, IRQ affinity moved off
     the hot CPUs. Not possible on this box without a reboot; not tested.
5. **No packet loss, and no hidden queueing, from inlining.**
   - Onload `oflow_drop` and `mem_drop` were 0 on every socket in every run
     captured.
   - Zero gaps and zero extra recoveries in all 26 valid runs, including all
     feed-A-only runs.
   - Exchange SendingTime -> t0 (excess over the window minimum, X0) for `rfs`
     unpinned matches base: ES p50 / p999 6.8 / 29.5 vs 6.5 / 27.8 us. Running
     decode on the reader thread did not push measurable waiting into the
     socket buffer.
   - With pinning, X0 p999 rose to 70-850 us: the reader itself was held off
     its CPU.
6. **Feed A only (B off) has shorter tails for the same config.**
   - fastsend_mbspin: ZN p999 109 vs 290, NQ 9.1 vs 15.0.
   - rfs pinned: ES 43 vs 254, NQ 16 vs 84, ZN 101 vs 362.
   - p4s: NQ 31 vs 347.
   - One fewer busy-polling socket reader per channel means less CPU
     contention. Under `rfs`, A and B also serialize on MsgBuf's mutex.
   - Medians are about the same.
   - Cost: no arbitration. A drop on feed A becomes a gap and a recovery.
     None were seen in 7 A-only runs (about 55 minutes).

### F3. Threats to validity (specific to these runs)

- **Live market, uncontrolled load.** Order was rotated, but some configs have
  1-2 runs only (`rfs` 1).
- **Shared, unisolated host.** Run-queue waits show contention.
- **The tail percentiles are thin.** p999 on ZN rests on about 100-400 samples
  per run.
- **t0 is software, after `recvfrom`.** The X0 stage compensates only partly:
  it includes network and exchange jitter, and its baseline is a rolling
  minimum.
- **Disturbed runs.** Three were touched by short rebuilds (`p2_p4s` 20 s,
  `x1_rfs_pin` 30 s, `x1_rfs_pin_A` 30 s), noted in `runs.log`.


## How to reproduce

Everything below is on branch `md-latency-experiments` of kaspar-hft. That
branch is experimental and is not merged to main.

### 1. Host requirements

- Live CME MDP3 multicast on two interfaces (feeds A and B). Group addresses
  come from `kaspr/genconfig/mdp3_prod.info`.
- OpenOnload installed, `onload` kernel module loaded.
- `onload_stackdump` readable, for the loss counters.
- No other kaspr reading the same groups. The probe runs solo: stop the live
  recorder first.
- A universe whose contracts are live on the run date. This study used Z6/H7
  (`kaspr/config/universe.csv`); M6/U6 had expired and produce empty books.
  Roll it for later dates.

### 2. Build

```bash
cd kaspar-hft
./build.sh                               # ALL libraries. Required after any header change.
./build.sh -C kaspr/src USE_TACHBOOK=1   # kaspr with TachBook and the latency probe
strings kaspr/src/kaspr | grep -c perf_LatencyProbe_   # must be > 0
```

Pitfall seen in this study: a header change to a class instantiated inside a
library (for example `SocketReader`, compiled into `libmcast_recv.a`) has no
effect until the library is rebuilt. `./build.sh -C <dir>` on a library
directory may silently do nothing. Always run plain `./build.sh`, then check the
library's timestamp. One run (`x1_rfs`) was lost to this.

If the link fails with `cannot find -lboost_*`, set
`BOOST_PATH=/usr/local/boost188`, or wherever Boost 1.88 is installed.

### 3. Configs

```bash
for c in base fastsend mbspin fastsend_mbspin rfs p4s fsmb_pin rfs_pin rfs_pin_A ...; do
  cp -r tech_reports/md_paper_data/configs/$c kaspr/config_$c
done
```

Edit `perf_csv_dir` in each `md_perf.ini` to the output directory for the run.

Pinned configs hard-code CPU ids for this host (AMD EPYC 9374F, NUMA node 2 =
CPUs 16-23 and 48-55). Remap them, and check
`/sys/devices/system/cpu/cpuN/topology/thread_siblings_list` and
`/proc/interrupts` first. This study found storage-controller IRQs on the
pinned CPUs, which is why pinning hurt.

### 4. One run

```bash
cd kaspar-hft
OUT=/path/to/output/<run>; C=<label>; mkdir -p $OUT
sed -i "s#perf_csv_dir .*#perf_csv_dir $OUT#" kaspr/config_$C/md_perf.ini
MASK=(); case $C in *_pin*) MASK=(taskset -c 0-15,24-47,56-63);; esac
KHPROJ=$PWD OUTDIR=$OUT "${MASK[@]}" kaspr/run_probe.sh --no-restart -t 480 \
    -c ../config_$C/md_perf.ini > $OUT/probe.out 2>&1 &
P=$(pgrep -x md_perf_meter)   # once it is up
python3 tech_reports/md_paper_data/schedsample.py $P $OUT/probe.out $OUT/sched.csv 10 &
# after the window: save Onload loss counters, then stop the probe
for id in $(onload_stackdump | awk -v p=$P '$3==p{print $1}'); do onload_stackdump lots $id; done > $OUT/onload.txt
kill -9 $P   # known defect: kaspr hangs on shutdown in ZMQ teardown; samples are already on disk
cp $(ls -t kaspr/kaspr_log.*.log | head -1) $OUT/kaspr.log
```

`tech_reports/md_paper_data/run_matrix3.sh` automates this for a whole schedule:

- start time, window, CPU mask, Onload dump, force-kill and log copy for each
  run
- the schedule itself is a list at the top of the file
- `run_matrix.sh` and `run_matrix2.sh` are the earlier versions, which rotated
  the order across passes

Method used here:

- 8-10 minute windows.
- Configs interleaved and the order rotated between passes, so time of day does
  not line up with a config.
- At least one reference run (`base`, `fastsend_mbspin`) in each block.

### 5. Analysis

```bash
R=/path/to/output bash tech_reports/md_paper_data/paper_all.sh   # R = parent of the per-run directories
```

That writes:

| file | contents |
|---|---|
| `e2e.md` | end-to-end t1 - t0 per config, pooled: p1 / p10 / p50 / p90 / p99 / p999 / max, per run, and by ingress qlen |
| `stages.md` | per-stage percentiles from the `STAGE` lines, median across 10 s windows |
| `sched.md` | run-queue wait per thread role; worst 10 s buckets |
| `health.md` | gaps and recoveries; Onload `oflow_drop`, `mem_drop` and max socket queue |

Definitions:

- **First 120 s of each run dropped** (snapshot recovery). For stages, the first
  12 windows per channel.
- **End-to-end:** t1 - t0 per message, from the probe's `.msg` files: one
  16-byte record per message (t1, latency, ingress qlen, index in packet).
- **Stages:** logged by `DataDecoder` (serial) or `HandlerIfActor` (parallel)
  every 10 s as `STAGE` lines in the kaspr log.
- **X0** (exchange SendingTime -> t0) is the excess over the previous window's
  minimum. The raw value includes this host's clock offset to CME, about
  7.9 ms here. Valid only from run `x2_rfs_pin` on.
- **Thread names** are the actor names (first 4 + last 11 chars), set by the
  actor Manager. The scheduler sampler relies on them.

### 6. Known limits of the setup

- **t0 is software, after `recvfrom`.** Time in the NIC and socket buffer is not
  in t1 - t0, only partly in X0.
- **Nothing is isolated** (`isolcpus`, `nohz_full` and IRQ affinity are all
  default). Busy-polling threads contend with each other and with device
  interrupts.
- **kaspr does not exit cleanly** (ZMQ teardown hang). Every run is force-killed
  after its window; samples are flushed on a timer before that.

## Progress log

All raw numbers live in `tech_reports/md_paper_data/`, regenerated from the run
files by `paper_all.sh`. **Every table carries the full p1 / p10 / p50 / p90 /
p99 / p999 / max curve.**

| file | contents |
|---|---|
| `e2e.md` | end-to-end t1 - t0 per config, pooled and per run; latency by ingress qlen |
| `stages.md` | per-stage percentiles (median across 10 s windows), serial and parallel |
| `sched.md` | run-queue wait per thread role; worst 10 s buckets with the waiting thread |
| `health.md` | gaps and recoveries per run; Onload socket drops (`oflow_drop`, `mem_drop`, max socket queue) |
| `runs.log` | every run: start/end time, pid, CPU mask, kaspr log |

Scripts are copied alongside.

### 2026-10-01

| time (ET) | what |
|---|---|
| 08:56-09:50 | Pass 1: base, fastsend, p4s, mbspin, fastsend_mbspin. 10 min each, feeds A+B. |
| 09:50-11:16 | Pass 2: the same five plus each with feed B off (`*_A`), 8 min each, order rotated. 10:51: kaspr rebuilt for 20 s during `p2_p4s` (noted, minor). |
| 11:16 onward | Experiments, interleaved with reference runs (base, fastsend_mbspin), 8 min each, see below. |

Experiment 1, **pinning (`fsmb_pin`).** fastsend + spinning MsgBuf, with every
busy thread pinned on NUMA node 2:

| channel | sock A | sock B | MsgBuf |
|---|---|---|---|
| ES | 17 | 49 | 16 |
| NQ | 19 | 51 | 18 |
| ZN | 21 | 53 | 20 |

- sock A and B of a channel sit on the two SMT siblings of one core.
- Each MsgBuf is alone on its own core.
- Idle actors run on cpu 22, books on cpu 54.
- Every other kaspr thread is confined to nodes 0, 1 and 3 (`taskset`).

Experiment 2, **socket reader fast_send into MsgBuf (`rfs`, `rfs_pin`,
`rfs_pin_A`).** Each reader runs MsgBuf, MessageProcessor, decode, handler
and book (book_fast_send) inline. There are no thread hops between socket read
and book publish.

- **Pinned layout:** each reader alone on its own physical core.
  - ES: A on 16, B on 17
  - NQ: A on 18, B on 19
  - ZN: A on 20, B on 21
- **Loss risk:** while a reader decodes, it is not reading its socket.
  Measured by Onload `oflow_drop` and `mem_drop` per socket, by gaps and
  recoveries, and with feed B off (`rfs_pin_A`), where any A-side drop
  becomes a gap.

### Current pooled snapshot (10:55, passes 1-2 partial), book latency, us

p1 / p10 / p50 / p90 / p99 / p999:

| config | runs | ES Z6 | NQ Z6 | ZN Z6 |
|---|---|---|---|---|
| base | 2 | 3.8 / 4.7 / 6.9 / 11.1 / 22.1 / 41.5 | 3.7 / 4.4 / 6.5 / 9.3 / 13.1 / 23.7 | 3.8 / 4.5 / 7.0 / 11.6 / 61.4 / 151.8 |
| fastsend | 2 | 2.2 / 2.9 / 4.9 / 8.1 / 19.6 / 80.1 | 2.0 / 2.7 / 4.7 / 6.8 / 10.3 / 20.8 | 1.9 / 2.7 / 4.7 / 7.8 / 54.0 / 179.3 |
| mbspin | 2 | 2.7 / 3.8 / 5.3 / 8.1 / 16.6 / 34.6 | 3.2 / 3.6 / 5.3 / 7.0 / 10.0 / 17.2 | 3.2 / 3.7 / 4.9 / 8.8 / 44.0 / 153.6 |
| fastsend_mbspin | 1 | 1.5 / 1.9 / 2.5 / 5.1 / 13.3 / 30.7 | 0.8 / 1.3 / 2.1 / 3.7 / 6.9 / 16.4 | 0.9 / 1.4 / 2.4 / 5.2 / 59.7 / 296.6 |
| fastsend_mbspin_A | 1 | 1.5 / 1.8 / 2.3 / 3.9 / 11.1 / 23.2 | 1.4 / 1.7 / 2.1 / 3.1 / 5.8 / 9.1 | 1.7 / 2.0 / 2.5 / 4.0 / 46.7 / 109.3 |
| base_A | 1 | 4.0 / 4.6 / 6.6 / 9.6 / 16.9 / 37.1 | 3.8 / 4.1 / 5.0 / 7.5 / 10.4 / 17.6 | 3.9 / 4.4 / 6.6 / 10.9 / 66.0 / 266.3 |

Notes:

- **The fastsend tail penalty weakened with the second run.**
  - ES p999 is now 1.9x base (80 vs 42).
  - ZN is 1.2x (179 vs 152).
  - NQ shows none (21 vs 24).
- **The fastsend median gain held at about 2 us on every book.**
- **fastsend_mbspin reaches a 2.1-2.5 us median,** about 3x better than base.
  ZN p999 is its weak point.
- **No gaps or extra recoveries in any run so far,** including feed-A-only.

## Preliminary results: pass 1 (08:56-09:50, one 10-minute run per config)

*To be replaced by pooled passes 1-3. Caveat: `mbspin` and `fastsend_mbspin`
straddled the 09:30 open (about 7x the ES/NQ messages), so they are not yet
comparable with the others.*

### P1. End-to-end book latency, t1 - t0 (us), first 120 s of each run dropped

p1 / p10 / p50 / p99 / p999:

| config | ES Z6 | NQ Z6 | ZN Z6 |
|---|---|---|---|
| base | 4.0 / 4.9 / 7.5 / 23.7 / 39 | 3.8 / 4.4 / 6.8 / 14.0 / 34 | 3.5 / 4.3 / 6.2 / 58.7 / 166 |
| fastsend | 2.1 / 2.7 / 3.9 / 21.9 / **998** | 1.9 / 2.7 / 4.3 / 10.4 / **155** | 1.7 / 2.6 / 4.3 / 51.8 / 155 |
| mbspin | 3.0 / 3.8 / 5.2 / 16.4 / 36 | 3.2 / 3.6 / 5.3 / 9.8 / 16 | 3.3 / 3.7 / 4.6 / **33.7 / 130** |
| fastsend_mbspin | 1.5 / 1.9 / **2.5** / 13.3 / 31 | 0.8 / 1.3 / **2.1** / 6.9 / 16 | 0.9 / 1.4 / **2.4** / 59.7 / **297** |
| p4s | 7.4 / 8.9 / 11.6 / 36.4 / 140 | 7.2 / 8.7 / 11.5 / 23.1 / 113 | 7.7 / 9.8 / 13.3 / 79.1 / 185 |

- **fast_send halves the median on every book**, and in the worst cases
  lengthens p999 by up to 25x: ES 39 -> 998 us, NQ 34 -> 155 us.
- **On ZN, the smallest tail is mbspin**, which keeps the async book send:
  p99 33.7, p999 130.
  - Adding fast_send on top gives the best median (2.4 us) and doubles p999
    (130 -> 297 us).
  - This is the trade-off in its cleanest form.

### P2. Mechanism: latency by ingress qlen (book streams pooled)

qlen is the number of packets ahead at MsgBuf when this one arrived.

| config | qlen | share | p50 | p99 | p999 |
|---|---|---|---|---|---|
| base | 0 | 90.5% | 6.9 | 24.5 | 68.9 |
| base | 2-3 | 0.28% | 9.5 | 338 | 971 |
| base | 4+ | 0.05% | 118 | 2,502 | 2,701 |
| fastsend | 0 | 90.7% | 4.2 | 19.3 | 50.2 |
| fastsend | 2-3 | **0.53%** | **30.1** | 978 | 1,069 |
| fastsend | 4+ | **0.26%** | 108 | 2,808 | 2,988 |

- **With fast_send, bursts queue at the decoder.** The share of packets arriving
  behind 2+ others roughly triples: 0.33% -> 0.79%.
- **Packets 2-3 deep wait 3x longer at the median:** 9.5 -> 30.1 us.
- The book update now runs on the decode thread, so every packet behind it in
  a burst waits for it.

### P3. Stage breakdown (median across 10 s windows, us)

Serial path, NQ, p50 / p99:

| stage | base | fastsend | mbspin |
|---|---|---|---|
| A1 reader read -> send | 0.3 / 4.0 | 0.3 / 3.5 | 0.4 / 3.6 |
| A2 send -> MsgBuf | **3.4** / 5.6 | 1.9 / 5.3 | **0.8** / 1.4 |
| A3 MsgBuf -> decode | 0.4 / 1.0 | 0.3 / 0.9 | 0.3 / 0.9 |
| G decode + handler | 0.4 / 2.0 | 0.4 / **3.6** | 0.3 / 1.9 |
| total t0 -> decoded | 4.7 / 8.9 | 3.0 / 8.9 | **1.9** / 5.4 |

- **The socket -> MsgBuf hop is the largest serial stage** (3.4 us p50). It is the
  futex wakeup of a sleeping MsgBuf on a sparse feed. Spinning MsgBuf cuts it to
  0.8 us.
- **Decode plus handler work is 0.3-0.4 us at p50.**

Decode-stage p99 (us):

| | ES | NQ | ZN |
|---|---|---|---|
| serial base (decode + handler + push to book) | 3.5 | 2.0 | 2.0 |
| serial fastsend (also the book update) | 6.9 | 3.6 | 3.6 |
| serial mbspin | 2.8 | 1.9 | 2.2 |
| serial fastsend_mbspin | 5.3 | 2.8 | 3.8 |
| parallel worker decode (decode + recording only) | 3.8 | 4.6 | **9.1** |

- **fast_send roughly doubles the decode-stage p99**, because it now contains
  the book update.
- **Parallel decode alone is worse at p99 than serial decode + handler
  together:** 4.5x on ZN. Recording turns each handler callback into a heap
  `std::function`, and ZN packets carry the most messages. ZN p50 shows the
  same: 2.8 us on the worker vs 0.5 us serial.
- **Decode is not the source of 100 us+ tails.** Windows with decode p999 above
  100 us: 0% for most configs, 6% fastsend ES, 2% parallel NQ.

### P4. Where parallel decode fails (p4s)

1. **A fixed cost on every packet.**
   - B (hop to worker) 0.9 + C (decode + recording) 1.3-2.7 + D (hop to
     handler) 0.8 + F (replay) 0.4-1.0 us at p50.
   - That is 3-4.5 us added at p1 and p50. Serial does the same work inline in
     0.3-0.4 us.
2. **The tail spreads over more hops.**

   Stage holding the window's worst message, in windows where it exceeded
   100 us:

   | | ES (11) | NQ (19) | ZN (13) |
   |---|---|---|---|
   | A2 socket -> MsgBuf (shared with serial) | 3 | **12** | **5** |
   | parallel-only B/C/D/E/F | **7** | 6 | 7 |

   Stage with the largest p999, over all windows:

   | | ES | NQ | ZN |
   |---|---|---|---|
   | A2 | 24 | 30 | 16 |
   | D worker -> handler | **18** | 3 | 2 |
   | C worker decode | 0 | 7 | **22** |
   | E reorder wait | 3 | 3 | 4 |

   - **E is a failure mode serial does not have:** head-of-line blocking
     behind a stalled worker. Typical worst case per window is 11-26 us.
3. **Threads off-CPU.** Run-queue wait (runnable, no CPU) per 10 s sample, us:

   | thread role | config | p50 | p99 | max |
   |---|---|---|---|---|
   | MsgBuf | base | 38 | 950 | 1,513 |
   | socket readers | base | 9 | 5,619 | 8,632 |
   | parallel handler | p4s | 256 | 13,027 | 24,019 |
   | parallel workers | p4s | 15 | 15,600 | 30,723 |
   | MsgBuf | mbspin | 0 | 10,049 | 15,539 |

   - **p4s had 31 of 48 10 s buckets with a book message over 100 us,** against
     13 of 49 for base.
   - **In 90 of the 91 such buckets across all configs, a hot-path thread had
     waited more than 100 us for a CPU.** The exception is one fastsend bucket.
   - Parallel adds 15 threads that must all be on-CPU at the right moment, on
     an unpinned shared box.
   - Unpinned spinning threads (mbspin) are also preempted heavily, which is
     why spinning shortens the median and p99 but leaves millisecond maxima.

### Feed health

No gap waits, gaps, or extra data recoveries in any pass-1 run.

## 1. System and measurement

- **Path:**
  1. socket reader A/B (Onload, busy-poll)
  2. MsgBuf: merges A/B, dedup by sequence
  3. MessageProcessor: reorder, gap detection
  4. decode: SBE
  5. handler: MBO book events
  6. book: TachBook, MBO L3
  7. publish
- **Stamps:**
  - t0: user space, right after `recvfrom`.
  - t1: TachBook, just before it publishes.
  - Reported latency is t1 - t0, per message.
  - Stage stamps at reader send, MsgBuf entry, decode start and decode end.
- **Limit:** time before t0 is invisible: NIC to socket buffer, and a reader
  waiting for a CPU. Socket readers are not isolated. The scheduler data shows
  them waiting in the run queue (section 5).
- **Host:** 64 logical CPUs. Nothing isolated or pinned. The recorder was down.

## 2. Configurations

| name | decode | book hand-off | MsgBuf mailbox | feeds |
|---|---|---|---|---|
| base | serial, inline | `send` (book thread) | BQueue (sleeps) | A+B |
| fastsend | serial | `fast_send` (inline) | BQueue | A+B |
| mbspin | serial | `send` | LockFreeMPSC, spinning | A+B |
| fastsend_mbspin | serial | `fast_send` | spinning | A+B |
| p4s | 4 parallel workers + resequencer, spinning | `send` | BQueue | A+B |
| *_A | same as above | | | A only |

Runs: 8-10 minutes each. Order rotated between passes. First 120 s of each run
dropped (snapshot recovery).

## 3. Result 1: inline book vs hand-off (median vs tail)

(TODO tables: end-to-end p1..p999 per config; latency by ingress qlen.)

Mechanism: with `fast_send`, the decode thread does the book update itself.
During a burst, the next packet waits at MsgBuf until it finishes. The ingress
qlen distribution shifts right, and the latency conditional on qlen 2+ grows.
With `send`, the decode thread is free after a heap copy and a push. The burst
queues at the book's mailbox instead, and only the book's own updates wait.

## 4. The hop that matters: socket reader -> MsgBuf

(TODO: stage A2 sleeping vs spinning. A sparse feed means MsgBuf is asleep for
nearly every packet. The futex wakeup costs 2-4 us at p50. Spinning cuts it to
about 0.8 us.)

## 5. Result 2: why parallel decode does not work

Data:

- **Parallel:** two `p4s` runs, 4 decode workers per channel, workers and
  HandlerIfActor busy-polling, about 75-81 steady 10 s windows per channel.
- **Serial:** four `base` runs, 137-149 windows per channel.
- First 12 windows (120 s) of each run dropped.
- Each cell is the median across windows of that window's p1 / p10 / p50 / p90 /
  p99 / p999, in us.
- Source: `STAGE` lines logged by HandlerIfActor (parallel) and DataDecoder
  (serial).

### 5.1 The pipelines being compared

```
serial (base)
  sock reader  ==send==>  MsgBuf (sleeps) --fast_send--> MessageProcessor --fast_send--> DataDecoder --call--> handler_if
  stages:      A1 read->send | A2 send->MsgBuf | A3 MsgBuf->decode | G decode+handler (inline)

parallel (p4s)
  sock reader  ==send==>  MsgBuf (sleeps) --fast_send--> MessageProcessor ==send==> worker k (spins) ==send==> HandlerIfActor (spins) --call--> handler_if
  stages:      A1 | A2 | A3 MsgBuf->dispatch (incl. packet copy) | B ->worker | C decode+record | D ->handler | E reorder wait | F replay
```

Both end with the same send to TachBook, which is not part of these stages.

### 5.2 Stage breakdown, NQ (318)

ES and ZN show the same pattern; full tables are in `md_paper_data/stages.md`.

| stage | serial base | parallel p4s |
|---|---|---|
| A1 reader read -> send | 0.1 / 0.2 / 0.3 / 1.1 / 3.6 / 6.2 | 0.1 / 0.2 / 0.3 / 1.0 / 3.5 / 5.5 |
| A2 send -> MsgBuf | 0.6 / 1.6 / 1.9 / 3.4 / 4.8 / 7.2 | 0.8 / 1.6 / **3.3** / 4.1 / 5.5 / 9.8 |
| A3 MsgBuf -> decode / dispatch | 0.2 / 0.2 / 0.3 / 0.4 / 0.8 / 1.1 | 0.4 / 0.8 / **1.1** / 1.7 / 3.1 / 5.5 |
| G decode + handler (serial, inline) | 0.1 / 0.2 / **0.4** / 0.9 / 2.0 / 3.8 | - |
| B hop to worker | - | 0.3 / 0.3 / 0.9 / 1.0 / 1.2 / 2.9 |
| C decode + record (worker) | - | 0.4 / 0.6 / **1.3** / 2.3 / 4.8 / 7.6 |
| D hop to handler | - | 0.2 / 0.3 / 0.8 / 1.0 / 2.7 / 5.0 |
| E reorder wait | - | 0.1 / 0.1 / **0.1** / 0.1 / 0.2 / 6.0 |
| F replay into handler | - | 0.1 / 0.2 / 0.4 / 1.1 / 2.1 / 3.8 |
| **total, t0 -> decoded / replayed** | 1.4 / 2.2 / **2.8** / 5.0 / 7.9 / 11.6 | 4.8 / 5.9 / **7.9** / 10.4 / 14.8 / 21.9 |

Same data at p50 for all three books:

| stage, p50 | ES serial / parallel | NQ serial / parallel | ZN serial / parallel |
|---|---|---|---|
| A2 send -> MsgBuf | 2.1 / 2.5 | 1.9 / 3.3 | 2.9 / 3.4 |
| A3 MsgBuf -> decode/dispatch | 0.3 / 1.3 | 0.3 / 1.1 | 0.3 / 1.3 |
| decode + handler work (serial G; parallel C+F) | 0.8 / 2.5 | 0.4 / 1.7 | 0.6 / 3.5 |
| hops B + D | - / 1.8 | - / 1.7 | - / 1.7 |
| total | 3.9 / 9.1 | 2.8 / 7.9 | 4.4 / 10.3 |

### 5.3 Why: four reasons, from the data

**1. There is nothing to parallelize.**

- The whole per-packet work (serial stage G: decode plus handler) is
  0.4-0.8 us at p50.
- Packets arrive at a median 261 (ES), 1,945 (NQ) and 1,016 (ZN) per second, one
  every 0.5-4 ms. The decode stage is busy well under 0.1% of the time.
- 84-88% of book events are the first event in their packet; a packet carries
  about one message.
- Parallelism only helps if a second packet arrives while the first is still
  being decoded. On this feed that almost never happens.

The reorder stage shows it directly: E is 0.1 us at p50 and p90. Packets
practically never finish out of order, which means two of them are practically
never in flight together. The four workers take turns doing work that one
thread could do between packets.

**2. Each packet pays a fixed hand-off cost that serial does not.**

At p50, against serial:

| item | added cost |
|---|---|
| A3: copy the packet and allocate a `ParDecodePacket` | +0.8-1.0 us |
| B and D: two cross-thread hops, even into spinning threads | +1.7-1.8 us |
| C + F: decode and handler work split across two threads | +1.3-2.9 us |

The split costs 2-3x what the same work costs inline:

- The worker records each handler callback as a heap-allocated `std::function`.
- The packet bytes and the recording each move between cores (dispatcher ->
  worker -> handler), with cache misses at each step.
- ZN, which carries the most events per packet, pays the most: decode +
  record p50 2.7 us and p99 9.2 us, against serial decode + handler 0.6 / 2.2.

The extra costs show already at p1: 4.8-6.3 us parallel against 1.4-2.2 us
serial. They are on every packet, not only in bursts.

**3. The serial path's own hop gets worse.**

Stage A2 (socket -> MsgBuf), which both designs share, rises from 1.9-2.9 to
2.5-3.4 us at p50. MsgBuf's thread waits longer for a CPU, per 10 s sample:

| thread | config | p50 | p99 | max |
|---|---|---|---|---|
| MsgBuf | base | 46 us | 831 us | 10 ms |
| MsgBuf | p4s | 173 us | 4.9 ms | 13 ms |

The 15 extra busy-polling threads (3 channels x 4 workers + 1 handler) compete
for the same cores.

**4. The tail comes from threads being off the CPU, and parallel has more of
them.** Windows whose worst packet exceeded 100 us:

| | ES | NQ | ZN |
|---|---|---|---|
| windows, of total | 20 of 81 | 42 of 74 | 26 of 75 |
| worst packet in A2 (socket -> MsgBuf) | 9 | 29 | 14 |
| worst packet in a parallel-only stage | 10 | 12 | 11 |
| ... of which E (head-of-line blocking) | 4 | 3 | 4 |

- **A2** is the hop serial also has, made worse by the contention above.
- **The parallel-only stages** are B, C, D, E and F.
- **E is head-of-line blocking.** When one worker is descheduled, every later
  packet waits behind it, even if its own worker finished.

Run-queue wait per 10 s sample, p4s:

| thread | p99 | max |
|---|---|---|
| workers | 14.8 ms | 56 ms |
| handler | 12.7 ms | 24 ms |
| socket readers | 13.3 ms | 53 ms |

In every config, 90 of 91 windows with a book message over 100 us had a
hot-path thread waiting more than 100 us for a CPU (pass-1 analysis, P4). With
feed B off (`p4s_A`), there are three fewer polling readers. The handler's wait
drops to 3 us p50, and p999 improves (NQ 31 vs 347 us). Contention is the
lever.

### 5.4 The serialization point

The parallel design fans **decode** out to N workers, but funnels every packet
back into **one** `HandlerIfActor`. That actor replays the decoded events into
`handler_if` in exchange order: a single, ordered consumer.

**What state is actually on the path.** Shared and mutated per message:

- **`handler_if`:** `orderid_to_securityid`, a flat hash map. On every book
  event it gets an insert on a new order or an erase on a delete
  (`handler_if.hpp:749-752`). That is the only shared structure written per
  message. `securityid_to_asset_id` is written only by instrument definitions;
  the two counters (`ingress_qlen_`, `pkt_entry_idx_`) are diagnostics.
- **Decoder** (`msg_decoder.hpp`): no state between packets. It reads each
  entry's per-instrument `rptSeq` (`:119`) but never checks it. It does
  allocate a `std::vector` per incremental-book message, a heap allocation on
  the hot path.
- **The book (TachBook):** keeps its own `orders` map. A modify or delete for an
  unknown order returns silently, with no log line and no counter
  (`TachBook.hpp:656-658`).

This is where ordering would matter. Applied out of order, a delete that
arrives before its add is silently ignored, and the add then leaves a phantom
order in the book.

**How often ordering actually comes into play (measured).** The resequencer's
wait (stage E) is non-zero exactly when a packet finishes decode before its
predecessor. Without the resequencer, that packet would be applied out of
order.

| | ES | NQ | ZN |
|---|---|---|---|
| E p90 | 0.1 us | 0.1 us | 0.1 us |
| E p99 | 0.3 us | 0.2 us | 3.6 us |

- On ES and NQ, E stays at the histogram floor through p99, so out-of-order
  completion is at most about 1% of packets, and possibly much less. On ZN, at
  least 1% of packets waited 3.6 us or more for a predecessor.
- **Not measured:** how many of those pairs touch the same order or
  instrument. Only those would corrupt the book.
- **Also not measured:** whether the book ever saw an unknown order. The only
  counter (`drop_baddata`) is printed at shutdown, and the runs were stopped
  with `kill -9`, so it never printed. The miss on an unknown order is silent
  anyway.

So it is not established that ordering would be an actual problem at this
load. What would settle it:

- log each reordered pair with its securityIDs and orderIDs;
- count unknown-order misses in TachBook;
- print the counters periodically, not only at shutdown.

**Amdahl.**

- Only decode runs in parallel: stage C, 1.3 us p50 on NQ, including the
  recording.
- The replay is serial: stage F, 0.4 us.
- Reaching the serial point costs two hops (B 0.9 + D 0.8 us) and exposes
  packets to head-of-line blocking (E).
- The serial path does decode and handler together, inline, in 0.4 us p50.

**Two independent reasons it cannot win here:**

1. **Structural.** One ordered consumer caps the speed-up at the decode share.
   The replay, the book update and the hand-offs stay serial.
2. **Load.** At 260-1,950 packets/s per channel, packets almost never overlap.
   Even a design with no serial point would have little to parallelize.

**Dropping the actor and locking `orderid_to_securityid`.** With only one
shared map mutated per message, this is feasible.

- **The lock is cheap if uncontended:** about 20 ns. Contended, it costs
  microseconds plus futex sleeps, with cache-line bouncing. At today's
  overlap (about 1% of packets) contention would be rare.
- **It removes the replay hop and the recording.** That is most of the
  parallel overhead.
- **What it does not handle is ordering.** Events would reach TachBook in
  completion order, not exchange order. At this load that is at most about 1%
  of packets on ES and NQ. Whether it corrupts the book is unmeasured. Bursts
  are exactly when overlap, and so reordering, rises. Correctness
  would need either:
  - a per-instrument ordering check, using MDP3's `rptSeq`, which is read
    today and ignored; or
  - routing all events of an instrument to the same worker (sharding by
    instrument, so order holds by construction, with no shared mutable state
    at all).
- It also does not remove the dispatch hop (B), the packet copy, or reason 2.

**Parallelism that works today is per channel.** Channels share no state and
already run on separate threads.

### 5.5 Conclusion

Parallel decode fails here for structural reasons, not because of a bug.

- **The median is worse.** The fan-out adds a fixed hand-off cost of about
  5-6 us per packet (5.1-5.9 us at p50) (copy, two hops, record and replay), to parallelize
  0.4-0.8 us of work. On a feed of about one message per packet, at under 2,000
  packets per second, that work never overlaps.
- **The tail is worse.** It adds three places to stall (B, D, E) and 15
  busy-polling threads. On a host without isolated cores, those threads get
  descheduled, and they push the shared MsgBuf hop's CPU waits up as well.
- **When it could pay:** parallel decode needs packets whose decode cost is
  large compared with the hand-off (many messages per packet), or an arrival
  rate high enough that packets overlap. It also needs isolated cores, so that
  extra threads do not compete.

## 6. Feed A only vs A+B

(TODO.)

## 7. Threats to validity

- Live market, so the load is not controlled. Order rotation helps.
- Unisolated cores, shared box.
- t0 is software, after `recvfrom`.
- Thin tails: p999 rests on hundreds of samples per run.
