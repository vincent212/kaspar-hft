# Japannext PTS equities market data

**Not certified.** The handler has not connected to Japannext and has not run on
its live data. It was built from the public documents below and tested on
Japannext's public sample captures only; see "What has been checked".

Field tables, the feed description and a market-data handler for Japannext PTS
equities: ITCH over MoldUDP64, with MoldUDP64 re-request and GLIMPSE snapshot
recovery over SoupBinTCP.

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
| `review.md` | The questions put to the owner, and the owner's decisions |
| `itch.tbl` | ITCH messages (`jnx::itch::from_jnx`), without a length prefix; `decode_mold()` reads one MoldUDP64 message block |
| `moldudp64.tbl` | MoldUDP64 downstream header, message block and request packet (`jnx::moldudp64`) |
| `glimpse.tbl` | SoupBinTCP session layer (`jnx::soup`) and GLIMPSE messages (`jnx::glimpse::from_jnx`) |
| `include/jnx_md/Dispatcher.hpp`, `src/Dispatcher.cpp` | ITCH and GLIMPSE messages to Kaspar L3 records (feed.md section 7); the order map and the handler's own book |
| `include/jnx_md/Handler.hpp`, `src/Handler.cpp` | One MoldUDP64 stream: sequencing, duplicates, gaps, re-request, GLIMPSE snapshot and its alignment, End of Session, new session |
| `include/jnx_md/Instruments.hpp`, `src/Instruments.cpp` | Instrument loader: tick tables and directory to `ref::RefData` assets (feed.md section 8) |
| `include/jnx_md/act/JnxFeed.hpp`, `src/JnxFeed.cpp` | The actor: datagrams from `mcast_recv`'s `SocketReader<uint64_t, 10>` in, `frame::mda::msg::Data` to each instrument's book and to the recorder out; re-request socket |
| `include/jnx_md/act/GlimpseClient.hpp`, `src/GlimpseClient.cpp` | The GLIMPSE SoupBinTCP session, on its own thread |
| `include/jnx_md/if/JnxFeed.hpp`, `src/JnxFactory.cpp` | Factories and ini settings (`config/jnx_example.ini`) |
| `include/jnx_md/Pcap.hpp` | pcap reader: UDP datagrams, one side of a TCP stream (`../pcap_file.hpp`, the parser the handlers under `market_data/` share) |
| `include/jnx_md/Writer.hpp`, `src/Writer.cpp` | MoldUDP64 packets, and a GLIMPSE snapshot written from a handler's state (tests and replay) |
| `tools/jnx_replay.cpp`, `tools/BookCheck.*` | Replay into real OBs with invariant checks and injected faults |
| `tests/` | `jnx_decode_test.cpp` (the decoders alone) and the Google Test suite |

A GLIMPSE session that fails (Login Rejected, End of Session before End of Snapshot,
or a transport failure the client reports) is asked for again after `glimpse_retry_s`,
doubling on each failure up to `glimpse_retry_max_s`; a snapshot that completes
starts the wait over. The Handler detects a rejected login or an early End of Session
in the bytes it decodes; the client reports only transport failures, so one failed
session gives one retry.

## Build and test

Generated headers go to `build/include/jnx_md/gen` and are not committed. The
Makefile is not part of the top-level build. It needs the Kaspar libraries
(frame, mcast_recv, logger, mq0, chutil, actors), Boost, ZeroMQ, zlib and Google
Test. `OB.cpp`, `RefData.cpp` and `point.cpp` are compiled from this tree and
linked ahead of `libframe`, because a `libframe` built before `JNX` was added to
`en::x` aborts on JNX records.

```sh
cd global_connectivity/non_certified/market_data/japannext
make test KSPRPROJ=~/kaspar-hft                     # decode test and unit tests
make test KSPRPROJ=~/kaspar-hft KSPRLIB=/path/to/lib ACTORS_LIB=/path/to/libactors.a \
     JNX_DATA=/dir/with/the/sample/pcaps            # plus the sample-data tests
make replay KSPRPROJ=~/kaspar-hft JNX_DATA=/dir/with/the/sample/pcaps
```

`build/jnx_replay --udp U.pcap --glimpse G.pcap [--all | --scenario NAME ...]`
replays the capture into one OB per traded instrument. The public UDP capture
starts after the day's directory, so the directory is preloaded from the GLIMPSE
capture, which is truncated before End of Snapshot and so cannot be used as a
snapshot. Recovery is tested with a snapshot written from a reference handler's
book at a sequence number inside a buffered packet.

## What has been checked

On the public samples (ITCH UDP capture 2023-10-19, 224,754 packets, sequence
12,562 to 234,750; GLIMPSE capture with 4,182 directory entries):

| Check | Result |
|---|---|
| Messages decoded | 222,189 ITCH (A 128,366, E 67,902, D 10,772, U 9,287, T 5,843, Y 16, S 2, H 1); 0 unknown, 0 malformed; 8,688 GLIMPSE |
| Sequence | 0 gaps, 0 duplicates in the capture |
| References | every E, D and U found its order: 0 unknown; 0 repeated adds (decision 1), 0 over-executions |
| Records sent | MBO 216,327, MBO trade 67,902, trade print 67,902, status 12,545, limits 4,182, end of packet 215,912 |
| Crossed books | the handler's book was never crossed or locked at the end of a timestamp (176,354 checks), in status 17 or any other; OB, built to abort on a cross, did not abort |
| Prices | 138,697 prices sent to OB: all whole Kaspar ticks, on the venue's grid, within the limits and on OB's ladder; OB counted 0 bad prices |
| OB against the handler | top 5 levels per side of every book touched, every 2,000 packets: 3,022 comparisons, 22,265 levels, 0 differences; at the end every level of all 144 books: 2,537 levels, 0 differences; best bid and offer equal |
| Duplicates and reordering | every 50th packet twice and every 97th pair swapped (6,397 packets injected): book equal to the uninterrupted replay, OB 0 differences |
| Re-request | 12 packets dropped in 4 runs, answered one reply packet per request: 4 gaps, 12 requests, no snapshot; book equal, OB 0 differences |
| Gap to snapshot | 4 packets dropped, no re-request server: gap record, channel reset, snapshot at sequence 112,864 (inside a buffered packet, whose first message was dropped as older); book equal to the uninterrupted replay (50,736 orders, same queue order), OB 0 differences right after the snapshot (41 books) and at the end |
| Forced reset | reset mid-stream, recovered by snapshot at 135,106: book equal, OB 0 differences |
| Late join | started 40% into the capture, joined by snapshot at 101,730: book equal, OB 0 differences |
| End of Session | injected after the capture: every handler book and every OB empty |
| Unit tests | 38 (36 without the sample data): each message type against its records, sequencing, recovery and its retry backoff, OB levels, the actor's routing, the GLIMPSE client against a fake server on 127.0.0.1 |

Not checked: anything live (no connection to Japannext, so the multicast
receiver, the re-request socket and the GLIMPSE client have only been compiled);
a real GLIMPSE snapshot with resting orders (the sample is truncated); a session
with several order book groups on one stream; the night session; End of Session
and a new session in real data; A/B feeds; ITCH over SoupBinTCP (not used by the
handler). The open questions are in `feed.md` section 10 and `review.md`.

## Regenerating the headers by hand

```sh
G=.claude/skills/build-feed-handler/tools/fieldgen.py
OUT=/tmp/jnx                      # any scratch directory
mkdir -p $OUT
for t in itch moldudp64 glimpse; do
  python3 $G global_connectivity/non_certified/market_data/japannext/$t.tbl --check
  python3 $G global_connectivity/non_certified/market_data/japannext/$t.tbl -o $OUT/$t.hpp
done
c++ -std=c++20 -Wall -Wextra -Werror -I$OUT global_connectivity/non_certified/market_data/japannext/tests/jnx_decode_test.cpp -o $OUT/jnx_decode_test
$OUT/jnx_decode_test
```

To regenerate section 4 of `feed.md`, run `fieldgen.py <table> --markdown` for each table.
