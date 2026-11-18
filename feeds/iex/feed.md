# Feed description: IEX DEEP+ over IEX-TP, with DEEP+ SNAP recovery

Every entry cites its source: document, version, page, section. Abbreviations:
**TP** IEX Transport Specification v1.25; **DPLS** IEX DEEP+ Specification v1.06;
**SNAP** IEX DEEP+ SNAP Specification v1.07; **DEEP** IEX DEEP Specification v1.08.

## 1. Venue and documents

| Document | Version | Date | Applies to |
|---|---|---|---|
| IEX Transport Specification (IEX-TP) | 1.25 | May 12, 2017 | Segment framing, heartbeats, gap fill requests (UDP and TCP) for every IEX feed. Latest revision; its examples are DEEP v1.0 (protocol 0x8004) messages (TP p.10, p.15). |
| IEX DEEP+ Specification | 1.06 | October 8, 2026 (revision table: October 7, 2026) | The feed extracted here: order-by-order displayed book, trades, administrative messages. Message Protocol ID 0x8005 (DPLS p.4). |
| IEX DEEP+ SNAP Specification | 1.07 | October 8, 2026 | TCP request/response snapshot service for late start and recovery (SNAP p.3-4). |
| IEX DEEP Specification | 1.08 | October 19, 2021 | Reference only: the price-level feed (protocol 0x8004). Used to identify messages DEEP has and DEEP+ does not. |

All four are public, from https://www.iex.io/resources/trading/market-data.
Revision histories (DPLS p.47, SNAP p.13, TP p.15) were read; DPLS v1.01 (September 30,
2024) changed the Add Order type byte, without giving the old value. No errata
documents exist beyond the revision tables.

## 2. Transport

- **Multicast, UDP, IEX-TP**, one segment per UDP datagram; a segment never exceeds the
  UDP payload (TP p.3).
- **A/B feeds:** A and B groups carry the same messages in the same order, but may
  batch them into different segments (TP p.3). Arbitration is by sequence number, not by
  segment.
- **Single channel** (DPLS p.7): group 233.215.21.8 (A), 233.215.21.138 (B),
  233.215.21.74 (C, DR at Equinix CH4), port 10378; ITF 233.215.21.242:32001. Sources
  23.226.155.128/28 (A), .192/28 (B), .176/28 (C), 23.226.155.8/29 (ITF) (DPLS p.6).
- **Multi-channel** (DPLS p.9-10): channels 1-16, ports 13337-13352, A groups
  233.215.21.10-.25, B groups .140-.155, DR .96-.111, ITF 233.215.21.245. Channels 1-8
  carry symbols by first letter from A, C, E, H, K, O, S, U respectively; channels 9-16
  carry heartbeats only (DPLS p.6, p.10).
- **Gap fill** (DPLS p.7-8 single channel, p.10-12 multi-channel): UDP and TCP; UDP
  response at most 1,000 messages per request; TCP unlimited; request type Sequenced
  Messages only. Single-channel servers: UDP 23.226.155.170 (A) / .234 (B) / .251 (C) /
  .20 (ITF) port 11378 (ITF 33001); TCP .171 / .235 / .252 / .21, same ports.
  Multi-channel: UDP ports 23337-23352, TCP ports 33337-33352, server addresses per
  channel range in the tables on DPLS p.10-12.
- **TCP gap fill session** (TP p.4): connect, send a request at once (otherwise the server
  drops the connection), receive segments; the server closes the connection when all
  ranges are filled or the retransmission has caught up with the live feed. Several
  requests may be sent on one connection; ranges must not overlap and must increase,
  also across requests on one connection; an invalid request ends a TCP session (TP p.7).
- **UDP gap fill** (TP p.3-4): one request datagram; the response segments are unicast to
  the request's source address and port, so they can be read on the multicast socket. A
  request with no ranges is a test; the response is one segment with no payload.
- **Heartbeats:** a segment with Payload Length and Message Count 0 after one second
  without messages, carrying the next stream offset and next sequence number (TP p.7).
- **SNAP** (SNAP p.5-6): TCP, single channel 23.226.155.172 (A) / .236 (B) / .253 (C)
  port 11379, ITF 23.226.155.22:33381; multi-channel ports 13337-13352 with per-channel
  addresses (SNAP p.5-6). Credentials (an authentication token) from IEX Market Ops.
  Quota 1,000 requests per channel per day; simultaneous requests unlimited. No
  heartbeats; either side may disconnect after 10 s without data (SNAP p.4).

## 3. Framing

- **Endianness:** little-endian for every binary field (TP p.5; DPLS p.5; SNAP p.4).
- **Segment** (TP p.5-7): 40-byte IEX-TP header, then Message Count message blocks.
  Payload Length counts the blocks, not the header.
- **Message block** (TP p.6-7): 2-byte Message Length, exclusive of itself, then that
  many bytes of message data. The first byte of DEEP+ message data is the Message Type.
  Blocks follow each other with no padding.
- **Message growth** (DPLS p.4): IEX may add message types and lengthen any message, by
  appending fields or flag bits; decoders must skip unknown types and accept messages
  longer than expected. The generated decoder does both (`unknown` with the size from the
  length; a longer length is accepted and the extra skipped).
- **Sequence numbering:** the header gives the first message's sequence number; the
  others in the segment are numbered implicitly (TP p.6).
- **SNAP** (SNAP p.7-11): every message is a 2-byte exclusive length, a type byte and
  fields. The SNAP tables give offsets with the length at 0; the field table puts the
  length in `@length_prefix`, so its offsets are the spec's minus 2. SnapshotData wraps a
  40-byte IEX-TP header and one message block (see section 10 on the example's extra
  length).
- **Types** (DPLS p.5): String fixed-length ASCII, left justified, space filled; Long 8
  signed; Price 8 signed, 4 implied decimals; Integer 4 unsigned; Byte 1 unsigned;
  Timestamp 8 signed, ns since POSIX epoch UTC; Event Time 4 unsigned seconds (not used by
  any DEEP+ message).

## 4. Message catalogue

Generated by `fieldgen.py iex.tbl --markdown`. Offsets are 0-based from the Message Type
byte; the 2-byte length prefix, where present, is not counted. `--check` passes: 5
sections, 23 messages.

### tp::from_iex

#### SegmentHeader (type none, 40 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| Version | 0 | 1 | Byte | 1, version of the transport specification |
| Reserved | 1 | 1 | Byte | (Reserved) |
| MessageProtocolID | 2 | 2 | Short | Higher-layer protocol; DEEP+ is 0x8005 (DPLS p.4), DEEP is 0x8004 |
| ChannelID | 4 | 4 | Integer | Stream of sequenced messages; same Channel ID from several sources means duplicates by sequence number (p.6) |
| SessionID | 8 | 4 | Integer | Session; a message is identified by Session ID and sequence number (p.6) |
| PayloadLength | 12 | 2 | Short | Bytes of payload after this header; excludes the header (p.6) |
| MessageCount | 14 | 2 | Short | Message blocks in the payload; 0 for a heartbeat or gap-fill test response (p.6, p.7) |
| StreamOffset | 16 | 8 | Long | Byte offset of the payload in the session's data stream (p.6) |
| FirstMessageSequenceNumber | 24 | 8 | Long | Sequence number of the first message; later ones are numbered implicitly; in a heartbeat, the next sequence number (p.6, p.7) |
| SendTime | 32 | 8 | Timestamp | Send time of the segment, ns since epoch UTC |

### tp::to_iex

#### GapFillRequestHeader (type none, 16 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| Version | 0 | 1 | Byte | 1 |
| RequestType | 1 | 1 | Byte | 1 sequenced messages, 2 bytestream; DEEP+ supports 1 only (DPLS p.7, p.10) |
| MessageProtocolID | 2 | 2 | Short | 0x8005 for DEEP+ |
| ChannelID | 4 | 4 | Integer | Channel the request is for |
| SessionID | 8 | 4 | Integer | Current session, taken from a multicast segment header (p.8) |
| RequestRangeCount | 12 | 4 | Integer | Range blocks that follow; 0 for a UDP gap fill test request (p.8) |

#### RequestRangeSequenced (type none, 16 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| FirstMessageSequenceNumber | 0 | 8 | Long | First sequence number requested, inclusive |
| LastMessageSequenceNumber | 8 | 8 | Long | Last sequence number requested, inclusive |

#### RequestRangeBytestream (type none, 16 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| StartingStreamOffset | 0 | 8 | Long | First byte requested, inclusive |
| EndingStreamOffset | 8 | 8 | Long | Last byte requested, inclusive |

### deepplus::from_iex

#### SystemEvent (type `S`, 10 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | SystemEvent: market or feed event, one per type per channel per session (DPLS v1.06, p.13, System Event Message) |
| SystemEvent | 1 | 1 | Byte | O start of messages, S start of system hours, R start of regular market hours, M end of regular market hours, E end of system hours, C end of messages |
| Timestamp | 2 | 8 | Timestamp | Time of the system event |

#### SecurityDirectory (type `D`, 31 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | SecurityDirectory: pre-market spin of all IEX-listed securities, then changes (DPLS v1.06, p.14, Security Directory Message) |
| Flags | 1 | 1 | Byte | 0x80 test security, 0x40 when issued, 0x20 ETP (Appendix A, p.28) |
| Timestamp | 2 | 8 | Timestamp | Time of the security information |
| Symbol | 10 | 8 | String | Nasdaq Integrated symbology |
| RoundLotSize | 18 | 4 | Integer | Shares in a round lot |
| AdjustedPOCPrice | 22 | 8 | Price | Corporate-action adjusted previous official closing price |
| LULDTier | 30 | 1 | Byte | 0 not applicable, 1 Tier 1 NMS stock, 2 Tier 2 NMS stock |

#### TradingStatus (type `H`, 22 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | TradingStatus: pre-market spin for all securities, then changes; absent from the spin means operationally halted (DPLS v1.06, p.15-16, Trading Status Message) |
| TradingStatus | 1 | 1 | Byte | H halted across all US markets, O halt released into an order acceptance period (IEX-listed), P paused and order acceptance period (IEX-listed), T trading on IEX |
| Timestamp | 2 | 8 | Timestamp | Time of the trading status |
| Symbol | 10 | 8 | String | Nasdaq Integrated symbology |
| Reason | 18 | 4 | String | T1 IPO1 IPOD MCB3 NA for halts; T2 IPO2 IPO3 MCB1 MCB2 for order acceptance; blank for P and T |

#### RetailLiquidityIndicator (type `I`, 18 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | RetailLiquidityIndicator: change in eligible retail liquidity interest; 0x20 for all symbols before trading (DPLS v1.06, p.17, Retail Liquidity Indicator Message) |
| RetailLiquidityIndicator | 1 | 1 | Byte | 0x20 not applicable, A buy interest, B sell interest, C buy and sell interest |
| Timestamp | 2 | 8 | Timestamp | Time of the indicator |
| Symbol | 10 | 8 | String | Nasdaq Integrated symbology |

#### OperationalHaltStatus (type `O`, 18 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | OperationalHaltStatus: pre-market spin, then changes; absent from the spin means operationally halted (DPLS v1.06, p.18, Operational Halt Status Message) |
| OperationalHaltStatus | 1 | 1 | Byte | O IEX operational halt, N not operationally halted |
| Timestamp | 2 | 8 | Timestamp | Time of the operational halt status |
| Symbol | 10 | 8 | String | Nasdaq Integrated symbology |

#### ShortSalePriceTestStatus (type `P`, 19 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | ShortSalePriceTestStatus: Reg SHO Rule 201 status, pre-market spin then intraday changes (DPLS v1.06, p.19, Short Sale Price Test Status Message) |
| ShortSalePriceTestStatus | 1 | 1 | Byte | 0 not in effect, 1 in effect |
| Timestamp | 2 | 8 | Timestamp | Time of the status |
| Symbol | 10 | 8 | String | Nasdaq Integrated symbology |
| Detail | 18 | 1 | Byte | 0x20 no test, A activated, C continued, D deactivated, N not available (non-IEX-listed) |

#### SecurityEvent (type `E`, 18 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | SecurityEvent: opening or closing process complete for a security (DPLS v1.06, p.20, Security Event Message) |
| SecurityEvent | 1 | 1 | Byte | O opening process complete, C closing process complete |
| Timestamp | 2 | 8 | Timestamp | Time of the security event |
| Symbol | 10 | 8 | String | Nasdaq Integrated symbology |

#### AddOrder (type `a`, 38 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | AddOrder: a displayed order added to the IEX book (DPLS v1.06, p.21, Add Order Message; type byte changed in v1.01) |
| Side | 1 | 1 | Byte | 0x38 '8' buy, 0x35 '5' sell |
| Timestamp | 2 | 8 | Timestamp | Time of the new order |
| Symbol | 10 | 8 | String | Nasdaq Integrated symbology |
| OrderID | 18 | 8 | Long | Order ID; may be reused by a later Add Order after a delete (p.23) |
| Size | 26 | 4 | Integer | Quoted (displayed) size |
| Price | 30 | 8 | Price | Booking price, 4 implied decimals |

#### OrderModify (type `M`, 38 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | OrderModify: price, size or priority of a displayed order changed (DPLS v1.06, p.22, Order Modify Message) |
| ModifyFlags | 1 | 1 | Byte | Bit 0: 0 reset priority, 1 maintain priority; bits 1-7 reserved |
| Timestamp | 2 | 8 | Timestamp | Time of the modification |
| Symbol | 10 | 8 | String | Nasdaq Integrated symbology |
| OrderIDReference | 18 | 8 | Long | Order ID from the Add Order |
| Size | 26 | 4 | Integer | New total quoted size |
| Price | 30 | 8 | Price | Booking price, 4 implied decimals |

#### OrderDelete (type `R`, 26 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | OrderDelete: a displayed order removed from the IEX book (DPLS v1.06, p.23, Order Delete Message) |
| Reserved | 1 | 1 | Byte | Reserved for future use |
| Timestamp | 2 | 8 | Timestamp | Time of the deletion |
| Symbol | 10 | 8 | String | Nasdaq Integrated symbology |
| OrderIDReference | 18 | 8 | Long | Order ID from the Add Order |

#### OrderExecuted (type `L`, 46 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | OrderExecuted: a displayed order executed against; removed when its quantity reaches 0, otherwise keeps time priority (DPLS v1.06, p.24, Order Executed Message) |
| SaleConditionFlags | 1 | 1 | Byte | 0x80 ISO, 0x40 extended hours, 0x20 odd lot, 0x10 trade-through exempt, 0x08 single-price cross (Appendix A, p.28-29) |
| Timestamp | 2 | 8 | Timestamp | Time of the trade |
| Symbol | 10 | 8 | String | Nasdaq Integrated symbology |
| OrderIDReference | 18 | 8 | Long | Order ID of the resting order executed |
| Size | 26 | 4 | Integer | Trade volume |
| Price | 30 | 8 | Price | Trade price; may differ from the order's displayed price (price improvement, sliding) |
| TradeID | 38 | 8 | Long | IEX generated identifier |

#### Trade (type `T`, 38 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | Trade: a non-displayed order executed against another non-displayed order; no displayed order changes (DPLS v1.06, p.25, Trade Message) |
| SaleConditionFlags | 1 | 1 | Byte | As Order Executed (Appendix A) |
| Timestamp | 2 | 8 | Timestamp | Time of the trade |
| Symbol | 10 | 8 | String | Nasdaq Integrated symbology |
| Size | 18 | 4 | Integer | Trade volume |
| Price | 22 | 8 | Price | Trade price |
| TradeID | 30 | 8 | Long | IEX generated identifier |

#### TradeBreak (type `B`, 38 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | TradeBreak: an execution broken on the same trading day (DPLS v1.06, p.26, Trade Break Message) |
| SaleConditionFlags | 1 | 1 | Byte | As Order Executed (Appendix A) |
| Timestamp | 2 | 8 | Timestamp | Time of the trade break |
| Symbol | 10 | 8 | String | Nasdaq Integrated symbology |
| Size | 18 | 4 | Integer | Trade break volume |
| Price | 22 | 8 | Price | Trade break price |
| TradeID | 30 | 8 | Long | Trade ID of the Order Executed or Trade message broken |

#### ClearBook (type `C`, 18 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | ClearBook: the IEX book for a symbol cleared of all orders (DPLS v1.06, p.27, Clear Book Message) |
| Reserved | 1 | 1 | Byte | Reserved for future use |
| Timestamp | 2 | 8 | Timestamp | Time of the clear |
| Symbol | 10 | 8 | String | Nasdaq Integrated symbology |

### snap::from_iex

#### ErrorResponse (type `e`, 2 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | ErrorResponse: the SnapshotRequest was rejected (SNAP v1.07, p.8, ErrorResponse Message; spec Message Length 2) |
| RejectReasonCode | 1 | 1 | Byte | Spec offset 3. A authentication, C wrong ChannelID, E request already active, Q quota exceeded, R no snapshot >= Minimum Sequence Number yet, S wrong SessionID, U unknown message |

#### SnapshotStart (type `s`, 9 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | SnapshotStart: first message of a snapshot response (SNAP v1.07, p.9, SnapshotStart Message; spec Message Length 9) |
| SnapshotLength | 1 | 8 | Long | Spec offset 3. Bytes of the whole response, this SnapshotStart, every SnapshotData and the SnapshotEnd, length prefixes included (example p.9: SnapshotStart counts 11) |

#### SnapshotData (type `d`, 43 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | SnapshotData: one DEEP+ message in an IEX-TP segment with Message Count 1 (SNAP v1.07, p.10, SnapshotData Message) |
| IEXTPHeader | 1 | 40 | Bytes | Spec offset 3. IEX-TP segment header (tp::from_iex SegmentHeader); carries the DEEP+ sequence number and send time |
| IEXTPMessageBlockLength | 41 | 2 | Short | Spec offset 43, typed Integer with length 2. IEX-TP Message Length of the block that follows |
| IEXTPMessageData | 43 | var | Bytes | Spec offset 45. The block's message data; see feed.md section 10 on the extra length in the example (p.10) |

#### SnapshotEnd (type `x`, 9 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | SnapshotEnd: last message of a snapshot response (SNAP v1.07, p.11, SnapshotEnd Message; spec Message Length 9) |
| SnapshotSequenceNumber | 1 | 8 | Long | Spec offset 3. DEEP+ sequence number at which the snapshot was created |

### snap::to_iex

#### SnapshotRequest (type `r`, 57 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | SnapshotRequest: authenticate and request a snapshot; the only client message (SNAP v1.07, p.7, SnapshotRequest Message; spec Message Length 57, total 59) |
| AuthenticationToken | 1 | 40 | Token | Spec offset 3. Token from IEX Market Ops, left justified, space padded |
| ChannelID | 41 | 4 | Integer | Spec offset 43. Channel 1-16, from the multicast IEX-TP header |
| SessionID | 45 | 4 | Integer | Spec offset 47. Session, from the multicast IEX-TP header |
| MinimumSequenceNumber | 49 | 8 | Long | Spec offset 51. Lowest snapshot sequence number usable; 0 for the latest snapshot |


## 5. Sequencing and recovery

- **Scope:** sequence numbers are per (Channel ID, Session ID) within a Message Protocol ID
  (TP p.6). A message is identified by Session ID and sequence number. Segments from
  several sources with one Channel ID are duplicates by sequence number (TP p.6).
- **Gap rule:** expected = previous First Message Sequence Number + previous Message
  Count. A segment whose first number is above that is a gap; below it, a duplicate (the
  other feed), whose messages already applied are dropped; a segment that straddles the
  expected number is applied from the expected message on. Heartbeats carry the next
  sequence number and so reveal a gap after the last data segment (TP p.7).
- **Stream offset:** a second continuity check by byte offset (TP p.6). DEEP+ supports
  only sequenced-message gap fill (DPLS p.7), so the handler uses the sequence number.
- **Retransmission:** Gap Fill Request (TP p.7-9) with one or more sequenced ranges,
  inclusive at both ends; UDP up to 1,000 messages per request, TCP unlimited (DPLS p.7).
  Only the current session can be requested (TP p.3). Responses are ordinary segments.
- **Snapshot (SNAP):** buffer the multicast first, then send SnapshotRequest with Channel
  ID, Session ID and Minimum Sequence Number from the multicast header (0 = latest
  snapshot). The response is SnapshotStart, SnapshotData messages, SnapshotEnd with the
  Snapshot Sequence Number. Rebuild from the snapshot, then apply buffered messages with
  higher sequence numbers (SNAP p.4, p.7, p.11). Reject code R means no snapshot at or
  above the minimum yet (SNAP p.8).
- **Snapshot content** (SNAP p.12): Security Directory per IEX-listed symbol; latest
  System Event; per symbol the latest Trading Status, Security Event, Operational Halt
  Status, Short Sale Price Test Status, an Add Order for every resting order net of all
  modifications and executions, the latest Retail Liquidity Indicator. Add Orders are not
  in feed order; a modified order appears with the modification's timestamp (example,
  SNAP p.12).
- **Alignment:** messages with sequence number > Snapshot Sequence Number are applied
  (SNAP p.4); whether number N itself is in the snapshot is not stated (section 10).
- **Resets:** a new Session ID starts a new sequence; the previous session's data cannot be
  requested (TP p.3-4). Clear Book clears one symbol (DPLS p.27). System Event C is the
  last message of a session (DPLS p.13).

## 6. Semantics

- **Book type:** order by order, displayed orders only; non-displayed orders and the
  hidden part of reserve orders are absent (DPLS p.3). Full depth, no level limit.
- **Add Order** `a`: new displayed order; Order ID identifies it in later messages
  (DPLS p.21). An Order ID may be reused by a later Add Order once deleted (DPLS p.23).
- **Order Modify** `M`: new price and new total quoted size; Modify Flags bit 0 says
  whether priority is kept (1) or reset (0) (DPLS p.22). The priority rule is explicit
  per message, so it need not be inferred from the size or price change.
- **Order Delete** `R`: order removed, by user or system (DPLS p.23).
- **Order Executed** `L`: a displayed resting order executed; remove it when its remaining
  size reaches 0, otherwise it keeps its time priority with the size reduced. The
  execution price may differ from the order's booking price (price improvement or
  sliding) (DPLS p.24). Book and trade are one message: no separate book delta follows.
- **Trade** `T`: non-displayed against non-displayed; no displayed order changes; may be
  ignored for book state (DPLS p.25). Executed volume is L plus T (Appendix A, p.29,
  Volume Eligible).
- **Trade Break** `B`: an earlier L or T broken, by Trade ID (DPLS p.26). Book unchanged.
- **Clear Book** `C`: every order of one symbol removed (DPLS p.27).
- **Trade side:** an L names the resting order, so the resting side is its side; the
  aggressor is not reported. T and B carry no side.
- **Events:** messages with the same Timestamp, whatever their type or symbol, come from
  one event and happened atomically (DPLS p.5). DEEP+ has no event-complete flag (DEEP's
  Event Flags, DEEP p.17, is absent).
- **Trading states:** Trading Status H halted, O order acceptance period (IEX-listed),
  P paused and order acceptance (IEX-listed), T trading (DPLS p.15-16); Operational Halt
  Status O/N (DPLS p.18); a symbol missing from the pre-market spin of either is treated
  as operationally halted (DPLS p.15, p.18). Security Event O opening complete, C closing
  complete (DPLS p.20). Short sale price test (DPLS p.19). Whether the book can be
  crossed in any state is not stated.
- **Implied prices, overlays:** none. Hidden quantity: not shown (DPLS p.3).
- **Routed executions:** not reported (DPLS p.3).

## 7. Mapping to Kaspar

Records are `bfile::l3_t` (`chutil/include/bfile/r_l3.hpp`), emitted as
`mdp3::handler_if` emits them (`mdp3/include/mdp3/handler_if.hpp` MBO path, lines
655-800; trade, 860-925; status, 1075-1130; gap, 1130-1160; reset, 1160-1195; end of
packet, 1260-1280). `orderUpdateAction` 0 new, 1 change, 2 delete; `side` '0' bid,
'1' ask; `pxd` the price as a double, converted to ticks by OB (`frame_kaspr/src/OB.cpp`
~1741); `transactTime` the message Timestamp; `sendingTime` the segment Send Time.
`venue` needs a new `en::x` code (none exists for IEX in
`chutil/gencode/enum/def/exch_code.enum`; codes are appended at the end).

| Venue message | Kaspar record or message | Notes |
|---|---|---|
| IEX-TP segment end | `l3_eop_t` (msgSeqNum, sendingTime) | One per segment, to the recorder. msgSeqNum is 32-bit; IEX sequence numbers are 64-bit (section 10). |
| sequence gap (section 5) | `l3_gap_v2_t` to every book on the channel | As `handler_if::Gap`. Then gap fill or SNAP. |
| new Session ID; SNAP rebuild start | `l3_chr_v2_t` to every book on the channel; clear the handler's order map | As `handler_if::ChannelReset` (#122). Snapshot orders follow as recovery records. |
| System Event `S` | none, or `l3_sst_t` per book | O/S/R/M/E/C are market-wide; no Kaspar record for them is required by OB. C ends the session. |
| Security Directory `D` | instrument loader, not a record | Round lot, LULD tier, test/ETP/when-issued flags; IEX-listed securities only (section 8). |
| Trading Status `H`, Operational Halt `O` | `l3_sst_t` (securityID from Symbol) | OB matches only when `tradingStatus == 17` (`OB.hpp:456`). Map T and not operationally halted to 17; H, O, P or operational halt O to a non-matching value; keep the IEX code in `haltReason`/`tradingEvent`. |
| Short Sale Price Test `P`, Retail Liquidity `I`, Security Event `E` | none (or `l3_sst_t` `tradingEvent` for E) | No book effect. |
| Add Order `a` | `l3_mbo_v2_t` action 0 | side '8'→'0', '5'→'1'; pxd = Price/10000; displayQty = Size; orderID = Order ID; priority assigned by the handler (see Modify). Record orderID → securityID and the order's side, price, size. |
| Order Modify `M`, bit 0 = 1 (maintain) | `l3_mbo_v2_t` action 1, same priority | OB treats a Change with a larger size or a new price as cancel and add (`OB.cpp` ~2430), losing priority; IEX may say maintain in those cases (section 10). |
| Order Modify `M`, bit 0 = 0 (reset) | `l3_mbo_v2_t` action 2 then action 0, new priority | OB keeps priority on a size-down Change at the same price, so a reset must be sent as delete and add to put the order at the back. Reading in section 10. |
| Order Delete `R` | `l3_mbo_v2_t` action 2 | Erase the order from the map. |
| Order Executed `L` | `l3_mbo_trd_v2_t` (orderID, lastQty = Size), then `l3_mbo_v2_t` action 1 with the remaining size, or action 2 at 0 | Trade before book record, the order CME uses and OB/TachBook's `FillSlate` expects (`frame_kaspr/include/frame/ob/FillSlate.hpp`). The trade record has no price; OB prices it from the book, i.e. the booking price, not the execution price (section 10). |
| Trade `T` | `l3_mbp_trd_t` (securityID, pxd, sz) or volume only | No order ID, so `l3_mbo_trd_v2_t` cannot route it (`handler_if` routes trades by order ID). OB handles `l3_mbp_trd_t` at `OB.cpp` ~1884; what it does with it was not traced. |
| Trade Break `B` | none | No Kaspar record for a break. Log it. |
| Clear Book `C` | `l3_mbo_v2_t` action 2 for every order of the symbol in the handler's map | `l3_chr_v2_t` has no securityID and clears the whole channel (section 10). |
| SNAP Add Order | `l3_mbo_v2_t` action 0, `recovery = true` | As `handler_if::SnapshotFullRefreshOrderBook`. Queue order within a level from the snapshot (section 10). |
| SNAP admin messages | as their live counterparts | |
| events | `endOfEvent` on the last record of a run with one Timestamp | DEEP+ has no event flag (section 10). |

## 8. Instruments

- **Identifier:** Symbol, 8 characters, Nasdaq Integrated symbology, space padded
  (DPLS p.14). No numeric instrument ID; the handler maps Symbol to Kaspar `securityID`
  (int32) from the universe.
- **Definitions:** Security Directory messages are sent for IEX-listed securities only
  (DPLS p.14; SNAP p.12). Every other symbol appears only through Trading Status,
  Operational Halt, Short Sale and order messages; its definition (round lot, tick) comes
  from outside the feed.
- **Channel assignment:** by first letter of the symbol, channels 1-8 (DPLS p.9).
- **Price:** signed fixed point, 4 decimals (DPLS p.5): pxd = Price / 10000.
- **Tick:** not given in any of the documents; from the instrument source.
- **Quantity:** shares, 4-byte unsigned (Size). Round lot per security from the directory
  (DPLS p.14); odd-lot trades are flagged 0x20 (Appendix A, p.28-29). Whether odd-lot
  orders appear in the book is not stated.
- **Lifecycle:** test securities are flagged 0x80 in the directory (DPLS p.28); ZIEXT in
  every example is a test symbol. When-issued 0x40.

## 9. Pitfalls

**1. Reading the specification**

- Several documents: DEEP+ 1.06, SNAP 1.07, IEX-TP 1.25 apply; DEEP 1.08 is a different
  protocol (0x8004) used only for comparison (section 1).
- Errata and change tables: none beyond revision histories; DPLS 1.01 changed the Add
  Order type byte (p.47), recorded in section 10.
- Tables across page breaks: Trading Status Reason (DPLS p.15-16) spans a page; rows
  checked against Appendix B (p.32) and `pdftotext`; no row lost.
- Offsets and length fields: DEEP+ offsets 0-based from Message Type; IEX-TP Message
  Length excludes itself (TP p.7). SNAP offsets include the length field (handled, section
  3).
- Reserved fields: TP header byte 1, Order Delete and Clear Book byte 1 kept in the table.
- Enumerations elsewhere: flags in Appendix A (DPLS p.28-29), copied into the field
  descriptions.
- Names differing between table and prose: SNAP fields `ChannelID`, `SessionID` against
  TP `Channel ID`; Order Modify "Modify Order" in the revision table (p.47). No effect.
- Examples contradicting tables: examples are consistent with the field tables (the decode
  test uses every one), but the 2017 example timestamps encode Eastern time as UTC
  (section 10); the SNAP SnapshotData example shows an extra length (section 10).
- Two layers in one table: IEX-TP, DEEP+ and SNAP are separate sections; SNAP data
  carries IEX-TP and DEEP+ inside it. Type codes overlap across layers (DEEP+ `R` delete,
  SNAP reject code R) but not within a section.
- Repeated names with different codes: none in DEEP+ (DEEP's `T` Trade Report and DEEP+'s
  `T` Trade share a code and differ in meaning; different protocol IDs).
- Variable-length trailing field: SnapshotData's message data, declared as a tail.
- Odd integer widths: none.

**2. Framing and encoding**

- Endianness: little-endian everywhere, including SNAP.
- Signed/unsigned: Long, Price and Timestamp signed; Integer, Short, Byte unsigned
  (DPLS p.5). Two's complement assumed (not stated).
- Fixed-point prices: global, 4 decimals.
- Null and sentinel values: none defined. Space (0x20) is a value of Retail Liquidity
  Indicator and Short Sale Detail; Reason is blank (spaces) for P and T.
- Strings: left justified, space padded, ASCII.
- Several messages per segment: yes, counted by Message Count; a message is never split
  across segments (blocks lie inside the payload, TP p.6).
- Timestamps: ns since POSIX epoch, UTC (DPLS p.5); several examples contradict this
  (section 10).

**3. Sequencing and recovery**

- Scope: per (channel, session) (section 5).
- A/B arbitration: reuse the existing arbitration by sequence number; A and B may segment
  differently (TP p.3), so arbitration must work per message, not per segment.
- Gap rule: section 5; a duplicate segment is the other feed or a retransmission.
- Recovery: IEX-TP gap fill (UDP ≤ 1,000 messages, TCP unlimited) and SNAP.
- Snapshot alignment: apply buffered messages with sequence > Snapshot Sequence Number
  (SNAP p.4); inclusive bound not stated (section 10).
- Snapshot too old: request with Minimum Sequence Number; reject R means not yet
  available; quota 1,000 per channel per day bounds retries (SNAP p.5, p.8).
- State to clear on reset: books of the channel, the order-ID map (Order ID → symbol,
  side, price, size), per-symbol status; on Clear Book, that symbol's orders in the map.
- Recovery paths: to be exercised in replay by injected gaps, duplicates, session changes
  and Clear Book.
- Session behaviour: no logon on multicast; TCP gap fill and SNAP connect per request;
  heartbeats every second on multicast only; SNAP has none and times out at 10 s.

**4. Book semantics**

- MBO (order by order), not price level.
- Level-index updates: not applicable.
- Depth-limited books: not applicable; full displayed depth.
- Queue priority on modify: explicit per message (Modify Flags bit 0). Kaspar OB infers it
  from size and price instead; mapping in section 7, ambiguity in section 10.
- Implied, overlay, iceberg: displayed part of reserve orders only; hidden part absent.
- Trades also as book deltas: no. L both reduces the order and reports the trade; T and B
  do not touch the book. The handler must not also apply a delete for an L.
- Trade side: L is the resting order's side; T and B have none.
- Trading states: section 6; crossed-book legality not stated.
- Book clears: Clear Book per symbol; new session; SNAP rebuild.

**5. Prices, quantities and instruments**

- Price or yield: price only (equities).
- Quantity multipliers: none; shares.
- Decimals sentinels: none.
- Price-dependent ticks: not in the documents; from the instrument source.
- Instrument definitions: Security Directory for IEX-listed only; Symbol → securityID map
  from the universe (section 8).
- Lifecycle: test, when-issued, ETP flags in the directory; symbol reuse across days
  possible, Order ID reuse within a day allowed (DPLS p.23).

**6. Behaviour the specification omits**

- Subscription options: none; multicast.
- History on subscribe: none on multicast; SNAP and gap fill on request.
- Snapshot is the whole book: yes, all resting displayed orders for all symbols of the
  channel (SNAP p.12).
- Order within an event: not stated; several messages share a Timestamp (DPLS p.5).
- Connection limits: SNAP 10 s idle timeout (SNAP p.4); TCP gap fill must send its request
  at once (TP p.4).

No sample capture could be checked for any of these: the sample pcaps linked from IEX's
market-data page all fail (GCS 403/401, Azure 404, checked 2026-11-18).

**7. Post-trade** — not applicable: DEEP+ is market data only (DPLS p.3).

- FIX version: not applicable.
- Venue dictionary: not applicable.
- Credentials in Logon: not applicable (SNAP's token is in SnapshotRequest).
- Missing-field Reject: not applicable.
- Requests before session open: not applicable.
- Trade capture subscription: not applicable.
- Report lifecycle: not applicable; Trade Break is the only correction message.
- Duplicates on reconnect: not applicable.
- Fixed-income fields: not applicable.
- Daily sequence reset: not applicable.

**8. Order entry** — not applicable: DEEP+ cannot enter orders (DPLS p.3).

- Client ids: not applicable.
- Cancel and replace ids: not applicable.
- Fill races: not applicable.
- Unsolicited cancels: not applicable.
- Busts as fills: not applicable.
- Throttling: not applicable.
- Reconnect duplicates: not applicable.
- Orders placed elsewhere: not applicable.
- Short sales: not applicable (the feed reports Rule 201 status only).

## 10. Ambiguities

| Question | Reading taken | Alternatives | Resolved by |
|---|---|---|---|
| Do the field tables match the wire? | As the tables say; every example decodes field by field. | — | Not resolved: no DEEP+ sample capture was reachable (IEX page links: GCS 403/401, Azure 404). A pcap reader exists (outside the repo) and needs one capture. |
| Add Order type byte. DPLS v1.01 (September 30, 2024) "updated message type byte value"; the old value is not given. | `a` (0x61) as in v1.06. | Captures from before October 2024 may carry another byte (decode reports `unknown`). | A capture from each period. |
| SnapshotData example (SNAP p.10): block length 33, then a further length 31 and a 31-byte Security Directory; Payload Length 35 and Message Length 76 agree with that. A normal IEX-TP block (TP p.6-7) has the DEEP+ type right after the block length. | The data after the block length is itself a length-prefixed DEEP+ message, as printed; decode it with the DEEP+ decoder (the test does). | The example is wrong and the block data is the bare DEEP+ message (block length 31). A handler can tell the two apart: the u16 at the data start equals block length − 2 in the first case. | SNAP capture or IEX. |
| SNAP alignment: is the message with sequence = Snapshot Sequence Number in the snapshot? (SNAP p.4: apply "higher sequence numbers"; p.11 "Sequence at which the Snapshot was created".) | In the snapshot: apply buffered messages with sequence > N. | N is the next message, not yet applied: apply ≥ N. | IEX, or a replay comparing a snapshot with the rebuilt book. |
| Sequence numbers in SnapshotData IEX-TP headers (SNAP p.10 example: 44130, equal to SnapshotEnd's). | Every SnapshotData header carries the snapshot's sequence number; they are not a stream and are not checked for continuity. | Each carries the original message's number. | SNAP capture. |
| Queue order within a price level from a snapshot. Add Orders "will not be in the original DEEP+ Feed Sequence or SendTime order" (SNAP p.12); a modified order carries the modification's Timestamp. | Priority within a level by Add Order Timestamp ascending (the time of the last priority reset), ties by message order. | Message order of the snapshot; or unknown, with snapshot orders queued arbitrarily. | IEX. Affects simulated queue position after every SNAP recovery. |
| Modify with Reset Priority (bit 0 = 0) at the same price and a smaller or equal size. Kaspar OB keeps priority on such a Change. | Emit Delete then New (new priority) for every reset; Change only for maintain. | Emit Change always and accept OB's inferred priority. | Decided here from the spec's explicit flag; check the flag distribution against size and price changes on a capture (the reader counts it). |
| Modify with Maintain Priority and a larger size or a new price. Kaspar OB turns such a Change into cancel and add. | Possible per the flag; OB will lose priority for it. Count how often it occurs. | IEX never sets maintain in these cases. | Capture (reader counts it). |
| Order Executed at a price other than the booking price (DPLS p.24). `l3_mbo_trd_v2_t` has no price; OB prices fills from the book. | Accept the booking price for the fill; log the difference. | Add a price to the trade record (a Kaspar change). | User. |
| Scope of Trade `T`: "a non-displayed order on the book that executed against another non-displayed order" (DPLS p.25). An incoming order hitting a resting non-displayed order, or the non-displayed part of a reserve order? | T reports every execution not against a displayed order; volume = L + T. | Some executions are in neither (only routed executions are excluded, DPLS p.3). | IEX. |
| DEEP messages absent from DEEP+: Auction Information `A`, Official Price `X` (DEEP p.22, p.25). SNAP v1.01 removed OfficialPrice from the snapshot content (SNAP p.13). | DEEP+ does not send them; if it does, `decode` returns `unknown` and they are skipped by length. | DEEP+ sends them unlisted. | Capture. |
| Example timestamps: the 2016 and 2022 examples are labelled in New York time (bytes are UTC = label + 4 h); the 2017 examples (17:00 End of System Hours, 09:30 Opening, DPLS p.13, p.14, p.20) encode the label as UTC. | Timestamps are UTC as the data types say (DPLS p.5); the 2017 examples are wrong. | — | Capture. |
| Event boundary for `endOfEvent`: no event flag in DEEP+; messages with one Timestamp are one event (DPLS p.5). An event may span segments. | Mark endOfEvent when the next message's Timestamp differs; at the end of a segment, mark it (the event is assumed not to span segments). | Hold the last record until the next segment arrives (adds latency). | Capture: count runs of one Timestamp across segment boundaries. |
| Clear Book is per symbol; `l3_chr_v2_t` has no securityID. | Emit a Delete for each order of the symbol held in the handler. | A per-instrument clear record (a Kaspar change). | User. |
| 64-bit sequence numbers against 32-bit `l3_eop_t.msgSeqNum` and `pkt_seq_num`. | Low 32 bits of First Message Sequence Number; the handler's own sequencing uses 64 bits. | Widen the records (format change). | User. |
| Order ID reuse: allowed after a delete (DPLS p.23). Scope across channels not stated. | Key the order map by (channel, Order ID); an Add with a live ID replaces the old order after logging. | IDs are unique across channels. | Capture (reader counts Adds with a live ID). |
| Trading Status values, DPLS p.15: the PDF runs the H and O bullets together ("markets r'O'"). | Two values, H and O, as in DEEP p.10. | — | Settled by the DEEP spec. |
| SNAP Message Length typed "Integer" (4 bytes) with length 2 (SNAP p.7-11); SnapshotData block length likewise. | 2-byte unsigned, as the length column, offsets and examples say. | — | Settled by the examples. |
| Timestamp Relationships (DPLS p.5) refer to "Price Level Update Messages", which DEEP+ does not have (copied from DEEP). | The rule applies to DEEP+ order messages: after a Security Event, all earlier book messages for the symbol have been sent. | — | IEX. |
| Session change mid-day; SystemEvent C. | A new Session ID resets the channel (CHR, clear maps). C does not clear books. | — | Capture. |
| Odd-lot orders in the book; tick sizes. | Not stated; tick from the instrument source; odd-lot orders accepted if they appear. | — | Capture, instrument source. |
