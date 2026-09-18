# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
#
# Licensed under the MIT License. See LICENSE file in the project root.
"""Build fill_tape from message_tape + bbbochg_tape.

One row per Trade record on the CME MDP3 feed, joined back to the maker order's
Add event via orderID. Each row carries both maker and aggressor perspectives —
the aggressor's markout is exactly the sign flip of the maker's markout, so
storing only the maker view and negating downstream saves half the columns.

Grain: one row per Trade record. Partial fills produce multiple rows for the
same maker_order_id; use `fill_seq_for_order` (1, 2, ...) to order them and
`remaining_size_after_fill` to detect the terminal row.

Signature:
    build_fill_tape(msg_tape_csv, bbbochg_csv, out_parquet, tick_size_ticks_1e9)

The builder is single-pass, streaming: memory footprint is O(active_orders +
total_fills), which for NQH5 on a busy day peaks around 100k live orders and
emits ~500k fills. Runs in ~30-60 seconds per session on a modern core.

Fields the tape stores as NaN in v1:
    size_ahead_at_submit, size_remaining_at_fill, n_orders_behind_at_fill

These require full L3 book reconstruction (per-price FIFO queues), which is a
separate builder that consumes the same message_tape and produces a per-Add and
per-Trade depth-of-queue tape. Wire it in in v2 by joining that tape here.
"""

from __future__ import annotations

import csv
import gzip
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterator, List, Optional, Tuple

import numpy as np
import pandas as pd


# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

# Markout horizons: fixed clock offsets (ns) plus event-count offsets.
MARKOUT_TAU_NS: Tuple[int, ...] = (
    int(0.1e9),   # 100 ms
    int(1.0e9),   # 1 s
    int(10.0e9),  # 10 s
    int(30.0e9),  # 30 s
)
MARKOUT_TAU_EVT: Tuple[int, ...] = (100, 500)

# msgtape side codes.
SIDE_BID = ord("0")  # '0' — bid
SIDE_ASK = ord("1")  # '1' — ask


# ---------------------------------------------------------------------------
# Maker state
# ---------------------------------------------------------------------------


@dataclass(slots=True)
class MakerState:
    """Live state of one resting maker order, held between its Add and its
    Delete (or its terminal fill)."""

    add_ts: int
    add_price_ticks: int
    side: int                   # +1 = bid, -1 = ask
    last_modify_ts: int
    remaining_size: int
    fill_count: int = 0


# ---------------------------------------------------------------------------
# msgtape reader
# ---------------------------------------------------------------------------


def _open_maybe_gzip(path: str):
    if path.endswith(".gz"):
        return gzip.open(path, "rt", newline="")
    return open(path, "rt", newline="")


def _iter_msgtape(path: str) -> Iterator[dict]:
    """Yield one dict per CSV row of a msgtape file.

    Coerces the numeric columns to int/float. Empty strings become 0.
    packet_seq / idx_in_packet columns are recognised but not parsed here —
    fill_tape doesn't need them, and skipping the int() coercions saves
    ~200M avoidable calls on a busy NQ session.
    """
    with _open_maybe_gzip(path) as f:
        reader = csv.reader(f)
        header = next(reader)
        col = {name: i for i, name in enumerate(header)}
        # Grab column indexes once, out of the hot loop.
        i_transact = col["transactTime"]
        i_typ = col["typ"]
        i_action = col["action"]
        i_side = col["side"]
        i_pxd = col["pxd"]
        i_sz = col["sz"]
        i_oid = col["orderID"]
        for row in reader:
            yield {
                "transactTime": int(row[i_transact]),
                "typ": row[i_typ],
                "action": row[i_action],
                "side": row[i_side],
                "pxd": float(row[i_pxd]) if row[i_pxd] else 0.0,
                "sz": int(row[i_sz]) if row[i_sz] else 0,
                "orderID": int(row[i_oid]),
            }


# ---------------------------------------------------------------------------
# BBO reader
# ---------------------------------------------------------------------------


def load_bbbochg(bbbochg_csv: str, sym: Optional[int] = None) -> pd.DataFrame:
    """Load a bbbochg tape into a DataFrame indexed by tx_time.

    If `sym` is given, filters to that sym only. Adds a `mid` column (in
    ticks — the same integer scale as best_bid/best_ask on the tape).
    """
    df = pd.read_csv(
        bbbochg_csv,
        dtype={
            "tx_time": "int64",
            "venue": "int32",
            "sym": "int32",
            "best_bid": "int32",
            "best_ask": "int32",
        },
    )
    if sym is not None:
        df = df[df["sym"] == sym].copy()
    df["mid"] = 0.5 * (df["best_bid"] + df["best_ask"])
    df = df.sort_values("tx_time").reset_index(drop=True)
    return df


def _bbo_mid_asof(bbo_tx: np.ndarray, bbo_mid: np.ndarray, target_ts: np.ndarray) -> np.ndarray:
    """Vectorised as-of lookup: for each `target_ts`, return the last `bbo_mid`
    at or before it (NaN if the target precedes the first BBO update).

    bbo_tx must be sorted ascending.
    """
    idx = np.searchsorted(bbo_tx, target_ts, side="right") - 1
    out = np.full(target_ts.shape, np.nan, dtype=np.float64)
    ok = idx >= 0
    out[ok] = bbo_mid[idx[ok]]
    return out


# ---------------------------------------------------------------------------
# Builder
# ---------------------------------------------------------------------------


PXD_STORAGE_SCALE = 1e9   # msgtape stores pxd × 1e9 as an integer.


def build_fill_tape(
    msg_tape_csv: str,
    bbbochg_csv: str,
    out_parquet: str,
    disp_factor: float,
    tick_size: float,
    bbo_sym: Optional[int] = None,
    log_every: int = 1_000_000,
) -> pd.DataFrame:
    """Build a fill_tape from a per-securityID message_tape and a channel-level
    bbbochg tape.

    Parameters
    ----------
    msg_tape_csv : path to CSV emitted by msgtape (this securityID only)
    bbbochg_csv  : path to gzipped CSV emitted by bin_replay_bbo (channel-wide)
    out_parquet  : where to write the fill_tape (parquet, snappy)
    disp_factor  : master_universe row's `dispFactor` for this instrument
                   (e.g. 0.01 for CME NQ/ES). Converts the raw pxd value from
                   msgtape into a native decimal price:
                       native_price = (pxd_stored / PXD_STORAGE_SCALE) * disp_factor
    tick_size    : master_universe row's `tickSize` for this instrument
                   (e.g. 0.25 for CME NQ/ES). Converts the OB tick-index in
                   bbbochg (best_bid, best_ask) into a native decimal price:
                       fwd_mid_native = fwd_mid_tick_index * tick_size
    bbo_sym      : if given, filter bbbochg to this sym (the venue's internal
                   sym integer). Set None to use the whole channel.
    log_every    : progress print interval (rows read from msg_tape).

    Notes on units — critical, do not remove
    ---------------------------------------
    msgtape's `pxd` field is what CME calls the "display price" divided by
    dispFactor, stored by msgtape as `int(round(pxd * 1e9))` to keep integer
    precision. Native price recovers as (pxd / 1e9) * disp_factor.

    bbbochg's `best_bid` / `best_ask` are the OB's internal tick-index integers
    (best_bid = 80123 for NQH5 near 20030.75); native price = tick_index *
    tick_size. tick_size = minPriceIncrement * dispFactor.

    Markouts are computed in NATIVE PRICE units so that the number a paper
    reader sees is directly interpretable (e.g., "0.75 points on NQ" =
    3 ticks) and so that identities across assets remain in comparable units.

    Returns
    -------
    DataFrame written to out_parquet.
    """
    if disp_factor <= 0 or tick_size <= 0:
        raise ValueError("disp_factor and tick_size must be positive; "
                         f"got disp_factor={disp_factor}, tick_size={tick_size}")
    # ------------------------------------------------------------------
    # Pass 1: stream the message tape, build fill rows.
    # ------------------------------------------------------------------
    maker_state: Dict[int, MakerState] = {}
    fills: List[Tuple] = []

    # For each Trade we also want to derive the aggressor_order_id: it's the
    # opposite side of the trade, and we resolve it by taking the orderID of
    # the next Trade in a batch that carries endOfEvent, OR — simpler — by
    # observing that the aggressor is the taker on the *other* side of the
    # book. The message_tape does not carry that field, so v1 sets it to 0
    # and we plan to derive it in v2 by co-reading the raw .bin.

    n = 0
    for r in _iter_msgtape(msg_tape_csv):
        n += 1
        if log_every and (n % log_every == 0):
            print(f"[fill_tape] {n:>12,} msg-tape rows read, "
                  f"{len(maker_state):>8,} orders live, "
                  f"{len(fills):>8,} fills emitted",
                  file=sys.stderr)

        typ = r["typ"]
        if typ == "M":
            oid = r["orderID"]
            action = r["action"]
            price = int(round(r["pxd"] * PXD_STORAGE_SCALE))
            side_str = r["side"]
            side_code = ord(side_str[0]) if side_str else 0
            side = +1 if side_code == SIDE_BID else (-1 if side_code == SIDE_ASK else 0)

            if action == "0":     # Add
                maker_state[oid] = MakerState(
                    add_ts=r["transactTime"],
                    add_price_ticks=price,
                    side=side,
                    last_modify_ts=r["transactTime"],
                    remaining_size=r["sz"],
                )
            elif action == "1":   # Modify / Change
                st = maker_state.get(oid)
                if st is not None:
                    # A price change resets queue priority; a pure size change
                    # does not.
                    if price != st.add_price_ticks:
                        st.last_modify_ts = r["transactTime"]
                        st.add_price_ticks = price
                    st.remaining_size = r["sz"]
                # if we've never seen this order (recovery snapshot etc.) —
                # skip; downstream trades won't attach.
            elif action == "2":   # Delete
                maker_state.pop(oid, None)
            # Other actions (Trade Summary, etc.) are ignored on the maker path.

        elif typ == "T":
            oid = r["orderID"]
            st = maker_state.get(oid)
            if st is None:
                # Maker not seen. Emit the row anyway with NaN maker fields —
                # so that trade-count reconciliation still holds.
                fills.append((
                    r["transactTime"],           # exec_ts
                    oid,                          # maker_order_id
                    0,                            # aggressor_order_id (v1: unknown)
                    np.nan,                       # maker_side
                    np.nan,                       # aggressor_side
                    np.nan,                       # exec_price_ticks
                    r["sz"],                      # exec_size
                    np.nan,                       # maker_add_ts
                    np.nan,                       # maker_last_modify_ts
                    np.nan,                       # time_in_queue
                    np.nan,                       # lifetime
                    np.nan,                       # maker_add_price_ticks
                    np.nan,                       # remaining_size_after_fill
                    np.nan,                       # fill_seq_for_order
                ))
                continue

            # Consume `sz` from the maker's remaining.
            filled = r["sz"]
            new_remaining = st.remaining_size - filled
            st.fill_count += 1
            st.remaining_size = max(new_remaining, 0)

            # Exec price = maker's resting price at time of fill. The trade
            # record doesn't carry a price on our msgtape (the T typ writes
            # blank pxd), so we take it from maker state — always the price
            # at which the order was resting when it got hit.
            exec_price_ticks = st.add_price_ticks

            fills.append((
                r["transactTime"],               # exec_ts
                oid,                              # maker_order_id
                0,                                # aggressor_order_id (v1: 0)
                st.side,                          # maker_side
                -st.side,                         # aggressor_side (opposite)
                exec_price_ticks,                 # exec_price_ticks
                filled,                           # exec_size
                st.add_ts,                        # maker_add_ts
                st.last_modify_ts,                # maker_last_modify_ts
                r["transactTime"] - st.last_modify_ts,   # time_in_queue
                r["transactTime"] - st.add_ts,    # lifetime
                st.add_price_ticks,               # maker_add_price_ticks
                st.remaining_size,                # remaining_size_after_fill
                st.fill_count,                    # fill_seq_for_order
            ))

            if st.remaining_size == 0:
                maker_state.pop(oid, None)

    print(f"[fill_tape] pass 1 done: {n:,} msg-tape rows, "
          f"{len(fills):,} fills emitted, "
          f"{len(maker_state):,} orders still live at EOF",
          file=sys.stderr)

    # ------------------------------------------------------------------
    # Pass 2: build DataFrame + attach markouts.
    # ------------------------------------------------------------------
    cols = [
        "exec_ts", "maker_order_id", "aggressor_order_id",
        "maker_side", "aggressor_side",
        "exec_price_ticks", "exec_size",
        "maker_add_ts", "maker_last_modify_ts",
        "time_in_queue", "lifetime",
        "maker_add_price_ticks",
        "remaining_size_after_fill", "fill_seq_for_order",
    ]
    df = pd.DataFrame(fills, columns=cols)
    df["is_partial"] = df["remaining_size_after_fill"] > 0

    # exec_price_native carries the native decimal price so downstream code
    # doesn't need to remember the pxd storage convention.
    df["exec_price_native"] = (df["exec_price_ticks"].astype("float64")
                               / PXD_STORAGE_SCALE) * disp_factor

    # Queue-depth placeholders (v2).
    df["size_ahead_at_submit"] = np.nan
    df["size_remaining_at_fill"] = np.nan
    df["n_orders_behind_at_fill"] = np.nan
    df["spread_at_submit_ticks"] = np.nan
    df["spread_at_fill_ticks"] = np.nan

    # ------------------------------------------------------------------
    # Markouts
    # ------------------------------------------------------------------
    print("[fill_tape] loading bbbochg tape...", file=sys.stderr)
    bbo = load_bbbochg(bbbochg_csv, sym=bbo_sym)

    if len(bbo) == 0:
        print("[fill_tape] WARNING: no BBO rows after sym filter; "
              "markouts will be NaN", file=sys.stderr)
        for tau in MARKOUT_TAU_NS:
            df[f"markout_{_tau_label(tau)}"] = np.nan
        for k in MARKOUT_TAU_EVT:
            df[f"markout_{k}evt"] = np.nan
    else:
        bbo_tx = bbo["tx_time"].to_numpy()
        bbo_mid_ticks = bbo["mid"].to_numpy()   # OB tick indices, not native.

        # Fixed-time markouts, computed in NATIVE PRICE.
        exec_ts = df["exec_ts"].to_numpy()
        maker_side = df["maker_side"].to_numpy()
        exec_price_native = df["exec_price_native"].to_numpy()

        for tau in MARKOUT_TAU_NS:
            fwd_mid_ticks = _bbo_mid_asof(bbo_tx, bbo_mid_ticks, exec_ts + tau)
            fwd_mid_native = fwd_mid_ticks * tick_size
            markout = maker_side * (fwd_mid_native - exec_price_native)
            df[f"markout_{_tau_label(tau)}"] = markout

        # Event-count markouts: for each fill, look up the mid at the k-th
        # bbbochg event strictly after the fill's exec_ts.
        for k in MARKOUT_TAU_EVT:
            idx = np.searchsorted(bbo_tx, exec_ts, side="right") + (k - 1)
            fwd_mid_ticks = np.where(
                idx < len(bbo_mid_ticks),
                bbo_mid_ticks[np.clip(idx, 0, len(bbo_mid_ticks) - 1)],
                np.nan,
            )
            fwd_mid_native = fwd_mid_ticks * tick_size
            markout = maker_side * (fwd_mid_native - exec_price_native)
            df[f"markout_{k}evt"] = markout

    # ------------------------------------------------------------------
    # Write
    # ------------------------------------------------------------------
    Path(out_parquet).parent.mkdir(parents=True, exist_ok=True)
    df.to_parquet(out_parquet, compression="snappy", index=False)
    print(f"[fill_tape] wrote {len(df):,} rows to {out_parquet}",
          file=sys.stderr)
    return df


def _tau_label(tau_ns: int) -> str:
    """Human-readable tau label used for markout column names."""
    if tau_ns >= int(1e9):
        s = tau_ns / 1e9
        if s == int(s):
            return f"{int(s)}s"
        return f"{s:g}s"
    ms = tau_ns / 1e6
    if ms == int(ms):
        return f"{int(ms)}ms"
    return f"{ms:g}ms"


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def main() -> int:
    import argparse
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--msg-tape", required=True, help="msgtape CSV (per securityID)")
    ap.add_argument("--bbbochg", required=True, help="bbbochg CSV (channel-wide, gzip ok)")
    ap.add_argument("--out", required=True, help="output parquet path")
    ap.add_argument("--bbo-sym", type=int, default=None,
                    help="BBO sym to filter on (default: no filter)")
    ap.add_argument("--disp-factor", type=float, required=True,
                    help="master_universe dispFactor for this instrument "
                         "(e.g. 0.01 for CME NQ/ES)")
    ap.add_argument("--tick-size", type=float, required=True,
                    help="master_universe tickSize for this instrument "
                         "(e.g. 0.25 for CME NQ/ES)")
    args = ap.parse_args()
    build_fill_tape(
        msg_tape_csv=args.msg_tape,
        bbbochg_csv=args.bbbochg,
        out_parquet=args.out,
        disp_factor=args.disp_factor,
        tick_size=args.tick_size,
        bbo_sym=args.bbo_sym,
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
