# LMAX Broker Market Data: review list

Built from section 10 of `feed.md`. Citations as there: MD is LMAX's Broker
Market Data `fix-spec.json` (release 272, 2019-11-13), CO the Connectivity
Overview of 21 January 2019, CS the Spot FX contract specification, DC the MTF
Drop Copy spec. None of these was checked against LMAX's live feed: there was no
demo key file (README, Live check).

## Needs your decision

### 1. Trades repeated across snapshots

**Question.** Each W may carry trade entries (MDEntryType 2) with price, size and
optionally date and time, and nothing else (MD W, group 268). MD does not say
whether a W carries only the trades since the previous W or repeats the last
trade until another happens.

**Options.**

- (a) A trade entry equal in price, size, MDEntryDate and MDEntryTime to one in
  the previous W of the same instrument is the same trade and is not applied
  again; trades in a connection's first W are not applied. (Implemented.)
- (b) Apply every trade entry of every W.
- (c) Do not request trades (`trades = false`): the book is unaffected, queue
  positions of simulated orders are not advanced by executions.

**Recommendation.** (a) until the live check shows which is true; it is correct
under both behaviours except when LMAX omits MDEntryTime and two identical trades
occur in consecutive snapshots, which (a) would merge. The live check prints the
first snapshots of each instrument and counts `trades_repeated`: if that count is
0 over 30 minutes while `trades_applied` is not, LMAX sends each trade once and
(a) and (b) agree.

**What depends on it.** Simulated fills: each applied trade removes chunks from
the front of the level in OB.

### 2. Aggressor side of a trade

**Question.** A trade entry has no side (MD W). Mbp2Mbo needs to know which side
of the book was hit.

**Options.**

- (a) Compare with the previous W: at or above the best offer, a buy; at or below
  the best bid, a sell; inside the spread, not applied (counted
  `trades_unclassified`). (Implemented.)
- (b) The level whose size fell at the trade price in the current W.
- (c) Do not apply trades to the book (option 1c).

**Recommendation.** (a). It needs only the previous book, which the handler
has, and leaves trades that cannot be classified out rather than guessing.
`trades_unclassified` in the live check shows how many that is.

**What depends on it.** Which side's chunks execute in OB.

### 3. Gap handling on the market-data session

**Question.** MD says the gateway "do[es] not support any FIX message recovery"
(MD prologue, Recovery). It does not say what LMAX does with a client's
ResendRequest. QuickFIX holds every message after a gap until it is filled.

**Options.**

- (a) Send a gap record to every book; treat the gap as closed once books arrive
  and no ResendRequest has gone out for `gap_timeout_ms`; otherwise drop the
  connection after twice that and log on again with 141=Y. (Implemented; 2 s.)
- (b) Drop the connection at the first ResendRequest.
- (c) Let QuickFIX wait.

**Recommendation.** (a). If LMAX gap-fills, the feed carries on without a
reconnect (test `GapFilledByTheGatewayKeepsGoing`); if it does not, the books
are back within about 4 s plus the reconnect interval (test
`GapNotFilledForcesAReconnectAndAResync`). (c) can stall the feed indefinitely.

**What depends on it.** How long OB holds a stale book after a lost message.

### 4. Size unit of MDEntrySize

**Question.** MD 271 is "Quantity or volume represented by the Market Data
Entry". OrderQty on the trading API is in contracts (TR 38: "Quantity of
contracts ordered"); a spot FX contract is 10,000 base currency with a 0.1
minimum (CS pp. 1-3). The MTF drop copy reports LastQty "in base currency ...
Disseminated in Notional as default, can be changed to LMAX Contracts" (DC 32), so
LMAX uses both units.

**Options.**

- (a) Contracts: `chunk = 0.1` (one chunk = 1,000 base currency). (Configured.)
- (b) Base currency: `chunk = 1000`.

**Recommendation.** (a), and check the first W the live check prints: EUR/USD
sizes of the order of 1 to 100 mean contracts; of 10,000 to 1,000,000 mean base
currency. Only `config/instruments_demo.csv` changes.

**What depends on it.** Every chunk count in OB and the size of simulated orders,
which must be in the same chunks.

### 5. Queue model for a price-level feed

**Question.** LMAX gives aggregated levels only (MD 266). OB needs orders.

**Options.**

- (a) Mbp2Mbo as used for the crypto feeds: a level that grows adds chunks at the
  back; one that shrinks removes chunks from the back; a trade removes them from
  the front. (Implemented.)
- (b) Remove shrinkage from the front (treats every decrease as a fill).

**Recommendation.** (a), the existing `sim/` convention, so that LMAX results are
comparable with the other price-level venues.

**What depends on it.** Simulated queue position and fill timing.

### 6. Trading status

**Question.** The Broker Market Data API has no status message. Spot FX trades
from Sunday evening to Friday 17:00 New York.

**Options.**

- (a) Status 17 always (Mbp2Mbo::start). (Implemented.)
- (b) Derive status 4 (closed) and 21 (pre-open) from LMAX's trading-hours
  schedule.

**Recommendation.** (a) for now; with no messages over the weekend the book is
frozen, not crossed. (b) needs LMAX's holiday calendar
(https://www.lmax.com/exchange/holiday-calendar/) and is a separate piece of work.

**What depends on it.** Whether OB treats a crossed book as an error.

### 7. Venue code

**Question.** `chutil/gencode/enum/def/exch_code.enum` has no LMAX code
(`kaspar_mapping.md`, Venue codes). This work was limited to the LMAX
directories, so it was not added.

**Options.** (a) Append `LMAX` to the enum and regenerate; (b) keep a
configurable code (`venue = UNI` in the example config).

**Recommendation.** (a) before any use with OB or the SOM.

**What depends on it.** The venue in every record, SOM routing for the order
manager, and post-trade reports.

### 8. Whether a W is the whole book or only the changed levels

**Question.** MD describes W as "Message sent by LMAX containing changes in
market data" (MD W), while its name is FullRefresh and MDUpdateType 0 (Full) is
the only value MD 265 allows (`feed.md` section 10, item 8).

**Options.**

- (a) Every W is the complete book to the requested depth; a level absent from
  a W is gone. (Implemented.)
- (b) A W carries only the levels that changed; absent levels are kept.

**Recommendation.** (a). The message name and the only allowed MDUpdateType both
say full book. If (b) were true, (a) would delete levels LMAX still has and the
book would be thin, not crossed, so no invariant would catch it; the live check
catches it only indirectly, by the number of levels per W staying near the
requested depth (printed in the summary as `levels`).

**What depends on it.** Every level in OB.

### 9. A book entry with MDEntrySize 0 or no size

**Question.** MDEntryPx and MDEntrySize are optional in group 268 (MD W). MD
does not say what an entry with size 0 or no size means.

**Options.**

- (a) Skip the entry and count it (`bad_entries`); under decision 8a the level
  is absent from the book. (Implemented.)
- (b) Treat it as an explicit removal of that level (the same result under 8a,
  different under 8b).

**Recommendation.** (a); it only matters if decision 8 goes to (b). The live
check reports `bad_entries` per instrument.

**What depends on it.** Levels in OB if decision 8 is (b).

## Settled

| Item | How it was settled |
|---|---|
| Where LMAX's FIX specs are | docs.lmax.com bundle lists `fix-api/<api>/fix/latest/fix-spec.json`; the PDFs at the same paths are placeholders |
| No incremental refresh | MD MsgType values (0-5, A, V, W, Y, xt) and MarketDataRequest 265 (0 = Full only) |
| QuickFIX dictionary | LMAX publishes none; generated from MD by `tools/fixspec2qf.py`, checked by parsing and validating every message of the spec (`LmaxDictionary.*`) |
| Demo host and port | CG "FIAT Demo": `fix-marketdata.london-demo.lmax.com:443`; CO p. 8 agrees |
| TLS without SNI | `openssl s_client -noservername` to the demo host completed the handshake (2026-11-18); QuickFIX sends no SNI |
| SecurityIDs | PD lists EUR/USD 4001 and USD/JPY 4004 with their increments |
| FIX version on demo (CO p. 8 says 4.2) | FIX 4.4 taken from MD; a wrong BeginString fails at logon, visibly |
| Depth | MarketDepth 20 (MD 264: maximum 20) |
| MDReqID format | lowercase hex, no leading zero, at most 16 (MD 262); `md_req_id()` and its test |
| Password on disk | QuickFIX's FileLog and FileStore would write the Logon with 554; `RedactLog` and `RedactStore` replace it (test `PasswordIsNotWrittenToTheStoreOrTheLog` in order_entry) |

## Low impact

| Item | Why |
|---|---|
| TestPing (999) in Logon, not in MD's field list | Typed from the message; LMAX does not send it to clients as far as MD says |
| `xt` Trace Subscription in MsgType values, no definition | Not requested by the handler; an unknown message would be rejected by QuickFIX, not by the handler |
| QuickFIX does not reject an out-of-range enum value inside a repeating group | Observed in `LmaxDictionary.RejectsAnUnknownMessageTypeAndAMissingRequiredField`; the handler checks MDEntryType itself |
| Certificate verification | QuickFIX's default verify level does not check the server name; the live check connects only to the demo host the guard allows |
| USD/JPY tick printed "0.001*" in CS p. 3 | Footnote text not in the document; PD gives 0.001 |
