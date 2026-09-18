# Arrival-Paper Tape Schema

*2026-09-18 — v@m2te.ch*

*Companion to `arrival_paper_methodology_2026-09-18.md` and `arrival_paper_workplan_2026-09-18.md`. Defines the file formats produced by the C++ tape tools and consumed by the Python analysis stack.*

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

## Tape 4 — `fill_tape` (per-maker-fill tape)

**One row per historical Trade record, joined back to the maker order's Add event via `orderID`.**

| column | type | meaning | units |
|---|---|---|---|
| `add_ts` | int64 | matching-engine time of the maker's Add | ns |
| `exec_ts` | int64 | matching-engine time of the Trade record | ns |
| `time_in_queue` | int64 | `exec_ts − last_modify_ts` — how long the maker's order sat at final price | ns |
| `lifetime` | int64 | `exec_ts − add_ts` — total maker order lifetime | ns |
| `side` | uint8 | maker's resting side (0=bid, 1=ask) | code |
| `add_price_ticks` | int64 | maker's Add price | ticks × 10⁹ |
| `exec_price_ticks` | int64 | trade price | ticks × 10⁹ |
| `exec_size` | int32 | trade size | contracts |
| `maker_order_id` | uint64 | maker order id | opaque |
| `aggressor_size` | int32 | trade's `lastQty` (executed size) | contracts |
| `size_ahead_at_submit` | int32 | total same-side same-price size ahead of maker's Add at add_ts | contracts |
| `size_remaining_at_fill` | int32 | total same-side same-price size at exec_ts+ after this fill | contracts |
| `n_orders_behind_at_fill` | int32 | count of orders behind maker in queue at exec_ts | count |
| `spread_at_submit_ticks` | int32 | `best_ask - best_bid` at add_ts | ticks |
| `spread_at_fill_ticks` | int32 | `best_ask - best_bid` at exec_ts | ticks |

**Rows per session**: ~10⁵ for NQ front (roughly equal to trade count).

**Producer**: Python — one-pass builder over `message_tape` maintaining an `order_id → add_state` hashmap. Joins on `bbbochg_tape` for spread lookups and per-price queue depths.

**Notes**:
- Attached at build time (not lookup time): `add_ts, exec_ts, spread_at_submit, spread_at_fill, size_ahead_at_submit, size_remaining_at_fill`. These are the fields the SEM and adverse-P&L attribution regression need.
- Per-fill markouts (`adv_pnl_at_100ms`, `adv_pnl_at_1s`, etc.) are computed *downstream* by merging with bbbochg_tape at query time — not stored in the fill_tape itself, to keep the file compact.

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

For per-fill markouts:

```python
for tau_ns in [int(0.1e9), int(1e9), int(10e9), int(30e9)]:
    fill[f"mid_plus_{tau_ns}"] = fill["exec_ts"].apply(lambda t: bbo["mid"].asof(t + tau_ns))
    signed_side = (fill["side"] == 0).astype(int) * 2 - 1   # bid=+1, ask=-1
    fill[f"adv_pnl_{tau_ns}"] = signed_side * (fill[f"mid_plus_{tau_ns}"] - fill["exec_price_ticks"] * 1e-9)
```

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

1. Wait for subagent to complete `bin_replay_bbo` driver + smoke test on NQ 2025-03-10
2. Add `packet_seq` + `idx_in_packet` to msgtape (30-min edit)
3. Write Python fill-tape builder (~4 hours)
4. Batch driver — run all four tapes across 731 × 3 sessions in parallel on 72 cores

## Change log

- **2026-09-18 v1** — initial schema, all four tapes defined. BTC 2025 corpus complete. Msgtape pilot day validated. BboRecorder actor+driver in flight.
