---
name: build-feed-handler
description: Build a kaspar-hft market-data or post-trade feed handler from an exchange's specification (PDF, SBE XML, FIX dictionary, JSON schema). Use when asked to connect Kaspar to a new venue, write a feed handler, a drop-copy or trade-capture client, or to extract a message spec from a PDF.
---

# Build a Kaspar feed handler from a specification

Read `docs/FEED_HANDLER_AGENT.md` first. The output is always a set of Kaspar
actors with a fixed contract:

- **Market data:** normalized `bfile::l3_t` records (MBO new/change/delete, trades,
  end of packet, gap, channel reset) sent to OB as `frame::mda::msg::Data`.
  Price-level feeds go through `sim::Mbp2Mbo` (with `sim::LevelWindow` when the
  venue sends more levels than are kept, or whole books).
- **Post-trade:** normalized trade-capture messages (new, amend, cancel, bust) for
  position keeping and STP.

Start from the nearest existing handler, never from scratch:

| Venue feed | Template |
|---|---|
| binary multicast with a machine-readable schema | `mdp3/` and `genschema/` |
| WebSocket, REST snapshot + diff stream | `sim/src/BinanceFeed.cpp`, `BinanceSync.cpp` |
| WebSocket, full snapshot + sequenced updates | `CoinbaseFeed.cpp`, `CoinbaseSync.cpp` |
| WebSocket, periodic whole books | `HyperliquidFeed.cpp`, `HyperliquidSync.cpp` |
| FIX session | `ttom/` |
| fixed-offset binary from a field table | the m2 Fenics/Dealerweb `*_def.txt` + `msg_gen.py` (private repo) |

Read `actors/cpp/CLAUDE_AGENT_GUIDE.md` before writing any actor.

## Workflow

1. **Inventory the documents.** List every specification file with its version and
   date. Where several versions exist (e.g. two ITCH specs, two rules-of-engagement
   revisions), state which applies and why. Ask the user if it is not clear.
2. **Extract into a feed description** using `feed_template.md`. For a long PDF, use
   one subagent per part (framing, each group of messages, recovery, semantics),
   each reading at most 20 pages per request. Cross-check every table against
   `pdftotext -layout` output. Every entry cites page and section.
3. **Go through `pitfalls.md`** item by item and record in the feed description how
   each applies, or that it does not. Anything unclear goes under *Ambiguities* with
   your reading and the alternatives.
4. **Stop.** Present the feed description and the ambiguities to the user. Do not
   generate code until the user approves it.
5. **Generate decoders** deterministically where a schema or field table exists;
   write them by hand only where none does.
6. **Write the actors** from the template: feed actor, sync/recovery, instrument
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
- Do not claim a handler works beyond what the tests and the live check showed.
- Use the probing tools available (live public endpoints, sample files) to settle
  ambiguities before asking the user, and say which ones were settled that way.
