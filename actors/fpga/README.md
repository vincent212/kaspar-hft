# FPGA actor runtime

An actor runtime that runs on an FPGA, alongside the CPU runtime (`actors/cpp`) and the
Rust runtime (`actors/rust`). Each runtime hosts its own actors, written in its own
language: an FPGA actor is written in Vitis HLS C++ and runs only on the FPGA. Actors on
different runtimes talk through bridges, with the same `send`, `fast_send` and `reply` as
inside one runtime.

**To write FPGA actors and designs, read
[FPGA_PROGRAMMERS_GUIDE.md](FPGA_PROGRAMMERS_GUIDE.md).** It also records the design
decisions and the known limitations.

> **Experimental.** The FPGA runtime and its CPU bridge are a research prototype. They
> are tested only in software: nothing has been synthesized or run on a card, the
> interfaces may change, and they are not part of the production trading system. Do not
> depend on them for anything live.

---

## Overview

- **Actors.** Each FPGA actor is its own hardware process: an id, private state, and
  handlers chosen by message id. It handles one message at a time.
- **Messages.** Plain structs with a fixed id and integer fields; the same layout crosses
  PCIe to the CPU.
- **`send`.** The sender writes straight into a FIFO that leads to the receiver. Nothing
  sits between two actors; a table fixed when the design is built tells the sender which
  FIFO leads where.
- **`fast_send`.** To an actor inside the same process, the callee's handler runs inline.
  To an actor in another process, or on the CPU, the request goes straight to the
  callee and the caller's process waits for the reply; the rest of the chip keeps running.
  Requests are served before ordinary messages.
- **CPU ↔ FPGA.** `host/FpgaBridge.hpp` joins the CPU actor runtime to the card. A CPU
  actor holds an ordinary `ActorRef` to an FPGA actor; an FPGA actor addresses a CPU actor
  by id. All four directions work for `send` and `fast_send`.

| directory | contents |
|---|---|
| `include/actors_fpga/` | the runtime: `envelope.hpp`, `links.hpp`, `actor.hpp`, `stream.hpp` |
| `host/` | the CPU side: `FpgaBridge.hpp`, `CardTransport.hpp`, `SoftCard.hpp` |
| `examples/ping_pong/` | an FPGA-only design: ping-pong between processes and inside one |
| `examples/cpu_fpga/` | CPU and FPGA actors talking in all four directions |
| `hls/` | the hardware top, per-part synthesis tops, `run_hls.tcl` |
| `tests/` | runtime tests and bridge tests |

---

## The next version

The design for the next version of the runtime and its CPU bridge is in
[docs/FPGA_BRIDGE_DIRECT_LANES.md](../../docs/FPGA_BRIDGE_DIRECT_LANES.md). The code in this
directory is the current version, described above.

- **Actors are written as on the CPU.** An actor derives from `kfpga::Actor<Self>` (no
  virtual functions), handlers are ordinary member functions, it sends with
  `ref.send(m, this)` and answers with `reply(m)`. Messages derive from
  `kfpga::Message<id>`. There is no `Ctx` in actor code.
- **References hold the streams.** An actor holds a `kfpga::ActorRef<Id>` for each actor
  it talks to. A reference to an actor in another process holds references to the FIFOs
  between the two, declared once in the HLS top, so every FIFO has one writer by
  construction. Actor ids live in the references, not in the actors.
- **A process is a `Group`.** A group of actors is declared in the HLS top,
  `kfpga::Group<A, B, ...>`, and runs as one process. It behaves like a CPU `Group`: one
  handler at a time, `send` through the group's own queue, `fast_send` inline. Which
  actors share a group is decided at deployment, as on the CPU; actors never contain
  other actors, and `KFPGA_INNER` goes away.
- **`fast_send` only within a group.** Between FPGA processes, and between the CPU and
  the card, actors only `send`; a request and its answer are two messages. The card has
  no natural mechanism for running another process's handler, and a cross-process
  `fast_send` could only be a `send` plus a blocking wait.
- **No routing threads in the bridge.** Each CPU thread writes to the card through its
  own window, and the card writes into rings owned by the CPU thread that runs the
  receiving actor.
- **Card-independent.** Everything card-specific is two adapters, one in the FPGA design
  and one `CardTransport` on the CPU. The first card is the AMD Alveo X3522PV.

---

## Running the examples and tests

Needs a C++17 compiler, GoogleTest, and the CPU actor library built in `actors/cpp`
(`make -C ../cpp`). No Vitis and no card: the FPGA design runs in software.

```
cd actors/fpga
make test        # C++14 check of the runtime, FPGA runtime tests, CPU <-> FPGA bridge tests
make example     # the CPU <-> FPGA example
```

`make example` runs `examples/cpu_fpga/`. The FPGA side runs `FpgaPong` (id 2) and
`FpgaCaller` (id 3); the CPU side runs `CpuPong` (id 8) and `CpuDriver` (id 9), which
drives four cases:

```
1 CPU->FPGA fast_send  Ping(1) -> Pong(1001)
2 CPU->FPGA send       Ping(2) -> Pong(2002)
3 FPGA->CPU send       3 round trips, last Pong(3003)
4 FPGA->CPU fast_send  3 round trips, last Pong(6003)
```

1. `CpuDriver` calls `fpga_pong.fast_send(&p, this)`, as it would to a CPU actor.
2. `CpuDriver` calls `fpga_pong.send(new Ping(2), this)`; the reply arrives in its
   mailbox.
3. `FpgaCaller` calls `ctx.send(kCpuPong, p)`; `CpuPong`'s ordinary `reply()` comes back
   to it.
4. `FpgaCaller` calls `ctx.fast_send(kCpuPong, p, r)` and waits for each reply.

Each Pong counts the pings it has seen, so the numbers also show that actor state
persists: 6003 is `CpuPong`'s sixth ping.

`examples/ping_pong/` has no CPU side; the runtime tests (`tests/test_runtime.cpp`) drive
it. To synthesize the hardware on Linux with Vitis HLS:

```
cd actors/fpga/hls && vitis_hls -f run_hls.tcl
```

See section 9 of the guide for what it reports.
