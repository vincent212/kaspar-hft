# The MDP3 handler on the card: design

The system on the Alveo X3522PV takes CME's MDP3 incremental feed from the wire,
decodes it, keeps what the strategy trades, and delivers it to the host in 64-byte
lines. It keeps no order book: the host builds the books. Every stage is an FPGA actor
(`kfpga`, see [../HLS.md](../HLS.md)), running as its own `hls::task`.

## System

![The MDP3 handler on the card and the host](figures/system.png)

| actor | input | output | state |
|---|---|---|---|
| **FeedRx** | Ethernet frames, 64-bit words | UDP payloads of one group and port, realigned | header words, offset, payload bytes left |
| **SbeDecoder** | UDP payload words | one message per entry, EndOfPacket, SeqGap | window, parser state, entry register |
| **Filter** | decoder messages; WantContract from the host | PackedEvent, EndOfPacket, SeqGap; WantAck to the host | contract table, packet flag |
| **Batcher** | PackedEvent, EndOfPacket, SeqGap | EventLine, SeqGap | the line being filled |
| **FpgaFeed** (host) | lines | packets to the strategy | line buffer, books |

Each actor takes one step per clock. FIFOs are declared at the top level and plugged
into the processes. `SbeDecoder` and `FeedRx` run on `Group::step_stream` and
`step_raw`, their input being words and not messages; `Filter` and `Batcher` run on
`Group::step`.

The top-level FIFOs between decoder and filter and between filter and batcher are 64
envelopes deep.

## Actors, messages and handlers

Packets reach the card through the network port, which is wired to the FPGA. No CPU,
socket or kernel is involved. The MAC turns the wire into 64-bit words for FeedRx.

`FeedRx` and `SbeDecoder` have no handlers, their input being raw words. `SbeDecoder`
sends these to `Filter`:

| message | when |
|---|---|
| `BookEntry` | each MBP book entry (template 46) |
| `Trade` | each trade entry (template 48) |
| `SecurityStatus` | template 30 |
| `ChannelReset` | template 4 |
| `SeqGap` | a sequence number was skipped |
| `EndOfPacket` | the packet is finished |

`Filter` handlers:

| handler | what it does |
|---|---|
| `on_book` | drops implied entries and unwanted contracts, packs the rest, sends `PackedEvent` |
| `on_trade`, `on_status` | sends `PackedEvent` if the contract is wanted |
| `on_reset` | always sends `PackedEvent`: a reset affects every contract |
| `on_end` | forwards `EndOfPacket` only if the packet had an event for us |
| `on_gap` | always forwards `SeqGap` |
| `on_want` | stores a contract in the table, replies `WantAck` to the host |

`Batcher` handlers:

| handler | what it does |
|---|---|
| `on_event` | holds up to 3 events; a 4th sends the full line and starts the next |
| `on_end` | sends the held events as a line marked End |
| `on_gap` | forwards `SeqGap` to the host |

`FpgaFeed` handlers, on the host:

| handler | what it does |
|---|---|
| `on_start` | sends `WantContract` for each contract, to load the filter table |
| `on_ack` | counts acks until the table is loaded |
| `on_line` | copies the line's events to a buffer |
| `on_line`, End line | applies the packet's events to the books, then calls the strategy once |
| `on_gap` | sets the gap flag, which stays set and goes to the strategy with each packet |

The strategy gets one call per packet: `on_packet(books, events, n, gap)`.

## FeedRx

The MAC delivers frames as 64-bit words with the FCS removed.

```
frame = Ethernet (14, or 18 with an 802.1Q tag) | IPv4 (20) | UDP (8) | payload
```

The first six words hold every header, so the accept decision is taken in one step: an
IPv4 frame without options, protocol UDP, addressed to the configured group and port.
Anything else is dropped. The payload starts 2 bytes into word 5, or 6 with a tag, so each
output word is the tail of one input word and the head of the next. The UDP length gives
the end of the payload, which removes Ethernet padding. The group and port are set at
build time.

## SbeDecoder

```
packet  = MsgSeqNum (4) | SendingTime (8) | message ...
message = MsgSize (2) | SBE header (8) | root block | groups ...
```

The decoder stores nothing. A two-word window yields up to 8 bytes per clock from any
byte offset, so misalignment costs no throughput, and a state machine tracks the position
in the packet:

![The states of SbeDecoder](figures/decoder_states.png)

| state | reads |
|---|---|
| `kPacket0`, `kPacket1` | packet header |
| `kMessage0`, `kMessage1` | message header: size, block length, template, schema, version |
| `kRoot`, `kRootRest` | root block, and what lies beyond the register |
| `kGroup` | group header: entry size, entry count |
| `kEntry`, `kEntryRest` | one group entry, and what lies beyond the register |
| `kTail` | the rest of the message, skipped |
| `kDrain` | the rest of a packet with a corrupt length |
| `kDone` | end of packet |

Root blocks and entries are collected in a 32-byte register. When complete, generated
decoders (`gen_decoder.py`, from CME's XML template) read it at fixed offsets. Blocks are
stepped by the lengths on the wire, so a later schema with longer blocks still decodes.

Templates 46 (book), 48 (trades), 30 (security status) and 4 (channel reset) are decoded;
all others are skipped by MsgSize, as is the order group of 46 and 48. At most one message
is sent per step.

The packet length is known only at the last word, so nothing can be checked against it
in advance. A corrupt MsgSize ends the walk of that packet. A truncated message may
already have sent events. The EndOfPacket is sent regardless.

The decision of how many bytes a step takes sets where the next step reads, and is the
timing-critical path of the design. It is made from the state and from flags held ready in
registers (`msg_ge8_`, `rest_ge8_` and their `gt8` pairs), not from comparisons in the
step, and a truncated packet changes the state only.

**Sequence check.** The decoder compares each packet's sequence number with the expected
one and sends a SeqGap ahead of that packet's EndOfPacket. The card repairs nothing: there
is no recovery from the snapshot feed, and the host decides what a gap means for its books.

## Filter

`Filter` holds a table of 64 security ids with a comparator on each entry, so that a lookup
takes one step. It:

- drops implied entries, keeping outright bids, offers and book resets
- drops contracts outside the table
- packs each surviving event to 16 bytes
- passes channel resets always, since they apply to every contract
- passes EndOfPacket only for a packet that had an event, so quiet packets never reach
  the host
- passes SeqGap always

The host fills the table at start through its own link: one WantContract per contract, each
answered by a WantAck. It sends no market data until every entry is acknowledged, and the
index it assigns is the contract's number in every later event.

## Batcher

`Batcher` holds up to three events in registers. A fourth event shows the line is not the
last of its packet, so the full line goes out unmarked and the new event starts the next.
At EndOfPacket the line held goes out marked End. Hence every packet with events ends in
exactly one End line, nothing waits for a later packet, and there is one send per step, so
the actor runs at II = 1.

## Host interface

Lines are written into a ring in host memory, one writer and one reader, and consumed
through the framework's `CpuEnd` and `FpgaLanes` mailbox, which polls. `FpgaFeed` copies a
packet's lines to a buffer and, at the End line, applies the packet to its books and hands it
to the strategy in one call on that thread. Nothing sleeps and nothing is woken, and the
kernel is not on the receive path.

### Messages to the host

| message | id | contents |
|---|---|---|
| `EventLine<N, End>` | 331-336 | N events, 1 to 3, unused slots zero; the id gives N and End |
| `SeqGap` | 337 | sequence expected, sequence received |
| `WantAck` | 339 | table index |

From the host, `WantContract` (338): table index and security id.

### Event format

Each event is 16 bytes:

![The packed event format](figures/event_format.png)

| bits | field |
|---|---|
| 63..0 | price mantissa, exponent -9 |
| 95..64 | size |
| 111..96 | order count |
| 119..112 | contract, the index in the filter's table |
| 123..120 | level, or the kind of an action 7 event |
| 126..124 | action: MDUpdateAction 0 to 5, 6 trade, 7 other |
| 127 | 1 for an offer; for a trade, set if the aggressor is the seller |

For action 7 the level field gives the kind: 0 security status, 1 channel reset, 2 book reset.

## Clocks and timing

The HLS target is 4 ns, 250 MHz, on one clock. At 8 bytes per clock this exceeds 10 GbE line
rate. Crossing from the MAC's clock domain, if it differs, is not yet designed. Decoder,
filter and batcher were placed and routed at 3.37 ns on the KU5P stand-in, before the End
flag, the table loading and FeedRx were added.

## Outside this design

The MAC and PCIe DMA engine are not built, and their interfaces here are assumed: a 64-bit
stream from the MAC, and a stream of 64-byte lines toward the host. The order path is a
separate project ([../ILINK_PLAN.md](../ILINK_PLAN.md)). `BookBank`
(`include/mdp3fpga/book.hpp`) is retained as a design for a book on the card and is not
part of this system; it does not synthesize as written.
