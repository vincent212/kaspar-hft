import os, sys, time, errno

# CONTEXT-SWITCH RECORDER, ALIGNED TO THE LATENCY BINS.
#
# The 10-second spot checks answered "is preemption the cause" with a number
# (0.03% runqueue wait on the hot thread). That is enough to rule it out on
# average and not enough for a paper, because the objection is always the same:
# the mean is fine, what about the moment the tail happened.
#
# So this records the SAME quantity continuously, on the SAME 100ms grid as
# lat_*.csv, so the two can be joined on bin_key_ns. Then the claim stops being
# "preemption is small on average" and becomes "here is the runqueue wait in
# the exact bin that produced the 12ms max".
#
# WHAT IS RECORDED, per thread per bin, all as DELTAS over the bin:
#   d_cpu_ns     schedstat field 1 -- time ON cpu
#   d_rqwait_ns  schedstat field 2 -- time RUNNABLE BUT NOT RUNNING.
#                THIS IS THE PREEMPTION TERM. It is the only field here that
#                is directly comparable to a latency number: nanoseconds the
#                thread was ready to work and was denied a core.
#   d_slices     schedstat field 3 -- times scheduled onto a cpu
#   d_vol,d_nonvol  from status, at 1 Hz (see below)
#
# WHAT IT CANNOT SEE. schedstat measures being OFF CORE. It does not measure
# running SLOWLY while on core. With 65 threads on 8 physical cores, an SMT
# sibling stealing issue slots shows up as zero rqwait and degraded IPC. If
# rqwait is flat in a bad bin, that rules out preemption and does NOT rule out
# SMT contention -- that needs perf counters, not procfs.
#
# COST. schedstat is one line and cheap. status is ~57 lines and procfs
# regenerates it per read, so vol/nonvol is sampled at 1 Hz, not 10 Hz, and
# written to its own file rather than smeared across ten bins it did not
# happen in.
#
# Idle threads are skipped (no cpu, no slices). 65 threads x 10 Hz would be
# 650 rows/s; in practice a handful are active and the file stays small.
#
# usage: kh_ctxrec.py <pid> [seconds] [outdir]

PID = int(sys.argv[1])
DUR = float(sys.argv[2]) if len(sys.argv) > 2 else 0.0      # 0 = until killed
OUT = sys.argv[3] if len(sys.argv) > 3 else '/home/vincent/perf/mdperf'

BIN_NS = 100 * 1000 * 1000        # 100ms, same grid as lat_*.csv
TDIR = '/proc/%d/task' % PID


def read_sched():
    """tid -> (cpu_ns, rqwait_ns, slices). Threads may die mid-scan."""
    out = {}
    try:
        tids = os.listdir(TDIR)
    except OSError:
        return out
    for t in tids:
        try:
            with open(TDIR + '/' + t + '/schedstat') as f:
                p = f.read().split()
            out[t] = (int(p[0]), int(p[1]), int(p[2]))
        except (IOError, OSError, IndexError, ValueError):
            continue
    return out


def read_names():
    out = {}
    try:
        tids = os.listdir(TDIR)
    except OSError:
        return out
    for t in tids:
        try:
            with open(TDIR + '/' + t + '/comm') as f:
                out[t] = f.read().strip() or '?'
        except (IOError, OSError):
            continue
    return out


def read_switches():
    """tid -> (voluntary, nonvoluntary). Whole-file read, so 1 Hz only."""
    out = {}
    try:
        tids = os.listdir(TDIR)
    except OSError:
        return out
    for t in tids:
        v = nv = None
        try:
            with open(TDIR + '/' + t + '/status') as f:
                for ln in f:
                    if ln.startswith('voluntary_ctxt_switches'):
                        v = int(ln.split()[1])
                    elif ln.startswith('nonvoluntary_ctxt_switches'):
                        nv = int(ln.split()[1])
                        break
        except (IOError, OSError, IndexError, ValueError):
            continue
        if v is not None and nv is not None:
            out[t] = (v, nv)
    return out


if not os.path.isdir(TDIR):
    sys.stderr.write('no such pid: %d\n' % PID)
    sys.exit(1)

f_sched = open(OUT + '/ctx_sched.csv', 'a')
f_sw = open(OUT + '/ctx_switch.csv', 'a')
# Header re-emitted on every start; readers take the LAST header as the
# boundary, same convention as lat_*.csv, because the file is appended across
# restarts and a restart resets every delta baseline.
f_sched.write('bin_key_ns,tid,name,d_cpu_ns,d_rqwait_ns,d_slices\n')
f_sw.write('bin_key_ns,tid,name,d_vol,d_nonvol\n')
f_sched.flush()
f_sw.flush()

names = read_names()
prev = read_sched()
prev_sw = read_switches()
t_start = time.time()
next_tick = (int(time.time() * 1e9) // BIN_NS + 1) * BIN_NS
last_sw_ns = next_tick
n_bins = 0

try:
    while True:
        now_ns = int(time.time() * 1e9)
        if now_ns < next_tick:
            time.sleep((next_tick - now_ns) / 1e9)
        # The bin that just CLOSED is the one ending at next_tick, so it is
        # keyed by next_tick - BIN_NS. Keying it by `now` would shift every
        # row one bin later than the latency it must be joined to.
        key = next_tick - BIN_NS

        cur = read_sched()
        for t, (c, w, s) in cur.items():
            p = prev.get(t)
            if p is None:
                continue                      # new thread, no baseline yet
            dc, dw, ds = c - p[0], w - p[1], s - p[2]
            if dc < 0 or dw < 0 or ds < 0:
                continue                      # tid reused, baseline invalid
            if dc == 0 and ds == 0:
                continue                      # idle, do not write a row
            f_sched.write('%d,%s,%s,%d,%d,%d\n'
                          % (key, t, names.get(t, '?'), dc, dw, ds))
        prev = cur

        if next_tick - last_sw_ns >= 1000000000:
            cur_sw = read_switches()
            for t, (v, nv) in cur_sw.items():
                p = prev_sw.get(t)
                if p is None:
                    continue
                dv, dnv = v - p[0], nv - p[1]
                if dv < 0 or dnv < 0 or (dv == 0 and dnv == 0):
                    continue
                f_sw.write('%d,%s,%s,%d,%d\n'
                           % (key, t, names.get(t, '?'), dv, dnv))
            prev_sw = cur_sw
            last_sw_ns = next_tick
            names = read_names()              # refresh, threads come and go
            f_sched.flush()
            f_sw.flush()

        n_bins += 1
        next_tick += BIN_NS
        # If we fell behind (the recorder itself got preempted), skip forward
        # rather than spin emitting bins that already passed.
        now_ns = int(time.time() * 1e9)
        if next_tick < now_ns:
            next_tick = (now_ns // BIN_NS + 1) * BIN_NS

        if DUR and time.time() - t_start >= DUR:
            break
except KeyboardInterrupt:
    pass
finally:
    f_sched.flush()
    f_sw.flush()
    sys.stderr.write('kh_ctxrec: %d bins written\n' % n_bins)
