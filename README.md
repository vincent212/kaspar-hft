<p align="center">
  <h1 align="center">Kaspar</h1>
  <p align="center">
    <strong>High-Frequency Trading Simulator with Position-Aware Order Book Matching</strong>
  </p>
  <p align="center">
    CME Futures &bull; MDP3 Market Data &bull; iLink 3 &bull; PCAP Replay &bull; Shadow Execution &bull; C++20
  </p>
</p>

---

**Kaspar** is a low-latency trading system simulator for CME futures (ES, NQ). It reconstructs full order books from MDP3 market data, simulates fills that respect queue position, and supports live paper trading or historical PCAP replay — all built on a lock-free C++ actor framework designed for microsecond-level performance.

Unlike toy backtesting engines that assume instant fills at mid, Kaspar models realistic execution: your simulated orders sit in the book at a specific price level and only fill when the market trades through your position in the queue.

Named after [Kasprowy Wierch](https://en.wikipedia.org/wiki/Kasprowy_Wierch) — *"a peak of a long crest in the Western Tatras, one of Poland's main winter ski areas."*

**Author:** [Vincent Mayeski](https://www.linkedin.com/in/vmayeski/) — [v@m2te.ch](mailto:v@m2te.ch) | [GitHub](https://github.com/vincent212)

## Key Features

- **Position-aware order book simulator** — MBP (market-by-price) book with simulated queue position tracking. Orders fill based on price-time priority, not magical instant execution.
- **CME MDP3 v12 market data** — Full SBE decoder for incremental book updates, trades, order-by-order (MBO), instrument definitions, and snapshot recovery. Handles sequence gaps automatically.
- **Three operating modes** — PCAP replay (backtest), live multicast (paper trading), and iLink 3 (live execution). Same codebase, same strategy code, switch with config.
- **Shadow execution algorithm** — Production-grade execution logic that piggybacks on real market flow. Places orders only when genuine interest appears at a price level. Zero idle quoting.
- **Lock-free actor framework** — Custom C++20 actor system with O(1) message dispatch, CPU affinity, and sub-microsecond send latency. No shared state, no locks on the hot path.
- **iLink 3 reference implementation** — Full CME iLink v3 session handler with SBE encoding, HMAC authentication, sequence management, and primary/secondary failover.
- **PCAP reader** — Replay recorded CME multicast captures for deterministic backtesting. Bit-exact reproduction of market conditions.
- **External strategy support** — Write strategies in C++ (in-process, lowest latency), or Python/Java via ZMQ remote actor messaging.
- **Rust port of the actor framework** — [`actors/rust2`](actors/rust2) (crate `actors2`): a from-scratch Rust port of the actor core — on-stack `fast_send`, integer-ID O(1) dispatch, the `BQueue` mailbox, and the per-type object pool — shipping a **price-time-FIFO order-book matching engine** as an example. In-process (no remoting/registry/groups yet).

## Architecture

```
CME MDP3 Multicast (or PCAP file)
         |
    SocketReader / PCAPReader
         |
    MessageProcessor ──── RecoveryProcessor (snapshot recovery on gap)
         |
    handler_if (SBE decode + routing)
         |
    ┌────┴────┐
    OB        TachBook          OB = MBP (sim)    TachBook = MBO L3 (live)
    |         |
    EndOfBurst (per instrument)
    |
    light22 (per side)              ← shadow execution lights
    |
    SOM (Simulated Order Manager)   ← queue-position-aware fill simulation
    |
    ┌──┴──┐
    DB    iLink (live only)
```

**Per instrument**: buy and sell lights share a lock-free `QCoord` (working order tracking) and `PCoord` (position state). No coordination messages — just shared memory counters.

## Quick Start

```bash
# Build (optimized)
KSPRPROJ=$(pwd) make -j6

# Rebuild kaspr executable with MBO L3 order book (for live trading)
cd kaspr/src && KSPRPROJ=~/m2_kaspar USE_TACHBOOK=1 make

# Run with PCAP replay
cd kaspr && ./kaspr config/kaspr.ini

# Run with position reset
cd kaspr && ./kaspr config/kaspr.ini --reset-positions
```

### Requirements

- C++20 compiler (GCC 12+)
- Boost 1.88+
- ZeroMQ (libzmq)
- nlohmann/json
- GSL (GNU Scientific Library)

## Operating Modes

| Mode | Data Source | Execution | Order Book | Use Case |
|------|-----------|-----------|------------|----------|
| **PCAP Replay** | `.pcap` files | SOM (simulated) | OB (MBP) | Backtesting, strategy development |
| **Paper Trading** | Live CME multicast | SOM (simulated) | OB (MBP) | Forward testing with real data |
| **Live Trading** | Live CME multicast | iLink 3 to CME | TachBook (MBO L3) | Production execution |

## Project Structure

```
kaspar/
├── actors/          C++20 actor framework (messaging, lifecycle, ZMQ remoting)
│   └── rust2/       Rust port of the actor framework (actors2) + matching_engine example
├── kaspr/           Main application (startup, wiring, config)
├── mdp3/            MDP3 market data decoder and recovery
├── mcast_recv/      Multicast UDP receiver and PCAP reader
├── frame_kaspr/     Order book (OB/TachBook), SOM, BFA, Timer
├── light/           Shadow execution algorithm (light22)
├── ilink/           CME iLink 3 session handler
├── ilink_v8/        iLink v8 SBE protocol headers (generated)
├── mktdata_v12/     MDP3 v12 SBE market data headers (generated)
├── chutil/          Core utilities (time, sockets, enums, binary formats)
├── interface/       Factory function headers for actor creation
├── db/              Database actor (stubbed)
├── mtd/             Monitoring and console display
├── mq0/             ZMQ console server
├── logger/          Logging actor
├── positionman/     Per-instrument position tracking
├── oogsl/           GSL math wrappers (stats, matrix, random)
├── genconfig/       CME config generators (MDP3 + iLink)
├── setclassid/      Message ID collision checker
└── mk_kaspr/        Build system templates
```

## Shadow Execution — The Core Innovation

Most execution algorithms either cross the spread (expensive) or continuously quote (noisy, adverse selection). Kaspar takes a third path: **shadow execution** — a percentage-of-volume algorithm that participates in natural market flow rather than fighting it. Shadow algorithms routinely outperform VWAP and TWAP benchmarks because they don't signal intent and they avoid adverse selection by only trading alongside genuine order flow.

Each instrument runs independent execution lights per side. They don't quote. They don't model. They **watch** — and strike only when the order book reveals genuine intent.

```
Real market participant places order at 6050.00
    → light22 sees ADD on its side
    → checks: position < target? price in range? level not crowded?
    → throttle gate: deterministic (every Nth) or stochastic (3%)
    → places at 6050.00 — same price, piggybacking on real flow
    → tracks the real order it attached to
    → if that order gets hit or pulled → auto-cancel with delay
```

**Why this works:**

| Property | Traditional MM | Shadow Execution |
|----------|---------------|-----------------|
| Adverse selection | High (stale quotes get picked off) | Low (only at prices with real interest) |
| Queue position | Poor (late to the level) | Better (enters alongside real flow) |
| Complexity | Model-heavy (fair value, skew, Greeks) | Microstructure-only (ADD/CANC signals) |
| Latency requirement | Ultra-low (race to cancel) | Moderate (no quotes to defend) |

The lights coordinate via **lock-free shared memory** — `QCoord` tracks aggregate working orders, `PCoord` tracks net position. No messages, no locks, no contention.

See [SHADOW_ALGORITHM.md](light/SHADOW_ALGORITHM.md) for the full specification, and the write-up
**"Shadow POV Execution: Trade Where the Market Is Going to Trade"** on the author's
[Substack](https://vincentmayeski.substack.com).

## Actor Framework

The actor framework provides the concurrency model for the entire system:

- **Lock-free message passing** — `BQueue` mailbox per actor, O(1) dispatch via `handler_cache[msg_id]`
- **Groups for deterministic simulation** — A `Group` runs multiple actors on a single thread with a single message queue. In PCAP replay, the entire pipeline (OB, lights, SOM) goes into one Group — market data, order placement, and fill matching execute in strict message order. No race conditions, no timing artifacts. Bit-exact reproducible backtests.
- **Coordinator for distributed simulation** — When simulation spans multiple processes or languages, the `CoordinatorActor` and `GroupManager` provide deterministic cross-process ordering. Groups in separate processes (C++, Python, Rust) request permission from a central GroupManager before processing each message — TOKEN → REQUEST → GRANT → DONE protocol over ZMQ. This extends single-process determinism to distributed systems.
- **Zero-copy fast path** — `fast_send()` executes handler in caller's thread for synchronous queries
- **CPU affinity** — Pin actors to cores for deterministic latency
- **Distributed deployment via ZMQ** — Actors communicate transparently across processes and machines. Run one actor group in NY4 and another in DC3 — actors use the same `send()` API whether the target is local or remote. `ZmqSender`/`ZmqReceiver` handle serialization and transport. Actors don't know or care if they're talking to a local thread or a remote process.
- **Rust port** — [`actors/rust2`](actors/rust2) (`actors2`) is a from-scratch Rust port of the actor core (on-stack `fast_send`, integer-ID O(1) dispatch, `BQueue`, object pool). It is in-process only (no ZMQ/registry/groups yet) and ships a **matching engine** as an example — see its [README](actors/rust2/README.md) and [DEVELOPER_GUIDE](actors/rust2/DEVELOPER_GUIDE.md).

The design behind the framework is written up here:
[**The Actor Model for Low-Latency Software**](https://vincentmayeski.substack.com/p/the-actor-model-for-low-latency-software),
[**A High-Performance Mailbox**](https://vincentmayeski.substack.com/p/high-performance-mailbox-in-the-kaspar)
(the `BQueue`), and
[**A Custom Memory Allocator (10× improvement)**](https://vincentmayeski.substack.com/p/a-custom-memory-allocator-for-the)
(the object pool).

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

See [actors/cpp/CLAUDE_AGENT_GUIDE.md](actors/cpp/CLAUDE_AGENT_GUIDE.md) for the complete framework reference.

## Console

Connect via ZMQ (default port 7777) for runtime monitoring:

| Command | Description |
|---------|-------------|
| `prices` | Show bid/ask for all instruments |
| `bbbo` | Best bid/offer in 32nds format |
| `fills` | Recent fill history |
| `get_orders` | Working orders |
| `assets` | Configured instruments |
| `startom` / `stopom` | Enable/disable order matching |
| `order sym=ESM6 sz=1 bs=BUY px=6000 x=CMEMDFUT` | Place order |
| `cancel id=123 x=CMEMDFUT` | Cancel order |

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
| [actors/rust2/README.md](actors/rust2/README.md) | Rust actor-framework port — overview & quickstart |
| [actors/rust2/DEVELOPER_GUIDE.md](actors/rust2/DEVELOPER_GUIDE.md) | Writing actors in the Rust port |
| [actors/rust2/MATCHING_ENGINE.md](actors/rust2/MATCHING_ENGINE.md) | The matching-engine example |

## Performance Characteristics

- **Message dispatch**: O(1) vector lookup by message ID — no virtual dispatch, no hash maps
- **Actor send**: Sub-microsecond enqueue (mutex + condition variable, no allocation on hot path)
- **Book update to strategy**: Single `EndOfBurst` message per MDP3 incremental cycle
- **Memory**: Pool allocators for high-frequency message types, zero GC pauses
- **Threading**: One thread per actor, CPU affinity pinning, no contention between instruments

## Writing

Deep-dives on the design behind Kaspar (author's Substack — [vincentmayeski.substack.com](https://vincentmayeski.substack.com)):

- [**The Actor Model for Low-Latency Software**](https://vincentmayeski.substack.com/p/the-actor-model-for-low-latency-software) — a concurrency model invented for single-CPU machines turned out to be the right one for multicore.
- [**A High-Performance Mailbox in the Kaspar C++ Actor System**](https://vincentmayeski.substack.com/p/high-performance-mailbox-in-the-kaspar) — ring buffers are great until they overflow (the `BQueue` design).
- [**A Custom Memory Allocator for the Kaspar Actor System Gives 10× Improvement**](https://vincentmayeski.substack.com/p/a-custom-memory-allocator-for-the) — when you know the size at compile time, almost everything an allocator does becomes unnecessary (the object pool).
- **Shadow POV Execution: Trade Where the Market Is Going to Trade** — a percentage-of-volume algorithm that follows passive flow (on the [Substack](https://vincentmayeski.substack.com)).

## License

MIT License. Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.). See [LICENSE](LICENSE).
