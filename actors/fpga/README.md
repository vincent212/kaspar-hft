# FPGA actors

Actors on an FPGA card, and CPU actors talking to them, with the same `send`.
Research prototype. The FPGA examples pass Vitis HLS C simulation, synthesis
and RTL co-simulation on a stand-in part (Kintex UltraScale+ KU5P); nothing has
been built for the target card yet. **How the design maps onto hardware, and
why it is built this way: [HLS.md](HLS.md).** In RTL co-simulation at 250 MHz
an FPGA <-> FPGA round trip takes 5 cycles (20 ns) and a pipelined process
takes a new message every cycle ([HLS.md](HLS.md) section 6).

> **Testing the FPGA design is done in AMD's simulator: `make hls`.** That
> runs Vitis HLS C simulation, synthesis and RTL co-simulation in xsim (see
> [HLS.md](HLS.md)). The software runs (`make run`) use `SoftCard`, a test
> harness only: it checks the actors' logic and nothing more. It models no
> clock cycles, timing, throughput or FPGA resources, and passing it says
> nothing about whether the design synthesizes or keeps up in hardware.
> `SoftCard` may be removed.

- **On the card:** a process is an `hls::task` (its own circuit, always
  running), `send` is one write into a FIFO with one writer and one reader,
  and a process reads its ports round-robin. A port is one link end: the FIFO
  it reads and the FIFO it writes. Two processes that talk are joined by a
  link (a FIFO each way); `reply()` answers on the port the message came in
  on. There are no routers.
- **Wired at build time, no stored pointers.** An `ActorRef` is a port number
  and an actor id; `send`/`reply` go into the actor's outbox (at most 4 per
  handler) and the process writes them out after the handler; each actor type
  is one object in the design. See [HLS.md](HLS.md) section 2.
- **Between the CPU and the card:** a CPU actor's own thread writes 64-byte
  envelopes into its window to the card. The card writes into lanes in host
  memory owned by the receiving CPU thread, whose `FpgaLanes` mailbox
  (`actors/cpp`) polls them alongside its CPU queue. There is no routing thread.
  The FPGA actor's stand-in on the CPU is the `sender`, so `reply()` works
  unchanged.
- **Groups:** several actors can share one process, `kfpga::Group<A, B, ...>`,
  declared in the design header. As in a CPU `Group`: one handler at a time, `send` between
  members through the group's own queue (handled on a later step), `fast_send`
  between members as an inline call with no FIFO. Each step takes the group's
  own queue first, then the ports round-robin, and hands the message to the
  member named by the envelope's destination id.
- **`fast_send` only within a group, checked at build time.** It goes through a
  `GroupRef<T>`, which only a group containing `T` can bind (`Group::bind`). Between processes
  and across the link, actors only `send`; a request and its reply are two
  messages.

## Layout

| path | what |
|---|---|
| `include/kfpga/envelope.hpp` | the 64-byte envelope, `kfpga::Message<id>`, `KFPGA_FIELDS` |
| `include/kfpga/ring.hpp` | the software FIFO / window / lane: sequence number last, batched reader position |
| `include/kfpga/actor.hpp` | `kfpga::Actor<Self>`, `ActorRef`, `GroupRef`, `Port`, `Link`, `Group`, `Process`, `KFPGA_HANDLERS` |
| `include/kfpga/soft_card.hpp` | test harness only, not a simulator: runs a design stepped (deterministic) or one thread per process |
| `host/kfpga/cpu_link.hpp` | the CPU end of a link: `CpuMsg<F>`, `Codec`, `CpuEnd` |
| `examples/ping_pong/` | shared messages and FPGA actors; FPGA <-> FPGA and group examples, each with a `*_design.hpp` (processes and wiring) shared by the software run and the HLS top |
| `hls/` | Vitis HLS tops (one `hls::task` per process), testbenches and configs for the examples |
| `HLS.md` | how the design maps onto hardware; running the Vitis flow; results |
| `MDP3_PLAN.md` | plan: the MDP3 handler on the X3522PV (channel A, MBP books, no recovery; end-of-packet books, events and trades out) |
| `mdp3/` | phase 1 of the plan: the MDP3 decoder, generated from CME's template, checked against CME's codec |
| `examples/cpu_fpga/` | CPU <-> FPGA example, both directions |

## Running

In AMD's tools (needs Vitis 2026.1 and a license; see [HLS.md](HLS.md)):

```
source ~/tools/Xilinx/2026.1/Vitis/settings64.sh
cd actors/fpga
make hls
```

In software only (a C++17 compiler; `cpu_fpga` also builds `actors/cpp`,
which needs Boost, ZeroMQ, cppzmq and nlohmann/json; point `BOOST_PATH`,
`ZMQ_PATH`, `CPPZMQ_PATH` and `JSON_PATH` at them if they are not under
`/usr/local`, e.g. at the `.deps/` that the top-level `./build.sh` fetches):

```
cd actors/fpga
make run
```

`fpga_to_fpga` plays 1000 round trips between `PingActor` and `PongActor` on the
card, stepped and then threaded. `group` puts `PingActor`, `PongActor` and
`SyncPing` in one process: Ping plays Pong with `send` through the group queue,
SyncPing with inline `fast_send`. `make check_group_fast_send` checks that moving
Pong to another group makes SyncPing's `fast_send` fail to compile. `cpu_fpga` uses the same two FPGA actors: a CPU
`CpuPing` plays against the FPGA `PongActor`, and the FPGA `PingActor` plays
against a CPU `CpuPong`. `make hls` puts all three FPGA designs through Vitis;
for `cpu_fpga` that is the FPGA half, with the testbench standing in for the
CPU actors ([HLS.md](HLS.md) section 5).
