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

**Why actors?** Each actor owns its private state and communicates only by messages, so no mutable state is shared between actors — and therefore no memory-level data race, and no locks in your own code; you reason about one message at a time against consistent state. (Empirical studies call data races and deadlocks *"two mistakes that are hard to make with actors."*  Actor code is also unusually easy for AI coding agents to write: they know the actor pattern well and generate actors, their message handlers, and self-contained unit tests — send a message in, assert on the reply — with little friction, precisely because there is no shared state or locking to reason about. The usual objection is the messaging overhead; Kaspar answers it with `fast_send`, which runs the receiver's handler inline on the caller's thread and returns the reply as a value (**~10 ns of overhead over a direct call**, loop-amortized on an Apple M3). The design and measurements are written up in [**tech_reports/fast_send.pdf**](tech_reports/fast_send.pdf) (benches in [`actors/cpp/perf`](actors/cpp/perf)).

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

**Quick start:** `./build.sh` sets the required `KSPRPROJ` environment variable
and the external-library paths for you, then runs the build — you don't have to
export anything. Any argument passes through to `make`:

```bash
./build.sh schema        # generate the CME SBE codecs (pinned MDP3 v12 / iLink v8)
./build.sh               # full build (== make all)
./build.sh debug         # debug build
./build.sh -C actors/cpp # build just one component
```

The rest of this section is the manual equivalent, plus the toolchain
prerequisites.

On Debian/Ubuntu, install the toolchain and dependencies:

```bash
sudo apt-get update && sudo apt-get install -y \
    build-essential git pkg-config \
    libzmq3-dev cppzmq-dev nlohmann-json3-dev libgsl-dev \
    libpcap-dev libcrypto++-dev zlib1g-dev libgtest-dev
```

Boost 1.88+ is newer than most distro packages — install a 1.88+ package or
build it from source, then point the build at it.

Generate the CME SBE codecs. `mktdata_v12/` (MDP3) and `ilink_v8/` (iLink 3) are
**generated from CME's SBE templates, not committed** — generate them before the
first build (needs Java and Python `paramiko`, plus network to Maven Central and
CME SFTP; see `genschema/README.md`). By default this regenerates the pinned,
tested versions (MDP3 v12 / iLink v8):

```bash
KSPRPROJ=$(pwd) make schema
```

Build the libraries (optimized):

```bash
KSPRPROJ=$(pwd) make
```

The build refuses to compile with a clear message (the `check-schema` guard) if
the codecs are missing. (Prefer plain `make` over `make -j` for the first build:
the schema guard is not parallel-safe, so on a fresh, un-generated tree `-j` can
start a compile before the guard fires.)

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
├── actors/         Custom C++20 actor framework — per-actor mailboxes, O(1) dispatch, groups, lifecycle; + Rust port & C++/Rust FFI interop
│   └── rust/       Rust port of the actor core (crate `actors`) + a price-time-FIFO `matching_engine` example
├── kaspr/          Main application — process startup, actor wiring, and config (`config/kaspr.ini`)
├── mdp3/           CME MDP 3.0 market-data SBE decoder + sequence-gap / snapshot recovery
├── mcast_recv/     Feed sources — multicast UDP receiver and offline PCAP reader
├── frame_kaspr/    Trading core — OB (MBP book) & TachBook (MBO L3 book), SOM (Simulated Order Manager), BFA (recorded binary-file replay), Timer
├── frame_ref/      Reference data & shared value types — instrument `Asset` defs, `Price`, the `RefData` universe
├── light/          Shadow / POV execution algorithm — the per-side `light22` lights
├── ilink/          CME iLink 3 order-entry session — SBE, HMAC auth, seq management, primary/secondary failover
├── ilink_v8/       Generated iLink v8 SBE protocol headers
├── mktdata_v12/    Generated MDP3 v12 SBE market-data headers
├── chutil/         Core utilities — time, sockets, enums, binary/CSV formats, assert/macros
├── interface/      Factory-function headers that create actors (keeps wiring decoupled from impl)
├── db/             Database persistence actor (stubbed)
├── mtd/            Monitoring — console command handlers + display tables
├── mq0/            ZMQ REQ/REP console server (runtime control/monitoring port)
├── logger/         Asynchronous logging actor
├── positionman/    Per-instrument position tracking
├── oogsl/          GSL math wrappers — stats, matrix, RNG
├── genconfig/      CME config generators (MDP3 + iLink)
├── setclassid/     Message-ID collision checker
└── mk_kaspr/       Build-system templates — glob_begin.mk, lib/app templates, path detection
```

## Actor Framework

The actor framework provides the concurrency model for the entire system:

- **Message passing** — `BQueue` mailbox per actor, O(1) dispatch via `handler_cache[msg_id]`
- **Groups for deterministic simulation** — A `Group` runs multiple actors on a single thread with a single message queue. In PCAP replay, the entire pipeline (OB, lights, SOM) goes into one Group — market data, order placement, and fill matching execute in strict message order. No race conditions, no timing artifacts. Bit-exact reproducible backtests.
- **Zero-copy fast path** — `fast_send()` executes the handler in the caller's thread for synchronous queries — no queue, no thread hop. See the technical report [**fast_send.pdf**](tech_reports/fast_send.pdf) for the synchronous-delivery design and its measured cost (~24 ns round trip; see [`actors/cpp/perf`](actors/cpp/perf)).
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

### Adding a message type

Inherit `MessageT<Derived>`. That's it — the dispatch ID is auto-assigned and
collision-free by construction, so there's nothing to hand-pick:

```cpp
struct MyMessage : public actors::MessageT<MyMessage> { /* ... */ };
```

Each message carries an integer ID that drives O(1) dispatch
(`handler_cache[msg_id]`); with `MessageT` it's assigned at first use and
`get_message_id()` is a non-virtual member read (no vtable on the dispatch path).

(A legacy `Message_N<N>` exists for the rare case that needs the ID as a
compile-time constant. Its IDs are hand-assigned and *not* uniqueness-checked at
compile time, so prefer `MessageT` — it removes the whole class of collision
bugs. See [`setclassid/README.md`](setclassid/README.md) if you must audit
existing `Message_N` IDs.)

## Choosing the Right Queue for Your Actor

Every actor has a **mailbox**: a multi-producer/single-consumer (MPSC) queue that
other threads push messages into and the actor's own thread drains. Kaspar ships
four mailbox implementations, and each actor picks one **in its constructor,
before its thread starts**:

```cpp
enum class MailboxKind { BQueue, BQueueBatched, ShardedBQueue, LockFreeMPSC };
void set_mailbox(MailboxKind kind, size_t cap = 0);   // cap = 0 -> per-kind default
```

> **Recommendation: do not call `set_mailbox` unless you are sure you need a
> specific performance characteristic.** The default `BQueue` is the right
> choice for almost every actor. Only override it when profiling shows a
> particular actor's mailbox is a bottleneck *and* you understand the trade-offs
> — otherwise you are likely to make things slower, not faster. The full
> cross-regime benchmarks and the reasoning behind each queue are here:
> [Not All Queues Fit All in Low-Latency Systems](https://vincentmayeski.substack.com/p/not-all-queues-fit-all-in-low-latency).

The four implementations:

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

The second argument is a sizing hint whose meaning depends on the kind: ring
capacity for `BQueue`/`BQueueBatched`/`LockFreeMPSC`, and **lane count** for
`ShardedBQueue`. Pass `0` (the default) to get each kind's own sensible default
(64 for `BQueue`/`BQueueBatched`, 8 lanes for `ShardedBQueue`, 1024 slots for
`LockFreeMPSC`).

### How to set the mailbox

Call `set_mailbox` **once, in the actor's constructor**, before the actor's
thread starts (switching a live mailbox is not supported). Pick exactly one kind
— or call nothing at all to keep the `BQueue` default:

```cpp
class MyActor : public actors::Actor {
public:
  MyActor() {
    // Choose ONE of the following (or omit to keep the default BQueue):
    set_mailbox(MailboxKind::BQueue);              // default: FIFO, mutex + condvar, unbounded overflow
    set_mailbox(MailboxKind::BQueueBatched);       // FIFO; consumer drains the whole mailbox under one lock
    set_mailbox(MailboxKind::ShardedBQueue, 32);   // 32 lanes for many concurrent producers (NOT FIFO)
    set_mailbox(MailboxKind::LockFreeMPSC, 4096);  // 4096-slot lock-free ring (bounded)

    MESSAGE_HANDLER(MyMessage, on_my_message);     // register handlers as usual
  }
  // ...
};
```

Omit the second argument (or pass `0`) to use the kind's default size; pass an
explicit value to size the ring (or lane count, for `ShardedBQueue`) for your
expected load.

### When in doubt, use BQueue (the default)

For the large majority of actors, **BQueue is the right choice and needs no
configuration.** Most actors are low-contention — driven by one timer, one
upstream stage, or grouped onto a shared thread — and in every one of those cases
the four mailboxes are within a few percent of each other, while BQueue has the
most stable latency tail of the four. Reaching for a "faster" queue here buys
nothing measurable and can hurt: `ShardedBQueue` is actually the *slowest* of the
four for a single producer or a grouped actor, because its lanes exist to spread
contention that isn't there. Do not make it a global default.

### Switch to ShardedBQueue for high-fan-in actors

There is one case where the mailbox choice matters a great deal: an actor that
**many threads write into concurrently** — for example an order book fed by a
dozen market-data handlers at once. Under that fan-in, a single-mutex mailbox
serializes every producer and its tail latency explodes; `ShardedBQueue` gives
each producer its own lane and wins decisively (up to ~10× lower p99 under 32
producers). Set the lane count to roughly the number of concurrent producers:

```cpp
class BookBuilder : public actors::Actor {
public:
  BookBuilder() {
    // Many feed handlers push here at once -> shard to avoid lock contention.
    set_mailbox(MailboxKind::ShardedBQueue, /*lanes=*/32);
    MESSAGE_HANDLER(MDUpdate, on_update);
  }
};
```

### Quick guide

| Actor's write pattern | Mailbox |
|---|---|
| Anything low-contention (one timer/upstream, or grouped) | **BQueue** (default) |
| Many producer threads writing at once (order book, aggregators) | **ShardedBQueue**, lanes ≈ producers |
| Unsure | **BQueue** |

`LockFreeMPSC` has the lowest median in the single-thread/grouped case but a
worse latency tail, and `BQueueBatched` ties `BQueue`; neither is worth switching
to as a default. The full cross-regime benchmarks and the reasoning behind these
recommendations are written up here:
**[Not All Queues Fit All in Low-Latency Systems](https://vincentmayeski.substack.com/p/not-all-queues-fit-all-in-low-latency)**.

## Monitoring

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
| [tech_reports/fast_send.pdf](tech_reports/fast_send.pdf) | Technical report: `fast_send` synchronous message delivery |
| [tech_reports/shadow_pov.pdf](tech_reports/shadow_pov.pdf) | Technical report: Shadow-PPOV passive execution |

## Performance Characteristics

- **Tick-to-trade latency**: median in the ~100 µs range; p99 under 1 ms (expected)
- **Message dispatch**: O(1) vector lookup by message ID — no virtual dispatch, no hash maps
- **Actor send**: Sub-microsecond enqueue (mutex + condition variable, no allocation on hot path)
- **Book update to strategy**: Single `EndOfBurst` message per MDP3 incremental cycle
- **Memory**: Pool allocators for high-frequency message types, zero GC pauses
- **Threading**: One thread per actor, CPU affinity pinning, no contention between instruments

### Measured: actor messaging round-trip latency

From the microbenchmarks in [`actors/cpp/perf`](actors/cpp/perf) (`bench_pingpong`),
ping → pong → reply, one message in flight. Two machines — **indicative, not a
spec; the ratios are the point.** macOS: Apple M3 (8-core, arm64), `-O3 -march=native`, no
pinning. Linux: AMD EPYC 9374F, RHEL 9, g++ 15, `-O3 -march=native`, `taskset` to
two cores (not fully quiesced — see [second data point](actors/cpp/perf/README.md#second-data-point-x86-64-linux)).

| path | macOS p50 | Linux p50 | notes |
|---|---:|---:|---|
| `send`, separate threads | ~2250 ns | ~3370 ns | cross-core mailbox wakeup (mutex + condvar), twice |
| `send`, one `Group` thread | ~125 ns | ~90 ns | no wakeup — queue push/pop + dispatch |
| `fast_send` (inline) | ~24 ns\* | ~30 ns\* | no queue, no thread hop; handler runs in the caller |

\* `fast_send` p50 sits at the `steady_clock` tick (~40 ns macOS; ~10 ns Linux),
so read its amortized cost, not p50: ~24 ns (macOS) / ~10 ns (Linux). The grouped
÷ ungrouped ratio is **18× (macOS), 37× (Linux)** — the shape holds on both.

**How much does the actor machinery cost over a bare function call?** Timed
cleanly (one clock-read pair around a tight loop, identical trivial work on a
stack input):

| | macOS | Linux |
|---|---:|---:|
| direct function call | ~1 ns | ~1 ns |
| `fast_send` (dispatch, no reply) | ~8 ns | ~10 ns |

So **`fast_send` adds ~7 ns over a plain call** (macOS; ~9 ns on an x86-64 Linux
EPYC box) — the uncontended mutex, the message field writes, the
`handler_cache[id]` pointer-to-member dispatch, and the reply `unique_ptr`. That is
the entire framework tax on the fast path: single-digit nanoseconds. Put
differently — a dispatch sweep on the Linux box shows `fast_send` costs **about one
polymorphic virtual call** (a like-for-like comparison against the thing you'd
otherwise write). How it's measured (`run_direct_call` vs `fast_send`, section D):
[perf README](actors/cpp/perf/README.md#d-fast_send-vs-a-bare-function-call) ·
[`bench_pingpong.cpp`](actors/cpp/perf/bench_pingpong.cpp).

**Allocation** — the MemoryPool is a compile-time switch (`DISABLE_MEMORY_POOL`).
Same pooled message types, grouped-send round trip (amortized ns):

| | pool ON | pool OFF |
|---|---:|---:|
| per round trip (2 msgs) | **~92 ns** | ~128 ns (= plain `new`) |
| allocator tail (`max`) | ~33 µs | ~5.5 ms |

The pool removes the per-message allocation (a global `new`+`delete` is ~15 ns on
macOS, ~3.6 ns on glibc/Linux; a pooled alloc is ~2–3 ns on both) and, more
importantly, cuts the allocator **tail** — the durable win. `fast_send` with a
**stack** request message and a pooled reply avoids the heap entirely.

**Second data point (x86-64 Linux, EPYC).** The ratios reproduce on a different
ISA/OS/allocator (grouped ÷ ungrouped is even wider, 37×); the *absolutes* that
don't travel are the `steady_clock` tick (~40 ns macOS vs ~10 ns Linux) and the
global allocator (so the pool's median win shrinks to ~7.5 %). The macOS numbers
above are indicative, not a spec. Full comparison, raw output, and the
run-on-server runbook in the
[perf README](actors/cpp/perf/README.md#second-data-point-x86-64-linux).

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

See [SHADOW_ALGORITHM.md](light/SHADOW_ALGORITHM.md) for the full specification, the write-up
[**"Shadow POV Execution: Trade Where the Market Is Going to Trade"**](https://vincentmayeski.substack.com/p/shadow-pov-execution-trade-where),
and the technical report [**shadow_pov.pdf**](tech_reports/shadow_pov.pdf).

## Versioning & compatibility

Releases are tagged; `main` tracks ongoing development and may contain breaking
changes ahead of the next tag.

| Tag | Actor `Message` ABI | Pin with |
|---|---|---|
| **v0.1.0** (current) | id is a non-virtual data-member read; `Message_N<N>` requires `N` in `[0,512)`; `MessageT<Derived>` auto-assigns collision-free ids ≥ 512 | `git checkout v0.1.0` |
| **v0.0.1** | id via **virtual** `get_message_id()`; original `Message` layout; `Message_N<N>` unconstrained | `git checkout v0.0.1` or the `release-0.0.1` branch |

**v0.0.1 → v0.1.0 is a breaking change** to the actor message layer. Code built
against v0.0.1 must be recompiled, and you must update any message type that:

- used `Message_N<N>` with `N ≥ 512` (now reserved for `MessageT`), or
- declared its own `get_message_id()` override (the base method is no longer
  virtual — prefer `MessageT<Derived>` for new messages, or `Message_N<N>` for a
  fixed compile-time id).

If you built against the old ABI and don't want to migrate yet, **stay on
`v0.0.1`** (or the `release-0.0.1` maintenance branch, which takes backported
fixes without the breaking change). See
[`actors/cpp/include/actors/msg/README.md`](actors/cpp/include/actors/msg/README.md)
for the current message API.

## License

MIT License. Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.). See [LICENSE](LICENSE).
