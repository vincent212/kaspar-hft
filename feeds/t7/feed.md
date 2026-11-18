# Feed description: Deutsche Börse T7 Enhanced Order Book Interface (EOBI)

Every entry cites its source. "M" is the EOBI Manual below, cited as (M p.N, section).
"XML" is the EOBI XML Representation of the same build, used as the answer key for the
layouts.

## 1. Venue and documents

| Document | Version | Date | Applies to |
|---|---|---|---|
| T7 Release 15.0, Enhanced Order Book Interface, Manual (M) | Version 1, build 150.522.2.ga-150005020-91, interface version 15.0 | 29 July 2026 | Message layouts, framing, sequencing, recovery, semantics. All T7 markets the manual names: Eurex (derivatives) and the Deutsche Börse cash markets (Xetra, Börse Frankfurt). |
| T7 R.15.0 EOBI XML Representation (`eobi.xml`, `eobi.xsd`, `EOBILayouts.h`) | Version 1, same build number | in the R15.0 package (published with the manual) | Machine-readable layouts and valid values. The change log says new layouts and the full value lists are "always" found there (M p.111, B). Used to check every table in section 4. |

Sources: https://www.cashmarket.deutsche-boerse.com/resource/blob/5409230/2887252247af7d3ba65ccee909ecfb58/data/T7_R.15.0_%20EOBI_Manual_Version_1.pdf and
https://www.cashmarket.deutsche-boerse.com/resource/blob/5409244/658c7505a8446e77dfca94320858a76a/data/T7_R.15.0_%20EOBI_XML_Representation_Version_1.zip
(linked from the Xetra "Release 15.0" page, Market and Reference Data Interfaces).

- One release applies: T7 EOBI has no layout-level backward compatibility between
  releases, and Deutsche Börse may change field behaviour within a release (M p.7, 2).
  The Packet Header TemplateID identifies the release (13002 since R14.1, M p.111, B).
- Change log for R15.0 (M p.111, B): interface version 15.0; text on open order books in
  cash auctions (4.1, 4.8). No layout change from R14.1 is listed; the log warns that
  unlisted changes may still move fields, which is why the XML was used as the key.
- Referenced but not obtained: T7 Functional Reference, T7 EMDI/MDI/RDI Manual (reference
  data, multicast addresses in RDI Product Snapshot), Exchange and Settlement Network
  Access (addresses), T7 ETI Manual (M p.8, 2.3; p.109, A.3-A.4).
- Other T7 markets: the manual names Eurex, Xetra and Börse Frankfurt (M p.1 disclaimer,
  p.7, 2.1). It does not mention BSE India, EEX or any other T7 licensee; nothing in this
  description covers their differences.
- Market-specific behaviour stated by M (cash = Xetra/Börse Frankfurt, derivatives = Eurex):

| Topic | Cash markets | Derivatives | Source |
|---|---|---|---|
| Market orders in the book | visible (OrdType 1, Price empty) | not visible | M p.11, 4 Table 1; p.56, 8.4 |
| Order book during auctions | open or closed by product setup (ClosedBookIndicator); quantities may be shown; imbalance in Auction Clearing Price | closed; only Auction BBO / Clearing Price, no quantities | M p.18-19, 4.8; p.71-73, 8.4 |
| Out of Main Session Trading (Xetra Retail) | exists; Order Mass Delete when leaving it; Order Modify may change price without changing priority time | does not exist | M p.28, 6; p.59, 8.4 |
| Reference price, (auction) price without turnover | Trade Report with LastQty 0 | not used | M p.21, 4.13 |
| Algorithmic trade indicator | set | not used | M p.22, 4.14 |
| Top Of Book | instrument state Trade At Close | post trading to end of day | M p.58, 8.4; p.29, 6 |
| After a restart | last trade price republished as a reference price | Add Complex Instrument republished for all complex instruments | M p.24-25, 4.16 |
| Complex, flexible, scaled simple instruments; TES trades; uncrossing (MatchType 5); synthetic matching | not used | used | M p.20, 4.10-4.11; p.49, 8.3; p.94, 8.6; p.106, A.1 |
| Volatility extension, sold out, boundary prices (HighPx/LowPx), IPO auction, Continuous Auction trading models | used | not used | M p.83-85, 8.5 |

## 2. Transport

- UDP multicast, push only, "Out-of-Band": incremental and snapshot messages on different
  multicast channels (M p.5, 1; p.10, 3.2). No session, logon, request or retransmission
  server; the only recovery is the snapshot channel (M p.23, 4.16).
- Addresses and ports come from T7 RDI / RDF, Product Snapshot: MDBookType 3 (Order
  Depth), MDFeedType `HI` incremental and `HS` snapshot, PrimaryServiceLocationID/SubID
  (service A address/port) and SecondaryServiceLocationID/SubID (service B). They do not
  change during trading hours (M p.10, 3.2; p.109, A.4). The full list is in "Exchange and
  Settlement Network Access" (M p.109, A.3), not obtained.
- A/B: "Live-Live", services A and B identically sequenced (M p.23, 4.16). Even
  PartitionID: A is sent first; odd: B first (M p.10, 3.2). One EOBI channel carries one
  partition only (M p.24, 4.16; p.31, 7.1).
- Datagram size: at most 1372 bytes (MTU) (M p.10, 3.2).
- Functional heartbeat: the Heartbeat message, per product, on the incremental channel
  when a product is quiet; only for products with RDI MarketSegmentStatus Active (M p.23,
  4.15; p.42, 8.2). Interval: "configured", not stated.
- Technical heartbeat (beacon): on every multicast address, on a separate port; variable
  payload beginning 0xC0 0xF8 (FAST Reset); content to be ignored; larger and more frequent
  at weekends (M p.23, 4.15). Not modelled in the table.
- Availability: between product states Start-Of-Day and Post-End-Of-Day; the snapshot
  channels stop once all products are in Post-End-Of-Day (M p.28-29, 6).
- IP TOS/DSCP: packets with an Execution Summary are marked (spread improved, widened,
  moved, LastQty > 1); copied into the Packet Header DSCP field; may change with notice
  (M p.110, A.5).
- Throttles, connection limits: none (receive only).

## 3. Framing

- Little endian throughout (M p.10, 3.2; p.36, 7.4). Messages are fixed size, padded for
  alignment (M p.10, 3.2); `EOBILayouts.h` states 1/2/4/8-byte natural alignment with all
  padding explicit.
- A UDP datagram is a Packet Header followed by one or more messages; one product
  (MarketSegmentID) per datagram (M p.30, 7.1; p.40, 8.2).
- Packet Header and every message start with the MessageHeader: BodyLen (2, unsigned,
  "number of bytes for the message, including this field"), TemplateID (2), MsgSeqNum (4)
  (M p.40, 8.2 and every layout in 8). BodyLen is inclusive: in the table it is the
  `@length_prefix 2 inclusive`, and TemplateID the `@type_code uint 2`, so table offsets
  are M offsets minus 2; each field's description gives the M offset.
- The Packet Header is a message with its own TemplateID (13002) and constant BodyLen 32
  (M p.23, 4.16). It is modelled as a typed message in its own section `pkt::from_t7`,
  not as an untyped struct: the TemplateID changes when the interface changes (up to five
  values reserved, M p.40, 8.2), so decoding it checks that the datagram has the layout
  this table describes; a datagram of another release gives `status::unknown`.
- TemplateID determines the fixed size of the message (M p.12, 4). Four messages end in a
  variable size array whose entry count is a field of the fixed part; BodyLen then counts
  the entries (TradeReversal, MassInstrumentStateChange, AddComplexInstrument,
  InstrumentSummary). Repeating groups are never cut off across messages (M p.31, 7.1;
  p.18, 4.7), but two messages split one logical list across fragments with LastFragment
  (M p.79, p.90).
- Messages are not split across datagrams. One unit of work of the matching engine is one
  datagram, or several when it does not fit; CompletionIndicator is 0 on all but the last,
  1 on the last (M p.31, 7.1). On the snapshot channel CompletionIndicator 1 marks the
  last datagram of a product's snapshot cycle (M p.34, 7.3).
- Data types (M p.36, 7.4; XML DataTypes): PriceType int64 with 8 decimals;
  QuantityType int64 with 4 decimals; UTCTimestamp uint64 ns since the UNIX epoch, UTC;
  Fixed String char array, NUL in the first byte when empty. No-value: signed minimum,
  unsigned all ones, string first byte 0x00. Counter: see section 10.

## 4. Message catalogue

Generated with `fieldgen.py eobi.tbl --markdown`. Offsets are 0-based from TemplateID
(M offset minus 2); sizes exclude BodyLen (M length minus 2). "(ofs N)" is the M offset.
Every structure (31: 1 packet header, 26 messages, 4 group entries) and every field (377,
BodyLen and TemplateID included) matches `eobi.xml` in name, offset, length and
signedness, and every struct size and TemplateID matches `EOBILayouts.h`
(`sizeof` checks compiled against the header).

What the format cannot express:

- Repeating groups: the array is a trailing variable-length `Group` field (decode returns
  it as `tail`/`tail_len`); one entry is an untyped struct in `grp::from_t7`. The count
  field (NoMDEntries, NoRelatedSym, NoLegs) is not tied to the tail by the generator: the
  handler checks `tail_len == count * sizeof(entry)`.
- Fixed-point scale and no-value sentinels are in descriptions only (no per-type scale or
  null in the format).
- Enumerations are in descriptions only.

### pkt::from_t7

#### PacketHeader (type `13002` (0x32CA), 30 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | PacketHeader: start of every datagram; MarketDataReport U20 (p.40-41, 8.2) |
| MsgSeqNum | 2 | 4 | UInt | not used; always 0xFFFFFFFF (p.23, 4.16) (ofs 4) |
| ApplSeqNum | 6 | 4 | UInt | packet sequence number, contiguous per channel across products (ofs 8) |
| MarketSegmentID | 10 | 4 | SInt | product identifier; one product per datagram (ofs 12) |
| PartitionID | 14 | 1 | UInt | grouping of T7 products; even: service A sent first, odd: B first (p.10, 3.2) (ofs 16) |
| CompletionIndicator | 15 | 1 | UInt | 0 Incomplete, 1 Complete: unit of work (incremental) or product snapshot (snapshot) ends here (ofs 17) |
| ApplSeqResetIndicator | 16 | 1 | UInt | 0 No Reset, 1 Reset; set in the first packets after a (re)start (ofs 18) |
| DSCP | 17 | 1 | UInt | copy of DSCP from the IP header, A.5 (ofs 19) |
| Pad4 | 18 | 4 | Pad | not used (ofs 20) |
| TransactTime | 22 | 8 | UTCTimestamp | time the feed handler put the packet on the wire (t_9) (ofs 24) |

### eobi::from_t7

#### Heartbeat (type `13001` (0x32C9), 14 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | Heartbeat: functional heartbeat per quiet product, incremental channel (p.42, 8.2) |
| MsgSeqNum | 2 | 4 | UInt | not used (ofs 4) |
| LastMsgSeqNumProcessed | 6 | 4 | UInt | last MsgSeqNum sent for the product; 0 before Start-Of-Day (p.32, 7.2) (ofs 8) |
| Pad4 | 10 | 4 | Pad | not used (ofs 12) |

#### ExecutionSummary (type `13202` (0x3392), 102 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | ExecutionSummary: one per match event triggered by an incoming order; not sent for matches without one (p.43-45, 8.3; p.17, 4.6) |
| MsgSeqNum | 2 | 4 | UInt | per product across all message types (ofs 4) |
| SecurityID | 6 | 8 | SInt | instrument of the incoming order (ofs 8) |
| RequestTime | 14 | 8 | UTCTimestamp | gateway-in time (t_3n/t_3), optional (ofs 16) |
| ExecID | 22 | 8 | UTCTimestamp | matching timestamp (t_7) (ofs 24) |
| LastQty | 30 | 8 | QuantityType | total executed quantity of the match event (ofs 32) |
| AggressorSide | 38 | 1 | UInt | 1 buy, 2 sell; empty for Continuous Auction Issuer (ofs 40) |
| Pad1 | 39 | 1 | Pad | not used (ofs 41) |
| TradeCondition | 40 | 2 | UInt | 1 Implied Trade (synthetic match) (ofs 42) |
| TradingHHIIndicator | 42 | 1 | UInt | HHI interval index of the match event, optional (ofs 44) |
| Pad3 | 43 | 3 | Pad | not used (ofs 45) |
| LastPx | 46 | 8 | PriceType | worst price of the match (ofs 48) |
| TrdRegTSPrevTimePriority | 54 | 8 | UTCTimestamp | RemainingOrderDetails: previous priority timestamp of the aggressor (ofs 56) |
| DisplayQty | 62 | 8 | QuantityType | RemainingOrderDetails: remaining quantity to be added, else empty (ofs 64) |
| Price | 70 | 8 | PriceType | RemainingOrderDetails: limit price of the remainder; empty for a disclosed market order (ofs 72) |
| RestingHiddenQty | 78 | 8 | QuantityType | executed or cancelled passive quantity not displayed; empty if TradeCondition = 1 (ofs 80) |
| RestingCxlQty | 86 | 8 | QuantityType | quantity cancelled by SMP in this match event, not part of LastQty (ofs 88) |
| AggressorTime | 94 | 8 | UTCTimestamp | matching-engine-in time (t_5), optional (ofs 96) |

#### QuoteRequest (type `13503` (0x34BF), 38 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | QuoteRequest: request for quote, incremental only (p.46, 8.3) |
| MsgSeqNum | 2 | 4 | UInt | per product (ofs 4) |
| SecurityID | 6 | 8 | SInt | instrument (ofs 8) |
| LastQty | 14 | 8 | QuantityType | requested quantity, optional (ofs 16) |
| Side | 22 | 1 | UInt | 1 buy, 2 sell, optional (ofs 24) |
| Pad7 | 23 | 7 | Pad | not used (ofs 25) |
| TransactTime | 30 | 8 | UTCTimestamp | transaction time (ofs 32) |

#### CrossRequest (type `13502` (0x34BE), 46 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | CrossRequest: cross announcement or liquidity improvement cross, incremental only (p.47-48, 8.3) |
| MsgSeqNum | 2 | 4 | UInt | per product (ofs 4) |
| SecurityID | 6 | 8 | SInt | instrument (ofs 8) |
| LastPx | 14 | 8 | PriceType | price of a Liquidity Improvement Cross, optional (ofs 16) |
| LastQty | 22 | 8 | QuantityType | requested quantity, optional (ofs 24) |
| Side | 30 | 1 | UInt | 1 buy, 2 sell, optional (ofs 32) |
| CrossRequestType | 31 | 1 | UInt | 1 Cross Announcement, 2 Liquidity Improvement Cross (ofs 33) |
| InputSource | 32 | 1 | UInt | 1 CLIP client broker, optional (ofs 34) |
| Pad5 | 33 | 5 | Pad | not used (ofs 35) |
| TransactTime | 38 | 8 | UTCTimestamp | transaction time (ofs 40) |

#### TradeReport (type `13201` (0x3391), 54 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | TradeReport: auction, Xetra retail, manual entry, reference price and price without turnover (LastQty 0) (p.49-51, 8.3; p.21, 4.13) |
| MsgSeqNum | 2 | 4 | UInt | per product (ofs 4) |
| SecurityID | 6 | 8 | SInt | instrument (ofs 8) |
| TransactTime | 14 | 8 | UTCTimestamp | transaction time (ofs 16) |
| LastQty | 22 | 8 | QuantityType | quantity; 0 for reference price and (auction) price without turnover (ofs 24) |
| LastPx | 30 | 8 | PriceType | price (ofs 32) |
| TrdMatchID | 38 | 4 | UInt | match step id, product-wide day-unique; not set for reference price (ofs 40) |
| MatchType | 42 | 1 | UInt | 3 manual, 5 uncrossing, 7 auction, 13 LIC, 14 PWT in continuous auction (ofs 44) |
| MatchSubType | 43 | 1 | UInt | 1 opening, 2 closing, 3 intraday, 4 circuit breaker, 5 IPO, 6 retail auction (ofs 45) |
| AlgorithmicTradeIndicator | 44 | 1 | UInt | 1 algorithmic trade; cash markets only (ofs 46) |
| Pad1 | 45 | 1 | Pad | not used (ofs 47) |
| TradeCondition | 46 | 2 | UInt | 107 k, 155 BB, 156 BC, 206 DA, 596 SA, 624 TC, 743 XR (ofs 48) |
| Pad6 | 48 | 6 | Pad | not used (ofs 50) |

#### TradeReversal (type `13200` (0x3390), 54 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | TradeReversal: full reversal of a previously reported trade by Market Supervision; NoMDEntries MDTradeEntry follow (p.52-53, 8.3; p.21, 4.12) |
| MsgSeqNum | 2 | 4 | UInt | per product (ofs 4) |
| SecurityID | 6 | 8 | SInt | instrument (ofs 8) |
| TransactTime | 14 | 8 | UTCTimestamp | time the reversal was processed (ofs 16) |
| LastQty | 22 | 8 | QuantityType | quantity of the reversed trade (ofs 24) |
| LastPx | 30 | 8 | PriceType | price of the reversed trade (ofs 32) |
| TrdRegTSExecutionTime | 38 | 8 | UTCTimestamp | matching timestamp of the new last trade, optional (ofs 40) |
| TrdMatchID | 46 | 4 | UInt | match step id of the reversed trade (ofs 48) |
| TradeCondition | 50 | 2 | UInt | 155 BB, 206 DA, 624 TC, 743 XR; cash markets only (ofs 52) |
| MDOriginType | 52 | 1 | UInt | 0 Book, 1 Off Book (ofs 54) |
| NoMDEntries | 53 | 1 | Counter | entries in MDTradeEntryGrp, 0..18 (ofs 55) |
| MDTradeEntryGrp | 54 | var | Group | NoMDEntries x grp::MDTradeEntry (24 bytes each) (ofs 56) |

#### TESTradeReport (type `13203` (0x3393), 78 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | TESTradeReport: off-book (T7 Entry Service) trade and volume (p.54-55, 8.3; p.20, 4.11) |
| MsgSeqNum | 2 | 4 | UInt | per product (ofs 4) |
| SecurityID | 6 | 8 | SInt | instrument (ofs 8) |
| TransactTime | 14 | 8 | UTCTimestamp | transaction time (ofs 16) |
| LastQty | 22 | 8 | QuantityType | quantity, optional (ofs 24) |
| LastPx | 30 | 8 | PriceType | price, optional (ofs 32) |
| TrdMatchID | 38 | 4 | UInt | match step id, optional (ofs 40) |
| TrdType | 42 | 2 | UInt | 1 block, 2 EFP, 54 OTC, 55 EBF, 1000 vola, 1001, 1002, 1004, 1006, 1007 (ofs 44) |
| TradeCondition | 44 | 2 | UInt | 107 out of sequence (basket) (ofs 46) |
| MultiLegReportingType | 46 | 1 | UInt | 1 single security, 2 leg of a multi-leg security (ofs 48) |
| MultiLegPriceModel | 47 | 1 | UInt | 0 standard, 1 user defined (ofs 49) |
| Pad6 | 48 | 6 | Pad | not used (ofs 50) |
| NonDisclosedTradeVolume | 54 | 8 | QuantityType | only while TESTradSesStatus = 5 Pre Close (ofs 56) |
| TransBkdTime | 62 | 8 | UTCTimestamp | time of the original trade in an external system (ofs 64) |
| NumberOfBuySides | 70 | 2 | UInt | buy sides of the TES trade (ofs 72) |
| NumberOfSellSides | 72 | 2 | UInt | sell sides of the TES trade (ofs 74) |
| Pad4 | 74 | 4 | Pad | not used (ofs 76) |

#### OrderAdd (type `13100` (0x332C), 54 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | OrderAdd: visible order or quote side added; key SecurityID+Side+TrdRegTSTimePriority (p.56-57, 8.4; p.15, 4.2) |
| MsgSeqNum | 2 | 4 | UInt | per product (ofs 4) |
| RequestTime | 6 | 8 | UTCTimestamp | gateway-in time; not set for self-triggered transactions (ofs 8) |
| SecurityID | 14 | 8 | SInt | instrument (ofs 16) |
| TrdRegTSTimePriority | 22 | 8 | UTCTimestamp | OrderDetails: priority timestamp (ofs 24) |
| DisplayQty | 30 | 8 | QuantityType | OrderDetails: displayed quantity (ofs 32) |
| Side | 38 | 1 | UInt | OrderDetails: 1 buy, 2 sell (ofs 40) |
| OrdType | 39 | 1 | UInt | OrderDetails: 1 market order; cash markets only (ofs 41) |
| HHIIndicator | 40 | 1 | SInt | OrderDetails: HHI interval at the price level; -1 no update (ofs 42) |
| Pad5 | 41 | 5 | Pad | OrderDetails: not used (ofs 43) |
| Price | 46 | 8 | PriceType | OrderDetails: price; optional (ofs 48) |

#### TopOfBook (type `13504` (0x34C0), 62 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | TopOfBook: BBO; derivatives post trading to end of day, cash during Trade At Close (p.58, 8.4) |
| MsgSeqNum | 2 | 4 | UInt | per product (ofs 4) |
| TransactTime | 6 | 8 | UTCTimestamp | transaction time (ofs 8) |
| SecurityID | 14 | 8 | SInt | instrument (ofs 16) |
| BidPx | 22 | 8 | PriceType | bid price, optional (ofs 24) |
| OfferPx | 30 | 8 | PriceType | offer price, optional (ofs 32) |
| BidSize | 38 | 8 | QuantityType | bid quantity, optional (ofs 40) |
| OfferSize | 46 | 8 | QuantityType | offer quantity, optional (ofs 48) |
| NumberOfBuyOrders | 54 | 2 | UInt | only in Trade At Close (ofs 56) |
| NumberOfSellOrders | 56 | 2 | UInt | only in Trade At Close (ofs 58) |
| Pad4 | 58 | 4 | Pad | not used (ofs 60) |

#### OrderModify (type `13101` (0x332D), 86 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | OrderModify: order modified with a new priority timestamp; old key in TrdRegTSPrevTimePriority (p.59-61, 8.4; p.16, 4.4) |
| MsgSeqNum | 2 | 4 | UInt | per product (ofs 4) |
| RequestTime | 6 | 8 | UTCTimestamp | gateway-in time (ofs 8) |
| TrdRegTSPrevTimePriority | 14 | 8 | UTCTimestamp | previous priority timestamp (old key) (ofs 16) |
| PrevPrice | 22 | 8 | PriceType | previous price, optional (ofs 24) |
| PrevDisplayQty | 30 | 8 | QuantityType | previous displayed quantity (ofs 32) |
| SecurityID | 38 | 8 | SInt | instrument (ofs 40) |
| TrdRegTSTimePriority | 46 | 8 | UTCTimestamp | OrderDetails: new priority timestamp (new key) (ofs 48) |
| DisplayQty | 54 | 8 | QuantityType | OrderDetails: new displayed quantity (ofs 56) |
| Side | 62 | 1 | UInt | OrderDetails: 1 buy, 2 sell (ofs 64) |
| OrdType | 63 | 1 | UInt | OrderDetails: 1 market order; cash only (ofs 65) |
| HHIIndicator | 64 | 1 | SInt | OrderDetails: HHI at the new price level; -1 no update (ofs 66) |
| Pad5 | 65 | 5 | Pad | OrderDetails: not used (ofs 67) |
| Price | 70 | 8 | PriceType | OrderDetails: new price, optional (ofs 72) |
| PrevPriceHHIIndicator | 78 | 1 | SInt | HHI at the previous price level if it still exists (ofs 80) |
| Pad7 | 79 | 7 | Pad | not used (ofs 81) |

#### OrderModifySamePriority (type `13106` (0x3332), 70 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | OrderModifySamePriority: order modified without loss of priority, e.g. quantity reduced (p.62-63, 8.4; p.16, 4.4) |
| MsgSeqNum | 2 | 4 | UInt | per product (ofs 4) |
| RequestTime | 6 | 8 | UTCTimestamp | gateway-in time, optional (ofs 8) |
| TransactTime | 14 | 8 | UTCTimestamp | transaction time (ofs 16) |
| PrevDisplayQty | 22 | 8 | QuantityType | previous displayed quantity (ofs 24) |
| SecurityID | 30 | 8 | SInt | instrument (ofs 32) |
| TrdRegTSTimePriority | 38 | 8 | UTCTimestamp | OrderDetails: unchanged priority timestamp (key) (ofs 40) |
| DisplayQty | 46 | 8 | QuantityType | OrderDetails: new displayed quantity (ofs 48) |
| Side | 54 | 1 | UInt | OrderDetails: 1 buy, 2 sell (ofs 56) |
| OrdType | 55 | 1 | UInt | OrderDetails: 1 market order; cash only (ofs 57) |
| HHIIndicator | 56 | 1 | SInt | OrderDetails: HHI; -1 no update (ofs 58) |
| Pad5 | 57 | 5 | Pad | OrderDetails: not used (ofs 59) |
| Price | 62 | 8 | PriceType | OrderDetails: price, optional (ofs 64) |

#### OrderDelete (type `13102` (0x332E), 62 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | OrderDelete: order removed from the book (p.64-65, 8.4; p.16, 4.5) |
| MsgSeqNum | 2 | 4 | UInt | per product (ofs 4) |
| RequestTime | 6 | 8 | UTCTimestamp | gateway-in time; not set for self-triggered deletes (ofs 8) |
| TransactTime | 14 | 8 | UTCTimestamp | transaction time (ofs 16) |
| SecurityID | 22 | 8 | SInt | instrument (ofs 24) |
| TrdRegTSTimePriority | 30 | 8 | UTCTimestamp | OrderDetails: priority timestamp (key) (ofs 32) |
| DisplayQty | 38 | 8 | QuantityType | OrderDetails: former displayed quantity (ofs 40) |
| Side | 46 | 1 | UInt | OrderDetails: 1 buy, 2 sell (ofs 48) |
| OrdType | 47 | 1 | UInt | OrderDetails: 1 market order; cash only (ofs 49) |
| HHIIndicator | 48 | 1 | SInt | OrderDetails: HHI if the level still exists; -1 no update (ofs 50) |
| Pad5 | 49 | 5 | Pad | OrderDetails: not used (ofs 51) |
| Price | 54 | 8 | PriceType | OrderDetails: former price, optional (ofs 56) |

#### OrderMassDelete (type `13103` (0x332F), 22 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | OrderMassDelete: the instrument's order book is emptied (p.66, 8.4) |
| MsgSeqNum | 2 | 4 | UInt | per product (ofs 4) |
| SecurityID | 6 | 8 | SInt | instrument whose book is cleared (ofs 8) |
| TransactTime | 14 | 8 | UTCTimestamp | transaction time (ofs 16) |

#### PartialOrderExecution (type `13105` (0x3331), 54 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | PartialOrderExecution: resting visible order partly executed; remaining = book quantity - LastQty (p.67-68, 8.4; p.16, 4.6) |
| MsgSeqNum | 2 | 4 | UInt | per product (ofs 4) |
| Side | 6 | 1 | UInt | 1 buy, 2 sell; side of the resting order (ofs 8) |
| OrdType | 7 | 1 | UInt | 1 market order; cash only (ofs 9) |
| AlgorithmicTradeIndicator | 8 | 1 | UInt | 1 algorithmic; cash only (ofs 10) |
| HHIIndicator | 9 | 1 | SInt | HHI; -1 no update (ofs 11) |
| TrdMatchID | 10 | 4 | UInt | match step id, always set (ofs 12) |
| Price | 14 | 8 | PriceType | price at which the order entered the book (key level) (ofs 16) |
| TrdRegTSTimePriority | 22 | 8 | UTCTimestamp | priority timestamp (key) (ofs 24) |
| SecurityID | 30 | 8 | SInt | instrument (ofs 32) |
| LastQty | 38 | 8 | QuantityType | executed quantity (ofs 40) |
| LastPx | 46 | 8 | PriceType | execution price; differs from Price in uncrossing (ofs 48) |

#### FullOrderExecution (type `13104` (0x3330), 54 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | FullOrderExecution: resting visible order fully executed; remove it (p.69-70, 8.4; p.16, 4.6) |
| MsgSeqNum | 2 | 4 | UInt | per product (ofs 4) |
| Side | 6 | 1 | UInt | 1 buy, 2 sell; side of the resting order (ofs 8) |
| OrdType | 7 | 1 | UInt | 1 market order; cash only (ofs 9) |
| AlgorithmicTradeIndicator | 8 | 1 | UInt | 1 algorithmic; cash only (ofs 10) |
| HHIIndicator | 9 | 1 | SInt | HHI if the level still exists; -1 no update (ofs 11) |
| TrdMatchID | 10 | 4 | UInt | match step id, always set (ofs 12) |
| Price | 14 | 8 | PriceType | price at which the order entered the book (key level) (ofs 16) |
| TrdRegTSTimePriority | 22 | 8 | UTCTimestamp | priority timestamp (key) (ofs 24) |
| SecurityID | 30 | 8 | SInt | instrument (ofs 32) |
| LastQty | 38 | 8 | QuantityType | executed quantity (ofs 40) |
| LastPx | 46 | 8 | PriceType | execution price; differs from Price in uncrossing (ofs 48) |

#### AuctionBestBidOffer (type `13500` (0x34BC), 62 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | AuctionBestBidOffer: BBO of an uncrossed book during an auction (p.71-72, 8.4; p.18, 4.8) |
| MsgSeqNum | 2 | 4 | UInt | per product (ofs 4) |
| TransactTime | 6 | 8 | UTCTimestamp | timestamp of the book entry (ofs 8) |
| SecurityID | 14 | 8 | SInt | instrument (ofs 16) |
| BidPx | 22 | 8 | PriceType | bid price, optional (ofs 24) |
| OfferPx | 30 | 8 | PriceType | offer price, optional (ofs 32) |
| BidSize | 38 | 8 | QuantityType | cash only (ofs 40) |
| OfferSize | 46 | 8 | QuantityType | cash only (ofs 48) |
| PotentialSecurityTradingEvent | 54 | 1 | UInt | 0 none; cash only (ofs 56) |
| BidOrdType | 55 | 1 | UInt | 1 market orders on the bid when BidPx is empty; cash only (ofs 57) |
| OfferOrdType | 56 | 1 | UInt | 1 market orders on the offer when OfferPx is empty; cash only (ofs 58) |
| Pad5 | 57 | 5 | Pad | not used (ofs 59) |

#### AuctionClearingPrice (type `13501` (0x34BD), 54 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | AuctionClearingPrice: potential auction price of a crossed book (p.73-74, 8.4; p.18, 4.8) |
| MsgSeqNum | 2 | 4 | UInt | per product (ofs 4) |
| TransactTime | 6 | 8 | UTCTimestamp | transaction time (ofs 8) |
| SecurityID | 14 | 8 | SInt | instrument (ofs 16) |
| LastPx | 22 | 8 | PriceType | potential auction price (ofs 24) |
| LastQty | 30 | 8 | QuantityType | executable quantity; cash only (ofs 32) |
| ImbalanceQty | 38 | 8 | QuantityType | surplus; cash only (ofs 40) |
| SecurityTradingStatus | 46 | 1 | UInt | 7 market imbalance buy, 8 sell; cash only (ofs 48) |
| PotentialSecurityTradingEvent | 47 | 1 | UInt | 0 none, 10 volatility extension; cash only (ofs 49) |
| Pad6 | 48 | 6 | Pad | not used (ofs 50) |

#### ProductStateChange (type `13300` (0x33F4), 22 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | ProductStateChange: product trading state; not sent for IPS products (p.75-76, 8.5; p.19, 4.9) |
| MsgSeqNum | 2 | 4 | UInt | per product (ofs 4) |
| TradingSessionID | 6 | 1 | UInt | 1 day, 3 morning, 5 evening, 6 after hours, 7 holiday (ofs 8) |
| TradingSessionSubID | 7 | 1 | UInt | 1 pre trading, 3 continuous, 4 closing, 5 post trading, 7 quiescent, 10 out of main session (ofs 9) |
| TradSesStatus | 8 | 1 | UInt | 1 halted, 2 open, 3 closed (ofs 10) |
| MarketCondition | 9 | 1 | UInt | 0 normal, 1 stressed (ofs 11) |
| FastMarketIndicator | 10 | 1 | UInt | 0 no, 1 yes (ofs 12) |
| TESTradSesStatus | 11 | 1 | UInt | 1 halted, 2 open, 3 closed, 5 pre close (ofs 13) |
| Pad2 | 12 | 2 | Pad | not used (ofs 14) |
| TransactTime | 14 | 8 | UTCTimestamp | transaction time (ofs 16) |

#### MassInstrumentStateChange (type `13302` (0x33F6), 30 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | MassInstrumentStateChange: state of all instruments of one type in a product, with an exception list; NoRelatedSym SecMassStat follow (p.77-82, 8.5) |
| MsgSeqNum | 2 | 4 | UInt | per product (ofs 4) |
| InstrumentScopeProductComplex | 6 | 1 | UInt | instrument type affected, 1..14 (ofs 8) |
| SecurityMassStatus | 7 | 1 | UInt | 1 active, 2 inactive, 4 expired, 6, 7, 9 suspended, 11, 12 (ofs 9) |
| SecurityMassTradingStatus | 8 | 1 | UInt | 2 halt, 200..221; list p.81 (ofs 10) |
| MassMarketCondition | 9 | 1 | UInt | 0 normal, 1 stressed (ofs 11) |
| FastMarketIndicator | 10 | 1 | UInt | 0 no, 1 yes (ofs 12) |
| SecurityMassTradingEvent | 11 | 1 | UInt | 10, 11 volatility extension; cash only (ofs 13) |
| MassSoldOutIndicator | 12 | 1 | UInt | 1 sold out; Continuous Auction Issuer only (ofs 14) |
| TESSecurityMassStatus | 13 | 1 | UInt | 1 active, 2 inactive, 4 expired, 9 suspended (ofs 15) |
| TransactTime | 14 | 8 | UTCTimestamp | transaction time (ofs 16) |
| LastFragment | 22 | 1 | UInt | 0 N, 1 Y: exception lists of fragments up to Y are joined before applying (ofs 24) |
| NoRelatedSym | 23 | 1 | Counter | entries in SecMassStatGrp, 0..24 (ofs 25) |
| Pad6 | 24 | 6 | Pad | not used (ofs 26) |
| SecMassStatGrp | 30 | var | Group | NoRelatedSym x grp::SecMassStat (32 bytes each) (ofs 32) |

#### InstrumentStateChange (type `13301` (0x33F5), 54 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | InstrumentStateChange: state of one instrument; also intraday expiry (p.83-87, 8.5) |
| MsgSeqNum | 2 | 4 | UInt | per product (ofs 4) |
| SecurityID | 6 | 8 | SInt | instrument (ofs 8) |
| SecurityStatus | 14 | 1 | UInt | 1 active, 2 inactive, 4 expired, 6, 7, 9 suspended, 11 pending deletion, 12 (ofs 16) |
| SecurityTradingStatus | 15 | 1 | UInt | 2 halt, 200 closed, 201 restricted, 202 book, 203 continuous, 204..221; list p.86 (ofs 17) |
| MarketCondition | 16 | 1 | UInt | 0 normal, 1 stressed (ofs 18) |
| FastMarketIndicator | 17 | 1 | UInt | 0 no, 1 yes (ofs 19) |
| SecurityTradingEvent | 18 | 1 | UInt | 10, 11 volatility extension; cash only (ofs 20) |
| SoldOutIndicator | 19 | 1 | UInt | 1 sold out; Continuous Auction Issuer only (ofs 21) |
| Pad2 | 20 | 2 | Pad | not used (ofs 22) |
| HighPx | 22 | 8 | PriceType | upper boundary price; cash only (ofs 24) |
| LowPx | 30 | 8 | PriceType | lower boundary price; cash only (ofs 32) |
| TransactTime | 38 | 8 | UTCTimestamp | transaction time (ofs 40) |
| TESSecurityStatus | 46 | 1 | UInt | 1 active, 2 inactive, 4 expired, 9 suspended (ofs 48) |
| Pad7 | 47 | 7 | Pad | not used (ofs 49) |

#### AddComplexInstrument (type `13400` (0x3458), 78 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | AddComplexInstrument: complex instrument created or modified; NoLegs InstrmtLeg follow (p.88-91, 8.6; p.20, 4.10) |
| MsgSeqNum | 2 | 4 | UInt | per product (ofs 4) |
| SecurityID | 6 | 8 | SInt | complex instrument (ofs 8) |
| TransactTime | 14 | 8 | UTCTimestamp | transaction time (ofs 16) |
| SecurityDesc | 22 | 40 | FixedString | description (ofs 24) |
| SecuritySubType | 62 | 4 | SInt | strategy type, optional (ofs 64) |
| ProductComplex | 66 | 1 | UInt | 2..14, see p.89 (ofs 68) |
| ImpliedMarketIndicator | 67 | 1 | UInt | 0 not implied, 3 implied in/out (ofs 69) |
| QuantityScalingFactor | 68 | 2 | UInt | optional (ofs 70) |
| LegRatioMultiplier | 70 | 4 | UInt | common leg multiple for volatility strategies, optional (ofs 72) |
| NoLegs | 74 | 1 | Counter | entries in InstrmtLegGrp, 2..20 (ofs 76) |
| Pad2 | 75 | 2 | Pad | padding (ofs 77) |
| LastFragment | 77 | 1 | UInt | 0 N, 1 Y: legs of fragments up to Y are joined; N only for commodity strips (ofs 79) |
| InstrmtLegGrp | 78 | var | Group | NoLegs x grp::InstrmtLeg (40 bytes each) (ofs 80) |

#### AddFlexibleInstrument (type `13401` (0x3459), 94 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | AddFlexibleInstrument: flexible option or future created (p.92-93, 8.6) |
| MsgSeqNum | 2 | 4 | UInt | per product (ofs 4) |
| SecurityID | 6 | 8 | SInt | instrument (ofs 8) |
| TransactTime | 14 | 8 | UTCTimestamp | transaction time (ofs 16) |
| SecurityDesc | 22 | 40 | FixedString | description (ofs 24) |
| SecurityType | 62 | 1 | UInt | 1 option, 2 future (ofs 64) |
| PutOrCall | 63 | 1 | UInt | 0 put, 1 call (ofs 65) |
| ExerciseStyle | 64 | 1 | UInt | 0 European, 1 American (ofs 66) |
| SettlMethod | 65 | 1 | UInt | 0 cash, 1 physical (ofs 67) |
| MaturityDate | 66 | 4 | UInt | YYYYMMDD (ofs 68) |
| StrikePrice | 70 | 8 | PriceType | strike, optional (ofs 72) |
| OptAttribute | 78 | 4 | UInt | optional (ofs 80) |
| ContractDate | 82 | 4 | UInt | YYYYMMDD (ofs 84) |
| SecurityReferenceDataSupplement | 86 | 4 | UInt | YYYYMMDD, same as ContractDate (ofs 88) |
| Pad4 | 90 | 4 | Pad | not used (ofs 92) |

#### AddScaledSimpleInstrument (type `13402` (0x345A), 78 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | AddScaledSimpleInstrument: scaled simple instrument created; derivatives only (p.94-95, 8.6) |
| MsgSeqNum | 2 | 4 | UInt | per product (ofs 4) |
| SecurityID | 6 | 8 | SInt | instrument (ofs 8) |
| TransactTime | 14 | 8 | UTCTimestamp | transaction time (ofs 16) |
| SecurityDesc | 22 | 40 | FixedString | description (ofs 24) |
| SecurityType | 62 | 1 | UInt | 1 option, 2 future (ofs 64) |
| Pad1 | 63 | 1 | Pad | not used (ofs 65) |
| QuantityScalingFactor | 64 | 2 | UInt | scaling factor (ofs 66) |
| Pad4 | 66 | 4 | Pad | not used (ofs 68) |
| RelatedSecurityID | 70 | 8 | SInt | RelatedInstrumentGrp: the instrument it scales (ofs 72) |

#### ProductSummary (type `13600` (0x3520), 22 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | ProductSummary: first message of a product's snapshot cycle; snapshot channel (p.96-97, 8.7; p.35, 7.3) |
| MsgSeqNum | 2 | 4 | UInt | per snapshot cycle per channel, 0 for the first message (ofs 4) |
| LastMsgSeqNumProcessed | 6 | 4 | UInt | last incremental MsgSeqNum of the product included in this snapshot (ofs 8) |
| TradingSessionID | 10 | 1 | UInt | as ProductStateChange; not set for IPS (ofs 12) |
| TradingSessionSubID | 11 | 1 | UInt | as ProductStateChange; not set for IPS (ofs 13) |
| TradSesStatus | 12 | 1 | UInt | 1 halted, 2 open, 3 closed (ofs 14) |
| MarketCondition | 13 | 1 | UInt | 0 normal, 1 stressed (ofs 15) |
| FastMarketIndicator | 14 | 1 | UInt | 0 no, 1 yes (ofs 16) |
| TESTradSesStatus | 15 | 1 | UInt | 1, 2, 3, 5 (ofs 17) |
| Pad6 | 16 | 6 | Pad | not used (ofs 18) |

#### InstrumentSummary (type `13601` (0x3521), 62 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | InstrumentSummary: state and trade statistics of one instrument, before its Snapshot Orders; NoMDEntries MDInstrumentEntry follow (p.98-104, 8.7) |
| MsgSeqNum | 2 | 4 | UInt | per snapshot cycle per channel (ofs 4) |
| SecurityID | 6 | 8 | SInt | instrument of the Snapshot Orders that follow (ofs 8) |
| LastUpdateTime | 14 | 8 | UTCTimestamp | last update of the book (ofs 16) |
| TrdRegTSExecutionTime | 22 | 8 | UTCTimestamp | last matching timestamp, optional (ofs 24) |
| TotNoOrders | 30 | 2 | Counter | Snapshot Orders that follow for this instrument (ofs 32) |
| SecurityStatus | 32 | 1 | UInt | as InstrumentStateChange (ofs 34) |
| SecurityTradingStatus | 33 | 1 | UInt | as InstrumentStateChange (ofs 35) |
| MarketCondition | 34 | 1 | UInt | 0 normal, 1 stressed (ofs 36) |
| FastMarketIndicator | 35 | 1 | UInt | 0 no, 1 yes (ofs 37) |
| SecurityTradingEvent | 36 | 1 | UInt | 10, 11; cash only (ofs 38) |
| SoldOutIndicator | 37 | 1 | UInt | 1 sold out (ofs 39) |
| HighPx | 38 | 8 | PriceType | upper boundary price; cash only (ofs 40) |
| LowPx | 46 | 8 | PriceType | lower boundary price; cash only (ofs 48) |
| ProductComplex | 54 | 1 | UInt | 1 simple .. 14 (ofs 56) |
| NoMDEntries | 55 | 1 | Counter | entries in MDInstrumentEntryGrp, 0..18 (ofs 57) |
| TESSecurityStatus | 56 | 1 | UInt | 1, 2, 4, 9 (ofs 58) |
| Pad5 | 57 | 5 | Pad | not used (ofs 59) |
| MDInstrumentEntryGrp | 62 | var | Group | NoMDEntries x grp::MDInstrumentEntry (32 bytes each) (ofs 64) |

#### SnapshotOrder (type `13602` (0x3522), 38 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | SnapshotOrder: one visible order; instrument from the preceding InstrumentSummary (p.105, 8.7) |
| MsgSeqNum | 2 | 4 | UInt | per snapshot cycle per channel (ofs 4) |
| TrdRegTSTimePriority | 6 | 8 | UTCTimestamp | OrderDetails: priority timestamp (key) (ofs 8) |
| DisplayQty | 14 | 8 | QuantityType | OrderDetails: displayed quantity (ofs 16) |
| Side | 22 | 1 | UInt | OrderDetails: 1 buy, 2 sell (ofs 24) |
| OrdType | 23 | 1 | UInt | OrderDetails: 1 market order; cash only (ofs 25) |
| HHIIndicator | 24 | 1 | SInt | OrderDetails: HHI index > 0; -1 no update (ofs 26) |
| Pad5 | 25 | 5 | Pad | OrderDetails: not used (ofs 27) |
| Price | 30 | 8 | PriceType | OrderDetails: price, optional (ofs 32) |

### grp::from_t7

#### MDTradeEntry (type none, 24 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MDEntryPx | 0 | 8 | PriceType | price (ofs 56) |
| MDEntrySize | 8 | 8 | QuantityType | quantity (ofs 64) |
| MDEntryType | 16 | 1 | UInt | 2 trade, 4 opening, 5 closing, 7 high, 8 low (ofs 72) |
| Pad7 | 17 | 7 | Pad | not used (ofs 73) |

#### SecMassStat (type none, 32 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| SecurityID | 0 | 8 | SInt | instrument (ofs 32) |
| HighPx | 8 | 8 | PriceType | as InstrumentStateChange (ofs 40) |
| LowPx | 16 | 8 | PriceType | as InstrumentStateChange (ofs 48) |
| SecurityStatus | 24 | 1 | UInt | as InstrumentStateChange (ofs 56) |
| SecurityTradingStatus | 25 | 1 | UInt | as InstrumentStateChange (ofs 57) |
| MarketCondition | 26 | 1 | UInt | 0 normal, 1 stressed (ofs 58) |
| SecurityTradingEvent | 27 | 1 | UInt | 10, 11 (ofs 59) |
| SoldOutIndicator | 28 | 1 | UInt | 1 sold out (ofs 60) |
| TESSecurityStatus | 29 | 1 | UInt | 1, 2, 4, 9 (ofs 61) |
| Pad2 | 30 | 2 | Pad | not used (ofs 62) |

#### InstrmtLeg (type none, 40 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| LegSymbol | 0 | 4 | SInt | product of the leg (ofs 80) |
| Pad4 | 4 | 4 | Pad | not used (ofs 84) |
| LegSecurityID | 8 | 8 | SInt | instrument of the leg (ofs 88) |
| LegPrice | 16 | 8 | PriceType | underlying leg price, optional (ofs 96) |
| LegRatioQty | 24 | 4 | SInt | leg ratio (ofs 104) |
| LegSecurityType | 28 | 1 | UInt | 1 multi-leg, 2 underlying leg (ofs 108) |
| LegSide | 29 | 1 | UInt | 1 buy, 2 sell (ofs 109) |
| RelatedPriceType | 30 | 1 | UInt | 1 basis, 2 custom underlying price (ofs 110) |
| TradingStyle | 31 | 1 | UInt | 1 regular, 2 TRF TAC, 3 TRF TAM, 4 MOC (ofs 111) |
| RelatedPrice | 32 | 8 | PriceType | price given by RelatedPriceType (ofs 112) |

#### MDInstrumentEntry (type none, 32 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MDEntryPx | 0 | 8 | PriceType | price (ofs 64) |
| MDEntrySize | 8 | 8 | QuantityType | quantity (ofs 72) |
| MDOriginType | 16 | 1 | UInt | 0 book, 1 off book (ofs 80) |
| MDEntryType | 17 | 1 | UInt | 2 trade, 4, 5, 7, 8, 66 volume, 101 prev close, 200..205 auctions (ofs 81) |
| TradeCondition | 18 | 2 | UInt | 107, 155, 624, 743 (ofs 82) |
| TrdType | 20 | 2 | UInt | as TESTradeReport (ofs 84) |
| MultiLegReportingType | 22 | 1 | UInt | 1, 2 (ofs 86) |
| MultiLegPriceModel | 23 | 1 | UInt | 0, 1 (ofs 87) |
| NonDisclosedTradeVolume | 24 | 8 | QuantityType | only during TES Pre Close (ofs 88) |


## 5. Sequencing and recovery

**Sequence numbers**

| Number | Where | Scope | Source |
|---|---|---|---|
| ApplSeqNum | Packet Header | per EOBI channel (one partition), across products; contiguous, +1 per datagram; identical on A and B | M p.11, 4; p.23-24, 4.16; p.31, 7.1 |
| MsgSeqNum (incremental) | MessageHeader | per product (MarketSegmentID), across all message types; contiguous | M p.12, 4; p.31, 7.1 |
| MsgSeqNum (snapshot) | MessageHeader | per snapshot cycle per channel: 0 for the first message of a cycle, +1 for every message across all products | M p.34, 7.3; p.96, 8.7 |
| MsgSeqNum in Packet Header | Packet Header | not used, always 0xFFFFFFFF | M p.23, 4.16 |
| LastMsgSeqNumProcessed | Heartbeat; Product Summary | last incremental MsgSeqNum of the product (0 before Start-Of-Day in a Heartbeat) | M p.32, 7.2; p.35, 7.3 |

**A/B and duplicates.** A datagram is identified by its first 12 bytes (BodyLen,
TemplateID, the constant MsgSeqNum 0xFFFFFFFF, ApplSeqNum); ApplSeqNum detects duplicates
and gaps (M p.23-24, 4.16). Arbitration: reuse the existing A/B implementation.

**Gap rule.** A datagram whose ApplSeqNum is not last+1 on both services is lost. Which
products it carried is not known from the channel; per product, a MsgSeqNum that is not
last+1 (or a Heartbeat whose LastMsgSeqNumProcessed exceeds the last seen MsgSeqNum)
identifies the products that lost data. Recovery is per product (M p.23, 4.16).
Products whose MsgSeqNum stays contiguous lost nothing (reading, section 10).

**Recovery: snapshot channel** (M p.13, 4.1; p.23-24, 4.16; p.33-35, 7.3).

1. Keep receiving and buffering the product's incremental messages.
2. Join the snapshot channel. A product's cycle is: Product Summary, then for each
   instrument an Instrument Summary followed by its Snapshot Orders (TotNoOrders of them,
   zig-zag by price level: bid, offer at each level, time priority within a level),
   ending with the datagram whose CompletionIndicator is 1. During an auction Auction BBO
   or Auction Clearing Price may replace the orders; Top Of Book is also present from
   post trading to end of day (M p.35, 7.3). The next Product Summary starts the next
   product.
3. Rebuild every instrument of the product from the snapshot (the whole product: the
   sequence number is per product and synthetic prices depend on all its books, M p.23,
   4.16).
4. Apply buffered incremental messages with MsgSeqNum > LastMsgSeqNumProcessed of the
   Product Summary; drop those at or below it.

Not in the snapshot (M p.15, 4.1): cross requests, quote requests, intraday-created
complex instruments (their books are in the snapshot; the definitions come from T7 RDI),
manually entered trades. A missed Add Complex Instrument must be recovered from T7 RDI
(M p.20, 4.10).

Snapshot too old: the alignment rule needs the buffer to hold every incremental message
after LastMsgSeqNumProcessed; if the first buffered MsgSeqNum is higher than
LastMsgSeqNumProcessed+1, wait for the next cycle (cycle period not stated).

**Exchange restart** (M p.24, 4.16): a failure always means a full restart of the service:
ApplSeqNum restarts at 1, every product's MsgSeqNum restarts at 1, and
ApplSeqResetIndicator is 1 in the first packets. A lower-or-equal ApplSeqNum on a
multicast address, from a new sender with Packet Header TransactTime t0, means a restart;
packets from the old sender all have TransactTime < t0. Packets may arrive out of order
during fail-over; wait for the first message after the restart before concluding.
Then rebuild from the new incremental sequence or from the snapshot. After a restart, cash
markets republish last trade prices as reference prices and derivatives republish Add
Complex Instrument for all complex instruments (M p.24-25, 4.16).

**State to clear** on restart or on a product rebuild: the product's books, the
(SecurityID, Side, TrdRegTSTimePriority) order map, per-product MsgSeqNum, partially
received multi-datagram units of work, partially joined LastFragment lists, the current
snapshot instrument.

## 6. Semantics

- **MBO**, full depth, all visible orders and quote sides (M p.6, 2; p.11, 4). Not
  visible: un-triggered stops, IOC, rejected orders, derivatives market orders,
  auction-only orders in closed-book auctions (M p.11, Table 1). Hidden quantity appears
  only as RestingHiddenQty in the Execution Summary (M p.43, 8.3).
- **Order key**: (SecurityID, Side, TrdRegTSTimePriority); no order ID on the feed
  (M p.12, 4; p.15, 4.2-4.3). TrdRegTSTimePriority is unique per product (M p.27, Table 3).
- **Add**: Order Add for each visible order entering the book; the unexecuted remainder
  of an incoming order is added after all its executions (M p.15, 4.2; p.17, 4.6). When
  the book opens, all visible orders are published as adds in zig-zag order (M p.14, 4.1;
  p.19, 4.8).
- **Modify** (M p.16, 4.4; p.59-63, 8.4): Order Modify (13101) when the order gets a new
  priority timestamp (price change, quantity increase, or a non-visible change such as an
  OCO stop price, where PrevPrice and PrevDisplayQty equal the new values); old key
  TrdRegTSPrevTimePriority, new key TrdRegTSTimePriority. Order Modify Same Priority
  (13106) when priority is kept (e.g. quantity reduced); the key is unchanged. In Out of
  Main Session Trading an Order Modify may change the price without changing the priority
  time (M p.59).
- **Delete**: Order Delete with the key, former price and quantity (M p.16, 4.5). Order
  Mass Delete empties one instrument's book (M p.66, 8.4); sent at Pre-Trading,
  Post-Trading, Halt, and when leaving Out of Main Session Trading (M p.28-29, 6).
- **Executions** (M p.16-17, 4.6; p.43-45, p.67-70): for a match triggered by an
  incoming order, first one Execution Summary (aggressor instrument, side, total LastQty,
  worst LastPx, remaining quantity and price, hidden and SMP-cancelled quantity), then one
  Partial or Full Order Execution per resting visible order executed. Execution messages
  report resting orders only; the aggressor is not listed. Executions against non-visible
  orders (market orders, synthetic matches) have no execution message (M p.17 footnote 1).
  The book must be maintained from the execution messages; the summary is for fast
  decisions (M p.17; p.43). No Execution Summary for matches not triggered by an incoming
  order (spread uncrossing after an opening auction); there Price (book price) and LastPx
  (match price) differ (M p.17, 4.6). Partial: remaining = book quantity - LastQty; Full:
  remove the order. Each match step (price level) has a TrdMatchID, product-wide
  day-unique (M p.17; p.107, A.2); a synthetic match may give several records with one
  TrdMatchID (M p.107).
- **Order of messages in a unit of work**: Execution Summary, executions, then the
  aggressor's Order Add (M p.17, 4.6; p.43, 8.3); otherwise "no well-defined sending
  order" for incremental messages (M p.32, 7.2).
- **Trade side**: Partial/Full Order Execution Side is the resting order's side;
  Execution Summary AggressorSide is the incoming order's (M p.43, p.67).
- **Trades without book records**: Trade Report for auction trades (published before the
  Instrument State Change that ends the auction), Xetra retail, manual entries by Market
  Supervision, reference prices and prices without turnover (LastQty 0, TrdMatchID not
  set) (M p.13, 4.1; p.19, 4.8; p.21, 4.12-4.13; p.49, 8.3). Trade Reversal for the full
  quantity of an earlier reported trade, with updated statistics (M p.21, 4.12). TES Trade
  Report for off-book trades (M p.20, 4.11).
- **Auctions** (M p.18-19, 4.8): Instrument State Change at entry; then Auction BBO
  (uncrossed) or Auction Clearing Price (crossed), mutually exclusive, no explicit clear
  at the end. Closed book: no order information; at the end, Trade Report(s), Instrument
  State Change, then all visible orders as adds when continuous trading starts.
  Open book (cash): orders stay visible; at the end, Full/Partial Order Executions and
  Trade Reports may both appear, plus adds/modifies/deletes from changed visibility.
- **Crossed book**: legal only as the auction state (Auction Clearing Price); the open
  book in continuous trading is not crossed (not stated explicitly).
- **States** (M p.19, 4.9): product state in TradingSessionSubID (with TradingSessionID
  for Quiescent); halt also TradSesStatus 1; fast market flag; no Product State Change for
  IPS products. Instrument state in SecurityTradingStatus (Instrument State Change); Mass
  Instrument State Change sets the state of all instruments of one type in a product,
  except an exception list joined across LastFragment fragments (M p.77-79, 8.5).
  SecurityStatus distinguishes active, suspended, inactive, expired (M p.20, 4.9).
- **Implied**: synthetic matching for futures spreads etc.; no synthetic prices on the
  feed, derived from the visible books (M p.9, 3.1; p.106, A.1).
- **Statistics**: only on the snapshot (Instrument Summary MDInstrumentEntryGrp); derive
  them from executions on the incremental channel (M p.18, 4.7).
- **Timestamps** (M p.26-27, 5): RequestTime gateway in (t_3n/t_3); AggressorTime
  matching engine in (t_5); TrdRegTSTimePriority, ExecID, TransactTime transaction time
  (t_7); Packet Header TransactTime the EOBI send time (t_9). All ns since epoch UTC.

## 7. Mapping to Kaspar

Records are `bfile::l3_t` (`chutil/include/bfile/r_l3.hpp`), emitted as
`mdp3::handler_if` emits them (`mdp3/include/mdp3/handler_if.hpp`: MBO 655-790, MBO trade
861-923, snapshot order 925-983, status 1074-1127, gap 1129-1156, reset 1158-1195, end of
packet 1262-1280). `orderUpdateAction` 0 new, 1 change, 2 delete; `side` '0' bid, '1'
ask; `pxd` = PriceType / 1e8 (`to_price(mantissa, -8)`); `displayQty` = QuantityType /
1e4; `transactTime` the message's t_7 field; `sendingTime` the Packet Header
TransactTime. OB treats a Change with a larger size or a different price as delete and
add (loses priority) and any other Change as in place (keeps priority)
(`frame_kaspr/src/OB.cpp` 2430-2500). `sim::Mbp2Mbo` is not used (EOBI is MBO). No
`en::x` venue code exists for T7 (`chutil/gencode/enum/def/exch_code.enum`; append one).

Order identity: Kaspar needs a `uint64_t orderID`. Reading taken (section 10):
`orderID = 2 * TrdRegTSTimePriority + (Side - 1)`, keyed with SecurityID; `priority` =
TrdRegTSTimePriority. An Order Modify changes the key, so it changes the orderID.

| Venue message | Kaspar record or message | Notes |
|---|---|---|
| Packet Header | `l3_eop_t` (msgSeqNum = ApplSeqNum, sendingTime = TransactTime) at the end of each datagram | To the recorder only, as `handler_if::EndOfPacket`. ApplSeqNum is 32-bit, as `msgSeqNum`. |
| CompletionIndicator 1 | `endOfEvent = true` on the last record emitted for the datagram | Units of work spanning datagrams end only at Complete. |
| ApplSeqNum or product MsgSeqNum gap | `l3_gap_v2_t` to the books of the affected product(s) | `l3_gap_v2_t` has no securityID; the handler chooses the recipient books (handler_if sends to every book). Then snapshot recovery. |
| restart (ApplSeqNum reset, ApplSeqResetIndicator 1); product rebuild from snapshot | `l3_chr_v2_t` to the books concerned (all books on restart, the product's books on a rebuild); clear the order map for them | As `handler_if::ChannelReset` (#122). |
| Order Add | `l3_mbo_v2_t` action 0 | orderID from the key; pxd, displayQty, side; record (SecurityID, orderID) -> price, qty, side. A cash market order (OrdType 1, Price empty) has no price: section 10. |
| Order Modify (13101, new priority) | `l3_mbo_v2_t` action 2 (old orderID), then action 0 (new orderID, new price/qty) | Not a Change: OB keeps priority on a Change with the same price and a smaller or equal size, but 13101 always means a new priority (OCO stop change: same price and size). |
| Order Modify Same Priority (13106) | `l3_mbo_v2_t` action 1, same orderID | Keeps queue position in OB when price is unchanged and size not increased; anything else is a contradiction to log (section 10). |
| Order Delete | `l3_mbo_v2_t` action 2 | Erase from the map. |
| Order Mass Delete | `l3_mbo_v2_t` action 2 for every order of SecurityID in the map, or `l3_chr_v2_t` to that one book | No per-instrument clear record exists. |
| Partial Order Execution | `l3_mbo_trd_v2_t` (orderID, lastQty), then `l3_mbo_v2_t` action 1 with book qty - LastQty | Trade before book record, as CME and `FillSlate` expect (`frame_kaspr/include/frame/ob/FillSlate.hpp`). All listed orders are resting, so FillSlate classifies them RESTING. The trade record has no price: OB prices it from the book, i.e. Price, not LastPx (differs in uncrossing; section 10). |
| Full Order Execution | `l3_mbo_trd_v2_t`, then `l3_mbo_v2_t` action 2 | As above. |
| Execution Summary | none | Counting it as well as the executions double counts volume. Hidden-quantity fills (RestingHiddenQty) appear only here: section 10. |
| Trade Report | `l3_mbp_trd_t` (securityID, pxd = LastPx, sz = LastQty), skip LastQty 0 (reference price, PWT) | Auction, retail, manual trades have no execution messages, except possibly in open-book auctions (section 10). |
| Trade Reversal | none | No Kaspar record for a bust. Log it. |
| TES Trade Report | none (off-book; not in the book or OB volume) | |
| Auction BBO, Auction Clearing Price, Top Of Book | none | No Kaspar record for an indicative price or a published BBO. |
| Product State Change | `l3_sst_t` per instrument of the product | OB matches only when `tradingStatus == 17` (`frame_kaspr/include/frame/ob/act/OB.hpp:456`). TradSesStatus 1 (halted) or a non-trading TradingSessionSubID -> non-17. |
| Instrument State Change | `l3_sst_t` (securityID) | SecurityTradingStatus 203 (Continuous) -> 17; all other values (auctions, halt 2, closed 200, book 202, freeze...) -> a non-matching value; keep the T7 code in `tradingEvent`. |
| Mass Instrument State Change | `l3_sst_t` for each instrument of the type in the product except the joined exception list; exceptions with their own state | Apply only after the fragment with LastFragment 1. |
| Add Complex / Flexible / Scaled Simple Instrument | instrument loader, not a record | Intraday instruments must be added to the universe before their orders arrive (M p.20, 4.10). |
| Cross Request, Quote Request, Heartbeat | none | Heartbeat feeds the per-product gap check. |
| Product Summary | none; starts the product's rebuild | `l3_chr_v2_t` to the product's books, then the summaries' orders. |
| Instrument Summary | none (or `l3_sst_t` from SecurityTradingStatus); sets the SecurityID for the Snapshot Orders that follow | |
| Snapshot Order | `l3_mbo_v2_t` action 0, `recovery = true` | As `handler_if::SnapshotFullRefreshOrderBook`; securityID from the preceding Instrument Summary. |

Gaps in Kaspar's records for this feed:

- `securityID` is `int32_t` in every record and in handler_if; T7 SecurityID is int64
  (M p.56). A mapping to a 32-bit id is needed.
- `displayQty` is `uint32_t` (and `lastQty` `int32_t`); T7 quantities are int64 with 4
  decimals. Non-integer quantities cannot be represented.
- No order ID on EOBI; the synthesized orderID changes on Order Modify.
- `l3_mbo_trd_v2_t` has no price and no securityID: execution price (LastPx) is lost when
  it differs from the book price; routing must use the T7 SecurityID, not handler_if's
  orderID map.
- `l3_gap_v2_t` and `l3_chr_v2_t` carry no product or instrument scope; scoping is by
  choice of recipient books only.
- No record for: Execution Summary (aggressor side, hidden quantity), trade reversal,
  indicative auction price/BBO, top of book, TES trades, per-instrument book clear.
- Negative prices (allowed by PriceType, M p.36) are rejected by OB's tick check
  (`OB.cpp` ~1741, `ticks > 0`).
- Market orders resting in cash books have no price.

## 8. Instruments

- Definitions: T7 RDI (multicast) and T7 RDF (FIXML files every ~5 minutes via the Common
  Report Engine) (M p.109, A.4). Intraday complex, flexible and scaled simple instruments
  also arrive on the EOBI incremental channel (Add Complex/Flexible/Scaled Simple
  Instrument); complex instruments are re-sent at Start-Of-Day and after a restart
  (M p.20, 4.10). Full descriptions only in RDI/RDF.
- Identifiers: MarketSegmentID = product (Packet Header); SecurityID = instrument (int64)
  (M p.40; p.56). Map SecurityID to Kaspar `sec_id`/asset through the universe.
- Prices: fixed 8 decimals for every instrument; no per-message or per-instrument exponent
  on EOBI. May be negative (M p.36, 7.4). Tick size and price steps: from RDI/RDF, not in
  this manual.
- Quantities: 4 decimals (M p.36). QuantityScalingFactor in Add Complex Instrument and
  Add Scaled Simple Instrument (M p.89, p.94); meaning not stated in M.
- Lifecycle: Instrument State Change also reports intraday expiry (SecurityStatus 4)
  (M p.83, 8.5); SecurityStatus 11 Pending Deletion.

## 9. Pitfalls

**1. Reading the specification**

- Several documents: one manual and its XML representation, same build; R15.0 applies (section 1).
- Errata and change tables: change log M p.111 lists no layout change for R15.0; it says the XML holds the layouts, so the XML was used as the check.
- Tables across page breaks: most layouts span pages (e.g. Mass Instrument State Change p.77-80); every field checked against the XML, 377 of 377 match.
- Offsets and length fields: M offsets are 0-based from BodyLen; BodyLen counts itself ("including this field", M p.40); table offsets are M - 2.
- Reserved and padding: PadN fields kept at their offsets as `Pad` bytes.
- Enumerations elsewhere: SecurityTradingStatus and SecurityMassTradingStatus valid values follow their tables (M p.81, p.86); copied into descriptions.
- Names differing: XML names `OrderModifySamePrio`, `AuctionBBO`; the table uses the manual's headings (OrderModifySamePriority, AuctionBestBidOffer). Heartbeat called "Hearbeat" in its constant table (M p.42). No effect.
- Examples contradicting tables: the manual has no byte examples; the only worked example (M p.22, 4.14) agrees with the layouts.
- Two layers in one table: Packet Header (pkt::from_t7), messages (eobi::from_t7) and group entries (grp::from_t7) are separate sections. Packet Header and messages share framing and TemplateID numbering without overlap.
- Repeated message names: none; Trade Report, TES Trade Report, Trade Reversal and Execution Summary are distinct templates of one FIX type U22.
- Variable-length trailing field: four repeating groups, modelled as tails (section 4).
- Odd integer widths: none (1, 2, 4, 8 only, M p.36).

**2. Framing and encoding**

- Endianness: little endian for all (M p.36).
- Signedness: per XML; prices, quantities, SecurityID, MarketSegmentID, HHIIndicator, LegSymbol, LegRatioQty, SecuritySubType signed two's complement ("most significant bit contains the sign", M p.36 read as two's complement; the C header uses int types).
- Fixed point: global, price 8 decimals, quantity 4 decimals (M p.36).
- Null values: signed minimum, unsigned all ones, string 0x00 (M p.36); many fields optional ("N").
- Strings: SecurityDesc only, 40 bytes, NUL-terminated if room, characters \x00-\xFF (M p.88).
- Several messages per datagram, one product per datagram; a unit of work may span datagrams (CompletionIndicator); no message spans datagrams.
- Timestamps: ns since UNIX epoch, UTC (M p.26).

**3. Sequencing and recovery**

- Sequence scope: ApplSeqNum per channel; MsgSeqNum per product (incremental) and per cycle (snapshot) (section 5).
- A/B: identical sequencing; first-on-wire alternates by partition parity; reuse the existing arbitration.
- Gap rule: ApplSeqNum last+1; lower or equal is a duplicate unless it is a restart (M p.24).
- Recovery: snapshot channel only, per product.
- Snapshot alignment: apply incremental MsgSeqNum > LastMsgSeqNumProcessed after the full product snapshot (M p.13, p.24).
- Snapshot too old: wait for the next cycle if the buffer starts after LastMsgSeqNumProcessed+1 (section 5).
- State to clear: books, order map, per-product sequence, partial units and fragment lists (section 5).
- Recovery paths: to be exercised by replay with injected ApplSeqNum gaps, duplicates, restarts.
- Session behaviour: no session; restart resets ApplSeqNum and MsgSeqNum to 1 with ApplSeqResetIndicator (M p.24).

**4. Book semantics**

- MBO or price level: MBO, keyed by SecurityID, Side, TrdRegTSTimePriority.
- Level-index updates: not applicable.
- Depth-limited books: no depth limit (M p.6).
- Priority on modify: explicit by template (13101 new priority, 13106 kept); 13101 mapped to delete+add (section 7); out-of-main-session exception in section 10.
- Implied and hidden: synthetic matching without synthetic prices; hidden only as RestingHiddenQty; market orders visible in cash books.
- Trades as book deltas: execution messages are both trade and book delta; Execution Summary and (in open-book auctions) Trade Reports overlap them: section 10.
- Trade side: execution Side is the resting order's; Execution Summary AggressorSide the incoming order's.
- Trading states: auction states carry indicative prices; crossed book only implied in auctions (Auction Clearing Price).
- Book clears: Order Mass Delete per instrument; restart; product rebuild.

**5. Prices, quantities and instruments**

- Price or yield: prices only in this manual; negative prices allowed (M p.36).
- Quantity multipliers: QuantityScalingFactor for complex and scaled simple instruments, meaning not in M (section 10).
- Decimals with sentinels: fixed decimals, no decimals field.
- Price-dependent ticks: from RDI/RDF, not in M.
- Instrument definitions: RDI/RDF plus intraday Add * Instrument on EOBI (section 8).
- Lifecycle: intraday creation of complex/flexible instruments, intraday expiry via Instrument State Change.

**6. Behaviour the specification omits**

- Subscription options: none (multicast).
- History on subscribe: none; snapshot cycles only.
- Snapshot size: full depth for every product on the channel, per cycle (M p.6).
- Order within an event: Execution Summary, executions, aggressor Order Add (M p.17); otherwise unspecified (M p.32).
- Connection limits: none; technical heartbeats keep multicast routes (M p.23).

**7. Post-trade** (not applicable: market data only)

- FIX version: not applicable.
- Custom tags: not applicable.
- Credentials: not applicable.
- Missing-field rejects: not applicable.
- Requests before session open: not applicable.
- Trade capture subscription: not applicable.
- Report lifecycle: not applicable (Trade Reversal is the only lifecycle event on EOBI; section 7).
- Duplicates on reconnect: not applicable.
- Fixed-income fields: not applicable.
- Daily sequence reset: not applicable.

**8. Order entry** (not applicable: market data only)

- Client ids: not applicable.
- Cancel/replace ids: not applicable.
- Fill races: not applicable.
- Unsolicited cancels: not applicable.
- Busts: not applicable.
- Throttling: not applicable.
- Reconnect resends: not applicable.
- Orders placed elsewhere: not applicable.
- Short sales: not applicable.

## 10. Ambiguities

| Question | Reading taken | Alternatives | Resolved by |
|---|---|---|---|
| Mass Instrument State Change (13302) and Add Scaled Simple Instrument (13402) are laid out in 8.5/8.6 but missing from Tables 2 and 5 (M p.13, p.32). | Both are incremental messages of R15.0; decoded. | Not sent in R15.0. | XML lists both (settled for the layout; whether they are sent: capture). |
| Counter type: Table 6 says "see 4 byte signed int" (M p.36) but counters are 1 or 2 bytes. | Unsigned, no value 0xFF / 0xFFFF. | Signed. | XML (min 0, noValue 0xFF/0xFFFF) and `EOBILayouts.h` (`uint8_t`, `uint16_t`). Settled. |
| Heartbeat MsgSeqNum "not used" (M p.42): does a Heartbeat consume a product MsgSeqNum? | No; use LastMsgSeqNumProcessed for the gap check, ignore its MsgSeqNum. | It carries the next number. | Capture. |
| Order identity for Kaspar's `orderID`. TrdRegTSTimePriority is unique per product (M p.27); the key also includes Side (a quote's two sides may share it). | `2 * TrdRegTSTimePriority + (Side - 1)`, scoped by SecurityID (not a channel-global map). | A hash of (SecurityID, Side, priority); a handler-assigned counter mapped from the key. | User decision. |
| Order Modify (13101) in Out of Main Session Trading changes the price without changing priority time (M p.59): TrdRegTSTimePriority equals TrdRegTSPrevTimePriority? Is queue position kept? | Treat as delete+add like every 13101 (OB would requeue on a price change anyway). | Keep position (Change in place) when the two timestamps are equal. | Capture of Xetra retail session; user. |
| A 13106 with a size increase or price change (should not happen per M p.16, p.59). | Log and apply as Change (OB then requeues). | Treat as delete+add. | Capture. |
| Trade volume: execution messages omit fills against non-visible orders (M p.17 fn.1), which only the Execution Summary includes (LastQty, RestingHiddenQty); the summary covers only the incoming order's instrument. | Volume from Partial/Full Order Executions only; Execution Summary ignored. Hidden fills are then missing. | Volume from Execution Summary LastQty (but no summary for uncrossing matches, M p.17); executions + RestingHiddenQty. | User decision. |
| Open-book auctions (cash): "optional Full Order Execution, Partial Order Execution and Trade Report message for the auction trades" (M p.19). Same trades twice? | Executions update the book; Trade Report counted only when no execution with its TrdMatchID was seen. | Count both; count Trade Report only. | Capture (TrdMatchID comparison). |
| Execution price: Kaspar's trade record has no price; LastPx can differ from Price (uncrossing, M p.17). | Accept book price (Price) in OB; LastPx lost. | Extend the record with a price. | User decision (record change). |
| How an existing order that is modified to cross (the aggressor) is reported: ES has TrdRegTSPrevTimePriority and the remainder (M p.44), but the manual does not say which message removes or rekeys the old order. | Expect an Order Modify (prev -> new key) or Order Delete after the executions; until it arrives the old order stays at its old price. | The old order is deleted before the executions. | Capture. |
| Cash market orders visible in the book (OrdType 1, Price empty, M p.56, p.11). | Keep them out of OB (no price); track in the map so deletes and executions resolve. | Place at the best price; place at a sentinel level. | User decision. |
| Gap scope: an ApplSeqNum gap does not say which products it carried. | Recover only products whose MsgSeqNum (or Heartbeat LastMsgSeqNumProcessed) shows a gap; mark all products of the channel suspect until each is confirmed contiguous. | Recover every product on the channel. | User decision; replay with injected gaps. |
| Units of work spanning datagrams (CompletionIndicator 0): may datagrams of other products interleave before Complete? M says one product per datagram (M p.30) but not that the sequence is uninterrupted. | Track completion per MarketSegmentID. | Per channel. | Capture. |
| Snapshot Orders carry no SecurityID (M p.105). A lost snapshot datagram mid-cycle would attach orders to the wrong instrument. | Discard the product's cycle on any snapshot ApplSeqNum gap; check TotNoOrders per instrument. | | Rule follows from M; no ambiguity in the layout. |
| Restart detection: "lesser or equal" ApplSeqNum may also be reordering during fail-over (M p.24). | Restart when ApplSeqResetIndicator = 1 and ApplSeqNum <= last, from a packet with a later TransactTime; otherwise drop as duplicate. | Restart on any ApplSeqNum = 1. | Capture of a fail-over (rare); replay injection. |
| SecurityID is int64 (M p.56); Kaspar uses int32. | Map T7 SecurityID to a 32-bit universe id in the handler. | Widen Kaspar's securityID. | User decision. |
| QuantityType has 4 decimals; Kaspar displayQty is uint32 units. | displayQty = qty / 10000, refuse non-integers with a log. | Scale per instrument. | Capture (are fractional quantities used?). |
| QuantityScalingFactor (M p.89, p.94): meaning not described. | Not applied. | Divide or multiply quantities. | T7 Functional Reference / RDI manual (not obtained). |
| Pad2 in Add Complex Instrument is Req'd N with "valid characters \x01-\x7E" (M p.90), unlike other pads. | Padding, ignored. | Carries data. | XML treats it as Pad2; settled for the layout. |
| Packet Header DSCP refers to "A.6 Packet classification" (M p.41); the appendix is A.5. | A.5. | | Editorial. |
| Market-specific behaviour of T7 markets other than Eurex/Xetra (e.g. BSE India). | Not covered: the manual does not mention them. | | Their own manuals. |

Settled with the XML representation (probing tool): the existence and layout of 13302 and
13402, Counter signedness, the Pad2 field, and every offset, length and type in section 4.
No public EOBI sample capture was found: searches of eurex.com, deutsche-boerse.com and the
web found none; a Eurex page returned by the search (`/ex-en/support/initiatives/archive/options-data-eobi`)
answers 404; historical EOBI data is sold (Deutsche Börse Data Shop, resellers). No
ambiguity was settled with real data.
