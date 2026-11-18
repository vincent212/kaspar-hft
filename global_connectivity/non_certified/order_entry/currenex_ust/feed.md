# Feed description: Currenex US Treasury OUCH 2.0 order entry

Every entry cites its source. "UST OUCH" is the Currenex *Ouch 2.0 US
Treasury Specification*; section numbers and page numbers are that
document's (printed page footers equal PDF page numbers). Other documents
are named where cited.

The handler is not certified and has never connected to Currenex. This
description was written from the public documents only.

## 1. Venue and documents

Currenex (State Street, per the documents' disclaimer) offers US Treasury
trading alongside FX. Order entry for Treasuries is a fixed-field binary
protocol Currenex calls OUCH, carried over TCP.

| Document | Version | Date | Applies to |
|---|---|---|---|
| *Ouch 2.0 US Treasury Specification* (`UST Ouch Specification.pdf`, 36 pages, https://www.currenex.com/support/documentation/ustreasury/) | Revision 1, Logon Version 200 | cover 10 February 2022; revision history 21 February 2022; PDF created 22 Feb 2022 | **This handler.** Order entry for US Treasuries by a market participant (MP). |
| *Ouch 3.0 US Treasury Specification for Firm LP* (`UST Firm LP Ouch Specification.pdf`, 36 pages) | Revision 1, Logon Version 300 ("Exclusively for Firm LP users") | 19 April 2024 | Not used. Firm liquidity providers only: Instrument Info adds Side and StreamReferenceID and is keyed by InstrumentInfoID (sections 6, 7, 8 and 11.5 of that document), trade messages add an LMUID. Version 300 is refused by the config check. |
| *OUCH Specification* (FX; `currenex/OUCH-Spec.pdf`, 33 pages) | revision 7 of 22 September 2016 in its history; cover 4 June 2024 | 4 June 2024 | Not used for layouts: it is the FX protocol (Logon type `A` (0x41) without a Version field, FX Instrument Info). Read for comparison only; its FAQ (11.3) is cited where it describes behaviour the UST document leaves out. |
| *Ouch Order Persistence* addendum (FX documentation tree, `OUCH_PersistenceAddendum.pdf`, 6 pages) | - | 4 June 2024 | Same persistence text as UST OUCH 5.5 plus a *Weekly Reset* paragraph (2.1, p.3): outbound numbering reset to 1 every week at 7:00 pm ET. Taken to apply to UST too (section 10, A7). |
| *CX OUCH Conformance Test Plan* (FX, `.xls`, last saved 23 January 2018) | - | 2018 | FX test cases with byte dumps. Its strings were read (not the spreadsheet layout) for one question: which ClOrderID a cancel acknowledgement carries (section 10, A9). |
| *UST Itch Specification* | - | - | Market data; not read. |

The specification calls itself "Confidential & Proprietary" in its footer
but is published without login on currenex.com. It is not copied into the
repository; the field table cites it.

## 2. Transport

- TCP, one connection per session (UST OUCH 3, p.5), over cross connects or
  metro circuits, not the Internet (1.1, p.3). Addresses and ports are
  given by Currenex at onboarding; none are in the specification.
- No A/B feeds, no multicast.
- Session and logon: section 5 below.
- Heartbeats: Currenex sends a Heartbeat every 3 seconds; the client must
  answer each with its own; two consecutive missed answers and Currenex
  closes the session (5.3, p.8; logout reason A9).
- Throttles: none stated. Error code 0x0C *Maximum Permitted Operations
  Exceeded* and 0x10 *Maximum Number of Active Orders Exceeded* exist
  (Appendix A, p.35) without limits.

## 3. Framing

- Every message is `SOH (0x01) | header | body | ETX (0x03)` (4.1, p.6,
  Figure 1). A "block" (what one write carries) is at most 1,000 bytes
  but several blocks can be sent together (4, p.6). The largest message
  (Spread PendingFillTrade) is 277 bytes plus the two delimiters.
- Header (9, p.17): SequenceNumber (Integer, offset 0, 4), Timestamp
  (Integer, 4, 4: milliseconds past midnight GMT, 4.2.4 p.7),
  MessageType (Alpha, 8, 1). Body offsets in every message table start at
  9, i.e. they count from the first header byte, not from SOH.
- No length field. Each type has a fixed length, so the receiver measures
  a message by its type. There is no escaping: integer fields can contain
  0x01 and 0x03, so SOH and ETX are checked at the positions the length
  gives, never searched for (except to resynchronise after a type the
  table does not list).
- Integers (Integer 4, Short 2, Long 8) are signed, big-endian (4.2.1,
  p.6). Amount: Long with 2 implied decimals (4.2.2). Rate: Long with 9
  implied decimals (4.2.3). Date: Long, milliseconds since 1970-01-01 UTC
  (4.2.4). Text (Alpha, Alphanumeric): left-justified, padded with spaces
  (4.2.5).
- The field table gives the header its own struct (`Header`); message
  structs start at MessageType (`@offset_base 8`), so `decode()` finds the
  type at its first byte. The 2-byte length prefix machinery of
  `fieldgen.py` is not used.

## 4. Message catalogue

Generated from `currenex_ust_ouch.tbl` with `fieldgen.py --markdown`.
Offsets are the specification's (from the first header byte). The size in
each heading is the struct's: from MessageType to the end, without the
8-byte header and the two delimiters; the specification's total is 8 more.
Section `ouch::to_currenex` is what the client sends, `ouch::from_currenex`
what Currenex sends; Logon, Logout and Heartbeat have the same layout in
both directions and are declared in both. All 27 messages of the
specification are in the table (10 to Currenex, 17 from Currenex), plus the
header.

### ouch::to_currenex_header

#### Header (type none, 8 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| SequenceNumber | 0 | 4 | Integer | Directional session-based sequence number |
| Timestamp | 4 | 4 | Integer | Milliseconds past midnight GMT (UST OUCH 4.2.4, p.7) |

### ouch::to_currenex

#### Logon (type `a`, 47 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 8 | 1 | char | Logon: open a session; Version 200 (UST OUCH 10.1, p.18; 5.1, p.8) |
| UserID | 9 | 20 | Alpha | User session login id |
| Password | 29 | 20 | Alpha | User password |
| SessionID | 49 | 4 | Integer | Unique session ID provided by Currenex; only used by Currenex in its Logon response |
| Version | 53 | 2 | Short | Version of the service requested; supported: 200 |

#### Logout (type `B`, 28 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 8 | 1 | char | Logout: end the session; the recipient answers with a Logout (UST OUCH 10.2, p.18; 5.2, p.8) |
| UserID | 9 | 20 | Alpha | User session login id |
| SessionID | 29 | 4 | Integer | Unique session ID provided by Currenex |
| Reason | 33 | 3 | Alpha | Logout reason code (Appendix B, p.35) |

#### Heartbeat (type `C`, 5 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 8 | 1 | char | Heartbeat: answer to each Currenex Heartbeat (UST OUCH 10.3, p.18; 5.3, p.8) |
| SessionID | 9 | 4 | Integer | Unique session ID provided by Currenex |

#### InstrumentInfoRequest (type `E`, 5 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 8 | 1 | char | InstrumentInfoRequest: asks for the Instrument Info messages, required after Logon (UST OUCH 10.4, p.18; 6, p.12) |
| SessionID | 9 | 4 | Integer | Unique session ID provided by Currenex |

#### RoutingUserInfoRequest (type `G`, 5 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 8 | 1 | char | RoutingUserInfoRequest: asks for the RoutingUserInfo messages, optional (UST OUCH 10.7, p.21; 7.1, p.13) |
| SessionID | 9 | 4 | Integer | Unique session ID provided by Currenex |

#### NewOrder (type `L`, 46 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 8 | 1 | char | NewOrder: NewOrderRequest (UST OUCH 10.9, pp.21-22) |
| ClOrderID | 9 | 4 | Integer | Client assigned order id |
| OrderType | 13 | 1 | Alpha | F limit, Z iceberg |
| InstrumentIndex | 14 | 2 | Short | From Instrument Info; valid for the session only |
| Side | 16 | 1 | Alpha | B buy, S sell |
| OrderAmt | 17 | 8 | Long | Order amount, notional with 2 implied decimals |
| MinAmt | 25 | 8 | Long | Minimum quantity for which the order can be executed; 0 valid |
| Price | 33 | 8 | Rate | Order rate, 9 implied decimals |
| ShowAmt | 41 | 8 | Long | Iceberg: displayed amount, MinAmt <= ShowAmt <= OrderAmt, 0 hidden; ignored for limit orders |
| ExpireType | 49 | 1 | Alpha | G good till cancel, I immediate or cancel (partial fills permitted) |
| RoutedUserKey | 50 | 4 | Integer | Routed user (OBO); 0 or -1 when not routing for another entity |

#### OrderCancelRequest (type `N`, 15 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 8 | 1 | char | OrderCancelRequest: CancelOrder (UST OUCH 10.11, p.23) |
| NewClOrderID | 9 | 4 | Integer | Client assigned id for the cancel action |
| PrevClOrderID | 13 | 4 | Integer | Client assigned id for the order to be cancelled |
| InstrumentIndex | 17 | 2 | Short | From Instrument Info |
| RoutedUserKey | 19 | 4 | Integer | Routed user (OBO) |

#### OrderReplaceOrCancelRequest (type `P`, 31 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 8 | 1 | char | OrderReplaceOrCancelRequest: replace; on a partially filled order the outstanding amount is cancelled instead (UST OUCH 10.13.1, p.24; 8.3, p.14) |
| NewClOrderID | 9 | 4 | Integer | Client assigned id for the replace or cancel action |
| OrigClOrderID | 13 | 4 | Integer | Client assigned id for the order to be replaced or cancelled |
| OrderAmt | 17 | 8 | Long | New order amount; Amount and Rate are both required |
| Price | 25 | 8 | Rate | New order rate, 9 implied decimals |
| InstrumentIndex | 33 | 2 | Short | From Instrument Info |
| RoutedUserKey | 35 | 4 | Integer | Routed user (OBO) |

#### OrderReplaceEvenIfPartiallyFilled (type `S`, 31 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 8 | 1 | char | OrderReplaceEvenIfPartiallyFilled: replace the unfilled part; OrderAmt is the whole order including fills (UST OUCH 10.13.2, p.24; 8.4, p.15) |
| NewClOrderID | 9 | 4 | Integer | Client assigned id for the replace action |
| OrigClOrderID | 13 | 4 | Integer | Client assigned id for the order to be replaced |
| OrderAmt | 17 | 8 | Long | New order amount; must exceed the amount already filled |
| Price | 25 | 8 | Rate | New order rate, 9 implied decimals |
| InstrumentIndex | 33 | 2 | Short | From Instrument Info |
| RoutedUserKey | 35 | 4 | Integer | Routed user (OBO) |

#### ResendRequest (type `2`, 5 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 8 | 1 | char | ResendRequest: replay from BeginSeqNo after a reconnect; persistence only (UST OUCH 10.18, p.34; 5.5, pp.9-11) |
| BeginSeqNo | 9 | 4 | Integer | Sequence number of the first message to resend |

### ouch::from_currenex

#### Logon (type `a`, 47 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 8 | 1 | char | Logon: Logon response; its header SequenceNumber is the session's current outbound number (UST OUCH 10.1, p.18; 5.5, p.9) |
| UserID | 9 | 20 | Alpha | User session login id |
| Password | 29 | 20 | Alpha | User password |
| SessionID | 49 | 4 | Integer | Unique session ID provided by Currenex |
| Version | 53 | 2 | Short | Version of the service; 200 |

#### Logout (type `B`, 28 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 8 | 1 | char | Logout: end of session or logon refused (UST OUCH 10.2, p.18) |
| UserID | 9 | 20 | Alpha | User session login id |
| SessionID | 29 | 4 | Integer | Unique session ID provided by Currenex |
| Reason | 33 | 3 | Alpha | Logout reason code A1 to A11 (Appendix B, p.35) |

#### Heartbeat (type `C`, 5 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 8 | 1 | char | Heartbeat: sent every 3 seconds; answer each (UST OUCH 10.3, p.18; 5.3, p.8) |
| SessionID | 9 | 4 | Integer | Unique session ID provided by Currenex |

#### OutrightInstrumentInfo (type `D`, 82 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 8 | 1 | char | OutrightInstrumentInfo: Instrument Info for one outright instrument (UST OUCH 10.5, p.19) |
| SessionID | 9 | 4 | Integer | Unique session ID provided by Currenex |
| InstrumentIndex | 13 | 2 | Short | Identifier used in order messages; unique for the session only |
| InstrumentType | 15 | 1 | Alpha | 1 foreign exchange, 2 cash metals, 3 treasury |
| InstrumentID | 16 | 20 | Alpha | Free form instrument definition string, e.g. OTR-2YR |
| SettlementDate | 36 | 8 | Long | Settlement date, ms since 1970-01-01 GMT |
| SecurityID | 44 | 9 | Alpha | CUSIP |
| SecurityType | 53 | 1 | Alpha | 1 TBill, 2 TNote, 3 TBond |
| MaturityDate | 54 | 8 | Long | Maturity date, ms since 1970-01-01 GMT; 0 if absent |
| IssueDate | 62 | 8 | Long | Issue date, ms since 1970-01-01 GMT; 0 if absent |
| InstrumentState | 70 | 10 | Alpha | WI when-issued, OTR on-the-run, OLD first off-the-run, OLDn (n 1 to 60) other off-the-run |
| InstrumentTenor | 80 | 10 | Alpha | Instrument tenor, e.g. 2Y |

#### SpreadInstrumentInfo (type `e`, 151 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 8 | 1 | char | SpreadInstrumentInfo: Instrument Info for one spread instrument (UST OUCH 10.6, pp.20-21) |
| SessionID | 9 | 4 | Integer | Unique session ID provided by Currenex |
| InstrumentIndex | 13 | 2 | Short | Identifier used in order messages; unique for the session only |
| InstrumentType | 15 | 1 | Alpha | 1 MLEG |
| InstrumentID | 16 | 20 | Alpha | Free form instrument definition string |
| InstrumentState | 36 | 15 | Alpha | Instrument state, e.g. OTR/OTR/A |
| InstrumentTenor | 51 | 10 | Alpha | Instrument tenor, e.g. 2Y/3Y |
| InstrumentTickSize | 61 | 8 | Rate | Tick size of the spread: WI rolls 0.125, all others 0.05 |
| AnchorLegInstrumentIndex | 69 | 2 | Short | Instrument index of the anchor leg |
| AnchorLegInstrumentID | 71 | 10 | Alpha | Instrument definition string of the anchor leg |
| AnchorLegInstrumentState | 81 | 10 | Alpha | Instrument state of the anchor leg |
| AnchorLegInstrumentTenor | 91 | 5 | Alpha | Instrument tenor of the anchor leg |
| AnchorLegSecurityID | 96 | 9 | Alpha | CUSIP of the anchor leg |
| AnchorLegRatio | 105 | 8 | Amount | Quantity of the anchor leg per unit of the spread |
| AnchorLegSide | 113 | 1 | Alpha | B buy, S sell: anchor leg side for a buy of the spread |
| CalculatedLegInstrumentIndex | 114 | 2 | Short | Instrument index of the calculated leg |
| CalculatedLegInstrumentID | 116 | 10 | Alpha | Instrument definition string of the calculated leg |
| CalculatedLegInstrumentState | 126 | 10 | Alpha | Instrument state of the calculated leg |
| CalculatedLegInstrumentTenor | 136 | 5 | Alpha | Instrument tenor of the calculated leg |
| CalculatedLegSecurityID | 141 | 9 | Alpha | CUSIP of the calculated leg |
| CalculatedLegRatio | 150 | 8 | Amount | Quantity of the calculated leg per unit of the spread |
| CalculatedLegSide | 158 | 1 | Alpha | B buy, S sell: calculated leg side for a buy of the spread |

#### RoutingUserInfo (type `g`, 29 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 8 | 1 | char | RoutingUserInfo: one routed user, and one for the session's own user (UST OUCH 10.8, p.21; 7.1, p.13) |
| SessionID | 9 | 4 | Integer | Unique session ID provided by Currenex |
| RoutedUserId | 13 | 20 | Alpha | String that uniquely identifies a routed user |
| RoutedUserKey | 33 | 4 | Integer | Number that uniquely identifies a routed user; same across sessions and services |

#### NewOrderAck (type `M`, 20 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 8 | 1 | char | NewOrderAck: accepted or rejected (UST OUCH 10.10, p.22) |
| ClOrderID | 9 | 4 | Integer | Client assigned order id |
| OrderID | 13 | 8 | Long | Currenex assigned order id; -1 on reject |
| Status | 21 | 1 | Alpha | C confirmed, R rejected |
| ErrorCode | 22 | 2 | Short | 1 to 99 on reject (Appendix A, p.35); 0 when confirmed |
| RoutedUserKey | 24 | 4 | Integer | Routed user (OBO) |

#### OrderCancelReject (type `O`, 15 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 8 | 1 | char | OrderCancelReject: cancel or replace refused (UST OUCH 10.12, p.23) |
| NewClOrderID | 9 | 4 | Integer | Client assigned id of the refused action |
| PrevClOrderID | 13 | 4 | Integer | Client assigned id for the order to be cancelled |
| ErrorCode | 17 | 2 | Short | Error code (Appendix A, p.35) |
| RoutedUserKey | 19 | 4 | Integer | Routed user (OBO) |

#### OrderReplaceAck (type `Q`, 16 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 8 | 1 | char | OrderReplaceAck: answer to a replace (UST OUCH 10.14, p.25; 8.3.1, p.14; 8.4, p.15); the spec names the first field NewClOrderD |
| NewClOrderID | 9 | 4 | Integer | Client assigned id for the replace action |
| PrevClOrderID | 13 | 4 | Integer | Client assigned id for the order to be replaced |
| Status | 17 | 1 | Alpha | P replaced, C in process (no replace; a separate cancel or trade follows), R rejected |
| ErrorCode | 18 | 2 | Short | Error code on reject (Appendix A, p.35); 0 otherwise |
| RoutedUserKey | 20 | 4 | Integer | Routed user (OBO) |

#### OrderCancelledOrExpired (type `R`, 20 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 8 | 1 | char | OrderCancelledOrExpired: Unsolicited Cancel; also the answer to a cancel (UST OUCH 10.15, p.25; 8.6, p.16) |
| ClOrderID | 9 | 4 | Integer | Client assigned id for the order |
| OrderID | 13 | 8 | Long | Currenex assigned order id |
| Status | 21 | 1 | Alpha | E expired, C cancelled |
| Type | 22 | 2 | Short | 0 user cancel, 1 system cancel, 2 below minimum cancel |
| RoutedUserKey | 24 | 4 | Integer | Routed user (OBO) |

#### PendingFill (type `U`, 42 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 8 | 1 | char | PendingFill: the order matched; a trade (t or f) or a PendingFillCancel follows (UST OUCH 10.16.1, pp.25-26; 8.5, p.15) |
| ClOrderID | 9 | 4 | Integer | Client assigned id for the order |
| OrderID | 13 | 8 | Long | Currenex assigned order id |
| TradeLinkID | 21 | 4 | Integer | Links pending fill, fill cancel and trade; unique within the order only |
| FillAmt | 25 | 8 | Long | Trade amount, 2 implied decimals |
| FillRate | 33 | 8 | Long | Done trade rate, 9 implied decimals (typed Long in the spec) |
| AggressorFlag | 41 | 1 | Alpha | 1 client is aggressor, 2 client is not aggressor |
| ExecBroker | 42 | 4 | Alpha | Execution broker's code, or NA |
| RoutedUserKey | 46 | 4 | Integer | Routed user (OBO) |

#### PendingFillCancel (type `V`, 42 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 8 | 1 | char | PendingFillCancel: the matched fill did not complete; the order can match again (UST OUCH 10.16.2, pp.26-27; 8.5.2, p.16) |
| ClOrderID | 9 | 4 | Integer | Client assigned id for the order |
| OrderID | 13 | 8 | Long | Currenex assigned order id |
| TradeLinkID | 21 | 4 | Integer | Links pending fill, fill cancel and trade; unique within the order only |
| FillAmt | 25 | 8 | Long | Trade amount, 2 implied decimals |
| FillRate | 33 | 8 | Long | Done trade rate, 9 implied decimals (typed Long in the spec) |
| AggressorFlag | 41 | 1 | Alpha | 1 client is aggressor, 2 client is not aggressor |
| ExecBroker | 42 | 4 | Alpha | Execution broker's code, or NA |
| RoutedUserKey | 46 | 4 | Integer | Routed user (OBO) |

#### PendingFillTrade (type `t`, 132 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 8 | 1 | char | PendingFillTrade: Outright PendingFillTrade, the trade after a PendingFill (UST OUCH 10.16.3, pp.27-28) |
| ClOrderID | 9 | 4 | Integer | Client assigned id for the order |
| OrderID | 13 | 8 | Long | Currenex assigned order id |
| TradeLinkID | 21 | 4 | Integer | Links pending fill, fill cancel and trade; unique within the order only |
| InstrumentIndex | 25 | 2 | Short | From Instrument Info |
| Side | 27 | 1 | Alpha | B buy, S sell |
| FillAmt | 28 | 8 | Long | Trade amount, 2 implied decimals |
| FillRate | 36 | 8 | Rate | Done trade rate, 9 implied decimals |
| ExecBroker | 44 | 4 | Alpha | Execution broker's code, or NA |
| ExecutionID | 48 | 20 | Alphanumeric | Trade ID |
| ExecType | 68 | 1 | Alpha | 1 new trade |
| SettleDate | 69 | 8 | Long | Settlement date, ms since 1970-01-01 GMT |
| TradeDate | 77 | 8 | Long | Trade date, ms since 1970-01-01 GMT; after the roll the next business date |
| TransactTime | 85 | 8 | Long | Match time, ms since 1970-01-01 GMT |
| LeavesAmt | 93 | 8 | Long | Amount open for further execution; 0 if ExecType is not 1 |
| SecurityID | 101 | 9 | Alpha | CUSIP |
| MaturityDate | 110 | 8 | Long | Maturity date; 0 if absent |
| IssueDate | 118 | 8 | Long | Issue date; 0 if absent |
| CouponRate | 126 | 8 | Rate | Coupon rate, 9 implied decimals; 0 if absent |
| PriceType | 134 | 1 | Alpha | 1 price (percent of par), 2 discount, 3 yield |
| AggressorFlag | 135 | 1 | Alpha | 1 client is aggressor, 2 client is not aggressor |
| RoutedUserKey | 136 | 4 | Integer | Routed user (OBO) |

#### PendingFillSpreadTrade (type `f`, 269 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 8 | 1 | char | PendingFillSpreadTrade: Spread PendingFillTrade, the trade after a PendingFill on a spread (UST OUCH 10.16.4, pp.28-30) |
| ClOrderID | 9 | 4 | Integer | Client assigned id for the order |
| OrderID | 13 | 8 | Long | Currenex assigned order id |
| TradeLinkID | 21 | 4 | Integer | Links pending fill, fill cancel and trade; unique within the order only |
| InstrumentIndex | 25 | 2 | Short | From Instrument Info |
| Side | 27 | 1 | Alpha | B buy, S sell |
| FillAmt | 28 | 8 | Long | Trade amount (spreads: contracts) |
| FillRate | 36 | 8 | Rate | Done trade rate, 9 implied decimals |
| ExecBroker | 44 | 4 | Alpha | Execution broker's code, or NA |
| ExecutionID | 48 | 20 | Alphanumeric | Trade ID |
| ExecType | 68 | 1 | Alpha | 1 new trade |
| TradeDate | 69 | 8 | Long | Trade date, ms since 1970-01-01 GMT |
| TransactTime | 77 | 8 | Long | Match time, ms since 1970-01-01 GMT |
| LeavesAmt | 85 | 8 | Long | Amount open for further execution; 0 if ExecType is not 1 |
| PriceType | 93 | 1 | Alpha | 4 rate differential (basis points) |
| AggressorFlag | 94 | 1 | Alpha | 1 client is aggressor, 2 client is not aggressor |
| AnchorLegInstrumentIndex | 95 | 2 | Short | Instrument index of the anchor leg |
| AnchorLegSecurityID | 97 | 9 | Alpha | CUSIP of the anchor leg trade |
| AnchorLegMaturityDate | 106 | 8 | Long | Maturity date; 0 if absent |
| AnchorLegIssueDate | 114 | 8 | Long | Issue date; 0 if absent |
| AnchorLegCouponRate | 122 | 8 | Rate | Coupon rate; 0 if absent |
| AnchorLegSide | 130 | 1 | Alpha | B buy, S sell |
| AnchorLegFillRate | 131 | 8 | Rate | Done trade rate of the anchor leg |
| AnchorLegFillAmt | 139 | 8 | Long | Trade amount of the anchor leg |
| AnchorLegYield | 147 | 8 | Rate | Yield to maturity of the anchor leg trade |
| AnchorLegPriceType | 155 | 1 | Alpha | 1 price (percent of par), 2 discount, 3 yield |
| AnchorLegSettlementDate | 156 | 8 | Date | Settlement date of the anchor leg, ms since 1970-01-01 GMT |
| AnchorLegExecutionID | 164 | 20 | Alphanumeric | Trade ID of the anchor leg |
| CalculatedLegInstrumentIndex | 184 | 2 | Short | Instrument index of the calculated leg |
| CalculatedLegSecurityID | 186 | 9 | Alpha | CUSIP of the calculated leg trade |
| CalculatedLegMaturityDate | 195 | 8 | Long | Maturity date; 0 if absent |
| CalculatedLegIssueDate | 203 | 8 | Long | Issue date; 0 if absent |
| CalculatedLegCouponRate | 211 | 8 | Rate | Coupon rate; 0 if absent |
| CalculatedLegSide | 219 | 1 | Alpha | B buy, S sell |
| CalculatedLegFillRate | 220 | 8 | Rate | Done trade rate of the calculated leg |
| CalculatedLegFillAmt | 228 | 8 | Long | Trade amount of the calculated leg |
| CalculatedLegYield | 236 | 8 | Rate | Yield to maturity of the calculated leg trade |
| CalculatedLegPriceType | 244 | 1 | Alpha | 1 price (percent of par), 2 discount, 3 yield |
| CalculatedLegSettlementDate | 245 | 8 | Date | Settlement date of the calculated leg, ms since 1970-01-01 GMT |
| CalculatedLegExecutionID | 253 | 20 | Alphanumeric | Trade ID of the calculated leg |
| RoutedUserKey | 273 | 4 | Integer | Routed user (OBO) |

#### Trade (type `T`, 128 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 8 | 1 | char | Trade: OutrightTrade, without the pending fill workflow (UST OUCH 10.17.1, pp.31-32) |
| ClOrderID | 9 | 4 | Integer | Client assigned id for the order |
| OrderID | 13 | 8 | Long | Currenex assigned order id |
| InstrumentIndex | 21 | 2 | Short | From Instrument Info |
| Side | 23 | 1 | Alpha | B buy, S sell |
| FillAmt | 24 | 8 | Long | Trade amount, 2 implied decimals |
| FillRate | 32 | 8 | Rate | Done trade rate, 9 implied decimals |
| ExecBroker | 40 | 4 | Alpha | Execution broker's code, or NA |
| ExecutionID | 44 | 20 | Alphanumeric | Trade ID |
| ExecType | 64 | 1 | Alpha | 1 new trade |
| SettleDate | 65 | 8 | Long | Settlement date, ms since 1970-01-01 GMT |
| TradeDate | 73 | 8 | Long | Trade date, ms since 1970-01-01 GMT; after the roll the next business date |
| TransactTime | 81 | 8 | Long | Match time, ms since 1970-01-01 GMT |
| LeavesAmt | 89 | 8 | Long | Amount open for further execution; 0 if ExecType is not 1 |
| SecurityID | 97 | 9 | Alpha | CUSIP |
| MaturityDate | 106 | 8 | Long | Maturity date; 0 if absent |
| IssueDate | 114 | 8 | Long | Issue date; 0 if absent |
| CouponRate | 122 | 8 | Rate | Coupon rate, 9 implied decimals; 0 if absent |
| PriceType | 130 | 1 | Alpha | 1 price (percent of par), 2 discount, 3 yield |
| AggressorFlag | 131 | 1 | Alpha | 1 client is aggressor, 2 client is not aggressor |
| RoutedUserKey | 132 | 4 | Integer | Routed user (OBO) |

#### SpreadTrade (type `F`, 265 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 8 | 1 | char | SpreadTrade: trade on a spread, without the pending fill workflow (UST OUCH 10.17.2, pp.32-34) |
| ClOrderID | 9 | 4 | Integer | Client assigned id for the order |
| OrderID | 13 | 8 | Long | Currenex assigned order id |
| InstrumentIndex | 21 | 2 | Short | From Instrument Info |
| Side | 23 | 1 | Alpha | B buy, S sell |
| FillAmt | 24 | 8 | Long | Trade amount (spreads: contracts) |
| FillRate | 32 | 8 | Rate | Done trade rate, 9 implied decimals |
| ExecBroker | 40 | 4 | Alpha | Execution broker's code, or NA |
| ExecutionID | 44 | 20 | Alphanumeric | Trade ID |
| ExecType | 64 | 1 | Alpha | 1 new trade |
| TradeDate | 65 | 8 | Long | Trade date, ms since 1970-01-01 GMT |
| TransactTime | 73 | 8 | Long | Match time, ms since 1970-01-01 GMT |
| LeavesAmt | 81 | 8 | Long | Amount open for further execution; 0 if ExecType is not 1 |
| PriceType | 89 | 1 | Alpha | 4 rate differential (basis points) |
| AggressorFlag | 90 | 1 | Alpha | 1 client is aggressor, 2 client is not aggressor |
| AnchorLegInstrumentIndex | 91 | 2 | Short | Instrument index of the anchor leg |
| AnchorLegSecurityID | 93 | 9 | Alpha | CUSIP of the anchor leg trade |
| AnchorLegMaturityDate | 102 | 8 | Long | Maturity date; 0 if absent |
| AnchorLegIssueDate | 110 | 8 | Long | Issue date; 0 if absent |
| AnchorLegCouponRate | 118 | 8 | Rate | Coupon rate; 0 if absent |
| AnchorLegSide | 126 | 1 | Alpha | B buy, S sell |
| AnchorLegFillRate | 127 | 8 | Rate | Done trade rate of the anchor leg |
| AnchorLegFillAmt | 135 | 8 | Long | Trade amount of the anchor leg |
| AnchorLegYield | 143 | 8 | Rate | Yield to maturity of the anchor leg trade |
| AnchorLegPriceType | 151 | 1 | Alpha | 1 price (percent of par), 2 discount, 3 yield |
| AnchorLegSettlementDate | 152 | 8 | Date | Settlement date of the anchor leg, ms since 1970-01-01 GMT |
| AnchorLegExecutionID | 160 | 20 | Alphanumeric | Trade ID of the anchor leg |
| CalculatedLegInstrumentIndex | 180 | 2 | Short | Instrument index of the calculated leg |
| CalculatedLegSecurityID | 182 | 9 | Alpha | CUSIP of the calculated leg trade |
| CalculatedLegMaturityDate | 191 | 8 | Long | Maturity date; 0 if absent |
| CalculatedLegIssueDate | 199 | 8 | Long | Issue date; 0 if absent |
| CalculatedLegCouponRate | 207 | 8 | Rate | Coupon rate; 0 if absent |
| CalculatedLegSide | 215 | 1 | Alpha | B buy, S sell |
| CalculatedLegFillRate | 216 | 8 | Rate | Done trade rate of the calculated leg |
| CalculatedLegFillAmt | 224 | 8 | Long | Trade amount of the calculated leg |
| CalculatedLegYield | 232 | 8 | Rate | Yield to maturity of the calculated leg trade |
| CalculatedLegPriceType | 240 | 1 | Alpha | 1 price (percent of par), 2 discount, 3 yield |
| CalculatedLegSettlementDate | 241 | 8 | Date | Settlement date of the calculated leg, ms since 1970-01-01 GMT |
| CalculatedLegExecutionID | 249 | 20 | Alphanumeric | Trade ID of the calculated leg |
| RoutedUserKey | 269 | 4 | Integer | Routed user (OBO) |

#### GapFill (type `4`, 6 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 8 | 1 | char | GapFill: sent in place of messages not resent (UST OUCH 10.19, p.34; 5.5.1, p.9) |
| NewSeqNo | 9 | 4 | Integer | Sequence number of the next message to be transmitted |
| ReasonCode | 13 | 1 | Alpha | 1 NOT_RESENDABLE (Logon, Logout, Heartbeat), 2 NOT_AVAILABLE |

## 5. Logon and session

- **Logon** (5.1, p.8; 10.1, p.18): the client sends Logon (`a`) with UserID
  and Password (20 characters each, space padded), SessionID 0 ("only used by
  Currenex in response to Logon requests") and Version 200. Currenex answers
  with a Logon carrying the SessionID. The client's Logon is always sequence
  number 1 and each later client message adds one (5.1.1); a wrong number
  closes the session with Logout A10.
- **Refusal**: no Logon reject message is defined. A refused logon is taken
  to be answered with a Logout (A5 authentication failure, A11 invalid
  version, Appendix B) in place of the Logon response (section 10, A12).
- **SessionID** goes in Heartbeat, InstrumentInfoRequest,
  RoutingUserInfoRequest and Logout; order messages carry RoutedUserKey
  instead (7.1, p.13).
- **After Logon**: the client must send InstrumentInfoRequest (6, p.12) and
  may send RoutingUserInfoRequest (7.1). Neither answer has an end marker.
- **Credentials and account**: user and password only; on-behalf-of trading
  selects the booking entity per order with RoutedUserKey (7.1). The OM reads
  credentials from a file outside the repository (`credentials` key).
- **Sequence numbers**: directional (header, 9). Client to Currenex restarts
  at 1 on every connection (5.1.1). Currenex to client: with persistence (a
  per-user option Currenex configures) the session lives for the trading
  week across connections and numbering continues (5.5, p.9); without it,
  every session starts at 1 (FX OUCH 11.3 FAQ 5). Weekly reset at 7 pm ET
  (Persistence addendum 2.1).
- **Heartbeats**: section 2. The OM's session answers each Currenex
  Heartbeat and declares the link down after `receive_timeout_ms` (10 s by
  default) without receiving anything. It sends no Heartbeat of its own
  accord.
- **Logout** (5.2): either side may send it; the recipient answers with a
  Logout. The OM sends reason A6 *User Logout* on shutdown and does not wait
  for the answer.
- **Trading session status**: there is no session or instrument status
  message. Error 0x1C *Trading Disabled Off Hours* and 0x1B *Application Not
  Available* are the only signals (Appendix A).
- **Cancel on disconnect**: on by default for every OUCH id; active orders
  are cancelled on logout or disconnect; Currenex turns it off on request
  (8.7, p.16). A match just before the disconnect can still fill.

## 6. Order identifiers

- **ClOrderID** (Integer, signed 32 bits) is assigned by the client for each
  new order (10.9) and **each cancel or replace takes a new one**
  (NewClOrderID) and names the order by its current id (PrevClOrderID or
  OrigClOrderID) (10.11, 10.13). After a successful replace the order lives
  under the replace's id (FX-style chain).
- Uniqueness scope: **not stated**. Error 0x0A *Invalid Client Order Id*
  exists. The OM takes ids from the clock as milliseconds since the start of
  the UTC week (Saturday 00:00) and keeps them strictly increasing: a whole
  week of milliseconds (604,800,000) fits a positive Integer, and a restart
  later in the week starts above anything issued before as long as the
  previous process averaged at most one id per millisecond (section 10, A8).
- **OrderID** (Long) is Currenex's, from NewOrderAck; -1 on reject. It is
  the `xordid` of the SOM Ack.
- **Which id each report carries**: NewOrderAck: ClOrderID of the order.
  OrderCancelReject: both NewClOrderID (the refused request) and
  PrevClOrderID. OrderReplaceAck: both. OrderCancelledOrExpired, PendingFill,
  PendingFillCancel and trades: one ClOrderID, which after a cancel request
  may be the order's or the request's (section 10, A9). The OM maps every id
  it has used to its order, so either resolves.
- **InstrumentIndex** (Short) is valid for one session only (6, p.12): the
  OM maps CUSIP (SecurityID of Outright Instrument Info) to index after each
  Logon, and a cancel uses the current session's index for the order's
  CUSIP.

## 7. New order: fields and supported combinations

| Field | Values (UST OUCH 10.9, pp.21-22) | What the OM sends |
|---|---|---|
| OrderType | F limit, Z iceberg | F. Market, stop and stop-limit orders do not exist on OUCH and are refused locally. Iceberg is not offered (the SOM has no display size). |
| InstrumentIndex | from Instrument Info | the index of the order's CUSIP (`Order::cusip`, else the asset's `cusip`); refused if the CUSIP is not listed in this session or is listed under two indexes |
| Side | B, S | B for a buy, S for any sell. Treasuries have no short-sale marking, so `short_sale` is not refused. |
| OrderAmt | notional, 2 decimals | SOM size × `face_per_unit` (1,000,000 dollars by default) × 100; sizes must be whole units |
| MinAmt | 0 allowed | `min_amt` from the config (0 by default), capped at OrderAmt |
| Price | Rate, 9 decimals; percent of par for outrights | SOM ticks × tick size × 10^9, rounded; must be positive |
| ShowAmt | iceberg only; ignored for limit | 0 |
| ExpireType | G good till cancel, I immediate or cancel | IOC → I; GTC → G; **DAY → G** (OUCH has no day order; cancel on disconnect ends it at logout, section 10, A10). FOK, at the open, at the close and extended hours are refused locally. |
| RoutedUserKey | 0 or -1 not routed; else the routed user's key | `routed_user_key` (-1), or the key looked up for `routed_user_id` |

Refused locally besides: broker algorithms (`strategy`), routes (`route`),
cancel/replace from the SOM (`oid_to_cancel`), sizes that are not whole
units, and the configured limits. Spread instruments (Spread Instrument
Info `e`; amounts in contracts, prices as rate differentials, 4.2.2.2) are
not traded.

## 8. Reports, throttles, reconnect and risk checks

### 8.1 Reports and what they become

| Currenex message | Book call | SOM message |
|---|---|---|
| NewOrderAck `M`, Status C | `on_accepted(cid, OrderID)` | `Ack(oid, OrderID)`, once per order |
| NewOrderAck `M`, Status R | `on_rejected(cid)` | `Reject(oid, reason)`: 0x0C → THROTTLE; 0x0B, 0x10, 0x1A → EXCHLIMIT; others UNKNOWN; text from Appendix A in the log |
| Trade `T` (no pending fill workflow), PendingFillTrade `t` (with it) | `on_fill(cid, whole units)` | `Fill`: size FillAmt / unit (may be fractional), price FillRate as a decimal, leaves from the OM's own amounts, time TransactTime. Duplicates by ExecutionID are skipped. LeavesAmt 0 with amount still unfilled and no pending fill outstanding ends the order (CancAck). |
| PendingFill `U` | none | none: logged; the TradeLinkID is held until its trade or cancel (8.5.1) |
| PendingFillCancel `V` | none | none: logged; the order can match again (8.5.2) |
| OrderCancelledOrExpired `R` (any Status, any Type) | `on_canceled(cid)` | `CancAck(oid, units filled)`, unless the order is already done (logged) |
| OrderCancelReject `O` | `on_cancel_rejected(request cid)` | `CancReject(oid, FROMEXCHANGE)` (0x19 *Cannot Cancel Already Filled* → FILLEDALREADY); `Late` when the order already filled: logged only |
| OrderReplaceAck `Q` | none | none: the OM sends no replaces; logged as an error |
| SpreadTrade `F`, PendingFillSpreadTrade `f` | none | none: the OM places no spread orders; logged as an error to check positions |
| Outright Instrument Info `D` | - | CUSIP → InstrumentIndex for this session |
| Spread Instrument Info `e` | - | logged |
| RoutingUserInfo `g` | - | the RoutedUserKey of `routed_user_id`; the OM is ready once it is known |
| Heartbeat `C` | - | answered by the session; counted in the sequence |
| GapFill `4` | - | moves the expected sequence number to NewSeqNo; NOT_AVAILABLE (2) is logged as an error |

Races: a fill before the ack is applied and the ack still sent; a fill
racing a cancel counts and the cancel's reject comes back `Late`; a fill
after the cancel acknowledgement counts (8.7: a match just before a cancel
can still fill). Busts and corrections: OUCH defines none. A report the OM
cannot match is logged as an error, a fill on an unknown order with
"CHECK POSITIONS".

### 8.2 Throttles and busy states

No message-rate limits are published. The TCP session queues requests and
returns Busy above `max_queued_bytes`; the OM then rejects orders with
THROTTLE and refuses cancels until the queue drains to half. A refused new
order is never retried. `max_orders_per_sec` is a local token bucket.

### 8.3 Reconnect and replay

- The OM keeps the next Currenex sequence number it expects across
  connections. On a Logon response numbered N:
  - first Logon of the process: expect N+1 (or ask for a replay from
    `first_resend_from` if set);
  - N equal to the expected number: nothing was missed;
  - N greater, persistence configured: send ResendRequest(BeginSeqNo =
    expected) and apply the replay (5.5.1, 5.5.3). GapFills cover Logon,
    Logout and Heartbeat messages (NOT_RESENDABLE) and lost ones
    (NOT_AVAILABLE). The Logon response's own number is skipped when the
    replay reaches it. A live message during the replay is applied at
    once and skipped when the replay catches up.
  - N greater, no persistence: the missed messages are lost; logged.
  - N smaller: numbering restarted (no persistence, or the weekly reset).
    Orders that were live are reported cancelled (cancel on disconnect)
    with an error to reconcile through the FIX STP drop copy, which 8.7
    recommends; with `cancel_on_disconnect = false` they are left as they
    were and logged.
- Reports already applied are skipped by sequence number; trades also by
  ExecutionID within the order.
- Instrument indexes and the instrument map are rebuilt after each Logon.
- Logout reasons A1 (replaced by a new session), A3, A5 and A11 stop the
  OM (unrouted until restart); other reasons and disconnects reconnect
  after `reconnect_ms`.

### 8.4 Risk checks

Currenex enforces credit (0x0B), absolute trading limits (0x1A) and active
order counts (0x10); values are per account and not published. The OM
enforces `max_order_face` (dollars of face per order) and
`max_orders_per_sec` before sending; the SOM's own limits apply before
that.

## 9. Pitfalls

One line per item of `pitfalls.md`.

### 9.1 Reading the specification

- Several documents: UST OUCH 2.0 (version 200) applies; the Firm LP
  document (version 300) and the FX OUCH document differ in layouts (section 1).
- Errata and change tables: the revision history has one entry (document
  created). The FX persistence addendum adds the weekly reset.
- Tables across page breaks: Spread InstrumentInfo, PendingFill,
  PendingFillCancel, both trade messages and both spread trades continue
  across pages; `fieldgen.py --check` found no missing row (offsets
  contiguous) and the tables were re-read against `pdftotext -layout`.
- Offsets 0-based from the first header byte; no length field.
- Reserved fields: none.
- Enumerations in another section: error codes (Appendix A) and logout codes
  (Appendix B).
- Names that differ: OrderReplaceAck's first field is printed *NewClOrderD*
  (10.14); taken as NewClOrderID. Section titles and table titles differ
  (CancelOrder (4E) / OrderCancelRequest; OrderCancelledorExpired (52) /
  Unsolicited Cancel); the table uses one name per message.
- Examples that contradict tables: the 5.5.3 figure shows Logon as type 41
  (`A`, the FX type) where 10.1 says `a` (0x61); the header example says
  "41hex Achar"; IssueDate examples (`3c00483e00000001`) are not dates; the
  UserID example contains `0202020`; the InstrumentTenor example `32591f`
  has a stray `1f`. Tables were followed (section 10).
- Two layers in one table: session messages (Logon, Logout, Heartbeat,
  ResendRequest, GapFill) and order messages share one header, one
  numbering and one type-code space with no reuse within a direction, so
  they are one layer per direction here, not two sections.
- Repeated names: Outright and Spread Instrument Info, Outright and Spread
  trades, pending and non-pending: each has a distinct name in the table.
- Variable-length trailing fields: none.
- Integers of odd width: none; all 2, 4 or 8 bytes.

### 9.2 Framing and encoding

- Endianness: big-endian throughout.
- Signed: all integers signed (4.2.1); RoutedUserKey uses -1.
- Fixed point: amounts 2 decimals, rates 9 decimals, fixed by the spec, not
  per instrument; spread amounts are whole contracts (4.2.2.2).
- Sentinels: OrderID -1 on reject; RoutedUserKey 0 or -1; dates 0 when
  absent; ExecBroker "NA".
- Strings: left-justified, space padded.
- Several messages per write (Figure 1); a message split across reads is
  handled by the deframer (tested).
- Timestamps: header ms past midnight GMT; trade times ms since epoch UTC.

### 9.3 Sequencing and recovery

- Scope: per direction per session; Currenex's continues across
  connections with persistence, for the week.
- A/B arbitration: not applicable.
- Gap rule: Logon response number versus the last applied (8.3).
- Recovery: ResendRequest with a begin number only; the end is implied by
  the Logon response (section 10, A4).
- Snapshot alignment: no snapshots.
- State cleared on a new session: instrument map and readiness; the
  order maps are kept (orders outlive connections with persistence).
- Recovery paths exercised in tests: replay with GapFills, over-replay,
  duplicate ExecutionID, numbering restart, NOT_AVAILABLE GapFill.
- Session behaviour: logon, heartbeats, logout reasons, cancel on
  disconnect: section 5.

### 9.4 Book semantics

Not applicable: order entry. AggressorFlag is logged only.

### 9.5 Prices, quantities and instruments

- Price or yield: outrights trade in price, percent of par (4.2.3); trade
  reports carry PriceType 1 price, 2 discount, 3 yield, and spreads 4 rate
  differential. The OM sends and reads outright prices only.
- Quantity multipliers: amounts are notional dollars; `face_per_unit` maps
  them to SOM units; trades below one unit are passed as fractions.
- Decimals sentinels: none.
- Ticks: not in Outright Instrument Info; the spread tick is (0.05, WI
  rolls 0.125). Rate precision errors come back as 0x11.
- Instrument definitions: Instrument Info after every Logon, indexes per
  session; mapped by CUSIP.
- Lifecycle: InstrumentState WI, OTR, OLD, OLDn. A CUSIP listed twice is
  refused (section 10, A11).

### 9.6 Behaviour the specification omits

- Subscription options: pending fill workflow is per user, configured by
  Currenex (8.5); the OM handles both.
- History on subscribe: replay only on ResendRequest.
- End of Instrument Info and RoutingUserInfo lists: no marker (A13).
- Message order within an event: PendingFill before its trade (Figure 5).
- Idle timeouts: two missed heartbeat answers close the session.

### 9.7 Post-trade

Not applicable. 8.7 recommends the FIX STP service for guaranteed trade
delivery; the OM does not connect to it.

### 9.8 Order entry

- Client ids: scope unstated; clock-based weekly ids (section 6).
- Cancel and replace take new ids; every id maps back to the order.
- Fill before ack, racing a cancel, after the cancel ack: each counted once
  (tests).
- Unsolicited cancels: R with Type 1 system, 2 below minimum, Status E
  expired; reach the SOM.
- Busts and corrections: none in the protocol.
- Busy connection: section 8.2.
- Reconnect: section 8.3.
- Orders placed outside the OM: reports logged as errors.
- Short sales: no marking for Treasuries.

## 10. Ambiguities

| # | Question | Reading taken | Alternatives | Resolved by |
|---|---|---|---|---|
| A1 | Logon type: `a` (0x61) in 10.1 and the section title, `41` in the 5.5.3 figure and the header example | `a`; the `41`s are left over from the FX document, whose Logon is `A` | `A` | spec tables; FX document comparison |
| A2 | Are client messages also framed with SOH/ETX? Section 4.1 does not say "both directions" | yes, both directions | client messages unframed | reading of 4.1 (no direction given) |
| A3 | Logon response sequence number: does it consume a number in Currenex's outbound stream? | yes: in 5.5.3, after 7 messages the response is 8 and the replay ends at 7 | it repeats the last number; it is the next number to come | 5.5.3 figure |
| A4 | Where does a replay end? ResendRequest has no end number | at the Logon response's number less one; new events queue until the replay is complete (5.5.2) | Currenex sends an end marker (none is defined) | 5.5.2, 5.5.3 |
| A5 | Does the client's sequence number count Heartbeat answers? | yes: every client message after Logon adds one (5.1.1; 5.5.3 shows Heartbeat 2) | only order messages | 5.1.1, 5.5.3 figure |
| A6 | Are Currenex Heartbeats numbered in its outbound stream? | yes (5.5.3: Heartbeat 2; GapFill covers them) | unnumbered | 5.5.3, 5.5.1 |
| A7 | Weekly reset: the 7 pm ET reset is stated in the FX persistence addendum only | applies to UST too; the OM treats any lower Logon number as a restart | UST sessions never reset | FX addendum 2.1 |
| A8 | ClOrderID uniqueness: per session, day, week or forever? | per trading week (the persistent session's life); ids are clock-based and increasing | per day; forever | not stated; ask Currenex |
| A9 | Which ClOrderID does OrderCancelledOrExpired carry after a cancel request: the order's or the request's? | either is resolved; the simulator sends the request's | - | FX conformance test plan (cancel test: request 12 for order 11, cancel acknowledgement shows 12) |
| A10 | Day orders: OUCH has only GTC and IOC | DAY sent as GTC; cancel on disconnect ends it at logout | refuse DAY | not stated |
| A11 | Can one CUSIP appear in several Instrument Info messages (settlement dates, WI and OTR)? | if it does, orders on it are refused as ambiguous | first one wins; choose by InstrumentID | not stated |
| A12 | Logon refusal: there is no Logon reject message | a Logout in place of the Logon response, reason A5 or A11 | the connection is simply closed (also handled: reconnect) | Appendix B codes |
| A13 | End of the Instrument Info list | none needed: an order is refused only while its CUSIP is unknown | wait a fixed time after the request | not stated |
| A14 | 5.4 says Currenex responds with "a Reject message" to badly formatted messages; none is defined | unknown types are logged and counted | a NewOrderAck R or OrderCancelReject is that reject | not stated |
| A15 | LeavesAmt: "Order amount opened for further execution" while pending fills are outstanding | LeavesAmt 0 ends the order only when no pending fill is unsettled | LeavesAmt excludes or includes pending amounts | not stated |
| A16 | Spread PendingFillTrade and SpreadTrade have no SettleDate after ExecType, unlike their outright counterparts | as printed: no SettleDate; offsets are contiguous so a missing row would show | a SettleDate dropped from the document | offsets contiguous, `--check` |
| A17 | PendingFill and PendingFillCancel type FillRate as Long, trades as Rate | same encoding either way (signed 8 bytes, 9 decimals) | - | 4.2.3 |
| A18 | Spread Instrument Info leg InstrumentIDs are 10 characters and leg tenors 5, against 20 and 10 for the spread itself | as printed | - | offsets contiguous |
| A19 | Persistence: is it on for this user? | a config flag; with it off, a higher Logon number is logged as lost messages | detect from the Logon number | 5.5 ("contact your account manager") |
| A20 | RoutedUserKey when not routing: 0 or -1 | -1, which the spec calls the default | 0 | 7.1, 10.9 |
| A21 | Does the client send Heartbeats of its own when idle? | no: it answers Currenex's | send every 3 s | 5.3 (only answers described) |

## Decisions

None by the owner yet. This description was produced blind, without the
review step of SKILL.md (step 4), as the task asked.
