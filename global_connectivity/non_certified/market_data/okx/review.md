# OKX V5 public market data handler: review list

Built from sections 5, 6 and 10 of `feed.md`. Answer the items in "Needs your
decision" by number.

## Needs your decision

**1. Integrity check without OKX's checksum.**
OKX fixed the `checksum` field at 0 on 2026-06-23 (changelog) and tells clients to
rely on `seqId`/`prevSeqId`. The CRC32 check the test plan asked for cannot run:
37 627 live book messages over 32 minutes all carried 0, and the spread channel
whose page still describes a checksum also sent 0.
- Options: (a) `seqId`/`prevSeqId` continuity plus the REST comparison at equal
  `seqId` as the integrity check; (b) also compare with REST periodically in
  production (costs rate limit: 40 requests per 2 s per IP); (c) move to a channel
  with a checksum (none found).
- Recommendation: (a). The REST comparison at equal `seqId` matched 189 of 189
  snapshots over 400 levels per side (151 200 levels, 0 mismatches). The CRC32
  code stays: it runs if OKX sends a non-zero value again, and is unit-tested
  against zlib, not against an OKX value.
- Depends on it: whether a silently corrupted level can go undetected between
  gaps.

**2. Book channel.**
`books` (400 levels, 100 ms) is used. `books-l2-tbt` and `books50-l2-tbt` (10 ms)
need VIP4 or above and a login; the SBE stream (`ws/v5/public-sbe`) needs VIP4 and
has no snapshot on the stream.
- Options: (a) `books`; (b) `books-l2-tbt` once the account qualifies (same JSON,
  same sequencing; a login is added); (c) SBE.
- Recommendation: (a) now, (b) when an account qualifies: 100 ms batching hides
  the order of events within each interval from the simulator.
- Depends on it: queue-position accuracy of simulated fills.

**3. Window depth and chunk size sent to OB.**
50 levels per side reach OB (`window_depth`), in synthetic orders of 0.01 BTC
(spot) and 1 contract = 0.01 BTC (swap).
- Options: other depths (up to 400) or chunk sizes.
- Recommendation: keep 50 and the chunks until a strategy needs more; the 30-minute
  OB comparisons used them.
- Depends on it: OB memory and update cost; granularity of simulated queue
  position.

**4. libframe built in this directory.**
OB rejects records whose venue code is not valid in the enum libframe was compiled
with. The installed `/Users/vm/kaspar-hft/lib/libframe.a` predates the OKX codes,
so the Makefiles compile libframe from this tree into `build/`.
- Options: (a) keep the local build; (b) rebuild the installed libraries from a
  tree with the new `en::x` codes and drop the local build.
- Recommendation: (b) when the crypto venues merge.
- Depends on it: link time; any mismatch between the two libframes.

## Settled

- **Snapshot alignment.** The snapshot is pushed on subscribe; no REST snapshot
  is used for sync. Settled by the documentation and the live run (1 snapshot per
  instrument, 0 gaps in 32 minutes).
- **Gap rule.** `prevSeqId` must equal the previous `seqId`; heartbeats
  (`seqId == prevSeqId`) and maintenance resets (`seqId < prevSeqId`) obey it.
  Tested with the documentation's sequence example and injected gaps.
- **Depth limit.** Levels leaving the top 400 arrive as deletes: the book never
  exceeded 400 per side, and REST at the same `seqId` matched over all 400 levels.
- **Trade side.** `side` is the taker's (documentation).
- **Trade ordering.** A trade's `seqId` places it relative to the book; live,
  every trade (43 303) was ahead of the last book update applied.

## Low impact

- **Order count per level** is decoded but not used: Mbp2Mbo makes its own chunk
  orders.
- **`trades` versus `trades-all`.** `trades` aggregates per taker order and price;
  volume is the same, the print count is lower. `tradeId` continuity is checked
  with `count`.
- **Priority within a level.** Not published (price-level feed); OB's rule
  through Mbp2Mbo is assumed, as for the other crypto feeds.
