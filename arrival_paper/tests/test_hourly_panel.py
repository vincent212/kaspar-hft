# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Licensed under the MIT License.
"""Tests for the hourly panel builder."""

from __future__ import annotations

import csv
import gzip

import numpy as np
import pandas as pd
import pytest

from arrival_paper.hourly_panel import (
    make_windows,
    fill_p95_absmarkout,
    return_p95_from_fill,
    build_hourly_panel,
    load_msgtape_latencies,
    latency_p99_in_window,
)
from arrival_paper.tests.test_hawkes import simulate_hawkes_ogata


class TestMakeWindows:
    def test_split_into_30min_windows(self):
        # 2 hours of ticks, 30 min windows → 4 windows
        span_ns = 2 * 3600 * 1_000_000_000
        ts = np.linspace(0, span_ns, 4000, dtype=np.int64)
        w = make_windows(ts, window_minutes=30)
        # Depending on inclusive end, 4 or 5 windows — assert at least 4
        assert len(w) >= 4
        # Every window should have a reasonable event count
        assert (w["n_events"] > 0).all()

    def test_empty_input(self):
        w = make_windows(np.array([], dtype=np.int64), window_minutes=30)
        assert len(w) == 0
        assert list(w.columns) == ["window_id", "start_ns", "end_ns", "n_events"]

    def test_all_in_one_window(self):
        # 100 events within 10 seconds, 30 min windows → 1 window
        ts = np.arange(1_000_000_000, 1_000_000_000 + 10 * 1_000_000_000, 100_000_000, dtype=np.int64)
        w = make_windows(ts, window_minutes=30)
        assert len(w) == 1
        assert w["n_events"].iloc[0] == len(ts)


class TestFillP95AbsMarkout:
    def test_typical_case(self):
        # Fills spread across 3 windows
        fill = pd.DataFrame({
            "exec_ts": np.arange(100, 200, dtype=np.int64),
            "markout_1s": np.linspace(-5, 5, 100),
        })
        # Take fills in [100, 200] — 100 fills, p95 |markout| ~= p95 of |.|
        # of a linspace(-5, 5, 100)
        p95 = fill_p95_absmarkout(fill, 100, 200)
        assert p95 == pytest.approx(4.7, abs=0.3)

    def test_returns_nan_if_too_few_fills(self):
        fill = pd.DataFrame({
            "exec_ts": np.array([100, 101], dtype=np.int64),
            "markout_1s": [1.0, 2.0],
        })
        assert np.isnan(fill_p95_absmarkout(fill, 100, 200))


class TestReturnP95FromFill:
    def test_positive_variance_gives_finite_return(self):
        fill = pd.DataFrame({
            "exec_ts": np.arange(0, 60_000_000_000, 30_000_000, dtype=np.int64),
            "exec_price_native": 100 + np.cumsum(np.random.default_rng(42).normal(0, 0.1, 2000)),
        })
        r = return_p95_from_fill(fill, 0, 60_000_000_000, stride_ns=1_000_000_000)
        assert np.isfinite(r)
        assert r > 0

    def test_returns_nan_on_empty_slice(self):
        fill = pd.DataFrame({
            "exec_ts": np.array([100, 200], dtype=np.int64),
            "exec_price_native": [100.0, 100.5],
        })
        assert np.isnan(return_p95_from_fill(fill, 1000, 2000))


class TestBuildHourlyPanel:
    def _write_msg_tape(self, path, ts_ns_list):
        with open(path, "w") as f:
            f.write("transactTime,sendingTime,handlerendtim,recv_time,"
                    "packet_seq,idx_in_packet,typ,action,side,pxd,sz,orderID\n")
            for ts in ts_ns_list:
                # sendingTime = transactTime + 100us, handlerendtim = sendingTime + 2us,
                # recv_time = 0 (MBO rows carry no recv_time)
                f.write(f"{ts},{ts+100_000},{ts+102_000},0,0,0,M,0,0,1000.0,1,42\n")

    def test_synthetic_hawkes_gives_finite_row(self, tmp_path):
        # Simulate a Hawkes with known params over 3600 s → arrivals in one 30-min window
        events_sec = simulate_hawkes_ogata(mu=5.0, alpha=0.7, beta=1.0,
                                           T=1800.0, seed=7)
        ts_ns = (events_sec * 1e9).astype(np.int64)
        assert len(ts_ns) > 5000, f"only {len(ts_ns)} events"

        msg_path = tmp_path / "msg.csv"
        self._write_msg_tape(msg_path, ts_ns.tolist())

        fill = pd.DataFrame({
            "exec_ts": np.linspace(ts_ns[0], ts_ns[-1], 100, dtype=np.int64),
            "markout_1s": np.random.default_rng(0).normal(0, 1, 100),
            "exec_price_native": 100 + np.cumsum(np.random.default_rng(1).normal(0, 0.1, 100)),
        })
        fill_path = tmp_path / "fill.parquet"
        fill.to_parquet(fill_path)

        out_path = tmp_path / "panel.parquet"
        panel = build_hourly_panel(
            str(msg_path), str(fill_path), str(out_path),
            window_minutes=30, min_events=1000,
            session_date="20250310", symbol="NQH5",
        )
        assert len(panel) >= 1
        # At least one window kept
        assert panel["kept"].any()
        kept = panel[panel["kept"]]
        # Recover branching ratio roughly (Hawkes MLE tolerance)
        assert not kept["n_branch"].isna().all()
        assert (kept["n_branch"] > 0.3).any()   # true 0.7, allow slop

    def test_low_activity_gets_filtered(self, tmp_path):
        # A window with only 50 events must be filtered when min_events=1000
        ts_ns = np.linspace(0, 1000_000_000_000, 50, dtype=np.int64)
        msg_path = tmp_path / "msg.csv"
        self._write_msg_tape(msg_path, ts_ns.tolist())
        fill_path = tmp_path / "fill.parquet"
        pd.DataFrame({"exec_ts": pd.array([], dtype="int64"),
                      "markout_1s": pd.array([], dtype="float64"),
                      "exec_price_native": pd.array([], dtype="float64")}).to_parquet(fill_path)
        out_path = tmp_path / "panel.parquet"
        panel = build_hourly_panel(
            str(msg_path), str(fill_path), str(out_path),
            window_minutes=30, min_events=1000,
        )
        assert not panel["kept"].any()
        assert panel["n_branch"].isna().all()

    def test_empty_msgtape_writes_empty_parquet(self, tmp_path):
        msg_path = tmp_path / "msg.csv"
        with open(msg_path, "w") as f:
            f.write("transactTime,sendingTime,handlerendtim,recv_time,"
                    "packet_seq,idx_in_packet,typ,action,side,pxd,sz,orderID\n")
        fill_path = tmp_path / "fill.parquet"
        pd.DataFrame({"exec_ts": pd.array([], dtype="int64"),
                      "markout_1s": pd.array([], dtype="float64")}).to_parquet(fill_path)
        out_path = tmp_path / "panel.parquet"
        panel = build_hourly_panel(
            str(msg_path), str(fill_path), str(out_path),
            window_minutes=30, min_events=1000,
        )
        assert len(panel) == 0
        expected_cols = {"session_date", "symbol", "window_id", "start_ns",
                         "end_ns", "n_events", "kept", "qsim_service_us",
                         "n_branch", "lambda_bar", "log_mean_lambda",
                         "p50_lat_me_ns", "p99_lat_me_ns",
                         "p50_lat_handler_ns", "p99_lat_handler_ns",
                         "p50_qsim_ns", "p99_qsim_ns",
                         "p50_absmark", "p95_absmark",
                         "p50_absret", "p95_absret"}
        assert expected_cols.issubset(set(panel.columns))


class TestLoadMsgtapeLatencies:
    def test_reads_both_latency_streams(self, tmp_path):
        p = tmp_path / "msg.csv"
        with open(p, "w") as f:
            f.write("transactTime,sendingTime,handlerendtim,recv_time,"
                    "packet_seq,idx_in_packet,typ,action,side,pxd,sz,orderID\n")
            for i in range(30):
                ts = 1_000_000_000 + i * 1_000_000
                f.write(f"{ts},{ts+50_000},{ts+52_000},0,0,0,M,0,0,1000.0,1,{i}\n")
            for i in range(20):
                ts = 2_000_000_000 + i * 1_000_000
                st = ts + 60_000
                he = st + 15_000
                f.write(f"{ts},{st},{he},0,0,0,T,0,0,1000.0,1,{i+100}\n")

        m = load_msgtape_latencies(str(p))
        assert len(m["ts_ns"]) == 50
        # ME latency valid on all rows (sendingTime > 0 on all)
        assert len(m["lat_me_ns"]) == 50
        assert (m["lat_me_ns"] > 0).all()
        assert 50_000 in m["lat_me_ns"] and 60_000 in m["lat_me_ns"]
        # Handler latency valid on all rows (handlerendtim > 0 on all)
        assert len(m["lat_hd_ns"]) == 50
        # MBO rows: 52_000 − 50_000 = 2_000; T rows: 15_000
        assert 2_000 in m["lat_hd_ns"] and 15_000 in m["lat_hd_ns"]

    def test_skips_rows_where_handlerendtim_is_zero(self, tmp_path):
        p = tmp_path / "msg.csv"
        with open(p, "w") as f:
            f.write("transactTime,sendingTime,handlerendtim,recv_time,"
                    "packet_seq,idx_in_packet,typ,action,side,pxd,sz,orderID\n")
            f.write("1000,1050,0,0,0,0,M,0,0,0.0,1,1\n")     # no handlerendtim
            f.write("2000,2050,2100,0,0,0,M,0,0,0.0,1,2\n")  # full row
        m = load_msgtape_latencies(str(p))
        # ME latency defined on both rows
        assert len(m["lat_me_ns"]) == 2
        # Handler latency only on the row with handlerendtim > 0
        assert len(m["lat_hd_ns"]) == 1
        assert m["lat_hd_ns"][0] == 50


class TestLatencyP99InWindow:
    def test_p99_of_uniform_latencies(self):
        # 100 latencies 1..100 ns at timestamps 0..99
        lat_ts = np.arange(100, dtype=np.int64)
        lat_ns = np.arange(1, 101, dtype=np.int64)
        p99 = latency_p99_in_window(lat_ts, lat_ns, 0, 99)
        # np.quantile(1..100, 0.99) ≈ 99.01
        assert p99 == pytest.approx(99.01, abs=0.5)

    def test_returns_nan_below_min_samples(self):
        lat_ts = np.arange(5, dtype=np.int64)
        lat_ns = np.arange(1, 6, dtype=np.int64)
        assert np.isnan(latency_p99_in_window(lat_ts, lat_ns, 0, 4))

    def test_window_selection(self):
        lat_ts = np.arange(100, dtype=np.int64)
        lat_ns = np.concatenate([np.full(50, 10), np.full(50, 1000)]).astype(np.int64)
        # First half only → p99 near 10
        p99 = latency_p99_in_window(lat_ts, lat_ns, 0, 49)
        assert p99 == 10
        # Second half only → p99 near 1000
        p99b = latency_p99_in_window(lat_ts, lat_ns, 50, 99)
        assert p99b == 1000

    def test_masks_non_positive_latency(self):
        lat_ts = np.arange(30, dtype=np.int64)
        lat_ns = np.concatenate([np.full(10, -1), np.full(20, 42)]).astype(np.int64)
        # 20 valid samples → passes min_samples floor, p99 = 42
        p99 = latency_p99_in_window(lat_ts, lat_ns, 0, 29)
        assert p99 == 42

    def test_empty_input_returns_nan(self):
        assert np.isnan(latency_p99_in_window(
            np.array([], dtype=np.int64),
            np.array([], dtype=np.int64),
            0, 100))
