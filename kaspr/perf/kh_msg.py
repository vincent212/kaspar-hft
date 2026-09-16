import sys, os, glob, struct

# PER-MESSAGE QLEN vs LATENCY. The thing every earlier table was a proxy for.
#
# Reads lat_<sym>_<pop>.msg, written by LatencyProbe::msg_record(). One
# 16-byte record per admitted message:
#
#     t1     uint64   publish ts
#     l1_ns  uint32   t1 - t0 for THIS message
#     qlen   uint16   ingress_qlen for THIS message
#     idx    uint16   decode position inside its own packet
#
# WHY THIS IS DIFFERENT FROM EVERY PREVIOUS TABLE. Until now qlen and latency
# only ever met inside a 100ms bin, as (qlen_max, qlen_sum, l1_sum, n).
#
#   - Grouping on qlen_max was not interpretable: the group was SELECTED on
#     the deepest message in the bin but AVERAGED over all of them, and
#     msgs/bin itself rises with qlen, so the effect was divided by a
#     denominator that grew with it.
#   - Grouping on qlen_sum/n (kh_scat.py) is sound but ECOLOGICAL: it says
#     "bins where messages saw longer queues were slower", not "a message at
#     qlen d costs a + b*d".
#
# Here the pair is intact. mean(l1 | qlen == d) is a conditional mean over
# messages, with d an exact integer. No bin, no max, no sum, no dilution.
#
# usage: kh_msg.py [HH:MM:SS] [HH:MM:SS]

import time

DIR = os.environ.get('MDPERF_DIR', '/home/vincent/perf/mdperf')
HDR = 64
REC = 16
MAGIC = b'KHMSGV01'


def _ns(hms):
    lt = time.localtime()
    h, m, s = [int(x) for x in hms.split(':')]
    return int(time.mktime((lt.tm_year, lt.tm_mon, lt.tm_mday,
                            h, m, s, 0, 0, -1))) * 1000000000


T0 = _ns(sys.argv[1]) if len(sys.argv) > 1 else 0
T1 = _ns(sys.argv[2]) if len(sys.argv) > 2 else (1 << 62)


def load(path):
    """-> (recs, bin_ns, nbad). Tolerates a trailing partial record: the
    writer flushes on a timer and the file can be read mid-write."""
    recs = []
    bin_ns = 100000000
    nbad = 0
    with open(path, 'rb') as f:
        blob = f.read()
    off = 0
    n = len(blob)
    while off + HDR <= n and blob[off:off + 8] == MAGIC:
        # A restart appends a fresh header. Walk them all rather than
        # assuming one, so a restarted file is not silently truncated.
        recsz, ver = struct.unpack_from('<II', blob, off + 8)
        bin_ns = struct.unpack_from('<Q', blob, off + 56)[0] or bin_ns
        if recsz != REC:
            nbad += 1
            break
        off += HDR
        while off + REC <= n:
            if blob[off:off + 8] == MAGIC:
                break                     # next header, not a record
            t1, l1, q, ix = struct.unpack_from('<QIHH', blob, off)
            off += REC
            if t1 == 0:
                nbad += 1
                continue
            recs.append((t1, l1, q, ix))
    return recs, bin_ns, nbad


def pct(v, p):
    return v[min(len(v) - 1, int(p * (len(v) - 1)))]


def report(sym, pop, recs):
    if len(recs) < 500:
        return
    lat = [r[1] / 1e3 for r in recs]
    slat = sorted(lat)
    n = len(recs)
    print()
    print('=' * 74)
    print('%s %s   %d messages' % (sym, pop, n))
    print('  mean %.1fus  p50 %.1fus  p90 %.1fus  p99 %.1fus  p999 %.1fus  max %.1fus'
          % (sum(lat) / n, pct(slat, .5), pct(slat, .9), pct(slat, .99),
             pct(slat, .999), slat[-1]))

    # ---- EXACT conditional mean at each integer qlen ------------------
    # This row is the measurement. No selection, no averaging across
    # qlen values, no bin. d is the qlen THIS message saw.
    g = {}
    for t1, l1, q, ix in recs:
        g.setdefault(q, []).append(l1 / 1e3)
    print()
    print('  PER-MESSAGE, exact integer qlen')
    print('  %-6s %10s %8s %10s %10s %10s %10s'
          % ('qlen', 'msgs', 'share', 'mean', 'p50', 'p99', 'max'))
    base = None
    for q in sorted(g):
        v = sorted(g[q])
        if len(v) < 30:
            continue
        m = sum(v) / len(v)
        if base is None:
            base = m
        print('  %-6d %10d %7.2f%% %9.1fus %9.1fus %9.1fus %9.1fus%s'
              % (q, len(v), 100.0 * len(v) / n, m, pct(v, .5), pct(v, .99),
                 v[-1], '' if q == 0 else '  (%+.1f)' % (m - base)))
    small = sum(len(v) for q, v in g.items() if len(v) < 30)
    if small:
        print('  %d msgs in qlen values with n<30, not shown' % small)

    # ---- qlen 0 split by batch position -------------------------------
    # At qlen 0 nothing is queued AHEAD of the packet, but messages after
    # the first in a packet still wait on the ones ahead of them inside it.
    # That wait is in leg 1 and invisible to qlen. Splitting qlen 0 by idx
    # separates the two waits that were previously confounded.
    z = [r for r in recs if r[2] == 0]
    if len(z) > 500:
        gi = {}
        for t1, l1, q, ix in z:
            gi.setdefault(min(ix, 10), []).append(l1 / 1e3)
        print()
        print('  qlen == 0 ONLY, split by position inside the packet')
        print('  %-6s %10s %10s %10s %10s'
              % ('idx', 'msgs', 'mean', 'p50', 'p99'))
        for ix in sorted(gi):
            v = sorted(gi[ix])
            if len(v) < 30:
                continue
            print('  %-6s %10d %9.1fus %9.1fus %9.1fus'
                  % ('%d' % ix if ix < 10 else '10+', len(v),
                     sum(v) / len(v), pct(v, .5), pct(v, .99)))

    # ---- the clean cell: qlen 0 AND first in packet --------------------
    # Nothing ahead in MsgBuf, nothing ahead inside the packet. This is the
    # hot path with both waits removed, measured directly instead of
    # extrapolated to a zero-queue intercept.
    hot = sorted(r[1] / 1e3 for r in recs if r[2] == 0 and r[3] == 0)
    if len(hot) >= 200:
        print()
        print('  HOT PATH  qlen==0 AND idx==0: %d msgs (%.1f%%)'
              % (len(hot), 100.0 * len(hot) / n))
        print('    mean %.2fus  p50 %.2fus  p90 %.2fus  p99 %.2fus  max %.1fus'
              % (sum(hot) / len(hot), pct(hot, .5), pct(hot, .9),
                 pct(hot, .99), hot[-1]))

    # ---- per-message least squares -------------------------------------
    # Same form as the bin-level fit in kh_scat.py so the two are directly
    # comparable. If the slopes differ, the bin-level one was ecological.
    sx = sum(r[2] for r in recs) / float(n)
    sy = sum(lat) / n
    sxx = sum((r[2] - sx) ** 2 for r in recs)
    sxy = sum((r[2] - sx) * (l - sy) for r, l in zip(recs, lat))
    syy = sum((l - sy) ** 2 for l in lat)
    if sxx > 0 and syy > 0:
        b = sxy / sxx
        a = sy - b * sx
        r = sxy / (sxx * syy) ** 0.5
        print()
        print('  per-message fit: lat = %.2f + %.2f * qlen   r=%.3f r2=%.3f'
              % (a, b, r, r * r))
        print('  mean qlen %.4f, so the queue term contributes %.2fus of the'
              ' %.2fus mean' % (sx, b * sx, sy))


print('=' * 74)
print('PER-MESSAGE QLEN vs LEG-1 LATENCY -- exact pairs, no binning')
print('=' * 74)

tot = 0
for path in sorted(glob.glob(DIR + '/lat_*_*.msg')):
    b = os.path.basename(path)[4:-4]
    sym, _, pop = b.rpartition('_')
    recs, bin_ns, nbad = load(path)
    recs = [r for r in recs if T0 <= r[0] < T1]
    tot += len(recs)
    if nbad:
        print('%s %s: %d malformed records skipped' % (sym, pop, nbad))
    report(sym, pop, recs)

print()
print('%d messages total.' % tot)
print("""
Every row above is a conditional mean over MESSAGES at an exact integer
qlen. It is not a bin mean, not a group selected on a max, and nothing is
diluted by a denominator that moves with the effect. This is the first table
here that says what a message at qlen d actually cost.""")
