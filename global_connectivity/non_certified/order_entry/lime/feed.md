# Feed description: Lime (Lime Trading Corp.) C++ Trading API, order entry

Not certified. Never connected to Lime. Citations use the short names below,
with section and page of the PDF.

## 1. Venue and documents

Lime Trading Corp. ("Lime Direct") is a US broker-dealer (FINRA, SIPC). Its
developer site, https://docs.lime.co, publishes the documents below. All were
fetched on 2026-11-18.

| Short name | Document | Version | Date | Used for |
|---|---|---|---|---|
| TO | Lime C++ Trading Api Overview, https://docs.lime.co/cpp/LimeTradingApiOverview.pdf (38 pages) | 1.78.0 | 3/8/2022 | everything about the C++ API: constructor, calls, callbacks, exceptions |
| FM | Lime Trading FIX Manual (TS3 Trading System FIX Specification), https://docs.lime.co/fix/LimeFIXManual.pdf | 6.2.5 | 02/27/2024 | route values (appendix C), symbology (appendix A), hours (3.5), breaks and corrections (3.6), short sale fields (9009, 9067, 114), LSS algo strategies (7.1.25) |
| TS | TS+ Trading System Guide, https://docs.lime.co/binary/LimeTSPlusGuide.pdf | 1.8.0 | 06/06/2022 | not used (binary protocol, another platform) |
| PY | Lime Direct Python SDK pages (session, trading, callbacks), docs.lime.co | web, no version | fetched 2026-11-18 | ExecType values (0=Fill, 1=Bust, 2=Correction), which TO does not give |
| JV | Lime Direct Java API javadoc, https://docs.lime.co/Java/index.html (`Callback`, `Client`, options classes) | web, no version | fetched 2026-11-18 | wording of the callbacks (onOrderReject reasons are for people; onCorrect) |
| CS, RS | .NET and REST pages | web | fetched 2026-11-18 | not used (other APIs) |

What is public and what is not:

- Public: TO describes the whole C++ API surface the OM needs (constructor,
  `placeOrder`, `placeAlgoOrder`, `cancelOrder`, `cancelAllOpenOrders`, every
  callback and its arguments, `CallStatus`, eventId replay, exceptions).
- Not public: the SDK itself (headers and library, "packaged as a binary and a
  set of header files", TO 2), the example client (TO appendix A), the test
  environment, hostnames and accounts. The C++ index page,
  https://docs.lime.co/cpp, answered "Page Not Found"; only the PDF is
  reachable.
- Therefore: header file names, the `ExecType` enumerator names, the values of
  `marketPrice` and `samePrice`, and the exact declarations of `CpuSet`,
  `AllowRouting` and the property classes are not known. `sdk_doc/LimeTradingApiDoc.hh`
  transcribes TO's declarations and marks every guess `GUESS`. The real session
  (`src/SdkSession.cpp`) compiles against it; it has never compiled against the
  vendor's header.

Which document applies: the C++ API is a client of the TS3 trading platform,
the same platform FM describes (both are titled "TS3 Trading System"). TO
footnote 3 sends route values to FM appendix C, and TO appendix B maps API
fields to FIX tags, so FM is used for the platform's semantics where TO is
silent. TS describes TS+, a different platform; it is not used.

## 2. Transport

| Item | Value | Source |
|---|---|---|
| Library | C++, 64-bit, static (.a) and shared (.so), namespace `LimeBrokerage` | TO 2 |
| Connection | made by the `TradingApi` constructor to `hostname` (DNS name or IP; default `nullHostname`: the platform on the same host) | TO 3 |
| Transport | `transportTypeTcp` (default) or `transportTypeInfiniband` | TO 3 |
| Threads | one callback thread, the only thread the API creates; calling the API from a callback is prohibited, including the destructor; the object must be destroyed on the thread that constructed it | TO 2 |
| Calls | asynchronous; a call returns once the request is handed to the platform or refused | TO 2, 4 |
| Hours | login from 05:50 ET; regular hours 06:00 to 17:30 ET; Lime disconnects all sessions after 17:30; no overnight connections (stated for FIX) | FM 3.5 |
| Accounts | one `TradingApi` instance trades one account | TO 2 |

## 3. API surface

The OM uses these calls and callbacks; the others are listed so that the
choice is visible.

| Call | Used | Source |
|---|---|---|
| `TradingApi(listener, account, user, password, eventId, cancelAllOnDisconnect, hostname, transportType, callbackCpuset)` | yes | TO 3 |
| `placeOrder(orderId, route, symbol, side, quantity, price, OrderProperties)` | yes | TO 4.1 |
| `placeAlgoOrder(orderId, route, symbol, side, quantity, strategy, price, AlgoOrderProperties)` | yes, when the SOM order names a strategy | TO 4.3 |
| `cancelOrder(orderId)` | yes | TO 4.5 |
| `cancelAllOpenOrders()` | in the session interface; not called by the OM | TO 4.11 |
| `partialCancelOrder`, `cancelReplaceOrder`, `cancelReplaceAlgoOrder` | no (the SOM sends whole cancels; replace is refused) | TO 4.6, 4.7, 4.9 |
| options calls (`placeUSOptionsOrder`, ...) | no | TO 4.2, 4.4, 4.8 |
| `setCallbackCpuAffinity` | no | TO 3 |

| Callback | Required | Source |
|---|---|---|
| `onLoginAccepted()`, `onLoginAccepted(eventId)` | optional | TO 3 |
| `onLoginFailed(reason)`, `onConnectionFailed(reason)` | pure virtual | TO 3 |
| `onConnectionBusy()`, `onConnectionAvailable()` | optional | TO 3 |
| `onOrderAccept(orderId, limeOrderId, OrderAckAttributes, eventId)` | pure virtual | TO 5 |
| `onOrderFill(orderId, FillInfo, eventId)` | pure virtual | TO 5 |
| `onOrderCancel(orderId, eventId)` and `onOrderCancel(orderId, reason, eventId)` | pure virtual | TO 5 |
| `onOrderPartialCancel(orderId, leftQty, eventId)` | pure virtual | TO 5 |
| `onOrderReplace(orderId, replaceOrderId, limeReplaceOrderId, OrderAckAttributes, eventId)` | pure virtual | TO 5 |
| `onOrderReject(orderId, reason, eventId)` | pure virtual | TO 5 |
| `onOrderCancelReject(orderId, reason, eventId)` | optional; "venue dependant" | TO 3, 5 |
| `onOrderCancelReplaceReject(orderId, replaceOrderId, reason, eventId)` | optional | TO 5 |
| `onManualOrder`, `onManualOrderReplace` | optional; account must be configured | TO 5 |
| `onUSOptionsOrderFill`, `onManualUSOptionsOrder` | options; not used | TO 5 |

`FillInfo` (TO 5): `symbol`, `side`, `lastShares`, `lastPrice` (scaled),
`leftQty`, `liquidity`, `transactTime` (ms since the Unix epoch, market
timestamp), `execType` (Fill, Bust, Correction), `execId` (unique, Lime
generated; for a bust or correction, the id of the original execution),
`contraBroker`, `lastMarket`, `clientData1..3`, `marketLiquidity`,
`transactTimeNanos`.

## 4. Logon and session

- Credentials: `account`, `user`, `password` are constructor arguments (TO 3).
  The OM reads them from a credentials file named in its config, outside the
  repository.
- Login result: `onLoginAccepted` (trading may start only after it) or
  `onLoginFailed`; a connection that cannot be made gives
  `onConnectionFailed`. TO 3 also says "onLoginRejected" once, in the same
  paragraph; the declared callback is `onLoginFailed` (ambiguity A5).
- Session: "A single session is maintained for each account during a trading
  day. Every callback contains an eventID that uniquely identifies it throughout
  the day", starting at 1, increasing, with gaps (heartbeats consume eventIds)
  (TO 3). Exceptions: `onLoginAccepted` and rejects generated inside the API
  carry no eventId (TO 3 footnote 2).
- Heartbeats and sequence numbers: handled inside the API; not visible.
- Trading-session status: none; orders may be sent after `onLoginAccepted`.
- Cancel on disconnect: the `cancelAllOnDisconnect` constructor flag "indicates
  whether all open orders on the account should be canceled when the connection
  goes down" (TO 3). The OM sets it from config, default true.
- Connection failure: the instance is "deactivated"; a new instance must be
  constructed (TO 3).
- Exceptions: the constructor may throw (version incompatibility, connectivity,
  invalid options); trading calls may throw on internal errors; never from the
  callback thread; after an exception the instance should be deleted (TO 6).

## 5. Order identifiers

- Client id: `OrderId`, `uint64_t`, "must be unique for a given account
  throughout the day" (TO 4.1). Numeric. No other rule.
- The OM uses `BrokerOrderBook`'s client id (milliseconds since midnight << 36
  | counter, below 2^63). It increases, and a restart the same day starts above
  the last id of the previous process because the clock has advanced.
- Cancel: `cancelOrder(orderId)` names the order by its own id; "The ID for the
  cancel request itself is generated by the Lime trading platform" (TO 4.5). No
  new client id.
- Replace (not used): `cancelReplaceOrder(orderId, replaceOrderId, ...)` takes a
  new client id (TO 4.7); `onOrderReplace` carries both.
- Every order callback carries the client `orderId`. `onOrderAccept` also
  carries `limeOrderId`, the id Lime uses with venues (TO 5); it is the `Ack`'s
  `xordid` and is put on every `Fill`.

## 6. New order

| SOM `Order` | Lime | Source | OM |
|---|---|---|---|
| `side` BUY | `sideBuy` | TO 4.1 | sent |
| `side` SELL, `short_sale` NONE | `sideSell` | TO 4.1 | sent |
| `short_sale` SHORT | `sideSellShort` | TO 4.1 | sent if `allow_short` |
| `short_sale` SHORT_EXEMPT | `sideSellShortExempt` | TO 4.1 | sent if `allow_short` |
| (buy to cover) | `sideBuyToCover` | TO 4.1 | never sent; the SOM has no such side |
| `locate_id` set | no field in the C++ API (FM has LocateReqd 114 for FIX) | TO 4.1, FM 9009 | refused locally |
| `ord_type` LIMIT | price > 0, scaled by 10,000 | TO 4.1 | sent |
| `ord_type` MARKET | price 0 (`marketPrice`) | TO 4.1 | sent only if `allow_market` |
| `ord_type` STOP, STOP_LIMIT | no stop type in `placeOrder` | TO 4.1 | refused locally |
| `tif` DAY | `timeInForceDay` | TO 4.1 | sent |
| `tif` IOC | `timeInForceIoc` | TO 4.1 | sent |
| `tif` AT_OPEN | `timeInForceOpg` | TO 4.1 | sent |
| `tif` AT_CLOSE | `timeInForceAtTheClose` | TO 4.1 (algo: `timeinForceAtTheClose`, TO 4.3) | sent |
| `tif` EXTENDED_DAY | `timeInForceExtendedDay` | TO 4.1 | sent |
| `tif` GTC, FOK | none in the enum | TO 4.1 | refused locally |
| `route` | `route` argument, FM appendix C values; empty: `default_route` from config | TO 4.1 fn 3 | sent; no route at all is refused |
| `strategy` | `placeAlgoOrder` `strategy`, on `algo_route` (e.g. LSS) | TO 4.3, FM 7.1.25 | only strategies in `algo_strategies` (default VWAP, TWAP, IS, CLOSE); POV and SCALEPOV need MaxParticipation (FM 9103), which `AlgoOrderProperties` does not have, so config refuses them |
| `sym` | `symbol`, the asset name (FM appendix A symbology) | TO 4.1 | sent |
| `sz` | `int quantity`, whole shares | TO 4.1 | fractional, zero, negative or above INT_MAX refused |
| `px` (ticks) | `ticks * tick_size * 10000`, rounded | TO 4.1 | sent |
| (DVP accounts) | `OrderProperties::saleAffirm` on sells | TO 4.1, FM 9009 | from config |
| (pre-market) | `regularSessionOnly` on DAY orders | TO 4.1 | from config |
| `oid_to_cancel` (replace) | — | | refused locally |

Account: the `TradingApi` instance is bound to one account (TO 2); there is no
per-order account field.

## 7. Reports

| Lime callback | Book call | SOM message |
|---|---|---|
| `onOrderAccept` | `on_accepted(id, limeOrderId)` | `Ack(oid, limeOrderId)`, once |
| `onOrderFill`, execType Fill | `on_fill(id, lastShares)` after the execId check | `Fill` (lastShares, lastPrice, leaves, limeOrderId, transactTimeNanos or transactTime x 10^6) |
| `onOrderFill`, execType Bust or Correction | none | none; logged as an error, "CHECK POSITIONS" |
| `onOrderCancel` (either overload) | `on_canceled(id)` | `CancAck(oid)`, requested or unsolicited |
| `onOrderReject` | `on_rejected(id)` | `Reject(oid)` |
| `onOrderCancelReject` | `on_cancel_rejected(id)` | `CancReject(oid, FROMEXCHANGE)`; the order stays working |
| `onOrderPartialCancel`, `onOrderReplace`, `onOrderCancelReplaceReject` | none (the OM never asks for them) | logged as errors |
| `onManualOrder`, `onManualOrderReplace` | none | logged as warnings; not tracked |

- Fill before ack: `BrokerOrderBook` accepts it; the `Fill` carries no venue id
  and the later accept still sends the `Ack`.
- Fill racing a cancel: the fill is applied while the cancel is pending; a cancel
  after the order filled comes back `Late` and is logged.
- Fill after the cancel ack: applied by the book and forwarded.
- A report the book does not know: logged; a fill on an unknown order is an
  error for a person to check.
- `FillInfo::side` is described as the "reversed side" (TO 5); the OM ignores it
  and uses the order's side (ambiguity A3).

## 8. Throttles, busy states, risk checks

- Busy: when the platform's buffers fill, calls return `statusConnectionBusy`
  and optionally `onConnectionBusy` is called; `onConnectionAvailable` follows
  when the connection accepts messages again. The client "will reduce the rate
  of order submission" (TO 3). A call that does not return `statusSuccess`
  produces no callback (TO 4).
- The OM: on `statusConnectionBusy` or `onConnectionBusy`, new orders are
  rejected with `THROTTLE` until `onConnectionAvailable`; a cancel is still
  attempted, and if it is refused the SOM gets `CancReject(THROTTLE)` and the
  order stays working. Nothing is retried automatically.
- Message-rate limits: none published. The OM has a local token bucket
  (`max_orders_per_sec`, new orders only).
- Risk checks: Lime's own checks are not described for the API beyond "rejected
  by the Lime trading platform or an exchange" (TO 4) and price decimalization
  (FM 3.4). The OM checks `max_order_qty` and `max_order_notional` before
  sending.

## 9. Reconnect and replay

- After `onConnectionFailed` (or an exception), the OM destroys the session on
  its own thread and, after `reconnect_ms`, constructs a new one with
  `eventId = highest eventId applied + 1` (TO 3: "the next value after the
  highest eventID received"). 1 replays the day, 0 continues without replay.
- First session of the process: `first_event_id` from config (default 0). If
  orders were sent but no event was applied yet, the reconnect uses 1.
- Duplicates: an event at or below the highest eventId applied is skipped.
  Fills are also checked against the execIds applied for the order. Events from
  an older session object are dropped.
- `onLoginFailed` is not retried: the venue stays unrouted until restart.

## 10. Ambiguities

| # | Question | Reading implemented | Alternatives |
|---|---|---|---|
| A1 | `ExecType` enumerator names and values in C++ | names guessed (`execTypeFill`, `execTypeBust`, `execTypeCorrection`), values 0, 1, 2 from PY | anything else is a compile error against the real header |
| A2 | A correction: TO 5 has execType Correction with the original execId; FM 3.6 says Lime sends a correction as a bust of the original followed by a standard execution | Bust and Correction are logged, never applied; a following Fill is applied as a fill | apply a Correction as a replacement fill (needs its fields: does lastShares/lastPrice carry the corrected values?); reverse a Bust in the SOM |
| A3 | `FillInfo::side`: "original order value of symbol and reversed side" | ignored; the order's own side is used | — |
| A4 | Two `onOrderCancel` overloads, with and without a reason; which one Lime calls | both map to `CancAck` | — |
| A5 | TO 3 names `onLoginRejected` once; the class declares `onLoginFailed` | `onLoginFailed` | — |
| A6 | Whether `onLoginFailed` is permanent (bad password) or can be transient | permanent until restart | retry with backoff |
| A7 | Whether the orders cancelled by `cancelAllOnDisconnect` are reported as `onOrderCancel` events replayed to the next session | assumed yes (replay from the next eventId) | not reported: orders would stay working in the book |
| A8 | Whether `onLoginAccepted(eventId)` or `onLoginAccepted()` is called, and what its eventId is | either; eventId 0 means "not replayed" | — |
| A9 | Whether the 05:50 to 17:30 ET hours and the nightly disconnect of FM 3.5 also apply to API sessions, and whether eventIds restart each day | assumed yes; the OM keeps eventIds only within a process | — |
| A10 | Short sales without a locate field: how the C++ API learns the locate | short sales sent without one (allowed by config); an order with `locate_id` is refused | refuse all short sales |
| A11 | The values of `marketPrice` and `samePrice` | `marketPrice` = 0 ("Setting price to zero has the same effect", TO 4.1) | — |
| A12 | Whether `cancelOrder` on a busy connection should be retried | refused to the SOM with `THROTTLE`; never retried | queue cancels until available |

## 11. Pitfalls (`pitfalls.md`)

Sections 1 to 3 (specification reading, framing, sequencing) apply only through
the API: there is no wire format to decode. Market-data sections 4 to 6 do not
apply. Section 8:

| Pitfall | Lime | Where handled |
|---|---|---|
| Client id uniqueness | per account per day (TO 4.1) | `BrokerOrderBook` cid from the clock; test `IdsAreUniqueAndIncreasingAcrossRestart` |
| Cancel/replace ids | cancel by the order's own id; replace takes a new id (TO 4.5, 4.7) | cancel only; tests `CancelRoundTripUsesTheOrdersOwnId` |
| Fill before ack, racing a cancel, after the cancel ack | not described; possible with asynchronous callbacks | tests `FillBeforeAck`, `FillRacingCancel`, `FillAfterCancelAckStillCounts` |
| Unsolicited cancels | cancel on disconnect, venue cancels, IOC remainder | `UnsolicitedCancelReachesTheSom` |
| Busts and corrections | execType Bust, Correction (TO 5); FM 3.6 | `BustIsNotAFill`, `CorrectionIsNotAFill`; A2 |
| Busy | `statusConnectionBusy`, `onConnectionBusy`, `onConnectionAvailable` (TO 3) | `BusyConnectionThrottlesUntilAvailable` |
| Reconnect duplicates | eventId replay (TO 3) | `ReplayedEventsAreNotAppliedTwice`, `ReplayOverlapIsSkipped`, `SameExecIdIsAppliedOnce` |
| Orders placed outside the OM | `onManualOrder` (TO 5) | `UnknownReportsAndManualOrdersAreLoggedAndIgnored` |
| Short sales | `sideSellShort`, `sideSellShortExempt`; no locate field | `SidesAndShortSales`, `ShortSalesRefusedWhenDisabled` |
| State cleared on session restart | the API is stateless; the platform keeps state (TO 2) | the book is kept across sessions; only the session object is replaced |
| Calls from callbacks, destruction thread | prohibited (TO 2) | callbacks only post a `LimeEvent`; test `SessionIsCreatedOnTheActorThread` |
