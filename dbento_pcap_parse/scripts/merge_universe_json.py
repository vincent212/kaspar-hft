#!/usr/bin/env python3
# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
#
# Licensed under the MIT License. See LICENSE file in the project root.
"""Merge the per-date universe JSONs into master_universe.<chan>.json.

build_universe_all.sh merges the per-date CSVs into a master CSV but not the
JSONs, and the JSON is what filter_bin and build_grid_universe.py read. Union by
securityID, first definition wins -- definitions are static, so any date that
carried the instrument describes it the same way.

    ./merge_universe_json.py 318 > out/universe/318/master_universe.318.json
"""
import glob, json, os, sys

chan = sys.argv[1] if len(sys.argv) > 1 else sys.exit(__doc__)
base = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'out', 'universe', chan)
files = sorted(glob.glob(os.path.join(base, f'universe.{chan}.2*.json')))
if not files:
    sys.exit(f"no per-date universes in {base}")

by_id, chan_name = {}, None
for f in files:
    try:
        d = json.load(open(f))
    except Exception:
        continue
    chan_name = chan_name or d.get('channel')
    for inst in d.get('instruments', []):
        sid = inst.get('securityID')
        if sid is not None and sid not in by_id:
            by_id[sid] = inst

out = {'channel': chan_name or int(chan),
       'date': 'merged',
       'instruments': [by_id[k] for k in sorted(by_id)]}
print(json.dumps(out, indent=1, sort_keys=True))
sys.stderr.write(f"{chan}: {len(files)} per-date universes -> {len(by_id)} instruments\n")
