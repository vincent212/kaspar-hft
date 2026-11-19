# Feed description: Nasdaq OUCH 5.0 order entry

Order entry for Nasdaq U.S. equities over OUCH 5.0 and SoupBinTCP 3.0, as
implemented by `OuchOM`. Not certified: nothing here has been run against
Nasdaq.

Every entry cites its source. Abbreviations:

- **OUCH**: Nasdaq OUCH 5.0 Order Entry Specification, "Updated October,
  2025", latest revision 1.05 of 10/7/2025 (Appendix E, p.26), 26 pages. Page
  numbers are PDF pages; they equal the printed footers.
- **Soup**: SoupBinTCP Version 3.00, last revision 02/22/2017 (Soup p.9),
  9 pages.

## 1. Venue and documents

| Document | Version | Date | Applies to |
|---|---|---|---|
| OUCH 5.0 Order Entry Specification (`OUCH5.0.pdf`, https://www.nasdaqtrader.com/content/technicalsupport/specifications/TradingProducts/OUCH5.0.pdf) | 5.0, revision 1.05 | October 2025 (revision history, OUCH p.26) | All OUCH messages, both directions. The specification names BX-only and Nasdaq-only values (CrossType, OUCH p.5; liquidity flags, pp.25-26), so one document covers Nasdaq, BX and PSX. |
| SoupBinTCP (`soupbintcp.pdf`, https://www.nasdaqtrader.com/content/technicalsupport/specifications/dataproducts/soupbintcp.pdf) | 3.00 | 02/22/2017 (Soup p.9) | The session layer that carries OUCH (OUCH 1.1, p.3). The same document as `global_connectivity/non_certified/market_data/nasdaq/` uses. |

Which applies: OUCH 5.0 is the version Nasdaq publishes for its equity
markets; `.../TradingProducts/Ouch5.0.pdf` and `.../OUCH5.0.pdf` return the
same file. Nasdaq also still publishes OUCH 4.2 (`OUCH4.2.pdf`, "Updated
October, 2025"), an earlier protocol version with different layouts; it is
not used here.

Not covered by these documents: host addresses and ports, port rate limits,
cancel-on-disconnect, the account's default firm and capacity, test
symbols, and the certification process. They come from Nasdaq with the port.

## 2. Transport

- **TCP, SoupBinTCP 3.0.** The client opens a socket and sends a Login
  Request first; the server answers Login Accepted and then Sequenced Data
  packets; the connection lasts until the socket breaks (Soup 1.2, p.3). The
  server may drop a client that sends no Login Request within "typically 30
  seconds" (Soup 2.3.1, p.6).
- **Heartbeats.** Each side sends a heartbeat when more than 1 second has
  passed since it last sent anything; the server may close the socket after
  "typically 15 seconds" without hearing from the client (Soup 1.3, p.3).
  SoupSession sends a Client Heartbeat after `heartbeat_ms` (1000) without
  sending, and declares the link down after `receive_timeout_ms` (15000)
  without receiving.
- **Mirrored hosts.** One OUCH account can be bound to several physical OUCH
  hosts that mirror each other; both accept orders and both generate the
  outbound messages (OUCH 1.3, p.4). OuchOM connects to one host; see section
  10.
- **Inbound requests are not guaranteed** and may be resent benignly; Nasdaq
  processes the inbound messages of one port sequentially (OUCH 2, p.4).
- **Logout**: the server closes the connection on a Logout Request (Soup
  2.3.4, p.8). OuchOM sends one on Shutdown.

## 3. Framing

- **SoupBinTCP packet**: a 2-byte big-endian length counting the packet type
  and payload but not itself, a 1-byte packet type, then the payload; no
  maximum payload length is defined (Soup 1.1, p.2). Packets do not map to
  TCP segments (Soup 1.1, p.2). The session layer is the existing table
  `global_connectivity/non_certified/market_data/nasdaq/soupbintcp.tbl`; `@length_prefix 2 exclusive` there.
- **OUCH inside SoupBinTCP**: each Sequenced Data packet (`S`) carries one
  outbound OUCH message (Soup 1.2, p.3; 2.2.3, p.5); each inbound OUCH message
  goes in an Unsequenced Data packet (`U`) (Soup 2.3.2, p.7). An OUCH message
  has no length of its own: the SoupBinTCP length frames it. `ouch50.tbl`
  therefore has no `@length_prefix`. `@framed_decode` does not fit (the
  envelope is a length and a type byte, not a length alone), nor do
  `@include` (OUCH carries no other layer's messages) or `@offset_base`
  (OUCH offsets start at 0, the type byte).
- **Data types** (OUCH 1.2, p.3): numeric fields are unsigned big-endian
  binary of 1, 2, 4 or 8 bytes; Alpha fields are left-justified and padded on
  the right with spaces; prices have 4 implied decimals, maximum $199,999.9900;
  `0x7FFFFFFF` ($214,748.3647) marks a market order for a cross, and $200,000.00
  is also treated as market. Timestamps are nanoseconds since midnight (OUCH
  3.1, p.11).
- **Optional Appendage**: Appendage Length (2 bytes) then TagValue elements,
  each Length (1 byte, counting what follows it), OptionTag (1), OptionValue
  (OUCH 1.2, pp.3-4); options in Appendix A (pp.21-23). Kept as an opaque
  trailing field in the table; `OuchCodec.hpp` builds and reads it.
- **Messages whose appendage may be absent**: on C, D, B, J, P, I, T, M (from
  Nasdaq) and Q (Account Query Response) both Appendage Length and the
  appendage "are both optional and will be absent unless this message results
  from the incoming message specifying non zero userRefIdx" (OUCH 3.4 p.14,
  3.5 p.14, 3.7 p.16, 3.8 p.16, 3.9 p.17, 3.10 p.17, 3.11 p.17, 3.12 p.18,
  3.17 p.20). In the table both are one trailing field, `AppendageWithLength`,
  on every message from Nasdaq, so a report of another type (A, U, E, R, X, G,
  K) that arrives without them decodes too, with an empty tail.
  On X, M and Q to Nasdaq they "are optional" (OUCH 2.3 p.7, 2.4 p.8, 2.8
  p.10); OuchOM always sends Appendage Length 0.
- **Batching**: none at the OUCH layer; several SoupBinTCP packets can arrive in
  one read and one can span reads (tested: `SoupSession.PacketsSplitAcrossReads`).

## 4. Message catalogue

Rendered from `ouch50.tbl` with `fieldgen.py --markdown`; `fieldgen.py
--check` passes (2 sections, 25 messages: 8 to Nasdaq, 17 from Nasdaq).
Offsets are 0-based from the type byte.

### ouch::to_nasdaq

#### EnterOrder (type `O`, 47 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | EnterOrder: new order (OUCH 5.0 2.1, pp.4-5) |
| UserRefNum | 1 | 4 | UserRefNum | Day-unique and strictly increasing per OUCH account (or per UserRefIdx channel) |
| Side | 5 | 1 | Alpha | B buy, S sell, T sell short, E sell short exempt |
| Quantity | 6 | 4 | Integer | Total shares; greater than zero and less than 1,000,000 (spec type "Interger") |
| Symbol | 10 | 8 | Alpha | Stock symbol |
| Price | 18 | 8 | Price | 4 implied decimals; max $199,999.9900; 0x7FFFFFFF = market (for a cross) |
| TimeInForce | 26 | 1 | Alpha | 0 Day, 3 IOC, 5 GTX (extended hours), 6 GTT (needs ExpireTime), E after hours |
| Display | 27 | 1 | Alpha | Y visible, N hidden, A attributable |
| Capacity | 28 | 1 | Alpha | A agency, P principal, R riskless, O other |
| InterMarketSweepEligibility | 29 | 1 | Alpha | Y eligible, N not eligible |
| CrossType | 30 | 1 | Alpha | N continuous, O opening, C closing, H halt/IPO, S supplemental, R retail (BX), E extended life, A after hours close |
| ClOrdID | 31 | 14 | Alpha | Customer order identifier; not checked for day-uniqueness |
| AppendageLength | 45 | 2 | Integer | Length of the Optional Appendage that follows |
| OptionalAppendage | 47 | var | TagValue | TagValue elements (Appendix A) |

#### ReplaceOrderRequest (type `U`, 40 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | ReplaceOrderRequest: cancel and replace in one message (OUCH 5.0 2.2, pp.6-7) |
| OrigUserRefNum | 1 | 4 | UserRefNum | UserRefNum of the Enter Order or of the last Replace Order |
| UserRefNum | 5 | 4 | UserRefNum | New UserRefNum for the replacement; unique and strictly increasing |
| Quantity | 9 | 4 | Integer | Total shares liable for the whole chain, including earlier executions |
| Price | 13 | 8 | Price | Price of the replacement |
| TimeInForce | 21 | 1 | Alpha | 0 Day, 3 IOC, 5 GTX, 6 GTT |
| Display | 22 | 1 | Alpha | Y visible, N hidden, A attributable |
| InterMarketSweepEligibility | 23 | 1 | Alpha | Y eligible, N not eligible |
| ClOrdID | 24 | 14 | Alpha | Customer order identifier for the replacement |
| AppendageLength | 38 | 2 | Integer | Length of the Optional Appendage that follows |
| OptionalAppendage | 40 | var | Options | TagValue elements (Appendix A); Firm and GroupID are inherited |

#### CancelOrderRequest (type `X`, 11 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | CancelOrderRequest: cancel or reduce an order (OUCH 5.0 2.3, pp.7-8) |
| UserRefNum | 1 | 4 | UserRefNum | UserRefNum of the order as entered |
| Quantity | 5 | 4 | Integer | New intended order size; 0 cancels all remaining shares |
| AppendageLength | 9 | 2 | Integer | Optional on this message; sent as 0 here |
| OptionalAppendage | 11 | var | TagValue | UserRefIdx only |

#### ModifyOrderRequest (type `M`, 12 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | ModifyOrderRequest: change side or reduce without losing priority (OUCH 5.0 2.4, p.8) |
| UserRefNum | 1 | 4 | UserRefNum | UserRefNum of the order as entered |
| Side | 5 | 1 | Alpha | New side; only S to E, S to T, E to T allowed |
| Quantity | 6 | 4 | Integer | New intended order size |
| AppendageLength | 10 | 2 | Integer | Optional on this message |
| OptionalAppendage | 12 | var | TagValue | UserRefIdx, SharesLocated, LocateBroker |

#### MassCancelRequest (type `C`, 19 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | MassCancelRequest: cancel booked orders matching the criteria (OUCH 5.0 2.5, p.9) |
| UserRefNum | 1 | 4 | UserRefNum | Day-unique and strictly increasing per order port |
| Firm | 5 | 4 | Alpha | Firm identifier, all caps |
| Symbol | 9 | 8 | Alpha | Optional, spaces if not specified |
| AppendageLength | 17 | 2 | Integer | 0 if no appendage follows |
| OptionalAppendage | 19 | var | TagValue | Side, Group ID, UserRefIdx |

#### DisableOrderEntryRequest (type `D`, 11 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | DisableOrderEntryRequest: block new orders on the account (OUCH 5.0 2.6, pp.9-10) |
| UserRefNum | 1 | 4 | UserRefNum | Day-unique and strictly increasing per order port |
| Firm | 5 | 4 | Alpha | Firm identifier, all caps |
| AppendageLength | 9 | 2 | Integer | Length of the Optional Appendage that follows |
| OptionalAppendage | 11 | var | TagValue | UserRefIdx |

#### EnableOrderEntryRequest (type `E`, 11 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | EnableOrderEntryRequest: unblock new orders on the account (OUCH 5.0 2.7, p.10) |
| UserRefNum | 1 | 4 | UserRefNum | Day-unique and strictly increasing per order port |
| Firm | 5 | 4 | Alpha | Firm identifier, all caps |
| AppendageLength | 9 | 2 | Integer | Length of the Optional Appendage that follows |
| OptionalAppendage | 11 | var | TagValue | UserRefIdx |

#### AccountQueryRequest (type `Q`, 3 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | AccountQueryRequest: ask for the next available UserRefNum (OUCH 5.0 2.8, p.10) |
| AppendageLength | 1 | 2 | Integer | Optional on this message; sent as 0 here |
| OptionalAppendage | 3 | var | TagValue | UserRefIdx |

### ouch::from_nasdaq

#### SystemEvent (type `S`, 10 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | SystemEvent: start or end of day (OUCH 5.0 3.1, p.11) |
| Timestamp | 1 | 8 | Timestamp | Nanoseconds since midnight |
| EventCode | 9 | 1 | Alpha | S start of day, E end of day |

#### OrderAccepted (type `A`, 64 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | OrderAccepted: the Enter Order was accepted (OUCH 5.0 3.2, pp.11-12) |
| Timestamp | 1 | 8 | Timestamp | Nanoseconds since midnight |
| UserRefNum | 9 | 4 | UserRefNum | As entered (spec spells it "UesrRefNum") |
| Side | 13 | 1 | Alpha | As entered |
| Quantity | 14 | 4 | Integer | Total shares accepted |
| Symbol | 18 | 8 | Alpha | As entered |
| Price | 26 | 8 | Price | Accepted price; better than or equal to the entered price |
| TimeInForce | 34 | 1 | Alpha | Accepted; equal to or shorter in scope than entered |
| Display | 35 | 1 | Alpha | Y visible, N hidden, A attributable, Z conformant |
| OrderReferenceNumber | 36 | 8 | Numeric | Day-unique order reference number assigned by Nasdaq |
| Capacity | 44 | 1 | Alpha | As specified |
| InterMarketSweepEligibility | 45 | 1 | Alpha | Y eligible, N not eligible |
| CrossType | 46 | 1 | Alpha | As entered |
| OrderState | 47 | 1 | Alpha | L live, D dead (accepted and automatically canceled; nothing further follows) |
| ClOrdID | 48 | 14 | Alpha | Customer order identifier |
| AppendageWithLength | 62 | var | TagValue | Appendage Length (62, 2) and Optional Appendage (64); TagValue elements (Appendix A) |

#### OrderReplaced (type `U`, 68 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | OrderReplaced: the Replace Order was accepted (OUCH 5.0 3.3, pp.12-14) |
| Timestamp | 1 | 8 | Timestamp | Nanoseconds since midnight |
| OrigUserRefNum | 9 | 4 | UserRefNum | UserRefNum of the order replaced |
| UserRefNum | 13 | 4 | UserRefNum | UserRefNum of the replacement, as entered |
| Side | 17 | 1 | Alpha | As entered |
| Quantity | 18 | 4 | Integer | Total shares outstanding (on the book) after the replace |
| Symbol | 22 | 8 | Alpha | As entered |
| Price | 30 | 8 | Price | Accepted price of the replacement |
| TimeInForce | 38 | 1 | Alpha | Accepted time in force |
| Display | 39 | 1 | Alpha | Y visible, N hidden, A attributable, Z conformant |
| OrderReferenceNumber | 40 | 8 | Numeric | Day-unique order reference number assigned by Nasdaq |
| Capacity | 48 | 1 | Alpha | As specified |
| InterMarketSweepEligibility | 49 | 1 | Alpha | Y eligible, N not eligible |
| CrossType | 50 | 1 | Alpha | As entered |
| OrderState | 51 | 1 | Alpha | L live, D dead |
| ClOrdID | 52 | 14 | Alpha | Customer order identifier for the replacement |
| AppendageWithLength | 66 | var | TagValue | Appendage Length (66, 2) and Optional Appendage (68); TagValue elements (Appendix A) |

#### OrderCanceled (type `C`, 18 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | OrderCanceled: an order was reduced or canceled (OUCH 5.0 3.4, p.14) |
| Timestamp | 1 | 8 | Timestamp | Nanoseconds since midnight |
| UserRefNum | 9 | 4 | UserRefNum | UserRefNum of the order (partially) canceled |
| Quantity | 13 | 4 | Numeric | Shares decremented by this message; incremental, not cumulative |
| Reason | 17 | 1 | Alpha | Appendix B (the text says Appendix C) |
| AppendageWithLength | 18 | var | TagValue | Appendage Length (18, 2) and Optional Appendage (20); absent unless UserRefIdx was non-zero |

#### AIQCanceled (type `D`, 32 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | AIQCanceled: reduced by self-match prevention (OUCH 5.0 3.5, pp.14-15) |
| Timestamp | 1 | 8 | Timestamp | Nanoseconds since midnight |
| UserRefNum | 9 | 4 | UserRefNum | UserRefNum of the order reduced |
| DecrementShares | 13 | 4 | Numeric | Shares decremented by this message; incremental |
| Reason | 17 | 1 | Alpha | Always Q |
| QuantityPreventedFromTrading | 18 | 4 | Integer | Shares that would have executed |
| ExecutionPrice | 22 | 8 | Price | Price the trade would have had |
| LiquidityFlag | 30 | 1 | Alpha | Appendix D (the text says Appendix E) |
| AIQStrategy | 31 | 1 | Alpha | AIQ strategy used on the canceled order |
| AppendageWithLength | 32 | var | TagValue | Appendage Length (32, 2) and Optional Appendage (34); absent unless UserRefIdx was non-zero |

#### OrderExecuted (type `E`, 36 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | OrderExecuted: all or part of an order executed (OUCH 5.0 3.6, p.15) |
| Timestamp | 1 | 8 | Timestamp | Nanoseconds since midnight |
| UserRefNum | 9 | 4 | UserRefNum | UserRefNum of the order executed |
| Quantity | 13 | 4 | Numeric | Shares executed by this message; incremental |
| Price | 17 | 8 | Price | Execution price |
| LiquidityFlag | 25 | 1 | Alpha | Appendix D (the text says Appendix E) |
| MatchNumber | 26 | 8 | Numeric | Trade id; shared by the buy and the sell execution |
| AppendageWithLength | 34 | var | TagValue | Appendage Length (34, 2) and Optional Appendage (36); UserRefIdx |

#### BrokenTrade (type `B`, 36 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | BrokenTrade: an execution was broken (OUCH 5.0 3.7, p.16) |
| Timestamp | 1 | 8 | Timestamp | Nanoseconds since midnight |
| UserRefNum | 9 | 4 | UserRefNum | UserRefNum of the order whose execution is broken |
| MatchNumber | 13 | 8 | Numeric | Match Number of the Executed message broken |
| Reason | 21 | 1 | Alpha | E erroneous, C consent, S supervisory, X external |
| ClOrdID | 22 | 14 | Alpha | Customer order identifier |
| AppendageWithLength | 36 | var | TagValue | Appendage Length (36, 2) and Optional Appendage (38); absent unless UserRefIdx was non-zero |

#### Rejected (type `J`, 29 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | Rejected: an Enter or Replace Order was rejected (OUCH 5.0 3.8, p.16) |
| Timestamp | 1 | 8 | Timestamp | Nanoseconds since midnight |
| UserRefNum | 9 | 4 | UserRefNum | UserRefNum of the order rejected; it cannot be reused |
| Reason | 13 | 2 | Numeric | Appendix C (the text says Appendix D) |
| ClOrdID | 15 | 14 | Alpha | Customer order identifier |
| AppendageWithLength | 29 | var | TagValue | Appendage Length (29, 2) and Optional Appendage (31); absent unless UserRefIdx was non-zero |

#### CancelPending (type `P`, 13 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | CancelPending: a cross order cannot be canceled until the cross completes (OUCH 5.0 3.9, p.17) |
| Timestamp | 1 | 8 | Timestamp | Nanoseconds since midnight |
| UserRefNum | 9 | 4 | UserRefNum | UserRefNum of the order |
| AppendageWithLength | 13 | var | TagValue | Appendage Length (13, 2) and Optional Appendage (15); absent unless UserRefIdx was non-zero |

#### CancelReject (type `I`, 13 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | CancelReject: a partial cancel of a cross order was refused (OUCH 5.0 3.10, p.17) |
| Timestamp | 1 | 8 | Timestamp | Nanoseconds since midnight |
| UserRefNum | 9 | 4 | UserRefNum | UserRefNum of the order |
| AppendageWithLength | 13 | var | TagValue | Appendage Length (13, 2) and Optional Appendage (15); absent unless UserRefIdx was non-zero |

#### OrderPriorityUpdate (type `T`, 30 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | OrderPriorityUpdate: the system changed the order's priority (OUCH 5.0 3.11, pp.17-18) |
| Timestamp | 1 | 8 | Timestamp | Nanoseconds since midnight |
| UserRefNum | 9 | 4 | UserRefNum | UserRefNum of the order |
| Price | 13 | 8 | Price | Limit price of the order |
| Display | 21 | 1 | Alpha | New display; Y anonymous, price to comply |
| OrderReferenceNumber | 22 | 8 | Numeric | New order reference number |
| AppendageWithLength | 30 | var | TagValue | Appendage Length (30, 2) and Optional Appendage (32); absent unless UserRefIdx was non-zero |

#### OrderModified (type `M`, 18 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | OrderModified: response to a Modify Order (OUCH 5.0 3.12, p.18) |
| Timestamp | 1 | 8 | Timestamp | Nanoseconds since midnight |
| UserRefNum | 9 | 4 | UserRefNum | UserRefNum of the order |
| Side | 13 | 1 | Alpha | Side as entered in the Modify Order |
| Quantity | 14 | 4 | Numeric | Total shares outstanding |
| AppendageWithLength | 18 | var | TagValue | Appendage Length (18, 2) and Optional Appendage (20); absent unless UserRefIdx was non-zero |

#### OrderRestated (type `R`, 16 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | OrderRestated: the system modified the order (OUCH 5.0 3.13, pp.18-19) |
| Timestamp | 1 | 8 | Timestamp | Nanoseconds since midnight |
| UserRefNum | 9 | 4 | UserRefNum | UserRefNum of the order |
| Reason | 13 | 1 | Alpha | R refresh of display (reserves), P update of displayed price |
| AppendageWithLength | 14 | var | TagValue | Appendage Length (14, 2) and Optional Appendage (16); Display Quantity, Display Price, SecondaryOrdRefNum, UserRefIdx |

#### MassCancelResponse (type `X`, 27 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | MassCancelResponse: a Mass Cancel Request was received (OUCH 5.0 3.14, p.19) |
| Timestamp | 1 | 8 | Timestamp | Nanoseconds since midnight |
| UserRefNum | 9 | 4 | UserRefNum | UserRefNum of the request |
| Firm | 13 | 4 | Alpha | Firm identifier |
| Symbol | 17 | 8 | Alpha | Spaces if not specified |
| AppendageWithLength | 25 | var | TagValue | Appendage Length (25, 2) and Optional Appendage (27); Side, Group ID, UserRefIdx |

#### DisableOrderEntryResponse (type `G`, 19 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | DisableOrderEntryResponse: a Disable Order Entry Request was received (OUCH 5.0 3.15, pp.19-20) |
| Timestamp | 1 | 8 | Timestamp | Nanoseconds since midnight |
| UserRefNum | 9 | 4 | UserRefNum | UserRefNum of the request |
| Firm | 13 | 4 | Alpha | Firm identifier |
| AppendageWithLength | 17 | var | TagValue | Appendage Length (17, 2) and Optional Appendage (19); UserRefIdx |

#### EnableOrderEntryResponse (type `K`, 19 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | EnableOrderEntryResponse: an Enable Order Entry Request was received (OUCH 5.0 3.16, p.20) |
| Timestamp | 1 | 8 | Timestamp | Nanoseconds since midnight |
| UserRefNum | 9 | 4 | UserRefNum | UserRefNum of the request |
| Firm | 13 | 4 | Alpha | Firm identifier |
| AppendageWithLength | 17 | var | TagValue | Appendage Length (17, 2) and Optional Appendage (19); UserRefIdx |

#### AccountQueryResponse (type `Q`, 13 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | AccountQueryResponse: the next available UserRefNum (OUCH 5.0 3.17, p.20) |
| Timestamp | 1 | 8 | Timestamp | Nanoseconds since midnight |
| NextUserRefNum | 9 | 4 | UserRefNum | Next available UserRefNum |
| AppendageWithLength | 13 | var | TagValue | Appendage Length (13, 2) and Optional Appendage (15); absent unless UserRefIdx was non-zero |

## 5. Logon and session

- **Credentials**: username (6 characters) and password (10), right-padded
  with spaces, case-insensitive, in the Login Request (Soup 2.3.1, pp.6-7).
  OuchOM reads them from a credentials file outside the repository
  (`config/ouch_example.ini`, key `credentials`).
- **Account**: each physical OUCH port is bound to one Nasdaq-assigned logical
  OUCH account (OUCH 1.1, p.3). There is no account field in the messages. The
  Firm option (tag 2) selects the firm when one account enters for several; if
  blank, the account's default firm is used (Appendix A, p.21). OuchOM sends
  Firm when `firm` is configured.
- **Sequence numbers**: the first Sequenced Data packet of each session is 1;
  sequence numbers are implied, counted from the number in Login Accepted
  (Soup 1.2, p.3; 2.2.3, p.5). A login names the session (blank for the
  current one) and the next sequence number wanted, 0 meaning "the most
  recently generated message" (Soup 2.3.1, p.7). Neither document says how
  long a session lasts; it is taken as one trading day, since UserRefNums are
  day-unique (OUCH 1.1, p.3) and a new session is entered only after End of
  Session (Soup 1.4, p.3).
- **First login** of a process: blank session, sequence `first_sequence`
  (default 0: no replay). **Later logins** in the process (reconnects): the
  session from Login Accepted and the sequence after the last report applied.
- **Ready for orders**: only after Login Accepted *and* the Account Query
  Response (section 6). Orders before that get `Reject(NOTLOGGEDIN)`.
- **Trading-session status**: System Event `S` (start of day: "ready to accept
  orders") and `E` (end of day: no new orders; breaks and cancels may still
  come) (OUCH 3.1, p.11). OuchOM logs them and does not gate orders on them;
  an order sent before start of day is Nasdaq's to reject.
- **End of Session** (`Z`): no more messages in the session; the connection
  closes (Soup 2.2.5, p.6). OuchOM stops and does not reconnect.
- **Login Rejected**: `A` not authorized, `S` session not available; the
  server closes the socket (Soup 2.2.2, p.5). OuchOM stays down until
  restarted (retrying cannot help with either).
- **Cancel-on-disconnect**: not in OUCH 5.0 or SoupBinTCP 3.0. Section 10.

## 6. Order identifiers

- **UserRefNum** (4-byte unsigned) identifies an order on the account for the
  day; it "must be both unique and strictly increasing throughout the trading
  day", starts at 1, and Nasdaq "ignores new order requests identified with
  UserRefNums lower than the last one processed, assuming they are
  retransmissions" (OUCH 1.2, p.3). With the optional UserRefIdx (tag 28) the
  rule holds per order-flow channel instead of per port (OUCH 1.2, p.3);
  OuchOM does not use UserRefIdx.
- **Which messages take a new UserRefNum**: Enter Order, Replace Order (the
  replacement's), Mass Cancel, Disable and Enable Order Entry (OUCH 2.1-2.7,
  pp.4-10). **Cancel Order and Modify Order name the order's own UserRefNum**
  and take none (OUCH 2.3, 2.4, pp.7-8). A Rejected UserRefNum cannot be reused
  (OUCH 3.8, p.16).
- **Restart**: the Account Query Request returns "the next available
  UserRefNum", intended "when recovering state" (OUCH 2.8, p.10; 3.17, p.20).
  OuchOM sends it after every login and takes the larger of its own next value
  and Nasdaq's. A restarted process therefore continues after the highest
  UserRefNum used that day (test `IdsAreUniqueAndIncreasingAcrossRestart`).
- **Mapping to kaspar ids**: `BrokerOrderBook` assigns 63-bit client ids;
  UserRefNum is 32-bit. OuchOM keeps the map client id ↔ UserRefNum; every
  report's UserRefNum resolves through it to the book.
- **ClOrdID** (14 Alpha) is free text, "not checked for day-uniqueness" (OUCH
  2.1, p.5). OuchOM puts the SOM order id in it, for people reading logs.
- **Order Reference Number** (8 bytes, day-unique, assigned by Nasdaq) comes
  in Order Accepted (OUCH 3.2, p.11); it is the `xordid` of the `Ack`. Order
  Priority Update assigns a new one (OUCH 3.11, p.18); OuchOM logs it.
- **Reports carry** the order's UserRefNum (A, C, D, E, B, J, P, I, T, M, R);
  Order Replaced carries both the original and the new (OUCH 3.3, p.13).

## 7. New order: fields and supported combinations

Enter Order (OUCH 2.1, pp.4-5) as OuchOM fills it from `frame::som::msg::Order`:

| OUCH field | From | Values sent |
|---|---|---|
| UserRefNum | next UserRefNum | section 6 |
| Side | `side`, `short_sale` | `B` buy; `S` sell; `T` sell short (`short_sale` SHORT); `E` sell short exempt (SHORT_EXEMPT). `short_sale` is ignored on a buy. |
| Quantity | `sz` | whole shares, 1 to 999,999 ("greater than zero and less than 1,000,000", OUCH p.4) |
| Symbol | the asset's name | at most 8 characters, space padded |
| Price | `px` (ticks) × tick size × 10,000 | LIMIT: 1 to 1,999,999,900. MARKET: `0x7FFFFFFF` (OUCH 1.2, p.3) |
| Time In Force | `tif` | DAY `0`; IOC `3`; EXTENDED_DAY `5` (GTX, extended hours); AT_OPEN and AT_CLOSE `0` with a cross type |
| Display | config `display` | `Y` (default), `N`, `A` |
| Capacity | config `capacity` | `A` (default), `P`, `R`, `O` |
| InterMarket Sweep Eligibility | config `intermarket_sweep` | `N` (default) |
| CrossType | `tif` | `N` continuous; AT_OPEN `O` opening cross; AT_CLOSE `C` closing cross (Nasdaq only, OUCH p.5) |
| ClOrdID | SOM `oid` | decimal |
| Optional Appendage | config `firm`; `locate_id` | Firm (tag 2) when configured; on a short sale with `locate_id`: SharesLocated (25) `Y` and LocateBroker (26) = `locate_id` |

Refused locally with `Reject` (nothing is sent):

| Request | Reason |
|---|---|
| STOP, STOP_LIMIT | OUCH 5.0 has no stop orders (no field for a trigger) |
| GTC, FOK | no GTC in OUCH's Time In Force list (p.5); no FOK (section 10) |
| `strategy` set | OUCH has no broker algorithms |
| `route` other than empty or `NASDAQ` | Enter Order has no route option; Route (tag 14) appears only on Order Accepted (OUCH p.12) |
| `oid_to_cancel` set (cancel/replace) | Replace Order is not implemented; the SOM contract has no replace |
| MARKET with EXTENDED_DAY | market orders outside regular hours: not offered (section 10) |
| quantity 0, ≥ 1,000,000, or fractional | OUCH 2.1, p.4 |
| price 0 or above $199,999.99 | OUCH 1.2, p.3 |
| symbol over 8 characters; `locate_id` over 4 | field widths (OUCH p.4; Appendix A, p.22) |
| over `max_order_qty`, `max_order_notional`, `max_orders_per_sec` | local limits (section 8.4) |

Not offered at all: GTT (`6`, needs ExpireTime), After hours (`E`), the other
cross types (H, S, R, E, A), pegs, discretion, reserve (MaxFloor,
RandomReserves), MinQty, PostOnly, TradeNow, HandleInst, CustomerType, AIQ
(self-match prevention) options, Group ID, UserRefIdx (Appendix A,
pp.21-23); Replace, Modify, Mass Cancel, Disable/Enable Order Entry (OUCH
2.2, 2.4-2.7). The SOM's `Order` has no fields for them.

## 8. Reports, throttles, reconnect and risk checks

### 8.1 Reports and what they become

| Nasdaq report | BrokerOrderBook call | To the SOM |
|---|---|---|
| Order Accepted `A`, Order State `L` (OUCH 3.2) | `on_accepted(cid, Order Reference Number)` | `Ack(oid, ref)`, once per order |
| Order Accepted `A`, Order State `D` ("accepted and automatically canceled", no further messages, OUCH p.11) | `on_accepted`, then `on_canceled` | `Ack`, then `CancAck` |
| Order Executed `E` (OUCH 3.6) | `on_fill(cid, Quantity)`, unless the order's Match Number was already applied | `Fill(oid, NASDAQ, sym, Price/10,000 dollars, side, qty, leaves, owner, time)` |
| Order Canceled `C` (OUCH 3.4), AIQ Canceled `D` (OUCH 3.5) | shares decremented are counted; when nothing is left open at Nasdaq, `on_canceled` | `CancAck` when the order ends; nothing for a partial reduction |
| Rejected `J` (OUCH 3.8) | `on_rejected` | `Reject(oid, reason)`: reasons 0x28-0x2A (rate restrictions) THROTTLE; 0x06, 0x1E, 0x1F, 0x20-0x33 (fat finger, thresholds, risk) EXCHLIMIT; others UNKNOWN; the text from Appendix C is logged |
| Cancel Reject `I` (OUCH 3.10) | `on_cancel_rejected` | `CancReject(FROMEXCHANGE)`; the order stays working |
| Cancel Pending `P` (OUCH 3.9) | none | none: logged; the cancel completes after the cross with `C` |
| Broken Trade `B` (OUCH 3.7) | none | none: logged as an error to check positions. Not a fill. |
| Order Replaced `U`, Order Modified `M` | none | none: OuchOM never requests them; logged as an error (an order changed outside the OM) |
| Order Priority Update `T`, Order Restated `R` | none | none: logged |
| System Event `S` | none | none: logged |
| Account Query Response `Q` | none | none: next UserRefNum; ready for orders |
| Mass Cancel Response `X`, Disable/Enable Order Entry Response `G`/`K` | none | none: never requested; logged |
| a type not in OUCH 5.0, or too short to decode | none | none: logged; its sequence number is still counted |

How the hard cases resolve (each is a test in `tests/test_ouch_om.cpp`):

- **Partial fills**: each `E` is incremental (OUCH p.15); `leaves` comes from
  the book.
- **Fill before ack**: "Accepted Messages normally come before any Executed"
  (OUCH 3.2, p.11), so not guaranteed. The fill is applied (the book marks the
  order accepted), and the later Accepted still gives the one `Ack`.
- **Fill racing a cancel**: OUCH has no "too late to cancel"; a cancel for an
  order no longer live is silently ignored (OUCH 2.3, p.8). A full fill ends the
  order with its last `Fill`; a partial fill is followed by `C` for the rest and
  `CancAck`.
- **Fill after the cancel ack**: counted (`Fill` after `CancAck`), as
  `BrokerOrderBook` does.
- **Unsolicited cancels**: `C` with any reason (supervisory, timeout, IOC,
  closed, self-match prevention; Appendix B, pp.23-24) reaches the SOM as
  `CancAck` once nothing is left open.
- **Partial reductions** by Nasdaq (a `C` or `D` that leaves shares open;
  "does not necessarily mean the entire order is dead", OUCH 3.4, p.14): the
  SOM has no partial cancel, so it is told nothing until the order ends; if
  the rest then fills, `Fill` is followed by `CancAck` for the shares Nasdaq
  took off. Logged as a warning when it happens.
- **Busts**: Broken Trade follows the Executed it breaks (OUCH 3.7, p.16); it
  is logged, never turned into a negative fill. There is no trade correction
  message (removed in revision 1.02, OUCH p.26).
- **Fill time**: the report's Timestamp (ns since midnight) plus the epoch
  time of the latest US Eastern midnight (section 10).

### 8.2 Throttles and busy states

- OUCH 5.0 states no message-rate limit. Nasdaq enforces symbol, port and
  duplicate message rate restrictions as Reject reasons 0x28, 0x29, 0x2A
  (Appendix C, p.25); OuchOM maps them to `Reject(THROTTLE)`.
- **Local rate limit**: `max_orders_per_sec`, a token bucket on new orders;
  over it, `Reject(THROTTLE)`. Cancels are never throttled locally.
- **Busy**: SoupSession queues requests for the socket; above
  `max_queued_bytes` (1 MB) a request returns Busy. OuchOM then refuses new
  orders with `Reject(THROTTLE)` and cancels with `CancReject(THROTTLE)` until
  the session reports the queue drained to half. A refused request is never
  retried automatically.

### 8.3 Reconnect and replay

- **What is resent**: every Sequenced Data message from the sequence number
  the login asks for (Soup 1.2, p.3). Inbound requests are not resent by
  SoupBinTCP (Soup 2.3.2, p.7); OUCH allows the client to resend them
  benignly (OUCH 2, p.4), but OuchOM does not (order_entry.md: never retry a
  new order automatically); the SOM decides.
- **Resume**: after a lost connection OuchOM waits `reconnect_ms` and logs in
  to the same session at the sequence after the last report applied. Reports
  sent while it was down arrive once (test
  `ReconnectResumesAndDeliversMissedFillOnce`).
- **No double application**: a report at a sequence number already applied is
  skipped (a server that replays from earlier than asked, test
  `ReplayedReportsAreNotAppliedTwice`); an execution whose Match Number was
  already applied to the order is skipped (test `SameMatchNumberIsAppliedOnce`;
  this covers a fail-over between mirrored hosts, section 10). A second
  Accepted for an order does not give a second `Ack`.
- **Login Accepted at a later sequence than asked** (reports in between lost):
  logged as an error; nothing else can be done.
- **While disconnected**: new orders get `Reject(DISCONNECTED)`, cancels
  `CancReject(DISCONNECTED)`; during the new login, `Reject(NOTLOGGEDIN)`.

### 8.4 Risk checks

- **Nasdaq**: fat-finger, notional, ADV, aggregate exposure, restricted
  stock, short-sale restrictions, locate requirement, market-order and
  pre/post-market permissions, max quantity (Reject reasons 0x06, 0x1E-0x33,
  Appendix C, pp.24-25). Which are enabled is port configuration.
- **OuchOM, before sending**: quantity 1 to 999,999 and whole; price within
  OUCH's range; symbol and locate widths; `max_order_qty`; `max_order_notional`
  (limit price × quantity; a market order is refused when a notional limit is
  set, since its price is unknown); `max_orders_per_sec`.
- **The SOM**: position and exposure limits across venues.

## 9. Pitfalls

From `pitfalls.md`, item by item.

**1. Reading the specification**

- Several documents: one OUCH 5.0 (rev. 1.05), one SoupBinTCP (3.00); OUCH 4.2,
  also published, is an earlier protocol and not used (section 1).
- Errata and change log: revision history (OUCH p.26) read; 1.02 removed the
  Trade Correction message; 1.04 clarified which appendages are optional
  (section 3); nothing contradicts the body.
- Tables across page breaks: Enter Order (pp.4-5), Replace Order (pp.6-7),
  Order Accepted (pp.11-12), Order Replaced (pp.13-14), AIQ Canceled (pp.14-15),
  Order Restated (pp.18-19), Disable Order Entry Request and Response (pp.9-10,
  pp.19-20); each checked against `pdftotext -layout`; offsets are contiguous
  (`fieldgen --check`).
- Offsets: 0-based from the type byte in OUCH; SoupBinTCP's tables count the
  length field, handled by `@length_prefix` in its table.
- Reserved fields: none in OUCH 5.0.
- Enumerations elsewhere: options Appendix A, cancel reasons Appendix B, reject
  reasons Appendix C, liquidity flags Appendix D. The message tables point to
  the wrong letters (section 10).
- Names differing: "UesrRefNum" (Order Accepted, p.11); Quantity typed
  "Interger" (p.4); Order Restated's table titled "Best Bid OR Ask Update
  Message" (pp.18-19); Disable/Enable responses described as echoing the
  "Enter Order Message" (p.19). Table rows and type codes taken as
  authoritative.
- Examples against tables: the specification has no byte examples; the
  Replace quantity examples (pp.6, 12-13) agree with the field notes.
- Two layers: SoupBinTCP and OUCH are separate tables. They share type codes
  (`A`, `S`, `U`, `O`, `J`...), and OUCH's two directions share `U`, `X`, `M`,
  `C`, `D`, `E`, `Q`; each direction is its own section.
- Repeated names: none; outbound `D` is named AIQCanceled, outbound `X`
  MassCancelResponse, as the spec titles them.
- Variable-length trailing field: every OUCH message ends in one (the
  appendage), generated as a tail, not a member.
- Odd-width integers: none (1, 2, 4, 8 bytes only).

**2. Framing and encoding**

- Endianness: big-endian throughout (OUCH 1.2; Soup 1.5).
- Signed: all unsigned except PegOffset and DiscretionPegOffset ("Signed
  Price", Appendix A), which OuchOM does not use.
- Prices: fixed 4 decimals; the SOM's ticks are converted with the asset's
  tick size.
- Sentinels: price `0x7FFFFFFF` (market for a cross); Symbol all spaces in Mass
  Cancel (no symbol); option defaults (Appendix A, column "Default").
- Strings: Alpha left-justified, space padded (OUCH 1.2); SoupBinTCP session
  and sequence fields left padded (Soup 2.2.1).
- Packets: one OUCH message per SoupBinTCP packet; packets split and joined by
  TCP (handled and tested).
- Timestamps: nanoseconds since midnight; the time zone is not stated
  (section 10).

**3. Sequencing and recovery**

- Scope: one sequence per SoupBinTCP session (a day) per port, over all
  outbound messages.
- A/B arbitration: not applicable (TCP). Mirrored hosts: section 10.
- Gap rule: SoupBinTCP over TCP cannot lose messages within a connection; a
  number below the next expected is a replay (skipped), above it a loss
  (logged).
- Recovery: TCP replay from a requested sequence number (section 8.3).
- Snapshot alignment, old snapshots: not applicable.
- State to clear on a session change: a new SoupBinTCP session id restarts
  sequence numbering; OuchOM resets its expected sequence and logs an error.
  UserRefNums come from Account Query, not from the session.
- Recovery paths exercised: reconnect with missed fill, over-replay, duplicate
  match number, a dropped connection with orders working (tests).
- Session behaviour: login, heartbeats, end of session (section 5);
  throttles (8.2); cancel-on-disconnect not documented (section 10).

**4. Book semantics**: not applicable (order entry).

**5. Prices, quantities and instruments**

- Price only (no yield or spread). Quantity in shares, 1 to 999,999 per order
  and per replace chain (OUCH p.6). Round lots: MinQty "must be a round lot"
  (Appendix A), not used.
- Tick sizes: from the SOM asset (`get_units()`); Nasdaq's sub-dollar ticks
  are the asset's concern.
- Instruments: the Kaspar asset name is sent as the OUCH symbol (section 10).

**6. Behaviour the specification omits**: port rate limits,
cancel-on-disconnect, the behaviour of sequence 0 at login, and whether
mirrored hosts number messages identically. Not probed: no venue access
(section 10).

**7. Post-trade**: not applicable.

**8. Order entry**

- Client ids: UserRefNum unique and strictly increasing per day per account
  (OUCH 1.2); restart safety from Account Query (section 6).
- Cancel and replace: cancels use the order's own UserRefNum (OUCH 2.3);
  replace not implemented. Every id an order used maps back to it.
- Fill before ack, fill racing a cancel, fill after the cancel ack: each
  counted once (section 8.1, tests).
- Unsolicited cancels: reach the SOM (section 8.1).
- Busts: logged, not fills; no corrections in OUCH 5.0.
- Busy or throttled: section 8.2.
- Reconnect: section 8.3.
- Orders placed outside the OM: reports for unknown UserRefNums, Order
  Replaced and Order Modified are logged as errors and not forwarded (test
  `UnknownReportsAreLoggedAndIgnored`). Another session on the same account
  shares the UserRefNum space; Account Query at login covers what it used
  before, not what it uses afterwards (section 10).
- Short sales: `T` sell short, `E` exempt; SharesLocated and LocateBroker
  (Appendix A, p.22) set from `locate_id`; Nasdaq may reject with 0x27 "Risk:
  Locate Required".

## 10. Ambiguities

| Question | Reading taken | Alternatives | Resolved by |
|---|---|---|---|
| Is Account Query's NextUserRefNum above every UserRefNum used today on the account, including rejected ones and those of other sessions? | Yes: "the next available UserRefNum that can be used for identifying new transactions" (OUCH 2.8, p.10). OuchOM refuses orders until it has the answer. | Persist the last UserRefNum locally as well. | Owner; Nasdaq test environment |
| Time zone of "nanoseconds since midnight" (OUCH 3.1, p.11) | US Eastern (Nasdaq's), with the US daylight-saving rule; used only for `Fill::tim`. | UTC midnight. | Nasdaq; a test-session capture |
| Price is 8 bytes, but its limits are given as 32-bit values (`7735939C`, `7FFFFFFF`, OUCH 1.2, p.3) | An unsigned 64-bit field holding those values (market = 2,147,483,647). | The upper four bytes have a meaning. | Nasdaq; a test-session capture |
| Market orders outside crosses | Sent as price `0x7FFFFFFF` with any time in force except extended hours: "Orders entered with a price of $200,000.00 or the max integer value will also be treated as market orders" (OUCH 1.2, p.3). | Accept MARKET only for AT_OPEN and AT_CLOSE, the case the sentence before names ("for a cross"). | Owner |
| Time in force for opening and closing cross orders | `0` (Day) with CrossType `O` or `C`. The spec does not say which TIF a cross order takes. | IOC (`3`). | Nasdaq rules of engagement; test environment |
| EXTENDED_DAY | `5` GTX ("Extended Hours"). | `E` "After hours" (Enter Order only; not in Replace's list, OUCH p.7). | Owner |
| FOK | Refused. | IOC with MinQty equal to the quantity (not the same: MinQty must be a round lot, Appendix A). | Owner |
| Locate on a short sale | `locate_id` is taken as the 4-character locate broker code; SharesLocated `Y` when it is set. | A configured locate broker; SharesLocated from a separate flag. | Owner |
| First login's requested sequence | 0 (no replay; reports of an earlier process today are not seen). Soup 2.3.1 does not say whether "the most recently generated message" is itself re-sent; OuchOM counts from Login Accepted either way. | 1: replay the day; earlier processes' reports are logged as unknown. Configurable (`first_sequence`). | Owner |
| Nasdaq takes part of an order off (C or D with shares left) | The SOM is told nothing until the order ends, then `CancAck`. | `CancAck` at once and later fills on a canceled order. | Owner |
| Order Accepted for fewer shares than entered ("Total number of shares accepted", OUCH p.11) | The difference is treated as taken off by Nasdaq. | An error. | Nasdaq |
| Mirrored hosts (OUCH 1.3, p.4) | One host per OuchOM. After a fail-over to the mirror, Match Numbers prevent a second fill; sequence numbers are assumed to be the same on both. | Two connections with arbitration. | Nasdaq |
| Cancel-on-disconnect | Not in OUCH 5.0. Orders may stay live while OuchOM is down; their reports arrive in the replay. | Port configured to cancel on disconnect: the cancels arrive in the replay as `C`. | Port configuration with Nasdaq |
| Appendix references in the message tables | The appendix titles: options A, cancel reasons B, reject reasons C, liquidity flags D. The tables say B, C, D and E (e.g. pp.5, 14, 16). | none | Spec error |
| Order Restated's table title "Best Bid OR Ask Update Message" (pp.18-19) | Type `R`, Order Restated, per section 3.13's heading and the type row. | none | Spec error |
| Appendage Length on A, U, E, R, X, G, K (not marked optional) | Always present. The table takes it with the appendage as one trailing field, so a report that arrives without it still decodes (test `ReportWithoutAppendageLengthIsStillApplied`). | Treat it as malformed. | Nasdaq test environment |
| Symbol | The Kaspar asset name, at most 8 characters, sent as is. How Nasdaq writes share classes (`BRK.B`) in OUCH is not in the spec. | A per-asset OUCH symbol in the instrument definition. | Owner |
| Other sessions on the same account | Not supported: their UserRefNums after this OM's login are not known to it, and Nasdaq ignores a lower one (OUCH 1.2). | UserRefIdx (tag 28) per session. | Owner |
| Port rate limits | Not stated; `max_orders_per_sec` is local and must be set from the port's configuration. | none | Nasdaq |

## Decisions (owner, 2026-11-18)

All nine section-10 readings accepted as written: Account Query alone gives the next UserRefNum after a restart; US Eastern timestamps; unsigned 64-bit price; market orders allowed at any time; cross orders as Day with cross type O/C, extended hours as GTX, FOK refused; locate_id carries the locate broker code; first login asks for sequence 0; partial cancels by Nasdaq reported when the order ends; cancel-on-disconnect, rate limits, mirrored hosts and share-class symbology to be obtained from Nasdaq with a port.
