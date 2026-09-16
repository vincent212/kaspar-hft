import sys, os, struct, time

# THE idx LADDER ON MEDIANS, NOT MEANS.
#
# kh_idx.py fits mean(l1 | idx == k). On a short window that is fine. On a long
# one it is not: the mean at a given idx is dominated by a handful of multi-
# millisecond stalls, and those stalls land wherever they land -- mostly at low
# idx, because low idx is where most messages are. The result is a fit whose
# intercept is 319us and whose slope is NEGATIVE, describing nothing.
#
# The serialisation claim is a claim about the TYPICAL message: if you are k-th
# in the packet you wait for k decodes. That is a statement about the centre of
# the distribution, so it should be tested on the centre. Median is the right
# estimator; the stalls are a separate phenomenon with their own section.
#
# Prints median, p25, p75 so the spread is visible and the reader can see the
# median is not hiding a bimodal mess.
#
# usage: kh_imed.py [HH:MM:SS] [HH:MM:SS]

DIR = os.environ.get('MDPERF_DIR', '/home/vincent/perf/mdperf')
HDR, REC, MAGIC = 64, 16, b'KHMSGV01'
MIN_N = 40


def _ns(hms):
    lt = time.localtime()
    h, m, s = [int(x) for x in hms.split(':')]
    return int(time.mktime((lt.tm_year, lt.tm_mon, lt.tm_mday,
                            h, m, s, 0, 0, -1))) * 1000000000


def load(path):
    recs = []
    with open(path, 'rb') as f:
        blob = f.read()
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
                recs.append((t1, l1, q, ix))
    return recs


def pct(sv, p):
    """sv already sorted."""
    if not sv:
        return 0.0
    i = int(p * (len(sv) - 1))
    return sv[i]


def fit(pts):
    """pts = [(x, y, w)] weighted least squares -> (a, b, r2)"""
    sw = sum(w for _x, _y, w in pts)
    mx = sum(x * w for x, _y, w in pts) / sw
    my = sum(y * w for _x, y, w in pts) / sw
    sxy = sum(w * (x - mx) * (y - my) for x, y, w in pts)
    sxx = sum(w * (x - mx) ** 2 for x, _y, w in pts)
    if sxx == 0:
        return my, 0.0, 0.0
    b = sxy / sxx
    a = my - b * mx
    sst = sum(w * (y - my) ** 2 for _x, y, w in pts)
    sse = sum(w * (y - (a + b * x)) ** 2 for x, y, w in pts)
    return a, b, (1 - sse / sst) if sst else 0.0


def main():
    t_lo = _ns(sys.argv[1]) if len(sys.argv) > 1 else 0
    t_hi = _ns(sys.argv[2]) if len(sys.argv) > 2 else (1 << 62)

    print('=' * 74)
    print('idx LADDER ON MEDIANS  --  qlen == 0, so the packet is the only wait')
    print('=' * 74)
    print('kh_idx.py fits means and the means are eaten by stalls on a long')
    print('window. This fits the median at each idx: the typical message.')

    for fn in sorted(os.listdir(DIR)):
        if not fn.endswith('.msg'):
            continue
        recs = [r for r in load(os.path.join(DIR, fn))
                if t_lo <= r[0] <= t_hi and r[2] == 0]
        if len(recs) < 5000:
            continue

        acc = {}
        for _t1, l1, _q, ix in recs:
            acc.setdefault(ix, []).append(l1)

        rows = []
        for ix in sorted(acc):
            v = sorted(acc[ix])
            if len(v) < MIN_N:
                continue
            rows.append((ix, len(v), pct(v, 0.25) / 1e3,
                         pct(v, 0.50) / 1e3, pct(v, 0.75) / 1e3))
        if len(rows) < 6:
            continue

        pts = [(ix, med, n) for ix, n, _q1, med, _q3 in rows]
        a, b, r2 = fit(pts)
        h = len(pts) // 2
        s1 = (pts[h][1] - pts[0][1]) / float(pts[h][0] - pts[0][0] or 1)
        s2 = (pts[-1][1] - pts[h][1]) / float(pts[-1][0] - pts[h][0] or 1)
        if s1 > 0 and s2 / s1 > 1.15:
            verd = 'CONVEX  %.2fx' % (s2 / s1)
        elif s1 > 0 and s2 / s1 < 0.87:
            verd = 'CONCAVE %.2fx' % (s2 / s1)
        else:
            verd = 'straight'

        print('\n%s   idx %d..%d, %d msgs at qlen 0'
              % (fn[4:-4], rows[0][0], rows[-1][0], len(recs)))
        print('  fit  med = %.2f + %.3f * idx   r2=%.3f   %s  (%.3f->%.3f)'
              % (a, b, r2, verd, s1, s2))
        print('  %4s %9s %9s %9s %9s %9s'
              % ('idx', 'msgs', 'p25', 'median', 'p75', 'resid'))
        step = max(1, len(rows) // 12)
        for i in range(0, len(rows), step):
            ix, n, q1, med, q3 = rows[i]
            print('  %4d %9d %8.2fus %8.2fus %8.2fus %+8.2fus'
                  % (ix, n, q1, med, q3, med - (a + b * ix)))
        if (len(rows) - 1) % step:
            ix, n, q1, med, q3 = rows[-1]
            print('  %4d %9d %8.2fus %8.2fus %8.2fus %+8.2fus'
                  % (ix, n, q1, med, q3, med - (a + b * ix)))

    print("""
If the median ladder is straight where the mean ladder was not, the mean fit
was measuring stalls, not serialisation. The two are separate effects and the
median is the one that answers "what does position in the packet cost".""")


if __name__ == '__main__':
    main()
