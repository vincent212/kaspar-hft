# FPGA actors in hardware: how the design maps onto Vitis HLS

How the FPGA actors become circuits, why the framework is built the way it is,
and how to check a design with AMD's tools. Written as the base for a
tutorial: each section explains one idea, and the code is in this directory.

## 1. The model in one picture

```
            hls::task                       hls::task
         +-------------+   pp_fwd FIFO   +-------------+
 tb  --> | PingProc    | --------------> | PongProc    |
     <-- |  PingActor  | <-------------- |  PongActor  |
         +-------------+   pp_back FIFO  +-------------+
          port 0: tb                      port 0: pp
          port 1: pp
```

| software idea | on the chip |
|---|---|
| process (`kfpga::Group` / `kfpga::Process`) | an `hls::task`: its own circuit, always running |
| link (`kfpga::Link`) | two `hls::stream`s, one each way: wires plus a small FIFO memory |
| port | one link end of a process: the FIFO it reads and the FIFO it writes |
| `send` | one FIFO write |
| actor | state (registers) plus handlers (logic) inside a process |

Every FIFO has exactly one writer and one reader. A port does not change that:
"send to port 1" means "write the one FIFO plugged into port 1". Several actors
in one group may send on the same port, but they are one circuit running one
handler at a time, so the FIFO still has one writer.

A process reads its ports round-robin, hands the message to the member named
by the envelope's destination id, runs the handler, then writes out whatever
the handler sent. That is one *step*.

## 2. The rule that shapes everything: no stored pointers

On a chip, every connection is a wire, fixed when the design is built. HLS can
follow a pointer only if it can work out at compile time what it points to. A
pointer kept in an object and followed later (the usual C++ way to wire objects
together) is rejected:

```
ERROR: [HLS 214-134] ... Pointer to pointer is not supported
```

The first version of this framework did exactly that: `ActorRef` held a
`Fifo*`, `Group` held `std::tuple<A*...>`, each actor held a `reply_to_`
pointer. It ran in software and in C simulation and failed synthesis. Each
pointer was replaced as follows; what an actor writes did not change.

### ActorRef: a port number, not a pointer

```cpp
ping.pong    = kfpga::ActorRef(1, kPong);       // port 1, actor id kPong
ping.starter = kfpga::ActorRef(0, kTestbench);  // port 0
```

The top decides what each port is plugged into, so an actor still does not
know where its peers are. A port number is a small constant the compiler can
turn into fixed wiring.

### send and reply: an outbox

A handler cannot reach the FIFOs (that would need a stored pointer again). So
`send` and `reply` put the envelope and its port into a small fixed outbox
inside the actor:

```cpp
pong.send(p, this);   // -> outbox: {envelope, port 1}
reply(r);             // -> outbox: {envelope, the port the message came in on}
```

When the handler returns, the process writes each entry to its port's FIFO
with a `switch` on the port, a multiplexer in hardware. The outbox holds
`kOutbox` = **4** envelopes: at most four `send`/`reply` calls per handler.
Hardware needs a fixed bound; going over it trips an assert in software and C
simulation (raise `kOutbox` if an actor needs more).

### Actors: one object per type

Each actor type is one object in the whole design, `kfpga::instance<T>()`, and
exactly one process holds it. A group finds its members, and a `GroupRef<T>`
finds the target of a `fast_send`, by type, with no pointer.

The limit: one actor of each type per design. Several actors of the same type
(say four buy lights) would need an instance number in the type and a distinct
id each; not built yet, since `kId` is per type today.

### fast_send: by type, checked at build time

```cpp
kfpga::GroupRef<PongActor> pong;            // in SyncPing
pong.fast_send(p, this, r);                 // runs PongActor's handler now, inline
Grp::bind(sync.pong);                       // in the wiring
```

`bind` has a `static_assert` that the target is a member of the group, so a
`fast_send` to an actor in another process does not compile
(`make check_group_fast_send`).

### Ports: references built on each step, inlined

A process function gets its streams as arguments and builds its port table on
every call:

```cpp
kfpga::Port ports[2] = {{tb_in, tb_out}, {pp_in, pp_out}};
proc.step(ports);
```

This works because `step` and everything it calls are inlined
(`#pragma HLS INLINE`): after inlining, `ports[1].out` is a fixed stream. The
round-robin read uses two passes over fixed indices rather than
`ports[next]`, so no port is ever picked by a variable index.

### A group's own queue only when it has two or more members

`send` between members of a group goes through the group's own queue, a FIFO
inside the process. A process of one actor has no other member to send to, and
HLS rejects a stream that nothing writes (`XFORM 203-123`), so a one-actor
process has no queue.

## 3. Writing a design

A design is three small files. The examples in `examples/ping_pong/` and
`hls/` follow this layout.

**The design header** (`examples/ping_pong/fpga_to_fpga_design.hpp`): the
processes and the wiring, shared by the software run and the HLS top so the
two cannot drift apart.

```cpp
using PingProc = kfpga::Process<PingActor>;   // port 0: testbench, port 1: Pong
using PongProc = kfpga::Process<PongActor>;   // port 0: Ping

inline void wire()
{
  PingActor &ping = PingProc::member<PingActor>();
  ping.starter = kfpga::ActorRef(0, kTestbench);
  ping.pong = kfpga::ActorRef(1, kPong);
}
```

**The HLS top** (`hls/fpga_to_fpga_top.cpp`): one task per process, two
streams per link.

```cpp
static void ping_task(Fifo &tb_in, Fifo &tb_out, Fifo &pp_in, Fifo &pp_out)
{
  static PingProc proc;          // static: the state survives between steps
  wire();
  kfpga::Port ports[2] = {{tb_in, tb_out}, {pp_in, pp_out}};
  proc.step(ports);
}

void fpga_to_fpga_top(Fifo &in, Fifo &out)
{
  hls_thread_local Fifo tb_fwd, tb_back, pp_fwd, pp_back;
  hls_thread_local hls::task relay_in(relay, in, tb_fwd);
  hls_thread_local hls::task relay_out(relay, tb_back, out);
  hls_thread_local hls::task ping(ping_task, tb_fwd, tb_back, pp_back, pp_fwd);
  hls_thread_local hls::task pong(pong_task, pp_fwd, pp_back);
}
```

An `hls::task` repeats its function forever as its own circuit. The relays are
there for co-simulation: a process polls its FIFOs with non-blocking reads, and
RTL co-simulation hangs when those are on a top-level port of a free-running
design (`COSIM 212-382`). The relays touch the top-level ports with plain
blocking reads and writes, and the processes only poll internal streams.

**The testbench** (`hls/tb_fpga_to_fpga.cpp`): writes Start into `in`, calls
the top once, reads Done from `out`. The same file drives C simulation and RTL
co-simulation.

**The software run** (`examples/ping_pong/fpga_to_fpga.cpp`) uses the same
design header with `kfpga::Link` and `SoftCard`. That is a quick check of the
actors' logic only; it models no cycles or timing.

## 4. Running it

Needs Vitis 2026.1 and a license (below).

```
source ~/tools/Xilinx/2026.1/Vitis/settings64.sh
cd actors/fpga
make hls          # csim, synthesis and co-simulation for both examples
make hls_csim     # or one step
```

Each design's part and clock are in `hls/<design>.cfg`; logs go to
`hls/<design>.<step>.log`, reports to `hls/w_<design>/`.

| step | tool | what it shows |
|---|---|---|
| C simulation | `vitis-run --mode hls --csim` | the C++ gives the right answers, built the way HLS builds it |
| synthesis | `v++ -c --mode hls` | it becomes hardware: cycles, clock speed, resources |
| co-simulation | `vitis-run --mode hls --cosim` | the generated Verilog, run in xsim with the same testbench, gives the same answers |

## 5. Part and license

The target card is the Alveo X3522PV (`xcux35-vsva1365-2-e`, Virtex
UltraScale+), which no free license covers. For testing we use the **Kintex
UltraScale+ KU5P** (`xcku5p-ffvb676-2-e`): the same UltraScale+ generation
(logic, DSP slices, block RAM and UltraRAM), covered by AMD's free license.
Latency, II and rough resource use carry over; it says nothing about the X3's
network side or whether a full design fits.

Vivado and Vitis need a license file even for the free tier. From AMD's
Product Licensing site, generate a node-locked license with the no-charge
**Vivado Basic Tier** and **Vitis HLS** entries for the build machine's
Ethernet MAC, and put `Xilinx.lic` in `~/.Xilinx/`. The installer must
include the device family (Kintex UltraScale+ here).

## 6. Results

KU5P, 4 ns clock (250 MHz), 1000 round trips each:

| design | C sim | synthesis | co-simulation | resources |
|---|---|---|---|---|
| `fpga_to_fpga` (Ping and Pong as two tasks) | PASS | Fmax ≈ 342 MHz | PASS, 352 µs simulated | 4678 FF, 7435 LUT, no BRAM |
| `group` (three actors, one task; `send` and `fast_send`) | PASS | Fmax ≈ 369 MHz | PASS, 557 µs simulated | 4065 FF, 7325 LUT, 33 BRAM_18K |

## 7. Not done yet

- **Speed.** A process step takes about 33 to 60 cycles, so a round trip
  between two processes is about 88 cycles. Nothing is pipelined: each step
  copies whole 64-byte envelopes through the outbox, and the read, dispatch
  and write phases run one after another. Correct first; tuning is next.
- **The real part.** Nothing has been built for the X3522PV.
- **The CPU <-> FPGA link** (`host/kfpga/cpu_link.hpp`) runs in software only,
  and the `cpu_fpga` example is not on this branch.
- **Several actors of one type** in a design (see section 2).
