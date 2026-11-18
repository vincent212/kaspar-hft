# Nasdaq TotalView-ITCH 5.0 handler: review list

Built from section 10 of `feed.md`. Every section-10 item, including the two known
deviations, appears once below. Answer the items in "Needs your decision" by number.

## Needs your decision

**1. GLIMPSE alignment: where the live stream resumes after a snapshot.**
End of Snapshot `G` gives a sequence number N. GLIMPSE p.10 says to process live
messages "beginning with" N but also calls N the sequence "when the snapshot was taken";
none of Mold, Soup or GLIMPSE says whether N counts in MoldUDP64 or SoupBinTCP numbering,
or whether the two are the same.
- Options: (a) N is not in the snapshot, apply live messages from N; both transports
  share one numbering; (b) N is in the snapshot, apply from N + 1.
- Recommendation: (a), the literal instruction on p.10, with a check in the handler: a
  Delete or Execute at N for an order not in the spin, or an Add at N already in it, is
  logged. Confirm with one live GLIMPSE session against MoldUDP64.
- Depends on it: every GLIMPSE recovery; the wrong choice drops one message or applies it
  twice (a lost or duplicated add, an execution counted twice, a missed delete).

**2. Queue order within a price level after a GLIMPSE recovery.**
GLIMPSE says only that it sends "Add Order messages for all the displayable orders"
(p.3), not in what order or with which Timestamp.
- Options: (a) queue in spin order; (b) queue by the spun Add's Timestamp, ties by spin
  order; (c) treat queue position as unknown after a recovery.
- Recommendation: (a) until a live spin is compared with a book rebuilt from the start of
  day; switch to (b) if the spun Timestamps turn out to be the original entry times.
- Depends on it: simulated queue position, and so simulated fills, after every GLIMPSE
  recovery.

**3. Priority after Order Replace (U).**
U gives the order a new reference and makes the original "no longer accessible"
(ITCH pp.15-16); ITCH states no priority rule.
- Options: (a) delete the original, add the new reference at the back of its level;
  (b) for a same-price, smaller-size replace, send a change under the original `orderID`
  and map the new reference to it, so OB keeps the place; other replaces as (a).
- Recommendation: (a). The feed reports a new order, and (a) needs no mapping between
  references. Test it on a Sample day: if same-price, size-down replaced orders execute
  ahead of orders added after the original, switch to (b).
- Depends on it: simulated queue position of every order behind a replaced order.
  51,378 U in the Sample's first 50 MB; the size-down same-price share is not measured.

**4. Cross executions and the Cross Trade print.**
C with Printable `N` is counted in the later bulk print (ITCH p.14); ITCH does not say
that every execution in a cross is reported that way. An execution at the order's
display price could come as E, which is always printed, and be counted again in Q.
- Options: (a) print every E, C-`Y`, P and Q, none for C-`N`; (b) as (a), but suppress E
  prints between the start of a cross and its Q.
- Recommendation: (a), after decoding the Sample through the 09:30 opening cross (beyond
  the first 50 MB) and checking that Q Shares equal the C-`N` shares of that cross; use
  (b) only if E messages are found inside the cross.
- Depends on it: traded volume at the open, close, and IPO and halt crosses.

**5. Aggressor side of E and C prints.**
ITCH reports the resting order only and has no aggressor record.
- Options: (a) `aggresorSide` opposite to the resting order's side (2 sell if the order
  is a buy, 1 buy if a sell); (b) 0 on every print.
- Recommendation: (a). In continuous trading the resting order is the passive side.
  Cross executions are C-`N` and produce no print; P and Q get 0.
- Depends on it: buy and sell volume and any trade signal read from the prints.

**6. Trading status: extended hours.**
OB matches only in status 17. Stock Trading Action `T` means "trading on Nasdaq" with no
session qualifier (ITCH p.8); ITCH does not say in which states the book can be crossed.
- Options: (a) the section-7 table: 21 before System Event S, `T` → 17 from S to E (pre-
  and post-market included), 4 after E; (b) `T` → 17 only from System Event Q to M, 21
  before Q and 26 PostClose after M.
- Recommendation: (a). `T` carries no session limit, and with (b) OB would accept a
  crossed book without error in the pre- and post-market. Mapping `P` to 2 or 21 is not
  part of the decision: OB treats every value other than 17 alike.
- Depends on it: when OB reports a crossed book as an error, and whether simulated
  orders fill between 04:00 and 09:30 and between 16:00 and 20:00 ET.

## Settled

- Priority after Order Cancel (X): change at the same price with a smaller size, OB keeps
  the place; settled by kaspar_mapping.md (the specification is silent, OB's rule is
  assumed).
- Event grouping and equal timestamps: `endOfEvent` on the last record of each ITCH
  message, timestamps as sent; settled by kaspar_mapping.md (no transaction markers) and
  OB checking for a cross only when `transactTime` advances.
- Execution price of C, and the known deviation that simulated orders behind the executed
  order fill at its book price: the print carries the execution price; settled by
  kaspar_mapping.md.
- P and Q prints: `l3_mbp_trd_t`, one per P and per Q with Shares > 0; settled by
  kaspar_mapping.md.
- `pxd`: Price / 10,000, the price in the venue's units, ticks from the instrument
  source; settled by kaspar_mapping.md.
- Timestamp time zone: US Eastern, midnight local; settled by the Sample (Start of
  Messages 03:01:32, End of Messages 20:05:00).
- Market Category `M`: undocumented, seen only on test issue `MTEST`; settled by the
  Sample, treated as data.
- MPID of A orders: `NSDQ`; settled by the specification (ITCH p.13). MPID is not carried
  in the records.
- ITCH over SoupBinTCP framing: payload of a Sequenced Data packet with no ITCH length;
  settled by the specification (Soup p.5).

## Low impact

- G Sequence Number padding: the handler trims spaces on both sides and accepts leading
  zeros, which reads all three layouts.
- Reserve (iceberg) replenishment: the handler maps whatever messages carry it (A, U or
  X); no handler choice depends on the answer.
- MoldUDP64 Session format and padding: the handler only compares the 10 bytes for
  equality.
- MoldUDP64 sequence at a new session: a new session is a channel reset and the handler
  starts from the first packet's Sequence Number, so either numbering gives the same
  book.
- K Stock Locate 0: K produces no record; it is looked up by Stock if used at all.
- IPO Price text or integer (K): K produces no record.
- DLCR Near Execution Time unit: DLCR produces no record.
- RPII still sent: it produces no record and decodes either way.
- Broken Trade (known deviation): the book is unchanged (ITCH p.18); only volume is
  overstated by the broken size. No B in the Sample.
