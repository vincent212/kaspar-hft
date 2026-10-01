#!/usr/bin/env python3
# Per-thread scheduler sampler for one process.
# usage: schedsample.py PID PROBE_STDOUT OUT_CSV [interval_s]
# Every interval, per thread: CPU time, run-queue wait (time runnable but not
# running: /proc/.../schedstat field 2), forced (nonvoluntary) and voluntary
# context switches, as deltas; plus the CPU it last ran on. Thread names come
# from kaspr's "<actor> tid: <n>" startup lines.
import os, re, sys, time

pid, probe_out, out = sys.argv[1], sys.argv[2], sys.argv[3]
interval = float(sys.argv[4]) if len(sys.argv) > 4 else 10.0
tid_rx = re.compile(r'(\S[^\n]*?) tid: (\d+)')


def names():
    m = {}
    try:
        for mm in tid_rx.finditer(open(probe_out, errors='replace').read()):
            m[mm.group(2)] = mm.group(1).strip()
    except OSError:
        pass
    return m


def snap():
    s = {}
    base = f'/proc/{pid}/task'
    for tid in os.listdir(base):
        try:
            cpu_ns, wait_ns, _ = open(f'{base}/{tid}/schedstat').read().split()
            st = open(f'{base}/{tid}/status').read()
            vol = int(re.search(r'^voluntary_ctxt_switches:\s+(\d+)', st, re.M).group(1))
            nonvol = int(re.search(r'^nonvoluntary_ctxt_switches:\s+(\d+)', st, re.M).group(1))
            last_cpu = open(f'{base}/{tid}/stat').read().rsplit(')', 1)[1].split()[36]
            comm = open(f'{base}/{tid}/comm').read().strip()
            s[tid] = (int(cpu_ns), int(wait_ns), vol, nonvol, last_cpu, comm)
        except (OSError, AttributeError, IndexError, ValueError):
            pass
    return s


with open(out, 'w') as f:
    f.write('epoch_s,tid,name,cpu_ms,runq_wait_us,vol_cs,nonvol_cs,last_cpu\n')
    prev = snap()
    while os.path.exists(f'/proc/{pid}'):
        time.sleep(interval)
        try:
            cur = snap()
        except FileNotFoundError:
            break
        nm = names()
        now = int(time.time())
        for tid, v in cur.items():
            p = prev.get(tid)
            if not p:
                continue
            f.write(f'{now},{tid},{v[5]},{(v[0]-p[0])/1e6:.1f},'
                    f'{(v[1]-p[1])/1e3:.1f},{v[2]-p[2]},{v[3]-p[3]},{v[4]}\n')
        f.flush()
        prev = cur
