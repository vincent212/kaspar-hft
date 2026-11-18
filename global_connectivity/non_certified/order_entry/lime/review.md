# Lime C++ Trading API order manager: review list

Built from section 10 of `feed.md`. TO is the Lime C++ Trading Api Overview,
v1.78.0, 3/8/2022; FM is the Lime Trading FIX Manual, v6.2.5, 02/27/2024.
Nothing here was checked against Lime: the SDK is not public and there is no
account (README).

## Needs your decision

### 1. Busts and corrections

**Question.** TO 5 reports busts and corrections through `onOrderFill` with
`execType` Bust or Correction and the original execution's `execId`. FM 3.6 says
Lime sends a correction as a bust of the original trade followed by a standard
execution. TO does not say whether a Correction carries the corrected
quantity and price.

**Options.**

- (a) Never apply a Bust or a Correction; log it as an error for a person to
  check; apply a following Fill normally. (Implemented.)
- (b) Reverse a Bust in the SOM (a negative fill) and apply a Correction as a
  replacement fill.

**Recommendation.** (a) until a real report has been seen. Under (b) a
correction sent both as Bust + Fill (FM) and as Correction (TO) would be
counted twice.

**What depends on it.** The SOM's position after a bust: under (a) it is wrong
until a person corrects it.

### 2. Cancel on disconnect

**Question.** `cancelAllOnDisconnect` is a constructor argument (TO 3).

**Options.** (a) true (implemented default); (b) false.

**Recommendation.** (a): while disconnected the OM cannot cancel, and the
SOM's orders keep their state until the replay.

**What depends on it.** What is working at Lime while the OM is disconnected.
Feed A7: whether these cancels are replayed to the next session is not
documented.

### 3. Login failure

**Question.** `onLoginFailed` (TO 3) does not say whether the failure can be
transient.

**Options.** (a) stay down until restart (implemented); (b) retry with
backoff.

**Recommendation.** (a): repeated logins with a wrong password may lock the
user.

### 4. Short sales without a locate

**Question.** The C++ `placeOrder` has `sideSellShort` and
`sideSellShortExempt` but no locate field; FM has LocateReqd (114) and
ShortSaleAffirm (9009) for FIX.

**Options.** (a) send short sales without a locate, refuse an order that names
one (implemented, `allow_short = true`); (b) refuse all short sales
(`allow_short = false`).

**Recommendation.** Ask Lime how locates are given for API accounts before
enabling short sales.

### 5. Market orders

**Options.** (a) refused unless `allow_market` (implemented, default false);
(b) always allowed (price 0, TO 4.1).

## Settled from the documents

- Client id: `uint64`, unique per account per day (TO 4.1); the book's
  clock-based id.
- Cancel by the order's own id (TO 4.5).
- Replay from the highest eventId applied + 1 (TO 3); 0 continues without
  replay.
- Busy: refuse new orders with `THROTTLE` until `onConnectionAvailable` (TO 3,
  4).
- Session destroyed and recreated on the actor's thread, never from a callback
  (TO 2); exceptions drop the session (TO 6).
- Time in force: DAY, IOC, OPG, AT_THE_CLOSE, EXTENDED_DAY sent; GTC and FOK do
  not exist in the API (TO 4.1).
- Stop orders do not exist in the API (TO 4.1).
- POV and SCALEPOV need MaxParticipation (FM 9103), absent from
  `AlgoOrderProperties` (TO 4.3); refused by config.

## Low impact

- `FillInfo::side` ("reversed side", TO 5) is ignored.
- `onOrderCancel` with and without a reason both give `CancAck`.
- `onManualOrder` is logged, not tracked.
- `leftQty` on a fill is compared with the book's leaves; a difference is
  logged.
