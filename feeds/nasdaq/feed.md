# Feed description: Nasdaq TotalView-ITCH 5.0

Nasdaq, BX and PSX TotalView-ITCH 5.0 over MoldUDP64 and SoupBinTCP, with the
GLIMPSE 5.0 snapshot service.

Every entry cites its source. Abbreviations:

- **ITCH**: Nasdaq TotalView-ITCH 5.0, revision of April 28, 2023. Pages are PDF
  pages; they equal the printed "PAGE n" footers on pages 1-34.
- **Mold**: MoldUDP64 Protocol Specification V 1.00, last revision 8/2/24.
- **Soup**: SoupBinTCP Version 3.00, last revision 02/22/2017.
- **GLIMPSE**: Nasdaq GLIMPSE Version 5.0, revision of August 22, 2024.
- **Sample**: Nasdaq's public sample file `S061226-v50.txt.gz` (first 50,000,000
  bytes after decompression) and `NOII/S050922-v50-NOII.txt.gz` (whole file), from
  https://emi.nasdaq.com/ITCH/Nasdaq%20ITCH/.

## 1. Venue and documents

| Document | Version | Date | Applies to |
|---|---|---|---|
| Nasdaq TotalView-ITCH 5.0 (`NQTVITCHspecification.pdf`, 36 pp.) | 5.0 | April 28, 2023 (latest entry of Appendix A, ITCH p.22) | Application messages on Nasdaq, BX and PSX. The same messages over MoldUDP64, SoupBinTCP and compressed SoupBinTCP (ITCH p.3, Architecture). |
| MoldUDP64 Protocol Specification (`moldudp64.pdf`, 6 pp.) | V 1.00 | 8/2/24 (formatting; last content change 7/7/09) (Mold p.6) | Multicast transport and retransmission requests. |
| SoupBinTCP (`soupbintcp.pdf`, 9 pp.) | 3.00 | 02/22/2017 (Soup p.9) | TCP transport of ITCH, and of GLIMPSE. |
| Nasdaq GLIMPSE (`NQGlimpseSpecification.pdf`, 16 pp., https://www.nasdaqtrader.com/content/technicalsupport/specifications/dataproducts/NQGlimpseSpecification.pdf) | 5.0 | August 22, 2024 (GLIMPSE p.12, Appendix A) | Snapshot of the Nasdaq, BX and PSX books over SoupBinTCP. The URL `.../dataproducts/GLIMPSE5.0.pdf` returns an HTML "Page Not Available". |

Which version applies: ITCH 5.0 is the only version Nasdaq publishes for
TotalView today; Appendix A records that the version number was rolled back to
5.0 on May 3, 2018 (ITCH p.22). The FPGA and software feeds carry the same
payload in the same order (ITCH p.4, FPGA version). The GLIMPSE 2024 revision
"unified BX, PSX, and Nasdaq GLIMPSE specifications" (GLIMPSE p.12), so one
specification covers the three venues.

Not covered by these documents: multicast addresses and ports (Mold p.2 points to
http://www.nasdaqtrader.com/Trader.aspx?id=FeedIPS, not read), the compressed
SoupBinTCP option (ITCH p.3 names it, no specification here), A/B feed
redundancy, and logon credentials.

## 2. Transport

- **MoldUDP64** (Mold pp.2-5). Downstream packets by UDP multicast, one
  well-known group per stream. Retransmission: a 20-byte Request Packet by UDP
  unicast to a re-request server; the reply is a normal downstream packet unicast
  to the requesting socket, so the multicast socket can also receive it (Mold
  p.5). A reply holds only as many messages as fit in one UDP packet; the client
  requests the rest (Mold p.5). Heartbeat: a packet with Message Count 0,
  "typically" once per second, carrying the next expected sequence number (Mold
  p.4). End of session: packets with Message Count 0xFFFF sent "for a short
  while" in place of heartbeats; re-requests are still served during that time
  (Mold p.4). The FPGA feed is MoldUDP64 only, from Carteret, NJ, and needs a 10 or
  40 Gb connection (ITCH p.4).
- **SoupBinTCP** (Soup pp.2-8). Login Request first, within "typically 30
  seconds" (Soup p.6); Login Accepted gives the session and next sequence number
  (Soup p.4); Login Rejected `A` not authorized, `S` session not available, then
  the socket is closed (Soup p.5). Heartbeats both ways after 1 second without
  sending; the server may close the socket after "typically 15 seconds" without
  hearing from the client (Soup p.3, 1.3). End of Session `Z`, then the
  connection is closed (Soup p.6). Logout `O` (Soup p.8). Username and password
  are case-insensitive, right-padded with spaces (Soup p.6).
- **GLIMPSE**: point to point, SoupBinTCP only; the client must log in for
  sequence 1 (GLIMPSE p.2, Architecture).
- Throttles: none stated in these documents.

## 3. Framing

- **MoldUDP64 packet** (Mold pp.3-4): 20-byte header (Session 10 ASCII, Sequence
  Number 8, Message Count 2), then Message Count message blocks. Each block is a
  2-byte big-endian Message Length that does not count itself, then that many
  bytes; total block size is length + 2 (Mold p.4). Blocks follow each other with
  no padding; the first starts at byte 20 (Mold p.4). A message may be 0 bytes
  (Mold p.4). Sequence Number is the number of the first message; later messages
  are numbered implicitly (Mold p.3).
- **ITCH message** (ITCH p.4): one type byte at offset 0, then fixed fields. In
  `itch50.tbl` the MoldUDP64 block length is declared as
  `@length_prefix 2 exclusive`, so `decode` starts at the block length and
  `size` steps to the next block. Nasdaq's sample files use the same 2-byte
  length before every message, with no MoldUDP64 header (Sample: first bytes
  `00 0c 53 ...`, a 12-byte System Event).
- **SoupBinTCP packet** (Soup p.2, 1.1): 2-byte big-endian length counting the
  type byte and payload, not itself; 1 type byte; payload. One Sequenced Data
  packet carries one higher-level message with no inner length (Soup p.5, 2.2.3).
  ITCH over SoupBinTCP and GLIMPSE therefore have no 2-byte ITCH length; GLIMPSE
  is decoded from `soup::from_nasdaq`'s `tail` with `glimpse::from_nasdaq::decode`
  (`glimpse50.tbl`, no length prefix).
- **Encoding**: integers big-endian, unsigned unless noted, and none is noted
  (ITCH p.4; Mold p.2; Soup p.4, 1.5). Alpha: ASCII, left justified, right-padded
  with spaces (ITCH p.4). Two-character codes shorter than the field are padded
  the same way: Issue Sub-Type `Z` is `5a 20` (Sample). Prices: unsigned integers
  with implied decimals, Price(4) or Price(8); the largest Price(4) is
  200,000.0000 (`0x77359400`) (ITCH p.4). Timestamps: 6-byte unsigned,
  nanoseconds since midnight (ITCH p.4).
- **Field position**: Stock Locate is at offset 1 in every ITCH message (ITCH
  p.3). The Reg SHO message names the field Locate Code (ITCH p.9, 1.2.3); the
  table keeps that name, so code that reads the locate generically reads bytes
  1-2.
- Alignment: none; all structures are packed.

## 4. Message catalogue

Generated by `fieldgen.py --markdown` from `itch50.tbl`, `moldudp64.tbl`,
`soupbintcp.tbl` and `glimpse50.tbl`. Offsets are 0-based from the type byte;
the length prefix, where a section has one, is not counted. Message counts:
ITCH 23, MoldUDP64 2 (untyped), SoupBinTCP 10, GLIMPSE 9.

### itch::from_nasdaq

#### SystemEvent (type `S`, 12 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | SystemEvent: market or feed handler event (ITCH 5.0, 1.1, p.4) |
| StockLocate | 1 | 2 | Integer | Always 0 |
| TrackingNumber | 3 | 2 | Integer | Nasdaq internal tracking number |
| Timestamp | 5 | 6 | Integer | Nanoseconds since midnight |
| EventCode | 11 | 1 | Alpha | O start of messages, S start of system hours, Q start of market hours, M end of market hours, E end of system hours, C end of messages (p.5) |

#### StockDirectory (type `R`, 39 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | StockDirectory: one per active symbol at start of day (ITCH 5.0, 1.2.1, pp.5-8) |
| StockLocate | 1 | 2 | Integer | Locate code assigned to the symbol for the day |
| TrackingNumber | 3 | 2 | Integer | Nasdaq internal tracking number |
| Timestamp | 5 | 6 | Integer | Nanoseconds since midnight |
| Stock | 11 | 8 | Alpha | Symbol, right-padded with spaces |
| MarketCategory | 19 | 1 | Alpha | Q G S Nasdaq tiers; N NYSE, A NYSE American, P NYSE Arca, Z BATS Z, V IEX; space not available |
| FinancialStatusIndicator | 20 | 1 | Alpha | D E Q S G H J K C N (Nasdaq-listed); space not available |
| RoundLotSize | 21 | 4 | Integer | Shares in a round lot |
| RoundLotsOnly | 25 | 1 | Alpha | Y only round lots accepted, N no size restriction |
| IssueClassification | 26 | 1 | Alpha | Appendix D (p.34) |
| IssueSubType | 27 | 2 | Alpha | Appendix E (pp.35-36) |
| Authenticity | 29 | 1 | Alpha | P live/production, T test |
| ShortSaleThresholdIndicator | 30 | 1 | Alpha | Y restricted under Rule 203(b)(3), N not, space not available |
| IPOFlag | 31 | 1 | Alpha | Y set up as new IPO, N not, space not available |
| LULDReferencePriceTier | 32 | 1 | Alpha | 1 Tier 1, 2 Tier 2, space not available |
| ETPFlag | 33 | 1 | Alpha | Y ETP, N not, space not available |
| ETPLeverageFactor | 34 | 4 | Integer | Integral leverage of the ETP, rounded down |
| InverseIndicator | 38 | 1 | Alpha | Y inverse ETP, N not |

#### StockTradingAction (type `H`, 25 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | StockTradingAction: trading state of a security (ITCH 5.0, 1.2.2, p.8) |
| StockLocate | 1 | 2 | Integer | Locate code identifying the security |
| TrackingNumber | 3 | 2 | Integer | Nasdaq internal tracking number |
| Timestamp | 5 | 6 | Integer | Nanoseconds since midnight |
| Stock | 11 | 8 | Alpha | Symbol, right-padded with spaces |
| TradingState | 19 | 1 | Alpha | H halted all US markets, P paused (Nasdaq-listed), Q quotation only, T trading on Nasdaq |
| Reserved | 20 | 1 | Alpha | Reserved |
| Reason | 21 | 4 | Alpha | Trading action reason code, Appendix C (pp.32-33) |

#### RegSHORestriction (type `Y`, 20 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | RegSHORestriction: Reg SHO short sale price test status (ITCH 5.0, 1.2.3, p.9) |
| LocateCode | 1 | 2 | Integer | Locate code identifying the security (the spec names this field Locate Code) |
| TrackingNumber | 3 | 2 | Integer | Nasdaq internal tracking number |
| Timestamp | 5 | 6 | Integer | Nanoseconds since midnight |
| Stock | 11 | 8 | Alpha | Symbol, right-padded with spaces |
| RegSHOAction | 19 | 1 | Alpha | 0 no price test, 1 restriction in effect (intraday drop), 2 restriction remains in effect |

#### MarketParticipantPosition (type `L`, 26 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | MarketParticipantPosition: market maker status in an issue (ITCH 5.0, 1.2.4, pp.9-10) |
| StockLocate | 1 | 2 | Integer | Locate code identifying the security |
| TrackingNumber | 3 | 2 | Integer | Nasdaq internal tracking number |
| Timestamp | 5 | 6 | Integer | Nanoseconds since midnight |
| MPID | 11 | 4 | Alpha | Market participant identifier |
| Stock | 15 | 8 | Alpha | Symbol, right-padded with spaces |
| PrimaryMarketMaker | 23 | 1 | Alpha | Y primary market maker, N not |
| MarketMakerMode | 24 | 1 | Alpha | N normal, P passive, S syndicate, R pre-syndicate, L penalty |
| MarketParticipantState | 25 | 1 | Alpha | A active, E excused/withdrawn, W withdrawn, S suspended, D deleted |

#### MWCBDeclineLevel (type `V`, 35 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | MWCBDeclineLevel: market-wide circuit breaker levels for the day (ITCH 5.0, 1.2.5.1, p.10) |
| StockLocate | 1 | 2 | Integer | Always 0 |
| TrackingNumber | 3 | 2 | Integer | Nasdaq internal tracking number |
| Timestamp | 5 | 6 | Integer | Nanoseconds since midnight |
| Level1 | 11 | 8 | Price8 | MWCB level 1, Price(8) |
| Level2 | 19 | 8 | Price8 | MWCB level 2, Price(8) |
| Level3 | 27 | 8 | Price8 | MWCB level 3, Price(8) |

#### MWCBStatus (type `W`, 12 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | MWCBStatus: a circuit breaker level was breached (ITCH 5.0, 1.2.5.2, pp.10-11; the table is headed "MWCB Decline Level Message") |
| StockLocate | 1 | 2 | Integer | Always 0 |
| TrackingNumber | 3 | 2 | Integer | Nasdaq internal tracking number |
| Timestamp | 5 | 6 | Integer | Nanoseconds since midnight |
| BreachedLevel | 11 | 1 | Alpha | 1, 2 or 3 |

#### IPOQuotingPeriodUpdate (type `K`, 28 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | IPOQuotingPeriodUpdate: anticipated IPO quotation release time (ITCH 5.0, 1.2.6, p.11) |
| StockLocate | 1 | 2 | Integer | The spec says always 0 |
| TrackingNumber | 3 | 2 | Integer | Nasdaq internal tracking number |
| Timestamp | 5 | 6 | Integer | Nanoseconds since midnight |
| Stock | 11 | 8 | Alpha | Symbol, right-padded with spaces |
| IPOQuotationReleaseTime | 19 | 4 | Integer | Seconds since midnight; 0 when cancelled or postponed |
| IPOQuotationReleaseQualifier | 23 | 1 | Alpha | A anticipated release time, C release cancelled/postponed |
| IPOPrice | 24 | 4 | Price4 | IPO price, Price(4); 0 when cancelled or postponed |

#### LULDAuctionCollar (type `J`, 35 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | LULDAuctionCollar: reopening auction collars after a LULD pause (ITCH 5.0, 1.2.7, pp.11-12) |
| StockLocate | 1 | 2 | Integer | Locate code identifying the security |
| TrackingNumber | 3 | 2 | Integer | Nasdaq internal tracking number |
| Timestamp | 5 | 6 | Integer | Nanoseconds since midnight |
| Stock | 11 | 8 | Alpha | Symbol, right-padded with spaces |
| AuctionCollarReferencePrice | 19 | 4 | Price4 | Reference price for the collars, Price(4) |
| UpperAuctionCollarPrice | 23 | 4 | Price4 | Upper collar, Price(4) |
| LowerAuctionCollarPrice | 27 | 4 | Price4 | Lower collar, Price(4) |
| AuctionCollarExtension | 31 | 4 | Integer | Number of extensions of the reopening auction |

#### OperationalHalt (type `h`, 21 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | OperationalHalt: operational halt on one Nasdaq market centre (ITCH 5.0, 1.2.8, p.12) |
| StockLocate | 1 | 2 | Integer | Locate code identifying the security |
| TrackingNumber | 3 | 2 | Integer | Nasdaq internal tracking number |
| Timestamp | 5 | 6 | Integer | Nanoseconds since midnight |
| Stock | 11 | 8 | Alpha | Symbol, right-padded with spaces |
| MarketCode | 19 | 1 | Alpha | Q Nasdaq, B BX, X PSX |
| OperationalHaltAction | 20 | 1 | Alpha | H operationally halted, T halt lifted |

#### AddOrder (type `A`, 36 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | AddOrder: new displayed order without MPID attribution (ITCH 5.0, 1.3.1, p.13) |
| StockLocate | 1 | 2 | Integer | Locate code identifying the security |
| TrackingNumber | 3 | 2 | Integer | Nasdaq internal tracking number |
| Timestamp | 5 | 6 | Integer | Nanoseconds since midnight |
| OrderReferenceNumber | 11 | 8 | Integer | Unique for the day |
| BuySellIndicator | 19 | 1 | Alpha | B buy, S sell |
| Shares | 20 | 4 | Integer | Displayed quantity |
| Stock | 24 | 8 | Alpha | Symbol, right-padded with spaces |
| Price | 32 | 4 | Price4 | Display price, Price(4) |

#### AddOrderMPID (type `F`, 40 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | AddOrderMPID: new displayed order with MPID attribution (ITCH 5.0, 1.3.2, p.13) |
| StockLocate | 1 | 2 | Integer | Locate code identifying the security |
| TrackingNumber | 3 | 2 | Integer | Nasdaq internal tracking number |
| Timestamp | 5 | 6 | Integer | Nanoseconds since midnight |
| OrderReferenceNumber | 11 | 8 | Integer | Unique for the day |
| BuySellIndicator | 19 | 1 | Alpha | B buy, S sell |
| Shares | 20 | 4 | Integer | Displayed quantity |
| Stock | 24 | 8 | Alpha | Symbol, right-padded with spaces |
| Price | 32 | 4 | Price4 | Display price, Price(4) |
| Attribution | 36 | 4 | Alpha | MPID of the entering participant |

#### OrderExecuted (type `E`, 31 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | OrderExecuted: part or all of a resting order executed at its display price (ITCH 5.0, 1.4.1, p.14) |
| StockLocate | 1 | 2 | Integer | Locate code identifying the security |
| TrackingNumber | 3 | 2 | Integer | Nasdaq internal tracking number |
| Timestamp | 5 | 6 | Integer | Nanoseconds since midnight |
| OrderReferenceNumber | 11 | 8 | Integer | The resting order executed |
| ExecutedShares | 19 | 4 | Integer | Shares executed; subtract from the order's displayed shares |
| MatchNumber | 23 | 8 | Integer | Unique for the day; referenced by BrokenTrade |

#### OrderExecutedWithPrice (type `C`, 36 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | OrderExecutedWithPrice: resting order executed at a price other than its display price (ITCH 5.0, 1.4.2, pp.14-15) |
| StockLocate | 1 | 2 | Integer | Locate code identifying the security |
| TrackingNumber | 3 | 2 | Integer | Nasdaq internal tracking number |
| Timestamp | 5 | 6 | Integer | Nanoseconds since midnight |
| OrderReferenceNumber | 11 | 8 | Integer | The resting order executed |
| ExecutedShares | 19 | 4 | Integer | Shares executed; subtract from the order's displayed shares |
| MatchNumber | 23 | 8 | Integer | Unique for the day; referenced by BrokenTrade |
| Printable | 31 | 1 | Alpha | N non-printable (counted in a later bulk print), Y printable |
| ExecutionPrice | 32 | 4 | Price4 | Execution price, Price(4) |

#### OrderCancel (type `X`, 23 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | OrderCancel: part of a resting order cancelled (ITCH 5.0, 1.4.3, p.15) |
| StockLocate | 1 | 2 | Integer | Locate code identifying the security |
| TrackingNumber | 3 | 2 | Integer | Nasdaq internal tracking number |
| Timestamp | 5 | 6 | Integer | Nanoseconds since midnight |
| OrderReferenceNumber | 11 | 8 | Integer | The order reduced |
| CancelledShares | 19 | 4 | Integer | Shares removed from the displayed size |

#### OrderDelete (type `D`, 19 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | OrderDelete: a resting order removed in full (ITCH 5.0, 1.4.4, p.15) |
| StockLocate | 1 | 2 | Integer | Locate code identifying the security |
| TrackingNumber | 3 | 2 | Integer | Nasdaq internal tracking number |
| Timestamp | 5 | 6 | Integer | Nanoseconds since midnight |
| OrderReferenceNumber | 11 | 8 | Integer | The order deleted |

#### OrderReplace (type `U`, 35 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | OrderReplace: order cancel-replaced under a new reference number; side, symbol and MPID carry over (ITCH 5.0, 1.4.5, pp.15-16) |
| StockLocate | 1 | 2 | Integer | Locate code identifying the security |
| TrackingNumber | 3 | 2 | Integer | Nasdaq internal tracking number |
| Timestamp | 5 | 6 | Integer | Nanoseconds since midnight |
| OriginalOrderReferenceNumber | 11 | 8 | Integer | The order replaced; remove it |
| NewOrderReferenceNumber | 19 | 8 | Integer | Reference used for all later updates |
| Shares | 27 | 4 | Integer | New displayed quantity |
| Price | 31 | 4 | Price4 | New display price, Price(4) |

#### Trade (type `P`, 44 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | Trade: execution of a non-displayed order; no book effect (ITCH 5.0, 1.5.1, pp.16-17) |
| StockLocate | 1 | 2 | Integer | Locate code identifying the security |
| TrackingNumber | 3 | 2 | Integer | Nasdaq internal tracking number |
| Timestamp | 5 | 6 | Integer | Nanoseconds since midnight |
| OrderReferenceNumber | 11 | 8 | Integer | Zero since December 6, 2010 |
| BuySellIndicator | 19 | 1 | Alpha | Always B since 07/14/2014, whatever the resting side |
| Shares | 20 | 4 | Integer | Shares matched |
| Stock | 24 | 8 | Alpha | Symbol, right-padded with spaces |
| Price | 32 | 4 | Price4 | Match price, Price(4) |
| MatchNumber | 36 | 8 | Integer | Referenced by BrokenTrade |

#### CrossTrade (type `Q`, 40 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | CrossTrade: bulk print of a cross (ITCH 5.0, 1.5.2, p.17; the table is headed "Trade Message (Non-Cross)") |
| StockLocate | 1 | 2 | Integer | Locate code identifying the security |
| TrackingNumber | 3 | 2 | Integer | Nasdaq internal tracking number |
| Timestamp | 5 | 6 | Integer | Nanoseconds since midnight |
| Shares | 11 | 8 | Integer | Shares matched in the cross; may be 0 |
| Stock | 19 | 8 | Alpha | Symbol, right-padded with spaces |
| CrossPrice | 27 | 4 | Price4 | Cross price, Price(4) |
| MatchNumber | 31 | 8 | Integer | Unique for the day |
| CrossType | 39 | 1 | Alpha | O opening, C closing, H IPO and halted/paused |

#### BrokenTrade (type `B`, 19 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | BrokenTrade: an execution was broken; no book effect (ITCH 5.0, 1.5.3, p.18) |
| StockLocate | 1 | 2 | Integer | Locate code identifying the security |
| TrackingNumber | 3 | 2 | Integer | Nasdaq internal tracking number |
| Timestamp | 5 | 6 | Integer | Nanoseconds since midnight |
| MatchNumber | 11 | 8 | Integer | Match number of an earlier E, C or P |

#### NOII (type `I`, 50 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | NOII: net order imbalance indicator before a cross (ITCH 5.0, 1.6, pp.18-19) |
| StockLocate | 1 | 2 | Integer | Locate code identifying the security |
| TrackingNumber | 3 | 2 | Integer | Nasdaq internal tracking number |
| Timestamp | 5 | 6 | Integer | Nanoseconds since midnight |
| PairedShares | 11 | 8 | Integer | Shares matchable at the current reference price |
| ImbalanceShares | 19 | 8 | Integer | Shares not paired at the current reference price |
| ImbalanceDirection | 27 | 1 | Alpha | B buy, S sell, N none, O insufficient orders, P paused |
| Stock | 28 | 8 | Alpha | Symbol, right-padded with spaces |
| FarPrice | 36 | 4 | Price4 | Clearing price for cross orders only, Price(4) |
| NearPrice | 40 | 4 | Price4 | Clearing price for cross and continuous orders, Price(4) |
| CurrentReferencePrice | 44 | 4 | Price4 | Price at which the shares are calculated, Price(4) |
| CrossType | 48 | 1 | Alpha | O opening, C closing, H IPO and halted/paused, A extended trading close |
| PriceVariationIndicator | 49 | 1 | Alpha | L <1%, 1-9, A 10-19.99%, B 20-29.99%, C >=30%, space not calculable |

#### RPII (type `N`, 20 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | RPII: retail price improvement interest (ITCH 5.0, 1.7, pp.19-20; greyed out in the PDF) |
| StockLocate | 1 | 2 | Integer | Locate code identifying the security |
| TrackingNumber | 3 | 2 | Integer | Nasdaq internal tracking number |
| Timestamp | 5 | 6 | Integer | Nanoseconds since midnight |
| Stock | 11 | 8 | Alpha | Symbol, right-padded with spaces |
| InterestFlag | 19 | 1 | Alpha | B buy side, S sell side, A both, N none |

#### DLCRPriceDiscovery (type `O`, 48 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | DLCRPriceDiscovery: direct listing with capital raise price discovery (ITCH 5.0, 1.8, p.20) |
| StockLocate | 1 | 2 | Integer | Locate code identifying the security |
| TrackingNumber | 3 | 2 | Integer | Nasdaq internal tracking number |
| Timestamp | 5 | 6 | Integer | Nanoseconds since midnight |
| Stock | 11 | 8 | Alpha | Symbol, right-padded with spaces |
| OpenEligibilityStatus | 19 | 1 | Alpha | N not eligible, Y eligible for release |
| MinimumAllowablePrice | 20 | 4 | Price4 | 20% below registration statement lower price, Price(4) |
| MaximumAllowablePrice | 24 | 4 | Price4 | 80% above registration statement highest price, Price(4) |
| NearExecutionPrice | 28 | 4 | Price4 | Reference price when the volatility test passed, Price(4) |
| NearExecutionTime | 32 | 8 | Integer | Time the near execution price was set; unit not stated |
| LowerPriceRangeCollar | 40 | 4 | Price4 | 10% below the near execution price, Price(4) |
| UpperPriceRangeCollar | 44 | 4 | Price4 | 10% above the near execution price, Price(4) |


### mold::from_nasdaq

#### DownstreamPacketHeader (type none, 20 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| Session | 0 | 10 | ANUM | Session the packet belongs to; padding not stated |
| SequenceNumber | 10 | 8 | NUM | Sequence number of the first message in the packet; later messages are numbered implicitly |
| MessageCount | 18 | 2 | NUM | Message blocks in the packet; 0 heartbeat, 0xFFFF end of session (p.3, p.4) |

### mold::to_nasdaq

#### RequestPacket (type none, 20 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| Session | 0 | 10 | ANUM | Session the request belongs to |
| SequenceNumber | 10 | 8 | NUM | First requested sequence number |
| RequestedMessageCount | 18 | 2 | NUM | Messages requested; the reply holds only as many as fit in one UDP packet |


### soup::from_nasdaq

#### Debug (type `+`, 1 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | Debug: free-form text (SoupBinTCP 3.0, 2.1) |
| Text | 1 | var | Payload | Printable text |

#### LoginAccepted (type `A`, 31 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | LoginAccepted: login succeeded (2.2.1) |
| Session | 1 | 10 | Session (right-aligned) | Session the client is logged into, left padded with spaces |
| SequenceNumber | 11 | 20 | Numeric (right-aligned) | Sequence number of the next sequenced message, ASCII, left padded with spaces |

#### LoginRejected (type `J`, 2 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | LoginRejected: login failed; the server then closes the socket (2.2.2) |
| RejectReasonCode | 1 | 1 | Alpha | A not authorized, S session not available |

#### SequencedData (type `S`, 1 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | SequencedData: one message of the higher-level protocol (2.2.3) |
| Message | 1 | var | Payload | Higher-level protocol message |

#### ServerHeartbeat (type `H`, 1 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | ServerHeartbeat: sent after one second without other packets (2.2.4) |

#### EndOfSession (type `Z`, 1 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | EndOfSession: no more sequenced messages in this session (2.2.5) |

### soup::to_nasdaq

#### LoginRequest (type `L`, 47 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | LoginRequest: first packet of a connection (2.3.1) |
| Username | 1 | 6 | Alphanumeric | Username, right-padded with spaces |
| Password | 7 | 10 | Alphanumeric | Password, right-padded with spaces |
| RequestedSession | 17 | 10 | Session (right-aligned) | Session to log into; all spaces for the currently active session. Padding not stated; taken as in LoginAccepted |
| RequestedSequenceNumber | 27 | 20 | Numeric (right-aligned) | Next sequence number wanted, ASCII; 0 for the most recently generated message. Padding not stated; taken as in LoginAccepted |

#### UnsequencedData (type `U`, 1 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | UnsequencedData: one message of the higher-level protocol (2.3.2) |
| Message | 1 | var | Payload | Higher-level protocol message |

#### ClientHeartbeat (type `R`, 1 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | ClientHeartbeat: sent after one second without other packets (2.3.3) |

#### LogoutRequest (type `O`, 1 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | LogoutRequest: ends the session (2.3.4) |


### glimpse::from_nasdaq

#### SystemEvent (type `S`, 12 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | SystemEvent: market or feed handler event (GLIMPSE 5.0, 1.1, p.3) |
| StockLocate | 1 | 2 | Integer | Always 0 |
| TrackingNumber | 3 | 2 | Integer | Nasdaq internal tracking number |
| Timestamp | 5 | 6 | Integer | Nanoseconds since midnight |
| EventCode | 11 | 1 | Alpha | O start of messages, S start of system hours, Q start of market hours, M end of market hours, E end of system hours, C end of messages (p.5) |

#### AddOrder (type `A`, 36 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | AddOrder: new displayed order without MPID attribution (GLIMPSE 5.0, 1.2.1, pp.4-5) |
| StockLocate | 1 | 2 | Integer | Locate code identifying the security |
| TrackingNumber | 3 | 2 | Integer | Nasdaq internal tracking number |
| Timestamp | 5 | 6 | Integer | Nanoseconds since midnight |
| OrderReferenceNumber | 11 | 8 | Integer | Unique for the day |
| BuySellIndicator | 19 | 1 | Alpha | B buy, S sell |
| Shares | 20 | 4 | Integer | Displayed quantity |
| Stock | 24 | 8 | Alpha | Symbol, right-padded with spaces |
| Price | 32 | 4 | Price4 | Display price, Price(4) |

#### AddOrderMPID (type `F`, 40 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | AddOrderMPID: new displayed order with MPID attribution (GLIMPSE 5.0, 1.2.2, p.5) |
| StockLocate | 1 | 2 | Integer | Locate code identifying the security |
| TrackingNumber | 3 | 2 | Integer | Nasdaq internal tracking number |
| Timestamp | 5 | 6 | Integer | Nanoseconds since midnight |
| OrderReferenceNumber | 11 | 8 | Integer | Unique for the day |
| BuySellIndicator | 19 | 1 | Alpha | B buy, S sell |
| Shares | 20 | 4 | Integer | Displayed quantity |
| Stock | 24 | 8 | Alpha | Symbol, right-padded with spaces |
| Price | 32 | 4 | Price4 | Display price, Price(4) |
| Attribution | 36 | 4 | Alpha | MPID of the entering participant |

#### StockDirectory (type `R`, 39 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | StockDirectory: one per symbol at the start of the spin (GLIMPSE 5.0, 1.3, pp.5-8) |
| StockLocate | 1 | 2 | Integer | Locate code assigned to the symbol for the day |
| TrackingNumber | 3 | 2 | Integer | Nasdaq internal tracking number |
| Timestamp | 5 | 6 | Integer | Nanoseconds since midnight |
| Stock | 11 | 8 | Alpha | Symbol, right-padded with spaces |
| MarketCategory | 19 | 1 | Alpha | Q G S Nasdaq tiers; N NYSE, A NYSE American, P NYSE Arca, Z BATS Z, V IEX; space not available |
| FinancialStatusIndicator | 20 | 1 | Alpha | D E Q S G H J K C N (Nasdaq-listed); space not available |
| RoundLotSize | 21 | 4 | Integer | Shares in a round lot |
| RoundLotsOnly | 25 | 1 | Alpha | Y only round lots accepted, N no size restriction |
| IssueClassification | 26 | 1 | Alpha | Appendix B (p.14) |
| IssueSubType | 27 | 2 | Alpha | Appendix C (pp.14-16) |
| Authenticity | 29 | 1 | Alpha | P live/production, T test |
| ShortSaleThresholdIndicator | 30 | 1 | Alpha | Y restricted under Rule 203(b)(3), N not, space not available |
| IPOFlag | 31 | 1 | Alpha | Y set up as new IPO, N not, space not available |
| LULDReferencePriceTier | 32 | 1 | Alpha | 1 Tier 1, 2 Tier 2, space not available |
| ETPFlag | 33 | 1 | Alpha | Y ETP, N not, space not available |
| ETPLeverageFactor | 34 | 4 | Integer | Integral leverage of the ETP, rounded down |
| InverseIndicator | 38 | 1 | Alpha | Y inverse ETP, N not |

#### StockTradingAction (type `H`, 25 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | StockTradingAction: trading state of a security (GLIMPSE 5.0, 1.4, pp.8-9) |
| StockLocate | 1 | 2 | Integer | Locate code identifying the security |
| TrackingNumber | 3 | 2 | Integer | Nasdaq internal tracking number |
| Timestamp | 5 | 6 | Integer | Nanoseconds since midnight |
| Stock | 11 | 8 | Alpha | Symbol, right-padded with spaces |
| TradingState | 19 | 1 | Alpha | H halted all US markets, P paused (Nasdaq-listed), Q quotation only, T trading on Nasdaq |
| Reserved | 20 | 1 | Alpha | Reserved |
| Reason | 21 | 4 | Alpha | Trading action reason code (ITCH 5.0 Appendix C, pp.32-33) |

#### RegSHORestriction (type `Y`, 20 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | RegSHORestriction: Reg SHO short sale price test status (GLIMPSE 5.0, 1.5, p.9) |
| LocateCode | 1 | 2 | Integer | Locate code identifying the security (the spec names this field Locate Code) |
| TrackingNumber | 3 | 2 | Integer | Nasdaq internal tracking number |
| Timestamp | 5 | 6 | Integer | Nanoseconds since midnight |
| Stock | 11 | 8 | Alpha | Symbol, right-padded with spaces |
| RegSHOAction | 19 | 1 | Alpha | 0 no price test, 1 restriction in effect (intraday drop), 2 restriction remains in effect |

#### RPII (type `N`, 20 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | RPII: retail price improvement interest (GLIMPSE 5.0, 1.6, pp.9-10; BX only) |
| StockLocate | 1 | 2 | Integer | Locate code identifying the security |
| TrackingNumber | 3 | 2 | Integer | Nasdaq internal tracking number |
| Timestamp | 5 | 6 | Integer | Nanoseconds since midnight |
| Stock | 11 | 8 | Alpha | Symbol, right-padded with spaces |
| InterestFlag | 19 | 1 | Alpha | B buy side, S sell side, A both, N none |

#### OperationalHalt (type `h`, 21 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | OperationalHalt: operational halt on one Nasdaq market centre (GLIMPSE 5.0, 1.8, pp.10-11) |
| StockLocate | 1 | 2 | Integer | Locate code identifying the security |
| TrackingNumber | 3 | 2 | Integer | Nasdaq internal tracking number |
| Timestamp | 5 | 6 | Integer | Nanoseconds since midnight |
| Stock | 11 | 8 | Alpha | Symbol, right-padded with spaces |
| MarketCode | 19 | 1 | Alpha | Q Nasdaq, B BX, X PSX |
| OperationalHaltAction | 20 | 1 | Alpha | H operationally halted, T halt lifted |

#### EndOfSnapshot (type `G`, 21 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 0 | 1 | char | EndOfSnapshot: last message of the spin; gives the ITCH sequence number to resume at (GLIMPSE 5.0, 1.7, p.10) |
| SequenceNumber | 1 | 20 | Numeric (right-aligned) | TotalView-ITCH sequence number when the snapshot was taken; process real-time ITCH from this number, ASCII |



### Check against Nasdaq sample data

Every message of the Sample was decoded with the header generated from
`itch50.tbl`, walking the file by `size`.

| File | Bytes | Decoded | Unknown | Malformed | Incomplete | Covers |
|---|---|---|---|---|---|---|
| `S061226-v50.txt.gz`, first 50,000,000 bytes | 50,000,000 | 1,653,656 | 0 | 0 | 1 (the cut at 50 MB) | 03:01:32 to 04:06:53 ET (pre-market) |
| `NOII/S050922-v50-NOII.txt.gz`, whole | 309,648,488 | 5,954,783 | 0 | 0 | 0 | 03:02 to 20:05 ET |

Counts by type, first file: AddOrder 679,301; OrderDelete 528,616;
MarketParticipantPosition 270,668; OrderCancel 59,871; OrderReplace 51,378;
OrderExecuted 16,324; StockDirectory 12,809; RegSHORestriction 12,800;
StockTradingAction 12,796; Trade 9,083; AddOrderMPID 8; SystemEvent 2 (`O`, `S`).
Second file: NOII 5,954,777; SystemEvent 6 (`O S Q M E C`). Every length prefix
equals the table's `@size` (no message is longer than its table).

Not present in either sample, so checked only against the specification and the
unit test: OrderExecutedWithPrice (C), CrossTrade (Q), BrokenTrade (B),
MWCBDeclineLevel (V), MWCBStatus (W), IPOQuotingPeriodUpdate (K),
LULDAuctionCollar (J), OperationalHalt (h), RPII (N), DLCRPriceDiscovery (O).
The first 50 MB of a day file end before the market opens; reaching C, Q and the
open requires decompressing several GB of the 17.9 GB file.

Consistency, first file:

- Stock Locate: 12,809 directory entries, no locate repeated. All 1,640,845
  messages that carry a locate (and K, none present) name a locate seen in an
  earlier Stock Directory; every Stock field equals that locate's directory
  symbol.
- Order references: 656,189 Executed, Cancel, Delete and Replace messages; every
  one references an order added earlier in the sample (100%), with the same
  locate as its Add. No execution or cancel exceeds the order's remaining shares.
  No order reference is added twice (Adds and Replace new references). 10,401
  orders were executed to zero; 140,292 remain at the cut. The file starts at
  Start of Messages, so 100% is the expected result; it does not exercise a
  mid-day join.
- Trade (P): Order Reference Number is 0 and Buy/Sell is `B` in all 9,083
  (ITCH p.16-17, 1.5.1).
- Timestamps never decrease. 7,477 of 1,344,581 order and trade messages share
  their timestamp with the previous one, mostly runs of E (4,472 `EE`), P after P,
  and E followed by A (one incoming order executing against several resting
  orders, then resting).
- Enumerated values are within the specification's lists, with one exception:
  Market Category `M` on one directory entry, `MTEST` (Authenticity `T`, test
  issue). Round Lot Size is 100 for 12,545 issues, 40 for 244 (e.g. AAPL) and 10
  for 20.
- NOII file: Cross Type `C O A H`, Imbalance Direction `O N S B`, Price Variation
  Indicator space, `L`, `1`-`9`, `A`, `B`, `C`; all in the lists (ITCH p.19). The
  file holds no Stock Directory, so its locates cannot be checked.

## 5. Sequencing and recovery

- **Sequence scope.** One sequence per MoldUDP64 session (stream); the packet
  header numbers its first message and the rest are implicit (Mold p.3). Over
  SoupBinTCP the number is implicit and counted by the client from the Login
  Accepted value; the first message of a session is 1 (Soup p.3, 1.2; p.5,
  2.2.3). The FPGA and software feeds carry messages in the same order (ITCH p.4).
  Whether the MoldUDP64 and SoupBinTCP sequence numbers of one day are the same
  numbering is not stated; GLIMPSE's End of Snapshot gives "TotalView-ITCH
  sequence number" without saying which transport (GLIMPSE p.10, 1.7). See 10.
- **Session.** 10 ASCII characters in every MoldUDP64 packet (Mold p.3); the
  receiver checks it against the expected session and aborts on a mismatch (Mold
  p.6, step 3). Its format and padding are not stated.
- **Gap rule.** Next expected = header sequence + message count of the last
  packet applied. A packet whose sequence number is higher is a gap: send a
  Request Packet for the missing range (Mold p.6, step 5). Lower: already
  received (a duplicate, or a retransmission overlapping data held); the
  specification does not say, the reading is to skip messages below next
  expected. Heartbeats and end-of-session packets carry the next expected number
  (Mold p.4), so a gap is visible without traffic.
- **Recovery mechanisms.**
  1. MoldUDP64 re-request: unicast request, unicast reply in downstream format
     (Mold p.5). Only as many messages as fit in one reply; repeat for the rest.
  2. SoupBinTCP replay: log in with the session and the next wanted sequence
     number; the server resends from there (Soup p.3, 1.2; p.7, 2.3.1).
  3. GLIMPSE snapshot (GLIMPSE pp.2-3, p.10): Stock Directory for all symbols,
     Stock Trading Action for active ones, Add Order (A/F) for every displayed
     order, then End of Snapshot `G` with an ITCH sequence number (p.3).
     Operational Halt is sent on a last-seen basis (p.11). Reg SHO (Y) and RPII
     (N, BX only) are among GLIMPSE's message formats (pp.9-10) but not in the
     list of what a spin contains (p.3).
- **Snapshot alignment.** "firms should process real-time TotalView-ITCH messages
  beginning with the message sequence number reflected in this snapshot message"
  (GLIMPSE p.10, 1.7). Reading: with G = N, the snapshot holds the effect of
  messages 1..N-1; buffer live messages, discard those < N, apply N onward. The
  same field is described as "the TotalView-ITCH sequence number when the
  GLIMPSE snapshot was taken" (p.10) and, on p.3, as reflecting "the most recent
  order reference number". Whether N itself is in the snapshot is not certain;
  see 10.
- **Snapshot too old.** If N is below the oldest buffered live message, the gap
  N..oldest-1 can be filled by MoldUDP64 re-request; or take a new snapshot. Not
  addressed by the specifications.
- **State GLIMPSE does not carry.** Market Participant Position (L), MWCB levels
  (V, W), IPO Quoting (K), LULD collars (J), NOII (I), DLCR (O) and the day's
  trades are not in the spin (GLIMPSE p.3). A handler that joins by snapshot does
  not have them until they are next sent.
- **Resets.** Locate codes are assigned per day starting at 1 (ITCH p.3); order
  references are day-unique (ITCH p.12, 1.3). A new MoldUDP64 session, or an End
  of Messages `C` (ITCH p.5), ends the day: clear books, the locate directory and
  the order-reference map. Whether a MoldUDP64 sequence restarts at 1 in a new
  session is not stated for MoldUDP64 (it is for SoupBinTCP).

## 6. Semantics

- **Book type.** Order by order (MBO), full depth, displayed orders only (ITCH
  p.3; p.12, 1.3). Non-displayed orders never appear in the book; their
  executions come as Trade (P) (ITCH p.16, 1.5.1).
- **Order lifecycle** (ITCH p.14, 1.4). Add (A, F) creates an order with a
  day-unique reference. E, C and X subtract their shares from the order's
  displayed shares; effects are cumulative; at zero the order is removed. D
  removes it in full. U removes the original and creates a new order with a new
  reference, new shares and price, and the original's side, symbol and MPID
  (ITCH pp.15-16, 1.4.5). Modify messages carry only the reference (and locate);
  side, price, symbol and remaining shares must be kept from the Add.
- **Executions.** E executes at the order's display price; C at the price it
  carries, with a Printable flag; non-printable executions are counted in a later
  bulk print, typically the cross (ITCH p.14, 1.4.2). E and C report the resting
  order only; there is no aggressor record.
- **Trades without book effect.** P (non-displayed match), Q (cross bulk print,
  may show 0 shares), B (break of an earlier E, C or P match number) (ITCH
  pp.16-18). The specification says P and B may be ignored by a book builder
  (ITCH p.16, p.18).
- **Priority.** Not stated. Price-time priority with the book order given by the
  order of Add messages is the reading; see 10 for X and U.
- **Trading states.** System Event `O S Q M E C` (ITCH p.5); per security Stock
  Trading Action `H P Q T` (ITCH p.8); a pre-open spin sends `T` for every
  security eligible at start of system hours, and a security absent from it is
  treated as halted (ITCH p.8, 1.2.2). Operational Halt per market centre `Q B X`
  (ITCH p.12, 1.2.8). Whether the book can be crossed or locked in a state is not
  stated.
- **Auctions.** NOII from 9:25 and 15:50 ET, every 10 s then every second; for
  halts, IPOs and pauses every second; Extended Trading Close 16:00-16:05 every
  5 s (ITCH p.18, 1.6). Cross completion is signalled by Q per security (ITCH
  p.17, 1.5.2).
- **End of day.** After End of System hours, Broken Trade and Order Delete may
  still arrive (ITCH p.5).
- **Implied, iceberg, hidden.** No implied prices. Reserve (iceberg) orders: the
  specification does not describe how replenishment appears. Hidden orders: P
  only.

## 7. Mapping to Kaspar

Records are `bfile::l3_t` sent to OB as `frame::mda::msg::Data`, as
`mdp3/include/mdp3/handler_if.hpp` does for CME MBO. Conventions taken from it
and from `sim/src/Mbp2Mbo.cpp`: `orderUpdateAction` 0 New, 1 Change, 2 Delete;
`side` `'0'` buy, `'1'` sell (`TachBook.hpp` `get_side`); a trade is an
`l3_mbo_trd_v2_t` (MBOT_V2) with `orderID` and `lastQty` followed by the book
record of the same order with the same `transactTime` (`Mbp2Mbo::trade`); OB
does not change the book on MBOT_V2 ("execs have no impact on the book",
`OB.cpp`); `recovery = true` records must be adds (`OB.cpp` assertion); the
`orderID -> securityID` map is cleared on a channel reset (#122).

Common fields: `securityID` from Stock Locate via the day's directory (locate ->
symbol -> Kaspar asset); `orderID` = Order Reference Number (8 bytes, fits
`uint64_t`); `transactTime` = epoch nanoseconds of midnight ET of the trading
date + Timestamp; `sendingTime` = the same (neither transport carries a send
time); `pxd` = Price / 10,000 (see 10 on prices against ticks); `priority` = a
counter the handler increments on every new order; `venue` = an `en::x` value
for Nasdaq, BX or PSX, which does not exist yet (`exch_code.enum` has none).

| Venue message | Kaspar record or message | Notes |
|---|---|---|
| A Add Order, F Add Order MPID | MBO_V2 New | `displayQty` = Shares, side B -> `'0'`, S -> `'1'`, price, new priority. Store reference -> {securityID, side, price, remaining, priority}. MPID is not carried. |
| E Order Executed | MBOT_V2 (`orderID`, `lastQty` = Executed Shares), then MBO_V2 Change (`displayQty` = remaining, same price and priority) or Delete when remaining is 0 | One event; both records get the message timestamp. |
| C Order Executed With Price | as E | `l3_mbo_trd_v2_t` has no price field, so Execution Price is not carried; Printable `N` executions are still book reductions. See 10. |
| X Order Cancel | MBO_V2 Change with remaining shares, or Delete at 0 | Priority kept (reading; see 10). |
| D Order Delete | MBO_V2 Delete | Remove from the reference map. |
| U Order Replace | MBO_V2 Delete (original reference), then MBO_V2 New (new reference, original side and securityID, new Shares and Price, new priority) | One event, `endOfEvent` on the New. The new reference is used for all later updates (ITCH p.16). |
| P Trade (non-cross) | no book record | No resting order in the book (reference 0, side always `B`). Kaspar has no MBO-path record for a print without an order: options in 10. |
| Q Cross Trade | no book record | Bulk print; the orders it filled were reduced by their own C/E messages. Same open question as P. |
| B Broken Trade | no book record | No effect on the book (ITCH p.18). No generic Kaspar record for a bust; post-trade concern. |
| S System Event | `l3_sst_t` for the venue's securities, or none | Mapping of `Q`/`M` to OB's matching status (17) is open: see 10. `C` ends the day: clear state. |
| H Stock Trading Action | `l3_sst_t` (SST) for the security | `T` -> 17 (OB's only matching status, `OB.hpp` `is_matching_status`), `H P Q` -> a non-matching status. See 10. |
| h Operational Halt | `l3_sst_t` when Market Code is this feed's market | Halt on another market centre has no effect on this book. |
| R Stock Directory | instrument loader, no record | Locate -> symbol for the day; round lot; test issues (Authenticity `T`). |
| Y, L, V, W, K, J, I, N, O | no record | Not book events. |
| MoldUDP64 packet end | `l3_eop_t` (EOP), recorder only | `msgSeqNum` is `uint32_t`; MoldUDP64 sequence is 8 bytes. |
| MoldUDP64 gap | `l3_gap_v2_t` (GAP_V2) to every book | Then re-request or snapshot. |
| New MoldUDP64 session, End of Messages, start of a GLIMPSE recovery | `l3_chr_v2_t` (CHR_V2) to every book; clear the reference map and the per-order state | GLIMPSE Adds then go out as MBO_V2 New with `recovery = true`. |
| MoldUDP64 heartbeat (count 0), end of session (0xFFFF) | none | Used for gap detection and end-of-session handling. |

Event boundaries: ITCH has no event or batch marker. Reading: each ITCH message
is one event, `endOfEvent` on its last record. Consecutive messages can share a
timestamp (Sample: 0.56% of order and trade messages), while `Mbp2Mbo` notes
that OB expects `transactTime` to increase from one event to the next. See 10.

## 8. Instruments

- **Source.** Stock Directory, one per symbol at start of day (ITCH p.5, 1.2.1),
  and at the start of a GLIMPSE spin (GLIMPSE p.5, 1.3). Locate codes are per
  day, from 1; they do not change intraday and are not stable across days (ITCH
  p.3). Map by symbol to the Kaspar asset and `sec_id` every day.
- **Symbology.** Up to 8 characters, right-padded; suffixes after the root, a
  `.` delimiter in some cases (ITCH p.31, Appendix B).
- **Prices.** Price(4) for order, trade, NOII, collar and IPO prices; Price(8)
  for MWCB levels (ITCH p.10). The IPO Price note that describes a decimal string
  (ITCH p.11) contradicts its type, Price(4); see 10. Tick size is not in the
  feed (sub-penny prices are representable).
- **Quantities.** Shares, one unit each. Round Lot Size per issue (ITCH p.6);
  Sample shows 100, 40 and 10. Round Lots Only is `N` for all 12,809 Sample
  issues, and 153,819 of 679,309 Sample Adds are below their issue's round lot:
  odd-lot orders are in the book.
- **Lifecycle.** Issues added intraday get a directory message (implied by the
  Market Category note in Appendix A, ITCH p.27); IPOs through K and H; test
  issues have Authenticity `T` and should not be shown (ITCH p.6).

## 9. Pitfalls

**1. Reading the specification**

- Several documents: one ITCH (5.0, 2023), one GLIMPSE (5.0, 2024, unified
  Nasdaq/BX/PSX), one MoldUDP64, one SoupBinTCP; section 1.
- Errata and change logs: ITCH Appendix A (pp.22-30) and GLIMPSE Appendix A
  (pp.12-13) read; no entry contradicts the 2023 body. July 2022 "non-integer
  leverage factors" changed only the ETP Leverage Factor description, which still
  says integer, rounded down (ITCH p.7; Sample values 0-3).
- Tables across page breaks: Stock Directory (pp.5-8), MWCB Status (pp.10-11),
  Order Executed With Price (pp.14-15), Trade (pp.16-17), NOII (pp.18-19), RPII
  (pp.19-20); each checked visually and against `pdftotext -layout`; `@size`
  confirms no row lost.
- Offsets 0-based in all four documents; SoupBinTCP's tables include the length
  field at offset 0, removed in the table by `@length_prefix`. MoldUDP64 and
  SoupBinTCP lengths exclude themselves (Mold p.4; Soup p.2).
- Reserved fields: Stock Trading Action offset 20 kept (ITCH p.8).
- Enumerations elsewhere: Trading Action reasons Appendix C (ITCH pp.32-33),
  Issue Classification Appendix D (p.34), Sub-Type Appendix E (pp.35-36).
- Names differing: Reg SHO "Locate Code" for Stock Locate (ITCH p.9); W's table
  headed "MWCB Decline Level Message" (p.11); Q's table headed "Trade Message
  (Non-Cross)" (p.17); GLIMPSE F described as "No MPID Attribution" (GLIMPSE
  p.5); E and C notes say "Add Order -" (ITCH p.14). Table rows taken as
  authoritative.
- Examples against tables: the specifications have no byte examples.
- Two layers in one table: kept apart; MoldUDP64, SoupBinTCP, ITCH and GLIMPSE
  are separate sections. ITCH and SoupBinTCP share type codes (`A`, `S`, `H`,
  `J`, `L`, `U`, `O`, `R`); separate sections avoid the clash.
- Repeated names with different codes: A/F are AddOrder and AddOrderMPID; E/C
  OrderExecuted and OrderExecutedWithPrice.
- Variable-length trailing field: none in ITCH or GLIMPSE; SoupBinTCP Debug and
  data packets carry a `bytes` tail.
- Odd-width integers: 6-byte timestamps (`uint_n<6>`).

**2. Framing and encoding**

- Endianness: big-endian everywhere, including MoldUDP64 fields marked NUM (Mold
  p.2).
- Signedness: all unsigned (ITCH p.4; GLIMPSE p.2).
- Fixed-point prices: Price(4) and Price(8) per field (ITCH p.4, p.10).
- Null and sentinel values: P Order Reference Number null-filled = 0 (ITCH
  p.17); space means "not available" in several directory fields (pp.5-8); K
  release time and price 0 when cancelled (p.11); Cross Trade shares may be 0
  (p.17).
- Strings: Alpha left justified, space padded (ITCH p.4); SoupBinTCP session and
  sequence left padded (Soup p.4); MoldUDP64 session padding not stated; GLIMPSE G
  sequence padding not stated (10).
- Several messages per packet: MoldUDP64 counts them (Mold p.3); TCP may split or
  merge SoupBinTCP packets (Soup p.2), handled by `incomplete`.
- Timestamps: nanoseconds since midnight (ITCH p.4); time zone not stated; Sample
  consistent with ET (Start of Messages at 03:01:32, End of Messages at
  20:05:00). DLCR Near Execution Time has no stated unit (10).

**3. Sequencing and recovery**

- Sequence scope: per MoldUDP64 session; per SoupBinTCP session; section 5.
- A/B arbitration: not described by these documents; reuse the existing
  arbitration if Nasdaq provides two feeds.
- Gap rule and duplicates: section 5; duplicates not addressed by the spec.
- Recovery: MoldUDP64 re-request, SoupBinTCP replay, GLIMPSE; section 5.
- Snapshot alignment: G sequence number, inclusive reading; section 5 and 10.
- Snapshot too old: not addressed; section 5.
- State to clear on reset: books, reference map (order -> locate, side, price,
  remaining), locate directory; on GLIMPSE recovery, CHR before the spin (#122).
- Exercise recovery paths: to do at step 7; the Sample has no gaps.
- Session behaviour: logon, heartbeats, timeouts, end of session; section 2.

**4. Book semantics**

- MBO, not price level (section 6).
- Level-index updates: not applicable.
- Depth-limited: full depth.
- Priority on modify: X keeps the reference; U gives a new reference. Whether the
  matching engine keeps priority on either is not stated; confirm (10).
- Implied, iceberg, hidden: no implied; reserve replenishment not described;
  hidden only through P (10).
- Trades also as book deltas: E and C are both; emit MBOT then the book record,
  do not reduce twice. P and Q are not book deltas; Q's volume duplicates the
  non-printable C executions (ITCH p.14, p.17).
- Side of a trade: E/C are the resting order's side; P's side is always `B` and
  carries no information (ITCH p.17); Q has no side.
- Trading states: section 6; crossed or locked books per state not stated.
- Book clears: new day or session, End of Messages, GLIMPSE recovery. INET-era
  note that open orders are cancelled at end of day (ITCH p.29, 2001 entry) is
  history, not a 5.0 statement.

**5. Prices, quantities and instruments**

- Price or yield: price only.
- Quantity multipliers: none; shares. Round lot varies by issue (Sample 10, 40,
  100).
- Decimals sentinels: none; fixed decimals per field.
- Price-dependent ticks: not in the feed; Nasdaq's tick regime is outside these
  documents.
- Instrument definitions: Stock Directory per day; section 8.
- Instrument lifecycle: per-day locates, IPO messages, test issues; section 8.

**6. Behaviour the specification omits**

- Subscription options: none for MoldUDP64; GLIMPSE login at sequence 1 only.
- History on subscribe: SoupBinTCP TotalView replays from the requested sequence
  (Soup p.7); GLIMPSE is a spin of current state.
- Snapshot of the whole book: GLIMPSE sends every displayed order (GLIMPSE p.3).
- Order within an event: Sample shows E runs followed by A for an aggressor that
  rests; there is no aggressor record.
- Connection limits: SoupBinTCP login within ~30 s, server timeout ~15 s
  (Soup pp.3, 6); MoldUDP64 none.

**7. Post-trade**: not applicable to a market-data feed (FIX version, custom tags,
credentials in Logon, missing-field rejects, session status, capture requests,
report lifecycle, duplicates, fixed-income fields, daily resets). Broken Trade is
the only post-trade-like message; it carries a match number only.

**8. Order entry**: not applicable (client ids, cancel/replace ids, fill races,
unsolicited cancels, busts, throttling, reconnect, external orders, short
sales). Nasdaq order entry is OUCH, not covered here.

## 10. Ambiguities

| Question | Reading taken | Alternatives | Resolved by |
|---|---|---|---|
| GLIMPSE G sequence number N: is message N already in the snapshot? | No: apply live messages from N on, discard < N ("begin to process ... beginning with the sequence number stated", GLIMPSE p.10) | N is the last message reflected; apply from N+1 (p.10 also says "sequence number when the GLIMPSE snapshot was taken"; p.3 calls it "the most recent order reference number") | Open. Settle with a live GLIMPSE session: a Delete/Executed at N for an order not in the spin, or an Add at N present in the spin. |
| Which sequence does G refer to: MoldUDP64, SoupBinTCP, or both (same numbering)? | Both transports number the day's messages identically | MoldUDP64 numbering differs from SoupBinTCP | Open; not stated in Mold, Soup or GLIMPSE. |
| G Sequence Number padding (20 ASCII digits) | Left padded with spaces, as SoupBinTCP Numeric (`@type Numeric char right`) | Right padded; zero padded | Open; GLIMPSE p.10 says only "Numeric". `get_SequenceNumber()` strips leading spaces only. |
| Does a GLIMPSE Add Order spin preserve time priority within a price level, and what Timestamp does a spun Add carry? | Spin order = queue order; Timestamp not used for priority | Arbitrary order; queue position unknown after a snapshot | Open; GLIMPSE says only "Add Order messages for all the displayable orders" (p.3). Affects simulated fills after a recovery. |
| Priority after Order Cancel (X, partial) | Kept (same reference) | Lost | Open; the specification states no priority rule. Nasdaq rulebook or a sample-day comparison against executions. |
| Priority after Order Replace (U) | Lost: the new reference joins the back of its level | Kept when only the size decreases at the same price | Open; ITCH p.15-16 says the original is "no longer accessible" and the replacement has a new reference. |
| Reserve (iceberg) replenishment | Appears as a new Add (new reference, back of queue) | Replace; Cancel/Add pairs | Open; not described. Probe a full day of the Sample around executions that empty an order. |
| Event grouping and `transactTime` when consecutive messages share a timestamp | One ITCH message = one event; equal timestamps kept as sent | Group consecutive messages with one timestamp into one event; or make `transactTime` strictly increasing as `Mbp2Mbo::stamp` does | Open, Kaspar side. Sample: 7,477 of 1,344,581 order/trade messages share the previous timestamp. |
| Execution price of C in Kaspar | Dropped; MBOT_V2 has no price, so OB fills at the order's book price | Add a price to a new trade record; emit a separate MBPT | Open, Kaspar side. |
| P and Q prints: Kaspar record | None on the book path | `l3_mbp_trd_t` (MBPT; its handler_if and OB paths are compiled out); `l3_vol_t` for volume | Open, Kaspar side. |
| Trading status mapping to OB's `tradingStatus` (17 = matching) | H `T` -> 17 from System Event `Q` until `M`; H `H P Q`, Operational Halt `H` on this market -> non-matching | Treat pre- and post-market `T` as matching too | Open, Kaspar side; depends on which sessions the simulator should fill in. |
| `pxd`: price or ticks | Price / 10,000, as handler_if does for CME | Ticks (`Mbp2Mbo`: price / tick, asset units 1) | Open, Kaspar side; tick depends on price (sub-penny below $1). |
| Timestamp time zone | US Eastern, midnight local (DST aware) | UTC | Settled by Sample: Start of Messages 03:01:32, End of Messages 20:05:00 since midnight, consistent with ET only. |
| MoldUDP64 Session format and padding | 10 ASCII characters, compared as-is | | Open; not stated (Mold p.3). |
| MoldUDP64 sequence at a new session | Restarts at 1 | Continues | Open; stated only for SoupBinTCP (Soup p.3). |
| K Stock Locate "Always set to 0" while the message carries Stock | Use Stock to find the security; locate may be 0 | Locate is the security's | Open; no K in the Sample. |
| IPO Price (K): Price(4) integer, but the note describes a space-padded decimal string (ITCH p.11) | Price(4) binary integer, as the type column says | Text | Note is from pre-4.0 text (ITCH p.28-29, 2000-2001 entries describe the text format). Not in Sample. |
| DLCR Near Execution Time (8 bytes) unit | Nanoseconds since midnight | Other unit | Open; ITCH p.20 states none. |
| RPII (N) still sent | Possibly: decoder keeps it | Retired with the RPI program 12/31/14 (ITCH p.22); greyed out in the ITCH PDF (pp.19-20); GLIMPSE lists it "Only for BX" (p.9) | Open; none in the Sample. |
| Market Category `M` | Undocumented value, seen only on test issue `MTEST` | | Sample; treat unknown categories as data, not errors. |
| MPID of A orders | `NSDQ` when one is shown (ITCH p.13) | | Spec. |
| ITCH over SoupBinTCP framing | Payload of a Sequenced Data packet, no ITCH length | | Spec (Soup p.5); needs a prefix-less ITCH decoder, see the README. |
