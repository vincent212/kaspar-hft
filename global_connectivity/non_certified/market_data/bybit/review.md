# Bybit V5 public market data handler: review list

Built from section 10 of `feed.md`. Answer the items in "Needs your decision" by
number.

## Needs your decision

**1. Book topic: `orderbook.full` (200 ms) or `orderbook.50` (20 ms).**
The handler uses the full book: every level, an exact REST snapshot to start from
and to check against, and gap recovery without resubscribing. Its updates come
every 200 ms. `orderbook.50` updates every 20 ms but covers 50 levels, and a gap is
repaired only by resubscribing.
- Options: (a) full book only (as built); (b) `orderbook.50` only (DEPTH mode, as
  built); (c) both, the 50-level stream for the top and the full book below it,
  ordered by `seq` (not built).
- Recommendation: (a) for simulation and replay; (c) if the top of the book must
  move at 20 ms. (c) needs a merge rule that is not documented: `seq` orders the
  two streams, but nothing says the two are consistent at equal `seq`.
- Depends on it: how stale the simulated top of book is (up to 200 ms with (a)).

**2. Window and chunk size sent to OB.**
`LevelWindow` sends the best 1000 levels per side to `Mbp2Mbo`, which cuts each into
orders of `chunk` (linear BTCUSDT 0.1 BTC, spot 0.01 BTC in the tests and live run).
Sizes not a multiple of the chunk leave a remainder order.
- Options: (a) 1000 levels, chunks as above; (b) fewer levels (200) to keep OB small;
  (c) a chunk equal to the minimum order size (0.001 linear), giving many more OB
  orders.
- Recommendation: (a). The live run checked 1000 levels per side against OB with no
  mismatch. (c) multiplies OB orders by 100 on BTCUSDT.
- Depends on it: OB memory and update cost; the granularity of simulated queue
  position inside a level.

**3. Trading status.**
The stream carries no trading state, so the handler sends no `l3_sst_t`. A delisting
or pre-auction arrives as `u = 1` with an empty book, which the handler turns into
`l3_chr_v2_t`.
- Options: (a) no status (as built); (b) poll `GET /v5/market/instruments-info`
  `status` and send `l3_sst_t` (Trading -> 17, PreLaunch -> 21, Delivering / Closed
  -> 4).
- Recommendation: (a) for BTCUSDT; (b) before adding pre-launch or delivering
  instruments. No crossed book was seen in 126 live minutes or two archive days.
- Depends on it: OB's crossed-book handling during a pre-market auction (spot
  listings).

**4. RPI liquidity.**
Retail Price Improvement orders are absent from the book topics and the REST book;
trades against them are flagged `RPI` and sent as prints only. Simulated orders do
not see RPI liquidity.
- Options: (a) ignore RPI (as built); (b) add `orderbook.rpi.{symbol}` (50 levels,
  100 ms, RPI size per level) to the simulator's view.
- Recommendation: (a). RPI orders can only trade against retail flow, which a
  Kaspar order is not; 759 of 1,056,951 spot trades and 0 of 2,287,314 linear trades
  were RPI on 2026-11-17.
- Depends on it: the share of prints that move no book level.

**5. Archive trade file side.**
`public.bybit.com` trade files have a `side` column that is not documented; the
replay reads it as the taker side, as on the WebSocket.
- Options: (a) taker (as built); (b) maker.
- Recommendation: (a), confirm by matching one hour of WebSocket trades (`i`) with the
  file (`trdMatchID` linear, `id` spot).
- Depends on it: `aggresorSide` in replays from the archive only.

## Settled

- REST `u` is the stream's `u`: 748 of 748 live REST snapshots (two runs, spot and linear) matched
  the book rebuilt at exactly that `u`, over every level in the common range.
- `u` is per symbol and topic, not per connection.
- The REST full book returns the whole book (up to 40,806 levels per side seen),
  not 10,000 as documented; the comparison uses the range both books cover.
- `publicTrade` `S` is the taker side (documentation).
- No checksum exists for Bybit books.

## Low impact

- The documented sync procedure skips step 6 and states the staleness test in `seq`;
  the handler's test on `u` is equivalent because `u` is consecutive.
- Field order in book messages differs by product (`ts` before or after `type`); the
  JSON parser does not depend on it.
- The order-book archive is not documented; its format was read from the files.
