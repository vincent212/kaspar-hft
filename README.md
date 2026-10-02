<p align="center">
  <h1 align="center">Kaspar</h1>
  <p align="center">
    <strong>Turn-Key Production Trading System and Position-Aware Order Book Simulator</strong>
  </p>
  <p align="center">
    CME Futures &bull; MDP3 Market Data &bull; iLink 3 &bull; PCAP Replay &bull; POV Execution &bull; C++20
  </p>
</p>

---

<p align="center">
  <a href="https://arxiv.org/abs/2609.21173">Actor Framework for HFT&nbsp;paper&nbsp;(arXiv)</a> &bull;
  <a href="https://arxiv.org/abs/2609.18019">Shadow-POV&nbsp;paper&nbsp;(arXiv)</a> &bull;
  <a href="tech_reports/kaspar_onepager.pdf">one-pager</a>
</p>

**Kaspar-hft** is a turn-key CME futures production trading system *and* a queue-position-accurate order-book simulator — the same strategy code runs in backtest, paper trading, and live — built on a high-performance C++20 actor framework with nanosecond-scale messaging.

## Kaspar-hft highlights

- **Low-latency actor framework:** No data races, no locks in your code, and almost no framework overhead.
- **Backtest == production:** The same strategy, execution algorithm, and order book run in PCAP replay, paper trading, and live iLink 3; switching is a config change, so a backtest exercises the exact code path that will trade.
- **CME-certified:** The MDP3 market-data handler and the iLink 3 order-entry session have passed CME autocertification and implement the full session lifecycle.
- **Shadow execution algorithm:** Efficient, production-grade, turn-key execution algorithm.
- **Strategy authoring in C++ or Rust:** Write strategies as in-process actors in C++ (lowest latency), or in Rust via the in-process C++/Rust FFI interop.
- **Two papers to dive deeper, more in the works:** The C++ actor framework design [arXiv:2609.21173](https://arxiv.org/abs/2609.21173) and execution algorithm results [arXiv:2609.18019](https://arxiv.org/abs/2609.18019).
- **Formal semantics for fast_send:** `fast_send` added to the actor model as a second primitive, with an operational semantics and proofs of what it keeps (isolation, determinism relative to replies) and what it costs (liveness holds unless actors wait on each other in a circle) — [paper draft (PDF)](https://github.com/vincent212/kaspar-hft/blob/feature/fpga-runtime/tech_reports/extended_actor_model/extended_actor_model.pdf).
- **Async and sync (`fast_send`) delivery are interchangeable:** A handler is the same code either way, so the mapping of actors to threads can be decided at deployment and tuned for tail latency ([arXiv:2609.32848](https://arxiv.org/abs/2609.32848v1)).

---

**Kaspar** is two things sharing one codebase.

It is a **turn-key production trading system**: MDP3 multicast in, full order books reconstructed order-by-order, an execution algorithm on top, and iLink 3 sessions out to CME — with SBE encoding, HMAC authentication, sequence management and primary/secondary failover already certified.

It is also a **position-aware order book simulator**: order books rebuilt from packet capture files, with your orders placed in the price-time queue and filled only when the market actually trades through them. Fills are inferred from exact queue accounting.

The same strategy code, the same execution algorithm and the same book run in PCAP replay, in live paper trading, and against the live exchange; moving between them is a configuration change. A backtest exercises the code path that will trade. It is built on a custom C++ actor framework with nanosecond-scale messaging.

**Why actors?** Each actor owns its private state and communicates only by messages, so no mutable state is shared between actors: no memory-level data races, no torn reads or writes, and no locks in your own code. You reason about one message at a time against consistent state. Deadlocks are still possible — a cycle of synchronous `fast_send` calls can create one — but they are much harder to make. Actor code is also unusually easy for AI coding agents to write: the model is simple, the framework's documentation is written for them, and an actor comes with a natural self-contained unit test — send a message in, assert on the reply. The usual objection to the actor model is the messaging overhead; Kaspar answers it with `fast_send`, which runs the receiver's handler inline on the caller's thread and returns the reply as a value (**about 7.5 ns of overhead over a direct call**).

Named after [Kasprowy Wierch](https://en.wikipedia.org/wiki/Kasprowy_Wierch) — *"a peak of a long crest in the Western Tatras, one of Poland's main winter ski areas."*

**Author:** [Vincent Mayeski](https://www.linkedin.com/in/vmayeski/) (published as Vincent Maciejewski) — [mayeski@gmail.com](mailto:mayeski@gmail.com)

## Production-grade session handling — CME-certified

Kaspar's market-data and order-entry stacks are complete session implementations and both have passed **CME autocertification**. They implement the full protocol lifecycle and its edge cases — the recovery, reconnection, and failover logic a commercial SDK is sold to cover — so you don't hand-roll any of it.

## Build

**Quick start** with `./build.sh`:

```bash
./build.sh schema        # generate the CME SBE codecs (pinned versions)
./build.sh               # libraries (== make all)
./build.sh -C kaspr/src  # the kaspr binary -- see note below
./build.sh debug         # debug build
./build.sh -C actors/cpp # build just one component
```

Two things the quick start does not do:

- **`./build.sh` alone does not build the `kaspr` binary.** The default target
  builds the libraries and exits 0, so a missing executable looks like success.
  Build it explicitly with `./build.sh -C kaspr/src`, and add
  `USE_TACHBOOK=1` if you need TachBook MBO L3 books or the latency probe
  (without that define `create_probes()` is not compiled in at all and a probe
  run writes no samples).
- **`./build.sh schema` needs network access to CME** (`sftpng.cmegroup.com`,
  plus Java and Python `paramiko`). Without it the codecs cannot be generated
  and the build stops before compiling anything. The codecs are pinned — MDP3
  v12, iLink 3 v8 — and are not committed; if you already have a generated set,
  copying it into `mdp3_sbe/` and `ilink3_sbe/` is enough. Verify
  `SBE_SCHEMA_VERSION = 12` before trusting the result: CME's current templates
  are v13 and regenerating against "latest" breaks the pin.

## Operating Modes

| Mode | Data Source | Execution | Order Book | Use Case |
|------|-----------|-----------|------------|----------|
| **PCAP Replay** | `.pcap` files | SOM (simulated) | OB (MBP) | Backtesting, strategy development |
| **Paper Trading** | Live CME multicast | SOM (simulated) | OB (MBP) | Forward testing with real data |
| **Live Trading** | Live CME multicast | iLink 3 to CME | TachBook (MBO L3) | Production execution |


## Actor Framework

The actor framework provides a uniform concurrency model for the entire system. Coding agents will pick up the documentation and will write actor components for you. All you have to do is fill in
your message handlers.

- **Message passing** — a mailbox per actor (`BQueue` by default; see below)
- **Groups** — A `Group` runs multiple actors on a single thread with a single message queue. Enables deterministic simulation.
- **Zero-copy fast path** — `fast_send()` executes the handler in the caller's thread for synchronous queries — no queue, no thread hop, message passed on the stack.
- **C++/Rust interop** — Strategies can be coded in C++ or Rust.

The design behind the framework is written up in the articles listed under
[Deep Dives](#deep-dives).

```cpp
class MyStrategy : public Actor {
public:
    MyStrategy(actor_ptr ob, actor_ptr som) : Actor("strategy") {
        MESSAGE_HANDLER(frame::ob::msg::EndOfBurst, on_book_update);
        MESSAGE_HANDLER(frame::som::msg::Fill, on_fill);
        this->ob = ob;
        this->som = som;
    }

private:
    void on_book_update(const frame::ob::msg::EndOfBurst* eob) {
        // React to order book changes
        auto* order = new frame::som::msg::Order(
            "ESM6", en::BuySell::BUY, 1, eob->best_bid, en::x::CMEMDFUT);
        som->send(order, this);
    }

    void on_fill(const frame::som::msg::Fill* fill) {
        // Handle execution
    }
};
```

The same strategy actor in the Rust port ([`actors/rust`](actors/rust)) — handlers are plain methods,
wired up by the `handle_messages!` macro; message ids are compile-time constants:

```rust
struct MyStrategy {
    ob: ActorRef,
    som: ActorRef,
}

impl MyStrategy {
    fn on_book_update(&mut self, eob: &EndOfBurst, ctx: &mut ActorContext) {
        // React to order book changes
        self.som.send(
            Box::new(Order::new("ESM6", Side::Buy, 1, eob.best_bid, Venue::CmeMdFut)),
            ctx.self_ref(),
        );
    }

    fn on_fill(&mut self, _fill: &Fill, _ctx: &mut ActorContext) {
        // Handle execution
    }
}

handle_messages!(MyStrategy,
    EndOfBurst => on_book_update,
    Fill       => on_fill,
);
```

## Choosing the Right Queue for Your Actor

Every actor has a **mailbox**: a multi-producer/single-consumer (MPSC) queue that
other threads push messages into and the actor's own thread drains.

Pick the best queue for your use case:

- **BQueue** — mutex + condition variable around a ring buffer. Simple, FIFO,
  sleeps when idle. **This is the default** (no `set_mailbox` call needed).
- **BQueueBatched** — same, but the consumer drains the whole mailbox under one
  lock instead of locking per message.
- **ShardedBQueue** — the mailbox split into N independent lanes, each with its
  own lock; producers round-robin across lanes to avoid contending on one lock.
  It does **not** preserve FIFO across lanes, so use it only for actors whose
  handlers are order-independent (an aggregator / order book), never where a
  Start-before-Data or sequence-number ordering is assumed.
- **LockFreeMPSC** — a bounded lock-free ring; producers claim a slot with a
  single atomic operation. Park-free while space is available; if the ring
  fills, a producer spins briefly then blocks (so size the ring for peak
  backlog).
- **LockFreeMPSCSpin** — the same ring with a consumer that never sleeps: the
  actor's thread busy-polls its mailbox. The lowest hand-off latency between
  threads, at the cost of a whole core per actor; only worth it on isolated cores.

More on choosing: [**Not All Queues Fit All in Low-Latency Systems**](https://vincentmayeski.substack.com/p/not-all-queues-fit-all-in-low-latency).

## Monitoring

A running `kaspr` process exposes a **ZMQ request/reply control console** (the
`mq0` server) for live monitoring and manual intervention — inspect books and
positions, place/cancel orders by hand, and pause/resume the order matcher, all
without restarting.

### Connecting

```python
import zmq

ctx = zmq.Context()
sock = ctx.socket(zmq.REQ)
sock.setsockopt(zmq.RCVTIMEO, 10000)
sock.setsockopt(zmq.SNDTIMEO, 10000)
sock.setsockopt(zmq.LINGER, 0)
sock.connect("tcp://localhost:7777")

sock.send_string("bbbo")
print(sock.recv_string())        # prints an ASCII table
```

## Configuration

```ini
kaspr {
    general {
        universe config/universe.csv
        mqport 7777
        tachbook false           # true + USE_TACHBOOK=1 for live
    }
    channels {
        chan_310 true             # ES futures
        chan_318 true             # NQ futures
    }
}
```

## Documentation

| Document | Description |
|----------|-------------|
| [STRATEGY_SIMULATOR_GUIDE.md](STRATEGY_SIMULATOR_GUIDE.md) | Complete guide to all three operating modes |
| [SHADOW_ALGORITHM.md](light/SHADOW_ALGORITHM.md) | Shadow execution algorithm specification |
| [ACTORS_INVENTORY.md](ACTORS_INVENTORY.md) | Every actor in the system |
| [FILE_STRUCTURE.md](FILE_STRUCTURE.md) | Complete file and directory inventory |
| [CLAUDE_AGENT_GUIDE.md](actors/cpp/CLAUDE_AGENT_GUIDE.md) | Actor framework technical reference |
| [actors/rust/README.md](actors/rust/README.md) | Rust actor-framework port — overview & quickstart |
| [actors/rust/DEVELOPER_GUIDE.md](actors/rust/DEVELOPER_GUIDE.md) | Writing actors in the Rust port |
| [tech_reports/fast_send.pdf](tech_reports/fast_send.pdf) &middot; [arXiv:2609.21173](https://arxiv.org/abs/2609.21173) | Technical report: `fast_send` synchronous message delivery |
| [tech_reports/shadow_pov.pdf](tech_reports/shadow_pov.pdf) &middot; [arXiv:2609.18019](https://arxiv.org/abs/2609.18019) | Technical report: Shadow-POV passive execution |

## Performance Characteristics

- **Actor async send**: fast enqueue (mutex + condition variable, no allocation on hot path)
- **Actor sync send**: on the stack, on the caller's thread, bypassing the queue (it never interrupts a handler that is running)
- **Memory**: Pool allocators for same-size messages
- **Grouping**: One thread per actor group

### Measured: actor messaging round-trip latency

<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="tech_reports/img/fast_send_latency_dark.png">
    <img src="tech_reports/img/fast_send_latency.png" width="620"
         alt="fast_send round-trip latency (EPYC): 3370 ns cross-thread async, 90 ns grouped, 30 ns fast_send">
  </picture>
</p>

**How much does the actor machinery cost over a bare function call?** Timed
cleanly (one clock-read pair around a tight loop, identical trivial work on a
stack input), on the same AMD EPYC 9374F: a direct, non-inlined call takes
1.6 ns and a `fast_send` dispatch 9.1 ns, so the actor machinery adds **about
7.5 ns** per hop. A full `fast_send` round trip with a reply is about 30 ns (the
chart above). Details: [perf README](actors/cpp/perf/README.md#d-fast_send-vs-a-bare-function-call).

## Market-data latency (live CME MDP3)

Socket-to-book latency measured on live CME futures (ES, NQ and ZN front month)
on 2026-10-01, with Onload kernel bypass and one feed (feed B switched off). The
socket reader thread decodes each packet and updates the book itself, with no
thread hand-off, on a CPU core of its own. Measured per message from the socket
read (`t0`, right after `recvfrom`) to the book publish (`t1`); time in the NIC and
socket buffer is not included. All figures in µs.

| stream | messages | p1 | p10 | p50 | p90 | p99 | p99.9 |
|---|---:|---:|---:|---:|---:|---:|---:|
| ES book | 249,085 | 0.6 | 0.8 | 1.2 | 4.0 | 16.6 | 42.8 |
| NQ book | 358,522 | 0.6 | 0.7 | 1.1 | 2.6 | 6.2 | 15.7 |
| ZN book | 139,630 | 0.6 | 0.8 | 1.2 | 3.3 | 36.8 | 100.5 |
| ES trade | 33,589 | 0.5 | 0.8 | 3.4 | 11.2 | 48.1 | 86.1 |
| NQ trade | 10,141 | 0.5 | 0.7 | 1.5 | 4.7 | 14.4 | 28.6 |
| ZN trade | 13,304 | 0.5 | 0.9 | 8.5 | 53.6 | 97.8 | 140.7 |

## Shadow Execution Algo

Most execution algorithms either cross the spread (expensive) or continuously quote (noisy, adverse selection). Kaspar takes a third path: **shadow execution** — a percentage-of-volume algorithm that participates in natural market flow by following the orders other participants place.

For more detail — *Model-Free Passive Execution via Order-Level Shadowing* — on arXiv: [**arXiv:2609.18019**](https://arxiv.org/abs/2609.18019).

## Deep Dives

Deep-dives on the design behind Kaspar (author's Substack — [vincentmayeski.substack.com](https://vincentmayeski.substack.com)):

- [**Low-Latency Actor Systems in C++ and Rust**](https://vincentmayeski.substack.com/p/low-latency-actor-systems-in-c-and) — building the same actor framework in both languages: this repo's C++ core and its Rust port (`actors/rust`), and what carries over vs. what the borrow checker changes.
- [**Actors in C++ and Rust: The Benchmarks, and the Bridge Between Them**](https://vincentmayeski.substack.com/p/actors-in-c-and-rust-the-benchmarks) — the two ports benchmarked head to head (`fast_send`, `send`, allocation), and the in-process C++/Rust interop bridge, with numbers.
- [**Lock-Free Isn't Free: Cache Pollution, Busy Cores, and Why Kaspar Blocks**](https://vincentmayeski.substack.com/p/lock-free-isnt-free-cache-pollution) — why lock-free can be the slower choice under load (spinning consumers, cache coherence, oversubscription), and why the `BQueue` blocks and `fast_send` minimizes thread hops instead.
- [**If a Machine Is Going to Write the Code, Make It Rust**](https://vincentmayeski.substack.com/p/if-a-ai-is-going-to-write-the-code) — why Rust is the best language for AI-generated code (compiled, plus the strictest mainstream compiler at catching bugs up front), and why Kaspar added C++/Rust interop.
- [**The Actor Model for Low-Latency Software**](https://vincentmayeski.substack.com/p/the-actor-model-for-low-latency-software) — a concurrency model invented for single-CPU machines turned out to be the right one for multicore.
- [**A High-Performance Mailbox in the Kaspar C++ Actor System**](https://vincentmayeski.substack.com/p/high-performance-mailbox-in-the-kaspar) — ring buffers are great until they overflow (the `BQueue` design).
- [**A Custom Memory Allocator for the Kaspar Actor System Gives 10× Improvement**](https://vincentmayeski.substack.com/p/a-custom-memory-allocator-for-the) — when you know the size at compile time, almost everything an allocator does becomes unnecessary (the object pool).
- [**How Message Batching More Than Doubles Actor Model Throughput**](https://vincentmayeski.substack.com/p/how-message-batching-more-than-doubles) — draining the whole mailbox under one lock, plus the message pool, for a 2.46× throughput win (and why batching the *sender* backfires). *The code was on the `sharded-mailbox` branch ([PR #20](https://github.com/vincent212/kaspar-hft/pull/20)), which was closed without merging; batched draining is available as the `BQueueBatched` mailbox.*
- [**Medians Lie, Tails Kill: Why Kaspar Shards Mailbox Locks**](https://vincentmayeski.substack.com/p/medians-lie-tails-kill-why-kaspar) — per-actor mailboxes shard lock contention to flatten tail-latency jitter (~180× at p99.9). *The sharded mailbox is available as `ShardedBQueue`.*
- [**The Lock-Free Illusion: Why CAS Storms Kill Actor Queues Under Contention**](https://vincentmayeski.substack.com/p/the-lock-free-illusion-why-cas-storms) — when a lock-free mailbox wins and when it tails worse than a mutex; the queue-selection matrix. *The lock-free mailbox is available as `LockFreeMPSC`.*
- [**Shadow POV Execution: Trade Where the Market Is Going to Trade**](https://vincentmayeski.substack.com/p/shadow-pov-execution-trade-where) — a percentage-of-volume algorithm that follows passive flow.

## License

MIT License. Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.). See [LICENSE](LICENSE).
