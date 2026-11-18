# Aquis Exchange market data: review list

Built from section 10 of `feed.md`; each of its 23 items appears once below.
Citations as in `feed.md`: MD is the Aquis Market Data Technical
Specification 4.2 (production, August 2026), AQSE the Aquis Stock Exchange
Multicast Market Data Specification 1.2.3. No Aquis capture was available, so
nothing below was settled from data.

## Needs your decision

### 1. One `transactTime` per bundle

**Question.** MD 4.2 marks each sequencer cycle with MD Bundle Start and End
(MD p.11, 3.4.1-3.4.2). The messages inside carry their own timestamps. OB
treats the records sharing one `transactTime` as one transaction and checks the
book for a cross when `transactTime` advances, not at `endOfEvent`
(`frame_kaspr/src/OB.cpp`, `data_handler`). If an aggressive order's messages
carry different timestamps (an Add before the Trades it caused), OB could check
a book that is only half updated.

**Options.**

- (a) Every record of a bundle gets the timestamp of the bundle's first
  message; `endOfEvent` on the last record before Bundle End.
- (b) Each record keeps its message's timestamp; `endOfEvent` as in (a).

**Recommendation.** (a), implemented. Reason: it is the only way the records
can tell OB where Aquis's transaction ends; the cost is that record times inside
a bundle are the bundle's start, not the message's (at most one sequencer cycle
apart). Check on the first test-environment capture how far apart timestamps
inside a bundle are.

**What depends on it.** OB's cross check and the event times seen by the
lights.

### 2. Do Bundle Start and End consume sequence numbers?

**Question.** MD says only that the Heartbeat does not increment the sequence
(MD p.10, 3.2; p.11, 3.3.2); it does not say whether the new bundle markers do.

**Options.**

- (a) Yes: sequenced like every business message.
- (b) No: like Heartbeat.

**Recommendation.** (a), implemented, with `bundles_sequenced = false` for
(b). Reason: the text exempts only Heartbeat. The wrong choice shows at once
as a gap or a duplicate at every bundle (counted in `gaps` and `duplicates`).
Confirm on the first capture or ask Aquis support.

**What depends on it.** Every gap decision, replay requests, and whether the
handler ever reaches a steady live state.

### 3. Snapshot alignment: is message streamSeqNo in the snapshot?

**Question.** MD p.19, 3.7.1: streamSeqNo is "the sequence number of the last
message in the continuous stream which relates to this snapshot". MD p.8, 2.2:
"the sequence number in the continuous stream at the time this snapshot was
captured", which could be the next expected number.

**Options.**

- (a) The snapshot includes message streamSeqNo; apply buffered messages from
  streamSeqNo + 1.
- (b) The snapshot excludes it; apply from streamSeqNo.

**Recommendation.** (a), implemented. Reason: 3.7.1 is the message definition
and says "last message". Off by one in either direction gives a plausible book:
an add applied twice (OB sees a duplicate orderID) or one missing. Check in the
test environment by comparing a snapshot with the stream around its
streamSeqNo.

**What depends on it.** Every recovery by snapshot and every late start.

### 4. A modify that changes nothing

**Question.** "On revision of quantity down the order retains its position in
the book, otherwise the book is re-ordered" (MD p.13, 3.4.5). Read literally, an
Order Modify with the same price and quantity loses priority; OB would keep it.
MD does not say whether such a modify is ever published.

**Options.**

- (a) Delete then add, same `orderID`, same `transactTime` (the
  `kaspar_mapping.md` translation for a venue reset where OB keeps priority).
- (b) A change record (action 1): priority kept.

**Recommendation.** (a), implemented, counted in `same_size_modifies`.
Reason: it follows the text; if the count stays at zero on real data the
choice has no effect.

**What depends on it.** Queue position of simulated orders behind such an
order.

### 5. Can a trade type other than 1 execute a visible order?

**Question.** MD describes orderRef for tradeType 1 only ("zero if against
hidden quantity or a MaC trade", MD p.13, 3.4.6). AoD (6) trades with lit
orders are not described.

**Options.**

- (a) Only type 1 changes the book; other types are prints. A non-zero orderRef
  naming a live order on another type is counted
  (`orderref_on_other_trade`).
- (b) Any trade whose orderRef names a live order executes it.

**Recommendation.** (a), implemented. Reason: it follows the text; the counter
shows on the first capture whether (b) is needed. Under (a), if Aquis did
execute lit orders in AoD without a Modify or Cancel, those orders would stay
on the book with their old size.

**What depends on it.** Book correctness during AoD.

### 6. Which trades are prints, and the MaC feeds

**Question.** Kaspar counts volume from prints. Aquis counts every trade type
(CLOB, AMP, AoD, MaC, LIS, Benchmark, AVM) in total volume (MD p.9, 2.4). MaC
trades are listed as tradeType 3 of the Trade message, but MaC has its own
feeds (MD p.18, 3.6); whether MaC trades also appear on the continuous feed is
not stated.

**Options.**

- (a) A print for every Trade on the continuous feed; MaC feeds not read.
- (b) As (a), and read the MaC feeds for prints only.
- (c) Prints only for trade types that form the lit price (1, 2), the rest
  dropped.

**Recommendation.** (a), implemented, until a capture shows whether type 3
appears on the continuous feed; then (b) if it does not and the strategy's
volume should include the close. (c) if the participation algorithm should
count lit volume only: that is a strategy choice, not a feed one.

**What depends on it.** Volume seen by percentage-of-volume execution.

### 7. Sequence numbers after a failover or restart

**Question.** MD numbers each feed from 1 per trading day (MD p.8, 2.1) and
describes channels from a secondary data centre (MD p.6, 1.1), but not whether
the sequence continues across a failover or how an intraday restart is shown.

**Options.**

- (a) Treat seqNo 1 after higher numbers as a duplicate (current); one handler
  per day.
- (b) Treat it as a channel reset: clear the books and the order table and
  restart at 1.

**Recommendation.** (a) until Aquis answers; ask Aquis support. Reason: (b)
on a stray old packet would wipe every book; (a) on a real restart freezes the
feed, which the duplicate counter and the absence of new records show.

**What depends on it.** Behaviour on Aquis incidents.

### 8. Aquis Stock Exchange

**Question.** AQSE has its own multicast feed (AQSE 1.2.3, decoded by
`aqse_md.tbl`) with market-maker quotes and trade reports. Since 28 July 2025,
19 AQSE securities trade on Aquis UK (Member Notice 2025_0007; 14 rows with MIC
AQSE in the AQXE security file of 2026-11-18).

**Options.**

- (a) No AQSE handler; AQSE securities through Aquis UK.
- (b) An AQSE handler: Quote Add/Replace as one synthetic order per (trader,
  security, side), replaced by delete and add; trade reports as prints or not.

**Recommendation.** (a). Reason: the AQSE book itself is quote driven and the
quote mapping changes queue semantics (a replaced quote loses priority) in a
way that needs its own review; (b) when trading on AQSE itself is wanted.

**What depends on it.** Coverage of AQSE-listed securities.

## Settled

| Item | How it was settled |
|---|---|
| Which document applies | Production MD 4.2, effective 17 August 2026 (Member Notices 2026_0008, 2026_0010); found by listing Aquis's public S3 bucket (aquis.eu answered 429). The task's MD 2.5 and AQSE 1.2.2 are superseded |
| Price change with smaller quantity | Priority lost: a new price is a new queue; OB's own rule, so a change record |
| Modify or Cancel after a type 1 trade | The handler's result is the same either way: Modify is absolute, a Cancel of a removed order is counted and ignored |
| Snapshot completeness | securityCount and entries; every lost-packet pattern breaks a count (tests: incomplete snapshot, next one used) |
| orderRef scope | One order table per feed; `orderID` = orderRef |
| Instrument key | UMTF: 1,107 ISINs repeat in the AQXE security file, no UMTF does |
| Security Definition fields in MN 2026_0008 ("Security Status") | MD's tables and history; a wrong reading would show as a 37-byte Security Status |
| Trading status | 1, 8, 24 with continuous open → 17; closed → 4; Halted → 2; Suspended → 18 (feed.md section 7) |
| Aggressor side | Opposite of the resting order on type 1; 0 otherwise (Aquis publishes no side) |
| Venue codes | `en::x::AQXE`, `en::x::AQEU` |
| A/B arbitration | By message seqNo; Aquis packets have no packet sequence for the `mdp3` arbitration |

## Low impact

| Item | Why |
|---|---|
| Trade Bust | No record can withdraw a print; book unaffected |
| Market-wide Security Status | A message with an unrouted securityID is ignored |
| Replay limits | Unknown; `max_replay` (50,000) sends larger gaps to the snapshot |
| Message order within an aggressive order's cycle | Bundles (decision 1) make it irrelevant to OB's check |
| MaC Book Entry feed | Not used |
| Heartbeats on the snapshot feed | An unknown type there is skipped |
| Stale cross-references (MD 3.5.2-3.5.4, 3.6.4; AQSE 3.5.3, 3.7.2) | Decoding is by type code |
| `fieldgen.py` cannot frame a length after the type | Header fields in every message; the packet reader frames |
| MaC Update without pubTimestamp | As tabled; `length` would show otherwise |
| Version-history dates disagree between editions | Effective dates from Member Notices |
