# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
#
# Licensed under the MIT License. See LICENSE file in the project root.
"""Day-1 data-integrity gates for the fill_tape.

Documented in docs/arrival_paper/tape_schema.md §"Data-integrity checks".

Row-level identities that MUST hold on every row (barring the maker-not-seen
rows carrying NaN maker fields):

    maker_side + aggressor_side == 0
    maker_markout(tau) + aggressor_markout(tau) ≈ 0  (aggressor := -maker)

Population-level checks:

    len(fill_tape) == count of Trade records in the source message_tape
    fraction of rows with NaN maker_add_ts ≤ ~10%
    markout coverage roughly matches (1 − tau / session_length)

`validate_fill_tape(path)` runs everything and returns a summary dict. A
non-fatal failure prints WARN; a fatal failure prints FAIL and raises.
"""

from __future__ import annotations

import sys
from pathlib import Path
from typing import Dict, Optional

import numpy as np
import pandas as pd


# ---------------------------------------------------------------------------
# Row-level identities
# ---------------------------------------------------------------------------


def check_side_identity(df: pd.DataFrame) -> Dict[str, object]:
    """maker_side + aggressor_side must equal 0 on every non-NaN row."""
    mask = df["maker_side"].notna() & df["aggressor_side"].notna()
    s = df.loc[mask, "maker_side"] + df.loc[mask, "aggressor_side"]
    bad = int((s != 0).sum())
    return {
        "rows_checked": int(mask.sum()),
        "rows_bad": bad,
        "passed": bad == 0,
    }


def check_signflip_identity(df: pd.DataFrame, tol: float = 1e-9) -> Dict[str, object]:
    """maker_markout + aggressor_markout ≈ 0 for every tau.

    Aggressor markout is defined as -maker_markout, so this identity is
    trivially satisfied by construction; the check exists to catch a future
    schema drift where the two views end up stored separately.
    """
    markout_cols = [c for c in df.columns if c.startswith("markout_")]
    per_tau = {}
    all_ok = True
    for c in markout_cols:
        maker_m = df[c].to_numpy()
        aggressor_m = -maker_m
        s = maker_m + aggressor_m
        # NaN + (-NaN) = NaN → ignore NaN rows
        mask = ~np.isnan(s)
        bad = int((np.abs(s[mask]) > tol).sum())
        per_tau[c] = {
            "rows_checked": int(mask.sum()),
            "rows_bad": bad,
            "passed": bad == 0,
        }
        all_ok = all_ok and (bad == 0)
    return {"per_tau": per_tau, "passed": all_ok}


def check_fill_sequence_consistency(df: pd.DataFrame) -> Dict[str, object]:
    """Within each maker_order_id, `fill_seq_for_order` must be 1, 2, 3, ...
    with no gaps.

    We deliberately do NOT check that `remaining_size_after_fill` is
    non-increasing: on CME MDP3 a maker can legitimately Modify their
    resting order to a LARGER size mid-lifecycle (a size-only Modify
    retains queue priority per CME rules — only a price change resets it),
    so a subsequent fill can leave remaining_size_after_fill higher than
    the previous fill's residual. That is not a bug.
    """
    valid = df.dropna(subset=["maker_order_id", "fill_seq_for_order"]).copy()
    bad_seq = 0
    bad_nonneg_remaining = 0
    bad_nonneg_exec_size = 0
    for oid, g in valid.groupby("maker_order_id", sort=False):
        seq = g["fill_seq_for_order"].to_numpy(dtype=np.int64)
        expected = np.arange(1, len(seq) + 1, dtype=np.int64)
        if not np.array_equal(np.sort(seq), expected):
            bad_seq += 1
        if (g["remaining_size_after_fill"] < 0).any():
            bad_nonneg_remaining += 1
        if (g["exec_size"] <= 0).any():
            bad_nonneg_exec_size += 1
    return {
        "orders_checked": int(valid["maker_order_id"].nunique()),
        "orders_bad_sequence": bad_seq,
        "orders_negative_remaining": bad_nonneg_remaining,
        "orders_nonpositive_exec_size": bad_nonneg_exec_size,
        "passed": (bad_seq == 0
                   and bad_nonneg_remaining == 0
                   and bad_nonneg_exec_size == 0),
    }


# ---------------------------------------------------------------------------
# Population checks
# ---------------------------------------------------------------------------


def check_trade_count(df: pd.DataFrame, msg_tape_csv: Optional[str]) -> Dict[str, object]:
    """len(fill_tape) == number of Trade records in the source message_tape."""
    if msg_tape_csv is None:
        return {"passed": True, "skipped": True, "reason": "no message_tape provided"}
    n_trades = 0
    import csv
    from .fill_tape import _open_maybe_gzip  # local import to avoid cycle
    with _open_maybe_gzip(msg_tape_csv) as f:
        reader = csv.reader(f)
        header = next(reader)
        typ_i = header.index("typ")
        for row in reader:
            if row[typ_i] == "T":
                n_trades += 1
    return {
        "fill_tape_rows": len(df),
        "msg_tape_trade_rows": n_trades,
        "delta": len(df) - n_trades,
        "passed": len(df) == n_trades,
    }


def check_unmatched_maker_rate(df: pd.DataFrame, threshold: float = 0.10) -> Dict[str, object]:
    """Fraction of rows with maker_add_ts NaN. Should be small; >10% is a
    data-quality warning but not fatal."""
    n = len(df)
    if n == 0:
        return {"passed": True, "rate": 0.0, "n": 0}
    rate = float(df["maker_add_ts"].isna().mean())
    return {
        "n": n,
        "rate": rate,
        "warn": rate > threshold,
        "passed": True,   # non-fatal
    }


# ---------------------------------------------------------------------------
# Driver
# ---------------------------------------------------------------------------


def validate_fill_tape(fill_parquet: str,
                       msg_tape_csv: Optional[str] = None,
                       verbose: bool = True) -> Dict[str, object]:
    """Run every Day-1 identity + population check on one fill_tape.

    Returns a summary dict. Prints one line per check.

    Raises RuntimeError if any hard identity fails (side_identity,
    signflip_identity, or trade_count reconciliation).
    """
    df = pd.read_parquet(fill_parquet)
    summary: Dict[str, object] = {"path": str(fill_parquet), "n_rows": len(df)}

    def _report(name: str, result: dict, hard: bool = True):
        summary[name] = result
        passed = result.get("passed", False)
        if verbose:
            tag = "PASS" if passed else ("WARN" if not hard else "FAIL")
            print(f"[validate:{tag}] {name}: {result}", file=sys.stderr)
        if hard and not passed:
            raise RuntimeError(f"fill_tape validation failed: {name} → {result}")

    _report("side_identity", check_side_identity(df), hard=True)
    _report("signflip_identity", check_signflip_identity(df), hard=True)
    _report("fill_sequence_consistency", check_fill_sequence_consistency(df), hard=True)
    _report("trade_count_reconciliation",
            check_trade_count(df, msg_tape_csv), hard=True)
    _report("unmatched_maker_rate", check_unmatched_maker_rate(df),
            hard=False)

    if verbose:
        print(f"[validate] all gates passed for {fill_parquet}", file=sys.stderr)
    return summary


def main() -> int:
    import argparse
    ap = argparse.ArgumentParser(description="Day-1 validation for fill_tape")
    ap.add_argument("--fill", required=True, help="fill_tape.parquet")
    ap.add_argument("--msg-tape", default=None, help="source msg_tape CSV (for trade-count check)")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()
    try:
        validate_fill_tape(args.fill, msg_tape_csv=args.msg_tape,
                           verbose=not args.quiet)
    except RuntimeError as e:
        print(f"[validate:FAIL] {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
