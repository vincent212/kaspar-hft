# global_connectivity: other venues, in development

**Nothing in this directory is working connectivity.** None of it has connected to
a venue, run in production, run in a development environment on a venue's live
data, or passed a venue's certification. Kaspar's certified connectivity is CME
(MDP3 market data, `mdp3/`; iLink 3 order entry, `ilink/`) and Trading
Technologies (FIX 4.4 order routing, `ttom/`), described in the top-level README.

This directory holds the work toward connecting Kaspar to other venues, produced
with the build-feed-handler skill (`.claude/skills/build-feed-handler/`,
design in `docs/FEED_HANDLER_AGENT.md`) from each venue's public specification.

| Directory | Status |
|---|---|
| [`non_certified/`](non_certified/) | Everything today: market-data specifications for seven venues extracted and checked, decoders generated and tested. Not for use against a venue. |
| [`certified/`](certified/) | Empty. A venue moves here only when it meets the criteria in [`certified/README.md`](certified/README.md). |

Each venue directory states, in its own README, exactly what has been done and
checked, and what has not.
