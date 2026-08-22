<!--
    Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
    Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
    Licensed under the MIT License. See LICENSE file in the project root.
-->

# Choosing an Actor Mailbox

Every actor owns a mailbox — a queue that other threads push messages into and the
actor's own thread drains. All mailboxes implement one interface, `Queue<T>`
(`Queue.hpp`), so they're interchangeable. Which one an actor uses is a per-actor
decision driven almost entirely by **how many threads produce into it**, because that
is what sets tail-latency jitter.

## The three mailboxes

| Mailbox | Header | Optimizes for |
|---|---|---|
| `BQueue` | `BQueue.hpp` | The default. One mutex + condvar; a fixed ring with an unbounded overflow deque. Best median; tails under high contention because a losing producer parks. |
| `ShardedBQueue` | `ShardedBQueue.hpp` | Many producers (high fan-in). N mutex lanes, round-robin push, round-robin-merge drain. Spreads contention across lanes → lowest p99 under load. |
| `LockFreeMPSC` | `LockFreeMPSC.hpp` | Few producers, latency-critical. Bounded Vyukov ring, park-free push. Flat sub-µs tail when contention is low; degrades under high fan-in (CAS-retry storms). |

## Selection matrix

| Your actor's producers | Use | Why |
|---|---|---|
| 1 producer | `BQueue` or `LockFreeMPSC` | Both ~42 ns; no contention to fix. |
| Few (2–4), latency-critical | **`LockFreeMPSC`** | Park-free → flat, sub-microsecond tail. |
| Many (high fan-in) | **`ShardedBQueue`** | Fewest threads per sync point → lowest p99. |
| Many *and* latency-critical | Sharded lock-free (roadmap) | N lock-free lanes → few producers per CAS line. |

Rule of thumb: **shard first, go lock-free second.** A single lock-free queue becomes
a hotspot the moment its producer count climbs — the CAS loop starts retrying and
tails just like a mutex.

## Measurements

Latency of a single `push()` under load, ns (Apple Silicon, `-O3`, median of
interleaved runs). Jitter lives in the tail, so read p99/p99.9, not the median.

**High contention (8 producers into one mailbox):**

| Mechanism | p50 | p99 | p99.9 |
|---|---:|---:|---:|
| `BQueue` | 42 | ~35 µs | ~79 µs |
| `ShardedBQueue` (8 lanes) | ~420 | **~7.4 µs** | ~75 µs |
| `LockFreeMPSC` | ~875 | ~38 µs | ~113 µs |

**Low contention (2 producers):**

| Mechanism | p50 | p99 | p99.9 |
|---|---:|---:|---:|
| `BQueue` | 250 | 333 | 4.3 µs |
| `LockFreeMPSC` | 170 | 292 | **417 ns** |

The far tail (p99.99+) is the OS scheduler, not the queue. To go under it: `SCHED_FIFO`,
pin threads to dedicated cores, `isolcpus`.

## How to select one

Each actor picks its mailbox by which base constructor it calls. The default
constructor gives you a `BQueue`; a protected `Actor(Queue<const Message*>*)`
constructor lets a subclass install any mailbox (the `Actor` takes ownership and
frees it in its destructor).

```cpp
// Default: single-lane BQueue — do nothing special.
class Logger : public Actor {
public:
  Logger() { strncpy(name, "Logger", sizeof(name)); /* MESSAGE_HANDLER(...); */ }
};

// High fan-in actor: 8-lane sharded mailbox.
#include "actors/ShardedBQueue.hpp"
class OrderManager : public Actor {
public:
  OrderManager()
    : Actor(new ShardedBQueue<const Message*>(8)) {
    strncpy(name, "OrderManager", sizeof(name));
    // MESSAGE_HANDLER(...);
  }
};

// Few-producer, latency-critical actor: lock-free mailbox (capacity rounds up to pow2).
#include "actors/LockFreeMPSC.hpp"
class FastStrategy : public Actor {
public:
  FastStrategy()
    : Actor(new LockFreeMPSC<const Message*>(4096)) {
    strncpy(name, "FastStrategy", sizeof(name));
    // MESSAGE_HANDLER(...);
  }
};
```

Constructor arguments: `BQueue(ring_size)`, `ShardedBQueue(num_lanes)`,
`LockFreeMPSC(capacity)` (rounded up to a power of two). Everything downstream
(`send`, `pop_batch`, the run loop) goes through the `Queue<T>` interface, so nothing
else in the actor changes.

> Note: mailbox selection is currently a C++ feature. The Rust port hard-codes
> `BQueue`; matching it would mean making `ActorCell`'s queue a trait object.

## Further reading

The design and measurements behind these mailboxes:

- [How Message Batching More Than Doubles Actor Model Throughput](https://vincentmayeski.substack.com/p/how-message-batching-more-than-doubles)
- [Medians Lie, Tails Kill: Why Kaspar Shards Mailbox Locks](https://vincentmayeski.substack.com/p/medians-lie-tails-kill-why-kaspar)
- [The Lock-Free Illusion: Why CAS Storms Kill Actor Queues Under Contention](https://vincentmayeski.substack.com/p/the-lock-free-illusion-why-cas-storms)
