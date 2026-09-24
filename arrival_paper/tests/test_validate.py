# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Licensed under the MIT License.
"""Tests for the fill_tape Day-1 validation checks."""

from __future__ import annotations

import numpy as np
import pandas as pd
import pytest

from arrival_paper.validate import (
    check_fill_sequence_consistency,
    check_side_identity,
    check_signflip_identity,
    check_trade_count,
    check_unmatched_maker_rate,
    validate_fill_tape,
)


def _minimal_fill_df(**overrides):
    """Build a plausible 3-row fill_tape DataFrame for validation tests."""
    df = pd.DataFrame({
        "exec_ts": [1_000_000_000, 2_000_000_000, 3_000_000_000],
        "maker_order_id": [1, 1, 2],
        "aggressor_order_id": [0, 0, 0],
        "maker_side": [1, 1, -1],
        "aggressor_side": [-1, -1, 1],
        "exec_price_ticks": [1000e9, 1000e9, 1100e9],
        "exec_size": [1, 4, 1],
        "maker_add_ts": [500_000_000, 500_000_000, 500_000_000],
        "maker_last_modify_ts": [500_000_000, 500_000_000, 500_000_000],
        "time_in_queue": [500_000_000, 1_500_000_000, 2_500_000_000],
        "lifetime": [500_000_000, 1_500_000_000, 2_500_000_000],
        "maker_add_price_ticks": [1000e9, 1000e9, 1100e9],
        "remaining_size_after_fill": [4, 0, 0],
        "fill_seq_for_order": [1, 2, 1],
        "is_partial": [True, False, False],
        "size_ahead_at_submit": [np.nan, np.nan, np.nan],
        "size_remaining_at_fill": [np.nan, np.nan, np.nan],
        "n_orders_behind_at_fill": [np.nan, np.nan, np.nan],
        "spread_at_submit_ticks": [np.nan, np.nan, np.nan],
        "spread_at_fill_ticks": [np.nan, np.nan, np.nan],
        "markout_100ms": [1.0, 2.0, -1.0],
        "markout_1s": [3.0, 4.0, -2.0],
        "markout_10s": [5.0, 6.0, -3.0],
        "markout_30s": [7.0, 8.0, -4.0],
        "markout_100evt": [1.5, 2.5, -1.5],
        "markout_500evt": [1.0, 3.0, -2.0],
    })
    for k, v in overrides.items():
        df[k] = v
    return df


class TestCheckSideIdentity:
    def test_all_valid(self):
        df = _minimal_fill_df()
        r = check_side_identity(df)
        assert r["passed"]
        assert r["rows_bad"] == 0
        assert r["rows_checked"] == 3

    def test_one_bad_row(self):
        df = _minimal_fill_df()
        df.loc[1, "aggressor_side"] = 1   # both sides positive → sum = 2
        r = check_side_identity(df)
        assert not r["passed"]
        assert r["rows_bad"] == 1

    def test_nan_maker_side_skipped(self):
        df = _minimal_fill_df()
        df.loc[0, "maker_side"] = np.nan
        r = check_side_identity(df)
        assert r["passed"]
        assert r["rows_checked"] == 2


class TestCheckSignflipIdentity:
    def test_trivial_by_construction(self):
        df = _minimal_fill_df()
        r = check_signflip_identity(df)
        assert r["passed"]
        # per_tau dict should have one entry per markout column
        assert set(r["per_tau"].keys()) == {"markout_100ms", "markout_1s",
                                             "markout_10s", "markout_30s",
                                             "markout_100evt", "markout_500evt"}

    def test_tolerance_permits_floating_noise(self):
        # The check computes aggressor = -maker, so it always passes for
        # any finite value; this test just verifies we don't crash on
        # unusual magnitudes.
        df = _minimal_fill_df()
        df["markout_100ms"] = [1e15, -1e15, 0.0]
        r = check_signflip_identity(df)
        assert r["passed"]

    def test_all_nan_row_ignored(self):
        df = _minimal_fill_df()
        df.loc[0, [c for c in df.columns if c.startswith("markout_")]] = np.nan
        r = check_signflip_identity(df)
        assert r["passed"]
        # rows_checked drops by one for each all-NaN markout row
        assert r["per_tau"]["markout_100ms"]["rows_checked"] == 2


class TestCheckFillSequenceConsistency:
    def test_all_valid(self):
        df = _minimal_fill_df()
        r = check_fill_sequence_consistency(df)
        assert r["passed"]

    def test_gap_in_sequence_fails(self):
        df = _minimal_fill_df()
        df.loc[1, "fill_seq_for_order"] = 3   # 1, 3, 1 for oid=1
        r = check_fill_sequence_consistency(df)
        assert not r["passed"]
        assert r["orders_bad_sequence"] == 1

    def test_negative_remaining_fails(self):
        df = _minimal_fill_df()
        df.loc[0, "remaining_size_after_fill"] = -1
        r = check_fill_sequence_consistency(df)
        assert not r["passed"]
        assert r["orders_negative_remaining"] == 1

    def test_nonpositive_exec_size_fails(self):
        df = _minimal_fill_df()
        df.loc[0, "exec_size"] = 0
        r = check_fill_sequence_consistency(df)
        assert not r["passed"]
        assert r["orders_nonpositive_exec_size"] == 1

    def test_modify_up_mid_lifecycle_passes(self):
        """A maker Modifies size up between fills: remaining goes 3, 7, 0.
        This is legitimate on CME and must NOT fail the validator."""
        df = pd.DataFrame({
            "exec_ts": [1, 2, 3],
            "maker_order_id": [1, 1, 1],
            "aggressor_order_id": [0, 0, 0],
            "maker_side": [1, 1, 1],
            "aggressor_side": [-1, -1, -1],
            "exec_price_ticks": [1000e9] * 3,
            "exec_size": [2, 1, 7],
            "maker_add_ts": [0, 0, 0],
            "maker_last_modify_ts": [0, 0, 0],
            "time_in_queue": [1, 2, 3],
            "lifetime": [1, 2, 3],
            "maker_add_price_ticks": [1000e9] * 3,
            "remaining_size_after_fill": [3, 7, 0],   # goes UP from 3 to 7
            "fill_seq_for_order": [1, 2, 3],
            "is_partial": [True, True, False],
            "markout_100ms": [0.0, 0.0, 0.0],
        })
        r = check_fill_sequence_consistency(df)
        assert r["passed"]


class TestCheckTradeCount:
    def test_matches(self, tmp_path):
        df = _minimal_fill_df()
        msg = tmp_path / "msg.csv"
        with open(msg, "w") as f:
            f.write("transactTime,sendingTime,handlerendtim,recv_time,"
                    "packet_seq,idx_in_packet,typ,action,side,pxd,sz,orderID\n")
            for _ in range(3):
                f.write("0,0,0,0,0,0,T,X,,,1,42\n")
        r = check_trade_count(df, str(msg))
        assert r["passed"]
        assert r["delta"] == 0

    def test_mismatch_fails(self, tmp_path):
        df = _minimal_fill_df()
        msg = tmp_path / "msg.csv"
        with open(msg, "w") as f:
            f.write("transactTime,sendingTime,handlerendtim,recv_time,"
                    "packet_seq,idx_in_packet,typ,action,side,pxd,sz,orderID\n")
            for _ in range(5):
                f.write("0,0,0,0,0,0,T,X,,,1,42\n")
        r = check_trade_count(df, str(msg))
        assert not r["passed"]
        assert r["delta"] == -2

    def test_skip_when_no_msg_tape(self):
        r = check_trade_count(_minimal_fill_df(), None)
        assert r["passed"]
        assert r["skipped"]


class TestUnmatchedMakerRate:
    def test_zero_rate_ok(self):
        df = _minimal_fill_df()
        r = check_unmatched_maker_rate(df)
        assert r["rate"] == 0.0
        assert not r.get("warn")

    def test_high_rate_warns_but_passes(self):
        df = _minimal_fill_df()
        df.loc[0, "maker_add_ts"] = np.nan
        r = check_unmatched_maker_rate(df, threshold=0.10)
        assert r["passed"]        # non-fatal
        assert r["warn"]           # 1/3 > 10%


class TestValidateFillTape:
    def test_end_to_end_pass(self, tmp_path):
        df = _minimal_fill_df()
        p = tmp_path / "fill.parquet"
        df.to_parquet(p)
        summary = validate_fill_tape(str(p), msg_tape_csv=None, verbose=False)
        assert summary["n_rows"] == 3
        # Every hard check present
        for k in ("side_identity", "signflip_identity",
                  "fill_sequence_consistency", "trade_count_reconciliation",
                  "unmatched_maker_rate"):
            assert k in summary

    def test_hard_failure_raises(self, tmp_path):
        df = _minimal_fill_df()
        df.loc[1, "aggressor_side"] = 1   # side identity broken
        p = tmp_path / "fill.parquet"
        df.to_parquet(p)
        with pytest.raises(RuntimeError, match="side_identity"):
            validate_fill_tape(str(p), msg_tape_csv=None, verbose=False)
