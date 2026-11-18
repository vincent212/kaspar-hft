---
name: build-feed-handler
description: Build a kaspar-hft market-data, order-entry or post-trade handler from an exchange's specification (PDF, SBE XML, FIX dictionary, JSON schema). Use when asked to connect Kaspar to a new venue or broker, write a feed handler, an order manager, a drop-copy or trade-capture client, or to extract a message spec from a PDF.
---

# Build a Kaspar feed handler from a specification

Read `docs/FEED_HANDLER_AGENT.md` first. The output is always a set of Kaspar
actors with a fixed contract:

- **Market data:** normalized `bfile::l3_t` records, mapped as `kaspar_mapping.md`
  says (OB and the record format are not changed for a venue), (MBO new/change/delete, trades,
  end of packet, gap, channel reset) sent to OB as `frame::mda::msg::Data`.
  Price-level feeds go through `sim::Mbp2Mbo` (with `sim::LevelWindow` when the
  venue sends more levels than are kept, or whole books).
- **Post-trade:** normalized trade-capture messages (new, amend, cancel, bust) for
  position keeping and STP.
- **Order entry:** an order manager registered with the SOM for its venue: `Order`
  and `Cancel` in; `Ack`, `Fill`, `Reject`, `CancAck`, `CancReject` out; order state
  in `frame::som::BrokerOrderBook`.

Start from the nearest existing handler, never from scratch:

| Venue feed | Template |
|---|---|
| binary multicast with a machine-readable schema | `mdp3/` and `genschema/` |
| WebSocket, REST snapshot + diff stream | `sim/src/BinanceFeed.cpp`, `BinanceSync.cpp` |
| WebSocket, full snapshot + sequenced updates | `CoinbaseFeed.cpp`, `CoinbaseSync.cpp` |
| WebSocket, periodic whole books | `HyperliquidFeed.cpp`, `HyperliquidSync.cpp` |
| crypto exchange (JSON over WebSocket and REST; market data, order entry, fills, testnets) | read `crypto.md` |
| FIX market data (snapshots and incremental refreshes over a FIX session) | `global_connectivity/non_certified/market_data/lmax/` (QuickFIX initiator, dictionary generated from the venue's JSON specification, snapshots diffed through `Mbp2Mbo`, a simulated gateway) |
| FIX order entry | `ttom/`; read `order_entry.md` |
| fixed-offset binary (ITCH, OUCH, Glimpse, SoupBinTCP, PITCH) | a field table + `tools/fieldgen.py`: read `binary_codegen.md` |
| a venue carried on a protocol Kaspar already decodes (EBS on CME MDP3) | the existing handler: assess its coverage (below) instead of generating a decoder |
| post-trade: trade capture or drop copy over FIX | `ttom/` for the actor, the m2 `postrade/` clients: read `post_trade.md` |
| order entry: broker or exchange order manager | `frame/som/BrokerOrderBook.hpp`, `ttom/`, kaspar-equities `csom/` (FIX) and `lime/` (SDK): read `order_entry.md` |

Read `actors/cpp/CLAUDE_AGENT_GUIDE.md` before writing any actor.

## Where the output goes

Everything the skill produces goes under
`global_connectivity/non_certified/<market_data|order_entry|post_trade>/<venue>/`,
except for the NDA venues, whose work goes to the private repository. Nothing moves
to `global_connectivity/certified/` without the owner's sign-off against the
criteria in `global_connectivity/certified/README.md`. Every venue README states
that the handler is not certified and what has and has not been checked.

## Workflow

1. **Inventory the documents.** List every specification file with its version and
   date. Where several versions exist (e.g. two ITCH specs, two rules-of-engagement
   revisions), state which applies and why. Ask the user if it is not clear. Sources
   other than PDFs are listed the same way (see *Specification sources* below).
2. **Extract into a feed description** using `feed_template.md`. For a fixed-offset
   protocol, the message catalogue is a field table checked with
   `fieldgen.py --check` and rendered with `--markdown`. For a long PDF, use
   one subagent per part (framing, each group of messages, recovery, semantics),
   each reading at most 20 pages per request. Cross-check every table against
   `pdftotext -layout` output. Every entry cites its source as *Specification
   sources* says. Section 11 of the feed description lists what the handler needs
   that the specification does not give (rate limits, tick sizes, account options,
   hosts), each marked *to obtain from the venue*.
3. **Go through `pitfalls.md`** item by item and record in the feed description how
   each applies, or that it does not. Anything unclear goes under *Ambiguities* with
   your reading and the alternatives.
4. **Stop.** Present the feed description and the ambiguities to the user. Do not
   generate code until the user approves it. When the owner asks for an end-to-end
   build in one run, there is no stop: the agent records its readings of the
   ambiguities in `feed.md`, builds and tests on them, and writes `review.md` with
   every decision the owner has to confirm; the venue README says that the review
   happens after the code, on `feed.md` and `review.md`.
5. **Generate decoders** deterministically: SBE XML through `genschema/`; a
   fixed-offset protocol through a field table and `tools/fieldgen.py`
   (`binary_codegen.md`); FIX through the venue's QuickFIX dictionary
   (`post_trade.md`). Write a decoder by hand only for JSON and other self-describing
   encodings. Never edit generated code; fix its source and regenerate.
6. **Write the actors** from the template, translating venue semantics into Kaspar
   records by the rules in `kaspar_mapping.md` (priority resets, trade prints,
   clears, identifiers, quantity units, event boundaries, trading status): feed actor, sync/recovery, instrument
   loader, configuration, Makefile entries.
7. **Test:** unit tests from the specification's examples and from sample messages;
   replay with invariants; gap, duplicate and reset injection; comparison with a
   reference where one exists. Build and run the full unit-test suite. In a git
   worktree, `KSPRPROJ` is the worktree, whose `lib/` and `actors/cpp/` hold no
   built libraries: build them there, or pass `KSPRLIB=<checkout>/lib` and
   `ACTORS_LIB=<checkout>/actors/cpp/libactors.a` to the venue's Makefile. The
   unit-test suite includes MDP3 headers, so a fresh worktree needs the CME SBE
   codecs generated (`./build.sh schema`, `CLAUDE.md`) or copied from a checkout
   that has them. After appending an `en::x` code, rebuild every library
   (`kaspar_mapping.md`, venue codes).
8. **Live check.** Public market data that needs no account (crypto exchanges) is
   checked live as `crypto.md` describes. Anything needing credentials only when
   the user has supplied them, and for order entry only in a test environment:
   logon, subscribe, decode the first messages, run the `--dump` monitor.
9. **Report:** decisions, tests and results, open ambiguities. Open a pull request
   on a feature branch; never commit to main.

## A venue on a protocol Kaspar already decodes

Some venues publish on another venue's protocol (EBS Market on CME Globex uses
MDP 3.0, with the same transport, framing, schema and sequencing as CME futures).
No decoder is generated. The feed description records, for each of the venue's
messages and fields: whether the generated codecs contain it (`genschema/` output
from the venue's current templates), whether Kaspar's decoder dispatches it
(`mdp3/include/mdp3/msg_decoder.hpp`), whether the handler maps it to records
(`handler_if.hpp`), and what the venue's semantics need that the handler does not
do (price-level books, sizes, trades without quantity, instrument definitions).
Tests feed constructed messages of each kind through the unchanged decoder and
report which reach the handler. The output is the coverage and a list of the
changes needed, each with its file; changes to a certified handler (`mdp3/`) are
proposed, not made, and the owner decides (`global_connectivity/certified/`).

## Specification sources

| Source | How it is read | How an entry cites it |
|---|---|---|
| PDF | directly, at most 20 pages per request, cross-checked against `pdftotext -layout` | document, version, page, section |
| Confluence wiki (CME's Client Systems Wiki) | the REST API (`/rest/api/content/<id>?expand=body.storage,version`), not the rendered page | page title, page id and version number, section heading |
| Markdown or HTML documentation (crypto exchanges, GitHub-hosted specs) | the source file, at a commit or a dated download | URL, commit or download date, heading |
| JSON specification (LMAX `fix-spec.json`) | parsed by a script; the QuickFIX dictionary is generated from it, never typed | file, version or download date, JSON path |
| SBE XML templates | `genschema/` | file, schema id and version, message name and id |
| QuickFIX dictionary from the venue | used as given, then validated (`post_trade.md`) | file and version |

Some sites refuse automated requests (cmegroup.com, aquis.eu). Use the venue's
public file hosting or API where one exists (CME's SFTP for templates and channel
configuration, the public S3 bucket that hosts Aquis's specifications), and
otherwise ask the user for the files. Record every download's URL and date in
section 1 of the feed description.

## Rules

- Specifications and data under NDA are never committed to kaspar-hft. The feed
  description and generated code for such a venue go to the user's private
  repository unless the user says otherwise.
- Never resolve a book-semantics or recovery ambiguity silently: a wrong choice
  produces a plausible book that no invariant catches.
- Never send an order to a production session. Order managers are tested against
  fakes and simulators, and checked live only in the venue's UAT or certification
  environment, when the user asks (`order_entry.md`).
- Do not claim a handler works beyond what the tests and the live check showed.
- Use the probing tools available (live public endpoints, sample files) to settle
  ambiguities before asking the user, and say which ones were settled that way.
