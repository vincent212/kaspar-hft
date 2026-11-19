# Feed description: Deribit market data (Starbase SBE multicast and JSON-RPC book)

Not certified. Two feeds are covered: Starbase, the order-by-order SBE multicast
of Deribit's new matching engine (reachable only from LD4 or AWS PrivateLink),
and the public JSON-RPC WebSocket book, a price-level feed reachable from the
internet. Citations are to docs.deribit.com pages as downloaded on 2026-11-18
into `build/docs/` by the Makefile.

## 1. Venue and documents

| Document | Version | Date | Applies to |
|---|---|---|---|
| `deribit-sbe-market-data-api.xml` (SBE schema, package `cb.deribit.gateway.sbe.marketdata.api`, id 2102) | schema version 2, semanticVersion 1.0 | fetched 2026-11-18 | Starbase decoding; codecs generated from it |
| starbase/binary-api-reference | web page | fetched 2026-11-18 | framing, sequencing, `sortOrderId` |
| starbase/order-book-maintenance | web page | fetched 2026-11-18 | sync rule, queue priority, gap recovery |
| starbase/multicast-channels, starbase/trades, starbase/overview | web pages | fetched 2026-11-18 | channels, trade messages, access |
| changelogs/starbase, "Starbase Update 18.11.2026" | web page | 2026-11-18 | `sortOrderId` now assigned when an order rests |
| `starbase-market-data.pcap` (statics.deribit.com) | sample capture | packets from 2026-04-29 08:18 UTC | the only Starbase data available |
| Starbase market-data SDK 2.0 (`starbase-deribit-md-sdk-2.0.zip`, Python) | 2.0 | fetched 2026-11-18 | reference decoder (`make sdk-check`) |
| subscriptions/orderbook/bookinstrument_nameinterval | AsyncAPI page | fetched 2026-11-18 | JSON book channel |
| subscriptions/trades/tradesinstrument_nameinterval | AsyncAPI page | fetched 2026-11-18 | JSON trades channel |
| api-reference/market-data/public-get_order_book | OpenAPI page | fetched 2026-11-18 | REST snapshot for the live comparison |
| articles/market-data-collection-best-practices, articles/rate-limits | web pages | fetched 2026-11-18 | raw vs aggregated, limits |

The sample PCAP predates the 18.11.2026 change to `sortOrderId` (section 10).

## 2. Transport

**Starbase.** UDP multicast, A and B feeds, channels sharded by product group;
incremental port 4220, snapshot port 4230, plus test channels
(starbase/multicast-channels). Access only from an LD4 cross-connect or hosted
colocation, or AWS PrivateLink (starbase/overview). A Retransmit Gateway
answers RetransmitRequest (200) over TCP from the same networks
(starbase/order-book-maintenance). None of this was reachable here: the feed
was exercised only from the sample PCAP.

**JSON-RPC.** `wss://www.deribit.com/ws/api/v2` (test:
`test.deribit.com`). `public/subscribe` with
`book.<instrument>.<interval>` and `trades.<instrument>.<interval>`.
Intervals `raw`, `100ms`, `agg2`; `raw` (1 ms aggregation) "is only available
to authorized users" (bookinstrument_nameinterval), so without an account the
finest book is `100ms`, which this handler uses. `public/set_heartbeat`
makes the server send `test_request`, answered with `public/test`
(`DeribitWs.hpp`). REST snapshot: `GET /api/v2/public/get_order_book`
(depth parameter); public non-matching-engine requests are limited by a credit
pool of about 20 requests a second (articles/rate-limits); the live check
sends one every 10 s.

## 3. Framing

Starbase, little-endian (schema `byteOrder`):

- packet = `PacketHeader` (24 bytes: `sendingTime` int64 ns, `seqNum` int64,
  `channelId` int32, `type` uint16 bit set {incremental, snapshot,
  retransmit}, `messageCount` uint16) + `messageCount` messages. The schema
  defines `PacketHeader` but no message uses it, so the SBE tool does not
  generate it; its layout is written by hand in `Starbase.cpp` and checked
  against the SDK (`make sdk-check`).
- message = `mdMessageHeader` (16 bytes: `messageLength`, `templateId`,
  `schemaId`, `version`, `flags` {startOfTransaction, endOfTransaction},
  `transactTime` int64 ns) + body. `messageLength` counts header and body and
  is padded to a multiple of 8 (observed in the PCAP; `Starbase.cpp`).
- a packet with `messageCount` 0 is a heartbeat (every 5 s) carrying the next
  expected sequence number.
- prices are `Price9` (int64 mantissa, exponent -9); quantities int64 in the
  instrument's units.

JSON-RPC: one JSON object per WebSocket text frame.

## 4. Message catalogue

Starbase messages are decoded by codecs generated from the schema with the
real-logic SBE tool (1.30.0, the jar genschema/ uses), never by hand
(`make gen`). Messages handled: InstrumentDefinition (10),
InstrumentStatusUpdate (16), BidPut/AskPut (20/21: `orderId`, `price`,
`qty`, `sortOrderId`), BidQtyReduced/AskQtyReduced (22/23),
BidDelete/AskDelete (24/25), TradeSummary (30: `takerOrderId`,
`takerFlags.isSell`), Trade (31: `makerOrderId`, `matchId`, `price`,
`qty`), BlockTrade (33), SnapshotHeader/SnapshotTrailer (100/101:
`instrumentId`, `incrementalSeqNum`), EndOfCycle (119). IndexDefinition
(11), IndexInfo (12), InstrumentInfo (14), InstrumentRef (15) are decoded and
not used; RetransmitRequest/Reject (200/202) belong to the TCP gateway.

JSON book notification `data`: `type` (`snapshot` or `change`),
`instrument_name`, `timestamp` (ms), `change_id`, `prev_change_id` (absent
on the first), `bids` and `asks` as `[action, price, amount]` with action
`new`, `change`, `delete`. JSON trade: `trade_id`, `trade_seq`,
`timestamp` (ms), `price`, `amount`, `direction`.

## 5. Sequencing and recovery

**Starbase** (starbase/order-book-maintenance, "Maintaining the order book"):

- Sequence numbers are per channel. The packet header holds the first
  message's number; message i has `seqNum + i`. Higher than expected is a gap,
  lower a duplicate (dropped).
- Join: listen to the incremental channel and buffer; read the snapshot
  channel. Each instrument's snapshot is SnapshotHeader, its Puts,
  SnapshotTrailer; `incrementalSeqNum` (in header and trailer, which must
  agree) is the last incremental message already in it. Apply the buffered
  messages for that instrument with a higher number, drop the rest.
- After a gap every instrument on the channel is unsynchronised (a gap record
  goes to each routed book) until a snapshot whose `incrementalSeqNum` is at
  or past the last missing number. The Retransmit Gateway is not used (not
  reachable from here).
- Snapshot Puts arrive "starting with the first-to-execute order, in sequence
  of execution priority".

**JSON-RPC** (bookinstrument_nameinterval): the first notification is the
whole book (every level, no depth limit); each later one carries
`prev_change_id`, which must equal the previous `change_id`. Scope: the
subscription. A mismatch is a gap: the handler resubscribes and starts again
from the new full snapshot. There is no checksum. The REST snapshot carries
`change_id`, so where the stream delivered that exact `change_id` the two
books can be compared exactly; with `100ms` aggregation the snapshot's
`change_id` usually falls inside an aggregate and is not delivered.

## 6. Semantics

**Starbase**: market by order, every resting order, full depth. Queue
priority is explicit: each level is a FIFO ordered by `sortOrderId` (smaller
first); it is "assigned when the order rests", "strictly increasing, not
consecutive", and "never changes once visible" (order-book-maintenance,
"Queue priority"). Put of an unknown order adds it; QtyReduced keeps priority;
Delete removes. Trades: TradeSummary (aggressor side from
`takerFlags.isSell`) then one Trade per maker fill with `makerOrderId`; a
Trade with a null `makerOrderId` is an implied fill. The book changes of the
fills (QtyReduced or Delete of the maker) are in the same transaction
(start/end flags). BlockTrade is off-book. InstrumentStatus values: 0 OPEN,
1 INACTIVE, 2 SETTLEMENT, 3 DELIVERED, 4 LOCKED, 5 HALTED.

**JSON-RPC**: price levels; amounts are absolute (a `change` gives the new
size, `delete` removes the level); for futures and perpetuals `amount` is
USD, for options it is coin. Trade `direction` is "the taker's"
(tradesinstrument_nameinterval), so a `sell` hit the bid.

## 7. Mapping to Kaspar

Starbase (`StarbaseBook`):

| Venue message | Kaspar record | Notes |
|---|---|---|
| Put of an unknown order | `l3_mbo_v2_t` action 0 | |
| Put of a known order, same `sortOrderId` | action 1 | priority kept: same `sortOrderId` |
| Put of a known order, new `sortOrderId` | action 2 then action 0, same `orderID`, same time, `endOfEvent` only on the add | priority reset, as `kaspar_mapping.md` requires |
| QtyReduced | action 1 | smaller size, OB keeps priority, as Deribit does |
| Delete | action 2 | |
| Trade with a maker | `l3_mbo_trd_v2_t` placed before that maker's book change in the transaction, then `l3_mbp_trd_t` | the execution must precede the change wherever Starbase put the Trade |
| Trade without a maker (implied) | `l3_mbp_trd_t` only | |
| BlockTrade | none | off-book; not volume on the book |
| InstrumentStatus | `l3_sst_t`: OPEN 17, INACTIVE 18, SETTLEMENT 2, DELIVERED 4, LOCKED 2, HALTED 2 | review.md |
| synchronisation | `l3_chr_v2_t`, then every order as a recovery add in `sortOrderId` order | OB orders by arrival, so recovery adds must come in priority order |
| gap | `l3_gap_v2_t` to every routed book on the channel | |
| end of packet | `l3_eop_t` (low 32 bits of the sequence) and `l3_eob_t` when no transaction is open | |

Identifiers: `orderId` (int64, unique across the venue) is the `orderID`;
`instrumentId` maps to the Kaspar asset through the `route` callback.
Quantities: `displayQty` = qty / `minOrderQuantity`, rounded up (review.md).
Prices: `pxd` = price9 / `tickSize9`. `endOfEvent` on the last record of each
Starbase transaction per instrument. Instruments that can have zero or
negative prices (combos) are kept but not routed.

JSON-RPC (`DeribitBookSync`): levels to `sim::LevelWindow` (best 50 per side)
to `sim::Mbp2Mbo` (levels to synthetic orders of `chunk` USD); trades to
`l3_mbp_trd_t` with the aggressor from `direction`. A resubscription sends
`l3_chr_v2_t` before the new snapshot (section 10).

## 8. Instruments

Starbase: InstrumentDefinition (10) on the snapshot channel: `instrumentId`,
name, `tickSize9`, `minOrderQuantity`, status. JSON-RPC: configured per
instrument (tick 0.5 and contract size 10 USD for BTC-PERPETUAL from
`public/get_instrument` on 2026-11-18).

## 9. Pitfalls

| Item (`pitfalls.md`) | How it applies |
|---|---|
| 1 several documents | one schema version (2); the 18.11.2026 changelog changes `sortOrderId` semantics, and the sample PCAP predates it |
| 1 length counts itself | `messageLength` counts the 16-byte header; padded to 8 |
| 1 two layers in one table | PacketHeader is a schema composite no message uses; decoded by hand, checked against the SDK |
| 2 endianness, fixed point | little-endian; Price9 exponent -9 |
| 2 null values | `makerOrderId` null (INT64 min) marks an implied fill |
| 3 sequence scope | per channel (Starbase); per subscription (JSON `change_id`) |
| 3 snapshot alignment | per instrument, apply seq > `incrementalSeqNum` |
| 3 recovery paths rare | gap, duplicate and heartbeat-gap injected in `test_starbase_book.cpp` |
| 3 state to clear | order maps are per instrument and rebuilt on resync |
| 4 priority on modify | explicit `sortOrderId`; translated as section 7 |
| 4 trades also as book deltas | yes: per-order execution plus the maker's QtyReduced/Delete; one print per Trade |
| 4 trade side | Starbase `takerFlags.isSell`; JSON `direction` is the taker's |
| 4 trading states | mapped in section 7 |
| 5 tick, quantity | `tickSize9`, `minOrderQuantity` per instrument |
| 6 update-rate options | `raw` needs an account; `100ms` used |
| 6 whole book on subscribe | JSON snapshot is every level (615 bids and 958 asks in the recorded BTC-PERPETUAL snapshot, `tests/data`); LevelWindow keeps 50 |
| 6 connection limits | heartbeat test_request must be answered |

## 10. Ambiguities

| Question | Reading taken | Alternatives | Resolved by |
|---|---|---|---|
| A Put of an order already on the book | a replace: same `sortOrderId` keeps priority (action 1); a new one resets it (delete + add) | treat every Put of a known order as a reset | the PCAP: 286 Puts in its 488 incremental messages, 3 of new orders and 283 of orders already on the book, every one of those with a new `sortOrderId`; no Put of a known order kept its `sortOrderId`, so the action-1 path is exercised only by unit tests |
| Does `sortOrderId` change? | the code handles both: the docs (18.11.2026) say it never changes once visible; the April 2026 PCAP has 283 Puts that give an order already on the book a new one | | not resolved: the PCAP predates the change and the live feed is not reachable |
| Snapshot Puts in priority order | not relied on: recovery adds are sorted by `sortOrderId` | trust snapshot order | PCAP: 135 snapshot levels not in `sortOrderId` order (8,259 snapshots compared, all equal to the rebuilt book as sets) |
| SETTLEMENT and LOCKED | TradingHalt (2) | Pause | review.md |
| JSON resubscription clearing OB | send `l3_chr_v2_t` before the new snapshot | rely on Mbp2Mbo's recovery records | a recording of two subscriptions: OB still in recovery from the first snapshot kept stale levels |
| REST `change_id` inside a 100 ms aggregate | compare the top 20 levels with the stream book nearest in `change_id` and report the share that match | | no exact alignment is possible without `raw` |
