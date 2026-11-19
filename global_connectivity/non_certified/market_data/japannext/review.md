# Japannext feed: review list

Built from section 10 of `feed.md`. Every section-10 item appears once below.

## Needs your decision

### 1. Order Numbers shared by several groups on one stream

- **Question:** may the handler assume that Order Numbers do not repeat across the order book groups carried on one MoldUDP64 stream?
- **Options:** (a) assume it: one order map per stream keyed by Order Number, and log an add whose number is already live as an error; (b) ask JNX first and hold the handler until it answers; (c) give each group its own stream in configuration and refuse a stream that carries more than one group.
- **Recommendation:** (a), and send the question to JNX. ITCH 4.7 says "unique per day per order book group", but `E`, `D` and `U` carry no group, so a protocol with repeating numbers on one stream could not be decoded at all; the sample (one group, 128,366 adds) has no repeat, and its numbers are a date plus a counter.
- **Depends on it:** how `E`, `D` and `U` find their order, and the `orderID` composition (section 7).

### 2. Queue priority after an Order Replaced (`U`)

- **Question:** does a replace at the same price with a smaller size keep the order's place in the queue?
- **Options:** (a) action 1 and OB's rule: the place is kept (current reading, the rule in `kaspar_mapping.md` when the venue says nothing); (b) every replace loses priority: action 2 then action 0 with the same `orderID`.
- **Recommendation:** (a) until JNX answers, and ask JNX. The new Order Number hints at (b), but the specification does not state it, and `kaspar_mapping.md` says to apply OB's rule when the venue is silent.
- **Depends on it:** simulated fills behind replaced orders. The readings differ for 303 of 9,287 replaces in the sample (3.3%).

### 3. Recovery design before JNX's connectivity documents

- **Question:** build recovery now on the public documents, or wait for JNX's connectivity details? Three open items make up this question: whether A/B multicast feeds exist; which MoldUDP64 session and stream a GLIMPSE End of Snapshot sequence number refers to; whether ITCH over SoupBinTCP can replay a whole day from sequence 1.
- **Options:** (a) build now: single feed with MoldUDP64 re-request, GLIMPSE snapshot applied to the one ITCH stream the GLIMPSE server serves, SoupBinTCP used only to resume; (b) wait for JNX's answers.
- **Recommendation:** (a). The live path does not depend on these answers; if JNX has A/B, the existing arbitration is added, and if GLIMPSE serves several streams, the alignment step takes the stream as a parameter.
- **Depends on it:** gap handling and recovery only.

### 4. Order within a price level in a GLIMPSE snapshot

- **Question:** does GLIMPSE send the orders of each price level in time priority?
- **Options:** (a) apply `A`/`F` in arrival order; (b) sort each level by Order Number.
- **Recommendation:** (a). Order Numbers follow entry time, but under item 2 (a) a replaced order keeps its place with a newer number, so sorting by number would misplace it. Check on a full snapshot capture by comparing it with the book built from the live feed; the sample snapshot is truncated before any resting order.
- **Depends on it:** queue positions, and so simulated fills, after every recovery.

### 5. Trading status after a GLIMPSE recovery

- **Question:** after a snapshot, which status does a book in state `T` get, given that GLIMPSE carries no start or end of market hours (only System Events `O` and `C`)?
- **Options:** (a) derive the market-hours phase from the clock and the Market Events Schedule; (b) treat `T` as 17 immediately; (c) hold the book at 21 until a live `Q`.
- **Recommendation:** (a). (b) lets OB match outside market hours after an evening recovery, and (c) stops matching for the rest of the day, because `Q` is sent once at the start of market hours.
- **Depends on it:** whether OB matches after a recovery (status 17 is the only matching state).

## Settled

- **Executed Quantity per execution, not cumulative:** sample, 100 orders filled exactly by the sum of their partial executions, 0 overfills.
- **A full execution removes the order with no `D`:** sample, 66,858 full executions, none followed by `D`.
- **Side and book of a replacement order:** specification text (ITCH 4.10, "replaced"): taken from the original order.
- **Event boundaries (`endOfEvent`):** `kaspar_mapping.md`, every record when the protocol has no transaction marker; the sample rules out packets (173 of 376 same-timestamp `E` pairs cross a packet boundary).
- **Time zone of timestamps:** sample, JST (27,004 s in a packet captured at 22:30:04 UTC).
- **MoldUDP64 Session padding:** sample, the session is 10 digits (`1697659284`); all 10 bytes are compared.
- **GLIMPSE Requested Sequence Number:** sample, a blank session and `0` were accepted with next sequence 1.
- **Kaspar status codes for `V`, `M` and the spin:** `kaspar_mapping.md`, halted 2, closed 4, pre-open 21, not available 18.
- **MoldUDP64 one-to-many, not point-to-point:** specification text (MOLD 2, 2.6.1).
- **Timestamps in a GLIMPSE snapshot:** sample, every message carries the snapshot time (09:39:32.026810 JST, the capture's start); recovery records are stamped 1 ns earlier so OB keeps live records from that microsecond.
- **Width of Order Numbers:** sample, a date plus a 10-digit counter, at most 58 bits, which leaves 6 bits for the stream index in `orderID`.

## Low impact

- **Night-session timestamps after midnight (count past 86,400 or wrap):** the handler adds 86,400 s when the seconds go backward within a session, so both readings give the same `transactTime`.
- **Tick at a band boundary (3,000.0 in table 37):** boundary prices are multiples of both ticks; only next-tick arithmetic exactly at the boundary depends on it, and the book does not.
- **Expired orders at session end (`D` sent or not):** after `M` the books are in status 4, so OB does not match against leftovers, and End of Session clears every book.

## Decisions (owner, 2026-11-18)

- 1: assume Order Numbers do not repeat on one stream; log a repeat; ask JNX (a)
- 2: OB's rule (action 1) until JNX answers; ask JNX (a)
- 3: build recovery now on the public documents (a)
- 4: apply snapshot orders in arrival order (a)
- 5: derive the market-hours phase from the clock and the Market Events Schedule (a)
