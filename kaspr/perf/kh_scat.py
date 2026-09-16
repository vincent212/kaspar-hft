import sys, os, glob, time

# PER-BIN MEAN QLEN vs PER-BIN MEAN LATENCY.
#
# This replaces the qlen_max grouping, which was not interpretable. The
# problem there: bins were selected on the DEEPEST message in the bin but
# averaged over ALL of them, and msgs/bin itself rose with depth, so the
# effect was divided by a denominator that grew with the effect.
#
# Here both axes are means over THE SAME MESSAGES:
#
#     x = qlen_sum / l1_n      mean queue depth seen by a message in the bin
#     y = l1_sum_ns / l1_n     mean leg-1 latency of a message in the bin
#
# Same numerator set, same denominator. Nothing is selected on, nothing is
# held fixed, and the dilution cancels because it is identical on both axes.
#
# WHAT THIS STILL IS NOT. It is a bin-level (ecological) relation, not a
# per-message one. A slope here says "bins where messages saw deeper queues
# had higher mean latency", NOT "a message at depth d costs a+b*d". Those
# differ whenever the within-bin distributions are skewed, which they are.
# The per-message record is still the only thing that closes that gap.
#
# The weighted fit is weighted by l1_n because a bin with 500 messages is a
# better-measured point than a bin with 3, and the unweighted fit is dominated
# by the quiet bins where the cold-cache effect lives.
#
# usage: kh_scat.py [HH:MM:SS] [HH:MM:SS]      optional window

exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       'kh_report.py')).read().split("store, drops = {}, {}")[0])


def _ns(hms):
    lt = time.localtime()
    h, m, s = [int(x) for x in hms.split(':')]
    return int(time.mktime((lt.tm_year, lt.tm_mon, lt.tm_mday,
                            h, m, s, 0, 0, -1))) * 1000000000


T0 = _ns(sys.argv[1]) if len(sys.argv) > 1 else 0
T1 = _ns(sys.argv[2]) if len(sys.argv) > 2 else (1 << 62)

W, H = 64, 18
RAMP = ' .:-=+*#%@'


def scatter(sym, pop, pts):
    """pts = [(qmean, lat_us, n), ...] one entry per populated bin."""
    if len(pts) < 30:
        return
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]

    # Clip the y axis at p99 so one 12ms stall does not compress everything
    # else into a single row. The clipped points are counted and reported, not
    # silently dropped.
    sy = sorted(ys)
    ylo = sy[0]
    yhi = sy[int(0.99 * (len(sy) - 1))]
    sx = sorted(xs)
    xlo, xhi = sx[0], sx[int(0.995 * (len(sx) - 1))]
    if yhi <= ylo or xhi <= xlo:
        return
    nclip = sum(1 for p in pts if p[1] > yhi or p[0] > xhi)

    grid = [[0] * W for _ in range(H)]
    for x, y, n in pts:
        if y > yhi or x > xhi:
            continue
        c = int((x - xlo) / (xhi - xlo) * (W - 1))
        r = int((y - ylo) / (yhi - ylo) * (H - 1))
        grid[H - 1 - r][c] += 1

    mx = max(max(row) for row in grid) or 1
    print()
    print('%s %s   %d bins, %d msgs' % (sym, pop, len(pts),
                                        sum(p[2] for p in pts)))
    for r in range(H):
        yv = ylo + (yhi - ylo) * (H - 1 - r) / (H - 1.0)
        line = ''.join(
            RAMP[min(len(RAMP) - 1,
                     int((grid[r][c] / float(mx)) ** 0.35 * (len(RAMP) - 1)))]
            if grid[r][c] else ' '
            for c in range(W))
        print('%8.1f |%s' % (yv, line))
    print('%8s +%s' % ('', '-' * W))
    print('%8s  %-*s%s' % ('', W - 8, '%.2f' % xlo, '%.2f' % xhi))
    print('%8s  mean qlen in bin ->        (y = mean leg1 us)' % '')
    if nclip:
        print('         %d of %d bins off-scale (clipped at y=%.1fus, x=%.2f)'
              % (nclip, len(pts), yhi, xhi))

    # Weighted least squares, weight = messages in the bin.
    sw = sum(p[2] for p in pts)
    mxq = sum(p[0] * p[2] for p in pts) / sw
    myq = sum(p[1] * p[2] for p in pts) / sw
    sxx = sum(p[2] * (p[0] - mxq) ** 2 for p in pts)
    sxy = sum(p[2] * (p[0] - mxq) * (p[1] - myq) for p in pts)
    syy = sum(p[2] * (p[1] - myq) ** 2 for p in pts)
    if sxx > 0 and syy > 0:
        b = sxy / sxx
        a = myq - b * mxq
        r = sxy / (sxx * syy) ** 0.5
        print('  weighted fit: lat = %.2f + %.2f * qlen   r=%.3f  r2=%.3f'
              % (a, b, r, r * r))
        print('  intercept %.2fus is the fitted zero-queue cost;'
              ' slope is us per unit of mean depth' % a)

    # Dose-response on the SAME axis, so the table and the plot agree.
    print('  %-14s %7s %9s %10s %10s' %
          ('mean qlen', 'bins', 'msgs', 'mean lat', 'p95 of bin'))
    for lo, hi in ((0, .01), (.01, .05), (.05, .1), (.1, .25),
                   (.25, .5), (.5, 1.), (1., 1e9)):
        sel = [p for p in pts if lo <= p[0] < hi]
        if not sel:
            continue
        n = sum(p[2] for p in sel)
        if n < 200:
            continue
        v = sorted(p[1] for p in sel)
        print('  %-14s %7d %9d %9.1fus %9.1fus'
              % ('%.2f-%.2f' % (lo, hi) if hi < 1e9 else '%.2f+' % lo,
                 len(sel), n,
                 sum(p[1] * p[2] for p in sel) / n,
                 v[int(0.95 * (len(v) - 1))]))


print('=' * 78)
print('MEAN QLEN vs MEAN LEG-1, one point per 100ms bin, matched denominators')
print('=' * 78)

for path in sorted(glob.glob(DIR + '/lat_*.csv')):
    sym = os.path.basename(path)[4:-4]
    hdr, rows = load(path)
    if not rows:
        continue
    rows, _ = drop_startup(hdr, rows, startup_pkts(sym))
    rows = [r for r in rows if T0 <= r[0] < T1]
    for pop in ('book', 'trade'):
        i_n = hdr.index(pop + '_l1_n')
        i_s = hdr.index(pop + '_l1_sum_ns')
        i_q = hdr.index(pop + '_qlen_sum')
        pts = [(r[i_q] / float(r[i_n]), r[i_s] / float(r[i_n]) / 1e3, r[i_n])
               for r in rows if r[i_n]]
        scatter(sym, pop, pts)

print()
print("""Both axes are averages over the same messages, so this does not have the
qlen_max defect. It is still BIN-LEVEL: the slope is us per unit of mean
depth across bins, not the cost to a single message at that depth.""")
