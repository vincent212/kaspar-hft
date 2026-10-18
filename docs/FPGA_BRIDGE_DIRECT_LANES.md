# FPGA bridge without routing threads

**Status:** design proposal, 2026-10-15. Nothing here is implemented yet.
**Applies to:** `feature/fpga-runtime` (`actors/fpga/`), plus a new mailbox kind and a host for portable actors in `actors/cpp`.
**Cards:** the design is card-independent; the first card is the AMD Alveo X3522PV (section 2.6).

---

## 1. The problem

On `feature/fpga-runtime`, CPU actors already talk to FPGA actors through an ordinary
`ActorRef`; they do not know the peer is on a card. That part is right and stays.

What is wrong is the CPU end of the link, `actors/fpga/host/FpgaBridge.hpp`. Everything
the card sends goes through threads whose only job is routing:

| path | what happens today | thread hand-offs |
|---|---|---|
| CPU → FPGA `send` | caller encodes and writes to the card | **0** (fine) |
| CPU → FPGA `fast_send` | caller locks the bridge-wide `pending_mu_`, inserts into an `unordered_map` (allocates), writes, **sleeps on a condition variable**; the reader thread polls the card, finds the reply, locks, wakes the caller | **1 wake-up** |
| FPGA → CPU `send` | reader thread decodes (allocates) and calls `target->send()` into the CPU actor's mailbox | **1** (reader → actor) |
| FPGA → CPU `fast_send` | reader thread pushes onto a worker queue (mutex + `deque` + condition variable), **wakes the worker**, the worker runs the CPU handler via `Actor::fast_send` and writes the reply | **1 wake-up**, and the FPGA actor's whole process stalls through it |

Waking a sleeping thread costs a few microseconds; the README's cross-thread async round
trip on this machine is 3.37 µs. A PCIe round trip is roughly 1–2 µs and an FPGA actor
step is tens of nanoseconds. **The routing threads cost more than the trip to the card.**
(These are estimates from reading the code and from existing CPU measurements; nothing has
been measured across the bridge.)

The threads exist for a correctness reason: the reader must never run a CPU handler,
because a handler that calls back into the FPGA would wait for a reply that only the
reader can deliver. The fix has to remove the threads without bringing that deadlock back.

### 1.1 Decision: no `fast_send` across the link or between FPGA processes

`fast_send` exists for performance: on the CPU the caller's thread runs the receiver's
handler itself, with no queue and no hand-off. **The card has no natural mechanism for
this.** On an FPGA a handler is a piece of circuit built into its own actor's process,
wired to that actor's state; another process cannot run it, and neither can a CPU thread
on the other side of PCIe. A `fast_send` between FPGA processes, or between the CPU and the
card, can only be built out of streams: a request FIFO, the receiver's process running the
handler, and a blocking wait on a reply FIFO. That is a `send` plus a wait. It gives none
of the speed that `fast_send` exists for, it stalls the caller, and it adds the only way to
deadlock (two callers waiting on each other).

So:

- **between the CPU and the card: `send` only**, in both directions;
- **between FPGA processes: `send` only**;
- **`fast_send` on the FPGA only to an actor in the caller's own group**, which runs as one
  process, where the
  handler is part of the caller's circuit and runs inline. A process is a CPU `Group`
  (9.5): one handler at a time, `send` through its own queue, `fast_send` inline;
- **`fast_send` between CPU actors** is unchanged.

A request and its answer between processes, or across the link, are two messages: the
caller `send`s, and the receiver's `reply` arrives as a message to a second handler. If a
synchronous form ever proves worth having, it can be added later on top of the same
streams.

This removes the two `fast_send` rows of the table above, and with them the reply map,
the condition variables, the worker threads and the deadlock they guarded against.

## 2. Primer: how a CPU and an FPGA card communicate

This section is background for the rest of the document. It describes the mechanisms
available on a PCIe card and the practices low-latency systems follow. Latency figures
are typical ranges, not measurements on our hardware; they vary by platform, card and
PCIe generation and must be measured before they are quoted.

### 2.1 The link

The card sits on a PCIe link (Gen3/Gen4/Gen5, x8 or x16) attached to one CPU socket's root
complex. Everything that crosses travels as PCIe *transactions*. Two kinds matter:

- **Posted writes.** The sender issues the write and moves on; nothing comes back. Cheap
  for the sender: it does not wait for the round trip.
- **Non-posted reads.** The reader issues a request and **waits** for the data to come back
  across the link: a full round trip, typically around 0.5–1 µs, during which the reading
  CPU core is stalled.

**The first rule follows from this: write across the link, never read across it on the
hot path.** Each side pushes data to the other; nobody pulls.

PCIe keeps posted writes from one sender in order. If the card writes a payload and then a
flag, the CPU will never see the flag before the payload. Most of the protocols below rely
on this.

### 2.2 The three mechanisms

**1. MMIO (memory-mapped I/O), CPU → card.** The card exposes register windows (BARs) that
the CPU maps into its address space. A CPU store to that address becomes a PCIe write to
the card.
- *Writes* are posted. If the window is mapped **write-combining (WC)**, the CPU gathers
  up to 64 bytes in a WC buffer and sends them as one transaction. A 64-byte message is
  then a single PCIe write: the lowest-latency way to get a small message to the card
  (often called PIO). Finish with a store fence (`sfence`) so the buffer is flushed now,
  not whenever the CPU gets round to it.
- *Reads* are non-posted and slow (above). Never poll a card register in a loop.

**2. DMA (direct memory access), card → host memory and host memory → card.** The card
itself reads or writes host RAM, without the CPU.
- *Card → CPU*: the card writes into a ring buffer in host memory. The CPU polls that
  memory, which is an ordinary cache hit until the card's write arrives. This is how the
  card delivers anything to the CPU.
- *CPU → card* by DMA: the CPU writes a message (or a descriptor) into host memory and
  then rings a **doorbell** (an MMIO write) telling the card where it is; the card then
  reads it by DMA. That is a write, then a card-side read round trip, so it is slower for
  single small messages than MMIO, but it is better for large or batched transfers.
- Vendor DMA engines (for example AMD's QDMA on Alveo cards, or XDMA) give queues,
  descriptors and doorbells ready-made. They are built for throughput; their per-message
  latency has to be checked, and low-latency designs often add their own simple path next
  to them.

**3. Interrupts (MSI-X), card → CPU.** The card writes a special message that makes the
CPU take an interrupt; the kernel driver can turn that into an `eventfd` that a sleeping
thread waits on. The interrupt plus the wake-up typically costs several microseconds plus
scheduler jitter. It suits anything not latency-critical; on the hot path, threads poll
instead.

### 2.3 Where a DMA write lands

On Intel servers, DDIO lets a DMA write land directly in the CPU's last-level cache, so the
polling thread's next read is a cache hit. Without it, the write goes to DRAM and
invalidates the cached copy; the first poll after it misses to memory (on the order of
100 ns). Check what the AMD EPYC platform offers here before relying on either behaviour;
it affects the numbers, not the design.

### 2.4 Best practices

These are the practices this design follows. Each later section points back to them.

1. **Push, never pull.** Writes across PCIe only. The CPU writes to the card (MMIO or
   doorbell); the card writes to host memory (DMA). Neither side reads the other's memory
   on the hot path.
2. **Poll host memory, never the card.** A CPU thread waiting for the card spins on a
   cache line in host memory. That costs nothing on the link until the card writes it.
3. **Payload first, then the flag, in one direction.** Each slot carries a sequence
   number or valid flag. The writer writes the payload, then the flag; PCIe ordering (or a
   store fence on the CPU side) makes sure the reader never sees the flag before the data.
   The reader reads the flag with acquire ordering, then the payload.
4. **One cache line per message, aligned.** A 64-byte slot is one WC burst going out and
   one cache line coming in. Keep the producer's index and the consumer's index on
   separate cache lines so they do not bounce between cores (false sharing).
5. **Keep index updates off the link.** The consumer should not tell the producer about
   every message it takes. Producers keep a local copy of how much room there is and
   refresh it rarely (credits, or a consumer index written back in batches). Reading the
   other side's index on every message would be a PCIe read on the hot path.
6. **One writer per ring.** No locks and no atomic claims across PCIe. Give each CPU
   thread its own ring to the card and each card output its own ring to each CPU thread.
   (This is the same rule as the FPGA runtime's own FIFOs.)
7. **Pinned, physically contiguous, NUMA-local memory.** Rings live in huge pages that are
   locked in memory and mapped for the card through the IOMMU (VFIO), on the NUMA node the
   card is attached to. Threads that talk to the card are pinned to cores on that node.
   Crossing to the other socket adds latency to every message.
8. **Kernel bypass.** Map the card's BARs and the DMA rings into the user process (VFIO, or
   the vendor's user-space driver) so the hot path never enters the kernel. No system calls
   per message.
9. **Spin on isolated cores where latency matters; interrupts elsewhere.** Polling threads
   go on isolated, pinned cores (`isolcpus`, `nohz_full`). Threads that can tolerate
   microseconds of wake-up use MSI-X.
10. **Batch only off the hot path.** Batching doorbells or DMA descriptors raises
    throughput but delays the first message in the batch. The hot path sends each message
    as soon as it exists.
11. **Keep the hot loop on the card.** In trading systems the usual split is: tick-to-trade
    decisions entirely on the FPGA; the CPU sends parameters and limits down and receives
    fills, state and logs up, mostly asynchronously. A synchronous CPU round trip in the
    middle of a trading decision costs at least a PCIe round trip (around 1–2 µs); design
    so that it is rare.

### 2.5 How this maps onto actors

| actor operation | mechanism |
|---|---|
| CPU actor `send` to FPGA actor | MMIO write-combined 64-byte store into the sending thread's window (or a ring write plus doorbell) |
| FPGA actor `send` to CPU actor | DMA write into a lane ring of the CPU actor's thread, polled by that thread |
| `fast_send` either way | not supported (1.1) |
| waking a CPU actor that does not spin | MSI-X interrupt → `eventfd` that the actor's own thread waits on |

Sections 4–8 make this concrete.

### 2.6 Cards, and the first one: AMD Alveo X3522PV

**The design does not depend on a particular card.** Everything card-specific is kept to
two adapters at the edge:

- **On the FPGA:** a card adapter that connects the actor design's host streams
  (`from_host`, `from_host_reply`, `to_pcie`, or the per-thread windows and rings of
  sections 5 and 8) to that card's PCIe endpoint, DMA engine or vendor shell.
- **On the CPU:** one `CardTransport` implementation per card (`host/CardTransport.hpp`),
  which maps that card's windows and rings into the process.

Actor code, messages, the FPGA runtime, the lanes protocol and the CPU bridge are the same
on every card. `SoftCard` is already one such implementation. A card is usable if it has a
PCIe endpoint the CPU can write into (ideally through a write-combining window) and a way
for the card to write into host memory.

The first card is the **AMD Alveo X3522PV** (A-X3522PV-P08G-PQ-G). From AMD's data sheet
(DS1002 v1.3) and product brief:

| | X3522PV |
|---|---|
| chip | one XCUX35 FPGA (XCUX35-3VSVA1365E), 16 nm UltraScale+, -3 speed grade |
| logic | 1,030K LUTs, 2,059K registers, 1,320 DSP slices; 74.3 Mb block RAM, 99 Mb UltraRAM |
| memory on the card | 2 × 4 GB DDR4 with ECC |
| network | 2 × DSFP28 cages, two ports each (4 × 10/25 GbE); wired to the programmable logic |
| host link | PCIe Gen4 ×8 (Gen3 ×8 fallback) |
| software AMD lists | Vivado (RTL), Vitis HLS, P4; XRT/XOCL |
| power | 70–75 W, passively cooled, half height, half length |

**There is no NIC on the PV.** The data sheet lists Onload, TCPDirect and ef_vi for the
X3522 (the low-latency network adapter built on the same chip), not for the X3522PV. The
network ports go straight into the FPGA's logic, and the host link is whatever our design
puts behind the PCIe endpoint. So on this card:

- **The CPU ↔ FPGA transport is PCIe, memory-mapped**, exactly as in 2.1–2.5. There is no
  Ethernet option between the host and the card without building a NIC in the FPGA.
- **Our FPGA design provides the host interface:** the chip's hard PCIe block (Gen4 ×8),
  write-combining windows the CPU writes envelopes into (5.1), and a DMA engine that writes
  envelopes into host-memory rings (5.2–5.4). The DMA engine is either AMD's DMA IP or our
  own; which AMD IP Vivado supports on the XCUX35 is still to check (section 11).
- **The host side uses `vfio-pci`, not XRT.** XRT/XOCL is supported on the PV, but it is
  built around buffer objects and kernel launches, with every call going through its
  driver. The bridge maps the BARs and the rings into the process with VFIO and runs the
  ring protocol directly.
- **Write-combining under VFIO.** `vfio-pci` maps BARs uncached, so a 64-byte store would
  leave as several small writes. The envelope windows therefore go in a **prefetchable BAR**
  in our design, mapped through the kernel's sysfs file for it
  (`/sys/bus/pci/devices/<address>/resourceN_wc`). The control registers stay in an
  ordinary BAR mapped through VFIO.
- **One envelope, one store.** An envelope is about 60 bytes, padded to 64. The CPU sends
  it with one AVX-512 store into its window followed by `sfence`; a full, aligned 64-byte
  write-combining line normally leaves as one PCIe write. The EPYC 9374F (Zen 4) has
  AVX-512.
- **Market data can arrive on the card.** The PV's four ports feed the FPGA directly, so
  FPGA actors could take MDP3 off the wire themselves instead of through the SFC9250 and
  Onload. That is a separate decision from the bridge and does not change this design.

### 2.7 Not chosen: X3522 in hybrid mode (ef_vi)

The X3522 has a paid hybrid-mode upgrade (SKU SW-X3522-001). AMD's April 2023 STAC Summit
slide, marked "subject to change", shows it as: AMD's low-latency NIC fixed in the chip,
so the host keeps Onload, TCPDirect and ef_vi; up to 400K LUTs of our logic between the
Ethernet MACs and the NIC, on four 32-bit AXI-Stream links each way at 322 MHz; plus an
AXI memory-mapped DMA path to the host and access to the DDR.

The CPU could then reach FPGA actors with raw Ethernet frames through ef_vi: frames the
CPU sends pass through our logic on their way to the MAC, and our logic can hand frames to
the NIC for delivery to the CPU. It is not chosen because:

- **Frames can be lost.** A receive queue with no room drops the frame, and a bad checksum
  drops it too. Actor messages cannot be lost silently, so every lane would need credits
  and sequence numbers. PCIe is lossless.
- **X3 ef_vi constraints.** Receive buffers are shared and managed by the driver (frames
  arrive as `EF_EVENT_TYPE_RX_REF`, read with `efct_vi_rxpkt_get` and released promptly);
  receive queues can be shared between processes unless one is reserved with
  `EF_FILTER_FLAG_EXCLUSIVE_RXQ`; transmit is CTPIO only, with no checksum offload; there
  is no interrupt mode yet; an interface has at most 15 VIs, some taken by Onload. The
  lanes design wants up to three per CPU thread.
- **Less logic and an unconfirmed interface.** 400K LUTs instead of 1,030K, and the
  layout comes from one slide marked "subject to change".

If the bridge ever has to run on an X3522 rather than a PV, this is the fallback, and the
design above (sections 4–8) still holds: a lane becomes a VI, and a ring write becomes a
CTPIO send or a frame received.

## 3. Goals

1. **No routing threads.** Every crossing is written by the thread that has the data and
   read by the thread that will use it.
2. **Same semantics as the CPU runtime for `send`.** One message at a time per actor,
   isolation, per-sender FIFO order. `fast_send` only where it is natural (1.1): between
   CPU actors, and on the card inside one process.
3. **Transparent to actor code.** Senders hold an `ActorRef`; receivers get messages in
   their handlers. Whether a peer is on the card is a deployment decision, made the same
   way a mailbox kind is chosen today.
4. **No allocation and no locks on the hot path.**

Non-goals for the link: changing the FPGA runtime's internals (`actor.hpp`, `links.hpp`)
or the envelope format. How actor code is written for both runtimes is section 9.

## 4. The principle

The FPGA runtime already has the right rule: **nothing between two actors; every FIFO has
one writer and one reader.** This design extends that rule across PCIe.

Every FIFO that crosses the link becomes a ring in host memory (or a card register
window), with **exactly one CPU thread on its end**. There is no shared queue, no
dispatcher, no reader thread.

```
CPU thread T ──(T's window on the card)──────► card host-in ──► FPGA actor
FPGA actor ──► card host-out ──(T's lane ring)──► CPU thread T's mailbox
```

An envelope is about 60 bytes (`envelope.hpp`: 12-byte header, 12 × 4-byte payload), so
it fits in one 64-byte cache line. Each crossing is one cache-line write by the sender
and one cache-line read by the receiver.

**Rings belong to threads, not to actors.** A `Group` runs several actors on one thread,
so the group's thread owns the rings and serves all its members. A stand-alone actor owns
its own.

## 5. The paths

### 5.1 CPU → FPGA `send`

Done entirely on the caller's thread inside `FpgaActorRef::send`:

1. encode the message into an envelope on the stack (fixed size, no allocation);
2. write it into **this thread's** request ring to the card;
3. on the X3522PV, the write is a single 64-byte write-combined store into this thread's
   window on the card, followed by `sfence` (2.6); the card takes each 64-byte write as one
   envelope. A ring in host memory plus a doorbell, with the card fetching by DMA, is the
   fallback if the windows run short.

Each sending thread has its own ring, so producers need no lock or atomic claim.
Per-sender FIFO order holds: an actor always sends from the same thread, so its messages
to a given FPGA actor go through one ring in order.

### 5.2 CPU → FPGA `fast_send`: not supported

See 1.1. A CPU actor that needs an answer from an FPGA actor `send`s the request; the FPGA
actor's `reply` comes back on the CPU actor's lane (5.3) as a message. There are no reply
slots and no spinning caller.

### 5.3 FPGA → CPU `send`

**The card writes straight into the CPU actor's mailbox.**

The CPU actor's thread has an extra input lane: a ring in host memory that the card
writes into. The thread's drain loop reads that lane alongside its normal queue, decodes
the envelope (onto the stack, or into the message pool), and dispatches it like any other
message. The sender it sees is the existing per-FPGA-actor stand-in (`FpgaBridge::Proxy`),
so `reply()` and `send()` to `m->sender` work unchanged. They go back out through **this
thread's** request ring (5.1), still with no other thread involved.

To keep one writer per ring on the card side too, there is one lane per (card host-out,
CPU thread) pair. The card's discovery table maps a CPU actor id to the lane of the
thread that runs it.

### 5.4 FPGA → CPU `fast_send`: not supported

See 1.1. An FPGA actor that needs an answer from a CPU actor `send`s the request through
its reference (5.3 in reverse: the card's host-in path, 5.1); the CPU actor's `reply` goes
back to the card as a message. No CPU handler ever runs on behalf of a waiting FPGA
process, so there is no priority lane, no callee-thread `fast_send` and no locking
question.

## 6. Changes in `actors/cpp`

The mailbox is already a `std::variant` of concrete queue types, with a drain loop
instantiated per type (`Actor.hpp`, `Mailbox` and `run_loop`). This design adds **one
more mailbox kind**:

```cpp
enum class MailboxKind { BQueue, BQueueBatched, ShardedBQueue, LockFreeMPSC,
                         LockFreeMPSCSpin, FpgaLanes };
```

`FpgaLanes` is:
- the existing `LockFreeMPSC` ring, for CPU senders;
- one or more **external lanes**: host-memory rings the card writes into.

Its drain loop takes the external lanes round-robin, then the CPU ring.

It has **no condition variable**: the card cannot signal one. See section 7.

A `Group` that hosts FPGA-facing actors uses the same lane set on the group thread.

`ActorRef`/`FpgaActorRef` keep their current shape (the `FpgaLink` interface added on the
branch). Only the implementation behind `FpgaLink::send` changes: from "write, and let the
reader route" to 5.1. `FpgaLink::fast_send` is removed (1.1); calling `fast_send` on an
`ActorRef` to an FPGA actor fails.

## 7. The cost: who spins

The card cannot wake a sleeping thread through a condition variable. So **a CPU thread
that receives from the card has to poll its lanes**, like `LockFreeMPSCSpin` today:

- **Latency-critical actors** (anything that must see the card's messages at once): spinning
  `FpgaLanes`, pinned to an isolated core. This is normal HFT practice and the same
  tradeoff the framework already offers.
- **Everything else** (logging, risk reports, monitoring): `FpgaLanes` with **interrupt
  wake-up**. The card raises an MSI-X interrupt, mapped to an `eventfd` that the actor's
  own thread waits on. This is slower (microseconds), but there is still no routing
  thread: the wake goes straight to the thread that will handle the message.

The choice is per actor or per group, at deployment, like choosing a mailbox kind.

**Expected result:** a message from the card reaches the CPU actor's handler after one
PCIe crossing and a poll, with no thread wake-up in between. This is an estimate; it has to
be measured (section 12).

## 8. Changes on the card side

The host link processes in `links.hpp` change from "one stream in, one stream out" to
rings:

| process | today | proposed |
|---|---|---|
| `host_in` | reads `from_host` | takes envelopes from the per-thread write-combining windows, round-robin (2.6) |
| `host_in_reply` | reads `from_host_reply` | **removed**: it carried CPU replies to FPGA `fast_send`s (1.1) |
| `host_out` | writes `to_pcie` | writes each envelope into the lane ring of the CPU thread that runs its destination |

The discovery table gains a host-side map: CPU actor id → lane ring.
This is fixed at deployment, like the rest of the table.

## 9. Actor code: one source for CPU and FPGA

Sections 4–8 make the *link* transparent: a CPU actor holding an `ActorRef` does not know
the peer is on the card. This section is about the *code*: how to keep an actor written
for the FPGA as close as possible to one written for the CPU, ideally the same source.

### 9.1 Where we are today

The same actor, written for each runtime on the branch (`examples/cpu_fpga/`):

**CPU (`actors/cpp`)**

```cpp
struct Ping : public actors::Message_N<301>
{
  uint32_t count = 0;
  Ping() = default;
  explicit Ping(uint32_t c) : count(c) {}
  KFPGA_FIELDS(count)
};

class CpuPong : public actors::Actor
{
public:
  CpuPong() { MESSAGE_HANDLER(Ping, on_ping); }
  uint32_t pings = 0;

private:
  void on_ping(const Ping *m)
  {
    ++pings;
    reply(new Pong(m->count + 1000 * pings));
  }
};
```

**FPGA (`actors/fpga`)**

```cpp
struct Ping
{
  static constexpr kfpga::MsgId id = 301;
  uint32_t count = 0;
  KFPGA_FIELDS(count)
};

struct FpgaPong
{
  static constexpr kfpga::ActorId kId = 2;
  uint32_t pings = 0;

  KFPGA_HANDLERS(KFPGA_ON(Ping, on_ping))

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

The logic is identical. Everything around it differs:

| | CPU | FPGA |
|---|---|---|
| message | class deriving `Message_N<301>`, heap-allocated with `new` | plain struct with `static id`, passed by value |
| message declared | once per side, by hand, kept in sync by hand | |
| actor | class deriving `actors::Actor` | plain struct with `kId` |
| handler table | `MESSAGE_HANDLER` in the constructor, at run time | `KFPGA_HANDLERS(...)`, a compile-time `switch` |
| handler | `void on_x(const X *m)` | `template <class Ctx> void on_x(const X &m, Ctx &ctx)` |
| send | `ref.send(new X(...), this)` | `ctx.send(dst_id, x)` |
| reply | `reply(new X(...))` | `ctx.reply(x)` |
| `fast_send` | `auto r = ref.fast_send(&x, this)` returns `unique_ptr<const Message>` | `bool ok = ctx.fast_send(dst, x, rep)` |
| addressing | `ActorRef` handed in at construction | numeric id, routed by a table fixed at build time |

Two copies of every message and two styles of every actor: that is what has to go.

One more thing has to go: on the FPGA today an actor is a plain struct with no base
class, and it sends **through its context, by id** (`ctx.send(dst_id, x)`,
`ctx.fast_send(dst, x, rep)`). That is not the actor model as Kaspar uses it. An actor
derives from an actor base class, sends through an **actor reference** with `this` as the
sender, and replies with `reply`. 9.2 does this on both runtimes.

### 9.2 The approach: portable actors

Keep two kinds of actor:

- **CPU actors.** Every existing Kaspar actor, unchanged. They can talk to FPGA actors
  through `ActorRef` (sections 4–8), and can use anything C++ offers.
- **Portable actors.** Written **once**, in the FPGA style, and run **unchanged** on
  either runtime. Where one runs is a deployment decision, like choosing a `Group` or a
  mailbox kind today.

The FPGA style is the one to standardize on, because it is the restrictive one: code that
synthesizes also compiles for the CPU, but not the other way round. The CPU runtime gets
an adapter that hosts a portable actor as an ordinary `actors::Actor`.

Four pieces make this work.

**1. One message definition.** A message is declared once and derives from
`kfpga::Message<id>`, the FPGA counterpart of `actors::Message_N<id>`: a base class that
carries the id, with no virtual functions and nothing on the heap, so it synthesizes. The id
is assigned by hand, as `Message_N` ids are, because the message crosses between the CPU
and the card and both sides must agree on the integer. The CPU class is derived from it,
not written by hand:

```cpp
// msgs.hpp: the only declaration
struct Ping : kfpga::Message<301>
{
  uint32_t count = 0;
  KFPGA_FIELDS(count)
};

// actors/cpp side, generic (written once in the framework, not per message)
namespace actors {
template <class P>
struct Wire : public Message_N<P::id>, public P
{
  Wire() = default;
  explicit Wire(const P &p) : P(p) {}
};
}

// CPU code that sends to a portable or FPGA actor
namespace cpu { using Ping = actors::Wire<msgs::Ping>; }
```

`Wire<P>` *is a* `P`, so a CPU handler receiving `const Wire<Ping> *` can pass it to
portable code as `const Ping &` with no copy, and the bridge encodes it with the same
`KFPGA_FIELDS` list. There is one source of truth for the id and the fields. For larger
sets of messages, generate `msgs.hpp` from a schema, as `actors/rust/interop/codegen/
generate.py` already does for Rust.

**2. Actors derive from a base class and send through references that hold streams.**
Actor code is written as on the CPU:

- an actor derives from `kfpga::Actor<Self>` (CRTP: no virtual functions, which HLS does
  not allow);
- handlers are ordinary member functions, `void on_x(const X &m)`;
- it holds a `kfpga::ActorRef<Id>` member for each actor it talks to, and sends with
  `ref.send(m, this)`, passing `this` as the sender. `ref.fast_send(req, rep, this)`
  exists only on a reference to an actor in the same group (1.1);
- it answers with `reply(m)`, from the base class.

There is no `Ctx` in actor code.

**On the FPGA, a reference holds the streams.** An `ActorRef` held by actor A and pointing
at actor B in another process stands for exactly one (sender, receiver) pair, so it holds
references to the two FIFOs between A and B:

```cpp
template <kfpga::ActorId Id>
struct ActorRef
{
  static constexpr kfpga::ActorId kId = Id;   // B's id: ids live in references

  hls::stream<Envelope> &msg;   // A -> B messages
  hls::stream<Envelope> &back;  // B -> A replies to A's messages

  template <class M, class Self>
  void send(const M &m, Self *self) { msg.write(pack(m, kId)); }

  // no fast_send: B is another process (1.1)
};
```

(Simplified: the real code reports errors, as `Ctx` does today.)

- **The FIFOs are declared once, in the HLS top**, because a FIFO has two ends in two
  processes and both must reach it. The top binds each reference to its two FIFOs and
  gives B's process the same two.
- **One writer per FIFO by construction.** Only A holds the reference, so A is the only
  writer of `msg` and B the only writer of `back`. This is what made
  `Ctx` necessary before; the reference now carries it.
- **B's mailbox** is the set of `msg` FIFOs from every reference that points at B. B's
  process merges them: its self-queue first, then messages, round-robin.
- **`reply`.** Before running a handler, the runtime records in the base class which
  reference's FIFOs the current message came in on, and `reply(m)` writes to that pair's
  `back`. For an inline `fast_send` from the owner, the reply is returned to the caller
  as a value.
- **Actors declare no id.** An actor's id lives in the references that point at it, as a
  CPU actor is known by its `ActorRef`. The target's id travels in each envelope (`dst`).
  The sender's id is not needed inside the chip, because a reply goes back on the pair of
  FIFOs the message came in on. For the host link and error reports, the top gives each
  actor process its id when it binds the design, and the runtime stamps it as `src`.
- **A reference to an actor in the same group** is bound by the top directly to that actor
  instead of to FIFOs, and its `fast_send` runs the target's handler inline (9.5). Actors
  never contain other actors; `KFPGA_INNER` is removed, because the group lists its
  members.
- **A reference to a CPU actor** is bound to the holding actor's FIFOs to and from the host
  link. Several such references held by one actor share those FIFOs; there is still one
  writing process.
- **Ids still travel in the envelope** (`dst`, `src`, `tag`), for the host link, for error
  reports and for the CPU side. Actor code never sends by id.

This assumes Vitis HLS accepts `hls::stream&` members in a struct bound in the top;
AMD's documentation neither shows nor forbids it, so the first synthesis run of the
reworked runtime confirms it.

Sketch of the top for Ping → Pong:

```cpp
void my_top(/* host streams */)
{
#pragma HLS DATAFLOW
  static hls::stream<Envelope> p2q_msg, q2p_back;
  static PingActor ping{{p2q_msg, q2p_back}};   // ping.pong bound
  static PongActor pong;
  ping_process(ping /*, Ping's inputs */);
  pong_process(pong, p2q_msg, q2p_back /*, Pong's other inputs */);
}
```

**On the CPU host**, the same source compiles against CPU implementations of the same two
types:

| call | on the FPGA | on the CPU host |
|---|---|---|
| `ref.send(m, this)` | one write into the reference's `msg` FIFO | `actors_ref.send(new Wire<M>(m), host)` |
| `ref.fast_send(req, rep, this)` | only within a group: the handler runs inline | only within a group: the handler runs inline |
| `reply(m)` | `back` of the pair the message came in on; to an inline caller, returned as a value | `host->reply(new Wire<M>(m))` |

where `host` is the `Portable` actor hosting this one (piece 4) and `actors_ref` is the
`actors::ActorRef` the reference was bound to.

**3. One addressing scheme: references bound at deployment.** On the FPGA, the HLS top
binds references to FIFOs; on the CPU, `Portable` binds them from a table that maps
id → `actors::ActorRef`. That is where "Risk runs on the card, Strategy runs on core 7" is
written down. The actor's code never says. The FPGA's `DiscoveryTable` remains only for the
host link, to deliver envelopes from the CPU to the right actor process.

**This replaces the current FPGA API.** Plain actor structs with handlers templated on
`Ctx`, `Ctx::send(ActorId, ...)`, `Ctx::fast_send(ActorId, ...)`, `Ctx::fast_send(b, ...)`,
actors held as members of other actors, `KFPGA_INNER`
and `ctx.reply` in `actor.hpp` are removed; `Links<NE>` with FIFOs for every pair of
endpoints becomes FIFOs declared per reference, so only pairs that talk get FIFOs. The
examples and tests move to the base class and references.

**4. A CPU host for portable actors.** `actors::Portable<Logic>` is an ordinary CPU actor
that owns a `Logic` (the portable actor), binds its references from the deployment table,
registers a handler for each message in `Logic`'s handler table, and calls the portable
handler:

```cpp
template <class Logic>
class Portable : public actors::Actor
{
public:
  Portable(const char *nm, const CpuDiscovery &rt) : logic(rt, this)   // binds logic's refs
  {
    std::strncpy(name, nm, sizeof(name) - 1);
    register_handlers(typename Logic::kfpga_messages{});   // one MESSAGE_HANDLER per type
  }
  Logic logic;

private:
  template <class M>
  void on(const actors::Wire<M> *m)
  {
    logic.kfpga_set_current(m->sender);                     // for reply()
    logic.kfpga_dispatch_typed(static_cast<const M &>(*m));
  }
};
```

This needs `KFPGA_HANDLERS` to also expose the list of message types it handles
(`kfpga_messages`) and a typed dispatch, so the CPU host can register them. The FPGA side
keeps its `switch`.

### 9.3 Example: Pong, written once

```cpp
// pong.hpp: compiled into the FPGA design AND into the CPU binary
struct Pong : kfpga::Actor<Pong>
{
  uint32_t pings = 0;

  KFPGA_HANDLERS(KFPGA_ON(msgs::Ping, on_ping))

  void on_ping(const msgs::Ping &m)
  {
    ++pings;
    msgs::Pong r;
    r.count = m.count + 1000 * pings;
    reply(r);
  }
};
```

On the card it is a process in the HLS top, with the references that point at it bound
to its FIFOs (piece 2). On the CPU it is
`new actors::Portable<Pong>("pong", table)`, added to the `Manager` like any
other actor. A CPU actor talking to it writes the same thing either way:

```cpp
auto *p = new cpu::Ping; p->count = 1;
pong_ref.send(p, this);   // pong_ref: an ActorRef, on the CPU or on the card;
                          // the Pong comes back as a message
```

(A CPU actor may `fast_send` a `Portable<Pong>` on the CPU, but not a Pong on the card,
so code meant to work with either uses `send`.)

### 9.4 Example: a pre-trade risk gate

A more realistic actor, and a natural one to move to the card: every order passes through
it, it keeps a little state, and it must be fast. The strategy sends it orders; it checks
position and rate limits, and forwards the order to the order gateway or rejects it.

```cpp
namespace msgs {
struct NewOrder : kfpga::Message<310>
{
  uint16_t instr = 0;     // index into the universe, not a symbol string
  uint8_t side = 0;       // 0 buy, 1 sell
  int32_t qty = 0;
  int64_t px = 0;         // price in ticks: no floating point in messages
  uint32_t client_id = 0;
  KFPGA_FIELDS(instr, side, qty, px, client_id)
};
struct Reject : kfpga::Message<311>
{
  uint32_t client_id = 0;
  uint8_t reason = 0;     // 1 position limit, 2 rate limit, 3 unknown instrument
  KFPGA_FIELDS(client_id, reason)
};
struct Fill : kfpga::Message<312>
{
  uint16_t instr = 0;
  uint8_t side = 0;
  int32_t qty = 0;
  KFPGA_FIELDS(instr, side, qty)
};
struct Tick : kfpga::Message<313>   // sent by a timer actor; on the card, by the runtime's cycle counter
{
  KFPGA_FIELDS()
};
}

struct RiskGate : kfpga::Actor<RiskGate>
{
  kfpga::ActorRef<kGateway> gateway;         // where accepted orders go
  static constexpr int kInstr = 16;          // fixed size: no heap, no std::vector
  static constexpr int kMaxPerTick = 50;

  int32_t pos[kInstr] = {};
  int32_t limit[kInstr] = {};               // set by a Limits message (not shown)
  int32_t sent_this_tick = 0;

  KFPGA_HANDLERS(KFPGA_ON(msgs::NewOrder, on_order)
                 KFPGA_ON(msgs::Fill, on_fill)
                 KFPGA_ON(msgs::Tick, on_tick))

  void on_order(const msgs::NewOrder &o)
  {
    if (o.instr >= kInstr)
      return reject(o, 3);
    const int32_t signed_qty = o.side == 0 ? o.qty : -o.qty;
    const int32_t after = pos[o.instr] + signed_qty;
    if (after > limit[o.instr] || after < -limit[o.instr])
      return reject(o, 1);
    if (sent_this_tick >= kMaxPerTick)
      return reject(o, 2);
    ++sent_this_tick;
    gateway.send(o, this);                   // to the gateway, wherever it runs
  }

  void on_fill(const msgs::Fill &f)
  {
    if (f.instr < kInstr)
      pos[f.instr] += f.side == 0 ? f.qty : -f.qty;
  }

  void on_tick(const msgs::Tick &) { sent_this_tick = 0; }

private:
  void reject(const msgs::NewOrder &o, uint8_t why)
  {
    msgs::Reject r;
    r.client_id = o.client_id;
    r.reason = why;
    reply(r);                                // to the strategy that sent the order
  }
};
```

The strategy, a CPU actor, does not change when the gate moves:

```cpp
void Strategy::on_book_update(const frame::ob::msg::EndOfBurst *eob)
{
  auto *o = new cpu::NewOrder;               // actors::Wire<msgs::NewOrder>
  o->instr = es_; o->side = 0; o->qty = 1; o->px = to_ticks(eob->best_bid);   // illustrative
  o->client_id = ++next_id_;
  risk_.send(o, this);                       // ActorRef: Portable<RiskGate> or the card
}

void Strategy::on_reject(const cpu::Reject *r) { /* ... */ }
```

Deployment, CPU only (backtest, paper):

```cpp
auto *risk = new actors::Portable<RiskGate>("risk", table);
table.put(kRisk, actors::ActorRef(risk));
table.put(kGateway, actors::ActorRef(som));
```

Deployment, risk on the card (live):

```cpp
table.put(kRisk, bridge->ref(kRisk));        // same id, now an FPGA ActorRef
// and in the HLS top: a RiskGate process, with the references to it bound to its FIFOs
```

The backtest then runs the **same** risk logic that the card runs live, which is the
whole point of "backtest == production".

### 9.5 A process is a `Group`

An FPGA process holding several actors has exactly the semantics of a CPU `Group`, and
this is the organizing idea for placing actors on the card:

| | CPU `Group` | FPGA process |
|---|---|---|
| members | several actors, one thread, one shared mailbox | several actors, one state machine |
| handlers | one at a time across all members, each to the end; no locks between members | the same: one state machine steps through one handler at a time |
| `send` between members | enqueued on the group's queue, handled after the current handler | enqueued on the process's own queue, handled on a later step |
| `fast_send` between members | inline on the same thread, reply as a value | inline in the same circuit, reply as a value |
| messages from outside | arrive in the shared mailbox, dispatched to the member | arrive on the process's input FIFOs, dispatched to the member |
| order | deterministic | deterministic |
| different groups / processes | run in parallel on different threads | run in parallel, every cycle |

So the deployment question "which actors share a `Group`" and "which actors share a
process" are the same question: actors that call each other synchronously and must not run
concurrently go together; actors that only exchange messages go apart and run in parallel.
It is also why `fast_send` stops at the process boundary (1.1): inside a group or process
the handler can run inline; across processes nothing can run another process's circuit.

**On the card, a group is declared in the HLS top, as on the CPU.** A CPU `Group` is
assembled at deployment with `grp.add(...)`; actors never contain each other. The FPGA
runtime gets the same thing, `kfpga::Group<A, B, ...>` (new; the current runtime has no
group type, and puts actors in one process only by having one actor contain the others,
with `KFPGA_INNER`). A group:

- runs as one process: one state machine, one handler at a time across its members;
- lists its members, so the process knows whom to dispatch to; no `KFPGA_INNER`;
- has its own queue for `send` between members;
- binds references between its members directly to the target actor, so `fast_send` on
  them runs inline. References to actors outside the group are bound to FIFOs and have no
  `fast_send`.

Example: a quote engine that checks every quote with the risk gate before it goes out.
The actors are written independently:

```cpp
struct QuoteEngine : kfpga::Actor<QuoteEngine>   // BookTop, make_quote() not shown
{
  kfpga::ActorRef<kRisk> risk;

  KFPGA_HANDLERS(KFPGA_ON(msgs::BookTop, on_top))

  void on_top(const msgs::BookTop &t)
  {
    msgs::NewOrder o = make_quote(t);
    msgs::Reject rej;
    if (risk.fast_send(o, rej, this))        // RiskGate::on_order runs now, inline
      return;                                // rejected
    // accepted: RiskGate already forwarded it to the gateway
  }
};
```

and deployment puts them in one group:

```cpp
// HLS top
static QuoteEngine quotes;
static RiskGate risk;
static kfpga::Group<QuoteEngine, RiskGate> g(quotes, risk);   // one process

// CPU
actors::Group grp("quoting");
grp.add(new actors::Portable<QuoteEngine>("quotes", table));
grp.add(new actors::Portable<RiskGate>("risk", table));
```

`QuoteEngine` does not say where `RiskGate` runs. If deployment moved `RiskGate` to another
group, the FPGA build would no longer compile, and the `fast_send` would have to become a
`send`. (On the CPU, `fast_send` across groups still works; portable code should not rely
on it.)

### 9.6 The rules for portable code

A portable actor follows the FPGA rules (guide, section 4) everywhere, including on the
CPU:

- no heap: fixed-size members and arrays, no `new`, `std::vector`, `std::string`;
- every loop has a compile-time bound;
- no recursion, virtual functions, function pointers or exceptions;
- messages carry integers only: prices in ticks, sizes as integers, instruments as
  indices, never symbol strings;
- C++14, the dialect Vitis HLS accepts;
- no I/O: no logging, no files. To log, send a message to a CPU logging actor.

### 9.7 How we make sure the code stays the same

Writing it once is not enough; we need to know the two builds behave the same.

1. **One source file, two builds, in CI.** Every portable actor is compiled for the CPU
   host and checked as C++14 for the FPGA (`make check14`), and periodically run through
   Vitis HLS C simulation. Code that breaks either build fails CI.
2. **Compile-time checks.** `static_assert`s on message structs (trivially copyable,
   integer fields only, at most 12 words) and on actor structs (no virtual functions,
   trivially destructible). These catch most rule violations in the CPU build already.
3. **One test suite, three runtimes.** The same tests (send messages in, check the
   replies and the state) run against:
   - `Portable<A>` on the CPU runtime;
   - the FPGA runtime stepped in software (`Design::step`, deterministic);
   - the FPGA runtime on `SoftCard` behind the bridge (section 5).

   A differential test then feeds the same recorded message stream (for example, orders
   generated from a PCAP replay) through the CPU host and through the FPGA model, and
   requires identical outputs, message for message.
4. **Semantics written down once.** The extended actor model paper defines `send`,
   `fast_send` and `reply`; both runtimes are tested against it. Known differences are
   listed in 9.8 and nowhere else.

### 9.8 What will stay different

These cannot be made identical, and are kept as small and explicit as possible:

| difference | why | how it is contained |
|---|---|---|
| deployment code (`Manager`, `Group` vs the HLS top and discovery table) | hardware is fixed at build time | lives only in the deployment files, never in actor code |
| lifecycle (`Start`, shutdown) | the card has no `Manager` | the FPGA runtime sends `Start` to each actor at reset, so handlers see the same message |
| timers | the card has no OS timer | a `Tick` message: from the timer actor on the CPU, from a cycle-counter process on the card |
| multiple replies to a `fast_send` | the FPGA keeps the first; the CPU behaviour has to be checked in `Actor::reply` | make both keep the first, and test it |
| a full FIFO (section 11, item 1) | hardware FIFOs are bounded; CPU mailboxes grow | sizing rules per design; reported, never silent |
| CPU-only actors | they use the heap, strings, logging, the OS | they stay CPU actors; they still talk to portable and FPGA actors through `ActorRef` |
| floating point | not in messages; avoid in portable logic | fixed-point integer types shared by both sides |

## 10. What carries over from the branch

| part | fate |
|---|---|
| `envelope.hpp`, `actor.hpp`, `links.hpp` (actor-to-actor), `stream.hpp` | keep |
| FPGA runtime tests (19) | keep |
| `FpgaActorRef` / `FpgaLink` in `ActorRef.hpp` | keep, same interface |
| `FpgaBridge`: codecs, `Proxy` stand-ins, error reporting, orderly `stop()` | keep, adapted |
| `FpgaBridge`: reader thread, worker threads, `pending_` map, condition variables | **remove** |
| `SoftCard` | rework into a software model of the host-memory rings, so all of this is testable without hardware |
| bridge tests (16) | keep the `send` scenarios; drop the CPU ↔ FPGA `fast_send` ones (1.1) |
| extended actor model paper | keep; add 1.1: `fast_send` only where the handler can run inline |

## 11. Open questions

1. **Full rings.** A CPU sender facing a full request ring spins (as a `LockFreeMPSC`
   producer does). A card `host_out` facing a full CPU lane stalls, and that blocks every
   other destination behind it (head-of-line blocking). Options: per-lane credits, so
   `host_out` skips a full lane, or a dedicated `host_out` per lane. To decide.
2. **Windows and DMA on the XCUX35.** The starting point is per-thread write-combining
   windows in a prefetchable BAR (2.6). To settle: how many windows, so how large a BAR;
   which AMD DMA IP (QDMA, XDMA) Vivado supports on the XCUX35 for the card → host rings,
   or whether to write our own; and confirming on the card that one AVX-512 store leaves
   as one 64-byte PCIe write.
3. **Id space.** `kMaxActors = 16` is shared by FPGA and CPU actors today. CPU actors
   addressed by the card need a separate, larger id space (it only has to map to lanes).
4. **Interrupt mode.** MSI-X → `eventfd` needs driver support; it may come later than the
   spinning mode.
5. **Write-combining under VFIO.** Mapping the envelope windows through sysfs
   `resourceN_wc` while the device is bound to `vfio-pci` has to be tried on the card;
   if it does not work, find another way to get a write-combining mapping of that BAR.

## 12. Plan

1. **Loopback prototype.** Software rings in shared memory, with a thread standing in for
   the card. Implement 5.1 and 5.3 and the `FpgaLanes` mailbox. Benchmark round-trip latency
   against today's `FpgaBridge` on the same loopback, same machine, pinned. This tells us
   how much the routing threads actually cost before any hardware work.
2. **Semantics tests.** Port the `send` bridge scenarios to the new transport. Add tests
   for FIFO order per sender across lanes, request/reply as two messages in both
   directions, and that `fast_send` to an FPGA actor is rejected.
3. **Card side.** Rework `host_in` / `host_out` for the windows and rings in the software
   model, then synthesize for the X3522PV's XCUX35 (XCUX35-3VSVA1365E).
4. **Real transport.** A `CardTransport` for the X3522PV over `vfio-pci`: the PCIe endpoint,
   envelope windows and DMA engine in the design (2.6), then measure on hardware.
5. **Portable actors (section 9).** `kfpga::Group<...>` declared in the HLS top, the
   `kfpga::Actor<Self>` base class and
   `kfpga::ActorRef<Id>` holding stream references, replacing `Ctx` and `Links<NE>`
   (confirm in the first synthesis run that Vitis HLS accepts the `hls::stream&` members);
   CPU implementations of both, `actors::Wire<P>`, `actors::Portable<Logic>`,
   the CPU discovery table, and the `KFPGA_HANDLERS` type list. Port Pong, then write
   `RiskGate`, with one test suite run on all three runtimes.
6. **Differential test.** Replay a recorded order stream through `Portable<RiskGate>` and
   through the FPGA model, and require identical output.
