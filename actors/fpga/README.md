# FPGA actors

Actors on an FPGA card, and CPU actors talking to them, with the same `send`.
Research prototype: everything runs in software (no Vitis, no card), and
nothing has been synthesized yet.

- **On the card:** an actor is a process, `send` is one write into a FIFO with
  one writer and one reader, and a process reads its input FIFOs round-robin.
  Two actors that talk are joined by a `Link` (a FIFO each way); `reply()`
  answers on the link the message came in on. There are no routers.
- **Between the CPU and the card:** a CPU actor's own thread writes 64-byte
  envelopes into its window to the card. The card writes into lanes in host
  memory owned by the receiving CPU thread, whose `FpgaLanes` mailbox
  (`actors/cpp`) polls them alongside its CPU queue. There is no routing thread.
  The FPGA actor's stand-in on the CPU is the `sender`, so `reply()` works
  unchanged.
- **`send` only across processes and across the link.** A request and its reply
  are two messages.

## Layout

| path | what |
|---|---|
| `include/kfpga/envelope.hpp` | the 64-byte envelope, `kfpga::Message<id>`, `KFPGA_FIELDS` |
| `include/kfpga/ring.hpp` | the software FIFO / window / lane: sequence number last, batched reader position |
| `include/kfpga/actor.hpp` | `kfpga::Actor<Self>`, `ActorRef`, `Link`, `Process`, `KFPGA_HANDLERS` |
| `include/kfpga/soft_card.hpp` | runs a design stepped (deterministic) or one thread per process |
| `host/kfpga/cpu_link.hpp` | the CPU end of a link: `CpuMsg<F>`, `Codec`, `CpuEnd` |
| `examples/ping_pong/` | shared messages and FPGA actors; FPGA <-> FPGA example |
| `examples/cpu_fpga/` | CPU <-> FPGA example, both directions |

## Running

Needs a C++17 compiler and `actors/cpp` (built by the Makefile).

```
cd actors/fpga
make run
```

`fpga_to_fpga` plays 1000 round trips between `PingActor` and `PongActor` on the
card, stepped and then threaded. `cpu_fpga` uses the same two FPGA actors: a CPU
`CpuPing` plays against the FPGA `PongActor`, and the FPGA `PingActor` plays
against a CPU `CpuPong`.
