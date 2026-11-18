# EBS Market on MDP 3.0: items for review

Built from section 10 of `feed.md`. Every section-10 row appears once below. No EBS
capture was available, so nothing here was settled with real data.

## Needs your decision

**1. Simulated fills when EBS trades carry no quantity**

- Question: EBS Trade Summary messages have MDEntrySize 0 and NumberOfOrders 0
  (TS; TSX), and there are no order-level executions. How should a trade move the
  synthetic queue that simulated orders sit in?
- Options:
  a. Infer: within one event, the drop in the resting side's quantity at the trade
     price (from the 64 update that follows the trade) is executed from the front of
     the queue; any further drop is a cancel from the back.
  b. No trade-driven fills: every decrease is a cancel from the back (what
     `Mbp2Mbo` does today with a zero-size trade; test `ZeroSizeTradeExecutesNothing`).
     Simulated orders fill only when the level is gone or price moves through them.
  c. Execute the whole level decrease at the trade price from the front, ignoring
     cancels.
- Recommendation: (a). (b) never fills an order at the front of a queue, so a
  passive simulation on EBS would under-fill systematically; (c) counts every cancel
  at the trade price as a fill and over-fills. (a) is exact when nothing else changed
  at that price in the event, and conflation (1 ms on 518/519) bounds how much else
  can. The order of the trade and its book update inside an EBS event is not stated,
  so (a) has to accept the update on either side of the trade within the event.
- Depends on it: queue position and fill simulation for every EBS book (OB via
  `Mbp2Mbo`), and the `Mbp2Mbo` change in `patch_proposal.md` item 6.

**2. Volume for the percentage-of-volume light**

- Question: what volume do EBS prints carry, when the only traded quantity on the feed
  is cumulative electronic volume (template 66), sent 60 s after the trade on the
  Ultra 1 ms channels (VOL)?
- Options:
  a. Print `sz` from the inferred execution quantity of decision 1(a); keep the
     delayed 66 totals as a check, logged when they disagree.
  b. Print `sz` 0; add volume only from 66 differences, 60 s late, with no price.
  c. Do not run the POV light on EBS; record 66 for later comparison.
- Recommendation: (c) until (a) has been compared with 66 on captured data, then (a).
  (b) feeds the light volume a minute late, which a participation rate cannot use.
- Depends on it: `l3_mbp_trd_t.sz` on EBS and the POV light's participation.

**3. Recovery on the UDP channels**

- Question: how does an EBS book recover from a gap or a late start, given that
  `RecoveryProcessor` decodes only the MBO snapshot (53) and EBS sends the MBP
  snapshot (69) on the Market Recovery feed?
- Options:
  a. Add template 69 to `RecoveryProcessor`: rebuild each instrument's
     `MbpLevelBook` from the snapshot, align on LastMsgSeqNumProcessed and RptSeq as
     for futures MBP (SNAP).
  b. TCP Replay for small gaps (feed-type H in CFG), snapshot otherwise.
  c. No recovery: start at the weekly open only; on a gap, mark the books with
     `l3_gap_v2_t` and leave them out of use until a channel reset.
- Recommendation: (a). It is the mechanism CME gives every EBS UDP channel (SPEC),
  the alignment rule is the futures one, and (c) leaves a 24-hour market without books
  after any loss. (b) can follow later. Until (a) exists the proposed `cme_ebs.ini`
  sets `cme_do_recovery false`.
- Depends on it: whether an EBS book is usable after a gap; `RecoveryProcessor.hpp`.

**4. Key from the EBS instrument definition to the Kaspar asset**

- Question: `handler_if` looks assets up by tag 55 Symbol. The EBS values of tag 55
  were not available. Which field keys `universe.csv`?
- Options: a. Asset (6937, product code such as 6EUS); b. Symbol (55);
  c. FXCurrencySymbol (37725, "EUR/USD").
- Recommendation: (a). CME publishes it in the channel configuration and on the CPI
  page, each code appears once per channel in CFG, and it is stable across trade dates.
  (c) is not unique where a pair trades in two sizes (DM: XAU/USD and XAU/USD SM).
  Confirm on the first FX definition seen that each Asset has one SecurityID.
- Depends on it: `config/universe_ebs.csv` names; the lookup in the EBS handler.

**5. Which channels to take**

- Question: 518/519 (1 ms), 528/529 (5 ms, same products), or both; and the NDF
  channels 520/530?
- Options: a. 518 and 519; b. 528 and 529; c. a or b plus 520/530 for NDFs.
- Recommendation: (a). The shorter interval loses fewer book states (decision 1),
  and 518/519/530 are the channels with delayed electronic volume (VOL). NDFs need
  their own pricing and quantity check before they are routed.
- Depends on it: `config/cme_ebs.ini`; conflation error in queue simulation.

**6. Quantity unit (chunk size)**

- Question: OB needs whole units that fit `uint32`; EBS sizes are uint64 base-currency
  amounts. What is one unit?
- Options: a. 1,000,000 base currency for every pair; b. per instrument, the LotType 5
  quantity increment (1231) from the FX definition; c. per instrument, MinTradeVol
  (562).
- Recommendation: (b). It is the granularity orders can have, so a level is always a
  whole number of chunks; (a) is wrong for metals (ounces) and for any pair whose
  increment is not one million; (c) can exceed the increment and round levels up.
  The tests use 1,000,000 as a stand-in.
- Depends on it: OB sizes and simulated order sizes on EBS.

**7. Tick used for OB prices**

- Question: with CPI, resting prices can fall on the alternate tick (example 0.000025
  against 0.00005, CPI). `Mbp2Mbo` sends prices in ticks. Which tick?
- Options: a. AltMinPriceIncrement (37739) where not null, else MinPriceIncrement
  (969); b. MinPriceIncrement always, rounding sub-tick prices; c. a fixed fine tick
  (1e-6) for all.
- Recommendation: (a). (b) merges a sub-tick level with a standard-tick level and
  changes queue sizes; (c) inflates `maxpx` with no gain.
- Depends on it: `units` and `maxpx` in `universe_ebs.csv`; `Mbp2Mbo::Config::tick`.

**8. MDUpdateAction 5 (Overlay) on EBS books**

- Question: MBP lists Overlay (279=5) as a valid EBS value; no page read defines it.
- Options: a. replace price and quantity at MDPriceLevel; b. treat as Change;
  c. treat as a book error and recover.
- Recommendation: (a), the prototype's reading, and ask CME (Global Command Center)
  to confirm. Log every Overlay seen. (b) keeps a stale price if the price changed;
  (c) recovers on a message CME says is valid.
- Depends on it: book correctness whenever CME sends Overlay; `MbpLevelBook::apply`.

## Settled

- Market Best entries (269=w/x): not book data; excluded (CS: "EBS Market Best should
  be excluded from credit screened book management"; it is the top regardless of
  credit, a reference).
- Template 66 on EBS UDP: decoded on 518/519/530. VOL (2026-09-22) is newer than SPEC
  (2024-12-26), which does not list it.
- Channel names, intervals and products: CFG, where CHG differs. CHG says config.xml
  is to be used for product scope and connectivity.
- Size Priority (P) and Institutional Prioritization (V): synthetic FIFO queues;
  known deviation. kaspar_mapping.md: what the records cannot express is recorded,
  not modelled.
- Instrument-level Security Status: routed to that instrument's book only (SST: 48
  present, 1151 null). `handler_if` broadcasts it today (`feed.md` section 7).
- Quantities above 2^31: carried as uint64 until divided by the chunk (MBP:
  uInt64NULL). Test `QuantityAboveInt32SurvivesAsChunks`.
- MarketDepth: read per instrument from tag 264.
- Credit-screened Conflated TCP channels: per-firm books, not a public book; out of
  scope (CS).
- Schema: version 13 already contains every EBS template; `genschema/` unchanged.

## Low impact

- `$` / `#` prefixed product and group codes in CFG (`#6EUS`, `$6SUS`, `#GCUS`): not
  explained in any page read; excluded from the universe.
- 519SB's second source host (164.74.55.39) repeats 519SA's; the other channels pair
  .3x with .13x. Only a source filter would use it.
- ON SEF NDF channels 521/531 still configured; trading ceased 2024-09-27 (NDF).
- Trade date roll (17 with 1174=4): book unchanged; reset the volume total.
- NDF and metal pricing and quantity: spot only in the proposal, plus one metal row
  marked as inferred.
- DisplayFactor: prices used as sent; CME FX Spot+ instruments carry 1 (SECDEF).
- Order of trade and book update within an event: only matters for decision 1(a),
  which accepts either order.
- N2023 read only through a search engine summary (cmegroup.com refused the request);
  the schema version it announces is confirmed by CHG and XML.
