import sys, os, struct, time

# IS THE idx LADDER CURVED, OR IS IT COMPOSITION?
#
# kh_idx.py pools every message at a given idx regardless of which packet it
# came from. But idx 50 can only occur inside a packet of span >= 51, so the
# high-idx end of that ladder is built ENTIRELY from big packets and the low-idx
# end is dominated by small ones. If big packets cost more per message for a
# reason unrelated to position -- a different message mix, a book rebuild that
# touches many levels, more cache footprint -- the pooled ladder bends upward
# and it is composition, not a rising marginal cost.
#
# Same disease as the bin-level fit and the qlen table. Same cure: stop pooling
# over the other variable.
#
# Here: pick packets of one exact span, and inside that fixed span walk idx.
# Every point then comes from packets of identical size, so the composition is
# held constant and the only thing varying is position.
#
#   CONVEX at fixed span  -> marginal cost really does rise inside a packet
#                            (cache/TLB accumulating as the packet is worked)
#   LINEAR at fixed span  -> constant service cost; the pooled curvature was
#                            packet-size composition all along
#
# usage: kh_isp.py [HH:MM:SS] [HH:MM:SS]

DIR = os.environ.get('MDPERF_DIR', '/home/vincent/perf/mdperf')
HDR, REC, MAGIC = 64, 16, b'KHMSGV01'
MIN_PKTS = 40          # packets needed at a span before it is worth fitting
MIN_N = 25             # messages needed at an idx before the point is used


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


def packets(recs):
    """Split on an idx reset. idx is non-decreasing inside a packet, so a
    boundary is any record whose idx <= the previous one. Can only merge two
    packets, never split one."""
    out, cur, prev = [], [], -1
    for r in recs:
        if r[3] <= prev and cur:
            out.append(cur)
            cur = []
        cur.append(r)
        prev = r[3]
    if cur:
        out.append(cur)
    return out


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

    print('=' * 76)
    print('idx LADDER AT FIXED PACKET SPAN  --  curvature, or composition?')
    print('=' * 76)
    print('Every row below is built from packets of ONE exact span, so packet')
    print('size cannot vary along the ladder. Compare the half-slope ratio here')
    print('against the pooled one from kh_idx.py.')

    for fn in sorted(os.listdir(DIR)):
        if not fn.endswith('.msg'):
            continue
        recs = [r for r in load(os.path.join(DIR, fn)) if t_lo <= r[0] <= t_hi]
        if len(recs) < 5000:
            continue

        by_span = {}
        for p in packets(recs):
            by_span.setdefault(p[-1][3] + 1, []).append(p)

        print('\n%s' % fn[4:-4])
        print('  %5s %8s  %-34s %s' % ('span', 'pkts', 'fit', 'curvature'))

        # Largest spans first: those have the longest ladders and the most to say.
        cands = [s for s in by_span if s >= 6 and len(by_span[s]) >= MIN_PKTS]
        for span in sorted(cands, reverse=True)[:4]:
            pk = by_span[span]
            acc = {}
            for p in pk:
                for r in p:
                    acc.setdefault(r[3], []).append(r[1])
            pts = [(ix, sum(v) / len(v) / 1e3, len(v))
                   for ix, v in sorted(acc.items()) if len(v) >= MIN_N]
            if len(pts) < 6:
                continue
            a, b, r2 = fit(pts)
            h = len(pts) // 2
            s1 = ((pts[h][1] - pts[0][1]) /
                  float(pts[h][0] - pts[0][0])) if pts[h][0] != pts[0][0] else 0
            s2 = ((pts[-1][1] - pts[h][1]) /
                  float(pts[-1][0] - pts[h][0])) if pts[-1][0] != pts[h][0] else 0
            if s1 > 0 and s2 / s1 > 1.15:
                verd = 'CONVEX  %.2fx' % (s2 / s1)
            elif s1 > 0 and s2 / s1 < 0.87:
                verd = 'CONCAVE %.2fx' % (s2 / s1)
            else:
                verd = 'straight'
            print('  %5d %8d  lat = %5.2f + %.3f*idx  r2=%.3f  %s  (%.3f->%.3f)'
                  % (span, len(pk), a, b, r2, verd, s1, s2))

    print("""
If the fixed-span rows are straight while kh_idx.py's pooled ladder is convex,
the pooled curvature was packet-size composition: big packets cost more per
message for reasons other than position, and pooling smears that into the
shape. If the fixed-span rows are convex too, the marginal cost genuinely
rises as a packet is worked.""")


if __name__ == '__main__':
    main()
