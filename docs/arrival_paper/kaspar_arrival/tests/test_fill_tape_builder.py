# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Licensed under the MIT License.
"""End-to-end tests for the fill_tape builder.

Each test builds a tiny synthetic message_tape covering one specific scenario
(one Add → one Trade; multiple partials; Modify-price-reset; Modify-up-mid-
lifecycle; unmatched trade; both sides) and asserts the emitted fill_tape has
the exact row(s) and values.

The BBO tape is deliberately kept simple (constant spread around a walking
mid) so markout arithmetic is easy to verify.
"""

from __future__ import annotations

import numpy as np
import pandas as pd
import pytest

from kaspar_arrival.fill_tape import build_fill_tape, _tau_label


def _run(tmp_path, msg_rows, bbo_rows, write_msg, write_bbo, bbo_sym=10):
    msg_path = tmp_path / "msg.csv"
    bbo_path = tmp_path / "bbo.csv.gz"
    out_path = tmp_path / "fill.parquet"
    write_msg(msg_path, msg_rows)
    write_bbo(bbo_path, bbo_rows, gzipped=True)
    df = build_fill_tape(
        msg_tape_csv=str(msg_path),
        bbbochg_csv=str(bbo_path),
        out_parquet=str(out_path),
        bbo_sym=bbo_sym,
        log_every=0,
    )
    return df, msg_path


class TestSimpleFill:
    """Add at t=1 for 5 lots; single Trade at t=2 for 5 lots → 1 row, remaining=0."""

    def test_single_full_fill(self, tmp_path, msg_writer, bbo_writer,
                              write_msg, write_bbo):
        msg = [
            msg_writer(ts=1_000_000_000, typ="M", action="0", side="0",
                       pxd=1000.0, sz=5, oid=42),
            msg_writer(ts=2_000_000_000, typ="T", sz=5, oid=42),
        ]
        bbo = [
            bbo_writer(ts=500_000_000, sym=10, bid=999, ask=1001),
            bbo_writer(ts=1_500_000_000, sym=10, bid=999, ask=1001),
        ]
        df, _ = _run(tmp_path, msg, bbo, write_msg, write_bbo)
        assert len(df) == 1
        row = df.iloc[0]
        assert row["maker_order_id"] == 42
        assert row["maker_side"] == +1        # bid
        assert row["aggressor_side"] == -1
        assert row["exec_size"] == 5
        assert row["remaining_size_after_fill"] == 0
        assert row["fill_seq_for_order"] == 1
        assert not row["is_partial"]
        assert row["time_in_queue"] == 1_000_000_000
        assert row["lifetime"] == 1_000_000_000


class TestPartialFills:
    def test_two_partials_then_final(self, tmp_path, msg_writer, bbo_writer,
                                     write_msg, write_bbo):
        # Add 10 lots on the ask, then trades of 3, 4, 3.
        msg = [
            msg_writer(ts=1_000_000_000, typ="M", action="0", side="1",
                       pxd=1100.0, sz=10, oid=99),
            msg_writer(ts=2_000_000_000, typ="T", sz=3, oid=99),
            msg_writer(ts=3_000_000_000, typ="T", sz=4, oid=99),
            msg_writer(ts=4_000_000_000, typ="T", sz=3, oid=99),
        ]
        bbo = [bbo_writer(ts=500_000_000, sym=10, bid=1095, ask=1105)]
        df, _ = _run(tmp_path, msg, bbo, write_msg, write_bbo)
        assert len(df) == 3
        assert list(df["fill_seq_for_order"]) == [1, 2, 3]
        assert list(df["exec_size"]) == [3, 4, 3]
        assert list(df["remaining_size_after_fill"]) == [7, 3, 0]
        assert list(df["is_partial"]) == [True, True, False]
        # All rows share the same maker_order_id and maker_add_ts.
        assert set(df["maker_order_id"]) == {99}
        assert set(df["maker_add_ts"]) == {1_000_000_000}
        # maker_side is -1 (ask)
        assert set(df["maker_side"]) == {-1}
        assert set(df["aggressor_side"]) == {1}


class TestDeletePathway:
    def test_add_then_delete_produces_no_fill(self, tmp_path, msg_writer,
                                              bbo_writer, write_msg, write_bbo):
        msg = [
            msg_writer(ts=1_000_000_000, typ="M", action="0", side="0",
                       pxd=1000.0, sz=5, oid=42),
            msg_writer(ts=2_000_000_000, typ="M", action="2", side="0",
                       pxd=1000.0, sz=5, oid=42),
        ]
        bbo = [bbo_writer(ts=500_000_000, sym=10, bid=999, ask=1001)]
        df, _ = _run(tmp_path, msg, bbo, write_msg, write_bbo)
        assert len(df) == 0


class TestModifyPriceReset:
    """A price change resets last_modify_ts (loses queue priority per CME
    rules). time_in_queue < lifetime after the price change."""

    def test_price_change_resets_priority(self, tmp_path, msg_writer,
                                          bbo_writer, write_msg, write_bbo):
        msg = [
            msg_writer(ts=1_000_000_000, typ="M", action="0", side="0",
                       pxd=1000.0, sz=5, oid=7),
            msg_writer(ts=1_500_000_000, typ="M", action="1", side="0",
                       pxd=1001.0, sz=5, oid=7),   # price change
            msg_writer(ts=3_000_000_000, typ="T", sz=5, oid=7),
        ]
        bbo = [bbo_writer(ts=500_000_000, sym=10, bid=999, ask=1002)]
        df, _ = _run(tmp_path, msg, bbo, write_msg, write_bbo)
        assert len(df) == 1
        row = df.iloc[0]
        assert row["lifetime"] == 2_000_000_000
        assert row["time_in_queue"] == 1_500_000_000  # since the modify
        assert row["time_in_queue"] < row["lifetime"]


class TestModifySizeOnly:
    """A size-only Modify retains queue priority: time_in_queue == lifetime."""

    def test_size_only_modify_keeps_priority(self, tmp_path, msg_writer,
                                             bbo_writer, write_msg, write_bbo):
        msg = [
            msg_writer(ts=1_000_000_000, typ="M", action="0", side="0",
                       pxd=1000.0, sz=5, oid=7),
            msg_writer(ts=1_500_000_000, typ="M", action="1", side="0",
                       pxd=1000.0, sz=3, oid=7),   # size only
            msg_writer(ts=3_000_000_000, typ="T", sz=3, oid=7),
        ]
        bbo = [bbo_writer(ts=500_000_000, sym=10, bid=999, ask=1001)]
        df, _ = _run(tmp_path, msg, bbo, write_msg, write_bbo)
        assert len(df) == 1
        row = df.iloc[0]
        assert row["lifetime"] == 2_000_000_000
        assert row["time_in_queue"] == 2_000_000_000
        assert row["exec_size"] == 3


class TestModifyUpMidLifecycle:
    """Size increase mid-fill-sequence is legitimate on CME; remaining can go up."""

    def test_modify_up_between_fills(self, tmp_path, msg_writer, bbo_writer,
                                     write_msg, write_bbo):
        msg = [
            msg_writer(ts=1_000_000_000, typ="M", action="0", side="0",
                       pxd=1000.0, sz=5, oid=7),
            msg_writer(ts=2_000_000_000, typ="T", sz=2, oid=7),   # partial 1: remaining=3
            msg_writer(ts=2_500_000_000, typ="M", action="1", side="0",
                       pxd=1000.0, sz=8, oid=7),                   # resize up to 8
            msg_writer(ts=3_000_000_000, typ="T", sz=1, oid=7),   # partial 2: remaining=7
            msg_writer(ts=4_000_000_000, typ="T", sz=7, oid=7),   # final:    remaining=0
        ]
        bbo = [bbo_writer(ts=500_000_000, sym=10, bid=999, ask=1001)]
        df, _ = _run(tmp_path, msg, bbo, write_msg, write_bbo)
        assert len(df) == 3
        assert list(df["fill_seq_for_order"]) == [1, 2, 3]
        # remaining goes 3, 7, 0 — legitimate: size Modify raised residual.
        assert list(df["remaining_size_after_fill"]) == [3, 7, 0]
        assert list(df["is_partial"]) == [True, True, False]


class TestUnmatchedTrade:
    """A Trade whose maker was never observed (recovery-snapshot situation)
    still produces a row, with NaN maker fields, so trade-count reconciliation
    holds."""

    def test_trade_without_prior_add(self, tmp_path, msg_writer, bbo_writer,
                                     write_msg, write_bbo):
        msg = [
            msg_writer(ts=1_000_000_000, typ="T", sz=5, oid=999_999),
        ]
        bbo = [bbo_writer(ts=500_000_000, sym=10, bid=999, ask=1001)]
        df, _ = _run(tmp_path, msg, bbo, write_msg, write_bbo)
        assert len(df) == 1
        row = df.iloc[0]
        assert row["maker_order_id"] == 999_999
        assert pd.isna(row["maker_side"])
        assert pd.isna(row["maker_add_ts"])
        assert row["exec_size"] == 5


class TestSignFlipIdentity:
    """Every emitted row must satisfy maker_side + aggressor_side == 0
    and aggressor_markout == -maker_markout by construction (aggressor
    markout is derived as -markout at query time — the tape stores only
    the maker view)."""

    def test_signflip_holds_on_synthetic(self, tmp_path, msg_writer,
                                         bbo_writer, write_msg, write_bbo):
        msg = [
            msg_writer(ts=1_000_000_000, typ="M", action="0", side="0",
                       pxd=1000.0, sz=1, oid=1),
            msg_writer(ts=2_000_000_000, typ="T", sz=1, oid=1),
            msg_writer(ts=3_000_000_000, typ="M", action="0", side="1",
                       pxd=1100.0, sz=1, oid=2),
            msg_writer(ts=4_000_000_000, typ="T", sz=1, oid=2),
        ]
        bbo = [
            bbo_writer(ts=500_000_000, sym=10, bid=995, ask=1005),
            bbo_writer(ts=2_500_000_000, sym=10, bid=1090, ask=1110),
        ]
        df, _ = _run(tmp_path, msg, bbo, write_msg, write_bbo)
        assert len(df) == 2
        # Row-level identity.
        assert ((df["maker_side"] + df["aggressor_side"]) == 0).all()
        # Sign flip on markouts is definitional (aggressor := -maker), so
        # verify that the stored value is finite when it should be.
        for c in [c for c in df.columns if c.startswith("markout_")]:
            aggressor = -df[c]
            mask = df[c].notna()
            assert ((df[c] + aggressor)[mask] == 0).all()


class TestMarkoutArithmetic:
    """Force a controlled BBO trajectory and verify the maker markout equals
    the hand-computed value."""

    def test_maker_bid_gets_negative_markout_when_mid_falls(
        self, tmp_path, msg_writer, bbo_writer, write_msg, write_bbo
    ):
        # Maker is +1 (bid), fills at price 1000 (in tick × 1e9 units → 1000.0).
        # 1 s after exec, mid falls from 1000 to 990 → maker markout = +1 × (990 - 1000) = -10.
        msg = [
            msg_writer(ts=1_000_000_000, typ="M", action="0", side="0",
                       pxd=1000.0, sz=1, oid=1),
            msg_writer(ts=2_000_000_000, typ="T", sz=1, oid=1),
        ]
        bbo = [
            bbo_writer(ts=500_000_000, sym=10, bid=995, ask=1005),
            bbo_writer(ts=3_000_000_000, sym=10, bid=985, ask=995),  # 1 s after exec: mid=990
        ]
        df, _ = _run(tmp_path, msg, bbo, write_msg, write_bbo)
        assert len(df) == 1
        # markout at 1 s: maker_side × (mid_forward - exec_price_ticks / tick_scale)
        # exec_price_ticks stored as tick × 1e9 = 1e12. tick_scale = 1e9 → exec/scale = 1000.
        # mid_forward at t=3s is (985+995)/2 = 990.
        assert df["markout_1s"].iloc[0] == pytest.approx(990 - 1000)   # = -10

    def test_maker_ask_gets_positive_markout_when_mid_falls(
        self, tmp_path, msg_writer, bbo_writer, write_msg, write_bbo
    ):
        # Maker is -1 (ask), fills at price 1100. mid drops after: aggressor bought
        # high; maker sold high → maker markout is positive.
        msg = [
            msg_writer(ts=1_000_000_000, typ="M", action="0", side="1",
                       pxd=1100.0, sz=1, oid=1),
            msg_writer(ts=2_000_000_000, typ="T", sz=1, oid=1),
        ]
        bbo = [
            bbo_writer(ts=500_000_000, sym=10, bid=1095, ask=1105),
            bbo_writer(ts=3_000_000_000, sym=10, bid=1085, ask=1095),  # 1 s later: mid=1090
        ]
        df, _ = _run(tmp_path, msg, bbo, write_msg, write_bbo)
        # maker_side = -1, mid_forward = 1090, exec/scale = 1100 → -1 × (1090 - 1100) = +10
        assert df["markout_1s"].iloc[0] == pytest.approx(10)


class TestMarkoutEventCount:
    def test_100evt_markout_picks_kth_event_forward(
        self, tmp_path, msg_writer, bbo_writer, write_msg, write_bbo
    ):
        # Build 100 bbo events after the fill, each stepping mid by +1.
        # markout_100evt should therefore be +100 for a bid maker.
        msg = [
            msg_writer(ts=1_000_000_000, typ="M", action="0", side="0",
                       pxd=1000.0, sz=1, oid=1),
            msg_writer(ts=2_000_000_000, typ="T", sz=1, oid=1),
        ]
        bbo = [bbo_writer(ts=500_000_000, sym=10, bid=999, ask=1001)]
        # 100 BBO changes AFTER the exec_ts; each nudges mid by +1.
        base_ts = 2_500_000_000
        for k in range(1, 101):
            mid = 1000 + k
            bbo.append(bbo_writer(ts=base_ts + k * 100_000_000, sym=10,
                                  bid=mid - 1, ask=mid + 1))
        df, _ = _run(tmp_path, msg, bbo, write_msg, write_bbo)
        # 100th event after exec has mid = 1100, exec_price / scale = 1000.
        assert df["markout_100evt"].iloc[0] == pytest.approx(100)


class TestTauLabel:
    @pytest.mark.parametrize("tau_ns,expected", [
        (int(0.1e9), "100ms"),
        (int(1e9), "1s"),
        (int(10e9), "10s"),
        (int(30e9), "30s"),
        (int(500e6), "500ms"),
        (int(2.5e9), "2.5s"),
    ])
    def test_labels(self, tau_ns, expected):
        assert _tau_label(tau_ns) == expected


class TestBothSides:
    """Verify sign convention: side='0' → bid → maker_side=+1;
    side='1' → ask → maker_side=-1."""

    def test_bid_side_signs(self, tmp_path, msg_writer, bbo_writer,
                            write_msg, write_bbo):
        msg = [
            msg_writer(ts=1_000_000_000, typ="M", action="0", side="0",
                       pxd=1000.0, sz=1, oid=1),
            msg_writer(ts=2_000_000_000, typ="T", sz=1, oid=1),
        ]
        bbo = [bbo_writer(ts=500_000_000, sym=10, bid=995, ask=1005)]
        df, _ = _run(tmp_path, msg, bbo, write_msg, write_bbo)
        assert df["maker_side"].iloc[0] == +1

    def test_ask_side_signs(self, tmp_path, msg_writer, bbo_writer,
                            write_msg, write_bbo):
        msg = [
            msg_writer(ts=1_000_000_000, typ="M", action="0", side="1",
                       pxd=1100.0, sz=1, oid=1),
            msg_writer(ts=2_000_000_000, typ="T", sz=1, oid=1),
        ]
        bbo = [bbo_writer(ts=500_000_000, sym=10, bid=1095, ask=1105)]
        df, _ = _run(tmp_path, msg, bbo, write_msg, write_bbo)
        assert df["maker_side"].iloc[0] == -1


class TestTradeCountReconciliation:
    """Total emitted rows must equal number of Trade records in msg_tape,
    even when the Trade is unmatched."""

    def test_reconciles_with_unmatched_trades(self, tmp_path, msg_writer,
                                              bbo_writer, write_msg, write_bbo):
        msg = [
            msg_writer(ts=1_000_000_000, typ="T", sz=1, oid=1),   # unmatched
            msg_writer(ts=2_000_000_000, typ="M", action="0", side="0",
                       pxd=1000.0, sz=5, oid=2),
            msg_writer(ts=3_000_000_000, typ="T", sz=2, oid=2),
            msg_writer(ts=4_000_000_000, typ="T", sz=3, oid=2),
            msg_writer(ts=5_000_000_000, typ="T", sz=1, oid=99),  # unmatched
        ]
        bbo = [bbo_writer(ts=500_000_000, sym=10, bid=999, ask=1001)]
        df, _ = _run(tmp_path, msg, bbo, write_msg, write_bbo)
        assert len(df) == 4


class TestBBOSymFilter:
    """Rows for other syms in the bbbochg tape must not leak into markouts."""

    def test_filter_keeps_only_target_sym(self, tmp_path, msg_writer,
                                          bbo_writer, write_msg, write_bbo):
        msg = [
            msg_writer(ts=1_000_000_000, typ="M", action="0", side="0",
                       pxd=1000.0, sz=1, oid=1),
            msg_writer(ts=2_000_000_000, typ="T", sz=1, oid=1),
        ]
        # sym 7 has mid 5000 (wildly different); sym 10 has mid 990.
        bbo = [
            bbo_writer(ts=500_000_000, sym=7, bid=4995, ask=5005),
            bbo_writer(ts=500_000_000, sym=10, bid=995, ask=1005),
            bbo_writer(ts=3_000_000_000, sym=7, bid=4995, ask=5005),
            bbo_writer(ts=3_000_000_000, sym=10, bid=985, ask=995),
        ]
        df, _ = _run(tmp_path, msg, bbo, write_msg, write_bbo, bbo_sym=10)
        # If sym 7 leaked in, markout would be huge (~4000). It shouldn't.
        assert abs(df["markout_1s"].iloc[0]) < 100
