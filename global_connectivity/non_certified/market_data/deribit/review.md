# Deribit market data handler: review list

Built from section 10 of `feed.md`. Answer the items in "Needs your decision" by
number.

## Needs your decision

**1. A Put of an order already on the book.**
Deribit's docs (updated 18.11.2026) say `sortOrderId` "never changes once it
appears in market data". The only Starbase data available, Deribit's sample PCAP
of 2026-04-29, has 283 Puts that give an order already on the book a new
`sortOrderId`, and none that keep it. The handler sends a Put with a new
`sortOrderId` as delete then add (priority lost) and a Put with the same one as a
change (OB's rule: priority kept if the size does not grow).
- Options: (a) as built; (b) treat every Put of a known order as delete then add.
- Recommendation: (a). It follows the explicit key, whichever behaviour the live
  feed has. (b) would differ only for a Put that keeps its `sortOrderId` and
  grows, which OB already moves to the back.
- Depends on it: simulated queue position after an amend.

**2. Snapshot order within a level.**
The docs say snapshots list each level in execution priority. In the PCAP, 135
snapshot levels are not in `sortOrderId` order (all 8,259 compared snapshots are
equal to the rebuilt book as sets of orders). The handler sorts recovery adds by
`sortOrderId` and ignores the snapshot's order.
- Options: (a) sort by `sortOrderId` (as built); (b) keep the snapshot's order.
- Recommendation: (a): `sortOrderId` is the documented priority key.
- Depends on it: queue position of simulated orders after a resync.

**3. Quantity unit for OB.**
`displayQty` = qty / `minOrderQuantity`, rounded up, so an order below one
minimum (seen only in options) shows as one unit.
- Options: (a) as built; (b) a configured unit per instrument.
- Recommendation: (a) for futures and perpetuals, where every quantity is a
  multiple of the minimum (`qty_off_step` 0 in the PCAP).

**4. SETTLEMENT and LOCKED status.**
Both are mapped to TradingHalt (2), INACTIVE to 18, DELIVERED to Close (4), OPEN
to 17.
- Options: (a) as built; (b) SETTLEMENT to 17 (Deribit's daily settlement pauses
  trading for a few seconds; error 10041 settlement_in_progress).
- Recommendation: (a): no trading happens in either state.

**5. Block trades.**
BlockTrade (33) produces no record: it is off-book.
- Options: (a) none (as built); (b) an `l3_mbp_trd_t` print, which counts it as
  volume.
- Recommendation: (a) for simulation, which fills against on-book volume.

**6. JSON book interval: `100ms` without an account, or `raw` with one.**
Without an account only the aggregated intervals are available; `100ms` is used.
A REST snapshot's `change_id` then usually falls inside an aggregate, so the live
comparison is of the top levels nearest in `change_id`, not exact.
- Options: (a) `100ms` (as built); (b) `raw` with an authenticated connection
  (test or production key), which also makes the REST comparison exact.
- Recommendation: (a) for public monitoring; (b) wherever an account exists.
- Depends on it: how stale the JSON-fed OB is (up to 100 ms).

**7. JSON window and chunk.**
`LevelWindow` sends the best 50 levels per side; `Mbp2Mbo` cuts each into orders
of 1,000 USD (100 contracts) in the live check.
- Options: (a) as built; (b) a chunk of one contract (10 USD), 100 times more OB
  orders.
- Recommendation: (a).

## Settled from the data

- Starbase decoding: the generated codecs and the hand-written packet framing give
  output identical to Deribit's Python SDK for the whole sample PCAP (`make
  sdk-check`, 13,306 packets).
- Starbase synchronisation: all 1,849 instruments in the PCAP synchronise from
  their snapshots with 0 gaps, 0 duplicates and 0 references to unknown orders.
- JSON sync: no `change_id` gap and no crossed book in the live run (README.md).
