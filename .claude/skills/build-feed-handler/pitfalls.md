# Pitfalls checklist

For every item, record in the feed description how it applies to this venue,
with a citation, or why it does not apply. Items marked **(seen)** have caused
problems in kaspar-hft or m2 handlers.

## 1. Reading the specification

- Several documents for one feed, in different versions (e.g. a venue ITCH spec and
  a later ATS ITCH spec; rules of engagement revisions b and c). Which applies?
- Errata, release notes and "changes in this version" tables that override the body.
- Tables that continue across a page break: rows dropped or duplicated by extraction.
- Offsets: 0-based or 1-based. Lengths: does a length field count itself? **(seen:
  a length field documented as "excluding this field")**
- Reserved, padding and filler fields: keep them in offsets; never skip them.
- Enumerations defined in a separate section from the field that uses them.
- Field names that differ between the message table and the prose.
- Examples in the specification that contradict its tables: report both.
- Two protocol layers in one table: session messages (login, heartbeat, the
  sequenced-data envelope) and the application messages they carry reuse type codes.
  **(seen: a venue's logout request and order entry both use type `O`)** The layers
  may also be framed differently (session packets with a length prefix, application
  messages without). **(seen)** Put each layer in its own section.
- Message names that repeat with different type codes (two variants of an add-order
  message). **(seen)** Give each a distinct name from the specification's wording.
- A variable-length field at the end of a message (a block of nested messages): it
  has no fixed length and is not a struct member. **(seen: generated as a pointer
  member, which made the struct 8 bytes too long)**
- Integers whose width is not 1, 2, 4 or 8 bytes (ITCH timestamps are 6 bytes).

## 2. Framing and encoding

- Endianness, per message family (binary feeds sometimes mix).
- Signed or unsigned; two's complement or sign-magnitude.
- Fixed-point prices: global exponent, per-message exponent, or per-instrument
  decimals from the instrument definition.
- Null and sentinel values (all bits set, zero, -1, spaces).
- Strings: left- or right-padded, with spaces or NULs; character set.
- Several messages per packet; a message split across packets; batch headers.
- Timestamps: unit, epoch, time zone; nanoseconds since midnight versus since epoch.

## 3. Sequencing and recovery

- Scope of sequence numbers: per channel, per instrument, per session, per
  connection across all subscriptions. **(seen: Coinbase numbers every message on a
  connection, across channels)**
- A/B arbitration: reuse the existing implementation; do not write a new one.
- Gap detection rule, including what a duplicate or an older number means.
- Recovery mechanism: snapshot channel, request/retransmission (MoldUDP64-style),
  Glimpse-style snapshot giving the next sequence number, REST snapshot, TCP replay.
- The rule that aligns a snapshot with buffered incremental messages: which
  buffered messages are already in the snapshot, and which must straddle it.
  **(seen: Binance spot `U <= L+1 <= u` versus futures `U <= L <= u`)**
- A snapshot that is too old for the buffer: refetch, with backoff (rate limits).
- State to clear on a channel reset or session restart: books, order-ID maps,
  accumulators. **(seen: stale orderID -> securityID map after ChannelReset, #122)**
- Recovery paths run rarely: exercise each by injecting gaps, duplicates and resets
  in replay.
- Session-level behaviour: logon, sequence reset at start of day, heartbeats,
  test requests, throttles, cancel-on-disconnect.

## 4. Book semantics (market data)

- MBO or price level; if price level, by price or by level index.
- Level-index updates: insert shifts lower levels down; delete shifts them up;
  "delete through" and "delete from".
- Depth-limited books: levels that fall off the bottom, and levels that come back.
- Queue priority on modify: size down keeps priority, size up or price change loses
  it? This decides simulated fills. **Always confirm.**
- Implied prices and books; overlay or iceberg orders; hidden quantity.
- Trades: are they also reported as book deltas? If both, avoid double counting.
  **(seen: the late-trade rule in `Mbp2Mbo`)**
- The side a trade reports: the resting side or the aggressor. **(seen: Coinbase
  `side` is the maker's, Hyperliquid `side` is the taker's)**
- Trading states: pre-open, auction, halt, closed; whether a crossed book is legal
  in each.
- Book clears: on reset, on session start, on instrument deletion.

## 5. Prices, quantities and instruments

- Price or yield: some instruments trade in yield, some in price, some in spread.
  **(seen: a US Treasury venue declares per book whether it trades in price, yield or spread)**
- Quantity multipliers: notional per unit, per book. **(seen: a US Treasury
  venue publishes the notional of one unit per book, round lot versus odd lot)**
- Decimals fields with sentinel values. **(seen: a decimals field of -1 meaning
  ignore the field)**
- Ticks that depend on price. **(seen: Hyperliquid five significant figures)**
- Instrument definitions: source (channel, file, REST, FIX security list), when they
  arrive, and how IDs map to Kaspar assets and `sec_id`.
- Instrument lifecycle: new issues, when-issued, expiry, symbol reuse.

## 6. Behaviour the specification omits

Probe a live or sample feed before asking the user:

- Subscription options that change the update rate. **(seen: Hyperliquid `l2Book`
  `fast`, about 2 books a second against one every several seconds)**
- History replayed on subscribe. **(seen: Hyperliquid trades; Coinbase
  `market_trades` snapshot event)**
- A snapshot that is the whole book rather than its top. **(seen: Coinbase level2,
  tens of thousands of levels)**
- Order of messages within one event: trade before or after the book update.
- Connection limits: idle timeouts, required client pings, 24-hour disconnects.
  **(seen: Hyperliquid drops a silent client after 60 s; Binance after 24 h)**

## 7. Post-trade

- FIX version and transport: FIX 4.4, or FIX 5.0 SP2 over FIXT 1.1.
- Custom tags and repeating groups in the venue's dictionary; use the venue's
  QuickFIX XML, not the standard dictionary. If there is none, write it from the
  specification and have it reviewed.
- Credentials sent in the Logon or in a separate `UserRequest` after it. **(seen)**
- A missing field in a report handler throws; QuickFIX turns the exception into a
  `Reject` sent to the venue. **(seen: the m2 clients refuse to send Rejects in `toApp`)**
- Requests sent before the venue's `TradingSessionStatus` says the session is open.
- Subscription: `TradeCaptureReportRequest` type, replay from start of day, and the
  acknowledgement the venue expects (`TradeCaptureReportAck`).
- Report lifecycle: new, amend, cancel, bust (`TradeReportTransType`,
  `TradeReportType`); which identifier ties them together (`TradeReportID`,
  `ExecID`, `TradeID`).
- Duplicates on reconnect (`PossDupFlag`, `PossResend`); idempotent processing.
- Price versus yield, accrued interest, settlement date, counterparty and account
  fields for fixed income.
- Sessions that reset sequence numbers daily; behaviour across the reset.

## 8. Order entry

The actor tests listed in `order_entry.md` cover each of these.

- Client ids: uniqueness per day, per session or forever; a restart must not reuse
  one.
- Cancel and replace: a new client id (FIX) or the order's own id (some SDKs); which
  id each report then carries. Map every id an order has used back to it.
- Fill before ack, fill racing a cancel, fill after the cancel ack: each must count
  exactly once.
- Unsolicited cancels (cancel on disconnect, risk, end of day) must reach the SOM.
- Busts and corrections are not fills.
- Busy or throttled connections: what the API returns, and when it is available
  again.
- Reconnect: reports resent after a reconnect must not be applied twice.
- Orders placed outside this OM (manually, by another session) appear in reports.
- Short sales: locate requirements, sell short versus sell short exempt; refuse
  locally what the OM cannot mark correctly.
