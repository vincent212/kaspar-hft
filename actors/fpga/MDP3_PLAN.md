# Plan: the MDP3 handler on the Alveo X3522PV, with FPGA actors

The CME MDP3 market-data path (packets in, order books and trades out) moved
onto the FPGA of an Alveo X3522PV, built from the FPGA actor framework in this
directory (see [HLS.md](HLS.md)). A plan, not built yet.

## Decisions

- **Channel A only.** No A/B arbitration.
- **The book is built from MBP**, CME's market-by-price entries (template 46,
  the book entries), not from MBO. Outright prices only, matching today's live
  book (TachBook, built from MBO, has no implied liquidity); an implied book
  can be added later as an option.
- **No recovery.** No snapshot feed, no reseeding. The card checks sequence
  numbers only to flag a gap in its output (see "Known limitations").
- **The card's output, per packet:** the order book at the end of the packet,
  for every book the packet touched; the packet's book events (the MBP level
  updates); and the trades. CME applies a packet as one event, so the book at
  the end of a packet is consistent; mid-packet states are not published.
- **The strategy does not change.** A CPU actor turns the card's output into
  the messages `light22` gets today.

### Why MBP and not MBO

| | MBP | MBO |
|---|---|---|
| book state on the card | 10 levels per side, in registers | every live order, a hash table in UltraRAM |
| an update | "level *i*: price, quantity, order count; new / change / delete", applied as a shift and a write | an order add / modify / delete, an order-id lookup, then a level update |
| best price | level 1 | a bitmap and a priority encoder |
| capacity risk | none | live orders at peak must fit |
| depth | 10 levels (CME's MBP depth for these futures) | full |
| order detail | none | each order's priority (queue position), individual adds and cancels |
| events | the MBP entries of a packet are the event list | per-order events |

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

```
MAC (channel A) -> FeedRx -> SeqCheck -> SbeDecoder -> Router -> BookBank -> lanes -> CPU: FpgaFeed -> light22
                                                         ^
CPU: security id -> book table, at start -> window ------+
```

| stage | kind | what it does |
|---|---|---|
| **FeedRx** | streaming stage | filters the channel's incremental multicast UDP from the MAC, strips Ethernet/IP/UDP, stamps the MAC's hardware receive time. Packets are variable length (up to about 1.4 KB), so this streams wide data words, not 64-byte envelopes. |
| **SeqCheck** | actor | checks the packet sequence number and, on a gap, sets the gap flag on every book (see "Known limitations"). Repairs nothing. |
| **SbeDecoder** | streaming stage | parses the message header, template id and repeating groups, and emits **one fixed-size event per entry**, plus an **end-of-packet** marker. Templates: 46 (MBP book entries), the trade summary, 30 (SecurityStatus), 4 (ChannelReset); MBO entries are skipped. Generated from the CME XML templates, as `genschema` does for the C++ codecs, so a new template version is regenerated rather than rewritten. |
| **Router** | actor | security id -> book index, from a small table the CPU loads at start from the instrument definitions; trades to the trade output. |
| **BookBank** | actor | **one process holding N books**, 10 levels per side each, indexed by book id (see "Framework changes"). Applies each MBP entry: *new* inserts at level *i* and shifts deeper levels down, *delete* removes level *i* and shifts up, *change* overwrites level *i*. A ChannelReset clears the books. Marks the books a packet touched; at the end-of-packet marker, publishes each of them. |

### Output

Through the card's lanes in host memory, per packet:

- **BookSnapshot**, once per touched book: book id, packet sequence number,
  the exchange's transact time, the card's receive time, the two latency
  stamps (below), the gap flag, and the 10 levels per side (price in ticks, quantity, order count). More than one
  48-byte payload, so a snapshot is a short run of envelopes, numbered, the
  last one flagged; the CPU acts on the last.
- **BookEvent**, one per MBP entry in the packet: book id, side, level, action
  (new / change / delete), price, quantity, order count. Each fits one
  envelope.
- **Trade**, one per trade entry: book id, price, quantity, aggressor side,
  packet sequence number, transact time.

### The CPU side

- **FpgaFeed**, a CPU actor on an `FpgaLanes` mailbox (`CpuEnd`, see
  `host/kfpga/cpu_link.hpp`): reads snapshots, events and trades and sends the
  same messages `light22` gets today. The strategy does not know the books
  are on the card.
- At start, loads the security id -> book table into the card from the
  instrument definitions.
- The software handler stays: for PCAP replay and simulation, and as a shadow
  to check the card against.

## Known limitations (no recovery)

- **Start-up.** The books start empty. Incremental MBP sends only the levels
  that change, so a level that has not moved since the card started is
  missing until it does. In an active market (ES, NQ) the top levels fill
  within moments; deeper, quiet levels can take longer.
- **A gap.** MBP updates are by level number: a missed *new* or *delete*
  leaves the deeper levels shifted. Later *change* entries overwrite the
  quantity of their own level, but nothing guarantees the layout heals. The
  gap flag on every snapshot after a gap says so; clearing it means a restart
  of the card's books (or, later, the snapshot feed if we ever add recovery).
- **ChannelReset** clears the books, which then refill as levels update, as at
  start-up.

## Measuring latency

A free-running cycle counter on the card stamps every packet twice: **in**,
when its first byte reaches FeedRx, and **out**, when the last envelope of
its output leaves for the lanes. Both stamps travel in every BookSnapshot and
Trade, so every packet reports its own latency on the card, in cycles.

| level | what is measured | how | covers |
|---|---|---|---|
| **simulation** (phases 1-3) | the design's latency per packet | RTL co-simulation of recorded packets: the stamps give exact cycles, times the clock period (about 3.1 ns at 322 MHz) | decode and book only; not the MAC/PHY, not PCIe. A distribution over minutes of real data, bursts included, not one best case. |
| **on the card** (phase 4) | the same, on live traffic | the same stamps | on-card time per packet |
| **wire to CPU** (phase 4) | packet on the wire to `FpgaFeed` handling it | the MAC's hardware receive timestamp against the CPU clock, synced by PTP (what the FDK exposes is confirmed in phase 0) | adds PCIe, the largest part |
| **card against software** (phase 4) | the same packets through both paths | in shadow mode, the existing software measurement (`kaspr/perf/`: NIC hardware timestamp to book updated, `run_probe.sh`, `kh_*.py`) side by side with the card's | the comparison that matters |

Not covered: wire to wire (packet in, order out). That needs the order path
on the card and an external tap or capture box with hardware timestamps.

## Framework changes needed first

- **Streaming stages alongside actors**, for packet bytes. Actors carry
  messages; raw packets are a stream of data words.
- **Many books of one type.** The framework allows one actor of each type
  today (HLS.md section 2). BookBank holds N books in one process, indexed by
  book id, which avoids N actor types.
- **Many events per packet.** One packet carries many entries, more than an
  outbox of 4. The decoder streams its events; applying one MBP entry is
  loop-free (a fixed 10-level shift), so BookBank pipelines at II = 1.
  Publishing at the end of a packet is a short fixed loop over the touched
  books.
- **The MAC's clock.** Design for the network side's clock (about 322 MHz for
  25 GbE), not the 250 MHz used in the examples.

## Verification

The reference is the existing C++ path, and the test data is real: the
recorders on our NY4 box capture CME packets.

- **C simulation:** a full day of recorded packets through the HLS design;
  every end-of-packet book compared with TachBook's top 10 after the same
  packet (they should agree except where a level involves implied prices, and
  until the card's books have filled after start), every trade with the
  software path's trades. The software side also builds an MBP book from the
  same entries, which must agree with the card exactly.
- **Co-simulation:** minutes of data, including recorded gaps and channel
  resets, to check the gap flag and the reset.
- **On the card, shadow mode:** the card and the software handler side by side
  on the live feed, outputs diffed, before anything trades on the card's
  books.

## Phases

| phase | work | done when |
|---|---|---|
| **0. Card facts** (1-2 weeks, no card) | Get the X3522PV FDK and its license. From its documents: how packets reach the FPGA (AXI-Stream from the MACs?), how the FPGA writes to host memory (DMA? through the X3 NIC's path?), the clocks. | the interfaces of FeedRx and the lanes are known |
| **1. SbeDecoder** | generated HLS decoder for 46 (MBP), trades, 30, 4; testbench fed from recorded packets; the in/out cycle stamps | the same MBP entries and trades as `msg_decoder` over a full day; cycles per packet from co-simulation |
| **2. One book** | BookBank with one instrument (ES front month), end-of-packet snapshots, events, trades, SeqCheck and the gap flag | identical to a software MBP book over a full day; top 10 agrees with TachBook as above |
| **3. The chain in simulation** | Router, N books, lanes, CPU `FpgaFeed`; SoftCard and HLS co-simulation | `light22` gets identical messages from either path |
| **4. On the card, shadow** | first the FDK reference design on the card (loopback latency measured); then our bitstream for the XCUX35 through the FDK, timing closure, run next to the software handler on the live feed | days of live data with no differences |
| **5. Cut over** | the strategy reads the card's books | wire-to-book latency measured; fallback to software tested |

**Phases 0 to 3 need no card.** They run in simulation on recorded packets:
Vitis HLS C simulation and RTL co-simulation, on the KU5P stand-in under the
free license (or the XCUX35 while the Enterprise evaluation lasts), and
SoftCard for the CPU side. Until phase 0 gives the card's interfaces, they use
an assumed AXI-Stream packet input and the lanes already simulated, and adapt
the boundary afterwards; the decoder and the books do not depend on the card.
The card is needed from phase 4.

## Status

- **Phase 1 started** (`mdp3/`, see its README): the decoder is generated from
  CME's template and matches CME's own codec exactly on 5000 test packets (C
  simulation) and 300 (RTL co-simulation). Open: recorded packets, the clock
  (95 MHz estimated, against 250), the latency stamps.

## Risks and open questions

- **The card's interfaces are assumptions until phase 0**: where the MACs
  connect and how the FPGA reaches host memory decide FeedRx and the lanes.
- **License.** The XCUX35 is not in AMD's free tier. The 60-day Enterprise
  evaluation in our license file has started (Oct 29, 2026); the real part
  needs the X3 FDK license from AMD.
- **No recovery** (see "Known limitations"): a gap leaves a book suspect until
  the card's books are restarted. How often channel A gaps on our line in NY4
  can be measured from the recordings in phase 2.
- **Where the time goes.** Decode and an MBP book update should take a few to
  tens of cycles: roughly 50-150 ns from packet to updated book, plus the MAC
  and PHY. The hop to the CPU is PCIe, around a microsecond. The large gain
  comes when the strategy's trigger also runs on the card; this plan puts the
  books there first.
