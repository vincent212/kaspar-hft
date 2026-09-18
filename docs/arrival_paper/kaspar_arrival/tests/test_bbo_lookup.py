# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Licensed under the MIT License.
"""Tests for the as-of BBO lookup and bbbochg loader."""

from __future__ import annotations

import numpy as np
import pandas as pd
import pytest

from kaspar_arrival.fill_tape import _bbo_mid_asof, load_bbbochg


class TestBboMidAsof:
    def test_exact_hit_returns_that_row(self):
        bbo_tx = np.array([100, 200, 300], dtype=np.int64)
        bbo_mid = np.array([1.0, 2.0, 3.0])
        out = _bbo_mid_asof(bbo_tx, bbo_mid, np.array([200]))
        assert out[0] == 2.0

    def test_between_returns_prior_row(self):
        bbo_tx = np.array([100, 200, 300], dtype=np.int64)
        bbo_mid = np.array([1.0, 2.0, 3.0])
        out = _bbo_mid_asof(bbo_tx, bbo_mid, np.array([150, 250, 299]))
        np.testing.assert_array_equal(out, [1.0, 2.0, 2.0])

    def test_before_first_returns_nan(self):
        bbo_tx = np.array([100, 200, 300], dtype=np.int64)
        bbo_mid = np.array([1.0, 2.0, 3.0])
        out = _bbo_mid_asof(bbo_tx, bbo_mid, np.array([50, 99]))
        assert all(np.isnan(out))

    def test_after_last_returns_last(self):
        bbo_tx = np.array([100, 200, 300], dtype=np.int64)
        bbo_mid = np.array([1.0, 2.0, 3.0])
        out = _bbo_mid_asof(bbo_tx, bbo_mid, np.array([301, 10_000]))
        np.testing.assert_array_equal(out, [3.0, 3.0])

    def test_vectorised_shape_preserved(self):
        bbo_tx = np.arange(10, dtype=np.int64) * 100
        bbo_mid = np.arange(10, dtype=np.float64)
        target = np.array([50, 150, 250, 999])
        out = _bbo_mid_asof(bbo_tx, bbo_mid, target)
        assert out.shape == target.shape

    def test_empty_target_returns_empty(self):
        bbo_tx = np.array([100, 200], dtype=np.int64)
        bbo_mid = np.array([1.0, 2.0])
        out = _bbo_mid_asof(bbo_tx, bbo_mid, np.array([], dtype=np.int64))
        assert out.shape == (0,)


class TestLoadBbbochg:
    def test_no_sym_filter(self, tmp_path, write_bbo, bbo_writer):
        rows = [
            bbo_writer(ts=1, sym=7, bid=100, ask=102),
            bbo_writer(ts=2, sym=10, bid=200, ask=204),
            bbo_writer(ts=3, sym=7, bid=101, ask=103),
        ]
        p = tmp_path / "b.csv.gz"
        write_bbo(p, rows, gzipped=True)
        df = load_bbbochg(str(p))
        assert len(df) == 3
        assert set(df["sym"].unique()) == {7, 10}

    def test_sym_filter(self, tmp_path, write_bbo, bbo_writer):
        rows = [
            bbo_writer(ts=1, sym=7, bid=100, ask=102),
            bbo_writer(ts=2, sym=10, bid=200, ask=204),
            bbo_writer(ts=3, sym=7, bid=101, ask=103),
        ]
        p = tmp_path / "b.csv.gz"
        write_bbo(p, rows, gzipped=True)
        df = load_bbbochg(str(p), sym=10)
        assert len(df) == 1
        assert df["sym"].iloc[0] == 10

    def test_mid_computed_correctly(self, tmp_path, write_bbo, bbo_writer):
        p = tmp_path / "b.csv.gz"
        write_bbo(p, [bbo_writer(ts=1, sym=10, bid=100, ask=110)], gzipped=True)
        df = load_bbbochg(str(p))
        assert df["mid"].iloc[0] == 105.0

    def test_output_sorted_by_tx_time(self, tmp_path, write_bbo, bbo_writer):
        # Write rows out of tx_time order; loader must sort.
        rows = [
            bbo_writer(ts=3, sym=10, bid=1, ask=2),
            bbo_writer(ts=1, sym=10, bid=1, ask=2),
            bbo_writer(ts=2, sym=10, bid=1, ask=2),
        ]
        p = tmp_path / "b.csv.gz"
        write_bbo(p, rows, gzipped=True)
        df = load_bbbochg(str(p))
        assert list(df["tx_time"]) == [1, 2, 3]

    def test_non_gzip_load(self, tmp_path, write_bbo, bbo_writer):
        p = tmp_path / "b.csv"
        write_bbo(p, [bbo_writer(ts=1, sym=10, bid=100, ask=110)], gzipped=False)
        df = load_bbbochg(str(p))
        assert len(df) == 1
