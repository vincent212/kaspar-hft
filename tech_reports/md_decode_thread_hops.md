# MDP3 decode: serial vs parallel design, counted in thread hops

Status 2026-10-01. Branch `parallel-actors` (PR #139) on top of
`remove-parallel-decode` (PR #138).

A **thread hop** is a message that crosses from one thread to another through
an actor mailbox (`send`). `fast_send` runs the receiver's handler inline on
the caller's thread under the receiver's `fast_send_mutex`
(`actors/cpp/Actor.cpp:119`), so it is not a hop. A direct virtual call is not a
hop.

A hop into an actor whose thread is asleep costs a futex wakeup: microseconds,
more if the core is in a deep C-state. That cost is what this note is about.

## What the latency probe measures

| stamp | where | thread |
|---|---|---|
| t0 | `m->recv_ts = chutil::Time::epoch();` right after `recvfrom` (`mcast_recv/include/mcast_recv/act/SocketReader.hpp:128`). Carried as `recv_time` -> `l3.handlerendtim` (`mdp3/include/mdp3/handler_if.hpp:728`) -> `pl->hndl_tim_epoch` | socket reader |
| t1 | `pl->publish_ts = chutil::Time::epoch();` just before TachBook sends to subscribers (`frame_kaspr/include/frame/ob/act/TachBook.hpp:1260`) | TachBook |
| t2 | probe's own stamp on receipt (`frame_kaspr/include/frame/perf/act/LatencyProbe.hpp:1109`) | probe |

Every latency number in the perf reports is **t1 - t0**: socket read to book
publish. The book -> probe hop (t1 -> t2) is outside it.

The probe only sees data in perf mode: probes are built for TachBooks only
(`kaspr/src/kaspr.cpp:262-295`), and TachBook only gets the feed when
`perf_route_tachbook` swaps the handler's book vector (`kaspr.cpp:497-504`).

Known defect, both paths: a packet released from the reorder map after a gap
carries the **gap-filling** packet's t0, not its own (`MessageProcessor.hpp`,
`processq(m->buf.recv_ts, ...)`).

## Serial design (PR #138, `cme_decode_workers` 0 or absent)

One packet, socket to book:

| # | actor | runs on | to next | hop |
|---|---|---|---|---|
| 1 | SocketReader A and B (`SocketReader.hpp:34`) | sock_A / sock_B | `send` ProcessQ to MsgBuf (`:280`) | **yes** |
| 2 | MsgBuf (`mcast_recv/include/mcast_recv/act/MsgBuf.hpp:27`) | msgbuf | `fast_send` ProcessQ to MessageProcessor (`:81`) | no |
| 3 | MessageProcessor `processq` (`mdp3/include/mdp3/act/MessageProcessor.hpp`) | msgbuf | `fast_send` DecodePacket to DataDecoder | no |
| 4 | DataDecoder `mbo_data` (`mdp3/include/mdp3/DataDecoder.hpp`) | msgbuf | direct call into handler_if | no |
| 5 | handler_if<false,...> (`handler_if.hpp:651`) | msgbuf | `BOOKSEND`: heap copy + `send` Data to the book (`:29-37`, `:784`) | **yes** |
| 6 | TachBook (`TachBook.hpp:472,1192`) stamps t1 | TachBook | `send` EndOfBurst to the probe (`:1271`) | yes, outside t1-t0 |

**Hops inside t1 - t0: 2.** Socket -> MsgBuf, and handler_if -> TachBook.
Steps 2 to 5 all run on the MsgBuf thread. MessageProcessor and DataDecoder have
their own threads, but those threads are idle on this path. A and B both feed
the one MsgBuf (`interface/mdp3/if/mdp3.hpp:96,107`).

### How each thread waits today

| thread | waits by | spins? |
|---|---|---|
| sock_A / sock_B | `select` with no timeout, then blocking `recvfrom` (`chutil/include/chutil/udp_socket.hpp:125,155`). `blocksock` is ignored (`mdp3.hpp:94`). Under Onload, `select`/`recvfrom` are intercepted and busy-poll for `EF_POLL_USEC=3000` us before blocking. | under Onload, yes (3 ms) |
| msgbuf | default `BQueue`: mutex + condvar, sleeps when empty (`actors/cpp/include/actors/BQueue.hpp:43`) | no |
| TachBook | default `BQueue` | no |
| probe | default `BQueue` | no (outside the window) |

`spinbuf` is passed to MsgBuf and ignored (`MsgBuf.hpp:42`).

### What can spin in the serial path

Both hops in the window land on a thread that sleeps when idle:

1. **MsgBuf mailbox.** Switch to `LockFreeMPSC` with `consumer_spin`. Removes
   the wakeup on hop 1. MsgBuf has two producers (A and B), which LockFreeMPSC
   supports.
2. **TachBook mailbox.** Same change. Removes the wakeup on hop 2. Costs one
   spinning core per book (6 books in the perf config).

The socket threads already spin under Onload.

Two hops could be removed rather than spun:

- **handler_if -> TachBook:** `handler_if<UseFastSend=true>` already exists
  and runs the book inline on the msgbuf thread (`handler_if.hpp:29-37`). That
  takes hop 2 off the path entirely, at the price of book work on the decode
  thread.
- **SocketReader -> MsgBuf:** could be a `fast_send` (MsgBuf's mutex
  serializes A and B), putting decode on the socket thread. Then t1 - t0 has
  **zero** hops.

None of these four are implemented. They are options to measure.

## Parallel design (PR #139, `cme_decode_workers` = N, power of two)

### Shape

- **MessageProcessor** keeps the serial gap and ordering logic unchanged. For
  each in-order packet it copies the bytes into a `ParDecodePacket` with a dense
  `dispatch_id` and an `epoch`, and sends it round-robin to a worker. The MDP3 seq
  is not used for ordering because it jumps after recovery.
- **DataDecoderActor** (N of them) runs the unchanged serial decoder,
  `DataDecoder::mbo_data`, against a `RecordingHandler`. Every handler_if
  callback becomes a recorded call with copied arguments. `char*` fields stay as
  pointers into the packet bytes, which travel with the calls, and are not
  NUL-terminated. Arrays that point at decoder stack locals are copied. One
  `DecodedPacket` per packet, even an empty one, goes to HandlerIfActor.
- **HandlerIfActor** (one) is the only thread that touches handler_if while
  live decode runs. It replays packets strictly in `dispatch_id` order,
  buffering early arrivals, and acks each with `DecodeDone`.
- **Failure:** on `rc=false` the packet's earlier callbacks are applied, then
  `Gap()`, the same as serial. Later packets of the same epoch are dropped.
  MessageProcessor bumps the epoch, rewinds `qseq_num`, and requests recovery.
- **Recovery** is deferred until every dispatched packet is acked, because
  RecoveryProcessor writes handler_if from its own thread.

### Hops

| # | actor | runs on | to next | hop |
|---|---|---|---|---|
| 1 | SocketReader A/B | sock | `send` ProcessQ to MsgBuf | **yes** |
| 2 | MsgBuf | msgbuf | `fast_send` to MessageProcessor | no |
| 3 | MessageProcessor | msgbuf | `send` ParDecodePacket to worker k | **yes** |
| 4 | DataDecoderActor k | worker k | `send` DecodedPacket to HandlerIfActor | **yes** |
| 5 | HandlerIfActor replays into handler_if | handler | `BOOKSEND` `send` to TachBook | **yes** |
| 6 | TachBook stamps t1 | TachBook | | |

**Hops inside t1 - t0: 4**, against 2 serial. The ack `DecodeDone`
(HandlerIfActor -> MessageProcessor) is a fifth send, but it is off the latency
path. It runs on MessageProcessor's own thread, serialized against `processq`
(on the msgbuf thread) by MessageProcessor's `fast_send_mutex`.

Per packet the parallel path also pays:

- one heap copy of the bytes
- one heap `DecodedPacket`
- one `std::function` allocation per handler callback (the captures are too
  large for small-buffer storage)
- a `std::map` insert when a packet arrives out of order

### What can spin in the parallel path

`cme_decode_spin true` (commit b911995) puts the workers and HandlerIfActor on
`LockFreeMPSC` with `consumer_spin`, removing the wakeups on hops 3 and 4. The
MsgBuf and TachBook hops can be spun the same way as in the serial path, but are
not yet. Cost: N + 1 spinning cores per channel; 3 channels at N=4 is 15 cores.

### Why parallel is slower at the median

CME packets carry about 1.06 to 1.10 messages, so a fan-out has nothing to
divide. Decode is about 1 us. The parallel path adds two hops to sleeping
threads, plus allocations. Measured, Onload, 2026-10-01 pre-market, about
9 minutes each, first 5 minutes (serial) or 1 minute (parallel) dropped:

| stream | serial p50 / p99 (us) | 4 workers, no spin, p50 / p99 (us) |
|---|---|---|
| ES book | 7.6 / 27.1 | 15.3 / 43.5 |
| NQ book | 6.8 / 14.9 | 13.6 / 28.7 |
| ZN book | 7.1 / 53.6 | 14.2 / 97.2 |

About +7 us at p50, consistent with two extra futex wakeups. The runs are 20
minutes apart, so this is indicative, not a controlled A/B.

### Known limitations (from review)

- **Discarded packets after a decode failure.** The packets after a failed one
  are dropped and are already gone from `msg_q`. Serial would keep and replay
  them, so parallel can need a second recovery.
- **No back-pressure.** `inflight` is unbounded. A stalled worker grows mailboxes
  and the reorder buffer.
- **Commands out of order.** `BURSTEND` (compiled out) and `PRINTSTATS` reach
  HandlerIfActor by `fast_send`, ahead of packets still at the workers.
- **New threads unpinned.** Workers and HandlerIfActor are not covered by
  `cme_cpus`.

## Next measurements

Onload, `run_probe.sh`, 5 minutes each, interleaved so the open does not bias
the result:

1. Serial (S1).
2. Parallel, 1 worker, no spin (P1). Hop cost, no fan-out.
3. Parallel, 4 workers, spin (P4s).
4. Parallel, 8 workers, spin (P8s).
5. Parallel, 1 worker, spin (P1s).
6. Serial (S2).

Then the serial options above, starting with spinning MsgBuf and TachBook.
