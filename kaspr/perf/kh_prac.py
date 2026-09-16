import os, sys, glob, struct, time

# Backing arithmetic for the practitioner section. Three questions, all
# message-weighted, because a practitioner tunes for messages and not for
# packets and those two weightings disagree badly on the trade streams.
#
#   1. Where does the typical message actually sit inside its packet? The
#      packet-span table in the article is PACKET-weighted (p50 span 1 on
#      every book). Message-weighted is the larger number -- big packets
#      contain more messages, so they get more votes. Both are true; only
#      the second one answers "what does a message pay".
#
#   2. Split the observed total latency into floor vs (idx * slope), using
#      the PUBLISHED MEDIAN ladder. Not the mean-based kh_idx.py fit -- that
#      one is repudiated in the article's own Reproducing section.
#
#   3. Does slope rank-order p99? Claimed by eye earlier. Compute Spearman
#      rather than assert it, because pooling books and trades breaks the
#      clean within-family ordering.

DIR = os.environ.get('MDPERF_DIR', '/home/vincent/perf/mdperf')
HDR, REC, MAGIC = 64, 16, b'KHMSGV01'

# THE TIME CUT IS NOT OPTIONAL. The article's tables are a 53-minute window,
# 14:25-15:18. The .msg files have since been appended to by later probe
# cycles, so reading them whole gives a different and much worse sample --
# NQ book mean latency goes from 7.5us to 122us because the extra span
# includes a different regime. Anything published must use the same cut.


def _ns(hms):
    lt = time.localtime()
    h, m, s = [int(x) for x in hms.split(':')]
    return int(time.mktime((lt.tm_year, lt.tm_mon, lt.tm_mday,
                            h, m, s, 0, 0, -1))) * 1000000000


T0 = _ns(sys.argv[1] if len(sys.argv) > 1 else '14:25:00')
T1 = _ns(sys.argv[2] if len(sys.argv) > 2 else '15:18:00')

# published median-ladder intercept (us) and slope (us/msg), article lines 507-515
LADDER = {
    'ES book':  (6.98, 0.566), 'ES trade': (6.81, 0.714),
    'NQ book':  (7.23, 0.312), 'NQ trade': (7.14, 0.526),
    'ZN book':  (6.83, 0.966), 'ZN trade': (6.81, 0.965),
}
# published unconditional p99 (us), article lines 54-60
P99 = {'ES book': 18.5, 'NQ book': 13.6, 'ZN book': 57.0,
       'ES trade': 38.0, 'NQ trade': 31.2, 'ZN trade': 219.4}

FILES = [('ES book', 'ESZ6', 'book'), ('NQ book', 'NQZ6', 'book'),
         ('ZN book', 'ZNZ6', 'book'), ('ES trade', 'ESZ6', 'trade'),
         ('NQ trade', 'NQZ6', 'trade'), ('ZN trade', 'ZNZ6', 'trade')]


def q(sorted_v, p):
    if not sorted_v:
        return 0
    k = int(p * (len(sorted_v) - 1))
    return sorted_v[k]


def load(sym, pop):
    idx, lat = [], []
    for fn in sorted(glob.glob('%s/lat_%s_%s*.msg' % (DIR, sym, pop))):
        with open(fn, 'rb') as f:
            if f.read(8) != MAGIC:
                continue
            f.seek(HDR)
            b = f.read()
        for o in range(0, len(b) - REC + 1, REC):
            t1, l1, _, i = struct.unpack_from('<QIHH', b, o)
            if t1 < T0 or t1 >= T1:
                continue
            idx.append(i)
            lat.append(l1)
    return idx, lat


rows = []
print('%-9s %10s   %s' % ('', 'messages', 'MESSAGE-WEIGHTED idx (position in packet)'))
print('%-9s %10s %7s %6s %5s %5s %5s %5s' %
      ('stream', 'n', 'at 0', 'mean', 'p50', 'p90', 'p99', 'max'))
for name, sym, pop in FILES:
    idx, lat = load(sym, pop)
    if not idx:
        print('%-9s  NO DATA' % name)
        continue
    s = sorted(idx)
    n = len(s)
    z = sum(1 for v in s if v == 0)
    mean_idx = sum(s) / float(n)
    print('%-9s %10d %6.1f%% %6.2f %5d %5d %5d %5d' %
          (name, n, 100.0 * z / n, mean_idx, q(s, .5), q(s, .9), q(s, .99), s[-1]))
    rows.append((name, n, mean_idx, s, sorted(lat)))

print()
print('DOES THE LADDER PREDICT THE HEADLINE MEDIAN?')
print('pred = floor + p50_idx*slope.  This is a CHECK, not an identity -- the')
print('median of a sum is not the sum of medians. It holds only if idx is the')
print('dominant term, which is the claim being tested.')
print('%-9s %8s %7s %6s %9s %8s %6s %9s %8s %7s' %
      ('stream', 'floor', 'slope', 'i@p50', 'pred p50', 'obs p50',
       'i@p99', 'pred p99', 'obs p99', 'cover'))
for name, n, mean_idx, s, sl in rows:
    a, b = LADDER[name]
    i50, i99 = q(s, .5), q(s, .99)
    p50, p99 = a + b * i50, a + b * i99
    print('%-9s %7.2fus %5.0fns %6d %8.2fus %7.2fus %6d %8.1fus %7.1fus %6.0f%%' %
          (name, a, b * 1000, i50, p50, q(sl, .5) / 1000.0,
           i99, p99, q(sl, .99) / 1000.0, 100.0 * p99 / (q(sl, .99) / 1000.0)))

print()
print('WHERE THE TIME GOES.  total = observed sum of l1 over all messages.')
print('floor and idx*slope are the published ladder applied per message.')
print('This budget is SUM-weighted, so unlike the rest of the article it is')
print('exposed to the stalls. That is the point of the residual column: the')
print('ladder is fitted at qlen 0 on medians, so whatever it does not account')
print('for is queueing plus stalls, and that is the part worth reading.')
print('%-9s %11s %9s %9s %9s' %
      ('stream', 'total ms', 'floor %', 'slope %', 'residual'))
for name, n, mean_idx, s, sl in rows:
    a, b = LADDER[name]
    total_us = float(sum(sl)) / 1000.0
    fp = 100.0 * (a * n) / total_us
    sp = 100.0 * (b * mean_idx * n) / total_us
    print('%-9s %11.1f %8.1f%% %8.1f%% %8.1f%%' %
          (name, total_us / 1000, fp, sp, 100.0 - fp - sp))

print()
print('SPEARMAN, slope vs unconditional p99, n=6 streams')
names = [r[0] for r in rows] or list(LADDER)
sl = sorted(names, key=lambda k: LADDER[k][1])
p9 = sorted(names, key=lambda k: P99[k])
d2 = sum((sl.index(k) - p9.index(k)) ** 2 for k in names)
m = len(names)
print('  slope order: %s' % ' < '.join(sl))
print('  p99   order: %s' % ' < '.join(p9))
print('  sum d^2 = %d   rho = %.3f' % (d2, 1 - 6.0 * d2 / (m * (m * m - 1))))
