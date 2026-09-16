import sys, os, glob, struct, time

# IS THE idx LADDER LINEAR, AND WHAT IS ITS SLOPE?
#
# The per-message table showed latency rising monotonically with idx (position
# inside the packet) at qlen == 0. The shape matters more than the fact:
#
#   LINEAR in idx  => deterministic serialisation. Message k waits for k
#                     decodes at a fixed cost each. The slope IS the per-message
#                     service time S. This is a D/D/1 pipeline, not a queue: the
#                     backlog is known at arrival, nothing is stochastic.
#
#   CONVEX in idx  => something degrades as the packet is worked: cache
#                     pressure, book growth, allocator. Then the slope is not a
#                     service time and the last message costs more than the
#                     first for a reason beyond waiting.
#
# Textbook queueing gives E[W] ~ rho/(1-rho), which is CONVEX and explodes near
# saturation. That is the shape being looked for and it is NOT what the idx
# ladder should show, because idx is not a stochastic queue. If idx comes out
# linear, the tail here is not a utilisation effect at all -- it is arrival
# burstiness converting into large batches, with each batch position costing a
# fixed amount.
#
# Restricted to qlen == 0 so nothing is waiting on a packet ahead: the only
# thing a message at idx k waits for is the k messages in its own packet.
#
# usage: kh_idx.py [HH:MM:SS] [HH:MM:SS]

DIR = os.environ.get('MDPERF_DIR', '/home/vincent/perf/mdperf')
HDR, REC, MAGIC = 64, 16, b'KHMSGV01'


def _ns(hms):
    lt = time.localtime()
    h, m, s = [int(x) for x in hms.split(':')]
    return int(time.mktime((lt.tm_year, lt.tm_mon, lt.tm_mday,
                            h, m, s, 0, 0, -1))) * 1000000000


T0 = _ns(sys.argv[1]) if len(sys.argv) > 1 else 0
T1 = _ns(sys.argv[2]) if len(sys.argv) > 2 else (1 << 62)


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


def fit(pts):
    """pts = [(x, y, n)] weighted by n -> (a, b, r2)."""
    sw = sum(p[2] for p in pts)
    mx = sum(p[0] * p[2] for p in pts) / sw
    my = sum(p[1] * p[2] for p in pts) / sw
    sxx = sum(p[2] * (p[0] - mx) ** 2 for p in pts)
    sxy = sum(p[2] * (p[0] - mx) * (p[1] - my) for p in pts)
    syy = sum(p[2] * (p[1] - my) ** 2 for p in pts)
    if sxx <= 0 or syy <= 0:
        return None
    b = sxy / sxx
    return (my - b * mx, b, (sxy / (sxx * syy) ** 0.5) ** 2)


print('=' * 76)
print('SHAPE OF THE idx LADDER  --  qlen == 0 only, so nothing waits on a')
print('packet ahead. The only wait left is the messages in your own packet.')
print('=' * 76)

for path in sorted(glob.glob(DIR + '/lat_*_*.msg')):
    b = os.path.basename(path)[4:-4]
    sym, _, pop = b.rpartition('_')
    recs = [r for r in load(path) if T0 <= r[0] < T1 and r[2] == 0]
    if len(recs) < 2000:
        continue

    g = {}
    for t1, l1, q, ix in recs:
        g.setdefault(ix, []).append(l1 / 1e3)

    # Only idx values with enough messages to have a stable mean, and only
    # the contiguous run from the smallest -- a gap would make "linear in
    # idx" meaningless.
    ks = sorted(k for k in g if len(g[k]) >= 40)
    if len(ks) < 5:
        continue
    run = [ks[0]]
    for k in ks[1:]:
        if k == run[-1] + 1:
            run.append(k)
        else:
            break
    if len(run) < 5:
        continue

    pts = [(k, sum(g[k]) / len(g[k]), len(g[k])) for k in run]
    f = fit(pts)
    if not f:
        continue
    a, s, r2 = f

    print()
    print('%s %s   idx %d..%d, %d msgs on the fitted run'
          % (sym, pop, run[0], run[-1], sum(p[2] for p in pts)))
    print('  fit  lat = %.2f + %.3f * idx   r2=%.3f' % (a, s, r2))
    print('  slope %.0f ns per message ahead of you in the packet' % (s * 1000))

    # Residual from the straight line, in the units of the thing itself.
    # A monotone residual pattern (all negative in the middle, positive at
    # the ends) is curvature; scattered signs are noise.
    print('  %-5s %8s %9s %9s %9s' % ('idx', 'msgs', 'mean', 'linear', 'resid'))
    resid = []
    for k, y, n in pts:
        pred = a + s * k
        resid.append(y - pred)
        print('  %-5d %8d %8.2fus %8.2fus %+8.2fus' % (k, n, y, pred, y - pred))

    # Curvature test without fitting a quadratic: compare the slope over the
    # first half of the run to the slope over the second half. Equal slopes
    # = straight line. Second > first = convex (degrading). Second < first =
    # concave (warming up).
    h = len(pts) // 2
    s1 = (pts[h][1] - pts[0][1]) / float(pts[h][0] - pts[0][0])
    s2 = (pts[-1][1] - pts[h][1]) / float(pts[-1][0] - pts[h][0])
    shape = ('straight' if abs(s2 - s1) < 0.25 * abs(s1)
             else ('CONVEX, back half %.2fx steeper' % (s2 / s1) if s2 > s1
                   else 'CONCAVE, back half %.2fx shallower' % (s2 / s1)))
    print('  first-half slope %.3f, second-half %.3f  ->  %s' % (s1, s2, shape))
    print('  max |resid| %.2fus against a %.2fus rise across the run'
          % (max(abs(x) for x in resid), pts[-1][1] - pts[0][1]))

print()
print("""A straight line here means the wait is DETERMINISTIC SERIALISATION, not
queueing. Message k waits for k decodes at a fixed cost, and the slope is that
cost. Nothing about it is rho/(1-rho): the backlog is known the moment the
packet lands, and there is no utilisation term to explode.

Which is why the tail does not come from saturation. It comes from the arrival
process delivering occasional large packets -- and a large packet costs its
last message slope*idx, linearly, every time.""")
