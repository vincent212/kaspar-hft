# The MDP3 handler on the card: design

The system on the Alveo X3522PV takes CME's MDP3 incremental feed from the wire,
decodes it, keeps what the strategy trades, and delivers it to the host in 64-byte
lines. It keeps no order book: the host builds the MBP books from the events the card sends. Every stage is an FPGA actor
(`kfpga`, see [../HLS.md](../HLS.md)), running as its own `hls::task`.

## System

![The MDP3 handler on the card and the host](figures/system.png)

| actor | input | output | state |
|---|---|---|---|
| **FeedRx** | Ethernet frames, 64-bit words | UDP payloads of one group and port, realigned | header words, offset, payload bytes left |
| **SbeDecoder** | UDP payload words | one message per entry, EndOfPacket | window, parser state, entry register |
| **Filter** | decoder messages; WantSecurity from the host | PackedEvent, EndOfPacket; WantAck to the host | security table |
| **Batcher** | PackedEvent, EndOfPacket | EventLine, EndLine | the events held for the next line |
| **Igmp** | JoinGroup, LeaveGroup from the host; IgmpQuery from FeedRx | IGMP frames to the MAC; Listen to FeedRx; IgmpAck to the host | the group, the card's addresses, the frame being sent |
| **FpgaFeed** (host) | lines; WantAck; IgmpAck | WantSecurity, JoinGroup; the books, to the strategy | books |

Each actor takes one step per clock. FIFOs are declared at the top level and plugged
into the processes. `SbeDecoder` and `FeedRx` run on `Group::step_stream`, their input
being words and not messages; `Filter`, `Batcher` and `Igmp` run on `Group::step`, and
Igmp on `step_stream` too while it writes a frame. `ACTORS.md` has each actor in
detail, the MAC, and why the handler is on the card at all.

The top-level FIFOs between decoder and filter and between filter and batcher are 64
envelopes deep.

## Actors, messages and handlers

Packets reach the card through the network port, which is wired to the FPGA. No CPU,
socket or kernel is involved. The MAC turns the wire into 64-bit words for FeedRx.

`SbeDecoder` has no handlers, its input being raw words; `FeedRx` has one, for `Listen`. `SbeDecoder`
sends these to `Filter`:

| message | when |
|---|---|
| `BookEntry` | each MBP book entry (template 46) |
| `Trade` | each trade entry (template 48) |
| `SecurityStatus` | template 30 |
| `ChannelReset` | template 4 |
| `EndOfPacket` | the packet is finished, with its sequence number |

`Filter` handlers:

| handler | what it does |
|---|---|
| `on_book` | drops implied entries and unwanted security ids, packs the rest, sends `PackedEvent` |
| `on_trade` | sends `PackedEvent` if the security id is wanted |
| `on_status` | always sends `PackedEvent`: CME sends status for the group, with a null security id |
| `on_reset` | always sends `PackedEvent`: a reset affects every security |
| `on_end` | always forwards `EndOfPacket` |
| `on_want` | stores a security id in the table, replies `WantAck` to the host |

`Batcher` handlers:

| handler | what it does |
|---|---|
| `on_event` | holds events; sends a full line when more follow, and starts the next |
| `on_end` | sends the End line: the packet's sequence number and up to 2 events |

`FpgaFeed` handlers, on the host:

| handler | what it does |
|---|---|
| `on_start` | sends `WantSecurity` for each security id, to load the filter table, and `JoinGroup` to Igmp |
| `on_ack`, `on_joined` | count the acks; ready when the table is loaded and the group joined |
| `on_line` | applies the line's three events to the books at once, and keeps them for the strategy |
| End line | checks the sequence number, sets the gap flag on a skip, and applies the last 0 to 2 events |

## FeedRx

The MAC delivers frames as 64-bit words with the FCS removed.

FeedRx is called from `rx_task()` in `hls/feed_top.cpp`, its process, a `kfpga::Process<FeedRx>`:
each clock the task calls `proc.step_stream<FeedRx>(ports, frames, words, listen)`, which
calls `on_step`. That reads at most one frame word, writes at most one payload word, and
handles at most one `Listen`; what FeedRx sent, an `IgmpQuery`, is then written to Igmp. The
source, `include/mdp3fpga/feed_rx.hpp`, documents the states and the realignment in detail.

```
frame = Ethernet (14, or 18 with an 802.1Q tag) | IPv4 (20) | UDP (8) | payload
```

The first six words hold every header, so the accept decision is taken in one step: an
IPv4 frame without options, protocol UDP, addressed to the configured group and port.
Anything else is dropped. The payload starts 2 bytes into word 5, or 6 with a tag, so each
output word is the tail of one input word and the head of the next. The UDP length gives
the end of the payload, which removes Ethernet padding. The group and port come from Igmp,
in a `Listen`, when the host joins; until then nothing is accepted. An IGMP frame (protocol 2)
takes one more word, since a query's Router Alert option pushes its fields past the sixth;
a membership query goes to Igmp as an `IgmpQuery`.

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

At most one message is sent per step. Each template is treated as follows, against what
`handler_if.hpp` does with it on the CPU today:

| template | on the CPU today | on the card |
|---|---|---|
| 46 MBP book | books | decoded |
| 48 trades | books | decoded |
| 30 security status | every book, for halt state | decoded |
| 4 channel reset | every book | decoded |
| 47 MBO book | `TachBook` | skipped: the host's book is built from MBP |
| 37 volume | binrec only | skipped |
| 50 limits and banding | binrec only | skipped |
| 51 session statistics, 49 daily statistics | empty handlers | skipped |
| 54, 55, 56 instrument definitions | security id to asset map, RefData | skipped: the host gets them from the instrument feed |
| 12 admin heartbeat | none | skipped |

Skipped templates are passed over by MsgSize, as is the order group of 46 and 48.

The packet length is known only at the last word, so nothing can be checked against it
in advance. A corrupt MsgSize ends the walk of that packet. A truncated message may
already have sent events. The EndOfPacket is sent regardless.

The decision of how many bytes a step takes sets where the next step reads, and is the
timing-critical path of the design. It is made from the state and from flags held ready in
registers (`msg_ge8_`, `rest_ge8_` and their `gt8` pairs), not from comparisons in the
step, and a truncated packet changes the state only.

**Sequence numbers.** The decoder repairs nothing and checks nothing: every EndOfPacket
carries the packet's sequence number on to the host, which detects gaps. There is no
recovery from the snapshot feed.

## Filter

`Filter` holds a table of 64 security ids with a comparator on each entry, so that a lookup
takes one step. It:

- drops implied entries, keeping outright bids, offers and book resets
- drops security ids outside the table
- packs each surviving event to 16 bytes
- passes channel resets and security status always. CME sends status for the group, with
  a null security id, and the host's books use it for halt state
- passes EndOfPacket always, so the host sees every packet's sequence number

The host fills the table at start through its own link: one WantSecurity per security id, each
answered by a WantAck. It sends no market data until every entry is acknowledged, and the
index it assigns is the security's number in every later event.

## Batcher

`Batcher` holds events in registers and sends a 64-byte line when it is full and more
follow. At EndOfPacket it sends the End line, which carries the packet's sequence number
and up to 2 events. If more events are held than fit, a full line goes first. A packet with
no event for us is an End line with no events. Hence every packet produces exactly one End
line, nothing waits for a later packet, and there is one send per step, so the actor runs at
II = 1.

## Host interface

Lines are written into a ring in host memory, one writer and one reader, and consumed
through the framework's `CpuEnd` and `FpgaLanes` mailbox, which polls. `FpgaFeed` copies a
packet's events as they arrive and applies each full line to its MBP books at once, while
the rest of the packet is still on the wire. At the End line it checks the sequence number,
setting a gap flag that stays set if a packet was skipped, and applies the last 0 to 2
events. A late or duplicate packet whose full lines were already applied also sets the gap
flag. Nothing
sleeps and nothing is woken, and the kernel is not on the receive path.

### Messages to the host

| message | id | contents |
|---|---|---|
| `EventLine` | 331 | 3 events |
| End line | 332-334 | the packet's sequence number and 0 to 2 events; the id gives the number |
| `WantAck` | 339 | table index, and whether it was stored |
| `IgmpAck` | 342 | the group joined or left, and whether it was |

From the host: `WantSecurity` (338), table index and security id; `JoinGroup` (340), the
group and port and the card's MAC address, IP address and VLAN; `LeaveGroup` (341).

Between actors on the card, Igmp and FeedRx exchange `IgmpQuery` (343) and `Listen` (344).

### Event format

Each event is 16 bytes:

![The packed event format](figures/event_format.png)

| bits | field |
|---|---|
| 63..0 | price mantissa, exponent -9 |
| 95..64 | size |
| 111..96 | order count |
| 119..112 | security index: the security's index in the filter's table |
| 123..120 | level, or the kind of an action 7 event |
| 126..124 | action: MDUpdateAction 0 to 5, 6 trade, 7 other |
| 127 | 1 for an offer; for a trade, set if the aggressor is the seller |

For action 7 the level field gives the kind: 0 security status, 1 channel reset, 2 book reset.
A security status event also carries trading status, halt reason and trading event.

## Clocks and timing

The HLS target is 4 ns, 250 MHz, on one clock. At 8 bytes per clock this exceeds 10 GbE line
rate. The MAC's 10 GbE stream runs at 156.25 MHz and crosses into this clock through an
asynchronous FIFO, not yet built (`ACTORS.md`, "The MAC"). The four receive
actors were placed and routed at 3.37 ns, about 296 MHz, on the KU5P stand-in: 6,066 LUTs,
13,413 flip-flops and 23 block RAMs, and on the card's own device, the XCUX35 at speed grade
-3, at 3.32 ns, about 301 MHz: 5,824 LUTs, 13,511 flip-flops, 23 block RAMs. With Igmp added, the five actors on the XCUX35
met 250 MHz at 3.29 ns, about 304 MHz: 6,100 LUTs, 14,431 flip-flops, 23 block RAMs.

## Test harness

`hls/feed_top.cpp` also holds four plain functions, `load`, `words_in`, `lines_out`
and `tx_out`. They are the counted boundary co-simulation needs (HLS.md section 3): they
move a known number of words and envelopes between the top-level ports and the actor
tasks so that the top returns. They are not stages of the handler, and on the card the
MAC and the DMA engine take their place.

## Outside this design

The MAC and PCIe DMA engine are not built, and their interfaces here are assumed: a 64-bit
stream from the MAC and one back to it, and a stream of 64-byte lines toward the host. The order path is a
separate project ([../ILINK_PLAN.md](../ILINK_PLAN.md)). `BookBank`
(`include/mdp3fpga/book.hpp`) is retained as a design for a book on the card and is not
part of this system; it does not synthesize as written.
