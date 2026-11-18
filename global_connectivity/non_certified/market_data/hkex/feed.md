# Feed description: HKEX OMD-C Securities FullTick, with retransmission and refresh

Every entry cites its source: document, version, page, section. Abbreviations:
**SPEC** OMD-C Interface Specifications, Binary Protocol v1.47; **DG** OMD-C Developers
Guide v1.15; **CG** OMD-C Connectivity Guide v2.9. Products: **SS** Securities Standard
(conflated, price level), **SP** Securities Premium (streaming, price level), **SF**
Securities FullTick (streaming, order by order).

## 1. Venue and documents

| Document | Version | Date | Applies to |
|---|---|---|---|
| OMD-C Interface Specifications, Binary Protocol | 1.47 | 3 November 2026 | Framing, every message, recovery, aggregate book management. Latest on the HKEX OMD-C page (checked 2026-11-18). The 1.47 change is Appendix A indices only (SPEC p.72). |
| OMD-C Developers Guide | 1.15 | 14 July 2026 | Line arbitration, retransmission and refresh procedures, exception handling, full order book rules. Cited throughout. |
| OMD-C Developers Guide | 1.16 | 21 August 2026 | Published on the HKEX page; differs from 1.15 only in section 10.10 (OMD-C does not run on Hong Kong holidays; DG 1.16 p.29, p.40). Page numbers are the same; 1.15 is cited. |
| OMD-C Connectivity Guide, Securities Market & Index Datafeed | 2.9 | 12 October 2026 | Channel IDs, multicast groups, retransmission server addresses (CG p.8-10). Downloaded from the HKEX page to settle transport. |

All are public on https://www.hkex.com.hk (Services, Market Data Services, Infrastructure,
OMD-C). SPEC p.4 forbids copying or disclosing the document without permission; DG pages
are footed "Classification – Internal" although published. This description cites them
and does not reproduce them.

Product applicability is per section of SPEC (summary table, SPEC p.4-5, checked on the
rendered page because `pdftotext` loses the column of each bullet):

| Message | SS | SP | SF |
|---|---|---|---|
| Control, retransmission, refresh (3.4-3.6), reference data (3.7), status data (3.8) | ● | ● | ● |
| Add Order 30, Modify Order 31, Delete Order 32 | | | ● |
| Add/Delete Odd Lot Order 33/34 | ▲ | ▲ | ▲ |
| Aggregate Order Book Update 53 | ● | ● | |
| Broker Queue 54 | ● | ▲ | ▲ |
| Order Imbalance 56, IEP 41, Reference Price 43, VCM Trigger 23 | ● | ● | ● |
| Trade 50, Trade Cancel 51 | | ● | ● |
| Trade Ticker 52 | ● | | |
| Closing Price 62, Nominal Price 40, Statistics 60, Market Turnover 61, Yield 44, News 22 | ● | ● | |
| Index Definition 70, Index Data 71 | OMD Index only | | |
| Stock Connect 80, 81 | ▲ | ▲ | ▲ |

● part of the product; ▲ complimentary feed on its own channels (odd lot, conflated broker
queue, Stock Connect). SF therefore has **no** Closing Price, Nominal Price or Statistics
message and **no** Orderbook Clear (that is UpdateAction 74 of message 53, SS/SP).

## 2. Transport

- **Multicast UDP**, one packet per datagram, at most 1500 bytes including multicast
  headers (SPEC p.6, 2.1.1; p.10, 3.2).
- **A/B lines:** every channel is a pair of multicast groups, Line A (239.1.1.x) and Line
  B (239.1.127.x); same sequence numbers, same messages, same order, but packets may
  hold different messages (SPEC p.44-45, 4.2; DG p.9-10). Arbitrate per message sequence
  number, first arrival wins. SF order book, trade and security status data are
  active-active: each line comes from a different component and a failover interrupts
  one line only; reference data and trading session status are active-standby and a
  failover interrupts both (SPEC p.7-8, 2.2.4.1; DG p.26, 10.7). Data missed on a line
  during its failover is not resent on that line (DG p.26).
- **SF production channels** (CG p.8, 2.1.3), real-time / refresh channel IDs:
  1 / 501 reference data (Market Definition, Security Definition, Liquidity Provider,
  Currency Rate), 239.1.1.1:51000 and 239.1.127.1:51000, refresh 239.1.1.11:51000;
  2 / 502 Trading Session Status, port 51001; 30-38 / 530-538 full order book channels
  (Add, Modify, Delete Order, Trade, Trade Cancel, IEP, VCM Trigger, Reference Price,
  Order Imbalance, Security Status), 239.1.1.4:51000-51008, refresh 239.1.1.14:51000-51008;
  9 DR Signal, 239.1.1.1:51008. Securities are assigned to channels 30-38 **daily**; some
  channels may carry only heartbeats; listen to all nine (CG p.8).
- **Complimentary feeds** (CG p.9): conflated broker queue channel 60 / 560
  (239.1.1.8:51000); odd lot order channels 70-74 / 570-574 (239.1.1.9:51000-51004).
- **Retransmission server (RTS)**, TCP: production 10.1.90.82 and .83 (primary site),
  10.2.90.82 and .83 (DR site), port 55455; all four active; connect to one at a time
  (CG p.10, 2.1.8; SPEC p.48; DG p.13, 6.1.1). Readiness test servers on CG p.15.
- **RTS session** (SPEC p.46-48, 4.3; DG p.12-17): Logon within 5 s; Logon Response;
  requests answered by Retransmission Response then the messages, in packets with their
  original sequence numbers, possibly framed differently. One session per user; requests
  served serially, cannot be cancelled. Server heartbeat every 30 s: a packet header with
  MsgCount 0, to be echoed byte for byte within 5 s. Limits: last 50,000 messages per
  channel, 10,000 per request, 1,000 requests per day across all channels (OMD-C and OMD
  Index counted separately, DG p.13). After reject 101 the server closes the connection;
  stop requesting for the day (DG p.15).
- **Refresh service:** a refresh multicast channel per real-time channel, A and B lines,
  snapshots at regular intervals; no retransmission for refresh channels (SPEC p.48-49,
  4.4; DG p.18).
- **DR Signal channel:** heartbeats only in normal operation (SPEC p.12, 3.4.3).
- **Day:** connect at or after 06:00; messages stop about 18:30; shutdown after 21:00
  (SPEC p.7, 2.2.1-2.2.3). Multicast heartbeats about every 2 s on an idle channel only
  (SPEC p.7, 2.2.2; DG p.12).

## 3. Framing

- **Endianness:** little-endian for every integer (SPEC p.9, 3.1; DG p.7, 3).
- **Packet** (SPEC p.10-11, 3.2-3.3): 16-byte header (PktSize including the header,
  MsgCount, Filler, SeqNum of the first message, SendTime), then MsgCount messages with no
  padding. A message never spans packets. Same header on multicast, refresh and RTS
  (DG p.5, 2.1).
- **Message:** MsgSize (Uint16, counts itself) then MsgType (Uint16) (SPEC p.10, Table 1;
  DG p.6, 2.3). In the field table MsgSize is `@length_prefix 2 inclusive` and MsgType
  `@type_code uint 2`; the table's offsets are the specification's minus 2, and each
  `@size` is the specification's Total Length minus 2. The generated structs include the
  prefix (`sizeof(AddOrder) == 32`, the specification's total) and the decoder returns
  `size` = MsgSize.
- **Heartbeat:** a packet header with MsgCount 0 (SPEC p.11, 3.4.1).
- **RTS requests** (SPEC p.12-13, 3.5): the client sends a packet header (PktSize 32,
  MsgCount 1, SeqNum 0, SendTime 0) before Logon or Retransmission Request; the server
  uses the same header for its responses, whose SeqNum and SendTime are not relevant.
- **Repeating groups:** Security Definition (8-byte underlying entries), Liquidity
  Provider (2-byte broker numbers), Aggregate Order Book Update (24-byte entries), Broker
  Queue (4-byte items), News (three groups with counts between them). Each is modelled as
  the fixed part plus a trailing variable-length field; the entry layouts are untyped
  structs in `omdc::from_hkex_group` (section 4).
- **Layers in the table:** `pkt::from_hkex` packet header; `omdc::from_hkex` every message
  that can arrive on a multicast channel, including Sequence Reset, DR Signal and Refresh
  Complete, which share the channel's framing and sequence numbers; `omdc::from_hkex_group`
  group entries; `rts::from_hkex` and `rts::to_hkex` the RTS session messages. On the RTS
  stream the handler decodes with `rts::from_hkex` and, for retransmitted data, with
  `omdc::from_hkex`; their type codes do not overlap.
- **Types** (SPEC p.9, 3.1): String ASCII, left aligned, space padded unless stated;
  Binary UTF-16LE (Chinese names, EXC news); Int64 null 0x8000000000000000, used in Index
  Data and Stock Connect, "could be used at all Int64 fields" (SPEC p.9, 3.1.1).

## 4. Message catalogue

Generated by `fieldgen.py omdc.tbl --markdown`; `--check` passes: 5 sections, 48 messages.
Offsets are 0-based **after MsgSize**: add 2 to get the specification's offset. The byte
count in each heading excludes MsgSize; the specification's Total Length is 2 more. A
field name the specification repeats in one message has the specification's offset
appended (`Filler199`, `MaturityDate456`). Every table offset plus 2 was matched against
an "offset name" line of the `pdftotext -layout` output (226 fields); the table pages were
also read as rendered.

### pkt::from_hkex

#### PacketHeader (type none, 16 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| PktSize | 0 | 2 | Uint16 | Size of the packet, including this header |
| MsgCount | 2 | 1 | Uint8 | Messages in the packet; 0 for a heartbeat |
| Filler | 3 | 1 | String | Filler |
| SeqNum | 4 | 4 | Uint32 | Sequence number of the first message; in a heartbeat, the sequence number of the previous message sent on the channel |
| SendTime | 8 | 8 | Uint64 | Nanoseconds since the Unix epoch, UTC; millisecond precision |

### omdc::from_hkex

#### SequenceReset (type `100` (0x64), 6 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | SequenceReset: first message of the day on every channel; also sent to rectify reference data before market open; clear all cached data, next expected sequence number = NewSeqNo (SPEC v1.47 p.11-12, 3.4.2) |
| NewSeqNo | 2 | 4 | Uint32 | New sequence number; always 1 |

#### DisasterRecoverySignal (type `105` (0x69), 6 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | DisasterRecoverySignal: site failover status, on the dedicated DR channel only (SPEC v1.47 p.12, 3.4.3) |
| DRStatus | 2 | 4 | Uint32 | 1 DR in progress (clear cached data), 2 DR completed (rebuild from refresh) |

#### RefreshComplete (type `203` (0xCB), 6 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | RefreshComplete: end of a refresh snapshot, on refresh channels (SPEC v1.47 p.15, 3.6.1) |
| LastSeqNum | 2 | 4 | Uint32 | Real-time sequence number with which the snapshot is synchronized; 0 if nothing has been published on the real-time channel (DG p.19) |

#### MarketDefinition (type `10` (0xA), 38 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | MarketDefinition: one per market segment at start of day (SPEC v1.47 p.15, 3.7.1) |
| MarketCode | 2 | 4 | String | MAIN, GEM, NASD, ETS |
| MarketName | 6 | 25 | String | Market segment name |
| CurrencyCode | 31 | 3 | String | Base currency of the segment (ISO 4217, plus CNH) |
| NumberOfSecurities | 34 | 4 | Uint32 | Securities in the segment |

#### SecurityDefinition (type `11` (0xB), 542 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | SecurityDefinition: all reference data of a security; may be resent intraday (SPEC v1.47 p.15-19, 3.7.2); total length 544 + 8 * NoUnderlyingSecurities |
| SecurityCode | 2 | 4 | Uint32 | Security code 1-99999 |
| MarketCode | 6 | 4 | String | MAIN, GEM, NASD, ETS |
| ISINCode | 10 | 12 | String | ISIN |
| InstrumentType | 22 | 4 | String | BOND, EQTY, TRST, WRNT |
| ProductType | 26 | 1 | Uint8 | 1 ordinary shares, 2 preference, 3 DW, 4 debt, 6 rights, 7 HDR ordinary, 8 REIT, 9 unit trust, 10 L&I product, 11 CBBC, 12 HDR preference, 13 equity warrant, 15 inline warrant, 16 equity ETF, 17 fixed income/MM ETF, 18 commodities ETF, 21 SPAC warrant, 24 SPAC shares, 99 other |
| Filler29 | 27 | 1 | String | Filler |
| SpreadTableCode | 28 | 2 | String | Tick table of the Second Schedule of the Rules of the Exchange: 01 Part A, 03 Part B, 04 inline warrants (Part E up to HK$1.00), 05 Part D, 06 Part E |
| SecurityShortName | 30 | 40 | String | Short name |
| CurrencyCode | 70 | 3 | String | Trading currency |
| SecurityNameGCCS | 73 | 60 | Binary | Traditional Chinese name, UTF-16LE |
| SecurityNameGB | 133 | 60 | Binary | Simplified Chinese name, UTF-16LE |
| LotSize | 193 | 4 | Uint32 | Board lot size |
| Filler199 | 197 | 4 | String | Filler |
| PreviousClosingPrice | 201 | 4 | Int32 | 3 implied decimals; NAV before first listing day for ETF/L&I; may be 0 |
| VCMFlag | 205 | 1 | String | Y VCM applicable, N not |
| ShortSellFlag | 206 | 1 | String | Y short sell allowed, N not |
| CASFlag | 207 | 1 | String | Y closing auction applicable, N not |
| CCASSFlag | 208 | 1 | String | Y CCASS security, N not |
| DummySecurityFlag | 209 | 1 | String | Y dummy security, N normal |
| Filler212 | 210 | 1 | String | Filler |
| StampDutyFlag | 211 | 1 | String | Y stamp duty required, N not |
| Filler214 | 212 | 1 | String | Filler |
| ListingDate | 213 | 4 | Uint32 | YYYYMMDD; 19000101 unknown |
| DelistingDate | 217 | 4 | Uint32 | YYYYMMDD; 0 none |
| FreeText | 221 | 38 | String | Free text, spaces when none; may change intraday |
| Filler261 | 259 | 62 | String | Filler |
| POSFlag | 321 | 1 | String | Y pre-opening session applicable, N not |
| POSUpperLimit | 322 | 4 | Int32 | 3 implied decimals; upper limit of orders in POS order input, and of at-auction limit sell orders in POS no-cancellation and random matching; 0 not available |
| POSLowerLimit | 326 | 4 | Int32 | 3 implied decimals; lower limit, as above for buy orders; 0 not available |
| DomainStmtSecurityCode | 330 | 4 | Uint32 | Security code of the domain settlement counter; 0 if this security is it |
| Filler336 | 334 | 37 | String | Filler |
| EFNFlag | 371 | 1 | String | Bonds: Y EFN, N non-EFN |
| AccruedInterest | 372 | 4 | Uint32 | Bonds: 3 implied decimals |
| CouponRate | 376 | 4 | Uint32 | Bonds: 3 implied decimals |
| Filler382 | 380 | 1 | String | Filler |
| FaceValue | 381 | 8 | Uint64 | Bonds: DecimalsInFaceValue decimals; 0 not available |
| DecimalsInFaceValue | 389 | 1 | Uint8 | Bonds: decimals of FaceValue |
| FaceValueCurrency | 390 | 3 | String | Bonds: currency of FaceValue |
| MaturityDate395 | 393 | 4 | Uint32 | Bonds: maturity date YYYYMMDD (MaturityDate at specification offset 395) |
| InvestorType | 397 | 1 | String | Bonds: R retail, P professional |
| Filler400 | 398 | 44 | String | Filler |
| ConversionRatio | 442 | 4 | Uint32 | Structured products: 3 implied decimals; 0 not available |
| StrikePrice1 | 446 | 4 | Int32 | 3 implied decimals; the strike, or the lower strike when StrikePrice2 is not 0 |
| StrikePrice2 | 450 | 4 | Int32 | 3 implied decimals; upper strike; 0 if one strike only |
| MaturityDate456 | 454 | 4 | Uint32 | Warrants and structured products: maturity date YYYYMMDD (MaturityDate at specification offset 456) |
| CallPutFlag | 458 | 1 | String | DW/inline warrants: C call, P put, O other; CBBC: C bull, P bear |
| Style | 459 | 1 | String | A American, E European, blank other |
| Filler462 | 460 | 2 | String | Filler |
| WarrantType | 462 | 1 | String | N normal, X exotic, 0 not available |
| CallPrice | 463 | 4 | Int32 | CBBC call price, DecimalsInCallPrice decimals; 0 not available |
| DecimalsInCallPrice | 467 | 1 | Uint8 | Decimals of CallPrice |
| Entitlement | 468 | 4 | Int32 | DecimalsInEntitlement decimals; 0 not available |
| DecimalsInEntitlement | 472 | 1 | Uint8 | Decimals of Entitlement |
| NoWarrantsPerEntitlement | 473 | 4 | Uint32 | Warrants per entitlement |
| Filler479 | 477 | 63 | String | Filler |
| NoUnderlyingSecurities | 540 | 2 | Uint16 | 0 or 1: entries of 8 bytes that follow (UnderlyingSecurity) |
| UnderlyingSecurities | 542 | var | Binary | Repeating group: NoUnderlyingSecurities x UnderlyingSecurity (8 bytes) |

#### LiquidityProvider (type `13` (0xD), 8 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | LiquidityProvider: liquidity providers of a security, at start of day (SPEC v1.47 p.20, 3.7.3); total length 10 + 2 * NoLiquidityProviders |
| SecurityCode | 2 | 4 | Uint32 | Security code |
| NoLiquidityProviders | 6 | 2 | Uint16 | 1 to 50 entries of 2 bytes follow (LiquidityProviderEntry) |
| LPBrokerNumbers | 8 | var | Binary | Repeating group: NoLiquidityProviders x LiquidityProviderEntry (2 bytes) |

#### CurrencyRate (type `14` (0xE), 14 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | CurrencyRate: HKD value of a foreign currency (SPEC v1.47 p.21, 3.7.4) |
| CurrencyCode | 2 | 3 | String | Currency |
| Filler7 | 5 | 1 | String | Filler |
| CurrencyFactor | 6 | 2 | Uint16 | n: CurrencyRate is the HKD value of 10^n units (examples, p.21) |
| Filler10 | 8 | 2 | String | Filler |
| CurrencyRate | 10 | 4 | Uint32 | HKD per 10^CurrencyFactor units, 4 implied decimals |

#### TradingSessionStatus (type `20` (0x14), 30 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | TradingSessionStatus: session of a market segment, on its own channel, not synchronized with order data (SPEC v1.47 p.21-23, 3.8.1) |
| MarketCode | 2 | 4 | String | MAIN, GEM, NASD, ETS |
| Filler8 | 6 | 1 | String | Filler |
| TradingSessionSubID | 7 | 1 | Uint8 | 100 not yet open; POS: 1 order input, 101 no cancellation, 108 random matching, 2 order matching, 7 blocking; 3 continuous trading; CAS: 105 reference price fixing, 5 order input, 106 no cancellation, 107 random close, 4 order matching; 102 exchange intervention, 103 close, 104 order cancel, 0 day close |
| TradingSesStatus | 8 | 1 | Uint8 | 0 unknown, 10 halted, 20 pre-open, 30 open, 40 pre-close, 50 closed, 100 day closed |
| TradingSesControlFlag | 9 | 1 | String | 0 automatic, 1 manual |
| Filler12 | 10 | 4 | String | Filler |
| StartDateTime | 14 | 8 | Uint64 | ns since epoch UTC, second precision; 0 not available |
| EndDateTime | 22 | 8 | Uint64 | ns since epoch UTC, second precision; 0 not available |

#### SecurityStatus (type `21` (0x15), 10 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | SecurityStatus: at start of day for securities not available for trading, and on every change (SPEC v1.47 p.23, 3.8.2) |
| SecurityCode | 2 | 4 | Uint32 | Security code |
| SuspensionIndicator | 6 | 1 | Uint8 | 2 trading halt or suspend, 3 resume |
| Filler | 7 | 3 | String | Filler |

#### VCMTrigger (type `23` (0x17), 34 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | VCMTrigger: a VCM cooling-off period starts (SPEC v1.47 p.34-35, 3.10.8; SS, SP, SF per summary table p.5) |
| SecurityCode | 2 | 4 | Uint32 | Security code |
| CoolingOffStartTime | 6 | 8 | Uint64 | ns since epoch UTC, second precision |
| CoolingOffEndTime | 14 | 8 | Uint64 | ns since epoch UTC, second precision |
| VCMReferencePrice | 22 | 4 | Int32 | 3 implied decimals |
| VCMLowerPrice | 26 | 4 | Int32 | Lower band, 3 implied decimals |
| VCMUpperPrice | 30 | 4 | Int32 | Upper band, 3 implied decimals |

#### AddOrder (type `30` (0x1E), 30 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | AddOrder: SF only. New order in the board-lot book (SPEC v1.47 p.23-24, 3.9.1) |
| SecurityCode | 2 | 4 | Uint32 | Security code |
| OrderId | 6 | 8 | Uint64 | Unique per security within the day; not consecutive |
| Price | 14 | 4 | Int32 | 3 implied decimals; 0 for a market order (DG p.23) |
| Quantity | 18 | 4 | Uint32 | Shares |
| Side | 22 | 2 | Uint16 | 0 bid, 1 offer |
| OrderType | 24 | 1 | String | 1 market, 2 limit |
| Filler | 25 | 1 | String | Filler |
| OrderBookPosition | 26 | 4 | Int32 | Always 0 for securities |

#### ModifyOrder (type `31` (0x1F), 26 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | ModifyOrder: SF only. Quantity of an order changed; quantity is the only modifiable attribute (SPEC v1.47 p.24, 3.9.2) |
| SecurityCode | 2 | 4 | Uint32 | Security code |
| OrderId | 6 | 8 | Uint64 | Order modified |
| Quantity | 14 | 4 | Uint32 | New quantity, shares |
| Side | 18 | 2 | Uint16 | 0 bid, 1 offer |
| Filler | 20 | 2 | String | Filler |
| OrderBookPosition | 22 | 4 | Int32 | Always 0 for securities |

#### DeleteOrder (type `32` (0x20), 18 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | DeleteOrder: SF only. Order removed (SPEC v1.47 p.25, 3.9.3) |
| SecurityCode | 2 | 4 | Uint32 | Security code |
| OrderId | 6 | 8 | Uint64 | Order deleted |
| Side | 14 | 2 | Uint16 | 0 bid, 1 offer |
| Filler | 16 | 2 | String | Filler |

#### AddOddLotOrder (type `33` (0x21), 26 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | AddOddLotOrder: complimentary odd lot order feed (SS, SP, SF). New order in the odd-lot book (SPEC v1.47 p.25-26, 3.9.4) |
| SecurityCode | 2 | 4 | Uint32 | Security code |
| OrderId | 6 | 8 | Uint64 | Unique per security within the day |
| Price | 14 | 4 | Int32 | 3 implied decimals |
| Quantity | 18 | 4 | Uint32 | Shares |
| BrokerID | 22 | 2 | Uint16 | Broker |
| Side | 24 | 2 | Uint16 | 0 bid, 1 offer |

#### DeleteOddLotOrder (type `34` (0x22), 18 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | DeleteOddLotOrder: complimentary odd lot order feed (SS, SP, SF). Odd-lot order removed (SPEC v1.47 p.26, 3.9.5) |
| SecurityCode | 2 | 4 | Uint32 | Security code |
| OrderId | 6 | 8 | Uint64 | Order deleted |
| BrokerID | 14 | 2 | Uint16 | Broker |
| Side | 16 | 2 | Uint16 | 0 bid, 1 offer |

#### AggregateOrderBookUpdate (type `53` (0x35), 10 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | AggregateOrderBookUpdate: SS and SP only. Price-level deltas, board lots, 10 tick levels (SPEC v1.47 p.26-27, 3.9.6; section 5 p.50-57); total length 12 + 24 * NoEntries |
| SecurityCode | 2 | 4 | Uint32 | Security code |
| Filler | 6 | 3 | String | Filler |
| NoEntries | 9 | 1 | Uint8 | Entries of 24 bytes that follow (AggregateOrderBookEntry); 0 in a refresh for an emptied book (p.49) |
| Entries | 10 | var | Binary | Repeating group: NoEntries x AggregateOrderBookEntry (24 bytes), applied in order |

#### BrokerQueue (type `54` (0x36), 10 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | BrokerQueue: SS; complimentary conflated broker queue feed for SP and SF. Top 40 broker IDs of one side (SPEC v1.47 p.27-28, 3.9.7); total length 12 + 4 * ItemCount |
| SecurityCode | 2 | 4 | Uint32 | Security code |
| ItemCount | 6 | 1 | Uint8 | 0 to 40 items of 4 bytes follow (BrokerQueueItem) |
| Side | 7 | 2 | Uint16 | 1 buy, 2 sell (not the 0/1 of order messages) |
| BQMoreFlag | 9 | 1 | String | Y more brokers exist in the queue, N no more |
| Items | 10 | var | Binary | Repeating group: ItemCount x BrokerQueueItem (4 bytes) |

#### OrderImbalance (type `56` (0x38), 18 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | OrderImbalance: imbalance at the IEP during POS and CAS (SPEC v1.47 p.28-29, 3.9.8; SS, SP, SF) |
| SecurityCode | 2 | 4 | Uint32 | Security code |
| OrderImbalanceDirection | 6 | 1 | String | N buy = sell, B buy surplus, S sell surplus, space not applicable |
| Filler9 | 7 | 1 | String | Filler |
| OrderImbalanceQuantity | 8 | 8 | Uint64 | Absolute imbalance at IEP; ignore when direction is space |
| Filler18 | 16 | 2 | String | Filler |

#### Trade (type `50` (0x32), 30 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | Trade: SP and SF. One trade (SPEC v1.47 p.29-30, 3.10.1) |
| SecurityCode | 2 | 4 | Uint32 | Security code |
| TradeID | 6 | 4 | Uint32 | Per security, from 1, +1 per trade, reset daily |
| Price | 10 | 4 | Int32 | 3 implied decimals |
| Quantity | 14 | 4 | Uint32 | Shares |
| TrdType | 18 | 2 | Int16 | 0 automatch normal, 4 late trade (P), 22 non-direct off-exchange (M), 100 automatch internalized (Y), 101 direct off-exchange (X), 102 odd lot (D), 103 auction (U), 104 overseas |
| Filler | 20 | 2 | String | Filler |
| TradeTime | 22 | 8 | Uint64 | ns since epoch UTC, microsecond precision |

#### TradeCancel (type `51` (0x33), 10 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | TradeCancel: SP and SF. A trade cancelled (SPEC v1.47 p.30-31, 3.10.2) |
| SecurityCode | 2 | 4 | Uint32 | Security code |
| TradeID | 6 | 4 | Uint32 | Trade cancelled |

#### TradeTicker (type `52` (0x34), 34 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | TradeTicker: SS only. Trades at one price aggregated; resent with remaining quantity on a cancel (SPEC v1.47 p.31-32, 3.10.3) |
| SecurityCode | 2 | 4 | Uint32 | Security code |
| TickerID | 6 | 4 | Uint32 | Per security, from 1, +1 per ticker |
| Price | 10 | 4 | Int32 | 3 implied decimals |
| AggregateQuantity | 14 | 8 | Uint64 | Shares; remaining quantity when TrdCancelFlag = Y |
| TradeTime | 22 | 8 | Uint64 | ns since epoch UTC, microsecond precision; not applicable when TrdCancelFlag = Y |
| TrdType | 30 | 2 | Int16 | As Trade, without 104; not applicable when TrdCancelFlag = Y |
| TrdCancelFlag | 32 | 1 | String | Y a trade in this ticker was cancelled, N not |
| Filler | 33 | 1 | String | Filler |

#### ClosingPrice (type `62` (0x3E), 14 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | ClosingPrice: SS and SP only. Near end of day (SPEC v1.47 p.32, 3.10.4) |
| SecurityCode | 2 | 4 | Uint32 | Security code |
| ClosingPrice | 6 | 4 | Int32 | 3 implied decimals; 0 not available |
| NumberOfTrades | 10 | 4 | Uint32 | Trades in the day; not populated for SS |

#### NominalPrice (type `40` (0x28), 10 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | NominalPrice: SS and SP only (SPEC v1.47 p.32-33, 3.10.5) |
| SecurityCode | 2 | 4 | Uint32 | Security code |
| NominalPrice | 6 | 4 | Int32 | 3 implied decimals; may be 0 |

#### IndicativeEquilibriumPrice (type `41` (0x29), 18 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | IndicativeEquilibriumPrice: IEP and IEV during POS and CAS (SPEC v1.47 p.33, 3.10.6; SS, SP, SF) |
| SecurityCode | 2 | 4 | Uint32 | Security code |
| Price | 6 | 4 | Int32 | IEP, 3 implied decimals; 0 not available |
| AggregateQuantity | 10 | 8 | Uint64 | IEV |

#### ReferencePrice (type `43` (0x2B), 18 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | ReferencePrice: reference price and order-input limits in POS and CAS (SPEC v1.47 p.33-34, 3.10.7; SS, SP, SF) |
| SecurityCode | 2 | 4 | Uint32 | Security code |
| ReferencePrice | 6 | 4 | Int32 | 3 implied decimals; 0 not available |
| LowerPrice | 10 | 4 | Int32 | POS: lower limit of at-auction limit sell orders; CAS: lower band; 3 implied decimals; 0 not available |
| UpperPrice | 14 | 4 | Int32 | POS: upper limit of at-auction limit buy orders; CAS: upper band; 3 implied decimals; 0 not available |

#### Statistics (type `60` (0x3C), 50 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | Statistics: SS and SP only. After trades, manual, odd lot trades and cancels (SPEC v1.47 p.35-36, 3.11.1) |
| SecurityCode | 2 | 4 | Uint32 | Security code |
| SharesTraded | 6 | 8 | Uint64 | Shares traded |
| Turnover | 14 | 8 | Int64 | 3 implied decimals |
| HighPrice | 22 | 4 | Int32 | 3 implied decimals |
| LowPrice | 26 | 4 | Int32 | 3 implied decimals |
| LastPrice | 30 | 4 | Int32 | 3 implied decimals |
| VWAP | 34 | 4 | Int32 | 3 implied decimals; not populated for SS |
| ShortSellSharesTraded | 38 | 4 | Uint32 | Updated at most twice a day |
| ShortSellTurnover | 42 | 8 | Int64 | 3 implied decimals |

#### MarketTurnover (type `61` (0x3D), 18 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | MarketTurnover: SS and SP only. Turnover per segment and currency, about every 2 s (SPEC v1.47 p.36-37, 3.11.2) |
| MarketCode | 2 | 4 | String | MAIN, GEM, NASD, ETS |
| CurrencyCode | 6 | 3 | String | Currency; blank: all currencies in HKD |
| Filler | 9 | 1 | String | Filler |
| Turnover | 10 | 8 | Int64 | 3 implied decimals |

#### Yield (type `44` (0x2C), 10 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | Yield: SS and SP only. Bond yield change (SPEC v1.47 p.37, 3.11.3) |
| SecurityCode | 2 | 4 | Uint32 | Security code |
| Yield | 6 | 4 | Int32 | 3 implied decimals; 0 not available |

#### News (type `22` (0x16), 346 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | News: SS and SP only. Possibly fragmented over consecutive messages (SPEC v1.47 p.37-38, 3.12.1); total length 356 + 4nM + 4nS + 160nP |
| NewsType | 2 | 3 | String | EXN exchange news (ASCII), EXC Chinese exchange news (UTF-16LE) |
| NewsID | 5 | 3 | String | News page number within NewsType |
| Headline | 8 | 320 | Binary | ASCII or UTF-16LE per NewsType; first fragment only |
| CancelFlag | 328 | 1 | String | Y cancelled, N not |
| LastFragment | 329 | 1 | String | Y last fragment, N not |
| Filler332 | 330 | 4 | String | Filler |
| ReleaseTime | 334 | 8 | Uint64 | ns since epoch UTC, second precision |
| Filler344 | 342 | 2 | String | Filler |
| NoMarketCodes | 344 | 2 | Uint16 | 0 to 4 |
| Groups | 346 | var | Binary | NoMarketCodes x NewsMarketCode (4), NewsSecurityCodesHeader (4), NoSecurityCodes x NewsSecurityCode (4), NewsLinesHeader (4), NoNewsLines x NewsLineEntry (160) |

#### IndexDefinition (type `70` (0x46), 18 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | IndexDefinition: OMD Index only (SPEC v1.47 p.39, 3.13.1) |
| IndexCode | 2 | 11 | String | Index code (Appendix A) |
| IndexSource | 13 | 1 | String | C CSI and CES, H HSI, X HKEX |
| CurrencyCode | 14 | 3 | String | Currency of index turnover; may be blank |
| Filler | 17 | 1 | String | Filler |

#### IndexData (type `71` (0x47), 110 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | IndexData: OMD Index only. Int64 fields may be null 0x8000000000000000 (SPEC v1.47 p.39-40, 3.13.2; p.9, 3.1.1) |
| IndexCode | 2 | 11 | String | Index code |
| IndexStatus | 13 | 1 | String | C closing, I indicative, O opening, P last close (prev. session), R preliminary close, S stop loss, T real-time; may be blank |
| IndexTime | 14 | 8 | Int64 | ns since epoch UTC, second precision |
| IndexValue | 22 | 8 | Int64 | 4 implied decimals |
| NetChgPrevDay | 30 | 8 | Int64 | 4 implied decimals |
| HighValue | 38 | 8 | Int64 | 4 implied decimals |
| LowValue | 46 | 8 | Int64 | 4 implied decimals |
| EASValue | 54 | 8 | Int64 | 2 implied decimals |
| IndexTurnover | 62 | 8 | Int64 | 4 implied decimals |
| OpeningValue | 70 | 8 | Int64 | 4 implied decimals |
| ClosingValue | 78 | 8 | Int64 | 4 implied decimals |
| PreviousSesClose | 86 | 8 | Int64 | 4 implied decimals |
| IndexVolume | 94 | 8 | Int64 | CSI and CES only |
| NetChgPrevDayPct | 102 | 4 | Int32 | 4 implied decimals |
| Exception | 106 | 1 | String | # HSIL exceptional rule applied, space normal |
| Filler | 107 | 3 | String | Filler |

#### StockConnectDailyQuotaBalance (type `80` (0x50), 22 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | StockConnectDailyQuotaBalance: complimentary. Northbound daily quota balance (SPEC v1.47 p.41, 3.14.1) |
| StockConnectMarket | 2 | 2 | String | SH Shanghai, SZ Shenzhen |
| TradingDirection | 4 | 2 | String | NB northbound |
| DailyQuotaBalance | 6 | 8 | Int64 | RMB; null while at or above 30% of quota; 0 used up |
| DailyQuotaBalanceTime | 14 | 8 | Uint64 | ns since epoch UTC, second precision |

#### StockConnectMarketTurnover (type `81` (0x51), 30 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | StockConnectMarketTurnover: complimentary (SPEC v1.47 p.41-42, 3.14.2) |
| StockConnectMarket | 2 | 2 | String | SH, SZ |
| TradingDirection | 4 | 2 | String | NB northbound, SB southbound |
| BuyTurnover | 6 | 8 | Int64 | Southbound HKD, rounded down; null for northbound |
| SellTurnover | 14 | 8 | Int64 | Southbound HKD, rounded down; null for northbound |
| BuySellTurnover | 22 | 8 | Int64 | Buy+SellTurnover: RMB northbound, HKD southbound |

### omdc::from_hkex_group

#### UnderlyingSecurity (type none, 8 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| UnderlyingSecurityCode | 0 | 4 | Uint32 | Security code of the underlying |
| Filler | 4 | 4 | String | Filler |

#### LiquidityProviderEntry (type none, 2 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| LPBrokerNumber | 0 | 2 | Uint16 | Broker number of the liquidity provider |

#### AggregateOrderBookEntry (type none, 24 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| AggregateQuantity | 0 | 8 | Uint64 | Shares at the level |
| Price | 8 | 4 | Int32 | 3 implied decimals |
| NumberOfOrders | 12 | 4 | Uint32 | Orders at the level |
| Side | 16 | 2 | Uint16 | 0 bid, 1 offer |
| PriceLevel | 18 | 1 | Uint8 | Price level, 1 = best; levels below a new or deleted entry shift implicitly |
| UpdateAction | 19 | 1 | Uint8 | 0 new, 1 change, 2 delete, 74 orderbook clear (both sides) |
| Filler | 20 | 4 | String | Filler |

#### BrokerQueueItem (type none, 4 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| Item | 0 | 2 | Uint16 | Broker number (Type B), or spreads from the best price (Type S; 0 = no brokers at the level of the previous S) |
| Type | 2 | 1 | String | B broker number, S number of spreads |
| Filler | 3 | 1 | String | Filler |

#### NewsMarketCode (type none, 4 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MarketCode | 0 | 4 | String | MAIN, GEM, NASD, ETS |

#### NewsSecurityCodesHeader (type none, 4 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| Filler | 0 | 2 | String | Filler |
| NoSecurityCodes | 2 | 2 | Uint16 | 0 to 200 |

#### NewsSecurityCode (type none, 4 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| SecurityCode | 0 | 4 | Uint32 | Security code |

#### NewsLinesHeader (type none, 4 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| Filler | 0 | 2 | String | Filler |
| NoNewsLines | 2 | 2 | Uint16 | At most 10 per news page |

#### NewsLineEntry (type none, 160 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| NewsLine | 0 | 160 | Binary | ASCII (EXN) or UTF-16LE (EXC) |

### rts::from_hkex

#### LogonResponse (type `102` (0x66), 6 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | LogonResponse: answer to Logon; the server closes the connection after a rejection (SPEC v1.47 p.13, 3.5.2; p.46, 4.3) |
| SessionStatus | 2 | 1 | Uint8 | 0 session active, 5 invalid username or IP address, 100 user already connected |
| Filler | 3 | 3 | String | Filler |

#### RetransmissionResponse (type `202` (0xCA), 14 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | RetransmissionResponse: accepts or rejects a request; accepted messages follow (SPEC v1.47 p.14, 3.5.4) |
| ChannelID | 2 | 2 | Uint16 | Multicast channel of the request |
| RetransStatus | 4 | 1 | Uint8 | 0 accepted, 1 unknown/unauthorized channel, 2 messages not available, 100 exceeds maximum range, 101 exceeds maximum requests a day (connection closed) |
| Filler | 5 | 1 | String | Filler |
| BeginSeqNum | 6 | 4 | Uint32 | First sequence number of the range |
| EndSeqNum | 10 | 4 | Uint32 | Last sequence number of the range |

### rts::to_hkex

#### PacketHeader (type none, 16 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| PktSize | 0 | 2 | Uint16 | 32: 16 header + 16 message |
| MsgCount | 2 | 1 | Uint8 | 1 |
| Filler | 3 | 1 | String | Empty |
| SeqNum | 4 | 4 | Uint32 | 0, not used |
| SendTime | 8 | 8 | Uint64 | 0, not used (DG p.13 says nanoseconds since epoch) |

#### Logon (type `101` (0x65), 14 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | Logon: first message on the TCP connection, within 5 s (SPEC v1.47 p.13, 3.5.1; p.48 system limits) |
| Username | 2 | 12 | StringNul | Username, padded with binary nulls |

#### RetransmissionRequest (type `201` (0xC9), 14 bytes, 2-byte inclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 2 | code | RetransmissionRequest: inclusive range of one channel, at most 10,000 messages, among the last 50,000 (SPEC v1.47 p.14, 3.5.3; p.48) |
| ChannelID | 2 | 2 | Uint16 | Multicast channel ID (Connectivity Guide) |
| Filler | 4 | 2 | String | Filler |
| BeginSeqNum | 6 | 4 | Uint32 | First sequence number requested |
| EndSeqNum | 10 | 4 | Uint32 | Last sequence number requested, inclusive |


## 5. Sequencing and recovery

- **Scope:** per channel (real-time and refresh channels each), per business day
  (SPEC p.6, 2.2; p.43, 4.1). Strictly increasing within a day. The packet header has no
  channel ID: the handler knows the channel from the multicast group it read.
- **Numbering:** SeqNum is the first message's number; the others in the packet follow
  implicitly; next packet = SeqNum + MsgCount (SPEC p.43, Table 3). A heartbeat carries
  the number of the previous message and does not advance the sequence (SPEC p.11).
- **Gap rule** (SPEC p.44, Figure 2; DG p.10, p.33 Appendix C): with `next` the expected
  number, a packet with SeqNum > next is a gap (spool it); SeqNum + MsgCount <= next is a
  duplicate; otherwise process the messages whose number equals next, skipping those
  below. A heartbeat whose SeqNum >= next reveals lost messages up to SeqNum (DG p.12,
  Table 3; reading in section 10).
- **Line arbitration:** first arrival wins per message (SPEC p.45; DG p.9). The test
  (`tests/hkex_decode_test.cpp`) replays the DG p.10 example where A and B pack 101-107
  differently.
- **Retransmission** (SPEC p.46-48; DG p.12-17): Retransmission Request with ChannelID
  and an inclusive range (DG p.14-15 example: 107-108). Statuses: 0 accepted, 1 unknown or
  unauthorized channel, 2 not available (outside the last 50,000), 100 range too large
  (split into requests of at most 10,000), 101 daily limit reached. The DG suggests waiting
  for the other line before requesting (approach 1) or requesting at once (approach 2),
  spooling live packets meanwhile (DG p.10-11). Not available for refresh channels.
- **Refresh** (SPEC p.48-49, 4.4; DG p.18-21): a cycle is the messages between two
  Refresh Complete messages. Procedure: cache the real-time channel; join the refresh
  channel; discard until a Refresh Complete; apply everything up to the next Refresh
  Complete; keep its LastSeqNum; leave the refresh channel; drop cached real-time
  messages with sequence <= LastSeqNum, apply those above. A gap on the refresh channel
  voids the cycle: wait for the next one (DG p.20). LastSeqNum 0 means nothing has been
  published on the real-time channel (DG p.19).
- **Alignment rule:** the snapshot includes message LastSeqNum; apply cached messages
  with sequence > LastSeqNum (SPEC p.49; DG p.19, p.25). Appendix E's pseudo code (DG p.36)
  instead applies cached messages ">= expectedSeqNum" of the refresh channel; section 10.
  If the earliest cached real-time message is above LastSeqNum + 1, the cycle is too old
  for the cache: not stated; reading in section 10.
- **Refresh content** (SPEC p.48-49): latest Market Definition, Security Definition,
  Liquidity Provider, Trading Session Status; Security Status for halted securities and
  those resumed today; latest VCM Trigger per security in cooling off; **Add Order for all
  non-empty books**; Add Odd Lot Order; Aggregate Order Book (NoEntries 0 for an emptied
  book); Broker Queue (ItemCount 0 likewise); latest Order Imbalance, Closing, IEP,
  Nominal, Reference Price, Statistics, Market Turnover, Currency Rate, Yield, Index,
  Stock Connect; all News. **Not** Trade, Trade Cancel or Trade Ticker (SPEC p.64, v1.2):
  trades lost in a gap are not recoverable from refresh.
- **Order of recovery:** reference data channels first; other channels are processed
  only after full reference data (DG p.20, p.24, 10.1).
- **Sequence Reset 100** (SPEC p.11-12, 3.4.2; DG p.25, 10.4): first message of the day
  on each channel; may be repeated (OMD-C restart before open, DG p.25, 10.5; reference
  data rectification before open). Set next = NewSeqNo (1), clear cached data, recover
  from refresh. The first packet after it should have SeqNum 1; otherwise the packet is
  lost: recover from the other line or the RTS (DG p.8, p.25). A Sequence Reset on a
  real-time channel during a refresh cycle voids the cycle (DG p.20). On a refresh
  channel it clears the cached refresh messages (DG p.27). Scope of the clear: section 10.
- **DR Signal 105** (SPEC p.12; DG p.27-29, 10.9): status 1, clear all cached market data;
  status 2, rebuild from refresh; repeated until end of day. Multicast addresses stay the
  same; RTS moves to the DR-site servers. Failover target 5 minutes.
- **Intraday restart** (DG p.25-26, 10.6): no multicast at all, RTS disconnected; HKEX
  notifies clients; after restart no Sequence Reset is to be relied on: clear and recover
  from refresh.
- **Component failover** (DG p.26-27, 10.7): streaming channels (SF order book): one line
  only, arbitration covers it. Conflated channels (reference data, trading session
  status, broker queue): a few seconds' outage, then latest values resent.

## 6. Semantics

- **Book type (SF):** order by order, board lots, full depth (SPEC p.23-25, 3.9.1-3.9.3;
  DG p.23, 9). Odd lots are a separate book per security (SPEC p.50, section 5) on the
  complimentary odd lot feed (33, 34).
- **Order identity:** OrderId is unique **per security** within the day, not consecutive
  (SPEC p.23); the order map key is (SecurityCode, OrderId) (DG p.23).
- **Add Order 30:** price, quantity, side 0 bid / 1 offer, OrderType 1 market / 2 limit;
  market orders have Price 0 (DG p.23). OrderBookPosition is always 0 for securities
  (SPEC p.23-24), so the feed gives no queue rank: priority is the arrival order of Add
  Orders.
- **Modify Order 31:** quantity is the only attribute that can change (SPEC p.24). No
  price, no flag saying whether priority is kept. The priority effect of a size increase
  is not stated (section 10).
- **Delete Order 32:** order removed (SPEC p.25).
- **Executions:** Trade 50 carries SecurityCode, TradeID, price, quantity, TrdType and
  TradeTime, but no OrderId and no side (SPEC p.29-30). The book change from an execution
  can only come from Modify/Delete Order; that it does is not stated (section 10).
- **Trade Cancel 51:** identifies (SecurityCode, TradeID); no book effect stated.
- **Trade types** (SPEC p.30): 0 automatch, 103 auction, 100 automatch internalized are
  on-book matches; 4 late, 22 non-direct off-exchange, 101 direct off-exchange, 104
  overseas are not; 102 odd lot trades against the odd lot book.
- **Auctions:** "The full order book information is not available in Pre-Opening Auction
  Session and Closing Auction Session" (SPEC p.23, 3.9). IEP 41 and Order Imbalance 56
  describe the auction; Reference Price 43 gives the reference price and order-input
  limits (SPEC p.33-34; Appendix B p.63). What the SF order messages do at the start and
  end of POS and CAS is not stated (section 10).
- **VCM:** VCM Trigger 23 starts a cooling-off period with a price band; trading
  continues inside the band (SPEC p.34-35). VCMFlag in Security Definition.
- **Trading states:** Trading Session Status 20 is per market segment (MAIN, GEM, NASD,
  ETS), not per security; sub-IDs and statuses in section 4; on its own channel, so it
  races with order data (SPEC p.8, 2.4; p.21, 3.8.1; DG p.22, 7). Security Status 21:
  2 halt or suspend, 3 resume, per security, on the order book channels in SF (CG p.8).
- **Book clears (SF):** on Sequence Reset, DR Signal 1, intraday restart and refresh
  rebuild. SF has no per-security clear message.
- **Price-level products (SS, SP):** Aggregate Order Book Update 53, board lots, 10 tick
  levels per side (a level more than 9 spreads from the best is not maintained), entries
  addressed by PriceLevel and applied one by one; New inserts and shifts lower levels
  down, Delete removes and shifts up; the client deletes levels pushed past 10 price
  levels (implicit deletion); the server deletes levels pushed past 10 ticks while within
  10 levels (explicit deletion) and re-sends levels revealed by a shrink; 74 clears both
  sides of the security (SPEC p.50-57; DG p.22, 8). After a conflated-channel failover:
  74 then New entries for securities updated during the outage (DG p.26-27). The test
  replays examples 1-6 with these rules and reaches the specification's books, except one
  quantity the specification itself contradicts (section 10).
- **Broker queue 54:** top 40 broker IDs of one side, best price first, with spread
  entries (Type S: Item = spreads from best; Item 0 = no brokers at the previous S level);
  Side 1 buy / 2 sell; conflated, not synchronized with SP/SF (SPEC p.27-28).
- **Trade Ticker 52 (SS):** trades at one price aggregated; a cancel resends the ticker
  with the remaining quantity (SPEC p.31-32).
- **Nominal, closing price, statistics (SS, SP only):** Nominal Price may follow any book
  or trade change; before the first one it equals PreviousClosingPrice; may be 0 (SPEC
  p.32-33). Closing Price near end of day, 0 if none (p.32). Statistics after trades,
  excluding overseas trades (p.35-36). An SF handler that needs them derives them from
  Trade 50 and Security Definition.

## 7. Mapping to Kaspar

The handler follows `.claude/skills/build-feed-handler/kaspar_mapping.md`. OB, TachBook and
the record format are unchanged; every SF event is expressed in the existing L3 records
(`chutil/include/bfile/r_l3.hpp`), emitted the way `mdp3::handler_if` emits them
(`mdp3/include/mdp3/handler_if.hpp`).

### Conventions and record fields

- **Venue code:** `venue` = `en::x::HKEX` on every record, for every channel.
- **Actions and sides:** `orderUpdateAction` 0 new, 1 change, 2 delete. `side` `'0'` buy,
  `'1'` sell, from Side 0 bid / 1 offer. `order_flags` stays 0.
- **`securityID`:** the Kaspar asset id of the SecurityCode (Uint32, 1-99999, never reused,
  SPEC p.16). The instrument loader builds the map from Security Definition 11 through the
  universe file (section 8). The map is global, not per channel: a security's channel
  among 30-38 changes daily (CG p.8).
- **`orderID`:** OrderId is unique only per security within the day (SPEC p.23; DG p.23),
  and its range is not stated, so it cannot be packed with SecurityCode into 64 bits
  safely. The handler derives `orderID` from a 64-bit counter, incremented on every Add
  Order and never reset while the process runs. Its order map is keyed by
  (SecurityCode, OrderId) and holds (`securityID`, `orderID`, side, price, remaining
  quantity, OrderType). Modify and Delete look the order up by (SecurityCode, OrderId).
- **`priority`:** the same counter value as `orderID`: Add Orders in arrival order.
  OrderBookPosition is always 0 for securities (SPEC p.23-24) and is not used.
- **Time:** `sendingTime` = packet SendTime (ns since epoch UTC, millisecond precision).
  Order messages carry no timestamp, so `transactTime` is derived from SendTime: each
  event gets max(SendTime, previous event's `transactTime` + 1), as `Mbp2Mbo::stamp`
  does, and every record of one event shares it. OB checks the book for a cross when
  `transactTime` advances and rejects live records stamped at or before the last
  recovery record (`frame_kaspr/src/OB.cpp` 1995-2016), so the stamp strictly increases
  from event to event, across real-time, refresh and retransmitted messages.
- **Price:** `pxd` = Price / 1000 (Int32, 3 implied decimals in every price field, SPEC
  p.23, p.29). Securities do not trade at zero or negative prices, so every SF book is
  routed to OB. The only zero price is a market order (OrderType 1, Price 0, DG p.23):
  it is kept in the order map with no record sent, and its Modify and Delete send none
  (section 10).
- **Quantity:** shares, whole units, no scaling (unit = 1 share). Quantity is Uint32 and
  fits `displayQty`; `lastQty` and `sz` are `int32_t`, and a quantity above 2^31 − 1 is
  logged as an error and capped (section 10).

### Messages to records

| Venue message | Kaspar records, in order | Notes |
|---|---|---|
| Add Order 30, OrderType 2 | `l3_mbo_v2_t` action 0: `orderID`, `priority`, `side`, `pxd`, `displayQty` = Quantity | Insert into the order map. |
| Add Order 30, OrderType 1 (Price 0) | none | Kept in the map, not in the book (section 10). |
| Modify Order 31, not an execution | `l3_mbo_v2_t` action 1: price and side from the map, `displayQty` = Quantity | Priority below. |
| Delete Order 32, not an execution | `l3_mbo_v2_t` action 2: price and side from the map | Erase from the map. |
| Modify Order 31 or Delete Order 32 that is an execution (see "Trades") | 1. `l3_mbo_trd_v2_t`: `orderID`, `lastQty` = the reduction. 2. `l3_mbo_v2_t` action 1 with the remaining quantity (Modify), or action 2 (Delete) | One `transactTime` on both. |
| Trade 50, every TrdType | `l3_mbp_trd_t`: `pxd` = Price, `sz` = Quantity, `numorders` = 1; `side` and `aggresorSide` from the matched resting order when "Trades" finds one (`side` its side, `aggresorSide` the other in CME's encoding, 1 buy, 2 sell), otherwise `aggresorSide` 0 | One print per Trade; volume is counted from these prints. |
| Trade Cancel 51 | none | Logged. The bust stays in volume (section 10). |
| Security Status 21; Trading Session Status 20 | `l3_sst_t` per security when its derived status changes | "Trading status" below. |
| VCM Trigger 23, Reference Price 43, IEP 41, Order Imbalance 56 | none | Auction and band information; they do not change the book. |
| Security Definition 11, Market Definition 10, Liquidity Provider 13, Currency Rate 14 | none | Instrument loader (section 8). |
| Add/Delete Odd Lot Order 33/34 | none | The odd-lot book is separate from the board-lot book (SPEC p.50); the complimentary odd lot channels are not routed to OB. Odd-lot trades still arrive as Trade 50, TrdType 102. |
| refresh Add Order 30 (OrderType 2) | `l3_mbo_v2_t` action 0, `recovery = true`, `orderID` and `priority` from the counter in refresh order | Preceded by `l3_chr_v2_t` (below). OB requires recovery records to be adds (`OB.cpp` 2165). |
| refresh Security Status 21, Trading Session Status 20 | `l3_sst_t` as for real-time | |
| Sequence Reset 100; DR Signal 1; intraday restart; start of a refresh rebuild | `l3_chr_v2_t` | "Resets, clears and gaps" below. |
| gap not closed by the other line | `l3_gap_v2_t` | "Resets, clears and gaps" below. |
| packet end | `l3_eop_t`: `msgSeqNum` = the packet's SeqNum, `sendingTime` = SendTime | One per real-time packet, as `handler_if::EndOfPacket`. A heartbeat (MsgCount 0) sends none. |
| SS/SP Aggregate Order Book Update 53, Trade Ticker 52, Closing Price 62, Nominal Price 40, Statistics 60, Market Turnover 61, Yield 44, News 22, Broker Queue 54, Index 70/71, Stock Connect 80/81 | none | Not part of SF, or not book data. An SS/SP handler would turn 53 entries into by-price levels through `sim::Mbp2Mbo` with `tick` 0.001, the grid every HKEX spread table is a multiple of, and send UpdateAction 74 as `l3_chr_v2_t` to that security's book only. |

### Priority on change

Modify Order 31 changes quantity only; the specification states nothing about priority
and gives no queue position (SPEC p.24; OrderBookPosition always 0). The venue saying
nothing, the handler sends action 1 and OB's rule decides: a smaller size keeps the
order's place, a larger size sends it to the back. OB's rule is assumed (section 10). A
price change cannot occur on Modify; if HKEX sends it as Delete and Add with a new
OrderId, the handler maps it as a delete and an add with a new `orderID`, which is what
the venue did.

### Trades

- SF reports each trade once, as Trade 50; there is no auction summary or second trade
  report on SF, and refresh carries no trades (SPEC p.64). The handler sends one
  `l3_mbp_trd_t` per Trade 50, at the Trade's price, including off-book types (4 late,
  22 and 101 off-exchange, 104 overseas) and odd lot (102), which have no board-lot
  order behind them.
- Trade carries no OrderId and no side (SPEC p.29-30), so the order-level execution is
  inferred. The handler reads the whole packet before sending its records. A Trade with
  TrdType 0 or 100 (on-book automatch) at price P and quantity Q is matched to the size
  reductions (Modify to a smaller size, or Delete) in the same packet of orders resting at
  P on one side, in message order, until Q is used up. Each matched reduction is sent as
  an execution (`l3_mbo_trd_v2_t` then the book change); unmatched reductions are sent as
  plain changes and deletes. The executed orders' side sets the print's `side` and
  `aggresorSide`. This relies on HKEX sending Modify/Delete for executed orders and on a
  match's messages sharing a packet, neither of which is stated (section 10).
- TrdType 103 (auction) is sent as a print only. The order book is not published during
  the auction sessions (SPEC p.23), so there is no reduction to match.
- Trade Cancel 51 has no record. OB fills simulated orders from `l3_mbo_trd_v2_t`, not
  from prints, so a bust changes only the volume count (section 10).

### Event boundaries

OMD-C has no transaction marker, so `endOfEvent` is set on every record. The one
exception is an execution: its `l3_mbo_trd_v2_t` and book change share a `transactTime`
and `endOfEvent` is set on the book change only. A packet boundary is not used as an event
boundary.

### Trading status

OB matches only in `tradingStatus` 17 (`frame_kaspr/include/frame/ob/act/OB.hpp:456`). The
handler keeps, per market segment, the last TradingSessionSubID from Trading Session
Status 20, and per security the last SuspensionIndicator from Security Status 21. A
security's status is 2 TradingHalt while it is halted or suspended (21 with 2), otherwise
its segment's status below. On a change it sends `l3_sst_t` (`haltReason` 0,
`tradingEvent` = the TradingSessionSubID) to the security's book.

| TradingSessionSubID (SPEC p.22) | `tradingStatus` |
|---|---|
| 100 not yet open | 18 NotAvailableForTrading |
| 1 POS order input; 5 CAS order input | 21 PreOpen |
| 101 POS no cancellation; 108 POS random matching; 106 CAS no cancellation; 107 CAS random close | 24 PreCross |
| 2 POS order matching; 4 CAS order matching | 25 Cross |
| 105 CAS reference price fixing | 15 NewPriceIndication |
| 3 continuous trading | 17 ReadyToTrade |
| 7 blocking | 18 NotAvailableForTrading |
| 102 exchange intervention | 2 TradingHalt |
| 103 close | 4 Close |
| 104 order cancel; 0 day close | 26 PostClose |

A SubID not in the table is mapped from TradingSesStatus: 10 → 2, 20 → 21, 30 → 17,
40 → 21, 50 and 100 → 4, 0 → 18. Trading Session Status arrives on channel 2, not
synchronized with the order channels (SPEC p.8, 2.4): the status can take effect a few
messages early or late around a session change (section 10).

### Resets, clears and gaps

| Event | Records |
|---|---|
| Sequence Reset 100 on an order book channel (30-38) | `l3_chr_v2_t` to every book of that channel, then clear that channel's part of the order map and its securities' status; recover from refresh. The books of a channel are the securities the handler has seen on it that day, since the assignment changes daily (CG p.8). Scope: section 10. |
| Sequence Reset 100 on channel 1 or 2 (reference data, session status) | No book record; clear the reference data or segment states and recover them from refresh. |
| DR Signal 105 status 1; intraday restart (DG p.25-26) | `l3_chr_v2_t` to every book on every channel; clear the order map and all status; DR Signal 2 or restart: recover from refresh. |
| Start of a refresh rebuild of an order book channel | `l3_chr_v2_t` to every book of the channel and clear its part of the order map, then the refresh records with `recovery = true`, then the cached real-time messages above LastSeqNum (section 5). A book empty in the refresh gets no Add Order and stays cleared. |
| One security cleared | SF has no per-security clear (section 6). SS/SP UpdateAction 74 is the only one; see the SS/SP row above. |
| Gap that the other line has not filled | `l3_gap_v2_t` to every book of the channel, as `handler_if::Gap`; then RTS or refresh (section 5). A gap filled by line arbitration sends nothing. |

## 8. Instruments

- **Identifier:** SecurityCode, Uint32, 1-99999, never changes once allocated (SPEC p.16;
  DG p.11, 5.2.3). Map to Kaspar `securityID` (int32) from the universe; the security's
  channel among 30-38 changes daily (CG p.8), so the map must not be per channel.
- **Definitions:** Security Definition 11 on reference data channel 1 shortly after start
  of day, and in refresh; may be resent intraday (FreeText) (SPEC p.7, 2.2.1; p.16).
  Fields: MarketCode, ISIN, InstrumentType, ProductType, SpreadTableCode, LotSize,
  CurrencyCode, PreviousClosingPrice, VCM/CAS/POS flags, POS limits,
  DomainStmtSecurityCode (single tranche multiple counters), bond and warrant fields.
- **Price scaling:** every price is Int32 with 3 implied decimals (SPEC p.23, p.29, ...);
  turnover Int64 3 decimals; index values 4 decimals, EAS 2 (p.39-40). Variable decimals
  only for FaceValue, CallPrice, Entitlement, given by a Decimals field (SPEC p.18-19; DG
  p.11 example CallPrice 1234567 with 5 decimals = 12.34567).
- **Tick (spread table):** SpreadTableCode 01 Part A, 03 Part B, 04 inline warrants (Part
  E up to HK$1.00), 05 Part D, 06 Part E (added for the 2025 minimum spread reduction,
  SPEC p.71) refers to the Second Schedule of the Rules of the Exchange, which is not
  part of these documents. Ticks depend on price within each table; the ladders must be
  loaded from the Rules. Kaspar's `ref::Price` converts with one `units` per asset
  (`frame_ref/include/frame/ref/Price.hpp`), so a price-dependent tick needs `units` =
  0.001 (the feed's grid) and a separate ladder for "next tick" logic.
- **Quantity:** shares, Uint32; board lot LotSize per security; odd lots are a separate
  book.
- **Currency:** CurrencyCode per security; Currency Rate 14 gives HKD per 10^CurrencyFactor
  units, 4 decimals (SPEC p.21).
- **Lifecycle:** ListingDate, DelistingDate; DummySecurityFlag Y marks a dummy security
  (SPEC p.17). Previous closing price 0 on a first listing day (SPEC p.19 note).

## 9. Pitfalls

**1. Reading the specification**

- Several documents: SPEC 1.47, DG 1.15 (1.16 differs in 10.10 only), CG 2.9; all current
  on the HKEX page on 2026-11-18 (section 1).
- Errata and change tables: none beyond the document histories; read for SPEC 1.40-1.47
  (DomainStmtSecurityCode added 1.40, spread table 06 added 1.43, ProductType 14 removed
  1.45) and DG 1.10-1.16.
- Tables across page breaks: Security Definition (p.16-19), Trading Session Status
  (p.21-23), Trade (p.29-30), Trade Ticker (p.31-32), VCM (p.34-35), Statistics (p.35-36),
  News (p.37-38) span pages; every row matched against `pdftotext` and the rendered page;
  offsets checked contiguous by `fieldgen --check`.
- Offsets and length fields: 0-based, MsgSize counts itself (SPEC p.10); PktSize counts the
  header (DG p.6). Table offsets are the specification's minus 2 (section 4).
- Reserved fields: every Filler kept, including the 62-, 37-, 44- and 63-byte fillers of
  Security Definition.
- Enumerations elsewhere: spread tables in the Rules of the Exchange (not provided);
  currency codes in the Third Schedule (SPEC p.9); index codes in Appendix A.
- Names differing between table and prose: "Add Odd Lot Oder" (SPEC p.10, CG p.9), "OrderId"
  vs "OrderID" (DG p.23), "SecurityTradingStatus" renamed SuspensionIndicator (SPEC p.65).
  No effect.
- Examples contradicting tables or each other: section 5 example 4 result (SPEC p.54) shows
  9660 at 200 where example 3 left 150; DG p.7 example struct declares SeqNum `unsigned
  long` (8 bytes on LP64) where the table says Uint32; DG Appendix E alignment differs
  from the text (section 10).
- Two layers in one table: packet header, multicast messages, group entries, RTS
  from/to are separate sections; RTS type codes (101, 102, 201, 202) do not collide with
  data codes.
- Message names repeated with different codes: none. Field names repeated within a
  message (Filler, MaturityDate in Security Definition) are suffixed with their offset.
- Variable-length trailing field: five messages with repeating groups (section 3);
  modelled as a trailing `bytes` field, not a member.
- Odd integer widths: none.

**2. Framing and encoding**

- Endianness: little-endian throughout, including RTS.
- Signed or unsigned: prices Int32, turnover Int64, TrdType Int16; quantities unsigned.
  Two's complement (SPEC p.9, null value).
- Fixed-point prices: global 3 decimals; per-field decimals only for FaceValue, CallPrice,
  Entitlement.
- Null and sentinel values: Int64 0x8000000000000000 (SPEC p.9); 0 means "not available"
  in many price fields (IEP, Reference Price, POS limits, Closing Price, Nominal Price);
  ListingDate 19000101 unknown; DelistingDate 0 none; OrderImbalanceDirection space.
- Strings: space padded, except Logon Username (NUL padded, SPEC p.13); Binary is
  UTF-16LE.
- Several messages per packet: yes, MsgCount; never split across packets (SPEC p.10). RTS
  may frame retransmitted messages differently (SPEC p.46).
- Timestamps: ns since Unix epoch UTC; SendTime millisecond precision, TradeTime
  microsecond, session/VCM/news times second. Order messages have none.

**3. Sequencing and recovery**

- Scope: per channel, per day; refresh channels have their own sequence (DG p.18).
- A/B arbitration: Kaspar's `mdp3::MessageProcessor` keys packets by packet sequence
  number (`mdp3/include/mdp3/act/MessageProcessor.hpp` 207-216); OMD-C needs arbitration
  per message because A and B pack messages differently (SPEC p.45). The existing code
  cannot be reused unchanged.
- Gap rule: section 5; heartbeat SeqNum reveals trailing loss.
- Recovery mechanism: RTS (TCP, request/response, small gaps) and refresh (multicast
  snapshot cycles).
- Snapshot alignment: discard cached <= LastSeqNum, apply > LastSeqNum (SPEC p.49).
- Snapshot too old: wait for the next cycle; refresh is continuous, no request needed;
  RTS can fill between LastSeqNum and the cache if within 50,000 messages (reading).
- State to clear on reset: books, the (SecurityCode, OrderId) map, odd-lot map, status,
  pending trades; scope per channel or all channels (section 10).
- Recovery paths: to be exercised by injected gaps, duplicates, Sequence Reset mid-refresh,
  DR Signal, RTS rejects 2/100/101.
- Session behaviour: no logon on multicast; RTS logon, 30 s heartbeats echoed within 5 s,
  1,000 requests a day, one session per user.

**4. Book semantics**

- MBO (SF); price level by level index (SS, SP).
- Level-index updates: New inserts and shifts down; Delete shifts up; applied entry by
  entry (SPEC p.52; DG p.22). Checked against examples 1-6 in the test.
- Depth-limited books (SS/SP): 10 ticks / 10 levels, implicit and explicit deletions,
  revealed levels re-sent (SPEC p.50-57).
- Queue priority on modify: not stated; Modify changes quantity only; OrderBookPosition
  always 0 (section 10).
- Implied, iceberg, hidden: not mentioned in any document.
- Trades also as book deltas: Trade has no OrderId; book changes from executions must come
  through Modify/Delete (not stated, section 10). Do not apply Trade to the book.
- Trade side: not reported (neither resting nor aggressor).
- Trading states: per segment (20) and per security (21); crossed-book legality not
  stated; order book data absent in POS and CAS (SPEC p.23).
- Book clears: Sequence Reset, DR, refresh; SS/SP UpdateAction 74 per security.

**5. Prices, quantities and instruments**

- Price or yield: price; Yield 44 (SS/SP) is informational for bonds.
- Quantity multipliers: none; shares; board lot per security.
- Decimals sentinels: DecimalsInFaceValue/CallPrice/Entitlement not applicable when the
  value is 0.
- Price-dependent ticks: yes, spread tables in the Rules of the Exchange (section 8).
- Instrument definitions: Security Definition on channel 1 and refresh 501.
- Lifecycle: new listings (PreviousClosingPrice 0), delisting dates, dummy securities,
  daily reassignment of securities to channels 30-38.

**6. Behaviour the specification omits**

- Subscription options: none (multicast).
- History on subscribe: none; refresh on its own channels.
- Snapshot is the whole book: yes, every order of every non-empty book (SPEC p.48).
- Order within an event: Trade relative to Modify/Delete of the matched orders is not
  stated (section 10).
- Connection limits: RTS logon timeout 5 s, heartbeat response 5 s; multicast stops about
  18:30.

No sample capture was checked: HKEX publishes no sample OMD-C data on its OMD-C page
(only a Missing Index Report CSV); captures are sold by third parties (e.g. LSEG Tick
History PCAP).

**7. Post-trade** — not applicable: OMD-C is market data only.

- FIX version: not applicable.
- Venue dictionary: not applicable.
- Credentials in Logon: RTS Logon carries a username only; authorization also by client IP
  (SessionStatus 5, SPEC p.13).
- Missing-field Reject: not applicable.
- Requests before session open: not applicable.
- Trade capture subscription: not applicable.
- Report lifecycle: Trade Cancel is the only correction.
- Duplicates on reconnect: retransmitted messages carry their original sequence numbers;
  dedupe by sequence.
- Fixed-income fields: bond fields in Security Definition; not a post-trade concern.
- Daily sequence reset: Sequence Reset each morning on every channel.

**8. Order entry** — not applicable: OMD-C cannot enter orders.

- Client ids: not applicable.
- Cancel and replace ids: not applicable.
- Fill races: not applicable.
- Unsolicited cancels: not applicable.
- Busts as fills: not applicable.
- Throttling: not applicable (RTS limits are in section 2).
- Reconnect duplicates: not applicable.
- Orders placed elsewhere: not applicable.
- Short sales: not applicable (ShortSellFlag is informational).

## 10. Ambiguities

| Question | Reading taken | Alternatives | Resolved by |
|---|---|---|---|
| Do the field tables match the wire? | As the tables say; every message decodes from bytes built at the specification's offsets. | — | Not resolved: no capture. HKEX publishes none; third-party captures are commercial. |
| Priority on Modify Order: size down, size up. Quantity is the only modifiable attribute; OrderBookPosition is always 0 (SPEC p.23-24). | The venue says nothing, so the handler sends action 1 and OB's rule is assumed: size down keeps priority, size up loses it (kaspar_mapping.md, priority on a change). | Size up keeps priority (cannot be expressed: OB resets it; a known deviation); or a size increase arrives as Delete + Add with a new OrderId (mapped as such, no deviation). | The Rules of the Exchange, or a capture. Decides simulated queue position after a size increase. |
| Are executions reported as Modify/Delete Order of the resting orders in addition to Trade? Trade has no OrderId (SPEC p.29). | Yes: book state is changed only by order messages; Trade 50 gives the print (`l3_mbp_trd_t`) and is matched to the reductions to send `l3_mbo_trd_v2_t` (section 7, Trades). | Trade implies a reduction the client must apply (then the order messages would not come, and the handler would have to reduce the orders at the front of the queue at the Trade's price itself). | Capture: check that the book stays consistent and uncrossed using order messages only. |
| Order of a Trade and the Modify/Delete messages of the same match, and whether they share a packet. | Unknown. The handler reads the whole packet, then matches each on-book Trade (TrdType 0, 100) to the size reductions at its price in the same packet, in message order, so the order of the messages within a packet does not matter. A reduction in another packet is sent as a plain change or delete: no `l3_mbo_trd_v2_t`, so simulated orders behind it are not filled (known deviation). Aggressor side is the opposite of the matched orders' side, 0 when nothing matches. | Match across packets (hold records until the next packet); or emit no `l3_mbo_trd_v2_t` at all. | Capture: the share of on-book Trades whose reductions share their packet. |
| Orders during POS and CAS: "full order book information is not available" (SPEC p.23). Are auction orders sent as Add Order at the end of the auction? Are existing orders deleted at CAS start? | Book messages pause during POS/CAS; the handler keeps the book, the status is non-matching (21, 24, 25), and auction trades (TrdType 103) are prints only, with no `l3_mbo_trd_v2_t`. | A burst of Add/Delete at session transitions; or Add Orders during auctions with market orders at price 0. | Capture across 09:00-09:30 and 16:00-16:10. |
| Market orders (OrderType 1, Price 0, DG p.23) in the book. | Keep in the order map with no record sent (OB does not accept a zero price); Modify and Delete of such an order send none. | Place at the best price; or they never rest outside auctions. | Capture. |
| Scope of the clear on Sequence Reset: "all instruments received from the channels" (SPEC p.12), "for all instruments" (SPEC p.7), "on that channel" (DG p.25, which SPEC 1.41 says it was aligned with). | Clear the state of the channel that sent it; securities move between channels 30-38 daily, so clear by the channel's order map, not by a static security list. | Clear every channel. | HKEX. |
| Sequence number of Sequence Reset: it "carries sequence number 1" (SPEC p.7) but next expected becomes NewSeqNo = 1 and the next packet "should have" SeqNum 1 (DG p.25). | Sequence Reset is outside the sequence: do not count it; the first data message is 1. | SR is message 1 and data starts at 2. | Capture of start of day. |
| Refresh LastSeqNum 0 with real-time heartbeats at SeqNum 1 (DG p.19 note). | Nothing to discard; next expected 1. | — | Capture. |
| Alignment rule: SPEC p.49 and DG p.19, p.25 discard <= LastSeqNum; DG Appendix E (p.36) applies cached ">= expectedSeqNum", a refresh-channel variable. | The text: apply > LastSeqNum. | Appendix E as printed. | Settled by the two documents' text; Appendix E taken as loose pseudo code. |
| A refresh cycle older than the start of the real-time cache. | Fill LastSeqNum+1 .. first cached − 1 from the RTS if within limits, else wait for the next cycle. | Always wait. | Not stated; design choice. |
| Queue order within a price level from refresh Add Orders. | Arrival order in the refresh = time priority. | Arbitrary order. | HKEX or capture (compare with the live-built book). |
| Event boundaries: no event flag, no per-message timestamp on order messages. | `endOfEvent` on every record, an execution's `l3_mbo_trd_v2_t` and book change being one event; `transactTime` = max(SendTime, previous + 1) per event (as `Mbp2Mbo::stamp`). | Per packet (rejected: an event can span packets and a packet can hold several). | Settled by kaspar_mapping.md (event boundaries). |
| Heartbeat and gaps: DG p.12 says to ignore heartbeats in gap detection, and in the same section's Table 3 detects a gap from a heartbeat. | A heartbeat does not advance `next`; SeqNum >= next means messages up to SeqNum were lost. | Ignore heartbeats entirely. | Settled by DG Table 3. |
| Request packet header SeqNum/SendTime: SPEC p.13 "0, not used"; DG p.13-14 "optional" and "nanoseconds since 1970". | Send 0. | Send the time. | Either is accepted per both texts. |
| CurrencyFactor: "all price fields for this security" (SPEC p.21) in a per-currency message. | Applies to CurrencyRate only (HKD for 10^n units), as both examples show. | Prices of securities in that currency scale by 10^n. | Settled by the examples. |
| AOB example 4 result (SPEC p.54): 9660 at 200, after example 3 set 150 (p.53). | 150; the test reaches 150 from the entries. | — | Settled by the entries. |
| DG p.7 example struct: SeqNum `unsigned long`. | Uint32 per SPEC p.11 and DG Table 2 (p.6). | — | Settled by the tables. |
| OrderId uniqueness across the board-lot and odd-lot books of one security. | Separate maps per book; the odd lot channels are not routed to OB. | One map. | Capture of the odd lot feed; matters only if odd lots are routed. |
| Trade Cancel effect on the book and on volume. | No book effect (it cancels a reported trade, not orders). No record exists for a bust: the cancelled print stays in the volume count (known deviation; frequency not measured). | Orders reinstated by Add Order (handled as ordinary adds). | Capture. |
| Trading Session Status 20 is on channel 2, not synchronized with the order channels (SPEC p.8, 2.4). | Apply each status when it arrives; it can take effect a few messages early or late relative to order data around a session change (known deviation). | Delay order data until the status channel catches up (no common clock or sequence to do it with). | Not resolvable from the feed. |
| CME `SecurityTradingStatus` for HKEX auction phases (section 7, Trading status). | Order input 21, no cancellation and random matching/close 24, order matching 25, reference price fixing 15, order cancel and day close 26. | Any other non-17 code. | Only 17 changes OB's behaviour; the others are logged. |
| Quantities above 2^31 − 1 in `lastQty` and `sz` (`int32_t`); Quantity is Uint32. | Log and cap. | — | Not expected for board-lot orders; a capture would show the maximum. |
| Security Definition fields after NoUnderlyingSecurities: Total Length 544 + 8nU with UnderlyingSecurityCode at 544 and Filler at 548 (SPEC p.19). | Fixed part 544, then nU entries of 8 bytes. | — | Settled by the total length formula. |
