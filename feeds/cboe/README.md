# Cboe U.S. Equities Multicast PITCH, Gap Request Proxy, Spin Server

Feed description and field table for Cboe BZX, BYX, EDGX and EDGA Multicast
PITCH, its retransmission (Gap Request Proxy) and snapshot (Spin Server)
sessions, with the differences in Cboe Europe Multicast PITCH. No handler code
yet: this is steps 1-4 of the build-feed-handler workflow.

## Documents

Both public, from cdn.cboe.com.

| Title | Version | Date | URL |
|---|---|---|---|
| Cboe Titanium U.S. Equities/Options Multicast PITCH Specification | 2.41.87 | November 10, 2026 | https://cdn.cboe.com/resources/techspec/technical-specifications-cboe-titanium-u-s-equitiesoptions-multicast-pitch-specification.md |
| Cboe Titanium Europe Multicast PITCH Specification | 6.69 | 06 November 2026 | https://cdn.cboe.com/resources/participant_resources/BATS_Europe_MC_PITCH_Specification.pdf |

Sample data used in `feed.md`: Databento's public sample
https://sample-pcaps-dl.databento.com/bats/20230822/ny4-bats-pitch-a-20230822T133000.pcap.zst
(BZX, A feed, 2023-08-22), first 50 MB after decompression.

## Contents

| File | What |
|---|---|
| `feed.md` | Feed description: documents, transport, framing, message catalogue, sequencing and recovery, semantics, mapping to Kaspar `bfile::l3_t`, instruments, pitfalls, ambiguities, differences in Cboe Europe |
| `pitch_us.tbl` | Field table, venue `cboe`, little endian: `unit::from_cboe` and `unit::to_cboe` (Sequenced Unit Header), `pitch::from_cboe` (22 equities PITCH messages), `grp::to_cboe` and `grp::from_cboe` (Gap Request Proxy), `spin::to_cboe` and `spin::from_cboe` (Spin Server) |
| `tests/cboe_decode_test.cpp` | Decodes the specification's example messages field by field, a sequenced unit with two messages walked by `size`, encodes GRP and Spin requests against the examples, and reads a Spin TCP stream |

Offsets in `pitch_us.tbl` exclude the 1-byte inclusive Length, so they are the
specification's offsets minus 1, and each `@size` is the specification's total
minus 1.

## Regenerating headers

Headers are generated, not committed. From the repository root:

```bash
G=.claude/skills/build-feed-handler/tools/fieldgen.py
OUT=build/cboe              # any directory outside the source tree
mkdir -p $OUT
python3 $G feeds/cboe/pitch_us.tbl --check
python3 $G feeds/cboe/pitch_us.tbl -o $OUT/pitch_us.hpp
c++ -std=c++20 -Wall -Wextra -Werror -I$OUT feeds/cboe/tests/cboe_decode_test.cpp -o $OUT/cboe_decode_test
$OUT/cboe_decode_test
```

`python3 $G feeds/cboe/pitch_us.tbl --markdown` renders the message catalogue in
`feed.md` section 4.
