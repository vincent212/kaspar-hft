# Feed description: LMAX Exchange Broker Market Data (FIX 4.4)

Not certified. Every entry cites its source by the short names in section 1.
LMAX publishes its FIX APIs as machine-readable JSON documents with no page
numbers; those are cited by prologue heading, message and field. The PDFs at
the same addresses were 91 to 105-byte placeholders when fetched
("PLACEHOLDER — replace with the real ... PDF"), so no page can be cited for
them.

## 1. Venue and documents

LMAX Exchange (LMAX Limited, London), spot FX. Fetched 2026-11-18; copies in the
build scratchpad, not in the repository.

| Short name | Document | Version | Date | Applies to |
|---|---|---|---|---|
| MD | Broker Market Data `fix-spec.json`, https://docs.lmax.com/fix-api/broker-market-data/fix/latest/fix-spec.json | FIX.4.4 version; latest changelog entry release 272 | 2019-11-13 | the feed: session rules, V, W, Y |
| TR | Broker Trading `fix-spec.json` (same site) | FIX.4.4 version, release 370 | 2023-12-02 | session schedule shared with the trading gateway |
| CG | LMAX Exchange connectivity guide, https://www.lmax.com/connectivity-guide | web page, undated | fetched 2026-11-18 | demo and UAT hosts and ports (section "FIAT Demo") |
| CO | LMAX Exchange Connectivity Overview, https://static-www.lmax.com/pdf/en/LMAX-Exchange-Connectivity-Overview.pdf | 8 pages | 21 January 2019 | older host tables; p. 8 "DEMO Test Venue" |
| CS | Contract Specifications Spot FX, https://www.lmax.com/documents/LMAXExchange-Contract-Specifications-Spot-FX.pdf | 4 pages | 2023-07-03 (PDF creation date) | contract size, minimum quantity, tick size |
| PD | Public Data REST API on demo, https://public-data-api.london-demo.lmax.com/v1/instruments | live response | fetched 2026-11-18 | SecurityIDs and increments of the 5 instruments it lists |
| DC | MTF Drop Copy `fix-spec.json` (same site) | 2.7 | 2023-03-13 | MTF service, not used; cited only as evidence on identifiers and units |

Which document applies: the task's starting point,
https://www.lmax.com/documents/LMAXExchange-Connectivity-Overview.pdf, now returns
the HTML connectivity guide (CG), so CG is the current host list; CO is the 2019
PDF of the same overview, kept for its page-numbered tables. CO p. 8 labels the
demo hosts "FIX 4.2"; MD defines only a FIX.4.4 version. MD is taken (section 10,
item 7).

## 2. Transport

| Item | Value | Source |
|---|---|---|
| Demo host | `fix-marketdata.london-demo.lmax.com:443`, TLS | CG "FIAT Demo"; CO p. 8 |
| UAT host (broker) | `fix-marketdata.london-uat.lmax.com:443` | CG "User Acceptance Test (UAT)" |
| Production (refused by this handler) | `fix-market-data.london-perpetual.lmax.com:52001` and others | CG; CO pp. 4-7 |
| TLS | "All messages are sent over an SSL encrypted connection"; EncryptMethod 0 | MD Logon, field 98 |
| SNI | Not needed: the demo host completed a TLS handshake without SNI (`openssl s_client -noservername`, 2026-11-18; certificate `CN=*.london-demo.lmax.com`, DigiCert). QuickFIX's SSL initiator sends no SNI. | probe |
| Credentials | Username (553) and Password (554) on the client's Logon | MD Logon |
| Sequence reset | "LMAX expect a reset of Sequence Numbers on all logins"; 141=Y on every Logon | MD prologue, Session Resetting |
| Schedule | open and close at 17:02:00 America/New_York, every day except maintenance | MD prologue, Fix Session Connectivity Schedule |
| Message size | at most 8192 bytes either way; a larger inbound message logs the client off | MD prologue, Messages sizes |
| Unknown tags | LMAX asks clients to accept and ignore tags they do not know | MD prologue, Compatibility |
| Heartbeat | HeartBtInt (108) on Logon; no value prescribed | MD Logon |

Environment guard: `lmax::fix::host_allowed` accepts only `*.london-demo.lmax.com`
for `environment = demo` and `*.london-uat.lmax.com` for `uat`, rejects numeric
addresses for both, and refuses `production` outright. It runs before any
QuickFIX object is made (tests: `LmaxFix.HostGuard`,
`MdFeedConfigTest.RefusesAProductionHostForDemo`).

## 3. Framing

FIX 4.4 tag=value over TLS: BeginString `FIX.4.4`, BodyLength, MsgType first;
CheckSum last (MD field list, tags 8, 9, 10, 35). Standard header fields LMAX
lists: 49, 56, 34, 52, 43, 97, 122. PossResend (97) is always N from LMAX and
ignored by LMAX (MD field 97).

The QuickFIX dictionary `dict/LMAX-FIX44-MarketData.xml` is generated from MD
by `tools/fixspec2qf.py` (LMAX publishes none). Components are flattened into
the messages; TestPing (999) is in the Logon message but not in MD's field list
and takes its type from the message. The tables below are rendered by
`fixspec2qf.py --markdown` from the same JSON.

## 4. Message catalogue

Application messages and the Logon. Heartbeat, TestRequest, ResendRequest,
Reject, SequenceReset and Logout are standard FIX 4.4 (MD session section). LMAX
lists no MarketDataIncrementalRefresh (X): MsgType's values in MD are 0-5, A, V,
W, Y and `xt` ("Trace Subscription", no message definition).

### Logon (35=A, bidirectional)

Message used to establish a connection between a client and the LMAX FIX Gateway.

| Tag | Field | Req | Type | Values | LMAX description |
|---|---|---|---|---|---|
| 98 | EncryptMethod | Y | INT | 0=NoneOther | Method of encryption. LMAX does not support encryption at the FIX layer. All messages are sent over an SSL encrypted connection. |
| 108 | HeartBtInt | Y | INT |  | Heartbeat interval (seconds) |
| 141 | ResetSeqNumFlag | N | BOOLEAN | N=No, Y=Yes | Indicates that the both sides of the FIX session should reset sequence numbers. |
| 383 | MaxMessageSize | N | LENGTH |  | Maximum number of bytes supported for a single message. |
| 553 | Username | N | STRING |  | LMAX username. This is required on the login message sent by the client |
| 554 | Password | N | STRING |  | LMAX Password or passphrase. This is required on the login message sent by the client |
| 999 | TestPing | N | BOOLEAN |  |  |

### MarketDataRequest (35=V, incoming)

Message sent by the client to request market data.

| Tag | Field | Req | Type | Values | LMAX description |
|---|---|---|---|---|---|
| 262 | MDReqID | Y | STRING |  | Unique identifier for Market Data Request. MDReqId is restricted to a maximum of 16 lowercase hexadecimal characters [0123456789abcdef] with no leading 0 cha... |
| 263 | SubscriptionRequestType | Y | CHAR | 1=SnapshotUpdate, 2=Unsubscribe | Subscription Request Type. |
| 264 | MarketDepth | Y | INT |  | Depth of market for Book Snapshot. LMAX supported values: 0 = Full Depth (LMAX supports a maximum of 20 levels of depth) 1 = Top of Book 2-20 = Report best N... |
| 265 | MDUpdateType | N | INT | 0=Full | Specifies the type of Market Data update. |
| 266 | AggregatedBook | N | BOOLEAN | Y=one book entry per side per price | Specifies whether or not book entries should be aggregated (only aggregated book entries are supported). |
| 267 | NoMDEntryTypes | Y | group |  |  |
| 269 | &nbsp;&nbsp;MDEntryType | Y | CHAR | 0=Bid, 1=Offer, 2=Trade | Type Market Data entry. |
| 146 | NoRelatedSym | Y | group |  |  |
| 48 | &nbsp;&nbsp;SecurityID | Y | STRING |  | LMAX Security identifier. Requires SecurityIDSource=8. |
| 22 | &nbsp;&nbsp;SecurityIDSource | Y | STRING | 8=ExchSymb | Identifies class or source of the SecurityID (48) value. Required if SecurityID is specified. |

### MarketDataSnapshotFullRefresh (35=W, outgoing)

Message sent by LMAX containing changes in market data

| Tag | Field | Req | Type | Values | LMAX description |
|---|---|---|---|---|---|
| 262 | MDReqID | N | STRING |  | Unique identifier for Market Data Request. MDReqId is restricted to a maximum of 16 lowercase hexadecimal characters [0123456789abcdef] with no leading 0 cha... |
| 48 | SecurityID | Y | STRING |  | LMAX Security identifier. Requires SecurityIDSource=8. |
| 22 | SecurityIDSource | Y | STRING | 8=ExchSymb | Identifies class or source of the SecurityID (48) value. Required if SecurityID is specified. |
| 268 | NoMDEntries | Y | group |  |  |
| 269 | &nbsp;&nbsp;MDEntryType | Y | CHAR | 0=Bid, 1=Offer, 2=Trade | Type Market Data entry. |
| 270 | &nbsp;&nbsp;MDEntryPx | N | PRICE |  | Price of the Market Data Entry. |
| 271 | &nbsp;&nbsp;MDEntrySize | N | QUANTITY |  | Quantity or volume represented by the Market Data Entry. |
| 272 | &nbsp;&nbsp;MDEntryDate | N | UTC_DATE |  | Date of Market Data Entry. |
| 273 | &nbsp;&nbsp;MDEntryTime | N | UTC_TIME |  | Time of Market Data Entry. |

### MarketDataRequestReject (35=Y, outgoing)

Message sent in response to an invalid Market Data Request.

| Tag | Field | Req | Type | Values | LMAX description |
|---|---|---|---|---|---|
| 262 | MDReqID | Y | STRING |  | Unique identifier for Market Data Request. MDReqId is restricted to a maximum of 16 lowercase hexadecimal characters [0123456789abcdef] with no leading 0 cha... |
| 281 | MDReqRejReason | N | CHAR | 0=Unknown symbol, 1=Duplicate MDReqID, 2=Insufficient Bandwidth, 3=Insufficient Permissions, 4=Unsupported SubscriptionRequestType, 5=Unsupported MarketDepth, 6=Unsupported MDUpdateType, 7=Unsupported AggregatedBook, 8=Unsupported MDEntryType, 9=Unsupported TradingSessionID, A=Unsupported Scope, B=Unsupported OpenCloseSettleFlag, C=Unsupported MDImplicitDelete | Reason for the rejection of a Market Data request. |
| 58 | Text | N | STRING |  | Free format text string (Note: this field does not have a specified maximum length) |

## 5. Sequencing and recovery

| Item | Rule | Source |
|---|---|---|
| Sequence scope | FIX MsgSeqNum per session, both directions; reset to 1 on every logon (141=Y) | MD prologue, Session Resetting |
| Recovery | "The LMAX FIX Market Data Gateways do not support any FIX message recovery." | MD prologue, Recovery |
| LMAX's own resend requests | answered messages are ignored by LMAX | MD prologue, Recovery |
| Book recovery | none needed for the book: every W is the whole book (section 6) | MD MarketDataRequest 265, 266 |
| Snapshot alignment | not applicable: no incremental stream to align | MD |
| State to clear on reconnect | the handler's LevelWindow and Mbp2Mbo state: the next W of each instrument goes out after a book-clear record (`l3_chr_v2_t`) as recovery records | handler design, `kaspar_mapping.md` |

What happens on a MsgSeqNum gap is not stated (section 10, item 3). QuickFIX
answers a gap with a ResendRequest and holds every later message until the gap
is filled; if LMAX neither resends nor gap-fills, the feed stalls. The handler
watches for the ResendRequest leaving (QuickFIX `toAdmin`), sends a gap record
(`l3_gap_v2_t`) to every book at once, considers the gap closed once books
arrive and no ResendRequest has gone out for `gap_timeout_ms` (default 2 s),
and otherwise drops the connection after twice that; QuickFIX logs on again with
141=Y and every instrument resynchronises from its next W. `stall_ms`
(default off) does the same when no W at all arrives for that long.

## 6. Semantics

| Item | LMAX | Source |
|---|---|---|
| Book type | price level, aggregated: "only aggregated book entries are supported", "one book entry per side per price" | MD MarketDataRequest, 266 |
| Depth | MarketDepth 0 = full depth, at most 20 levels; 1 = top; 2-20 = best N | MD MarketDataRequest, 264 |
| Update type | MDUpdateType 0 (Full) is the only value | MD MarketDataRequest, 265 |
| Message | W, MarketDataSnapshotFullRefresh, for every update; no X | MD MsgType values |
| Entries | MDEntryType 0 bid, 1 offer, 2 trade; MDEntryPx, MDEntrySize, MDEntryDate, MDEntryTime all optional in the group | MD W, group 268 |
| Order of entries | not stated; the handler sorts each side by price | MD |
| Subscription | SubscriptionRequestType 1 (snapshot + updates) or 2 (unsubscribe) | MD 263 |
| MDReqID | at most 16 lowercase hex digits, no leading zero, unique among current requests | MD 262 |
| Trades | MDEntryType 2 in W; no aggressor side, no trade id | MD W |
| Trading states | no SecurityStatus message in this API | MD message list |
| Implied, hidden, iceberg | not mentioned | MD |
| Queue priority on modify | not applicable: no orders, only levels | MD 266 |

The W description reads "Message sent by LMAX containing changes in market data"
(MD W). With MDUpdateType 0 the only value and the name FullRefresh, the handler
reads every W as the complete book to the requested depth (section 10, item 8).
The live check tests this reading: the book rebuilt from the records is
compared with every W.

## 7. Mapping to Kaspar

LMAX gives price levels, so the handler goes through `sim::Mbp2Mbo`, as `sim/`
does for Hyperliquid's whole 20-level books (`sim/src/HyperliquidSync.cpp`):
`LmaxMdSync` writes each W into a `sim::LevelWindow` and sends Mbp2Mbo only the
levels that changed; Mbp2Mbo splits each level into synthetic orders of one
chunk.

| LMAX | Kaspar record | Notes |
|---|---|---|
| first W of an instrument on a connection | `l3_sst_t` status 17 once per process (Mbp2Mbo::start), then `l3_mbo_v2_t` adds with `recovery` set, `endOfEvent` on the last, `l3_eob_t` | after a reconnect, preceded by `l3_chr_v2_t` to that book |
| later W | `l3_mbo_v2_t` adds (level grew or appeared) and deletes (level shrank or left the 20) | Mbp2Mbo: growth joins the back, shrinkage leaves from the back |
| trade entry (269=2) not seen in the previous W | `l3_mbo_trd_v2_t` + delete per chunk at the front of the level | applied before the same W's book; side inferred (section 10, items 1-2) |
| trade entry inside the previous spread | nothing (counted `trades_unclassified`) | |
| MsgSeqNum gap (ResendRequest sent) | `l3_gap_v2_t` to every book | |
| logout, disconnect | `l3_gap_v2_t` to every book; next W per instrument as above | |

- Priority translation: none from LMAX (levels only). Mbp2Mbo's queue model applies:
  chunks added at the back, removed from the back on a size decrease, from the front
  on a trade. This is a modelling choice of `sim/`, not LMAX behaviour (section 10,
  item 5).
- Trade prints: Mbp2Mbo emits per-chunk executions only, no `l3_mbp_trd_t` (as for
  the crypto feeds in `sim/`). Volume from prints is therefore not available.
- Order identifiers: Mbp2Mbo's counter, unique per instrument object; each
  instrument has its own Mbp2Mbo and OB, and `l3_mbo_trd_v2_t` is routed with the
  instrument's records to its OB only.
- Instrument identifier: LMAX SecurityID (48) to the Kaspar asset id
  (`config/instruments_demo.csv`, `sec_id`).
- Quantity unit: one chunk = `chunk` MDEntrySize units; 0.1 for spot FX, the
  minimum tradable number of contracts (CS p. 2), if MDEntrySize is in contracts
  (section 10, item 4).
- `endOfEvent`: on the last record of each W (one W is one event); `l3_eob_t`
  after it.
- `transactTime`: SendingTime (52) of the W, made strictly increasing by Mbp2Mbo.
- Trading status: 17 always; LMAX sends no status (section 10, item 6).

## 8. Instruments

| Item | Value | Source |
|---|---|---|
| Identifier | SecurityID (48) with SecurityIDSource 8; numeric text, e.g. 4001 | MD Instrument component; PD |
| Demo list | EUR/USD 4001 (price_increment 0.00001, quantity_increment 1000), USD/JPY 4004 (0.001, 1000), XAU/USD 100637, XTI/USD 100800, UK100 100089 | PD |
| Contract size, spot FX | 10,000 units of the base currency | CS pp. 1-3 |
| Minimum quantity | 0.1 contract | CS pp. 1-3 |
| Tick | 0.00001 for most pairs, 0.001 for JPY pairs, 0.0001 for CZK pairs | CS pp. 1-3 |
| Definitions over FIX | not in the Broker Market Data API (no SecurityDefinition); the trading API's SecurityDefinitionRequest is defined only in its FIX.4.2 version | MD; TR FIX.4.2 |

The instruments file is written by hand from PD and CS. PD's quantity_increment
(1000, base currency) equals CS's 0.1 contract of 10,000, which supports reading
FIX quantities as contracts, but PD is a REST API and does not state the FIX unit.
Only EUR/USD and USD/JPY are in the example file; other pairs need their
SecurityIDs from LMAX.

## 9. Pitfalls

`pitfalls.md`, item by item.

**1. Reading the specification**

- Several documents, versions: MD is the only market-data spec; CO (2019) and CG
  (current) differ on host names; CO p. 8 says FIX 4.2 for demo (item 7).
- Errata, change tables: MD changelog read; release 272 changed only the
  descriptions of V and W.
- Tables across page breaks: not applicable (JSON). The tables in section 4 are
  generated from the JSON.
- Offsets, length fields: not applicable (tag=value).
- Reserved fields: none.
- Enumerations in another section: MD lists values on the field; used as is.
- Names differing between table and prose: W is described as "changes in market
  data" while named FullRefresh (item 8).
- Examples contradicting tables: MD has no examples.
- Two protocol layers: FIX session and application messages are separate
  message types; handled by QuickFIX.
- Repeated message names: none.
- Variable-length field at the end: none.
- Odd integer widths: none.

**2. Framing and encoding**

- Endianness, signedness, fixed point: not applicable; prices and sizes are FIX
  decimals.
- Prices: decimal text; converted to ticks with the instrument's tick, off-grid
  entries counted and skipped (`bad_entries`).
- Null values: optional fields simply absent.
- Strings: FIX text.
- Several messages per packet, split messages: handled by QuickFIX.
- Timestamps: SendingTime UTC; MDEntryDate/MDEntryTime UTC (UTCDateOnly,
  UTCTimeOnly).

**3. Sequencing and recovery**

- Scope: per FIX session, all instruments together.
- A/B arbitration: none (one TCP session).
- Gap rule: QuickFIX's; behaviour of LMAX on a ResendRequest unknown (item 3).
- Recovery mechanism: none at LMAX; each W is complete.
- Snapshot alignment: not applicable.
- Old snapshot: not applicable.
- State to clear: done on logout and on a forced reconnect (section 5).
- Recovery paths exercised: gap filled, gap not filled, disconnect
  (`test_md_feed.cpp`).
- Session behaviour: reset on every logon, daily 17:02 New York schedule.

**4. Book semantics**

- MBO or price level: price level, aggregated, by price.
- Level index updates: not applicable (whole book each time).
- Depth-limited: 20 levels; a level that leaves the 20 is deleted, one that comes
  back joins at the back.
- Queue priority on modify: not applicable; Mbp2Mbo's model (item 5).
- Implied, iceberg, hidden: not stated.
- Trades also in book deltas: yes in effect; the next W shows the level after the
  trade. Handled by applying the trade first and letting the W restore LMAX's
  size (Mbp2Mbo's late-trade rule; test `TradeThenRefilledLevelKeepsLmaxSize`).
- Trade side: not given; inferred (item 2).
- Trading states, crossed books: no states; crossed or locked W counted
  (`crossed`), OB would see status 17.
- Book clears: on reconnect (`l3_chr_v2_t`).

**5. Prices, quantities, instruments**

- Price or yield: price.
- Quantity multiplier: contract of 10,000 base currency (CS); MDEntrySize unit
  unconfirmed (item 4).
- Decimals sentinels: none.
- Price-dependent ticks: no; per-instrument ticks (CS).
- Instrument definitions: by hand (section 8).
- Lifecycle: spot FX has no expiry; not handled further.

**6. Behaviour the specification omits**

- Update rate options: none in MD. Not measured (no live run).
- History on subscribe: whether the first W carries an old trade: assumed
  possible; trades in a connection's first W are not applied.
- Whole book or top: up to 20 levels as requested.
- Order of messages within an event: one W is one event.
- Connection limits: 8192-byte messages; heartbeats; daily 17:02 New York
  disconnect.

**7, 8.** Post-trade and order entry: see `../../post_trade/lmax/feed.md` and
`../../order_entry/lmax/feed.md`.

## 10. Ambiguities

| # | Question | Reading taken | Alternatives | Resolved by |
|---|---|---|---|---|
| 1 | Does a W repeat the last trade until a new one occurs, or carry only trades since the previous W? (MD W) | A trade entry equal in price, size, MDEntryDate and MDEntryTime to one in the previous W of the instrument is the same trade; trades in a connection's first W are not applied | carry only new trades (then apply every entry); repeat a fixed recent history (dedupe by a longer window) | review.md decision 1; live check |
| 2 | Which side was the aggressor? (MD W has no side) | At or above the previous W's best offer: buy; at or below the best bid: sell; inside the spread: not applied | ignore trades for the book (book-only Mbp2Mbo); infer from the level that shrank | review.md decision 2 |
| 3 | What does LMAX do with a client's ResendRequest on the market-data session? (MD Recovery says only that recovery is not supported) | Unknown; reconnect with 141=Y if the gap is not closed within 2 x `gap_timeout_ms` | LMAX gap-fills at once (then no reconnect happens); LMAX ignores it (then reconnect after 4 s by default) | review.md decision 3; live check |
| 4 | Unit of MDEntrySize: contracts or base currency? (MD 271 "Quantity or volume") | contracts; chunk 0.1 | base currency, chunk 1000 (DC 32 says LastQty "in base currency ... as default, can be changed to LMAX Contracts" for drop copy) | review.md decision 4; first W of the live check prints sizes |
| 5 | Queue model for synthetic orders | Mbp2Mbo's (back on growth, back on shrink, front on trades) | none from LMAX | review.md decision 5 |
| 6 | Trading status: LMAX sends none; the FX week closes Friday 17:00 New York | status 17 always (OB may see a frozen or one-sided book over the weekend; `one_sided` counted) | map the session schedule to status 4/21 from the clock | review.md decision 6 |
| 7 | FIX version on demo: CO p. 8 says FIX 4.2, MD defines FIX.4.4 only | FIX.4.4 | FIX 4.2 (would fail at logon with a BeginString error, visibly) | review.md, settled at first logon |
| 8 | W described as "changes in market data" | complete book to the requested depth (MDUpdateType 0 only) | partial book: the handler would delete levels LMAX still has | live check compares rebuilt and sent books after every W |
| 9 | MDEntrySize 0 or missing on a level | entry skipped and counted (`bad_entries`) | treat as level removal | live check counter |
| 10 | Price precision of W (MDEntryPx) beyond the tick | off-grid entries skipped and counted | round to the tick | live check counter |
