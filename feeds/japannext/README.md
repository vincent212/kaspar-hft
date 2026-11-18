# Japannext PTS equities market data

Field tables and the feed description for Japannext PTS equities: ITCH over
MoldUDP64, and the GLIMPSE snapshot service over SoupBinTCP.

## Documents

Public, at `https://www.japannext.co.jp/pub_data/pub_onboarding/<file>`:

| Title | Version | Date | File |
|---|---|---|---|
| ITCH Market Data Specification (Equities) | 2.02 | 2026-07-02 | `JNX_ITCH_Market_Data_Specification_Equities_2.02.pdf` |
| GLIMPSE Market Data Specification (Equities) | 2.01 | 2026-01-16 | `JNX_GLIMPSE_Market_Data_Specification_Equities_2.01.pdf` |
| MoldUDP64 Specification (All Markets) | 1.02 | 2026-07-02 | `JNX_MoldUDP64_Specification_1.02.pdf` |
| SoupBinTCP Specification (All Markets) | 1.01 | 2026-01-16 | `JNX_SoupBinTCP_Specification_1.01.pdf` |
| Trading Rules (Equities) | 2.02 | 2026-08-03 | `JNX_Trading_Rules_Equities_2.02_EN.pdf` |
| Market Events Schedule | 1.2 | 2024-05-31 | `Japannext_Market_Events_Schedule.pdf` |

Sample captures at the same address: `Japannext_PTS_ITCH_Equities_v1.7.UDP.pcap`,
`Japannext_PTS_ITCH_Equities_v1.7.TCP.pcap`, `Japannext_PTS_GLIMPSE_Equities_v1.4.pcap`.

## Contents

| File | What it holds |
|---|---|
| `feed.md` | Feed description: transport, framing, message catalogue, recovery, semantics, mapping to Kaspar records, instruments, pitfalls, ambiguities |
| `itch.tbl` | ITCH messages (`jnx::itch::from_jnx`), without a length prefix |
| `moldudp64.tbl` | MoldUDP64 downstream header, message block and request packet (`jnx::moldudp64`) |
| `glimpse.tbl` | SoupBinTCP session layer (`jnx::soup`) and GLIMPSE messages (`jnx::glimpse::from_jnx`) |
| `tests/jnx_decode_test.cpp` | Decodes ITCH messages, a MoldUDP64 packet and a GLIMPSE session from bytes |

## Regenerating the headers

Generated headers are not committed. From the repository root:

```sh
G=.claude/skills/build-feed-handler/tools/fieldgen.py
OUT=/tmp/jnx                      # any scratch directory
mkdir -p $OUT
for t in itch moldudp64 glimpse; do
  python3 $G feeds/japannext/$t.tbl --check
  python3 $G feeds/japannext/$t.tbl -o $OUT/$t.hpp
done
c++ -std=c++20 -Wall -Wextra -Werror -I$OUT feeds/japannext/tests/jnx_decode_test.cpp -o $OUT/jnx_decode_test
$OUT/jnx_decode_test
```

To regenerate section 4 of `feed.md`, run `fieldgen.py <table> --markdown` for each table.
