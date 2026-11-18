# Feed description: Nasdaq DROP (post-trade drop copy)

Every entry cites its source: document, version, page, section. "DROP" is
*DROP, Version 2.20, updated October 2024*; "Soup" is *SoupBinTCP, Version 3.00*.
Both are public on nasdaqtrader.com. Copies used for this description were
downloaded on 2026-11-18 and are not in the repository.

## 1. Venue and documents

| Document | Version | Date | Applies to |
|---|---|---|---|
| DROP (`drop2.2.pdf`, 8 pages) | 2.20 | title page "Updated October, 2024"; last dated revision entry 02/29/2016 (p.8, section 3) | the DROP trade message line: the application layer this client parses |
| SoupBinTCP (`soupbintcp.pdf`, 9 pages) | 3.00 | last revision entry 02/22/2017 (p.9, section 5) | the session layer that carries DROP: login, heartbeats, sequenced data, end of session |
| DROP to FIX (OUCH) Format (`ouch_drop1.0.pdf`) | 1.00 | February 2026 | not used: the FIX 4.0-4.2 variant of the same drop copy (p.2, sections 1 and 2.1) |
| DROP, PSX (`psxdrop21.pdf`) | 2.1 | June 4, 2015 | not used: Nasdaq PSX; older than 2.20 |
| DROP, ETMF (`DROPETMF.pdf`) | 2.30 | March 31, 2015 | not used: NextShares (ETMF) variant |

DROP 2.20 is the current document for the Nasdaq Stock Market and is the one
this client implements. DROP says the DROP host is reached over "SoupTCP or
SoupBinTCP" (DROP p.1, section 1.1); this client uses SoupBinTCP (section 10,
item 1).

### Why Nasdaq DROP

The four U.S. equities exchange groups were surveyed on 2026-11-18:

| Venue | Public post-trade / drop-copy specification | Protocol |
|---|---|---|
| Nasdaq | DROP 2.20 (October 2024); DROP to FIX (OUCH) Format 1.00 (February 2026) | DROP: fixed-format ASCII lines over SoupBinTCP. OUCH DROP: FIX 4.0-4.2 |
| Cboe U.S. Equities | Cboe Titanium U.S. Equities FIX Specification 2.9.62 (November 6, 2026), "FIX Drop" pp.55-56 (Standard FIX Drop, Order by Order FIX Drop); FIX 4.2 (p.6). The BOE 2.4.58 (October 5, 2026, p.40) and BOEv3 1.0.17 (October 20, 2026) specifications refer to ODROP and FIXDROP ports, which are FIX | FIX 4.2 only |
| NYSE (Pillar) | Pillar Gateway Binary Protocol Specification 6.1 (September 22, 2026), section 7.4 p.21: drop copies "are available via a separate FIX gateway interface"; Pillar Gateway FIX Protocol Specification 6.0 (August 14, 2026), section 9 p.67 | FIX 4.2 only |
| IEX | The IEX documents page lists an "IEX Equities FIX Specification"; its link (`iextrading.com/documents/iex-fix-specification`) returned HTTP 404 on 2026-11-18. No public drop-copy specification was retrieved | unknown |

Nasdaq DROP is the only public, current exchange drop copy among them that is
not FIX. It is a fixed-offset format over SoupBinTCP, which `fieldgen.py`
generates and which needs no QuickFIX (not built on this machine). The three FIX
drop copies would each need a QuickFIX dictionary written from the PDF and could
not be linked or run here.

## 2. Transport

- **TCP, SoupBinTCP 3.00.** The client opens a TCP connection to the DROP host
  and sends a Login Request first (Soup p.3, section 1.2; p.6, section 2.3.1).
  The server answers Login Accepted or Login Rejected; after a reject it closes
  the socket (Soup p.5, section 2.2.2). Host, port, username and password are
  assigned by Nasdaq; none is in the specification.
- **Credentials.** In the Login Request: Username 6 bytes, Password 10 bytes,
  case-insensitive, padded on the right with spaces (Soup p.6-7, section 2.3.1).
  There is no separate user request.
- **Session and sequence.** Requested Session (10 bytes, blank for the currently
  active session) and Requested Sequence Number (20 bytes ASCII; 0 for "the most
  recently generated message") (Soup p.7, section 2.3.1). The first sequenced
  message of each session is 1 (Soup p.3, section 1.2).
- **Heartbeats.** The server sends a Server Heartbeat after more than 1 second
  without data; the client must send a Client Heartbeat after more than 1 second
  without sending (Soup p.3, section 1.3). A server "typically" closes a client
  silent for 15 seconds (same section). This client sends a heartbeat every
  `heartbeat_ms` (1000) of silence and treats `server_timeout_ms` (15000) of
  silence from the server as a lost link.
- **Login timeout.** The server may close a socket that sends no Login Request
  within "typically 30 seconds" (Soup p.6, section 2.3.1); the client sends it
  immediately.
- **Logout.** Logout Request; the server closes the connection (Soup p.8,
  section 2.3.4). Sent on `stop()`.
- **TLS.** Not mentioned in either document. None is implemented.
- **Redundancy.** Several DROP hosts can be configured to send the same events
  (DROP p.2, section 1.4). The client connects to one host (section 10, item 10).
- **Throttles.** None: the client sends nothing but login, heartbeats and logout.

## 3. Framing

- **SoupBinTCP packet.** A 2-byte big-endian length that counts the packet type
  and payload but not itself, a 1-byte packet type, a variable payload (Soup p.2,
  section 1.1). Packets need not align with TCP segments (same section): the
  reader buffers and decodes with `status::incomplete` until a packet is whole.
  No maximum payload length (same section).
- **Integers** in SoupBinTCP are big-endian; character fields are ASCII (Soup
  p.4, section 1.5). The only integer is the length.
- **One DROP line per Sequenced Data packet.** "Each Sequenced Data Packet
  carries a single higher-level protocol message" (Soup p.3, section 1.2), and the
  sequence number is implied by counting from the number in Login Accepted (Soup
  p.5, section 2.2.3).
- **DROP line.** Fixed format, comma delimited, CR/LF terminated ASCII (DROP p.2,
  section 1.6). 126 bytes of fields and commas; the CR/LF, if present inside the
  Sequenced Data payload, is accepted and ignored (section 10, item 2).
- **DROP data types** (DROP p.2, section 1.3): numeric fields right justified and
  space filled on the left; alpha fields left justified, padded with spaces on
  the right; prices 6 whole places, a point, 4 decimals, the whole part padded on
  the left with spaces and the decimals padded on the right with zeros;
  timestamps numeric seconds past midnight Eastern Time.
- **Offsets.** The DROP table gives 0-based offsets; between one field's end and
  the next field's offset there is always exactly one byte, the comma, which the
  table does not list (pp.2-7). The field table writes each comma as a 1-byte
  field (`Sep<offset>`) so that the offsets are contiguous and checked. The parser
  checks every comma.

## 4. Message catalogue

Generated from `nasdaq_drop.tbl` with `fieldgen.py --markdown`. Offsets are
0-based. The SoupBinTCP offsets include the 2-byte length, as the specification
prints them (`@offset_base 2`).

### soup::from_nasdaq

#### Debug (type `+`, 1 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 2 | 1 | char | Debug: free-form human readable text; ignored by the client (SoupBinTCP 3.00, 2.1, p.4) |
| Text | 3 | var | Payload | Free form human readable text |

#### LoginAccepted (type `A`, 31 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 2 | 1 | char | LoginAccepted: login succeeded; first non-debug packet after a valid login (SoupBinTCP 3.00, 2.2.1, p.4) |
| Session | 3 | 10 | Session (right-aligned) | Session ID now logged into, left padded with spaces |
| SequenceNumber | 13 | 20 | Numeric (right-aligned) | Sequence number of the next Sequenced Message to be sent, ASCII, left padded with spaces |

#### LoginRejected (type `J`, 2 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 2 | 1 | char | LoginRejected: login failed; the server then closes the socket (SoupBinTCP 3.00, 2.2.2, p.5) |
| RejectReasonCode | 3 | 1 | Alpha | A not authorized (username/password), S session not available |

#### SequencedData (type `S`, 1 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 2 | 1 | char | SequencedData: one message of the higher-level protocol; sequence number implied (SoupBinTCP 3.00, 2.2.3, p.5) |
| Message | 3 | var | Payload | Defined by the higher-level protocol: one DROP trade message line |

#### ServerHeartbeat (type `H`, 1 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 2 | 1 | char | ServerHeartbeat: sent after more than 1 second without other data (SoupBinTCP 3.00, 2.2.4, p.6) |

#### EndOfSession (type `Z`, 1 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 2 | 1 | char | EndOfSession: the current session is finished; the connection closes shortly after (SoupBinTCP 3.00, 2.2.5, p.6) |

### soup::to_nasdaq

#### LoginRequest (type `L`, 47 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 2 | 1 | char | LoginRequest: first packet on a new connection (SoupBinTCP 3.00, 2.3.1, p.6-7) |
| Username | 3 | 6 | Alphanumeric | Username, case-insensitive, padded on the right with spaces |
| Password | 9 | 10 | Alphanumeric | Password, case-insensitive, padded on the right with spaces |
| RequestedSession | 19 | 10 | Session (right-aligned) | Session to log into, all blanks for the currently active session. Padding of a non-blank value not stated; taken as in LoginAccepted |
| RequestedSequenceNumber | 29 | 20 | Numeric (right-aligned) | Next sequence number wanted, ASCII; 0 for the most recently generated message. Padding not stated; taken as in LoginAccepted |

#### UnsequencedData (type `U`, 1 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 2 | 1 | char | UnsequencedData: a higher-level message to the server; not used by DROP (SoupBinTCP 3.00, 2.3.2, p.7) |
| Message | 3 | var | Payload | Defined by the higher-level protocol |

#### ClientHeartbeat (type `R`, 1 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 2 | 1 | char | ClientHeartbeat: sent after more than 1 second without other data (SoupBinTCP 3.00, 2.3.3, p.7) |

#### LogoutRequest (type `O`, 1 bytes, 2-byte exclusive length prefix)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| MessageType | 2 | 1 | char | LogoutRequest: the server terminates the connection (SoupBinTCP 3.00, 2.3.4, p.8) |

### drop::from_nasdaq

#### TradeMessageLine (type none, 126 bytes)

| Field | Offset | Length | Type | Meaning |
|---|---|---|---|---|
| TimeStamp | 0 | 9 | DTimestamp (right-aligned) | Seconds past midnight Eastern Time, to the millisecond (sample 34293.1) (p.2; data type 1.3) |
| Sep9 | 9 | 1 | Comma | , |
| Type | 10 | 1 | DType | A new order accepted, E executed, X canceled, B execution broken, U replaced, Y canceled (AIQ), C trade correction (p.3) |
| Sep11 | 11 | 1 | Comma | , |
| Source | 12 | 6 | DAlphanum | Source of the order: account of the OUCH port, or $PHON for the phone desk (p.3) |
| Sep18 | 18 | 1 | Comma | , |
| OrderToken | 19 | 15 | DAlphanum | Token given by the order entry firm, user(4)+comma(1)+token(10) (p.3) |
| Sep34 | 34 | 1 | Comma | , |
| ReplacedToken | 35 | 10 | DAlphanum | Token of the replaced order; only on a replaced message (p.3) |
| Sep45 | 45 | 1 | Comma | , |
| BuySell | 46 | 1 | DAlpha | B bought, S sold, T sold short, E sold short exempt (p.3) |
| Sep47 | 47 | 1 | Comma | , |
| Shares | 48 | 6 | DNumeric (right-aligned) | Accept: total shares; cancel: shares canceled; execution: shares executed; break: shares of the broken execution (p.4) |
| Sep54 | 54 | 1 | Comma | , |
| Stock | 55 | 8 | DAlpha | Stock symbol (p.4) |
| Sep63 | 63 | 1 | Comma | , |
| Price | 64 | 11 | DPrice (right-aligned) | Accept: limit price; execution: execution price; cancel: limit price of the open order (p.4) |
| Sep75 | 75 | 1 | Comma | , |
| Firm | 76 | 4 | DAlpha | Order entry firm (p.4) |
| Sep80 | 80 | 1 | Comma | , |
| Reference | 81 | 12 | DNumeric (right-aligned) | Order reference number assigned by Nasdaq, unique per order (p.4) |
| Sep93 | 93 | 1 | Comma | , |
| MatchTIF | 94 | 12 | DNumeric (right-aligned) | Executions and breaks: match number of the trade (one buy and one sell order); other messages: time in force (p.5) |
| Sep106 | 106 | 1 | Comma | , |
| Capacity | 107 | 1 | DAlpha | A agency, P principal, R riskless (p.5) |
| Sep108 | 108 | 1 | Comma | , |
| LiquidityCode | 109 | 1 | DAlpha | Executions: liquidity code (p.5-7); cancels: cancel reason (p.7) |
| Sep110 | 110 | 1 | Comma | , |
| ClearingCode | 111 | 1 | DAlpha | Clearing path of the trade, Q = QSR (p.7) |
| Sep112 | 112 | 1 | Comma | , |
| ReferencePrice | 113 | 11 | DPrice (right-aligned) | Reference price of the execution, may be 0 (p.7) |
| Sep124 | 124 | 1 | Comma | , |
| ReferencePriceType | 125 | 1 | DAlpha | blank not applicable, I intraday indicative value (p.7) |


## 5. Sequencing and recovery

- **Scope.** One sequence per SoupBinTCP session, starting at 1 (Soup p.3,
  section 1.2). DROP lines carry no sequence number of their own.
- **Logon and replay.** The client asks for sequence number 1 with a blank
  session by default (`replay_from_start true`): the server sends the whole
  session from its first line (Soup p.3, section 1.2). DROP's own description of
  the same mechanism, "the DROP host always begins transmission with the first
  message for the current day (line #1)" unless a line number is given (DROP
  p.1, section 1.2), is written for the line-based SoupTCP login. With
  `replay_from_start false` the client asks for 0, "the most recently generated
  message" (Soup p.7, section 2.3.1).
- **Reconnect.** After a lost connection the session logs in again to the
  session named in the last Login Accepted, at the next sequence number it has
  not delivered (Soup p.3, section 1.2). The sequence number in the new Login
  Accepted is authoritative: if it is lower than requested, the lines in between
  are delivered again and the ledger drops them.
- **Gaps.** SoupBinTCP over TCP cannot skip a number (Soup p.5, section 2.2.3). A
  line numbered past the one expected is counted (`Stats::gaps`) and logged as an
  error; it is still applied.
- **End of session.** End of Session means no more messages in this session; the
  client must log in again with the new session id or a blank one for the next
  session (Soup p.3, section 1.4; p.6, section 2.2.5). The client stops on End of
  Session and does not reconnect (section 10, item 6).
- **Daily reset.** A Login Accepted for a session other than the one the ledger
  holds starts a new trading day: the ledger is cleared, because the new session
  numbers from 1 again and match numbers may repeat.
- **State cleared on reset:** the ledger (trades, tombstones, report ids), the
  highest sequence number seen, the trade date.

## 6. Semantics

### Report lifecycle

| DROP Type (DROP p.3) | Meaning | Lifecycle event |
|---|---|---|
| `A` | new order accepted | none: order event, counted, not forwarded |
| `E` | existing order executed | **New** trade: one party's side of one match |
| `X` | existing order canceled | none: order event |
| `B` | previous execution broken | **Bust** of the trade with the same match number and order reference |
| `U` | existing order replaced | none: order event |
| `Y` | existing order cancelled (AIQ) | none: order event |
| `C` | trade correction | **Amend** of the trade with the same match number and order reference (section 10, item 3) |

There is no trade cancellation separate from a break; `Cancel` is not produced
for Nasdaq.

### Identifiers

- **Match number** (Match/TIF, offset 94): "the match number assigned by NASDAQ
  to this trade. Each match consists of an execution between a buy order and a
  sell order" (DROP p.5). It is the same on both sides of a trade.
- **Order reference number** (Reference, offset 81): "the order unique reference
  number assigned by NASDAQ to this order" (DROP p.4).
- A DROP account can cover several firms (DROP p.2, section 1.5), so both sides
  of one match can appear on one port. The stable trade identifier is therefore
  `<match number>.<order reference>`: one party's side of one match. Breaks and
  corrections carry the same pair.
- **Report identifier:** `<SoupBinTCP session>:<sequence number>`. It is the same
  every time the line is delivered, in this session or after a reconnect or a
  replay from line 1, which is what lets the ledger drop duplicates.
- **Ordering:** the sequence number orders the reports of a session
  (`report_seq`).

### Acknowledgement

DROP has no application-level acknowledgement and takes no input from the
client ("DROP does not provide the ability to enter orders", DROP p.1, section
1); SoupBinTCP has no acknowledgement packet either (Soup pp.6-8, section 2.3).
The client's only acknowledgement is implicit: the sequence number it requests
when it logs in again. It sends Client Heartbeats as SoupBinTCP requires.

### Instrument and economics

| Item | Source | Notes |
|---|---|---|
| symbol | Stock, 8 bytes alpha (DROP p.4) | Nasdaq symbol; mapped to a Kaspar asset by the `assets` table in the configuration |
| side | Buy/Sell: B, S, T sold short, E sold short exempt (DROP p.3) | |
| quantity | Shares, 6 digits (DROP p.4) | for E: shares executed in this trade; for B: shares of the broken execution |
| price | Price, 6.4 decimal (DROP p.4) | for E: the execution price. Kept as a mantissa with 4 decimals |
| reference price | Reference Price and Type (DROP p.7) | in `raw` only |
| trade time | Time Stamp, seconds past midnight Eastern Time, to the millisecond (DROP p.2) | `trade_time_ns`, nanoseconds since Eastern midnight |
| trade date | not in DROP | `trade_date` from the configuration, or the Eastern Time date at login (section 10, item 7) |
| settlement date | not in DROP | 0 |
| account | Source: "typically the account of the OUCH port used to enter the order", or `$PHON` (DROP p.3) | `account` |
| firm | Firm: the order entry firm (DROP p.4) | `firm` |
| counterparty | not in DROP | empty |
| capacity, liquidity code, clearing code | DROP p.5, pp.5-7, p.7 | in `raw` only |
| yield | not applicable to equities | NaN |

### Replay and duplicates

A replay from line 1 (every logon with `replay_from_start true`, and after a
restart) delivers the whole day again. The ledger applies it idempotently: lines
already applied are duplicates (same report id) and are not forwarded, so
position keeping is rebuilt from the replay, never added to.

## 7. Mapping to Kaspar

Post-trade does not produce L3 records; it produces
`post_trade::msg::TradeCapture` (`../common/include/post_trade/msg/TradeCapture.hpp`),
one per event that changed the client's `post_trade::TradeLedger`.

| DROP | TradeCapture | Notes |
|---|---|---|
| `E` | `event` New, `trade_id` `<match>.<reference>` | `ref_trade_id` empty |
| `C` | `event` Amend, `ref_trade_id` = `trade_id` | replaces quantity and price |
| `B` | `event` Bust, `ref_trade_id` = `trade_id` | quantity, side, symbol, price taken from the trade if the line leaves them out |
| `A`, `X`, `U`, `Y` | none | counted in `Stats::order_events` |
| every line | `report_id` `<session>:<seq>`, `report_seq` seq, `raw` the line | |
| | `venue` `en::x::NASDAQ` | existing code; no new `exch_code` entry |
| | `venue_order_id` the order reference | |
| | `symbol` Stock, `asset` from `assets`, -1 when not mapped | |
| | `side` B Buy, S Sell, T SellShort, E SellShortExempt | |
| | `qty` Shares, `price_mantissa` Price x 10^4, `price_decimals` 4 | |
| | `trade_date`, `trade_time_ns` | section 6 |
| | `account` Source, `firm` Firm, `counterparty` empty, `settle_date` 0 | |
| | `poss_dup` true when the line's sequence number is not above the highest already received by this process | informational; duplicates are already removed |

Ledger outcomes and what the client does:

| Outcome | Forwarded | Logged |
|---|---|---|
| Added, Amended, Removed | yes | |
| Duplicate | no | counted |
| Stale (older than what the ledger holds, or the trade was busted) | no | warning |
| Orphan (break of a trade not seen): a tombstone, so the late execution is Stale | no | error |
| Invalid (a second execution with the same match and reference) | no | error with the line |

A correction of a trade not yet seen carries the full economics: it is added as
the trade and forwarded as Amend, and the execution, when it arrives, is Stale.

## 8. Instruments

DROP has no instrument directory. The symbol is the Nasdaq symbol, up to 8
characters (DROP p.4). Prices have 4 decimals for every symbol (DROP p.2,
section 1.3). Quantities are whole shares. The Kaspar asset id comes from the
`assets` table in the client's configuration.

## 9. Pitfalls

### 1. Reading the specification

- Several documents: DROP 2.20 against PSX DROP 2.1, ETMF DROP 2.30 and the FIX
  OUCH DROP 1.00. 2.20 applies to the Nasdaq Stock Market (section 1).
- Errata and revision tables: DROP's title page says October 2024, but its
  revision history ends at 02/29/2016 (p.8); what changed in 2024 is not stated
  (section 10, item 9). SoupBinTCP's history (p.9) records language for a
  requested sequence number past the server's highest that is not in the body
  (section 10, item 5).
- Tables across page breaks: the DROP line table runs over pp.2-7; the
  contiguous-offset check of `fieldgen.py --check` passes on all 126 bytes, so no
  row was dropped.
- Offsets 0-based; the SoupBinTCP length excludes itself (Soup p.2, section 1.1)
  and is counted in the specification's offsets (`@offset_base 2`).
- Reserved and filler fields: none in DROP besides the commas, which are kept as
  1-byte fields.
- Enumerations elsewhere: liquidity codes and cancel reasons share one field
  (pp.5-7); not used by the mapping, kept in `raw`.
- Field names differ: the Order Token is "Token" in revisions before 06/15/2009
  (p.8); the table's name is used.
- Examples against tables: the sample timestamp `34293.1` and price `12.875` are
  not zero padded as section 1.3 describes; the parser accepts both forms.
- Two layers: SoupBinTCP and DROP are separate sections of the field table; the
  DROP type code is at offset 10, so the line has no generator type code and the
  dispatcher reads `Type`.
- Repeated message names: no.
- Variable-length fields: the Sequenced Data and Debug payloads, as tails.
- Odd integer widths: none; DROP numbers are ASCII.

### 2. Framing and encoding

- Endianness: big-endian length (Soup p.4, section 1.5); everything else ASCII.
- Signed values: none; prices and quantities are unsigned ASCII.
- Fixed-point prices: 4 decimals for every field and symbol (DROP p.2, section 1.3).
- Null and sentinel values: blank Replaced Token except on U (p.3); Reference
  Price "may be 0 if not applicable" (p.7), and blank is read as 0; blank
  Reference Price Type means not applicable (p.7).
- Strings: padded with spaces, left or right as section 3 says; the Order Token
  contains a comma at its fifth byte (p.3).
- Several messages per packet: one DROP line per Sequenced Data packet; several
  packets per TCP read and packets split across reads are handled by the reader.
- Timestamps: seconds past midnight Eastern Time with milliseconds (DROP p.2);
  no date (section 10, item 7).

### 3. Sequencing and recovery

- Scope: per SoupBinTCP session (section 5).
- A/B arbitration: not applicable; mirrored hosts exist (DROP p.2, section 1.4),
  one is used.
- Gap rule: none possible on TCP; counted if seen (section 5).
- Recovery: TCP replay by requested sequence number (Soup p.3, section 1.2).
- Snapshot alignment: not applicable; there is no snapshot.
- State to clear on reset: the ledger on a new session (section 5).
- Rare paths: reconnect, overlap after a reconnect, rejected login, end of session,
  a new session and a refused connection are each exercised in the tests against
  the fake and against the simulator.
- Session behaviour: login, heartbeats both ways, logout (section 2). No
  cancel-on-disconnect: DROP enters no orders.

### 4. Book semantics

Not applicable: post-trade.

### 5. Prices, quantities and instruments

- Price or yield: price only (equities).
- Quantity multipliers: none; whole shares. Shares is 6 digits, so an execution
  above 999,999 shares cannot be written (section 10, item 8).
- Decimals sentinels: none.
- Ticks: not used by post-trade.
- Instrument definitions: none in DROP (section 8).
- Instrument lifecycle: not visible in DROP.

### 6. Behaviour the specification omits

No live or sample DROP data was available, and no connection to Nasdaq was
made, so none of these was probed: whether CR/LF is inside the Sequenced Data
payload, how a `C` line is filled, whether the server repeats lines on a
relogin, how sessions roll over at the end of the day. Each is in section 10.

### 7. Post-trade

- FIX version and transport: not FIX; SoupBinTCP 3.00 over TCP.
- Custom tags and dictionary: not applicable; the field table plays the role of
  the dictionary.
- Credentials: in the Login Request (Soup p.6-7, section 2.3.1); no user request.
- Missing field throws, Reject sent to the venue: not applicable; a line that
  does not parse is logged and counted, and the client sends nothing to the
  venue.
- Requests before a trading-session status: not applicable; DROP takes no
  requests after login.
- Subscription, replay from start of day, acknowledgement: the Login Request's
  sequence number is the whole subscription; replay from line 1 is the default;
  no acknowledgement exists (section 6).
- Report lifecycle and stable identifier: E, C, B with `<match>.<reference>`
  (section 6).
- Duplicates on reconnect: SoupBinTCP has no PossDup flag; the report id
  `<session>:<seq>` makes processing idempotent; `poss_dup` is set from the
  sequence number.
- Price, yield, accrued interest, settlement, counterparty: price only; no
  settlement date, no counterparty in DROP.
- Daily sequence reset: a new SoupBinTCP session numbers from 1; the client
  clears its ledger when the session id changes.

### 8. Order entry

Not applicable: DROP enters no orders. Breaks and corrections are not fills;
they reach the subscriber as Bust and Amend.

## 10. Ambiguities

| # | Question | Reading taken | Alternatives | Resolved by |
|---|---|---|---|---|
| 1 | Which transport does Nasdaq provide for DROP today? DROP says "SoupTCP or SoupBinTCP" (p.1, section 1.1) but describes the SoupTCP text login `password[,line number]` (p.1, section 1.2) | SoupBinTCP 3.00; its Requested Sequence Number plays the role of the line number | SoupTCP (ASCII, newline-framed lines), which would need another session layer | Nasdaq Subscriber Services, when the port is ordered |
| 2 | Is the CR/LF inside the SoupBinTCP Sequenced Data payload? (DROP p.2 says lines are CR/LF terminated; Soup p.5 says the payload is one message) | either: 126 bytes, or 126 followed by CR/LF or LF | — | first live session |
| 3 | What does a `C` (trade correction, added 02/29/2016, p.8) line contain? Its fields are not described (pp.3-7) | same match number and order reference as the execution; Shares and Price are the corrected values, replacing the trade | a correction of price only; a new match number; a pair of lines | Nasdaq, or a sample |
| 4 | Which price does a `B` line carry? (p.4 describes Price for accept, execution and cancel only) | ignored; the bust takes the economics of the trade it removes | the execution price | not needed by the mapping |
| 5 | What does the server do with a requested sequence number past its highest? (revision 03/20/2014, Soup p.9, refers to language not in the body) | the Login Accepted's number is authoritative; the simulator starts at the end | a Login Rejected; waiting for the number | Nasdaq |
| 6 | After End of Session, when is the next session available? (Soup p.3, section 1.4) | the client stops; it is restarted for the next day | relogin with a blank session after a delay, which could replay the ended session in a loop | owner's decision |
| 7 | Trade date: DROP has none. A replay after midnight Eastern belongs to the previous day | `trade_date` from the configuration, else the Eastern Time date at login | derive it from the session id, whose format is not specified | owner's decision |
| 8 | Shares is 6 digits (p.4): how is an execution above 999,999 shares reported? | not handled; such a line would be written in more than one execution, or truncated | — | Nasdaq |
| 9 | What changed in the October 2024 issue of DROP 2.20? The revision history ends in 2016 (p.8) | nothing that affects the layout; the 126-byte table is self-consistent | — | Nasdaq |
| 10 | Mirrored DROP hosts (p.2, section 1.4): do they share session ids and sequence numbers? | one host is used | two hosts with the ledger deduplicating, which requires the same report ids | Nasdaq |
| 11 | Order Token is 15 bytes, user(4)+comma+token(10) (p.3), while revision 06/15/2009 says 14 (p.8); Replaced Token is 10 (p.3) | offsets of the table; tokens are carried as text and not used for matching | — | not needed by the mapping |
| 12 | Trade identity: is `<match>.<reference>` unique for a day? A match is one buy and one sell order (p.5); the reference is unique per order (p.4) | yes | match number alone, which collides when both sides are on one port | Nasdaq, or a sample with both sides |
| 13 | Requested Session and Requested Sequence Number padding (Soup p.7 does not say) | as Login Accepted: left padded with spaces | right padded | first live login |
| 14 | Account: Source (the OUCH port's account) or Firm (the MPID)? | `account` = Source, `firm` = Firm | account = Firm | owner's decision |
