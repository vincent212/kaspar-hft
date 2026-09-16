import sys, os, struct, time

# IS THE qlen TABLE ITSELF CONFOUNDED BY idx?
#
# RESULT_per_message.md prints mean(l1 | qlen == d) and reads the rise across d
# as the cost of the queue. But the same burst that makes the ring deep also
# makes packets big, so messages at qlen d>0 may simply sit later in their own
# packet. If so that table is an UPPER bound on the queue, not an estimate of
# it: part of what it charges to qlen is really idx.
#
# The fix is the same one that fixed the bin-level fit: stop averaging over the
# other variable. Hold idx == 0 and vary qlen. A message at idx 0 is first in
# its own packet, so there is no in-packet serialisation left in its latency,
# and whatever still tracks qlen is the queue.
#
# Loader is duplicated from kh_msg.py on purpose: that file is a flat script
# with no main guard, so importing it runs the whole report.
#
# usage: kh_qidx.py [HH:MM:SS] [HH:MM:SS]

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
        recsz = struct.unpack_from('<I', blob, off + 8)[0]
        if recsz != REC:
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


def mean(v):
    return sum(v) / float(len(v)) if v else 0.0


def med(v):
    """Mean at a given qlen is eaten by the occasional multi-millisecond stall,
    which has nothing to do with the queue. Median answers the question the
    table is actually asking: what does the typical message at qlen d pay."""
    if not v:
        return 0.0
    s = sorted(v)
    n = len(s)
    return s[n // 2] if n % 2 else (s[n // 2 - 1] + s[n // 2]) / 2.0


def main():
    t_lo = _ns(sys.argv[1]) if len(sys.argv) > 1 else 0
    t_hi = _ns(sys.argv[2]) if len(sys.argv) > 2 else (1 << 62)

    print('=' * 78)
    print('qlen EFFECT WITH idx HELD AT 0  --  is the qlen table confounded?')
    print('=' * 78)
    print('mean(all) averages over every idx at that qlen. mean@idx0 holds')
    print('position in the packet fixed, so only the queue is left varying.')

    for fn in sorted(os.listdir(DIR)):
        if not fn.endswith('.msg'):
            continue
        recs = [r for r in load(os.path.join(DIR, fn)) if t_lo <= r[0] <= t_hi]
        if len(recs) < 500:
            continue

        # Packet span per message. idx is non-decreasing inside a packet, so a
        # boundary is any record whose idx <= the previous one. span = max idx
        # + 1 = total decode positions in that packet. Every message in the
        # packet is tagged with it, so "mean span at qlen d" answers directly:
        # when the ring is deep, are the packets also big?
        spans = [0] * len(recs)
        start, prev_ix = 0, -1
        for i, (_t1, _l1, _q, ix) in enumerate(recs):
            if ix <= prev_ix and i > start:
                s = recs[i - 1][3] + 1
                for j in range(start, i):
                    spans[j] = s
                start = i
            prev_ix = ix
        s = recs[-1][3] + 1
        for j in range(start, len(recs)):
            spans[j] = s

        by_all, by_i0, by_ix, by_sp = {}, {}, {}, {}
        for i, (_t1, l1, q, ix) in enumerate(recs):
            by_all.setdefault(q, []).append(l1)
            by_ix.setdefault(q, []).append(ix)
            by_sp.setdefault(q, []).append(spans[i])
            if ix == 0:
                by_i0.setdefault(q, []).append(l1)

        print('\n%-14s %d messages' % (fn[4:-4], len(recs)))
        print('  %4s %9s %8s %8s %8s %8s   %8s %16s %16s'
              % ('qlen', 'msgs', 'mean', 'median', 'm.span', 'm.idx',
                 'n@idx0', 'mean@idx0', 'med@idx0'))
        b_all = b_i0 = b_d0 = None
        for q in sorted(by_all):
            if len(by_all[q]) < MIN_N:
                continue
            m_all = mean(by_all[q]) / 1e3
            d_all = med(by_all[q]) / 1e3
            i0 = by_i0.get(q, [])
            enough = len(i0) >= MIN_N
            m_i0 = mean(i0) / 1e3 if enough else None
            d_i0 = med(i0) / 1e3 if enough else None
            if b_all is None:
                b_all, b_i0, b_d0 = m_all, m_i0, d_i0

            def delta(v, base):
                return '' if (q == 0 or v is None or base is None) \
                       else ' (%+.1f)' % (v - base)

            s_m0 = '     n/a' if m_i0 is None else \
                   '%7.1fus%s' % (m_i0, delta(m_i0, b_i0))
            s_d0 = '     n/a' if d_i0 is None else \
                   '%7.1fus%s' % (d_i0, delta(d_i0, b_d0))
            print('  %4d %9d %7.1fus %7.1fus %8.2f %8.2f   %8d %16s %16s'
                  % (q, len(by_all[q]), m_all, d_all,
                     mean(by_sp[q]), mean(by_ix[q]), len(i0), s_m0, s_d0))

    print("""
If mean idx climbs with qlen, the qlen table in RESULT_per_message.md is
confounded and its rise is an upper bound. The @idx0 columns are the
deconfounded version: position fixed, queue varying.

Read the median columns, not the mean ones. A single multi-millisecond stall
inside a 30-message cell moves the mean by tens of microseconds and says
nothing about the queue. The median cell says what the typical message at that
qlen actually paid.""")


if __name__ == '__main__':
    main()
