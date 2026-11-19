# Feed description: Clear Street FIX API (FIX 4.2), order entry

Every entry cites its source. **G** is Clear Street's public FIX API guide,
**CL** its changelog, **SS** the short-selling guide, **RL** the rate-limits
guide (section 1). Sections of G are cited by their heading.

## 1. Venue and documents

Clear Street is a US broker-dealer and clearing firm. The FIX API is order
entry for US common stocks through a Clear Street account. Everything below
was public, without login, on 2026-11-18.

| Document | Version | Date | Applies to |
|---|---|---|---|
| G: "FIX API" guide, `docs.clearstreet.com/guides/fix-api/` (Markdown at `.../index.md`) | none printed; the current page | FIX API released 2026-09-29 (CL); page retrieved 2026-11-18 | everything: session, D, F, G, 8, 9, 3 |
| CL: changelog, `docs.clearstreet.com/changelog` | entries to 2026-11-17 | 2026-11-18 | overrides G where they differ: 2026-09-29 (FIX API release), 2026-10-10 (OpenClose 77 checked, BREAKING), 2026-10-23 (margin floor applies to 35=D and 35=G) |
| SS: "Short Selling and Locates" guide | current page | 2026-11-18 | locates, sides (REST); FIX sides are in G |
| RL: "Rate Limits" guide | current page | 2026-11-18 | REST only; says nothing about FIX |
| `clear-street.github.io/FIX-docs` | FIX 4.2 | undated | a different product: inbound trade reporting from client OMSs (35=8 with 9001-range tags). Not used. |
| `docs.clearstreet.io/olympus/reference/locates-fix` | FIX 4.2 | undated | a different product: locate requests (QuoteRequest/Quote). Not used. |

Not public: a QuickFIX dictionary or any machine-readable spec, hosts and
ports, the TargetCompID, a UAT/certification environment, sample logs, and the
format of the account list in the Logon's Text (58). Section 10 lists what
that leaves open.

Which document applies: G for the protocol, with CL's 2026-10-10 entry
replacing G's statement that OpenClose (77) is advisory (G "Short selling and
position intent" still says "advisory"; CL says it is now checked and the
order rejected if it disagrees).

## 2. Transport

- FIX 4.2 over TCP. "The network endpoint and TLS/connectivity details" are
  provided at provisioning; "transport security is established outside FIX, so
  EncryptMethod (98) is always 0" (G, FAQ). Whether that means TLS on the
  socket or a private line is not said. The OM supports both
  (`tls = yes`: QuickFIX `SSLSocketInitiator`; `no`: plain TCP).
- Session hours: "Sunday 8 pm EST to Friday 8 pm EST" (G, FAQ). Section 5.
- One session per SenderCompID/TargetCompID; a second connection is a
  "duplicate connection" and is logged out (G, Supported messages, Logout).
- Maximum message 64 KiB (G, Message conventions).

## 3. Framing and dictionary

Standard FIX 4.2 tag=value. 8 first, 9 second, 35 third (G, Standard header).
Timestamps UTC `YYYYMMDD-HH:MM:SS[.sss]`; decimals plain fixed point, no
exponent (G, Message conventions). "A tag may occur only once per message;
fields not listed in this guide are unsupported and rejected" (same).

Clear Street publishes no dictionary. `tools/mkdict.py` writes
`build/ClearStreet-FIX42.xml` from QuickFIX's `FIX42.xml`, adding only what G
uses and FIX 4.2 lacks, each with its G section:

| Added | Where | G section |
|---|---|---|
| Username (553), Password (554) | Logon | Logon (35=A) |
| Text (58) | Logon (Clear Street's: account list) | Logon (35=A) |
| OpenClose (77), TradingSessionID (336), TargetStrategy (847) | D, G | Submitting an order; Extended hours; Execution strategies |
| TimeInForce value 7 (At the Close) | field 59 | Time in force (a FIX 4.3 value, a Clear Street extension) |

QuickFIX validation: outbound requests are built only with G's fields.
Inbound, `ValidateFieldsOutOfOrder = N`, `ValidateUserDefinedFields = N`,
`AllowUnknownMsgFields = Y`, because a report that fails validation would be
answered with a Reject (35=3) and never reach the OM, and G's report tables
may not be all Clear Street sends. Parsers never throw (`CsFix.cpp`); a report
that does not parse reaches the actor as "unparsed" and is logged as an error.

## 4. Message catalogue

Session messages (A, 0, 1, 2, 3, 4, 5) are QuickFIX's, with 553/554 added to
the Logon (section 5). Application messages as G defines them; "Sent" says
what the OM puts in each field.

### NewOrderSingle (35=D), client to Clear Street (G, Submitting an order)

| Tag | Field | Req | G | Sent |
|---|---|---|---|---|
| 1 | Account | Y | must be authorized for the session; unknown or unauthorized: **Reject (35=3)** | `account` from config |
| 11 | ClOrdID | Y | 1-64 printable ASCII, never reused, "even after reconnecting or on a later trading day" | section 6 |
| 21 | HandlInst | Y | must be 1 | 1 |
| 55 | Symbol | Y | case-sensitive ticker | instrument file |
| 167 | SecurityType | N | CS if present | omitted |
| 54 | Side | Y | 1 buy, 2 sell; 5 and 6 rejected | 1 or 2 |
| 77 | OpenClose | N | O / C; checked against the position since CL 2026-10-10 | O on a SOM short sale (below), else omitted |
| 38 | OrderQty | Y | decimal > 0 | whole shares |
| 40 | OrdType | Y | 1 market, 2 limit, 3 stop, 4 stop limit | from SOM `ord_type` |
| 44 | Price | C | limit and stop limit only | ticks x tick, digits of the tick |
| 99 | StopPx | C | stop and stop limit only | same |
| 59 | TimeInForce | Y | 0 day, 1 GTC, 2 OPG, 3 IOC, 4 FOK, 7 at the close | from SOM `tif` |
| 847 | TargetStrategy | N | 1001 VWAP, 1002 TWAP, 1003 DMA | from SOM `strategy` / `route` |
| 168 | EffectiveTime | N | TWAP/VWAP window start | not sent (window = arrival to close) |
| 126 | ExpireTime | N | TWAP/VWAP window end | not sent |
| 100 | ExDestination | C | MIC, required with 847=1003 only | SOM `route` |
| 336 | TradingSessionID | N | 6 = extended hours | 6 for SOM `EXTENDED_DAY` |
| 60 | TransactTime | Y | UTC | now |
| 15 | Currency | N | USD | omitted |
| 58 | Text | N | not forwarded | omitted |

Matrices (G, Order types; Time in force; Execution strategies; Extended
hours), all applied locally before sending:

- Price required for 2 and 4, forbidden for 1 and 3; StopPx required for 3 and
  4, forbidden for 1 and 2.
- TIF: GTC not for market; OPG, IOC, FOK, at-the-close not for stop and stop
  limit.
- Strategies (847) only on market and limit, only TIF Day, never with 336.
  ExDestination only with DMA and then required, from G's list of 18 MICs.
- 336=6 only on limit and stop limit, only TIF Day, no strategy.

Sides (G, Short selling and position intent; CL 2026-10-10; SS):

| SOM order | Sent | Why |
|---|---|---|
| buy | 54=1 | |
| sell | 54=2, no 77 | Clear Street decides the position effect |
| sell, `ShortSale::SHORT` | 54=2, 77=O (when `mark_short_open = yes`) | the only way to state "open a short" in G; since CL 2026-10-10 Clear Street rejects (39=8) if 77 disagrees with its own determination. `mark_short_open = no` refuses the order locally. |
| sell, `ShortSale::SHORT_EXEMPT` | refused locally | 54=6 is rejected and G has no other exempt marking |
| any order with a `locate_id` | refused locally | G has no locate field; Clear Street "evaluates your account's position, locates" itself (G, SS) |

### OrderCancelRequest (35=F) (G, Canceling an order)

| Tag | Field | Sent |
|---|---|---|
| 1 | Account | config account |
| 41 | OrigClOrdID | the order's *current* ClOrdID (from D or the last accepted G) |
| 11 | ClOrdID | a new id (section 6) |
| 55 | Symbol | the order's |
| 54 | Side | the order's (1 or 2) |
| 60 | TransactTime | now |
| 38 | OrderQty | the order's total quantity |

Nothing else: "the order-description fields that only 35=D and 35=G carry" are
rejected on F.

### OrderCancelReplaceRequest (35=G) (G, Replacing an order)

The full D body plus 41. Only OrderQty (not below the filled quantity), Price
and StopPx may change; a change to side, symbol, order type, TIF or extended
hours is refused with 35=9. The OM refuses such a replace locally, and also
one that changes strategy, route or short marking (G: "All other fields: No").

### ExecutionReport (35=8) (G, Execution Report)

Fields read: 37 OrderID (stable across replace; `NONE` if rejected before one
was assigned), 17 ExecID (unique per report), 20 ExecTransType (always 0 in
this version), 150 ExecType, 39 OrdStatus, 11 ClOrdID (current id), 41
OrigClOrdID (on a confirmed cancel/replace; **absent on unsolicited
cancels**), 1, 55, 54, 38, 44, 99, 31 LastPx, 32 LastShares (0 on non-fills),
151 LeavesQty, 14 CumQty, 6 AvgPx, 60 TransactTime, 168 EffectiveTime (on
150=A), 103 OrdRejReason (3 over limit, 6 duplicate), 378
ExecRestatementReason, 58 Text. PossDupFlag (43) from the header.

| 150 | G | 
|---|---|
| A | Pending New: accepted and queued (e.g. outside hours); 168 = release time |
| 0 | New: accepted and working |
| 1 | Partial fill |
| 2 | Fill |
| 3 | Done for day; a GTC order is not terminated and is restated (150=D) at the next open |
| 4 | Canceled; 41 set when requested, absent when unsolicited |
| 5 | Replaced; 41 = superseded id, 11 = new current id |
| 8 | Rejected: validation, risk, routing or the market |
| D | Restated |

### OrderCancelReject (35=9) (G, Order Cancel Reject)

37 (`NONE` if no known order), 11 (the refused request's id), 41, 39 (the
order's current status; 8 if unknown), 1, 60, 434 (1 cancel, 2 replace), 102
(0 too late, 1 unknown order, 3 pending), 58.

### Reject (35=3) (G, Reject)

45 RefSeqNum, 371, 372, 373, 58. Used for session-level errors **and for an
unknown or unauthorized Account on 35=D** (G, Submitting an order, tag 1).
The session keeps the MsgSeqNum and ClOrdID of every request it sends, so a
Reject is matched to its order by RefSeqNum (section 7).

## 5. Logon and session

| Item | G | OM |
|---|---|---|
| Logon | 98=0, 108 HeartBtInt, 141 optional, **553 Username = SenderCompID, 554 Password = the API key** (Logon (35=A)) | 553/554 added in `toAdmin` to the Logon only; the key is read from `key_file`, never from the repository |
| Logon response | carries the effective 108 and, in 58, "the list of accounts the session may trade" | the OM looks for its account as a whole token in 58 and logs an error if absent; it does not stop |
| Authentication | network connection + exact 49/56 pair + 553/554 (FAQ) | |
| Logout on error | no authorized accounts, duplicate connection, unanswered Test Request, sequence violation (Supported messages) | logged; orders are refused locally until the next Logon |
| Heartbeats | idle one interval: Heartbeat; Clear Street sends TestRequest after two silent intervals and closes after one more | QuickFIX |
| Sequence numbers | journaled across disconnects; reset only on a coordinated 141=Y; a lower number without 43=Y: Logout (Sequence numbers and recovery) | `ResetOnLogon/Logout/Disconnect = N`, `PersistMessages = Y`, FileStore kept between runs |
| Trading-session status | none in G; orders accepted after Logon | ready on Logon |
| Session hours | Sunday 8 pm to Friday 8 pm "EST" (FAQ) | QuickFIX schedule in UTC, default Monday 01:00 to Saturday 00:00 (8 pm EST = 01:00 UTC); see section 10 |
| Cancel on disconnect | not mentioned | none assumed: orders keep working while disconnected |
| Password on disk | | QuickFIX's FileLog and FileStore are wrapped to write 554 as `***` |

## 6. Order identifiers

- G: ClOrdID 1-64 printable ASCII, unique, "never reuse it, even after
  reconnecting or on a later trading day"; cancel and replace ids follow "the
  same rules and per-account uniqueness" (G, Canceling an order, tag 11).
- OM: `ClOrdID = <prefix><YYYYMMDD>-<client id>`, the client id from
  `BrokerOrderBook`'s `ClientIdGen` (milliseconds since midnight << 36 |
  counter, at most 19 digits) and the UTC date the OM started. The book's ids
  are unique within a day and across restarts in the day; the date makes them
  unique across days. Example `K20261118-4561237654321`.
- Cancel and replace each take a new ClOrdID; 41 is the order's current id.
  After 150=5 the replace's id becomes current (G, Replacing an order).
- Reports carry the current id in 11; 41 on 150=4 (requested) and 150=5. The
  OM looks up 11, then 41; every id an order used maps back to it in the book.
- OrderID (37) is a string (G's example is UUID-like). The SOM's `xordid` is
  its 64-bit FNV-1a hash; the string is logged.

## 7. Reports to BrokerOrderBook and SOM messages

| Clear Street | Book call | SOM message (on Ok) |
|---|---|---|
| 8, 150=0 New | `on_accepted(cid, hash(37))` | `Ack` |
| 8, 150=A Pending New | `on_accepted` (release time logged) | `Ack` |
| 8, 150=1 / 2 | `on_fill(cid, CumQty - applied)` (an ack first if none yet) | `Fill`; nothing when CumQty has not increased |
| 8, 150=4, 41 set | `on_canceled` (+ any unseen CumQty as a fill first) | `CancAck` |
| 8, 150=4, 41 absent (unsolicited) | same | `CancAck` |
| 8, 150=3 Done for day, non-GTC | `on_canceled` | `CancAck` |
| 8, 150=3, GTC with leaves > 0 | none (stays working) | none, logged |
| 8, 150=5 Replaced | `on_replaced` | `CancAck(old oid)`, `Ack(new oid)` |
| 8, 150=8, new order | `on_rejected` | `Reject` (103=3: `EXCHLIMIT`, else `UNKNOWN`) |
| 8, 150=8, the replace's ClOrdID | `on_replace_rejected` | `CancReject(old, REJECTONMOD)`, `Reject(new)` |
| 8, 150=8 on an accepted order | `on_canceled` | `CancAck` (no longer working) |
| 8, 150=D Restated | CumQty applied if increased | none, logged |
| 8, ExecTransType != 0 (bust/correct; "not sent in this version") | none | none; logged as an error |
| 9, 434=1 | `on_cancel_rejected` | `CancReject` (39=2 `FILLEDALREADY`, 39=4/3/C `CANCELLEDALREADY`, 102=1 `NOTFOUND`, else `FROMEXCHANGE`) |
| 9, 434=2 | `on_replace_rejected` | `CancReject(old)`, `Reject(new)` |
| 3, RefSeqNum = a D | `on_rejected` | `Reject` |
| 3, RefSeqNum = an F or G | `on_cancel_rejected` / `on_replace_rejected` | `CancReject` (+ `Reject(new)` for G) |
| report whose 11/41 is not this OM's | none | logged as an error; a fill flagged "a FILL the SOM does not know about" |

Fills are counted from CumQty (14), not by adding LastShares: the fill is the
increase over what has been applied. A report resent after a reconnect, or a
cancel report that carries a fill the OM had not seen, therefore adds exactly
what is new. The fill price is LastPx when the report carries exactly the new
quantity, else what AvgPx implies for the unseen part. Duplicate ExecIDs are
dropped before any of this. Every order ends in one `Fill` (last), `Reject` or
`CancAck`; later reports are `Late` and logged.

## 8. Throttles, reconnect and replay, risk checks

- **Throttles.** G gives no message-rate limit for FIX. RL's limits (1,000
  writes a minute per user, 429 responses) are for REST requests. There is no
  busy state in the FIX API. The OM has a local token bucket
  (`max_orders_per_sec`, new orders and replaces) that refuses with
  `Reject(THROTTLE)`; nothing is queued or retried.
- **Reconnect.** FIX resend (G, Sequence numbers and recovery): sequence
  numbers survive disconnects, the client answers ResendRequests from its
  store with 43=Y; Clear Street "never delivers a correctly marked duplicate to
  the market a second time". Clear Street's own missed reports come back
  through QuickFIX's ResendRequest. G says order status requests "are not
  supported" (FAQ), so there is no application-level recovery: the FIX resend
  is the only way to learn what happened while disconnected.
- **Requests while disconnected** are refused locally (`DISCONNECTED` /
  `NOTLOGGEDIN`), never stored: QuickFIX would otherwise store them and send
  them in answer to a ResendRequest after the next Logon, an order sent late.
- **Duplicates.** ExecID set; CumQty-based fill counting.
- **Risk checks at Clear Street:** entitlement, pre-trade risk (103=3),
  duplicate order (103=6), buying power and margin (CL 2026-10-23), position
  effect against 77 (CL 2026-10-10), the account's `risk.max_notional` (CL
  2026-11-10, REST). **In the OM:** instrument configured, whole shares,
  `max_order_qty`, `max_notional` (a market order cannot be checked and is
  refused when it is set), `max_orders_per_sec`, and every matrix of section 4.

## 9. Pitfalls

| `pitfalls.md` item | Here |
|---|---|
| Several documents / versions | G has no version number; G and CL disagree on 77 (section 1, CL wins). The GitHub FIX-docs and Olympus locates pages are other products. |
| Errata / release notes | CL 2026-10-10 and 2026-10-23 change D and G behaviour. |
| Tables across page breaks, offsets, padding, binary encodings, endianness | do not apply (HTML page, tag=value). |
| Enumerations defined elsewhere | 39 lists `A 0 1 2 3 4 5 6 8 E` but not their meanings; FIX 4.2's are assumed. 373 values: "standard FIX 4.2". |
| Examples contradicting tables | none found: the D, F, G and 8 examples carry the fields their tables mark Y. G's Logon example puts `<SenderCompID>` in 553, consistent with the table. |
| Two protocol layers | session Reject (35=3) also refuses an order (unauthorized account): handled in section 7. |
| Timestamps | UTC, ms or finer; parsed to ns. |
| Sequence scope | per session, journaled across disconnects. |
| Gap rule / recovery | standard FIX; section 8. |
| State to clear on reset | a coordinated 141=Y resets sequence numbers only; the OM's book is not cleared (orders survive). |
| Session-level behaviour | section 5. |
| Book semantics, prices/yield, market data items | do not apply (order entry). |
| Prices and ticks | prices from SOM ticks x instrument tick; G accepts any decimal. Sub-penny rules are the market's, not checked. |
| Quantities | G allows fractional OrderQty (example `38=25.5`); the OM sends and accepts whole shares only and logs a fractional fill as invalid. |
| Instrument definitions | G points to the REST instruments endpoint; the OM uses a CSV file (asset id, ticker, tick). |
| Post-trade items | G has no trade capture; the separate GitHub trade-reporting spec is another product. |
| Client ids | section 6. |
| Cancel/replace ids | section 6; 41 is the current id. |
| Fill before ack, racing a cancel, after the cancel ack | CumQty counting; tests `FillBeforeAck`, `FillRacingACancel`, `FillAfterTheCancelAckStillCounts`, `CancelReportCarryingAnUnseenFill`. |
| Unsolicited cancels | 150=4 without 41, and 150=3 for non-GTC: `CancAck`. |
| Busts and corrections | G: not sent (20 always 0); if one comes, not a fill, logged. |
| Busy / throttled | none in FIX; local token bucket. |
| Reconnect duplicates | ExecID + CumQty. |
| Orders placed outside this OM | REST and other sessions trade the same accounts (G, Overview); reports for them on this session are logged. Whether they are sent here is not said (section 10). |
| Short sales | section 4: 77=O or refuse; short exempt refused. |

## 10. Ambiguities

| # | Question | Reading taken | Alternatives | Resolved by |
|---|---|---|---|---|
| 1 | How to mark a short sale. G: only 54=1/2, 77 advisory; CL 2026-10-10: 77 checked and the order rejected if it disagrees. | SOM short sale: 54=2 + 77=O (`mark_short_open`), so Clear Street rejects rather than silently closes a long; short exempt and locate ids refused. | 54=2 with no 77 (Clear Street decides; a "short" may close a long instead); refuse all SOM shorts. | Clear Street; review decision 1 |
| 2 | Format of the account list in the Logon's 58. | Search for the account as a whole token; log an error if missing, keep running. | Treat a missing account as fatal; parse a specific separator. | a real Logon |
| 3 | Is 150=8 ever sent for an order already accepted (venue reject after routing)? | Treated as the end of the order: `CancAck`. | `Reject` (the SOM may not expect it after an `Ack`); log only. | Clear Street |
| 4 | Done for day (150=3) on a non-GTC order: is a 150=4 sent too? | 150=3 ends the order (`CancAck`); a later 150=4 is `Late`. | Wait for 150=4. | Clear Street |
| 5 | Is 150=A followed by 150=0 at release? | 150=A is the `Ack`; a later 150=0 changes nothing. | Ack only on 150=0. | G says "accepted and queued", read as accepted |
| 6 | Session hours "EST" in G's FAQ: EST or US Eastern (EDT in summer)? | Default schedule in UTC fixed to EST (Sunday 20:00 EST = Monday 01:00 UTC); configurable. | Use US/Eastern with QuickFIX `TimeZone`. | Clear Street; review decision 3 |
| 7 | TLS: on the socket, or a private line? | Configurable (`tls`); default no. | | provisioning details |
| 8 | Is there a UAT / certification environment? | G names none. The OM refuses `environment = production` unless `production_ok = yes`; no live check was run. | | Clear Street |
| 9 | Cancel on disconnect. | None (G says nothing). | Clear Street cancels on logout. | Clear Street |
| 10 | Fractional quantities (G example `38=25.5`). | Whole shares only. | Scale by a per-instrument unit. | owner |
| 11 | Orders placed by REST or another session on the same account: reported on this session? | If they are, logged as unknown (fills as errors). | | Clear Street |
| 12 | Message-rate limit for FIX. | None documented; local token bucket. | | Clear Street |
| 13 | A GTC order alive across a restart on a later day carries the earlier date in its ClOrdID; the OM, matching only today's prefix, does not recognise its reports. | Logged as unknown. | Accept any date in the prefix and recover the order from the book (not possible: the book starts empty). | owner; review |
