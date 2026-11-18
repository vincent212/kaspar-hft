# Cboe U.S. Equities PITCH: review list

Built from section 10 of `feed.md`. Each of its 29 items appears once below.
Citations as in `feed.md`: US is the PITCH specification 2.41.87, Sample is the
2023-08-22 BZX capture.

## Needs your decision

### 1. Order Id scope and the Kaspar `orderID`

**Question.** Is the PITCH Order Id unique within an exchange for the day, or only
within a unit? US says "day-specific" and nothing more (US, Add Order Message
Fields).

**Options.**

- (a) `orderID` = the venue Order Id unchanged; one handler instance per exchange.
- (b) The handler assigns its own 64-bit `orderID` from a counter, keyed by (unit,
  Order Id) in the per-unit order table it already keeps.

**Recommendation.** (a), after a check on the Sample that no Order Id is live in
two units at the same time. Reason: venue ids stay traceable in logs and replays,
and the check is a one-off count. If the check finds any collision, (b), which is
correct for either scope at the cost of one field in the order table.

**What depends on it.** The order-table key, the routing of `l3_mbo_trd_v2_t` (it
has no `securityID` and is routed by `orderID`), and whether two live orders in
different units can overwrite each other in OB.

## Settled

| Item | How it was settled |
|---|---|
| Unit-header example: Hdr Length 49 or 50 | Sample: Hdr Length = UDP payload in 517,719 of 517,719 frames |
| Add Order Short example shows an 8-byte price | Sample: 399,578 short Adds of 26 bytes decoded with the table's 2-byte price, none malformed |
| Trade Expanded offsets and total length | Equities: 43 bytes, no Trade Condition, as the equities example; the options layout is not decoded by this handler |
| Trade Short options example: 33 or 34 bytes | Table and the example's Length byte agree on 34; options only |
| Auction Update prices typed Binary | 4 implied decimals: US examples ($102.50 = 1025000) and Sample (on the scale of Auction Summary prices) |
| Time message 6 or 10 bytes | Decoded by Length; Sample: all 307 Time messages 6 bytes (before 11/02/26) |
| Modify, Maintain Priority = 0, same price, same or smaller size | Delete then Add, same `orderID`, `endOfEvent` on the Add (`kaspar_mapping.md`); Sample: 150 of 51,501 modifies |
| Modify, Maintain Priority = 1, larger size or new price | Change record; OB resets priority: known deviation (`kaspar_mapping.md`); Sample: 0 cases |
| Reduce Size priority not stated | OB's rule (smaller size keeps place) assumed, as `kaspar_mapping.md` prescribes when the venue is silent |
| Order Executed at Price/Size beyond the booked size | `displayQty` = Remaining Quantity (US text; Sample: 20 of 130 differ from booked − executed); a resulting size-up resets priority in OB, the only outcome the records allow |
| `endOfEvent` without transaction markers | On the last record of every venue message (`kaspar_mapping.md`) |
| Spin requested at a sequence that is not an image | Request only Spin Image Available sequences; US states such a spin ends exactly there |
| Spin Response Order Count counts Adds only | End the spin on Spin Finished (US, Spin Response Message Fields) |
| Gap replay before the Gap Response, in blocks | Apply by sequence, drop duplicates (US text; Sample: blocks of 100) |
| Trading states and crossed books | `T`, `E`, `P`, `L`, `O` → 17; `A`, `Q` → 21; `H` → 2; `S` → 18. State meanings from US Table 1; 17 as the only matching state from `kaspar_mapping.md` |
| Auction Summary repeats auction executions | Prints come from the execution messages (auction fills carry prefix `C`, `M`, `P`, US Execution IDs); Auction Summary sends none |
| Add Flags bit 0 | Ignored; Sample: set on all 409,937 Adds |
| Trading Status 0x31 options layout | Feeds are separate per exchange; the handler reads equities units only (US, Multicast PITCH Feed Descriptions) |

## Low impact

| Item | Why |
|---|---|
| BYX Auction Update reference price: populated or zero | Auction Update produces no record |
| Auction Summary example symbol typo | Example text only; decoding unaffected |
| Time Reference inside equities spins | Accepting it only refreshes the unit's time state |
| Midnight and DST for BYX and EDGA | They run from 6:00 a.m. ET, outside the repeated 01:00-01:59 hour; America/New_York midnight of the trade date is exact |
| Reserve refresh: old id deleted or executed to 0 | The handler removes the old id on either path; the output is the same |
| Aggressor side 0 on Trade messages and auction fills | `aggresorSide` does not move the book or volume |
| Trade Break has no record | Book unaffected; only a broken print stays in volume |
| Reg SHO Action, Auction Update, Retail Price Improvement not sent | None changes the book or the prints |
| Spin Request cross-reference error | Documentation only; the rules are under Spin Servers |
| Instrument Definition Response counts an undefined message | Options only; the handler does not use the instrument spin |
