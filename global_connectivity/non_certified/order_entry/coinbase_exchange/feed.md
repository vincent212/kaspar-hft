# Feed description: Coinbase Exchange order entry (FIX 5.0 SP2)

Order entry for Coinbase Exchange spot over FIX 5.0 SP2 on FIXT 1.1. The REST
order API exists too (RAUTH); FIX is used, as the survey recommends. There is
no WebSocket order entry.

Every entry cites its source; pages fetched as Markdown on 2026-11-18.
Abbreviations:

- **OE5**: Exchange FIX Order Entry Messages 5.0, https://docs.cdp.coinbase.com/exchange/fix-api/order-entry-messages/order-entry-messages5
- **CONN**: Exchange FIX API Connectivity, https://docs.cdp.coinbase.com/exchange/fix-api/connectivity
- **FRL**: FIX Rate Limits, https://docs.cdp.coinbase.com/exchange/fix-api/rate-limits
- **BP**: FIX Best Practices, https://docs.cdp.coinbase.com/exchange/fix-api/best-practices
- **DICT**: `cb_exch_fix_dictionaries-20260612.zip`, https://docs.cdp.coinbase.com/exchange/fix-api/dictionary-downloads (files `order-entry/FIXT11-prod-sand.xml` and `order-entry/FIX50-prod-sand.xml`, both dated 2024-02-23 inside the zip; copied to `fix/`)
- **SBX**: Exchange Sandbox, https://docs.cdp.coinbase.com/exchange/introduction/sandbox
- **RAUTH**: REST Authentication, https://docs.cdp.coinbase.com/exchange/rest-api/authentication
- **CL**: Exchange Changelog, https://docs.cdp.coinbase.com/exchange/changes/changelog

## 1. Venue and documents

| Document | Version | Date | Applies to |
|---|---|---|---|
| OE5 | baseline FIX 5.0 SP2 | fetched 2026-11-18 | Every message the OM sends and receives |
| DICT | zip of 2026-06-12 (order-entry files dated 2024-02-23) | 2026-06-12 | The QuickFIX dictionaries |
| CONN, FRL, BP | none | fetched 2026-11-18 | Hosts, TLS, limits, batches, modify |
| SBX | none | fetched 2026-11-18 | Sandbox hosts and certificate |
| CL | to 2026-Feb-02 | fetched 2026-11-18 | FIX 4.2 deprecated 2025-06-03; icebergs removed 2025-05-27; ERs for orders of earlier sessions delivered to the current session since 2025-10-02 |

FIX 4.2 order entry was deprecated on 2025-06-03 (CL); `FIX42-prod-sand.xml`
in the zip is not used.

## 2. Transport

- `tcp+ssl://fix-ord.exchange.coinbase.com:6121` (production) and
  `tcp+ssl://fix-ord.sandbox.exchange.coinbase.com:6121` (sandbox) (OE5, note;
  SBX). TLS only, TLS 1.2 or higher (CONN, "Ciphers"; CL 2025-OCT-20).
- Probed (TLS handshake only, no FIX message, 2026-11-18): the sandbox hosts
  `fix-ord` (6121) and `fix-dc` (6122) present `CN=*.sandbox.exchange.coinbase.com`,
  issued by Amazon RSA 2048 M01, valid 2026-01-04 to 2027-02-01, chaining to
  Amazon Root CA 1, with or without SNI. The certificate SBX asks clients to
  "validate" (valid 2022-03-27 to 2023-04-25) is out of date; the OM verifies
  the chain against the system's CAs (or `ca_file`).
- QuickFIX's SSL initiator verifies the chain but not the host name, and sends
  no SNI (`SSLSocketConnection.cpp`); recorded in section 10.
- One connection per API key; 75 connections per profile; 2 logons per second
  per key (FRL; OE5 warning).
- Every Saturday at 1 PM ET (18:00 UTC) every FIX 5 session is logged out (OE5,
  warning).

## 3. Framing

FIXT 1.1 tag=value, SOH-delimited; `BeginString` `FIXT.1.1`;
`DefaultApplVerID` 9 (FIX50SP2) on the Logon; `SenderCompID` the API key,
`TargetCompID` `Coinbase` (OE5, "Standard Header"). `SendingTime` with exactly
three fraction digits (OE5, "FIX 5.0 Logon Details").

## 4. Logon and session

| Tag | Field | OM sends | Source |
|---|---|---|---|
| 98 | EncryptMethod | 0 | OE5, Logon |
| 108 | HeartBtInt | 10 (configurable, at most 30) | OE5, Logon |
| 141 | ResetSeqNumFlag | Y | OE5, Logon |
| 553 | Username | API key | OE5 |
| 554 | Password | passphrase | OE5 |
| 95 | RawDataLength | length of RawData | OE5 |
| 96 | RawData | base64(HMAC-SHA256(base64decode(secret), SendingTime ␁ MsgType ␁ MsgSeqNum ␁ SenderCompID ␁ TargetCompID ␁ Passphrase)) | OE5, "FIX 5.0 Logon Details" and its Python example |
| 1137 | DefaultApplVerID | 9 | OE5 |
| 8013 | CancelOrdersOnDisconnect | Y (profile's orders; Coinbase's recommendation), S, or none | OE5 |

- Sequence numbers: the client's are always reset after a disconnect; the
  server's are reset when `ResetSeqNumFlag` is Y (OE5, Logon). Resuming with
  `141=N` and a ResendRequest in pages of 1,000 is documented (OE5, "How to
  resume a session"); the OM does not use it (section 8, "Reconnect").
- Heartbeat: Coinbase sends a TestRequest after 1.5 × HeartBtInt of silence
  and disconnects after 2 × (OE5, Logon).
- No TradingSessionStatus is documented; orders are accepted after the Logon.
- Credentials: read at run time from `~/.kaspar/keys/coinbase_exchange_sandbox.json`
  (README.md, "Credentials"); never in the repository or a log. QuickFIX's
  message log is replaced by `MaskingLog`, which writes `554=***` and `96=***`.

## 5. Order identifiers

- `ClOrdID` (11): "a variant 1 UUIDv4 ... all lowercase and hyphens ... 8, 4,
  4, 4, 12"; "shouldn't match the ClOrdID of any open orders" (OE5,
  NewOrderSingle). Uniqueness beyond open orders: not stated.
- A cancel takes a new `ClOrdID`, names the order by `OrigClOrdID` (41) and
  `OrderID` (37) (OE5, OrderCancelRequest: both "Y", and "at least one of" must
  be given).
- `OrderID` (37) is a UUID assigned by Coinbase; replaced orders keep it (OE5,
  OrderCancelReplaceRequest).
- The OM: `frame::som::BrokerOrderBook` client ids (63-bit, unique for the
  trading day, increasing across restarts) each get a random UUIDv4
  (`std::random_device`-seeded `mt19937_64`), mapped both ways. `Ack.xordid` and
  `BrokerOrderBook::exch_oid` are `order_id64(OrderID)`: the UUID's high 64 bits
  with the top bit cleared, the id the market-data handler gives the order.

## 6. New order

NewOrderSingle (35=D) as the OM writes it (OE5, NewOrderSingle):

| Tag | Field | Value |
|---|---|---|
| 11 | ClOrdID | UUIDv4, lowercase |
| 55 | Symbol | product id from the products table, keyed by the Kaspar asset name |
| 54 | Side | 1 buy, 2 sell |
| 38 | OrderQty | SOM size × `qty_unit`, written with the product's base decimals ("0.00250000") |
| 40 | OrdType | 2 limit, 1 market, 4 stop limit |
| 44 | Price | SOM price (ticks) × `quote_increment` ("64000.00"); not on market orders |
| 99 | StopPx | stop limit only |
| 59 | TimeInForce | GTC 1, IOC 3, FOK 4; SOM DAY → `day_tif` (GTC by default, or GTD 6 with 126 ExpireTime = next 00:00 UTC, or refused) |
| 18 | ExecInst | A (post only) on limit orders when `post_only` |
| 7928 | SelfTradeType | D (default), O, N or B (configured) |

Market orders go IOC. Refused locally with a Reject: plain STOP (Coinbase has
stop limit and TPSL only), AT_OPEN, AT_CLOSE, EXTENDED_DAY, short-sale marks,
routes, broker algorithms, cancel/replace (`oid_to_cancel`), assets not in the
products table, sizes that are not whole positive units, non-positive prices,
and the local limits. Not used: `CashOrderQty` (152), TPSL (40=O),
`TriggerPriceDirection` (1109), batches (U6, U4), `HandlInst` E (EXN).

## 7. Reports

| Venue report | Book call | SOM message |
|---|---|---|
| 35=8, 150=0 New | `on_accepted(cid, order_id64(37))` | `Ack` (once) |
| 35=8, 150=F Trade | `on_fill(cid, LastQty in units)`, after `on_accepted` | `Ack` first if none was sent, then `Fill` (`pxi` = LastPx in ticks, `still_to_be_filled` = leaves) |
| 35=8, 150=4 Canceled | `on_canceled` | `CancAck` (requested or unsolicited) |
| 35=8, 150=C Expired (IOC, FOK, GTD) | `on_canceled` | `CancAck` |
| 35=8, 150=8 Rejected | `on_rejected` | `Reject` (`EXCHLIMIT`) |
| 35=8, 150=D Restated (STP partial decline, 378=5) | none while LeavesQty > 0; `on_canceled` at LeavesQty 0 | `CancAck` at 0; never a `Fill` |
| 35=8, 150=I Order status | settles the order (section 8) | `Ack`, a missing `Fill`, `CancAck` or `Reject` |
| 35=8, 150=5 Replaced | none (the OM never replaces) | logged as an error |
| 35=9 OrderCancelReject | `on_cancel_rejected` | `CancReject` (`FROMEXCHANGE`) |
| 35=j BusinessMessageReject, 372=D | `on_rejected` (379 = ClOrdID) | `Reject` |
| 35=j, 372=F | `on_cancel_rejected` | `CancReject` (`FROMEXCHANGE_BUSINESSREJECT`) |
| 35=3 Reject | none | logged with RefSeqNum, RefTagID, reason |

The ExecutionReport's ClOrdID is the request's: the order's for New and Trade,
the cancel's for a requested Canceled (OE5, ExecutionReport: "The client order
ID of the (new) order"). The OM maps any id an order used.

Fill before ack, fill racing a cancel and fill after the cancel ack are counted
once each (BrokerOrderBook); a Trade report whose `CumQty` (14) is not above
what the order has filled is a replay, skipped. A fill whose `LastQty` is not a
whole number of `qty_unit` is logged as an error, never rounded (with the
default `qty_unit` = `base_increment` this cannot happen).

## 8. Throttles, reconnect, risk

- **Rate**: 100 requests/s per session; disconnected above 200 messages/s;
  batches of up to 15 count as one (FRL; BP). The OM throttles new orders at
  `max_orders_per_sec` (50) and refuses the excess with `Reject(THROTTLE)`;
  cancels are never throttled. Coinbase documents no "busy" answer; the session
  interface's busy/available is kept for parity with the other OMs.
- **Reconnect**: after a disconnect the OM makes a new session after
  `reconnect_ms`, logs on with `141=Y` (so nothing is resent), and sends
  OrderStatusRequest (35=H, OE5) for every live order. The answer (150=I)
  gives `OrdStatus` and `CumQty`: a cancel while away becomes `CancAck`;
  quantity filled while away and never reported becomes one `Fill` priced so the
  order's average is `AvgPx`, logged as an error for a person to check; an order
  Coinbase does not know (`OrdStatus` 8, `OrdRejReason` 5) that was never
  acknowledged becomes `Reject`. Reports resent later are skipped by ExecID and
  CumQty. CL (2025-OCT-1): ERs for orders of earlier sessions are delivered to
  the current session.
- **Cancel on disconnect**: 8013=Y by default (OE5); "orders that were sent ...
  but not yet acknowledged ... are not guaranteed to be canceled" (OE5,
  OrderMassCancelRequest caution).
- **Risk**: Coinbase checks funds (cancel reason 105, insufficient funds), price
  protection (104) and the product's state (cancel reasons, https://docs.cdp.coinbase.com/exchange/websocket-feed/channels, "Cancel Reason"). The OM checks
  `max_order_qty`, `max_order_notional` and `max_orders_per_sec` before sending.
- **Never** a new order resent after a timeout.

## 9. Pitfalls (`pitfalls.md` sections 1, 2, 3 and 8)

- Documents: OE5 and the drop-copy page repeat the ExecutionReport table; they
  agree. DICT lags OE5: its FIXT11 dictionary does not define `RawDataLength`
  (95), which OE5 requires on the Logon (section 10); its FIX50 file declares
  `servicepack='0'` while the Logon announces SP2 (9). `TradeID` (1003) and
  `OrderStatusRequest` are in DICT.
- Errata: CL is the errata list (above).
- Enumerations: ExecType and OrdStatus values are in the ExecutionReport table
  (OE5).
- Names that differ: OE5's `SelfTradeType` is `SelfTradePrevention` in the
  FIX 4.2 dictionary; same tag 7928.
- Examples against tables: the one wire example (CL, 2025-SEP-3, an RFQ fill)
  has a BodyLength of 440 and a CheckSum of 204 that do not match its content
  (344, 244); `LastQty` (32) is typed `Char` in the OE5 table and holds a decimal.
- Two layers: session messages (A, 0, 1, 2, 3, 4, 5) come from QuickFIX; the OM
  sees only application messages and session Rejects.
- Repeating groups: `NoMiscFees` (136) on fills; read through.
- Timestamps: `SendingTime` milliseconds outbound (exactly three digits);
  inbound `SendingTime` with 6 digits seen in the CL example; `TransactTime`
  parsed with any number of digits.
- Sequencing: client numbering always resets after a disconnect (OE5); the OM
  logs on with reset and reconciles by status (section 8).
- Client ids: UUIDv4 per request; never reused (random 122 bits, map checked).
- Cancel/replace: not used by the OM; OE5 says priority is kept on a size
  decrease (relevant to the market-data handler).
- Fill before ack, racing a cancel, after the cancel ack: tested.
- Unsolicited cancels (STP, cancel on disconnect, expiry): reach the SOM.
- Busts and corrections: OE5 lists no bust or correction ExecType for order
  entry; Restated (D) is not a fill.
- Busy: not documented (above).
- Reconnect: section 8.
- Orders placed outside the OM: their reports are logged and ignored
  (ClOrdID unknown).
- Short sales: not applicable to spot; refused locally if marked.

## 10. Ambiguities

| Question | Reading taken | Alternatives | Resolved by |
|---|---|---|---|
| DICT's FIXT11 does not define RawDataLength (95), OE5 requires it | Send 95 (OE5 and its Python example do) | Leave it out | Sandbox logon (step 3). Note: a QuickFIX venue that validated with DICT as published would reject the Logon (`test_qf_session.cpp` patches its venue's copy) |
| SOM DAY on a 24/7 venue with no DAY | GTC (`day_tif = gtc`) | GTD to next 00:00 UTC (`gtd`); refuse (`reject`) | **Owner decision** |
| Resume with `141=N` and ResendRequest, or reset and reconcile with OrderStatusRequest | Reset and reconcile; missed fills are reported at an AvgPx-derived price and logged as errors | Resend (exact fills, PossDup), as OE5 describes | **Owner decision**; with the drop copy and REST `/fills` available, reconciliation by fills is also possible |
| Is `ClOrdID` unique per open order only, or forever? | Never reused | | OE5 says open orders; the OM is stricter |
| QuickFIX checks the certificate chain but not the host name, and sends no SNI | Accepted for the sandbox; chain checked | stunnel with host verification in front of a plain initiator | **Owner decision** before production |
| `cancel_on_disconnect` Y (profile) or S (session) | Y, Coinbase's recommendation | S if other sessions share the profile | **Owner decision** |
| Units: SOM size unit = `base_increment` (1e-8 BTC); `BrokerOrderBook` holds sizes in `int`, so an order is at most 21.47 BTC | `qty_unit` = base_increment | A coarser unit (e.g. 1e-6) | **Owner decision** per product |
| Replace (35=G) keeps priority on a size decrease | Not used; SOM cancel/replace refused | Implement `request_replace` | Owner |
