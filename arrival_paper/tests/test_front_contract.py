# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Licensed under the MIT License.
"""Tests for the front-contract picker (DB-primary, volstats cross-check)."""

from __future__ import annotations

import csv
import json
import sqlite3
from pathlib import Path

import pytest

from arrival_paper.front_contract import (
    build_front_contract_csv,
    load_kaspar_db_fronts,
    load_universe_csv,
    volstats_top_fdf,
)


# ---------------------------------------------------------------------------
# Fixture helpers
# ---------------------------------------------------------------------------


def _write_universe(path: Path, rows: list[dict]):
    fields = ["type", "securityID", "symbol", "venue", "asset",
              "minPriceIncrement", "minCabPrice", "dispFactor",
              "tickSize", "securityType"]
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        for r in rows:
            w.writerow({k: r.get(k, "") for k in fields})


def _write_volstats(path: Path, chan: int, ymd: str, insts: list[dict]):
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w") as f:
        json.dump({"channel": chan, "date": ymd, "instruments": insts}, f)


def _make_kaspar_db(path: Path, equity_rows=None, btc_rows=None):
    """Build a minimal kaspar.db with front_contract_equity + front_contract_btc."""
    con = sqlite3.connect(str(path))
    con.execute("""CREATE TABLE front_contract_equity (
        session_date TEXT, es_front TEXT, nq_front TEXT,
        es_raw TEXT, nq_raw TEXT, sync_flag INTEGER, stab_flag INTEGER)""")
    con.execute("""CREATE TABLE front_contract_btc (
        session_date TEXT, btc_front TEXT)""")
    for r in equity_rows or []:
        con.execute(
            "INSERT INTO front_contract_equity VALUES (?,?,?,?,?,?,?)",
            (r["session_date"], r.get("es_front"), r.get("nq_front"),
             r.get("es_raw"), r.get("nq_raw"),
             r.get("sync_flag", 0), r.get("stab_flag", 0))
        )
    for r in btc_rows or []:
        con.execute("INSERT INTO front_contract_btc VALUES (?,?)",
                    (r["session_date"], r.get("btc_front")))
    con.commit()
    con.close()


# ---------------------------------------------------------------------------
# load_universe_csv
# ---------------------------------------------------------------------------


class TestLoadUniverseCsv:
    def test_keyed_by_symbol_fdf_only(self, tmp_path):
        p = tmp_path / "u.csv"
        _write_universe(p, [
            {"type": "FDF", "securityID": "42288528", "symbol": "NQH5",
             "asset": "NQ", "tickSize": "0.25"},
            {"type": "SDF", "securityID": "999", "symbol": "SPREAD",
             "asset": "NQ", "tickSize": "0.25"},
            {"type": "ODF", "securityID": "888", "symbol": "OPT",
             "asset": "NQ", "tickSize": "0.25"},
        ])
        univ = load_universe_csv(str(p))
        assert "NQH5" in univ
        assert univ["NQH5"]["securityID"] == "42288528"
        assert "SPREAD" not in univ
        assert "OPT" not in univ


# ---------------------------------------------------------------------------
# load_kaspar_db_fronts
# ---------------------------------------------------------------------------


class TestLoadKasparDbFronts:
    def test_equity_and_btc(self, tmp_path):
        db = tmp_path / "kaspar.db"
        _make_kaspar_db(db,
            equity_rows=[
                {"session_date": "2025-03-10", "es_front": "ESH5", "nq_front": "NQH5"},
                {"session_date": "2025-03-11", "es_front": "ESM5", "nq_front": "NQM5",
                 "sync_flag": 1, "stab_flag": 0},
            ],
            btc_rows=[
                {"session_date": "2025-03-10", "btc_front": "BTCH5"},
            ]
        )
        fronts = load_kaspar_db_fronts(str(db), channels=[310, 318, 326])
        assert fronts[(310, "20250310")]["symbol"] == "ESH5"
        assert fronts[(318, "20250310")]["symbol"] == "NQH5"
        assert fronts[(310, "20250311")]["sync_flag"] == 1
        assert fronts[(326, "20250310")]["symbol"] == "BTCH5"

    def test_nulls_skipped(self, tmp_path):
        db = tmp_path / "kaspar.db"
        _make_kaspar_db(db,
            equity_rows=[{"session_date": "2025-03-10",
                          "es_front": None, "nq_front": "NQH5"}]
        )
        fronts = load_kaspar_db_fronts(str(db), channels=[310, 318])
        # ES row was None → not stored
        assert (310, "20250310") not in fronts
        assert fronts[(318, "20250310")]["symbol"] == "NQH5"


# ---------------------------------------------------------------------------
# volstats_top_fdf
# ---------------------------------------------------------------------------


class TestVolstatsTopFdf:
    def test_filters_micros_and_spreads(self, tmp_path):
        universe = {
            "MNQH5": {"type": "FDF", "securityID": "1", "asset": "MNQ"},
            "NQH5":  {"type": "FDF", "securityID": "2", "asset": "NQ"},
            "MESH5": {"type": "FDF", "securityID": "3", "asset": "MES"},
        }
        p = tmp_path / "v.json"
        _write_volstats(p, 318, "20250310", [
            {"securityID": 1, "vol_max": 5_000_000},   # MNQ — highest raw
            {"securityID": 2, "vol_max": 1_000_000},   # NQ
            {"securityID": 3, "vol_max": 4_000_000},   # MES
        ])
        # Target NQ: micro must be excluded even though it outvolumes NQ.
        assert volstats_top_fdf(p, universe, target_asset="NQ") == 2

    def test_no_universe_argmax_across_all(self, tmp_path):
        p = tmp_path / "v.json"
        _write_volstats(p, 326, "20250310", [
            {"securityID": 5, "vol_max": 100},
            {"securityID": 6, "vol_max": 999},
            {"securityID": 7, "vol_max": 50},
        ])
        assert volstats_top_fdf(p, universe_by_sym=None, target_asset=None) == 6

    def test_missing_json_returns_none(self, tmp_path):
        p = tmp_path / "does_not_exist.json"
        assert volstats_top_fdf(p, universe_by_sym={}, target_asset="NQ") is None

    def test_no_matching_asset_returns_none(self, tmp_path):
        universe = {"MES": {"type": "FDF", "securityID": "1", "asset": "MES"}}
        p = tmp_path / "v.json"
        _write_volstats(p, 310, "20250310",
                        [{"securityID": 1, "vol_max": 999}])
        assert volstats_top_fdf(p, universe, target_asset="ES") is None


# ---------------------------------------------------------------------------
# build_front_contract_csv (end-to-end)
# ---------------------------------------------------------------------------


class TestBuildFrontContractCsv:
    def _make_scaffolding(self, tmp_path):
        db_path = tmp_path / "kaspar.db"
        _make_kaspar_db(db_path,
            equity_rows=[
                {"session_date": "2025-03-10", "es_front": "ESH5", "nq_front": "NQH5"},
                {"session_date": "2025-03-11", "es_front": "ESM5", "nq_front": "NQM5",
                 "sync_flag": 1},
            ],
            btc_rows=[{"session_date": "2025-03-10", "btc_front": "BTCH5"}],
        )
        universe_root = tmp_path / "universe"
        _write_universe(universe_root / "310" / "master_universe.310.csv", [
            {"type": "FDF", "securityID": "5002", "symbol": "ESH5", "asset": "ES"},
            {"type": "FDF", "securityID": "4916", "symbol": "ESM5", "asset": "ES"},
        ])
        _write_universe(universe_root / "318" / "master_universe.318.csv", [
            {"type": "FDF", "securityID": "42288528", "symbol": "NQH5", "asset": "NQ"},
            {"type": "FDF", "securityID": "42005804", "symbol": "NQM5", "asset": "NQ"},
        ])
        volstats_root = tmp_path / "volstats"
        _write_volstats(volstats_root / "310" / "volume.310.20250310.json",
                        310, "20250310",
                        [{"securityID": 5002, "vol_max": 1_000_000},
                         {"securityID": 4916, "vol_max": 100_000}])
        _write_volstats(volstats_root / "318" / "volume.318.20250310.json",
                        318, "20250310",
                        [{"securityID": 42288528, "vol_max": 800_000}])
        _write_volstats(volstats_root / "326" / "volume.326.20250310.json",
                        326, "20250310",
                        [{"securityID": 999_777, "vol_max": 40_000}])
        return db_path, universe_root, volstats_root

    def test_happy_path(self, tmp_path):
        db, univ, vol = self._make_scaffolding(tmp_path)
        out = tmp_path / "front.csv"
        n = build_front_contract_csv(str(db), str(univ), str(vol),
                                     channels=[310, 318, 326], out_csv=str(out))
        # 4 equity rows (ES×2 dates + NQ×2 dates) + 1 BTC row
        assert n == 5
        rows = list(csv.DictReader(open(out)))
        by_key = {(int(r["channel"]), r["session_date"]): r for r in rows}
        # ES on 20250310: DB says ESH5 → secID 5002, volstats agrees.
        r = by_key[(310, "20250310")]
        assert r["symbol"] == "ESH5" and r["security_id"] == "5002" and r["volstats_ok"] == "yes"
        # BTC (chan 326, no master_universe): sec_id from volstats fallback
        r = by_key[(326, "20250310")]
        assert r["security_id"] == "999777"
        assert "volstats fallback" in r["notes"]
        # 20250311 rows exist even though volstats fixtures don't include them
        assert (310, "20250311") in by_key
        assert (318, "20250311") in by_key
        # sync_flag propagated
        assert by_key[(310, "20250311")]["sync_flag"] == "1"

    def test_symbol_missing_from_master_leaves_sec_id_empty(self, tmp_path):
        """Regression: earlier build would silently populate sec_id from
        volstats_top when master_universe lacked the DB symbol — that gave
        the WRONG securityID for a mismatched symbol (NQH4 in 2024 → NQH5's
        secID). Now sec_id must stay empty."""
        db_path = tmp_path / "kaspar.db"
        _make_kaspar_db(db_path,
            equity_rows=[{"session_date": "2024-01-02",
                          "es_front": "ESH4", "nq_front": "NQH4"}]
        )
        universe_root = tmp_path / "universe"
        # Master universe only has 2025 contracts; 2024 is missing.
        _write_universe(universe_root / "318" / "master_universe.318.csv", [
            {"type": "FDF", "securityID": "42288528", "symbol": "NQH5", "asset": "NQ"},
        ])
        volstats_root = tmp_path / "volstats"
        _write_volstats(volstats_root / "318" / "volume.318.20240102.json",
                        318, "20240102",
                        [{"securityID": 42288528, "vol_max": 100_000}])
        out = tmp_path / "front.csv"
        build_front_contract_csv(str(db_path), str(universe_root),
                                 str(volstats_root), channels=[318],
                                 out_csv=str(out))
        row = next(csv.DictReader(open(out)))
        assert row["symbol"] == "NQH4"
        # Bug repro: this used to be "42288528" (NQH5's secID). Now empty.
        assert row["security_id"] == ""
        assert "not in master_universe" in row["notes"]

    def test_volstats_mismatch_reported(self, tmp_path):
        db, univ, vol = self._make_scaffolding(tmp_path)
        # Overwrite volstats so ES's top volume is ESM5 (the next contract),
        # but the DB pick for 20250310 is still ESH5.
        _write_volstats(vol / "310" / "volume.310.20250310.json",
                        310, "20250310",
                        [{"securityID": 4916, "vol_max": 2_000_000},   # ESM5 top
                         {"securityID": 5002, "vol_max": 500_000}])    # ESH5 tail
        out = tmp_path / "front.csv"
        build_front_contract_csv(str(db), str(univ), str(vol),
                                 channels=[310], out_csv=str(out))
        row = next(csv.DictReader(open(out)))
        assert row["symbol"] == "ESH5"
        assert row["security_id"] == "5002"
        assert row["volstats_top"] == "4916"
        assert row["volstats_ok"] == "no"
        assert "volstats top 4916 != DB pick 5002" in row["notes"]

    def test_drop_roll_window_removes_mismatch_and_neighbors(self, tmp_path):
        """A volstats-mismatch session and its D-1 / D+1 must be dropped
        when drop_roll_window=1 is set. Only the same-channel neighbors
        should be dropped, not the other channel's."""
        db_path = tmp_path / "kaspar.db"
        _make_kaspar_db(db_path,
            equity_rows=[
                # Day before, day of, day after all present on both channels.
                {"session_date": "2025-03-10", "es_front": "ESH5", "nq_front": "NQH5"},
                {"session_date": "2025-03-11", "es_front": "ESM5", "nq_front": "NQH5"},  # ES rolls, NQ unchanged
                {"session_date": "2025-03-12", "es_front": "ESM5", "nq_front": "NQH5"},
            ]
        )
        universe_root = tmp_path / "universe"
        _write_universe(universe_root / "310" / "master_universe.310.csv", [
            {"type": "FDF", "securityID": "5002", "symbol": "ESH5", "asset": "ES"},
            {"type": "FDF", "securityID": "4916", "symbol": "ESM5", "asset": "ES"},
        ])
        _write_universe(universe_root / "318" / "master_universe.318.csv", [
            {"type": "FDF", "securityID": "42288528", "symbol": "NQH5", "asset": "NQ"},
        ])
        volstats_root = tmp_path / "volstats"
        # On 20250311 volstats top for ES is still ESH5 (5002), DB says ESM5 (4916) → mismatch
        _write_volstats(volstats_root / "310" / "volume.310.20250311.json",
                        310, "20250311",
                        [{"securityID": 5002, "vol_max": 2_000_000},
                         {"securityID": 4916, "vol_max": 1_500_000}])
        # NQ has no mismatch on any of the 3 days.
        for ymd in ("20250310", "20250311", "20250312"):
            _write_volstats(volstats_root / "318" / f"volume.318.{ymd}.json",
                            318, ymd,
                            [{"securityID": 42288528, "vol_max": 800_000}])
        out = tmp_path / "front.csv"
        build_front_contract_csv(str(db_path), str(universe_root),
                                 str(volstats_root), channels=[310, 318],
                                 out_csv=str(out), drop_roll_window=1)
        rows = list(csv.DictReader(open(out)))
        by_key = {(int(r["channel"]), r["session_date"]): r for r in rows}
        # ES chan on 3/10, 3/11, 3/12 all dropped (3/11 mismatch → window ±1)
        assert (310, "20250310") not in by_key
        assert (310, "20250311") not in by_key
        assert (310, "20250312") not in by_key
        # NQ chan untouched by the ES mismatch — all 3 NQ rows survive
        assert (318, "20250310") in by_key
        assert (318, "20250311") in by_key
        assert (318, "20250312") in by_key

    def test_drop_roll_window_zero_keeps_mismatch(self, tmp_path):
        """drop_roll_window=0 (the default in code, though the CLI defaults
        to 1) keeps mismatch rows in the output."""
        db, univ, vol = self._make_scaffolding(tmp_path)
        # Force a mismatch on 20250310 ES.
        _write_volstats(vol / "310" / "volume.310.20250310.json",
                        310, "20250310",
                        [{"securityID": 4916, "vol_max": 2_000_000},
                         {"securityID": 5002, "vol_max": 500_000}])
        out = tmp_path / "front.csv"
        n = build_front_contract_csv(str(db), str(univ), str(vol),
                                     channels=[310], out_csv=str(out),
                                     drop_roll_window=0)
        # 2 ES rows: 20250310 (mismatch) + 20250311 (no volstats for it)
        assert n == 2

    def test_min_yyyymmdd_filter(self, tmp_path):
        db, univ, vol = self._make_scaffolding(tmp_path)
        out = tmp_path / "front.csv"
        n = build_front_contract_csv(str(db), str(univ), str(vol),
                                     channels=[310, 318], out_csv=str(out),
                                     min_yyyymmdd="20250311")
        # 20250310 rows filtered out; only 20250311 kept (2 rows: ES + NQ)
        assert n == 2
        rows = list(csv.DictReader(open(out)))
        assert all(r["session_date"] == "20250311" for r in rows)
