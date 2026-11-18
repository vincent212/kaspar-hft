# Review: Currenex US Treasury OUCH order manager

What was checked, how, and what was not. The handler is not certified and
has never connected to Currenex.

## How it was made

From the public Currenex documents only (`feed.md` section 1), following
the build-feed-handler skill and the Nasdaq OUCH handler in
`global_connectivity/non_certified/order_entry/nasdaq_ouch/` as the model.
The skill's step 4 (stop and have the user approve the feed description
before code) was skipped: the task asked for the whole handler in one pass.

## Field table

- Every message table of UST OUCH sections 9 and 10 is in
  `currenex_ust_ouch.tbl`: the header, 10 messages to Currenex and 17 from
  Currenex: 239 fields besides the 27 type codes (the header's two
  included). Logon, Logout and Heartbeat are
  declared in both directions.
- `fieldgen.py --check` passes: offsets contiguous, no repeated codes in a
  direction, all types declared. The specification states no message
  totals, so there are no `@size` lines; the totals in the codec tests are
  each table's last offset plus length.
- Tables were read from `pdftotext -layout` output of the PDF; tables that
  continue across a page (Spread InstrumentInfo, PendingFill,
  PendingFillCancel, Outright PendingFillTrade, Spread PendingFillTrade,
  OutrightTrade, SpreadTrade) were followed across the break.
- Not done: a second, independent extraction (one subagent per group) to
  compare against. Each table was extracted once.

## Tests (96, clang on macOS, three consecutive clean runs)

- Codec (31): every request encoded and compared byte for byte with a frame
  written out by hand from the tables (header, SOH, ETX); every report
  written out by hand and decoded, every field compared; framing (several
  frames per block, a frame split at every byte, 0x01 and 0x03 inside
  binary fields, garbage, unknown type, missing ETX); rates, amounts, the
  weekly ClOrderID clock.
- Actor against the fake (48): the order_entry.md list, where the protocol
  has the feature. Not applicable: short-sale marking, broker algorithms,
  busts (OUCH has none), account queries.
- Actor against the simulator (6).
- TCP session against a loopback server (11), whose side is written from
  the tables by hand.
- `make syntax` with g++-15 passes.

## What was not checked

- Any behaviour of the real Currenex service. In particular the readings in
  `feed.md` section 10: the Logon type, framing of client messages, the
  Logon response's sequence number, the replay end, which id a cancel
  acknowledgement carries, ClOrderID uniqueness, weekly reset, what a
  refused logon looks like.
- The FX conformance test plan (`.xls`) was read only as extracted strings,
  for one question (A9).
- Spread trading: decoded and tested, not traded.
- Iceberg orders and replaces: encoded and tested, never sent by the OM.
- Replay of very long gaps, and throughput.
- Libraries: the test binary links the kaspar libraries built from
  `~/kaspar-hft` (main), with headers from this worktree.

## Known limitations

- The SOM size unit is a config value (`face_per_unit`); trades below a
  unit reach the SOM as fractional `Fill` sizes while the book counts whole
  units.
- No `en::x` code for Currenex exists; the venue is named in the config.
- After a numbering restart (no persistence, or the weekly reset), orders
  live before it are reported cancelled although a late match may have
  filled them; the FIX STP drop copy (8.7) is the reconciliation, and the
  OM does not read it.
