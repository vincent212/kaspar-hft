# There Is No Fastest Queue

*Picking an actor mailbox for low-latency trading: four queues, three load
regimes, and why the ranking inverts.*

Here is a number from my actor framework's mailbox, measured this morning on an
Apple M3: sending a message — allocate it, push it onto the queue — costs
**42 nanoseconds** at the median. Ship it.

Here is the same line at the 99th percentile, under load: **24,083 nanoseconds**.
A 570× jump. Same queue, same machine, same line of code.

That gap is the whole story of why a high-frequency trading system has not one
message queue but four — and why picking the wrong one is a decision your median
will happily hide from you until the one afternoon it doesn't.

## The setup

Kaspar (my C++20 HFT actor framework) is built on actors that talk only through
messages. Every actor has a **mailbox**: many threads push, one thread (the
actor's own) drains. Multi-producer, single-consumer — MPSC. The mailbox *is*
the latency between two components, so it is worth getting right.

There are four mailbox implementations, all measured below, plus `fast_send`
(the inline path that skips the queue entirely) as the floor:

- **BQueue** — a mutex + condition variable around a ring buffer. Simple, FIFO,
  sleeps when idle. The default.
- **BQueueBatched** — same, but the consumer drains the *whole* mailbox in one
  lock instead of one lock per message.
- **ShardedBQueue** — the mailbox split into N lanes, each with its own lock;
  producers round-robin across them.
- **LockFreeMPSC** — a bounded lock-free ring (Vyukov). Producers claim a slot
  with a CAS; nobody holds a lock, nobody sleeps on the push path.

Everything below is one M3, 500k messages/row, thread-to-thread. Your Linux
server will differ — that's a later post — but the *shape* holds.

## Regime 1 — one message at a time: simplicity wins

First, the lowest-latency case: one message outstanding, ping then wait for pong.
No queueing behind anyone. This is `p50` latency in nanoseconds:

| mailbox | p50 | p99 |
|---|---|---|
| **BQueue** | **2250** | 6291 |
| BQueueBatched | 2291 | 7291 |
| ShardedBQueue | 3792 | 8834 |
| LockFreeMPSC | 3541 | 7667 |

The plain mutex queue **wins**, and the fancy ones *lose by ~1.5×*. This surprises
people. The sharded queue's atomic round-robin cursor, the lock-free queue's CAS
loop and memory fences — that machinery exists to survive contention, and when
there is no contention it's just overhead over a single uncontended
`lock`/`unlock`. An uncontended mutex on modern hardware is a handful of
nanoseconds. You cannot beat it by working harder.

For reference, the same round trip with **no queue at all** (`fast_send`, handler
runs inline on the caller's thread) is **41 ns** — 55× faster than the cheapest
queued path. If two actors can run on the same thread, that is the real win;
everything else is paying for a thread hop.

## Same thread, no wakeup: the ranking flips

Kaspar can also put two actors in one **Group** — they share a thread and a
single mailbox, so that mailbox is only ever touched by one thread (it pushes
when a handler sends, then pops). No contention, and — crucially — no cross-core
*wakeup*, because the consumer never sleeps; there is always a next message
waiting. Same window=1 round trip, `p50` ns:

| mailbox | grouped (1 thread) | solo (cross-thread) |
|---|---|---|
| **LockFreeMPSC** | **84** | 3541 |
| BQueue | 125 | 2250 |
| BQueueBatched | 125 | 2291 |
| ShardedBQueue | ~200 | 3792 |

Now **LockFreeMPSC wins** — the opposite of the cross-thread case. On one thread
its push is an uncontended CAS and its pop is a CAS: no mutex to take, no
condition variable to signal. `BQueue` pays a `lock`/`unlock` *and* a
`notify_one` on every push even though nobody is waiting, and that wakeup
machinery is pure overhead when the consumer never sleeps. `ShardedBQueue` is
worst again — eight lanes and an atomic cursor managing contention that isn't
there.

So the ranking doesn't just depend on *contention* — it depends on whether
there's a **thread hop** at all. Cross-thread with one message in flight, the
cost is the wakeup and `BQueue`'s condvar wakes fastest. Same-thread, the cost is
the raw push/pop and the lock-free CAS wins. Two "low contention" cases, two
different winners.

## Regime 2 — a burst: throughput converges

Now keep **16 messages in flight** instead of one. The mailbox stays backlogged,
so the consumer never stalls waiting for a round trip. `amort` is nanoseconds per
message (throughput):

| mailbox | p50 latency | amort (ns/msg) |
|---|---|---|
| BQueue | 3458 | 229.6 |
| BQueueBatched | 3375 | **226.0** |
| ShardedBQueue | 3583 | 246.4 |
| LockFreeMPSC | 3125 | **223.5** |

Two things. First, throughput (~225 ns/msg) is ~12× better than the single-message
amortized cost (2744 ns) — because 16 messages overlap, one *finishes* every
225 ns even though each *takes* ~3.4 µs end to end. Little's Law:
`throughput ≈ latency ÷ concurrency`. Pipelining trades latency for throughput.

Second, the queues are now within a few percent of each other. Batching shaves a
little (one lock for N messages), lock-free shaves a little (no wakeup). But this
is one producer; the mailbox type barely matters when only one thread is pushing.

To see the queues actually separate, you need a crowd.

## Regime 3 — fan-in: the tail is the product

Six producer threads, all hammering **one** consumer — the load sharded and
lock-free queues are built for. Here `p50`/`p99`/`p99.9` are the cost of a
**`send()`** (allocate a message and push it) under contention, and `amort` is
end-to-end throughput:

| mailbox | push p50 | push **p99** | p99.9 | amort (ns/msg) |
|---|---|---|---|---|
| BQueue | 42 | **24083** | 49917 | 215 |
| BQueueBatched | 42 | **24500** | 50458 | 215 |
| ShardedBQueue | 209 | **4000** | 33625 | **111** |
| LockFreeMPSC | 625 | **5875** | **16375** | 206 |

There it is. Plain `BQueue` has the *best median* — 42 ns, because most pushes
grab the lock uncontended — and a **p99 of 24 microseconds**, because when six
threads collide on one mutex, the losers park in the kernel. The median tells you
the lock is usually free. The tail tells you what happens when it isn't, and in
trading the tail is when everyone is trying to do something at once — the open,
the print, the spike. That is exactly when you cannot afford 24 µs.

**ShardedBQueue** is the standout: a higher 209 ns median (that atomic cursor
again) buys a **6× lower p99** — 4 µs vs 24 — *and* **~2× the throughput**
(111 vs 215 ns/msg). With one lane per producer, the six threads almost never
touch the same lock. **LockFreeMPSC** pays a higher 625 ns median for its
CAS-and-fence push and lands a close 5.9 µs p99, but it degrades the most
gracefully *deep* in the tail — a 16 µs p99.9 vs ShardedBQueue's 34 — because no
producer ever holds a lock or parks. Both replace `BQueue`'s 24 µs contention
cliff with a 4–6 µs slope.

## The point: there is no best queue

Line the three regimes up and the same four queues invert their ranking:

| regime | winner | why |
|---|---|---|
| cross-thread, 1 msg (solo) | **BQueue** | wakeup-bound; condvar wakes fastest |
| same-thread (grouped) | **LockFreeMPSC** | no mutex/condvar, no wakeup |
| burst throughput | ~tie (LockFree/Batched) | one producer, queue barely matters |
| fan-in p99 + throughput | **ShardedBQueue** | one lane per producer, no shared lock |
| fan-in deep tail (p99.9) | **LockFreeMPSC** | park-free push, never blocks |

A single "fastest queue" number is a lie the median tells. The right queue
depends on how many threads push, whether you care about p50 or p99, and whether
you're optimizing a round trip or a firehose. An actor that a dozen feeds write
into wants ShardedBQueue; an actor that one timer pokes wants BQueue and would be
*slower* with anything cleverer.

So Kaspar makes it a per-actor choice. The mailbox is a `std::variant` of the
four concrete queue types, held by value; the actor picks one in its constructor
(before its thread starts) with a single call — no call means the default
`BQueue`:

```cpp
class BookBuilder : public actors::Actor {
public:
  BookBuilder() {
    set_mailbox(MailboxKind::ShardedBQueue, /*lanes=*/16);  // many feeds write here
    MESSAGE_HANDLER(MDUpdate, on_update);
  }
  // ...
};
```

Because the variant holds the concrete type (not a `Queue*` base pointer),
`std::visit` hands the hot loop a real `BQueue&` / `LockFreeMPSC&` — so `push`
and the drain loop are **devirtualized and inlined**, no per-message vtable
indirection. You get the freedom to choose without paying an indirect call on
every message. (That refactor is a story of its own; the short version is that a
closed set of types is a `variant`, not an inheritance hierarchy.)

## How to reproduce these numbers

Every number here comes from **one** self-contained program —
`actors/cpp/perf/bench_pingpong.cpp` in the Kaspar tree (branch
`feat/variant-mailbox`). It links only the actor library; no market-data or
exchange dependencies. On an Apple M3:

```bash
export KSPRPROJ=$(pwd)
make -C actors/cpp opt                # build libactors.a
make -C actors/cpp test               # optional: 32 queue unit tests
cd actors/cpp/perf && make            # build the benchmark

# solo, burst16, fanin, transport tables — 500k msgs/row, 20k warmup:
./bench_pingpong 500000 20000 all

# the grouped table (single thread, no contention):
./bench_pingpong 300000 10000 grouped
```

`bench_pingpong [N] [warmup] [section]`, where `section` is
`solo | batch | grouped | fanin | transport | all`. `solo`/`batch` are
thread-to-thread window 1 / 16; `fanin` spins up `min(32, hardware_concurrency − 2)`
producer threads into one consumer; `grouped` puts both actors on one thread. The
queue each actor uses is the one `set_mailbox(...)` line above. That's the whole
harness — run it on your own box and the ranking will shift with your core count.

## The number to remember

`BQueue`: **42 ns median, 24,083 ns p99.** Same queue, same machine, one line
apart. Medians lie. Tails kill. Benchmark the percentile you'll actually be
judged on — and in trading, that's never the median.

---

*Measured on an Apple M3, single run: the solo / burst / fan-in / transport
tables at 500k messages/row, the grouped table at 300k. The Linux x86-64 numbers
— where more cores mean more producers and the sharded/lock-free advantage
should widen — are coming in a follow-up.*

> **Follow-up is in — see `queue_bench_linux_results.md`.** On a 32-core EPYC
> (32 producers instead of six, 3 runs, recorder stopped) the fan-in prediction
> holds and then some: ShardedBQueue sweeps p50, p99, p99.9 *and* throughput, by
> up to 10.6×. Three things above do **not** survive the port, and they are
> flagged there: the closing "42 ns median" (BQueue has the *worst* median of the
> four at 32 producers), the Regime-1 ranking (thread placement moves that number
> 2.2× and flips the winner, so it is not measurable unpinned), and the
> LockFreeMPSC deep-tail win (inverted — ShardedBQueue takes p99.9). The thesis
> survives intact; the specific numbers are M3 numbers.
>
> The EPYC has its own post — `queue_types_article_epyc.md`, *"Your Queue Is
> Worth 7%. Where You Put the Thread Is Worth 220%."* — because the headline
> there is not a queue at all: on a 32-core, 8-CCD box, thread placement moves
> the Regime-1 number more than the queue choice does, and three of the four
> regimes cannot tell the four mailboxes apart.
