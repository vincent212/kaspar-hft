# Plan: the MDP3 handler on the Alveo X3522PV, with FPGA actors

The CME MDP3 market-data path (packets in, order books out) moved onto the
FPGA of an Alveo X3522PV, built from the FPGA actor framework in this
directory (see [HLS.md](HLS.md)). A plan, not built yet.

## Decisions

- **Channel A only.** No A/B arbitration. The card still checks sequence
  numbers: a gap is reported, not repaired.
- **The card's output is the order book at the end of each packet**, for every
  book the packet touched, **and the trades**. CME applies a packet as one
  event, so the book at the end of a packet is consistent; mid-packet states
  are not published. A list of the individual book events is not needed (it
  could be added later as an option).
- **Recovery stays on the CPU.** Snapshots, instrument definitions and channel
  resets are rare and complicated; per-packet work is on the card.
- **The strategy does not change.** A CPU actor turns the card's output into
  the messages `light22` gets today.

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
                                                                   ^     \
CPU: recovery, instrument definitions -> windows -----------------+      `-> trades -> lanes
```

| stage | kind | what it does |
|---|---|---|
| **FeedRx** | streaming stage | filters the channel's multicast UDP from the MAC, strips Ethernet/IP/UDP, stamps the MAC's hardware receive time. Packets are variable length (up to about 1.4 KB), so this streams wide data words, not 64-byte envelopes. |
| **SeqCheck** | actor | checks the packet sequence number. In order: pass it on. A gap: send `Gap` to the books and to the CPU; the books go stale until the CPU reseeds them. |
| **SbeDecoder** | streaming stage | parses the message header, template id and repeating groups, and emits **one fixed-size event per entry**, plus an **end-of-packet** marker. An MBO entry (order id, priority, price, qty, security id, side, action: about 33 bytes) fits the 48-byte envelope payload. Generated from the CME XML templates, as `genschema` already does for the C++ codecs, so a new template version is regenerated rather than rewritten. Templates: 46/47 (MBO book), the trade summary, 30 (SecurityStatus), 4 (ChannelReset). |
| **Router** | actor | security id -> book index, from a table in BRAM that the CPU loads at start and after instrument recovery; the order id -> book lookup that `handler_if` does today. Trades go to the trade output. |
| **BookBank** | actor | **one process holding N books**, arrays indexed by book id (see "framework changes"). Price levels in BRAM; best bid and ask from a bitmap and a priority encoder (TachBook's linear scan after a level empties is fine on a CPU, slow in hardware); the order table (order id -> price, qty, side) as a hash table in UltraRAM. Marks the books a packet touched; at the end-of-packet marker, publishes each of them. |

### Output

Two kinds of message to the CPU, through the card's lanes in host memory:

- **BookSnapshot**, once per touched book per packet: book id, packet
  sequence number, the exchange's transact time, the card's receive time, and
  the top N levels per side (price in ticks, total quantity, order count),
  N configurable, default 10. That is more than one 48-byte payload, so a
  snapshot is a short run of envelopes, numbered, the last one flagged; the
  CPU acts on the last. Each level is 8 bytes (price as a tick index, quantity),
  so 5-6 levels fit one envelope.
- **Trade**, one per trade entry: book id, price, quantity, aggressor side,
  packet sequence number, transact time.

Plus `Gap` and `Stale`/`Reseeded` status per book, so the CPU knows when a
book cannot be trusted.

### The CPU side

- **FpgaFeed**, a CPU actor on an `FpgaLanes` mailbox (`CpuEnd`, see
  `host/kfpga/cpu_link.hpp`): reads snapshots and trades and sends the same
  messages `light22` gets today. The strategy does not know the books are on
  the card.
- **Recovery** as today (snapshot feed, instrument definitions), then reseeds
  the card: the security -> book table and each book's levels and orders,
  through its window.
- The software handler stays: for PCAP replay and simulation, and as a shadow
  to check the card against.

## Framework changes needed first

- **Streaming stages alongside actors**, for packet bytes. Actors carry
  messages; raw packets are a stream of data words.
- **Many books of one type.** The framework allows one actor of each type
  today (HLS.md section 2). BookBank holds N books in one process, indexed by
  book id, which avoids N actor types.
- **Many events per packet.** One packet carries many entries, more than an
  outbox of 4. The decoder streams its events; each per-entry handler stays
  loop-free, so the process pipelines at II = 1. Publishing snapshots at the
  end of a packet is a short fixed loop (N levels, touched books).
- **The MAC's clock.** Design for the network side's clock (about 322 MHz for
  25 GbE), not the 250 MHz used in the examples.

## Verification

The reference model is the existing C++ path (`msg_decoder`, `handler_if`,
TachBook), and the test data is real: the recorders on our NY4 box capture
CME packets.

- **C simulation:** a full day of recorded packets through the HLS design;
  every end-of-packet snapshot and every trade compared with what the
  software path gives at the same packet. Bit for bit.
- **Co-simulation:** minutes of data, including recorded gaps and channel
  resets.
- **On the card, shadow mode:** the card and the software handler side by side
  on the live feed, outputs diffed, before anything trades on the card's
  books.

## Phases

| phase | work | done when |
|---|---|---|
| **0. Card facts** (1-2 weeks) | Get the X3522PV FDK. Find out: how packets reach the FPGA (AXI-Stream from the MACs?), how the FPGA writes to host memory (DMA? through the X3 NIC's path?), the XCUX35's UltraRAM/BRAM, the clocks, the FDK license. Build the FDK reference design. | the reference design runs on a card; loopback latency measured |
| **1. SbeDecoder** | generated HLS decoder; testbench fed from recorded packets | the same entries as `msg_decoder` over a full day |
| **2. SeqCheck** | sequence check, gap reporting | the same gaps as `MessageProcessor` on recorded data, with losses injected |
| **3. One book** | BookBank with one instrument (ES front month), the order table, bitmap best price, end-of-packet snapshots, trades | snapshots and trades identical to the software path over a full day |
| **4. The chain in simulation** | Router, N books, lanes, CPU `FpgaFeed`; SoftCard and HLS co-simulation | `light22` gets identical messages from either path |
| **5. On the card, shadow** | bitstream for the XCUX35 through the FDK, timing closure, run next to the software handler on the live feed | days of live data with no differences |
| **6. Cut over** | the strategy reads the card's books | wire-to-book latency measured; fallback to software tested |

## Risks and open questions

- **The card's interfaces are assumptions until phase 0**: where the MACs
  connect and how the FPGA reaches host memory decide FeedRx and the lanes.
- **License.** The XCUX35 is not in AMD's free tier. The 60-day Enterprise
  evaluation in our license file has started (Oct 29, 2026); the real part
  needs the X3 FDK license from AMD.
- **Order table size.** The number of live orders on ES/NQ at peak must fit
  in UltraRAM; measure it from recordings in phase 3.
- **Recovery and reseeding.** The hard part of any feed handler. Keeping it on
  the CPU is deliberate; the card needs only a clean stale / reseeded
  protocol per book.
- **Where the time goes.** Decode and book should take tens of cycles: roughly
  100-200 ns from packet to updated book, plus the MAC and PHY. The hop to the
  CPU is PCIe, around a microsecond. The large gain comes when the strategy's
  trigger also runs on the card; this plan puts the books there first.
