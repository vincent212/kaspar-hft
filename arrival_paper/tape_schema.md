# Arrival-Paper Tape Schema

*2026-09-18 — v@m2te.ch*

*Companion to `methodology.md` and `workplan.md`. Defines the file formats produced by the C++ tape tools and consumed by the Python analysis stack.*

## Storage layout

All tapes live outside the git repo, under a shared vast directory:

```
/vast/home/vmayeski/out/arrival_paper/
├── tapes/
│   ├── 310/                             # ES (chan 310)
│   │   ├── message/<yyyymmdd>.parquet   # per-message event tape
│   │   ├── bbbochg/<yyyymmdd>.csv.gz    # top-of-book change tape
│   │   ├── packet/<yyyymmdd>.parquet    # per-packet arrival tape
│   │   └── fill/<yyyymmdd>.parquet      # per-maker-fill tape
│   ├── 318/                             # NQ (chan 318) — same shape
│   └── 326/                             # BTC (chan 326) — same shape
├── fits/{310,318,326}/<yyyymmdd>.hawkes.json   # per-session Hawkes fits
├── panels/                              # aggregate multi-day panels
│   ├── daily_panel.parquet
│   └── hourly_panel.parquet
├── figs/                                # figures for the paper
└── logs/                                # tape-build logs
```

## Naming convention

- **Chan-numeric top-level** — matches the CME MDP3 channel ID used throughout the pipeline: `310` = ES, `318` = NQ, `326` = BTC crypto futures.
- **`<yyyymmdd>`** — session date in ET (`20250310` = 2025-03-10). Not calendar date UTC.
- **RTH only for now** — 09:30-16:00 ET. Overnight session is a separate scope.
- **Per-stream, per-day, per-tape-kind** — one file per intersection. No cross-stream concatenation. Downstream Python reads the specific file needed.

## Tape 1 — `message_tape` (event-level arrival tape)

**One row per MBO / trade record for the front-month securityID during RTH.**

| column | type | meaning | units |
|---|---|---|---|
| `t_match` | int64 | matching-engine `transactTime` from MDP3 | ns since epoch |
| `t_send` | int64 | CME gateway `sendingTime` from MDP3 packet header | ns since epoch |
| `t_recv` | int64 | pcap wire timestamp at Databento's colo tap (`handlerendtim` in .bin) | ns since epoch |
| `msg_type` | uint8 | 0=MBO Add, 1=MBO Modify, 2=MBO Delete, 3=Delete-Thru, 4=Delete-From, 5=Overlay, 100=Trade | code |
| `side` | uint8 | 0=bid, 1=ask, 255=n/a (trades) | code |
| `price_ticks` | int64 | native price × 10^9 (raw `pxd` scaled) | ticks × 10⁹ (fixed-point) |
| `size` | int32 | display qty for MBO, `lastQty` for trades | contracts |
| `order_id` | uint64 | maker order ID (0 for records without one) | opaque |
| `packet_seq` | int64 | running packet counter — increments each new `sendingTime` | 0-based per session |
| `idx_in_packet` | int32 | 0-based position within containing UDP packet | position |

**Rows per session**: ~10⁷ for NQ front, ~10⁵-10⁶ for BTC front.

**Producer**: `dbento_pcap_parse/msgtape/src/msgtape` (extends the pilot v1 which already emits most of this; needs `packet_seq` and `idx_in_packet` added).

**Notes**:
- `price_ticks` fixed-point avoids floating-point mixing between price streams that use different tick sizes; native `pxd × 10⁹` is a lossless integer for all CME-listed instruments' tick grids.
- Packet boundaries are detected by consecutive rows sharing the same `t_send`; `packet_seq` increments when `t_send` changes.

## Tape 2 — `bbbochg_tape` (top-of-book change tape)

**One row per event that changes the top of book for the front-month securityID during RTH.**

CSV, gzipped, header:

```
tx_time,venue,sym,best_bid,best_ask
```

| column | type | meaning | units |
|---|---|---|---|
| `tx_time` | int64 | matching-engine time at which top-of-book changed | ns since epoch |
| `venue` | string | CME code (e.g., `XCME`) | 4-char |
| `sym` | int32 | securityID (front-month) | — |
| `best_bid` | int32 | best bid in native ticks | ticks |
| `best_ask` | int32 | best ask in native ticks | ticks |

**Rows per session**: ~10⁶ for NQ front.

**Producer**: `dbento_pcap_parse/bin_replay_bbo/src/bin_replay_bbo` — replays a `.bin` through the actor graph and records `BBBOChg` messages published by `frame_kaspr::ob::OB` (or `TachBook`).

**Notes**:
- Emitted from CSV because that's what the recorder actor produces natively; downstream Python converts to parquet on first read.
- `best_bid` and `best_ask` are in ticks (integer). `mid_ticks = (best_bid + best_ask) / 2` computed in Python.
- Locked / crossed markets are filtered upstream by OB (see `OB.cpp` line 1079 comment).

## Tape 3 — `packet_tape` (per-UDP-packet arrival tape)

**One row per UDP packet in the message_tape. Derived from message_tape by grouping on `t_send`.**

| column | type | meaning | units |
|---|---|---|---|
| `packet_seq` | int64 | packet index (0-based per session) | — |
| `t_send` | int64 | CME gateway send timestamp for this packet | ns since epoch |
| `t_recv` | int64 | pcap wire arrival timestamp (from first message in packet) | ns since epoch |
| `span` | int32 | number of SBE messages inside this packet | count |
| `first_msg_type` | uint8 | msg_type of the first message in the packet | code |

**Rows per session**: ~10⁵-10⁶ (each packet aggregates 1-50+ messages).

**Producer**: Python — one-pass groupby on message_tape's `packet_seq`. Not a C++ tool.

**Notes**:
- Used by the Lindley latency simulation — `span` becomes `idx_in_packet` range, `S_i = floor + slope · idx_i`.
- Used by fast_send-style packet-level statistics as a cross-reference vs message-level.

## Tape 4 — `fill_tape` (per-maker-fill tape, both perspectives on one row)

**One row per historical Trade record**, joined back to the maker order's Add event via `maker_order_id`. Row carries **both maker- and aggressor-perspective fields**, so downstream analyses can pull either view without rebuilding the tape.

### Row schema

**Identity + timing**:

| column | type | meaning | units |
|---|---|---|---|
| `exec_ts` | int64 | matching-engine time of the Trade record | ns |
| `maker_order_id` | uint64 | resting order that got hit | opaque |
| `aggressor_order_id` | uint64 | the order that crossed the spread | opaque |
| `maker_add_ts` | int64 | matching-engine time of the maker's Add | ns |
| `maker_last_modify_ts` | int64 | last time the maker's order had priority reset (price change) | ns |
| `time_in_queue` | int64 | `exec_ts − maker_last_modify_ts` | ns |
| `lifetime` | int64 | `exec_ts − maker_add_ts` | ns |

**Trade parameters** (same for both perspectives):

| column | type | meaning | units |
|---|---|---|---|
| `maker_side` | int8 | maker's resting side: +1 = bid, −1 = ask | signed |
| `aggressor_side` | int8 | aggressor's cross direction: +1 = buy-aggressor, −1 = sell-aggressor | signed |
| `exec_price_ticks` | int64 | trade price | ticks × 10⁹ |
| `exec_size` | int32 | `lastQty` on the Trade record | contracts |
| `maker_add_price_ticks` | int64 | maker's original Add price (may differ from exec_price if maker Modified up/down before fill) | ticks × 10⁹ |

**Fill-sequence flags** (partial vs full):

| column | type | meaning | units |
|---|---|---|---|
| `remaining_size_after_fill` | int32 | maker's residual size after this Trade. 0 = fully consumed; >0 = partial fill; more Trades may follow on same `maker_order_id` | contracts |
| `fill_seq_for_order` | int32 | 1 for first fill of this maker_order_id, 2 for second, etc. | int |
| `is_partial` | bool | derived: `remaining_size_after_fill > 0` | flag |

**Queue + book state** (from bbbochg_tape lookup):

| column | type | meaning | units |
|---|---|---|---|
| `size_ahead_at_submit` | int32 | same-side same-price size ahead of maker at maker_add_ts | contracts |
| `size_remaining_at_fill` | int32 | same-side same-price total size at exec_ts+ after this fill | contracts |
| `n_orders_behind_at_fill` | int32 | orders still queued behind this maker at exec_ts | count |
| `spread_at_submit_ticks` | int32 | `best_ask − best_bid` at maker_add_ts | ticks |
| `spread_at_fill_ticks` | int32 | `best_ask − best_bid` at exec_ts | ticks |

**Markouts — stored maker-perspective, sign convention baked in**:

| column | type | meaning |
|---|---|---|
| `markout_100ms` | int64 | `maker_side × (mid(exec_ts + 100ms) − exec_price_ticks)`, ticks × 10⁹ |
| `markout_1s` | int64 | same at τ = 1 s |
| `markout_10s` | int64 | same at τ = 10 s |
| `markout_30s` | int64 | same at τ = 30 s |
| `markout_100evt` | int64 | same at τ = 100 book events forward |
| `markout_500evt` | int64 | same at τ = 500 book events forward |

**Sign convention**: `markout` is stored **from maker's perspective**. Negative = adverse selection (maker loses). Aggressor markout is just `-markout` (trivial Python view — no separate tape).

### Deriving the aggressor perspective

```python
import pandas as pd
fill = pd.read_parquet("fill/20250310.parquet")
aggr = fill.copy()
for c in fill.filter(like="markout_").columns:
    aggr[c] = -fill[c]
# Now group by aggressor_order_id for taker-side analyses.
```

**Why one tape instead of two**: aggressor markouts are exact negatives of maker markouts by construction (same exec_price, same exec_ts anchor, same future mid). Storing both explicitly would duplicate data. Two Python lines derive the aggressor view.

**Where the two perspectives differ empirically**: aggregated statistics (per-order clustering, session-level summaries) look different when grouped by `aggressor_order_id` vs `maker_order_id`, because the populations of resting makers and aggressive takers have different clustering properties even though every individual Trade has exact opposite markouts on the two sides. This is a *finding to report*, not a data quirk.

**Rows per session**: ~10⁵ for NQ front.

**Producer**: Python — one-pass builder over `message_tape` maintaining `order_id → maker_state` hashmap. Joins on `bbbochg_tape` for spread and per-price queue depths; joins on itself later for markout lookups.

**Handling of edge cases**:

- **Partial fills**: multiple rows per `maker_order_id`, distinguished by `fill_seq_for_order` and `is_partial`. Standard-error clustering on `maker_order_id` is required in regressions to handle correlation across the same order's multiple fills.
- **Multi-fill sequences from one aggressor**: implicit. Multiple rows at the same (or nearly same) `exec_ts` share an `aggressor_order_id`. For taker-side analyses, cluster on `aggressor_order_id`.
- **Iceberg orders**: no special handling. Each visible slice is either a distinct `maker_order_id` (CME's usual convention on refill) or the same order with size Modifies — either way, per-Trade rows land in fill_tape correctly. Iceberg complications only matter for BBO / depth reconstruction, not for markouts.
- **Missing maker Add** (order added before RTH open or during recovery snapshot): emit row with NaN for `maker_add_ts`, `maker_add_price_ticks`, `time_in_queue`, `lifetime`; log the fraction affected. Non-zero rate is a data-quality warning but doesn't block analysis.

## Reading tapes from Python

Canonical read pattern:

```python
import pandas as pd
BASE = "/vast/home/vmayeski/out/arrival_paper/tapes"

msg = pd.read_parquet(f"{BASE}/318/message/20250310.parquet")
bbo = pd.read_csv(f"{BASE}/318/bbbochg/20250310.csv.gz", dtype={
    "tx_time": "int64", "sym": "int32",
    "best_bid": "int32", "best_ask": "int32",
})
fill = pd.read_parquet(f"{BASE}/318/fill/20250310.parquet")
```

For time-based lookups on the BBO tape:

```python
bbo["mid"] = 0.5 * (bbo["best_bid"] + bbo["best_ask"])
bbo = bbo.sort_values("tx_time").set_index("tx_time")
# midprice at any arbitrary time via forward-fill
def mid_at(ts_ns): return bbo["mid"].asof(ts_ns)
```

Per-fill markouts are **already stored** in fill_tape (`markout_100ms`, `markout_1s`, `markout_10s`, `markout_30s`, `markout_100evt`, `markout_500evt`), signed from the maker's perspective. To recompute or add a new τ:

```python
# fill_tape's maker_side is already ±1 (bid=+1, ask=-1)
for tau_ns in [int(0.1e9), int(1e9), int(10e9), int(30e9)]:
    mid_forward = fill["exec_ts"].apply(lambda t: bbo["mid"].asof(t + tau_ns))
    exec_px = fill["exec_price_ticks"] * 1e-9
    fill[f"markout_{tau_ns}"] = fill["maker_side"] * (mid_forward - exec_px)
```

To flip to aggressor perspective, negate any `markout_*` column (see §Deriving the aggressor perspective).

## Data-integrity checks (Day-1 gate before batch production)

Before running the fill_tape builder across the full 731-day × 3-stream corpus, every candidate day must pass these row-level identities. If **any** row fails on **any** day, halt production and investigate — a failing identity indicates a bug in the builder (order-lifecycle hashmap, side accounting, or markout join) that will silently corrupt every downstream regression.

### Row-level identities (must hold for every row in fill_tape)

- **Side identity**: `maker_side + aggressor_side == 0` for every row. (Maker and aggressor are on opposite sides of every Trade — a Trade with `maker_side = +1 (bid)` requires `aggressor_side = −1 (sell aggressor)`.) A violation means side attribution is wrong somewhere in the message-tape parse.

- **Sign-flip identity on markouts**: for every row × every τ ∈ {100 ms, 1 s, 10 s, 30 s, 100 evt, 500 evt}, the aggressor markout — derived as `−markout_τ` — must satisfy the natural interpretation: if the maker's fill is adversely selected (`markout < 0`, mid moves against the maker after fill), then the aggressor's markout is favorable by exactly the same amount. This is a definitional identity, not an empirical claim; if it fails, the markout join is broken (wrong mid alignment, wrong τ offset, wrong sign convention).

- **Fill-sequence consistency**: for every `maker_order_id`, the sum of `exec_size` across all rows with that id equals the maker's original order size minus `remaining_size_after_fill` on the terminal row. `fill_seq_for_order` is 1, 2, 3, … with no gaps.

### Population-level checks (must hold in aggregate on each day)

- **Trade count reconciliation**: `len(fill_tape) == count of Trade records in message_tape`. Every Trade record on the raw stream produces exactly one fill_tape row.

- **Unmatched-maker rate**: fraction of rows with `maker_add_ts == NaN` (order was already resting before the message tape's start, or added during a snapshot recovery). Should be small (~1-2% typically); a day with >10% unmatched-maker rate is a data-quality warning to flag but not fail on.

- **Markout coverage**: for τ = 30 s and τ = 500 evt, some rows near end-of-session will not have enough forward data. `markout_τ = NaN` on those rows is expected; measure the coverage rate and confirm it matches expectation (roughly `1 − τ/session_length`).

### How the checks run in the pipeline

The `kaspar_arrival` Python module ships a `validate_fill_tape(day_parquet)` function that runs all three row-level identities and all three population checks, printing a pass/fail line per identity and returning a summary dict. Batch production calls this on the first 5 days of each stream (ES, NQ, BTC) before enqueuing the remaining 726. Failure aborts the batch.

## File sizes (rough)

| tape | rows | file size (parquet, gzipped where noted) |
|---|---|---|
| message | 10-20M / day | ~500-800 MB per stream per day |
| bbbochg | 0.5-2M / day | ~30-100 MB gzipped-CSV per stream per day |
| packet | 0.5-2M / day | ~30 MB per stream per day |
| fill | 0.05-0.2M / day | ~5-15 MB per stream per day |

Corpus total for 731 days × 3 streams: **~2-3 TB** of tapes. Fits comfortably on the vast partition (73 TB free).

## Producer status (as of 2026-09-18)

- **BTC .bin** — 2025 done (313 sessions ✓), 2024 running in background (consolidated pcaps)
- **ES + NQ .bin** — already complete (731 sessions each ✓)
- **msgtape v1** — one-day proof-of-concept for NQ 2025-03-10 ✓; needs `packet_seq` + `idx_in_packet` fields added, then full-corpus batch
- **BboRecorder actor** — subagent drafting ✓; driver binary `bin_replay_bbo` in progress
- **fill_tape builder** — Python, not yet written
- **packet_tape builder** — Python (trivial groupby), not yet written

## Immediate next actions

1. Confirm `bin_replay_bbo` smoke test on NQ 2025-03-10 emits a well-formed bbbochg CSV with row count roughly proportional to Add/Modify/Delete event count (unthrottled).
2. Add `packet_seq` + `idx_in_packet` to msgtape.cpp (30-min edit) so packet_tape can be built as a downstream groupby.
3. Write Python fill_tape builder (~4 hours) — order-lifecycle hashmap, both perspectives on one row, `remaining_size_after_fill` and `fill_seq_for_order` flags. **First step in the builder is a call to `validate_fill_tape()` on the first 5 days per stream** (see Data-integrity checks); do not enqueue the remaining 726 days per stream until the identities pass.
4. Batch driver — run all four tape builders across 731 × 3 sessions in parallel on 72 cores.
5. Ship the paper's companion `kaspar_arrival` Python module (MIT licensed) so all code that produced the paper's tables and charts is open-sourced with the paper.

## Change log

- **2026-09-18 v1** — initial schema, all four tapes defined. BTC 2025 corpus complete. Msgtape pilot day validated. BboRecorder actor+driver in flight.
- **2026-09-18 v2** — Tape 4 (`fill_tape`) rewritten: one row per Trade now carries **both maker and aggressor perspectives** (`maker_side`, `aggressor_side`, `maker_order_id`, `aggressor_order_id`). Markouts still stored maker-perspective; aggressor view is `-markout` (two-line Python transform). Added `remaining_size_after_fill`, `fill_seq_for_order`, `is_partial` flags to handle partial fills without altering the one-row-per-Trade grain. New **Data-integrity checks** section codifies the sign-flip identity and side identity as Day-1 gates before full-corpus production.
