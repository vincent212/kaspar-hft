# Kraken spot public market data handler: review list

Built from sections 5, 6 and 10 of `feed.md`. Answer the items in "Needs your
decision" by number.

## Needs your decision

**1. Which book feeds OB: price levels (`book`) or order by order (`level3`).**
`book` at depth 1000 is public and was checked live (CRC32 on every message, REST
comparison, OB chain; README.md). `level3` gives real queue positions but needs an
API key and was not checked live: KrakenL3Sync is built from the documentation and
passes its checksum example and synthetic sequences only.
- Options: (a) `book` through Mbp2Mbo; (b) `level3` once a key is available,
  after a live run of its checksum; (c) both, `level3` for queue position and
  `book` as a cross-check.
- Recommendation: (a) now; (b) after a live `level3` run with the same checks.
- Depends on it: queue-position accuracy of simulated fills.

**2. Matching `level3` book changes to trades.**
`level3` does not say which order a trade hit. KrakenL3Sync turns a modify-down or
delete within 1 s of a trade at the same side and price into an execution, and
anything else into a cancel.
- Options: (a) this time-and-price rule; (b) treat every reduction as a cancel and
  print trades only as `l3_mbp_trd_t`.
- Recommendation: (a), to be measured on live level3 data before use.
- Depends on it: whether OB counts a fill ahead of our order as a trade or a cancel.

**3. Trading status.**
No pair status is mapped; the `instrument` channel is not subscribed.
- Options: (a) leave it; (b) subscribe `instrument` and map `status`
  (`online`, `cancel_only`, `post_only`, `limit_only`, `reduce_only`, `maintenance`,
  `delisted`) to a Kaspar trading-status record.
- Recommendation: (b) before any live trading.
- Depends on it: lights quoting into a halted or post-only pair.

**4. Window depth and chunk size sent to OB.**
1000 levels per side reach OB in chunks of 0.01 BTC: 25.7 million records in
32 minutes (README.md). The OB comparisons ran with these values.
- Options: a shallower window (LevelWindow) or a larger chunk.
- Recommendation: a window of 100 levels unless a strategy needs the whole book.
- Depends on it: OB memory and CPU; simulated queue granularity.

## Settled

- **Book build.** WebSocket snapshot on subscribe, then updates; no REST alignment.
  Documented and confirmed live (1 snapshot, 0 resubscribes in 32 minutes).
- **Checksum format.** Asks low to high then bids high to low, top 10, `.` and
  leading zeros removed, CRC32. Documented example passes; live: every book
  message matched (README.md).
- **Absolute sizes, zero removes, truncation without deletes.** Documented;
  checked live by the checksum, which fails if any of them is applied wrongly.
- **Trade side.** The taker's (documented); mapped as the aggressor.
- **Decimal text.** Kept from the JSON text, since the checksum depends on trailing
  zeros (documented tip; a double-parsed book fails the checksum).

## Low impact

- REST `Depth` lags the stream by up to 4.6 s (median 1.4 s) and rounds quantities up to 3
  decimals; it is used only by the verify tool.
- `trade_id` gaps are counted, not recovered (none seen live).
