# Cboe Europe Multicast PITCH (BXE, CXE): review list

Built from section 10 of `feed.md`. Each of its 28 items appears once below.
Citations as in `feed.md`: EU is the Multicast PITCH Specification 6.69, PM the
Participant Manual v59, FIX the FIX Specification 3.53. No Cboe Europe sample
data exists publicly, so nothing below was settled by data.

## Needs your decision

### 1. Which Trade - Extended messages count as volume

**Question.** On the BXE and CXE main feeds, Trade - Extended (0x32) carries
negotiated trades brought on exchange (EU 4.9.4, p.23), executions against
retail (RLP) orders, which are hidden on the main feed (EU 1.9, p.9), and
amendments: a Cancellation-flagged copy of the original trade, then a
Modification-flagged amended trade (EU 2.6, p.11). Kaspar counts volume from
prints and has no record that withdraws one.

**Options.**

- (a) No print from Trade - Extended.
- (b) A print when the MMT flags say an on-book, price-forming, original trade:
  level 1 in {`1` CLOB, `3` dark, `5` periodic auction}, level 3.2/3.10 `-`
  (not negotiated), level 3.4 `-` (not an amendment or cancellation), level 3.8
  `P`, price > 0 (EU 4.17, pp.32-34).
- (c) A print for every Trade - Extended with a price.

**Recommendation.** (b), implemented as the default
(`ExtendedPrints::on_book`). It counts RLP executions, which reach the main feed
only as Trade - Extended, and keeps negotiated and off-book trades and both
halves of an amendment out, so an amended trade stays counted once, through
its original Order Executed or Trade. Which level-1 value RLP executions carry
is not stated; if it is not one of the three, they are missed.

**What depends on it.** Volume on BXE and CXE; nothing in the book.

### 2. Transaction Begin and End as event boundaries

**Question.** EU defines Transaction Begin/End for any PITCH message (EU
4.11-4.12, p.26) but states their use only for the Retail Liquidity Feed (EU
1.9, p.9), and Appendix A omits them (p.76). Are they sent on BXE and CXE?

**Options.**

- (a) Honour them where they appear: the block is one event, one
  `transactTime`, `endOfEvent` on the last record of each book in the block;
  every other message is its own event.
- (b) Ignore them: every message is its own event.

**Recommendation.** (a), implemented as the default (`honour_transactions`).
If they are never sent, (a) and (b) give the same output. If they are, (a)
stops OB from checking a book for a cross half way through one aggressor's
sweep. The per-book `endOfEvent` is a choice the mapping rules do not cover:
they say "the last record of each transaction", which leaves a book whose
records are not last without one.

**What depends on it.** OB's cross checks and the lights' event processing.

### 3. Trading phases U, S and R

**Question.** PM 2.16 (pp.32-33) settles most phases (Settled below). Three
remain open:

- `U` Post Market Crossing: the Cboe Closing Cross, a separate at-limit book
  uncrossing every 15 s (PM 2.1, p.12), while continuous trading is closed.
- `S` Suspension: all orders cancelled, no order entry.
- `R` Off-book Reporting: continuous trading unavailable, no order entry.

**Options.** `U`: 21 PreOpen (crossed book allowed, OB does not match) or 4
Closed. `S`: 18 NotAvailableForTrading or 2 TradingHalt. `R`: 18 or 4.

**Recommendation.** `U` 21, `S` 18, `R` 18 (implemented). 21 for `U` because if
3C orders appear on the feed (decision 5) the book can cross between
uncrossings; 18 for `S` matches the U.S. handler's `S`.

**What depends on it.** Whether OB matches simulated orders, and whether a
crossed book is reported as an error.

### 4. DXE (Amsterdam)

**Question.** The task names "BXE and CXE, London and Amsterdam". BXE and CXE
are the UK venue in London; the NL venue in Amsterdam is DXE (EU 1.6, p.7).
`en::x` has `CBOEBXE` and `CBOECXE`, no DXE code.

**Options.**

- (a) BXE and CXE only.
- (b) Also DXE: append `CBOEDXE` to `chutil/gencode/enum/def/exch_code.enum`
  and regenerate; DXE's feed is the same protocol with its own addresses
  (EU 7.5, p.47; 8.4, p.68).

**Recommendation.** (a) now; (b) when DXE is wanted. The handler needs no change
for DXE other than the venue code; this run could not add it (writes were
limited to this directory).

### 5. Periodic Auction Book and Cboe Closing Cross orders on the BXE feed

**Question.** The Periodic Auction Book and the Cboe Closing Cross are separate
lit books "provided in the BXE environment" (PM 2.1, pp.11-12). EU does not say
whether their orders appear as Add Orders on the BXE feed, under the same
symbol as the Integrated Book. If they do, OB merges two books into one.

**Options.**

- (a) Add Orders are Integrated Book orders only; periodic auctions publish
  only Auction Update (PM 2.1, p.11: "Indicative uncrossing price and volume
  are disseminated") and their fills as executions.
- (b) Add Orders may belong to any of the three books; the handler would need
  a way to tell them apart, which the messages do not carry.

**Recommendation.** (a), taken by the handler. Check on the first UAT or
production capture: a BXE book crossed during phase `T` would show (b).

**What depends on it.** The whole BXE book; CXE is not affected.

## Settled

| Item | How it was settled |
|---|---|
| Order Executed, Order Executed at Price/Size, Trade Long, Trade Short: tables one byte longer than the examples | Tables: the examples lack exactly the MMT level 3.9 character added with MMT v4.1 (Revision History 6.48-6.49, p.96). Not confirmed on data; a short message is counted as malformed |
| Trade - Extended example Length 67 vs 68 bytes | 68: the bytes listed and the table agree |
| Gap Response example without Count | Table: Length 10 needs Count |
| Spin Finished example with a Status byte | Table and Length byte: 6 bytes |
| Execution Id "12 characters, base 36" | Display form of the 8-byte binary: the p.82 example's Order Id is `631WC4000005` in base 36 (decode test) |
| Modify Order priority | FIX 5.3, p.27: price change or size increase loses priority = OB's rule; change record |
| Reduce Size priority | FIX 5.3, p.27: a decrease keeps priority = OB's rule |
| Order Id scope | Tables keyed by (unit, Order Id); an instrument lives in one unit (EU 1.3, p.6), so ids reaching one OB are unique; venue id kept, as the U.S. decision |
| Trading phases T, V, N, O, E, M, P, I, Y, C, H | PM 2.16, pp.32-33: `V` (static collar) and `N` (no dark reference price) keep continuous trading, 17; auctions and extensions 21; `C` 4; `H` 2; `Y` from EU 4.13 only, 21 |
| Spin timestamps against OB's recovery checks | CHR to the unit's books, then strictly increasing `transactTime` per unit (`kaspar_mapping.md`, `sim/Mbp2Mbo.cpp`) |
| Spin usable only with all later messages buffered | EU Appendix C, p.85; `spin_usable()` |
| Auction Summary repeats auction executions | Executions carry auction trading modes (EU p.79 `K`, p.80 `O`); Auction Summary sends no print |

## Low impact

| Item | Why |
|---|---|
| Trade - Extended example Cboe Flag `R` | Example text only |
| Reduce Size example offset 448,792 ns vs text 449,000 | Example text only |
| Trading Status example reserved bytes 00 | Reserved |
| Trade Break removed in 6.9 yet still listed | No record is sent for it either way |
| Iceberg reload shown as Add or size-up Modify | OB gives the reloaded quantity new priority in both cases, as the venue does |
| Time base after a spin | Timestamps only, under 1 s |
| Starting sequence and rollover not stated | A start above 1 costs one spin; rollover needs 4.29 billion messages in a unit |
| Unsequenced messages on multicast | Counted and ignored; none is described in EU |
| Index Quote 8 vs 6-byte timestamp | Indices feed decommissioned (6.67, p.97) |
| SI Expanded Add price decimals | SI quote feed not handled |
| DST change days | Sundays; no trading |
