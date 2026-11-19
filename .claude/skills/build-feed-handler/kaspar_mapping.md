# Mapping venue messages to Kaspar records

A handler expresses every venue's semantics in the existing L3 records
(`chutil/include/bfile/r_l3.hpp`). OB, TachBook and the record format are not
changed for a venue; where a venue does something the records cannot say, the
handler translates it as below, and section 7 of the feed description states the
translation. Anything that still cannot be expressed is recorded under *Known
deviations* in section 10 of the feed description.

## Records a handler sends

| Venue event | Kaspar record | Notes |
|---|---|---|
| order added | `l3_mbo_v2_t`, `orderUpdateAction` 0 | `side` `'0'` buy, `'1'` sell; `pxd` price; `displayQty` whole units |
| order changed | `l3_mbo_v2_t`, action 1 | see priority below |
| order deleted | `l3_mbo_v2_t`, action 2 | |
| execution against a resting order | `l3_mbo_trd_v2_t` (`orderID`, `lastQty`), then the book change: action 1 with the remaining size, or action 2 when filled | same `transactTime` on both, as `Mbp2Mbo` and the CME handler do |
| the trade print | `l3_mbp_trd_t`: `securityID`, `pxd` (the trade price), `sz`, `aggresorSide` | one per trade; see trades below |
| trade without an order on the book (non-displayed, off-book, cross, auction print with no per-order executions) | `l3_mbp_trd_t` only | |
| trading state | `l3_sst_t`, to the books of the instruments it applies to (one instrument, or every instrument of a group) | see trading status below |
| book cleared for one instrument | `l3_chr_v2_t` sent to that instrument's book only | |
| channel or session reset | `l3_chr_v2_t` to every book on the channel; clear the handler's own order maps | |
| gap that recovery has not yet closed | `l3_gap_v2_t` to the books on the affected channel | |
| end of a packet | `l3_eob_t` to the books the packet touched; `l3_eop_t` (with the packet sequence number, its low 32 bits where the venue's is wider) to the recorder only | see event boundaries below |

Every record carries the venue's `en::x` code in `venue`. `order_flags` stays 0:
nothing in Kaspar reads it.

## What OB checks

OB applies or drops each record by the rules below; a handler that breaks one gets
a silently wrong book or an aborted process.

| Record | OB's rule | What the handler does |
|---|---|---|
| `l3_mbo_v2_t` | dropped unless `securityID` is the asset's `sec_id` or its asset id | sets `securityID` to the asset's `sec_id` |
| `l3_mbo_trd_v2_t` | has no `securityID`; matched by `orderID` against the orders the book holds, ignored otherwise | sends it to the book that holds the order |
| `l3_sst_t`, `l3_chr_v2_t`, `l3_gap_v2_t`, `l3_eob_t` | applied whatever their `securityID` | sends each only to the books it concerns |
| `l3_eop_t` | ignored | sends it to the recorder only |
| `l3_mbp_trd_t` | ignored unless OB is built with `USEMBP`, and then only counted | see trades below |
| any record with `venue` not a valid `en::x` (0, or a code the libraries were built without) | the process aborts ("invalid mkt") | see venue codes below |
| a live (non-recovery) MBO record with `sendingTime` 0 | the process aborts | sets `sendingTime` (the venue's send time, or `transactTime`) |
| an add with size 0, or any size of 10,000,000 or more (`CHOPIN_MAX_ORD_SZ`) | the process aborts | chooses the quantity unit so that sizes stay below it |
| an execution with `lastQty` 0 | aborts when it would fill a simulated order queued ahead of the executed one | never sends one; see trades without size |
| a recovery record that is not an add | the process aborts | sends a snapshot as adds |
| an add whose price is 0 or below, or at or above `maxpx` ticks | dropped (logged `BAD PRICE`) | |
| a change that moves an order off the ladder | the order is removed from the book | |

## Priority on a change

OB decides priority from what a change does: a smaller size at the same price keeps
the order's place; a larger size or a new price sends it to the back of the queue.

| The venue says | The handler sends |
|---|---|
| the same as OB's rule, or nothing | action 1 (change). If the venue says nothing, section 10 records that OB's rule was assumed. |
| priority is reset where OB would keep it (an explicit reset flag, as in IEX DEEP+ and Cboe PITCH; a same-size or smaller modify that the venue treats as a new order) | action 2 (delete) then action 0 (add) with the same `orderID`, the same `transactTime`, and `endOfEvent` only on the add |
| priority is kept where OB would reset it (NYSE: same price, larger size) | action 1; OB resets priority. This cannot be expressed: record it as a known deviation with its frequency in the data, if measured |

The `priority` field of `l3_mbo_v2_t` is not read by OB: queue position comes
from the order in which records arrive and the rule above. The CME handler fills
it with the venue's order priority, `Mbp2Mbo` with the order id. A handler sets it
to the venue's priority value where the venue sends one, and to the `orderID`
otherwise.

## Transactions, timestamps and event boundaries

`transactTime` defines a transaction for OB. All records of one venue transaction
(an aggressor's add, the executions and the deletes it causes) carry the same
`transactTime`, and consecutive transactions carry strictly increasing ones:

- OB checks the book for a cross, while the instrument is matching, when a record's
  `transactTime` is later than the previous one's, so a transaction is checked
  only after all of its records are applied. Two transactions with one stamp are
  checked as one; a stamp that goes backwards is not a boundary.
- OB holds an execution until the book change of its order arrives, and releases
  executions of earlier transactions when a later `transactTime` arrives.

Where the venue's timestamps can repeat across transactions (millisecond or
microsecond clocks, one timestamp per packet) or go backwards, the handler stamps
`max(venue time, previous stamp + 1 ns)` per transaction, as `Mbp2Mbo::stamp`
does, and section 7 says so.

`endOfEvent` is set on the last record of each transaction where the protocol
marks transactions (T7 completion indicators, Cboe Europe Transaction Begin and
End, CME's end-of-event flag), and on every record otherwise. A packet boundary
is not used: an event can span packets (seen in Japannext's data), and one packet
can hold several events. OB itself uses `endOfEvent` only for a diagnostic: on an
`l3_eob_t`, if its last book record carried `endOfEvent`, it logs a crossed book
without stopping. Recorders and replays keep the flag.

`l3_eob_t` is what moves the lights. OB publishes the end of burst for its latest
book change when the next add or change arrives, or when an `l3_eob_t` arrives;
without one, the lights see the last change of a burst only when the next record
comes. A handler sends one `l3_eob_t` after the last record of each packet to each
book the packet touched (`Mbp2Mbo` sends one per event; the CME handler one per
drained burst to every book).

## Snapshots and recovery

OB treats `l3_mbo_v2_t` records with `recovery` set as a snapshot:

- The first recovery record after a live one clears the book; the following
  recovery records rebuild it. The book leaves recovery at the next live record.
- A second snapshot with no live record between the two does not clear the book:
  orders already held are updated in place, and orders that disappeared between the
  snapshots stay. Before every snapshot after the first, send `l3_chr_v2_t` to the
  book; it clears the book and resets the timestamp checks below.
- Once OB has seen both a live and a recovery record (since the last
  `l3_chr_v2_t`), it drops a live record whose `transactTime` is not later than the
  last recovery record's, and a recovery record whose `transactTime` is earlier
  than the last live record's.

So the handler stamps every recovery record of a snapshot with one time: 1 ns
before the first live record it applies after the snapshot (a REST or snapshot
channel time, or the local clock, is not comparable with the venue's live
timestamps), and no earlier than the last live record before it. A buffered live
record that the alignment rule says the snapshot does not contain, and whose
stamp is at or below the snapshot's, is restamped by the rule above (previous
stamp + 1 ns) rather than sent with its own.

## Trades and volume

- The order-level execution (`l3_mbo_trd_v2_t`) moves the book and fills simulated
  orders queued ahead of the executed order. It fills at the order's book price.
  OB's own volume counters come from these records.
- The print (`l3_mbp_trd_t`) carries the price the trade happened at; when a venue
  executes an order at a price other than its book price (auction uncrossing,
  price improvement), the print has the true price. OB does not read prints (built
  with `USEMBP` it only counts their volume): they serve the recorder and consumers
  of the record stream, such as volume and VWAP from a recording.
- Where a venue reports the same trades twice (per-order executions and an auction
  summary or trade report), send one print per trade and say in section 7 which
  message it comes from.
- `aggresorSide` is 1 when the buyer aggressed, 2 when the seller did, 0 when the
  venue does not say (CME's `AggressorSide` values).

### Trades without size, conflated feeds, snapshot-only feeds

- A trade with no size (EBS on MDP3: `MDEntrySize` is always 0 per CME's
  documentation) is sent as a print with `sz` 0 and no execution record. Volume
  from that venue is unknown, and section 10 says so.
- A conflated feed (book states at intervals, trade summaries) has no per-order
  executions. The book goes through `Mbp2Mbo` as level changes and the trades as
  prints; `Mbp2Mbo::trade` is called only where the trade's price, size and resting
  side are known. Without executions, simulated orders fill only when a real order
  arrives at or through their price, never from their queue position; section 10
  records this.
- A feed that sends only full snapshots (LMAX's 20-level snapshots, Hyperliquid's
  books) is diffed: each snapshot is compared with the previous one and the
  changed levels are sent as level changes (`HyperliquidSync`). Trades carried in
  snapshots may lack a side or an id and may repeat in consecutive snapshots: the
  handler removes repeats (by trade id, or by time, price and size over the
  snapshots' overlap) and sends a trade without a side as a print with
  `aggresorSide` 0, not to `Mbp2Mbo::trade`.

### Price levels keyed by level number

`Mbp2Mbo` and `LevelWindow` key levels by price. A book whose updates name a level
number (MDP3 MBP books, with insert shifting lower levels down and delete shifting
them up, and implied or overlay levels) goes through an adapter that keeps the
levels in order, applies the shifts, and passes `(side, price, quantity)` per
changed level to `LevelWindow::set` or `Mbp2Mbo::level`.

## Instruments, identifiers and quantities

- The handler's instrument loader maps each venue instrument identifier (any width:
  64-bit T7 SecurityIDs, NYSE symbol indexes, Nasdaq stock locates) to a Kaspar
  asset. The asset's `sec_id` is the `securityID` in every record.
- Assets created at run time come from `RefData::add_future_asset(symbol, venue,
  sec_id, cfi, group, tick)`. `sec_id` is an `int32_t` that must be unique among
  all assets in the process (the map from `sec_id` to asset keeps the last one
  registered): the venue's numeric identifier where it fits and is unique,
  otherwise a number the loader allocates from a range that no other venue in the
  process uses. `add_future_asset` returns the existing asset, unchanged, when the
  symbol is already registered.
- `orderID` must be unique among the orders a handler tracks. Where the venue's
  order identifiers are unique only per instrument or per book group (Japannext,
  HKEX), the handler composes a unique 64-bit id and states the composition in
  section 7. Where the venue has no order identifier (T7 EOBI), section 7 states the
  identifier the handler derives. String identifiers (UUIDs) are mapped to 64-bit
  ids by the handler.
- Quantities are whole units below 10,000,000 (`CHOPIN_MAX_ORD_SZ`). A venue with
  fractional quantities (crypto, odd lots of notional) is scaled by a
  per-instrument unit (as `Mbp2Mbo`'s chunk does for crypto), and section 8 states
  the unit and the cap that results: the largest representable order is
  9,999,999 units, and a smaller quantity than one unit rounds as section 8 says.
  The order manager (`BrokerOrderBook` quantities are `int`) and the post-trade
  message use the same unit.

## Prices and the price ladder

- OB converts `pxd` to an integer tick by dividing by the asset's tick
  (`set_units`, the `min_price_increment` argument of `add_future_asset`) and
  rounding to the nearest integer. A price off the tick is rounded without a
  warning. `Mbp2Mbo` sends prices in ticks, so its assets have a tick of 1.
- An asset has one tick. Where the venue's tick depends on the price (tick tables),
  the asset's tick is the greatest common divisor of the ticks the instrument can
  trade in, and section 10 states the deviation: OB treats every multiple of it
  as a valid price, and distances in ticks (in the lights) are in that unit.
- OB's price ladder has one queue per tick per side, from 1 to `maxpx`. `maxpx`
  must be set on the asset before its OB is constructed: `add_future_asset` leaves
  it at -1 and the OB constructor then aborts. A full ladder allocates `maxpx + 2`
  queues per side, which is large for instruments with fine ticks and many books.
  Either set `ladder_ticks` (a power of two) in the OB's configuration, which makes
  each side a ring of that many ticks around the best price (orders outside it are
  kept and placed when they move inside), or create books only for the instruments
  traded.
- OB does not accept zero or negative prices: instruments that can trade at them
  (spreads, some futures strategies) are not routed to OB, and section 8 says
  which.
- With a full ladder, OB's best bid is 1 (not 0) after the bid side empties, and
  its best ask is `maxpx - 1` after the ask side empties; with `ladder_ticks` an
  empty bid side is 0. Comparisons of OB's top of book with a reference treat these
  values as an empty side.

## Trading status

OB treats `tradingStatus` 17 (CME's ReadyToTrade) as the only matching state, and
checks for a crossed book only while matching: in state 17 a crossed book is an
error; in every other state (pre-open, halt, auction) the book may cross. The
handler maps each venue state to the CME `SecurityTradingStatus` value with the
same meaning (17 trading, 2 halted, 21 pre-open, 4 closed, 18 not available), and
section 7 lists the mapping.

OB starts with matching on, except when its first record is timed between 17:00
and 18:00 America/New_York (CME's daily break), whatever the venue: then matching
stays off until an `l3_sst_t` arrives. A handler sends each book an `l3_sst_t`
with the instrument's current state before its first book record (as
`Mbp2Mbo::start` does).

## Venue codes

Each market has its own `en::x` code, defined in
`chutil/gencode/enum/def/exch_code.enum` (the file is the current list). A new venue
appends its code at the end and regenerates with `chutil/gencode/enum/genenum.py`;
existing values never change. Branches append in one shared order: check the end
of the file on the main branch before appending, and renumber on a rebase if
another venue was appended first.

The generated tables (`chutil/include/enum/exch_code.hpp`, including
`x_num_syms()` and `is_valid()`) are inline in a header, so every library that
includes them has the number of codes compiled in. After appending a code, rebuild
all the libraries, not only the handler: a library built before the change treats
the new code as invalid, and OB aborts with "invalid mkt" on the first record.
