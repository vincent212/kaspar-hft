import struct, glob, os, math

# WHAT IS THE ARRIVAL PROCESS, ACTUALLY?
#
# CV > 1 and Fano > 1 only say "not Poisson". They do not say what it is.
# Several very different processes produce CV > 1, and they imply different
# things about the latency tail, so the distinction is the whole point:
#
#   (a) RENEWAL with a heavy-tailed gap. Gaps are iid, just not exponential.
#       Overdispersed, but NO memory: a burst does not make another burst
#       more likely. Fano rises to a CONSTANT (= CV^2) and stops.
#
#   (b) SELF-EXCITING (Hawkes). Each arrival raises the intensity for the
#       ones after it. Gaps are POSITIVELY CORRELATED. Fano keeps RISING
#       with the window, without bound in the critical limit.
#
#   (c) NON-STATIONARY rate. A slow deterministic intensity (open, close,
#       announcements) with Poisson arrivals underneath. Also gives rising
#       Fano, so it must be separated from (b) or (b) is unfalsifiable.
#
# Three tests, all off the raw stamps:
#
#   1. LAG-1..k AUTOCORRELATION OF GAPS. Renewal => 0 at every lag. This is
#      the single test that separates (a) from (b)/(c).
#   2. FANO SCALING EXPONENT. Fano(T) ~ T^(2H-1). H = 0.5 is Poisson or any
#      renewal at large T; H > 0.5 is long-range dependence.
#   3. SHUFFLE CONTROL. Permute the gaps and recompute. Shuffling destroys
#      ORDER but preserves the gap DISTRIBUTION exactly. So whatever Fano
#      survives the shuffle is due to the marginal distribution alone, and
#      whatever COLLAPSES was due to ordering, i.e. real clustering. This
#      is what stops (a) being mistaken for (b).
#
# Test 3 does NOT separate (b) from (c): a deterministic rate change is also
# destroyed by shuffling. Separating those needs the intensity, which is a
# longer sample than we have. Reported as an explicit caveat, not hidden.

DIR = '/home/vincent/perf/mdperf'


def load(p):
    b = open(p, 'rb').read()
    if len(b) < 64:
        return None
    assert b[:8] == b'KHARRIV1'
    recsz, ver = struct.unpack_from('<II', b, 8)
    tag = b[16:48].split(b'\0')[0].decode()
    body = b[64:]
    body = body[:len(body) - (len(body) % 16)]
    recs = [struct.unpack_from('<QIHH', body, i) for i in range(0, len(body), 16)]
    # drop the startup backlog: the leading run of identical t0
    k = 1
    while k < len(recs) and recs[k][0] == recs[0][0]:
        k += 1
    return tag, recs[k if k > 1 else 0:]


def acf(x, lags):
    n = len(x)
    m = sum(x) / n
    d = [v - m for v in x]
    v0 = sum(y * y for y in d)
    if v0 <= 0:
        return [0.0] * len(lags)
    return [sum(d[i] * d[i - L] for i in range(L, n)) / v0 for L in lags]


def fano(t, w_ms):
    w = w_ms * 1000000
    base = t[0]
    nb = int((t[-1] - base) // w) + 1
    if nb < 20:
        return None
    c = [0] * nb
    for x in t:
        c[int((x - base) // w)] += 1
    mu = sum(c) / nb
    if mu <= 0:
        return None
    v = sum((y - mu) ** 2 for y in c) / nb
    return v / mu


def hurst(scales):
    """Fano(T) ~ T^(2H-1); least squares on log-log gives 2H-1."""
    pts = [(math.log(a), math.log(b)) for a, b in scales if b and b > 0]
    if len(pts) < 3:
        return None
    n = len(pts)
    mx = sum(p[0] for p in pts) / n
    my = sum(p[1] for p in pts) / n
    sxx = sum((p[0] - mx) ** 2 for p in pts)
    if sxx <= 0:
        return None
    sxy = sum((p[0] - mx) * (p[1] - my) for p in pts)
    slope = sxy / sxx
    return slope, (slope + 1.0) / 2.0


print('=' * 76)
print('ARRIVAL PROCESS IDENTIFICATION -- raw .arr stamps, startup burst removed')
print('=' * 76)

WINS = (1, 5, 10, 50, 100, 500, 1000, 5000)
LAGS = (1, 2, 3, 5, 10, 20, 50)

for p in sorted(glob.glob(DIR + '/*.arr')):
    got = load(p)
    if not got:
        continue
    tag, recs = got
    if len(recs) < 500:
        continue
    t = [r[0] for r in recs]
    ia = [t[i] - t[i - 1] for i in range(1, len(t))]

    m = sum(ia) / len(ia)
    var = sum((x - m) ** 2 for x in ia) / len(ia)
    cv = (var ** 0.5) / m if m else 0

    print()
    print('%-22s n=%d  rate=%.1f/s  CV=%.2f'
          % (os.path.basename(p), len(recs),
             len(recs) / ((t[-1] - t[0]) / 1e9), cv))

    # --- TEST 1: gap autocorrelation. Renewal => all zero. ---
    a = acf(ia, LAGS)
    # 95% band for white noise is +-1.96/sqrt(n)
    band = 1.96 / math.sqrt(len(ia))
    sig = sum(1 for v in a if abs(v) > band)
    print('   gap ACF   ' + '  '.join('L%d=%+.3f' % (L, v)
                                      for L, v in zip(LAGS, a)))
    print('   %d of %d lags outside the +-%.3f white-noise band -> %s'
          % (sig, len(LAGS), band,
             'NOT renewal, gaps have memory' if sig else 'consistent with renewal'))

    # --- TEST 2 + 3: Fano scaling, real vs order-shuffled ---
    real = [(w, fano(t, w)) for w in WINS]
    real = [(w, f) for w, f in real if f]

    # Shuffle the GAPS deterministically and rebuild a stamp series. Same
    # marginal distribution, order destroyed. Seeded so the run repeats.
    sh = list(ia)
    st = 12345
    for i in range(len(sh) - 1, 0, -1):
        st = (1103515245 * st + 12345) & 0x7FFFFFFF
        j = st % (i + 1)
        sh[i], sh[j] = sh[j], sh[i]
    ts = [t[0]]
    for g in sh:
        ts.append(ts[-1] + g)
    shuf = [(w, fano(ts, w)) for w in WINS]
    shuf = [(w, f) for w, f in shuf if f]

    print('   %-9s %s' % ('window', '  '.join('%7d' % w for w, _ in real)))
    print('   %-9s %s' % ('Fano', '  '.join('%7.1f' % f for _, f in real)))
    sd = dict(shuf)
    print('   %-9s %s' % ('shuffled', '  '.join('%7.1f' % sd.get(w, 0)
                                                for w, _ in real)))

    hr = hurst(real)
    hs = hurst(shuf)
    if hr:
        print('   Fano ~ T^%.3f  ->  H=%.3f   (H=0.5 Poisson/renewal)'
              % (hr[0], hr[1]))
    if hs:
        print('   shuffled   T^%.3f  ->  H=%.3f   (this is the null)'
              % (hs[0], hs[1]))

    # Branching ratio, IF the Hawkes reading is taken. For a stationary
    # Hawkes the asymptotic Fano is 1/(1-n)^2, so n = 1 - 1/sqrt(Fano).
    # Quoted at the LARGEST window only, and it is an upper bound on n
    # because any non-stationary drift inflates that Fano too.
    big = real[-1][1]
    if big and big > 1:
        print('   if Hawkes: branching ratio n <= 1 - 1/sqrt(%.1f) = %.3f'
              % (big, 1 - 1 / math.sqrt(big)))

print()
print('=' * 76)
print("""READING IT

Gap ACF at zero and Fano equal to its shuffle  => renewal. Bursts are luck,
not memory, and the tail is the gap distribution alone.

Gap ACF positive and Fano ABOVE its shuffle    => clustering. Arrivals bunch
in time beyond what their own distribution explains. This is the case that
makes a queue build, because work arrives faster than the mean over exactly
the stretch where it matters.

The shuffle is the control: identical marginal, order destroyed. It is NOT a
control for a slowly drifting rate, which shuffling also destroys. Both read
as clustering here and this sample cannot separate them.""")
