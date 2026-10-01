#!/usr/bin/env python3
# End-to-end (t1 - t0) latency per config, pooled across passes, from the
# LatencyProbe .msg files. Also latency conditional on ingress qlen (burst
# depth) -- the mechanism behind the fast_send tail.
#
# usage: paper_e2e.py [ROOT] [SKIP_S]
import glob, os, re, struct, sys
from collections import defaultdict

ROOT = sys.argv[1] if len(sys.argv) > 1 else '/home/vincent/perf/mdperf/paper'
SKIP = float(sys.argv[2]) if len(sys.argv) > 2 else 90.0
HDR, REC, MAGIC = 64, 16, b'KHMSGV01'
STREAMS = ['ESZ6_book', 'NQZ6_book', 'ZNZ6_book', 'ESZ6_trade', 'NQZ6_trade', 'ZNZ6_trade']
LEGEND = """
## Legend

**What is measured.** t1 - t0 per message, in microseconds:

- t0: user-space timestamp on the socket-reader thread, right after `recvfrom`
  returns the packet.
- t1: timestamp in TachBook (the MBO book) just before it publishes the updated
  book.

Time before t0 (NIC, socket buffer) is not included. Stream: front-month
contract (Z6), `book` = book updates, `trade` = trades.

**Columns.**

| column | meaning |
|---|---|
| config | label below |
| runs | number of 8-10 minute runs pooled |
| msgs | messages measured, after dropping the first 120 s of every run (startup recovery) |
| p1..p999 | percentiles of t1 - t0 over those messages |
| max | slowest single message |

Rows in the per-stream tables are sorted by p90, fastest first.

**Config labels.** Every config is `base` plus the listed changes. Exact files:
`configs/<label>/`. Full description: `../md_median_vs_tail_draft.md`,
section "Configuration labels".

| label | what changes vs base | thread hops between t0 and t1 |
|---|---|---|
| `base` | none (production path): socket reader -> MsgBuf (sleeps between packets) -> decode inline -> book on its own thread (sleeps) | 2, both into sleeping threads |
| `fastsend` | book update runs inline on the decode thread (`book_fast_send`) | 1 |
| `mbspin` | MsgBuf busy-polls instead of sleeping (`cme_msgbuf_mailbox lockfree_spin`) | 2, first without wakeup |
| `fastsend_mbspin` | both of the above | 1, without wakeup |
| `rfs` | socket reader runs MsgBuf, decode and book inline (`cme_reader_fast_send` + `book_fast_send`) | 0 |
| `p4s` | parallel decode: 4 busy-polling workers + one resequencing handler per channel (`cme_decode_workers 4`, `cme_decode_spin`) | 4 |
| `fsmb_pin` | `fastsend_mbspin`, busy threads pinned to NUMA node 2 CPUs (not isolated), everything else kept off node 2 | 1 |
| `rfs_pin` | `rfs`, each socket reader pinned alone on a physical core | 0 |
| `<label>_A` | same as `<label>`, feed B switched off (`cme_feed_b false`); one socket reader per channel | same |

All runs: Onload kernel bypass, live CME channels 310 (ES), 318 (NQ) and 344
(ZN), 2026-10-01 08:56-13:00 ET.

**Sections.**

- **Per-pass:** p50 / p99 / p999 of each run block, as a consistency check.
  - `p1`, `p2` = matrix passes 1 and 2.
  - `x1`, `x2` = experiment rounds 1 and 2.
- **Latency by ingress qlen:** latency grouped by how many packets were already
  queued at MsgBuf when this one arrived.
  - Shows queueing in bursts.
  - For `rfs*` there is no MsgBuf queue (the reader calls it directly), so qlen
    is always 0.
"""

CFG_ORDER = ['base', 'fastsend', 'mbspin', 'fastsend_mbspin', 'p4s', 'base_A', 'fastsend_A', 'mbspin_A', 'fastsend_mbspin_A', 'p4s_A', 'fsmb_pin', 'rfs', 'rfs_pin', 'rfs_pin_A']


def load(path):
    recs = []
    blob = open(path, 'rb').read()
    off, n = 0, len(blob)
    while off + HDR <= n and blob[off:off + 8] == MAGIC:
        recsz = struct.unpack_from('<I', blob, off + 8)[0]
        if recsz != REC:
            break
        off += HDR
        while off + REC <= n:
            if blob[off:off + 8] == MAGIC:
                break
            t1, l1, q, ix = struct.unpack_from('<QIHH', blob, off)
            off += REC
            if t1:
                recs.append((t1, l1, q, ix))
    return recs


def pct(v, p):
    return v[min(len(v) - 1, int(p * (len(v) - 1)))]


def runs():
    for d in sorted(glob.glob(os.path.join(ROOT, '[px][0-9]*_*'))):
        m = re.match(r'([px]\d+)_(.+)$', os.path.basename(d))
        if m:
            yield m.group(1), m.group(2), d


# pooled[cfg][stream] -> list of (lat_us, qlen, idx); per_pass[(cfg,pass)][stream] -> lat list
pooled = defaultdict(lambda: defaultdict(list))
per_pass = defaultdict(lambda: defaultdict(list))
for ps, cfg, d in runs():
    for s in STREAMS:
        f = os.path.join(d, f'lat_{s}.msg')
        if not os.path.exists(f):
            continue
        recs = load(f)
        if not recs:
            continue
        t0 = min(r[0] for r in recs) + SKIP * 1e9
        for r in recs:
            if r[0] >= t0:
                pooled[cfg][s].append((r[1] / 1e3, r[2], r[3]))
                per_pass[(cfg, ps)][s].append(r[1] / 1e3)

cfgs = [c for c in CFG_ORDER if c in pooled]
PS = [('p1', .01), ('p10', .10), ('p50', .50), ('p90', .90), ('p99', .99), ('p999', .999)]

print(f'# End-to-end t1-t0 (us), pooled over passes, first {SKIP:.0f}s of each run dropped')
print(LEGEND)
for s in STREAMS:
    print(f'\n## {s}')
    print(f'| config | runs | msgs | ' + ' | '.join(p for p, _ in PS) + ' | max |')
    print('|---|---|---|' + '---|' * (len(PS) + 1))
    rows = []
    for c in cfgs:
        v = sorted(x[0] for x in pooled[c][s])
        if len(v) < 100:
            continue
        nr = len([k for k in per_pass if k[0] == c and per_pass[k][s]])
        rows.append((pct(v, .90), f'| {c} | {nr} | {len(v):,} | ' +
                     ' | '.join(f'{pct(v, q):.1f}' for _, q in PS) + f' | {v[-1]:.0f} |'))
    for _, line in sorted(rows):
        print(line)

print('\n# Per-pass p50 / p99 / p999 (consistency check)')
for s in STREAMS[:3]:
    print(f'\n## {s}')
    passes = sorted({k[1] for k in per_pass})
    print('| config | ' + ' | '.join(f'{p}' for p in passes) + ' |')
    print('|---|' + '---|' * len(passes))
    for c in cfgs:
        cells = []
        for p in passes:
            v = sorted(per_pass.get((c, p), {}).get(s, []))
            cells.append(f'{pct(v,.5):.1f} / {pct(v,.99):.1f} / {pct(v,.999):.0f}' if len(v) > 100 else '-')
        print(f'| {c} | ' + ' | '.join(cells) + ' |')

print('\n# Latency by ingress qlen (packets queued ahead at MsgBuf), book streams pooled')
print('| config | qlen | share | msgs | p50 | p99 | p999 |')
print('|---|---|---|---|---|---|---|')
for c in cfgs:
    allv = [x for s in STREAMS[:3] for x in pooled[c][s]]
    if not allv:
        continue
    groups = defaultdict(list)
    for lat, q, _ in allv:
        groups[0 if q == 0 else 1 if q == 1 else 2 if q <= 3 else 4].append(lat)
    for g, lab in [(0, '0'), (1, '1'), (2, '2-3'), (4, '4+')]:
        v = sorted(groups.get(g, []))
        if len(v) < 30:
            continue
        print(f'| {c} | {lab} | {100*len(v)/len(allv):.2f}% | {len(v):,} | {pct(v,.5):.1f} | '
              f'{pct(v,.99):.1f} | {pct(v,.999):.1f} |')

_disc = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'e2e_discussion.md')
if os.path.exists(_disc):
    print()
    print(open(_disc).read())
