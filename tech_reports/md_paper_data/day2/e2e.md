<!-- Generated 2026-10-02 10:43 EDT from /home/vincent/perf/mdperf/day2 -->
# End-to-end t1-t0 (us), pooled over passes, first 60s of each run dropped

## Legend

**What is measured.** t1 - t0 per message, in microseconds:

- t0: user-space timestamp on the socket-reader thread, right after `recvfrom`
  returns the packet.
- t1: timestamp in TachBook (the MBO book) just before it publishes the updated
  book.

Time before t0 (NIC, socket buffer) is not included. Stream: front-month
contract (Z6), `book` = book updates, `trade` = trades.

**Columns.**

| column | meaning |
|---|---|
| config | label below |
| runs | number of 8-10 minute runs pooled |
| msgs | messages measured, after dropping the first 120 s of every run (startup recovery) |
| p1..p999 | percentiles of t1 - t0 over those messages |
| max | slowest single message |

Rows in the per-stream tables are sorted by p90, fastest first.

**Config labels.** Every config is `base` plus the listed changes. Exact files:
`configs/<label>/`. Full description: `../md_median_vs_tail_draft.md`,
section "Configuration labels".

| label | what changes vs base | thread hops between t0 and t1 |
|---|---|---|
| `base` | none (production path): socket reader -> MsgBuf (sleeps between packets) -> decode inline -> book on its own thread (sleeps) | 2, both into sleeping threads |
| `fastsend` | book update runs inline on the decode thread (`book_fast_send`) | 1 |
| `mbspin` | MsgBuf busy-polls instead of sleeping (`cme_msgbuf_mailbox lockfree_spin`) | 2, first without wakeup |
| `fastsend_mbspin` | both of the above | 1, without wakeup |
| `rfs` | socket reader runs MsgBuf, decode and book inline (`cme_reader_fast_send` + `book_fast_send`) | 0 |
| `p4s` | parallel decode: 4 busy-polling workers + one resequencing handler per channel (`cme_decode_workers 4`, `cme_decode_spin`) | 4 |
| `fsmb_pin` | `fastsend_mbspin`, busy threads pinned to NUMA node 2 CPUs (not isolated), everything else kept off node 2 | 1 |
| `rfs_pin` | `rfs`, each socket reader pinned alone on a physical core | 0 |
| `<label>_A` | same as `<label>`, feed B switched off (`cme_feed_b false`); one socket reader per channel | same |

All runs: Onload kernel bypass, live CME channels 310 (ES), 318 (NQ) and 344
(ZN), 2026-10-01 08:56-13:00 ET.

**Sections.**

- **Per-pass:** p50 / p99 / p999 of each run block, as a consistency check.
  - `p1`, `p2` = matrix passes 1 and 2.
  - `x1`, `x2` = experiment rounds 1 and 2.
- **Latency by ingress qlen:** latency grouped by how many packets were already
  queued at MsgBuf when this one arrived.
  - Shows queueing in bursts.
  - For `rfs*` there is no MsgBuf queue (the reader calls it directly), so qlen
    is always 0.


## ESZ6_book
| config | runs | msgs | p1 | p10 | p50 | p90 | p99 | p999 | max |
|---|---|---|---|---|---|---|---|---|---|
| rfs_A | 2 | 401,462 | 0.5 | 0.7 | 1.1 | 2.7 | 9.9 | 24.1 | 153 |
| rfs_tbspin_A | 2 | 203,201 | 1.1 | 1.3 | 1.7 | 4.3 | 15.8 | 37.6 | 157 |
| fastsend_mbspin_A | 2 | 175,462 | 1.5 | 1.8 | 2.4 | 5.1 | 18.1 | 150.6 | 179 |
| mbspin_tbspin_A | 2 | 227,792 | 1.7 | 2.2 | 3.1 | 7.0 | 27.6 | 75.6 | 159 |
| mbspin_A | 2 | 216,439 | 3.2 | 3.6 | 4.4 | 7.7 | 18.9 | 47.1 | 140 |
| base_A | 2 | 197,819 | 4.0 | 4.6 | 6.5 | 10.7 | 23.1 | 56.1 | 739 |

## NQZ6_book
| config | runs | msgs | p1 | p10 | p50 | p90 | p99 | p999 | max |
|---|---|---|---|---|---|---|---|---|---|
| rfs_A | 2 | 581,010 | 0.4 | 0.5 | 0.8 | 1.9 | 4.7 | 8.8 | 33 |
| rfs_tbspin_A | 2 | 274,918 | 0.6 | 0.9 | 1.5 | 2.7 | 5.7 | 16.0 | 698 |
| fastsend_mbspin_A | 2 | 233,802 | 0.7 | 1.1 | 2.1 | 3.3 | 6.7 | 19.3 | 1764 |
| mbspin_tbspin_A | 2 | 254,794 | 1.9 | 2.4 | 2.9 | 4.3 | 7.5 | 25.0 | 489 |
| mbspin_A | 2 | 239,846 | 2.6 | 3.6 | 4.3 | 6.6 | 10.1 | 17.7 | 68 |
| base_A | 2 | 221,899 | 3.8 | 4.2 | 5.8 | 8.8 | 12.0 | 22.2 | 116 |

## ZNZ6_book
| config | runs | msgs | p1 | p10 | p50 | p90 | p99 | p999 | max |
|---|---|---|---|---|---|---|---|---|---|
| rfs_A | 2 | 141,972 | 0.4 | 0.6 | 1.0 | 2.5 | 20.6 | 69.3 | 114 |
| fastsend_mbspin_A | 2 | 117,232 | 1.5 | 1.8 | 2.2 | 4.2 | 36.7 | 96.1 | 114 |
| rfs_tbspin_A | 2 | 135,811 | 0.9 | 1.3 | 1.6 | 4.3 | 56.9 | 152.4 | 262 |
| mbspin_tbspin_A | 2 | 107,522 | 2.2 | 2.5 | 3.0 | 5.3 | 58.9 | 151.9 | 252 |
| mbspin_A | 2 | 144,642 | 3.0 | 3.5 | 4.8 | 9.4 | 125.7 | 178.2 | 823 |
| base_A | 2 | 155,915 | 3.8 | 4.2 | 6.3 | 9.8 | 52.6 | 119.0 | 192 |

## ESZ6_trade
| config | runs | msgs | p1 | p10 | p50 | p90 | p99 | p999 | max |
|---|---|---|---|---|---|---|---|---|---|
| rfs_A | 2 | 45,733 | 0.5 | 0.7 | 2.5 | 8.8 | 36.2 | 70.1 | 87 |
| rfs_tbspin_A | 2 | 23,183 | 1.2 | 1.6 | 4.0 | 11.8 | 53.4 | 82.5 | 96 |
| fastsend_mbspin_A | 2 | 20,170 | 1.5 | 2.0 | 4.1 | 12.6 | 400.9 | 476.0 | 489 |
| mbspin_A | 2 | 27,677 | 3.4 | 4.0 | 6.5 | 15.7 | 48.5 | 71.4 | 83 |
| mbspin_tbspin_A | 2 | 29,441 | 1.8 | 2.8 | 5.7 | 16.5 | 61.6 | 97.0 | 111 |
| base_A | 2 | 23,566 | 4.3 | 5.5 | 8.9 | 17.8 | 51.0 | 77.4 | 89 |

## NQZ6_trade
| config | runs | msgs | p1 | p10 | p50 | p90 | p99 | p999 | max |
|---|---|---|---|---|---|---|---|---|---|
| rfs_A | 2 | 21,093 | 0.4 | 0.6 | 0.9 | 3.3 | 17.8 | 31.5 | 44 |
| rfs_tbspin_A | 2 | 10,231 | 0.7 | 1.0 | 2.0 | 5.7 | 31.3 | 80.0 | 93 |
| fastsend_mbspin_A | 2 | 8,160 | 0.7 | 1.2 | 2.5 | 6.5 | 170.9 | 248.5 | 259 |
| mbspin_tbspin_A | 2 | 9,095 | 2.2 | 2.6 | 3.4 | 7.3 | 24.6 | 42.3 | 48 |
| mbspin_A | 2 | 9,712 | 3.1 | 3.9 | 5.3 | 9.7 | 24.1 | 43.7 | 50 |
| base_A | 2 | 10,523 | 3.9 | 4.5 | 7.0 | 11.5 | 25.4 | 48.7 | 58 |

## ZNZ6_trade
| config | runs | msgs | p1 | p10 | p50 | p90 | p99 | p999 | max |
|---|---|---|---|---|---|---|---|---|---|
| rfs_A | 2 | 13,282 | 0.4 | 0.7 | 6.6 | 41.5 | 90.5 | 124.0 | 140 |
| fastsend_mbspin_A | 2 | 10,335 | 1.5 | 1.9 | 7.5 | 50.7 | 154.0 | 233.8 | 243 |
| mbspin_A | 2 | 12,493 | 3.3 | 4.2 | 11.6 | 55.4 | 111.2 | 208.6 | 221 |
| rfs_tbspin_A | 2 | 14,194 | 1.1 | 1.6 | 11.0 | 62.1 | 187.2 | 250.6 | 264 |
| mbspin_tbspin_A | 2 | 9,677 | 2.3 | 3.1 | 15.2 | 78.0 | 235.6 | 320.0 | 332 |
| base_A | 2 | 12,937 | 4.0 | 5.8 | 16.7 | 79.0 | 181.9 | 209.2 | 218 |

# Per-pass p50 / p99 / p999 (consistency check)

## ESZ6_book
| config | d1 | d2 |
|---|---|---|
| base_A | 6.9 / 21.8 / 56 | 6.3 / 23.7 / 56 |
| mbspin_A | 5.7 / 25.9 / 55 | 4.2 / 17.0 / 35 |
| fastsend_mbspin_A | 2.6 / 14.5 / 28 | 2.4 / 19.4 / 156 |
| mbspin_tbspin_A | 3.2 / 20.3 / 41 | 3.1 / 29.2 / 80 |
| rfs_tbspin_A | 1.8 / 17.4 / 44 | 1.7 / 15.4 / 33 |
| rfs_A | 1.0 / 9.6 / 23 | 1.1 / 10.3 / 26 |

## NQZ6_book
| config | d1 | d2 |
|---|---|---|
| base_A | 6.0 / 12.4 / 23 | 5.6 / 11.7 / 22 |
| mbspin_A | 6.0 / 12.3 / 25 | 4.1 / 8.6 / 14 |
| fastsend_mbspin_A | 2.2 / 6.7 / 12 | 2.1 / 6.7 / 22 |
| mbspin_tbspin_A | 2.8 / 7.2 / 13 | 3.0 / 7.5 / 27 |
| rfs_tbspin_A | 1.7 / 6.5 / 20 | 1.4 / 5.5 / 16 |
| rfs_A | 0.8 / 4.7 / 9 | 0.9 / 4.8 / 9 |

## ZNZ6_book
| config | d1 | d2 |
|---|---|---|
| base_A | 6.4 / 54.4 / 119 | 6.2 / 50.0 / 119 |
| mbspin_A | 5.6 / 55.4 / 100 | 4.2 / 148.9 / 198 |
| fastsend_mbspin_A | 2.3 / 28.3 / 60 | 2.2 / 46.7 / 101 |
| mbspin_tbspin_A | 2.9 / 74.3 / 188 | 3.1 / 44.2 / 98 |
| rfs_tbspin_A | 1.6 / 43.6 / 121 | 1.7 / 68.2 / 187 |
| rfs_A | 1.0 / 31.4 / 79 | 1.0 / 16.4 / 39 |

# Latency by ingress qlen (packets queued ahead at MsgBuf), book streams pooled
| config | qlen | share | msgs | p50 | p99 | p999 |
|---|---|---|---|---|---|---|
| base_A | 0 | 99.00% | 569,890 | 6.2 | 24.8 | 76.3 |
| base_A | 1 | 0.97% | 5,572 | 11.0 | 113.8 | 137.1 |
| base_A | 2-3 | 0.03% | 164 | 12.3 | 79.4 | 114.9 |
| mbspin_A | 0 | 99.45% | 597,619 | 4.4 | 32.7 | 155.7 |
| mbspin_A | 1 | 0.50% | 3,000 | 10.9 | 139.8 | 152.8 |
| mbspin_A | 2-3 | 0.04% | 270 | 81.8 | 126.8 | 157.3 |
| mbspin_A | 4+ | 0.01% | 38 | 23.2 | 138.1 | 138.1 |
| fastsend_mbspin_A | 0 | 98.96% | 521,033 | 2.2 | 12.7 | 32.4 |
| fastsend_mbspin_A | 1 | 0.80% | 4,194 | 8.9 | 84.8 | 99.6 |
| fastsend_mbspin_A | 2-3 | 0.14% | 761 | 67.0 | 111.4 | 113.4 |
| fastsend_mbspin_A | 4+ | 0.10% | 508 | 128.0 | 178.3 | 279.4 |
| mbspin_tbspin_A | 0 | 99.51% | 587,221 | 3.0 | 26.4 | 86.1 |
| mbspin_tbspin_A | 1 | 0.47% | 2,784 | 7.8 | 94.2 | 102.2 |
| mbspin_tbspin_A | 2-3 | 0.01% | 75 | 24.4 | 347.3 | 347.3 |
| rfs_tbspin_A | 0 | 100.00% | 613,930 | 1.6 | 22.9 | 91.3 |
| rfs_A | 0 | 100.00% | 1,124,444 | 0.9 | 8.4 | 28.9 |

# Discussion of each setup

Numbers quoted below are from the 2026-10-01 runs in the tables above. They are
front-month book updates, written ES / NQ / ZN, in microseconds. If the tables
are regenerated from new runs, re-check the numbers quoted here.

A "hop" means a message handed to another thread's mailbox. A hop into a
thread that is asleep costs a futex wakeup, about 2-4 us on this feed. The feed
is sparse: about 1 packet per ms on NQ and ZN, 1 per 10 ms on ES. So a
receiving thread is asleep for almost every packet.

## Diagram notation

Each box is an actor. Each column of boxes under a `THREAD` heading runs on
that one OS thread.

```
==send==>        async send: message queued in the receiver's mailbox; the receiver's
                 own thread processes it later. A thread hop.
--fast_send-->   synchronous: the receiver's handler runs immediately, on the
                 caller's thread, under the receiver's lock. No hop.
--call-->        plain virtual function call. No hop.
(sleeps)         the receiving mailbox parks its thread on a condvar when empty;
                 every message pays a futex wakeup.
(spins)          the receiving mailbox busy-polls, so there is no wakeup; it burns
                 a core.
t0 / t1          where the latency clock starts and stops.
```

The book -> LatencyProbe send happens after t1, so it is outside the measured
latency. It is shown only for completeness.

## base (production path)

```
THREAD sock A                         THREAD sock B
  [SocketReader A]  t0                  [SocketReader B]  t0
        |                                     |
        |  ==send==>                          |  ==send==>
        +-----------------+-------------------+
                          v
THREAD MsgBuf  (sleeps)
  [MsgBuf]
    --fast_send-->  [MessageProcessor]   (sequence check, reorder)
    --fast_send-->  [DataDecoder]        (SBE decode)
    --call-->       [handler_if]         (book events)
                          |
                          |  ==send==>  (heap copy of each book event)
                          v
THREAD TachBook  (sleeps)
  [TachBook]  t1
    ==send==>  [LatencyProbe]   (after t1, not measured)

hops between t0 and t1: 2, both into sleeping threads
```

The socket reader hands each packet to MsgBuf (hop 1). MsgBuf, MessageProcessor,
decode and the handler then run inline on the MsgBuf thread. The handler copies
each book event and sends it to TachBook on its own thread (hop 2). Both
receiving threads sleep between packets. This is the path the live recorder
runs.

It is slow at the median (p50 6.6 / 6.3 / 7.3) because both hops pay a wakeup.
The decode and handler work itself is only 0.3-0.9 us. Its tails are among the
better ones with both feeds on (p999 65.6 / 21.9 / 138), for two reasons. Book
work never delays decode: a burst of book updates queues at TachBook, not in
front of the next packet. And no thread busy-polls, so there is little
competition for CPUs. It is the reference point, not a good target.

## fastsend (book inline)

```
THREAD sock A                         THREAD sock B
  [SocketReader A]  t0                  [SocketReader B]  t0
        |                                     |
        |  ==send==>                          |  ==send==>
        +-----------------+-------------------+
                          v
THREAD MsgBuf  (sleeps)  -- also runs the book
  [MsgBuf]
    --fast_send-->  [MessageProcessor]   (sequence check, reorder)
    --fast_send-->  [DataDecoder]        (SBE decode)
    --call-->       [handler_if]         (book events)
    --fast_send-->  [TachBook]  t1       (book update on this same thread)
    ==send==>  [LatencyProbe]   (after t1, not measured)

hops between t0 and t1: 1, into a sleeping thread
```

Same as base, except the handler calls TachBook directly. The book update runs
on the MsgBuf thread, so hop 2 disappears. One hop remains, and it still pays a
wakeup.

The median drops by about 2 us (4.9 / 4.7 / 4.7), which is the cost of hop 2.
The price is in the tail on the books with heavier per-event work. ES p999 goes
from 66 to 80 and ZN from 138 to 179; NQ is unchanged (21). During a burst, the
next packet now waits behind the previous packet's book update. Pass 1 showed
this directly: packets that arrived 2-3 deep in the queue had a p50 of 30 us,
against 9.5 us on base. This is the median-vs-tail trade-off of inlining.

## mbspin (spinning MsgBuf)

```
THREAD sock A                         THREAD sock B
  [SocketReader A]  t0                  [SocketReader B]  t0
        |                                     |
        |  ==send==>                          |  ==send==>
        +-----------------+-------------------+
                          v
THREAD MsgBuf  (SPINS: LockFreeMPSC busy-poll, no wakeup)
  [MsgBuf]
    --fast_send-->  [MessageProcessor]   (sequence check, reorder)
    --fast_send-->  [DataDecoder]        (SBE decode)
    --call-->       [handler_if]         (book events)
                          |
                          |  ==send==>  (heap copy of each book event)
                          v
THREAD TachBook  (sleeps)
  [TachBook]  t1
    ==send==>  [LatencyProbe]   (after t1, not measured)

hops between t0 and t1: 2, the first without a wakeup
```

Same as base, but MsgBuf's mailbox busy-polls instead of sleeping (LockFreeMPSC
consumer spin). Hop 1 still exists, but nothing has to be woken. It costs one
full CPU core per channel.

The socket -> MsgBuf stage drops from 2-4 us to about 0.8 us at the median.
End-to-end p50 improves by about 1-2 us (5.3 / 5.3 / 4.9). The tails are as
good as base or better (p999 34.6 / 17.2 / 153.6), because book work still runs
on its own thread. On an unshared host this would be the safest improvement.
Here the spinning thread is sometimes preempted, which is why its maxima stay
in the milliseconds.

## fastsend_mbspin (both)

```
THREAD sock A                         THREAD sock B
  [SocketReader A]  t0                  [SocketReader B]  t0
        |                                     |
        |  ==send==>                          |  ==send==>
        +-----------------+-------------------+
                          v
THREAD MsgBuf  (SPINS)  -- also runs the book
  [MsgBuf]
    --fast_send-->  [MessageProcessor]   (sequence check, reorder)
    --fast_send-->  [DataDecoder]        (SBE decode)
    --call-->       [handler_if]         (book events)
    --fast_send-->  [TachBook]  t1       (book update on this same thread)
    ==send==>  [LatencyProbe]   (after t1, not measured)

hops between t0 and t1: 1, without a wakeup
```

One hop left, and it does not wake anything: the reader hands off to a spinning
MsgBuf, which runs decode, handler and book inline. It is the best of the
"one hop" designs.

The median is about 3x better than base (2.5 / 2.2 / 2.4), and NQ's tail is
good (p999 15). ZN shows the trade-off at its clearest: p999 290 against 154
for mbspin. ZN packets carry more book events, and they are all processed in
line before the next packet starts. It also spins one core per channel.

## rfs (reader fast_send: zero hops)

```
THREAD sock A                         THREAD sock B
  [SocketReader A]  t0                  [SocketReader B]  t0
        |                                     |
        |  --fast_send-->                     |  --fast_send-->
        +-----------------+-------------------+
                          v   (A and B take turns through MsgBuf's lock)
  [MsgBuf]          (runs on whichever reader thread got there first)
    --fast_send-->  [MessageProcessor]   (sequence check, reorder)
    --fast_send-->  [DataDecoder]        (SBE decode)
    --call-->       [handler_if]         (book events)
    --fast_send-->  [TachBook]  t1       (book update on this same thread)
    ==send==>  [LatencyProbe]   (after t1, not measured)

MsgBuf's own thread is idle. The second copy of each packet (from the other feed)
is dropped by MessageProcessor's sequence check.
hops between t0 and t1: 0. Everything runs on the socket-reader thread.
```

The socket reader calls MsgBuf directly. MsgBuf, MessageProcessor, decode,
handler and the book update all run on the reader thread. t0 and t1 are taken
on the same thread. With feeds A and B, the two reader threads take turns
through MsgBuf's lock. The second one to arrive drops the duplicate packet.

This is the fastest design measured (p50 1.6 / 1.3 / 1.4; p1 0.5-0.6). It had
no packet loss (Onload socket and NIC-ring drop counters all 0) and no gaps.
The exchange-to-t0 measurement shows no extra waiting in the socket buffer
compared with base. Its p999 (28.6 / 12.9 / 162.5) is better than base on ES and
NQ and similar on ZN. The risks: the reader is not reading its socket while it
works, so a heavy burst could build up in the socket buffer. And A and B
contend for one lock. This is one unpinned run on a quiet day; it needs repeats,
including on a high-volume day.

## p4s (parallel decode)

```
THREAD sock A                         THREAD sock B
  [SocketReader A]  t0                  [SocketReader B]  t0
        |                                     |
        |  ==send==>                          |  ==send==>
        +-----------------+-------------------+
                          v
THREAD MsgBuf  (sleeps)
  [MsgBuf]
    --fast_send-->  [MessageProcessor]
                          |
                          |  ==send==>  (copy of the packet, round-robin to worker k)
                          v
THREAD worker k  (x4 per channel, SPINS)
  [DataDecoderActor k]
    --call-->  [DataDecoder] + [RecordingHandler]   (decode, record handler calls)
                          |
                          |  ==send==>  (packet bytes + recorded calls)
                          v
THREAD HandlerIfActor  (SPINS)
  [HandlerIfActor]   (puts packets back in dispatch order)
    --call-->  [handler_if]   (replays the recorded calls)
    ==send==>  DecodeDone back to MessageProcessor's own thread (off the latency path)
                          |
                          |  ==send==>  (heap copy of each book event)
                          v
THREAD TachBook  (sleeps)
  [TachBook]  t1
    ==send==>  [LatencyProbe]   (after t1, not measured)

hops between t0 and t1: 4 (MsgBuf asleep, worker spins, handler spins, TachBook asleep)
```

MessageProcessor copies each packet and sends it round-robin to 4 decode
workers per channel. Each worker decodes into a recording of handler calls. A
single HandlerIfActor replays the recordings into the handler in packet order,
then sends to the book. Workers and HandlerIfActor busy-poll. Hops: MsgBuf,
worker, HandlerIfActor, book.

It is the slowest design (p50 12.3 / 11.0 / 13.7) and has some of the worst
tails (p999 85 / 347 / 265). CME packets carry about one message each, so there
is nothing to split across workers. Each packet just pays for two extra hops,
two copies, and a heap-allocated recording per handler call (worker decode p99
on ZN is 9.1 us, against 2.0 for serial decode). Fifteen extra threads must
also be on a CPU at the right moment. In 61 of 84 ten-second windows a hot
thread was waiting for a CPU while a message took over 100 us. And one stalled
worker holds up every packet behind it.

## fsmb_pin and rfs_pin (pinned, not isolated)

```
fsmb_pin = the fastsend_mbspin diagram, threads pinned (ES / NQ / ZN, NUMA node 2):
  SocketReader A  -> cpu 17 / 19 / 21
  SocketReader B  -> cpu 49 / 51 / 53   (SMT sibling of A: same physical core)
  MsgBuf (spins; runs decode + book) -> cpu 16 / 18 / 20 (a core to itself)
  idle actors -> cpu 22, TachBook threads -> cpu 54, all other threads -> off node 2

rfs_pin = the rfs diagram, threads pinned:
  SocketReader A  -> cpu 16 / 18 / 20   (a core to itself)
  SocketReader B  -> cpu 17 / 19 / 21   (a core to itself)
  MsgBuf thread (idle) -> cpu 22, TachBook threads -> cpu 54, all other threads -> off node 2

CPUs 17-22 also handle the storage controller's interrupts (mpi3mr0).
Nothing is isolated.
```

These are fastsend_mbspin and rfs with every busy thread pinned to fixed CPUs
on NUMA node 2. All other kaspr threads are kept off node 2. In fsmb_pin, sock
A and sock B of a channel share one core as SMT siblings, and MsgBuf has a core
to itself. In rfs_pin, each socket reader has a core to itself. The CPUs are not
isolated from the kernel.

The medians are excellent (fsmb_pin 1.7 / 1.4 / 1.9; rfs_pin 1.3 / 1.1 / 1.3).
The tails are the worst measured (fsmb_pin p999 739 / 312 / 1283; rfs_pin 254 /
84 / 362). The pinned threads waited 22-51 ms per 10 s in the run queue. Those
CPUs also handle the storage controller's interrupts (mpi3mr0), and a pinned
thread cannot move away when kernel or interrupt work lands on it; an unpinned
one simply migrates. Pinning only pays when the CPUs are isolated (isolcpus /
nohz_full, IRQ affinity moved off them). That needs a reboot and was not
tested.

## *_A variants (feed B off)

```
THREAD sock A
  [SocketReader A]  t0
        |  ==send==>   (or --fast_send--> in rfs_pin_A)
        v
  ... rest exactly as in the named config ...

SocketReader B is never started: one copy of each packet, no A/B arbitration,
one fewer busy-polling thread per channel.
```

Same as the named config, but socket reader B is never started. Each channel
reads feed A only, so there is no arbitration between A and B.

Medians are about the same as with both feeds. Tails are almost always shorter:

| config | with A+B (p999) | A only (p999) |
|---|---|---|
| fastsend_mbspin | 30 / 15 / 290 | 23 / 9 / 109 |
| rfs_pin | 254 / 84 / 362 | 43 / 16 / 101 |
| p4s | 85 / 347 / 265 | 50 / 31 / 164 |

Two reasons: one fewer busy-polling thread per channel competing for CPUs, and,
in rfs, no A/B contention on MsgBuf's lock. The cost is resilience. A packet
lost on feed A becomes a gap and a recovery instead of being filled from B.
None happened in 7 A-only runs (about 55 minutes), but that is a short and quiet
sample.

# How to reproduce these numbers

All code, configs and scripts are on kaspar-hft branch `md-latency-experiments`.
That branch is experimental and is not merged to main. The full guide is in
`../md_median_vs_tail_draft.md`, section "How to reproduce". In short:

1. **Host.** Live CME MDP3 multicast on two interfaces, with OpenOnload
   installed. Stop any other kaspr reading the same groups: runs are solo. Use a
   universe of contracts that are live on the run date
   (`kaspr/config/universe.csv`).
2. **Build.**
   - Rebuild all the libraries with `./build.sh`, then kaspr with
     `./build.sh -C kaspr/src USE_TACHBOOK=1`.
   - Rebuild everything after any header change: a stale library cost one run
     here.
   - If the link fails, set `BOOST_PATH=/usr/local/boost188`.
3. **Configs.** Copy `configs/<label>/` to `kaspr/config_<label>/` for each
   label in the tables. The pinned configs hard-code CPU ids for this host
   (EPYC 9374F, node 2 = CPUs 16-23 and 48-55); remap them for another machine.
4. **Run each config**, 8-10 minutes, interleaving configs and rotating their
   order between passes. `run_matrix3.sh` in this directory does all of this:

   ```bash
   KHPROJ=$PWD OUTDIR=$OUT kaspr/run_probe.sh --no-restart -t 480 -c ../config_<label>/md_perf.ini
   ```

   - Pinned configs: run under `taskset -c 0-15,24-47,56-63`.
   - Meanwhile: `schedsample.py <pid> $OUT/probe.out $OUT/sched.csv 10`.
   - After the window: save `onload_stackdump lots` for the process's stacks to
     `$OUT/onload.txt`, then `kill -9` the probe (known ZMQ shutdown hang;
     samples are already flushed) and copy the newest `kaspr/kaspr_log.*.log`
     to `$OUT/kaspr.log`.
   - Name each run directory `<pass>_<label>`, e.g. `p1_base` or `x2_rfs_pin_A`.
5. **Analyse.** `R=<parent of the run directories> bash paper_all.sh`
   regenerates this file, plus `stages.md`, `sched.md` and `health.md`. The
   first 120 s of each run are dropped. Percentiles are over every message of
   the run directories with that label.

