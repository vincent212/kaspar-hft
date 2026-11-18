# Post-trade feeds: trade capture over FIX

A post-trade feed delivers the firm's executed trades from the venue, independently
of the order-entry session: for reconciliation, position keeping, and straight-through
processing (STP) to clearing and the back office. Most venues deliver it as a FIX
session in which the client requests trade capture reports and the venue sends one
`TradeCaptureReport` (35=AE) per trade, amendment or cancellation. Some venues use
drop copy instead (`ExecutionReport`, 35=8, copies of the order session's fills);
the same structure applies, with the differences in *Drop copies of execution
reports* below.

Templates:

| Source | What it shows |
|---|---|
| `ttom/` (`TTFixClient`, `TTFIXOM`) | a QuickFIX application wrapped in a Kaspar actor: initiator started in `Start`, session callbacks on QuickFIX's thread, results sent as actor messages |
| `global_connectivity/non_certified/post_trade/common/` | the normalized `post_trade::msg::TradeCapture` and `TradeLedger` (idempotent application of reports) |
| `global_connectivity/non_certified/market_data/lmax/` (`FixInitiator`, `RedactLog`) | a QuickFIX initiator with a log and a store that redact the password, TLS or plain TCP |
| m2 `postrade/` (private repository) | trade-capture clients for three venues: `DlrwebSTPClient` and `FenicsSTPClient` (FIX 4.4), `ICAPSTPClient` (FIX 5.0 SP2 over FIXT 1.1); `STPHandler` owns the initiator; `stppubdc2/m2_stp_pub.py` publishes the captured trades |

## Inputs

1. The venue's post-trade or trade-capture specification (rules of engagement).
2. The venue's **QuickFIX data dictionary** (XML). Use the venue's dictionary, not the
   standard one: venues add custom tags and repeating groups, and change required
   fields. If the venue supplies only a PDF, the agent writes the dictionary from it
   (start from QuickFIX's standard `FIX44.xml` or `FIX50SP2.xml` and add the venue's
   fields, groups and changes), and this dictionary is reviewed like a field table.
   If the venue supplies a machine-readable specification (LMAX's `fix-spec.json`),
   the dictionary is generated from it by a script that is committed with it.
   A venue's own dictionary can be wrong: Coinbase's FIXT 1.1 dictionary lacks
   `RawDataLength` (95), which its Logon's `RawData` (96) needs. Validate the
   dictionary against the specification before using it: every message the
   specification defines, built from its example or its field list, parses and
   validates with it. Corrections go into a copy of the dictionary, each listed in
   the feed description.
3. Session parameters from the venue: host, port, TLS, SenderCompID, TargetCompID,
   username and password (and where they go: `Username`/`Password` on Logon, or
   a separate `UserRequest`), the session schedule, heartbeat interval.

## Workflow

Steps 1 to 4 of `SKILL.md` apply. The feed description's sections for a post-trade
feed:

- **Transport:** FIX version; for FIX 5.0, FIXT 1.1 with `DefaultApplVerID`; TLS
  (QuickFIX `SSLSocketInitiator` or stunnel); the session schedule and when sequence
  numbers reset (daily, weekly, never).
- **Logon:** which tags carry credentials; whether a `UserRequest` (35=BE) follows the
  Logon (ICAP sends one and waits for `UserResponse`, 35=BF); whether a trading-session
  status message arrives before requests are accepted.
- **Subscription:** the `TradeCaptureReportRequest` (35=AD): `TradeRequestID`,
  `TradeRequestType` (0 all trades, 5 unacknowledged trades are the usual choices),
  `SubscriptionRequestType` (snapshot, or snapshot plus updates), and date or time
  filters. The `TradeCaptureReportRequestAck` (35=AQ) and what its
  `TradeRequestResult` and `TradeRequestStatus` values mean.
- **Report lifecycle:** how a new trade, an amendment, a cancellation and a bust are
  sent (`TradeReportTransType`, `TradeReportType`, `ExecType`, `TrdType`,
  `TrdSubType`); which identifier is stable across them (`TradeReportID`, `TradeID`,
  `ExecID`, `SecondaryTradeReportID`); where the side and account are (the `NoSides`
  repeating group in FIX 4.4 and 5.0).
- **Acknowledgement:** whether the venue requires a `TradeCaptureReportAck` (35=AR) for
  each report, and which fields it must echo (they differ by venue: the m2 clients
  for Dealerweb, Fenics and ICAP all acknowledge every report, each with a different
  set of fields). With `TradeRequestType` 5, a report not acknowledged is sent again
  after a reconnect.
- **Instrument and economics:** symbology (CUSIP, ISIN, the venue's symbol and suffix),
  price or yield, accrued interest, settlement date, gross and net amounts,
  commissions, counterparty.
- **Mapping to Kaspar:** each report type to the normalized trade-capture message.

## Implementation

| Part | Responsibility |
|---|---|
| QuickFIX application | `FIX::Application` + `FIX::MessageCracker` for the venue's FIX version |
| `toAdmin` | add credentials to the outgoing Logon (and only to the Logon) |
| `onLogon` | send the `UserRequest` if the venue has one, then the `TradeCaptureReportRequest` |
| `onMessage(TradeCaptureReport)` | parse into the normalized message; send it to its consumer; send the `TradeCaptureReportAck` if required |
| `onMessage(TradeCaptureReportRequestAck)` | log; a rejection is an error that stops the client |
| `onMessage(BusinessMessageReject)` | log `RefMsgType`, `BusinessRejectReason`, `Text` |
| `toApp` | refuse to send `Reject` (35=3) messages generated by the client itself: a field missing in an `onMessage` handler throws, QuickFIX converts the exception into a Reject, and the venue may resend or drop the session; log the field instead |
| actor | owns the `SocketInitiator`; starts it in `Start` (not the constructor, as `TTFIXOM` explains), stops it in `Shutdown`; callbacks arrive on QuickFIX's thread, so they `send` messages to actors and never touch actor state directly |
| settings | QuickFIX session file: `BeginString`, `DefaultApplVerID`, CompIDs, host and port, schedule, `HeartBtInt`, `FileStorePath`, `FileLogPath`, `DataDictionary` (FIX 4.4) or `TransportDataDictionary` + `AppDataDictionary` (FIX 5.0) |

Configuration conventions:

- QuickFIX's `FileLog` writes every message, and `FileStore` keeps the messages
  sent, both with the Logon's `Password` (554) in clear. Use a log and a store that
  redact it (LMAX's `RedactLog`), or a memory store and no file log.
- QuickFIX's `SSLSocketInitiator` sends no SNI and does not check the server's host
  name against its certificate. Where the venue needs SNI, or the check matters
  (any connection over the internet), terminate TLS in a proxy that does both
  (stunnel with `sni` and `checkHost`) and connect QuickFIX to it in plain TCP.
- Credentials are not in the repository: the settings file and the credentials file
  are supplied at deployment and read by path from the actor's config.
- Keep `FileStorePath` between runs: it holds the session sequence numbers. Deleting
  it forces a sequence reset that the venue may refuse.
- Relax validation only for what the venue's dictionary cannot express, and record
  each relaxed setting (`ValidateUserDefinedFields`, `ValidateFieldsOutOfOrder`,
  `ValidateIncomingMessage`) and the reason in the feed description.

## Normalized output

The m2 clients write the raw report into MySQL. In kaspar-hft the output is an actor
message (`MessageT`), defined once for all venues, with at least: venue, trade
identifier, report identifier, lifecycle event (new, amend, cancel, bust), the
identifier of the trade an amendment or cancellation refers to, instrument (venue
symbol and Kaspar asset), side, quantity, price, yield where the instrument trades
in yield, trade time, settlement date, account, counterparty, `PossDup` flag, and the
raw FIX message. Consumers (position keeping, an STP publisher, a recorder) subscribe
to it. Do not add a database dependency to the client.

## Drop copies of execution reports

A drop copy that sends `ExecutionReport`s (Coinbase's FIX drop copy) has no
`TradeCaptureReportRequest` and no acknowledgement: the venue starts sending on
logon, for the accounts the session is entitled to. Fills are the reports with
`ExecType` F (trade); corrections and busts are `ExecType` G and H, referring to
the fill by `ExecRefID`. `ExecID` is the stable identifier, `OrderID` and
`ClOrdID` tie a fill to its order, and the reports for new, cancelled and
replaced orders that the same session carries are not trades. The feed
description lists each `ExecType` and `OrdStatus` the venue sends and what the
handler does with it.

## Quantities and prices in the normalized message

`TradeCapture` keeps the price as the venue sent it (`price_mantissa`,
`price_decimals`), but `qty` is a whole number with no scale. For a venue with
fractional quantities (crypto, notional amounts in decimals), the handler sends
the quantity in the instrument's unit, the same unit as its book and its order
manager (`kaspar_mapping.md`), and section 8 of the feed description states it; a
quantity that is not a whole number of units is logged as an error with the raw
report, not rounded.

## Duplicates and replay

- A report may arrive more than once: after a reconnect, with `PossDupFlag` (43) or
  `PossResend` (97), or because it was not acknowledged. Processing must be
  idempotent on the stable identifier: the second copy of a report is dropped, an
  amendment replaces the earlier version, a cancel or bust removes it.
- A `TradeRequestType` of 0 replays the whole day on every logon; position keeping
  must rebuild from it, not add to what it already has.
- Across the daily sequence reset the venue may send the day's reports again.

## Tests

1. Unit tests that build each report type as a QuickFIX message from the
   specification's examples (`FIX::Message(string, dictionary)` parses a tag=value
   string with the venue's dictionary) and check the normalized message: new,
   amend, cancel, bust, a report with two sides, a duplicate with `PossDupFlag`.
2. Dictionary check: every message in the specification's examples parses with the
   venue's dictionary and validation on.
3. Live check, with credentials: logon, request, the request ack, the day's reports,
   one ack per report, a reconnect that produces no duplicate trade downstream.
