#!/usr/bin/env python3
# Per-stage latency per config, pooled over passes. Source: the STAGE lines each
# run's kaspr.log (copied into the run dir). For every 10 s window we have
# p1..p999; the table shows the median across windows (first SKIPW windows of
# each run and channel skipped: recovery backlog), plus the fraction of windows
# whose p999 exceeded 100 us (spike rate).
#
# usage: paper_stages.py [ROOT] [SKIPW]
import glob, os, re, statistics as st, sys
from collections import defaultdict

ROOT = sys.argv[1] if len(sys.argv) > 1 else '/home/vincent/perf/mdperf/paper'
SKIPW = int(sys.argv[2]) if len(sys.argv) > 2 else 12
rx = re.compile(r'(DataDecoder|HandlerIfActor)_(\d+) STAGE (\S+) n=(\d+) (.*) us')
PCT = ['p1', 'p10', 'p50', 'p99', 'p999']
CFG_ORDER = ['base', 'fastsend', 'mbspin', 'fastsend_mbspin', 'p4s', 'base_A', 'fastsend_A', 'mbspin_A', 'fastsend_mbspin_A', 'p4s_A', 'fsmb_pin', 'rfs', 'rfs_pin', 'rfs_pin_A', 'mbspin_tbspin_A', 'rfs_tbspin_A', 'rfs_A']
CH = {'310': 'ES', '318': 'NQ', '344': 'ZN'}

win = defaultdict(list)   # (cfg, ch, stage) -> [dict]
for d in sorted(glob.glob(os.path.join(ROOT, '[pxd][0-9]*_*'))):
    m = re.match(r'[pxd](\d+)_(.+)$', os.path.basename(d))
    f = os.path.join(d, 'kaspr.log')
    if not m or not os.path.exists(f):
        continue
    cfg = m.group(2)
    seen = defaultdict(int)
    for line in open(f, errors='replace'):
        mm = rx.search(line)
        if not mm:
            continue
        _, ch, stage, n, rest = mm.groups()
        if stage.startswith(('A1', 'TOTAL')) or stage == 'A1_read_to_send':
            pass
        key = (ch, stage)
        seen[key] += 1
        if seen[key] <= SKIPW or int(n) < 50:
            continue
        vals = {k: float(v) for k, v in (kv.split('=') for kv in rest.split())}
        win[(cfg, ch, stage)].append(vals)

cfgs = [c for c in CFG_ORDER if any(k[0] == c for k in win)]
stages = []
for k in win:
    if k[2] not in stages:
        stages.append(k[2])
for ch in ['310', '318', '344']:
    print(f'\n## {CH[ch]} ({ch}) -- median across 10 s windows of p1 / p10 / p50 / p99 / p999 (us); spike% = windows with p999 > 100 us')
    print('| stage | ' + ' | '.join(cfgs) + ' |')
    print('|---|' + '---|' * len(cfgs))
    for s in stages:
        cells = []
        for c in cfgs:
            ws = win.get((c, ch, s), [])
            if not ws:
                cells.append('-')
                continue
            meds = [st.median(w[p] for w in ws) for p in PCT]
            spike = 100 * sum(1 for w in ws if w['p999'] > 100) / len(ws)
            cells.append(' / '.join(f'{v:.1f}' for v in meds) + f' [{len(ws)}w, {spike:.0f}%]')
        print(f'| {s} | ' + ' | '.join(cells) + ' |')
