# Your Queue Is Worth 7%. Where You Put the Thread Is Worth 220%.

*Four actor mailboxes on a 32-core EPYC. One of them wins by 10×. It is not the
one you tune first, and three of the regimes do not care which one you picked.*

Here is a benchmark result from my actor framework, measured on an AMD EPYC
9374F: two actors, one message in flight, ping and wait for pong. Median round
trip **1,470 nanoseconds**.

Here is the same binary, same benchmark, same machine, run again thirty seconds
later: **7,010 nanoseconds**.

I did not change the queue. I did not change the message. I changed nothing at
all — I ran it twice and let the Linux scheduler decide where the two threads
landed. That 4.8× is the number this whole post is about, because it is bigger
than every difference between the four queues I was actually trying to measure,
and on a many-core server it will eat your benchmark before your benchmark
measures anything.

## The setup

Kaspar (my C++20 HFT actor framework) is built on actors that talk only through
messages. Every actor has a **mailbox**: many threads push, one thread — the
actor's own — drains. Multi-producer, single-consumer. The mailbox *is* the
latency between two components, so it's worth getting right.

Four implementations, all measured below:

- **BQueue** — mutex + condition variable around a ring buffer. Simple, FIFO,
  sleeps when idle. The default.
- **BQueueBatched** — same, but the consumer drains the *whole* mailbox under one
  lock instead of taking the lock per message.
- **ShardedBQueue** — the mailbox split into N lanes, each with its own lock;
  producers round-robin across them.
- **LockFreeMPSC** — a bounded lock-free ring (Vyukov). Producers claim a slot
  with a CAS; nobody holds a lock, nobody parks.

And the machine, which turns out to matter more than any of them:

```
AMD EPYC 9374F, 32 cores / 64 threads
4 NUMA nodes, 8 L3 instances  ->  4 cores per CCD, 8 cores per NUMA node
Linux 5.14 (EL9), g++ 15.2.0, -march=native, MemoryPool enabled
```

Eight separate L3 caches is the fact to hold onto. Two threads on the same CCD
share a last-level cache. Two threads on different CCDs do not, and a cache line
that bounces between them goes out to Infinity Fabric.

Everything below is **3 runs of every row, medians reported**, 500k messages per
row (300k for the grouped table), with the market-data recorder shut down —
every raw file records `# kaspr running: 0` and a load average of 1.4–1.7. I
checked every ranking claim for stability across the three runs, and I say so
below when it isn't stable, because on this box that happens a lot.

## Regime 1 — one message at a time: the queue is not the variable

Lowest-latency case: one message outstanding, ping then wait for pong, no
queueing behind anyone. Unpinned, `p50` in nanoseconds, all three runs shown
because the medians alone would be a lie:

| mailbox | run 1 | run 2 | run 3 | median |
|---|---|---|---|---|
| BQueue | 6700 | 3610 | 3550 | 3610 |
| BQueueBatched | 6780 | 7160 | 6660 | 6780 |
| ShardedBQueue | 3431 | 7280 | 7010 | 7010 |
| LockFreeMPSC | 7340 | 3640 | 6860 | 6860 |

The winner changes every run. Individual rows swing 2.1×. And look at the actual
values: every one of the twelve is either about 3,500 or about 7,000. It's
bimodal. There is no "the answer is 3,610" here — there are two answers and a
coin flip.

So stop measuring the queue and measure the coin. Same benchmark, pinned with
`taskset`, three reps per placement:

| placement | BQueue | Batched | Sharded | LockFree | spread |
|---|---|---|---|---|---|
| **one core** (`-c 2`) | 1680 | 1700 | **1470** | 1630 | 1.07× |
| SMT pair (`-c 2,34`) | **3240** | 3260 | 3340 | 3230 | 1.03× |
| same CCD (`-c 2,3`) | 3540 | 3530 | 3440 | **3380** | 1.07× |
| cross-CCD (`-c 2,6`) | 6720 | 6830 | 1510 | 1700 | bimodal |
| cross-NUMA (`-c 2,26`) | 1470 | 1480 | 1520 | 1710 | bimodal |

Read the columns and you learn almost nothing: within any fixed placement the
four queues are **3–7% apart**. Read the rows and you learn everything:
placement moves the same number from 1,470 to 3,240 to ~7,000 — **2.2× between
the controlled cases, 4.8× if you count the unpinned tail.** The confound is an
order of magnitude larger than the effect.

**And the fastest configuration is both threads on one core.** That inverts the
usual intuition, and it shouldn't. A window=1 ping-pong has *zero parallelism* —
the two threads strictly alternate, one is always blocked — so a second core
buys you nothing to overlap. What it costs you is an inter-processor interrupt
to wake the peer plus a cache line dragged across the interconnect. A same-CPU
context switch is cheaper than that. Two cores are worse than one when there is
no concurrency to spend them on.

The two-CPU cpuset rows are still bimodal because a two-CPU cpuset still lets
the scheduler choose: it can co-locate both threads on one of them or not, and
it does both. (I did not instrument this. `wake_affine` is the obvious suspect
and I'm calling it a suspect, not a cause.)

The ranking flips with placement too. ShardedBQueue is stably *best* on one core
— 1,470 vs BQueue's 1,680, in all three reps — and stably *worst* on an SMT
pair. Same binary, same benchmark, opposite conclusion.

**So on this box, "which mailbox is fastest for a one-message round trip" is not
a measurable question.** It is dominated by where two threads land. If you have
a request/response path that matters, the tuning knob is `taskset`, and the
mailbox type is a rounding error.

## Same thread, no wakeup: now the queue matters again

Kaspar can also put two actors in one **Group**: they share a thread and a
single mailbox, so that mailbox is only ever touched by one thread — it pushes
when a handler sends, then pops. No contention, and crucially no cross-core
wakeup, because the consumer never sleeps; there's always a next message
waiting. Same window=1 round trip, `p50` ns, dedicated 300k run:

| mailbox | p50 | p99 |
|---|---|---|
| **LockFreeMPSC** | **70** | 90 |
| BQueue | 90 | 120 |
| BQueueBatched | 90 | 129 |
| ShardedBQueue | 190 | 230 |

Stable in all three runs, and 21× faster than the *best* cross-thread placement
(1,470 on one core) — 50× faster than a typical unpinned one. LockFreeMPSC wins
because on one thread its push is an uncontended CAS
and its pop is a CAS — no mutex, no condition variable. BQueue pays a
`lock`/`unlock` **and** a `notify_one` on every push even though nobody is
waiting, and that wakeup machinery is pure overhead when the consumer never
sleeps. ShardedBQueue is worst: eight lanes and an atomic cursor managing
contention that does not exist.

This is the one regime where the mailbox choice is both large and reproducible,
and it is exactly the regime with **no placement question** — one thread, so
there is nothing for the scheduler to get wrong. That is not a coincidence.

For the floor: `fast_send`, which skips the queue entirely and runs the handler
inline on the caller's thread, amortizes at **56 ns/msg**, against **7,209
ns/msg** for a cross-thread `send`. That ratio is 129×, but treat it as an order
of magnitude and not a number — you just watched the denominator move 2.2× with
`taskset`. (Also: `steady_clock` on this box quantizes to ~10 ns, so the `p50`
of 30 for `fast_send` is resolution-bound. The amortized figures are the real
ones.)

## Regime 2 — a burst: throughput, and no convergence

Now keep **16 messages in flight** instead of one. The mailbox stays backlogged,
so the consumer never stalls waiting for a round trip. One producer thread.
`amort` is nanoseconds per message:

| mailbox | p50 latency | amort (ns/msg) |
|---|---|---|
| BQueue | 8480 | 605.8 |
| BQueueBatched | 8160 | **584.9** |
| ShardedBQueue | 15550 | 983.1 |
| LockFreeMPSC | 10280 | 672.0 |

Little's Law is doing its usual work: 16 messages overlap, so one *finishes*
every ~585 ns even though each one *takes* ~8 µs end to end. Pipelining trades
latency for throughput, and the ratio is roughly your window.

The interesting row is ShardedBQueue: **68% worse than BQueueBatched, stably
worst in all three runs**, with one producer. Sharding is a contention
structure. With a single producer there is no contention to spread, so all it
does is scatter consecutive messages across eight lanes and make the consumer
chase eight cache lines instead of one. It costs you exactly when it can't help
you.

BQueue vs BQueueBatched is a genuine tie — they trade places between runs and
sit within 4%.

## Regime 3 — fan-in: this is what you actually buy

**32 producer threads into one consumer.** On this box that means producers
spread across all 8 CCDs and all 4 NUMA nodes, all pushing into one actor. This
is the regime the sharded and lock-free queues exist for. `p50`/`p99`/`p99.9`
are the cost of a `send()` — allocate a message and push it — under contention;
`amort` is end-to-end throughput:

| mailbox | push p50 | push p99 | p99.9 | amort (ns/msg) |
|---|---|---|---|---|
| BQueue | 12180 | 69850 | 102830 | 493.9 |
| BQueueBatched | 14000 | 72441 | 105881 | 581.8 |
| **ShardedBQueue** | **2210** | **6600** | **11980** | **136.7** |
| LockFreeMPSC | 2991 | 16480 | 32670 | 175.0 |

**ShardedBQueue sweeps every column**, and every ranking here was stable across
all three runs. It is 5.5× better than BQueue on the median, **10.6× on p99**,
8.6× on p99.9, and 3.6× on throughput. With one lane per producer, 32 threads
almost never touch the same lock; with one shared mutex, 32 threads collide on
every push and the losers park in the kernel. A p99 of **69 microseconds** on a
message send is not a latency, it's an outage.

LockFreeMPSC is the clear second: no producer ever holds a lock or parks, so it
lands a 16 µs p99 against BQueue's 70. But it is also the **least stable row I
measured**: its p99.9 moved 1.8× across the three runs, 30,540 → 55,270. It does
the same thing in the grouped table — best p50 of the four at 70 ns, and the
*worst* `max` of the four, 16 µs and 14 µs in two of three reps against BQueue's
2.4–9 µs. Park-free is not the same as jitter-free.

Now the part I did not expect. Look at BQueue's minimum in the raw data: **20
nanoseconds.** The uncontended push is still as cheap as it ever was. It just
never happens anymore. With a handful of producers the lock is usually free, so
that 20 ns lands on your *median* and the queue looks fantastic. With 32
producers the lock is essentially never free, so the same 20 ns lands on your
*minimum*, where nobody looks, and the median is 12 microseconds.

"Medians lie" is the standard advice and it's true. What's less standard:
**which statistic does the lying is a function of your core count.** The
seductive number doesn't go away when you scale up, it relocates. Port your
benchmark to a bigger box and it will move again.

## The point

Line up all four regimes on one machine:

| regime | winner | margin | is it real? |
|---|---|---|---|
| cross-thread, 1 msg | — | queues within 3–7% | **no** — placement swamps it 2.2× |
| same-thread (grouped) | **LockFreeMPSC** | 1.3× over BQueue | yes, stable |
| burst, 1 producer | BQueueBatched | 1.68× over Sharded | yes — and Sharded *hurts* |
| fan-in p50/p99/p99.9/throughput | **ShardedBQueue** | up to 10.6× | yes, stable 3/3 |

Two conclusions, and they point in different directions.

**One: for most of your actors, the mailbox type is not the lever.** In three of
the four regimes above, the spread between the best and worst queue is smaller
than what you get from a `taskset` line or from putting two actors on the same
thread. If an actor is poked by one timer, leave it on the default and go
optimize something else.

**Two: for the actor everything writes into, it's a 10× lever.** The order book
that a dozen feed handlers push into is the fan-in row, and the fan-in row is
not close. That is also the actor whose p99 you'll be judged on, because
fan-in peaks and market events are the same event — the open, the print, the
spike. BQueue's 24-hour-a-day 12 µs median under that load is bad; its 70 µs p99
during the one minute you care about is the whole problem.

So Kaspar makes it a per-actor choice. The mailbox is a `std::variant` of the
four concrete queue types, held by value; the actor picks one in its constructor,
before its thread starts:

```cpp
class BookBuilder : public actors::Actor {
public:
  BookBuilder() {
    set_mailbox(MailboxKind::ShardedBQueue, /*lanes=*/32);  // many feeds write here
    MESSAGE_HANDLER(MDUpdate, on_update);
  }
};
```

Because the variant holds the concrete type rather than a `Queue*` base pointer,
`std::visit` hands the hot loop a real `BQueue&` / `ShardedBQueue&` — `push` and
the drain loop are devirtualized and inlined, so choosing costs you nothing per
message. Lane count is worth a thought: one lane per expected producer is what
made the fan-in row win, and eight lanes with one producer is what made the
burst row lose.

## How to reproduce this

One self-contained program, `actors/cpp/perf/bench_pingpong.cpp` in the Kaspar
tree (branch `feat/variant-mailbox`). It links only the actor library — no
market-data or exchange dependencies.

```bash
export KSPRPROJ=$(pwd)
make -C actors/cpp opt                 # build libactors.a
make -C actors/cpp test                # 32 queue unit tests -- run these first
cd actors/cpp/perf && make

./bench_pingpong 500000 20000 all      # solo, burst16, fanin, transport
./bench_pingpong 300000 10000 grouped  # the single-thread table

# and the part that actually mattered:
taskset -c 2    ./bench_pingpong 500000 20000 solo   # both threads, one core
taskset -c 2,34 ./bench_pingpong 500000 20000 solo   # SMT pair
taskset -c 2,3  ./bench_pingpong 500000 20000 solo   # same CCD
taskset -c 2,6  ./bench_pingpong 500000 20000 solo   # different CCD
```

`fanin` spawns `min(32, hardware_concurrency − 2)` producers — on a 64-thread box
that's 32, not 62. Get your topology from `lscpu` and the CCD grouping from
`lscpu -e` or `/sys/devices/system/cpu/cpu*/cache/index3/shared_cpu_list`; the
CPU numbers above are specific to this machine and copying them blindly will
give you a different experiment.

Run everything three times before you believe any ordering. That is not
boilerplate advice — it is the only reason I caught that Regime 1 was noise.

**What I did not control.** `nohz_full=` and `rcu_nocbs=` are empty in
`/proc/cmdline` and `chrt` is denied (`RLIMIT_RTPRIO=0`), so the `max` column is
a scheduler artifact throughout and I've only quoted it as evidence of
instability. The fan-in and burst tables are unpinned — pinning 32 producers
isn't obviously meaningful, but NUMA-aware placement of the *consumer* is
untested, and everything above suggests it would matter. One machine, one
compiler, one build.

## The number to remember

**1,470 and 7,010.** Same queue, same benchmark, same machine, same afternoon.
The only difference is which cores the scheduler picked.

Before you benchmark four queues, verify that your harness can distinguish them
from the scheduler. On a 32-core, 8-CCD server, mine could not — in three of
four regimes the thing I was measuring was smaller than the thing I wasn't
controlling. The one regime where the queue genuinely dominated, it dominated by
10×.

Find that regime. Ignore the rest.

---

*Measured on an AMD EPYC 9374F (32 cores / 64 threads, 4 NUMA nodes, 8 CCDs),
Linux 5.14, g++ 15.2.0, branch `feat/variant-mailbox` @ `d15b8a4`, market-data
recorder stopped. 3 runs of every row, medians reported; raw output and the
analysis scripts are in `actors/cpp/perf/results/linux_quiet/`.*
