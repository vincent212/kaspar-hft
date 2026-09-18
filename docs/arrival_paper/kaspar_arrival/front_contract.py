# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Licensed under the MIT License.
"""Front-contract picker: builds one CSV row per (session_date, channel)
naming the highest-volume outright for the target asset.

Inputs:
    /vast/home/vmayeski/out/arrival_paper/volstats/<chan>/volume.<chan>.<yyyymmdd>.json
    /home/vmayeski/kaspar-hft/dbento_pcap_parse/scripts/out/universe/<chan>/master_universe.<chan>.csv

For chan 310 → ES front. For chan 318 → NQ front. Micros (MNQ / MES) and
spreads are excluded from the picker on purpose: the paper's v1 scope is
outright front-month only, one stream per channel. Cross-market and micro
excitation is documented under "future work" in the outline.

For chan 326 (BTC), there is no master_universe file, so the picker chooses
argmax volume over all securityIDs on that channel — for BTC the front
contract dominates volume and this is a safe heuristic.

Output CSV columns:
    session_date  yyyymmdd
    channel       310 / 318 / 326
    asset         ES / NQ / BTC
    security_id
    symbol        (empty for BTC without universe)
    volume        the vol_max field from volstats (session peak sighting)
    trades        MBO trade-record count for that instrument
"""

from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path
from typing import Dict, Iterable, List, Optional


CHANNEL_ASSETS: Dict[int, str] = {
    310: "ES",
    318: "NQ",
    326: "BTC",
}


def load_universe_csv(path: str) -> Dict[int, dict]:
    """Load master_universe.<chan>.csv into secID -> row dict.

    Expects columns: type, securityID, symbol, venue, asset, minPriceIncrement,
    minCabPrice, dispFactor, tickSize, securityType.
    """
    out: Dict[int, dict] = {}
    with open(path) as f:
        reader = csv.DictReader(f)
        for row in reader:
            try:
                sid = int(row["securityID"])
            except (KeyError, ValueError):
                continue
            out[sid] = row
    return out


def pick_front(volstats_json: dict,
               universe: Optional[Dict[int, dict]],
               target_asset: Optional[str]) -> Optional[dict]:
    """Choose the front-month outright from a single volstats JSON.

    If universe is None (chan 326), argmax volume across the whole channel.
    Otherwise keep only rows whose (securityID -> universe) is `type='FDF'`
    (outright futures) and, when target_asset is given, whose `asset` matches.
    """
    instruments = volstats_json.get("instruments", [])
    best: Optional[dict] = None
    best_vol = -1
    for inst in instruments:
        sid = int(inst.get("securityID", 0))
        vol = int(inst.get("vol_max", 0))
        if universe is not None:
            row = universe.get(sid)
            if row is None:
                continue
            if row.get("type") != "FDF":
                continue
            if target_asset is not None and row.get("asset") != target_asset:
                continue
        if vol > best_vol:
            best_vol = vol
            best = {
                "security_id": sid,
                "symbol": (universe.get(sid, {}).get("symbol", "") if universe else ""),
                "vol_max": vol,
                "vol_last": int(inst.get("vol_last", 0)),
                "trades": int(inst.get("trades", 0)),
            }
    return best


def build_front_contract_csv(
    volstats_root: str,
    universe_root: str,
    channels: Iterable[int],
    out_csv: str,
) -> int:
    """Aggregate one CSV row per (session_date, channel) into out_csv."""
    universes: Dict[int, Optional[Dict[int, dict]]] = {}
    for chan in channels:
        univ_csv = Path(universe_root) / str(chan) / f"master_universe.{chan}.csv"
        universes[chan] = load_universe_csv(str(univ_csv)) if univ_csv.exists() else None

    rows: List[dict] = []
    for chan in channels:
        asset = CHANNEL_ASSETS.get(chan)
        chan_dir = Path(volstats_root) / str(chan)
        if not chan_dir.exists():
            continue
        for jf in sorted(chan_dir.glob(f"volume.{chan}.*.json")):
            # Parse yyyymmdd from filename.
            stem = jf.stem   # volume.310.20250310
            parts = stem.split(".")
            if len(parts) < 3:
                continue
            ymd = parts[2]
            try:
                with open(jf) as f:
                    js = json.load(f)
            except (json.JSONDecodeError, OSError):
                continue
            picked = pick_front(js, universes[chan], asset)
            if picked is None:
                rows.append({
                    "session_date": ymd, "channel": chan, "asset": asset or "",
                    "security_id": "", "symbol": "", "volume": 0, "trades": 0,
                    "notes": "no_matching_outright",
                })
                continue
            rows.append({
                "session_date": ymd,
                "channel": chan,
                "asset": asset or "",
                "security_id": picked["security_id"],
                "symbol": picked["symbol"],
                "volume": picked["vol_max"],
                "trades": picked["trades"],
                "notes": "",
            })

    rows.sort(key=lambda r: (r["session_date"], r["channel"]))
    fieldnames = ["session_date", "channel", "asset",
                  "security_id", "symbol", "volume", "trades", "notes"]
    Path(out_csv).parent.mkdir(parents=True, exist_ok=True)
    with open(out_csv, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fieldnames)
        w.writeheader()
        w.writerows(rows)
    return len(rows)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--volstats-root",
                    default="/vast/home/vmayeski/out/arrival_paper/volstats")
    ap.add_argument("--universe-root",
                    default="/home/vmayeski/kaspar-hft/dbento_pcap_parse/scripts/out/universe")
    ap.add_argument("--channels", default="310,318,326",
                    help="comma-separated list of channel ints (default 310,318,326)")
    ap.add_argument("--out", required=True, help="output CSV path")
    args = ap.parse_args()
    chans = [int(c) for c in args.channels.split(",") if c.strip()]
    n = build_front_contract_csv(
        volstats_root=args.volstats_root,
        universe_root=args.universe_root,
        channels=chans,
        out_csv=args.out,
    )
    print(f"wrote {n} rows to {args.out}")
    return 0


if __name__ == "__main__":
    import sys
    sys.exit(main())
