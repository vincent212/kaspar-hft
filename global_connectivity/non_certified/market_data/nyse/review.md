# NYSE Pillar Integrated Feed handler: review list

Built from section 10 of `feed.md`. Every section-10 item appears once below. Answer the
items in "Needs your decision" by number.

## Needs your decision

**1. Trading status while an auction has not yet uncrossed the book.**
NYSE sends Resume (5) or the Core session change (O) before the reopening or opening
auction executes. In the startup sample, 35 of 1,962 changes into a trading state found
the book crossed (36 episodes, 364 messages), each ended by non-printable executions and
a Cross Trade; in the NYSE hour, 0 of 758. OB aborts a simulation run on a crossed book
under `tradingStatus` 17.
- Options: (a) send 17 at the status message, as NYSE signals it; (b) send 25 Cross while
  the handler's book of the symbol is crossed and 17 once it uncrosses; (c) send 25 until
  the symbol's Cross Trade.
- Recommendation: (b). It follows the book, which is what OB checks; the handler already
  holds every order's side and price, so it needs only a best bid and offer per symbol.
  (a) aborts replays at reopening auctions; (c) is not enough, because in the samples
  Cross Trades are sent before some of the per-order executions.
- Depends on it: whether replays survive halt resumptions and opening auctions; when
  simulated orders can fill after a resume.

**2. Refresh alignment: is the message numbered LastSeqNum part of the refresh?**
CCS p.20 says discard buffered messages "lower than" LastSeqNum, p.21 apply those "higher
than" it, and p.25 calls the refresh the book "as of this sequence number".
- Options: (a) it is in the refresh: per symbol, apply buffered messages with
  SymbolSeqNum > LastSymbolSeqNum; (b) it is not: apply >= LastSymbolSeqNum.
- Recommendation: (a), the reading of "as of" on p.25. Confirm with a replay that
  compares a refresh with the book rebuilt from the live feed.
- Depends on it: every refresh after a gap, Symbol Clear or failover. The wrong choice
  applies one message twice or drops it (a duplicated add, a missed delete or execution).

**3. Queue order within a price level after a refresh.**
The specification does not give the order of Add Order Refresh messages. In the refresh
sample, 4,632 of 5,007 consecutive same-level pairs have a falling OrderID; where
SourceTime differs (876 pairs), SourceTime and OrderID agree in every pair; live OrderIDs
rise with time (595,695 of 595,696).
- Options: (a) queue by SourceTime, then OrderID, ascending; (b) message order;
  (c) SymbolSeqNum order.
- Recommendation: (a). OrderIDs are assigned in time order, and message order mostly
  runs against them, so (b) would put most levels in reverse. Ask NYSE what SourceTime of
  an Add Order Refresh is (entry time or last priority change) to confirm.
- Depends on it: simulated queue position after every refresh.

**4. NYSE Arca overnight session: Security Status X and the trading-day boundary.**
IF p.11 says orders are cancelled without Delete messages at X; IF v2.5h adds an Arca
overnight session from 8:59pm, while the feed starts at 2:03am and Order IDs are valid
"for the current day" (CCS p.11, p.31).
- Options: (a) every X clears the symbol's book; the trading day, Order ID scope and
  books run from one Sequence Number Reset to the next; (b) orders entered for a later
  session survive X, and the handler keeps them.
- Recommendation: (a) for NYSE, American, Texas and National, where no session follows
  X; for Arca, decide after reading an overnight Arca capture, because (a) empties the
  book that the overnight session trades against if those orders survive.
- Depends on it: the Arca book from 8pm to 8:59pm and through the overnight session;
  whether the handler can treat Order IDs as unique per Sequence Number Reset.

**5. Request Server details only a CERT session settles.**
Three points the specification leaves open: whether every TCP message from the Request
Server is in a 16-byte packet (requests and heartbeats are; responses are not said to
be); whether Request Response Status is ASCII '0'-'9' or binary 0-9; whether Message
Unavailable (31) comes on the Retransmission multicast (CCS p.25) or over TCP (IF p.6).
- Options: (a) build to the readings in section 10 (packets, ASCII, multicast) and
  confirm in CERT; (b) build the decoder to accept both forms of each, then remove the
  unused one after CERT.
- Recommendation: (b). Each alternative is cheap to accept (a DeliveryFlag check, a
  digit-or-binary check, listening on both paths), and none can be checked without a
  registered Source ID and a CERT session.
- Depends on it: retransmission and refresh requests, i.e. every gap recovery.

## Settled

- Same-price modify with a larger volume: action 1; known deviation (OB resets priority,
  NYSE keeps it), settled by kaspar_mapping.md; 4 of 24,545 same-price modifies.
- PositionChange: not used; 0x00 in every sample modify, including price changes.
- FirmID padding: spaces, in every sample Add Order.
- LastSeqNum per symbol in an all-symbol refresh: 12 distinct values in 33,935 headers of
  the refresh sample.
- End of a symbol in a single-symbol refresh: CurrentRefreshPkt = TotalRefreshPkts,
  settled by CCS p.27.
- Refresh channel sequence: own sequence per refresh channel, checked for loss only;
  refresh sample.
- Symbol Clear NextSourceSeqNum: the symbol's next SymbolSeqNum, 7,638 of 7,638 in the
  startup sample.
- Heartbeat SeqNum: the next expected sequence number, all heartbeats in two samples.
- Replace Order priority: delete then add with NewOrderID, new priority; settled by IF
  p.14 and kaspar_mapping.md.
- TradeID width: 4 bytes, from every table and the sample decodes.
- Order Execution at a price other than the order's: the print carries the execution
  price, the order-level execution fills at the book price; settled by kaspar_mapping.md.
- Auction prints: one print per trade, from printable 103 and 110 and from Cross Trade;
  settled by IF p.12, p.22 and kaspar_mapping.md, consistent with the samples.
- `endOfEvent`: on the last record of every venue message; settled by kaspar_mapping.md
  (no transaction markers).

## Low impact

- MsgSize of the 8-byte Refresh Header: never observed (every symbol fit one packet); the
  handler reads the 8-byte layout the specification gives.
- Add Order with a live OrderID: sent as delete then add and logged; 0 occurrences in one
  NYSE hour.
- Imbalance fields for NYSE National: the handler sends no record for Imbalance.
- SymbolIndex across markets: keying by (market, SymbolIndex) is correct under both
  readings.
- Source Time Reference join: by the symbol's SystemID is correct whether the ID is per
  partition or per channel; the sample has one partition.
- CCS version 2.4r against the revision row "2.6r": no field depends on it.
- TradeCond1's empty bullet: no record carries trade conditions.
- Types 140 and 220 in the National sample: other feeds in the same file; they decode as
  `unknown` and are skipped.
- Non-printable executions outside an auction: no print, so volume misses them; 1 of
  8,576 executions (100 shares) in the NYSE hour.
- Trade Cancel and Cross Correction: no book effect; only volume keeps the busted or
  corrected size; 10 cancels in the startup sample, 0 in the NYSE hour, no corrections.
- Aggressor side of prints: derived from the resting side; nothing in OB or TachBook reads
  `aggresorSide`.

## Decisions (owner, 2026-11-18)

- 1: status 25 while the symbol's book is crossed, 17 once it uncrosses (b)
- 2: LastSeqNum is in the refresh; apply SymbolSeqNum > LastSymbolSeqNum (a)
- 3: queue by SourceTime, then OrderID, ascending (a); ask NYSE what SourceTime means
- 4: every X clears the symbol's book for NYSE, American, Texas, National; Arca decided after an overnight capture (a)
- 5: accept both forms of each Request Server point until CERT (b)
