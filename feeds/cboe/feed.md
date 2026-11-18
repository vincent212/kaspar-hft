# Feed description: Cboe U.S. Equities Multicast PITCH

Cboe BZX, BYX, EDGX and EDGA depth of book, by order, over UDP multicast, with
retransmission through the Gap Request Proxy (GRP) and snapshots from the Spin
Server. The same document covers Cboe's four U.S. options exchanges; this
description extracts the equities messages and marks the options-only ones.
A final section lists the differences in Cboe Europe Multicast PITCH.

Every entry cites its source. Abbreviations:

- **US**: Cboe Titanium U.S. Equities/Options Multicast PITCH Specification,
  version 2.41.87, November 10, 2026 (Markdown edition). It has no section
  numbers or pages; citations give the heading and table, e.g. "US, Add Order
  Message Fields, Table 2".
- **EU**: Cboe Titanium Europe Multicast PITCH Specification, version 6.69,
  06 November 2026 (PDF, 97 pp.). Citations give section and PDF page.
- **Sample**: Databento's public sample capture
  `bats/20230822/ny4-bats-pitch-a-20230822T133000.pcap.zst` (BZX, A feed,
  2023-08-22 from 13:30:00 UTC), first 52,428,800 bytes after decompression:
  517,719 frames, 8.72 s. The capture is three years older than US 2.41.87.

## 1. Venue and documents

| Document | Version | Date | Applies to |
|---|---|---|---|
| Cboe Titanium U.S. Equities/Options Multicast PITCH Specification (https://cdn.cboe.com/resources/techspec/technical-specifications-cboe-titanium-u-s-equitiesoptions-multicast-pitch-specification.md) | 2.41.87 | November 10, 2026 (US, Revision History, last row) | BYX, BZX, EDGA, EDGX equities; BZX, C1, C2, EDGX options (US, Introduction) |
| Cboe Titanium Europe Multicast PITCH Specification (https://cdn.cboe.com/resources/participant_resources/BATS_Europe_MC_PITCH_Specification.pdf) | 6.69 | 06 November 2026 (EU p.1) | BXE, CXE, DXE, TRF, SI quotes, Indices (EU 1.6-1.8, pp.7-8); compared in the last section only |

Which version applies: US 2.41.87 is the current edition at the URL above and
covers all four equities venues in one document. Changes with a future effective
date that affect decoding are recorded where they apply: the Time Reference
message and the Epoch Time field of Time on BZX and EDGX equities from 11/02/26
(US, Revision History 2.41.72); Trading Status values E, L, O, P from 02/01/27
(US, Trading Status Message Fields, Table 1); Execution ID prefix `1` on BZX/EDGX
"Effective TBD" (US, Execution IDs; Revision History 2.41.86).

Documents referred to and not read: Cboe Titanium U.S. Equities Auction Process
Specification (auction semantics), Cboe Titanium U.S. Symbology Reference (US,
References), the daily unit-assignment CSV for units 32-35 and BZX-listed symbols
(US, U.S. Equities Unit/Symbol Distribution), the GRP and Spin Server TCP
addresses (not in US).

## 2. Transport

- **Real-time multicast**, one or more units per multicast group, UDP port
  `30000 + unit` on the primary data center and `31000 + unit` on the secondary
  (US, BZX Address/Unit Distribution, Tables 1-2; the BYX, EDGA, EDGX tables
  have the same shape). Feeds per exchange: Gig-shaped A, B; WAN-shaped C, D
  (primary) and E (secondary) (US, Multicast PITCH Feed Descriptions, Table 1).
  Content is identical across feeds, framing is not; frame-level arbitration
  cannot fill gaps (US, Message Framing). Sample: unit = UDP port − 30000 in all
  516,602 real-time frames.
- **Gap response multicast**: a separate group per unit range, same ports (US,
  BZX Address/Unit Distribution, "Gap Resp. MC" columns).
- **Gap Request Proxy** (GRP): TCP; Login first, then Gap Request; heartbeats
  (empty unit headers) from the client at least every 5 s, two missed ones close
  the connection (US, Heartbeat Messages; Login Message Fields). Limits per
  session: 100 messages per request, 320 requests per second, 1,500 per minute,
  100,000 per day, within 1,000,000 messages of the real-time sequence; gap
  responses are delayed 2 ms to merge requests (US, Limitations/Configurations -
  Production, Table 1).
- **Spin Server**: TCP, one per unit, same Login and heartbeat rules; one spin at
  a time (US, Spin Servers).
- **Heartbeats**: unit header with Hdr Count 0 after 1 s without data, on GRP and
  every multicast group; real-time heartbeats carry the next sequence, gap
  heartbeats sequence 0; outside trading hours sequence 0; none between 00:00 and
  01:00 ET (US, Heartbeat Messages). Sample: 258 heartbeats, all on gap groups,
  all with sequence 0; none on real-time groups during the 8.7 s.
- **MTU** 1500; Gig-shaped 1 Gb/s, WAN-shaped 100 Mb/s (500 Mb/s for equities
  C, D, E from 12/14/26) (US, Limitations/Configurations - Production).
- **Hours**: 23x5 on BZX and EDGX equities: Sunday about 2:35 p.m. ET to Friday
  8:05 p.m. ET, daily restart between 8:05 and 8:25 p.m. ET with a sequence
  reset (US, 24x5 Feed Hours and System Restart).

## 3. Framing

- Every UDP payload is one Sequenced Unit Header (8 bytes) followed by Hdr Count
  messages; a frame never holds two headers and never crosses Ethernet frames
  (US, Message Format; Sequenced Unit Header Message Fields, Table 1).
  Hdr Length counts the header and the messages. Sample: Hdr Length equals the
  UDP payload length, and the messages fill it exactly, in all 517,719 frames.
- The GRP and Spin TCP streams carry the same header, unsequenced (unit and
  sequence 0), in both directions (US, Sequenced Unit Header Message Fields; Gap
  Request Proxy Messages; Spin Messages). Messages may cross TCP segments (US,
  Message Format).
- Each message: Length (1 byte, counts itself), Message Type (1 byte, hex code),
  body (US, Message Format; every message table). A message may grow at its end
  without notice; decoders must skip unknown types and extra trailing bytes (US,
  Message Format). The field table declares the length as
  `@length_prefix 1 inclusive`, so its offsets are the specification's minus 1.
- All integers little endian, unsigned (US, Data Types). Text left justified,
  space padded (US, Data Types).
- One header holds either sequenced or unsequenced messages, never both (US,
  Sequenced Unit Header Message Fields).
- The same type code has different lengths by market: options append Trade
  Condition to 0x23, 0x24, 0x2A, 0x2B, 0x30 and Client ID plus 5 reserved bytes to
  0x2F; Time (0x20) is 6 bytes on BYX and EDGA and 10 on BZX and EDGX; Trading
  Status (0x31) has another layout of the same length on options (US, the tables
  named in section 4). Decoding by the Length byte handles all but the last.

## 4. Message catalogue

Generated by `fieldgen.py pitch_us.tbl --markdown`. Offsets are 0-based and
exclude the 1-byte inclusive length prefix (specification offset − 1); the
header size is the specification's Total Length − 1.

### unit::from_cboe

#### SequencedUnitHeader (type none, 8 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| HdrLength | 0 | 2 | Binary | Length of the header and the HdrCount messages that follow |
| HdrCount | 2 | 1 | Binary | Number of messages after this header; 0 is a heartbeat |
| HdrUnit | 3 | 1 | Binary | Unit of the messages; 0 when unsequenced |
| HdrSequence | 4 | 4 | Binary | Sequence of the first message; 0 when unsequenced; rolls from 4294967295 to 1 |

### unit::to_cboe

#### SequencedUnitHeader (type none, 8 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| HdrLength | 0 | 2 | Binary | Length of the header and the HdrCount messages that follow |
| HdrCount | 2 | 1 | Binary | Number of messages after this header |
| HdrUnit | 3 | 1 | Binary | 0 (unsequenced) |
| HdrSequence | 4 | 4 | Binary | 0 (unsequenced) |

### pitch::from_cboe

#### TimeReference (type `177` (0xB1), 17 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | TimeReference: midnight reference and trade date; BZX and EDGX equities only (US, Time Reference Message Fields, Table 1; spec 18 bytes) |
| MidnightReference | 1 | 4 | Binary | Midnight Eastern Time, whole seconds since 1970-01-01 UTC |
| Time | 5 | 4 | Binary | Whole seconds since the start of the Eastern Time calendar day (wall clock) |
| TimeOffset | 9 | 4 | TimeOffset | Nanoseconds since the last unit timestamp |
| TradeDate | 13 | 4 | BinaryDate | YYYYMMDD as a binary integer |

#### TimeMessage (type `32` (0x20), 5 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | TimeMessage: new base second for the unit's time offsets (US, Time Message Fields, Table 1; spec 6 bytes, 10 on BZX and EDGX equities) |
| Time | 1 | 4 | Binary | Whole seconds since the start of the Eastern Time calendar day (wall clock) |
| EpochTime | 5 | var | Opaque | BZX and EDGX equities only: 4 bytes, uint32 little endian, whole seconds since 1970-01-01 UTC; absent (tail_len 0) on BYX and EDGA |

#### UnitClear (type `151` (0x97), 5 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | UnitClear: clear every order of the unit in the header (US, Unit Clear Message Fields, Table 1; spec 6 bytes) |
| TimeOffset | 1 | 4 | TimeOffset | Nanoseconds since the last unit timestamp |

#### AddOrderLong (type `33` (0x21), 33 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | AddOrderLong: a visible order added to the book (US, Add Order Message Fields, Table 1; spec 34 bytes) |
| TimeOffset | 1 | 4 | TimeOffset | Nanoseconds since the last unit timestamp |
| OrderId | 5 | 8 | Binary | Day-specific order identifier |
| SideIndicator | 13 | 1 | Alphanumeric | B buy, S sell |
| Quantity | 14 | 4 | Binary | Shares added (may be less than entered) |
| Symbol | 18 | 6 | PrintableASCII | Right padded with spaces |
| Price | 24 | 8 | BinaryLongPrice | 4 implied decimals |
| AddFlags | 32 | 1 | BitField | Bit 0 reserved, set to 1; bit 3 AON (options only); others reserved |

#### AddOrderShort (type `34` (0x22), 25 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | AddOrderShort: a visible order added, short form (US, Add Order Message Fields, Table 2; spec 26 bytes) |
| TimeOffset | 1 | 4 | TimeOffset | Nanoseconds since the last unit timestamp |
| OrderId | 5 | 8 | Binary | Day-specific order identifier |
| SideIndicator | 13 | 1 | Alphanumeric | B buy, S sell |
| Quantity | 14 | 2 | Binary | Shares added (may be less than entered) |
| Symbol | 16 | 6 | PrintableASCII | Right padded with spaces |
| Price | 22 | 2 | BinaryShortPrice | 2 implied decimals |
| AddFlags | 24 | 1 | BitField | Bit 0 reserved, set to 1; bit 3 AON (options only); others reserved |

#### AddOrderExpanded (type `47` (0x2F), 40 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | AddOrderExpanded: a visible order added, 8-character symbol and attribution (US, Add Order Message Fields, Table 3; spec 41 bytes equities, 50 options) |
| TimeOffset | 1 | 4 | TimeOffset | Nanoseconds since the last unit timestamp |
| OrderId | 5 | 8 | Binary | Day-specific order identifier |
| SideIndicator | 13 | 1 | Alphanumeric | B buy, S sell |
| Quantity | 14 | 4 | Binary | Shares added (may be less than entered) |
| Symbol | 18 | 8 | PrintableASCII | Right padded with spaces |
| Price | 26 | 8 | BinaryLongPrice | 4 implied decimals |
| AddFlags | 34 | 1 | BitField | Bit 0 reserved, set to 1; bit 3 AON (options only); others reserved |
| ParticipantID | 35 | 4 | Alphanumeric | MPID, or RTAL for retail orders; spaces when not attributed |
| CustomerIndicator | 39 | 1 | Alphanumeric | R retail priority (EDGX equities only); space on other equities markets |

#### OrderExecuted (type `35` (0x23), 25 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | OrderExecuted: a visible order executed at its booked price (US, Order Executed Message Fields, Table 1; spec 26 bytes equities, 27 options) |
| TimeOffset | 1 | 4 | TimeOffset | Nanoseconds since the last unit timestamp |
| OrderId | 5 | 8 | Binary | Order executed |
| ExecutedQuantity | 13 | 4 | Binary | Shares executed |
| ExecutionId | 17 | 8 | Binary | Day-unique execution identifier, referenced by Trade Break |

#### OrderExecutedAtPriceSize (type `36` (0x24), 37 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | OrderExecutedAtPriceSize: a visible order executed at another price, or beyond its displayed size (US, Order Executed at Price/Size, Table 1; spec 38 bytes equities, 39 options) |
| TimeOffset | 1 | 4 | Binary | Nanoseconds since the last unit timestamp (typed Binary in the spec) |
| OrderId | 5 | 8 | Binary | Order executed |
| ExecutedQuantity | 13 | 4 | Binary | Shares executed |
| RemainingQuantity | 17 | 4 | Binary | Shares left on the book; 0 removes the order |
| ExecutionId | 21 | 8 | Binary | Day-unique execution identifier, referenced by Trade Break |
| Price | 29 | 8 | BinaryLongPrice | Execution price, 4 implied decimals |

#### ReduceSizeLong (type `37` (0x25), 17 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | ReduceSizeLong: part of a visible order cancelled (US, Reduce Size Message Fields, Table 1; spec 18 bytes) |
| TimeOffset | 1 | 4 | TimeOffset | Nanoseconds since the last unit timestamp |
| OrderId | 5 | 8 | Binary | Order reduced |
| CanceledQuantity | 13 | 4 | Binary | Shares cancelled |

#### ReduceSizeShort (type `38` (0x26), 15 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | ReduceSizeShort: part of a visible order cancelled, short form (US, Reduce Size Message Fields, Table 2; spec 16 bytes) |
| TimeOffset | 1 | 4 | TimeOffset | Nanoseconds since the last unit timestamp |
| OrderId | 5 | 8 | Binary | Order reduced |
| CanceledQuantity | 13 | 2 | Binary | Shares cancelled |

#### ModifyOrderLong (type `39` (0x27), 26 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | ModifyOrderLong: new size and price of a visible order (US, Modify Order Message Fields, Table 1; spec 27 bytes) |
| TimeOffset | 1 | 4 | TimeOffset | Nanoseconds since the last unit timestamp |
| OrderId | 5 | 8 | Binary | Order modified |
| Quantity | 13 | 4 | Binary | Shares after this modify |
| Price | 17 | 8 | BinaryLongPrice | Limit price after this modify, 4 implied decimals |
| ModifyFlags | 25 | 1 | BitField | Bit 0 display (aggregated in the Cboe SIP quote); bit 1 maintain priority (0 reset, 1 maintain) |

#### ModifyOrderShort (type `40` (0x28), 18 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | ModifyOrderShort: new size and price of a visible order, short form (US, Modify Order Message Fields, Table 2; spec 19 bytes) |
| TimeOffset | 1 | 4 | TimeOffset | Nanoseconds since the last unit timestamp |
| OrderId | 5 | 8 | Binary | Order modified |
| Quantity | 13 | 2 | Binary | Shares after this modify |
| Price | 15 | 2 | BinaryShortPrice | Limit price after this modify, 2 implied decimals |
| ModifyFlags | 17 | 1 | BitField | Bit 0 display (aggregated in the Cboe SIP quote); bit 1 maintain priority (0 reset, 1 maintain) |

#### DeleteOrder (type `41` (0x29), 13 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | DeleteOrder: a visible order removed from the book (US, Delete Order Message Fields, Table 1; spec 14 bytes) |
| TimeOffset | 1 | 4 | TimeOffset | Nanoseconds since the last unit timestamp |
| OrderId | 5 | 8 | Binary | Order removed; the same id may be added again later |

#### TradeLong (type `42` (0x2A), 40 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | TradeLong: execution of a non-displayed or routed order; does not change the book (US, Trade Message Fields, Table 1; spec 41 bytes equities, 42 options) |
| TimeOffset | 1 | 4 | TimeOffset | Nanoseconds since the last unit timestamp |
| OrderId | 5 | 8 | Binary | Obfuscated order id by default |
| SideIndicator | 13 | 1 | Alphanumeric | Always B, whatever the resting side |
| Quantity | 14 | 4 | Binary | Shares executed |
| Symbol | 18 | 6 | PrintableASCII | Right padded with spaces |
| Price | 24 | 8 | BinaryLongPrice | Execution price, 4 implied decimals |
| ExecutionId | 32 | 8 | Binary | Day-unique execution identifier, referenced by Trade Break |

#### TradeShort (type `43` (0x2B), 32 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | TradeShort: execution of a non-displayed or routed order, short form (US, Trade Message Fields, Table 2; spec 33 bytes equities, 34 options) |
| TimeOffset | 1 | 4 | TimeOffset | Nanoseconds since the last unit timestamp |
| OrderId | 5 | 8 | Binary | Obfuscated order id by default |
| SideIndicator | 13 | 1 | Alphanumeric | Always B, whatever the resting side |
| Quantity | 14 | 2 | Binary | Shares executed |
| Symbol | 16 | 6 | PrintableASCII | Right padded with spaces |
| Price | 22 | 2 | BinaryShortPrice | Execution price, 2 implied decimals |
| ExecutionId | 24 | 8 | Binary | Day-unique execution identifier, referenced by Trade Break |

#### TradeExpanded (type `48` (0x30), 42 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | TradeExpanded: execution of a non-displayed or routed order, 8-character symbol (US, Trade Message Fields, Table 3; spec 43 bytes equities, 44 options) |
| TimeOffset | 1 | 4 | TimeOffset | Nanoseconds since the last unit timestamp |
| OrderId | 5 | 8 | Binary | Obfuscated order id by default |
| SideIndicator | 13 | 1 | Alphanumeric | Always B, whatever the resting side |
| Quantity | 14 | 4 | Binary | Shares executed |
| Symbol | 18 | 8 | PrintableASCII | Right padded with spaces |
| Price | 26 | 8 | BinaryLongPrice | Execution price, 4 implied decimals |
| ExecutionId | 34 | 8 | Binary | Day-unique execution identifier, referenced by Trade Break |

#### TradeBreak (type `44` (0x2C), 13 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | TradeBreak: an earlier execution broken (US, Trade Break Message Fields, Table 1; spec 14 bytes) |
| TimeOffset | 1 | 4 | TimeOffset | Nanoseconds since the last unit timestamp |
| ExecutionId | 5 | 8 | Binary | Execution Id of an earlier Order Executed or Trade message |

#### EndOfSession (type `45` (0x2D), 5 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | EndOfSession: the unit shuts down; no more sequenced messages (US, End of Session Message Fields, Table 1; spec 6 bytes) |
| Timestamp | 1 | 4 | Binary | Nanoseconds since the last unit timestamp (named Timestamp in the spec) |

#### TradingStatusMessage (type `49` (0x31), 17 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | TradingStatusMessage: trading status of a symbol, equities layout (US, Trading Status Message Fields, Table 1; spec 18 bytes) |
| TimeOffset | 1 | 4 | TimeOffset | Nanoseconds since the last unit timestamp |
| Symbol | 5 | 8 | PrintableASCII | Right padded with spaces |
| TradingStatus | 13 | 1 | Alpha | A accepting orders for queuing, H halted, Q quote-only, S suspension, T trading; E, L, O, P effective 02/01/27 |
| RegSHOAction | 14 | 1 | Alphanumeric | 0 no price test, 1 Reg SHO price test in effect |
| Reserved1 | 15 | 1 | Alpha | Reserved |
| Reserved2 | 16 | 1 | Alpha | Reserved |

#### AuctionUpdate (type `149` (0x95), 46 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | AuctionUpdate: auction price and size; BZX and BYX equities only (US, Auction Update Message Fields, Tables 1 and 2; spec 47 bytes) |
| TimeOffset | 1 | 4 | TimeOffset | Nanoseconds since the last unit timestamp |
| StockSymbol | 5 | 8 | PrintableASCII | Right padded with spaces |
| AuctionType | 13 | 1 | Alphanumeric | O opening, C closing, H halt, I IPO, M Cboe Market Close (BZX); P periodic (BYX) |
| ReferencePrice | 14 | 8 | Binary | Collared auction price; spec type Binary, examples use 4 implied decimals |
| BuyShares | 22 | 4 | Binary | Buy shares at the reference price; matched shares for CMC |
| SellShares | 26 | 4 | Binary | Sell shares at the reference price; matched shares for CMC |
| IndicativePrice | 30 | 8 | Binary | Auction and continuous books match here; 0 on BYX; spec type Binary |
| AuctionOnlyPrice | 38 | 8 | Binary | Auction book alone matches here; 0 on BYX; spec type Binary |

#### AuctionSummary (type `150` (0x96), 26 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | AuctionSummary: auction result; BYX and BZX equities and options (US, Auction Summary Message Fields, Table 1; spec 27 bytes) |
| TimeOffset | 1 | 4 | TimeOffset | Nanoseconds since the last unit timestamp |
| StockSymbol | 5 | 8 | PrintableASCII | Right padded with spaces |
| AuctionType | 13 | 1 | Alphanumeric | C closing, H halt, I IPO, M Cboe Market Close, P periodic (BYX); O, G, V options |
| Price | 14 | 8 | BinaryLongPrice | Auction or CMC price, 4 implied decimals |
| Shares | 22 | 4 | Binary | Shares executed in the auction (Shares/Contracts in the spec) |

#### RetailPriceImprovementMessage (type `152` (0x98), 14 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | RetailPriceImprovementMessage: retail liquidity indicator; BYX and EDGX only (US, Retail Price Improvement Message Fields, Table 1; spec 15 bytes) |
| TimeOffset | 1 | 4 | TimeOffset | Nanoseconds since the last unit timestamp |
| Symbol | 5 | 8 | PrintableASCII | Right padded with spaces |
| RetailPriceImprovement | 13 | 1 | Alpha | B buy side, S sell side, A both, N none |

### grp::to_cboe

#### Login (type `1` (0x1), 21 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | Login: first message on the connection (US, Login Message Fields, Table 1; spec 22 bytes) |
| SessionSubId | 1 | 4 | Alphanumeric | Supplied by Cboe |
| Username | 5 | 4 | Alphanumeric | Supplied by Cboe |
| Filler | 9 | 2 | Alphanumeric | Spaces |
| Password | 11 | 10 | Alphanumeric | Supplied by Cboe |

#### GapRequest (type `3` (0x3), 8 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | GapRequest: request retransmission of a sequence range (US, Gap Request Message Fields, Table 1; spec 9 bytes) |
| Unit | 1 | 1 | Binary | Unit of the gap |
| Sequence | 2 | 4 | Binary | First (lowest) sequence of the range |
| Count | 6 | 2 | Binary | Number of messages; at most 100 (US, Limitations/Configurations - Production) |

### grp::from_cboe

#### LoginResponse (type `2` (0x2), 2 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | LoginResponse: login accepted or refused (US, Login Response Message Fields, Table 1; spec 3 bytes) |
| Status | 1 | 1 | Alphanumeric | A accepted, N not authorized, B session in use, S invalid session |

#### GapResponse (type `4` (0x4), 9 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | GapResponse: one per Gap Request (US, Gap Response Message Fields, Table 1; spec 10 bytes) |
| Unit | 1 | 1 | Binary | Unit from the request |
| Sequence | 2 | 4 | Binary | Sequence from the request |
| Count | 6 | 2 | Binary | Count from the request |
| Status | 8 | 1 | Alphanumeric | A accepted; O out of range, D daily, M minute, S second limit, C count limit, I invalid unit, U unit unavailable |

### spin::to_cboe

#### Login (type `1` (0x1), 21 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | Login: same layout as the GRP Login (US, Spin Messages, Login) |
| SessionSubId | 1 | 4 | Alphanumeric | Supplied by Cboe |
| Username | 5 | 4 | Alphanumeric | Supplied by Cboe |
| Filler | 9 | 2 | Alphanumeric | Spaces |
| Password | 11 | 10 | Alphanumeric | Supplied by Cboe |

#### SpinRequest (type `129` (0x81), 5 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | SpinRequest: request the book up to a sequence (US, Spin Request Message Fields, Table 1; spec 6 bytes) |
| Sequence | 1 | 4 | Binary | From one of the last ten Spin Image Available messages |

#### InstrumentDefinitionRequest (type `132` (0x84), 5 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | InstrumentDefinitionRequest: request a spin of Symbol Mapping messages; options use (US, Instrument Definition Request Message Fields, Table 1; spec 6 bytes) |
| Sequence | 1 | 4 | Binary | Must be 0 |

### spin::from_cboe

#### LoginResponse (type `2` (0x2), 2 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | LoginResponse: same layout as the GRP Login Response (US, Spin Messages, Login Response) |
| Status | 1 | 1 | Alphanumeric | A accepted, N not authorized, B session in use, S invalid session |

#### SpinImageAvailable (type `128` (0x80), 5 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | SpinImageAvailable: a spin is available through this sequence; about once a second (US, Spin Image Available Message Fields, Table 1; spec 6 bytes) |
| Sequence | 1 | 4 | Binary | Last sequence applied to the spin image |

#### SpinResponse (type `130` (0x82), 10 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | SpinResponse: whether the spin will be sent (US, Spin Response Message Fields, Table 1; spec 11 bytes) |
| Sequence | 1 | 4 | Binary | Sequence from the Spin Request |
| OrderCount | 5 | 4 | Binary | Number of Add Order messages in the spin |
| Status | 9 | 1 | Alphanumeric | A accepted, O out of range, S spin already in progress |

#### SpinFinished (type `131` (0x83), 5 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | SpinFinished: all messages of the spin sent (US, Spin Finished Message Fields, Table 1; spec 6 bytes) |
| Sequence | 1 | 4 | Binary | Sequence from the Spin Request |

#### InstrumentDefinitionResponse (type `133` (0x85), 10 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | InstrumentDefinitionResponse: whether the instrument spin will be sent; options use (US, Instrument Definition Response Message Fields, Table 1; spec 11 bytes) |
| Sequence | 1 | 4 | Binary | Always 0 |
| InstrumentCount | 5 | 4 | Binary | Number of Symbol Mapping and Complex Instrument Definition messages |
| Status | 9 | 1 | Alphanumeric | A accepted, O out of range, S spin already in progress |

#### InstrumentDefinitionFinished (type `134` (0x86), 1 bytes, 1-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | InstrumentDefinitionFinished: instrument spin complete; options use (US, Instrument Definition Finished Message Fields, Table 1; spec 2 bytes) |


### Messages not in the field table

Options-only messages. `pitch::from_cboe::decode` returns `unknown` with the size
to skip.

| Code | Message | Spec length | Citation |
|---|---|---|---|
| 0xBC | Transaction Begin | 6 | US, Transaction Begin Message Fields (Options Only) |
| 0xBD | Transaction End | 6 | US, Transaction End Message Fields (Options Only) |
| 0x2E | Symbol Mapping (unsequenced) | 38 | US, Symbol Mapping Message Fields (Options Only) |
| 0xD2 | Width Update | 19 | US, Width Update Message Fields (Options Only) |
| 0xD1 | Options Auction Update | 64 | US, Options Auction Update Message Fields (Options Only) |
| 0xAD | Auction Notification | 47 | US, Auction Notification Message Fields (C1, C2, and EDGX Options Only) |
| 0xAE | Auction Cancel | 14 | US, Auction Cancel Message Fields |
| 0xAF | Auction Trade | 34 | US, Auction Trade Message Fields |
| 0x9D | SOQ Strike Range Update | 42 | US, SOQ Strike Range Update Message Fields (C1 Only) |
| 0x9E | Constituent Symbol Mapping (unsequenced) | 58 | US, Constituent Symbol Mapping Message Fields (C1 Only) |

Options layouts of shared codes, not tabled: 0x23 +Trade Condition (27 bytes),
0x24 +Trade Condition (39), 0x2A (42), 0x2B (34), 0x30 (44, see section 10),
0x2F +Client ID, Reserved(5) (50), 0x31 Symbol(6) + Reserved(2), Trading
Status, Reserved, GTH Trading Status, Reserved2 (18) (US, the respective tables).

Equities-venue restrictions within the table: Time Reference and the Time Epoch
Time field BZX and EDGX only; Auction Update BZX and BYX; Auction Summary BZX and
BYX; Retail Price Improvement BYX and EDGX (US, message headings).

Spin-server messages 0x84, 0x85, 0x86 (Instrument Definition Request, Response,
Finished) serve the options Symbol Mapping spin (US, Spin Servers); they are in
the table because they are part of the Spin protocol.

## 5. Sequencing and recovery

- **Scope**: one sequence per unit, per exchange; starts at 1 each day, +1 per
  sequenced message; after 4,294,967,295 it rolls to 1, never 0 (US, Symbol
  Ranges, Units, and Sequence Numbers). Messages carry no sequence: the n-th
  message of a frame has Hdr Sequence + n (US, Sequenced Unit Header Message
  Fields). Several units share one multicast group, so the key is (exchange,
  unit), not the group.
- **Gap rule**: expected = previous Hdr Sequence + previous Hdr Count, with the
  rollover (US, Sequenced Unit Header Message Fields). Higher: gap. Lower: a
  duplicate (the other feed of an A/B pair, or a retransmission). Real-time
  heartbeats carry the next expected sequence and so reveal a gap at once (US,
  Heartbeat Messages). Sequence 0: unsequenced (heartbeat outside hours, Symbol
  Mapping on options), not part of the sequence (US, Sequenced Unit Header).
- **Restart**: on 23x5 BZX and EDGX the sequence resets daily between 8:05 and
  8:25 p.m. ET, after all orders are deleted; GTC/GTD orders are added again
  later (US, 24x5 Feed Hours and System Restart). End of Session (0x2D) closes a
  unit; heartbeats may follow (US, End of Session Message Fields). Unit Clear
  (0x97) is sent at startup each day on equities and in recovery events such as
  a data-center fail-over (US, Unit Clear Message Fields).
- **A/B arbitration**: by unit and sequence, not by frame (US, Message Framing).
  Reuse the existing arbitration (pitfalls 3).
- **Retransmission (GRP)**: Gap Request (unit, first sequence, count ≤ 100) on
  TCP; one Gap Response per request; status `A` means the messages come on the
  unit's gap multicast group with their original sequences (US, Gap Request
  Proxy and Message Retransmission; Gap Response Message Fields). Replayed data
  may arrive before or after the Gap Response; overlapping requests from all
  members are merged and sent once, in non-contiguous blocks (same section). A
  range crossing the rollover skips 0: received 4,294,967,293 then 3 → request
  4,294,967,294, count 4 (US, Gap Server Rollover Usage Example).
- **Snapshot (Spin)**: Spin Image Available (about once a second) gives the
  sequence through which a spin exists; Spin Request with that sequence; Spin
  Response gives the number of Add Orders; the spin is Time, Add Order (long,
  short, expanded), Trading Status, Auction Update (BZX), Retail Price
  Improvement (BYX, EDGX); Spin Finished ends it (US, Spin Servers; Spin
  Response, Spin Finished Message Fields).
- **Alignment rule**: buffer multicast messages with sequence > the requested
  Spin Image Available sequence from before the request; after Spin Finished,
  apply them to the spun book (US, Spin Servers; Spin Server Usage Example: SIA
  310175, apply cached 310176 onward). A spin taken through S is usable only if
  every message after S is buffered (same example: SIA 310169 is useless when
  310170-310171 were not cached). If the requested sequence is not one of the
  last ten Spin Image Available sequences, the spin is cut at the next greater
  one (US, Spin Servers), and the client cannot tell from Spin Finished which
  (section 10). A request above the latest image waits for the next image, then
  `O` (US, Spin Servers). Rollover: the same rule with sequences compared modulo
  the wrap (US, Spin Server Rollover Usage Example).
- **State to clear**: on Unit Clear, all orders of the unit (US, Unit Clear
  Message Fields); on a spin, the unit's books before applying it; on a daily
  restart, the order-ID map and time state of the unit; the Time base on every
  Time message and the midnight reference on every Time Reference, per unit (US,
  Time, Time Reference Message Fields).
- **Sample**: 35 real-time units (1-35, A feed), 723,551 sequenced messages in
  516,602 frames: no gaps, no duplicates, no unsequenced frames. 33 gap-response
  groups: 6,818 messages in blocks of 100 with gaps between blocks, as US says.
  Unknown 0, malformed 0, incomplete 0.

## 6. Semantics

- **Book type**: market by order, full depth, visible orders only; hidden orders
  have no Add (US, Trade Message Fields; Order Representation).
- **Add Order** (0x21 long, 0x22 short, 0x2F expanded): a new visible order with a
  day-specific Order Id; quantity may be less than entered (US, Add Order Message
  Fields). An Order Id deleted may be added again later (US, Delete Order Message
  Fields). A reserve order's refreshed display gets a new Order Id via a new Add
  (US, Reserve Orders, item 5).
- **Order Executed** (0x23): executes at the order's current booked price;
  remaining = previous − executed, removed at 0 (US, Order Executed Message
  Fields; Order Modification Messages).
- **Order Executed at Price/Size** (0x24): at another price, or more than the
  booked size (reserve); Remaining Quantity is absolute; 0 removes (US, Order
  Executed at Price/Size).
- **Reduce Size** (0x25, 0x26): quantity cancelled, a delta (US, Reduce Size
  Message Fields).
- **Modify Order** (0x27, 0x28): absolute new quantity and price; bit 1 of Modify
  Flags says whether priority is kept; a modify that changes nothing still loses
  priority (US, Modify Order Message Fields). Sample: all 51,501 modifies of orders
  added inside the capture had bit 1 = 0, including 144 that changed nothing and 6
  that reduced size at the same price.
- **Delete Order** (0x29): order removed (US, Delete Order Message Fields).
- **Trades**: Trade (0x2A, 0x2B, 0x30) reports executions of non-displayed and
  routed orders, and of the hidden part of reserve orders; it does not change the
  book (US, Trade Message Fields). Order Executed messages report displayed
  executions and do change the book. Together they are all executions (same).
  Trade Side Indicator is always `B`, whatever the resting side (US, Trade
  Message Fields, Tables 1-3); sample: 11,368 of 11,368 `B`. Order Id on Trade is
  obfuscated by default, so partial fills of one hidden order have different ids
  (US, Hidden Orders). Executed side of an Order Executed is the resting order's
  side.
- **Trade Break** (0x2C): breaks an earlier execution by Execution Id (US, Trade
  Break Message Fields).
- **Trading Status** (0x31): `S` implied at startup; `A` from 6:00 a.m. ET (2:30
  a.m. on BZX, EDGX), `T` from 7:00 (4:00) a.m.; `H` halt, `Q` quote-only for
  Cboe-listed auctions; Reg SHO flag (US, Trading Status Message Fields).
  Crossed-book legality per state is not stated (section 10).
- **Auctions**: Auction Update (0x95) indicative prices and sizes, Auction
  Summary (0x96) results, for Cboe-listed symbols on BZX and periodic auctions on
  BYX (US, Auction Update, Auction Summary Message Fields). Auction fills appear
  as executions with Execution Id prefix `C` (US, Execution IDs).
- **Time**: timestamp = Time (seconds since Eastern midnight, from the last Time
  message of the unit) + Time Offset (ns) (US, Data Types; Time Message Fields).
  On BZX and EDGX, Time Reference gives the midnight in epoch seconds and the
  trade date, and Time carries Epoch Time (US, Time Reference Message Fields).
  The 23x5 day crosses midnight: a new Time Reference, and Time restarts from 0
  (US, Time Message Fields). Sample (2023): 307 Time messages, all 6 bytes.
- **Implied, iceberg, hidden**: no implied orders; reserve orders show only the
  displayed part (US, Reserve Orders).

## 7. Mapping to Kaspar

Kaspar records in `chutil/include/bfile/r_l3.hpp`; emission as in
`mdp3/include/mdp3/handler_if.hpp` (`MDIncrementalRefreshBook` MBO path,
`MDIncrementalRefreshTradeSummary`, `SecurityStatus`, `Gap`, `ChannelReset`,
`EndOfPacket`) and `sim/src/Mbp2Mbo.cpp` (exec record before the book record).
PITCH sends deltas and refers to orders by id only, so the handler keeps a per
unit order table: id → (securityID, side, price, quantity).

| Venue message | Kaspar record or message | Notes |
|---|---|---|
| Sequenced Unit Header | per frame: sequencer; after the last message, `l3_eop_t` (`msgSeqNum` = Hdr Sequence, `sendingTime` = last transact time) | PITCH has no send time |
| Time Reference, Time | none; per-unit time state | `transactTime` = midnight (epoch ns) + Time × 1e9 + offset |
| Unit Clear | `l3_chr_v2_t` to each book of the unit; clear the unit's order table | needs the unit → symbols map (section 8) |
| Add Order 0x21, 0x22, 0x2F | `l3_mbo_v2_t`, `orderUpdateAction` 0, `side` `'0'`/`'1'` from `B`/`S`, `pxd` = Price / 10⁴ (long) or / 10² (short), `displayQty`, `priority` = unit sequence | `recovery` = true when from a spin |
| Modify Order 0x27, 0x28, Maintain Priority = 1 | `l3_mbo_v2_t` action 1 with new price and quantity | TachBook keeps priority unless size up or price change; a size-up with bit 1 = 1 would lose it there (section 10) |
| Modify Order, Maintain Priority = 0 | `l3_mbo_v2_t` action 2 then action 0, same order id | TachBook keeps priority on size-down or no-op change; the spec says those lose it (section 10) |
| Reduce Size 0x25, 0x26 | `l3_mbo_v2_t` action 1, `displayQty` = old − cancelled; action 2 at 0 | |
| Delete Order 0x29 | `l3_mbo_v2_t` action 2 | erase the order-table entry |
| Order Executed 0x23 | `l3_mbo_trd_v2_t` (`orderID`, `lastQty`), then `l3_mbo_v2_t` action 1 (old − executed) or 2 at 0 | the order Mbp2Mbo uses (`push_exec` then `push_mbo`) |
| Order Executed at Price/Size 0x24 | `l3_mbo_trd_v2_t`, then `l3_mbo_v2_t` action 1 with `displayQty` = Remaining, or 2 at 0 | the execution price is not carried: `l3_mbo_trd_v2_t` has no price, TachBook uses the booked price |
| Trade 0x2A, 0x2B, 0x30 | `l3_mbp_trd_t` (`securityID`, `pxd`, `sz`) | no book change; `side` unknown (always `B`); TachBook ignores it, OB counts it only with `USEMBP` |
| Trade Break 0x2C | none | no generic bust record |
| Trading Status 0x31 | `l3_sst_t`: `T` → 17 (ReadyToTrade, OB's matching status), `A`, `Q` → 21 (PreOpen), `H` → 2 (TradingHalt), `S` → 18 (NotAvailableForTrading) | CME `SecurityTradingStatus` codes, the ones OB reads; Reg SHO not representable |
| Auction Update 0x95, Auction Summary 0x96, Retail Price Improvement 0x98 | none | |
| End of Session 0x2D | none; mark the unit closed | |
| sequence gap | `l3_gap_v2_t` to the unit's books, then GRP request or spin | |
| spin start | `l3_chr_v2_t` to the unit's books | spin Adds then carry `recovery` = true |
| GRP, Spin session messages | none (session actor) | |

Event boundaries: `endOfEvent` must be set on the last record of an event. PITCH
equities have no transaction markers and frames differ across feeds (US, Message
Framing), so the boundary is a choice (section 10).

Gaps in Kaspar's records:

- `en::x` (`chutil/include/enum/exch_code.hpp`) has no Cboe venue codes; `venue`
  needs four new values (BZX, BYX, EDGX, EDGA).
- `l3_mbo_trd_v2_t` has no price and no securityID: Order Executed at
  Price/Size loses its execution price, and a Trade on an obfuscated order id
  cannot be routed by order id (handler_if routes MBOT through
  `orderid_to_securityid`).
- `l3_mbo_v2_t` cannot say "priority reset" on a change; TachBook infers it from
  size and price, which disagrees with PITCH's Maintain Priority bit.
- No record for a trade bust, Reg SHO state, auction indicative data, or the
  execution-id prefix (internal, routed, auction).

## 8. Instruments

- No instrument definitions on the equities feed. Symbols appear in Add Order,
  Trade, Trading Status; 6 characters in short and long forms, 8 in expanded
  forms and status/auction messages, right padded (US, message tables). The
  expanded forms exist for symbols longer than 6 characters (US, Add Order
  Message Fields, text before Table 3). Cboe symbology in a separate document (US, References).
- Units: symbols are assigned by alphabetical ranges, units 1-31, fixed per day;
  Cboe-listed ETPs dynamically on 32-35 and BZX-listed symbols dynamically, from a
  CSV published at 2:00 a.m. ET (US, U.S. Equities Unit/Symbol Distribution).
  Changes need 48 hours notice (same). Kaspar `sec_id`: from the symbol, through
  the instrument loader; the unit is needed for Unit Clear and recovery scope.
- Prices: Binary Long Price 4 implied decimals, Binary Short Price 2 (US, Data
  Types). Auction Update prices are typed Binary but use 4 decimals (section 10).
  Tick: not in the document (Reg NMS: $0.01 at or above $1, $0.0001 below; RPI in
  $0.001 increments (US, Retail Price Improvement)).
- Quantities in shares; long forms 4 bytes, short 2 (US, message tables).

## 9. Pitfalls

### Reading the specification

- Several documents: one US document for four equities venues and the options
  venues; EU separate and binary incompatible (EU Appendix D, p.87).
- Errata and change tables: future-dated changes in the Revision History affect
  Time (0x20) and Time Reference (0xB1) on BZX/EDGX from 11/02/26 and Trading Status values (from
  02/01/27); recorded in section 1.
- Tables across page breaks: the Markdown edition has no page breaks; every
  table's offsets were checked by `fieldgen.py --check`.
- Offsets 0-based; Length counts itself (US, every table "including this
  field"); Hdr Length includes the header (US, Sequenced Unit Header). Table
  offsets shifted by 1 for the prefix.
- Reserved and filler: Reserved1, Reserved2 on Trading Status, Filler on Login
  kept with their offsets.
- Enumerations: Trade Condition codes in a separate section, options only (US,
  Options Trade Condition Codes); Execution Id prefixes in a separate section
  (US, Execution IDs).
- Names differ: End of Session's offset field is named Timestamp; Order Executed
  at Price/Size types Time Offset as Binary; examples call Add Flags
  "AddBitField1" and Trading Status "Halt Status".
- Examples contradict tables: unit-header example (Hdr Length), Add Order Short
  example (8-byte price), Trade Short options example (33 vs 34), Trade Expanded
  (offsets and lengths), BYX Auction Update (reference price), Auction Summary
  (symbol) — all in section 10.
- Two layers: unit header, PITCH, GRP and Spin each in its own section; GRP and
  Spin both use 0x01 and 0x02 for Login and Login Response (US, Message Types),
  separate sections.
- Repeated names: Add Order, Reduce Size, Modify Order, Trade in long, short and
  expanded forms with different codes; named AddOrderLong, AddOrderShort, etc.
  Trade Expanded is labelled "(long)" in its table (US, Trade Message Fields,
  Table 3); named TradeExpanded from Message Types.
- Variable-length end: the Epoch Time of Time (0x20) is present on BZX/EDGX only;
  tabled as a trailing `bytes` field. No nested blocks.
- Odd-width integers: none; all 1, 2, 4, 8 bytes.

### Framing and encoding

- Endianness: little endian everywhere (US, Data Types).
- Signedness: all binary fields unsigned (US, Data Types).
- Fixed-point prices: global, by field type: 4 or 2 decimals (US, Data Types).
- Sentinels: Auction Update Indicative and Auction Only Price 0 on BYX (US,
  Auction Update, Table 2); ParticipantID spaces when not attributed.
- Strings: left aligned, space padded, printable ASCII (US, Data Types).
- Several messages per frame, never split across UDP frames; may split across
  TCP segments on GRP and Spin (US, Message Format).
- Timestamps: seconds since Eastern midnight (wall clock, DST-dependent) plus
  ns offset; epoch seconds only on BZX/EDGX (US, Time, Time Reference).

### Sequencing and recovery

- Scope: per unit per exchange; several units per multicast group (section 5).
- A/B arbitration by unit sequence; reuse the existing implementation.
- Gap, duplicate, older: section 5; heartbeats carry the next sequence.
- Recovery: GRP retransmission on multicast, and Spin snapshot over TCP.
- Snapshot alignment: buffer sequence > Spin Image Available sequence; apply
  after Spin Finished (section 5); ambiguity on non-image sequences (section 10).
- Snapshot too old: a Spin Image is usable only if everything after it is
  buffered; wait for the next image (about 1 s). Gap requests are limited
  (section 2).
- State to clear: books and order map per unit on Unit Clear, spin, daily
  restart; time state per unit (section 5). The order-id map must be per unit:
  ids are day-specific per exchange, and the #122 defect (stale map after a
  reset) applies to Unit Clear.
- Rare paths: inject gaps, duplicates, rollover (4,294,967,295 → 1), Unit Clear
  and End of Session in replay.
- Session: GRP and Spin heartbeats every 5 s or disconnect; Login status codes;
  daily sequence reset on BZX/EDGX 23x5 (section 2).

### Book semantics

- MBO, by order id, full depth.
- Level-index updates: not applicable.
- Depth limit: none.
- Queue priority on modify: explicit Maintain Priority bit; no-op modifies lose
  priority (US, Modify Order Message Fields); mapping in section 7, open in
  section 10.
- Hidden and reserve: hidden orders never on the book; reserve shows display
  only, refreshed under a new Order Id (US, Reserve Orders).
- Trades also book deltas: Order Executed is both trade and book delta; Trade is
  trade only and must not touch the book (US, Trade Message Fields).
- Trade side: Order Executed's side is the resting order's; Trade's side is
  always `B` and means nothing (US, Trade Message Fields).
- Trading states: section 6; crossed book legality not stated.
- Book clears: Unit Clear (daily on equities, fail-over), daily restart preceded
  by Deletes (US, 24x5 Feed Hours).

### Prices, quantities and instruments

- Price or yield: price only.
- Quantity multipliers: none on equities; shares.
- Decimals with sentinels: none; decimals fixed by field type.
- Price-dependent ticks: Reg NMS sub-dollar ticks (not in US); prices carry 4
  decimals so no conversion issue; short forms carry 2.
- Instrument definitions: none on the feed; symbol to unit from tables and the
  daily CSV (section 8).
- Lifecycle: new listings and IPO auctions (Auction Type `I`); symbol reuse not
  addressed in US.

### Behaviour the specification omits

- Subscription options: none (multicast).
- History on subscribe: none; Spin gives the current book.
- Snapshot of the whole book: a spin is the whole unit (about 12,500 orders per
  equities unit, US, Spin Server Usage Example notes).
- Order within an event: not stated; Reserve executions report display before
  hidden (US, Reserve Orders, item 4).
- Connection limits: GRP/Spin heartbeat 5 s; one spin at a time per server (US,
  Spin Servers).

### Post-trade

- FIX version, custom tags, credentials, Rejects in `toApp`, requests before
  session open, trade capture subscription, report lifecycle, duplicates, fixed
  income fields, sequence resets: not applicable (market data feed).

### Order entry

- Client ids, cancel/replace ids, fill races, unsolicited cancels, busts,
  throttling, reconnect duplicates, foreign orders, short sales: not applicable
  (PITCH cannot enter orders, US, Protocol).

## 10. Ambiguities

| Question | Reading taken | Alternatives | Resolved by |
|---|---|---|---|
| Unit-header example: Hdr Length bytes `31 00` (49), described as "50 bytes"; 8 + 26 + 16 = 50 (US, Example Messages, Sequenced Unit Header with 2 Messages) | 50; Hdr Length counts header and messages | 49 | Sample: Hdr Length = UDP payload in 517,719 of 517,719 frames. EU's own example is 49 for 8 + 25 + 16 (EU p.82). |
| Add Order Short example has an 8-byte price in a 26-byte message (US, Add Order - Short Message Example) | Table: 2-byte Binary Short Price at spec offset 23 | 8-byte price (32 bytes) | Sample: 399,578 short Adds of length 26 decoded, none malformed |
| Trade Expanded: Execution Id at 35 (8 bytes) ends at 43, but Trade Condition is at 41; options total 44, the options example's Length is 0x2B (43) (US, Trade Message Fields, Table 3; Trade - Expanded (Options) Message Example) | Equities: 43 bytes, no Trade Condition (consistent with the equities example) | Options: Trade Condition at 43, total 44; or example's 43 | Equities by example; options not resolved (not tabled) |
| Trade Short options example: Length byte 0x22 (34) described as "33 bytes" | 34 (table) | 33 | Table and Length byte agree |
| Auction Update prices typed "Binary", not Binary Long Price (US, Auction Update, Table 1) | 4 implied decimals | integer | Examples ($102.50 = 1025000) and Sample (opening auction updates on the scale of the Auction Summary prices: XJUL 304750 vs 305000) |
| BYX Auction Update example: Reference Prc `A4 0D 03 00…` labelled "n/a, set to zero"; Table 2 says the reference price is populated | Table 2: populated | always zero | Not resolved; no BYX sample |
| Auction Summary example symbol bytes `…56 5F` = "00mEV_", text "00mEVO" | bytes as sent | | Typo; no effect on decoding |
| Time 6 or 10 bytes | By Length: Epoch Time present when Length ≥ 10 | by venue configuration | Sample (2023, before 11/02/26): all 307 Time messages 6 bytes |
| Time Reference in equities spins: "Time Reference messages will be included in a spin response" (US, Time Reference) but the US Equities spin list omits it (US, Spin Servers) | Accept it in a spin | | Not resolved |
| Midnight for BYX and EDGA (no Time Reference); DST days repeat 01:00-01:59 (US, Time Message Fields) | Local trade date and America/New_York midnight | | Not resolved for BYX/EDGA; BZX/EDGX carry Time Reference and Epoch Time |
| Modify with Maintain Priority = 0 at same price and same or lower size: Kaspar TachBook keeps priority | Emit Delete + Add, same order id | a new `l3_mbo_v2_t` flag; or accept TachBook's inference | Sample: all 51,501 tracked modifies had bit 1 = 0, 150 of them size-down or no-op. Needs the user's decision |
| Modify with Maintain Priority = 1 and size up | Change record (priority kept) | | TachBook would reset priority; no case in the Sample |
| Order Executed at Price/Size when executed > booked (reserve) | `displayQty` = Remaining Quantity | booked − executed | Spec text (US, Order Executed at Price/Size); Sample: 20 of 130 had remaining ≠ booked − executed |
| Reserve refresh: the displayed order gets a new Order Id (US, Reserve Orders, item 5): is the old id deleted, or does it reach 0 by Order Executed? | Old id removed when its quantity reaches 0; a Delete is applied if it comes | | Not resolved |
| `endOfEvent`: no transaction markers on equities; frames differ by feed | Last message of each arbitrated frame of the unit | per message; per Time Offset change | Not resolved; affects TachBook publication |
| Spin requested with a sequence that is not an image: the spin runs to the next greater image, and Spin Finished echoes the requested sequence (US, Spin Servers; Spin Finished Message Fields) | Only request sequences taken from Spin Image Available | track the next image after the request | Design choice; the spec's own buffering note says buffered messages beyond the requested point "may" be in the spin |
| Spin Response Order Count counts Add Orders only; Trading Status, Time, Auction Update also come | Use Spin Finished, not the count, to end the spin | | US, Spin Response Message Fields |
| Gap replay may arrive before the Gap Response and in non-contiguous blocks (US, Gap Request Proxy and Message Retransmission) | Apply gap multicast by sequence regardless of response; drop duplicates | | Spec text; Sample gap groups show blocks of 100 |
| Trading states and crossed books: which states allow a crossed or locked book | Matching (OB) only in `T` | | Not stated in US; Auction Process Specification not read |
| Add Flags bit 0 "Reserved, set to 1"; the header example calls `01` "Display" | Ignore bit 0 | bit 0 = displayed | Sample: bit 0 set on all 409,937 Adds |
| Spin Request refers to "Options Specific Symbol Processing" for sequence rules; the rules are under Spin Servers | Spin Servers | | Cross-reference error |
| Instrument Definition Response counts "Complex Instrument Definition" messages, not defined in US | Unknown type, skip by length | | Options only; not resolved |
| Trading Status 0x31 has the same code and length in its options layout | Equities layout; a handler reads equities units only | detect by feed | Feeds are separate per exchange (US, Multicast PITCH Feed Descriptions) |

## Differences in Cboe Europe

Cboe Titanium Europe Multicast PITCH 6.69 (EU) against US 2.41.87, message by
message. Same: the Sequenced Unit Header (EU 2.4, p.11), heartbeats (EU 2.7,
p.11), little endian, inclusive 1-byte Length, Message Type (EU 2.1-2.2, p.10).
EU's own list: EU Appendix D, p.87.

### Framing, data types, sequencing

| Item | US | EU |
|---|---|---|
| Binary Long Price | 4 decimals | 4 on BXE/CXE, 6 on TRF (EU 2.2, p.10) |
| Time base | seconds since Eastern midnight | seconds since London midnight (EU 4.1, p.14) |
| Sequence rollover to 1 | stated (US, Symbol Ranges) | not stated (EU 1.3, p.6) |
| Units | 35 per equities exchange | 12 per book (CXE, BXE, DXE), separate TRF, SI quote, Indices, Retail Liquidity feeds (EU 1.6-1.9, pp.7-9) |
| Execution Id | 9-character base 36 with prefix meaning | "12 characters, base 36" (EU 2.5, p.11) |
| Gap limits | per session: 320/s, 1,500/min | per feed across sessions: 50/s, 500/min; Gig 1 Gb/s, WAN 50 Mb/s (EU 6, p.38) |
| GRP Gap Response status `U` | present | absent (EU 3.4, p.13) |
| Indices | | no retransmission for indices data (EU 3, p.12) |

### PITCH messages

| Message | US | EU | Difference |
|---|---|---|---|
| Time 0x20 | 6 or 10 bytes (Epoch Time) | 6 bytes (EU 4.1, p.14) | no Epoch Time; London midnight |
| Time Reference 0xB1 | BZX/EDGX | absent (EU Appendix A, p.76) | |
| Unit Clear 0x97 | 6 bytes | 6 bytes, same (EU 4.2, p.14); not in EU Appendix A | same layout |
| Add Order Long | 0x21, 34 bytes, Symbol 6 at 19, Price at 25, Add Flags at 33 | **0x40**, 35 bytes: Symbol **8** at 19, Price at 27, **no Add Flags** (EU 4.3.1, p.15) | binary incompatible, new code |
| Add Order Short 0x22 | 26 bytes, Add Flags at 25 | 25 bytes, no Add Flags (EU 4.3.2, p.15) | last field dropped |
| Add Order Expanded 0x2F | 41 bytes equities; Add Flags bit 0 reserved, bit 3 AON; ParticipantID; Customer Indicator at 40 | 40 bytes: Add Flags **bit 1 = SI Quote**; ParticipantID at 36; no Customer Indicator (EU 4.3.3, p.16) | SI quote feed only, non-executable |
| Order Executed 0x23 | 26 bytes | 30 bytes: **Execution Flags** (4 chars, MMT levels 1, 2, 3.6, 3.9) at 26 (EU 4.4, p.17) | appended field; field name Executed Shares |
| Order Executed at Price/Size 0x24 | 38 bytes | 42 bytes: Execution Flags (4) at 38 (EU 4.5, p.18) | appended field; Remaining Shares |
| Reduce Size 0x25, 0x26 | 18, 16 | 18, 16 (EU 4.6, p.19) | same; Cancelled Shares |
| Modify Order Long 0x27 | 27 bytes, Modify Flags at 26 | 26 bytes, no Modify Flags (EU 4.7.1, p.20) | no Maintain Priority bit; EU states no priority rule |
| Modify Order Short 0x28 | 19 bytes | 18 bytes, no Modify Flags (EU 4.7.2, p.20) | as above |
| Delete Order 0x29 | 14 | 14 (EU 4.8, p.20) | same; EU: "completely cancelled", no re-add note |
| Trade Long | 0x2A, 41 bytes, Symbol 6 | **0x41**, 48 bytes: Symbol 8 at 19, Price at 27, Execution Id at 35, **Trade Flags** (5 chars, MMT) at 43 (EU 4.9.1, p.21) | binary incompatible, new code; dark-book trades have Order Id 0 (EU example p.80) |
| Trade Short 0x2B | 33 bytes | 38 bytes: Trade Flags (5) at 33 (EU 4.9.2, p.21) | appended field |
| Trade Expanded 0x30 | 43 bytes | absent (EU Appendix D, p.87) | |
| Trade Extended 0x32 | absent | 68 bytes: Shares (**8 bytes**) at 6, Symbol 8 at 14, Price at 22, Trade ID 8 at 30, Trade Timestamp (epoch ns, 8) at 38, Execution Venue (MIC, 4) at 46, Currency 3 at 50, Cboe Trade Flags 1 at 53, Extended Trade Flags 14 at 54 (EU 4.9.4, p.23) | new; no Order Id; amendments are a Cancel-flagged then a Modification-flagged pair (EU 2.6, p.11) |
| Trade Unknown Symbol 0x35 | absent | 72 bytes: Shares 8, Symbol (ISIN) 12, Price, Trade ID, Trade Timestamp, Execution Venue, Currency, Cboe Trade Flags, Extended Trade Flags (EU 4.9.5, p.24); TRF only; not in Appendix A | new |
| Trade Break 0x2C | 14 bytes | listed (EU Appendix A, p.76) and in an example of 14 bytes (EU p.81), no field table in EU 4 | assume same layout; not on Retail Liquidity Feed (EU 1.9, p.9) |
| End of Session 0x2D | field "Timestamp" | field "Time Offset", 6 bytes (EU 4.10, p.26) | same layout |
| Transaction Begin/End 0xBC/0xBD | options only | all messages may be in a block, e.g. one aggressor against several orders (EU 4.11-4.12, p.26); not in Appendix A | usable as `endOfEvent` boundary in EU |
| Trading Status 0x31 | Status, Reg SHO, Reserved1, Reserved2 | Status at 14, Reserved1 (3 bytes) at 15; values T, R, C, S, N, V, I, O, E, H, M, P, U, Y (EU 4.13, p.27) | different enumeration; no Reg SHO |
| Statistics 0x34 | absent | 24 bytes: Symbol 8, Price 8, Statistic Type (C, H, L, O, P), Price Determination (0, 1) (EU 4.14, p.28); in spins | new |
| Auction Update | 0x95, 47 bytes | **0xAC**, 37 bytes: Reference Price 15, Indicative Price 23, Indicative Shares 31 (4), Outside Tolerance 35, Includes Primary 36 (EU 4.15.1, p.29) | different code and layout |
| Auction Summary 0x96 | 27 bytes; types C, H, I, M, P, O, G, V | 27 bytes, same offsets; types O, C, H, V, P, U (EU 4.15.2, p.30) | same layout, different enumeration |
| Index Quote 0xD8, Index Quote EDSP 0xD9 | absent | 29 and 28 bytes: Timestamp 8 (ns since midnight), Index Ticker 10, Price 8, Index Status (0xD8 only) (EU 4.16, p.31); dedicated feed | new |
| Retail Price Improvement 0x98, Symbol Mapping and other options messages | present | absent | |

### Session messages

| Message | Difference |
|---|---|
| Login 0x01, Login Response 0x02, Gap Request 0x03, Gap Response 0x04 | same layouts (EU 3.1-3.4, pp.12-13); Gap Response has no `U` status |
| Spin Image Available 0x80, Spin Request 0x81, Spin Response 0x82, Spin Finished 0x83 | same layouts (EU 5.3-5.6, pp.36-37); Spin Request must match one of the last ten images (EU 5.4, p.36), no "next greater image" rule; spin = Trading Status and Statistics for every symbol first (Time Offset 0), then Add Orders, Time, Auction Update (EU 1.5, p.6) |
| Instrument Definition 0x84-0x86 | absent |

### Contradictions inside EU (for the later extraction)

- Order Executed example Length 0x1D (29) with 3 flag bytes; table 30 bytes, 4
  flags (EU p.17 vs p.78-79). Order Executed at Price/Size: example 0x29 (41),
  table 42 (EU p.18 vs p.79).
- Trade Long example 0x2F (47) with 4 flag bytes; table 48, 5 flags. Trade Short
  example 0x25 (37); table 38 (EU p.21 vs p.80).
- Trade Extended example 0x43 (67); table 68; example names Trade ID "Execution
  Id" and shows Cboe Flag `R` "Retail", not in the table's values (EU p.23, p.25
  vs pp.80-81).
- Expanded Add Order example: type labelled "End of Session" (EU p.81).
- Gap Response example lacks Count (EU p.77); Spin Finished example has a Status
  byte (EU p.78); Time example labels the field "Sequence" (EU p.78).
- Index Quote examples: Timestamp 6 bytes, Length 0x19 (25) and 0x18 (24), against
  8-byte Timestamp, 29 and 28 bytes (EU p.31 vs p.84).
- Trading Status example Reserved bytes are 00, not spaces (EU p.81).
- Appendix A omits Unit Clear (0x97), Transaction Begin/End, Trade Unknown
  Symbol (0x35) and the Index messages, all defined in section 4 (EU p.76).
