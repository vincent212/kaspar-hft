#!/usr/bin/env python3
# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Licensed under the MIT License. See LICENSE file in the project root.
#
# Reduce raw bench_pingpong stdout to the tables in
# ../BENCH_RESULTS_HFT_SERVER.md.
#
#   python3 aggregate.py [dir]      # dir defaults to this script's directory
#
# Expects <dir>/{pool_on,pool_off,fastsend}_N.txt as written by run_bench.sh.

import re, sys, glob, os, statistics

D = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.abspath(__file__))

ROW = re.compile(
    r'^([a-z][a-z0-9 _+()\-]*?)\s{2,}'
    r'(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+([\d.]+)\s+([\d.]+)\s*$')

COLS = ['p50', 'p90', 'p99', 'p99.9', 'min', 'max', 'mean', 'amort']


def parse(path):
    rows = {}
    with open(path) as fh:
        for line in fh:
            m = ROW.match(line)
            if m:
                rows[m.group(1).strip()] = [float(x) for x in m.groups()[1:]]
    return rows


for tag in ['pool_on', 'pool_off']:
    files = sorted(glob.glob(os.path.join(D, f'{tag}_*.txt')))
    if not files:
        print(f'### {tag}: no files in {D}')
        continue
    per = [parse(f) for f in files]
    print(f'### {tag}  (n={len(files)} runs, median of the {len(files)} run-medians)')
    print(f'{"mode":<22}' + ''.join(f'{c:>10}' for c in COLS) + '   p50 spread')
    for mode in per[0]:
        vals = [p[mode] for p in per if mode in p]
        med = [statistics.median(v[i] for v in vals) for i in range(len(COLS))]
        p50s = [v[0] for v in vals]
        print(f'{mode:<22}' + ''.join(f'{m:>10.1f}' for m in med)
              + f'   {min(p50s):.0f}-{max(p50s):.0f}')
    print()

print('### fastsend headline')
for f in sorted(glob.glob(os.path.join(D, 'fastsend_*.txt'))):
    for line in open(f):
        if 'direct call :' in line or 'fast_send   :' in line:
            print(os.path.basename(f), line.rstrip())

print()
print('### sanity gate: fast_send < send grouped < send ungrouped (p50, per run)')
fail = False
for f in sorted(glob.glob(os.path.join(D, 'pool_on_*.txt'))):
    r = parse(f)
    fs, g, u = r['fast_send'][0], r['send grouped'][0], r['send ungrouped'][0]
    ok = fs < g < u
    fail |= not ok
    print(f'{os.path.basename(f)}: fast_send={fs:.0f} grouped={g:.0f} '
          f'ungrouped={u:.0f}  -> {"PASS" if ok else "FAIL"}')

# The runbook says: if the ordering does not hold, stop and diagnose; do not publish.
sys.exit(1 if fail else 0)
