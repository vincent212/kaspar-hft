# T7 EOBI: items for review

Built from section 10 of `feed.md`. Every section-10 row appears once below. No EOBI
capture was available, so nothing here was settled with real data.

## Needs your decision

**1. Volume from fills against non-visible liquidity**

- Question: should volume include fills against hidden quantity, derivatives market
  orders and synthetic matches, which only the Execution Summary reports?
- Options:
  a. Prints from Partial/Full Order Executions only; those fills are missing from volume.
  b. As (a), plus one print per Execution Summary for LastQty minus the executions'
     LastQty in the aggressor's instrument, at LastPx.
  c. Prints from the Execution Summary only: one print per match event at the worst
     price, and none for uncrossing matches, which have no Execution Summary.
- Recommendation: (b). The step prints keep their exact prices, and the residual adds the
  missing quantity; its price is the worst price of the match event, which is the only
  price the feed gives for it.
- Depends on it: volume counted from `l3_mbp_trd_t` on T7 instruments, which the
  percentage-of-volume light uses. The book and simulated fills do not change.

**2. Cash market orders resting in the book**

- Question: where do visible cash market orders (OrdType 1, no price) go, given that OB
  takes no order without a price?
- Options: a. keep them out of OB, tracked in the handler's map; b. place them at the
  best price on their side; c. place them at a sentinel price level.
- Recommendation: (a). The feed gives no price, (b) moves the order every time the best
  price moves, which the feed does not report, and (c) puts a price into OB that never
  traded. Executions against these orders still print.
- Depends on it: queue length ahead of simulated orders in Xetra and Börse Frankfurt
  books when market orders rest (mainly in auctions); derivatives are unaffected (their
  market orders are not visible).

**3. Scaled simple instruments**

- Question: are scaled simple instruments routed to OB while the meaning of
  QuantityScalingFactor is unknown (the manual does not describe it; the T7 Functional
  Reference was not obtained)?
- Options: a. not routed until the meaning is known; b. routed with quantities as sent;
  c. routed with quantities divided or multiplied by the factor.
- Recommendation: (a). (b) and (c) can give sizes off by the factor, and a wrong size
  changes queue positions and fills without any error. Complex instruments are excluded
  from OB anyway (they can trade at zero or negative prices), so the factor matters only
  here.
- Depends on it: whether Eurex scaled simple instruments can be simulated; the instrument
  loader's routing list.

## Settled

- Existence and layout of 13302 and 13402: both in the XML representation; decoded
  whether or not R15.0 sends them.
- Counter type: unsigned, no value 0xFF/0xFFFF, per the XML and `EOBILayouts.h`.
- Heartbeat MsgSeqNum: the manual marks it "not used" (M p.42); the gap check uses
  LastMsgSeqNumProcessed only. A capture would confirm.
- Order identity: handler-assigned 64-bit counter per channel, kept across Order Modify;
  kaspar_mapping.md requires a unique orderID that is the same on both records of a
  delete+add, which a timestamp-derived id cannot give.
- Order Modify in Out of Main Session Trading (price change, priority kept): action 1;
  OB resets priority. kaspar_mapping.md's priority table makes this a known deviation.
- 13106 with a price change or larger size: action 1, logged; known deviation under the
  same table.
- Open-book auction trades reported by both executions and Trade Report: one print per
  (SecurityID, TrdMatchID), per kaspar_mapping.md's one-print-per-trade rule and the
  manual's TrdMatchID.
- Execution price: `l3_mbo_trd_v2_t` fills at the book price and the print carries LastPx,
  as kaspar_mapping.md specifies.
- Gap scope: gap record to every book on the channel (kaspar_mapping.md); rebuild only
  products whose MsgSeqNum shows a gap, since MsgSeqNum is contiguous per product
  (M p.31).
- Snapshot Orders without SecurityID: discard the product's cycle on a snapshot gap and
  check TotNoOrders; follows from the manual.
- SecurityID int64: the instrument loader maps it to the Kaspar asset id
  (kaspar_mapping.md).
- Quantity decimals: per-instrument quantity unit (kaspar_mapping.md); RDI/RDF tells
  whether any instrument needs a unit other than 1.
- Pad2 in Add Complex Instrument: padding, per the XML.

## Low impact

- Aggressor modified to cross: the end-of-unit check removes the old order whichever
  message T7 uses, so the book is the same in every case.
- Units of work spanning datagrams: tracked per product, which gives the same result if
  units never interleave.
- Restart detection during fail-over: rare; the reading (reset flag plus later
  TransactTime) drops reordered packets and still detects a restart.
- DSCP reference to A.6: editorial; the appendix is A.5.
- T7 markets other than Eurex and Xetra: outside the manual and this handler.
- Trade Reversal: no record reverses a print; reversals are rare manual actions by
  Market Supervision, and the book is unaffected.
- Auction BBO, Auction Clearing Price, Top Of Book: published while OB is not matching
  (auctions) or after continuous trading; the book is unaffected.
- Executions without a transaction time: `transactTime` from the Execution Summary's
  ExecID, or the packet send time when there is none; record order and book state are
  unchanged.
- Simple instruments with zero or negative prices: complex instruments, where such prices
  occur, are already excluded; a simple instrument that shows one is withdrawn from OB
  and logged, so it cannot corrupt a book.
- Product state versus instrument state: the combined rule and the instrument state alone
  differ only when the product is not Open and Continuous while an instrument reports
  Continuous, which the manual does not describe.
- Börse Frankfurt has no `en::x` code: `venue` is XETRA on cash channels; OB uses `venue`
  only to label its internal order references, so the book does not change.
