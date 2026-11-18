# Mapping venue messages to Kaspar records

A handler expresses every venue's semantics in the existing L3 records
(`chutil/include/bfile/r_l3.hpp`). OB, TachBook and the record format are not
changed for a venue; where a venue does something the records cannot say, the
handler translates it as below, and section 7 of the feed description states the
translation. Anything that still cannot be expressed is recorded in section 10 as
a known deviation.

## Records a handler sends

| Venue event | Kaspar record | Notes |
|---|---|---|
| order added | `l3_mbo_v2_t`, `orderUpdateAction` 0 | `side` `'0'` buy, `'1'` sell; `pxd` price; `displayQty` whole units |
| order changed | `l3_mbo_v2_t`, action 1 | see priority below |
| order deleted | `l3_mbo_v2_t`, action 2 | |
| execution against a resting order | `l3_mbo_trd_v2_t` (`orderID`, `lastQty`), then the book change: action 1 with the remaining size, or action 2 when filled | same `transactTime` on both, as `Mbp2Mbo` and the CME handler do |
| the trade print | `l3_mbp_trd_t`: `securityID`, `pxd` (the trade price), `sz`, `aggresorSide` | one per trade |
| trade without an order on the book (non-displayed, off-book, cross, auction print with no per-order executions) | `l3_mbp_trd_t` only | |
| trading state | `l3_sst_t` | see trading status below |
| book cleared for one instrument | `l3_chr_v2_t` sent to that instrument's book only | |
| channel or session reset | `l3_chr_v2_t` to every book on the channel; clear the handler's own order maps | |
| gap that recovery has not yet closed | `l3_gap_v2_t` to the books on the affected channel | |
| end of a packet | `l3_eop_t` with the packet sequence number (its low 32 bits where the venue's is wider) | |

`order_flags` stays 0: nothing in Kaspar reads it.

## Priority on a change

OB decides priority from what a change does: a smaller size at the same price keeps
the order's place; a larger size or a new price sends it to the back of the queue.

| The venue says | The handler sends |
|---|---|
| the same as OB's rule, or nothing | action 1 (change). If the venue says nothing, section 10 records that OB's rule was assumed. |
| priority is reset where OB would keep it (an explicit reset flag, as in IEX DEEP+ and Cboe PITCH; a same-size or smaller modify that the venue treats as a new order) | action 2 (delete) then action 0 (add) with the same `orderID`, the same `transactTime`, and `endOfEvent` only on the add |
| priority is kept where OB would reset it (NYSE: same price, larger size) | action 1; OB resets priority. This cannot be expressed: record it as a known deviation with its frequency in the data, if measured |

## Trades and volume

- The order-level execution (`l3_mbo_trd_v2_t`) moves the book and fills simulated
  orders queued behind the executed order. It fills at the order's book price.
- The print (`l3_mbp_trd_t`) carries the price the trade happened at. When a venue
  executes an order at a price other than its book price (auction uncrossing,
  price improvement), the print has the true price.
- Volume is counted from prints only. Where a venue reports the same trades twice
  (per-order executions and an auction summary or trade report), send one print per
  trade and say in section 7 which message it comes from.

## Instruments, identifiers and quantities

- The handler's instrument loader maps each venue instrument identifier (any width:
  64-bit T7 SecurityIDs, NYSE symbol indexes, Nasdaq stock locates) to the Kaspar
  asset id, which is the `securityID` in every record.
- `orderID` must be unique among the orders a handler tracks. Where the venue's
  order identifiers are unique only per instrument or per book group (Japannext,
  HKEX), the handler composes a unique 64-bit id and states the composition in
  section 7. Where the venue has no order identifier (T7 EOBI), section 7 states the
  identifier the handler derives.
- Quantities are whole units. A venue with fractional quantities is scaled by a
  per-instrument unit (as `Mbp2Mbo` does for crypto), chosen so that sizes fit
  `uint32_t`; section 8 states the unit.
- Prices are the venue's price in its own units, converted from its fixed-point
  encoding. OB does not accept zero or negative prices: instruments that can trade
  at them (spreads, some futures strategies) are not routed to OB, and section 8
  says which.

## Event boundaries

- Where the protocol marks transactions (T7 completion indicators, Cboe Europe
  Transaction Begin and End, CME's end-of-event flag), `endOfEvent` is set on the
  last record of each transaction.
- Otherwise `endOfEvent` is set on every record. A packet boundary is not used: an
  event can span packets (seen in Japannext's data), and one packet can hold several
  events.

## Trading status

OB treats `tradingStatus` 17 as the only state in which the book can be crossed
without an error (CME's ReadyToTrade). The handler maps each venue state to the
CME `SecurityTradingStatus` value with the same meaning (17 trading, 2 halted,
21 pre-open, 4 closed, 18 not available), and section 7 lists the mapping.

## Venue codes

Each market has its own `en::x` code (`chutil/gencode/enum/def/exch_code.enum`):
IEX; NASDAQ, NASDAQBX, NASDAQPSX; NYSE, NYSEARCA, NYSEAMERICAN, NYSETEXAS,
NYSENATIONAL; CBOEBZX, CBOEBYX, CBOEEDGX, CBOEEDGA, CBOEBXE, CBOECXE, CBOEJP; JNX;
XETRA, EUREX; HKEX. A new venue appends its code at the end of the file and
regenerates with `chutil/gencode/enum/genenum.py`; existing values never change.
