# Feed description: EBS Market on CME Globex, MDP 3.0

EBS Market (spot FX, precious metals, NDFs) runs on CME Globex and publishes market
data over MDP 3.0, the protocol of Kaspar's CME-certified handler (`mdp3/`). This
description is the EBS view of MDP 3.0: what is identical to CME futures, what
differs, and what Kaspar's handler does with each EBS message today.

Every entry cites its source. CME documents EBS market data on its Client Systems
Wiki (cmegroupclientsite.atlassian.net, space EPICSANDBOX). Wiki pages have no page
numbers: a page is cited by its short name below, which section 1 maps to the page
id, title, version and date. A section heading of the page follows where useful,
e.g. (FXDEF, Repeating Group 4).

## 1. Venue and documents

All wiki pages were read on 2026-11-18 through the wiki's public REST API
(`/wiki/rest/api/content/<id>?expand=body.view,version`), which returns the page
version and last-modified date given here.

| Ref | Document | Version | Date | Applies to |
|---|---|---|---|---|
| CHG | MDP 3.0 - CME Globex Market Data Channel Guide (wiki 457323048) | v25 | 2026-11-17 | EBS channel numbers, market data groups, schema version |
| SPEC | MDP 3.0 - Message Specification (wiki 457323107) | v2 | 2024-12-26 | Templates each EBS feed carries |
| DISS | MDP 3.0 - Dissemination (wiki 457222936) | v2 | 2024-12-26 | Feeds per market data group; what EBS does not support |
| FXDEF | MDP 3.0 - Market Data Security Definition - FX (wiki 457327502) | v4 | 2026-04-20 | Template 63, valid values for EBS |
| MBP | MDP 3.0 - Market Data Incremental Refresh - MBP - LongQty (wiki 457423069) | v3 | 2026-02-19 | Template 64, valid values for EBS |
| TS | MDP 3.0 - Market Data Incremental Refresh - Trade Summary - LongQty (wiki 457227775) | v4 | 2026-02-19 | Template 65 |
| TSX | MDP 3.0 - Trade Summary (wiki 457418925) | v3 | 2026-05-12 | Trade fields by market (futures, BrokerTec, EBS) |
| VOL | MDP 3.0 - Conflation Processing - Delayed Electronic Volume for Ultra EBS Market Examples (wiki 1870299138) | v1 | 2026-09-22 | Template 66 on the Ultra 1 ms channels |
| SNAP | MDP 3.0 - Market Data Snapshot - Full Recovery - LongQty (wiki 457096744) | v3 | 2026-11-03 | Template 69 (UDP Market Recovery) |
| SNAPT | MDP 3.0 - Market Data Snapshot - Full Recovery - TCP LongQty (wiki 457227762) | v2 | 2024-12-26 | Template 68 (Conflated TCP) |
| CONF | MDP 3.0 - Conflation Processing - BrokerTec and EBS Markets Examples (wiki 457572870) | v21 | 2026-11-10 | Which messages are conflated; TransactTime under conflation; channel reset |
| CTCP | MDP 3.0 - Conflated TCP Market Data Group Processing (wiki 457325649) | v6 | 2026-11-10 | TCP session, packet header, request messages, EBS session access model |
| RTCP | MDP 3.0 - Recovery Services for Conflated TCP (wiki 457605817) | v2 | 2024-12-23 | TCP late join, channel reset |
| CS | MDP 3.0 - Credit Screened Market Best Processing for EBS (wiki 457325891) | v5 | 2025-06-06 | Credit-screened books, Market Best (269=w/x) |
| SST | MDP 3.0 - Security Status (wiki 457223126) | v4 | 2026-11-10 | Group and instrument states supported by EBS |
| TVD | MDP 3.0 - Trade and Value Date Processing (wiki 457325762) | v4 | 2026-11-10 | Trading Session group, trade date roll |
| RATES | MDP 3.0 - Market Recovery and Instrument Definition Rates (wiki 457227094) | v4 | 2026-03-19 | Snapshot / instrument replay rates per channel |
| MBPD | MDP 3.0 - Market by Price - Multiple Depth Book (wiki 457574047) | v2 | 2024-12-26 | MBP book rules (New/Change/Delete by level) |
| DR | EBS Market and eFix DR for MDP3 (wiki 457413064) | v2 | 2025-02-14 | Disaster recovery: sequence reset, definitions republished |
| SP | EBS Size Priority Matching (wiki 457421972) | v12 | 2026-06-04 | MatchAlgorithm P, LotType 6 |
| CPI | EBS Conditional Price Increments (wiki 457704180) | v8 | 2025-12-10 | Sub-pip tick, alternate MQL, PricePrecision 6; product/group/channel table |
| DM | EBS Spot FX (DataMine) (wiki 457573144) | v7 | 2026-10-20 | List of EBS pairs and metals; L2 history depth |
| NDF | EBS NDF (DataMine) (wiki 457638630) | v8 | 2026-10-20 | EBS NDF ON SEF trading ceased 2024-09-27 |
| XML | `templates_FixBinary.xml`, CME SFTP `SBEFix/Production/Templates/` | schema id 1, version 13, description "20230411" | fetched 2026-11-18; MD5 25913f3105137fa784f80d62f24c5fbb, identical to the copy `genschema/` uses (`~/kaspar-hft/genschema/templates_FixBinary.xml`) | Message layouts. The answer key for every layout below. |
| CFG | `EBSNYConfig.xml`, `EBSLNDConfig.xml`, `EBSOffSEFConfig.xml`, `EBSOnSEFConfig.xml`, CME SFTP `SBEFix/Production/Configuration/Ultra{NY,London,Off,On}/` | updated 2025-05-19 (NY, Off, On), 2025-07-11 (London) | fetched 2026-11-18 | Channels, products, groups, multicast addresses, ports, source hosts |
| SESS | `templates_MdpSessionMgmt_v0.xml`, CME SFTP `SBEFix/Production/SessionManagement/Templates/` | schema id 2, version 0, description "20190620" | fetched 2026-11-18 | Conflated TCP session templates 200-210 |
| SECDEF | `secdef.dat.gz`, CME SFTP `SBEFix/Production/` | daily file | fetched 2026-11-18 | Checked for EBS instruments: none (futures and options only, per its disclaimer). Holds 13 CME FX Spot+ instruments (channel 330) in the FX definition tags, used below only as an example of populated tags. |
| N2023 | EBS Market on CME Globex Notice, 7 September 2023 (cmegroup.com/notices/ebs/2023/09/20230907.html) | - | 2023-09-07 | Schema version 13 for EBS. **Not read directly**: cmegroup.com refused the automated request ("This IP address is blocked due to suspected web scraping activity"). The content cited here is the search engine's summary of the notice. |

- One schema applies: EBS is on Core Globex SBE schema version 13 (CHG, EBS rows),
  the same file and version as CME futures. N2023: the move of EBS to version 13 ran
  from 2023-10-22 to 2023-11-05 "to support future enhancements for EBS Market". The
  version-13 additions to template 63 are the four sub-tick fields (XML, `sinceVersion="13"`
  on AltMinPriceIncrement, AltMinQuoteLife, AltPriceIncrementConstraint,
  MaxBidAskConstraint; FXDEF; CPI).
- The generated codecs: this checkout's `mdp3_sbe/` is empty (codecs are generated,
  not committed). The `Makefile` here generates them into `build/mdp3_sbe/` from XML
  with the steps of `genschema/genschema.py`; all EBS templates (63-69) are present.
  No change to `genschema/` is needed.
- Not obtained: CME's EBS reference sheet ("CME Globex Reference Sheet", named by SP
  for size-priority products), the EBS Rulebook (tick and minimum quote life per
  pair), any EBS MDP 3.0 capture, CME Reference Data API output for EBS instruments.
- Out of scope here: EBS order entry (iLink 3, "iLink Binary Order Entry - EBS
  Market on CME Globex"), EBS Spectrum and the T VWAP feed, CME FX Spot+ (channel 330,
  a different CME Globex FX spot product, `207=GLBX`, SECDEF).

## 2. Transport

**What is the same as CME futures.** UDP multicast, feeds A and B for arbitration
(DISS, "UDP Incremental Feed"); an Instrument Replay feed and a Market Recovery
(snapshot) feed per channel; TCP Replay for small gaps (SPEC, "TCP Recovery for UDP";
CFG feed-type H). Little-endian SBE (XML `byteOrder="littleEndian"`).

**What differs.**

- Every EBS UDP channel is **conflated** ("CME MDP Conflated UDP" market data group;
  CHG; SPEC "EBS Market on CME Globex"). There is no unconflated (Premium) EBS feed.
- Datacenters: EBS New York instruments match in NY5, London instruments in LD4.2;
  Tokyo TY5 is an access point (DR, "Market Data DR Scenarios"). The multicast comes
  from CME source hosts 164.74.12.x / 164.74.14.x (NY) and 164.74.53.x / 164.74.55.x
  (London, NDF) (CFG), not the Aurora futures hosts.
- Disaster recovery: on a DR event, UDP feeds continue on the `dr-ip` addresses in
  CFG with the same ports; "Sequence numbers will be reset and Security Definition
  messages will be re-published" (DR, State 3).

### Channels (UDP)

From CFG. `config.xml` governs where it differs from CHG: CHG says "The config.xml
file provided via the CME FTP site should be utilized for the product scope and
connectivity details" (CHG, introduction). The differences are in section 10.

| Chan | Label (CFG) | Interval | Products / groups (CFG) | Incremental A / B | Instrument replay A | Snapshot A | Port |
|---|---|---|---|---|---|---|---|
| 518 | EBS Live Ultra - G10 + Metals- Conflated 1ms | 1 ms | 42 products; groups 6SUS, SPNZ, SPMT (+ $6SUS, $SPMT) | 233.91.101.4 / .67 | .6 | .5 | 14518 / 15518 |
| 528 | EBS Ultra New York | 5 ms (CHG) | same 42 products as 518 | 233.91.101.1 / .64 | .3 | .2 | 14528 / 15528 |
| 519 | EBS Live Ultra - EM FX - Conflated 1ms | 1 ms | 26 products; group 6SEM (+ $6SEM) | 233.91.101.19 / .83 | .21 | .20 | 14519 / 15519 |
| 529 | EBS Ultra London | 5 ms (CHG) | same 26 as 519 | 233.91.101.16 / .80 | .18 | .17 | 14529 / 15529 |
| 520 | Ultra OFF SEF NDF - Conflated 20ms | 20 ms | 27 products; RNDF, PNDF (+ $RNDF) | 233.91.101.35 / .99 | .37 | .36 | 14520 / 15520 |
| 530 | EBS Ultra OFF SEF NDF | (CHG: 1 ms, "Ultra EBS MTF NDF") | same 27 as 520 | 233.91.101.32 / .96 | .34 | .33 | 14530 / 15530 |
| 521 | Ultra ON SEF NDF - Conflated 20ms | 20 ms | 27 products; SNDF, SPHP | 233.91.101.51 / .115 | .53 | .52 | 14521 / 15521 |
| 531 | EBS Ultra ON SEF NDF | (CHG: 100 ms) | same 27 as 521 | 233.91.101.48 / .112 | .50 | .49 | 14531 / 15531 |

Every connection in CFG also gives a DR address and the CME source hosts; all of it
is in `config/ebs_mdp3_prod.info`. ON SEF NDF trading ceased on 2024-09-27 (NDF,
"Dates Available"), but CFG still lists 521 and 531.

### Conflated TCP channels (not used here)

532 (Credit Screened New York, 20 ms), 533 (Credit Screened London, 20 ms), 534
(Credit Screened EBS MTF NDF), 535 (Credit Screened OFF SEF NDF, 50 ms), 538 (EBS
Ultra Russian Ruble), 545 / 546 (EBS Ultra NY / London 5 ms) (CHG). TCP unicast,
one login per channel (CTCP, "EBS Session Access Model"); session negotiated with
templates Negotiate200 ... SubscriberHeartbeat210 of a separate schema, id 2 (SESS;
CTCP, "TCP MDP Session Messages"), signed with HMAC-SHA256 like iLink 3 (CTCP,
"Session Security"); 30 s heartbeats both ways (CTCP, "Heartbeat"); no session-level
retransmission (CTCP); late join by Security List, Security Status and Market Data
requests in that order (RTCP). Packet header 14 bytes: encoding type 0xCAFE (2),
message size (2), sequence (4), sending time (8) (CTCP, "Packet Structure"). The
credit-screened channels publish each firm's own view of the book (CS), not a public
book. Kaspar has no MDP TCP client; `DataDecoder::decode_one` asserts `SchemaID == 1`
(`mdp3/include/mdp3/DataDecoder.hpp`), so the session templates (schema 2) could not
pass through it.

## 3. Framing

Identical to CME futures UDP: packet header MsgSeqNum (uInt32) + SendingTime
(uInt64, ns since epoch), then messages, each a 2-byte MsgSize that counts itself, the
8-byte SBE header (BlockLength, TemplateID, SchemaID, Version) and the body.
Little-endian. Several messages per packet; a message never spans packets. This is
the framing `DataDecoder::mbo_data` walks (`mdp3/include/mdp3/DataDecoder.hpp`), and
the tests build packets the same way. Repeating groups use `groupSize` (blockLength
uInt16, numInGroup uInt8) except NoOrderIDEntries (`groupSize8Byte`) (XML).

## 4. Message catalogue

The layouts are XML's; the generated codecs are the decoders (SBE XML goes through
the SBE tool, SKILL.md step 5, so no field table is written). Offsets are 0-based from
the start of the message body. Only the fields EBS uses or that matter to the book are
listed; "EBS value" quotes the "Valid Values for EBS" column of the page cited.

### Templates per EBS feed

| Feed | Templates | Source |
|---|---|---|
| Incremental UDP (Conflated) | AdminHeartbeat12, ChannelReset4, SecurityStatus30, MDIncrementalRefreshLimitsBanding50, MDInstrumentDefinitionFX63, MDIncrementalRefreshBookLongQty64, MDIncrementalRefreshTradeSummaryLongQty65; MDIncrementalRefreshVolumeLongQty66 on 518, 519, 530 | SPEC "EBS Market on CME Globex"; VOL "Channel Scope" |
| Market Recovery UDP | AdminHeartbeat12, SnapshotFullRefreshLongQty69 | SPEC |
| Instrument Replay UDP | AdminHeartbeat12, MDInstrumentDefinitionFX63 | SPEC |
| TCP Recovery for UDP | as Incremental | SPEC |
| Conflated TCP | AdminHeartbeat12, SecurityStatus30, 50, 63, 64, 65, SnapshotFullRefreshTCPLongQty68 | SPEC |

Not sent for EBS: MBO (template 47, and NoOrderIDEntries in 64/65 is always 0: MBP
"Repeating Group 2"; TSX), implied books (MBP, MatchEventIndicator bit 4 "EBS Market
does not support implied functionality"), Request for Quote and MBOLD snapshots
(DISS), session and daily statistics (absent from SPEC's EBS list), the short-quantity
templates 46, 48, 37, 52 (SPEC).

### MDInstrumentDefinitionFX (template 63, blockLength 337, sinceVersion 12)

| Field | Tag | Offset | Type | EBS value / meaning |
|---|---|---|---|---|
| MatchEventIndicator | 5799 | 0 | uInt8 set | bit 7 last message of event (FXDEF) |
| TotNumReports | 911 | 1 | uInt32NULL | instruments in the replay loop, replay feed only (FXDEF) |
| SecurityUpdateAction | 980 | 5 | char | A add, D delete, M modify (FXDEF) |
| LastUpdateTime | 779 | 6 | uInt64 | |
| MDSecurityTradingStatus | 1682 | 14 | uInt8NULL | 2, 4, 17, 18, 21; "available in the Instrument Replay feed only" (FXDEF) |
| ApplID | 1180 | 15 | Int16 | channel id |
| MarketSegmentID | 1300 | 17 | uInt8 | e.g. 36 for 6EUS/6AUS, 38 for TWD NDF (CPI, Product Availability) |
| UnderlyingProduct | 462 | 18 | uInt8 | |
| SecurityExchange | 207 | 19 | char[4] | EBSC (EBS and eFix Markets), XEBS (OFF SEF / ON MTF NDFs) (FXDEF); NEXS for SEF NDF (CPI) |
| SecurityGroup | 1151 | 23 | char[6] | e.g. 6SUS |
| Asset | 6937 | 29 | char[6] | product code, e.g. 6EUS (CPI) |
| Symbol | 55 | 35 | char[20] | EBS values not seen (section 10) |
| SecurityID | 48 | 55 | Int32 | not reused until the next trade date after expiry or deletion (MBP) |
| SecurityType | 167 | 59 | char[6] | FXSPOT, FXNDF (CPI product names "FXSPOT.EUR/USD", "FXNDF.USD/TWD.1M") |
| CFICode | 461 | 65 | char[6] | |
| Currency / SettlCurrency / PriceQuoteCurrency | 15 / 120 / 1524 | 71 / 74 / 77 | char[3] | EUR/USD: 15=EUR, 1524=USD; NDFs settle in USD (FXDEF) |
| MatchAlgorithm | 1142 | 80 | char | F FIFO, V Institutional Prioritization, P Size Priority (FXDEF; SP) |
| MinTradeVol / MaxTradeVol | 562 / 1140 | 81 / 85 | uInt32 | base-currency amount (FX Spot+ example: 50000 / 3000000 for USD/ZAR, SECDEF) |
| MinPriceIncrement | 969 | 89 | PRICE9 | standard tick; "sent only if instrument is non-VTT eligible" (FXDEF) |
| DisplayFactor | 9787 | 97 | Decimal9 | display price to conventional price |
| PricePrecision | 2349 | 105 | uInt8 | 5; 6 for CPI products (CPI) |
| UnitOfMeasure / UnitOfMeasureQty | 996 / 1147 | 106 / 136 | char[30] / Decimal9NULL | |
| High/LowLimitPrice, MaxPriceVariation | 1149 / 1148 / 1143 | 144 / 152 / 160 | PRICENULL9 | |
| FinancialInstrumentFullName | 2714 | 169 | char[35] | e.g. FXSPOT.EUR/USD (CPI) |
| FXCurrencySymbol | 37725 | 204 | char[7] | "CCY1/CCY2" (FXDEF) |
| SettlType | 63 | 211 | char[3] | 0 for spot; B fixed-date NDF; Dx/Wx/Mx/Yx NDF tenors (FXDEF) |
| InterveningDays | 37730 | 214 | uInt16 | spot: business days trade date to value date (FXDEF) |
| FXBenchmarkRateFix, RateSource, FixRateLocalTime, FixRateLocalTimeZone | 2796, 1446, 37726, 37727 | 216, 236, 248, 256 | strings | NDF fixing |
| MinQuoteLife | 37731 | 276 | uInt32 | microseconds (FXDEF) |
| MaxPriceDiscretionOffset | 37728 | 280 | PRICE9 | 0 = no discretionary price (FXDEF) |
| InstrumentGUID | 37513 | 288 | uInt64NULL | |
| MaturityMonthYear | 200 | 296 | composite | fixed-date NDF maturity |
| SettlementLocale | 37734 | 301 | char[8] | |
| AltMinPriceIncrement | 37739 | 309 | PRICENULL9, v13 | CPI sub-tick (FXDEF; CPI) |
| AltMinQuoteLife | 37738 | 317 | uInt32NULL, v13 | MQL in µs for sub-tick orders |
| AltPriceIncrementConstraint | 37742 | 321 | PRICENULL9, v13 | sub-tick order must improve best standard-tick price by at least this |
| MaxBidAskConstraint | 37743 | 329 | PRICENULL9, v13 | sub-tick orders rejected above this spread |
| NoEvents (blockLength 9) | 864 | group | EventType 5 Activation, 7 Last eligible trade date; EventTime uInt64 |
| NoMDFeedTypes (4) | 1141 | group | MDFeedType "GBX" (Real Book), MarketDepth Int8 (FXDEF) |
| NoInstAttrib (4) | 870 | group | 871=24, 872 bitmap; bit 24 SEF, 25 MTF, 26 eFix (FXDEF) |
| NoLotTypeRules (9) | 1234 | group | LotType 2 regular amount, 5 quantity increment, 6 size-priority threshold; MinLotSize uInt64 (FXDEF; SP) |
| NoTradingSessions (18) | 386 | group | TradeDate, SettlDate (value date), MaturityDate (NDF fixing), SecurityAltID (ISIN, MTF NDFs) (FXDEF; TVD) |

### MDIncrementalRefreshBookLongQty (template 64, blockLength 11, sinceVersion 12)

Root: TransactTime 60 uInt64 (0), MatchEventIndicator 5799 (8).
NoMDEntries, blockLength 32:

| Field | Tag | Offset | Type | EBS value / meaning (MBP) |
|---|---|---|---|---|
| MDEntryPx | 270 | 0 | PRICENULL9 (mantissa int64, exponent -9) | |
| MDEntrySize | 271 | 8 | uInt64NULL | "This field is in a notional format" |
| SecurityID | 48 | 16 | Int32 | |
| RptSeq | 83 | 20 | uInt32 | per instrument; 0 on Conflated TCP |
| NumberOfOrders | 346 | 24 | Int32NULL | orders at the level |
| MDPriceLevel | 1023 | 28 | uInt8 | |
| MDUpdateAction | 279 | 29 | uInt8 | 0 New, 1 Change, 2 Delete, 5 Overlay |
| MDEntryType | 269 | 30 | char | 0 Bid, 1 Offer, w MarketBestOffer, x MarketBestBid |

NoOrderIDEntries (groupSize8Byte, blockLength 24): "Not used for EBS markets", count 0.

### MDIncrementalRefreshTradeSummaryLongQty (template 65, blockLength 11)

NoMDEntries, blockLength 40: MDEntryPx 270 PRICE9 (0); MDEntrySize 271 uInt64 (8)
**"Set to zero (EBS markets only)"**; SecurityID (16); RptSeq (20); NumberOfOrders
Int32 (24) "Always set to zero"; MDTradeEntryID 37711 uInt32 (28); AggressorSide 5797
uInt8 (32): 0 none, 1 buy, 2 sell; MDUpdateAction (33): 0 new trade, 1 change
(adjustment), 2 delete (cancel); MDEntryType constant '2'. NoOrderIDEntries count 0
(TS; TSX). On Conflated TCP bit 0 of MatchEventIndicator is set on every Trade
Summary (TS).

### MDIncrementalRefreshVolumeLongQty (template 66, blockLength 11)

NoMDEntries, blockLength 24: MDEntrySize 271 uInt64 (0), cumulative session notional;
SecurityID (8); RptSeq (12); MDUpdateAction (16) = 0; MDEntryType constant 'e'.
Sent "60 seconds in the next available 1ms interval" after the trade; trades in the
window are aggregated into one update; reset on Security Status 1174=4 (VOL,
"Electronic Volume Specification").

### SnapshotFullRefreshLongQty (template 69, blockLength 59)

LastMsgSeqNumProcessed 369 uInt32 (0); TotNumReports (4); SecurityID (8); RptSeq
(12); TransactTime (16); LastUpdateTime (24); TradeDate (32); MDSecurityTradingStatus
(34): 4, 17, 18, 21; High/LowLimitPrice, MaxPriceVariation (35, 43, 51).
NoMDEntries, blockLength 23: MDEntryPx (0), MDEntrySize uInt64NULL (8),
NumberOfOrders (16), MDPriceLevel uInt8NULL (20), OpenCloseSettlFlag (21) null,
MDEntryType (22): 0 bid, 1 offer, 2 trade, e electronic volume (SNAP).

### Shared with CME futures, unchanged

SecurityStatus30 (blockLength 30), ChannelReset4, MDIncrementalRefreshLimitsBanding50,
AdminHeartbeat12: same templates as futures (XML). EBS-specific use is in section 6.

## 5. Sequencing and recovery

Same as CME futures:

- MsgSeqNum per channel per packet, A/B arbitration on it; RptSeq per instrument
  (MBP; DISS). Kaspar's existing arbitration and gap detection (`mdp3/`,
  `mcast_recv/`) apply unchanged; the skill's rule "reuse the existing A/B
  implementation" is met.
- Snapshot alignment: LastMsgSeqNumProcessed in each snapshot is "Sequence number of
  the last Incremental feed packet processed. This value is used to synchronize the
  snapshot loop with the real-time feed" (SNAP); RptSeq in the snapshot is the
  instrument's last applied entry. Same rule as futures MBP recovery.
- Channel reset (269=J in ChannelReset4): supported on conflated EBS feeds; during a
  conflation channel reset, MBP messages are sent real-time (CONF, Example 6). Clear
  every book on the channel.
- Instrument definitions: on the incremental feed when added, modified or deleted
  mid-week, and in the Instrument Replay loop (FXDEF 980; SPEC).

Different from futures:

- Recovery is MBP: the snapshot feed carries template 69, not the MBO snapshot 53
  that Kaspar's `RecoveryProcessor` decodes.
- DR: sequence numbers reset and definitions are republished; TCP Replay and Market
  Recovery then cover DR messages only (DR, State 3 and 4).
- Rates: Market Recovery 100 packets/s on 528, 529, 530; 518 and 519 are not listed
  (RATES, MBP Market Recovery Rates).
- Conflation: book updates within an interval are merged into one event; "Messages
  that are not conflated will be sent real-time and will flush any queued conflated
  messages" (CONF). Not conflated: Security Definition, Security Status, Limits and
  Banding, Channel Reset (CONF table). Trades are each sent (CONF).

## 6. Semantics

- **Book type: MBP only**, aggregated by price level, depth from tag 264 MarketDepth
  of the FX definition (MBPD). No MBO, no implied book (section 4). CME FX Spot+
  instruments carry 264=10 (SECDEF); EBS DataMine level-2 history has "five or ten
  levels ... depending on currency pair" (DM). The EBS value of 264 was not seen.
- **Update rules** (MBPD): New inserts at MDPriceLevel and shifts lower levels down;
  levels past MarketDepth are deleted by the client. Delete removes the level and
  shifts lower levels up; "If available, an add data block will be sent to fill in
  the last price level". Change updates quantity / order count, never the price.
  "In general, if a trade occurs, CME Group will send a delete or change data block
  to update the book. The trade data block itself is not used to update the order
  book." Overlay (5) is valid for EBS (MBP) but not defined on any page read (section
  10).
- **Market Best** (269=w/x): EBS top of book regardless of credit, "sent before the
  MBP books within the event"; "EBS Market Best should be excluded from credit
  screened book management" (CS). CS describes it for conflated TCP; MBP lists w/x as
  valid EBS values without restricting the feed.
- **Trades**: no quantity. MDEntrySize 0 and NumberOfOrders 0 on every EBS trade
  (TS; TSX). AggressorSide gives the aggressor. The only traded quantity on the
  feed is the cumulative electronic volume (template 66), 60 s late on the Ultra 1 ms
  channels (VOL), and the snapshot's 'e' entry (SNAP).
- **TransactTime under conflation**: trade summaries carry the real trade time; all
  other conflated messages carry the last event's time (CONF).
- **Matching**: FIFO, Institutional Prioritization (V) or Size Priority (P) per
  instrument (FXDEF 1142). Size Priority: at each price, TOP order first, then orders
  whose original shown quantity is at least the threshold (LotType 6), FIFO, then the
  rest, FIFO (SP, "Size Priority Match Algorithm Process").
- **Hidden and discretionary liquidity**: display-quantity orders (iLink 1138) and
  price-discretion orders (845) exist on EBS (SP; EBS Messages, wiki 457606576 v5,
  2025-06-04). The MBP book shows displayed quantity at displayed price only.
- **Minimum quote life** (37731, µs) and the CPI alternate MQL (37738): constrain
  order cancellation, not the published book (CPI, "Minimum Quote Life Change").
- **Sub-pip prices** (CPI): where AltMinPriceIncrement is set, orders may rest at the
  alternate tick (example: standard 0.00005, alternate 0.000025) when the spread is at
  most MaxBidAskConstraint and the order improves the best standard-tick price by
  AltPriceIncrementConstraint. Book prices can therefore fall between standard ticks.
- **Trading states** (SST): EBS group states Open (17), Trade Date Roll while open
  (17 with 1174=4 reset stats, 327=7), Pause (2), Close not final (18), Close final (4);
  no Pre-Open, Opening or Post-Close for EBS. Instrument states Open (17), Pause (2),
  Close (18). Group messages carry 1151 and a null 48; instrument messages carry 48
  and a null 1151; the last message sent takes precedence (SST, "Security Status
  Priority").
- **Trade and value dates**: the Sunday FX definition lists every trade date of the
  week with its value date (386 group); a mid-week change republishes the definition
  (TVD).

## 7. Mapping to Kaspar

### What Kaspar does with each EBS message today

Checked by code reading and by the dispatch tests in `tests/ebs_mdp3_test.cpp`
(constructed packets through the unchanged `mdp3::DataDecoder::mbo_data`).

| Template | Kaspar today | Where | Test |
|---|---|---|---|
| 63 FX definition | Not decoded. Falls to `default:`, `log_err("unknown message: %d")`, skipped. Instrument Replay: `InstrumentRecoveryRecorder` decodes 54, 55, 56 only, logs "unkonwn template id". No securityID-to-asset mapping is ever made for an EBS instrument. | `DataDecoder.hpp` `decode_one` switch; `act/InstrumentRecoveryRecorder.hpp` | `Gap_InstrumentDefinitionFXIsNotDispatched` |
| 64 MBP LongQty | Not decoded (no case). | `DataDecoder.hpp` | `Gap_BookLongQtyIsNotDispatched` |
| 65 Trade LongQty | Not decoded. | `DataDecoder.hpp` | `Gap_TradeAndVolumeLongQtyAreNotDispatched` |
| 66 Volume LongQty | Not decoded. | `DataDecoder.hpp` | same |
| 69 Snapshot LongQty | Not decoded: `RecoveryProcessor` handles 52 (body under `#ifdef NOTIMPLEMENTED`), 53 and 12; anything else "data recovery: unknown TemplateID". | `act/RecoveryProcessor.hpp` | code reading |
| 30 Security Status | Decoded and sent to `handler_if::SecurityStatus`, which sends the `l3_sst_t` to **every book on the channel**, whatever the SecurityID (comment: CME sends these group-scoped). An EBS instrument-level Pause (SST) would mark every book on the channel halted. | `handler_if.hpp` `SecurityStatus` | `PacketWithEveryIncrementalTemplateDecodesWithoutError` (decode only) |
| 4 Channel Reset | Decoded; `l3_chr_v2_t` to every book; order-ID map cleared. Correct for EBS, but there is no MBP state to clear because there is no MBP path. | `handler_if.hpp` `ChannelReset` | `ChannelResetAndLimitsBandingAreDispatched` |
| 50 Limits Banding | Decoded; handler body empty. | `handler_if.hpp` | same |
| 12 Heartbeat | Ignored, as for futures. | `DataDecoder.hpp` | |

Further gaps on the path a decoded EBS message would take:

| # | Gap | Where |
|---|---|---|
| G1 | The MBP callback has no MDUpdateAction parameter: `decode_MDIncrementalRefreshBook46` reads price, size, orders, level and entry type but not 279, so New/Change/Delete/Overlay cannot be told apart. | `mbo_if.hpp` `MDIncrementalRefreshBook` (13-argument overload); `msg_decoder.hpp` `decode_MDIncrementalRefreshBook46` |
| G2 | Quantities are `int32_t` in every MBP, trade and volume callback. EBS sizes are uInt64 notional; a 3,000,000,000 level narrows to a negative `int32_t` (test `Gap_Int32QuantityInterfaceWouldTruncate`). | `mbo_if.hpp` |
| G3 | `handler_if` drops all MBP data: `filter_mbp_ = true` is fixed, and the MBP bodies sit under `#ifdef mbp` and reference `mbp_order_books`, a member that does not exist. There is no MBP-to-OB path in the CME handler. | `handler_if.hpp` |
| G4 | Instrument mapping exists only in `MDInstrumentDefinitionFuture` (and Option, Spread): lookup by tag 55 (or Asset for `TreasOnly`), `set_units(minPriceIncrement)`. No FX equivalent; no use of AltMinPriceIncrement. | `handler_if.hpp` |
| G5 | `kaspr.cpp` creates OBs only for `CMEMDFUT` assets whose mnemonic is ES (OB) or ES/NQ/ZN (TachBook), and starts channels with `handler_if`. No EBS venue code exists in `en::x`. | `kaspr/src/kaspr.cpp`; `chutil/gencode/enum/def/exch_code.enum` |
| G6 | `genconfig_mdp3_3.py` fetches `config.xml` only; EBS channels are in `Ultra*/EBS*Config.xml`. It maps `group_dr` only from connection ids containing `AMBO`; EBS snapshot connections are `<chan>SA`, so `group_dr`, which `kaspr.cpp` requires, would be missing. | `genconfig/genconfig_mdp3_3.py` |
| G7 | Conflated TCP channels need an MDP TCP client (schema 2 session, HMAC). None exists; `decode_one` asserts schema 1. | `DataDecoder.hpp` |

`genschema/` needs no change: CME's current production template (version 13) is the
file it already uses and contains every EBS template.

### Proposed mapping (for the patch in `patch_proposal.md`)

By `kaspar_mapping.md`. EBS is a price-level feed, so it goes through `sim::Mbp2Mbo`
with `sim::LevelWindow`.

| Venue message | Kaspar record | Notes |
|---|---|---|
| 64, 279=0/1/2/5, 269=0/1 | `ebs::MbpLevelBook` (prototype here) -> `LevelWindow` (depth = 264) -> `Mbp2Mbo`: `l3_mbo_v2_t` add/delete of synthetic chunks | level-index rules of MBPD; quantity as uint64 until divided by the chunk |
| 64, 269=w/x | none | reference only (CS) |
| 65 | `l3_mbp_trd_t` print: `pxd` = price in ticks, `aggresorSide` from 5797, `sz` = see decision 1/2 | 271 is 0 |
| 65 executions against resting chunks | see decision 1 | no order-level executions on EBS |
| 66 | none to OB; recorded | cumulative, 60 s late |
| 69 | `Mbp2Mbo::snapshot` (recovery records) | aligned on LastMsgSeqNumProcessed |
| 30 group (48 null) | `l3_sst_t` to every book in the group | status values map one to one: 17, 2, 18, 4 |
| 30 instrument (1151 null) | `l3_sst_t` to that instrument's book only | |
| 4 | `l3_chr_v2_t` to every book on the channel; clear `MbpLevelBook`s and `LevelWindow`s | |
| gap not yet recovered | `l3_gap_v2_t` to the channel's books | as futures |
| end of packet | `l3_eop_t` | as futures |

- Priority: none to translate; MBP has no order priority. Synthetic chunks are FIFO
  per level (`Mbp2Mbo`): a level that grows adds at the back, one that shrinks
  removes from the back. Size Priority and Institutional Prioritization cannot be
  expressed (known deviation, section 10).
- Order identifiers: `Mbp2Mbo` numbers its chunks per instance; one instance per
  instrument, one OB per instrument, so ids are unique per book.
- Quantity unit: decision 6 (`review.md`). The prototype and tests use 1,000,000.
- Prices: `Mbp2Mbo` sends prices in ticks (`pxd = price / tick`), so the asset's
  `units` is 1 and the tick is chosen per decision 7.
- `endOfEvent`: from MatchEventIndicator bit 7, on the last record of the event;
  `Mbp2Mbo::end_event` releases the event's records.
- Trading status: 17 -> 17, 2 -> 2, 18 -> 18, 4 -> 4, 21 -> 21 (values already CME's).

## 8. Instruments

- Source: template 63 on the incremental feed and the Instrument Replay loop (SPEC).
  No file source: SECDEF holds no EBS instruments, and its disclaimer says it covers
  "CME Group futures and options on futures only".
- Products per channel: CFG `<product code>` / `<group code>`. Examples with the pair
  stated by CME: 6EUS = FXSPOT.EUR/USD, 6AUS = FXSPOT.AUD/USD, group 6SUS, MIC EBSC,
  segment 36, channels 518 and 528 (CPI table). Every other code-to-pair
  correspondence is inferred from the code pattern; tag 37725 settles it.
- Pairs traded (DM): 59 spot pairs (EUR/USD ... USD/ZAR) and metals XPD/USD LN,
  XPT/USD LN, XPD/USD, XPT/USD, XAG/USD SM, XAU/EUR SM, XAU/USD SM; large silver
  discontinued 2026-07-17.
- Price: PRICE9 / PRICENULL9 (mantissa x 10^-9). Tick 969, alternate tick 37739;
  PricePrecision 5, or 6 for CPI products (CPI); DisplayFactor 9787.
- Quantity: base-currency amount (notional), uInt64 (MBP; FXDEF 562 for minimum).
  For metals, UnitOfMeasure gives the unit.
- Lifecycle: spot instruments carry Activation and Last Eligible Trade Date events
  (FXDEF group 1; FX Spot+ example: activation 2018, last trade 2050, SECDEF); the
  trade-date list and value dates come with the Sunday definition (TVD). NDF tenors
  roll with value dates (FXDEF 63, 541).
- Instruments with zero or negative prices: none expected for spot or metals; NDF
  prices are outright rates. Not checked.
- Universe proposal: `config/universe_ebs.csv`.

## 9. Pitfalls

One line per item of `pitfalls.md`.

**1. Reading the specification**
- Several documents, versions: one schema (v13); wiki pages dated 2024-12-26 to
  2026-11-17; SPEC (2024-12) omits template 66 for EBS while VOL (2026-09) adds it on
  518/519/530: the newer page is taken (section 10, settled).
- Errata / change tables: CHG's "Pending Client System Impacts" not read; N2023 only
  through a summary.
- Tables across page breaks: n/a (HTML tables via the REST API); the wiki's own table
  for channels 519-531 in CHG differs from CFG (section 10).
- Offsets 0/1-based, length counting itself: SBE offsets are from the XML; MsgSize
  counts itself (as `mbo_data` walks it).
- Reserved/padding: SBE blockLength covers them; codecs handle it.
- Enumerations elsewhere: MDEntryTypeBook, MDUpdateAction, SecurityTradingStatus read
  from XML.
- Field names differing: FXDEF calls 37739 "AltMinPriceIncrement"; CPI's text says
  "37739-AltMinQuoteLfe" for the alternate MQL, which is tag 37738 in XML and FXDEF.
  XML taken.
- Examples contradicting tables: CPI's alternate tick example 0.000025 against
  PricePrecision 6: consistent. None found.
- Two protocol layers: UDP has one; Conflated TCP has a separate session schema (id
  2), not handled.
- Repeated names with different codes: 46/64, 48/65, 37/66, 52/69 are the short and
  long quantity variants of one message; EBS uses only the long ones.
- Variable-length trailing field: none in the EBS templates.
- Odd-width integers: none.

**2. Framing and encoding**
- Endianness: little, all templates.
- Signedness: MDEntrySize unsigned 64 (book, trade, volume); NumberOfOrders signed 32.
- Fixed-point prices: global exponent -9 (PRICE9 constant exponent); PricePrecision is
  display precision only.
- Null values: PRICENULL9 mantissa INT64_MAX, uInt64NULL UINT64_MAX, Int32NULL
  INT32_MAX (XML).
- Strings: fixed-length char arrays, NUL-padded (codec `get...AsString`).
- Messages per packet: several; never split.
- Timestamps: ns since Unix epoch (60, 779, 1145); LocalMktDate days since epoch.

**3. Sequencing and recovery**
- Sequence scope: MsgSeqNum per channel; RptSeq per instrument; TCP sends RptSeq 0
  (MBP).
- A/B arbitration: existing `mdp3` implementation.
- Gap rule: as futures.
- Recovery mechanism: MBP snapshot loop (69), TCP replay; decision 3.
- Snapshot alignment: LastMsgSeqNumProcessed (SNAP).
- Snapshot too old: as futures (wait for the next loop).
- State to clear on reset: books, `MbpLevelBook`s, `LevelWindow`s, `Mbp2Mbo`
  queues, accumulated volume; also on DR, where sequence numbers reset (DR).
- Recovery paths rarely run: no capture, so not exercised (section 10).
- Session behaviour: UDP has none; DR changes source hosts, not groups (DR).

**4. Book semantics**
- MBP by level index with price included (MBPD).
- Insert shifts down, delete shifts up; DeleteThru/DeleteFrom not listed for EBS.
- Depth-limited: levels past MarketDepth are dropped by the client; a delete may be
  followed by an add for the last level (MBPD). Prototype test
  `DepthTenDropsTheEleventhLevel`.
- Priority on modify: n/a (MBP); size priority is a known deviation.
- Implied, iceberg, hidden: no implied; display quantity and discretion orders exist
  (hidden).
- Trades also as book deltas: yes, "CME Group will send a delete or change data block"
  (MBPD); trades carry no size, so no double counting is possible, and no execution
  quantity is available (decision 1).
- Side of trade: AggressorSide (aggressor).
- Trading states: SST; crossed book legal only in 17 for OB.
- Book clears: channel reset (CONF), DR (DR).

**5. Prices, quantities and instruments**
- Price or yield: price (FX rate, metal price).
- Quantity multipliers: notional base-currency amounts; no per-book multiplier tag
  seen (UnitOfMeasureQty for metals not seen).
- Decimals sentinels: AltMinPriceIncrement null means no sub-tick ("An Alternate
  Globex Tick of Null is interpreted as not allowing Alternate Globex Minimum Tick",
  CPI).
- Price-dependent ticks: MinPriceIncrement "sent only if instrument is non-VTT
  eligible" (FXDEF): a variable tick table may apply to some instruments; not seen.
- Instrument definition source and ids: template 63; SecurityID int32.
- Lifecycle: weekly definitions; NDF tenors; ON SEF NDFs ended 2024-09-27 (NDF).

**6. Behaviour the specification omits**
- Subscription options: n/a for UDP; channel choice sets the conflation interval.
- History on subscribe: snapshot carries last trade and volume entries (SNAP).
- Whole-book snapshots: limited to MarketDepth.
- Order within an event: Market Best before MBP (CS); trade before book delta is not
  stated for EBS.
- Connection limits: n/a for UDP; TCP 30 s heartbeat (CTCP).

**7-8. Post-trade, order entry**: not part of this feed.

## 10. Ambiguities

| Question | Reading taken | Alternatives | Resolved by |
|---|---|---|---|
| Fills and volume when trades carry no quantity (271=0) | Not resolved: no execution quantity exists on the feed | infer from book decrement at the trade price in the same event; delayed volume (66); no trade-driven fills | review.md decisions 1, 2 |
| Effect of conflation on queue simulation | Book states between conflation flushes are lost; queue positions are approximate | use 1 ms channels (518/519) to minimise | decision 1, 5 |
| Recovery for EBS UDP | Not resolved; `cme_do_recovery false` in the proposal | add 69 to RecoveryProcessor; TCP replay; restart at week start only | decision 3 |
| Instrument key (tag 55 values unknown) | Asset (6937), unique per product in CFG | tag 55; FXCurrencySymbol 37725 | decision 4 |
| Which channels | 518 and 519 (1 ms) | 528/529 (5 ms); NDF 520/530 | decision 5 |
| Quantity unit (chunk) | 1,000,000 in the prototype | LotType 5 increment per instrument; MinTradeVol | decision 6 |
| Tick for prices in OB | Alternate tick where 37739 is set, else 969 | standard tick and round sub-tick prices | decision 7 |
| Meaning of MDUpdateAction 5 Overlay for EBS | Replace price and quantity at MDPriceLevel | treat as Change; treat as error and recover | decision 8 (CME to confirm) |
| Market Best (w/x) on UDP Ultra | Excluded from the book if it appears (CS: excluded from book management) | | settled by CS |
| Template 66 on EBS UDP | Decoded on 518/519/530 | SPEC 2024-12 omits it | settled: VOL (2026-09) is newer |
| CHG vs CFG channel names/intervals: CHG has 519 "EBS Ultra London 1 ms" under OFF SEF NDF, 528/529 "5 ms", 520/521 "OFF SEF NDF 5 ms", 530 "Ultra EBS MTF NDF 1 ms", 531 "OFF SEF NDF 100 ms"; CFG has 519 EM FX 1 ms, 520/521 OFF/ON SEF NDF 20 ms, 530 OFF SEF NDF, 531 ON SEF NDF | CFG | CHG | settled by CHG's own note that config.xml governs; the CHG table's row spans may also have been flattened by the export |
| Size Priority (P) and Institutional Prioritization (V) | OB/Mbp2Mbo FIFO; known deviation | model LOS priority in Mbp2Mbo | settled by kaspar_mapping.md (cannot be expressed) |
| Instrument-level Security Status | Route to that instrument's book only | broadcast (handler_if today) | settled by SST (48 present, 1151 null = instrument level) |
| Quantities above 2^31 | Kept uint64 to the chunk division | int32 | settled by MBP (uInt64) |
| `$` / `#` prefixed products and groups in CFG (`#6EUS`, `$6SUS`, `#GCUS`) | Excluded from the universe | | low impact; not explained in any page read |
| Second source host of 519SB is 164.74.55.39, same as 519SA (others pair .3x/.13x) | Recorded as given | typo for .139 | low impact (source filter only) |
| 521/531 ON SEF NDF channels still configured after trading ceased | Not used | | low impact |
| MarketDepth for EBS instruments | Read per instrument from 264 | assume 10 | settled: per instrument from the definition |
| Trade date roll (17 + 1174=4) | Book unchanged; reset volume | | low impact |
| NDF and metals prices/quantities | Not routed in the proposal except one metal row | | low impact for spot; NDFs out of scope |
| DisplayFactor for EBS | Prices used as sent (CME FX Spot+ carries 1) | multiply by 9787 | low impact until a definition is seen |
| Order of trade and book delta within an EBS event | Not stated | | affects decision 1 option (b) only |

No EBS capture was available, so none of these was settled with real data. CME's
DataMine sample files for EBS (DM) are CSV time slices, not MDP 3.0 packets.
