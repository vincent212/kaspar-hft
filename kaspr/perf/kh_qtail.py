import sys, os, struct, time

# THE TAIL OF THE DECONFOUNDED qlen CELLS.
#
# kh_qidx.py reports the median at each qlen with idx held at 0. That answers
# "what does the typical message pay". It deliberately says nothing about the
# tail, and the tail is the thing anyone actually cares about.
#
# This prints the order statistics for the same cells. The catch is n: a p99
# needs enough samples that the 99th percentile is not just the max. ES qlen 6
# has 36 messages at idx 0, and p99 of 36 points IS the largest point. Reporting
# that as a percentile is a lie with a decimal place on it.
#
# So every row carries its n and a verdict:
#   n >= 1000  p99 is meaningful (>=10 points above it)
#   n >=  200  p99 is shaky      (2-9 points above it)
#   n <   200  p99 is the max in disguise -- printed as p90/max instead
#
# usage: kh_qtail.py [HH:MM:SS] [HH:MM:SS]

DIR = os.environ.get('MDPERF_DIR', '/home/vincent/perf/mdperf')
HDR, REC, MAGIC = 64, 16, b'KHMSGV01'
MIN_N = 30


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
    """sv sorted. Nearest-rank, no interpolation."""
    if not sv:
        return 0.0
    i = int(p * (len(sv) - 1))
    return sv[i]


def main():
    t_lo = _ns(sys.argv[1]) if len(sys.argv) > 1 else 0
    t_hi = _ns(sys.argv[2]) if len(sys.argv) > 2 else (1 << 62)

    print('=' * 86)
    print('TAIL OF THE DECONFOUNDED qlen CELLS  --  idx held at 0')
    print('=' * 86)
    print('n is messages at that qlen AND idx 0. "above" is how many samples')
    print('sit above p99 -- if that is 0 or 1, p99 is just the max.')

    for fn in sorted(os.listdir(DIR)):
        if not fn.endswith('.msg'):
            continue
        recs = [r for r in load(os.path.join(DIR, fn)) if t_lo <= r[0] <= t_hi]
        if len(recs) < 500:
            continue

        by_i0 = {}
        for _t1, l1, q, ix in recs:
            if ix == 0:
                by_i0.setdefault(q, []).append(l1)

        print('\n%s' % fn[4:-4])
        print('  %4s %9s %9s %9s %9s %9s %9s %7s  %s'
              % ('qlen', 'n@idx0', 'p50', 'p90', 'p99', 'p999', 'max',
                 'above', 'verdict'))
        for q in sorted(by_i0):
            v = sorted(by_i0[q])
            n = len(v)
            if n < MIN_N:
                continue
            above = n - 1 - int(0.99 * (n - 1))
            if n >= 1000:
                verd = 'p99 ok'
            elif n >= 200:
                verd = 'p99 shaky'
            else:
                verd = 'p99 == max, ignore'
            print('  %4d %9d %8.1fus %8.1fus %8.1fus %8.1fus %8.1fus %7d  %s'
                  % (q, n, pct(v, 0.50) / 1e3, pct(v, 0.90) / 1e3,
                     pct(v, 0.99) / 1e3, pct(v, 0.999) / 1e3,
                     v[-1] / 1e3, above, verd))

    print("""
The median columns in the article are stable because a median needs only that
half the points be on each side. A p99 needs the tail to be sampled, and at
qlen >= 4 it is not: those cells hold tens of messages, not thousands. Any p99
printed there is one stall wearing a percentile's clothes.""")


if __name__ == '__main__':
    main()
