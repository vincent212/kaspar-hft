# LMAX Broker Trading order manager: review list

Built from section 11 of `feed.md`. TR is LMAX's Broker Trading `fix-spec.json`
(FIX.4.4 version, release 370, 2023-12-02). Nothing here was checked against
LMAX: there was no demo key file (README, Live check).

## Needs your decision

### 1. Recovery after a disconnect

**Question.** TR offers FIX session recovery (the last 512 messages, sequence
numbers kept for the week, no failover between gateways) and Application
Recovery (OrderStatusRequest, TradeCaptureReportRequest), and says it "may be
simpler to ignore FIX session recovery entirely" and log on with 141=Y every
time (TR prologue, Recovery).

**Options.**

- (a) Both: keep sequence numbers in a FileStore (`reset_on_logon = false`) and
  send an OrderStatusRequest for every live order after each logon.
  (Implemented.)
- (b) Application Recovery only: `reset_on_logon = true`, OrderStatusRequest per
  live order, and the post-trade client's TradeCaptureReportRequest for fills of
  orders that ended while disconnected.

**Recommendation.** (a). The resend delivers the missed reports themselves
(cancels, fills with their prices), the status requests close what the resend
cannot, and fills are counted from CumQty so the two never double count (test
`ReconnectAsksForStatusAndDeliversAMissedFillOnce`). The cost is a store
directory that must be kept for the week. Under (b) an order that filled and
ended while disconnected is reported only through OrderStatus, priced from
AvgPx.

**What depends on it.** Whether fills of orders that end during a disconnect
reach the SOM with their own prices.

### 2. Cancel on disconnect

**Question.** FIX orders are cancelled by LMAX on logout or disconnect unless
ExecInst is H (TR prologue, Cancel on Disconnect). Users trading several
accounts must always send H.

**Options.** (a) LMAX's default, no ExecInst (implemented,
`reinstate_on_disconnect = false`); (b) H on every order.

**Recommendation.** (a): a lost connection then leaves no orders working that
the OM cannot manage. Use (b) only if the demo user is set up for several
accounts.

**What depends on it.** What is working at LMAX while the OM is disconnected.

### 3. Counting fills

**Question.** Each Trade report has LastQty and CumQty (TR 32, 14). Resends after
a reconnect, and OrderStatus answers, repeat fills already applied.

**Options.**

- (a) A fill is the increase of CumQty over what has been applied; fills learnt
  from OrderStatus are priced (AvgPx x CumQty - notional applied) / quantity.
  ExecIDs are also remembered. (Implemented.)
- (b) Add LastQty, dropping repeated ExecIDs only.

**Recommendation.** (a). (b) double counts a fill reported both by an
OrderStatus answer and by its own resent Trade report.

**What depends on it.** Position correctness after reconnects.

### 4. Reports TR does not describe precisely

**Question.** (i) Whether an IOC or FOK that does not fill is reported with
ExecType 4 or C; (ii) what LMAX answers to an OrderStatusRequest for a ClOrdID it
does not know; (iii) what ExecID an OrderStatus report carries.

**Readings taken.** (i) both mean cancelled (`CancAck`); (ii) OrdStatus 8 on an
order the OM has not seen accepted means rejected, otherwise logged; (iii)
OrderStatus reports are not deduplicated by ExecID.

**Recommendation.** Keep, and look at the IOC step of the live check, which
prints every report.

### 5. SOM messages for a replace

**Question.** The SOM sends a replace as a new `Order` with `oid_to_cancel`.

**Reading taken.** On Replaced: `CancAck(old oid)` then `Ack(new oid)`, as
`ttom/` TTFIXOM does; later reports go to the new oid. On a replace reject
(OrderCancelReject 434=2): `CancReject(old oid, reason)` and `Reject(new oid)`.

**Recommendation.** Confirm this is what the SOM expects for a rejected replace;
TTFIXOM's handling of that case was not traced.

### 6. Quantity unit

**Question.** OrderQty is "Quantity of contracts ordered" (TR 38); a spot FX
contract is 10,000 base currency, minimum 0.1 (CS pp. 1-3).

**Reading taken.** One Kaspar unit = 0.1 contract (the instrument file's
`chunk`), so OrderQty = units x 0.1.

**Recommendation.** Confirm with the live check's fill (one unit should fill
0.1, i.e. 1,000 EUR).

### 7. Market orders

**Question.** TR allows OrdType 1 with any TimeInForce and gives no rule.

**Reading taken.** Sent with the SOM's TimeInForce.

**Recommendation.** Refuse market orders other than IOC and FOK locally until LMAX
confirms what a DAY market order does.

## Settled

| Item | How it was settled |
|---|---|
| ClOrdID length | TR 11: at most 20 characters; the OM's ids have at most 19 digits |
| Replace fields | TR G: Price and OrdType required; the OM refuses replaces of non-limit orders |
| Stop and stop-limit orders | TR 40 values 3 and 4, StopPx 99 |
| Cancel on disconnect is per order | TR prologue and changelog release 95 |
| Password on disk | QuickFIX's FileStore and FileLog would hold the Logon's 554; both wrapped to redact it (test `PasswordIsNotWrittenToTheStoreOrTheLog`) |
| Demo host | CG "FIAT Demo": `fix-order.london-demo.lmax.com:443` |

## Low impact

| Item | Why |
|---|---|
| CumQty on a Replaced report (`feed.md` section 11, item 8) | Read as cumulative for the order across the replace, as FIX 4.4 defines it, and applied like any other CumQty; if LMAX resets it on replace, the next fill would be undercounted by the earlier fills. The live check fills only an unreplaced order, so it does not test this. |
| OrderID is text | Sent to the SOM as a number when numeric, else hashed; the text is logged |
| TR's FIX.4.2 version (MassQuote, SecurityDefinitionRequest) | Market-maker messages; not used |
| Commission fields (12, 13, 479) | Parsed and kept in the report; not used by the SOM |
| SettlDate (64) on ExecutionReport | Must be enabled by LMAX (TR changelog release 120); passed to post-trade when present |
