# Plan: the MDP3 handler on the Alveo X3522PV, with FPGA actors

The CME MDP3 market-data path, moved onto the FPGA of an Alveo X3522PV as FPGA
actors (see [HLS.md](HLS.md)): frames in, decoded and filtered events out to a CPU
actor that builds the books. Built and proven in simulation on recorded CME data;
not yet on a card. See "Status".

## Decisions

```
MAC (channel A) -> FeedRx -> SbeDecoder -> Filter -> Batcher -> CPU: FpgaFeed -> strategy
                                             ^                        |
                                             +---- table, at start ---+
```

- **The card decodes, filters and packs; the CPU builds the books.** One book per
  security id means many books (each product has a front and a back month, SOFR
  and CL many more), and the strategy reads across PCIe either way, so a book on the
  card pays only once the trigger is on the card too. BookBank
  (`mdp3/include/mdp3fpga/book.hpp`) stays as the design for then.
- **Everything on the card is an actor.**
- **The card joins the multicast group itself.** The port belongs to the FPGA, so the
  kernel cannot send the IGMP report for it; the Igmp actor does (IGMPv2), at the
  CPU's JoinGroup, and answers the switch's queries (`mdp3/ACTORS.md`).
- **Only the card decodes.** Raw SBE never reaches the CPU.
- **Channel A only.** No A/B arbitration.
- **Books are built from MBP**, CME's market-by-price entries (template 46), not
  from MBO: outright prices, 10 levels a side.
- **Implied entries are ignored**, in the books and in the events. They are most
  of the message traffic (81% of channel 490's book entries, levels 1 and 2 only),
  so dropping them lightens everything downstream.
- **No recovery.** No snapshot feed, no reseeding. Every packet's End line carries
  its sequence number, and FpgaFeed checks it and keeps a gap flag (see "Known
  limitations").
- **One update per packet.** The strategy gets the updated book and every event
  and trade of the packet together, in one call. CME applies a packet as one
  event, so the book at the end of a packet is consistent; mid-packet states are
  not shown. Lines may cross PCIe as they fill: FpgaFeed holds them until the End
  line.
- **The line is the minimum unit.** Batcher packs events to 16 bytes, three to a
  64-byte line. **The CPU gets the end of every packet**, even one with no event for
  it: an End line holds 0 to 2 events and the sequence number. A packet with no
  events only advances the sequence check.
- **The host ring is the buffer.** The card DMA-writes lines into host memory and
  FpgaFeed spins on it (the FpgaLanes mailbox).
- **No kernel on the receive path.** No socket and no Onload for market data; the
  kernel only maps the memory at start. Nothing sleeps and nothing is woken.
- **The ring cannot fall behind silently.** It is sized for the worst burst
  recorded, and the card sets a flag if it overflows. Not built yet.
- **Orders are a separate project**, [ILINK_PLAN.md](ILINK_PLAN.md).
- **Latency target: under 1 microsecond from the packet's last byte to its End
  line.** Measured from the first byte it cannot be met at 10 GbE: a 1,420-byte
  packet takes 1.14 microseconds on the wire alone.
- **The strategy does not change.** It gets the messages it gets today, from
  FpgaFeed's PacketSink. That adapter is not written yet.

### Why MBP and not MBO

| | MBP | MBO |
|---|---|---|
| book state | 10 levels per side, small and fixed | every live order, a hash table in UltraRAM |
| an update | "level *i*: price, quantity, order count; new / change / delete", applied as a shift and a write | an order add / modify / delete, an order-id lookup, then a level update |
| best price | level 1 | a bitmap and a priority encoder |
| capacity risk | none | live orders at peak must fit |
| depth | 10 levels (CME's MBP depth for these futures) | full |
| order detail | none | each order's priority (queue position), individual adds and cancels |
| events | the MBP entries of a packet are the event list | per-order events |

This table compares building each book **on the card**. The design does not: the card
decodes, filters and packs, and the CPU builds the books. With the book on the CPU, MBO
needs no hash table on the card. Passing MBO through means decoding template 47
(40-byte entries) and the order list of template 46 (24-byte entries, which point to a
price entry of the same message for their security), one event per line instead of
three, and many more events; FpgaFeed would hand them to TachBook. It is listed under
"Still to build".

The output is the top 10 levels and the trades, and nothing in `light22` or
the shadow algorithm uses MBO queue position (the simulator's queue-aware
fills come from `OB.cpp`, a price-level book). MBO stays available to the CPU
if queue position of our own orders is wanted later.

## Today's path

```
SocketReader -> MessageProcessor -> DataDecoder / msg_decoder -> handler_if -> TachBook -> light22
                (A/B, seq, gap queue,   (SBE decode)              (security -> book,    (MBO L3
                 recovery)                                         order-id map,         book)
                                                                   routing, binrec)
```

## The design on the card

| stage | what it does |
|---|---|
| **FeedRx** | Ethernet frames in; the UDP payloads of one group and port out, realigned, padding dropped. 802.1Q tags handled. Raw streams on both sides. |
| **SbeDecoder** | decodes as the packet arrives, nothing stored. Each clock takes up to 8 bytes from a two-word window at any byte offset. One event per entry: 46 (MBP), 48 (trades), 30 (SecurityStatus), 4 (ChannelReset); the rest skipped by length. Generated from CME's template (`gen_decoder.py`). |
| **Filter** | a table of up to 64 security ids, a parallel compare, loaded by the CPU. Drops implied entries and other security ids, numbers the rest, packs each to 16 bytes. SecurityStatus always passes: CME sends it for a security group, with no security id. |
| **Batcher** | three events to a line; at the end of every packet an End line with what is left and the sequence number. One send a step, so II = 1. |

All at II = 1. On the CPU, **FpgaFeed** (`mdp3/host/fpga_feed.hpp`) is an ordinary
Kaspar actor on an FpgaLanes mailbox: it loads the table (WantSecurity, answered
by WantAck) and sends the card nothing else; it copies each line to a buffer, and
at the End line applies the packet to its books and calls the strategy, on its own
thread.

### What crosses to the CPU

| message | size | when |
|---|---|---|
| `EventLine` | one line, 3 events | as lines fill |
| `EndLine<N>` | one line, N events (0 to 2) and the sequence number | once for every packet |
| `WantAck` | one line | at start, one per table entry |

A packed event is price, size, order count, security number, level, action and
side (`mdp3/include/mdp3fpga/events.hpp`). A status carries trading status, halt
reason and trading event. A packet of 44 events is 15 lines.

## Known limitations (no recovery)

- **Start-up.** Books start empty, and incremental MBP sends only the levels that
  change, so some levels are missing and some are stale until updates reach them.
- **Crossed books are not always an error.** CME holds a crossed book in pre-open.
  See "Status" for the case in the recording.
- **A gap.** MBP updates are by level number: a missed *new* or *delete* leaves the
  deeper levels shifted. Later entries overwrite their own level, so layouts heal
  as the market moves, but nothing guarantees when. FpgaFeed's gap flag stays set
  from the first gap: the CPU decides what to do.
- **ChannelReset** clears the books, which then refill as at start-up.
- **The recordings have gaps.** Whether they are feed A drops or capture drops is
  not known: feed B or binrec could tell.

## Measuring latency

The g++ test paces frames as 10 GbE delivers them to a 250 MHz design and reports
the clocks from a frame's last word reaching the card to its End line leaving,
queues included. Co-simulation confirms the cycle counts on the Verilog. Latency is
no longer carried in the messages.

| level | what is measured | status |
|---|---|---|
| **simulation** | last byte in to End line, on recorded data at wire pacing | done, see "Status". Not the MAC, not PCIe |
| **on the card** | the same, on live traffic | needs the card |
| **wire to CPU** | packet on the wire to FpgaFeed handling it: the MAC's receive stamp against the CPU clock, synced by PTP | needs the card; adds PCIe, the largest part |
| **card against software** | the same packets through both paths, with `kaspr/perf/` | needs the card |

Not covered: wire to wire (packet in, order out). That needs the order path and an
external tap or capture box.

## Framework changes

Done: raw streams (`Group::step_stream`, and an actor held by its own task), `OutPort` for a
process that only sends, `SoftCard::add_step`. See HLS.md section 2.

Still to come: more than one actor of a type, which BookBank's `Book<N>` would need.

## Verification

- **Reference:** CME's own codec for the events (`test/ref_dump`), and a software
  MBP book written apart from the design (`test/ref_book`).
- **g++:** whole recordings, in constant memory.
- **Co-simulation:** the busiest 2,000 consecutive packets of channel 344.
- **End to end:** the card's actors on a SoftCard, a real FpgaFeed on its mailbox.
- **Not yet:** against `msg_decoder`, the software path, and against TachBook.
- **On the card, shadow mode:** the card and the software handler side by side on
  the live feed, outputs diffed, before anything trades on it.

## Phases

| phase | work | status |
|---|---|---|
| **0. Card facts** | what the card gives us: UG1607, the XDC. The PV has no fixed shell that we found: Vivado RTL flow, cages and PCIe wired to the XCUX35, so we supply the MAC and PCIe DMA. Choose them (AMD IP or Corundum), line rate (UG1607: 10 GbE currently), clocks. | XDC downloaded; choices open |
| **1. SbeDecoder** | generated decoder | done, except the comparison with `msg_decoder` |
| **2. Filter, Batcher, FeedRx, Igmp** | the rest of the card's receive path, and joining the multicast group (IGMPv2) from the card | done in simulation |
| **3. FpgaFeed** | the CPU end, on the framework's existing link | done against a test sink; adapter to `light22` and the overflow flag not written |
| **4. On the card, shadow** | a loopback design first (MAC, PCIe DMA, latency measured); then our bitstream through Vivado, timing closure, next to the software handler on the live feed | needs the card |
| **5. Cut over** | the strategy reads the card's output | fallback to software tested |

Phases 0 to 3 need no card.

## Status

Built and proven in simulation, all on recorded CME data: channel 344 feed A,
the Nov 1 session (30,044,560 packets). Not yet on a card.

| check | result |
|---|---|
| decoder against CME's codec, g++ | `CME344_A` 331,183,779 lines and `CME490_A` 40,136,700, **0 differ** |
| FeedRx, frames rebuilt from 2,000 packets, 1 in 7 tagged, frames to drop mixed in | payloads byte for byte, **0 differ**; all 120 frames to drop dropped |
| frames to the CPU's book, g++, all of `CME344_A`, six security ids, 10 GbE pacing for a 250 MHz clock | **5,880,919 packet ends of security 42001080 compared, 0 differ**; 30,044,560 End lines |
| last byte of a frame in to its End line out (same run) | **mean 5.2, p99 7, max 8 clocks**: 21 to 32 ns at 250 MHz. At most one word ever queued |
| lines to the CPU (same run) | **30,612,938**: one End line per packet, plus 568,378 full lines, for 26,375,349 events |
| RTL co-simulation, XCUX35, the busiest 2,000 consecutive packets of 344 | **pass, 0 differ**; 292,746 cycles; all four actors at II = 1; estimated 212.8 MHz |
| end to end: real FpgaFeed actor on its FpgaLanes mailbox, card on a SoftCard | first 3,000,000 packets of 344, six security ids: **0 differ**, 0 codec errors |
| place and route, all four actors, KU5P stand-in | **250 MHz met; 3.37 ns, about 296 MHz**; 6,066 LUTs, 13,413 flip-flops, 23 block RAM |
| place and route, all four actors, XCUX35 -3 | **250 MHz met; 3.32 ns, about 301 MHz**; 5,824 LUTs, 13,511 flip-flops, 23 block RAM |
| place and route, five actors with Igmp, XCUX35 -3 | **250 MHz met; 3.29 ns, about 304 MHz**; 6,100 LUTs, 14,431 flip-flops, 23 block RAM; every task II = 1 |
| Igmp, on the whole card in g++ | join, repeats, queries (with and without Router Alert and VLAN tag), leave: every frame byte for byte against frames built from the RFCs; tcpdump confirms the checksums |

**Crossed books on security 42001080** (676 of 5,880,919 packet ends; 591 before
SecurityStatus reached the book, which adds batches): two runs, neither an error in
the decoder or the book.

- 633 in one run from seq 12,206 to 19,153, 17:47:52 to 18:00:00.079 ET: the
  pre-open before Sunday's open. The recording starts at 17:30, during pre-open; the
  channel's PreOpen status comes at 18,678, NewPriceIndication at 19,104,
  ReadyToTrade at 19,163, and the book uncrosses at 19,157, at the open. CME holds a
  crossed book in pre-open. Of the 633: 481 before the first status message, 110 in
  PreOpen, 42 in NewPriceIndication.
- 43 in one run from seq 21,942,850 to 21,942,979, 12:44 ET in open trading, 1.6 ms:
  17 packets after a gap of 70 missing packets. No status change near it, and no
  halt in the session. A no-recovery effect, healed by the market.

**A bug found and fixed:** CME sends SecurityStatus for a security group with no
security id (all 483 in this recording), and Filter dropped them, as did the book
reference, so the test could not see it. Filter now always passes a status, with
its trading status, halt reason and trading event.

**The recording has 221 sequence gaps, 5,576 packets missing.** Whether feed A
dropped them or the capture did is not known.

## Still to build

`mdp3/ACTORS.md` describes the handler as a whole, as it will run. These parts of it
exist only as design so far; everything else is built and tested in simulation.

**Network side**

- **MAC.** Transceiver, 10GBASE-R PCS and MAC on the port the feed arrives on: AMD's
  10G/25G Ethernet Subsystem, low-latency configuration, or Corundum's.
- **Clock-domain crossing.** Asynchronous FIFOs between the MAC's 156.25 MHz and the
  actors' 250 MHz, into `frames` and out of `tx`.
- **The FCS verdict.** The MAC flags a bad frame on its last word. Carry the flag
  through FeedRx and SbeDecoder to the End line, and have FpgaFeed treat a bad frame as
  a gap.

**PCIe side**

- **The PCIe endpoint.** AMD's QDMA or XDMA, or Corundum's, on the XCUX35's hard block.
- **The lane writer.** For each line from `lines`: the host address of the ring, the next
  slot and the sequence number. It issues a memory write of the first 60 bytes, then the
  4-byte sequence number as a separate, later write.
- **Flow control on the lane.** The card must not overwrite slots the CPU has not read.
  Either the CPU writes its read position into the card's memory now and then (the card
  never reads host memory, since a read costs a round trip), or the ring is sized so it
  cannot fill and the card sets an overflow flag.
- **The window.** A BAR region the CPU writes. Logic turns each slot written into a
  message on `to_filter` or `join`.
- **Host setup.** Pinned memory for the lane, whose address the card is given. The BAR
  mapped into the program (VFIO). `cpu_link.hpp`'s CpuEnd on the real lane and window,
  in place of the software rings.

**MBO**

- **Order-by-order data through the card.** Decode template 47 and the order list of
  template 46, keep the wanted securities, pack one order event per line, and feed
  TachBook on the CPU. The decoder's entry register grows from 32 to 40 bytes.

**CPU side**

- **The adapter to `light22`.** From FpgaFeed's books to the messages the lights take.

**Later**

- **More than one channel.** A group table in FeedRx and Igmp, and a channel index to
  the End line (`feed_rx.hpp`, "One channel").

## Risks and open questions

- **The card's interfaces are assumptions until phase 0**: the MAC and DMA we
  choose decide the two ends of the design.
- **Is the PV reachable through Onload?** AMD's documents are clear for the plain
  X3522 and not for the PV. It does not change the actors.
- **License.** The XCUX35 is not in AMD's free tier. The Enterprise evaluation in
  our license file runs to Dec 28, 2026; the Vivado for Alveo package (to Oct 29,
  2027) is untested on it.
- **Where the time goes.** The card's own part is tens of nanoseconds; the hop to
  the CPU is PCIe, about 300 to 400 ns by estimate (`mdp3/ACTORS.md`). The large gain comes when the
  trigger runs on the card too.
