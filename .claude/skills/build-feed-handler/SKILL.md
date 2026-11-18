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
| FIX session | `ttom/` |
| fixed-offset binary (ITCH, OUCH, Glimpse, SoupBinTCP) | a field table + `tools/fieldgen.py`: read `binary_codegen.md` |
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
   revisions), state which applies and why. Ask the user if it is not clear.
2. **Extract into a feed description** using `feed_template.md`. For a fixed-offset
   protocol, the message catalogue is a field table checked with
   `fieldgen.py --check` and rendered with `--markdown`. For a long PDF, use
   one subagent per part (framing, each group of messages, recovery, semantics),
   each reading at most 20 pages per request. Cross-check every table against
   `pdftotext -layout` output. Every entry cites page and section.
3. **Go through `pitfalls.md`** item by item and record in the feed description how
   each applies, or that it does not. Anything unclear goes under *Ambiguities* with
   your reading and the alternatives.
4. **Stop.** Present the feed description and the ambiguities to the user. Do not
   generate code until the user approves it.
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
   reference where one exists. Build and run the full unit-test suite.
8. **Live check** only when the user has supplied credentials and network access:
   logon, subscribe, decode the first messages, run the `--dump` monitor.
9. **Report:** decisions, tests and results, open ambiguities. Open a pull request
   on a feature branch; never commit to main.

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
