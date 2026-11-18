# IEX DEEP+ feed description and field tables

## Documents

Public, from https://www.iex.io/resources/trading/market-data:

| Document | Version | Date |
|---|---|---|
| IEX Transport Specification (IEX-TP) | 1.25 | May 12, 2017 |
| IEX DEEP+ Specification | 1.06 | October 8, 2026 |
| IEX DEEP+ SNAP Specification | 1.07 | October 8, 2026 |
| IEX DEEP Specification (reference only) | 1.08 | October 19, 2021 |

## Contents

| File | What it is |
|---|---|
| `feed.md` | Feed description: transport, framing, message catalogue, sequencing and recovery, semantics, mapping to Kaspar `bfile::l3_t` records, instruments, pitfalls, ambiguities. |
| `iex.tbl` | Field table, venue `iex`, little-endian. Sections: `tp::from_iex` (IEX-TP segment header), `tp::to_iex` (gap fill request), `deepplus::from_iex` (DEEP+ messages), `snap::from_iex` and `snap::to_iex` (DEEP+ SNAP). |
| `tests/iex_decode_test.cpp` | Decodes the example bytes of the three specifications through the generated header and checks every field. |

## Regenerating the header

The header is generated, not committed:

```bash
FG=.claude/skills/build-feed-handler/tools/fieldgen.py
python3 $FG global_connectivity/non_certified/market_data/iex/iex.tbl --check
python3 $FG global_connectivity/non_certified/market_data/iex/iex.tbl -o <dir>/iex.hpp
python3 $FG global_connectivity/non_certified/market_data/iex/iex.tbl --markdown      # section 4 of feed.md

c++ -std=c++20 -Wall -Wextra -Werror -I<dir> global_connectivity/non_certified/market_data/iex/tests/iex_decode_test.cpp -o iex_decode_test
./iex_decode_test
```
