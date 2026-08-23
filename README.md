<p align="center">
  <h1 align="center">Kaspar</h1>
  <p align="center">
    <strong>High-Frequency Trading Simulator with Position-Aware Order Book Matching</strong>
  </p>
  <p align="center">
    CME Futures &bull; MDP3 Market Data &bull; iLink 3 &bull; PCAP Replay &bull; POV Execution &bull; C++20
  </p>
</p>

---

**Kaspar** is a low-latency trading system simulator for CME futures (ES, NQ). It reconstructs full order books from MDP3 market data, simulates fills that respect queue position, and supports live paper trading or historical PCAP replay — all built on a custom C++ actor framework designed for microsecond-level performance.

Unlike toy backtesting engines that assume instant fills at mid, Kaspar models realistic execution: your simulated orders sit in the book at a specific price level and only fill when the market trades through your position in the queue.

Named after [Kasprowy Wierch](https://en.wikipedia.org/wiki/Kasprowy_Wierch) — *"a peak of a long crest in the Western Tatras, one of Poland's main winter ski areas."*

**Author:** [Vincent Mayeski](https://www.linkedin.com/in/vmayeski/) — [v@m2te.ch](mailto:v@m2te.ch) | [GitHub](https://github.com/vincent212)

## Key Features

- **Position-aware order book simulator** — MBP (market-by-price) book with simulated queue position tracking. Orders fill based on price-time priority, not magical instant execution.
- **CME MDP3 v12 market data** — Full SBE decoder for incremental book updates, trades, order-by-order (MBO), instrument definitions, and snapshot recovery. Handles sequence gaps automatically.
- **Three operating modes** — PCAP replay (backtest), live multicast (paper trading), and iLink 3 (live execution). Same codebase, same strategy code, switch with config.
- **Shadow execution algorithm** — Production-grade execution logic that piggybacks on real market flow. Places orders only when genuine interest appears at a price level. Zero idle quoting.
- **Actor framework** — Custom C++20 actor system with O(1) message dispatch, CPU affinity, and sub-microsecond send latency.
- **iLink 3 reference implementation** — Full CME iLink v3 session handler with SBE encoding, HMAC authentication, sequence management, and primary/secondary failover.
- **PCAP reader** — Replay recorded CME multicast captures for deterministic backtesting. Bit-exact reproduction of market conditions.
- **External strategy support** — Write strategies in C++ as in-process actors (lowest latency), or in Rust via the in-process C++/Rust FFI interop.
- **Rust port of the actor framework** — [`actors/rust`](actors/rust) (crate `actors`): a from-scratch Rust port of the actor core — on-stack `fast_send`, integer-ID O(1) dispatch, the `BQueue` mailbox, and the per-type object pool — shipping a **price-time-FIFO order-book matching engine** as an example. In-process (no remoting/registry/groups yet).

### Requirements

- C++20 compiler (GCC 12+), GNU Make, Git
- Boost 1.88+
- ZeroMQ — `libzmq` **and** the C++ bindings `cppzmq` (`zmq.hpp`)
- nlohmann/json
- GSL (GNU Scientific Library)
- libpcap (PCAP replay)
- Crypto++ (iLink 3 HMAC)
- zlib
- Google Test (to build and run the unit tests)
- Rust toolchain — optional, only for the `actors/rust` port

## Build

On Debian/Ubuntu, install the toolchain and dependencies:

```bash
sudo apt-get update && sudo apt-get install -y \
    build-essential git pkg-config \
    libzmq3-dev cppzmq-dev nlohmann-json3-dev libgsl-dev \
    libpcap-dev libcrypto++-dev zlib1g-dev libgtest-dev
```

Boost 1.88+ is newer than most distro packages — install a 1.88+ package or
build it from source, then point the build at it.

Build the libraries (optimized):

```bash
KSPRPROJ=$(pwd) make -j
```

Build and run the actor-framework unit tests:

```bash
make -C actors/cpp test        # requires Google Test
```

If your libraries live under a home-dir prefix (not `/usr` or `/usr/local`),
auto-detect and export the paths:

```bash
eval "$(./mk_kaspr/detect_paths.sh)"     # or: ./mk_kaspr/detect_paths.sh --check
```

Notes:

- External library paths (`BOOST_PATH`, `GSL_PATH`, `ZMQ_PATH`, …) are
  environment-overridable `?=` defaults in `mk_kaspr/glob_begin.mk` — set them
  in your shell or run `detect_paths.sh`. See `mk_kaspr/PATHS.md`.
- The Linux build targets x86-64 (`-mcx16`, `-mfpmath=sse`, `-march=native`);
  build on an x86-64 host (or under emulation).

## Operating Modes

| Mode | Data Source | Execution | Order Book | Use Case |
|------|-----------|-----------|------------|----------|
| **PCAP Replay** | `.pcap` files | SOM (simulated) | OB (MBP) | Backtesting, strategy development |
| **Paper Trading** | Live CME multicast | SOM (simulated) | OB (MBP) | Forward testing with real data |
| **Live Trading** | Live CME multicast | iLink 3 to CME | TachBook (MBO L3) | Production execution |

## Project Structure

```
kaspar/
├── actors/          C++20 actor framework (messaging, lifecycle) + Rust port + C++/Rust interop
│   └── rust/       Rust port of the actor framework (actors) + matching_engine example
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

## Shadow Execution Algo

Most execution algorithms either cross the spread (expensive) or continuously quote (noisy, adverse selection). Kaspar takes a third path: **shadow execution** — a percentage-of-volume algorithm that participates in natural market flow. Shadow algorithms can outperform VWAP and TWAP benchmarks because they avoid adverse selection by only trading alongside genuine order flow.

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

The lights coordinate via **shared memory** — `QCoord` tracks aggregate working orders, `PCoord` tracks net position — guarded by fine-grained mutexes rather than passing coordination messages.

See [SHADOW_ALGORITHM.md](light/SHADOW_ALGORITHM.md) for the full specification, and the write-up
[**"Shadow POV Execution: Trade Where the Market Is Going to Trade"**](https://vincentmayeski.substack.com/p/shadow-pov-execution-trade-where).

## Actor Framework

The actor framework provides the concurrency model for the entire system:

- **Message passing** — `BQueue` mailbox per actor, O(1) dispatch via `handler_cache[msg_id]`
- **Groups for deterministic simulation** — A `Group` runs multiple actors on a single thread with a single message queue. In PCAP replay, the entire pipeline (OB, lights, SOM) goes into one Group — market data, order placement, and fill matching execute in strict message order. No race conditions, no timing artifacts. Bit-exact reproducible backtests.
- **Zero-copy fast path** — `fast_send()` executes handler in caller's thread for synchronous queries
- **CPU affinity** — Pin actors to cores for deterministic latency
- **C++/Rust interop** — C++ and Rust actors can talk in the **same process** over a C-ABI FFI bridge (`send`/`fast_send` work across the language boundary). This is in-process only — there is no remote/cross-process actor transport.
- **Rust port** — [`actors/rust`](actors/rust) (`actors`) is a from-scratch Rust port of the actor core (on-stack `fast_send`, integer-ID O(1) dispatch, `BQueue`, object pool). It is in-process only (no ZMQ/registry/groups yet) and ships a **matching engine** as an example — see its [README](actors/rust/README.md) and [DEVELOPER_GUIDE](actors/rust/DEVELOPER_GUIDE.md).

The design behind the framework is written up here:
[**Low-Latency Actor Systems in C++ and Rust**](https://vincentmayeski.substack.com/p/low-latency-actor-systems-in-c-and)
(building this framework in both languages),
[**Actors in C++ and Rust: The Benchmarks, and the Bridge Between Them**](https://vincentmayeski.substack.com/p/actors-in-c-and-rust-the-benchmarks)
(the two ports benchmarked head to head, plus the in-process C++/Rust interop),
[**Lock-Free Isn't Free: Cache Pollution, Busy Cores, and Why Kaspar Blocks**](https://vincentmayeski.substack.com/p/lock-free-isnt-free-cache-pollution)
(why the `BQueue` blocks instead of spinning, and when lock-free is the slower choice),
[**If a Machine Is Going to Write the Code, Make It Rust**](https://vincentmayeski.substack.com/p/if-a-ai-is-going-to-write-the-code)
(why Rust is the language to have AI generate, and why Kaspar added Rust interop),
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

See [actors/cpp/CLAUDE_AGENT_GUIDE.md](actors/cpp/CLAUDE_AGENT_GUIDE.md) for the complete C++ framework
reference, and [actors/rust/DEVELOPER_GUIDE.md](actors/rust/DEVELOPER_GUIDE.md) for the Rust API.

## Console

A running `kaspr` process exposes a **ZMQ request/reply control console** (the
`mq0` server) for live monitoring and manual intervention — inspect books and
positions, place/cancel orders by hand, and pause/resume the order matcher, all
without restarting. It binds a TCP port set by `mqport` in the config (default
**7777**; see [Configuration](#configuration)).

### Protocol

Synchronous **REQ/REP**: the client sends a one-line command string and gets a
single text reply — usually a rendered ASCII table, or a short status line.
Commands are `verb key=value key=value …` (space-separated). Common keys:

| Key | Meaning | Example |
|-----|---------|---------|
| `sym` | instrument name | `ESM6` |
| `sz` | order size | `1` |
| `bs` | side | `BUY` / `SELL` |
| `px` | price | `6000` |
| `x` | venue / exchange | `CMEMDFUT` |
| `id` | order id (for cancel) | `123` |

The server enforces 10 s send/recv timeouts and TCP keepalive, and drops idle
connections after ~45 s — clients should set `RCVTIMEO`/`SNDTIMEO`/`LINGER`.
Full client notes (reconnect, pooling): [`mq0/MQ0_CLIENT_GUIDE.md`](mq0/MQ0_CLIENT_GUIDE.md).

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

### Commands

| Command | Reply | Description |
|---------|-------|-------------|
| `ping` | `OK` | Liveness check. |
| `prices` | table: `sym, bid, ask` | Best bid/ask (integer price) for every instrument with market data. |
| `bbbo` | table: `sym, bid32, bid, ask, ask32` | Best bid/offer, both as integer price and in 32nds. |
| `assets` | table: `id, name, mnem, units, sec_id, exch, maxpx, has_book` | Configured instrument universe. |
| `get_orders` | table | Current working (live) orders. |
| `fills` | table | Recent fill history. |
| `pos fname=<csv>` | table: `sym, pos` | Render a positions CSV file as a table. |
| `startom` / `stopom` | status line | Start / stop the Simulated Order Manager (SOM) — i.e. enable/disable order matching. |
| `order sym=ESM6 sz=1 bs=BUY px=6000 x=CMEMDFUT` | ack | Place an order into the simulator. |
| `cancel id=123 x=CMEMDFUT` | ack | Cancel a working order by id. |

### Example session

Using the REQ client above, each `send_string(...)` returns a text table or
ack. A typical flow:

```
send  "bbbo"                                        -> BBBO table (sym/bid/ask + 32nds)
send  "assets"                                      -> instrument universe
send  "order sym=ESM6 sz=1 bs=BUY px=600050 x=CMEMDFUT"  -> order acked
send  "get_orders"                                  -> the working order appears
send  "stopom"                                      -> "sent stop request to som"
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
| [actors/rust/MATCHING_ENGINE.md](actors/rust/MATCHING_ENGINE.md) | The matching-engine example |

## Performance Characteristics

- **Message dispatch**: O(1) vector lookup by message ID — no virtual dispatch, no hash maps
- **Actor send**: Sub-microsecond enqueue (mutex + condition variable, no allocation on hot path)
- **Book update to strategy**: Single `EndOfBurst` message per MDP3 incremental cycle
- **Memory**: Pool allocators for high-frequency message types, zero GC pauses
- **Threading**: One thread per actor, CPU affinity pinning, no contention between instruments

## Writing

Deep-dives on the design behind Kaspar (author's Substack — [vincentmayeski.substack.com](https://vincentmayeski.substack.com)):

- [**Low-Latency Actor Systems in C++ and Rust**](https://vincentmayeski.substack.com/p/low-latency-actor-systems-in-c-and) — building the same actor framework in both languages: this repo's C++ core and its Rust port (`actors/rust`), and what carries over vs. what the borrow checker changes.
- [**Actors in C++ and Rust: The Benchmarks, and the Bridge Between Them**](https://vincentmayeski.substack.com/p/actors-in-c-and-rust-the-benchmarks) — the two ports benchmarked head to head (`fast_send`, `send`, allocation), and the in-process C++/Rust interop bridge, with numbers.
- [**Lock-Free Isn't Free: Cache Pollution, Busy Cores, and Why Kaspar Blocks**](https://vincentmayeski.substack.com/p/lock-free-isnt-free-cache-pollution) — why lock-free can be the slower choice under load (spinning consumers, cache coherence, oversubscription), and why the `BQueue` blocks and `fast_send` minimizes thread hops instead.
- [**If a Machine Is Going to Write the Code, Make It Rust**](https://vincentmayeski.substack.com/p/if-a-ai-is-going-to-write-the-code) — why Rust is the best language for AI-generated code (compiled, plus the strictest mainstream compiler at catching bugs up front), and why Kaspar added C++/Rust interop.
- [**The Actor Model for Low-Latency Software**](https://vincentmayeski.substack.com/p/the-actor-model-for-low-latency-software) — a concurrency model invented for single-CPU machines turned out to be the right one for multicore.
- [**A High-Performance Mailbox in the Kaspar C++ Actor System**](https://vincentmayeski.substack.com/p/high-performance-mailbox-in-the-kaspar) — ring buffers are great until they overflow (the `BQueue` design).
- [**A Custom Memory Allocator for the Kaspar Actor System Gives 10× Improvement**](https://vincentmayeski.substack.com/p/a-custom-memory-allocator-for-the) — when you know the size at compile time, almost everything an allocator does becomes unnecessary (the object pool).
- [**How Message Batching More Than Doubles Actor Model Throughput**](https://vincentmayeski.substack.com/p/how-message-batching-more-than-doubles) — draining the whole mailbox under one lock, plus the message pool, for a 2.46× throughput win (and why batching the *sender* backfires). *Code on the experimental [`sharded-mailbox`](https://github.com/vincent212/kaspar-hft/tree/sharded-mailbox) branch ([PR #20](https://github.com/vincent212/kaspar-hft/pull/20)), not yet merged.*
- [**Medians Lie, Tails Kill: Why Kaspar Shards Mailbox Locks**](https://vincentmayeski.substack.com/p/medians-lie-tails-kill-why-kaspar) — per-actor mailboxes shard lock contention to flatten tail-latency jitter (~180× at p99.9). *Code on the experimental [`sharded-mailbox`](https://github.com/vincent212/kaspar-hft/tree/sharded-mailbox) branch, not yet merged.*
- [**The Lock-Free Illusion: Why CAS Storms Kill Actor Queues Under Contention**](https://vincentmayeski.substack.com/p/the-lock-free-illusion-why-cas-storms) — when a lock-free mailbox wins and when it tails worse than a mutex; the queue-selection matrix. *Code on the experimental [`sharded-mailbox`](https://github.com/vincent212/kaspar-hft/tree/sharded-mailbox) branch, not yet merged.*
- [**Shadow POV Execution: Trade Where the Market Is Going to Trade**](https://vincentmayeski.substack.com/p/shadow-pov-execution-trade-where) — a percentage-of-volume algorithm that follows passive flow.

## License

MIT License. Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.). See [LICENSE](LICENSE).
