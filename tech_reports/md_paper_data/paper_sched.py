#!/usr/bin/env python3
# Tail spikes vs scheduler: per 10 s wall-clock bucket, end-to-end latency
# (all book streams, from the .msg files) against run-queue wait and forced
# context switches of the hot-path threads (from sched.csv).
#
# usage: paper_sched.py [ROOT] [SKIP_S]
import csv, glob, os, re, struct, sys
from collections import defaultdict

ROOT = sys.argv[1] if len(sys.argv) > 1 else '/home/vincent/perf/mdperf/paper'
SKIP = float(sys.argv[2]) if len(sys.argv) > 2 else 120.0
HDR, REC, MAGIC = 64, 16, b'KHMSGV01'
HOT = re.compile(r'SketReader|MsgBuf|DataDecoder_|DataActor_|HandIfActor|TACHOB_|MgeProcessor')
CFG_ORDER = ['base', 'fastsend', 'mbspin', 'fastsend_mbspin', 'p4s', 'base_A', 'fastsend_A', 'mbspin_A', 'fastsend_mbspin_A', 'p4s_A', 'fsmb_pin', 'rfs', 'rfs_pin', 'rfs_pin_A', 'mbspin_tbspin_A', 'rfs_tbspin_A', 'rfs_A']


def role(name):
    if 'SketReader' in name: return 'sock_reader'
    if 'MsgBuf' in name: return 'msgbuf'
    if 'DataDecoder_' in name: return 'decoder(serial idle)'
    if 'DataActor_' in name: return 'par_worker'
    if 'HandIfActor' in name: return 'par_handler'
    if 'TACHOB_' in name: return 'tachbook'
    if 'MgeProcessor' in name: return 'msgproc'
    return 'other'


def load(path):
    recs = []
    blob = open(path, 'rb').read()
    off, n = 0, len(blob)
    while off + HDR <= n and blob[off:off + 8] == MAGIC:
        if struct.unpack_from('<I', blob, off + 8)[0] != REC:
            break
        off += HDR
        while off + REC <= n:
            if blob[off:off + 8] == MAGIC:
                break
            t1, l1, q, ix = struct.unpack_from('<QIHH', blob, off)
            off += REC
            if t1:
                recs.append((t1, l1))
    return recs


def pct(v, p):
    return v[min(len(v) - 1, int(p * (len(v) - 1)))] if v else 0


summary = defaultdict(lambda: defaultdict(list))  # cfg -> role -> [runq_wait_us per 10s]
spikes = defaultdict(list)                         # cfg -> (bucket max, p999, worst thread, its runq, nonvol, run)
corr = defaultdict(list)                           # cfg -> (bucket max e2e, max hot runq wait)

for d in sorted(glob.glob(os.path.join(ROOT, '[pxd][0-9]*_*'))):
    m = re.match(r'[pxd](\d+)_(.+)$', os.path.basename(d))
    if not m or not os.path.exists(os.path.join(d, 'sched.csv')):
        continue
    cfg = m.group(2)
    lat = defaultdict(list)
    tmin = None
    for f in glob.glob(os.path.join(d, 'lat_*Z6_book.msg')):
        recs = load(f)
        if recs:
            tmin = min(tmin or recs[0][0], min(r[0] for r in recs))
        for t1, l1 in recs:
            lat[t1 // 10_000_000_000].append(l1 / 1e3)
    if tmin is None:
        continue
    start_bucket = (tmin + int(SKIP * 1e9)) // 10_000_000_000
    rows = defaultdict(list)
    for r in csv.DictReader(open(os.path.join(d, 'sched.csv'))):
        if HOT.search(r['name']):
            b = (int(r['epoch_s']) - 1) // 10   # sample at epoch_s covers the preceding 10 s
            rows[b].append(r)
    for b, rs in rows.items():
        if b < start_bucket:
            continue
        for r in rs:
            summary[cfg][role(r['name'])].append(float(r['runq_wait_us']))
        v = sorted(lat.get(b, []))
        if len(v) < 50:
            continue
        worst = max(rs, key=lambda r: float(r['runq_wait_us']))
        corr[cfg].append((v[-1], float(worst['runq_wait_us'])))
        spikes[cfg].append((v[-1], pct(v, .999), worst['name'], float(worst['runq_wait_us']),
                            int(worst['nonvol_cs']), os.path.basename(d)))

cfgs = [c for c in CFG_ORDER if c in summary]
print(f'# Run-queue wait per thread role, us per 10 s sample (first {SKIP:.0f}s dropped)')
print('| config | role | samples | p50 | p90 | p99 | max |')
print('|---|---|---|---|---|---|---|')
for c in cfgs:
    for rl in sorted(summary[c]):
        v = sorted(summary[c][rl])
        print(f'| {c} | {rl} | {len(v)} | {pct(v,.5):.0f} | {pct(v,.9):.0f} | {pct(v,.99):.0f} | {v[-1]:.0f} |')

print('\n# Worst 10 s buckets by max end-to-end book latency, with the hot thread that waited longest')
print('| config | run | bucket max e2e us | bucket p999 us | worst thread | its runq wait us | its forced cs |')
print('|---|---|---|---|---|---|---|')
for c in cfgs:
    for s in sorted(spikes[c], reverse=True)[:6]:
        print(f'| {c} | {s[5]} | {s[0]:.0f} | {s[1]:.0f} | {s[2]} | {s[3]:.0f} | {s[4]} |')

print('\n# Bucket max e2e vs max hot-thread run-queue wait')
print('| config | buckets | buckets with max e2e > 100us | of those, hot runq wait > 100us | rank corr |')
print('|---|---|---|---|---|')
for c in cfgs:
    xs = corr[c]
    if len(xs) < 5:
        continue
    big = [x for x in xs if x[0] > 100]
    both = [x for x in big if x[1] > 100]
    def ranks(a):
        o = sorted(range(len(a)), key=lambda i: a[i]); r = [0] * len(a)
        for k, i in enumerate(o): r[i] = k
        return r
    ra, rb = ranks([x[0] for x in xs]), ranks([x[1] for x in xs])
    n = len(xs); ma = sum(ra) / n; mb = sum(rb) / n
    cov = sum((a - ma) * (b - mb) for a, b in zip(ra, rb))
    den = (sum((a - ma) ** 2 for a in ra) * sum((b - mb) ** 2 for b in rb)) ** .5 or 1
    print(f'| {c} | {n} | {len(big)} | {len(both)} | {cov/den:.2f} |')
