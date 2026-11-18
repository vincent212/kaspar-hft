# NYSE Pillar Integrated Feed: feed description and field table

## Documents

Public:

| Document | Version | Date | URL |
|---|---|---|---|
| Pillar Integrated Feed Client Specification (NYSE, NYSE American, NYSE Arca, NYSE Texas, NYSE National) | 2.5h | July 30, 2026 | https://www.nyse.com/publicdocs/nyse/data/NYSE_Pillar_Integrated_Feed_Client_Specification.pdf |
| Pillar Equities Common Client Specification | 2.4r | July 21, 2026 | https://www.ice.com/publicdocs/nyse/data/NYSE_Pillar_Equities_Common_Client_Specification_v2.4r.pdf |

## Contents

| File | What it is |
|---|---|
| `feed.md` | Feed description: transport, framing, message catalogue, sequencing and recovery, semantics, mapping to Kaspar `bfile::l3_t` records, instruments, pitfalls, ambiguities. |
| `pillar.tbl` | Field table, venue `nyse`, little-endian. Sections: `pkt::from_nyse` (packet header, short refresh header), `pkt::to_nyse` (request packet header), `ctl::from_nyse` (control, refresh and retransmission channel messages), `req::from_nyse` and `req::to_nyse` (Request Server over TCP), `xdp::from_nyse` (Integrated Feed messages). |
| `tests/nyse_decode_test.cpp` | Decodes messages built from the field definitions, and two packets from NYSE's public sample capture, through the generated header; checks every field. |

## Regenerating the header

The header is generated, not committed:

```bash
FG=.claude/skills/build-feed-handler/tools/fieldgen.py
python3 $FG global_connectivity/non_certified/market_data/nyse/pillar.tbl --check
python3 $FG global_connectivity/non_certified/market_data/nyse/pillar.tbl -o <dir>/pillar.hpp
python3 $FG global_connectivity/non_certified/market_data/nyse/pillar.tbl --markdown      # section 4 of feed.md

c++ -std=c++20 -Wall -Wextra -Werror -I<dir> global_connectivity/non_certified/market_data/nyse/tests/nyse_decode_test.cpp -o nyse_decode_test
./nyse_decode_test
```
