import sys, os, glob, struct

# Report over the 40-column 10 Hz CSV. Dispatches on header NAMES, never
# position.
#
# Reads only the rows written by the CURRENT process -- the file is appended
# across restarts and a restart re-emits the header, so the last header line
# marks the boundary.
#
# No stratification, no correlations, no controls. Counts, sums and extremes,
# each printed next to its own denominator.

DIR = '/home/vincent/perf/mdperf'


def load(path):
    hdr, rows = None, []
    for ln in open(path):
        ln = ln.rstrip('\n')
        if not ln or ln[0] == '#':
            continue
        if ln.startswith('bin_key_ns'):
            hdr = ln.split(',')
            rows = []
            continue
        if hdr is None:
            continue
        p = ln.split(',')
        if len(p) != len(hdr):
            continue             # torn final line, writer mid-flush
        rows.append([int(x) for x in p])
    return hdr, rows


def col(hdr, rows, name):
    i = hdr.index(name)
    return [r[i] for r in rows]


def startup_pkts(sym):
    """Size of the startup backlog IN PACKETS, per population, from the .arr.

    NOT a control and not a filter on the result. At start the socket buffer
    already holds a backlog; it is drained in ONE read, so every record in it
    carries the SAME hndl_tim_epoch. That is the recorder catching up, not the
    feed. Left in, it was 95.4% of NQ's total measured latency and put the
    book mean at 188us against a true 7.3us.

    Returned as a COUNT, not a timestamp. The .arr stamps ARRIVAL (t0); the
    CSV bins by PROCESSING time. NQ's backlog arrived 4ms BEFORE the first CSV
    bin opened but took ~15ms to drain, so cutting the CSV at an arrival
    timestamp removed nothing. Counting packets works because both sides count
    the same packets in the same order.
    """
    out = {}
    for pop in ('book', 'trade'):
        try:
            b = open(DIR + '/lat_%s_%s.arr' % (sym, pop), 'rb').read()
        except IOError:
            out[pop] = 0
            continue
        if len(b) < 64 + 32 or b[:8] != b'KHARRIV1':
            out[pop] = 0
            continue
        body = b[64:]
        n = len(body) // 16
        first, = struct.unpack_from('<Q', body, 0)
        k = 1
        while k < n:
            t, = struct.unpack_from('<Q', body, k * 16)
            if t != first:
                break
            k += 1
        out[pop] = k if k > 1 else 0
    return out


def drop_startup(hdr, rows, burst):
    if not burst or not any(burst.values()):
        return rows, 0
    seen = {'book': 0, 'trade': 0}
    i = {p: hdr.index(p + '_pkt_closed_n') for p in ('book', 'trade')}
    k = 0
    while k < len(rows):
        if all(seen[p] >= burst[p] for p in ('book', 'trade')):
            break
        for p in ('book', 'trade'):
            seen[p] += rows[k][i[p]]
        k += 1
    return rows[k:], k


def ia_moments(hdr, rows, pop):
    """Pooled interarrival mean/sd. sumsq is MICROSECONDS SQUARED now.

    The wrap check is kept even though the unit change should have ended it:
    Cauchy-Schwarz says sumsq*n >= sum^2 always, so a violation is free
    evidence that the fix did not hold. `wrapped` is expected to read 0. If it
    does not, the number beside it is not trustworthy and the column needs
    another look.
    """
    n = col(hdr, rows, pop + '_ia_n')
    s = col(hdr, rows, pop + '_ia_sum')          # ns
    q = col(hdr, rows, pop + '_ia_sumsq_us2')    # us^2
    N = S = 0
    Q = 0
    wrapped = 0
    for a, b, c in zip(n, s, q):
        if not a:
            continue
        b_us = b / 1000.0
        # Cauchy-Schwarz WITH THE TRUNCATION SLACK CARRIED. The probe banks
        # floor(gap/1000)^2, so each term is up to 1us^2 short of (gap/1000)^2
        # and a naive n*sumsq >= sum^2 fails on rounding alone -- at a == 1 it
        # fails for EVERY gap that is not an exact multiple of 1000ns. That
        # false positive is what made this column read 170-1309 instead of 0.
        #
        # floor(g/1000) > g/1000 - 1 for each of the `a` gaps, so
        # sum(floor) > b_us - a, and the true bound is (b_us - a)^2. Below
        # that is a REAL wrap.
        lb = b_us - a
        if lb > 0 and c * a < lb * lb:
            wrapped += 1
            continue
        N += a; S += b; Q += c
    if N < 2:
        return None
    m_us = S / N / 1000.0
    var = Q / N - m_us * m_us
    return N, m_us * 1000.0, (var ** 0.5 * 1000.0 if var > 0 else 0.0), wrapped


def fmt_ns(v):
    if v >= 1e9:
        return '%.2fs' % (v / 1e9)
    if v >= 1e6:
        return '%.2fms' % (v / 1e6)
    if v >= 1e3:
        return '%.1fus' % (v / 1e3)
    return '%.0fns' % v


store, drops = {}, {}
for path in sorted(glob.glob(DIR + '/lat_*.csv')):
    sym = os.path.basename(path)[4:-4]
    hdr, rows = load(path)
    if not rows:
        continue
    rows, ndrop = drop_startup(hdr, rows, startup_pkts(sym))
    store[sym] = (hdr, rows)
    drops[sym] = ndrop

print('=' * 78)
print('LEG 1  --  socket read (t0) -> book published (publish_ts)')
print('=' * 78)
print('%-6s %-6s %9s %10s %10s %10s %10s' %
      ('sym', 'pop', 'n', 'mean', 'min', 'max', 'bins (-burst)'))
for sym, (hdr, rows) in store.items():
    for pop in ('book', 'trade'):
        n = col(hdr, rows, pop + '_l1_n')
        s = col(hdr, rows, pop + '_l1_sum_ns')
        mn = [x for x in col(hdr, rows, pop + '_l1_min_ns') if x]
        mx = col(hdr, rows, pop + '_l1_max_ns')
        N = sum(n)
        if not N:
            continue
        nz = sum(1 for x in n if x)
        print('%-6s %-6s %9d %10s %10s %10s %6d/%d  (-%d)' %
              (sym, pop, N, fmt_ns(sum(s) / N),
               fmt_ns(min(mn) if mn else 0), fmt_ns(max(mx)),
               nz, len(rows), drops[sym]))
print()
print('mean = sum/n, denominator shown. min/max are over every payload the')
print('rejects admitted -- no subsampling. No p50: a median is not additive')
print('across bins and cannot be recovered from these columns.')

print()
print('=' * 78)
print('TAIL  --  distribution of PER-BIN max, the worst 100ms window')
print('=' * 78)
print('%-6s %-6s %9s %9s %9s %9s %9s' %
      ('sym', 'pop', 'p50', 'p90', 'p99', 'p999', 'max'))
for sym, (hdr, rows) in store.items():
    for pop in ('book', 'trade'):
        n = col(hdr, rows, pop + '_l1_n')
        mx = col(hdr, rows, pop + '_l1_max_ns')
        v = sorted(m for c, m in zip(n, mx) if c)
        if len(v) < 20:
            continue
        q = [v[int(p * (len(v) - 1))] for p in (.5, .9, .99, .999)]
        print('%-6s %-6s %9s %9s %9s %9s %9s' %
              (sym, pop, fmt_ns(q[0]), fmt_ns(q[1]), fmt_ns(q[2]),
               fmt_ns(q[3]), fmt_ns(v[-1])))
print()
print('Each point is already an extreme over its bin, so it sits above the')
print('per-message quantiles. It answers "how bad does a 100ms window get".')

print()
print('=' * 78)
print('QUEUE  --  ingress_qlen, MsgBuf depth at packet enqueue')
print('=' * 78)
print('%-6s %-6s %10s %6s   %s' % ('sym', 'pop', 'mean', 'max', 'note'))
for sym, (hdr, rows) in store.items():
    for pop in ('book', 'trade'):
        n = col(hdr, rows, pop + '_l1_n')
        s = col(hdr, rows, pop + '_qlen_sum')
        mx = col(hdr, rows, pop + '_qlen_max')
        N = sum(n)
        if not N:
            continue
        deep = sum(1 for x in mx if x > 10)
        print('%-6s %-6s %10.2f %6d   bins with max>10: %d' %
              (sym, pop, sum(s) / N, max(mx), deep))
print()
print('Denominator is *_l1_n: qlen is sampled once per admitted payload.')

print()
print('=' * 78)
print('BATCH  --  messages published per packet')
print('=' * 78)
print('%-6s %-6s %9s %8s %8s %8s' %
      ('sym', 'pop', 'pkts', 'mean', 'sd', 'CV'))
for sym, (hdr, rows) in store.items():
    for pop in ('book', 'trade'):
        k = col(hdr, rows, pop + '_pkt_closed_n')
        s = col(hdr, rows, pop + '_batch_sum')
        q = col(hdr, rows, pop + '_batch_sumsq')
        K = sum(k)
        if K < 10:
            continue
        m = sum(s) / K
        var = sum(q) / K - m * m
        sd = var ** 0.5 if var > 0 else 0.0
        print('%-6s %-6s %9d %8.2f %8.2f %8.2f' %
              (sym, pop, K, m, sd, sd / m if m else 0))
print()
print('Messages in one packet share one arrival stamp, so the message process')
print('is the packet process compounded with this distribution.')

print()
print('=' * 78)
print('ARRIVAL  --  packet interarrival, from the per-bin moments')
print('=' * 78)
print('%-6s %-6s %9s %12s %10s %8s   %s' %
      ('sym', 'pop', 'gaps', 'mean', 'sd', 'CV', 'wrapped (want 0)'))
for sym, (hdr, rows) in store.items():
    for pop in ('book', 'trade'):
        got = ia_moments(hdr, rows, pop)
        if not got or got[0] < 10:
            continue
        N, m, sd, wrapped = got
        print('%-6s %-6s %9d %12s %10s %8.2f   %d' %
              (sym, pop, N, fmt_ns(m), fmt_ns(sd), sd / m if m else 0, wrapped))
print()
print('CV == 1 is Poisson. These are WITHIN-BIN gaps only, pooled across')
print('bins, so this CV understates the true dispersion. The .arr log')
print('measures it without that caveat. wrapped is the Cauchy-Schwarz check')
print('on the us^2 fix -- anything but 0 means the overflow is still there.')

print()
print('=' * 78)
print('CHANNEL SHARE  --  our packets as a fraction of the channel')
print('=' * 78)
print('%-6s %-6s %10s %12s %8s' % ('sym', 'pop', 'ours', 'others', 'share'))
for sym, (hdr, rows) in store.items():
    for pop in ('book', 'trade'):
        p = sum(col(hdr, rows, pop + '_all_pkt_n'))
        o = sum(col(hdr, rows, pop + '_chan_other_n'))
        if not p:
            continue
        print('%-6s %-6s %10d %12d %7.2f%%' %
              (sym, pop, p, o, 100.0 * p / (p + o)))
print()
print('MsgSeqNum is per CHANNEL; this probe is per SYMBOL and downstream of')
print('the dedup. A sequence step is another instrument on the same channel,')
print('NOT a loss. Real loss is n_gap from MessageProcessor, upstream.')
