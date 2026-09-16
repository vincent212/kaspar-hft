#!/usr/bin/env python3
"""Mean leg1 ns per queue depth, per book.

WHAT THE FILE ACTUALLY HAS. Per 100ms bin the CSV stores qlen_sum, qlen_min,
qlen_max. It never stores the depth of an individual message. So there is no
per-message join between depth and leg1, and no way to compute a true
conditional mean.

The one case that IS well defined is a bin where qlen_min == qlen_max: every
message in it saw exactly that depth, so its mean leg1 is that depth's mean
leg1, exactly. Those are the only bins this table uses. Everything else is
dropped.

That costs two things, both printed so you can judge them:
  - n collapses. Most bins straddle depths and are thrown away.
  - the surviving bins are a biased sample -- a bin that sat at one depth for a
    full 100ms is not a typical bin at that depth.
The 'bins' column is the denominator: qualifying bins / bins seen at any depth.

The obvious alternative -- group each bin by its qlen_max -- is wrong and is
deliberately not offered. It files every message of a bin that idled at 0 and
touched 202 once under the row labelled 202, which inflates the low rows and
flattens the curve to nothing.

Depth is in PACKETS, not messages. One UDP packet can carry a hundred MDP3
messages and still count as 1. Messages inside a packet share a t0 and are
decoded serially, so leg1 rises with position-in-packet -- an effect this table
cannot see and cannot exclude. Measured at 700-1200 ns per extra message in the
packet, which is the same order as the depth effect below. Until the probe
records a message's index within its packet, the two are not separable.
"""
import glob
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import kh_corr
import kh_recovery


def table(rows, excl, pop):
    """-> {depth: [msgs, sum_ns, bins]}, total bins seen at any depth."""
    t0 = min(r['bin_key_ns'] for r in rows)
    floor = t0 + kh_corr.WARMUP_S * 1_000_000_000
    n1, s1 = pop + '_l1_n', pop + '_l1_sum_ns'
    qmn, qmx = pop + '_qlen_min', pop + '_qlen_max'
    acc = {}
    seen = 0
    for r in rows:
        k = r['bin_key_ns']
        if k < floor or any(lo <= k <= hi for lo, hi in excl):
            continue
        if r[n1] == 0:
            continue
        seen += 1
        if r[qmn] != r[qmx]:
            continue
        a = acc.setdefault(r[qmx], [0, 0, 0])
        a[0] += r[n1]
        a[1] += r[s1]
        a[2] += 1
    return acc, seen


def main():
    d = sys.argv[1] if len(sys.argv) > 1 else "/home/vincent/perf/mdperf"
    log = kh_corr.find_log(sys.argv[2] if len(sys.argv) > 2 else None)
    for f in sorted(glob.glob(os.path.join(d, "lat_*.csv"))):
        sym = os.path.basename(f)[4:-4]
        excl = kh_recovery.exclusions_for_symbol(log, sym) if log else []
        acc, seen = table(kh_corr.load(f), excl, "book")
        kept = sum(a[2] for a in acc.values())
        print("%s BOOK   mean leg1 ns by queue depth" % sym)
        print("  from %d of %d live bins (%.0f%%) -- only bins that held one "
              "depth the whole 100ms" % (kept, seen, 100.0 * kept / seen))
        print("  depth      msgs       mean ns    bins")
        for q in sorted(acc):
            m, s, b = acc[q]
            print("  %-10d %-10d %-10.0f %d" % (q, m, s / m, b))
        print()


if __name__ == "__main__":
    main()
