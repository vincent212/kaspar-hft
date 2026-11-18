# Aquis Exchange market data (not certified)

Feed description, field tables, decoders and a handler for the Aquis Exchange
multicast market data of Aquis UK (AQXE) and Aquis EU (AQEU), with the Aquis
Stock Exchange (AQSE) feed decoded but not handled.

**Not certified. Nothing here has connected to Aquis**, in production or in a
test environment, and no Aquis capture has been decoded: none is public. Every
test runs on packets built by hand from the specification's tables. Before any
use it needs the decisions in `review.md`, the network side (multicast
receivers, the replay TCP session), and a check against a capture from Aquis's
test environment.

## Documents

Public, from Aquis's S3 buckets (the aquis.eu document library refused
automated requests; the bucket listing was used to find the latest editions).

| Title | Version | Date | Use |
|---|---|---|---|
| Aquis Market Data Technical Specification [Production] | 4.2 | August 2026 | the field table and handler; in force since 17 August 2026 |
| Aquis Market Data Technical Specification [Pre-release] | 4.2 | May 2026 | compared: production adds the MaC+ flags only |
| Aquis Market Data Technical Specification [Production] | 4.1, 4.0, 2.5 | 2026, 2025, 2023 | compared for the change history (feed.md section 1) |
| Aquis Stock Exchange Multicast Market Data Specification | 1.2.3 (1.2.2 compared) | April 2024 | `aqse_md.tbl` |
| Member Notices 2025_0006, 2025_0007, 2025_0016, 2025_0017, 2026_0002, 2026_0008, 2026_0010, 2026_0019 | | 2025-2026 | effective dates, multicast groups |
| `SecurityList.csv`, `SecurityList_AQEU.csv` | | 2026-11-18 | instrument loader format and test |

URLs are in `feed.md` section 1. The task named MD 2.5 (November 2023) and AQSE
1.2.2 (February 2021); both are superseded.

## Contents

| Path | What |
|---|---|
| `feed.md` | Feed description: documents and versions, transport, framing, message catalogue (both venues), sequencing and recovery, semantics, mapping to Kaspar records, instruments, pitfalls, ambiguities, AQSE differences |
| `review.md` | The open questions of `feed.md` section 10: 8 decisions with recommendations, settled items, low-impact items |
| `aquis_md.tbl` | Field table, MD 4.2: packet header, 13 real-time messages, 4 snapshot messages, 3 replay session messages (21 in all) |
| `aqse_md.tbl` | Field table, AQSE 1.2.3 (24 messages) |
| `include/aquis/Handler.hpp`, `src/Handler.cpp` | Handler core: packet reader, dispatcher to `bfile::l3_t`, sequencer, replay and snapshot recovery |
| `include/aquis/Instruments.hpp`, `src/Instruments.cpp` | Instrument loader: Aquis security file, universe file (UMTF to Kaspar id), Security Definition and Tick Table Data from the feed |
| `include/aquis/act/AquisFeed.hpp`, `src/AquisFeed.cpp`, `include/aquis/msg/AquisBytes.hpp` | Actor: bytes in, `frame::mda::msg::Data` out to the OB of each security, channel-wide records to all, end of packet to the recorder |
| `config/aquis_example.ini` | The settings, with their defaults; not parsed by any code yet |
| `tests/aquis_decode_test.cpp` | Decode test of every message of both tables, bytes written at the specification's offsets |
| `tests/test_handler.cpp`, `test_instruments.cpp`, `test_feed.cpp`, `Pkt.hpp`, `data/` | gtest: handler, loader, actor |
| `Makefile` | Generates the headers into `build/` (git-ignored), builds and runs the tests; not in the top-level build |

The venue codes `en::x::AQXE` and `en::x::AQEU` were added to
`chutil/gencode/enum/def/exch_code.enum` (after `HKEX`), with a test in
`unit_test/src/test_order_fields.cpp`.

## Build and test

```bash
cd global_connectivity/non_certified/market_data/aquis
export KSPRPROJ=~/kaspar-hft
make decode-test                 # generated headers + decode test; no libraries
make test                        # also the handler tests (kaspar libs, Boost, ZeroMQ, gtest)
make KSPRLIB=/path/to/lib ACTORS_LIB=/path/to/libactors.a CXX=clang++ test
make check                       # fieldgen --check on both tables
AQUIS_SECURITY_LIST=/path/to/SecurityList.csv ./build/run_aquis_tests   # loads the whole public file
```

## Tests

Run on macOS with clang (Apple clang, C++20), against the kaspar libraries of
`~/kaspar-hft/lib`, on 2026-11-18.

- `aquis_decode_test`: 56 checks. Every message of `aquis_md.tbl` (21) and every
  AQSE message whose layout differs or is AQSE-only, field by field; a longer
  message decoded from its fixed part; truncated and unknown messages; Login and
  Replay Request encodes compared with hand-written bytes; a packet walked by
  `length`.
- `run_aquis_tests`: 37 tests; 36 pass and the whole-file loader test skips without `AQUIS_SECURITY_LIST`; with it set, 37 pass.
  - Dispatch: add, size-down, unchanged, price and size-up modifies, cancel;
    partial and full visible trades (print, execution, book change, one
    `transactTime`, aggressor side); iceberg peak, reserve and refresh; print-only
    trade types and bust; overfill clamped; bundle across two packets as one
    event; `transactTime` never decreasing; trading-status mapping; unrouted
    securities; definitions and tick tables from the feed; end-of-packet record.
  - Sequencing and recovery: A/B duplicates; gap, replay in arbitrary TCP
    splits, then the buffer; heartbeat-detected gap; start-of-day gap; refused
    replay, then snapshot rebuild and buffered messages; incomplete snapshot
    discarded; snapshot behind the buffer completed by replay; gap above
    `max_replay`; lost replay connection; malformed packets and messages
    longer than the table; unsequenced bundle markers.
  - Random flow with invariants, 8 seeds: 4,000 messages over three
    securities (one unrouted) in packets of 1-5, each sent on two copies lost
    with probability 0.1, plus late duplicates, replay served from the stream:
    8 to 22 gaps per seed, all recovered; the book rebuilt from the records
    equals the generator's book, every add and delete applied exactly once.
  - Instrument loader: a sample of the public AQXE file (7 rows, one ISIN on two
    listings), universe file, errors, feed definitions. With
    `AQUIS_SECURITY_LIST`: the whole public files load, 7,119 (AQXE) and 4,118
    (AQEU) securities, each securityID once.
  - Actor: routing of book, channel-wide and end-of-packet records; bad files.
- `unit_test/src/test_order_fields.cpp`, built alone against the same
  libraries: 10 tests pass, including the new `OrderFields.AquisVenueCodes`.
  The full `unit_test` suite was not built.

## Not checked

- Any Aquis data: no capture, no test environment, no live feed.
- The snapshot path on random flows (only on hand-built cases), and the
  `AquisFeed` actor running on its own thread with real OBs.
- Multicast receive, replay TCP session, timeouts, MaC feeds, AQSE handling,
  configuration parsing, `kaspr` wiring: not written.
- The decisions in `review.md`: each is implemented as recommended, and each
  could be wrong for the real feed.
- g++ and Linux builds.
