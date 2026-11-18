# Nasdaq TotalView-ITCH 5.0, MoldUDP64, SoupBinTCP, GLIMPSE 5.0

Feed description and field tables for Nasdaq, BX and PSX TotalView-ITCH 5.0
market data, its two transports and the GLIMPSE snapshot service. No handler code
yet: this is steps 1-4 of the build-feed-handler workflow.

## Documents

All public, from nasdaqtrader.com.

| Title | Version | Date | URL |
|---|---|---|---|
| Nasdaq TotalView-ITCH 5.0 | 5.0 | April 28, 2023 | https://www.nasdaqtrader.com/content/technicalsupport/specifications/dataproducts/NQTVITCHspecification.pdf |
| MoldUDP64 Protocol Specification | V 1.00 | 8/2/24 | https://www.nasdaqtrader.com/content/technicalsupport/specifications/dataproducts/moldudp64.pdf |
| SoupBinTCP | 3.00 | 02/22/2017 | https://www.nasdaqtrader.com/content/technicalsupport/specifications/dataproducts/soupbintcp.pdf |
| Nasdaq GLIMPSE | 5.0 | August 22, 2024 | https://www.nasdaqtrader.com/content/technicalsupport/specifications/dataproducts/NQGlimpseSpecification.pdf |

Sample data used for the check in `feed.md` section 4:
https://emi.nasdaq.com/ITCH/Nasdaq%20ITCH/ (`S061226-v50.txt.gz`, first 50 MB;
`NOII/S050922-v50-NOII.txt.gz`).

## Contents

| File | What |
|---|---|
| `feed.md` | Feed description: documents, transport, framing, message catalogue, recovery, semantics, mapping to Kaspar `bfile::l3_t`, instruments, pitfalls, ambiguities |
| `itch50.tbl` | ITCH 5.0, all 23 messages, `nasdaq::itch::from_nasdaq`, with the 2-byte MoldUDP64 message-block length as `@length_prefix` |
| `moldudp64.tbl` | MoldUDP64 downstream packet header and request packet, `nasdaq::mold` |
| `soupbintcp.tbl` | SoupBinTCP 3.0 session layer, `nasdaq::soup` (the skill's example table, unchanged) |
| `glimpse50.tbl` | GLIMPSE 5.0, 9 messages, `nasdaq::glimpse::from_nasdaq`, no length prefix (decoded from a SoupBinTCP payload) |
| `tests/nasdaq_decode_test.cpp` | Decodes hand-built messages of each kind, a MoldUDP64 packet, and GLIMPSE over SoupBinTCP |

ITCH over SoupBinTCP has no per-message length (the SoupBinTCP packet length
frames it), so `itch::from_nasdaq::decode` cannot be applied to a SoupBinTCP
payload directly. A field table section has one framing; a prefix-less ITCH
decoder needs a second section with the same messages, as `glimpse50.tbl` is for
the GLIMPSE subset.

## Regenerating headers

Headers are generated, not committed. From the repository root:

```bash
G=.claude/skills/build-feed-handler/tools/fieldgen.py
OUT=build/nasdaq            # any directory outside the source tree
mkdir -p $OUT
for t in itch50 moldudp64 soupbintcp glimpse50; do
  python3 $G feeds/nasdaq/$t.tbl --check
  python3 $G feeds/nasdaq/$t.tbl -o $OUT/$t.hpp
done
c++ -std=c++20 -Wall -Wextra -Werror -I$OUT feeds/nasdaq/tests/nasdaq_decode_test.cpp -o $OUT/nasdaq_decode_test
$OUT/nasdaq_decode_test
```

`--markdown` renders a table as the message catalogue in `feed.md` section 4.
