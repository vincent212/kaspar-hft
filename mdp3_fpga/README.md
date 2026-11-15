# The MDP3 handler on the card

[MDP3_PLAN.md](MDP3_PLAN.md): CME MDP3 frames in, decoded and filtered events
out to a CPU actor that builds the books, all as actors, in Vitis HLS for the Alveo
X3522PV. Checked against CME's own codec and a separate software book. No card
needed.

## What it does

```
frames -> FeedRx -> SbeDecoder -> Filter -> Batcher -> FpgaFeed (CPU) -> strategy
```

`include/mdp3fpga/`: `feed_rx.hpp` keeps one group and port of Ethernet frames and
strips them to payload words; `decoder.hpp` decodes as the packet arrives; `feed.hpp`
holds Filter (drops implied entries and security ids not in its table, packs what is
left) and Batcher (three events to a 64-byte line, and an End line for every packet,
carrying its sequence number). `host/fpga_feed.hpp` is the CPU end, an ordinary Kaspar actor, and `books.hpp`
its books. `book.hpp` is BookBank, a book on the card, not in this design.

Inside the card the decoder sends one envelope per event (`events.hpp`):

| event | from | fields |
|---|---|---|
| `BookEntry` | template 46, MBP entries (MBO order entries skipped) | price, size, security id, RptSeq, orders, level, action, type, tradeable size |
| `Trade` | template 48 (per-order fills skipped) | price, size, security id, RptSeq, orders, aggressor, trade id |
| `SecurityStatus` | template 30 | security id, trade date, status, halt reason, event |
| `ChannelReset` | template 4 | appl id |
| `EndOfPacket` | every packet | sequence number, sending time, message and event counts, length |

Every value is raw from the wire (prices are mantissas, exponent -9). Other
templates are skipped by their size. Blocks and group entries are stepped by
the lengths on the wire, so a newer CME version with longer blocks still
decodes.

The field decoders are **generated** from CME's template by `gen_decoder.py`
(offsets, sizes, both group header kinds, constants, fields newer than the
message's version), the way `genschema/` generates the C++ codecs: a new
template version is regenerated, not rewritten.

## How it is checked

The reference for events is CME's real-logic codec (`mdp3_sbe/`, what `msg_decoder`
uses): `test/ref_dump` decodes a packet file with it, and `make test` compares the
decoder's output line for line (`test/dump.hpp`). The reference for books is
`test/ref_book`, a software MBP book written apart from the design, for one
security, fed from that dump.

Test packets: `test/make_packets` encodes random but valid packets with CME's codec,
including templates to skip, MBO entries inside 46 and 48, null values and empty
packets. Recorded packets: `test/pcap_to_packets` takes the UDP payloads of one
group and port from a pcap.

```
make test                          # decoder vs CME's codec, 5000 packets (seconds)
make PACKETS=f.bin test            # the same on a recording
make PACKETS=f.bin rx              # FeedRx: frames rebuilt from it, with frames to drop
make PACKETS=f.bin SECURITY=id SECURITIES=id,id feed
                                   # frames to the CPU's book, against ref_book, the
                                   # first security compared, at 10 GbE wire pacing
make PACKETS=f.bin SECURITY=id SECURITIES=id,id feed_e2e
                                   # the same with a real FpgaFeed on a SoftCard

source ~/tools/Xilinx/2026.1/Vitis/settings64.sh
make FEED_PACKETS=f.bin SECURITY=id SECURITIES=id feed_cosim   # Vitis, on the XCUX35
```

Every g++ test holds one packet at a time and is capped at 4 GB: this box runs the
recorders.

## Results so far

All on channel 344 feed A, the Nov 1 session. Details in MDP3_PLAN.md, "Status".

| check | result |
|---|---|
| decoder against CME's codec, all of `CME344_A` and `CME490_A` | **0 differ** in 371 million lines |
| FeedRx, rebuilt frames with frames to drop | **0 differ** |
| frames to the CPU's book, all 30,044,560 packets, six security ids, 10 GbE pacing | **0 differ** in 5,880,919 compared |
| last byte in to End line | mean 5.2, max 8 clocks: 21 to 32 ns at 250 MHz |
| lines to the CPU | 30,612,938 for 26,375,349 events |
| RTL co-simulation on the XCUX35, busiest 2,000 packets | **pass**, all actors II = 1 |
| real FpgaFeed, first 3,000,000 packets | **0 differ** |
| place and route, four actors, KU5P | 250 MHz met, about 296 MHz |

## Not done yet

- The comparison with `msg_decoder`, the software path.
- The adapter from `PacketSink` to the messages `light22` takes.
- The ring's overflow flag, and sizing the ring.
- FeedRx's group and port loaded by the CPU; they come from the build.
- Place and route on the real chip.
- A MAC and PCIe DMA, and the real card.
