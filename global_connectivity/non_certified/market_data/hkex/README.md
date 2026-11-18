# HKEX OMD-C (Securities FullTick) feed description and field table

## Documents

Public, from the HKEX OMD-C page
(https://www.hkex.com.hk/Services/Market-Data-Services/Infrastructure/HKEX-Orion-Market-Data-Platform-Securities-Market-OMD-C):

| Document | Version | Date | URL |
|---|---|---|---|
| OMD-C Interface Specifications, Binary Protocol | 1.47 | 3 November 2026 | https://www.hkex.com.hk/eng/prod/dataprod/Documents/HKEX_OMD-C_Binary_Interface_Specifications_v1.47.pdf |
| OMD-C Developers Guide | 1.15 | 14 July 2026 | cited; 1.16 (21 August 2026, section 10.10 only) is the version now on the OMD-C page |
| OMD-C Connectivity Guide, Securities Market & Index Datafeed | 2.9 | 12 October 2026 | OMD-C page, Technical Documents |

## Contents

| File | What it is |
|---|---|
| `feed.md` | Feed description: documents and product applicability, transport, framing, message catalogue, sequencing, retransmission and refresh, semantics, mapping to Kaspar `bfile::l3_t` records, instruments, pitfalls, ambiguities. |
| `omdc.tbl` | Field table, venue `hkex`, little-endian. Sections: `pkt::from_hkex` (packet header), `omdc::from_hkex` (every multicast message: control, refresh complete, reference, status, SF order book and trades, and the SS/SP/odd lot/broker queue/index/Stock Connect messages), `omdc::from_hkex_group` (entries of repeating groups), `rts::from_hkex` and `rts::to_hkex` (retransmission session). |
| `tests/hkex_decode_test.cpp` | Builds messages at the specification's offsets, from its definitions and examples and those of the Developers Guide, and checks every field through the generated header; walks a packet by `size`; replays the aggregate order book examples and the line arbitration example. |

## Regenerating the header

The header is generated, not committed:

```bash
FG=.claude/skills/build-feed-handler/tools/fieldgen.py
python3 $FG global_connectivity/non_certified/market_data/hkex/omdc.tbl --check
python3 $FG global_connectivity/non_certified/market_data/hkex/omdc.tbl -o <dir>/omdc.hpp
python3 $FG global_connectivity/non_certified/market_data/hkex/omdc.tbl --markdown      # section 4 of feed.md

c++ -std=c++20 -Wall -Wextra -Werror -I<dir> global_connectivity/non_certified/market_data/hkex/tests/hkex_decode_test.cpp -o hkex_decode_test
./hkex_decode_test
```
