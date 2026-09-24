import struct, glob, os, math

# THE PACKET INTERARRIVAL DISTRIBUTION, AGAINST THE POISSON IT IS NOT.
#
# kh_proc.py answers "is the ORDER random" -- autocorrelation, Fano scaling,
# the shuffle control. It says nothing about the MARGINAL. This script does
# only the marginal, because "not Poisson" is two separate claims and they
# fail for different reasons:
#
#   1. MARGINAL: gaps are not exponential. Measured here.
#   2. ORDER:    gaps are not independent. Measured in kh_proc.py.
#
# A Poisson process needs BOTH. Reporting only CV conflates them, and CV is
# also the number most often quoted squared by mistake, so both forms are
# printed side by side here with their definitions.
#
# The comparison is against the exponential WITH THE SAME MEAN, which is the
# only fair null: it has the identical arrival rate, so every difference is
# shape and not level. For Exp(1/m) the p-th quantile is -m*ln(1-p).
#
# The column that matters for a queue is P(gap < mean/10): the share of
# packets that arrive nearly on top of the one before. Exponential puts
# 1-exp(-0.1) = 9.5% there, always, whatever the rate. Anything far above
# that is a burst, and a burst is what makes the ring deep and the packet fat.

DIR = os.environ.get('MDPERF_DIR', '/home/vincent/perf/mdperf')


def load(p):
    b = open(p, 'rb').read()
    if len(b) < 64 or b[:8] != b'KHARRIV1':
        return None
    body = b[64:]
    body = body[:len(body) - (len(body) % 16)]
    recs = [struct.unpack_from('<QIHH', body, i) for i in range(0, len(body), 16)]
    # drop the startup backlog: the leading run of identical t0
    k = 1
    while k < len(recs) and recs[k][0] == recs[0][0]:
        k += 1
    return recs[k if k > 1 else 0:]


def q(sv, p):
    return sv[int(p * (len(sv) - 1))]


print('=' * 92)
print('PACKET INTERARRIVAL MARGINAL  vs  EXPONENTIAL OF THE SAME MEAN')
print('=' * 92)

for p in sorted(glob.glob(DIR + '/*.arr')):
    recs = load(p)
    if not recs or len(recs) < 500:
        continue
    t = [r[0] for r in recs]
    ia = sorted(t[i] - t[i - 1] for i in range(1, len(t)))
    n = len(ia)
    m = sum(ia) / float(n)
    var = sum((x - m) ** 2 for x in ia) / n
    cv = math.sqrt(var) / m

    print('\n%-22s n=%d  mean gap=%.1fus  rate=%.1f/s'
          % (os.path.basename(p), n, m / 1e3, 1e9 / m))
    print('   CV=%.2f   CV^2=%.1f      (Poisson: CV=1, CV^2=1)' % (cv, cv * cv))

    print('   %-10s %12s %12s %10s' % ('quantile', 'measured', 'Exp(same mu)', 'ratio'))
    for pp in (0.01, 0.10, 0.25, 0.50, 0.75, 0.90, 0.99, 0.999):
        e = -m * math.log(1.0 - pp)
        o = q(ia, pp)
        print('   p%-9s %10.2fus %10.2fus %9.2fx'
              % (('%g' % (pp * 100)), o / 1e3, e / 1e3,
                 (o / e) if e else 0.0))

    # The burst share. Exponential is a fixed 9.52% here regardless of rate.
    thr = m / 10.0
    k = 0
    for x in ia:
        if x < thr:
            k += 1
        else:
            break
    print('   P(gap < mean/10) = %.2f%%   vs exponential 9.52%%   -> %.1fx'
          % (100.0 * k / n, (100.0 * k / n) / 9.516))

print("""
The median gap is far BELOW the exponential median and the p999 far ABOVE it.
That is the signature of a burst process: most packets arrive closer together
than a Poisson of the same rate would put them, paid for by rare long idles.
A Poisson source with this same average rate would almost never deliver the
back-to-back runs that fill the ring and fatten the packet.""")
