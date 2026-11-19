# Feed description: Aquis Exchange market data (and Aquis Stock Exchange)

Aquis Exchange's pan-European MTFs, Aquis UK (operating MIC AQXE, Aquis Exchange
Limited, London) and Aquis EU (AQEU, Aquis Exchange Europe, Paris), publish
market by order over UDP multicast, split by market of listing, with a snapshot
feed per split and a TCP replay service. Aquis Stock Exchange (AQSE), the
London listing venue, has its own multicast feed with the same framing and a
different message set; it is described here as far as the decoder (section 4.2)
and its differences (section 11). The handler (section 7) is for the Aquis
Exchange feed only.

Every entry cites its source. Abbreviations:

- **MD**: Aquis Market Data Technical Specification, version 4.2, August 2026,
  production edition (24 pp.). Printed page numbers equal PDF page numbers.
  Citations give page and section, e.g. "MD p.12, 3.4.3".
- **MD 4.2 pre**: the same document, pre-release edition, May 2026.
- **MD 4.1**, **MD 4.0**, **MD 2.5**: earlier production editions (section 1).
- **AQSE**: Aquis Stock Exchange Multicast Market Data Technical Specification,
  version 1.2.3, April 2024 (22 pp.). Citations give the PDF page; the printed
  page is the PDF page minus 4.
- **MN yyyy_nnnn**: Aquis Exchange Member Notice of that number.
- **SL**: Aquis's public security file `SecurityList.csv` (AQXE, 7,119 rows) and
  `SecurityList_AQEU.csv` (AQEU, 4,118 rows), downloaded 2026-11-18 from
  `https://aquis-public-files.s3.eu-west-2.amazonaws.com/`.

## 1. Venue and documents

| Document | Version | Date | Applies to |
|---|---|---|---|
| Aquis Market Data Technical Specification [Production] (`Production_Aquis_Market_Data_Technical_Specification_v4_2_a83557a9c6.pdf`) | 4.2 | August 2026 (MD p.4) | Aquis UK and Aquis EU from 17 August 2026 (MN 2026_0008; MN 2026_0010) |
| same, [Pre-release] (`Pre_release_Aquis_Market_Data_Technical_Specification_v4_2_2430865b4a.pdf`) | 4.2 | May 2026 | superseded by the production 4.2 |
| same, [Production] (`..._v4_1_43f611c3fc.pdf`) | 4.1 | January 2026 (its cover); "October 2025" in MD p.4 | 1 December 2025 (Segment MIC on Trade, MN 2025_0016, 2025_0017) to 14 August 2026 |
| same, [Production] (`..._v4_0_a7d63de081.pdf`) | 4.0 | 15 August 2025 (its history); cover still reads "Version 2.5, November 2023" | 11 August 2025 (MN 2025_0006) to 28 November 2025 |
| same, [Production] (`..._v2_5_53bde2dc69.pdf`), the version named in the task | 2.5 | November 2023 | until 8 August 2025 |
| Aquis Stock Exchange Multicast Market Data Specification (`Aquis_Stock_Exchange_Multicast_Market_Data_Specification_v1_2_3_2024_fc07d16378.pdf`) | 1.2.3 | April 2024 | AQSE; 1.2.3 changes branding only against 1.2.2 (AQSE p.4, history; a text diff of the two shows no other change) |
| same, the version named in the task (`aquis-public-files/pdf_assets/Aquis Stock Exchange Multicast Market Data Specification v1.2.2 .pdf`) | 1.2.2 | February 2021 | superseded by 1.2.3 |
| Member Notices 2025_0006, 2025_0007, 2025_0016, 2025_0017, 2026_0002, 2026_0008, 2026_0010, 2026_0019 | | May 2025 - October 2026 | effective dates, changes, multicast groups of the retiring 100 Mb feeds |
| AQSE Reference Data Technical Specification | 3.2 | July 2025 | AQSE reference files; not used by the handler |

All files are public, in the S3 bucket behind aquis.eu
(`https://aqx-web-prod-s3-public-read.s3.eu-west-2.amazonaws.com/`, listable).
The aquis.eu document library itself answered 429 with a bot checkpoint to
every request, so the bucket listing (3,688 keys, 2026-11-18) was used to find
the latest editions. Newest market-data specification in the bucket: the
production 4.2 above (uploaded 2026-08-18). Newest AQSE multicast
specification: 1.2.3 (uploaded 2024-04-30).

**Which version applies.** On 2026-11-18 the production MD 4.2 applies: its
changes (Bundle Start and End, pubTimestamp on seven messages, timestamp and
marketId on Security Definition) took effect on 17 August 2026 (MN 2026_0008,
MN 2026_0010), and the production edition adds only the MaC+ bits of Security
Status marketFlags to the pre-release one (diff of the two texts; MD p.5,
history). The field table (`aquis_md.tbl`) is MD 4.2. Every 4.x change appends
fields at the end of a message, except the Segment MIC inserted at offset 37 of
Trade in 4.1 (MD p.13, 3.4.6, against MD 4.0 printed p.6, 3.4.4), so 4.0 and 4.1 messages are
prefixes of 4.2 ones except Trade. MD 2.5, named in the task, describes the feed
before 11 August 2025 and is not decoded.

Timestamps carry full nanoseconds from 2 March 2026; before that they were
microseconds times 1000 (MD p.10, 3.1, footnote; MN 2026_0002).

Documents referred to and not read: the Aquis security and tick-table reference
data files (MD p.7, 2.1; FTP, members only); the network configuration,
multicast addresses of the 1 Gb and 10 Gb feeds and replay credentials, which
"can be provided by the Aquis connectivity team" (MD p.6, 1.1).

**Sample data.** No public Aquis capture was found: none in either public S3
bucket (all keys listed), none by web search. The public files that were used
are SL (instrument loader test) and the tick-table CSV
`pdf_assets/Tick Sizes CSV AQX.csv` (260 rows, 36 tables). The bucket's
`market_data/production/*_POST_*.csv` files are post-trade transparency
reports, not feed captures.

## 2. Transport

- **Real-time feeds**: IP multicast, one feed per "market split"; WAN-shaped
  (100 Mb) and Gig-shaped versions; more than one channel, e.g. two from the
  primary data centre and one from the secondary (MD p.6, 1.1; p.8, 2.2). The
  addresses are not in MD. The retirement notice for the 100 Mb feeds lists
  them: per venue, A, B and C feeds; Aquis UK MD splits 1 LSE/SWX, 2 ENXT, 3
  XET/AUS, 4 Others; Aquis EU MD splits 2-4; MaC splits 1-4 and combined MaC
  feeds, some 15 minutes delayed; e.g. Aquis UK MD split 1 A feed
  224.0.121.68:19020, B 224.0.121.132:19260, C 224.0.121.196:19580 (MN
  2026_0019). Those feeds end on 27 November 2026; the 1 Gb and 10 Gb addresses
  are not public.
- **Snapshot feed**: one per market split, multicast, full depth every 10 s
  (MD p.8, 2.2).
- **Replay service**: TCP, one per feed; Login (credentials from Aquis), then
  Replay Requests; the connection may be held all day or opened when needed;
  a bad login is dropped without a response (MD p.9, 2.3; p.20-21, 3.8).
- **Heartbeat**: after 1 s without data during trading hours, and pre and post
  market (MD p.8, 2.1; p.11, 3.3.2).
- **Throttles, session limits**: none stated.

The handler does not open sockets (section 7, "What is not built").

## 3. Framing

- A multicast packet is a 1-byte message count followed by that many messages
  (MD p.10, 3.2). No packet sequence number, no length field, no session id.
- Every message begins with a 6-byte header: msgType (u8), length (u8, the
  whole message including the header), seqNo (u32) (MD p.11, 3.3.1). The type
  comes before the length, so `fieldgen.py`'s `@length_prefix` cannot describe
  it (section 10, item 21); the header fields are written into every message of
  the table, and the packet reader steps by `length`. A `length` greater than
  the table's size is skipped to its end (later versions append fields); a
  smaller one is counted and skipped.
- 1-byte packing, little-endian integers, all unsigned; char(n) left justified,
  NUL padded; price u64 with 5 implied decimals ("1462500 represents 14.625");
  timestamp u64 nanoseconds since the Unix epoch (MD p.10, 3.1).
- The replay TCP stream carries the same messages back to back, framed by the
  header's length; session messages have seqNo 0 (MD p.20, 3.8).
- Largest message: Security Definition, 75 bytes; `length` is a u8, so no
  message can exceed 255 bytes (MD p.11, 3.3.1).

## 4. Message catalogue

Generated by `fieldgen.py aquis_md.tbl --markdown` and `fieldgen.py aqse_md.tbl
--markdown`. Offsets are 0-based and the specification's own (the header is at
0-5). Sizes are the sums of the specification's widths.

### 4.1 Aquis Exchange (MD 4.2)

#### pkt::from_aquis

##### PacketHeader (type none, 1 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| messageCount | 0 | 1 | u8 | Number of messages in this packet |

#### md::from_aquis

##### Heartbeat (type `1` (0x1), 6 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | Heartbeat: header only; seqNo is the next business message expected and is not consumed. Sent after 1 s without data during trading hours and pre and post market (MD p.11, 3.3.2; p.8, 2.1) |
| length | 1 | 1 | u8 | 6 |
| seqNo | 2 | 4 | u32 | Next expected business sequence number |

##### MDBundleStart (type `34` (0x22), 6 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | MDBundleStart: start of the messages generated in one sequencer cycle (MD p.11, 3.4.1) |
| length | 1 | 1 | u8 | 6 |
| seqNo | 2 | 4 | u32 | Sequence number of this message in the stream for the current trading day |

##### MDBundleEnd (type `35` (0x23), 6 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | MDBundleEnd: all messages of the sequencer cycle have been published (MD p.11, 3.4.2) |
| length | 1 | 1 | u8 | 6 |
| seqNo | 2 | 4 | u32 | Sequence number of this message in the stream for the current trading day |

##### OrderAdd (type `2` (0x2), 42 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | OrderAdd: visible order quantity posted to the book (MD p.12, 3.4.3) |
| length | 1 | 1 | u8 | 42 |
| seqNo | 2 | 4 | u32 | Sequence number in the stream for the current trading day |
| securityID | 6 | 2 | u16 | Numeric identifier of the security |
| side | 8 | 1 | u8 | 1 buy, 2 sell |
| quantity | 9 | 4 | u32 | Shares added to the book |
| price | 13 | 8 | price | Price of the order, 5 implied decimals |
| orderRef | 21 | 4 | u32 | Unique order reference number for the day |
| timestamp | 25 | 8 | timestamp | Time of this market data event |
| mdFlags | 33 | 1 | u8 | Bit 0: 1 unrestricted liquidity available at the top of the book on the order's side (MD p.23, 3.9.2) |
| pubTimestamp | 34 | 8 | timestamp | Time the market data event was published (added in 4.2) |

##### OrderCancel (type `3` (0x3), 29 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | OrderCancel: a visible order removed from the book (MD p.12, 3.4.4) |
| length | 1 | 1 | u8 | 29 |
| seqNo | 2 | 4 | u32 | Sequence number in the stream for the current trading day |
| securityID | 6 | 2 | u16 | Numeric identifier of the security |
| orderRef | 8 | 4 | u32 | Unique order reference number for the day |
| timestamp | 12 | 8 | timestamp | Time of this market data event |
| mdFlags | 20 | 1 | u8 | See MD p.23, 3.9.2 |
| pubTimestamp | 21 | 8 | timestamp | Time the market data event was published (added in 4.2) |

##### OrderModify (type `4` (0x4), 41 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | OrderModify: price and/or quantity of a visible order changed; same orderRef; quantity down keeps priority, otherwise the book is re-ordered (MD p.12-13, 3.4.5) |
| length | 1 | 1 | u8 | 41 |
| seqNo | 2 | 4 | u32 | Sequence number in the stream for the current trading day |
| securityID | 6 | 2 | u16 | Numeric identifier of the security |
| quantity | 8 | 4 | u32 | Shares remaining in the book (absolute) |
| price | 12 | 8 | price | Price of the order, 5 implied decimals |
| orderRef | 20 | 4 | u32 | Unique order reference number for the day |
| timestamp | 24 | 8 | timestamp | Time of this market data event |
| mdFlags | 32 | 1 | u8 | See MD p.23, 3.9.2 |
| pubTimestamp | 33 | 8 | timestamp | Time the market data event was published (added in 4.2) |

##### Trade (type `5` (0x5), 54 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | Trade: an order traded, partly or fully; also the AoD and MaC trade message (MD p.13-14, 3.4.6; p.18, 3.5.3, 3.6.2) |
| length | 1 | 1 | u8 | 54 |
| seqNo | 2 | 4 | u32 | Sequence number in the stream for the current trading day |
| securityID | 6 | 2 | u16 | Numeric identifier of the security |
| tradeType | 8 | 1 | u8 | 1 visible, 2 hidden or reserve, 3 MaC, 4 LIS Cross, 5 Benchmark Cross, 6 AoD, 12 AVM (MD p.14, 3.4.6) |
| quantity | 9 | 4 | u32 | Shares traded |
| price | 13 | 8 | price | Execution price, 5 implied decimals |
| orderRef | 21 | 4 | u32 | The resting visible order for tradeType 1; 0 against hidden quantity or for a MaC trade |
| tradeRef | 25 | 4 | u32 | Trade reference number |
| timestamp | 29 | 8 | timestamp | Time of this market data event |
| MIC | 37 | 4 | char | Segment MIC (added in 4.1) |
| BinaryMMT | 41 | 4 | u32 | Post-trade MMT flags, bit fields in MD p.22-23, 3.9.1 |
| mdFlags | 45 | 1 | u8 | Bit 0 refers to the passive side of the trade (MD p.23, 3.9.2) |
| pubTimestamp | 46 | 8 | timestamp | Time the market data event was published (added in 4.2) |

##### TradeBust (type `6` (0x6), 44 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | TradeBust: a trade declared erroneous; also the AoD and MaC trade bust (MD p.14, 3.4.7) |
| length | 1 | 1 | u8 | 44 |
| seqNo | 2 | 4 | u32 | Sequence number in the stream for the current trading day |
| securityID | 6 | 2 | u16 | Numeric identifier of the security |
| quantity | 8 | 4 | u32 | Shares of the original trade |
| price | 12 | 8 | price | Execution price of the original trade |
| tradeRef | 20 | 4 | u32 | Trade reference of the busted trade |
| timestamp | 24 | 8 | timestamp | Time of this market data event |
| BinaryMMT | 32 | 4 | u32 | Post-trade MMT flags (MD p.22-23, 3.9.1) |
| pubTimestamp | 36 | 8 | timestamp | Time the market data event was published (added in 4.2) |

##### TickTableData (type `7` (0x7), 33 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | TickTableData: one band of a dynamic tick table, or a static tick; pre-market (MD p.14-15, 3.4.8) |
| length | 1 | 1 | u8 | 33 |
| seqNo | 2 | 4 | u32 | Sequence number in the stream for the current trading day |
| tickTableID | 6 | 1 | u8 | Identifier of the tick table or static tick |
| name | 7 | 10 | char | Short name |
| threshold | 17 | 8 | price | Price from which this tick size applies |
| tickSize | 25 | 8 | price | Price increment |

##### SecurityDefinition (type `8` (0x8), 75 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | SecurityDefinition: a security on this feed; pre-market, rarely intraday on a change or correction (MD p.15-16, 3.4.9) |
| length | 1 | 1 | u8 | 75 |
| seqNo | 2 | 4 | u32 | Sequence number in the stream for the current trading day |
| securityID | 6 | 2 | u16 | Numeric identifier of the security |
| UMTF | 8 | 6 | char | Uniform MTF code |
| ISIN | 14 | 12 | char | ISIN |
| currency | 26 | 3 | char | Trading currency |
| MIC | 29 | 4 | char | MIC of the market of listing |
| tickTableId | 33 | 1 | u8 | The security's tick table |
| flags | 34 | 2 | u16 | Bit 0 macEnabled, 1 testStock, 2 illiquid, 4 aodEnabled, 7 avxEnabled; others reserved |
| exchangeSymbol | 36 | 20 | char | Exchange symbol (added in 4.0) |
| lotSize | 56 | 8 | u64 | Always 1,000,000,000: a lot of 1 (added in 4.0) |
| lotSizeDecimal | 64 | 1 | u8 | Always 9 (added in 4.0) |
| timestamp | 65 | 8 | timestamp | Time of this market data event (added in 4.2) |
| marketId | 73 | 2 | u16 | Numerical market identifier (added in 4.2) |

##### SecurityStatus (type `9` (0x9), 27 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | SecurityStatus: trading status of a security or of its market changed; also the AoD and MaC security status (MD p.16-17, 3.4.10; p.18, 3.5.2, 3.6.4) |
| length | 1 | 1 | u8 | 27 |
| seqNo | 2 | 4 | u32 | Sequence number in the stream for the current trading day |
| securityID | 6 | 2 | u16 | Numeric identifier of the security |
| tradingStatus | 8 | 1 | u8 | 1 Active, 2 Halted, 3 Suspended, 8 AoD Start, 24 AoD Finish |
| marketFlags | 9 | 1 | u8 | Bit 0 continuous trading open, 1 macOpen, 2 macRun (locked down), 5 macplusOpen, 6 macplusRun; 3, 4, 7 reserved |
| timestamp | 10 | 8 | timestamp | Time of this market data event |
| tradingPhase | 18 | 1 | u8 | 0 Closed, 1 Continuous (added in 4.0) |
| pubTimestamp | 19 | 8 | timestamp | Time the market data event was published (added in 4.2) |

##### AoDUpdate (type `17` (0x11), 36 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | AoDUpdate: indicative price and matched volume during an AoD; 0 and 0 when the auction has uncrossed (MD p.17-18, 3.5.1) |
| length | 1 | 1 | u8 | 36 |
| seqNo | 2 | 4 | u32 | Sequence number in the stream for the current trading day |
| securityID | 6 | 2 | u16 | Numeric identifier of the security |
| indicativePrice | 8 | 8 | price | Indicative auction price |
| matchVol | 16 | 4 | u32 | Indicative matched volume |
| timestamp | 20 | 8 | timestamp | Time of this market data event |
| pubTimestamp | 28 | 8 | timestamp | Time the market data event was published (added in 4.2) |

##### MaCUpdate (type `16` (0x10), 32 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | MaCUpdate: indicative price and buy and sell totals during the MaC; MaC feeds only (MD p.18, 3.6.1) |
| length | 1 | 1 | u8 | 32 |
| seqNo | 2 | 4 | u32 | Sequence number in the stream for the current trading day |
| securityID | 6 | 2 | u16 | Numeric identifier of the security |
| indicativePrice | 8 | 8 | price | Indicative auction price |
| closingBuyQty | 16 | 4 | u32 | Total buy quantity during the closing market, otherwise 0 |
| closingSellQty | 20 | 4 | u32 | Total sell quantity during the closing market, otherwise 0 |
| timestamp | 24 | 8 | timestamp | Time of this market data event |

#### snap::from_aquis

##### SnapshotStart (type `10` (0xA), 20 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | SnapshotStart: first message of each snapshot publication, every 10 s (MD p.19, 3.7.1; p.8, 2.2) |
| length | 1 | 1 | u8 | 20 |
| seqNo | 2 | 4 | u32 | Not described for the snapshot feed |
| streamSeqNo | 6 | 4 | u32 | Sequence number of the last continuous-stream message the snapshot relates to |
| securityCount | 10 | 2 | u16 | Securities reported in this snapshot |
| timestamp | 12 | 8 | timestamp | Time of this snapshot |

##### BookStatus (type `11` (0xB), 28 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | BookStatus: status of one security and the number of its open orders; Book Entry messages follow (MD p.19, 3.7.2) |
| length | 1 | 1 | u8 | 28 |
| seqNo | 2 | 4 | u32 | Not described for the snapshot feed |
| securityID | 6 | 2 | u16 | Numeric identifier of the security |
| tradingStatus | 8 | 1 | u8 | As Security Status |
| marketFlags | 9 | 1 | u8 | As Security Status |
| entries | 10 | 2 | u16 | Open orders in the book; that many Book Entry messages follow |
| closingBuyQty | 12 | 4 | u32 | Total buy quantity during the closing market, otherwise 0 |
| closingSellQty | 16 | 4 | u32 | Total sell quantity during the closing market, otherwise 0 |
| indicativePrice | 20 | 8 | price | Indicative auction price |

##### BookEntry (type `12` (0xC), 25 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | BookEntry: one open order; price-time priority for one side, then the other (MD p.19-20, 3.7.3) |
| length | 1 | 1 | u8 | 25 |
| seqNo | 2 | 4 | u32 | Not described for the snapshot feed |
| securityID | 6 | 2 | u16 | Numeric identifier of the security |
| Side | 8 | 1 | u8 | 1 buy, 2 sell |
| quantity | 9 | 4 | u32 | Open shares |
| Price | 13 | 8 | price | Price of the order |
| orderRef | 21 | 4 | u32 | Unique order reference number for the day |

##### MaCBookEntry (type `18` (0x12), 25 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | MaCBookEntry: during the MaC, the first 5 valid best bid and offer orders per security in chronological order; a snapshot (MD p.20, 3.7.4) |
| length | 1 | 1 | u8 | 25 |
| seqNo | 2 | 4 | u32 | Not described |
| securityID | 6 | 2 | u16 | Numeric identifier of the security |
| Side | 8 | 1 | u8 | 1 buy, 2 sell |
| Quantity | 9 | 4 | u32 | Open shares |
| Price | 13 | 8 | price | Price of the order |
| orderRef | 21 | 4 | u32 | Unique order reference number for the day |

#### replay::to_aquis

##### Login (type `13` (0xD), 26 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | Login: first message on a replay connection (MD p.20-21, 3.8.1; p.9, 2.3) |
| length | 1 | 1 | u8 | 26 |
| seqNo | 2 | 4 | u32 | 0; ignored by Aquis (MD p.20, 3.8) |
| username | 6 | 10 | char | As assigned by Aquis |
| password | 16 | 10 | char | As assigned by Aquis |

##### ReplayRequest (type `14` (0xE), 14 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | ReplayRequest: request one message or a range (MD p.21, 3.8.2) |
| length | 1 | 1 | u8 | 14 |
| seqNo | 2 | 4 | u32 | 0; ignored by Aquis (MD p.20, 3.8) |
| beginSeqNo | 6 | 4 | u32 | Stream seqNo of the first message requested |
| endSeqNo | 10 | 4 | u32 | Stream seqNo of the last message requested |

#### replay::from_aquis

Also decodes the messages of `md::from_aquis`.

##### ReplayResponse (type `15` (0xF), 7 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | ReplayResponse: Login accepted, or a Replay Request refused; an accepted request gets no response, only the messages (MD p.21, 3.8.3) |
| length | 1 | 1 | u8 | 7 |
| seqNo | 2 | 4 | u32 | 0 (MD p.20, 3.8) |
| responseCode | 6 | 1 | u8 | 0 Login successful, 1 bad beginSeqNo, 2 bad endSeqNo |


### 4.2 Aquis Stock Exchange (AQSE 1.2.3)

#### pkt::from_aqse

##### PacketHeader (type none, 1 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| messageCount | 0 | 1 | u8 | Number of messages in this packet |

#### md::from_aqse

##### Heartbeat (type `1` (0x1), 6 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | Heartbeat: header only; seqNo is the next business message expected and is not consumed (AQSE p.10, 3.3.2; p.7, 2.1) |
| length | 1 | 1 | u8 | 6 |
| seqNo | 2 | 4 | u32 | Next expected business sequence number |

##### OrderAdd (type `2` (0x2), 33 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | OrderAdd: visible order quantity posted to the book; continuous trading only (AQSE p.10, 3.4.1) |
| length | 1 | 1 | u8 | 33 |
| seqNo | 2 | 4 | u32 | Sequence number in the stream for the current trading day |
| securityID | 6 | 2 | u16 | Numeric identifier of the security |
| side | 8 | 1 | u8 | 1 buy, 2 sell |
| quantity | 9 | 4 | u32 | Shares added to the book |
| price | 13 | 8 | price | Price of the order |
| orderRef | 21 | 4 | u32 | Unique order reference number for the day |
| timestamp | 25 | 8 | timestamp | Time of this market data event |

##### OrderCancel (type `3` (0x3), 20 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | OrderCancel: a visible order removed from the book; continuous trading only (AQSE p.11, 3.4.2) |
| length | 1 | 1 | u8 | 20 |
| seqNo | 2 | 4 | u32 | Sequence number in the stream for the current trading day |
| securityID | 6 | 2 | u16 | Numeric identifier of the security |
| orderRef | 8 | 4 | u32 | Unique order reference number for the day |
| timestamp | 12 | 8 | timestamp | Time of this market data event |

##### OrderModify (type `4` (0x4), 32 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | OrderModify: price and/or quantity changed; quantity down keeps priority, otherwise re-ordered; continuous trading only (AQSE p.11, 3.4.3) |
| length | 1 | 1 | u8 | 32 |
| seqNo | 2 | 4 | u32 | Sequence number in the stream for the current trading day |
| securityID | 6 | 2 | u16 | Numeric identifier of the security |
| quantity | 8 | 4 | u32 | Shares remaining in the book |
| price | 12 | 8 | price | Price of the order |
| orderRef | 20 | 4 | u32 | Unique order reference number for the day |
| timestamp | 24 | 8 | timestamp | Time of this market data event |

##### QuoteAddReplace (type `20` (0x14), 42 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | QuoteAddReplace: a market maker's two-sided quote for a security (AQSE p.11-12, 3.4.4) |
| length | 1 | 1 | u8 | 42 |
| seqNo | 2 | 4 | u32 | Sequence number in the stream for the current trading day |
| traderID | 6 | 2 | u16 | Market maker quoting (Trader Definition) |
| securityID | 8 | 2 | u16 | Numeric identifier of the security |
| bidQuantity | 10 | 4 | u32 | Bid shares available |
| bidPrice | 14 | 8 | price | Bid price |
| offerQuantity | 22 | 4 | u32 | Offer shares available |
| offerPrice | 26 | 8 | price | Offer price |
| timestamp | 34 | 8 | timestamp | Time of this market data event |

##### QuoteCancel (type `21` (0x15), 18 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | QuoteCancel: a quote removed (AQSE p.12, 3.4.5) |
| length | 1 | 1 | u8 | 18 |
| seqNo | 2 | 4 | u32 | Sequence number in the stream for the current trading day |
| traderID | 6 | 2 | u16 | Originator of the quote |
| securityID | 8 | 2 | u16 | Numeric identifier of the security |
| timestamp | 10 | 8 | timestamp | Time of this market data event |

##### Trade (type `5` (0x5), 41 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | Trade: an order traded, partly or fully; also the AoD trade (AQSE p.12, 3.4.6; p.17, 3.5.2) |
| length | 1 | 1 | u8 | 41 |
| seqNo | 2 | 4 | u32 | Sequence number in the stream for the current trading day |
| securityID | 6 | 2 | u16 | Numeric identifier of the security |
| tradeType | 8 | 1 | u8 | 1 visible, 2 hidden or reserve, 6 AoD, 8 Trade Capture Report (AQSE p.19, 3.6.2) |
| quantity | 9 | 4 | u32 | Shares traded |
| price | 13 | 8 | price | Execution price |
| orderRef | 21 | 4 | u32 | Order reference, for tradeType 1 (visible) |
| tradeRef | 25 | 4 | u32 | Trade reference; not unique for the day, with timestamp it is (AQSE p.6, 2.1) |
| timestamp | 29 | 8 | timestamp | Time of this market data event |
| BinaryMMT | 37 | 4 | u32 | MMT flags (AQSE p.17-19, 3.6.1) |

##### TradeReport (type `25` (0x19), 45 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | TradeReport: a new trade report submitted to AQSE (AQSE p.13, 3.4.7) |
| length | 1 | 1 | u8 | 45 |
| seqNo | 2 | 4 | u32 | Sequence number in the stream for the current trading day |
| securityID | 6 | 2 | u16 | Numeric identifier of the security |
| tradeType | 8 | 1 | u8 | See AQSE p.19, 3.6.2 |
| quantity | 9 | 4 | u32 | Shares traded |
| price | 13 | 8 | price | Execution price |
| tradeRef | 21 | 4 | u32 | Trade report reference number |
| timestamp | 25 | 8 | timestamp | System received timestamp |
| BinaryMMT | 33 | 4 | u32 | MMT flags |
| transactTime | 37 | 8 | timestamp | Time the transaction occurred |

##### TradeReportModify (type `26` (0x1A), 57 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | TradeReportModify: a published trade report modified, up to five business days later; preceded by a Trade Report Cancel of the original (AQSE p.13, 3.4.8) |
| length | 1 | 1 | u8 | 57 |
| seqNo | 2 | 4 | u32 | Sequence number in the stream for the current trading day |
| securityID | 6 | 2 | u16 | Numeric identifier of the security |
| tradeType | 8 | 1 | u8 | See AQSE p.19, 3.6.2 |
| quantity | 9 | 4 | u32 | Shares traded |
| price | 13 | 8 | price | Execution price |
| tradeRef | 21 | 4 | u32 | Trade report reference number |
| timestamp | 25 | 8 | timestamp | System received timestamp |
| BinaryMMT | 33 | 4 | u32 | MMT flags |
| transactTime | 37 | 8 | timestamp | Time the transaction occurred |
| origTradeRef | 45 | 4 | u32 | Reference of the previous accepted trade report message |
| origTimestamp | 49 | 8 | timestamp | Timestamp of the previous accepted trade report message |

##### TradeReportCancel (type `27` (0x1B), 65 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | TradeReportCancel: a published trade report cancelled, up to five business days later (AQSE p.14, 3.4.9) |
| length | 1 | 1 | u8 | 65 |
| seqNo | 2 | 4 | u32 | Sequence number in the stream for the current trading day |
| origSecurityID | 6 | 2 | u16 | Security of the previous accepted trade report message |
| origTradeType | 8 | 1 | u8 | Trade type of the previous accepted trade report message |
| origQuantity | 9 | 4 | u32 | Shares of the previous accepted trade report message |
| origPrice | 13 | 8 | price | Price of the previous accepted trade report message |
| tradeRef | 21 | 4 | u32 | Trade report reference number |
| timestamp | 25 | 8 | timestamp | System received timestamp |
| BinaryMMT | 33 | 4 | u32 | MMT flags |
| transactTime | 37 | 8 | timestamp | Time the transaction occurred |
| origTradeRef | 45 | 4 | u32 | Reference of the previous accepted trade report message |
| origTimestamp | 49 | 8 | timestamp | Timestamp of the previous accepted trade report message |
| origTransactTime | 57 | 8 | timestamp | Time the original transaction occurred |

##### TradeBust (type `6` (0x6), 36 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | TradeBust: a trade declared erroneous; order generated trades only (AQSE p.14-15, 3.4.10) |
| length | 1 | 1 | u8 | 36 |
| seqNo | 2 | 4 | u32 | Sequence number in the stream for the current trading day |
| securityID | 6 | 2 | u16 | Numeric identifier of the security |
| quantity | 8 | 4 | u32 | Shares of the original trade |
| price | 12 | 8 | price | Execution price of the original trade |
| tradeRef | 20 | 4 | u32 | Trade reference of the busted trade |
| timestamp | 24 | 8 | timestamp | Time of this market data event |
| BinaryMMT | 32 | 4 | u32 | MMT flags |

##### SecurityStatistics (type `22` (0x16), 25 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | SecurityStatistics: opening and closing prices (AQSE p.15, 3.4.11; p.7, 2.1) |
| length | 1 | 1 | u8 | 25 |
| seqNo | 2 | 4 | u32 | Sequence number in the stream for the current trading day |
| securityID | 6 | 2 | u16 | Numeric identifier of the security |
| price | 8 | 8 | price | Price |
| priceType | 16 | 1 | u8 | 2 opening price, 5 closing price |
| timestamp | 17 | 8 | timestamp | Time of this market data event |

##### TraderDefinition (type `23` (0x17), 24 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | TraderDefinition: a quote originator; pre-market (AQSE p.15, 3.4.12) |
| length | 1 | 1 | u8 | 24 |
| seqNo | 2 | 4 | u32 | Sequence number in the stream for the current trading day |
| traderID | 6 | 2 | u16 | Numeric identifier of the trader |
| senderID | 8 | 16 | char | SenderID of the trader |

##### TickTableData (type `7` (0x7), 33 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | TickTableData: one band of a dynamic tick table, or a static tick; pre-market (AQSE p.15-16, 3.4.13) |
| length | 1 | 1 | u8 | 33 |
| seqNo | 2 | 4 | u32 | Sequence number in the stream for the current trading day |
| tickTableID | 6 | 1 | u8 | Identifier of the tick table or static tick |
| name | 7 | 10 | char | Short name |
| threshold | 17 | 8 | price | Price from which this tick size applies |
| tickSize | 25 | 8 | price | Price increment |

##### SecurityDefinition (type `8` (0x8), 34 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | SecurityDefinition: a security on this feed; pre-market, rarely intraday (AQSE p.16, 3.4.14) |
| length | 1 | 1 | u8 | 34 |
| seqNo | 2 | 4 | u32 | Sequence number in the stream for the current trading day |
| securityID | 6 | 2 | u16 | Numeric identifier of the security |
| UMTF | 8 | 6 | char | Not applicable to AQSE |
| ISIN | 14 | 12 | char | ISIN |
| currency | 26 | 3 | char | Trading currency |
| MIC | 29 | 4 | char | MIC of the market of listing |
| tickTableId | 33 | 1 | u8 | The security's tick table |

##### SecurityStatus (type `9` (0x9), 18 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | SecurityStatus: trading status of a security or of its market changed (AQSE p.16-17, 3.4.15) |
| length | 1 | 1 | u8 | 18 |
| seqNo | 2 | 4 | u32 | Sequence number in the stream for the current trading day |
| securityID | 6 | 2 | u16 | Numeric identifier of the security |
| tradingStatus | 8 | 1 | u8 | 1 Active, 2 Halted, 3 Suspended |
| marketFlags | 9 | 1 | u8 | Bit 0 continuous trading open (AoD available), bit 3 pre phase; with bit 0: 0/1 pre-open, 1/0 open, 1/1 pre-close, 0/0 closed |
| timestamp | 10 | 8 | timestamp | Time of this market data event |

##### AoDUpdate (type `17` (0x11), 28 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | AoDUpdate: indicative price and matched volume during an AoD; the first one starts the auction (AQSE p.17, 3.5.1) |
| length | 1 | 1 | u8 | 28 |
| seqNo | 2 | 4 | u32 | Sequence number in the stream for the current trading day |
| securityID | 6 | 2 | u16 | Numeric identifier of the security |
| indicativePrice | 8 | 8 | price | Indicative auction price |
| matchVol | 16 | 4 | u32 | Indicative matched volume |
| timestamp | 20 | 8 | timestamp | Time of this market data event |

#### snap::from_aqse

##### SnapshotStart (type `10` (0xA), 20 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | SnapshotStart: first message of each snapshot, every 10 s; continuous trading only (AQSE p.20, 3.7.1; p.7, 2.2) |
| length | 1 | 1 | u8 | 20 |
| seqNo | 2 | 4 | u32 | Not described for the snapshot feed |
| streamSeqNo | 6 | 4 | u32 | Sequence number of the last continuous-stream message the snapshot relates to |
| securityCount | 10 | 2 | u16 | Securities reported in this snapshot |
| timestamp | 12 | 8 | timestamp | Time of this snapshot |

##### BookStatus (type `11` (0xB), 28 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | BookStatus: status of one security and the number of its open orders (AQSE p.20, 3.7.2) |
| length | 1 | 1 | u8 | 28 |
| seqNo | 2 | 4 | u32 | Not described for the snapshot feed |
| securityID | 6 | 2 | u16 | Numeric identifier of the security |
| tradingStatus | 8 | 1 | u8 | As Security Status (the spec says "Section 3.4.13", which is Tick Table Data) |
| marketFlags | 9 | 1 | u8 | As Security Status |
| entries | 10 | 2 | u16 | Open orders in the book; that many Book Entry messages follow |
| closingBuyQty | 12 | 4 | u32 | Total buy quantity during the closing market, otherwise 0 |
| closingSellQty | 16 | 4 | u32 | Total sell quantity during the closing market, otherwise 0 |
| indicativePrice | 20 | 8 | price | Indicative auction price |

##### BookEntry (type `12` (0xC), 25 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | BookEntry: one open order; price-time priority for one side, then the other (AQSE p.21, 3.7.3) |
| length | 1 | 1 | u8 | 25 |
| seqNo | 2 | 4 | u32 | Not described for the snapshot feed |
| securityID | 6 | 2 | u16 | Numeric identifier of the security |
| Side | 8 | 1 | u8 | 1 buy, 2 sell |
| quantity | 9 | 4 | u32 | Open shares |
| Price | 13 | 8 | price | Price of the order |
| orderRef | 21 | 4 | u32 | Unique order reference number for the day |

#### replay::to_aqse

##### Login (type `13` (0xD), 26 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | Login: first message on a replay connection (AQSE p.21, 3.8.1) |
| length | 1 | 1 | u8 | 26 |
| seqNo | 2 | 4 | u32 | 0; ignored by AQSE (AQSE p.21, 3.8) |
| username | 6 | 10 | char | As assigned by AQSE |
| password | 16 | 10 | char | As assigned by AQSE |

##### ReplayRequest (type `14` (0xE), 14 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | ReplayRequest: request one message or a range (AQSE p.21-22, 3.8.2) |
| length | 1 | 1 | u8 | 14 |
| seqNo | 2 | 4 | u32 | 0; ignored by AQSE |
| beginSeqNo | 6 | 4 | u32 | Stream seqNo of the first message requested |
| endSeqNo | 10 | 4 | u32 | Stream seqNo of the last message requested |

#### replay::from_aqse

Also decodes the messages of `md::from_aqse`.

##### ReplayResponse (type `15` (0xF), 7 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | code | ReplayResponse: Login accepted, or a Replay Request refused (AQSE p.22, 3.8.3) |
| length | 1 | 1 | u8 | 7 |
| seqNo | 2 | 4 | u32 | 0 |
| responseCode | 6 | 1 | u8 | 0 Login successful, 1 bad beginSeqNo, 2 bad endSeqNo |


## 5. Sequencing and recovery

- **Scope**: one sequence per feed (market split) and trading day, from 1,
  incremented by every message except Heartbeat (MD p.8, 2.1; p.11, 3.3.2). The
  packet count and the first message's seqNo locate dropped packets (MD p.10,
  3.2). Whether MD Bundle Start and End consume numbers is not stated (section
  10, item 2). Snapshot and replay session messages carry seqNo values that MD
  does not describe (snapshot) or sets to 0 (replay, MD p.20, 3.8).
- **Gap rule**: expected = last applied + 1. Lower: duplicate (the other of the
  A/B/C copies, or a replay overlap), dropped. Higher: gap. A heartbeat whose
  seqNo is above expected also opens a gap, since it carries the next expected
  number (MD p.11, 3.3.2).
- **A/B arbitration**: the copies carry identical message sequence numbers
  (MD p.6, 1.1 describes channels of one stream), so the handler takes every
  copy and drops by message seqNo. The kaspar-hft arbitration in `mdp3/` works
  on CME packet sequence numbers, which Aquis packets do not have; nothing was
  reused (section 9).
- **Replay**: Login, then Replay Request (beginSeqNo, endSeqNo); accepted
  requests get the messages with no response; refused ones get Replay Response
  1 (bad begin) or 2 (bad end) (MD p.21, 3.8.2-3.8.3). Limits on range or rate:
  not stated.
- **Snapshot**: every 10 s, per split: Snapshot Start (streamSeqNo,
  securityCount), then per security a Book Status (entries) followed by that
  many Book Entry messages, in price-time priority, one side then the other
  (MD p.8, 2.2; p.19-20, 3.7.1-3.7.3).
- **Alignment rule**: streamSeqNo is "the sequence number of the last message
  in the continuous stream which relates to this snapshot" (MD p.19, 3.7.1);
  section 2.2 says "the sequence number in the continuous stream at the time
  this snapshot was captured" (MD p.8). Reading: the snapshot includes message
  streamSeqNo; buffered messages up to it are dropped and the rest applied
  (section 10, item 3).
- **Handler procedure** (`src/Handler.cpp`):
  1. Live: apply in order. On a gap: `l3_gap_v2_t` to every book; request
     replay of the missing range if it is shorter than `max_replay`, and buffer
     the stream; otherwise go to 3.
  2. Replay: apply replayed messages, then the buffer, in sequence; a further
     hole after the requested range gets its own request; back to live when
     nothing is missing. A refused request or a lost connection goes to 3.
  3. Snapshot: `l3_chr_v2_t` to every book, clear the order table, keep
     buffering. Collect a snapshot; it is complete when securityCount Book
     Status messages and each one's entries have arrived; a Book Status while
     entries are owed, an entry for another security, or a new Snapshot Start
     discards it (the snapshot seqNo is undocumented, so the counts are the only
     completeness check). Apply it (adds with `recovery` = true and a status
     per security), set expected = streamSeqNo + 1, then continue as in 2:
     a hole between the snapshot and the buffer is requested from replay.
  4. A late start (`start_of_day` = false) begins in 3; a start of day expects
     seqNo 1 and replays anything before the first message received.
- **State to clear**: on a snapshot rebuild, the order table, open bundle and
  pending records. Daily: the handler is created per trading day; MD does not
  describe an intraday sequence reset or the sequence across a data-centre
  failover (section 10, item 7). A seqNo of 1 after higher numbers is dropped
  as a duplicate.
- **Exercised by tests**: gaps (packet loss on both copies), duplicates and
  reordered late copies, a heartbeat-detected gap, a gap at start of day,
  replay in arbitrary TCP splits, refused replay, lost replay connection, a gap
  above `max_replay`, an incomplete snapshot, a snapshot behind the buffer
  completed by replay, a late start (README.md, Tests).

## 6. Semantics

- **Book type**: market by order, full depth, visible orders only; anonymous
  (MD p.7, 2.1; p.8, 2.2). Order references are assigned by Aquis, unique for
  the day, and refer to the same order every time they appear (MD p.7, 2.1).
- **Order Add**: visible quantity posted; side 1 buy, 2 sell (MD p.12, 3.4.3).
- **Order Cancel**: a visible order removed, by the member, by Aquis, at the
  close or by expiry (MD p.12, 3.4.4).
- **Order Modify**: absolute quantity and price, same orderRef. "On revision
  of quantity down the order retains its position in the book, otherwise the
  book is re-ordered" (MD p.13, 3.4.5). A modify that changes neither price nor
  quantity therefore loses priority by the text (section 10, item 4).
- **Trade**: one per execution. tradeType 1 is against visible quantity and
  carries the resting orderRef; "the traded quantity should be removed from the
  associated order", and a fully traded order removed (MD p.14, 3.4.6): the
  Trade itself changes the book; MD names no Modify or Cancel for it. tradeType
  2 is against hidden or reserve quantity, orderRef 0; an iceberg hit on both
  parts gives a type 1 then a type 2 trade, and the refreshed peak is a new
  Order Add with a new orderRef (same). Other types: 3 MaC, 4 LIS Cross, 5
  Benchmark Cross, 6 AoD, 12 AVM (MD p.14, 3.4.6); "others may be added". No
  side and no aggressor are published; mdFlags bit 0 describes liquidity on the
  passive side (MD p.23, 3.9.2). Segment MIC says which book traded (AQXE/AQEU
  CLOB, AQXD/AQED AMP, AQXA/AQEA AoD, AVXE/AVEU AVM; MD p.9, 2.4).
- **Statistical inclusion**: every trade type counts in total volume and value;
  only CLOB and MaC set open and close; LIS, Benchmark and AVM do not set high
  and low (MD p.9, 2.4).
- **Trade Bust**: an erroneous trade, by tradeRef (MD p.14, 3.4.7).
- **Security Status**: tradingStatus 1 Active, 2 Halted, 3 Suspended, 8 AoD
  Start, 24 AoD Finish; marketFlags bit 0 continuous trading open, bits 1-2 MaC,
  bits 5-6 MaC+; tradingPhase 0 closed, 1 continuous (MD p.16-17, 3.4.10).
  Halted or suspended: no trading, running auctions cancelled; Active: orders
  only while the market is open (MD p.17, 3.4.10.1). There is no opening or
  closing auction on the lit book; AoD runs during continuous trading (MD p.17,
  3.5).
- **Auctions**: AoD Update gives indicative price and volume; 0 and 0 after
  uncrossing (MD p.17, 3.5.1). MaC Update gives indicative price and buy and
  sell totals (MD p.18, 3.6.1); MaC Book Entry the first 5 valid best orders
  per side during the MaC (MD p.20, 3.7.4).
- **Bundles**: Bundle Start before and Bundle End after the messages
  "generated within the same sequencer cycle" (MD p.11, 3.4.1-3.4.2). Whether
  every message is inside a bundle, or only groups, is not stated.
- **Message order within an event** (an aggressive order trading then resting):
  not stated (section 10, item 16).
- **Implied, hidden**: no implied orders; hidden and reserve quantity appear
  only in type 2 trades.

## 7. Mapping to Kaspar

By `.claude/skills/build-feed-handler/kaspar_mapping.md`; records of
`chutil/include/bfile/r_l3.hpp`, built in `src/Handler.cpp`. OB, TachBook and the
record format are unchanged. The handler keeps one order table per feed:
orderRef → (Kaspar securityID, side, price, quantity).

### Messages and records

| Venue message | Records, in order | Notes |
|---|---|---|
| Order Add (2) | `l3_mbo_v2_t` action 0: `side` `'0'` for 1, `'1'` for 2; `pxd` = price / 10^5; `displayQty` = quantity; `priority` = seqNo | securities outside the universe are dropped with their later messages. An Add for a live orderRef deletes the old order first (counted) |
| Order Modify (4), same price, smaller quantity | `l3_mbo_v2_t` action 1 | venue and OB both keep priority |
| Order Modify, new price or larger quantity | action 1 | both reset priority |
| Order Modify, same price and quantity | action 2 then action 0, same `orderID`, same `transactTime`, `endOfEvent` on the add | the venue re-orders where OB would keep priority (section 10, item 4); counted |
| Order Modify, quantity 0 | action 2 | not described by MD |
| Order Cancel (3) | action 2 | an unknown orderRef is counted and ignored |
| Trade (5), tradeType 1, orderRef in the table | `l3_mbp_trd_t` (print), `l3_mbo_trd_v2_t` (`orderID`, `lastQty`), then action 1 with the remaining quantity, or action 2 at 0 | one `transactTime`; `endOfEvent` on the last. A quantity above the order's is clamped and counted |
| Trade, any other type, or orderRef 0 | `l3_mbp_trd_t` only | hidden, reserve, MaC, crosses, AoD, AVM (section 10, items 5, 6) |
| Trade Bust (6) | none, counted | a print cannot be withdrawn: known deviation (section 10, item 11) |
| Security Status (9) | `l3_sst_t` (trading status below) | |
| Security Definition (8), Tick Table Data (7) | none | update the instrument loader |
| AoD Update (17), MaC Update (16) | none | no book or trade content |
| MD Bundle Start (34), End (35) | none | event boundary, below |
| Heartbeat (1) | none | gap check only |
| end of a packet | `l3_eop_t`: `msgSeqNum` = the first message's seqNo, `sendingTime` = the last pubTimestamp in the packet | to the recorder only, as the CME handler does |
| gap | `l3_gap_v2_t` to every book of the feed | |
| snapshot rebuild | `l3_chr_v2_t` to every book, then per security `l3_sst_t` and one action-0 add per Book Entry, `recovery` = true, `priority` = position in the snapshot | |
| Snapshot Start, Book Status, Book Entry, MaC Book Entry | as above; MaC Book Entry none | |

`order_flags` stays 0. `sendingTime` = pubTimestamp (MD 4.2), or
`transactTime` where the message has none.

### Trade prints and volume

One `l3_mbp_trd_t` per Trade message of a routed security, whatever its
tradeType, so volume counts every trade, as MD p.9, 2.4 counts every type in
total volume (section 10, item 6). `sz` = quantity, `pxd` = the trade's
price. `aggresorSide` (CME values) on a type 1 trade: the side opposite the
resting order (1 buy when the resting order sells); 0 on every other trade,
since Aquis publishes no side. `side` = the resting order's side, or 0.

### Order identifiers and instruments

- `orderID` = orderRef (u32), unchanged. Unique for the day (MD p.7, 2.1); one
  order table per handler instance (one feed), so ids from other feeds or from
  the other venue never meet in it. `l3_mbo_trd_v2_t` is routed to the book of
  the order it executes.
- `securityID` = the Kaspar asset id the instrument loader maps the Aquis
  securityID to, through the security's UMTF (section 8).

### Quantities and prices

- Quantities are whole shares; Aquis lot size is always 1 (MD p.16, 3.4.9).
  u32 on the wire, `uint32_t` in `displayQty`; `lastQty` and `sz` are
  `int32_t`: a single trade above 2^31 shares would not fit (not seen as
  possible on these markets; not checked).
- `pxd` = price / 100000. Prices are unsigned (MD p.10, 3.1) and equities trade
  above zero, so every security is routed to OB. Units are the trading
  currency's: GBX (pence) for most London listings, and GBP, EUR, USD, CHF,
  SEK, NOK, DKK, CZK (SL: 9 currencies on AQXE, 7 on AQEU).

### Event boundaries

MD 4.2 marks sequencer cycles with Bundle Start and End. Inside a bundle every
record gets the `transactTime` of the bundle's first message, and `endOfEvent`
is set only on the last record before Bundle End; a bundle may span packets.
Outside a bundle `endOfEvent` is set on the last record of each message.
`transactTime` never decreases (`max` with the previous event). The reason for
one `transactTime` per bundle: OB treats the records sharing a `transactTime`
as one transaction and checks the book for a cross when it advances
(`frame_kaspr/src/OB.cpp`, `data_handler`), not at `endOfEvent` (section 10,
item 1).

### Trading status

| Aquis tradingStatus | marketFlags bit 0 | `tradingStatus` (CME SecurityTradingStatus) |
|---|---|---|
| 1 Active, 8 AoD Start, 24 AoD Finish | 1 (continuous open) | 17 ReadyToTrade |
| 1, 8, 24 | 0 | 4 Close |
| 2 Halted | any | 2 TradingHalt |
| 3 Suspended | any | 18 NotAvailableForTrading |
| other | any | 18 |

AoD Start and Finish concern the AoD book; the lit book trades on (MD p.17,
3.5). MaC and MaC+ flags and tradingPhase do not change the mapping.

### Venue codes

`en::x::AQXE` (Aquis UK) and `en::x::AQEU` (Aquis EU), appended after `HKEX` in
`chutil/gencode/enum/def/exch_code.enum`, in `venue` of every record.

### What is not built

- Socket readers: multicast receive (A/B/C) and the replay TCP session (connect,
  Login, write the requests the handler asks for, `on_replay_lost` on failure).
  `encode_login` and `encode_replay_request` produce the bytes. The handler and
  the `AquisFeed` actor take bytes.
- Timeouts: a replay request that is neither answered nor refused leaves the
  handler waiting; the session should call `on_replay_lost` after a timeout.
- MaC feeds, AQSE, configuration file parsing and `kaspr` wiring.

## 8. Instruments

- **Identifier**: u16 securityID, numeric; the security file and Security
  Definition give UMTF, ISIN, currency, MIC of the market of listing, tick table
  (MD p.7, 2.1; p.15-16, 3.4.9). SL: 7,119 AQXE and 4,118 AQEU securities, ids
  1-9,102 and 1-9,101, each id once per file; ids are per venue.
- **Key to Kaspar**: the UMTF. In SL, 1,107 ISINs appear on more than one row
  (one listing per market of listing and currency, e.g. Vodafone GB00BH4HKS39 as
  2003 `VODl` XLON GBX and 4421 `VODId` XETR EUR), and no UMTF repeats. The
  universe file is `umtf,sec_id`.
- **Sources** (`src/Instruments.cpp`): the security file before the open
  (column names from SL's header, quoted fields allowed, a leading byte-order
  mark ignored, `operating_mic` filter); Security Definition and Tick Table
  Data from the feed, pre-market and rarely intraday, which replace the file's
  entry (MD p.7-8, 2.1).
- **Tick**: Tick Table Data bands (threshold, tick size) per tickTableID; the
  tick at a price is the band with the highest threshold not above it (MD
  p.14-15, 3.4.8). SL names tables by string (`ADNT_80_600`), the feed by
  number; the mapping between the two is not documented. Tick sizes reach
  0.0001 (tick CSV), within 5 decimals.
- **Lot**: always 1 (lotSize 1,000,000,000 with 9 decimals, MD p.16, 3.4.9).
- **Test stocks**: flags bit 1 (MD p.15, 3.4.9); they are routed only if the
  universe names them.
- **Lifecycle**: files updated daily for admissions, removals and corporate
  actions (MD p.7, 2.1). AQSE-listed securities trade on Aquis UK from
  28 July 2025 with UMTF suffix `q` and MIC AQSE (MN 2025_0007; SL: 14 rows).

## 9. Pitfalls

### Reading the specification

- Several documents: six editions of MD and two of AQSE found; production MD
  4.2 and AQSE 1.2.3 apply (section 1). The task named MD 2.5 and AQSE 1.2.2,
  both superseded.
- Errata and change tables: MD p.4-5 history; Member Notices give effective
  dates. MN 2026_0008 says timestamp and marketId were added to "the Security
  Status Message"; MD's history and tables put them in Security Definition
  (section 10, item 19).
- Tables across page breaks: Order Modify (p.12-13), Security Definition
  (p.15-16), Security Status (p.16-17), AoD Update (p.17-18); every offset
  checked by `fieldgen.py --check` and against `pdftotext -layout`.
- Offsets 0-based; `length` includes the header (MD p.11, 3.3.1).
- Reserved fields: none as whole fields; reserved bits in flags.
- Enumerations elsewhere: tradeType after the Trade table; MMT and mdFlags in
  3.9 (MD p.22-23).
- Field names: `Side`, `Price`, `Quantity` capitalised in Book Entry and MaC
  Book Entry; kept as written.
- Examples: MD has none; only the data-type example 1462500 = 14.625.
- Two layers in one table: session messages (Login, Replay Request, Replay
  Response) share the header and code space with market data; separate
  sections (`replay::to_aquis`, `replay::from_aquis`).
- Repeated names with different codes: none. AQSE reuses codes 1-17 with other
  layouts; separate table and namespace.
- Variable-length fields: none.
- Odd-width integers: none.
- Stale cross-references: section 10, item 20.

### Framing and encoding

- Little endian throughout; unsigned (MD p.10, 3.1).
- Prices: global 5 implied decimals.
- Sentinels: orderRef 0 on Trade (no visible order); indicativePrice and
  matchVol 0 after an AoD uncross (MD p.17, 3.5.1).
- Strings: left justified, NUL padded (MD p.10, 3.1).
- Several messages per packet; a message never split across packets (each
  packet has a count of whole messages, MD p.10, 3.2); bundles may span packets.
- Timestamps: ns since the Unix epoch, UTC; µs×1000 before 2 March 2026.

### Sequencing and recovery

- Scope: per feed, per day (section 5).
- A/B: duplicates dropped by message seqNo; the `mdp3` arbitration does not
  apply (section 5).
- Gap rule, duplicates: section 5.
- Recovery: replay (request/response), snapshot every 10 s (section 5).
- Snapshot alignment: section 5 and section 10, item 3.
- Snapshot too old for the buffer: the hole is requested from replay; a
  snapshot whose streamSeqNo is below what was already applied cannot happen
  in the snapshot state (the books were cleared).
- State to clear: order table, pending bundle; section 5.
- Recovery paths exercised in tests: section 5.
- Session behaviour: Login, no heartbeats or limits stated for the replay TCP
  connection.

### Book semantics

- MBO, full depth.
- Level-index updates: not applicable.
- Depth limits: none (full depth; MaC Book Entry is top 5, not used).
- Priority on modify: stated (MD p.13, 3.4.5); same price and size: section 10,
  item 4.
- Implied: none. Iceberg: peak visible, reserve in type 2 trades, refresh as a
  new orderRef (MD p.14, 3.4.6).
- Trades as book deltas: the type 1 Trade is the delta; no separate Modify or
  Cancel is described (section 10, item 10).
- Side of a trade: not published; derived from the resting order for type 1.
- Trading states and crossed books: continuous only; no lit auction; section 7.
- Book clears: none published; the handler clears on snapshot rebuild.

### Prices, quantities and instruments

- Price, not yield.
- Quantity multiplier: lot 1.
- Decimals sentinel: lotSizeDecimal always 9.
- Price-dependent ticks: tick tables by threshold.
- Instrument definitions: file and feed; section 8.
- Lifecycle: daily file; intraday Security Definition corrections.

### Behaviour the specification omits

- Subscription options: none (multicast).
- History on subscribe: none; snapshot every 10 s.
- Snapshot is full depth.
- Message order within one event: not stated (section 10, item 16).
- Connection limits on replay: not stated.

### Post-trade, order entry

Not applicable.

## 10. Ambiguities

| # | Question | Reading taken | Alternatives | Resolved by |
|---|---|---|---|---|
| 1 | Which `transactTime` the records of one bundle get | the bundle's first message's timestamp on every record of the bundle | each record its own message's timestamp | owner (review.md 1) |
| 2 | Do Bundle Start and End consume sequence numbers (MD p.11, 3.4.1-3.4.2)? | yes: MD p.10, 3.2 and p.11, 3.3.2 exempt only Heartbeat | no, like Heartbeat; `bundles_sequenced = false` implements it | owner; first test-environment capture (review.md 2) |
| 3 | streamSeqNo: last message included in the snapshot (MD p.19, 3.7.1) or the stream position at capture (MD p.8, 2.2), possibly the next expected | last included; apply buffered > streamSeqNo | apply ≥ streamSeqNo | owner; a snapshot compared with the stream in the test environment (review.md 3) |
| 4 | Modify with unchanged price and quantity | loses priority ("otherwise the book is re-ordered", MD p.13, 3.4.5): delete then add | keeps priority (action 1); or never published | owner (review.md 4) |
| 5 | Does any trade type other than 1 execute a visible order (AoD against lit orders)? | no: orderRef is described for type 1 only (MD p.13, 3.4.6); other types are prints only; a non-zero orderRef of a live order on another type is counted | apply as an execution whenever orderRef names a live order | owner (review.md 5) |
| 6 | Which trades produce prints (and so volume); are MaC trades also on the continuous feed? | every Trade of the continuous feed; MaC feeds not read | exclude crosses, AVM, MaC; read the MaC feeds for MaC prints | owner (review.md 6) |
| 7 | Sequence numbers across a data-centre failover or intraday restart | not described; seqNo 1 after higher numbers dropped as duplicate; one handler per day | treat a reset as a channel reset | owner; Aquis (review.md 7) |
| 8 | Whether to build an AQSE handler | not built; AQSE securities are read from Aquis UK | build one (quotes need a mapping) | owner (review.md 8) |
| 9 | Price change with smaller quantity: kept or lost priority? | lost (a new price is a new queue); OB's rule | kept | MD p.13, 3.4.5 reading; OB agrees |
| 10 | Does Aquis also send Modify or Cancel for an order changed by a type 1 Trade? | no (MD p.14, 3.4.6 tells the recipient to apply it) | yes | the handler's result is the same either way: Modify is absolute and a Cancel of an order already removed is counted and ignored |
| 11 | Trade Bust | no record; the print stays in volume | | known deviation: the records cannot withdraw a print |
| 12 | Snapshot completeness: snapshot seqNo undocumented | counts (securityCount, entries) | | every loss pattern breaks a count (section 5) |
| 13 | Security Status for the market as a whole | per security as published | a market-wide message with another securityID | not routed and ignored; low impact |
| 14 | orderRef scope (feed, venue, Aquis) | unique for the day; one table per feed | | per-feed tables cannot collide |
| 15 | Replay limits (range, rate, connection idle) | none known; `max_replay` = 50,000 | | configuration |
| 16 | Order of messages when an aggressive order trades and rests | unknown; with bundles, OB sees one transaction | Add before Trades with an earlier timestamp | bundles (item 1) make the order irrelevant to the cross check |
| 17 | MaC Book Entry: snapshot feed (MD 3.7.4 is under Snapshot Feed) or MaC feed ("During the MaC phase") | not used | | low impact |
| 18 | Heartbeats on the snapshot feed | not stated; an unknown type there is skipped | | low impact |
| 19 | MN 2026_0008 puts timestamp and marketId in Security Status; MD puts them in Security Definition | MD tables (offsets 65 and 73 of Security Definition; Security Status ends at pubTimestamp, offset 19) | the notice | MD history and both tables agree; a wrong reading would show as `length` 37 on Security Status |
| 20 | Stale cross-references: MD p.18, 3.5.2 "Section 3.4.8.1" (none; 3.4.10.1 meant), 3.5.3 and 3.5.4 "3.4.4" and "3.4.5" (Order Cancel, Modify; 3.4.6 and 3.4.7 meant), 3.6.4 "3.4.8" (Tick Table; 3.4.10 meant); AQSE p.20, 3.7.2 "3.4.13" (Tick Table; 3.4.15 meant), p.17, 3.5.3 "3.4.8" (Trade Report Modify; 3.4.10 meant) | the message of the same name and type code | | low impact; decoding is by type code |
| 21 | `fieldgen.py` cannot express a length that follows the type code | header fields written in every message; the packet reader frames by `length` | | tooling (README.md) |
| 22 | MaC Update has no pubTimestamp while AoD Update gained one (MD p.18, 3.6.1; MN 2026_0008 lists AoD Update only) | as tabled | | low impact; `length` would show it |
| 23 | Version-history dates differ between editions (4.1: "January 2026" on its cover, "October 2025" in MD 4.2; 2.5: "13 Nov 2023" against "Sept 2023") | dates from Member Notices | | low impact |

## 11. Aquis Stock Exchange differences

AQSE 1.2.3 against MD 4.2, from the two tables:

- **Same**: data types, packet structure, header, heartbeat, snapshot and replay
  messages and procedure (AQSE p.9-10, 3.1-3.3; p.20-22, 3.7-3.8); MBO with the
  same Order Add/Cancel/Modify semantics and priority rule (AQSE p.10-11,
  3.4.1-3.4.3).
- **Missing**: mdFlags, pubTimestamp, Segment MIC, bundles, MaC; Security
  Definition ends at tickTableId (34 bytes, UMTF not applicable), Security Status
  at timestamp (18 bytes, AQSE p.16, 3.4.14-3.4.15).
- **Added**: Quote Add/Replace (20) and Quote Cancel (21), market-maker quotes
  keyed by traderID and securityID, with both sides in one message (AQSE p.11-12,
  3.4.4-3.4.5); Trader Definition (23); Trade Report (25), Trade Report Modify
  (26), Trade Report Cancel (27) for off-book reports, modifiable for five
  business days (AQSE p.13-14, 3.4.7-3.4.9); Security Statistics (22), opening
  and closing prices (AQSE p.15, 3.4.11); trade type 8 Trade Capture Report
  (AQSE p.19, 3.6.2).
- **Different**: tradeRef is not unique for the day; trade reference plus
  timestamp is (AQSE p.6, 2.1). marketFlags bit 3 marks pre-open and pre-close
  phases (AQSE p.16-17, 3.4.15). The snapshot covers continuous trading only
  (AQSE p.7, 2.2).
- **Not mapped**: a handler would need a decision on quotes (each a two-sided
  quote that replaces the previous one of that trader: one synthetic order per
  side, replaced by delete and add or changed in place) and on trade reports
  (prints or not). See review.md 8.
