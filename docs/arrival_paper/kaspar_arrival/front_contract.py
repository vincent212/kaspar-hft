# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Licensed under the MIT License.
"""Front-contract picker for the arrival-paper pipeline.

Primary source is kaspar.db (curated tables with roll-day flags baked in):
    front_contract_equity(session_date, es_front, nq_front, sync_flag, stab_flag)
    front_contract_btc   (session_date, btc_front)

The tables carry the front SYMBOL per session; we join to
    master_universe.<chan>.csv    (type, securityID, symbol, ..., asset, ...)
to look up the securityID for each symbol.

Optionally, if volstats JSONs are present at
    /vast/home/vmayeski/out/arrival_paper/volstats/<chan>/volume.<chan>.<yyyymmdd>.json
we cross-check: the DB-picked symbol's securityID should also be argmax-volume
among asset-matched FDFs in that day's volstats output. Disagreements are
flagged in the output CSV's `notes` column but do not fail the picker — the
DB wins.

Output CSV (columns):
    session_date  yyyymmdd
    channel       310 / 318 / 326
    asset         ES / NQ / BTC
    symbol        the front-month symbol (e.g. NQH5)
    security_id   the securityID that corresponds to that symbol in master_universe
    sync_flag     from kaspar.db when available; else empty
    stab_flag     from kaspar.db when available; else empty
    volstats_top  the top-volume FDF secID from volstats (if volstats present)
    volstats_ok   'yes' if security_id == volstats_top; 'no' if mismatch;
                  '' if no volstats data present for this session
    notes         free text on any quirks
"""

from __future__ import annotations

import argparse
import csv
import json
import sqlite3
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Tuple


# Channel → asset the paper uses on that channel.
CHANNEL_ASSETS: Dict[int, str] = {
    310: "ES",
    318: "NQ",
    326: "BTC",
}


def load_universe_csv(path: str) -> Dict[str, dict]:
    """Load master_universe.<chan>.csv keyed by symbol.

    Only FDF (outright futures) rows are kept — spreads (SDF) and options (ODF)
    live in this file too but the paper never wants them as a "front contract".
    """
    out: Dict[str, dict] = {}
    with open(path) as f:
        reader = csv.DictReader(f)
        for row in reader:
            if row.get("type") != "FDF":
                continue
            sym = row.get("symbol", "").strip()
            if not sym:
                continue
            out[sym] = row
    return out


def load_kaspar_db_fronts(
    db_path: str,
    channels: Iterable[int],
) -> Dict[Tuple[int, str], dict]:
    """Read the curated front-contract tables from kaspar.db.

    Returns a dict keyed by (channel, yyyymmdd) whose values carry the
    front symbol and (for equity) the sync/stab flags.
    """
    out: Dict[Tuple[int, str], dict] = {}
    con = sqlite3.connect(db_path)
    try:
        if 310 in channels or 318 in channels:
            cur = con.execute(
                "SELECT session_date, es_front, nq_front, sync_flag, stab_flag "
                "FROM front_contract_equity WHERE es_front IS NOT NULL "
                "OR nq_front IS NOT NULL"
            )
            for sd, es, nq, sync, stab in cur:
                ymd = sd.replace("-", "")
                if 310 in channels and es:
                    out[(310, ymd)] = {
                        "symbol": es, "sync_flag": sync, "stab_flag": stab,
                    }
                if 318 in channels and nq:
                    out[(318, ymd)] = {
                        "symbol": nq, "sync_flag": sync, "stab_flag": stab,
                    }
        if 326 in channels:
            cur = con.execute(
                "SELECT session_date, btc_front FROM front_contract_btc "
                "WHERE btc_front IS NOT NULL"
            )
            for sd, btc in cur:
                ymd = sd.replace("-", "")
                out[(326, ymd)] = {"symbol": btc, "sync_flag": None,
                                    "stab_flag": None}
    finally:
        con.close()
    return out


def volstats_top_fdf(
    volstats_json_path: Path,
    universe_by_sym: Optional[Dict[str, dict]],
    target_asset: Optional[str],
) -> Optional[int]:
    """Return the securityID of the highest-volume asset-matched FDF in the
    volstats JSON, or None if the file doesn't exist or the pick is empty.

    If universe_by_sym is None (chan 326), we return the plain argmax across
    all securityIDs — BTC front dominates chan 326 by design.
    """
    if not volstats_json_path.exists():
        return None
    try:
        with open(volstats_json_path) as f:
            js = json.load(f)
    except (json.JSONDecodeError, OSError):
        return None
    # Build sec_id -> universe row keyed by secID for a fast type/asset filter.
    universe_by_sid: Dict[int, dict] = {}
    if universe_by_sym is not None:
        for sym, row in universe_by_sym.items():
            try:
                universe_by_sid[int(row["securityID"])] = row
            except (KeyError, ValueError):
                continue

    best_sid: Optional[int] = None
    best_vol = -1
    for inst in js.get("instruments", []):
        try:
            sid = int(inst["securityID"])
            vol = int(inst.get("vol_max", 0))
        except (KeyError, ValueError):
            continue
        if universe_by_sym is not None:
            row = universe_by_sid.get(sid)
            if row is None or row.get("type") != "FDF":
                continue
            if target_asset is not None and row.get("asset") != target_asset:
                continue
        if vol > best_vol:
            best_vol = vol
            best_sid = sid
    return best_sid


def build_front_contract_csv(
    kaspar_db: str,
    universe_root: str,
    volstats_root: Optional[str],
    channels: Iterable[int],
    out_csv: str,
    min_yyyymmdd: Optional[str] = None,
) -> int:
    """Aggregate one CSV row per (session_date, channel) from kaspar.db,
    joined to master_universe for securityID + optional volstats cross-check.
    """
    universes_by_sym: Dict[int, Optional[Dict[str, dict]]] = {}
    for chan in channels:
        univ_csv = Path(universe_root) / str(chan) / f"master_universe.{chan}.csv"
        universes_by_sym[chan] = load_universe_csv(str(univ_csv)) if univ_csv.exists() else None

    fronts = load_kaspar_db_fronts(kaspar_db, channels)

    rows: List[dict] = []
    for (chan, ymd), db_row in sorted(fronts.items()):
        if min_yyyymmdd is not None and ymd < min_yyyymmdd:
            continue
        asset = CHANNEL_ASSETS.get(chan)
        symbol = db_row["symbol"]
        univ = universes_by_sym[chan]
        sec_id: Optional[str] = ""
        notes = ""
        if univ is not None:
            urow = univ.get(symbol)
            if urow is None:
                notes = f"symbol {symbol} not in master_universe.{chan}"
            else:
                if asset is not None and urow.get("asset") != asset:
                    notes = f"master_universe asset={urow.get('asset')} != {asset}"
                sec_id = urow.get("securityID", "")
        else:
            # No master_universe (chan 326). Fall through: volstats has to name secID.
            notes = "no master_universe for channel"

        # Optional volstats cross-check.
        vs_top: Optional[int] = None
        vs_ok = ""
        if volstats_root is not None:
            vs_path = Path(volstats_root) / str(chan) / f"volume.{chan}.{ymd}.json"
            vs_top = volstats_top_fdf(vs_path, univ, asset)
            if vs_top is not None:
                if sec_id:
                    if int(sec_id) == vs_top:
                        vs_ok = "yes"
                    else:
                        vs_ok = "no"
                        notes = (notes + "; " if notes else "") + \
                            f"volstats top {vs_top} != DB pick {sec_id}"
                elif univ is None:
                    # No master_universe for the channel (e.g. chan 326 BTC).
                    # Fall back to the top-volume outright per volstats. This
                    # is only safe when the channel is single-asset — never
                    # populate sec_id from volstats when a universe exists
                    # and the picked symbol is simply missing from it (that
                    # would fabricate the wrong securityID for a mismatched
                    # symbol; see the 2024 NQH4-vs-NQH5 case).
                    sec_id = str(vs_top)
                    vs_ok = "yes"
                    notes = (notes + "; " if notes else "") + \
                        "sec_id from volstats fallback (no master_universe)"

        rows.append({
            "session_date": ymd,
            "channel": chan,
            "asset": asset or "",
            "symbol": symbol,
            "security_id": sec_id,
            "sync_flag": db_row.get("sync_flag") or "",
            "stab_flag": db_row.get("stab_flag") or "",
            "volstats_top": vs_top if vs_top is not None else "",
            "volstats_ok": vs_ok,
            "notes": notes,
        })

    rows.sort(key=lambda r: (r["session_date"], r["channel"]))
    fieldnames = ["session_date", "channel", "asset", "symbol", "security_id",
                  "sync_flag", "stab_flag",
                  "volstats_top", "volstats_ok", "notes"]
    Path(out_csv).parent.mkdir(parents=True, exist_ok=True)
    with open(out_csv, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fieldnames)
        w.writeheader()
        w.writerows(rows)
    return len(rows)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--kaspar-db", default="/vast/home/vmayeski/db/kaspar.db")
    ap.add_argument("--universe-root",
                    default="/home/vmayeski/kaspar-hft/dbento_pcap_parse/scripts/out/universe")
    ap.add_argument("--volstats-root",
                    default="/vast/home/vmayeski/out/arrival_paper/volstats",
                    help="if present, cross-check secIDs against volstats output")
    ap.add_argument("--channels", default="310,318,326")
    ap.add_argument("--min-date", default=None,
                    help="skip sessions before this yyyymmdd (e.g. 20250101)")
    ap.add_argument("--out", required=True, help="output CSV path")
    args = ap.parse_args()
    chans = [int(c) for c in args.channels.split(",") if c.strip()]
    n = build_front_contract_csv(
        kaspar_db=args.kaspar_db,
        universe_root=args.universe_root,
        volstats_root=args.volstats_root,
        channels=chans,
        out_csv=args.out,
        min_yyyymmdd=args.min_date,
    )
    print(f"wrote {n} rows to {args.out}")
    return 0


if __name__ == "__main__":
    import sys
    sys.exit(main())
