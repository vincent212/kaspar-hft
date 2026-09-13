#!/usr/bin/env python3
# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
#
# Licensed under the MIT License. See LICENSE file in the project root.
"""Build grid_universe.<chan>.json: the master definitions, with every
contract's price limit widened to the widest it carried anywhere in the year.

    ./build_grid_universe.py 310 > out/universe/310/grid_universe.310.json

WHY THIS EXISTS. OB sizes its price ladder from the limit
(maxpx = high_limit_px / minPriceIncrement), and a ladder that is too small
silently drops far-resting orders -- the bad-price path removes an order it
cannot place rather than stranding it, so the loss is invisible rather than
fatal. The MASTER universe merges definitions across dates and its limit ends
up BELOW the widest limit actually observed: ESZ5 master 650,975 against
737,025 seen. So the master alone undersizes the ladder.

The limit is also the one genuinely daily quantity in an otherwise static
definition, which is why it cannot simply be taken from any single date.

WHY NOT PER-DATE UNIVERSES. 152 of 265 ES sessions have a per-date universe
that does not contain that session's own front month -- CME's instrument-replay
stream loops and restarts its sequence each cycle, so which definitions a date
captured is arbitrary. Definitions are static; only the limit moves. One
universe for every session also keeps the corpus from being split between two
treatments.

A ladder that is too LARGE costs one pointer per tick per side, which is
nothing. Erring wide is therefore free and erring narrow is silent corruption.
"""
import glob, json, sys, os

chan = sys.argv[1] if len(sys.argv) > 1 else sys.exit(__doc__)
base = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'out', 'universe', chan)

master_path = os.path.join(base, f'master_universe.{chan}.json')
if not os.path.exists(master_path):
    sys.exit(f"missing {master_path} -- run build_universe_all.sh {chan} first")
master = json.load(open(master_path))

# Widest limit each securityID carried on ANY date.
widest = {}
per_date = sorted(glob.glob(os.path.join(base, f'universe.{chan}.2*.json')))
for f in per_date:
    try:
        d = json.load(open(f))
    except Exception:
        continue
    for inst in d.get('instruments', []):
        sid = inst.get('securityID')
        hi = inst.get('high_limit_px')
        lo = inst.get('low_limit_px')
        if sid is None:
            continue
        w = widest.setdefault(sid, {'hi': None, 'lo': None})
        if hi is not None: w['hi'] = hi if w['hi'] is None else max(w['hi'], hi)
        if lo is not None: w['lo'] = lo if w['lo'] is None else min(w['lo'], lo)

widened = 0
for inst in master.get('instruments', []):
    w = widest.get(inst.get('securityID'))
    if not w:
        continue
    if w['hi'] is not None and w['hi'] > (inst.get('high_limit_px') or 0):
        inst['high_limit_px'] = w['hi']; widened += 1
    if w['lo'] is not None and inst.get('low_limit_px') is not None and w['lo'] < inst['low_limit_px']:
        inst['low_limit_px'] = w['lo']

print(json.dumps(master, indent=1, sort_keys=True))
sys.stderr.write(f"{chan}: {len(master.get('instruments', []))} instruments, "
                 f"{len(per_date)} per-date universes read, {widened} limits widened\n")
