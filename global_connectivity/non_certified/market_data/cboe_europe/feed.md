# Feed description: Cboe Europe Equities Multicast PITCH (BXE, CXE)

Cboe Europe depth of book, by order, over UDP multicast, with retransmission
through the Gap Request Proxy (GRP) and snapshots from the Spin Server. The
handler targets the BXE and CXE books, which are Cboe's UK venue in London
(EU 1.6, p.7). The same protocol and document cover the DXE books (Cboe NL,
Amsterdam), the Trade Reporting Facility (TRF), Systematic Internaliser (SI)
quotes and the Retail Liquidity Feed; their differences are noted where they
affect decoding.

Every entry cites its source. Abbreviations:

- **EU**: Cboe Titanium Europe Multicast PITCH Specification, version 6.69,
  06 November 2026, PDF, 97 pp. Citations give section and page; the printed
  page number equals the PDF page.
- **PM**: Cboe Europe Equities Recognised Investment Exchange Participant
  Manual, version 59, 28 September 2026, PDF
  (https://cdn.cboe.com/resources/participant_resources/Cboe_Europe_Participant_Manual.pdf).
- **FIX**: Cboe Titanium Europe FIX Specification, version 3.53, 29 July 2026, PDF
  (https://cdn.cboe.com/resources/participant_resources/BATS_Europe_FIX_Specification.pdf).
- **US**: the U.S. extraction in `../cboe/feed.md` (Cboe Titanium U.S.
  Equities/Options Multicast PITCH 2.41.87), whose last section lists the
  differences in Cboe Europe. Section 11 below says where that list was wrong
  or incomplete.

No sample data: there is no public Cboe Europe capture (section 1). Nothing
here has been checked against real Cboe Europe packets.

## 1. Venue and documents

| Document | Version | Date | Applies to |
|---|---|---|---|
| Cboe Titanium Europe Multicast PITCH Specification (https://cdn.cboe.com/resources/participant_resources/BATS_Europe_MC_PITCH_Specification.pdf) | 6.69 | 06 November 2026 (EU p.1; Revision History, p.97) | BXE, CXE, DXE, TRF, SI quotes, Retail Liquidity Feed (EU 1.6-1.9, pp.7-9) |
| Cboe Europe Participant Manual | 59 | 28 September 2026 (PM p.1) | trading phases (PM 2.16, pp.32-33), matching priority (PM 2.1, p.10) |
| Cboe Titanium Europe FIX Specification | 3.53 | 29 July 2026 (FIX p.1) | priority after Order Cancel/Replace (FIX 5.3, p.27) |

Which version applies: the PDF given for this work and the one at the URL
above on 2026-11-18 are byte-identical (915,072 bytes): version 6.69 is the
current one. The task named "v6.59 or later"; 6.59 (29 September 2025) added
Trading Status `I` (EU Revision History, p.96). Changes since 6.59 that touch
decoding: 6.62 removed deprecated MMT v3 flags, 6.63 added MMT v5 values (NEGO,
DEFF), 6.64 changed the retail trade example, 6.67 decommissioned the indices
feeds from 1 October 2026 (EU Revision History, pp.96-97).

Sample data: none found. Databento's public sample bucket
(https://sample-pcaps-dl.databento.com/, directory listing read 2026-11-18) has
U.S. Cboe venues (`bats`, `baty`, `edga`, `edgx`) and Cboe Global Indices
(`cgif`) but no Cboe Europe dataset (`bate`, `chix`, `ceux` absent). The Cboe
multicast test program (https://cdn.cboe.com/resources/membership/mcast_pitch.zip,
EU 7.17, p.64) holds C source and a README, no data. A web search found no other
public Cboe Europe PITCH capture.

Documents referred to and not read: Cboe Europe BOE specification (403 at
the guessed URL; FIX was used for the modify priority rule), the symbol
reference data file (EU Appendix E, p.88; the footnote's bats.com URL is
obsolete), the Periodic Auctions Book Guidance Note and the Cboe Closing Cross
Guidance Note (PM 2.1).

## 2. Transport

- **Books and feeds.** CXE, BXE and DXE are separate books, each with 12 units
  and its own real-time and gap multicast groups, GRPs and Spin Servers (EU 1.6,
  p.7). Per book: Gig-shaped A and B, WAN-shaped C and D in Equinix Slough
  (LD4), WAN-shaped E in Park Royal (LD3, migrating to LD9) (EU 1.6, p.7; 7.1,
  p.39). CXE feeds XA, XB, XC, XD, XE; BXE feeds BA, BB, BC, BD, BE (EU 1.6,
  p.7). Content is identical across feeds; framing is not, so frame-level
  arbitration cannot fill gaps (EU 2.3, p.10).
- **Multicast addresses.** One or more units per group, UDP port 31200 + unit
  in production (31501-31512 in UAT) (EU 7.3, pp.41-42; 7.4, pp.43-46). Two
  units can share a group with different source addresses (BXE BA units 7 and
  8 on 224.0.83.134; EU 7.3.1, p.41). Addresses, ports and sources are
  configuration, not code; Cboe may change them with notice (EU 7.2, p.39).
- **GRP**: TCP, one per feed for all units: BXE ports 18984-18987 and 18971,
  CXE 19984-19987 and 19971 (EU 8.1, p.65; 8.2, p.66). Login first; heartbeats
  (empty unit headers) from the client at least every 5 s, two missed close the
  connection (EU 2.7, p.11; 3.1, p.12).
- **Spin Servers**: TCP, one port per unit, two servers per book (EU 8.1, p.65;
  8.2, p.66); one spin at a time (EU 5.5, p.37).
- **Gap limits**, per feed across all of a participant's GRP sessions: 100
  messages per request, 50 requests per second, 500 per minute, 100,000 per
  day, within 1,000,000 messages of the real-time sequence; gap responses
  delayed 2 ms to merge requests (EU 6, p.38).
- **Heartbeats**: unit header with Hdr Count 0 after 1 s without data; real-time
  heartbeats carry the next sequence, gap heartbeats 0; outside trading hours
  sequence 0; none from 00:00 to 01:00 London time (EU 2.7, p.11).
- **MTU** 1,500 bytes; shaping 1 Gb/s Gig, 50 Mb/s WAN (EU 6, p.38). Bandwidth
  per unit: EU 7.12-7.13, pp.61-62.
- **Hours** (London): feeds available from 07:00, open 08:00, close 16:30,
  closing auction to 16:35-16:41:30, Cboe Closing Cross to 16:55, connections
  severed 18:15 (PM 2.16, pp.31-32). No overnight session.

## 3. Framing

- Every UDP payload is one Sequenced Unit Header (8 bytes) and Hdr Count
  messages; a UDP frame holds one header and never crosses Ethernet frames
  (EU 2.1, p.10; 2.4, p.11). Hdr Length counts the header and the messages
  (EU 2.4, p.11). The example on p.82 is consistent: 0x31 = 49 = 8 + 25 + 16.
- GRP and Spin TCP streams carry the same header, unsequenced (unit and
  sequence 0), both ways; messages may cross TCP segments (EU 2.1, p.10; 2.4,
  p.11; 3, p.12).
- Each message: Length (1 byte, counts itself), Message Type (1 byte), body
  (EU 2.1, p.10). Messages may grow at their end and new types may appear
  without notice; decoders skip unknown types and extra bytes (EU 2.1, p.10).
  The field table declares the length as `@length_prefix 1 inclusive`, so its
  offsets are the specification's minus 1.
- Little endian, unsigned binary; Alphanumeric left justified, space padded
  (EU 2.2, p.10).
- One header holds either sequenced or unsequenced messages, never both
  (EU 2.4, p.11).
- **Shared layers.** The Sequenced Unit Header (EU 2.4, p.11), Login, Login
  Response, Gap Request, Gap Response (EU 3.1-3.4, pp.12-13), Spin Image
  Available, Spin Request, Spin Response and Spin Finished (EU 5.3-5.6,
  pp.36-37) have the same layouts as in the U.S. specification; Spin Login and
  Login Response are the GRP's (EU 5.1-5.2, p.36). They were compared field by
  field with `../cboe/pitch_us.tbl` and are generated from it (sections
  `unit::`, `grp::`, `spin::`). The U.S. table also defines the Instrument
  Definition messages (0x84-0x86) and the Gap Response status `U`, which EU
  does not have; they are never received from a Cboe Europe server and do no
  harm. `fieldgen.py`'s `@include` resolves only sections earlier in the same
  table, so it cannot reuse a section of another table; the EU table uses the
  same `@venue cboe` instead, so the two generated headers share their helper
  definitions and compile together (checked by the decode test, which includes
  both).

## 4. Message catalogue

Generated by `fieldgen.py cboe_europe.tbl --markdown`. Offsets are 0-based and
exclude the 1-byte inclusive length prefix (specification offset − 1); the
header size is the specification's Total Length − 1. Unit header, GRP and Spin
messages: `../cboe/feed.md` section 4 (same layouts, section 3 above).

### pitch::from_cboe_eu

#### TimeMessage (type `32` (0x20), 5 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | TimeMessage: new base second for the unit's time offsets (EU 4.1, p.14; spec 6 bytes) |
| Time | 1 | 4 | Binary | Whole seconds since midnight London time |

#### UnitClear (type `151` (0x97), 5 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | UnitClear: clear all orders of the unit in the header; sent in recovery events such as a data centre fail-over (EU 4.2, p.14; spec 6 bytes) |
| TimeOffset | 1 | 4 | Binary | Nanoseconds since the last unit timestamp |

#### AddOrderLong (type `64` (0x40), 34 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | AddOrderLong: a visible order added; binary incompatible with U.S. 0x21 (EU 4.3.1, p.15; spec 35 bytes) |
| TimeOffset | 1 | 4 | Binary | Nanoseconds since the last unit timestamp |
| OrderId | 5 | 8 | Binary | Day-specific identifier assigned to this order |
| SideIndicator | 13 | 1 | Alphanumeric | B buy, S sell |
| Quantity | 14 | 4 | Binary | Shares added (may be less than entered) |
| Symbol | 18 | 8 | Alphanumeric | Right padded with spaces |
| Price | 26 | 8 | BinaryLongPrice | Limit price; 4 implied decimals on BXE/CXE, 6 on TRF (EU 2.2, p.10) |

#### AddOrderShort (type `34` (0x22), 24 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | AddOrderShort: a visible order added, short form; no Add Flags (EU 4.3.2, p.15; spec 25 bytes) |
| TimeOffset | 1 | 4 | Binary | Nanoseconds since the last unit timestamp |
| OrderId | 5 | 8 | Binary | Day-specific identifier assigned to this order |
| SideIndicator | 13 | 1 | Alphanumeric | B buy, S sell |
| Quantity | 14 | 2 | Binary | Shares added (may be less than entered) |
| Symbol | 16 | 6 | Alphanumeric | Right padded with spaces |
| Price | 22 | 2 | BinaryShortPrice | Limit price, 2 implied decimals |

#### AddOrderExpanded (type `47` (0x2F), 39 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | AddOrderExpanded: a Systematic Internaliser quote, non-executable; SI quote feed only (EU 4.3.3, p.16; spec 40 bytes) |
| TimeOffset | 1 | 4 | Binary | Nanoseconds since the last unit timestamp |
| OrderId | 5 | 8 | Binary | Day-specific identifier assigned to this order |
| SideIndicator | 13 | 1 | Alphanumeric | B buy, S sell |
| Quantity | 14 | 4 | Binary | Shares applicable to this quote |
| Symbol | 18 | 8 | Alphanumeric | Right padded with spaces |
| Price | 26 | 8 | BinaryLongPrice | Quote price |
| AddFlags | 34 | 1 | Binary | Bit 1 SI Quote; bits 0, 2-7 reserved |
| ParticipantID | 35 | 4 | Alphanumeric | Participant the quote is attributed to |

#### OrderExecuted (type `35` (0x23), 29 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | OrderExecuted: a visible order executed at its booked price (EU 4.4, p.17; spec 30 bytes) |
| TimeOffset | 1 | 4 | Binary | Nanoseconds since the last unit timestamp |
| OrderId | 5 | 8 | Binary | Order Id of a previously sent Add Order |
| ExecutedShares | 13 | 4 | Binary | Shares executed |
| ExecutionId | 17 | 8 | Binary | Day-unique execution id, referenced by Trade Break; shown as 12 base-36 characters (EU 2.5, p.11) |
| ExecutionFlags | 25 | 4 | Alphanumeric | MMT levels 1, 2, 3.6, 3.9 (EU 4.4.1, p.17; values EU 4.17, pp.32-35) |

#### OrderExecutedAtPriceSize (type `36` (0x24), 41 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | OrderExecutedAtPriceSize: a visible order executed at a price other than its booked price (EU 4.5, p.18; spec 42 bytes) |
| TimeOffset | 1 | 4 | Binary | Nanoseconds since the last unit timestamp |
| OrderId | 5 | 8 | Binary | Order Id of a previously sent Add Order |
| ExecutedShares | 13 | 4 | Binary | Shares executed |
| RemainingShares | 17 | 4 | Binary | Shares remaining after the execution; 0 removes the order |
| ExecutionId | 21 | 8 | Binary | Day-unique execution id, referenced by Trade Break |
| Price | 29 | 8 | BinaryLongPrice | Execution price |
| ExecutionFlags | 37 | 4 | Alphanumeric | MMT levels 1, 2, 3.6, 3.9 (EU 4.5.1, p.18) |

#### ReduceSizeLong (type `37` (0x25), 17 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | ReduceSizeLong: a visible order partially reduced (EU 4.6.1, p.19; spec 18 bytes) |
| TimeOffset | 1 | 4 | Binary | Nanoseconds since the last unit timestamp |
| OrderId | 5 | 8 | Binary | Order Id of a previously sent Add Order |
| CancelledShares | 13 | 4 | Binary | Shares cancelled |

#### ReduceSizeShort (type `38` (0x26), 15 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | ReduceSizeShort: a visible order partially reduced, short form (EU 4.6.2, p.19; spec 16 bytes) |
| TimeOffset | 1 | 4 | Binary | Nanoseconds since the last unit timestamp |
| OrderId | 5 | 8 | Binary | Order Id of a previously sent Add Order |
| CancelledShares | 13 | 2 | Binary | Shares cancelled |

#### ModifyOrderLong (type `39` (0x27), 25 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | ModifyOrderLong: new size and price of a visible order; no Modify Flags (EU 4.7.1, p.20; spec 26 bytes) |
| TimeOffset | 1 | 4 | Binary | Nanoseconds since the last unit timestamp |
| OrderId | 5 | 8 | Binary | Order Id of the original Add Order |
| Shares | 13 | 4 | Binary | Shares after this modify |
| Price | 17 | 8 | BinaryLongPrice | Limit price after this modify |

#### ModifyOrderShort (type `40` (0x28), 17 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | ModifyOrderShort: new size and price of a visible order, short form; no Modify Flags (EU 4.7.2, p.20; spec 18 bytes) |
| TimeOffset | 1 | 4 | Binary | Nanoseconds since the last unit timestamp |
| OrderId | 5 | 8 | Binary | Order Id of the original Add Order |
| Shares | 13 | 2 | Binary | Shares after this modify |
| Price | 15 | 2 | BinaryShortPrice | Limit price after this modify, 2 implied decimals |

#### DeleteOrder (type `41` (0x29), 13 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | DeleteOrder: an open order completely cancelled (EU 4.8, p.20; spec 14 bytes) |
| TimeOffset | 1 | 4 | Binary | Nanoseconds since the last unit timestamp |
| OrderId | 5 | 8 | Binary | Order Id of the original Add Order |

#### TradeLong (type `65` (0x41), 47 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | TradeLong: execution of a hidden order or a routed execution; does not change the book; binary incompatible with U.S. 0x2A (EU 4.9.1, p.21; spec 48 bytes) |
| TimeOffset | 1 | 4 | Binary | Nanoseconds since the last unit timestamp |
| OrderId | 5 | 8 | Binary | Obfuscated Order Id, or the Order Id of the non-displayed order |
| SideIndicator | 13 | 1 | Alphanumeric | Always B for hidden trades |
| Shares | 14 | 4 | Binary | Shares executed |
| Symbol | 18 | 8 | Alphanumeric | Right padded with spaces |
| Price | 26 | 8 | BinaryLongPrice | Execution price |
| ExecutionId | 34 | 8 | Binary | Day-unique execution id, referenced by Trade Break |
| TradeFlags | 42 | 5 | Alphanumeric | MMT levels 1, 2, 3.1+3.13, 3.5+3.11+3.12, 3.9 (EU 4.9.3, p.22) |

#### TradeShort (type `43` (0x2B), 37 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | TradeShort: execution of a hidden order or a routed execution, short form (EU 4.9.2, p.21; spec 38 bytes) |
| TimeOffset | 1 | 4 | Binary | Nanoseconds since the last unit timestamp |
| OrderId | 5 | 8 | Binary | Obfuscated Order Id, or the Order Id of the non-displayed order |
| SideIndicator | 13 | 1 | Alphanumeric | Always B for hidden trades |
| Shares | 14 | 2 | Binary | Shares executed |
| Symbol | 16 | 6 | Alphanumeric | Right padded with spaces |
| Price | 22 | 2 | BinaryShortPrice | Execution price, 2 implied decimals |
| ExecutionId | 24 | 8 | Binary | Day-unique execution id, referenced by Trade Break |
| TradeFlags | 32 | 5 | Alphanumeric | MMT levels 1, 2, 3.1+3.13, 3.5+3.11+3.12, 3.9 (EU 4.9.3, p.22) |

#### TradeExtended (type `50` (0x32), 67 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | TradeExtended: trades reported to or executed by Cboe, including negotiated trades, retail (RLP) executions and amendments; does not change the book (EU 4.9.4, p.23; spec 68 bytes) |
| TimeOffset | 1 | 4 | Binary | Nanoseconds since the last unit timestamp |
| Shares | 5 | 8 | Binary | Shares executed |
| Symbol | 13 | 8 | Alphanumeric | Right padded with spaces |
| Price | 21 | 8 | BinaryLongPrice | Execution price; 0 if pending (MMT level 3.8) |
| TradeID | 29 | 8 | Binary | Unique for at least 7 calendar days |
| TradeTimestamp | 37 | 8 | Binary | Nanoseconds since 1970-01-01 UTC |
| ExecutionVenue | 45 | 4 | Alphanumeric | MIC of the execution venue (LISX, LISZ, XWAP, VWAP, SINT, XOFF, ...) |
| Currency | 49 | 3 | Alphanumeric | Traded currency |
| CboeTradeFlags | 52 | 1 | Alphanumeric | Timing, regulator and retail flags (EU 4.9.7, p.25) |
| ExtendedTradeFlags | 53 | 14 | Alphanumeric | 14-character MMT string (EU 4.9.6, p.25; EU 4.17, pp.32-35) |

#### TradeUnknownSymbol (type `53` (0x35), 71 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | TradeUnknownSymbol: a trade reported on a symbol not known to Cboe, by ISIN; TRF only (EU 4.9.5, p.24; spec 72 bytes) |
| TimeOffset | 1 | 4 | Binary | Nanoseconds since the last unit timestamp |
| Shares | 5 | 8 | Binary | Shares traded |
| Symbol | 13 | 12 | Alphanumeric | ISIN |
| Price | 25 | 8 | BinaryLongPrice | Reported price (6 implied decimals on TRF); 0 if pending |
| TradeID | 33 | 8 | Binary | Unique for at least 7 calendar days |
| TradeTimestamp | 41 | 8 | Binary | Nanoseconds since 1970-01-01 UTC |
| ExecutionVenue | 49 | 4 | Alphanumeric | MIC of the execution venue, SINT or XOFF |
| Currency | 53 | 3 | Alphanumeric | Reported currency |
| CboeTradeFlags | 56 | 1 | Alphanumeric | Timing, regulator and retail flags (EU 4.9.7, p.25) |
| ExtendedTradeFlags | 57 | 14 | Alphanumeric | 14-character MMT string |

#### TradeBreak (type `44` (0x2C), 13 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | TradeBreak: an earlier execution broken; no field table in EU section 4, layout from the example (EU Appendix A, p.76; Appendix B, p.81; spec 14 bytes) |
| TimeOffset | 1 | 4 | Binary | Nanoseconds since the last unit timestamp |
| ExecutionId | 5 | 8 | Binary | Execution Id of an earlier Order Executed or Trade |

#### EndOfSession (type `45` (0x2D), 5 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | EndOfSession: the unit shuts down; heartbeats may follow (EU 4.10, p.26; spec 6 bytes) |
| TimeOffset | 1 | 4 | Binary | Nanoseconds since the last unit timestamp |

#### TransactionBegin (type `188` (0xBC), 5 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | TransactionBegin: following messages up to Transaction End are one transaction (EU 4.11, p.26; spec 6 bytes) |
| TimeOffset | 1 | 4 | Binary | Nanoseconds since the last unit timestamp |

#### TransactionEnd (type `189` (0xBD), 5 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | TransactionEnd: the transaction opened by Transaction Begin is complete (EU 4.12, p.26; spec 6 bytes) |
| TimeOffset | 1 | 4 | Binary | Nanoseconds since the last unit timestamp |

#### TradingStatus (type `49` (0x31), 17 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | TradingStatus: trading status of a symbol (EU 4.13, p.27; spec 18 bytes) |
| TimeOffset | 1 | 4 | Binary | Nanoseconds since the last unit timestamp |
| Symbol | 5 | 8 | Alphanumeric | Right padded with spaces |
| Status | 13 | 1 | Alpha | T, R, C, S, N, V, I, O, E, H (reserved), M, P, U, Y (EU 4.13, p.27) |
| Reserved1 | 14 | 3 | Alpha | Reserved |

#### Statistics (type `52` (0x34), 23 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | Statistics: opening, closing, high, low and previous closing prices (EU 4.14, p.28; spec 24 bytes) |
| TimeOffset | 1 | 4 | Binary | Nanoseconds since the last unit timestamp |
| Symbol | 5 | 8 | Alphanumeric | Right padded with spaces |
| Price | 13 | 8 | BinaryLongPrice | Price |
| StatisticType | 21 | 1 | Alphanumeric | C closing, H high, L low, O opening, P previous closing |
| PriceDetermination | 22 | 1 | Alphanumeric | 0 normal, 1 manual (market supervision) |

#### AuctionUpdate (type `172` (0xAC), 36 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | AuctionUpdate: indicative price and size during auction call and extension phases (EU 4.15.1, p.29; spec 37 bytes) |
| TimeOffset | 1 | 4 | Binary | Nanoseconds since the last unit timestamp |
| Symbol | 5 | 8 | Alphanumeric | Right padded with spaces |
| AuctionType | 13 | 1 | Alphanumeric | O opening, C closing, H halt, V volatility, P periodic, U Cboe Closing Cross |
| ReferencePrice | 14 | 8 | BinaryLongPrice | Reference price used in tie-breaks |
| IndicativePrice | 22 | 8 | BinaryLongPrice | Price at which the auction would match now |
| IndicativeShares | 30 | 4 | Binary | Shares at the indicative price |
| OutsideTolerance | 34 | 1 | Alphanumeric | O outside the Cboe EBBO collar, I inside, - not specified |
| IncludesPrimary | 35 | 1 | Alphanumeric | P EBBO includes the primary market, N excludes, - not specified |

#### AuctionSummary (type `150` (0x96), 26 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | AuctionSummary: result of an auction (EU 4.15.2, p.30; spec 27 bytes) |
| TimeOffset | 1 | 4 | Binary | Nanoseconds since the last unit timestamp |
| Symbol | 5 | 8 | Alphanumeric | Right padded with spaces |
| AuctionType | 13 | 1 | Alphanumeric | O opening, C closing, H halt, V volatility, P periodic, U Cboe Closing Cross |
| Price | 14 | 8 | BinaryLongPrice | Auction price |
| Shares | 22 | 4 | Binary | Cumulative shares executed in the auction |

### Messages not in the field table

| Code | Message | Spec length | Why | Citation |
|---|---|---|---|---|
| 0xD8 | Index Quote | 29 | indices feed (CXE XIC, XID, XIE) decommissioned from 1 October 2026 | EU 4.16.1, p.31; Revision History 6.67, p.97 |
| 0xD9 | Index Quote EDSP | 28 | as above | EU 4.16.2, p.31 |

`pitch::from_cboe_eu::decode` returns `unknown` for them with the size to skip.

Restrictions within the table: Expanded Add Order on the SI quote feed only
(EU 4.3.3, p.16); Trade - Unknown Symbol on the TRF only (EU 4.9.5, p.24);
Statistics and Trade Break not on the Retail Liquidity Feed (EU 1.9, p.9).
Binary Long Price has 4 implied decimals on BXE and CXE and 6 on the TRF
(EU 2.2, p.10).

## 5. Sequencing and recovery

- **Scope**: one sequence per unit per book; +1 per sequenced message (EU 1.3,
  p.6). Messages carry no sequence: the n-th message of a frame has Hdr
  Sequence + n (EU 2.4, p.11). Several units share a multicast group, so the
  key is (book, unit). The starting value and rollover are not stated
  (section 10).
- **Gap rule**: expected = previous Hdr Sequence + previous Hdr Count (EU 2.4,
  p.11). Higher: gap. Lower: duplicate (other feed, or retransmission).
  Real-time heartbeats carry the next sequence and so reveal a gap at once
  (EU 2.7, p.11). Sequence 0: unsequenced.
- **A/B arbitration**: by unit and sequence, not by frame (EU 2.3, p.10).
  The handler drops duplicates by sequence, so it can take both feeds directly
  or the output of the existing arbitration.
- **Retransmission (GRP)**: Gap Request (unit, first sequence, count ≤ 100);
  one Gap Response per request; `A` means the messages come on the unit's gap
  multicast group; other statuses are rejects (EU 3.3-3.4, p.13; 6, p.38).
  Overlapping requests are merged; replay may come in non-contiguous blocks
  and before or after the Gap Response (EU 1.4, p.6). Indices data cannot be
  retransmitted (EU 3, p.12).
- **Snapshot (Spin)**: Spin Image Available once a second gives the sequence
  through which a spin is available; the Spin Request must carry one of the
  last ten such sequences; Spin Response gives the number of Add Orders and
  `A`, `O` or `S`; Spin Finished ends it, and is sent only if the request was
  accepted (EU 1.5, p.6; 5.3-5.6, pp.36-37). The spin is Trading Status and
  Statistics for every symbol first, with Time Offset 0, then the open orders
  (Add Order long and short) and Time messages; Auction Update where relevant
  (EU 1.5, p.6).
- **Alignment rule**: buffer multicast messages with sequence > the requested
  sequence; after Spin Finished apply them (EU 1.5, p.6; 5.4, p.36). A spin
  through S is usable only if every message after S is buffered (EU Appendix C,
  p.85: image 310169 useless with 310170-310171 missing; image 310175 usable,
  apply 310176 onward).
- **State to clear**: on Unit Clear, all orders of the unit; sent in recovery
  events such as a data centre fail-over (EU 4.2, p.14; Revision History 6.32,
  p.95). On a spin, the unit's books before applying it. Time base per unit on
  each Time message (EU 4.1, p.14). End of Session closes a unit; heartbeats
  may follow (EU 4.10, p.26).

## 6. Semantics

- **Book type**: market by order, full depth, visible orders only; hidden
  orders have no Add and are reported by Trade messages (EU 4.9, p.21).
- **Add Order** (0x40 long, 0x22 short): a new visible order with a
  day-specific Order Id; quantity may be less than entered (EU 4.3, p.15).
  **Expanded Add Order** (0x2F) is an SI quote, non-executable; SI quote
  modifies are Delete then Expanded Add (EU 1.7, pp.7-8; 4.3.3, p.16).
- **Order Executed** (0x23): at the booked price (EU 4.4, p.17); effects are
  cumulative, the order dies at 0 (EU 4, p.14).
- **Order Executed at Price/Size** (0x24): at a price other than the booked
  price; Remaining Shares is absolute, 0 removes (EU 4.5, p.18).
- **Reduce Size** (0x25, 0x26): shares cancelled, a delta (EU 4.6, p.19).
- **Modify Order** (0x27, 0x28): absolute shares and price; no Maintain
  Priority flag (EU 4.7, p.20). Priority: "Any change in Price or increase in
  OrderQty will result in the order losing its time priority" (FIX 5.3, p.27);
  price-display-time priority in the Integrated Book (PM 2.1, p.10). This is
  OB's rule.
- **Delete Order** (0x29): completely cancelled (EU 4.8, p.20).
- **Trade** (0x41, 0x2B): executions of hidden orders and routed executions;
  do not alter the book; Side always `B`; Order Id obfuscated by default
  (EU 4.9, p.21). Order Executed plus Trade messages give all executions
  (EU 4.9, p.21).
- **Trade - Extended** (0x32): trades reported to or executed by Cboe,
  including negotiated trades brought on exchange (EU 4.9.4, p.23); retail
  (RLP) executions on the main feed, Cboe Trade Flags `G`/`K` (EU 1.9, p.9);
  amendments of order-book or reported trades as a Cancellation-flagged copy of
  the original then a Modification-flagged amended trade (EU 2.6, p.11). Does not
  alter the book.
- **MMT flags**: Execution Flags (4 characters: levels 1, 2, 3.6, 3.9; EU 4.4.1,
  p.17), Trade Flags (5: levels 1, 2, 3.1+3.13, 3.5+3.11+3.12, 3.9; EU 4.9.3,
  p.22), Extended Trade Flags (14; EU 4.17, pp.32-35).
- **Transaction Begin/End** (0xBC, 0xBD): the messages between them are one
  transaction, e.g. one aggressor against several resting orders; any message
  type, any price level (EU 4.11-4.12, p.26).
- **Trading Status** (0x31): sent on every change and for suspended symbols
  before the start of trading (EU 4.13, p.27); phase meanings in PM 2.16,
  pp.32-33.
- **Statistics** (0x34): open, high, low, close, previous close; resent on a
  bust (EU 4.14, p.28).
- **Auctions**: Auction Update (0xAC) indicative price and size during call and
  extension phases; Auction Summary (0x96) the result (EU 4.15, pp.29-30).
  Auction executions arrive as executions flagged with an auction trading mode
  (EU Appendix B: Order Executed at Price/Size with `K`, p.79; Trade Short with
  `O`, p.80).
- **Time**: timestamp = London midnight + Time (s) + Time Offset (ns) (EU 4.1,
  p.14; every Time Offset field). No epoch or trade date on the feed.
- **Other books on the BXE environment**: the Periodic Auction Book and the
  Cboe Closing Cross are separate lit books "provided in the BXE environment"
  (PM 2.1, pp.10-12); EU does not say whether their orders appear as Add Orders
  on the BXE feed (section 10).

## 7. Mapping to Kaspar

By `.claude/skills/build-feed-handler/kaspar_mapping.md`; implemented in
`src/PitchEuHandler.cpp`. The handler keeps one order table per unit: Order Id
→ (`securityID`, side, price, quantity), and the set of `securityID`s seen per
unit ("the unit's books").

### Messages and records

| Venue message | Records, in order | Notes |
|---|---|---|
| Add Order 0x40, 0x22 | `l3_mbo_v2_t` action 0: `side` `'0'` for `B`, `'1'` for `S`; `pxd`; `displayQty`; `priority` = the unit sequence (0 in a spin; OB does not read it) | `recovery` = true when from a spin. Symbols not in the instrument file are tracked and not emitted |
| Expanded Add Order 0x2F | none | non-executable SI quote, SI feed only (EU 4.3.3, p.16) |
| Modify Order 0x27, 0x28 | `l3_mbo_v2_t` action 1, new price and quantity; action 2 if quantity 0 | venue rule = OB rule (FIX 5.3, p.27) |
| Reduce Size 0x25, 0x26 | `l3_mbo_v2_t` action 1 with old − cancelled; action 2 at 0 | a reduction keeps priority (FIX 5.3, p.27) = OB |
| Delete Order 0x29 | `l3_mbo_v2_t` action 2 | unknown ids counted and ignored |
| Order Executed 0x23 | `l3_mbp_trd_t` (print at the booked price), `l3_mbo_trd_v2_t`, `l3_mbo_v2_t` action 1 (old − executed) or 2 | one `transactTime`; executed > booked is counted and removes the order |
| Order Executed at Price/Size 0x24 | as 0x23; print at the message's Price; book change to Remaining Shares | |
| Trade 0x41, 0x2B | `l3_mbp_trd_t` only | `aggresorSide` 0 |
| Trade - Extended 0x32 | `l3_mbp_trd_t` or none, by the filter in review.md decision 1 | default: MMT level 1 in {1, 3, 5}, level 3.2/3.10 `-`, level 3.4 `-`, level 3.8 `P`, price > 0, shares ≤ 2³¹−1 |
| Trade - Unknown Symbol 0x35, Trade Break 0x2C | none | TRF only; Trade Break counted (section 10) |
| Trading Status 0x31 | `l3_sst_t` | table below |
| Unit Clear 0x97 | `l3_chr_v2_t` to each of the unit's books; clear the unit's order table | |
| Transaction Begin/End 0xBC, 0xBD | none; event boundary | see Event boundaries |
| Statistics 0x34, Auction Update 0xAC, Auction Summary 0x96 | none | Auction Summary repeats executions already printed |
| Time 0x20 | none | per-unit time base |
| End of Session 0x2D | none | unit marked closed |
| end of a packet with applied messages | `l3_eop_t`: `msgSeqNum` = Hdr Sequence, `sendingTime` = the unit's last `transactTime`; to the recorder only (`sec_id` −1) | as `mdp3/handler_if.hpp` |
| sequence gap | `l3_gap_v2_t` to the unit's books; `OnGap(unit, first, count)` | count 0 when the buffer overflowed: only a spin recovers |
| spin accepted (`spin_begin`) | `l3_chr_v2_t` to the unit's books; clear the unit's order table | spin Adds carry `recovery` = true |

`order_flags` 0; `sendingTime` = `transactTime` (PITCH has no send time).

### Trade prints and volume

One print per execution message: Order Executed (0x23, 0x24) for displayed
orders, Trade (0x41, 0x2B) for hidden orders and routed executions (EU 4.9,
p.21), and the Trade - Extended messages the filter admits. Auction Summary
sends none: auction fills arrive as executions (section 6). `aggresorSide`
uses CME values: on 0x23 and 0x24 the side opposite the resting order (1 buy,
2 sell), 0 when the Execution Flags show an auction (level 1 `5`, or level 2
`1`, `O`, `K`, `I`, `U`, `P`; EU 4.17, p.32); 0 on Trade and Trade - Extended.

### Order identifiers and instruments

- `orderID` = the venue's 64-bit Order Id unchanged. The handler keys its
  order tables by (unit, Order Id), so an id reused in another unit cannot
  overwrite an order; each OB holds one instrument, which lives in one unit
  (EU 1.3, p.6), so ids reaching one OB are unique. `l3_mbo_trd_v2_t` has no
  `securityID`; the handler routes it with the order's.
- `securityID` from the right-trimmed Symbol (6 characters in short forms, 8 in
  long and status messages) through `load_instruments()`
  (`config/instruments_example.csv`: `<symbol>,<securityID>`).

### Quantities and prices

- Shares, whole: `displayQty` and `lastQty`, `sz` take the fields as they are.
  Trade - Extended Shares is 8 bytes; values above 2³¹−1 send no print.
- `pxd` = Binary Long Price / 10⁴ or Binary Short Price / 10² (EU 2.2, p.10),
  in the instrument's trading currency unit (pence for GBX lines; the feed
  carries no currency except on Trade - Extended).

### Event boundaries

With transaction markers honoured (default; review.md decision 2), the records
of all messages between Transaction Begin and Transaction End are one event:
held until Transaction End, one `transactTime` (the first record's), and
`endOfEvent` on the last record **of each book** in the block, since a block
may cover several symbols and each OB sees only its own records. Outside a
block, each venue message is one event. A gap, Unit Clear or spin closes an
open block. Packets are not boundaries; a block may span packets, and the
end-of-packet record of a packet inside a block is released with the block.
`transactTime` is made strictly increasing per unit (+1 ns when equal or
earlier), as `sim/Mbp2Mbo.cpp` does, so that OB never sees two events with one
stamp or an event before a recovery.

### Trading status

| PITCH Status | Phase (PM 2.16, pp.32-33) | `tradingStatus` |
|---|---|---|
| `T` | continuous trading | 17 |
| `V` | volatility interruption: static collar reached, new aggressive orders beyond it refused | 17 |
| `N` | no reference price: dark matching unavailable | 17 |
| `O`, `E` | opening, closing auction: no automated executions | 21 |
| `M`, `P` | market order imbalance, price monitoring extension | 21 |
| `I` | volatility auction | 21 |
| `Y` | halt auction (EU 4.13 only) | 21 |
| `U` | post-market crossing (Cboe Closing Cross) | 21 (review.md decision 3) |
| `C` | closed | 4 |
| `H` | halt, reserved for future use (EU 4.13, p.27) | 2 |
| `S` | suspension: orders cancelled | 18 |
| `R` | off-book reporting only | 18 |

### Venue codes

`en::x::CBOEBXE` and `en::x::CBOECXE` in `venue`, one handler instance per
book. DXE has no code (review.md decision 4).

## 8. Instruments

- No instrument definitions on the feed. Symbols in Add Order, Trade, Trading
  Status, Statistics, auction messages; Cboe symbology, e.g. `VODl` (EU
  Appendix B). Short forms 6 characters, long and status forms 8.
- Units by market and alphabetical range, fixed intraday, changed with 48 hours
  notice (EU 1.3, p.6; 7.2, p.39); distribution in EU Appendix E, pp.88-89.
  Symbol-to-unit mapping from Cboe's reference data file (EU Appendix E,
  p.88); the handler does not need it: a unit's books are learned from the
  records sent.
- Prices: 4 implied decimals on BXE and CXE (EU 2.2, p.10). Ticks and trading
  currency: from the reference data file, not the feed.
- Quantities in shares.

## 9. Pitfalls

### Reading the specification

- Several documents: one EU document for BXE, CXE, DXE, TRF, SI and retail
  feeds; it is binary incompatible with US (EU Appendix D, p.87). Trading-phase
  semantics are in PM, priority rules in FIX.
- Errata and changes: Revision History (pp.90-97) removes Trade Break (6.9,
  2016) but later text and Appendix A still list it; indices decommissioned
  (6.67). Recorded in sections 4 and 10.
- Tables across page breaks: none in the message tables (each fits a page);
  every table checked by `fieldgen.py --check` against its stated total.
- Offsets 0-based, Length counts itself (EU, every table "including this
  field"); Hdr Length includes the header (EU 2.4, p.11).
- Reserved fields kept: Trading Status Reserved1 (3 bytes).
- Enumerations elsewhere: MMT values in EU 4.17, pp.32-35; Cboe Trade Flags in
  EU 4.9.7, p.25; trading phases in PM 2.16.
- Names differ: Time example labels Time "Sequence" (p.78); Expanded Add Order
  example labels its type "End of Session" (p.81); Trade - Extended example
  names Trade ID "Execution Id" (p.81).
- Examples contradict tables: Order Executed, Order Executed at Price/Size,
  Trade Long, Trade Short, Trade - Extended lengths; Gap Response without
  Count; Spin Finished with a Status byte; Index Quote 6-byte timestamps;
  Trading Status reserved bytes 00 (section 10).
- Two layers: unit header, PITCH, GRP and Spin in separate sections; GRP and
  Spin share codes 0x01, 0x02 (EU Appendix A, p.76).
- Repeated names: Add Order, Reduce Size, Modify Order, Trade in long and short
  forms with different codes; named AddOrderLong, AddOrderShort, etc.
- Variable-length fields: none.
- Odd-width integers: none.

### Framing and encoding

- Little endian throughout (EU 2.2, p.10).
- Unsigned throughout (EU 2.2, p.10).
- Fixed-point prices by field type; Binary Long Price decimals depend on the
  system: 4 BXE/CXE, 6 TRF (EU 2.2, p.10).
- Sentinels: Trade - Extended Price 0 = pending (EU 4.9.4, p.23); Trade Order
  Id obfuscated (EU 4.9, p.21).
- Strings left aligned, space padded (EU 2.2, p.10); Index Ticker NUL padded
  in its example (p.84; not tabled).
- Several messages per frame; never split across UDP frames; may split across
  TCP segments (EU 2.1, p.10).
- Timestamps: seconds since London midnight + ns offset (EU 4.1, p.14); Trade -
  Extended Trade Timestamp in ns since the Unix epoch (EU 4.9.4, p.23).

### Sequencing and recovery

- Scope per unit per book; units share multicast groups (section 5).
- A/B: duplicates dropped by sequence; frame arbitration impossible (EU 2.3).
- Gap rule and heartbeats: section 5.
- Recovery: GRP retransmission and Spin snapshot (section 5).
- Snapshot alignment: buffer > requested sequence; only image sequences from
  the last ten (EU 5.4, p.36); `spin_usable()` checks Appendix C's rule.
- Snapshot too old: wait for the next image (about 1 s, EU 5.3, p.36).
- State to clear: books and the unit's order table on Unit Clear and spin; the
  #122 defect (stale order map after a reset) is covered: Unit Clear clears the
  unit's map (test `UnitClearClearsOnlyThatUnit`).
- Rare paths exercised in tests: gap, out-of-order retransmission, duplicate,
  heartbeat-revealed gap, Unit Clear, late join with spin, live data during a
  spin. Not exercised: rollover, buffer overflow.
- Session: GRP and Spin heartbeats every 5 s or disconnect (EU 2.7, p.11);
  Login statuses (EU 3.2, p.12). The session actors are not written.

### Book semantics

- MBO, by order id, full depth.
- Level-index updates: not applicable.
- Depth limit: none.
- Priority on modify: price change or size increase loses it, size decrease
  keeps it (FIX 5.3, p.27): OB's rule, mapped to action 1.
- Hidden, reserve: hidden orders never on the book; RLP orders hidden on the
  main feed (EU 1.9, p.9); iceberg reloads get new time priority (PM, Reserve
  Orders, p.26); how a reload is shown on PITCH is not stated (section 10).
- Trades as book deltas: Order Executed is both; Trade and Trade - Extended are
  trades only (EU 4.9, 4.9.4).
- Trade side: Order Executed's side is the resting order's; Trade Side always
  `B` (EU 4.9.1, p.21).
- Trading states and crossed books: section 7 table; PM 2.16.
- Book clears: Unit Clear; Suspension cancels all orders (PM 2.16, p.33),
  which the feed is expected to show as Deletes (not stated).

### Prices, quantities and instruments

- Price only (no yield).
- No quantity multipliers.
- Decimals by field type; no sentinels except price 0 on Trade - Extended.
- Ticks: from reference data, not the feed.
- Instruments: no definitions on the feed; reference data file (section 8).
- Lifecycle: not addressed in EU.

### Behaviour the specification omits

- Subscription options: none (multicast).
- History on subscribe: none; spin gives the current book.
- Whole-book snapshot: a spin is the whole unit.
- Order within an event: transaction blocks group them; order inside a block
  not stated.
- Connection limits: GRP/Spin heartbeats 5 s; one spin at a time.

### Post-trade, order entry

Not applicable: PITCH cannot enter orders (EU 2, p.10).

## 10. Ambiguities

| Question | Reading taken | Alternatives | Resolved by |
|---|---|---|---|
| Order Executed: table 30 bytes, 4 flag characters (EU 4.4, p.17); example Length 0x1D (29), 3 flags (p.78). Same for 0x24 (42 vs 41, p.18 vs p.79), Trade Long (48 vs 47), Trade Short (38 vs 37) (p.21 vs p.80) | Tables | Examples | The examples lack exactly the last character, MMT level 3.9 (Algorithmic Trade), which the tables have; MMT v4.1 support came in 6.48 and the flag descriptions were updated in 6.49 (Revision History, p.96). Not confirmed on data; a 29-byte Order Executed would decode as malformed and be counted |
| Trade - Extended: example Length 0x43 (67) with 68 bytes listed; table 68 (EU p.23 vs pp.80-81) | 68 | 67 | Count of the listed bytes agrees with the table |
| Trade - Extended example Cboe Flag `R` "Retail" not in EU 4.9.7 (`G`/`K` for retail) | Table values | | Example text only; no effect on decoding |
| Gap Response example has no Count but Length 0x0A (p.77) | Table (EU 3.4, p.13): Count at 7 | | Length 10 needs the Count |
| Spin Finished example adds a Status byte after Length 6 (p.78) | Table, 6 bytes | | Length byte; decode by Length |
| Reduce Size example offset bytes `18 D9 06 00` = 448,792 ns, text "449,000" (p.83) | Bytes | | Example text only |
| Trading Status example Reserved bytes `00 00 00`; type Alpha (spaces) (p.81) | Ignore content | | Reserved |
| Execution Ids "12 characters, base 36" (EU 2.5, p.11) but Execution Id fields are 8-byte Binary | The 8-byte binary rendered in base 36 | a 12-character field | EU p.82 shows Order Id bytes `05 40 5B 77 8F 56 1D 0B` as `631WC4000005`, which is that binary in base 36 (checked in the decode test) |
| Trade Break: "Removal of Trade Break messages" (Revision History 6.9, p.94), yet listed in Appendix A (p.76), in an example (p.81) and in text added in 2025 (EU 1.7, p.8; 1.9, p.9; Revision History 6.57, p.96); no field table in section 4 | Tabled with the example's layout (14 bytes, same as U.S. 0x2C); no record | not sent at all | Not resolved; no record either way |
| Trade - Extended prints: which ones are volume on the BXE/CXE main feed (negotiated, RLP, amendments, off book) | Filter on MMT flags (section 7) | none; all | **review.md decision 1** |
| Transaction Begin/End on BXE/CXE main feeds: defined in EU 4.11-4.12 (p.26), stated for the Retail Liquidity Feed (EU 1.9, p.9), absent from Appendix A (p.76) | Honour where present; per-book `endOfEvent` | ignore | **review.md decision 2** |
| Trading phases `U`, `S`, `R` and crossed books | Section 7 table | | **review.md decision 3**; the other phases settled by PM 2.16 |
| Trading phases other than `U`, `S`, `R`: crossed book legal or not; EU 4.13 (p.27) only lists names | Section 7 table | | PM 2.16, pp.32-33 phase descriptions; `Y` from EU only |
| Spin taken through an image when messages after it are missing | Not usable; wait for the next image | | EU Appendix C, p.85; `spin_usable()` |
| Auction Summary repeats auction volume that also arrives as executions | Prints from executions only | prints from Auction Summary | Examples: Order Executed at Price/Size with trading mode `K` (p.79), Trade Short with `O` (p.80) |
| Periodic Auction Book and Cboe Closing Cross orders: shown as Add Orders on the BXE feed under the integrated book's symbol? (PM 2.1, pp.10-12; EU silent) | No: Add Orders are the Integrated Book | yes: OB would merge books | **review.md decision 5** |
| Modify priority | action 1; OB's rule | | FIX 5.3, p.27: price change or size increase loses priority, which is OB's rule. BOE not read |
| Reduce Size priority | keeps priority | | FIX 5.3, p.27 (decrease keeps priority) |
| Order Id scope: "day-specific" (EU 4.3, p.15), per unit or per book not stated | Tables keyed by (unit, Order Id); `orderID` = venue id | handler-assigned ids | Each OB holds one instrument and an instrument is in one unit (EU 1.3, p.6), so ids are unique per OB either way. The U.S. owner decision (venue id, one handler per exchange) is kept |
| Iceberg reload: new time priority (PM, Reserve Orders, p.26); shown as a new Add, or a Modify size-up? | Either works: a new Add is a new order; a size-up Modify loses priority in OB, as the venue does | | Not resolved; same result |
| Spin timestamps: spin messages carry Time Offset 0 for status and the spin's Time messages; OB rejects recovery records older than live ones | Clear the unit's books (CHR), then stamp strictly increasing per unit | | `kaspar_mapping.md`, `sim/Mbp2Mbo.cpp` |
| Time base after a spin: buffered messages' offsets may refer to a Time message at or before the spin sequence | The last Time of the spin is the base until the next sequenced Time | | Not resolved; affects timestamps by under 1 s at worst, not the book |
| Starting sequence and rollover: EU 1.3 (p.6) says +1 per message, gives neither | Start at 1; modular comparison | | Not resolved; a later start costs one unnecessary spin |
| Unsequenced messages on multicast (EU 2.4, p.11): which ones, not stated | Counted and ignored | | Not resolved |
| Index Quote timestamp 8 bytes (EU 4.16, p.31) vs 6 in the examples (p.84) | Not tabled | | Feed decommissioned (6.67, p.97) |
| SI Expanded Add example price with 6 decimals "assumes TRF system" (p.81); SI is not TRF | Not used | | SI feed out of scope |
| Scope "BXE and CXE, London and Amsterdam": BXE and CXE are the UK venue (London); the NL venue (Amsterdam) is DXE (EU 1.6, p.7), which has no `en::x` code | Handler built and tested for BXE and CXE; DXE decodes identically | add `CBOEDXE` | **review.md decision 4** |
| DST: Time is seconds from London midnight; on the change Sundays wall-clock and elapsed seconds differ by 1 h | Wall clock from London midnight of the trade date | | No trading on Sundays (PM 2.17) |

## 11. Corrections to the U.S. extraction's "Differences in Cboe Europe"

`../cboe/feed.md`, last section, was used as a starting point; every field
above was taken from EU. Where that list was wrong or incomplete:

| Item in the U.S. list | What EU says |
|---|---|
| Trade Long: "dark-book trades have Order Id 0 (EU example p.80)" | The example note dates from 2010 (Revision History 2.8, p.90) and was superseded a month later: "Order IDs in Trade Messages are now obfuscated by default. This obsoletes the change made to Order IDs on 20 May 2010" (2.9, p.90); EU 4.9 (p.21) says obfuscated |
| Execution Id: "12 characters, base 36" listed as a format difference | The field is the same 8-byte binary as in the U.S.; 12 base-36 characters is its printed form (EU p.82 example; decode test) |
| Trade Break: "assume same layout" | Omits that Revision History 6.9 (p.94) removed Trade Break while later text keeps it (section 10) |
| Index Quote and Index Quote EDSP listed as EU messages | Decommissioned from 1 October 2026 (Revision History 6.67, p.97) |
| Trading Status values listed without qualification | `H` is "reserved for future use" (footnote, EU 4.13, p.27) |
| Modify Order: "EU states no priority rule" | True of the PITCH document; FIX 5.3 (p.27) states it, and it equals OB's rule |
| Spin: "Trading Status and Statistics first (Time Offset 0), then Add Orders, Time, Auction Update" | EU 1.5 (p.6) gives the first part; the order of Time and Auction Update after the Adds is not stated |
| Heartbeats: "Same" | The quiet hour is 00:00-01:00 London time, not Eastern (EU 2.7, p.11) |
| Not mentioned | Retail Liquidity Feed: RLP executions are Trade - Extended on the main feed (`G`/`K`) and Order Executed on the retail feed, with the same execution id; consume one, not both (EU 1.9, p.9) |
| Not mentioned | BXE also hosts the Periodic Auction Book and the Cboe Closing Cross (PM 2.1); how they appear on PITCH is not stated (section 10) |
| Not mentioned | GRP limits also include 100,000 requests a day and 1,000,000 messages range (EU 6, p.38); the list gave per-second and per-minute only |

The U.S. list's message-by-message layouts (codes, offsets, lengths) all agree
with EU section 4, and its list of contradictions inside EU was confirmed.
