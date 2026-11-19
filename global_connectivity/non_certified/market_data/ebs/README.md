# EBS Market on CME Globex: MDP 3.0 market data

**Not certified. Not connected. Not wired into Kaspar.** Nothing here has received
EBS data. It answers one question: what it takes for Kaspar's CME-certified MDP 3.0
handler (`mdp3/`) to receive EBS Market data.

## Answer in brief

EBS uses the same transport, framing, schema (version 13) and sequencing as CME
futures, and the codecs `genschema/` generates already contain every EBS template.
What is missing is everything after the decode: Kaspar's decoder dispatches none of
the EBS-specific templates (63 FX definition, 64 MBP, 65 trade, 66 volume, 69
snapshot), its MBP callback has no update action and 32-bit sizes, the CME handler
has no live MBP path, and EBS trades carry no quantity. `feed.md` section 7 lists each
gap with its file; `patch_proposal.md` lists the changes; `review.md` lists the eight
decisions needed first.

## What was checked

- CME's EBS market data documentation on the Client Systems Wiki (24 pages, versions
  and dates in `feed.md` section 1), CME's production `templates_FixBinary.xml`, the
  four EBS channel configuration files and the MDP TCP session schema, fetched from
  CME's SFTP on 2026-11-18. The production template is byte-identical (MD5) to the
  one `genschema/` uses.
- The channel 518 and 519 entries of `config/ebs_mdp3_prod.info` (incremental A/B,
  instrument replay, snapshot, DR groups, ports, source hosts) compared by hand with
  `EBSNYConfig.xml` and `EBSLNDConfig.xml` refetched from CME's SFTP: they match. The
  other channels in that file were not rechecked.
- Codecs generated from that template into `build/mdp3_sbe/` (66 headers, schema 13),
  by the same steps as `genschema/genschema.py`.
- `tests/ebs_mdp3_test.cpp`, 18 tests, all passing on macOS (Apple clang, Google Test
  from Homebrew):
  - layout of the EBS templates against the documented block lengths and offsets (3);
  - round trip of constructed FX definition, MBP, trade and snapshot messages,
    including the version-13 fields and a version-12 decode (5);
  - constructed EBS packets through the unchanged `mdp3::DataDecoder::mbo_data`:
    which templates reach the handler and which are dropped (6);
  - the prototype `include/ebs/MbpLevelBook.hpp` feeding `sim::LevelWindow` and
    `sim::Mbp2Mbo`: level-index New/Delete/Overlay, depth 10, sizes above 2^31,
    zero-size trades (4).

## What was not checked

- No EBS MDP 3.0 capture exists in the repository or was found publicly; CME's
  DataMine EBS samples are CSV time slices. Every message in the tests was constructed
  from the schema with values from the documentation, so the tests show what the code
  does with messages of the documented shape, not with what EBS sends.
- Tag 55 values, MarketDepth, ticks, lot increments and display factors of real EBS
  instruments were not seen (no EBS definitions in CME's `secdef.dat`).
- The CME notice of 7 September 2023 was read only through a search engine summary;
  cmegroup.com refused the automated request.
- The Conflated TCP channels, recovery (snapshot 69, TCP replay), A/B arbitration and
  gap handling on EBS channels: not exercised.
- The configuration proposals were not loaded by `kaspr`; they cannot be until the
  patch exists (no `en::x::EBS`, no EBS dispatch).
- Overlay (279=5), Market Best on UDP and the trade/book order within an event are
  readings, not confirmed (`review.md`).

## Files

| File | Contents |
|---|---|
| `feed.md` | Feed description: documents, transport and channels, framing, message catalogue (63-69 and shared templates), sequencing and recovery, semantics, what Kaspar does with each message today and the proposed mapping, instruments, pitfalls, ambiguities. |
| `review.md` | Decisions needed (8, with recommendations), settled items, low-impact items. |
| `patch_proposal.md` | Changes to `mdp3/`, `sim/`, `kaspr/`, `genconfig/`, `chutil/` that EBS needs. Not applied. |
| `config/ebs_mdp3_prod.info` | EBS UDP channels 518-531 in the format of `kaspr/genconfig/mdp3_prod.info`, from CME's EBS configuration files. Proposal. |
| `config/cme_ebs.ini` | Proposed `cme.ini` entries for channels 518 and 519. |
| `config/universe_ebs.csv` | Proposed universe rows for five EBS instruments, with the open values marked. |
| `include/ebs/MbpLevelBook.hpp` | Prototype MBP book by level index (the patch's item 5). Not used by Kaspar. |
| `tests/ebs_mdp3_test.cpp` | The tests above. |
| `Makefile` | Generates the codecs into `build/` and builds and runs the tests. |

## Build and test

```bash
cd global_connectivity/non_certified/market_data/ebs
make KSPRPROJ=~/kh-ukfx \
     KSPRLIB=~/kaspar-hft/lib ACTORS_LIB=~/kaspar-hft/actors/cpp/libactors.a \
     TEMPLATE=~/kaspar-hft/genschema/templates_FixBinary.xml \
     SBE_JAR=~/kaspar-hft/genschema/.cache/sbe-all-1.30.0.jar \
     test
```

`TEMPLATE` is CME's `templates_FixBinary.xml` (SFTP `SBEFix/Production/Templates/`, or
the copy `./build.sh` leaves in `genschema/`). `SBE_JAR` is the real-logic `sbe-all`
1.30.0 jar `genschema.py` caches. `build/` is git-ignored; nothing generated is
committed.
