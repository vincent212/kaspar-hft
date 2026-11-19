# Feed description: LMAX Exchange Broker Trading (FIX 4.4), order entry

Not certified. Citations use the short names below. LMAX's FIX specifications
are JSON documents without page numbers, cited by prologue heading, message and
field; the PDFs published beside them were placeholders when fetched.

## 1. Venue and documents

| Short name | Document | Version | Date | Applies to |
|---|---|---|---|---|
| TR | Broker Trading `fix-spec.json`, https://docs.lmax.com/fix-api/broker-trading/fix/latest/fix-spec.json | FIX.4.4 version (a FIX.4.2 version in the same file covers MassQuote, QuoteCancel, SecurityDefinitionRequest and SecurityStatusRequest; not used); latest changelog entry release 370 | 2023-12-02 | everything below |
| CG | LMAX connectivity guide, https://www.lmax.com/connectivity-guide | web page | fetched 2026-11-18 | demo host and port ("FIAT Demo") |
| CO | LMAX Exchange Connectivity Overview | 8 pages | 21 January 2019 | p. 8, demo hosts |
| CS | Contract Specifications Spot FX | 4 pages | 2023-07-03 | contract size, minimum, tick |
| PD | demo Public Data REST API `/v1/instruments` | live | fetched 2026-11-18 | SecurityIDs |
| DC | MTF Drop Copy `fix-spec.json` | 2.7 | 2023-03-13 | evidence only (MTF service) |

The market-data description (`../../market_data/lmax/feed.md`) holds the
session-level details the two gateways share.

## 2. Transport

| Item | Value | Source |
|---|---|---|
| Demo host | `fix-order.london-demo.lmax.com:443`, TLS (CO p. 8 labels it FIX 4.2; TR defines FIX.4.4 for these messages) | CG "FIAT Demo"; CO p. 8 |
| UAT (broker) | `fix-order.london-uat.lmax.com:443` | CG |
| Production | refused by the environment guard | |
| Schedule | open and close 17:02:00 America/New_York daily | TR prologue, Fix Session Connectivity Schedule |
| Message size | 8192 bytes at most; larger inbound messages log the client off | TR prologue, Messages sizes |
| Credentials | Username (553), Password (554) on the Logon | TR Logon |
| Sequence numbers | persisted through the week; reset enforced Saturday 17:02:00 New York; 141=Y resets | TR prologue, Session Resetting |
| FIX resend | the most recent 512 messages; LMAX ignores what it is resent ("to avoid out of date orders being placed into the market"); no session failover | TR prologue, FIX Session Recovery |
| Application recovery | OrderStatusRequest (H) and TradeCaptureReportRequest (AD) | TR prologue, Application Recovery |
| Cancel on disconnect | default for FIX orders; ExecInst (18) H keeps the order; users trading several accounts must always send H | TR prologue, Cancel on Disconnect |
| Unknown tags | accept and ignore | TR prologue, Compatibility |

## 3. Framing

FIX 4.4 tag=value over TLS, through QuickFIX. The dictionary
`dict/LMAX-FIX44-Trading.xml` is generated from TR's FIX.4.4 version by
`../../market_data/lmax/tools/fixspec2qf.py`. Requests are built in TR's field
order (QuickFIX would otherwise sort body fields by tag).

## 4. Message catalogue

Rendered from TR by `fixspec2qf.py --markdown`. Session messages are standard
FIX 4.4.

### Logon (35=A, bidirectional)

Message used to establish a connection between a client and the LMAX FIX Gateway.

| Tag | Field | Req | Type | Values | LMAX description |
|---|---|---|---|---|---|
| 98 | EncryptMethod | Y | INT | 0=NoneOther | Method of encryption. LMAX does not support encryption at the FIX layer. All messages are sent over an SSL encrypted connection. |
| 108 | HeartBtInt | Y | INT |  | The maximum time to wait before a Heartbeat message is sent in the absence of other activity on the connection. The same value is used by both parties. |
| 141 | ResetSeqNumFlag | N | BOOLEAN | N=No, Y=Yes | Indicates that the both sides of the FIX session should reset sequence numbers. |
| 383 | MaxMessageSize | N | LENGTH |  | Maximum number of bytes supported for a single message. |
| 553 | Username | N | STRING |  | LMAX username. This is required on the login message sent by the client |
| 554 | Password | N | STRING |  | LMAX Password or passphrase. This is required on the login message sent by the client |
| 999 | TestPing | N | BOOLEAN |  |  |

### NewOrderSingle (35=D, incoming)

Message sent by the client to place a new order with LMAX.

| Tag | Field | Req | Type | Values | LMAX description |
|---|---|---|---|---|---|
| 11 | ClOrdID | Y | STRING |  | The client supplied identifier for the order. ClOrdId is restricted to a maximum of 20 arbitrary ASCII characters. Must be unique for all currently working o... |
| 100 | ExDestination | N | EXCHANGE |  | Execution destination as defined by LMAX when order is entered. |
| 48 | SecurityID | N | STRING |  | LMAX Security identifier. Requires SecurityIDSource=8. |
| 22 | SecurityIDSource | N | STRING | 8=ExchSymb | Identifies class or source of the SecurityID (48) value. Required if SecurityID is specified. |
| 55 | Symbol | N | STRING |  | Ticker symbol |
| 1 | Account | N | STRING |  | The LMAX Account ID. If not specified then the user's default account will be used. |
| 54 | Side | Y | CHAR | 1=Buy, 2=Sell, 7=Undisc | Side of the order. |
| 18 | ExecInst | N | MULTIPLE_STRING_VALUES |  | Instructions for order handling on exchange trading floor. If more than one instruction is applicable to an order, this field can contain multiple instructio... |
| 60 | TransactTime | Y | UTC_TIMESTAMP |  | Time of execution/order creation (expressed in UTC (Universal Time Coordinated, also known as “GMT”) |
| 38 | OrderQty | Y | QUANTITY |  | Quantity of contracts ordered. |
| 40 | OrdType | Y | CHAR | 1=Market, 2=Limit, 3=Stop, 4=StopLimit | Order type. |
| 44 | Price | N | PRICE |  | Price per unit of quantity |
| 99 | StopPx | N | PRICE |  | Price per unit of quantity (e.g. per share) |
| 59 | TimeInForce | N | CHAR | 0=Day, 1=GoodTillCancel, 3=ImmediateOrCancel, 4=FillOrKill | Specifies how long the order remains in effect. |
| 1089 | MatchIncrement | N | QUANTITY |  | Allows orders to specify a minimum quantity that applies to every execution |

### OrderCancelRequest (35=F, incoming)

Message sent by the client to cancel an existing order which is working on an LMAX order book.

| Tag | Field | Req | Type | Values | LMAX description |
|---|---|---|---|---|---|
| 1 | Account | N | STRING |  | The LMAX Account ID. If not specified then the user's default account will be used. |
| 41 | OrigClOrdID | Y | STRING |  | ClOrdID of the previous order. Used to identify the previous order in cancel and cancel/replace requests. |
| 11 | ClOrdID | Y | STRING |  | The client supplied identifier for the order. ClOrdId is restricted to a maximum of 20 arbitrary ASCII characters. Must be unique for all currently working o... |
| 48 | SecurityID | N | STRING |  | LMAX Security identifier. Requires SecurityIDSource=8. |
| 22 | SecurityIDSource | N | STRING | 8=ExchSymb | Identifies class or source of the SecurityID (48) value. Required if SecurityID is specified. |
| 55 | Symbol | N | STRING |  | Ticker symbol |
| 54 | Side | N | CHAR | 1=Buy, 2=Sell, 7=Undisc | Side of the order. |
| 60 | TransactTime | Y | UTC_TIMESTAMP |  | Time of execution/order creation (expressed in UTC (Universal Time Coordinated, also known as “GMT”) |
| 38 | OrderQty | Y | QUANTITY |  | Quantity of contracts ordered. |

### OrderCancelReplaceRequest (35=G, incoming)

Message sent by the client to cancel an existing order which is working on an LMAX order book and replace it with another.

| Tag | Field | Req | Type | Values | LMAX description |
|---|---|---|---|---|---|
| 41 | OrigClOrdID | Y | STRING |  | ClOrdID of the previous order. Used to identify the previous order in cancel and cancel/replace requests. |
| 1 | Account | N | STRING |  | The LMAX Account ID. If not specified then the user's default account will be used. |
| 11 | ClOrdID | Y | STRING |  | The client supplied identifier for the order. ClOrdId is restricted to a maximum of 20 arbitrary ASCII characters. Must be unique for all currently working o... |
| 48 | SecurityID | N | STRING |  | LMAX Security identifier. Requires SecurityIDSource=8. |
| 22 | SecurityIDSource | N | STRING | 8=ExchSymb | Identifies class or source of the SecurityID (48) value. Required if SecurityID is specified. |
| 55 | Symbol | N | STRING |  | Ticker symbol |
| 54 | Side | Y | CHAR | 1=Buy, 2=Sell, 7=Undisc | Side of the order. |
| 60 | TransactTime | Y | UTC_TIMESTAMP |  | Time of execution/order creation (expressed in UTC (Universal Time Coordinated, also known as “GMT”) |
| 38 | OrderQty | Y | QUANTITY |  | Quantity of contracts ordered. |
| 18 | ExecInst | N | MULTIPLE_STRING_VALUES |  | Instructions for order handling on exchange trading floor. If more than one instruction is applicable to an order, this field can contain multiple instructio... |
| 40 | OrdType | Y | CHAR | 1=Market, 2=Limit, 3=Stop, 4=StopLimit | Order type. |
| 44 | Price | Y | PRICE |  | Price per unit of quantity |
| 59 | TimeInForce | N | CHAR | 0=Day, 1=GoodTillCancel, 3=ImmediateOrCancel, 4=FillOrKill | Specifies how long the order remains in effect. |

### OrderStatusRequest (35=H, incoming)

Message sent by the client to request the state of an order.

| Tag | Field | Req | Type | Values | LMAX description |
|---|---|---|---|---|---|
| 11 | ClOrdID | Y | STRING |  | The client supplied identifier for the order. ClOrdId is restricted to a maximum of 20 arbitrary ASCII characters. Must be unique for all currently working o... |
| 1 | Account | N | STRING |  | The LMAX Account ID. If not specified then the user's default account will be used. |
| 790 | OrdStatusReqID | N | STRING |  | Can be used to uniquely identify a specific Order Status Request message. |
| 48 | SecurityID | N | STRING |  | LMAX Security identifier. Requires SecurityIDSource=8. |
| 22 | SecurityIDSource | N | STRING | 8=ExchSymb | Identifies class or source of the SecurityID (48) value. Required if SecurityID is specified. |
| 55 | Symbol | N | STRING |  | Ticker symbol |
| 54 | Side | Y | CHAR | 1=Buy, 2=Sell, 7=Undisc | Side of the order. |

### ExecutionReport (35=8, outgoing)

Messsage sent by the LMAX FIX Gateway which reports the change in status of a client's Order

| Tag | Field | Req | Type | Values | LMAX description |
|---|---|---|---|---|---|
| 37 | OrderID | Y | STRING |  | Unique identifier for Order as assigned by LMAX. Unique over all time and orderbooks |
| 527 | SecondaryExecID | N | STRING |  | Execution ID as generated by the MTF. Currently, this is only populated for Executions reporting a Trade where this will be a 16 character string. For other ... |
| 11 | ClOrdID | N | STRING |  | The client supplied identifier for the order. ClOrdId is restricted to a maximum of 20 arbitrary ASCII characters. Must be unique for all currently working o... |
| 41 | OrigClOrdID | N | STRING |  | ClOrdID of the previous order. Used to identify the previous order in cancel and cancel/replace requests. |
| 790 | OrdStatusReqID | N | STRING |  | Can be used to uniquely identify a specific Order Status Request message. |
| 17 | ExecID | Y | STRING |  | Unique identifier of execution message as assigned by LMAX. Unique over all time. This is a 16 character ASCII string. |
| 150 | ExecType | Y | CHAR | 0=New, 3=Done, 4=Canceled, 5=Replaced, 6=PendingCxl, 7=Stopped, 8=Rejected, 9=Suspended, A=PendingNew, B=Calculated, C=Expired, D=Restated, E=PendingReplace, F=Trade, G=TradeCorrect, H=TradeCancel, I=OrderStatus | Describes the specific ExecutionRpt (i.e. Pending Cancel) while OrdStatus (39) will always identify the current order status (i.e. Partially Filled) |
| 39 | OrdStatus | Y | CHAR | 0=New, 1=Partial, 2=Filled, 3=Done, 4=Canceled, 6=Pending Cancel, 7=Stopped, 8=Rejected, 9=Suspended, A=PendingNew, B=Calculated, C=Expired, D=AcceptBidding, E=PendingRep | Identifies current status of order. |
| 103 | OrdRejReason | N | INT | 0=BrokerOpt, 1=UnknownSym, 10=InvInvID, 11=UnsuppOrderChar, 12=Surveillence, 13=IncorrectQuantity, 14=IncorrectAllocatedQuantity, 15=UnknownAccounts, 2=ExchClosed, 3=ExceedsLim, 4=TooLate, 5=Unknown, 6=Duplicate, 7=DuplicateVerbal, 8=Stale, 9=TradeAlongReq, 99=Other | Code to identify reason for order rejection. Note: Values 3, 4, and 5 will be used when rejecting an order due to pre-allocation information errors. |
| 1 | Account | N | STRING |  | The LMAX Account ID. If not specified then the user's default account will be used. |
| 12 | Commission | N | AMOUNT |  | Commission. |
| 13 | CommType | N | STRING | 3=absolute (total monetary amount) | Commission Type |
| 479 | CommissionCurrency | N | STRING |  | Commission currency |
| 48 | SecurityID | N | STRING |  | LMAX Security identifier. Requires SecurityIDSource=8. |
| 22 | SecurityIDSource | N | STRING | 8=ExchSymb | Identifies class or source of the SecurityID (48) value. Required if SecurityID is specified. |
| 55 | Symbol | N | STRING |  | Ticker symbol |
| 54 | Side | Y | CHAR | 1=Buy, 2=Sell, 7=Undisc | Side of the order. |
| 38 | OrderQty | Y | QUANTITY |  | Quantity of contracts ordered. |
| 40 | OrdType | N | CHAR | 1=Market, 2=Limit, 3=Stop, 4=StopLimit | Order type. |
| 44 | Price | N | PRICE |  | Price per unit of quantity |
| 99 | StopPx | N | PRICE |  | Price per unit of quantity (e.g. per share) |
| 59 | TimeInForce | N | CHAR | 0=Day, 1=GoodTillCancel, 3=ImmediateOrCancel, 4=FillOrKill | Specifies how long the order remains in effect. |
| 32 | LastQty | N | QUANTITY |  | Quantity of contracts bought/sold on this fill. |
| 31 | LastPx | N | PRICE |  | Price of this (last) fill. |
| 151 | LeavesQty | Y | QUANTITY |  | Quantity open for further execution. If the OrdStatus (39) is Canceled or Rejected (in which case the order is no longer active) then LeavesQty could be 0, o... |
| 14 | CumQty | Y | QUANTITY |  | Total quantity (e.g. number of shares) filled. |
| 6 | AvgPx | Y | PRICE |  | Calculated average price of all fills on this order. |
| 60 | TransactTime | N | UTC_TIMESTAMP |  | Time of execution/order creation (expressed in UTC (Universal Time Coordinated, also known as “GMT”) |
| 64 | SettlDate | N | LOCAL_DATE |  | Specific date of trade settlement (SettlementDate) in YYYYMMDD format. |
| 58 | Text | N | STRING |  | Free format text string (Note: this field does not have a specified maximum length) |

### OrderCancelReject (35=9, outgoing)

Message sent by the LMAX FIX Gateway when an OrderCancelRequest message cannot be successfully processed.

| Tag | Field | Req | Type | Values | LMAX description |
|---|---|---|---|---|---|
| 37 | OrderID | Y | STRING |  | Unique identifier for Order as assigned by LMAX. Unique over all time and orderbooks |
| 11 | ClOrdID | Y | STRING |  | The client supplied identifier for the order. ClOrdId is restricted to a maximum of 20 arbitrary ASCII characters. Must be unique for all currently working o... |
| 41 | OrigClOrdID | Y | STRING |  | ClOrdID of the previous order. Used to identify the previous order in cancel and cancel/replace requests. |
| 39 | OrdStatus | Y | CHAR | 0=New, 1=Partial, 2=Filled, 3=Done, 4=Canceled, 6=Pending Cancel, 7=Stopped, 8=Rejected, 9=Suspended, A=PendingNew, B=Calculated, C=Expired, D=AcceptBidding, E=PendingRep | Identifies current status of order. |
| 1 | Account | N | STRING |  | The LMAX Account ID. If not specified then the user's default account will be used. |
| 434 | CxlRejResponseTo | Y | CHAR | 1=OrdCxlReq, 2=OrdCxlRepReq | Identifies the type of request that a Cancel Reject is in response to. |
| 102 | CxlRejReason | N | INT | 0=TooLate, 1=Unknown, 2=BrokerOpt, 3=AlreadyPendingCxl, 4=UnableToProcess, 5=OrigOrdModTimeMismatch, 6=DupClOrdID, 99=Other | Code to identify reason for cancel rejection. |
| 58 | Text | N | STRING |  | Free format text string (Note: this field does not have a specified maximum length) |

### TradeCaptureReportRequest (35=AD, incoming)

Message sent by the client in order to request historic trades.

| Tag | Field | Req | Type | Values | LMAX description |
|---|---|---|---|---|---|
| 568 | TradeRequestID | Y | STRING |  | Trade Capture Report Request ID. String with maximum length of 16 ASCII characters. Must be unique while a request is active. For example &%^aUk1-\+4rk;!p |
| 569 | TradeRequestType | Y | INT | 1=MatchedTrades | Type of Trade Capture Report. |
| 263 | SubscriptionRequestType | N | CHAR | 0=Snapshot, 1=Snapshot and Updates | Subscription Request Type. |
| 1 | Account | N | STRING |  | The LMAX Account ID. If not specified then the user's default account will be used. |
| 580 | NoDates | Y | group |  |  |
| 60 | &nbsp;&nbsp;TransactTime | Y | UTC_TIMESTAMP |  | Time of execution/order creation (expressed in UTC (Universal Time Coordinated, also known as “GMT”) |

### TradeCaptureReport (35=AE, outgoing)

Message sent by LMAX to report an historic trade.

| Tag | Field | Req | Type | Values | LMAX description |
|---|---|---|---|---|---|
| 568 | TradeRequestID | N | STRING |  | Trade Capture Report Request ID. String with maximum length of 16 ASCII characters. Must be unique while a request is active. For example &%^aUk1-\+4rk;!p |
| 912 | LastRptRequested | N | BOOLEAN |  | Indicates whether this message is that last report message in response to a request, such as Order Mass Status Request. Y = Last message N = Not last message |
| 17 | ExecID | N | STRING |  | Unique identifier of execution message as assigned by LMAX. Unique over all time. This is a 16 character ASCII string. |
| 527 | SecondaryExecID | N | STRING |  | Execution ID as generated by the MTF. Currently, this is only populated for Executions reporting a Trade where this will be a 16 character string. For other ... |
| 48 | SecurityID | N | STRING |  | LMAX Security identifier. Requires SecurityIDSource=8. |
| 22 | SecurityIDSource | N | STRING | 8=ExchSymb | Identifies class or source of the SecurityID (48) value. Required if SecurityID is specified. |
| 55 | Symbol | N | STRING |  | Ticker symbol |
| 12 | Commission | N | AMOUNT |  | Commission. |
| 13 | CommType | N | STRING | 3=absolute (total monetary amount) | Commission Type |
| 479 | CommissionCurrency | N | STRING |  | Commission currency |
| 32 | LastQty | Y | QUANTITY |  | Quantity of contracts bought/sold on this fill. |
| 31 | LastPx | Y | PRICE |  | Price of this (last) fill. |
| 75 | TradeDate | Y | LOCAL_DATE |  | Indicates date of trade referenced in this message in YYYYMMDD format. Absence of this field indicates current day (expressed in local time at place of trade). |
| 60 | TransactTime | Y | UTC_TIMESTAMP |  | Time of execution/order creation (expressed in UTC (Universal Time Coordinated, also known as “GMT”) |
| 552 | NoSides | Y | group |  |  |
| 54 | &nbsp;&nbsp;Side | Y | CHAR | 1=Buy, 2=Sell, 7=Undisc | Side of the order. |
| 37 | &nbsp;&nbsp;OrderID | Y | STRING |  | Unique identifier for Order as assigned by LMAX. Unique over all time and orderbooks |
| 11 | &nbsp;&nbsp;ClOrdID | N | STRING |  | The client supplied identifier for the order. ClOrdId is restricted to a maximum of 20 arbitrary ASCII characters. Must be unique for all currently working o... |
| 1 | &nbsp;&nbsp;Account | N | STRING |  | The LMAX Account ID. If not specified then the user's default account will be used. |

### TradeCaptureReportRequestAck (35=AQ, outgoing)

Message sent by LMAX in response to a Trade Capture Report Request message to confirm the success or failure of the request.

| Tag | Field | Req | Type | Values | LMAX description |
|---|---|---|---|---|---|
| 568 | TradeRequestID | Y | STRING |  | Trade Capture Report Request ID. String with maximum length of 16 ASCII characters. Must be unique while a request is active. For example &%^aUk1-\+4rk;!p |
| 569 | TradeRequestType | Y | INT | 1=MatchedTrades | Type of Trade Capture Report. |
| 263 | SubscriptionRequestType | N | CHAR | 0=Snapshot, 1=Snapshot and Updates | Subscription Request Type. |
| 748 | TotNumTradeReports | N | INT |  | Total number of trade reports returned. Will only be set when no trades were found for the requested criteria when it's value will be 0. |
| 749 | TradeRequestResult | Y | INT | 0=Successful, 99=Other | Result of Trade Request. |
| 750 | TradeRequestStatus | Y | INT | 0=Accepted, 2=Rejected | Status of Trade Request. |
| 58 | Text | N | STRING |  | Free format text string (Note: this field does not have a specified maximum length) |

## 5. Logon and session

- Logon carries Username and Password (TR Logon 553, 554), added by the session
  in QuickFIX's `toAdmin`; they come from the key file, never from the
  repository, and are redacted from the FIX log and the message store
  (`lmax_fix/RedactLog.hpp`).
- Sequence numbers: kept in a QuickFIX FileStore (`store_path`) and not reset on
  logon (`reset_on_logon = false`), so LMAX's 512-message resend covers short
  disconnects. The session is weekly, Saturday 17:02 to Saturday 17:01 New York,
  matching LMAX's enforced reset (TR Session Resetting). Section 10, item 1.
- No trading-session status message exists in TR; orders are accepted after
  Logon.
- Cancel on disconnect: on unless `reinstate_on_disconnect` sends ExecInst H on
  every order (TR Cancel on Disconnect). Item 2.
- Accounts: Account (1) from the key file; empty means the user's default
  account (TR field 1).

## 6. Order identifiers

| Item | Rule | Source |
|---|---|---|
| ClOrdID | at most 20 ASCII characters; unique among working orders | TR field 11 |
| What the OM sends | BrokerOrderBook's client id in decimal: at most 19 digits, unique for the trading day and across restarts | `frame/som/BrokerOrderBook.hpp` |
| Cancel and replace | new ClOrdID, OrigClOrdID = the order's current one | TR F, G (41, 11) |
| OrderID | LMAX's, "unique over all time and orderbooks"; text. Sent to the SOM in `Ack.xordid` as its number when numeric, otherwise a 64-bit FNV-1a hash | TR field 37 |
| ExecID | 16 characters, unique over all time | TR field 17 |
| SecondaryExecID | the MTF's execution id on trades, "0" otherwise | TR field 527 |
| Which id a report carries | ClOrdID of the request it answers (new, cancel, replace); OrigClOrdID the previous one | TR 8, 9 |

Every id an order has used maps back to it in BrokerOrderBook, so a late report
on an old ClOrdID still resolves.

## 7. New order

| SOM `Order` | LMAX | Source |
|---|---|---|
| side BUY / SEL | 54 = 1 / 2 (TR also lists 7, undisclosed: not used) | TR 54 |
| short sale flag | ignored: spot FX has no short-sale marking; a sell is 54=2 | TR 54 |
| LIMIT, MARKET, STOP, STOP_LIMIT | 40 = 2, 1, 3, 4; Price (44) for 2 and 4; StopPx (99) for 3 and 4 | TR 40, 44, 99 |
| DAY, GTC, IOC, FOK | 59 = 0, 1, 3, 4 | TR 59 |
| AT_OPEN, AT_CLOSE, EXTENDED_DAY | refused locally | TR 59 has no such values |
| size (whole Kaspar units) | OrderQty (38) = size x `unit` contracts (0.1 for spot FX) | TR 38; CS |
| price (ticks) | ticks x tick, written with the tick's decimals | CS |
| route, strategy | refused unless empty (route `LMAX` accepted) | |
| MatchIncrement (1089), ExDestination (100) | not sent | TR D |

Replace (an `Order` with `oid_to_cancel`): only size and price may change, and
only for limit orders; OrdType, Price, OrderQty, Side, TimeInForce and
TransactTime are sent as TR requires for G.

## 8. Reports

| LMAX | BrokerOrderBook | SOM | Source |
|---|---|---|---|
| 8, ExecType 0 New | `on_accepted` | `Ack(oid, xordid)` once | TR 150 |
| 8, ExecType F Trade | `on_fill` with the increase of CumQty (14) over what is known, in units | `Fill`, price LastPx (31) in ticks; an `Ack` first if none was sent | TR 14, 31, 32 |
| 8, ExecType 4 Canceled, C Expired | `on_canceled` (after any fill its CumQty shows) | `CancAck`; a replace in flight gets `Reject` | TR 150 |
| 8, ExecType 5 Replaced | `on_replaced` | `CancAck(old oid)`, `Ack(new oid)`; later reports go to the new oid | TR 150 |
| 8, ExecType 8 Rejected | `on_rejected` (or `on_replace_rejected` when it answers a replace) | `Reject`, reason EXCHLIMIT for OrdRejReason 3, else UNKNOWN | TR 103 |
| 8, ExecType I OrderStatus | accepted, fills (CumQty, priced from AvgPx), cancelled or rejected as the status shows | as above | TR 150, 790 |
| 8, ExecType G TradeCorrect, H TradeCancel | none: not fills | none; logged as errors; copied to the post-trade client | TR 150 |
| 8, ExecType 3, 6, 7, 9, A, B, D, E | none | logged | TR 150 |
| 9 OrderCancelReject, 434=1 | `on_cancel_rejected` | `CancReject`: FILLEDALREADY (OrdStatus 2), CANCELLEDALREADY (4, C), REJECTEDPREVIOUSLY (8), NOTFOUND (CxlRejReason 1), else FROMEXCHANGE | TR 9 |
| 9, 434=2 | `on_replace_rejected` | `CancReject(old oid)`, `Reject(new oid)` | TR 9 |
| report with a ClOrdID this OM did not issue | none | logged as an error (a fill as one the SOM does not know) | |
| ExecID seen before (not on OrderStatus) | none | dropped | TR 17 |

Fills are counted from CumQty, not by adding LastQty: a report resent after a
reconnect, or one already covered by an OrderStatus answer, adds nothing (item 3).

## 9. Throttles, reconnect, risk checks

- Throttles: TR states none. The OM's `max_orders_per_sec` token bucket limits
  new orders and replaces; cancels are never throttled. A refused request is
  rejected (`THROTTLE`), never retried.
- Busy: QuickFIX has no busy state; a send while not logged on is refused
  (`Reject DISCONNECTED`, `CancReject DISCONNECTED`).
- Reconnect: QuickFIX reconnects after `reconnect_s`; with the FileStore it asks
  for what it missed and LMAX resends up to 512 messages, duplicates being
  dropped by ExecID and CumQty. After every logon the OM sends an
  OrderStatusRequest for each live order, which also covers a gateway failover
  (no resend) and more than 512 messages (TR Application Recovery).
- Risk checks: LMAX's are not documented in TR beyond OrdRejReason 3 (exceeds
  limit). The OM checks `max_order_qty` (units) and whole-unit sizes.

## 10. Pitfalls

`pitfalls.md` sections 1-3 apply as in the market-data description; section 8
(order entry):

- Client id uniqueness: LMAX requires uniqueness among working orders only; the
  BrokerOrderBook ids are unique for the day and across restarts
  (`ClientIdsAreUniqueAndARestartDoesNotReuseThem`).
- Cancel and replace ids: new ClOrdID each time; every id maps back to the order
  (`CancelRoundTripUsesANewClOrdID`, `ReplaceAcksTheNewOrderAndCancelsTheOld`).
- Fill before ack, fill racing a cancel, fill after the cancel ack: each counted
  once (tests of the same names).
- Unsolicited cancels: cancel on disconnect and expired IOC reach the SOM
  (`UnsolicitedCancelAndExpiryReachTheSom`,
  `CancelOnDisconnectIsReportedAfterTheReconnect`).
- Busts and corrections: ExecType G and H are not fills
  (`TradeCorrectionsAndCancelsAreNotFills`).
- Busy connections: not applicable (QuickFIX); throttle tested.
- Reconnect replay: CumQty-based counting and ExecIDs
  (`ReconnectAsksForStatusAndDeliversAMissedFillOnce`,
  `DuplicateReportsAreAppliedOnce`).
- Orders placed outside the OM: logged, ignored
  (`UnknownReportsAreLoggedAndIgnored`). TR: orders placed on the web channel are
  not cancelled when the FIX session drops.
- Short sales: not applicable to spot FX; sells are sent as 54=2.

Section 7 (post-trade) items that apply here: a missing field must not throw into
QuickFIX (the session catches everything in `fromApp` and reports it as
unparsed); the dictionary is LMAX's own, generated from TR.

## 11. Ambiguities

| # | Question | Reading taken | Alternatives | Resolved by |
|---|---|---|---|---|
| 1 | Recovery: FIX resend (512 messages, sequence numbers kept for the week) or reset on every logon and Application Recovery only? TR describes both and says the second "may be simpler" | both: FileStore, no reset on logon, plus OrderStatusRequest per live order on each logon | reset on every logon and rely on H and AD alone | review.md decision 1 |
| 2 | Cancel on disconnect | LMAX's default (no ExecInst sent) | ExecInst H on every order (`reinstate_on_disconnect`); required for multi-account users | review.md decision 2 |
| 3 | Fill accounting from CumQty; fills learnt from OrderStatus priced from AvgPx | as implemented | LastQty sums with ExecID dedupe only | review.md decision 3 |
| 4 | ExecType of an IOC or FOK that does not fill: 4 (Canceled) or C (Expired)? What does LMAX answer for an unknown ClOrdID in an OrderStatusRequest? What ExecID does an OrderStatus report carry? | 4 and C both mean cancelled; OrdStatus 8 on an order not yet accepted means rejected; OrderStatus reports are exempt from ExecID dedupe | | live check (IOC step), review.md decision 4 |
| 5 | SOM messages on replace | `CancAck(old)` + `Ack(new)`; on reject `CancReject(old)` + `Reject(new)` (as `ttom/` TTFIXOM does for Replaced) | | review.md decision 5 |
| 6 | OrderQty unit | contracts, 0.1 per Kaspar unit (TR 38 "Quantity of contracts"; CS) | | review.md decision 6; live check fill |
| 7 | Market orders: TR gives no TimeInForce rule | sent with the SOM's TimeInForce | force IOC | review.md decision 7 |
| 8 | Does a Replaced report carry CumQty of fills before the replace? | yes (CumQty is cumulative for the order); applied as any other CumQty | | tests use it; live check |
