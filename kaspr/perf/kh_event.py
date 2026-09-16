import os, sys, glob, time

# BEFORE/AFTER A SCHEDULED EVENT.
#
# A scheduled release is a natural experiment. The binary does not change, the
# host does not change, the code path does not change -- only the ARRIVAL
# PROCESS changes, by an order of magnitude, at a known instant. So it
# separates the two explanations for latency in a way quiet-market
# cross-section cannot:
#
#   If latency is the HOT PATH, it is a property of the code and must not move
#   when only the input rate moves.
#
#   If latency is QUEUEING, it must move, and it must move in step with the
#   things that drive a queue -- arrival rate, batch size, depth.
#
# THE DECISIVE COLUMN IS THE LAST ONE. Rate, batch and qlen all jump at a
# release, so an unconditional latency jump proves nothing by itself: it is
# consistent with both stories. The test is latency AT MATCHED BATCH. If the
# batch~1.0 floor is the SAME before and after while the aggregate mean
# doubles, then the per-message cost did not change and the entire move was
# composition -- more messages arriving behind other messages.
#
# That is the falsifiable form of the claim, and it can fail: if the floor
# itself rises, something in the code IS rate-sensitive and the thesis is
# wrong as stated.
#
# usage: kh_event.py "14:00:00"

exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       'kh_report.py')).read().split("store, drops = {}, {}")[0])

cut_s = sys.argv[1] if len(sys.argv) > 1 else '14:00:00'
lt = time.localtime()
h, m, s = [int(x) for x in cut_s.split(':')]
CUT = int(time.mktime((lt.tm_year, lt.tm_mon, lt.tm_mday, h, m, s,
                       0, 0, -1))) * 1000000000
print('cut at %s  (bin_key_ns >= %d)' % (cut_s, CUT))


def agg(hdr, rows, pop):
    i_n = hdr.index(pop + '_l1_n')
    i_s = hdr.index(pop + '_l1_sum_ns')
    i_x = hdr.index(pop + '_l1_max_ns')
    i_k = hdr.index(pop + '_pkt_closed_n')
    i_b = hdr.index(pop + '_batch_sum')
    i_q = hdr.index(pop + '_qlen_sum')
    i_qx = hdr.index(pop + '_qlen_max')
    n = sum(r[i_n] for r in rows)
    k = sum(r[i_k] for r in rows)
    if not n or not k:
        return None
    mx = [r[i_x] for r in rows if r[i_n]]
    mx.sort()
    span_s = len(rows) * 0.1          # 100 ms bins
    return dict(
        n=n, rate=k / span_s if span_s else 0,
        mean=sum(r[i_s] for r in rows) / n / 1e3,
        p99=mx[int(.99 * (len(mx) - 1))] / 1e3 if mx else 0,
        worst=max(mx) / 1e3 if mx else 0,
        batch=sum(r[i_b] for r in rows) / k,
        qmean=sum(r[i_q] for r in rows) / n,
        qmax=max(r[i_qx] for r in rows),
    )


def floor_at_unit_batch(hdr, rows, pop):
    """Mean leg1 over bins whose OWN batch is ~1, i.e. no serialisation.

    This is the control. Nothing here is held fixed by fiat -- the bins are
    selected on an observable (batch/pkt < 1.05) that is a property of the
    feed, not of the latency being measured.
    """
    i_n = hdr.index(pop + '_l1_n')
    i_s = hdr.index(pop + '_l1_sum_ns')
    i_k = hdr.index(pop + '_pkt_closed_n')
    i_b = hdr.index(pop + '_batch_sum')
    i_qx = hdr.index(pop + '_qlen_max')
    sel = [r for r in rows
           if r[i_n] and r[i_k] and r[i_b] / r[i_k] < 1.05 and r[i_qx] <= 1]
    n = sum(r[i_n] for r in sel)
    if n < 100:
        return None, n
    return sum(r[i_s] for r in sel) / n / 1e3, n


print()
print('=' * 92)
print('%-6s %-6s %-6s %8s %8s %9s %8s %9s %7s %7s %11s' %
      ('sym', 'pop', 'when', 'pkt/s', 'msgs', 'mean', 'p99bin', 'worst',
       'batch', 'qmean', 'floor@b~1'))
print('=' * 92)

for path in sorted(glob.glob(DIR + '/lat_*.csv')):
    sym = os.path.basename(path)[4:-4]
    hdr, rows = load(path)
    if not rows:
        continue
    rows, _ = drop_startup(hdr, rows, startup_pkts(sym))
    pre = [r for r in rows if r[0] < CUT]
    post = [r for r in rows if r[0] >= CUT]
    if not post:
        print('%-6s  (no rows after the cut yet)' % sym)
        continue
    for pop in ('book', 'trade'):
        for lab, rs in (('before', pre), ('after', post)):
            a = agg(hdr, rs, pop)
            if not a:
                continue
            f, fn = floor_at_unit_batch(hdr, rs, pop)
            print('%-6s %-6s %-6s %8.1f %8d %8.1fus %7.1fus %8.1fus %7.2f %7.3f %11s'
                  % (sym, pop, lab, a['rate'], a['n'], a['mean'], a['p99'],
                     a['worst'], a['batch'], a['qmean'],
                     ('%.1fus/%d' % (f, fn)) if f else '-'))
    print('-' * 92)

print("""
READ THE LAST COLUMN FIRST.

floor@b~1 is the mean over bins with batch/pkt < 1.05 AND qlen_max <= 1 --
messages that arrived alone and waited for nothing. That is the hot path with
the queue taken out, and it is the number that must NOT move.

If floor@b~1 is flat while mean, batch and qmean all jump, the latency move
was composition, not code. If floor@b~1 rises too, the hot path is itself
rate-sensitive and the claim needs weakening.

n is printed after the floor for a reason: in a violent minute there may be
very few unqueued messages left, and a floor computed from 100 of them is not
evidence.""")
