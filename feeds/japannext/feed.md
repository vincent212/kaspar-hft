# Feed description: Japannext PTS equities ITCH (MoldUDP64) and GLIMPSE

Every entry cites its source. Abbreviations used in citations:

- **ITCH**: ITCH Market Data Specification (Equities) 2.02
- **GLIMPSE**: GLIMPSE Market Data Specification (Equities) 2.01
- **MOLD**: MoldUDP64 Specification (All Markets) 1.02
- **SOUP**: SoupBinTCP Specification (All Markets) 1.01
- **RULES**: Trading Rules (Equities) 2.02
- **EVENTS**: Market Events Schedule 1.2
- **sample**: Japannext's public sample captures (section 1). Findings marked *sample* were measured on them with the generated headers. They were not taken from the documents.

## 1. Venue and documents

Japannext PTS (Japannext Co., Ltd., Tokyo) is a proprietary trading system for TSE-listed equities. Its markets are J-Market (daytime and nighttime sessions), X-Market and U-Market (ITCH 4.2).

| Document | Version | Date | Applies to |
|---|---|---|---|
| ITCH Market Data Specification (Equities), `JNX_ITCH_Market_Data_Specification_Equities_2.02.pdf` | 2.02 | 2026-07-02 | ITCH application messages. 2.00 to 2.02 state "no factual changes" to technical content (ITCH 5, p. 10). |
| GLIMPSE Market Data Specification (Equities), `JNX_GLIMPSE_Market_Data_Specification_Equities_2.01.pdf` | 2.01 | 2026-01-16 | Snapshot messages and the End of Snapshot message. This is the version the Japannext site lists, and no 2.02 exists (checked 2026-11-18: the site's document list, plus 404 for `..._Equities_2.02.pdf`, `JNX_GLIMPSE_Equities_2.02.pdf` and `..._2.03.pdf`). The copy supplied as `JNX_GLIMPSE_Equities_2.01.pdf` is byte-identical to the site's file. |
| MoldUDP64 Specification (All Markets), `JNX_MoldUDP64_Specification_1.02.pdf` | 1.02 | 2026-07-02 | Multicast framing, heartbeats, re-request. |
| SoupBinTCP Specification (All Markets), `JNX_SoupBinTCP_Specification_1.01.pdf` | 1.01 | 2026-01-16 | The TCP session layer of GLIMPSE and of ITCH over TCP. ITCH 2 and GLIMPSE 2 link to it, so it is used instead of Nasdaq SoupBinTCP 3.0. Differences from Nasdaq 3.0 are in section 3. |
| Trading Rules (Equities), `JNX_Trading_Rules_Equities_2.02_EN.pdf` | 2.02 | 2026-08-03 | Sessions, matching and tick tables. It is an English summary, and the Japanese text governs (RULES p. 1). |
| Market Events Schedule, `Japannext_Market_Events_Schedule.pdf` | 1.2 | 2024-05-31 | Session times. |
| Sample captures `Japannext_PTS_ITCH_Equities_v1.7.UDP.pcap`, `..._v1.7.TCP.pcap`, `Japannext_PTS_GLIMPSE_Equities_v1.4.pcap` | ITCH 1.7, GLIMPSE 1.4 | captured 2023-10-17/19 | Decoding checks. ITCH 1.7 (2023-02-17) is the last version with a layout change (Orderbook Id to Alpha), so the samples have the 2.02 layout (ITCH 5). |

All are public at `https://www.japannext.co.jp/pub_data/pub_onboarding/<file>`.

## 2. Transport

- **ITCH over MoldUDP64:** UDP multicast. Each downstream stream has its own multicast group (MOLD 2.6.1). Addresses and ports are not in the public documents. The sample stream is 232.66.1.2:11002 from 10.66.0.104 (*sample*).
- **Retransmission:** a Request Packet goes by UDP unicast to a re-request server, which answers by unicast to the requesting socket. The reply arrives on the multicast socket if the request was sent from it (MOLD 2.6.2, 4). The reply holds only the messages that fit in one UDP/IP packet, so the client must request the rest again (MOLD 4, p. 6).
- **A/B feeds:** none in the public documents (section 10).
- **Heartbeats:**
  - MoldUDP64 sends a heartbeat (Message Count 0) after more than 1 s without data (MOLD 3.3).
  - End of Session packets (Message Count 0xFFFF) replace heartbeats at the end, once a second. Re-requests remain possible while they are sent (MOLD 3.4, p. 5-6).
- **ITCH over SoupBinTCP:** the alternative transport (ITCH 2). The sample uses server port 15001 (*sample*).
- **GLIMPSE over SoupBinTCP** (GLIMPSE 2). The sample uses server port 15002 (*sample*).
- **SoupBinTCP logon** (SOUP 4.1, 5.3.1, Table 1):
  - The Login Request must arrive within about 30 s of connecting.
  - The username is tied to one TCP port, and a mismatch is rejected with code `A`.
- **GLIMPSE logon** (GLIMPSE 4):
  - The Requested Session must be empty. Any other value is rejected.
  - The sample client sent a blank session and sequence `0`. The server answered with Login Accepted, session blank, next sequence `1` (*sample*).
- **SoupBinTCP heartbeats:** both sides send one after more than 1 s without data. The connection is considered down after 15 s without packets (SOUP 4.3).
- **Throttles:** none stated.

## 3. Framing

- **Byte order:**
  - Every integer is unsigned big-endian (ITCH 3, GLIMPSE 3, MOLD 2.4, SOUP 3).
  - MOLD says that big-endian "does not apply to data in the Message Data field" (MOLD 2.4 footnote). That data is ITCH, which is itself big-endian.
- **MoldUDP64 packet** (MOLD 3.1-3.2):
  - A 20-byte header: Session (10), Sequence Number (8), Message Count (2).
  - Then Message Count Message Blocks, each a 2-byte Message Length followed by that many bytes.
  - The length excludes itself ("Length in bytes of Message Data"). A block may be empty.
  - Messages are up to 64 KB (MOLD 2.3), and packets carry whole messages only (MOLD 3).
- **ITCH messages:**
  - They have no length field of their own. The type code is at offset 0 (ITCH 4.x).
  - Over MoldUDP64 the block length frames them. Over SoupBinTCP, one ITCH message is the payload of one Sequenced Data Packet (SOUP 5.2.3).
  - `itch.tbl` therefore has no `@length_prefix`. `moldudp64.tbl` has `MessageBlock`.
- **SoupBinTCP packet** (SOUP 3, 5.x):
  - A 2-byte big-endian length that excludes itself, then the packet type, then the payload.
  - The JNX tables put Packet Length at offset 0 and Packet Type at offset 2. `glimpse.tbl` declares the length as `@length_prefix 2 exclusive` and subtracts 2 from every offset.
- **Differences from Nasdaq SoupBinTCP 3.0:**
  - JNX states that the Requested Session in the Login Request is left-padded with spaces (SOUP 5.3.1). Nasdaq leaves this unstated.
  - JNX states a 15 s timeout (SOUP 4.3).
  - JNX defines Login Rejected `A` to include a username/port mismatch (SOUP Table 1, footnote 1).
  - Packet types and lengths are the same.
- **Alignment:** none. Messages are packed.
- **Text:**
  - Alpha fields are left-justified and space-padded (ITCH 3).
  - SoupBinTCP Session and the Numeric sequence numbers are left-padded with spaces (SOUP 5.2.1, 5.3.1).
- **Message totals:** the specifications do not state them. Each `@size` is the last row's offset plus its length. MoldUDP64 states its 20-byte header (MOLD 3.1).

## 4. Message catalogue

Generated by `fieldgen.py --markdown` from `itch.tbl`, `moldudp64.tbl` and `glimpse.tbl` (in that order). Sizes exclude the SoupBinTCP length prefix, which is shown separately.

| Table | Section | Messages |
|---|---|---|
| itch.tbl | `itch::from_jnx` | 11 |
| moldudp64.tbl | `moldudp64::from_jnx` | 2 (untyped: header, message block) |
| moldudp64.tbl | `moldudp64::to_jnx` | 1 (untyped: request packet) |
| glimpse.tbl | `soup::from_jnx` | 6 |
| glimpse.tbl | `soup::to_jnx` | 5 |
| glimpse.tbl | `glimpse::from_jnx` | 9 |

### itch::from_jnx

#### TimestampSecondsMessage (type `T`, 5 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | TimestampSecondsMessage: seconds part of the timestamp, sent for every second in which another message is sent (JNX ITCH 2.02, 4.1, p. 3-4) |
| TimestampSeconds | 1 | 4 | Integer | Seconds past midnight of the day the trading session started |

#### SystemEventMessage (type `S`, 10 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | SystemEventMessage: data feed, system and market events (4.2, p. 4) |
| TimestampNanoseconds | 1 | 4 | Integer | Nanoseconds since the last Timestamp - Seconds Message |
| Group | 5 | 4 | Alpha | Order book group: DAY J-Market daytime, NGHT J-Market nighttime, DAYX X-Market, DAYU U-Market; blank if system-wide |
| SystemEvent | 9 | 1 | Alpha | O start of messages, S start of system hours, Q start of market hours, M end of market hours, E end of system hours, C end of messages (Table 1) |

#### PriceTickSizeMessage (type `L`, 17 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | PriceTickSizeMessage: one row of a price tick size table; sent before Orderbook Directory Messages (4.3, p. 4-5) |
| TimestampNanoseconds | 1 | 4 | Integer | Nanoseconds since the last Timestamp - Seconds Message |
| PriceTickSizeTableId | 5 | 4 | Integer | Price tick size table identifier |
| PriceTickSize | 9 | 4 | Integer | Price tick size |
| PriceStart | 13 | 4 | Integer | Start of the price range for this tick size |

#### OrderbookDirectoryMessage (type `R`, 45 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | OrderbookDirectoryMessage: an order book available for trading; sent at the start of each trading day (4.4, p. 5) |
| TimestampNanoseconds | 1 | 4 | Integer | Nanoseconds since the last Timestamp - Seconds Message |
| OrderbookId | 5 | 4 | Alpha | Securities Identification Code Committee (SICC) code |
| OrderbookCode | 9 | 12 | Alpha | ISIN |
| Group | 21 | 4 | Alpha | Order book group: DAY, NGHT, DAYX, DAYU |
| RoundLotSize | 25 | 4 | Integer | Shares in a round lot |
| PriceTickSizeTableId | 29 | 4 | Integer | Price tick size table identifier |
| PriceDecimals | 33 | 4 | Integer | Decimal places in price fields; value is 1 |
| UpperPriceLimit | 37 | 4 | Integer | Maximum tradable price |
| LowerPriceLimit | 41 | 4 | Integer | Minimum tradable price |

#### TradingStateMessage (type `H`, 14 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | TradingStateMessage: trading state of an order book; spin before start of system hours, then changes (4.5, p. 6) |
| TimestampNanoseconds | 1 | 4 | Integer | Nanoseconds since the last Timestamp - Seconds Message |
| OrderbookId | 5 | 4 | Alpha | SICC code |
| Group | 9 | 4 | Alpha | Order book group: DAY, NGHT, DAYX, DAYU |
| TradingState | 13 | 1 | Alpha | T trading, V suspended |

#### ShortSellingPriceRestrictionStateMessage (type `Y`, 14 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | ShortSellingPriceRestrictionStateMessage: short selling price restriction state of an order book (4.6, p. 6-7) |
| TimestampNanoseconds | 1 | 4 | Integer | Nanoseconds since the last Timestamp - Seconds Message |
| OrderbookId | 5 | 4 | Alpha | SICC code |
| Group | 9 | 4 | Alpha | Order book group: DAY, NGHT, DAYX, DAYU |
| ShortSellingState | 13 | 1 | Alpha | 0 no price restriction, 1 price restriction in effect |

#### OrderAddedMessage (type `A`, 30 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | OrderAddedMessage: Order Added Message - No Attributes; an order added to the displayable book, or a reference price when Order Number is 0 (4.7.1, p. 7) |
| TimestampNanoseconds | 1 | 4 | Integer | Nanoseconds since the last Timestamp - Seconds Message |
| OrderNumber | 5 | 8 | Integer | Order number, unique per day per order book group; 0 is a reference price message |
| BuySellIndicator | 13 | 1 | Alpha | B buy, S sell; ignore if reference price message |
| Quantity | 14 | 4 | Integer | Shares added to the book; ignore if reference price message |
| OrderbookId | 18 | 4 | Alpha | SICC code |
| Group | 22 | 4 | Alpha | Order book group: DAY, NGHT, DAYX, DAYU |
| Price | 26 | 4 | Integer | Order price, 1 decimal place; reference price message: 0x7FFFFFFF means none available |

#### OrderAddedWithAttributesMessage (type `F`, 35 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | OrderAddedWithAttributesMessage: an order with market-specific attributes added to the displayable book (4.7.2, p. 8) |
| TimestampNanoseconds | 1 | 4 | Integer | Nanoseconds since the last Timestamp - Seconds Message |
| OrderNumber | 5 | 8 | Integer | Order number, unique per day per order book group |
| BuySellIndicator | 13 | 1 | Alpha | B buy, S sell |
| Quantity | 14 | 4 | Integer | Shares added to the book |
| OrderbookId | 18 | 4 | Alpha | SICC code |
| Group | 22 | 4 | Alpha | Order book group: DAY, NGHT, DAYX, DAYU |
| Price | 26 | 4 | Integer | Order price, 1 decimal place |
| Attribution | 30 | 4 | Alpha | Reserved, always blank |
| OrderType | 34 | 1 | Alpha | Q DLP order |

#### OrderExecutedMessage (type `E`, 25 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | OrderExecutedMessage: a resting order fully or partly executed; price is the resting order's (4.8, p. 8) |
| TimestampNanoseconds | 1 | 4 | Integer | Nanoseconds since the last Timestamp - Seconds Message |
| OrderNumber | 5 | 8 | Integer | Order executed |
| ExecutedQuantity | 13 | 4 | Integer | Shares executed |
| MatchNumber | 17 | 8 | Integer | Match number, unique per day per order book group |

#### OrderDeletedMessage (type `D`, 13 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | OrderDeletedMessage: an order cancelled; remove it from the book (4.9, p. 9) |
| TimestampNanoseconds | 1 | 4 | Integer | Nanoseconds since the last Timestamp - Seconds Message |
| OrderNumber | 5 | 8 | Integer | Order cancelled |

#### OrderReplacedMessage (type `U`, 29 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | OrderReplacedMessage: an order replaced by one with a new order number (4.10, p. 9) |
| TimestampNanoseconds | 1 | 4 | Integer | Nanoseconds since the last Timestamp - Seconds Message |
| OriginalOrderNumber | 5 | 8 | Integer | Order replaced |
| NewOrderNumber | 13 | 8 | Integer | Order number of the replacement |
| Quantity | 21 | 4 | Integer | New displayed quantity |
| Price | 25 | 4 | Integer | New price |

### moldudp64::from_jnx

#### DownstreamPacketHeader (type none, 20 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| Session | 0 | 10 | ANUM | Session the packet belongs to |
| SequenceNumber | 10 | 8 | NUM | Sequence number of the first message in the packet; later messages are numbered implicitly |
| MessageCount | 18 | 2 | NUM | Messages in the packet; 0 heartbeat, 0xFFFF end of session (sequence number is then the next expected) |

#### MessageBlock (type none, 2 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageLength | 0 | 2 | NUM | Length in bytes of Message Data (excludes this field) |
| MessageData | 2 | var | Data | Application message (ITCH); may be zero length |

### moldudp64::to_jnx

#### RequestPacket (type none, 20 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| Session | 0 | 10 | ANUM | Session the packet belongs to |
| SequenceNumber | 10 | 8 | NUM | Sequence number of the first message requested |
| RequestedMessageCount | 18 | 2 | NUM | Messages requested; the reply holds only what fits in one UDP/IP packet |

### soup::from_jnx

#### DebugPacket (type `+`, 1 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | DebugPacket: human-readable text; either side may send it at any time (JNX SoupBinTCP 1.01, 5.1, p. 5) |
| Text | 1 | var | Payload | Human-readable text |

#### LoginAcceptedPacket (type `A`, 31 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | LoginAcceptedPacket: login succeeded; first non-debug packet after it (5.2.1, p. 5) |
| Session | 1 | 10 | Session (right-aligned) | Session ID of the current session, left-padded with spaces |
| SequenceNumber | 11 | 20 | Numeric (right-aligned) | Sequence number (ASCII) of the next Sequenced Message, left-padded with spaces |

#### LoginRejectedPacket (type `J`, 2 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | LoginRejectedPacket: login failed; the server then closes the socket (5.2.2, p. 5-6) |
| RejectReasonCode | 1 | 1 | Alpha | A not authorized (username, password, or username and port do not match), S session not available (Table 1) |

#### SequencedDataPacket (type `S`, 1 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | SequencedDataPacket: one sequenced message of the higher-level protocol (5.2.3, p. 6) |
| Message | 1 | var | Payload | GLIMPSE or ITCH message |

#### ServerHeartbeatPacket (type `H`, 1 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | ServerHeartbeatPacket: sent after more than 1 second without other data (5.2.4, p. 6) |

#### EndOfSessionPacket (type `Z`, 1 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | EndOfSessionPacket: no more messages; the connection is then closed (5.2.5, p. 6) |

### soup::to_jnx

#### DebugPacket (type `+`, 1 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | DebugPacket: human-readable text (5.1, p. 5) |
| Text | 1 | var | Payload | Human-readable text |

#### LoginRequestPacket (type `L`, 47 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | LoginRequestPacket: first packet after connecting; the server may drop the connection after about 30 seconds without it (5.3.1, p. 7) |
| Username | 1 | 6 | Alphanumeric | Case-sensitive, right-padded with spaces |
| Password | 7 | 10 | Alphanumeric | Case-sensitive, right-padded with spaces |
| RequestedSession | 17 | 10 | Session (right-aligned) | Session to log in to, blank for the current one, left-padded with spaces; GLIMPSE requires it blank (JNX GLIMPSE 2.01, 4) |
| RequestedSequenceNumber | 27 | 20 | Numeric (right-aligned) | Sequence number (ASCII) of the next message wanted, 0 for the most recent, left-padded with spaces |

#### UnsequencedDataPacket (type `U`, 1 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | UnsequencedDataPacket: one client message of the higher-level protocol (5.3.2, p. 7) |
| Message | 1 | var | Payload | Higher-level protocol message |

#### ClientHeartbeatPacket (type `R`, 1 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | ClientHeartbeatPacket: sent after more than 1 second without other data (5.3.3, p. 7) |

#### LogoutRequestPacket (type `O`, 1 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | LogoutRequestPacket: the server then closes the connection (5.3.4, p. 8) |

### glimpse::from_jnx

#### TimestampSecondsMessage (type `T`, 5 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | TimestampSecondsMessage: seconds part of the timestamp (JNX GLIMPSE 2.01, 5.1, p. 4) |
| TimestampSeconds | 1 | 4 | Integer | Seconds past midnight of the day the trading session started |

#### SystemEventMessage (type `S`, 10 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | SystemEventMessage: only the Start of Messages and End of Messages events that occurred before the snapshot (5.2, p. 4) |
| TimestampNanoseconds | 1 | 4 | Integer | Nanoseconds since the last Timestamp - Seconds Message |
| Group | 5 | 4 | Alpha | Order book group: DAY, NGHT, DAYX, DAYU; blank if system-wide |
| SystemEvent | 9 | 1 | Alpha | O start of messages, C end of messages (Table 1) |

#### PriceTickSizeMessage (type `L`, 17 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | PriceTickSizeMessage: one row of a price tick size table; sent before Orderbook Directory Messages (5.3, p. 5) |
| TimestampNanoseconds | 1 | 4 | Integer | Nanoseconds since the last Timestamp - Seconds Message |
| PriceTickSizeTableId | 5 | 4 | Integer | Price tick size table identifier |
| PriceTickSize | 9 | 4 | Integer | Price tick size |
| PriceStart | 13 | 4 | Integer | Start of the price range for this tick size |

#### OrderbookDirectoryMessage (type `R`, 45 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | OrderbookDirectoryMessage: an order book available for trading (5.4, p. 5) |
| TimestampNanoseconds | 1 | 4 | Integer | Nanoseconds since the last Timestamp - Seconds Message |
| OrderbookId | 5 | 4 | Alpha | SICC code |
| OrderbookCode | 9 | 12 | Alpha | ISIN |
| Group | 21 | 4 | Alpha | Order book group: DAY, NGHT, DAYX, DAYU |
| RoundLotSize | 25 | 4 | Integer | Shares in a round lot |
| PriceTickSizeTableId | 29 | 4 | Integer | Price tick size table identifier |
| PriceDecimals | 33 | 4 | Integer | Decimal places in price fields; value is 1 |
| UpperPriceLimit | 37 | 4 | Integer | Maximum tradable price |
| LowerPriceLimit | 41 | 4 | Integer | Minimum tradable price |

#### TradingStateMessage (type `H`, 14 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | TradingStateMessage: sent for every order book eligible for trading at the snapshot; a book without one is suspended (5.5, p. 6) |
| TimestampNanoseconds | 1 | 4 | Integer | Nanoseconds since the last Timestamp - Seconds Message |
| OrderbookId | 5 | 4 | Alpha | SICC code |
| Group | 9 | 4 | Alpha | Order book group: DAY, NGHT, DAYX, DAYU |
| TradingState | 13 | 1 | Alpha | T trading, V suspended |

#### ShortSellingPriceRestrictionStateMessage (type `Y`, 14 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | ShortSellingPriceRestrictionStateMessage: sent for every order book with a restriction in effect at the snapshot (5.6, p. 6) |
| TimestampNanoseconds | 1 | 4 | Integer | Nanoseconds since the last Timestamp - Seconds Message |
| OrderbookId | 5 | 4 | Alpha | SICC code |
| Group | 9 | 4 | Alpha | Order book group: DAY, NGHT, DAYX, DAYU |
| ShortSellingState | 13 | 1 | Alpha | 0 no price restriction, 1 price restriction in effect |

#### OrderAddedMessage (type `A`, 30 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | OrderAddedMessage: Order Added Message - No Attributes; an order present in the displayable book, or a reference price when Order Number is 0 (5.7.1, p. 7) |
| TimestampNanoseconds | 1 | 4 | Integer | Nanoseconds since the last Timestamp - Seconds Message |
| OrderNumber | 5 | 8 | Integer | Order number, unique per day per order book group; 0 is a reference price message |
| BuySellIndicator | 13 | 1 | Alpha | B buy, S sell; ignore if reference price message |
| Quantity | 14 | 4 | Integer | Shares; ignore if reference price message |
| OrderbookId | 18 | 4 | Alpha | SICC code |
| Group | 22 | 4 | Alpha | Order book group: DAY, NGHT, DAYX, DAYU |
| Price | 26 | 4 | Integer | Order price, 1 decimal place; reference price message: 0x7FFFFFFF means none available |

#### OrderAddedWithAttributesMessage (type `F`, 35 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | OrderAddedWithAttributesMessage: an order with market-specific attributes present in the displayable book (5.7.2, p. 8) |
| TimestampNanoseconds | 1 | 4 | Integer | Nanoseconds since the last Timestamp - Seconds Message |
| OrderNumber | 5 | 8 | Integer | Order number, unique per day per order book group |
| BuySellIndicator | 13 | 1 | Alpha | B buy, S sell |
| Quantity | 14 | 4 | Integer | Shares |
| OrderbookId | 18 | 4 | Alpha | SICC code |
| Group | 22 | 4 | Alpha | Order book group: DAY, NGHT, DAYX, DAYU |
| Price | 26 | 4 | Integer | Order price, 1 decimal place |
| Attribution | 30 | 4 | Alpha | Reserved, always blank |
| OrderType | 34 | 1 | Alpha | Q DLP order |

#### EndOfSnapshotMessage (type `G`, 9 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | EndOfSnapshotMessage: last message of the snapshot; the ITCH sequence number to resume the real-time feed from (5.8, p. 8) |
| SequenceNumber | 1 | 8 | Integer | ITCH market data feed sequence number |

## 5. Sequencing and recovery

- **Scope:** each MoldUDP64 stream has a Session (10 bytes) and a sequence number per message. The header carries the number of the first message, and later messages are numbered implicitly (MOLD 3.1). The sample stream's session is `1697659284`, and every packet in the capture carries it (*sample*).
- **Gap rule** (MOLD 2.6.3):
  - The session differs from the expected one: abort and report.
  - The first sequence number is above the expected one: request the missing messages and wait.
  - Heartbeats and End of Session packets carry the next expected number, so they also reveal a gap (MOLD 3.3, 3.4).
  - A packet whose first number is below the expected one is old or duplicate. Messages in it with numbers at or above the expected one are new.
- **Recovery mechanisms:**
  1. MoldUDP64 re-request (MOLD 4).
  2. A GLIMPSE snapshot (GLIMPSE 4, 5.8).
  3. ITCH over SoupBinTCP, logging in with Requested Session and Requested Sequence Number to resume (SOUP 4.2). Whether it replays from sequence 1 is not stated.
- **Snapshot alignment:**
  - End of Snapshot `G` gives "the next sequence number of the real-time ITCH market data feed at the time the snapshot was taken". The consumer "should begin to process the real-time feed from the sequence number provided" (GLIMPSE 4, 5.8).
  - Rule: buffer the multicast stream while the snapshot loads. Then drop buffered messages numbered below `G.SequenceNumber` and apply those at or above it.
  - If the buffer starts above `G.SequenceNumber`, re-request the missing range by MoldUDP64. If that is no longer possible, take a new snapshot.
  - `G` names no MoldUDP64 session (section 10).
- **Resets:**
  - A new MoldUDP64 session (a new trading day) starts numbering anew.
  - End of Session (count 0xFFFF) ends the current session (MOLD 3.4).
  - On either, and before applying a snapshot, emit a channel reset and clear all per-channel state: books, the order-number map (book, side, price, remaining quantity per order), the current Timestamp - Seconds, the tick tables, the directory and the trading and short-sell states.
- **Snapshot contents** (GLIMPSE 5.1-5.8):
  - Timestamp - Seconds.
  - System Events O and C only, up to the snapshot.
  - Price Tick Size, Orderbook Directory, Trading State for every tradable book, Short Selling Price Restriction for books with one in effect.
  - Order Added (including reference prices).
  - End of Snapshot.
  - There are no executions, deletes or replaces.
- **The GLIMPSE sample's snapshot is truncated** (*sample*):
  - It holds 8,688 sequenced packets: T 1, S 1 (`O`), L 13, R 4,182, H 4,181, Y 15, A 295 (all reference prices).
  - The client then reset the TCP connection (zero window, then RST) one byte into the next packet.
  - The sample therefore has no resting orders and no End of Snapshot.

## 6. Semantics

- **Book type:** MBO, every displayable order (ITCH 2).
- **Matching:** continuous with price-time priority. There are no opening or closing auctions (RULES 6).
- **Order Added (`A`, `F`):**
  - Adds an order with a new Order Number. Order Numbers are unique per day per order book group (ITCH 4.7).
  - `A` with Order Number 0 is a reference price, not an order. Side and Quantity are to be ignored. Price 0x7FFFFFFF means no reference price (ITCH 4.7.1).
  - Initial reference prices follow the directory, and a manual update sends another (ITCH 4.4, 4.7.1). In the sample's reference prices, side is a space and quantity is 0 (*sample*).
- **Order Executed (`E`):**
  - Executes part or all of a resting order at the resting order's price. The message carries no price (ITCH 4.8).
  - Executed Quantity is the shares executed by this message. There are several messages for one order when it fills in parts.
  - The specification's sentence "Order Executed Messages for the same order are cumulative" is read as "they add up" (section 10). *Sample:* 1,044 partial executions; 100 orders completed exactly by the sum of their executions; 0 executions larger than the remaining quantity.
  - A fully executed order leaves the book with no Order Deleted. *Sample:* 66,858 full executions, none followed by `D` for that order.
- **Order Deleted (`D`):** removes the remaining shares (ITCH 4.9).
- **Order Replaced (`U`):**
  - The original order is replaced by one with a New Order Number, Quantity and Price (ITCH 4.10).
  - Side, order book and group are not in the message. They are taken from the original order.
  - Priority after a replace is not stated (section 10). *Sample:* 9,287 replaces; 8,984 change the price, 303 keep the price at the same or lower quantity, none raise the quantity at the same price.
- **Trades:** only `E`. There is no non-displayed, cross or broken-trade message. A trade is not reported separately from its book change, because `E` is both.
- **Trade side:** `E` names the resting order, so the side of the trade is the resting side. The aggressor is the other side.
- **Implied, iceberg, hidden:** none. ITCH covers displayable orders only (ITCH 2).
- **DLP orders:** order type `Q` in `F` (ITCH 4.7.2).
- **Trading state** (`H`; ITCH 4.5):
  - `T` trading, `V` suspended.
  - The spin before start of system hours covers every book eligible for trading. A book missing from it is suspended. Later messages carry changes.
  - GLIMPSE has the same rule at snapshot time (GLIMPSE 5.5).
- **System events** (`S`):
  - Group-level or system-wide (blank group). The codes are O, S, Q, M, E, C (ITCH 4.2, Table 1).
  - *Sample (UDP):* one system-wide `S` and `Q` for `DAY`, one `H` (`V`) and 16 `Y`.
- **Short selling price restriction:** `Y` (ITCH 4.6). It is a state for order entry and does not change the book.
- **Crossed book:** not possible in continuous matching without auctions (RULES 6).

## 7. Mapping to Kaspar

Kaspar side conventions:

- `orderUpdateAction` is 0 new, 1 change, 2 delete. `side` is `'0'` buy, `'1'` sell (`mdp3/handler_if.hpp`, `frame_kaspr/src/OB.cpp` `get_side`).
- OB treats an `l3_mbo_trd_v2_t` as having no effect on the book (`OB.cpp`, "execs have no impact on the book"). An execution therefore needs its own book record, as in `sim/src/Mbp2Mbo.cpp`'s `trade`, which emits a trade followed by a delete.
- OB treats a change with a higher quantity or a new price as cancel/replace (loses priority), and a lower quantity as in place (`OB.cpp` `is_canc` branch).

Record fields:

- **Time:** `transactTime` and `sendingTime` = local midnight (JST) of the session's start date + `TimestampSeconds` × 10⁹ + `TimestampNanoseconds`, as UTC epoch nanoseconds.
- **Price:** `pxd` = `Price` / 10.
- **`securityID`:** the Kaspar id of (`OrderbookId`, `Group`), section 8.

| Venue message | Kaspar record or message | Notes |
|---|---|---|
| `T` Timestamp - Seconds | none | Handler state: the current seconds, applied to later nanosecond offsets. |
| `S` System Event | none for `O`, `S`, `E`, `C`. Proposed for `Q`/`M` of a group: `l3_sst_t` per book in the group (`Q` → 17 ReadyToTrade for books in state `T`; `M` → a non-trading status). | OB matches only in status 17 (`Mbp2Mbo::start`). The status for `M` and `V` is a choice (section 10). |
| `L` Price Tick Size | none | Instrument loader: tick table rows. |
| `R` Orderbook Directory | none to OB from the record stream; `l3_lim_t` (`pxh` = Upper, `pxl` = Lower Price Limit, /10) | Instrument loader: book, ISIN, group, round lot, tick table, decimals. |
| `H` Trading State | `l3_sst_t`: `T` → 17; `V` → 2 (TradingHalt) | Per (`OrderbookId`, `Group`). |
| `Y` Short Selling Price Restriction | none | For order entry (SOM) only. |
| `A` with Order Number 0 | none | Reference price: instrument state. |
| `A`, `F` | `l3_mbo_v2_t` action 0: `orderID` = Order Number, `side` from B/S, `displayQty` = Quantity, `pxd`, `priority` = MoldUDP64 sequence number of the message | Store (book, side, price, remaining quantity) per Order Number. |
| `E` | `l3_mbo_trd_v2_t` (`orderID`, `lastQty` = Executed Quantity), then `l3_mbo_v2_t` action 1 with the remaining quantity, or action 2 when it reaches 0 | The book change is not sent by JNX and must be derived. Erase the order on 0. |
| `D` | `l3_mbo_v2_t` action 2 | Erase the order. |
| `U` | `l3_mbo_v2_t` action 2 for Original Order Number, then action 0 for New Order Number (side and book from the original; new Quantity and Price) | Delete and add: the replacement goes to the back of its level (section 10). |
| MoldUDP64 packet end | `l3_eop_t`: `msgSeqNum` = low 32 bits of the packet's first sequence number; `sendingTime` = time of the packet's last message | MoldUDP64 has no send time. A packet is not an event (section 10): `endOfEvent` is set on the last record derived from each ITCH message. |
| MoldUDP64 gap not filled by re-request | `l3_gap_v2_t` | Then recover by GLIMPSE. |
| MoldUDP64 session change; End of Session; start of a GLIMPSE recovery | `l3_chr_v2_t` | Clear the order map and books, as `handler_if::ChannelReset` does (#122). |
| GLIMPSE `T`, `S`, `L`, `R`, `H`, `Y` | as for ITCH | |
| GLIMPSE `A`, `F` (Order Number ≠ 0) | `l3_mbo_v2_t` action 0 with `recovery = true` | OB clears the book on the first recovery record and requires adds (`OB.cpp` 2020-2031, 2165). |
| GLIMPSE `G` End of Snapshot | none | Switch to the buffered ITCH stream from `SequenceNumber` (section 5). The next live record has `recovery = false`, which ends OB's recovery. |
| SoupBinTCP session packets | none | Session actor. |

## 8. Instruments

- **Source:** Orderbook Directory `R`, sent at the start of each trading day before reference prices (ITCH 4.4). GLIMPSE carries it too (GLIMPSE 5.4).
- **Daily files:** master and base-price files are on SFTP at T-1 23:00 for the day markets and at 16:45 for the night market (EVENTS). Their format is not covered here.
- **Identity:**
  - `OrderbookId`: the 4-character SICC code, Alpha since ITCH 1.7 (ITCH 5). Do not parse it as a number.
  - `OrderbookCode`: the ISIN.
  - `Group`: `DAY`, `NGHT`, `DAYX` or `DAYU`.
  - The same stock has one book per group. A Kaspar asset is therefore (`OrderbookId`, `Group`), mapped to `sec_id` through the universe file.
  - Sample: 4,182 directory entries, all group `DAY`. First entry: `1301` `JP3257200000` (*sample*).
- **Price:**
  - A 4-byte unsigned integer with 1 implied decimal, so 9 whole digits and a maximum of 214,748,364.6 (ITCH 3).
  - `PriceDecimals` is always 1 (ITCH 4.4).
  - 0x7FFFFFFF is "no reference price" in `A` with Order Number 0 (ITCH 4.7.1).
  - Prices are in JPY (RULES).
- **Quantity:** shares, with a maximum of 2,147,483,647 (ITCH 3).
  - `RoundLotSize` is per book. The sample has 1, 10, 100 and 1,000 (*sample*).
  - Price and quantity carry no multiplier.
- **Ticks:**
  - Each `R` names a tick table (`PriceTickSizeTableId`). `L` messages define each table as rows (tick, price start), and both values are in price units. A price's tick is that of the row with the highest start below it (section 10 for the boundary).
  - In the sample, table 37 has rows (1, 0), (5, 30000), (10, 50000), (50, 300000), (100, 500000), (500, 3000000), (1000, 5000000). In JPY that is 0.1 below 3,000, then 0.5, 1, 5, 10, 50 and 100 above 500,000.
  - Table 37 is the J-Market table for stocks outside TOPIX 100 and Mid400. Table 58 (1, 5, 10, 50, 100, 1000 from 0, 10000, 30000, 100000, 300000, 1000000) is the TOPIX 100 / Mid400 table (RULES Appendix 3; *sample*). Sample books use table 37 (3,685) and 58 (497).
  - X-Market and U-Market have their own tables (RULES Appendices 4, 5).
- **Price limits:** `UpperPriceLimit` and `LowerPriceLimit` in `R`. JNX price ranges are the exchange's daily limits (RULES 7). An order outside them is rejected (RULES 5).
- **Sessions** (RULES 3, EVENTS):
  - J-Market daytime (`DAY`), X-Market and U-Market: 08:20-16:30 JST, with margin trading 09:00-11:30 and 12:30-15:30.
  - J-Market nighttime (`NGHT`): 17:00 to 06:00 JST the next day. EVENTS writes the end as "30:00".
  - Monday to Friday, except holidays. The night session may end on a holiday morning.
- **Timestamps:**
  - Seconds past midnight of the day the trading session started, plus nanoseconds since the last seconds message (ITCH 4.1).
  - In the night session, times after midnight are read as 86,400 s and above, up to 108,000 (section 10).
  - The time zone is not stated. The sample's seconds are JST: 27,004 s (07:30:04) in a packet captured at 22:30:04 UTC (*sample*).
  - In the sample, every nanosecond offset (216,346 messages) is a multiple of 1,000, so the resolution is 1 µs (*sample*).
- **Lifecycle:**
  - The tradable universe is set daily, about 4,000 names (RULES 1).
  - The directory at the start of each day is the complete list, so books not in it do not trade that day.
  - New listings without a primary-exchange price cannot be traded (RULES 5).

## 9. Pitfalls

| Item (pitfalls.md) | How it applies |
|---|---|
| **1. Reading the specification** | |
| Several documents and versions | ITCH 2.02, GLIMPSE 2.01 (latest on the site), MOLD 1.02 and SOUP 1.01 apply (section 1). Samples are ITCH 1.7 and GLIMPSE 1.4, which have the same layout (ITCH 5 history: 2.00-2.02 no factual change). |
| Errata, change tables | Revision histories read: ITCH 5, GLIMPSE 6, MOLD 5, SOUP 6. None overrides a table. |
| Tables across page breaks | ITCH 4.3 (p. 4-5) and 4.6 (p. 6-7) continue across pages. Both checked against the PDF images; offsets are contiguous and checked by `--check`. |
| Offsets 0/1-based; lengths counting themselves | Offsets are 0-based. The MoldUDP64 Message Length excludes itself (MOLD 3.2). The SoupBinTCP length excludes itself, and the JNX SoupBinTCP tables include it at offset 0, so offsets are shifted by 2 in `glimpse.tbl` (section 3). |
| Reserved and padding fields | `Attribution` in `F` is "Reserved. Always blank" (ITCH 4.7.2). Kept at offset 30. |
| Enumerations defined elsewhere | System Event codes are in ITCH Table 1 and Login Reject codes in SOUP Table 1. Both are copied into the field descriptions. |
| Field names differing between table and prose | 4.7.1 calls the message "Order Added Message – No Attributes" and its table says "Order Added Message". Named `OrderAddedMessage`. No field names differ. |
| Examples contradicting tables | The documents have no examples. The sample decodes with no unknown or malformed messages (section 10, real data). |
| Two protocol layers in one table | ITCH, MoldUDP64, the SoupBinTCP session and the GLIMPSE application are in separate sections. Session and application share type codes: `S` (Sequenced Data / System Event), `A` (Login Accepted / Order Added), `H` (Server Heartbeat / Trading State), `L` (Login Request / Price Tick Size), `R` (Client Heartbeat / Orderbook Directory). |
| Message names repeated with different codes | `A` and `F` are both Order Added. They are named `OrderAddedMessage` and `OrderAddedWithAttributesMessage`. |
| Variable-length trailing field | SoupBinTCP Debug, Sequenced Data and Unsequenced Data, and the MoldUDP64 Message Block. Declared without a length, as `bytes`. |
| Odd integer widths | None. All integers are 2, 4 or 8 bytes. |
| **2. Framing and encoding** | |
| Endianness | Big-endian throughout (section 3). |
| Signed or unsigned | Unsigned only (ITCH 3, MOLD 2.4). |
| Fixed-point prices | Global: 1 decimal. `PriceDecimals` in `R` is always 1 (ITCH 3, 4.4). |
| Null and sentinel values | Price 0x7FFFFFFF is "no reference price". Order Number 0 marks a reference price. Group is blank for system-wide events. MoldUDP64 Message Count 0 is a heartbeat and 0xFFFF End of Session. |
| String padding | Alpha fields are right-padded with spaces. SoupBinTCP Session and sequence numbers are left-padded with spaces. MoldUDP64 Session padding is not stated (section 10). |
| Several messages per packet; split messages | MoldUDP64 packets carry several whole messages, never split (MOLD 3). Over TCP, packets can be split across reads, and `decode` reports `incomplete`. |
| Timestamps | Seconds message plus nanosecond offset, from midnight of the session's start day. JST in the sample. Values can exceed 86,400 in the night session (section 10). |
| **3. Sequencing and recovery** | |
| Sequence scope | Per MoldUDP64 session and stream. GLIMPSE's SoupBinTCP numbering is its own: it starts at 1 and is unrelated to ITCH (*sample*). |
| A/B arbitration | No A/B in the public documents. If JNX provides A/B, use the existing implementation (section 10). |
| Gap detection | MOLD 2.6.3 (section 5). Duplicate or older packets are dropped. |
| Recovery mechanism | MoldUDP64 re-request; a GLIMPSE snapshot with the next ITCH sequence number; SoupBinTCP ITCH resume (section 5). |
| Snapshot alignment | Apply buffered ITCH messages numbered at or above `G.SequenceNumber` (GLIMPSE 5.8). |
| Snapshot too old for the buffer | Fill the range between `G` and the buffer by re-request, otherwise take a new snapshot. Re-request replies are limited to one UDP packet (MOLD 4). |
| State to clear on reset | Order map, books, seconds, tick tables, directory, states (section 5). Matches the #122 lesson. |
| Recovery paths exercised | To do in step 7: inject gaps, duplicates, session change and End of Session into the UDP sample. |
| Session-level behaviour | GLIMPSE login needs a blank session. Heartbeats every 1 s, 15 s timeout, login within 30 s, username bound to port (SOUP). Daily new MoldUDP64 session. |
| **4. Book semantics** | |
| MBO or price level | MBO. |
| Level-index updates | Not applicable (MBO). |
| Depth-limited books | Not applicable: every displayable order. |
| Queue priority on modify | `U` gives a new Order Number. Whether a same-price, lower-quantity replace keeps priority is not stated (section 10). |
| Implied, iceberg, hidden | None in the feed (ITCH 2). |
| Trades as book deltas | `E` is both trade and book change. Derive the MBO change or delete from it. JNX sends no separate delete, so there is no double counting (*sample*: no `D` after full executions). |
| Trade side | The resting order's side (`E` names the resting order). |
| Trading states | `H` T/V. No auctions. A crossed book is not legal in continuous matching (RULES 6). |
| Book clears | New MoldUDP64 session (daily), snapshot recovery. Orders expire at session end (DAY time in force, RULES 4). Whether JNX sends `D` for expired orders is not stated (section 10). |
| **5. Prices, quantities, instruments** | |
| Price or yield | Price (JPY). |
| Quantity multipliers | None. Shares, with a round lot per book. |
| Decimals with sentinels | `PriceDecimals` is fixed at 1. No sentinel. |
| Price-dependent ticks | Yes: tick tables from `L`, selected per book by `R` (section 8). |
| Instrument definitions | `R` at the start of day, also in GLIMPSE. Key (`OrderbookId`, `Group`). |
| Instrument lifecycle | Universe set daily (RULES 1). SICC codes are Alpha. |
| **6. Behaviour the specification omits** | |
| Subscription options | None. One multicast stream, no options. |
| History replayed on subscribe | GLIMPSE sends the current state, not history. ITCH over SoupBinTCP with sequence `0` starts at the most recent message (SOUP 5.2.3). The sample TCP login used `0` and started at 3,168,301. |
| Snapshot of the whole book | Yes: GLIMPSE is the whole state. The sample has 4,182 books before its truncation. |
| Order of messages within an event | Not stated. ITCH has no end-of-event marker. In the sample, executions with the same timestamp cross packet boundaries (173 of 376 pairs), so packets are not events (section 10). |
| Connection limits | SoupBinTCP: 15 s timeout, login within 30 s. MoldUDP64: none. |
| **7. Post-trade** | Not applicable: this is a market-data feed. Each item (FIX version, dictionary, credentials, rejects in `toApp`, session status, subscription, report lifecycle, duplicates, fixed-income fields, daily resets) has no counterpart in ITCH or GLIMPSE. |
| **8. Order entry** | Not applicable: this is a market-data feed. The JNX OUCH and FIX order-entry specifications are separate documents and not in scope. |

## 10. Ambiguities

| Question | Reading taken | Alternatives | Resolved by |
|---|---|---|---|
| Order Numbers are "unique per day per order book group" (ITCH 4.7), but `E`, `D`, `U` carry no group. How is an Order Number resolved when several groups share a stream? | One order map per MoldUDP64 stream, keyed by Order Number. This assumes numbers do not repeat across the groups on one stream. | Numbers repeat across groups on one stream. Then `E`/`D`/`U` would be ambiguous, which seems unlikely, but the spec does not exclude it. | Open: JNX, or a capture with several groups. The sample has one stream and one group (`DAY`), 128,366 adds, no repeated Order Number. |
| "Order Executed Messages for the same order are cumulative" (ITCH 4.8): is Executed Quantity cumulative per order, or per execution? | Per execution; the executions add up. | Cumulative total executed so far. | Settled by the sample: 100 orders filled exactly by the sum of their partial executions, 0 overfills. |
| Does a full execution remove the order, or does `D` follow? | Removed by the execution. | `D` follows. | Settled by the sample: 66,858 full executions, 0 followed by `D`. |
| Queue priority after `U` (ITCH 4.10 gives a New Order Number; priority not stated; RULES 4 allows price and quantity amendment). | Loses priority: delete, then add at the back. | A same-price, lower-quantity replace keeps priority (303 such replaces in the sample). | Open: JNX. This decides simulated fills. |
| Side and book of the replacement order (`U` carries neither). | Same as the original. | None reasonable. | Text (ITCH 4.10 "replaced"). |
| Event boundaries: ITCH has no end-of-event marker. Where is `endOfEvent` set? | On the last record derived from each ITCH message (after the pair from `E` or `U`). Every intermediate state of a sweep is an uncrossed book, and OB also groups by `transactTime`. | Per run of messages with the same timestamp, which needs one message of lookahead. | Partly settled by the sample: per packet is wrong. Of 376 consecutive `E` pairs with the same timestamp, 173 cross a MoldUDP64 packet boundary. |
| Night-session timestamps after midnight. | Seconds keep counting from the start day's midnight (86,400 and up). | They wrap at midnight. | Open: a night-session capture. ITCH 4.1 wording supports the reading. |
| Time zone of the timestamps. | JST. | UTC. | Settled by the sample (section 8). |
| End of Snapshot gives a sequence number but no MoldUDP64 session or stream. | It applies to the current session of the one ITCH stream that GLIMPSE serves. | Several streams, one GLIMPSE per stream. | Open: JNX connectivity details (not public). |
| Order within a price level in a GLIMPSE snapshot. | `A`/`F` arrive in time priority within each level. | Any order. | Open. The sample is truncated before any resting order. |
| Tick at a band boundary: `L` gives price start 30000 (3,000.0) for tick 0.5, while RULES Appendix 3 gives "≤ 3,000: 0.1". | The boundary price belongs to the lower band (RULES), so the higher tick starts above the start price. Only next-tick arithmetic at the boundary depends on it. | The higher tick applies from the start price inclusive. | Open: JNX. Low impact, because boundary prices are multiples of both ticks. |
| MoldUDP64 Session padding (10-byte ANUM). | Compare all 10 bytes verbatim. | None needed. | The sample sessions are 10 digits (`1697659284`). |
| A/B multicast feeds. | Single feed with re-request. | A/B exists in non-public connectivity documents. | Open: JNX. |
| GLIMPSE Requested Sequence Number. | `0`, as in the sample. | `1`. | Partly settled by the sample: blank session and `0` were accepted, next sequence 1. |
| State after a GLIMPSE snapshot: GLIMPSE has System Events O and C only (GLIMPSE 5.2), so start and end of market hours are not in it. | Book state comes from `H` (trading or suspended). Market hours come from the clock and EVENTS. | None in the documents. | Open: JNX. |
| Kaspar status codes for `V` and for System Event `M`. | `V` → 2 (TradingHalt), `M` → a non-17 status. | Others. | Kaspar-side choice, for the user. |
| Expired orders at session end (DAY time in force). | Expect `D` messages or a new session the next day. Clear books at End of Session in any case. | Orders silently disappear. | Open: an end-of-session capture. |
| MOLD 2.1 calls MoldUDP64 a "point-to-point transport layer", while MOLD 2 and ITCH 2 call it one-to-many. | One-to-many multicast. | None. | Text (MOLD 2, 2.6.1). |
| ITCH over SoupBinTCP: a whole-day replay from sequence 1 is not stated. | Use it only to resume a session. Recover with GLIMPSE. | Full replay available. | Open: JNX. |
