# The MDP3 decoder on the card

Phase 1 of [MDP3_PLAN.md](../MDP3_PLAN.md): CME MDP3 packets (UDP payloads)
in, decoded events out, in Vitis HLS for the Alveo X3522PV. Checked against
CME's own codec. No card needed.

## What it does

`include/mdp3fpga/decoder.hpp` walks a packet the way `DataDecoder::mbo_data`
does on the CPU (packet header, then each message: size, SBE header, root
block, groups) and sends one envelope per event (`events.hpp`):

| event | from | fields |
|---|---|---|
| `BookEntry` | template 46, MBP entries (MBO order entries skipped) | price, size, security id, RptSeq, orders, level, action, entry type, match event, tradeable size |
| `Trade` | template 48 (per-order fills skipped) | price, size, security id, RptSeq, orders, aggressor, action, match event, trade id |
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

The reference is CME's real-logic codec (`mdp3_sbe/`, what `msg_decoder`
uses). `test/ref_dump` decodes a packet file with it; the HLS testbench feeds
the same packets to the decoder; both print the same text lines
(`test/dump.hpp`), compared line for line.

Test packets: until we have recordings, `test/make_packets` encodes random but
valid packets with CME's codec: the four decoded templates, templates to skip
(47 MBO, 49 statistics), MBO entries inside 46 and 48, null prices and sizes,
empty packets. Recorded packets: `test/pcap_to_packets` takes the UDP payloads
of one group and port from a pcap (Ethernet, 802.1Q, IPv4, UDP).

```
make test                    # decoder vs reference with g++, 5000 packets (seconds)
source ~/tools/Xilinx/2026.1/Vitis/settings64.sh
make hls                     # Vitis: C simulation, synthesis, RTL co-simulation

test/pcap_to_packets rec.pcap rec.bin 224.0.31.1 14310   # recorded packets, one feed
make PACKETS=rec.bin test
```

## Results so far

KU5P stand-in, 4 ns clock target:

| check | result |
|---|---|
| g++ and Vitis C simulation, 5000 test packets | 48,708 events, **identical** to CME's codec |
| RTL co-simulation, 300 test packets | 2,894 events, **identical**; 38,625 cycles (about 129 a packet) |
| inner loops (store, book entries, trades) | one word or one entry a cycle (II = 1) |
| estimated clock | **95 MHz, short of the 250 MHz target** |
| size | 3969 FF, 19232 LUT, 128 BRAM_18K (8% of the KU5P's LUTs) |

## Not done yet

- **Recorded data.** Only CME-codec test packets so far; recordings of the
  real feed are the next check.
- **Timing.** The design stores the packet, then reads fields at any byte
  offset: each read is a wide multiplexer over the buffer, which is what
  holds the clock to 95 MHz. Next: parse as the words stream in, at fixed
  positions within a word, instead of store-then-walk. That also cuts the
  latency, since decoding starts with the first word.
- **Latency stamps** (MDP3_PLAN.md, "Measuring latency"): cycles per packet
  from co-simulation once the streaming parse is in.
