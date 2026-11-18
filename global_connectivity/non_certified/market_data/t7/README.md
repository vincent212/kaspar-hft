# Deutsche Börse T7 EOBI feed description and field table

## Documents

Public, from the Xetra / Eurex "T7 Release 15.0" pages (Market and Reference Data Interfaces):

| Document | Version | Date | URL |
|---|---|---|---|
| T7 Release 15.0 Enhanced Order Book Interface Manual | Version 1, build 150.522.2.ga-150005020-91 | 29 July 2026 | https://www.cashmarket.deutsche-boerse.com/resource/blob/5409230/2887252247af7d3ba65ccee909ecfb58/data/T7_R.15.0_%20EOBI_Manual_Version_1.pdf |
| T7 R.15.0 EOBI XML Representation (eobi.xml, eobi.xsd, EOBILayouts.h) | Version 1, same build | R15.0 package | https://www.cashmarket.deutsche-boerse.com/resource/blob/5409244/658c7505a8446e77dfca94320858a76a/data/T7_R.15.0_%20EOBI_XML_Representation_Version_1.zip |

Neither document is committed.

## Contents

| File | What it is |
|---|---|
| `feed.md` | Feed description: documents, transport, framing, message catalogue, sequencing and recovery, semantics, mapping to Kaspar `bfile::l3_t` records, instruments, pitfalls, ambiguities. |
| `eobi.tbl` | Field table, venue `t7`, little endian. Sections: `pkt::from_t7` (Packet Header), `eobi::from_t7` (the 26 incremental and snapshot messages), `grp::from_t7` (one entry of each of the four repeating groups). Checked against `eobi.xml` and `EOBILayouts.h`. |
| `tests/t7_decode_test.cpp` | Decodes messages built byte by byte from the manual's layouts through the generated header and checks every field; walks a datagram (header and two messages) by `size`. |

## Regenerating the header

The header is generated, not committed:

```bash
FG=.claude/skills/build-feed-handler/tools/fieldgen.py
python3 $FG global_connectivity/non_certified/market_data/t7/eobi.tbl --check
python3 $FG global_connectivity/non_certified/market_data/t7/eobi.tbl -o <dir>/eobi.hpp
python3 $FG global_connectivity/non_certified/market_data/t7/eobi.tbl --markdown      # section 4 of feed.md

c++ -std=c++20 -Wall -Wextra -Werror -I<dir> global_connectivity/non_certified/market_data/t7/tests/t7_decode_test.cpp -o t7_decode_test
./t7_decode_test
```
