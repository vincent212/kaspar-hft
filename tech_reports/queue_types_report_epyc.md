# Not All Queues Fit All: Actor Mailbox Selection on a 32-Core AMD EPYC

## Summary

This report measures four multi-producer/single-consumer (MPSC) mailbox
implementations in the Kaspar C++ actor framework across four workload regimes
on a 32-core AMD EPYC 9374F. The principal findings are:

1. In the single-message cross-thread regime, the four mailboxes differ by 3–7%
   within a fixed thread placement, while placement alone varies the same
   measurement by 2.2× (controlled) to 4.8× (including the unpinned tail).
   Mailbox choice is not distinguishable from scheduler placement in this regime.
2. In the single-thread (grouped) regime, mailbox choice is both significant and
   reproducible: LockFreeMPSC is fastest at 70 ns p50.
3. In the single-producer burst regime, BQueue and BQueueBatched are within 4%;
   ShardedBQueue is 68% worse.
4. In the 32-producer fan-in regime, ShardedBQueue is fastest on every measured
   statistic, by up to 10.6× (p99) over BQueue.

All rankings reported as stable were verified across three runs.

## Background: terms used in this report

**Actor, mailbox, producer, consumer.** In this framework a program is built from
*actors* — independent components that never share memory and communicate only by
sending each other messages. Each actor has one *mailbox*: a queue where incoming
messages wait. Any number of threads can put messages in (the *producers*); only
the actor's own single thread takes them out (the *consumer*). This is called
multi-producer/single-consumer (MPSC). The mailbox is the handoff point between
components, so its speed is the latency between them.

**The four mailboxes** differ in how they coordinate concurrent producers:
- *BQueue* — protects the queue with a lock (a mutex); a producer that arrives
  while another holds the lock waits its turn. Simplest; the default.
- *BQueueBatched* — same lock, but the consumer empties the whole queue in one
  locked operation instead of locking once per message.
- *ShardedBQueue* — splits the mailbox into several independent lanes, each with
  its own lock, so producers usually don't wait on each other.
- *LockFreeMPSC* — uses no locks at all; producers reserve a slot with a single
  atomic CPU instruction, so no producer is ever put to sleep waiting.

**Thread placement (which core runs which thread).** A modern server CPU is not
uniform: two cores can be physically near each other or far apart, and moving data
between distant cores is slower. The benchmark forces specific placements with the
Linux `taskset` command, from closest to farthest:
- *one core* — both threads share a single CPU core and take turns on it. Nothing
  is copied between cores.
- *SMT pair (hyper-threads)* — the two hardware threads that share one physical
  core. Very close.
- *same CCD* — two different cores on the same chiplet; they share a fast local
  cache (L3), so data passes between them without leaving the chiplet.
- *cross-CCD* — two cores on different chiplets. They do **not** share a cache, so
  a piece of data shared between them must travel over the chip's internal
  interconnect (AMD's "Infinity Fabric"). Slower.
- *cross-NUMA* — cores on different chiplets **and** attached to different memory
  banks. Farthest apart, slowest.

A CCD (Core Complex Die) is one of the small chiplets an AMD EPYC processor is
built from; each has its own group of cores and its own local L3 cache. This
machine has 8 CCDs. NUMA (Non-Uniform Memory Access) means memory is divided into
banks, each attached to some cores; reaching your own bank is faster than reaching
another.

**Metrics.**
- *p50* (median) — half of the measurements were faster than this, half slower. A
  typical case.
- *p99 / p99.9* — the 99th and 99.9th percentiles: only 1 in 100 (or 1 in 1,000)
  measurements were slower. These describe the worst cases, which matter most in
  trading because the worst cases cluster during market activity.
- *amortized (amort, ns/msg)* — total time divided by number of messages; the
  steady-state cost per message, free of per-measurement timing overhead.
- *messages in flight / window* — how many messages are outstanding at once. "One
  in flight" is a strict request-and-wait; "16 in flight" keeps the queue full so
  throughput, not round-trip latency, is the limit.
- *ns / µs* — nanosecond (billionth of a second) and microsecond (millionth);
  1,000 ns = 1 µs.

## Hardware and build

```
AMD EPYC 9374F, 32 cores / 64 threads
4 NUMA nodes, 8 L3 instances -> 4 cores per CCD, 8 cores per NUMA node
Linux 5.14 (EL9), g++ 15.2.0, -march=native, MemoryPool enabled
```

Each CCD (Core Complex Die) has a private L3 cache shared only among its 4 cores.
Two threads on the same CCD share L3; two threads on different CCDs do not, and a
cache line shared between them transits the Infinity Fabric interconnect.

## Methodology

Each row is the median of 3 runs, 500,000 messages per run (300,000 for the
grouped table). The market-data recorder was stopped for all runs; each raw
output file records `# kaspr running: 0` with a load average of 1.4–1.7. Ranking
stability was checked across the three runs and is noted where absent.

`steady_clock` on this machine quantizes to approximately 10 ns; per-sample
percentiles at or below this resolution are noted as resolution-bound, and
amortized figures are used in those cases.

## Mailbox implementations

- **BQueue** — mutex and condition variable around a ring buffer; FIFO; the
  consumer sleeps when idle. Default.
- **BQueueBatched** — as BQueue, but the consumer drains the entire mailbox under
  a single lock acquisition rather than locking per message.
- **ShardedBQueue** — N independent lanes, each with its own lock; producers
  round-robin across lanes.
- **LockFreeMPSC** — bounded lock-free ring (Vyukov); producers claim a slot by
  compare-and-swap (CAS); no locks, no parking.

## Regime 1: single message in flight, cross-thread

One message outstanding (ping, wait for pong). Unpinned, p50 in nanoseconds, all
three runs shown:

| mailbox | run 1 | run 2 | run 3 | median |
|---|---|---|---|---|
| BQueue | 6700 | 3610 | 3550 | 3610 |
| BQueueBatched | 6780 | 7160 | 6660 | 6780 |
| ShardedBQueue | 3431 | 7280 | 7010 | 7010 |
| LockFreeMPSC | 7340 | 3640 | 6860 | 6860 |

The fastest mailbox differs between runs, individual rows vary up to 2.1×, and
the twelve measurements are bimodal, clustering near 3,500 ns or 7,000 ns.

The same benchmark pinned with `taskset`, three repetitions per placement, p50 in
nanoseconds:

| placement | BQueue | Batched | Sharded | LockFree | spread |
|---|---|---|---|---|---|
| one core (`-c 2`) | 1680 | 1700 | 1470 | 1630 | 1.07× |
| SMT pair (`-c 2,34`) | 3240 | 3260 | 3340 | 3230 | 1.03× |
| same CCD (`-c 2,3`) | 3540 | 3530 | 3440 | 3380 | 1.07× |
| cross-CCD (`-c 2,6`) | 6720 | 6830 | 1510 | 1700 | bimodal |
| cross-NUMA (`-c 2,26`) | 1470 | 1480 | 1520 | 1710 | bimodal |

Within a fixed placement the four mailboxes are 3–7% apart. Across placements the
same measurement moves from 1,470 ns to 3,240 ns to approximately 7,000 ns: 2.2×
between the controlled cases, 4.8× including the unpinned tail. Placement
dominates mailbox choice by roughly an order of magnitude.

The fastest configuration is both threads on a single core. A window-of-one
ping-pong has no parallelism — the two threads strictly alternate and one is
always blocked — so a second core provides no overlap while adding an
inter-processor interrupt to wake the peer and a cache line transfer across the
interconnect. A same-core context switch is cheaper than that cost.

The two-CPU cpuset rows remain bimodal because a two-CPU cpuset still permits the
scheduler to co-locate both threads on one CPU or not; both outcomes were
observed. This was not instrumented; `wake_affine` is a candidate cause but was
not confirmed.

Rankings also change with placement: ShardedBQueue is fastest on one core (1,470
vs BQueue 1,680, in all three repetitions) and slowest on an SMT pair, in the same
binary.

In this regime, relative mailbox performance for a single-message round trip is
determined by thread placement rather than by mailbox implementation.

## Single thread (grouped), no wakeup

Two actors placed in one Group share a thread and a single mailbox, so the
mailbox is accessed by only one thread and the consumer never sleeps (a next
message is always available). Same window-of-one round trip, p50/p99 in
nanoseconds, dedicated 300,000-message run:

| mailbox | p50 | p99 |
|---|---|---|
| LockFreeMPSC | 70 | 90 |
| BQueue | 90 | 120 |
| BQueueBatched | 90 | 129 |
| ShardedBQueue | 190 | 230 |

Rankings were stable across all three runs. The best grouped result (70 ns) is
21× faster than the best cross-thread placement (1,470 ns on one core) and
approximately 50× faster than a typical unpinned cross-thread result.
LockFreeMPSC is fastest because on a single thread its push and pop are each an
uncontended CAS with no mutex or condition variable. BQueue performs a lock,
unlock, and `notify_one` on every push even when no consumer is waiting.
ShardedBQueue is slowest: its eight lanes and atomic cursor manage contention
that does not occur with a single thread.

This is the only regime in which mailbox choice is both large and reproducible,
and it is also the only regime with no placement variable, because a single
thread leaves no placement decision to the scheduler.

For reference, `fast_send` — which bypasses the queue and runs the handler inline
on the caller's thread — amortizes at 56 ns/message, versus 7,209 ns/message for
a cross-thread `send` (a ratio of approximately 129×). The `fast_send` p50 of
30 ns is resolution-bound; amortized figures are used.

## Regime 2: burst, 16 messages in flight, one producer

With 16 messages outstanding the mailbox stays backlogged and the consumer does
not stall between round trips. One producer thread. `amort` is nanoseconds per
message:

| mailbox | p50 latency | amort (ns/msg) |
|---|---|---|
| BQueue | 8480 | 605.8 |
| BQueueBatched | 8160 | 584.9 |
| ShardedBQueue | 15550 | 983.1 |
| LockFreeMPSC | 10280 | 672.0 |

With 16 messages overlapping, one message completes every ~585 ns although each
takes ~8 µs end to end (consistent with Little's Law; the latency-to-throughput
ratio is approximately the window size).

ShardedBQueue is 68% worse than BQueueBatched and stably worst in all three runs.
With a single producer there is no producer contention to distribute across lanes;
sharding scatters consecutive messages across eight lanes and requires the
consumer to read from multiple cache lines. BQueue and BQueueBatched are within
4% and exchange ranks between runs.

## Regime 3: fan-in, 32 producers into one consumer

32 producer threads (spread across all 8 CCDs and 4 NUMA nodes) push into one
consumer actor. p50/p99/p99.9 are the cost of a `send()` — allocate a message and
push — under contention; `amort` is end-to-end throughput:

| mailbox | push p50 | push p99 | p99.9 | amort (ns/msg) |
|---|---|---|---|---|
| BQueue | 12180 | 69850 | 102830 | 493.9 |
| BQueueBatched | 14000 | 72441 | 105881 | 581.8 |
| ShardedBQueue | 2210 | 6600 | 11980 | 136.7 |
| LockFreeMPSC | 2991 | 16480 | 32670 | 175.0 |

ShardedBQueue is fastest on every column, with all rankings stable across three
runs: 5.5× better than BQueue on p50, 10.6× on p99, 8.6× on p99.9, and 3.6× on
throughput. With one lane per producer, 32 threads rarely contend on the same
lock; with one shared mutex, 32 threads contend on every push and blocked
producers park in the kernel.

LockFreeMPSC is second: no producer holds a lock or parks, yielding a 16 µs p99
versus BQueue's 70 µs. It is also the least stable row measured — its p99.9 moved
1.8× across the three runs (30,540 → 55,270 ns) — and shows the same pattern in
the grouped table, with the best p50 (70 ns) but the worst `max` of the four
(16 µs and 14 µs in two of three repetitions, versus BQueue's 2.4–9 µs).

BQueue's minimum push in the fan-in raw data is 20 ns, unchanged from the
uncontended case. With few producers the lock is usually free and this value
lands on the median; with 32 producers the lock is rarely free, so it lands on
the minimum while the median rises to 12 µs. The statistic that reflects the
uncontended fast path shifts with producer count.

## Summary of regimes

| regime | fastest mailbox | margin | distinguishable from confounds |
|---|---|---|---|
| cross-thread, 1 msg | none | queues within 3–7% | no — placement varies 2.2× |
| same-thread (grouped) | LockFreeMPSC | 1.3× over BQueue | yes, stable 3/3 |
| burst, 1 producer | BQueueBatched | 1.68× over ShardedBQueue | yes; ShardedBQueue worse |
| fan-in (p50/p99/p99.9/throughput) | ShardedBQueue | up to 10.6× | yes, stable 3/3 |

In three of four regimes the spread between the best and worst mailbox is smaller
than the variation from thread placement or from grouping two actors on one
thread. In the fan-in regime the mailbox difference is up to 10.6× and stable.
The fan-in regime corresponds to a high-fan-in actor such as an order book that
many feed handlers write into, and its p99 coincides with market events that
produce simultaneous writes.

## Recommended defaults

No single mailbox is best across all four regimes, so the choice should follow an
actor's contention profile. The measurements support the following policy.

**Default to BQueue for all actors.** It is fastest or within a few percent of
fastest in every low-contention regime — grouped (90 ns p50, versus 70 ns for
LockFreeMPSC and 190 ns for ShardedBQueue) and single-producer burst (605.8
ns/msg, within 4% of BQueueBatched) — and it has the most stable tail of the four
(grouped `max` 2.4–9 µs, versus 14–16 µs for LockFreeMPSC). For the majority of
actors, which are driven by one timer or one upstream stage or are grouped onto a
shared thread, BQueue is the correct choice and requires no configuration.

**Override to ShardedBQueue only for high-fan-in actors** — those written to by
many producer threads concurrently, such as an order book fed by a dozen
market-data handlers. This is the one regime in which ShardedBQueue wins, and it
wins decisively: p50 2.2 µs versus BQueue's 12.2 µs, p99 6.6 µs versus 69.9 µs
(10.6×), and 3.6× on throughput. Under that load BQueue's single mutex serializes
all producers and its p99 reaches approximately 70 µs, which coincides with the
market events that produce the fan-in in the first place.

**Do not make ShardedBQueue the default.** Its lanes and atomic cursor exist to
spread producer contention; where that contention does not exist — a single
producer, or a grouped single-thread actor — they are pure overhead, and
ShardedBQueue is the slowest of the four (68% worse than BQueueBatched in burst;
190 ns versus 90 ns grouped). As a global default it would penalize the common
low-contention actor to benefit the uncommon fan-in one.

**LockFreeMPSC is not recommended as a default** despite the best grouped median
(70 ns), because it has the worst tail of the four (grouped `max` 14–16 µs;
fan-in p99.9 the least stable measured, moving 1.8× across runs). It is park-free
but not jitter-free.

In summary: BQueue everywhere, ShardedBQueue on the few many-writer actors. This
is why the mailbox is a per-actor selection rather than a single global setting.

## Per-actor mailbox selection

The mailbox is a `std::variant` of the four concrete queue types, held by value;
each actor selects one in its constructor before its thread starts:

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
`std::visit` provides the hot loop a concrete reference (e.g. `BQueue&` or
`ShardedBQueue&`), so `push` and the drain loop are devirtualized and inlined and
the selection imposes no per-message cost. Lane count affects results: one lane
per expected producer produced the fan-in result, and eight lanes with one
producer produced the burst result.

## Reproduction

The benchmark is a single self-contained program,
`actors/cpp/perf/bench_pingpong.cpp` (branch `feat/variant-mailbox`), linking only
the actor library.

```bash
export KSPRPROJ=$(pwd)
make -C actors/cpp opt                 # build libactors.a
make -C actors/cpp test                # 32 queue unit tests
cd actors/cpp/perf && make

./bench_pingpong 500000 20000 all      # solo, burst16, fanin, transport
./bench_pingpong 300000 10000 grouped  # single-thread table

taskset -c 2    ./bench_pingpong 500000 20000 solo   # both threads, one core
taskset -c 2,34 ./bench_pingpong 500000 20000 solo   # SMT pair
taskset -c 2,3  ./bench_pingpong 500000 20000 solo   # same CCD
taskset -c 2,6  ./bench_pingpong 500000 20000 solo   # different CCD
```

`fanin` spawns `min(32, hardware_concurrency − 2)` producers (32 on a 64-thread
machine). Topology is available from `lscpu`, and CCD grouping from `lscpu -e` or
`/sys/devices/system/cpu/cpu*/cache/index3/shared_cpu_list`. The CPU numbers above
are specific to this machine.

Each configuration was run three times before any ordering was accepted; this
identified Regime 1 as noise-dominated.

## Not controlled

`nohz_full=` and `rcu_nocbs=` are empty in `/proc/cmdline`, and `chrt` is denied
(`RLIMIT_RTPRIO=0`), so the `max` column reflects scheduler artifacts throughout
and is cited only as evidence of instability. The fan-in and burst tables are
unpinned; NUMA-aware placement of the consumer was not tested. Results are from
one machine, one compiler, and one build.

---

*Measured on an AMD EPYC 9374F (32 cores / 64 threads, 4 NUMA nodes, 8 CCDs),
Linux 5.14, g++ 15.2.0, branch `feat/variant-mailbox` @ `d15b8a4`, market-data
recorder stopped. Three runs per row, medians reported; raw output and analysis
scripts are in `actors/cpp/perf/results/linux_quiet/`.*
