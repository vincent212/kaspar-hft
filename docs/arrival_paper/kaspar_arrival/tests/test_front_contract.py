# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Licensed under the MIT License.
"""Tests for the front-contract picker."""

from __future__ import annotations

import csv
import json
from pathlib import Path

import pytest

from kaspar_arrival.front_contract import (
    build_front_contract_csv,
    load_universe_csv,
    pick_front,
)


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


class TestPickFront:
    def test_picks_max_volume_outright(self):
        universe = {
            42288528: {"type": "FDF", "symbol": "NQH5", "asset": "NQ"},
            42003617: {"type": "FDF", "symbol": "MNQH5", "asset": "MNQ"},
            99999999: {"type": "SDF", "symbol": "NQH5-NQM5", "asset": "NQ"},  # spread
        }
        insts = [
            {"securityID": 42003617, "vol_max": 3_000_000, "vol_last": 3_000_000, "trades": 2_500_000},
            {"securityID": 42288528, "vol_max": 800_000, "vol_last": 800_000, "trades": 700_000},
            {"securityID": 99999999, "vol_max": 500_000, "vol_last": 500_000, "trades": 400_000},
        ]
        picked = pick_front({"instruments": insts}, universe, target_asset="NQ")
        assert picked["security_id"] == 42288528  # MNQH5 excluded (wrong asset), spread excluded (SDF)
        assert picked["symbol"] == "NQH5"

    def test_micros_excluded_when_asset_is_full(self):
        """Chan 318 mixes NQ, MNQ, MES, RTY, M2K — MNQ has more raw volume
        than NQ on many days. Picker must keep NQ outrights only."""
        universe = {
            1: {"type": "FDF", "symbol": "MNQH5", "asset": "MNQ"},
            2: {"type": "FDF", "symbol": "NQH5", "asset": "NQ"},
            3: {"type": "FDF", "symbol": "MESH5", "asset": "MES"},
        }
        insts = [
            {"securityID": 1, "vol_max": 5_000_000, "vol_last": 0, "trades": 0},
            {"securityID": 2, "vol_max": 1_000_000, "vol_last": 0, "trades": 0},
            {"securityID": 3, "vol_max": 4_000_000, "vol_last": 0, "trades": 0},
        ]
        picked = pick_front({"instruments": insts}, universe, target_asset="NQ")
        assert picked["security_id"] == 2

    def test_spreads_excluded(self):
        universe = {
            1: {"type": "FDF", "symbol": "NQH5", "asset": "NQ"},
            2: {"type": "SDF", "symbol": "NQH5-NQM5", "asset": "NQ"},
        }
        insts = [
            {"securityID": 1, "vol_max": 100, "vol_last": 0, "trades": 0},
            {"securityID": 2, "vol_max": 10_000, "vol_last": 0, "trades": 0},
        ]
        picked = pick_front({"instruments": insts}, universe, target_asset="NQ")
        assert picked["security_id"] == 1

    def test_no_universe_picks_pure_argmax(self):
        """Chan 326 has no master_universe: fall through to argmax vol."""
        insts = [
            {"securityID": 1, "vol_max": 100, "vol_last": 0, "trades": 0},
            {"securityID": 2, "vol_max": 10_000, "vol_last": 0, "trades": 0},
            {"securityID": 3, "vol_max": 500, "vol_last": 0, "trades": 0},
        ]
        picked = pick_front({"instruments": insts}, universe=None, target_asset=None)
        assert picked["security_id"] == 2

    def test_returns_none_when_no_matching_asset(self):
        universe = {1: {"type": "FDF", "symbol": "MESH5", "asset": "MES"}}
        insts = [{"securityID": 1, "vol_max": 999, "vol_last": 0, "trades": 0}]
        picked = pick_front({"instruments": insts}, universe, target_asset="NQ")
        assert picked is None


class TestLoadUniverse:
    def test_load_master_universe_csv(self, tmp_path):
        p = tmp_path / "u.csv"
        _write_universe(p, [
            {"type": "FDF", "securityID": "42288528", "symbol": "NQH5",
             "asset": "NQ", "tickSize": "0.25"},
            {"type": "SDF", "securityID": "999", "symbol": "SPREAD",
             "asset": "NQ", "tickSize": "0.25"},
        ])
        univ = load_universe_csv(str(p))
        assert 42288528 in univ
        assert univ[42288528]["type"] == "FDF"
        assert univ[42288528]["symbol"] == "NQH5"
        assert 999 in univ


class TestBuildFrontContractCsv:
    def test_end_to_end_three_channels(self, tmp_path):
        volstats_root = tmp_path / "volstats"
        universe_root = tmp_path / "universe"

        # chan 310 (ES) universe + JSON
        _write_universe(universe_root / "310" / "master_universe.310.csv", [
            {"type": "FDF", "securityID": "111", "symbol": "ESH5", "asset": "ES"},
            {"type": "FDF", "securityID": "222", "symbol": "MESH5", "asset": "MES"},
        ])
        _write_volstats(volstats_root / "310" / "volume.310.20250310.json",
                        310, "20250310",
                        [{"securityID": 222, "vol_max": 5_000_000, "vol_last": 0, "trades": 0},
                         {"securityID": 111, "vol_max": 1_000_000, "vol_last": 0, "trades": 500_000}])

        # chan 318 (NQ) universe + JSON
        _write_universe(universe_root / "318" / "master_universe.318.csv", [
            {"type": "FDF", "securityID": "333", "symbol": "NQH5", "asset": "NQ"},
            {"type": "FDF", "securityID": "444", "symbol": "MNQH5", "asset": "MNQ"},
        ])
        _write_volstats(volstats_root / "318" / "volume.318.20250310.json",
                        318, "20250310",
                        [{"securityID": 444, "vol_max": 3_000_000, "vol_last": 0, "trades": 0},
                         {"securityID": 333, "vol_max": 800_000, "vol_last": 0, "trades": 600_000}])

        # chan 326 (BTC) — no universe
        _write_volstats(volstats_root / "326" / "volume.326.20250310.json",
                        326, "20250310",
                        [{"securityID": 555, "vol_max": 40_000, "vol_last": 0, "trades": 30_000}])

        out = tmp_path / "front_contract.csv"
        n = build_front_contract_csv(str(volstats_root), str(universe_root),
                                     channels=[310, 318, 326], out_csv=str(out))
        assert n == 3

        with open(out) as f:
            rows = list(csv.DictReader(f))
        by_chan = {int(r["channel"]): r for r in rows}
        # ES picked 111 (ESH5), not 222 (MESH5) — micro excluded
        assert by_chan[310]["security_id"] == "111"
        assert by_chan[310]["symbol"] == "ESH5"
        assert by_chan[310]["asset"] == "ES"
        # NQ picked 333 (NQH5), not 444 (MNQH5)
        assert by_chan[318]["security_id"] == "333"
        assert by_chan[318]["symbol"] == "NQH5"
        # BTC (no universe) picked argmax = 555
        assert by_chan[326]["security_id"] == "555"
        assert by_chan[326]["symbol"] == ""    # no universe → symbol blank

    def test_no_matching_outright_reports_note(self, tmp_path):
        volstats_root = tmp_path / "volstats"
        universe_root = tmp_path / "universe"
        _write_universe(universe_root / "310" / "master_universe.310.csv", [
            {"type": "FDF", "securityID": "222", "symbol": "MESH5", "asset": "MES"},
        ])
        _write_volstats(volstats_root / "310" / "volume.310.20250310.json",
                        310, "20250310",
                        [{"securityID": 222, "vol_max": 5, "vol_last": 0, "trades": 0}])
        out = tmp_path / "front_contract.csv"
        build_front_contract_csv(str(volstats_root), str(universe_root),
                                 channels=[310], out_csv=str(out))
        with open(out) as f:
            row = next(csv.DictReader(f))
        assert row["notes"] == "no_matching_outright"
