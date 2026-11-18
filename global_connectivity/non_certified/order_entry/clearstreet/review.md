# Clear Street order manager: review list

Built from section 10 of `feed.md`. G is Clear Street's public FIX API guide
(retrieved 2026-11-18, no version number; FIX API released 2026-09-29), CL its
changelog. Nothing here was checked against Clear Street: there is no account,
no session and no published test environment (README, Live check).

## Needs your decision

### 1. Short sales

**Question.** G accepts only 54=1 and 54=2 and calls OpenClose (77) advisory;
CL 2026-10-10 (BREAKING) says 77 is now checked and an order whose 77
disagrees with Clear Street's determination is rejected (39=8). The SOM
distinguishes sell, sell short and sell short exempt.

**Options.**

- (a) Sell short: 54=2 + 77=O; short exempt and locate ids refused locally.
  (Implemented, `mark_short_open = yes`.)
- (b) Sell short: 54=2 without 77; Clear Street decides.
- (c) Refuse every SOM short sale locally (`mark_short_open = no`).

**Recommendation.** (a). Under (b) a sale the SOM meant as a short could be
treated as closing a long and vice versa, with no error. (a) turns a
disagreement into a reject the SOM sees.

**What depends on it.** Whether the SOM's view of short positions matches
Clear Street's.

### 2. Reject after acceptance

**Question.** G lists "routing, or the market" among the causes of 150=8,
which suggests 150=8 can follow 150=0 for the same order.

**Reading taken.** A 150=8 on an accepted order ends it with `CancAck` (the SOM
already has the `Ack`; `BrokerOrderBook` returns `Invalid` for a reject after
acceptance). Any CumQty in the report is applied first.

**Recommendation.** Keep, unless the SOM handles a `Reject` after an `Ack`.

### 3. Session schedule

**Question.** G: "Sunday 8 pm EST to Friday 8 pm EST". EST all year, or US
Eastern time?

**Reading taken.** UTC schedule Monday 01:00 to Saturday 00:00 (EST), in the
config.

**Recommendation.** Ask Clear Street at provisioning; if Eastern, set
QuickFIX's `TimeZone` (`qf.TimeZone = America/New_York`) and the local times.

### 4. ClOrdID across days

**Question.** G requires ClOrdIDs never to be reused, on any day. The OM's
ClOrdID is `<prefix><YYYYMMDD>-<client id>` with the date the OM started. A GTC
order placed on one day and still live after a restart on a later day is not
recognised (its reports are logged as unknown), because the book starts
empty.

**Options.** (a) Keep: run GTC orders only within one OM lifetime (implemented).
(b) Refuse GTC locally. (c) Persist the book.

**Recommendation.** (b) until a restart path exists; the SOM has no way to
re-adopt an order after a restart anyway.

## Settled

| Item | How it was settled |
|---|---|
| Credentials | G Logon: 553 = SenderCompID, 554 = API key; added in `toAdmin`, key read from `key_file`; 554 sent and checked by the test acceptor (test `QfSessionTest.LogsOnWithCredentialsOrdersAndFills`); 554 redacted in the FIX log and store by `redact` (test `CsFix.RedactsTheApiKey`; the wrappers themselves are not tested) |
| Unauthorized account | G tag 1: Reject (35=3), matched by RefSeqNum to the order (tests `SessionRejectOfAnOrderRejectsIt`, `QfSessionTest.SessionRejectIsMatchedToTheOrder`) |
| Cancel identifies by the current ClOrdID | G Canceling an order: 41 = the current id |
| Replace may change only size, price, stop price | G Replacing an order |
| Unsolicited cancel | G 150=4 with 41 absent |
| Bust/correction | G: ExecTransType always 0; anything else logged, not applied |
| Order status requests | G FAQ: not supported; recovery is FIX resend only |
| Sequence numbers | G: journaled across disconnects; no reset on logon |
| Order types, TIF, strategies, extended hours | G matrices, applied locally (test `UnsupportedCombinationsAreRefusedLocally`, `CsFix.GuideMatricesInTheSimulator`) |

## Low impact

| Item | Why |
|---|---|
| Account list in Logon 58 (`feed.md` 10.2) | Only an error log when the account is missing; Clear Street rejects the orders anyway. |
| 150=A then 150=0 | The `Ack` is sent once. |
| Done for day then Canceled | The second report is `Late` and logged. |
| TWAP/VWAP window (168/126) | Not sent; Clear Street's defaults (arrival to close) apply. |
| Fractional shares | Refused; the SOM trades whole shares. |
| OrderID is a string | `xordid` is its hash; the string is logged. |
| No FIX rate limit published | Local `max_orders_per_sec`. |
