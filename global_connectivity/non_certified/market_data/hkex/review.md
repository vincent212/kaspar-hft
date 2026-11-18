# HKEX OMD-C SF handler: review list

Built from section 10 of `feed.md`. Every section-10 row appears in exactly one of the
three sections below. No sample capture has been checked: HKEX publishes none.

## Needs your decision

**1. Whether to buy a capture.**
- Question: do we buy a third-party OMD-C SF capture (for example LSEG Tick History PCAP)
  before building, or build from the specification alone?
- Options: (a) buy a capture covering a full day, including 09:00-09:30 and 16:00-16:10;
  (b) build from the specification and verify on the first live recording.
- Recommendation: (a). The field tables have never been checked against bytes, and items
  2, 3 and 5 below can only be settled from real data; each one changes simulated fills
  or the start-of-day sequence.
- Depends on it: the wire layout of every message (section-10 row "Do the field tables
  match the wire?"), and items 2, 3 and 5.

**2. How executions are identified.**
- Question: how does the handler turn Trade 50, which has no OrderId and no side, into
  order-level executions (`l3_mbo_trd_v2_t`) that fill simulated orders?
- Options: (a) read the whole packet, match each on-book Trade (TrdType 0, 100) to size
  reductions (Modify to smaller, or Delete) at its price in the same packet, in message
  order; reductions in other packets are plain changes and deletes; (b) as (a), but hold
  records until the next packet to match across a packet boundary; (c) send no
  `l3_mbo_trd_v2_t`, prints only; (d) if HKEX does not send Modify/Delete for executed
  orders, reduce the front of the queue at the Trade's price in the handler.
- Recommendation: (a). It needs no assumption about message order inside a packet and
  adds no latency beyond one packet; (b) delays every record by a packet; (c) leaves
  passive simulated orders unfilled; (d) contradicts the reading that order messages
  carry every book change, which a capture can check.
- Depends on it: passive fills of simulated orders, the print's `side` and
  `aggresorSide`, and whether the book stays uncrossed. Merges the section-10 rows
  "Are executions reported as Modify/Delete Order…" and "Order of a Trade and the
  Modify/Delete messages…".

**3. Book behaviour during auctions, and market orders at price 0.**
- Question: what does the handler do with the board-lot book during the pre-opening and
  closing auction sessions, when SPEC p.23 says full order book information is not
  available, and with market orders (OrderType 1, Price 0)?
- Options: (a) keep the book as it stood, status non-matching (21, 24, 25), auction
  trades (TrdType 103) as prints only, market orders kept in the order map with no
  record; (b) clear the book at the start of each auction and rebuild from the Add Orders
  that follow; (c) place market orders at the best price on their side.
- Recommendation: (a). It sends OB nothing the feed did not say; OB does not accept a
  zero price; (b) and (c) invent book state.
- Depends on it: the book OB holds at 09:30 and 16:10, fills at the open and close, and
  whether the book crosses when matching resumes.

**4. Scope of the clear on Sequence Reset.**
- Question: does a Sequence Reset on one order book channel clear only that channel's
  securities or every channel?
- Options: (a) that channel only (DG p.25, which SPEC 1.41 says it was aligned with);
  (b) every channel (SPEC p.7 and p.12 wording).
- Recommendation: (a), with the channel's securities taken from those seen on it that
  day, since the assignment to channels 30-38 changes daily. (b) clears books that are
  still correct and forces nine refresh rebuilds.
- Depends on it: which books receive `l3_chr_v2_t` and are rebuilt from refresh after an
  intraday Sequence Reset.

**5. Sequence number of the Sequence Reset message.**
- Question: is Sequence Reset outside the sequence (first data message is 1), or is it
  message 1 (data starts at 2)?
- Options: (a) outside: after it, next expected = 1 (DG p.25 "should have SeqNum 1");
  (b) it is message 1 (SPEC p.7 "carries sequence number 1").
- Recommendation: (a), as DG p.25 describes the procedure. If wrong, the handler reports
  a one-message gap at every start of day and requests it from the RTS.
- Depends on it: gap detection at start of day on every channel, and RTS requests
  against the 1,000-a-day limit.

**6. Refresh cycle older than the real-time cache.**
- Question: when a refresh cycle's LastSeqNum is below the first cached real-time
  message − 1, does the handler fill the hole from the RTS or wait for the next cycle?
- Options: (a) fill LastSeqNum+1 to first cached − 1 from the RTS when within its limits
  (last 50,000 messages, 10,000 per request), otherwise wait; (b) always wait.
- Recommendation: (a). It shortens recovery; (b) is simpler and never touches the daily
  request limit.
- Depends on it: recovery time after a gap, and RTS request usage.

## Settled

- Priority on Modify Order: the venue says nothing, so action 1 and OB's rule (size down
  keeps priority, size up loses it); settled by kaspar_mapping.md, priority on a change.
- Event boundaries: `endOfEvent` on every record, an execution's two records being one
  event, `transactTime` strictly increasing per event; settled by kaspar_mapping.md,
  event boundaries.
- Refresh LastSeqNum 0: nothing to discard, next expected 1; settled by the DG p.19 note.
- Snapshot alignment: apply cached messages above LastSeqNum; settled by SPEC p.49 and
  DG p.19, p.25 against the DG Appendix E pseudo code.
- Heartbeats and gaps: a heartbeat does not advance the sequence, but SeqNum >= next
  reveals loss; settled by DG Table 3.
- RTS request header SeqNum and SendTime: send 0; settled by SPEC p.13 and DG p.13-14,
  which both accept it.
- CurrencyFactor: applies to CurrencyRate only; settled by the two examples on SPEC p.21.
- Aggregate order book example 4 quantity: 150; settled by applying the example's
  entries in the test.
- DG p.7 example struct SeqNum width: Uint32; settled by SPEC p.11 and DG Table 2.
- Security Definition length after NoUnderlyingSecurities: fixed part 544, then 8-byte
  entries; settled by the total length formula on SPEC p.19.

## Low impact

- Queue order within a level from refresh Add Orders: refresh order is taken as time
  priority; the handler has no other source of order, and it affects only books rebuilt
  intraday.
- OrderId uniqueness across board-lot and odd-lot books: the odd lot channels are not
  routed to OB, so the board-lot map never sees odd-lot ids.
- Trade Cancel: no record for a bust, so the cancelled print stays in volume; OB fills
  from `l3_mbo_trd_v2_t`, not prints, so the book and fills are unchanged.
- Trading Session Status racing order data: a status takes effect a few messages early
  or late around a session change, where the book is already in transition.
- CME codes for the auction phases (21, 24, 25, 15, 26): only 17 changes OB's
  behaviour; the other codes are logged.
- Quantities above 2^31 − 1 in `lastQty` and `sz`: logged and capped; not expected for
  board-lot orders.
