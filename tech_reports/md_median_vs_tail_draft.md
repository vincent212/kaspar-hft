# Median vs tail in an HFT market-data path: inline vs hand-off, and why parallel decode loses

*Draft for an arXiv paper. Experimental branch `md-latency-experiments`; not production code.*
*Data: live CME MDP3, 2026-10-01, 08:56-12:10 ET (pre-open and RTH), kaspr on one AMD EPYC 9374F host.*

## Abstract

(TODO after data.) Two results.

1. Running the order book inline on the decode thread (`fast_send`) roughly
   halves median latency against handing it to its own thread (`send`). It
   lengthens the tail, because book work delays the packets queued behind it.
2. Fanning decode out to parallel workers adds two thread hops per packet. On a
   feed whose packets carry about one message each, it buys no throughput. It
   adds a hand-off floor of several microseconds and exposes more threads to
   descheduling, which is where its tail comes from.


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

## 5. Result 2: why parallel decode has longer tails

(TODO:

- stage breakdown
- run-queue wait per thread role
- spike buckets vs the waiting thread
- the p1 floor gap

Thesis: each extra hop adds a floor, and each extra thread adds a chance of
being off-CPU when work arrives.)

## 6. Feed A only vs A+B

(TODO.)

## 7. Threats to validity

- Live market, so the load is not controlled. Order rotation helps.
- Unisolated cores, shared box.
- t0 is software, after `recvfrom`.
- Thin tails: p999 rests on hundreds of samples per run.
