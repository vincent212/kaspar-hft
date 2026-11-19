# IEX DEEP+ handler: review list

Built from section 10 of `feed.md`. Every section-10 item appears once below. Answer the
items in "Needs your decision" by number.

## Needs your decision

**1. Source of a DEEP+ and SNAP capture.**
No sample capture is reachable (the pcap links on IEX's market-data page return GCS
403/401 and Azure 404), so the field tables are checked only against the specification's
examples.
- Options: (a) ask IEX Market Ops for a DEEP+ multicast capture and a SNAP response, when
  requesting the SNAP authentication token; (b) record one from a live connection;
  (c) build the handler on the tables alone.
- Recommendation: (a), falling back to (b). The handler depends on the field layouts
  being right, and only data can show that.
- Depends on it: confirmation of the field layouts; the measured frequency of the
  maintain-priority deviation; how often an Add reuses a live Order ID; whether DEEP
  Auction Information and Official Price messages appear; the SnapshotData layout and
  header sequence numbers.

**2. Replay of data from before October 2024.**
DPLS v1.01 (September 30, 2024) changed the Add Order type byte and does not give the old
value; the handler decodes only `a` (0x61).
- Options: (a) support data from October 2024 on only; (b) find the old byte from a
  pre-October-2024 capture and accept both.
- Recommendation: (a), unless older replays are needed. With (a), an older capture
  decodes every Add as `unknown` and builds an empty book.
- Depends on it: whether historical replay before October 2024 works.

**3. SNAP alignment: is the message at the Snapshot Sequence Number N in the snapshot?**
SNAP p.4 says to apply buffered messages with "higher sequence numbers"; p.11 calls N the
sequence "at which the Snapshot was created".
- Options: (a) N is in the snapshot, apply > N; (b) N is not, apply >= N.
- Recommendation: (a), the literal reading of p.4. Confirm by comparing a snapshot with
  the book rebuilt from the live feed.
- Depends on it: every SNAP recovery; the wrong choice applies one message twice or
  drops it (a duplicated add, an execution counted twice, or a missed change).

**4. Queue order within a price level after a SNAP recovery.**
Snapshot Add Orders are not in feed order, and a modified order carries the
modification's Timestamp (SNAP p.12).
- Options: (a) queue by Add Order Timestamp ascending (the time of the last priority
  reset), ties by message order; (b) queue in snapshot message order; (c) treat the order
  as unknown.
- Recommendation: (a). The Timestamp is the time the order last took its place in the
  queue, which is what priority follows. A maintain-priority modify also carries a new
  Timestamp, so (a) can misplace such orders; ask IEX which Timestamp the snapshot gives.
- Depends on it: simulated queue position after every SNAP recovery.

**5. Aggressor side of Order Executed prints.**
DEEP+ does not report the aggressor; an Order Executed names the resting order (DPLS
p.24).
- Options: (a) `aggresorSide` opposite to the resting order's side, 0 for single-price
  crosses (Sale Condition 0x08); (b) 0 on every print.
- Recommendation: (a). In continuous trading the resting order is the passive side, so
  the other side is the aggressor; auction crosses have none.
- Depends on it: the aggressor side seen by anything reading the prints (buy and sell
  volume, the shadow algorithm's trade signals).

**6. Trading state mapping.**
OB matches only in state 17. Trading Status O and P are order acceptance periods (DPLS
p.15-16); system hours run from System Event S to E (DPLS p.13); the specification does
not say whether the book can be crossed in any state.
- Options: (a) O and P → 21 PreOpen, before S → 21, after E → 4 Close; (b) O and P → 2
  TradingHalt; (c) after E → 26 PostClose; (d) no status from System Events, T → 17 at
  all hours.
- Recommendation: (a). O and P accept orders without matching, ahead of an auction, which
  is PreOpen; outside system hours IEX does not trade.
- Depends on it: when OB accepts a crossed book without error, and when simulated orders
  can fill.

## Settled

- Modify with Reset Priority at the same price and a smaller or equal size: delete then
  add; settled by the explicit flag (DPLS p.22) and kaspar_mapping.md.
- Modify with Maintain Priority and a larger size or a new price: cannot be expressed;
  a known deviation under kaspar_mapping.md (OB resets priority). Its frequency comes
  from a capture (item 1).
- Order Executed at a price other than the booking price: the print carries the
  execution price, the order-level execution fills at the booking price; settled by
  kaspar_mapping.md.
- `endOfEvent`: on the last record of every DEEP+ message; settled by kaspar_mapping.md
  (no transaction markers in the protocol).
- Clear Book: `l3_chr_v2_t` to that symbol's book only; settled by kaspar_mapping.md.
- 64-bit sequence numbers: low 32 bits in `l3_eop_t.msgSeqNum` and `pkt_seq_num`;
  settled by kaspar_mapping.md.
- Trading Status H and O run together in the PDF: two values, settled by DEEP p.10.
- SNAP Message Length typed Integer with length 2: 2-byte, settled by the examples.
- Session change: a new Session ID resets the channel (TP p.3-4, kaspar_mapping.md
  channel reset); System Event C ends the session without clearing books (DPLS p.13).

## Low impact

- SnapshotData example's extra length: the handler detects both layouts (the u16 at the
  data start equals block length − 2 only in the printed one).
- Sequence numbers in SnapshotData IEX-TP headers: the handler does not check them for
  continuity, so either reading gives the same book.
- Scope of Trade `T`: the handler prints every L and T as sent; no reading changes its
  output.
- DEEP Auction Information and Official Price in DEEP+: if sent, they decode as `unknown`
  and are skipped by length; neither affects the book.
- Example timestamps in New York time: the handler takes Timestamps as UTC (DPLS p.5) and
  derives no state from clock time.
- Order ID scope across channels: the order map is per channel, so the answer does not
  change the handler; an Add with a live ID is sent as delete then add and logged.
- Trade Break: no record expresses it; the book is unchanged (DPLS p.26) and only volume
  is overstated by the broken size.
- Timestamp Relationships mention Price Level Update messages: the handler does not rely
  on the ordering rule.
- Odd-lot orders and tick sizes: orders are passed as sent; ticks come from the
  instrument source.

## Decisions (owner, 2026-11-18)

- 1: ask IEX Market Ops for a DEEP+ and SNAP capture, else record one live (a)
- 2: support data from October 2024 on only (a)
- 3: N is in the snapshot; apply buffered messages > N, with a boundary check logged (a)
- 4: queue by Add Order Timestamp ascending, ties by message order (a)
- 5: aggressor opposite to the resting order's side; 0 for crosses (a)
- 6: O and P -> 21, before S -> 21, after E -> 4 (a)
