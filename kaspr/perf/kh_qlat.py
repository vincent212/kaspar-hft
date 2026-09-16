import sys, os, glob, struct

# QLEN vs MEAN LEG-1 LATENCY, BOTH POPULATIONS.
#
# Straight grouping. Bins are grouped by the deepest queue seen in the bin
# (qlen_max), and each group reports its own mean latency with the message
# count it was computed from. No correlation coefficient, no stratification,
# nothing held fixed.
#
# The one row that is exact is qlen_max == 0: when the deepest queue in a bin
# is zero, EVERY message in that bin had depth zero. Its mean is a clean
# baseline. Every other row is a group whose bins CONTAINED a message at that
# depth and also contained shallower ones, so the group mean sits below what a
# message at that depth actually cost. Those rows are lower bounds.
#
# TRADE IS NOT A SECOND COPY OF BOOK. Two things differ and both matter:
#   - trade packets are RARE (1-4/s vs 50-180/s) and BATCHED (mean 2.9-3.8
#     messages per packet vs 1.05-1.13). A batch shares ONE arrival stamp, so
#     every message after the first in a packet is already waiting on the ones
#     ahead of it -- serialisation that is invisible to qlen.
#   - qlen is MsgBuf depth, which counts PACKETS, not messages. A trade packet
#     at depth 0 can still carry 10 messages behind the one being measured.
# So on trade, depth 0 is NOT a no-waiting baseline, and the batch column has
# to be read next to it.

exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       'kh_report.py')).read().split("store, drops = {}, {}")[0])

BUCKETS = [(0, 0), (1, 1), (2, 2), (3, 4), (5, 9), (10, 1 << 30)]

# Optional window. Pooling an event into 30 quiet minutes dilutes it to
# nothing, so allow a cut: kh_qlat.py 14:00:00 [14:10:00]
import time as _t


def _ns(hms):
    lt = _t.localtime()
    h, m, s = [int(x) for x in hms.split(':')]
    return int(_t.mktime((lt.tm_year, lt.tm_mon, lt.tm_mday,
                          h, m, s, 0, 0, -1))) * 1000000000


T0 = _ns(sys.argv[1]) if len(sys.argv) > 1 else 0
T1 = _ns(sys.argv[2]) if len(sys.argv) > 2 else (1 << 62)


def table(sym, hdr, rows, pop, nd):
    i_n = hdr.index(pop + '_l1_n')
    i_s = hdr.index(pop + '_l1_sum_ns')
    i_x = hdr.index(pop + '_l1_max_ns')
    i_qx = hdr.index(pop + '_qlen_max')
    i_bk = hdr.index(pop + '_pkt_closed_n')
    i_bs = hdr.index(pop + '_batch_sum')

    pts = [(r[i_qx], r[i_n], r[i_s], r[i_x], r[i_bk], r[i_bs])
           for r in rows if r[i_n]]
    if not pts:
        return
    tot_msgs = sum(p[1] for p in pts)
    tot_lat = sum(p[2] for p in pts)
    if tot_msgs < 200:
        return

    print()
    print('%s %s   %d populated bins, %d messages, startup -%d'
          % (sym, pop, len(pts), tot_msgs, nd))
    print('  overall mean leg1 = %.1fus' % (tot_lat / tot_msgs / 1e3))
    print()
    print('  %-10s %8s %10s %12s %12s %8s' %
          ('qlen_max', 'bins', 'msgs', 'mean leg1', 'worst in grp', 'batch'))

    base = None
    for lo, hi in BUCKETS:
        sel = [p for p in pts if lo <= p[0] <= hi]
        if not sel:
            continue
        msgs = sum(p[1] for p in sel)
        lat = sum(p[2] for p in sel)
        mean_us = lat / msgs / 1e3
        if lo == 0:
            base = mean_us
        label = ('%d' % lo if lo == hi
                 else ('%d-%d' % (lo, hi) if hi < 1 << 30 else '%d+' % lo))
        delta = '' if base is None or lo == 0 else '  (%+.1fus)' % (mean_us - base)
        # messages per packet INSIDE this group -- the serialisation term
        pk = sum(p[4] for p in sel)
        bt = sum(p[5] for p in sel)
        print('  %-10s %8d %10d %10.1fus %10.1fus %8s%s' %
              (label, len(sel), msgs, mean_us,
               max(p[3] for p in sel) / 1e3,
               '%.2f' % (bt / pk) if pk else '-', delta))

    zb = sum(1 for p in pts if p[0] == 0)
    zm = sum(p[1] for p in pts if p[0] == 0)
    print('  never queued: %d of %d bins (%.1f%%), %d of %d msgs (%.1f%%)'
          % (zb, len(pts), 100.0 * zb / len(pts),
             zm, tot_msgs, 100.0 * zm / tot_msgs))


print('=' * 80)
print('QLEN vs LEG-1 LATENCY   --  book AND trade, 100ms bins, startup excluded')
print('=' * 80)

for path in sorted(glob.glob(DIR + '/lat_*.csv')):
    sym = os.path.basename(path)[4:-4]
    hdr, rows = load(path)
    if not rows:
        continue
    rows, nd = drop_startup(hdr, rows, startup_pkts(sym))
    rows = [r for r in rows if T0 <= r[0] < T1]
    for pop in ('book', 'trade'):
        table(sym, hdr, rows, pop, nd)

print()
print('=' * 80)
print("""qlen_max == 0 is exact ON BOOK: no message in those bins queued, so that
mean is a true zero-depth baseline.

On TRADE it is not. qlen counts PACKETS in MsgBuf, and a trade packet carries
2.9-3.8 messages that share one arrival stamp. Messages behind the first in a
batch wait on the ones ahead even at depth 0, and that wait is in leg 1 but
not in qlen. Read the batch column beside the mean: where batch is large the
depth-0 row is already carrying serialisation.

Every non-zero row, both populations, is a LOWER BOUND. The group is selected
on the DEEPEST message in the bin but averaged over all of them.""")
