# Market-data latency: decode design, thread hops, and the book hand-off

Live CME MDP3 measurements, 2026-10-01, 06:12 to 07:29 ET.
Branch `md-latency-experiments`, on top of PR #139 (`parallel-actors`) and
PR #138 (`remove-parallel-decode`).

## Summary

- **Hops cost microseconds.** The socket-to-book window (t1 - t0) contains 2
  thread hops on the serial path. Each hop lands on a thread that sleeps on a
  condvar when idle and must be woken.
- **Parallel decode is about 2x slower at the median.** With 4 workers and no
  spin, ES book p50 is 15.3 us against 7.6 us serial. It adds 2 hops. CME
  packets carry about one message each, so there is no decode work to split.
- **The book hand-off is worth about 3 us at the median on NQ.**
  - Running the book inline (`book_fast_send`) cut NQ book p50 from 6.8-6.9 us
    to 3.9 us.
  - Busy-polling the book's mailbox (`tachbook_spin`) cut it to 4.0 us.
  - Both baselines bracketing the experiment agree on NQ.
- **ES and ZN medians improved too, but the baseline drifted.** ES base moved
  from 7.8 to 5.9 us between the bracketing runs. fast_send (5.5) and spin (5.0)
  are both at or below the better baseline, so the ES gain is real but smaller
  than NQ's: about 0.5 to 2.5 us.
- **fast_send costs no cores. Spin burns one core per book** (6 at 100% in the
  test).
- **No book damage.** `drop_baddata` was 0 on ESZ6, NQZ6 and ZNZ6 in all four
  experiment runs, and order counts agree within 4%.
- **Decode is not where the time goes** (R6). SBE decode is about 1 us per
  packet. Each thread hop costs 1-5 us.
  - The socket -> MsgBuf hop is the largest single stage, on both serial and
    parallel.
  - Parallel adds 3-4.5 us of hand-off for nothing: packets almost never queue.
  - Its tail spikes are stalled hops, threads descheduled for up to 6 ms.

## Method

- **Probe:** `kaspr/run_probe.sh`.
  - Solo mode, with the recorder down.
  - Onload trading profile: `EF_POLL_USEC=3000`, `EF_INT_DRIVEN=0`.
  - kaspr built with `USE_TACHBOOK=1`, in `perf_route_tachbook` mode.
  - Channels 310 (ES), 318 (NQ), 344 (ZN family).
- **Books:** universe rolled to Z6/H7. M6/U6 had expired, which left the books
  empty and was why earlier probe runs wrote nothing.
- **Latency:** t1 - t0 per message, from the `.msg` files, one 16-byte record
  per message.
- **Warm-up:** the first 60 s of each window is dropped (300 s for the 900 s
  PR #138 run), to skip snapshot recovery.
- **Quantiles:** a scratch script that copies `kaspr/perf/kh_msg.py`'s loader.
- **Caveats:**
  - Windows are short, 3 to 4 minutes after warm-up.
  - The tape is pre-market and thin: 20-40k front-month book messages per
    window.
  - Runs are sequential, not interleaved. The experiment is bracketed by two
    baselines to expose drift.
  - Tails above p99 rest on tens to hundreds of samples. Read p999 and max as
    indicative only.
- **Known shutdown defect:** kaspr hangs on shutdown in ZMQ teardown, on every
  configuration. Each run was force-killed after its window, once the samples
  were on disk.

## What is measured

| stamp | where | thread |
|---|---|---|
| t0 | `m->recv_ts = chutil::Time::epoch();` right after `recvfrom` (`mcast_recv/include/mcast_recv/act/SocketReader.hpp:128`). Carried as `recv_time` -> `l3.handlerendtim` (`mdp3/include/mdp3/handler_if.hpp:728`) -> `pl->hndl_tim_epoch` | socket reader |
| t1 | `pl->publish_ts = chutil::Time::epoch();` just before TachBook sends to subscribers (`frame_kaspr/include/frame/ob/act/TachBook.hpp`, `publish_book`) | TachBook (msgbuf thread under fast_send) |
| t2 | probe's own stamp on receipt (`frame_kaspr/include/frame/perf/act/LatencyProbe.hpp:1109`) | probe |

Every number here is **t1 - t0**, from socket read to book publish. The
book-to-probe hop is outside it.

Known defect on all paths: a packet released from the reorder map after a gap
carries the gap-filling packet's t0, not its own.

A **thread hop** is a `send` into another actor's mailbox. `fast_send` runs the
receiver inline on the caller's thread, under the receiver's `fast_send_mutex`
(`actors/cpp/Actor.cpp:119`), and is not a hop.

## Serial design (PR #138)

| # | actor | runs on | to next | hop |
|---|---|---|---|---|
| 1 | SocketReader A and B (`SocketReader.hpp:34`) | sock_A / sock_B | `send` ProcessQ to MsgBuf (`:280`) | **yes** |
| 2 | MsgBuf (`mcast_recv/include/mcast_recv/act/MsgBuf.hpp:27`) | msgbuf | `fast_send` to MessageProcessor (`:81`) | no |
| 3 | MessageProcessor `processq` | msgbuf | `fast_send` DecodePacket to DataDecoder | no |
| 4 | DataDecoder `mbo_data` | msgbuf | direct call into handler_if | no |
| 5 | handler_if<UseFastSend=false> (`handler_if.hpp:651`) | msgbuf | `BOOKSEND`: heap copy + `send` to the book (`:29-37`, `:784`) | **yes** |
| 6 | TachBook stamps t1 | TachBook | `send` EndOfBurst to the probe | outside window |

**Hops in the window: 2.** Steps 2 to 5 run on the MsgBuf thread. A and B feed
the same MsgBuf (`interface/mdp3/if/mdp3.hpp:96,107`).

How each thread waits:

| thread | waits by | spins? |
|---|---|---|
| sock_A / sock_B | `select` then blocking `recvfrom` (`chutil/include/chutil/udp_socket.hpp:125,155`). Onload intercepts both and busy-polls for `EF_POLL_USEC`. | yes, under Onload |
| msgbuf | default `BQueue`, mutex + condvar (`actors/cpp/include/actors/BQueue.hpp:43`) | no |
| TachBook | default `BQueue` | no |

Options, one per hop:

| hop | spin it | remove it |
|---|---|---|
| socket -> MsgBuf | MsgBuf on spinning `LockFreeMPSC` (2 producers, supported) | SocketReader `fast_send`s to MsgBuf; decode moves to the socket thread |
| handler_if -> TachBook | `tachbook_spin` **(measured below)** | `book_fast_send` **(measured below)** |

## Parallel design (PR #139, `cme_decode_workers` = N)

- **MessageProcessor** keeps the serial gap and order logic. It copies each
  in-order packet into a `ParDecodePacket`, tagged with a dense `dispatch_id`
  and an `epoch`, and sends it round-robin to a worker.
- **DataDecoderActor** (N of them) runs the unchanged serial decoder,
  `DataDecoder::mbo_data`, against a `RecordingHandler`. Every handler_if
  callback becomes a recorded call. `char*` fields stay pointers into the packet
  bytes, which travel with the calls. Stack arrays are copied. One
  `DecodedPacket` per packet, even an empty one.
- **HandlerIfActor** (one) is the only thread that touches handler_if while live
  decode runs. It replays packets in `dispatch_id` order and acks each with
  `DecodeDone`.
- **On a decode failure**, the callbacks before the failure are applied, then
  `Gap()`, the same as serial. Later packets of that epoch are dropped, and
  recovery is requested.
- **Recovery waits for in-flight packets.** It is deferred until every
  dispatched packet is acked, because RecoveryProcessor writes handler_if from
  its own thread.

| # | actor | runs on | to next | hop |
|---|---|---|---|---|
| 1 | SocketReader A/B | sock | `send` to MsgBuf | **yes** |
| 2 | MsgBuf | msgbuf | `fast_send` to MessageProcessor | no |
| 3 | MessageProcessor | msgbuf | `send` ParDecodePacket to worker k | **yes** |
| 4 | DataDecoderActor k | worker k | `send` DecodedPacket to HandlerIfActor | **yes** |
| 5 | HandlerIfActor -> handler_if | handler | `send` to TachBook | **yes** |
| 6 | TachBook stamps t1 | TachBook | | |

**Hops in the window: 4**, against 2 serial. The `DecodeDone` ack is off the
latency path.

Each packet also costs:

- one heap copy of the bytes
- one heap `DecodedPacket`
- one `std::function` allocation per callback
- a `std::map` insert when the packet arrives out of order

`cme_decode_spin` busy-polls the worker and handler mailboxes, which takes the
wakeups off hops 3 and 4. It has not been measured yet.

Known limits:

- packets that follow a decode failure are discarded, so a second recovery may
  be needed
- `inflight` has no bound
- `PRINTSTATS`/`BURSTEND` can run out of packet order
- the new threads are unpinned

## Results

### R1. PR #138 serial baseline

06:12-06:22, 900 s window, first 300 s dropped.

| stream | msgs | p50 | p90 | p99 | p999 |
|---|---|---|---|---|---|
| ESZ6 book | 79,486 | 7.6 | 13.8 | 27.1 | 55.7 |
| NQZ6 book | 121,702 | 6.8 | 10.0 | 14.9 | 27.0 |
| ZNZ6 book | 47,698 | 7.1 | 13.2 | 53.6 | 157.7 |
| ESZ6 trade | 5,448 | 10.6 | 22.6 | 121.4 | 196.6 |
| ZNZ6 trade | 4,876 | 19.6 | 59.2 | 108.9 | 129.9 |

All values in us.

### R2. PR #139 parallel, 4 workers, no spin

06:35-06:44, first 60 s dropped.

| stream | msgs | p50 | p90 | p99 | p999 |
|---|---|---|---|---|---|
| ESZ6 book | 64,962 | 15.3 | 25.8 | 43.5 | 94.6 |
| NQZ6 book | 82,070 | 13.6 | 19.0 | 28.7 | 51.8 |
| ZNZ6 book | 50,320 | 14.2 | 26.3 | 97.2 | 231.3 |
| ESZ6 trade | 5,501 | 19.8 | 34.0 | 64.4 | 103.7 |
| ZNZ6 trade | 5,555 | 35.6 | 99.5 | 193.9 | 224.3 |

About +7 us at p50 against R1, about two futex wakeups' worth.

### R3. Book hand-off experiment (serial decode)

Four 5-minute runs, back to back, first 60 s of each dropped:

| run | start | config |
|---|---|---|
| B1 | 07:07 | baseline: `send`, sleeping TachBook |
| FS | 07:12 | `book_fast_send true`: TachBook runs inline on the msgbuf thread; 2 hops -> 1 |
| SP | 07:18 | `tachbook_spin true`: TachBook on `LockFreeMPSC`, busy-polling; still 2 hops, no wakeup on the second |
| B2 | 07:23 | baseline again |

p50 / p99 in us. Message counts in brackets.

| stream | B1 | FS | SP | B2 |
|---|---|---|---|---|
| ESZ6 book | 7.8 / 29.0 [24k] | 5.5 / 22.9 [29k] | **5.0** / 21.7 [26k] | 5.9 / 23.0 [22k] |
| ESH7 book | 10.9 / 25.4 | 6.2 / 18.0 | 6.8 / 19.8 | 8.5 / 22.7 |
| NQZ6 book | 6.9 / 15.2 [37k] | **3.9** / 10.0 [41k] | 4.0 / 10.4 [38k] | 6.8 / 15.4 [33k] |
| NQH7 book | 9.1 / 18.8 | 4.0 / 12.0 | 4.2 / 11.7 | 8.1 / 16.9 |
| ZNZ6 book | 4.7 / 25.5 [21k] | **3.5** / 36.6 [21k] | 5.7 / 81.7 [25k] | 5.2 / 44.4 [9k] |
| ESZ6 trade | 10.9 / 34.3 | 8.2 / 47.2 | 7.5 / 58.1 | 8.6 / 27.1 |
| NQZ6 trade | 9.0 / 23.7 | 5.2 / 23.0 | 5.6 / 25.4 | 9.2 / 36.9 |
| ZNZ6 trade | 8.3 / 62.1 [1.5k] | 10.2 / 128.5 [1.8k] | 13.6 / 151.1 [2.6k] | 41.1 / 209.0 [0.5k] |

p90, front-month book:

| stream | B1 | FS | SP | B2 |
|---|---|---|---|---|
| ES | 14.6 | 10.1 | 10.2 | 13.1 |
| NQ | 10.0 | 6.1 | 6.6 | 10.1 |
| ZN | 8.5 | 7.2 | 10.1 | 11.3 |

Book health (`drop_baddata`, orders at shutdown):

| run | ESZ6 | NQZ6 | ZNZ6 |
|---|---|---|---|
| B1 | 0 / 8,217 | 0 / 3,045 | 0 / 44,364 |
| FS | 0 / 7,922 | 0 / 3,042 | 0 / 43,931 |
| SP | 0 / 7,910 | 0 / 3,001 | 0 / 43,966 |
| B2 | 0 / 7,907 | 0 / 3,024 | 0 / 44,191 |

CPU: in SP, six threads (one per TachBook) ran at 100%. FS adds no threads.

#### Reading R3

- **NQ is the clean result.** The baselines agree (6.9 and 6.8 us p50), and
  both treatments land at 3.9-4.0 us. The book hand-off is about 3 us of a 7 us
  median: roughly 40% of socket-to-book on this feed.
  - NQ p90 drops from 10 to 6 us.
  - NQ p99 drops from 15 to 10 us.
- **ES:** the baseline moved, 7.8 -> 5.9 us. FS at 5.5 and SP at 5.0 are at or
  under the better baseline, and p90 drops by 3 to 4.5 us. The gain is real; its
  size is 0.5 to 2.5 us at p50.
- **ZN book p50:** FS is best (3.5 vs 4.7/5.2). SP is no better than baseline.
  ZN p99 is noisy across all four runs (25 to 82 us), including between the two
  baselines (25 vs 44). No conclusion on ZN tails.
- **ZN trade is too thin to read** (0.5k to 2.6k samples) and swings 8 to
  41 us p50 across the two identical baselines.
- **FS vs SP:** equal on NQ, close on ES. FS is better on ZN and costs no cores.

  FS has a structural cost. Book work now runs on the decode thread, so a
  burst on one book delays decode of the next packet on that channel. With
  several books per channel, a slow book holds up its neighbours. In SP that
  work stays on separate threads.

### R4. Pinned layout attempt (discarded)

07:40-07:58. Pinning, all on NUMA node 2:

- **Socket readers:** A and B of a channel together on one logical CPU (16, 17
  or 18).
- **MsgBuf:** on that core's SMT sibling (48, 49 or 50).
- **TachBooks:** 2 per CPU on 21, 22 and 23.

Medians matched the unpinned runs: fast_send was 3.5-4 us faster at p50 on all
books. **Tails were ruined in both cases:** p99 0.2-1.5 ms and p999 1.5-3 ms.

Cause: under Onload each socket reader busy-polls for up to 3 ms after every
packet. A and B carry the same feed, so both poll at once, and on one logical
CPU they preempt each other. The NQ pair was forcibly switched about 61,000
times in 5 minutes. A reader that stamps t0 and is then preempted before it
hands the packet to MsgBuf adds a scheduler time slice to that packet.

Rule: **never put A and B on the same logical CPU.** R4 is not used for any
conclusion.

### R5. Parallel decode, 4 workers, spinning, unpinned

- **Serial reference:** 08:05, 8 minutes.
- **P4 spin:** 08:14, 8 minutes. 4 workers per channel plus HandlerIfActor,
  all busy-polling: 15 spinning threads, confirmed at 100% CPU each.
- First 60 s of each run dropped.

| stream | case | msgs | p1 | p10 | p50 | p90 | p99 | p999 |
|---|---|---|---|---|---|---|---|---|
| ESZ6 book | serial | 35,344 | 4.1 | 4.8 | 7.2 | 14.7 | 28.2 | 62.3 |
| | P4 spin | 47,903 | 7.2 | 8.8 | 12.1 | 22.5 | 82.9 | 1,388 |
| NQZ6 book | serial | 66,054 | 3.5 | 4.2 | 5.3 | 8.6 | 13.7 | 220.1 |
| | P4 spin | 92,726 | 6.4 | 7.5 | 9.7 | 13.8 | 22.2 | 130.5 |
| ZNZ6 book | serial | 40,194 | 3.6 | 4.4 | 7.5 | 12.1 | 58.0 | 188.0 |
| | P4 spin | 60,083 | 7.8 | 9.7 | 13.4 | 22.4 | 118.9 | 2,655 |

- **Spin helps a little.** Against P4 without spin (R2, earlier hour), p50
  drops 1-4 us.
- **Parallel is still about 5 us behind serial at p50,** and 3-4 us behind at
  p1. The p1 gap is the floor cost of the extra hops, even with no wakeup.
- **The tail is worse, not better.**

### R6. Where the time goes in the parallel path

HandlerIfActor timestamps every packet through the stages below and logs
quantiles every 10 s (`STAGE` lines in the kaspr log). Same run as R5's P4 spin
(a second run, 08:23).

| stage | measures |
|---|---|
| A | t0 -> dispatch: socket -> MsgBuf hop, reorder, MessageProcessor, packet copy |
| B | dispatch -> worker start (hop) |
| C | worker decode, including recording the calls |
| D | worker end -> HandlerIfActor (hop) |
| E | wait behind an earlier packet (reorder) |
| F | replay into handler_if, including the send to the book |

Typical steady-state 10 s window, p50 / p99 in us:

| stage | ES (310) | NQ (318) | ZN (344) |
|---|---|---|---|
| A sock -> dispatch | **3.5 / 12.2** | **3.3 / 8.7** | **5.2 / 10.8** |
| B to worker | 0.9 / 2.8 | 0.9 / 2.1 | 0.9 / 2.5 |
| C decode | 1.1 / 3.8 | 1.4 / 6.2 | 1.3 / 8.7 |
| D to handler | 0.8 / 6.6 | 0.8 / 3.4 | 0.8 / 3.1 |
| E reorder wait | 0.1 / 0.3 | 0.1 / 0.3 | 0.1 / 3.5 |
| F replay | 0.9 / 3.1 | 0.5 / 2.0 | 0.5 / 2.0 |
| total to replayed | 7.2 / 22.7 | 7.0 / 16.3 | 8.7 / 21.9 |

The TachBook hop (replayed -> t1) adds 2-3 us on top.

Findings:

1. **Decode is not the long pole.**
   - Stage C is 1.1-1.4 us at p50, and that includes recording each callback
     into a heap `std::function`. Pure SBE decode of a one-message packet is
     well under that: fixed offsets, no parsing.
   - The longest stage is A, the socket -> MsgBuf hop, at 3.3-5.2 us. Serial
     pays it too.
2. **Parallel adds about 3-4.5 us of hand-off overhead and buys nothing.**
   - B + C + D + F is 3.2-4.4 us at p50. Serial does decode and handler work
     inline in about 1.5 us.
   - E, the reorder wait, is 0.1 us at p50, so packets almost never queue
     behind one another. With about 1.1 messages per packet and a few packets
     in flight at most, there is no backlog for parallel workers to absorb.
3. **Tail spikes are stalled hops, not slow decode.**
   - Most 10 s windows have a total p99 of 13-30 us. Some windows spike to
     0.2-6 ms.
   - The spike lands in A, B or D almost every time, for example a 5.9 ms
     D_to_handler on ES. Stage C max stays mostly under 50 us.
   - A stalled hop means the receiving thread was off the CPU. 15 spinning
     threads, unpinned on a shared box, get descheduled and preempted.
   - The first window after start also carries the snapshot-recovery backlog,
     up to 34 ms on ZN.

### R7. Stage A split, with p1 and p10 (08:33, P4 spin)

Stage A split into three parts:

| part | from -> to | includes |
|---|---|---|
| A1 | t0 -> reader `send` | reader batching, `ioctl(FIONREAD)`, buffer refill |
| A2 | `send` -> MsgBuf handler entry | `BQueue` push + condvar wakeup of MsgBuf |
| A3 | MsgBuf entry -> dispatch | `fast_send` to MessageProcessor, `msg_q` map copy, `ParDecodePacket` copy |

p1 / p10 / p50 / p99, steady-state 10 s window, in us:

| stage | ES (310) | NQ (318) | ZN (344) |
|---|---|---|---|
| A1 read -> send | 0.2 / 0.3 / 0.6 / 6.1 | 0.2 / 0.2 / 0.4 / 4.9 | 0.1 / 0.2 / 0.3 / 4.3 |
| **A2 send -> MsgBuf** | **1.3 / 3.1 / 3.8 / 11.9** | **1.0 / 3.0 / 3.5 / 8.2** | **0.4 / 1.5 / 1.9 / 5.9** |
| A3 MsgBuf -> dispatch | 0.4 / 1.0 / 1.4 / 3.7 | 0.4 / 0.9 / 1.3 / 3.9 | 0.3 / 0.8 / 1.1 / 4.9 |
| B to worker | 0.5 / 0.8 / 1.0 / 4.2 | 0.3 / 0.3 / 0.9 / 1.5 | 0.6 / 0.7 / 0.9 / 1.7 |
| C decode | 0.5 / 0.7 / 1.7 / 5.5 | 0.7 / 1.0 / 1.4 / 4.5 | 0.8 / 1.4 / 3.1 / 9.8 |
| D to handler | 0.6 / 0.8 / 0.9 / 5.9 | 0.6 / 0.7 / 0.8 / 3.6 | 0.6 / 0.7 / 0.8 / 678 |
| E reorder wait | 0.1 / 0.1 / 0.1 / 4.2 | 0.1 / 0.1 / 0.1 / 0.2 | 0.1 / 0.1 / 0.1 / 3.8 |
| F replay | 0.6 / 0.8 / 1.0 / 4.5 | 0.3 / 0.3 / 0.5 / 2.0 | 0.3 / 0.4 / 0.9 / 3.1 |
| **total -> replayed** | **6.9 / 8.8 / 10.9 / 29.8** | **5.9 / 7.6 / 9.1 / 19.0** | **5.7 / 6.9 / 9.8 / 686** |

Findings:

- **A2 is the single largest stage**: 1.9-3.8 us at p50, and already 1.5-3.1 us
  at p10. It is the one remaining hop into a sleeping thread (`BQueue` condvar).
  The spinning hops B and D cost 0.3-0.9 us.
- **The reader's batching and refill (A1) is small:** 0.3-0.6 us at p50.
- **The two packet copies plus the map insert (A3) cost 1.1-1.4 us.**
- **The p1 floor (5.7-6.9 us) has no single culprit.** It is nine steps of
  0.1-1.3 us each. The only way below it is fewer steps.
- **ZN's 686 us p99 in that window came from D alone**, the handler thread
  stalled. That is scheduling, not work.

### R8. MsgBuf mailbox: BQueue vs ShardedBQueue (P4 spin)

- **Change:** `cme_msgbuf_mailbox sharded` on all three channels. Everything
  else is the same as R7.
- **Runs:** BQueue 08:33, ShardedBQueue 08:36, 8 minutes each.
- **Method:** each value is the median, across the steady-state 10 s windows
  (the first 2 per channel skipped), of that window's percentile.

A2 (send -> MsgBuf), p1 / p10 / p50 / p99 / p999, in us:

| chan | BQueue | ShardedBQueue |
|---|---|---|
| ES 310 | 1.1 / 2.3 / 3.8 / 9.7 / 12.6 | 1.4 / 2.0 / 3.9 / 7.5 / 11.8 |
| NQ 318 | 0.8 / 2.2 / 2.8 / 6.8 / 12.5 | 1.2 / 1.9 / 3.7 / 6.4 / 61.8 |
| ZN 344 | 0.5 / 1.6 / 2.1 / 6.7 / 10.6 | 1.2 / 2.0 / 3.9 / 6.9 / 12.3 |

Total, socket -> replayed:

| chan | BQueue | ShardedBQueue |
|---|---|---|
| ES 310 | 6.6 / 8.6 / 10.5 / 25.2 / 35.6 | 6.7 / 7.9 / 10.5 / 23.9 / 30.3 |
| NQ 318 | 5.3 / 6.8 / 8.4 / 16.6 / 24.6 | 4.8 / 6.2 / 8.6 / 15.8 / 73.2 |
| ZN 344 | 5.1 / 6.7 / 9.7 / 20.1 / 31.5 | 6.7 / 8.6 / 11.6 / 20.3 / 31.4 |

Feed health over the 8 minutes:

| MsgBuf mailbox | "waiting for gap" | gaps declared | data recoveries beyond startup |
|---|---|---|---|
| BQueue | 0 | 0 | 0 |
| ShardedBQueue | 10 | 2 | **2** |

Findings:

- **No latency gain.** A2 is flat to worse: p50 3.7-3.9 us against
  2.1-3.8 us, and p1 is higher. ShardedBQueue only reduces producer-side lock
  contention. Its consumer still parks on a condvar, so the wakeup, which is
  what A2 costs, is unchanged. MsgBuf has two producers (A and B), so there was
  little contention to remove.
- **Correctness cost.** ShardedBQueue does not keep arrival order. Packets
  reached MessageProcessor out of sequence. Twice in 8 minutes the reordering
  was more than its 3-packet wait, so it declared a gap and ran a full data
  recovery on a healthy feed.
- **Verdict:** ShardedBQueue must not be used for MsgBuf. Removing the A2 cost
  needs a consumer that does not sleep: `cme_msgbuf_mailbox lockfree_spin`, not
  yet measured.

Implications:

- The next latency target is the **socket -> MsgBuf hop (stage A2)**, which
  both designs pay. Options: spin MsgBuf, or have the socket readers
  `fast_send` into it.
- Spinning threads must be **pinned to dedicated cores**, never sharing a
  logical CPU with each other or with the socket readers (see R4). Otherwise
  the tail gets worse.

## Conclusions

1. On the production path, the one hop that matters after the socket is
   handler_if -> book. Removing it (`book_fast_send`) is the cheapest win
   measured: about 3 us at p50 on NQ, with no extra cores and no book damage.
2. Spinning the book is about as fast but costs a core per book. Prefer it only
   if the decode thread must stay free of book work.
3. **Parallel decode does not pay on this feed.** The stage timing (R6) shows
   decode is about 1 us per packet. The hops around it cost more than the decode
   itself, and packets almost never queue, so there is nothing to parallelize.
   - Spinning the parallel mailboxes recovers 1-4 us of the 7 us penalty.
   - It is still about 5 us behind serial at p50, and the tail is worse.
4. **Socket -> MsgBuf is the largest stage on both paths** (3-5 us at p50). It
   is the next thing to remove.
5. **Pinning:** never put socket readers A and B on the same logical CPU (R4).
   Unpinned spinning threads produce ms-scale stalls (R6).

## Next measurements

- **Repeat R3 during RTH.** Use 15-minute windows and alternate FS and B several
  times, so ES and ZN get enough samples and drift averages out.
- **MsgBuf spin**, and SocketReader `fast_send` to MsgBuf (0 hops in the window).
- **Parallel decode with `cme_decode_spin`**, for N = 1, 4 and 8, against serial.
- **Pinning:** spun threads on quiet cores, for example NUMA node 2 per
  `kaspr/config/cme.ini`.

## Configuration keys added

All default off; production behaviour is unchanged.

| key | section | effect |
|---|---|---|
| `cme_decode_workers N` | `cme.<channel>` | parallel decode with N workers (power of two) |
| `cme_decode_spin true` | `cme.<channel>` | workers and HandlerIfActor busy-poll |
| `book_fast_send true` | `kaspr.general` | handler_if<UseFastSend=true>: book runs inline on the decode thread |
| `tachbook_spin true` | `kaspr.general` | every TachBook busy-polls its mailbox (one core each) |
| `cme_msgbuf_mailbox K` | `cme.<channel>` | MsgBuf mailbox: `bqueue` (default), `sharded` (do not use, see R8), `lockfree`, `lockfree_spin` |

Raw data: `/home/vincent/perf/mdperf/pr138_onload`, `pr139_par4`, `par_1_base`, `par_p4s`, `par_p4s_stages`, `par_p4s_split`, `par_p4s_sharded`, `exp_1_base`,
`exp_2_fastsend`, `exp_3_tbspin`, `exp_4_base`.
