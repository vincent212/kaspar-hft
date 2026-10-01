#!/usr/bin/env python3
# Socket-level loss per run from the onload_stackdump captured at the end of
# each window: oflow_drop / mem_drop summed over sockets, max socket queue depth.
import glob, os, re, sys
ROOT = sys.argv[1] if len(sys.argv) > 1 else '/home/vincent/perf/mdperf/paper'
rx = re.compile(r'rcv: oflow_drop=(\d+)\([^)]*\) mem_drop=(\d+) eagain=\d+ pktinfo=\d+ q_max_pkts=(\d+)')
print('\n| run | sockets | oflow_drop | mem_drop | max socket queue (pkts) |')
print('|---|---|---|---|---|')
for d in sorted(glob.glob(os.path.join(ROOT, '*_*'))):
    f = os.path.join(d, 'onload.txt')
    if not os.path.exists(f):
        continue
    rows = [tuple(map(int, m.groups())) for m in rx.finditer(open(f, errors='replace').read())]
    if not rows:
        print(f'| {os.path.basename(d)} | 0 | - | - | - |'); continue
    print(f'| {os.path.basename(d)} | {len(rows)} | {sum(r[0] for r in rows)} | '
          f'{sum(r[1] for r in rows)} | {max(r[2] for r in rows)} |')
