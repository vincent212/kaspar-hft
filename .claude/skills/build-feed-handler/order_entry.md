# Order entry: broker and exchange order managers

An order manager (OM) connects the SOM to one venue or broker. It registers with
the SOM as the exchange manager for its venue, turns the SOM's `Order` and `Cancel`
into the venue's requests, and turns the venue's reports into `Ack`, `Fill`,
`Reject`, `CancAck` and `CancReject` for the order's sender. Unlike a market-data or
post-trade handler, an OM sends orders that trade real money; the rules at the end
of this file apply to every step.

Templates:

| Source | What it shows |
|---|---|
| `frame/som/BrokerOrderBook.hpp` | the bookkeeping every OM needs; use it, do not write another |
| `frame/som/TimedRunner.hpp` | the broker's thread in a simulated broker |
| `ttom/` (`TTFixClient`, `TTFIXOM`) | a FIX OM over QuickFIX in kaspar-hft |
| kaspar-equities `csom/` (private) | `ClearStreetOM`: FIX 4.2 over QuickFIX, a simulated FIX gateway (`SimClearStreet`), 19 actor tests |
| kaspar-equities `lime/` (private) | `LimeOM`: a vendor C++ SDK behind an abstract session interface, `SimLime`, `FakeLime`, 21 actor tests |
| m2 Fenics and Dealerweb `OrderManager` (private) | binary order entry (OUCH-style) from field tables, with the session layer and the order layer in separate sections |

## The contract with the SOM

| Direction | Message | Meaning |
|---|---|---|
| OM → SOM, on `Start` | `SetExchManager(venue, this)` | the SOM forwards orders for `venue` to this OM |
| SOM → OM | `Order` | new order: `oid`, `sym`, `side`, `sz`, `px` (ticks), `venue`, `owner` |
| SOM → OM | `Cancel` | cancel order `id` (whole order; partial cancels are not supported) |
| OM → sender | `Ack(oid, xordid)` | accepted by the venue; `xordid` is the venue's order id |
| OM → sender | `Fill(...)` | one execution: quantity, price, leaves, venue order id, time |
| OM → sender | `Reject(oid, reason)` | the new order was refused (by the venue, or locally: `DISCONNECTED`, `NOTLOGGEDIN`, `THROTTLE`, `OVERLIMIT`) |
| OM → sender | `CancAck(oid)` | the order is cancelled, by request or unsolicited |
| OM → sender | `CancReject(oid, reason)` | the cancel was refused; the order is still working |

Every order ends in exactly one of `Fill` (the last), `Reject` or `CancAck`. A late
report after that is logged, never forwarded.

## Structure

| Part | Responsibility |
|---|---|
| session | the venue connection: a QuickFIX `Application`, a vendor SDK, or a TCP session over generated messages (`binary_codegen.md`). It runs on its own thread, parses each report into a plain message (`CSReport`, `LimeReport`) and `send`s it to the actor. It never touches order state. |
| session interface | for an SDK, an abstract class with the calls the OM makes (login, place, cancel) and the events it receives (accepted, filled, canceled, rejected, cancel rejected, busy, available, connection lost). The real SDK, the simulator and the test fake each implement it. |
| actor | owns the session and a `BrokerOrderBook`; handles `Start`, `Shutdown`, `Order`, `Cancel` and the report messages; every state change happens on its thread |
| `BrokerOrderBook` | client ids, the map from every id an order used to its SOM oid, the state machine (PendingNew, Working, PendingCancel, PendingReplace, Filled, Canceled, Rejected), quantities; returns an `Outcome` (Ok, Unknown, Duplicate, Late, Busy, Invalid) for each event |
| simulator | an in-process venue for `--sim` runs and demos: acks, fills after a delay, cancels, rejects, driven by a `TimedRunner` |
| factory | `create_<Venue>OM(config, som, venue)` returns `nullptr` with the reason on stderr for a bad config, so one broken venue does not stop the process |
| config | session file or SDK settings; credentials supplied at deployment, never in the repository |

The actor translates each `BrokerOrderBook` result into SOM messages:

| Event | Book call | On `Ok` |
|---|---|---|
| `Order` | `add`, then send; on a synchronous refusal `send_failed` | (request sent) |
| `Cancel` | `request_cancel`, then send; on refusal `send_failed` | (request sent) |
| accepted | `on_accepted(cid, xordid)` | `Ack` |
| fill | `on_fill(cid, qty)` | `Fill` |
| canceled | `on_canceled(cid)` | `CancAck` |
| cancel refused | `on_cancel_rejected(cid)` | `CancReject` |
| rejected | `on_rejected(cid)` | `Reject` |

`Unknown`, `Late`, `Busy` and `Invalid` are logged with the report; a fill that comes
back `Unknown` is logged as an error for a person to check, since it is a position
the SOM does not know about.

## The feed description for order entry

Sections 1 to 3 of `feed_template.md` apply. In place of the market-data sections:

- **Logon and session:** credentials and where they go; account selection; sequence
  numbers and their reset; heartbeats; whether orders are accepted before a
  trading-session status message; cancel-on-disconnect.
- **Order identifiers:** what the client id must satisfy (uniqueness per day, per
  session or forever; numeric or text; maximum length; increasing or not); whether
  cancel and replace take a new client id (FIX: yes) or reuse the order's own (some
  SDKs); which id each report carries.
- **New order:** every field and value the venue accepts: side (buy, sell, sell
  short, sell short exempt), order type, price format, time in force (day, IOC,
  FOK, at the open, at the close, extended hours), route or destination, broker
  algorithms (VWAP, TWAP and their parameters), account. Which combinations the OM
  supports, and that it refuses the others locally with a `Reject`.
- **Reports:** for each venue report (FIX `ExecutionReport` by `ExecType` and
  `OrdStatus`, `OrderCancelReject`, `BusinessMessageReject`; or the SDK's callbacks),
  the book call and SOM message it maps to. Partial fills; fills that arrive before
  the ack; fills that race a cancel; unsolicited cancels; busts and corrections.
- **Throttles and busy states:** message-rate limits; what the API returns when
  busy; whether a refused request is retried or rejected.
- **Reconnect and replay:** what the venue resends after a reconnect, and how the OM
  avoids applying a report twice (sequence numbers, event ids, `PossDupFlag`).
- **Risk checks:** limits enforced by the venue or broker, and those the OM or the
  SOM must enforce before sending.

## Client ids the venue constrains

`BrokerOrderBook` issues 63-bit numeric client ids (`ClientIdGen`: milliseconds
since midnight and a counter), unique for the day across restarts. Where the
venue's client id has another form, the OM allocates the venue's id for each
`ClientId` and keeps the map both ways, so that every report finds its order:

- a string id (Coinbase's `client_order_id`, a UUIDv4): a new UUID for each request
  that takes a client id, mapped to the `ClientId` the book issued for it;
- a narrower number that must rise through the day (OUCH's `UserRefNum`, 32 bits,
  each one greater than every earlier one that day): a counter kept by the OM, written
  to a file before each order is sent and read back on start, so that a restart
  continues above the last id used that day.

The map is cleared at the venue's day boundary, not on reconnect: reports for
earlier orders arrive after a reconnect.

## FIX sessions

`post_trade.md` applies to an OM over FIX as to a post-trade client: validate the
venue's dictionary against its specification, keep the Logon password out of
QuickFIX's log and store, and put TLS that needs SNI or host-name checks in a
proxy.

## Tests

Every OM gets actor tests against a fake venue, run synchronously, in the form of
kaspar-equities' `test_clearstreet_om.cpp` and `test_lime_om.cpp`. The list below,
taken from those two files, is the minimum.

- logs in and registers with the SOM; a bad config gives `nullptr`, not a crash;
  orders before login, and while disconnected, are rejected locally
- the fields of each order type, side, time in force, route and algorithm as sent;
  unsupported combinations, including short sales the OM cannot mark, refused
  locally
- ack carries the venue order id; partial then full fill; reject
- cancel round trip, using the id the venue requires; cancel reject leaves the order
  working; cancel of an unknown or filled order
- unsolicited cancel reaches the SOM
- fill before ack; fill racing a cancel; fill after the cancel ack still counts
- a bust or correction is not a fill
- busy connection: requests are throttled until the venue is available again
- reconnect resumes and delivers a missed fill exactly once; replayed events are not
  applied twice
- unknown reports and orders placed outside the OM are logged and ignored
- client ids are unique and increasing, and a restart does not reuse them
- shutdown with a report in flight completes

`BrokerOrderBook`'s own tests are in `unit_test/src/test_broker_order_book.cpp`.

## Rules

- Never send an order to a production session. Tests run against the fake and the
  simulator; live checks run only in the venue's UAT or certification environment,
  with credentials the user supplies for it, and only after the user asks for them.
- The live check is: logon, one order far from the market and its cancel, logout.
  Nothing that can fill.
- Never retry a new order automatically after a timeout: the first may have reached
  the venue. Report it and let the SOM decide.
- A report the OM cannot match to an order is logged as an error, never dropped
  silently and never guessed.
- Credentials are read from deployment files outside the repository.
