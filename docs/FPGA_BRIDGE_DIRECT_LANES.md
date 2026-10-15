# FPGA bridge without routing threads

**Status:** design proposal, 2026-10-15. Nothing here is implemented yet.
**Applies to:** `feature/fpga-runtime` (`actors/fpga/`), plus a new mailbox kind and a host for portable actors in `actors/cpp`.

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
| CPU actor `fast_send` to FPGA actor | the same write, then spin on the thread's reply slot in host memory, which the card fills by DMA |
| FPGA actor `send` to CPU actor | DMA write into a lane ring of the CPU actor's thread, polled by that thread |
| FPGA actor `fast_send` to CPU actor | DMA write into that thread's priority lane; the reply goes back like a CPU `send` |
| waking a CPU actor that does not spin | MSI-X interrupt → `eventfd` that the actor's own thread waits on |

Sections 4–8 make this concrete.

### 2.6 Variant: Solarflare NICs with Onload (ef_vi)

This machine has an AMD (Xilinx) Solarflare **XtremeScale SFC9250** (the X2 family) with
**Onload 8.1.3**, which Kaspar already uses for MDP3 market data. The SFC9250 is a NIC
ASIC, not an FPGA, so the FPGA is a separate card (for example the Alveo UL3524 the branch
targets). That gives two ways to connect CPU actors to FPGA actors:

- **Over PCIe**, as in sections 2.1–2.5: a DMA path to the FPGA card that we write and
  maintain ourselves.
- **Over Ethernet through the Solarflare NIC**: cable an FPGA card port to a Solarflare
  port and carry actor messages as Ethernet frames. The CPU side then uses Solarflare's
  existing, hardened kernel-bypass stack instead of a custom DMA driver.

This section explains the second option.

#### Use ef_vi, not Onload sockets

The Onload package contains three layers:

| layer | what it is | use here |
|---|---|---|
| **Onload** | transparent acceleration of ordinary sockets (`onload ./kaspr`) | no: socket semantics, UDP/TCP processing and per-call overhead we do not need |
| **TCPDirect** | a slimmer zero-copy TCP/UDP API | no: still a protocol stack |
| **ef_vi** | the raw layer: user-space access to the NIC's own queues | **yes** |

**ef_vi** gives a process a *virtual interface* (VI): its own receive ring, transmit ring
and event queue, mapped into user space, with no kernel on the data path. It is section
2.4's best practices already built and supported: rings in pinned host memory, polled from
user space, kernel bypass, one owner per VI. The headers are installed in
`/usr/include/etherfabric/` (`ef_vi.h`, `pio.h`, `efct_vi.h`).

#### The mechanisms, mapped

| primer concept (2.2) | ef_vi equivalent |
|---|---|
| write-combined MMIO push of a small message | **CTPIO** (cut-through PIO, `EF_VI_TX_CTPIO`): the CPU writes the frame straight into the NIC through a write-combined window, and the NIC starts sending it before the whole frame has arrived. The lowest-latency transmit path on X2 |
| DMA ring in host memory, polled | the VI's **receive ring and event queue**: the NIC writes the frame into a host buffer and posts an event; the thread polls `ef_eventq_poll()` |
| one ring per writer | **one VI per CPU thread**: each thread transmits on its own VI and receives on its own VI |
| choosing which ring receives | **hardware filters** (`ef_filter_spec`) steer a frame to a VI by MAC address, VLAN, or IP/port |
| interrupt wake-up | ef_vi can arm the event queue for an interrupt and block on it (`ef_eventq_wait`), for threads that do not spin |

#### The four paths over Ethernet

An actor envelope is about 60 bytes. With a 14-byte Ethernet header it is a 74-byte
frame, above Ethernet's 60-byte minimum, so one envelope is one frame. The FPGA side needs
a low-latency Ethernet MAC on the card; its `host_in` / `host_out` processes parse and
build these frames instead of PCIe transactions.

- **CPU → FPGA `send`:** the thread builds the frame on its stack and sends it with CTPIO
  on its own VI. No lock: the VI belongs to the thread.
- **CPU → FPGA `fast_send`:** the same CTPIO send, then the thread spins on its own VI's
  event queue for the reply. The FPGA addresses the reply to that thread, for example with
  a destination MAC per thread; a filter steers it to the thread's VI. The thread's VI is
  its reply slot.
- **FPGA → CPU `send`:** the FPGA sends a frame addressed to the receiving actor's thread;
  a filter steers it to that thread's lane VI. The `FpgaLanes` mailbox polls that VI's
  event queue.
- **FPGA → CPU `fast_send`:** the same, addressed to the thread's *priority* VI (a second
  VI with its own filter), which the drain loop polls first. The reply goes back by CTPIO.

The design in sections 4–8 is unchanged. Only the transport under the rings is different:
a "lane" is a VI, and a "ring write" is a CTPIO send or a NIC receive.

#### What Ethernet changes, and how to handle it

1. **Ethernet drops; PCIe does not.** When a VI's receive ring has no free buffers, the
   NIC **drops** the frame. The actor model forbids silent loss. Fix: **credit-based flow
   control.** The FPGA may send to a lane only while it holds credits for it; the CPU
   thread returns credits (in batches, piggy-backed on its own traffic) as it refills the
   receive ring. In the other direction, the FPGA's receive FIFOs need the same
   protection.
2. **Bit errors.** A frame with a bad checksum is dropped by the MAC. On a short direct
   cable this is rare, but it is not impossible. Fix: a **sequence number per lane** in
   the envelope header. A gap is detected and reported loudly; a lost `fast_send` request
   or reply is answered with an error, never left waiting forever.
3. **Ordering.** Frames on one link and one VI arrive in order, so per-sender FIFO order
   holds, as long as each (sender, receiver) pair uses one VI and one link.
4. **No switch.** Cable the FPGA directly to the NIC. A switch adds latency and jitter
   and another place to drop frames.
5. **Keep market data separate.** Kaspar's MDP3 feed already runs on this NIC through
   Onload. Put the FPGA link on its own port, and on its own VIs and cores, so actor
   traffic never queues behind market data.
6. **NUMA.** Pin the threads that own these VIs to the NIC's NUMA node, as in 2.4 (7).

#### PCIe or Ethernet?

| | PCIe DMA to the FPGA card | Ethernet via Solarflare + ef_vi |
|---|---|---|
| CPU-side code | our own driver: rings, doorbells, VFIO, the card's DMA IP | Solarflare's supported stack; we write only framing and credits |
| loss | none (the link is lossless) | possible: credits and sequence numbers needed |
| latency | one PCIe crossing each way | one PCIe crossing to the NIC, plus the two MACs and the wire; CTPIO keeps the NIC part small |
| FPGA side | PCIe DMA engine and host interface | an Ethernet MAC, which a trading FPGA usually has anyway |
| bonus | — | the FPGA can sit on the same network as market data and orders |

Which one is faster has to be measured. The Ethernet path costs extra MAC and wire hops
but saves writing and debugging a PCIe DMA path, and it fits how trading FPGAs are usually
built. A sensible order is to prototype over Ethernet with ef_vi first, since the CPU side
already exists on this machine, and keep the PCIe path as the alternative.

(The ef_vi library also has an extension API, `smartnic_exts.h`, for AMD NICs that carry
an FPGA region of their own. The SFC9250 does not have one; check this API if the hardware
moves to such a card.)

## 3. Goals

1. **No routing threads.** Every crossing is written by the thread that has the data and
   read by the thread that will use it.
2. **Same semantics as the CPU runtime.** One message at a time per actor, isolation,
   per-sender FIFO order, `fast_send` served ahead of the mailbox and never interrupting a
   running handler, deadlock only on a genuine `fast_send` cycle.
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
CPU thread T ──(T's request ring)─────────────► card host-in ──► FPGA actor
CPU thread T ◄──(T's reply slot)─────────────── card host-out ◄── FPGA actor   fast_send reply
FPGA actor ──► card host-out ──(T's lane ring)──► CPU thread T's mailbox       send / fast_send request
```

An envelope is about 60 bytes (`envelope.hpp`: 12-byte header, 12 × 4-byte payload), so
it fits in one 64-byte cache line. Each crossing is one cache-line write by the sender
and one cache-line read by the receiver.

**Rings belong to threads, not to actors.** A `Group` runs several actors on one thread,
so the group's thread owns the rings and serves all its members. A stand-alone actor owns
its own.

## 5. The four paths

### 5.1 CPU → FPGA `send`

Done entirely on the caller's thread inside `FpgaActorRef::send`:

1. encode the message into an envelope on the stack (fixed size, no allocation);
2. write it into **this thread's** request ring to the card;
3. ring the card's doorbell (or, on cards that support it, do a single 64-byte
   write-combined store into a per-thread register window).

Each sending thread has its own ring, so producers need no lock or atomic claim.
Per-sender FIFO order holds: an actor always sends from the same thread, so its messages
to a given FPGA actor go through one ring in order.

### 5.2 CPU → FPGA `fast_send`

1. Encode the request; its `tag` names **this thread's reply slot**.
2. Write it into this thread's request ring.
3. **Spin on the reply slot**: a cache line in host memory with a sequence number. The
   card's host-out process writes the reply envelope into the slot named by the tag,
   then bumps the sequence number.
4. Decode the reply onto the caller's stack and return it, as a CPU `fast_send` does.

A thread can have only one `fast_send` outstanding (it is blocked in it), so **one slot per
thread** is enough. There is no `pending_` map, no mutex and no condition variable.

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

### 5.4 FPGA → CPU `fast_send`

The card writes the request into a **priority lane** of the CPU actor's thread. The
thread's drain loop serves the priority lane before anything else, runs the handler,
and writes the reply into its request ring tagged as a `FAST_REPLY`. The card's host-in
puts that on the `from_host_reply` stream, which already exists, so a reply never waits
behind ordinary traffic.

**Same semantics as CPU `fast_send`:**
- it is served ahead of the mailbox;
- it never interrupts a running handler, because the drain loop checks the priority lane
  only between messages;
- the caller is blocked until it gets the reply (here the caller is an FPGA process, and
  its stall is local to that process).

**One unavoidable difference:** on the CPU, `fast_send` runs the handler on the
*caller's* thread. Here the caller is hardware and has no CPU thread, so the handler runs
on the *callee's* thread. Handler code and its guarantees do not change: the actor's
state is still touched by one thread at a time. To keep that true when other CPU threads
`fast_send` the same actor concurrently, the drain loop serves the request under the same
per-actor `fast_send_mutex` that `Actor::fast_send` takes (this needs checking against
the exact locking in `Actor.cpp`).

**The deadlock that the worker threads guarded against is gone.** If the CPU handler
`fast_send`s back to the FPGA, it does what 5.2 says: it writes its request and spins on
its own reply slot. Nobody else has to deliver that reply. Only a genuine cycle (FPGA
actor A → CPU actor B → FPGA actor A) deadlocks, exactly as a `fast_send` cycle does
inside the CPU runtime.

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
- one or more **external lanes**: host-memory rings the card writes into;
- one **priority lane** for FPGA `fast_send` requests.

Its drain loop, in order:
1. the priority lane (FPGA `fast_send` requests);
2. the external lanes, round-robin;
3. the CPU ring.

It has **no condition variable**: the card cannot signal one. See section 7.

A `Group` that hosts FPGA-facing actors uses the same lane set on the group thread.

`ActorRef`/`FpgaActorRef` keep their current shape (the `FpgaLink` interface added on the
branch). Only the implementation behind `FpgaLink::send` and `FpgaLink::fast_send`
changes: from "write, and let the reader route" to sections 5.1 and 5.2.

## 7. The cost: who spins

The card cannot wake a sleeping thread through a condition variable. So **a CPU thread
that receives from the card has to poll its lanes**, like `LockFreeMPSCSpin` today:

- **Latency-critical actors** (anything on the FPGA's synchronous path): spinning
  `FpgaLanes`, pinned to an isolated core. This is normal HFT practice and the same
  tradeoff the framework already offers.
- **Everything else** (logging, risk reports, monitoring): `FpgaLanes` with **interrupt
  wake-up**. The card raises an MSI-X interrupt, mapped to an `eventfd` that the actor's
  own thread waits on. This is slower (microseconds), but there is still no routing
  thread: the wake goes straight to the thread that will handle the message.

The choice is per actor or per group, at deployment, like choosing a mailbox kind.

**Expected result:** a `fast_send` round trip in either direction comes down to roughly
the PCIe round trip itself, plus a handler, instead of PCIe plus thread wake-ups. This is
an estimate; it has to be measured (section 12).

## 8. Changes on the card side

The host link processes in `links.hpp` change from "one stream in, one stream out" to
rings:

| process | today | proposed |
|---|---|---|
| `host_in` | reads `from_host` | reads the per-thread request rings, round-robin, via descriptor/doorbell |
| `host_in_reply` | reads `from_host_reply` | unchanged in role: CPU replies to FPGA `fast_send`s, kept separate so they never queue behind messages |
| `host_out` | writes `to_pcie` | writes each envelope to its destination: a CPU thread's lane, its priority lane, or a CPU caller's reply slot (from the tag) |

The discovery table gains a host-side map: CPU actor id → (lane ring, priority ring).
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

**1. One message definition.** A message is declared once, as the plain struct the FPGA
already uses. The CPU class is derived from it, not written by hand:

```cpp
// msgs.hpp: the only declaration
struct Ping
{
  static constexpr kfpga::MsgId id = 301;
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

**2. One handler body.** Handlers are templates on a `Ctx`, and use only the `Ctx` API:

| call | on the FPGA (`kfpga::Ctx`) | on the CPU (`CpuCtx`, new) |
|---|---|---|
| `ctx.send(dst, m)` | write into the FIFO to `dst` | `ref(dst).send(new Wire<M>(m), &actor)` |
| `ctx.reply(m)` | reply FIFO, or held for a `fast_send` caller | `actor.reply(new Wire<M>(m))`, which already handles both cases |
| `ctx.fast_send(dst, req, rep)` | request FIFO, wait for the reply | `ref(dst).fast_send(&wire, &actor)`, check the reply id, copy into `rep` |
| `ctx.fast_send(b, req, rep)` | `b` is an actor in the same process: its handler runs inline | the same: `b` is a member, its handler runs inline |
| `ctx.sender()`, `ctx.self()` | actor ids | actor ids, from the CPU table below |

**3. One addressing scheme.** Portable actors address peers by **actor id**, on both
sides. On the CPU, a deployment table maps id → `ActorRef`, the same idea as the FPGA's
`DiscoveryTable`. The table is where "Risk runs on the card, Strategy runs on core 7" is
written down. The actor's code never says.

**4. A CPU host for portable actors.** `actors::Portable<Logic>` is an ordinary CPU actor
that owns a `Logic` (the portable struct), registers a handler for each message in
`Logic`'s handler table, and calls the portable handler with a `CpuCtx`:

```cpp
template <class Logic>
class Portable : public actors::Actor
{
public:
  Portable(const char *nm, const CpuDiscovery &rt) : rt_(rt)
  {
    std::strncpy(name, nm, sizeof(name) - 1);
    register_handlers(typename Logic::kfpga_messages{});   // one MESSAGE_HANDLER per type
  }
  Logic logic;

private:
  template <class M>
  void on(const actors::Wire<M> *m)
  {
    CpuCtx ctx(*this, rt_, m->sender);
    logic.kfpga_dispatch_typed(static_cast<const M &>(*m), ctx);
  }
  const CpuDiscovery &rt_;
};
```

This needs one change to the FPGA runtime: `KFPGA_HANDLERS` must also expose the list of
message types it handles (`kfpga_messages`) and a typed dispatch, so the CPU host can
register them. The FPGA side keeps its `switch`.

### 9.3 Example: Pong, written once

```cpp
// pong.hpp: compiled into the FPGA design AND into the CPU binary
template <kfpga::ActorId Id>
struct Pong
{
  static constexpr kfpga::ActorId kId = Id;
  uint32_t pings = 0;

  KFPGA_HANDLERS(KFPGA_ON(msgs::Ping, on_ping))

  template <class Ctx>
  void on_ping(const msgs::Ping &m, Ctx &ctx)
  {
    ++pings;
    msgs::Pong r;
    r.count = m.count + 1000 * pings;
    ctx.reply(r);
  }
};
```

On the card it is `actor_proc<Pong<kPong>, 1>(links)`. On the CPU it is
`new actors::Portable<Pong<kPong>>("pong", table)`, added to the `Manager` like any
other actor. A CPU actor calling it writes the same thing either way:

```cpp
cpu::Ping p; p.count = 1;
auto r = pong_ref.fast_send(&p, this);   // pong_ref: an ActorRef, local or FPGA
```

### 9.4 Example: a pre-trade risk gate

A more realistic actor, and a natural one to move to the card: every order passes through
it, it keeps a little state, and it must be fast. The strategy sends it orders; it checks
position and rate limits, and forwards the order to the order gateway or rejects it.

```cpp
namespace msgs {
struct NewOrder
{
  static constexpr kfpga::MsgId id = 310;
  uint16_t instr = 0;     // index into the universe, not a symbol string
  uint8_t side = 0;       // 0 buy, 1 sell
  int32_t qty = 0;
  int64_t px = 0;         // price in ticks: no floating point in messages
  uint32_t client_id = 0;
  KFPGA_FIELDS(instr, side, qty, px, client_id)
};
struct Reject
{
  static constexpr kfpga::MsgId id = 311;
  uint32_t client_id = 0;
  uint8_t reason = 0;     // 1 position limit, 2 rate limit, 3 unknown instrument
  KFPGA_FIELDS(client_id, reason)
};
struct Fill
{
  static constexpr kfpga::MsgId id = 312;
  uint16_t instr = 0;
  uint8_t side = 0;
  int32_t qty = 0;
  KFPGA_FIELDS(instr, side, qty)
};
struct Tick   // sent by a timer actor; on the card, by the runtime's cycle counter
{
  static constexpr kfpga::MsgId id = 313;
  KFPGA_FIELDS()
};
}

template <kfpga::ActorId Id, kfpga::ActorId Gateway>
struct RiskGate
{
  static constexpr kfpga::ActorId kId = Id;
  static constexpr int kInstr = 16;          // fixed size: no heap, no std::vector
  static constexpr int kMaxPerTick = 50;

  int32_t pos[kInstr] = {};
  int32_t limit[kInstr] = {};               // set by a Limits message (not shown)
  int32_t sent_this_tick = 0;

  KFPGA_HANDLERS(KFPGA_ON(msgs::NewOrder, on_order)
                 KFPGA_ON(msgs::Fill, on_fill)
                 KFPGA_ON(msgs::Tick, on_tick))

  template <class Ctx>
  void on_order(const msgs::NewOrder &o, Ctx &ctx)
  {
    if (o.instr >= kInstr)
      return reject(o, 3, ctx);
    const int32_t signed_qty = o.side == 0 ? o.qty : -o.qty;
    const int32_t after = pos[o.instr] + signed_qty;
    if (after > limit[o.instr] || after < -limit[o.instr])
      return reject(o, 1, ctx);
    if (sent_this_tick >= kMaxPerTick)
      return reject(o, 2, ctx);
    ++sent_this_tick;
    ctx.send(Gateway, o);                    // to the gateway, wherever it runs
  }

  template <class Ctx>
  void on_fill(const msgs::Fill &f, Ctx &)
  {
    if (f.instr < kInstr)
      pos[f.instr] += f.side == 0 ? f.qty : -f.qty;
  }

  template <class Ctx>
  void on_tick(const msgs::Tick &, Ctx &) { sent_this_tick = 0; }

private:
  template <class Ctx>
  void reject(const msgs::NewOrder &o, uint8_t why, Ctx &ctx)
  {
    msgs::Reject r;
    r.client_id = o.client_id;
    r.reason = why;
    ctx.reply(r);                            // to the strategy that sent the order
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
auto *risk = new actors::Portable<RiskGate<kRisk, kGateway>>("risk", table);
table.put(kRisk, actors::ActorRef(risk));
table.put(kGateway, actors::ActorRef(som));
```

Deployment, risk on the card (live):

```cpp
table.put(kRisk, bridge->ref(kRisk));        // same id, now an FPGA ActorRef
// and in the HLS top: actor_proc<RiskGate<kRisk, kGateway>, 0>(links);
```

The backtest then runs the **same** risk logic that the card runs live, which is the
whole point of "backtest == production".

### 9.5 Example: actors in one process (the `Group` equivalent)

On the CPU, actors in one `Group` share a thread and `fast_send` each other cheaply. On
the card, the equivalent is an actor held as a member of another, called with the inline
`fast_send(b, ...)`. The same code works on both, because `CpuCtx` implements the inline
form the same way:

```cpp
template <kfpga::ActorId Id, kfpga::ActorId Gateway>
struct QuoteEngine   // BookTop and make_quote() not shown
{
  static constexpr kfpga::ActorId kId = Id;
  RiskGate<Id + 1, Gateway> risk;            // inside this actor's process / thread

  KFPGA_HANDLERS(KFPGA_ON(msgs::BookTop, on_top))
  KFPGA_INNER(risk)

  template <class Ctx>
  void on_top(const msgs::BookTop &t, Ctx &ctx)
  {
    msgs::NewOrder o = make_quote(t);
    msgs::Reject rej;
    if (ctx.fast_send(risk, o, rej))         // risk.on_order runs now, inline
      return;                                // rejected
    // accepted: RiskGate already forwarded it to the gateway
  }
};
```

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
| bridge tests (16) | keep the scenarios; they must pass against the new transport |
| extended actor model paper | keep; add a note on the callee-thread `fast_send` in 5.4 |

## 11. Open questions

1. **Full rings.** A CPU sender facing a full request ring spins (as a `LockFreeMPSC`
   producer does). A card `host_out` facing a full CPU lane stalls, and that blocks every
   other destination behind it (head-of-line blocking). Options: per-lane credits, so
   `host_out` skips a full lane, or a dedicated `host_out` per lane. To decide.
2. **How the card reads N request rings.** Per-thread doorbells with a DMA descriptor
   engine (e.g. QDMA on the Alveo UL3524), or per-thread write-combined MMIO windows.
   This depends on the card and needs a look at the vendor's DMA IP.
3. **Id space.** `kMaxActors = 16` is shared by FPGA and CPU actors today. CPU actors
   addressed by the card need a separate, larger id space (it only has to map to lanes).
4. **Locking for 5.4.** Confirm how the drain loop and `Actor::fast_send` serialize today,
   so serving FPGA requests on the callee's thread keeps isolation.
5. **Interrupt mode.** MSI-X → `eventfd` needs driver support; it may come later than the
   spinning mode.
6. **PCIe or Ethernet transport.** Section 2.6: measure an ef_vi/CTPIO loop through the
   SFC9250 against a PCIe DMA path to the FPGA card before committing to either. Check how
   many VIs and filters the SFC9250 gives one process, since the Ethernet variant uses up to
   three VIs per CPU thread (transmit, lane, priority lane).

## 12. Plan

1. **Loopback prototype.** Software rings in shared memory, with a thread standing in for
   the card. Implement 5.1–5.4 and the `FpgaLanes` mailbox. Benchmark round-trip latency
   against today's `FpgaBridge` on the same loopback, same machine, pinned. This tells us
   how much the routing threads actually cost before any hardware work.
2. **Semantics tests.** Port the 16 bridge scenarios to the new transport. Add tests for
   the cases this design changes: FPGA `fast_send` served on the callee's thread while CPU
   threads also `fast_send` it; a CPU handler calling back into the FPGA during an FPGA
   `fast_send`; FIFO order per sender across lanes.
3. **Card side.** Rework `host_in` / `host_out` for rings in the software model, then
   synthesize.
4. **Real transport.** A `CardTransport` for the card's DMA, then measure on hardware.
5. **Portable actors (section 9).** `actors::Wire<P>`, `CpuCtx`, `actors::Portable<Logic>`,
   the CPU discovery table, and the `KFPGA_HANDLERS` type list. Port Pong, then write
   `RiskGate`, with one test suite run on all three runtimes.
6. **Differential test.** Replay a recorded order stream through `Portable<RiskGate>` and
   through the FPGA model, and require identical output.
