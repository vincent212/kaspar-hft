# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.). Licensed under the MIT License.
"""Tests for arrival_paper.build_delay_samplers.

Every fixture writes a small synthetic msgtape + panel parquet into tmp_path
so the test drives the same reservoir + quantile path production hits, just
at trivial scale."""

from __future__ import annotations

import csv
import json
from pathlib import Path

import numpy as np
import pandas as pd
import pytest

from arrival_paper.build_delay_samplers import (
    QUANTILES,
    Reservoir,
    build,
    build_grid,
    build_rows,
    collect_samples,
    load_kept_windows,
    resolve_k_us,
)


# ---------------------------------------------------------------------------
# Helpers to synthesise inputs
# ---------------------------------------------------------------------------


MSG_HEADER = ("transactTime,sendingTime,handlerendtim,recv_time,"
              "packet_seq,idx_in_packet,typ,action,side,pxd,sz,orderID")


def _write_msg_csv(path: Path, tt: np.ndarray, st: np.ndarray,
                   he: np.ndarray) -> None:
    with open(path, "w", newline="") as f:
        f.write(MSG_HEADER + "\n")
        w = csv.writer(f)
        for i in range(len(tt)):
            w.writerow([int(tt[i]), int(st[i]), int(he[i]),
                        0, 0, 0, "M", "add", "B", "0", 0, i])


def _write_panel(path: Path, session: str, symbol: str,
                 windows: list[dict]) -> None:
    """windows: [{start_ns, end_ns, lambda_bar, n_branch, n_events}]."""
    rows = []
    for w in windows:
        rows.append({
            "session_date": session,
            "symbol": symbol,
            "window_id": w.get("window_id", 0),
            "start_ns": int(w["start_ns"]),
            "end_ns": int(w["end_ns"]),
            "n_events": w.get("n_events", 100_000),
            "kept": True,
            "qsim_service_us": 7,
            "mu": 1.0, "alpha": 0.5, "beta": 1.0,
            "n_branch": w["n_branch"],
            "lambda_bar": w["lambda_bar"],
            "log_mean_lambda": 0.0,
            "p50_lat_me_ns": 1000.0, "p99_lat_me_ns": 5000.0,
            "p50_lat_handler_ns": 10_000.0, "p99_lat_handler_ns": 20_000.0,
            "p50_qsim_ns": 7000.0, "p99_qsim_ns": 15_000.0,
            "p50_absmark": 0.5, "p95_absmark": 2.0,
            "p50_absret": 0.1, "p95_absret": 0.5,
            "converged": True,
        })
    pd.DataFrame(rows).to_parquet(path, compression="snappy", index=False)


# ---------------------------------------------------------------------------
# resolve_k_us
# ---------------------------------------------------------------------------


class TestResolveK:
    def test_missing_file_uses_default(self, tmp_path, capsys):
        k = resolve_k_us(str(tmp_path / "nope.ini"))
        assert k == 40
        err = capsys.readouterr().err
        assert "WARN" in err

    def test_reads_set_delay_call(self, tmp_path):
        p = tmp_path / "k.ini"
        p.write_text("some_call\nob->set_delay(500, 540);\nmore\n")
        assert resolve_k_us(str(p)) == 40

    def test_reads_ini_keys(self, tmp_path):
        p = tmp_path / "k.ini"
        p.write_text("ob_delay_us 500\nob_cancel_delay_us 580\n")
        assert resolve_k_us(str(p)) == 80

    def test_no_delay_config_uses_default(self, tmp_path):
        p = tmp_path / "k.ini"
        p.write_text("nothing_relevant here 1\n")
        assert resolve_k_us(str(p)) == 40


# ---------------------------------------------------------------------------
# Reservoir
# ---------------------------------------------------------------------------


class TestReservoir:
    def test_below_capacity_keeps_all(self):
        rng = np.random.default_rng(0)
        r = Reservoir(cap=100, rng=rng)
        r.offer_many(np.arange(50, dtype=np.int64))
        assert sorted(r.values().tolist()) == list(range(50))

    def test_over_capacity_holds_at_capacity(self):
        rng = np.random.default_rng(0)
        r = Reservoir(cap=100, rng=rng)
        r.offer_many(np.arange(10_000, dtype=np.int64))
        assert len(r.values()) == 100

    def test_reservoir_is_uniform_ish(self):
        # 10k draws into a cap of 500 should give an empirical median
        # close to the true median.
        rng = np.random.default_rng(1)
        r = Reservoir(cap=500, rng=rng)
        r.offer_many(np.arange(10_000, dtype=np.int64))
        assert 4000 < np.median(r.values()) < 6000


# ---------------------------------------------------------------------------
# Cell binning determinism
# ---------------------------------------------------------------------------


class TestGridBinning:
    def test_deterministic_binning(self, tmp_path):
        # Two identical panels -> identical bin assignments.
        wins = [
            {"start_ns": i * 1_000_000_000,
             "end_ns": (i + 1) * 1_000_000_000 - 1,
             "lambda_bar": float(i + 1),
             "n_branch": float(i * 0.1)}
            for i in range(25)
        ]
        p1 = tmp_path / "p1.parquet"
        p2 = tmp_path / "p2.parquet"
        _write_panel(p1, "20250101", "NQH5", wins)
        _write_panel(p2, "20250102", "NQH5", wins)
        df1 = load_kept_windows(str(tmp_path))
        _, _, df1b = build_grid(df1)
        _, _, df1c = build_grid(df1)
        assert df1b["lambda_bin"].tolist() == df1c["lambda_bin"].tolist()
        assert df1b["n_bin"].tolist() == df1c["n_bin"].tolist()


# ---------------------------------------------------------------------------
# End-to-end: CDF recovery on synthetic input
# ---------------------------------------------------------------------------


def _bin_of(edges: np.ndarray, x: float) -> int:
    idx = int(np.searchsorted(edges, x, side="right") - 1)
    return max(0, min(len(edges) - 2, idx))


class TestBuildEndToEnd:
    def _mk_synthetic(self, tmp_path: Path, seed: int = 7,
                      n_wins: int = 25):
        """One session, `n_wins` non-overlapping windows all drawing latencies
        from the same known distribution. Windows have distinct lambda_bar /
        n_branch values so the 5x5 quantile grid is populated in every cell."""
        rng = np.random.default_rng(seed)
        n_per = 1_500
        n = n_per * n_wins
        start0 = 1_000_000_000_000
        step = 60 * 1_000_000_000
        tt = np.empty(n, dtype=np.int64)
        for w in range(n_wins):
            s = start0 + w * step
            e = s + step - 1_000_000
            tt[w * n_per:(w + 1) * n_per] = np.linspace(
                s + 1, e, n_per, dtype=np.int64)
        out_us = rng.lognormal(mean=np.log(30_000), sigma=0.4, size=n)
        st = tt + out_us.astype(np.int64)
        inb_us = rng.lognormal(mean=np.log(10_000), sigma=0.6, size=n)
        he = st + inb_us.astype(np.int64)
        tape_dir = tmp_path / "tapes"
        tape_dir.mkdir()
        _write_msg_csv(tape_dir / "20250101.NQH5.csv", tt, st, he)
        panel_dir = tmp_path / "panels"
        panel_dir.mkdir()
        panel_wins = []
        # Lay out (lambda, n) on a 5x5 lattice so equal_quantile_bins produces
        # a real 5-bin grid.
        for wi in range(n_wins):
            i, j = wi // 5, wi % 5
            s = start0 + wi * step
            e = s + step - 1_000_000
            panel_wins.append({
                "window_id": wi,
                "start_ns": s, "end_ns": e,
                "lambda_bar": 100.0 * (i + 1),
                "n_branch": 0.1 * (j + 1),
                "n_events": n_per,
            })
        _write_panel(panel_dir / "20250101.NQH5.parquet",
                     "20250101", "NQH5", panel_wins)
        return panel_dir, tape_dir, out_us, inb_us

    def test_cdf_recovery(self, tmp_path):
        panel_dir, tape_dir, out_ns, inb_ns = self._mk_synthetic(tmp_path)
        out_csv = tmp_path / "s.csv"
        out_json = tmp_path / "s.json"
        build(str(panel_dir), str(tape_dir), str(out_csv), str(out_json),
              k_us=40, max_samples=100_000, min_samples=100, seed=1)
        df = pd.read_csv(out_csv)
        out_rows = df[(df["side"] == "outbound") & (df["action"] == "send")]
        out_rows = out_rows.dropna(subset=["latency_us"])
        assert len(out_rows) > 0
        # Compare each emitted quantile against the empirical quantile of the
        # SOURCE distribution: within each cell the samples were drawn from the
        # same lognormal, so aggregating cells is expected to hold.
        src_us = out_ns / 1000.0
        for q in QUANTILES:
            emitted = out_rows[np.isclose(out_rows["quantile"], q)]["latency_us"]
            if len(emitted) == 0:
                continue
            true_q = float(np.quantile(src_us, q))
            # Per-cell sample sizes are ~1500; extreme tail quantiles have
            # wide sampling variance, so widen the tolerance there.
            if q >= 0.995 or q <= 0.01:
                tol = 0.60
            elif q in (0.99, 0.95):
                tol = 0.30
            else:
                tol = 0.10
            for v in emitted:
                assert abs(v - true_q) / max(true_q, 1e-9) < tol, (
                    f"quantile {q}: emitted {v} vs true {true_q}")

    def test_cancel_is_send_plus_k(self, tmp_path):
        panel_dir, tape_dir, *_ = self._mk_synthetic(tmp_path)
        out_csv = tmp_path / "s.csv"
        out_json = tmp_path / "s.json"
        k = 55
        build(str(panel_dir), str(tape_dir), str(out_csv), str(out_json),
              k_us=k, max_samples=100_000, min_samples=100, seed=1)
        df = pd.read_csv(out_csv)
        send = df[(df["side"] == "outbound") & (df["action"] == "send")]
        canc = df[(df["side"] == "outbound") & (df["action"] == "cancel")]
        merged = send.merge(canc, on=["lambda_bin", "n_bin", "quantile"],
                            suffixes=("_send", "_canc"))
        merged = merged.dropna(subset=["latency_us_send", "latency_us_canc"])
        assert len(merged) > 0
        diffs = merged["latency_us_canc"] - merged["latency_us_send"]
        # Exact float k, up to float-format round-trip.
        assert (diffs.round(3) == float(k)).all(), diffs.head().tolist()

    def test_low_sample_cell_is_nan(self, tmp_path, capsys):
        panel_dir, tape_dir, *_ = self._mk_synthetic(tmp_path)
        out_csv = tmp_path / "s.csv"
        out_json = tmp_path / "s.json"
        # Set min_samples above what any cell can produce -> all NaN.
        build(str(panel_dir), str(tape_dir), str(out_csv), str(out_json),
              k_us=40, max_samples=100_000, min_samples=10_000_000, seed=1)
        df = pd.read_csv(out_csv)
        assert df["latency_us"].isna().all()
        # Warning path must fire at least once.
        assert "low-sample cell" in capsys.readouterr().err

    def test_json_sidecar(self, tmp_path):
        panel_dir, tape_dir, *_ = self._mk_synthetic(tmp_path)
        out_csv = tmp_path / "s.csv"
        out_json = tmp_path / "s.json"
        build(str(panel_dir), str(tape_dir), str(out_csv), str(out_json),
              k_us=77, max_samples=1000, min_samples=100, seed=1)  # noqa
        j = json.loads(out_json.read_text())
        assert j["k_us"] == 77
        assert "lambda_edges" in j and "n_edges" in j
        assert j["quantiles"] == QUANTILES


class TestBuildRowsDirect:
    """Direct exercise of build_rows without hitting disk."""

    def test_cancel_equals_send_plus_k(self):
        samples = {
            ("outbound", 0, 0): np.array([1000, 2000, 3000, 4000, 5000] * 250,
                                          dtype=np.int64),
            ("inbound", 0, 0): np.array([10_000] * 1500, dtype=np.int64),
        }
        rows = build_rows(samples, k_us=25, grid=1, min_samples=100)
        df = pd.DataFrame(rows)
        s = df[(df["side"] == "outbound") & (df["action"] == "send")]
        c = df[(df["side"] == "outbound") & (df["action"] == "cancel")]
        # every quantile row: cancel = send + 25
        for q in QUANTILES:
            sv = float(s[s["quantile"] == q]["latency_us"].iloc[0])
            cv = float(c[c["quantile"] == q]["latency_us"].iloc[0])
            assert cv - sv == pytest.approx(25.0, abs=1e-6)

    def test_low_sample_cell_emits_nan(self):
        # Only 5 samples in the outbound reservoir; min_samples=1000 -> NaN.
        samples = {
            ("outbound", 0, 0): np.array([1, 2, 3, 4, 5], dtype=np.int64),
        }
        rows = build_rows(samples, k_us=40, grid=1, min_samples=1000)
        df = pd.DataFrame(rows)
        out = df[df["side"] == "outbound"]
        assert out["latency_us"].isna().all()
