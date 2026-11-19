# Nasdaq TotalView-ITCH 5.0, MoldUDP64, SoupBinTCP, GLIMPSE 5.0

Feed description, field tables and a market-data handler for Nasdaq, BX and PSX
TotalView-ITCH 5.0 over MoldUDP64, with GLIMPSE 5.0 recovery.

**Not certified.** The handler has never connected to Nasdaq or run on its live
data. It has been run on Nasdaq's public sample file, on synthetic MoldUDP64
packets built from it, and against fakes (a re-request server on the loopback, a
GLIMPSE spin built from the replay's own book). What was checked, and what was not,
is below.

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
OUT=build/nasdaq            # git-ignored (build/ in .gitignore); any scratch directory works
mkdir -p $OUT
for t in itch50 moldudp64 soupbintcp glimpse50; do
  python3 $G global_connectivity/non_certified/market_data/nasdaq/$t.tbl --check
  python3 $G global_connectivity/non_certified/market_data/nasdaq/$t.tbl -o $OUT/$t.hpp
done
c++ -std=c++20 -Wall -Wextra -Werror -I$OUT global_connectivity/non_certified/market_data/nasdaq/tests/nasdaq_decode_test.cpp -o $OUT/nasdaq_decode_test
$OUT/nasdaq_decode_test
```

`--markdown` renders a table as the message catalogue in `feed.md` section 4.

## Handler

`make test` (unit tests and the decode test) and `make replay` (tools); see the
Makefile header for the library paths. Generated headers go to `build/gen`.

| Path | What |
|---|---|
| `include/nasdaq_md/Dispatcher.hpp` | ITCH messages -> `bfile::l3_t` records (feed.md section 7, review.md decisions); own order book; trading status |
| `include/nasdaq_md/MoldSequencer.hpp` | MoldUDP64 ordering, A/B duplicates, gaps, re-requests, sessions, snapshot hand-off |
| `include/nasdaq_md/Glimpse.hpp` | SoupBinTCP stream splitter, GLIMPSE login, `SpinWriter` (spins for the fakes) |
| `include/nasdaq_md/NasdaqFeed.hpp` | sequencer + dispatcher + GLIMPSE, transport-independent |
| `include/nasdaq_md/Instruments.hpp`, `src/Instruments.cpp` | locate -> symbol -> Kaspar asset (`ref::RefData::add_future_asset`), tick |
| `include/nasdaq_md/OwnBook.hpp` | the handler's order book |
| `include/nasdaq_md/Transport.hpp`, `src/Transport.cpp` | sample-file reader, MoldUDP64 packetizer, pcap reader/writer, UDP re-request client, GLIMPSE TCP client |
| `include/nasdaq_md/act/NasdaqMD.hpp`, `src/NasdaqMD.cpp` | the actor; `config_from_ini` |
| `include/nasdaq_md/if/NasdaqMD.hpp`, `src/Factory.cpp` | `create_NasdaqMD`, `create_NasdaqMD_reader` (mcast_recv `SocketReader<uint64_t,10>`) |
| `config/nasdaq_md.ini` | example section |
| `tests/handler/` | unit tests (gtest) |
| `tools/nasdaq_replay.cpp` | sample file -> MoldUDP64 packets (with injected drops, duplicates, resets, snapshot gaps) -> handler -> one OB per instrument; checks and report |
| `tools/itch_filter.cpp` | keeps selected securities of a sample stream; measures Cross Trade against C-N shares |

Wiring into `kaspr` is not done. OBs for these assets need a ring ladder
(`ladder_ticks`): maxpx is 2e7 ticks at 0.01 and 2e9 at 0.0001.

Linking against a `KSPRLIB` built from another tree: `OB.cpp`, `RefData.cpp` and
`point.cpp` are compiled from this tree (`TREE_SRC`), because a library built before
the NASDAQ `en::x` codes existed rejects them (`data_pay_load::is_valid`).

### Choices beyond feed.md section 7 (known deviations)

- **Tick.** Not in the feed. The registrar takes it from configuration: 0.01, or
  0.0001 for stocks quoted below $1. An order whose price is not on its asset's tick
  (sub-penny stub quotes below $1 on a penny-tick stock) is kept in the own book and
  not sent, nor is anything that later refers to it (`DispatcherStats::off_tick`).
- **End of burst.** `l3_eop_t` goes to the recorder only, as in `handler_if`; an
  `l3_eob_t` goes at the end of each MoldUDP64 packet to every book that received a
  record in it.
- **Timestamps after a recovery.** OB drops a live record stamped at or below the
  last recovery record's `transactTime`. Recovery records carry the spun Timestamp;
  live records at or below the largest of them are stamped one nanosecond after it
  until the stream passes it (`ts_clamped`).
- **Session change.** A packet of a new MoldUDP64 session is a channel reset (CHR to
  every book, maps cleared). If its sequence number is above 1, the handler takes a
  GLIMPSE spin (`snapshot_on_late_join`), as for a late join.
- **Gaps.** GAP record when a hole is detected; re-request (up to `max_tries`); then
  GLIMPSE; without GLIMPSE the hole is skipped and counted as lost.

### What was checked (2026-11-18)

Sample: `S061226-v50.txt.gz` (trading date 2026-06-12), the first 2,642,411,520
bytes after decompression: 84,571,531 messages, 03:01:32 to 09:35:39 ET, including
the opening cross. Packets are synthetic (8 messages per MoldUDP64 packet unless
stated); the sample has no MoldUDP64 framing.

- **Unit tests** (`make test`): 48 tests, all pass (dispatcher per message type,
  status table, sequencer, re-request over UDP to a fake server on 127.0.0.1, pcap
  path, GLIMPSE alignment, the actor, config), plus the decode test.
- **Whole stream, handler's own book** (`--own-only`, all 12,809 securities):
  every E, C, X, D and U refers to a known order (0 unknown); no execution larger
  than the order; timestamps never decrease; no sequence gaps. 3 own-book checks
  found XXII crossed while its status was 17 (sequence numbers 42,657,262-293);
  not investigated. XXII is the only security with a halt cross (Q type H) in the
  sample.
- **56 securities into real OBs** (25 named, AAPL MSFT NVDA ... SMCI, and every
  400th live directory entry), 11,075,198 messages: no OB abort; 0 mismatches in
  24,846 checks of best bid/offer and size and order count at the top 5 levels
  per side, at 45 points. Not compared: 4 levels beyond OB's ring window (offers at
  $199,999). 185 Add messages off their asset's tick were not sent.
- **Injection** on the same 56 securities: drops (1 in 97, and 1 in 13 with 30
  messages per packet) answered by the fake re-request server, duplicates (1 in 5),
  a new MoldUDP64 session mid-stream (GLIMPSE recovery), and a 2,000-packet hole
  with re-requests unanswered (GLIMPSE recovery): in every run, 0 lost messages, 0
  OB mismatches, and the handler's book equal to the uninterrupted one in every
  level and every queue (1.63M levels compared per run).
- **Decision 4 (Cross Trade):** 12,809 opening crosses. Q Shares equal the C-N
  shares since the previous Q in 8,105 (8,023 with 0 shares); differ in 4,704,
  always with Q above the C-N total (no Q below it); in most of the listed cases
  there is no C-N at all, so Q also counts shares that no order-level message
  reports. E messages at the Q's timestamp: 2 crosses (INTC 5 E / 73 shares, AMD 3 E /
  100 shares); whether those shares are also in Q cannot be told from the feed.
- **Decision 3 (Order Replace):** of E executions of orders created by a same-price
  U (10 smaller, 50 same size, 30 larger, 56 securities), all were at the head of
  their level under the handler's queue model. Of all E executions, 5,258 of
  119,210 from 09:30 (none of 77,600 before) executed an order with others ahead of
  it under that model (mean 44 ahead); not investigated.

Not checked: any live session; the multicast receiver, the re-request socket against
Nasdaq, and the GLIMPSE TCP client (compiled only); a real GLIMPSE spin (decisions 1
and 2 are tested only against spins built from the replay's own book); the rest of
the trading day (closing cross, post-market, End of Messages on real data); BX and PSX
data; wiring into `kaspr`.
