# Feed description: Coinbase Exchange FIX 5.0 drop copy

Post-trade capture for Coinbase Exchange: a FIX 5.0 SP2 drop-copy session that
receives an ExecutionReport for every order of the API key's profile, placed
through any FIX session or REST, turned into the shared
`post_trade::msg::TradeCapture` through `post_trade::TradeLedger`.

Every entry cites its source; pages fetched as Markdown on 2026-11-18.
Abbreviations:

- **DC**: Exchange FIX Drop Copy 5.0, https://docs.cdp.coinbase.com/exchange/fix-api/drop-copy
- **OE5**: Exchange FIX Order Entry Messages 5.0, https://docs.cdp.coinbase.com/exchange/fix-api/order-entry-messages/order-entry-messages5
- **BP**: FIX Best Practices, https://docs.cdp.coinbase.com/exchange/fix-api/best-practices
- **CL**: Exchange Changelog, https://docs.cdp.coinbase.com/exchange/changes/changelog
- **DICT**: `cb_exch_fix_dictionaries-20260612.zip` (order-entry dictionaries,
  in `../../order_entry/coinbase_exchange/fix/`)

## 1. Venue and documents

| Document | Version | Date | Applies to |
|---|---|---|---|
| DC | baseline FIX 5.0 SP2 | fetched 2026-11-18 | Logon, ExecutionReport, admin messages |
| OE5 | baseline FIX 5.0 SP2 | fetched 2026-11-18 | Session rules DC refers to (heartbeat, test request, logout, resend) |
| CL | to 2026-Feb-02 | fetched 2026-11-18 | The dedicated drop-copy fleet (2025-MAY-13); ERs for new and changed orders (2025-OCT-1), for cancels and stop activations (2025-OCT-31) |
| DICT | 2026-06-12 | 2026-06-12 | QuickFIX dictionaries (no drop-copy-specific file exists) |

Two kinds of drop copy exist: the dedicated fleet at `fix-dc` port 6122 (DC;
CL 2025-MAY-13), and `9406=Y` on an order-entry session (BP, "Drop Copy
Session"; "Existing Order Entry drop copy functionality (via tag 9406=Y) will
not change", CL). The client uses the dedicated fleet; its Logon also carries
9406=Y, which DC requires ("Must be `Y` for Drop Copy sessions").

Coinbase sends ExecutionReports, not TradeCaptureReports: there is no
TradeCaptureReportRequest, no request acknowledgement and no report
acknowledgement (DC lists none).

## 2. Transport

- `tcp+ssl://fix-dc.exchange.coinbase.com:6122` and
  `tcp+ssl://fix-dc.sandbox.exchange.coinbase.com:6122` (DC, note; CL). TLS
  probe of the sandbox host: section 2 of the order-entry feed.md.
- One connection per API key; order-entry rate limits do not apply (DC).
- Logged out every Saturday 1 PM ET (DC, warning).

## 3. Framing

FIXT 1.1 / FIX 5.0 SP2 tag=value, as order entry (DC, "Header").

## 4. Logon

Fields as order entry (`../../order_entry/coinbase_exchange/feed.md`, section 4)
with `DropCopyFlag` 9406=Y and no CancelOrdersOnDisconnect; `HeartBtInt` capped
at 300 s (DC, Logon). Credentials: `dc_key`, `dc_secret`, `dc_passphrase` from the
key file when present, else `key`, `secret`, `passphrase`.

## 5. Subscription and replay

None: the session delivers ExecutionReports once logged on (DC). With
`ResetSeqNumFlag` Y, nothing is replayed after a reconnect; OE5 describes a
resend (141=N, ResendRequest in pages of 1,000, 4 hours of history, admin
messages gap-filled, `PossDupFlag` Y on resent messages). The client logs on
with reset (section 10).

## 6. Report lifecycle

ExecutionReport (35=8) fields (DC table, which repeats OE5's): ClOrdID 11,
OrderID 37, OrigClOrdID 41, AvgPx 6, CumQty 14, LeavesQty 151, ExecID 17,
OrdStatus 39, ExecType 150, Symbol 55, Side 54, OrdType 40, LastQty 32, LastPx 31,
Price 44, OrderQty 38, CashOrderQty 152, Text 58, TransactTime 60,
OrdRejReason 103, ExecRestatementReason 378, TradeID 1003, AggressorIndicator
1057, TimeInForce 59, StopPx 99, TriggerPriceDirection 1109, ExecInst 18,
SelfTradeType 7928, ExpireTime 126, VenueType 1430, NoMiscFees 136 group (137,
138, 139, 891).

- ExecType values: 0 New, 4 Canceled, 5 Replaced, 8 Rejected, C Expired,
  D Restated, F Trade, I Order Status (DC). No Trade Cancel (H) or Trade Correct
  (G): Coinbase documents no busts or corrections.
- Stable identifiers: `ExecID` is a UUID per report (OE5, "ID identifying this
  execution report"); `TradeID` (1003) identifies the fill "for reporting"
  (DC); it is per product (`trade_id` on the WebSocket is per product,
  exchange/websocket-feed/channels, "Heartbeat Channel"; per-product numbering of 1003 not stated).

## 7. Mapping to Kaspar (`post_trade::msg::TradeCapture`)

| TradeCapture | From | Notes |
|---|---|---|
| `venue` | `en::x::COINBASEX` | |
| `trade_id` | `Symbol/TradeID/OrderID` | our side of one trade; `ExecID` when TradeID is absent (reports before 2025-10, CL) |
| `report_id` | ExecID (17) | the same on every delivery |
| `report_seq` | 0 | MsgSeqNum restarts with every logon: arrival order is used |
| `event` | ExecType F → New; G → Amend, H → Bust (`ref_trade_id` = `trade_id`) | G and H are not documented; mapped if they come |
| `venue_order_id` | OrderID (37) | |
| `symbol`, `asset` | Symbol (55); asset from the config | |
| `side` | 54: 1 Buy, 2 Sell | |
| `qty` | LastQty (32) in the product's base increments (1e-8 for BTC-USD) | TradeCapture has no quantity scale; feed.md states the unit |
| `price_mantissa`, `price_decimals` | LastPx (31) as sent ("64000.01" → 6400001, 2) | exact |
| `trade_date`, `trade_time_ns` | TransactTime (60), UTC | Coinbase's day is taken as the UTC day |
| `poss_dup` | 43=Y or 97=Y | |
| `raw` | the report, `tag=value|` | carries no credentials |
| `account`, `firm`, `counterparty`, `settle_date`, `yield` | not sent by Coinbase | |

Order events (New, Canceled, Replaced, Rejected, Expired, Restated, Status) are
counted, not forwarded. Duplicates: `TradeLedger` drops a second report with
the same `report_id`.

## 8. Instruments

Product ids; `base_increment` per product from GET /products/<id>, in
`config/cbx_drop_example.ini`.

## 9. Pitfalls (`pitfalls.md` section 7)

- FIX version: 5.0 SP2 over FIXT 1.1; DICT's FIX50 file declares SP0 (order
  entry feed.md, section 9).
- Custom tags: 9406, 8013, 7928, 1003 in DICT; 8152 LeavesFunds and 1430
  VenueType appear in CL/DC and not all in DICT: QuickFIX accepts unknown fields
  (`AllowUnknownMsgFields`).
- Credentials on the Logon (no UserRequest).
- A missing field throws in an `onMessage` handler: the client does not crack
  messages; it converts the whole message to `FixMsg` and never throws in
  `fromApp`.
- Requests before a session status: none are sent.
- Subscription and acknowledgement: none (section 5).
- Lifecycle and identifiers: section 6.
- Duplicates on reconnect: by ExecID in the ledger.
- Price, yield, accrued: price only.
- Daily reset: Saturday logout (DC); the ledger is cleared only by the owner
  (`TradeLedger::clear`) at the start of a trading day, never on a reconnect.

## 10. Ambiguities

| Question | Reading taken | Alternatives | Resolved by |
|---|---|---|---|
| Fills missed while disconnected | Lost to the drop copy (reset on logon); the order manager's status reconciliation and REST `GET /fills` are the backstop | Resend with 141=N (OE5), exact | **Owner decision** |
| Is TradeID unique across products? | Not assumed: the trade id includes the symbol | | |
| The "trading day" of a 24/7 venue | UTC date of TransactTime | Saturday-to-Saturday session (the weekly logout) | **Owner decision** |
| Trade Cancel/Correct ExecTypes | Mapped to Bust/Amend if they come | Treat as unknown | Coinbase documents none |
| Dedicated fleet (fix-dc:6122) or 9406=Y on an order-entry session | Dedicated fleet | 9406 on fix-ord | BP recommends a separate session |
