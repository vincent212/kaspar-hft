# Market data: non-certified venue feeds

**Not for use against a venue.** No handler here has connected to a venue or run on
its live data; see [`../README.md`](../README.md).

Market-data feeds extracted from public exchange specifications with the
build-feed-handler skill (`.claude/skills/build-feed-handler/`). Each directory
holds the feed description (`feed.md`, the reading of the specification), the field
tables (`*.tbl`, from which `fieldgen.py` generates the C++ decoders), a decode test,
a README with the documents used and how to regenerate, and `review.md`, the open
questions that need the owner's decision before a handler is written.

| Directory | Feed | Markets | Decoders checked against |
|---|---|---|---|
| `iex/` | IEX DEEP+ over IEX-TP, DEEP+ SNAP | IEX | specification examples |
| `nasdaq/` | TotalView-ITCH 5.0, MoldUDP64, SoupBinTCP, GLIMPSE 5.0 | Nasdaq, BX, PSX | Nasdaq sample ITCH files: 7.6M messages, none unknown or malformed |
| `japannext/` | ITCH, MoldUDP64, GLIMPSE, SoupBinTCP | Japannext | Japannext sample captures: 233k messages, none unknown or malformed, no sequence gaps |
| `cboe/` | Titanium U.S. Equities Multicast PITCH, GRP, Spin; differences in Cboe Europe | BZX, BYX, EDGX, EDGA | a BZX capture: 730k messages, none unknown or malformed |
| `nyse/` | Pillar Integrated Feed and common Pillar framing | NYSE, Arca, American, Texas, National | NYSE sample captures: 1.25M messages, none unknown or malformed |
| `t7/` | T7 EOBI, Release 15.0 | Xetra, Eurex | Deutsche Börse's machine-readable EOBI layout: 377 of 377 fields |
| `hkex/` | OMD-C binary, Securities FullTick | HKEX | specification and Developers Guide examples |

Run a venue's test:

```bash
G=.claude/skills/build-feed-handler/tools/fieldgen.py; v=global_connectivity/non_certified/market_data/nasdaq; out=$(mktemp -d)
for t in $v/*.tbl; do python3 $G $t -o $out/$(basename $t .tbl).hpp; done
c++ -std=c++20 -Wall -Wextra -Werror -I$out $v/tests/*.cpp -o $out/t && $out/t
```

Handlers, when written, follow `.claude/skills/build-feed-handler/kaspar_mapping.md`.

`pcap_file.hpp` is the pcap parser the handlers' replay tools and tests share
(classic pcap, Ethernet or cooked capture, UDP datagrams, one side of a TCP stream);
`test_support.hpp` the decode tests' helpers. Known duplication: `mcast_recv`'s
`PCAPReader` actor (`mcast_recv/include/mcast_recv/act/PCAPReader.hpp`) also reads
pcaps, through libpcap, as a live-feed stand-in for `kaspr`; it is not shared here.
Section 10 of each `feed.md` lists the questions the specification leaves open.
