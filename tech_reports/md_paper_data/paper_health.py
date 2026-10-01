#!/usr/bin/env python3
# Feed health per run, from each run's kaspr.log: gap waits, gaps declared,
# data recoveries beyond the startup one per channel, and per-config totals.
# Used for the feed A+B vs A-only comparison.
import glob, os, re, sys
from collections import defaultdict

ROOT = sys.argv[1] if len(sys.argv) > 1 else '/home/vincent/perf/mdperf/paper'
tot = defaultdict(lambda: [0, 0, 0, 0, 0])
print('| run | minutes | waiting for gap | gaps declared | extra data recoveries |')
print('|---|---|---|---|---|')
for d in sorted(glob.glob(os.path.join(ROOT, '[pxd][0-9]*_*'))):
    m = re.match(r'[pxd](\d+)_(.+)$', os.path.basename(d))
    f = os.path.join(d, 'kaspr.log')
    if not m or not os.path.exists(f):
        continue
    txt = open(f, errors='replace').read()
    waits = txt.count('waiting for gap to close')
    gaps = txt.count('have gap sn:')
    rec = len(re.findall(r'MessageProcessor initiating data recovery\b', txt))
    chans = len(set(re.findall(r'(\d{3})MessageProcessor initiating data recovery on start', txt))) or 3
    extra = max(0, rec - 2 * chans)
    wins = len(re.findall(r'(?:DataDecoder|HandlerIfActor)_310 STAGE TOTAL', txt))
    mins = wins / 6
    cfg = m.group(2)
    t = tot[cfg]
    t[0] += 1; t[1] += mins; t[2] += waits; t[3] += gaps; t[4] += extra
    print(f'| {os.path.basename(d)} | {mins:.1f} | {waits} | {gaps} | {extra} |')
print('\n| config | runs | minutes | waiting for gap / hr | gaps / hr | extra recoveries / hr |')
print('|---|---|---|---|---|---|')
for cfg in sorted(tot):
    n, mins, w, g, e = tot[cfg]
    hr = mins / 60 or 1
    print(f'| {cfg} | {n} | {mins:.0f} | {w/hr:.1f} | {g/hr:.1f} | {e/hr:.1f} |')
