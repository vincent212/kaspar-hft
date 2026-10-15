# FPGA bridge without routing threads

**Status:** design proposal, 2026-10-15. Nothing here is implemented yet.
**Applies to:** `feature/fpga-runtime` (`actors/fpga/`), plus one new mailbox kind in `actors/cpp`.

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

Non-goals: changing the FPGA runtime's internals (`actor.hpp`, `links.hpp`), the envelope
format, or the actor-code API. Those are handled separately.

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
an estimate; it has to be measured (section 11).

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

## 9. What carries over from the branch

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

## 10. Open questions

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

## 11. Plan

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
