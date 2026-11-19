# Cboe Europe Equities Multicast PITCH (BXE, CXE)

**Not certified. Not for use against a venue.** Nothing here has connected to
Cboe Europe, read its live or UAT data, or been checked against any real Cboe
Europe packet: no public capture exists (Databento's public samples have no
Cboe Europe dataset; Cboe publishes none).

Feed description, field table, decode test, review list and a first handler
(a plain class, no network) for Cboe Europe Multicast PITCH on the BXE and CXE
books, Cboe's UK venue in London. DXE (Cboe NL, Amsterdam) uses the same
protocol and has no `en::x` code yet (`review.md` decision 4).

## What was checked

- Every PITCH 2.X message layout against EU section 4 and Appendix A, with
  `fieldgen.py --check` (offsets contiguous, sizes equal the stated totals):
  24 messages.
- The unit header, GRP and Spin layouts against EU 2.4, 3 and 5, field by
  field, against `../cboe/pitch_us.tbl`; they are identical and generated from
  that table.
- Decoding of every EU Appendix B example (pp.77-84), or of the table's layout
  with the example's values where the example contradicts the table:
  `cboe_europe_decode_test`, 131 checks, all pass.
- The handler (`src/PitchEuHandler.cpp`) on synthetic packets: 18 Google Test
  cases, all pass: London midnight and DST, trading-status mapping, add,
  execute, execute at price, reduce, modify, delete, over-execution, trades
  and the Trade - Extended filter, transaction blocks (one and two books,
  honoured and not), A/B duplicates, gap and out-of-order retransmission,
  heartbeat-revealed gap, Unit Clear scoped to one unit, late join recovered
  by spin with live data arriving during the spin, spin clearing the books,
  unknown symbols, unknown and malformed messages, malformed unit headers.
- Built and run on macOS (Apple clang, arm64) against kaspar `libchutil.a`
  and Google Test 1.17.

## What was not checked

- Any real Cboe Europe data (none available). The message lengths of Order
  Executed, Order Executed at Price/Size, Trade Long and Trade Short follow the
  tables, which contradict the examples (`review.md`, Settled).
- The five decisions in `review.md`: the handler implements the recommended
  option of each, pending the owner's decision.
- Network: no multicast receiver, no GRP or Spin Server TCP session, no actor
  wiring to OB. The handler takes UDP payloads and spin messages from a caller
  and reports gaps through a callback; those parts are not written.
- Sequence rollover and buffer overflow paths (not exercised by a test).
- Kaspar's OB consuming these records (no OB-level test).
- Cboe Europe certification: not started.

## Documents

| Title | Version | Date | URL |
|---|---|---|---|
| Cboe Titanium Europe Multicast PITCH Specification | 6.69 | 06 November 2026 | https://cdn.cboe.com/resources/participant_resources/BATS_Europe_MC_PITCH_Specification.pdf |
| Cboe Europe Equities Participant Manual | 59 | 28 September 2026 | https://cdn.cboe.com/resources/participant_resources/Cboe_Europe_Participant_Manual.pdf |
| Cboe Titanium Europe FIX Specification | 3.53 | 29 July 2026 | https://cdn.cboe.com/resources/participant_resources/BATS_Europe_FIX_Specification.pdf |

The PITCH PDF at that URL on 2026-11-18 is byte-identical to the one used.

## Contents

| File | What |
|---|---|
| `feed.md` | Feed description: documents, transport, framing, message catalogue, sequencing and recovery, semantics, mapping to Kaspar records, instruments, pitfalls, ambiguities, and (section 11) where the U.S. extraction's list of Cboe Europe differences was wrong or incomplete |
| `cboe_europe.tbl` | Field table, `@venue cboe`, little endian: `pitch::from_cboe_eu`, 24 PITCH 2.X messages. Unit header, GRP and Spin come from `../cboe/pitch_us.tbl` |
| `review.md` | Open questions: 5 decisions with recommendations, 12 settled, 11 of low impact |
| `include/cboe_europe/PitchEuHandler.hpp`, `src/PitchEuHandler.cpp` | The handler: sequencing per unit, gap detection, retransmission fill, spin alignment, order tables per unit, translation to `bfile::l3_t`, instrument loader |
| `tests/cboe_europe_decode_test.cpp` | Decode test (no dependencies beyond the generated headers) |
| `tests/test_pitch_eu_handler.cpp` | Handler tests (Google Test) |
| `config/instruments_example.csv` | Instrument file format: `<Cboe symbol>,<Kaspar securityID>` |
| `Makefile` | Generates the headers into `build/` (git-ignored), builds and runs both tests |

Offsets in `cboe_europe.tbl` exclude the 1-byte inclusive Length: they are the
specification's offsets minus 1, and each `@size` is the specification's total
minus 1.

## Building and testing

Headers are generated, not committed. From this directory:

```bash
export KSPRPROJ=~/kaspar-hft          # the repository root
make KSPRLIB=/path/to/lib test       # needs libchutil.a, zlib, Google Test
```

Or the decode test alone, from the repository root:

```bash
G=.claude/skills/build-feed-handler/tools/fieldgen.py
V=global_connectivity/non_certified/market_data
OUT=$(mktemp -d)
python3 $G $V/cboe_europe/cboe_europe.tbl --check
python3 $G $V/cboe_europe/cboe_europe.tbl -o $OUT/cboe_europe.hpp
python3 $G $V/cboe/pitch_us.tbl -o $OUT/pitch_us.hpp
c++ -std=c++20 -Wall -Wextra -Werror -I$OUT $V/cboe_europe/tests/cboe_europe_decode_test.cpp -o $OUT/t && $OUT/t
```

`python3 $G $V/cboe_europe/cboe_europe.tbl --markdown` renders the message
catalogue in `feed.md` section 4.
