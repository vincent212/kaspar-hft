# FPGA actor runtime: programmer's guide

How to write FPGA actors and designs for the Kaspar-HFT FPGA runtime, how they talk to CPU
actors, and why the runtime is built the way it is. For an overview and how to run the
examples, see [README.md](README.md).

Status: everything here is written and tested on the host, with the FPGA design run in
software threads. Nothing has been synthesized or run on a card yet (see
[Known limitations](#known-limitations)).

---

## 1. The pieces

| piece | what it is | file |
|---|---|---|
| message | a plain struct with a fixed id and a field list | your code |
| actor | a struct with an id, private state and handlers | your code |
| endpoint | an actor process, or the host link | `links.hpp` |
| `Links<NE>` | the FIFOs between the NE endpoints of a design | `links.hpp` |
| `DiscoveryTable` | actor id → endpoint that holds it; fixed when the design is built | `links.hpp` |
| `Envelope` | one message as it travels: destination, sender, id, kind, length, tag, payload | `envelope.hpp` |
| `Ctx` | the handler's view of the runtime: `send`, `reply`, `fast_send` | `actor.hpp` |
| `actor_step` / `actor_process` | one step / the endless loop of an actor process | `actor.hpp` |
| `host_in_step`, `host_in_reply_step`, `host_out_step` | the host link | `links.hpp` |
| `FpgaBridge` | the CPU side: joins the CPU actor runtime to the card | `host/FpgaBridge.hpp` |
| `SoftCard` | runs a design in software threads, one per process | `host/SoftCard.hpp` |

Each actor process runs on its own. Between two endpoints `s` and `r` there are three
FIFOs, each with one writer and one reader:

```
  msg[s][r]    messages: send, and replies to a send
  freq[s][r]   fast_send requests from s to r
  frep[s][r]   replies from s to a fast_send that r made
```

A sender writes straight into the FIFO that leads to its receiver. Nothing sits between
two actors.

---

## 2. Messages

```cpp
struct Ping
{
  static constexpr kfpga::MsgId id = 301;
  uint32_t count = 0;
  KFPGA_FIELDS(count)
};
```

- `id` is fixed and unique. 1–15 are the framework's (`Error` is 1); applications use
  300–399.
- `KFPGA_FIELDS(...)` lists the fields that travel, in order.
- Fields are integers, enums, `bool` or `char`, of 8 bytes or less. No pointers, no
  floating point, no arrays: the encoding is exact on the FPGA and on the CPU.
- A message fits in 12 32-bit words; 8-byte fields take two. A message that does not fit
  is rejected at send time and reported (`ERR_PAYLOAD_FULL`).

A message that crosses to the CPU also needs a Kaspar class with the same id and fields
(section 8).

---

## 3. Actors

```cpp
struct PongActor
{
  static constexpr kfpga::ActorId kId = 2;   // unique on this design, 1..15
  uint32_t pings = 0;                        // private state

  KFPGA_HANDLERS(KFPGA_ON(Ping, on_ping))    // message id -> handler

  template <class Ctx>
  void on_ping(const Ping &m, Ctx &ctx)
  {
    ++pings;
    Pong r;
    r.count = m.count + 1000 * pings;
    ctx.reply(r);
  }
};
```

- `kId` is the actor's address, 1–15. 0 is the host.
- `KFPGA_HANDLERS(KFPGA_ON(Msg, handler) ...)` generates the dispatch: a `switch` on the
  message id. A message with no handler is reported (`ERR_NO_HANDLER`).
- **Handlers are templates on `Ctx`.** The `Ctx` type carries, at compile time, which
  process the handler runs in (section 10 explains why). Write
  `template <class Ctx> void on_x(const X &m, Ctx &ctx)`.
- One message at a time: a handler runs to the end before the actor takes the next
  message.

### The `Ctx` calls

| call | what it does |
|---|---|
| `ctx.send(dst, msg)` | writes `msg` straight into the FIFO to actor `dst`, on this FPGA or the CPU; returns at once |
| `ctx.reply(msg)` | answers the sender of the current message. To a `send`: a message back on the FIFO it came from. To a `fast_send`: held, and returned to the waiting caller when the handler ends; only the first reply counts |
| `ctx.fast_send(b, req, rep)` | `b` is an actor object inside this process: its handler runs now, inline. Returns `true` if it replied with `rep`'s type |
| `ctx.fast_send(dst, req, rep)` | `dst` is an actor id in another process, on this chip or on the CPU: the request goes straight into `dst`'s request FIFO and this process waits on the reply FIFO from `dst`. Returns `true` if it replied with `rep`'s type |
| `ctx.sender()` | the id of the actor that sent the current message |
| `ctx.self()` | the id this handler is running as |

`fast_send` returns `false` when:
- the callee did not reply;
- it replied with another type (reported, `ERR_WRONG_REPLY`);
- the call failed (no route, no handler, an error on the CPU side). The failure is
  reported once, by whoever detected it.

While an actor waits in a remote `fast_send`, its process handles nothing else. The rest
of the chip keeps running.

### What gets served first

Each step an actor process takes one message, in this order:
1. a `fast_send` request, from any endpoint, round-robin;
2. a message the process sent to itself;
3. a message from any endpoint, round-robin.

A request is never queued behind messages, as a CPU `fast_send` bypasses the mailbox. It
does not interrupt a handler that is already running.

---

## 4. Rules for actor code

Actor code is synthesized to hardware by Vitis HLS (C++14). Inside handlers:

- **No heap.** No `new`, `malloc`, `std::vector`, `std::string`. State is fixed-size
  members.
- **Fixed loop bounds.** Every loop needs a compile-time bound; break out early with an
  `if` (see `FusedPing` in `examples/ping_pong/pingpong.hpp`).
- **No recursion, no virtual functions, no function pointers, no exceptions.**
- **No floating point in messages.** Use integer mantissa and exponent.
- **Do not `fast_send` your own process.** A process cannot wait on itself; the call is
  reported (`ERR_CYCLE`) and returns `false`. Call an actor inside your own process with
  the inline `fast_send(b, …)`.
- **Sends to yourself** go into a queue of 8 inside the process. A ninth before the queue
  drains is reported (`ERR_SELF_FULL`).
- **A cycle of remote `fast_send`s deadlocks**, as on the CPU: A waits on B, which waits
  on A. Keep the call graph between actors acyclic.

`make check14` compiles the runtime and the design top as C++14 to keep to the dialect
Vitis HLS accepts.

---

## 5. Actors inside another actor's process

An actor can hold other actors as members. They run in its process, and it calls them
with the inline `fast_send(b, …)`: no FIFO at all.

```cpp
struct FusedPing
{
  static constexpr kfpga::ActorId kId = 3;
  PongActor<4> pong;                         // inside this process

  KFPGA_HANDLERS(KFPGA_ON(Start, on_start))
  KFPGA_INNER(pong)                          // messages to id 4 reach it here

  template <class Ctx>
  void on_start(const Start &m, Ctx &ctx)
  {
    Pong r;
    if (ctx.fast_send(pong, Ping{}, r))      // pong.on_ping runs now
      ctx.send(kfpga::kHost, Done{r.count});
  }
};
```

To make an inner actor reachable by others too, list it in `KFPGA_INNER(...)` and map its
id to the outer actor's endpoint in the discovery table. A message is handled by the
actor it is addressed to; one addressed to an id the process does not hold is reported
(`ERR_NOT_HERE`).

---

## 6. Writing a design

A design fixes which actors exist, which process each runs in, and the table that finds
them. `examples/ping_pong/` is the reference.

**1. Number the endpoints.** Actor processes are 0..N-1; the host link is N. `NE = N + 1`.

```cpp
constexpr int kNumActors = 3;                 // 0 Ping, 1 Pong, 2 FusedPing
constexpr int kEndpoints = kNumActors + 1;    // 3 = the host link
```

**2. Fill the discovery table.** Every actor id that anyone sends to maps to the endpoint
that holds it: an actor process, `kToHost` for CPU actors, `kNoRoute` for the rest.

```cpp
inline kfpga::DiscoveryTable discovery()
{
  kfpga::DiscoveryTable rt;
  for (int i = 0; i < kfpga::kMaxActors; ++i)
    rt.port[i] = kfpga::kNoRoute;
  rt.port[kfpga::kHost] = kfpga::kToHost;
  rt.port[kPing] = 0;
  rt.port[kPong] = 1;
  rt.port[kFusedPing] = 2;
  rt.port[kFusedPong] = 2;          // inside FusedPing's process
  rt.port[kCpuPong] = kfpga::kToHost;   // a CPU actor
  return rt;
}
```

**3. The hardware top.** One process per actor and three for the host link, connected by
one `Links<NE>`:

```cpp
using L = kfpga::Links<kEndpoints>;

template <class A, int K>
void actor_proc(L &links)
{
  static A actor;
  const kfpga::DiscoveryTable rt = discovery();
  kfpga::actor_process<A, kEndpoints, K>(actor, links, rt);
}

void my_top(hls::stream<Envelope> &from_host, hls::stream<Envelope> &from_host_reply,
            hls::stream<Envelope> &to_pcie)
{
#pragma HLS INTERFACE mode = axis port = from_host
#pragma HLS INTERFACE mode = axis port = from_host_reply
#pragma HLS INTERFACE mode = axis port = to_pcie
#pragma HLS INTERFACE mode = ap_ctrl_none port = return
#pragma HLS DATAFLOW
  static L links;
  host_in_proc(from_host, links);              // loops on host_in_step
  host_in_reply_proc(from_host_reply, links);  // loops on host_in_reply_step
  actor_proc<PingActor, 0>(links);
  actor_proc<PongActor<kPong>, 1>(links);
  actor_proc<FusedPing, 2>(links);
  host_out_proc(links, to_pcie);               // loops on host_out_step
}
```

The endpoint number `K` in `actor_proc<A, K>` must match the discovery table. The full
file is `hls/pingpong_top.cpp`.

**4. The software version.** For tests, the same steps run either one after another
(`examples/ping_pong/design.hpp`, `Design::step`), or each in its own thread on a
`SoftCard` (`examples/cpu_fpga/fpga_side.hpp`, `Design::steps`). Each actor needs its own
`ActorPorts<NE>` (round-robin position and self queue).

---

## 7. Testing

- **One step at a time.** Call each process's step in turn until nothing moves
  (`Design::run`). Deterministic, and you can check any FIFO between steps:
  `L.msg[0][1]` holds what endpoint 0 sent endpoint 1.
- **One thread per process.** An actor waiting on a remote `fast_send` blocks its step,
  as it stalls its process in hardware. Designs with such actors run on a `SoftCard`, or
  run the waiting actor's step in its own thread (`Links.FastSendIsOneFifoHopEachWay` in
  `tests/test_runtime.cpp`).
- **With the CPU.** Run the design on a `SoftCard` and connect an `FpgaBridge`
  (`tests/test_bridge.cpp`).

`make test` runs the C++14 check and both test binaries.

---

## 8. Talking to CPU actors

The CPU side is `FpgaBridge`. Set it up before starting the CPU actors:

```cpp
fpga_side::Design design;                    // the FPGA design, in software for now
auto card = std::make_shared<kfpga::SoftCard>(design.steps(), design.from_host,
                                              design.from_host_reply, design.to_pcie);
auto bridge = std::make_shared<kfpga::FpgaBridge>(card);
bridge->register_messages<Ping, Pong, Done>();      // before start()
bridge->add_cpu_actor(kCpuPong, cpu_pong);          // CPU actors the FPGA addresses
card->start();
bridge->start();
actors::ActorRef fpga_pong = bridge->ref(kFpgaPong);   // hand to CPU actors
```

**Messages.** Each message that crosses has a Kaspar class with the same id and fields as
its FPGA struct:

```cpp
struct Ping : public actors::Message_N<301> { uint32_t count = 0; KFPGA_FIELDS(count) };
```

**CPU → FPGA.** A CPU actor uses the `ActorRef` exactly as for a CPU actor:
`fpga_pong.send(new Ping(2), this)` or `auto r = fpga_pong.fast_send(&p, this)`. Several
CPU threads may have calls outstanding at once.

**FPGA → CPU.** An FPGA actor sends to the CPU actor's id; the discovery table maps it to
`kToHost`. A `fast_send` to a CPU actor runs the CPU handler on a bridge thread for the
calling FPGA actor, and the FPGA actor's process waits for the reply.

**Replies.** A CPU handler's `reply()` to a message from the FPGA reaches the FPGA actor.
`m->sender` is a stand-in for the FPGA actor: `send` and `fast_send` to it go to the
FPGA.

**Ids.** FPGA and CPU actors share ids 1–15. 0 is the host itself: a message from a CPU
sender that is not registered carries 0. A `fast_send` from such a sender gets its reply
as usual; a reply to a `send` from it has nowhere to go and is reported.

**Failures.** A CPU `fast_send` that fails throws: no route, unregistered message, an
error from the FPGA, or a bridge that is not running. Called from a handler, that ends the
process. Everything else is reported through `bridge->on_error` (stderr by default) and
counted in `bridge->errors()`. `stop()` releases any CPU caller still waiting, with no
reply, and reports it.

---

## 9. Synthesis and measuring

```
cd actors/fpga/hls && vitis_hls -f run_hls.tcl      # Linux, Vitis HLS installed
```

`run_hls.tcl` synthesizes one small top per runtime part (`bench_tops.cpp`), then the
full ping-pong design. The defaults are the Virtex UltraScale+ VU2P of the AMD Alveo
UL3524, at 3.2 ns. Both can be changed with `PART` and `CLOCK_NS`; check the exact part
string before quoting numbers. Each report,
`kfpga_hls/<top>/solution1/syn/report/<top>_csynth.rpt`, gives latency in cycles.

| top | part |
|---|---|
| `bench_actor_step` | an actor takes one message and replies |
| `bench_host_in` | the host link takes one message from PCIe |
| `bench_host_out` | the host link puts one message out to PCIe |
| `bench_fast_send` | one `fast_send` to an actor in the same process |

Synthesis numbers are estimates, not measurements on a card, and must be labelled as such.

---

## 10. Design decisions

**Nothing between two actors.** A message goes from the sender's process into a FIFO that
the receiver's process reads: one hop. A central stage that every message passes through
would add a hop and a shared bottleneck to every message. The discovery table is only a
lookup, done in the sender, of which FIFO leads to an actor.

**One writer and one reader per FIFO.** Vitis HLS dataflow requires it. That is why
there are separate FIFOs per pair of endpoints and per kind (message, request, reply), and
why a receiver with several senders reads several FIFOs: the merge happens inside the
receiver's own process, round-robin.

**Handlers are templates on `Ctx`.** For every FIFO to keep one writer, the code that
writes must know at compile time which process it runs in. A `Ctx<NE, K>` carries the
endpoint `K` as a template parameter, so `ctx.send` compiles to writes on `msg[K][*]`
only.

**`fast_send` requests are served first.** A request goes to its own FIFO and is taken
before messages, so a call is not delayed by the callee's backlog. It never interrupts a
running handler.

**Every `fast_send` is answered.** If the handler does not reply, the runtime still sends
a reply with id 0; if the call cannot be delivered, it sends one carrying an error. A
caller is never left waiting by the runtime itself.

**Replies carry the request's tag.** The CPU side can have many calls outstanding to one
FPGA actor; it matches each reply to its caller by tag, not by order or sender id.

**Replies from the CPU have their own input stream.** An FPGA actor waiting on a CPU actor
must get its reply even when the host link is blocked writing a message into a full FIFO.
On a shared input stream the reply could sit behind that message, forever.

**The bridge's reader never runs a CPU handler.** It only reads the card and delivers. CPU
handlers called by an FPGA actor run on a worker thread for that FPGA actor. A handler that
waits, or that calls back into the FPGA, then never stops replies from being delivered,
and two FPGA actors calling the CPU do not wait on each other.

**Failures are loud.** Everything the runtime cannot do is reported, and a failed CPU
`fast_send` throws; nothing is dropped silently.

**Same model as the CPU runtime.** `send`, `fast_send` and `reply` mean the same as in
`actors/cpp`, and the same rules hold: isolation, one message at a time, a `fast_send`
cycle deadlocks. The semantics are in
`tech_reports/extended_actor_model/extended_actor_model.pdf`.

---

## Known limitations

- **Not synthesized, not run on a card.** Whether the free-running `DATAFLOW` form of the
  top synthesizes, or needs `hls::task`, is open. There is no `CardTransport` for the
  UL3524's PCIe DMA yet.
- **Bounded FIFOs.** In hardware, FIFOs have a fixed depth. Two actors that send to each
  other can both block when the FIFOs between them are full. The software stand-in never
  fills, so the tests cannot show this. FIFO depths per pair are not set yet.
- **FIFOs for every pair.** `Links<NE>` declares FIFOs for every pair of endpoints, even
  pairs that never talk. Declaring only the pairs a design uses is not done.
- **One schema.** FPGA structs, CPU classes and discovery tables are written by hand;
  generating them from one schema, as `actors/rust/interop/codegen/generate.py` does for
  Rust, is not done.

---

## Future work: a GPU runtime

The same design would carry over to a GPU. It is not built (there is no GPU to run it on).

| actor model | GPU runtime |
|---|---|
| actor | a warp or thread block inside one persistent kernel |
| FIFO between actors | a ring buffer in device memory |
| handler dispatch | a `switch` on the message id |
| `send` | write to the receiver's ring |
| `fast_send` inside the GPU | a device function call |
| link to the CPU | rings in pinned host memory, polled by the kernel and by a CPU bridge thread |
| `fast_send` CPU → GPU | write the request, wait for the reply in the reply ring |
| `fast_send` GPU → CPU | the request to the host; a host thread runs the CPU handler and writes the reply back |

The envelope, the actor ids and the CPU bridge would be shared with the FPGA runtime.
