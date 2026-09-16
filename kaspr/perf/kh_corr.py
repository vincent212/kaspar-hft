#!/usr/bin/env python3
"""
qlen vs latency, per symbol, from the LatencyProbe CSVs.

WHAT IS BEING CORRELATED, AND WHAT IS LOST
------------------------------------------
The CSV is per-100ms-bin aggregates. Each row carries, for one population
(book or trade):

    l1_n, l1_sum, l1_min, l1_max      leg 1 = socket read -> book published
    qlen_sum, qlen_min, qlen_max      MsgBuf mailbox occupancy at enqueue

qlen here is NOT the 100 ms QLen gauge. LatencyProbe.hpp:146 -- it is
pl->ingress_qlen, stamped per message at enqueue, accumulated inside the same
branch of sample() that records the legs. So qlen_sum/l1_n and l1_sum/l1_n are
means over the SAME messages. The regressor is not a load proxy sampled on a
timer; it is the actual depth each timed message saw.

What IS lost is within-bin pairing. We have sum(latency) and sum(qlen) over a
bin, not the (latency, qlen) pair per message. So this is a bin-level
regression. If depth varies a lot inside a single 100 ms bin, averaging both
sides destroys most of the covariance and the correlation is biased toward
zero. A weak r here is therefore NOT evidence that depth does not matter.

A strong r IS evidence that it does.

LEG 2 AND THE FLUSH COLUMN
--------------------------
Bins with flush=1 are the bins the probe's own CSV write ran in. That write is
on the probe's thread, so it lands inside leg 2 for those bins. They are
dropped from leg-2 stats. Leg 1 is upstream of the probe entirely and is not
affected, so leg 1 keeps them -- dropping them would throw away real samples.
"""
import csv, glob, math, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import kh_recovery

# EXCLUSION IS DRIVEN BY THE LOG, NOT BY A FIXED WARMUP.
#
# The old rule was "drop the first 300 s", a guess standing in for "drop until
# the books are built". Book building IS instrument + data recovery, and the log
# says exactly when each channel finished. So we exclude the measured window
# per channel instead of guessing one for all of them. On the 2026-09-16 pinned
# run that recovers ~3 min of good data per symbol, and it is right rather than
# merely conservative -- a fixed cut is simultaneously too long for a clean
# start and far too short for a recovery that fires at minute 20.
#
# WARMUP_S is now a FLOOR applied on top, not the mechanism. Keep it small and
# non-zero: process start, page faults and cache warming are real but are not
# in the log. If a run has no parsable log at all this is the only protection
# left, and the report says so loudly.
WARMUP_S = int(os.environ.get("WARMUP_S", "30"))
DROP_FLUSH_FOR_L2 = True

# Set by main() once the log is located, so analyse() can report it.
LOGPATH = None


def find_log(hint=None):
    """Newest kaspr_log.*.log. NOT the stdout capture -- that file contains no
    log_inf/log_wrn output at all and grepping it returns a false clean."""
    if hint and os.path.exists(hint):
        return hint
    c = glob.glob("/home/vincent/kh-probe/kaspr/kaspr_log.*.log")
    c = [f for f in c if os.path.getsize(f) > 0]
    return max(c, key=os.path.getmtime) if c else None

COLS = ("bin_key_ns book_l1_n book_l1_sum_ns book_l1_min_ns book_l1_max_ns "
        "book_l2_n book_l2_sum_ns book_l2_min_ns book_l2_max_ns "
        "trade_l1_n trade_l1_sum_ns trade_l1_min_ns trade_l1_max_ns "
        "trade_l2_n trade_l2_sum_ns trade_l2_min_ns trade_l2_max_ns "
        "book_qlen_sum book_qlen_min book_qlen_max "
        "trade_qlen_sum trade_qlen_min trade_qlen_max flush").split()


def pearson(xs, ys):
    n = len(xs)
    if n < 3:
        return None
    mx = sum(xs) / n
    my = sum(ys) / n
    sxy = sum((a - mx) * (b - my) for a, b in zip(xs, ys))
    sxx = sum((a - mx) ** 2 for a in xs)
    syy = sum((b - my) ** 2 for b in ys)
    if sxx <= 0 or syy <= 0:
        return None          # a constant column has no correlation, not r=0
    return sxy / math.sqrt(sxx * syy)


def rank(v):
    """Average ranks, so ties do not invent ordering."""
    order = sorted(range(len(v)), key=lambda i: v[i])
    r = [0.0] * len(v)
    i = 0
    while i < len(order):
        j = i
        while j + 1 < len(order) and v[order[j + 1]] == v[order[i]]:
            j += 1
        avg = (i + j) / 2.0 + 1.0
        for k in range(i, j + 1):
            r[order[k]] = avg
        i = j + 1
    return r


def spearman(xs, ys):
    if len(xs) < 3:
        return None
    return pearson(rank(xs), rank(ys))


def load(path):
    rows = []
    with open(path) as f:
        for line in f:
            if line.startswith('#') or line.startswith('bin_key_ns'):
                continue
            p = line.rstrip('\n').split(',')
            if len(p) != len(COLS):
                continue
            try:
                rows.append({c: int(v) for c, v in zip(COLS, p)})
            except ValueError:
                continue
    return rows


def scatter(pairs, width=58, height=14):
    """Plain ASCII. '#' = many bins at that cell, '.' = one."""
    if not pairs:
        return ["  (no data)"]
    xs = [p[0] for p in pairs]
    ys = [p[1] for p in pairs]
    x0, x1 = min(xs), max(xs)
    y0, y1 = min(ys), max(ys)
    if x1 == x0:
        return ["  (qlen is constant at %g -- nothing to plot against)" % x0]
    if y1 == y0:
        return ["  (latency is constant -- nothing to plot)"]
    grid = [[0] * width for _ in range(height)]
    for x, y in pairs:
        cx = int((x - x0) / (x1 - x0) * (width - 1))
        cy = int((y - y0) / (y1 - y0) * (height - 1))
        grid[height - 1 - cy][cx] += 1
    out = []
    for r, row in enumerate(grid):
        lab = ""
        if r == 0:
            lab = "%8.1f" % (y1 / 1000.0)
        elif r == height - 1:
            lab = "%8.1f" % (y0 / 1000.0)
        else:
            lab = " " * 8
        cells = "".join('#' if c > 2 else ('+' if c == 2 else ('.' if c else ' '))
                        for c in row)
        out.append(lab + " |" + cells)
    out.append(" " * 8 + " +" + "-" * width)
    out.append(" " * 9 + " %-*s%s" % (width - 8, "%.2f" % x0, "%.2f" % x1))
    return out


def analyse(path, pop, excl):
    rows = load(path)
    if not rows:
        return "%s: NO ROWS" % path

    t0 = min(r['bin_key_ns'] for r in rows)
    floor = t0 + WARMUP_S * 1_000_000_000

    def contaminated(k):
        if k < floor:
            return True
        return any(lo <= k <= hi for lo, hi in excl)

    kept = [r for r in rows if not contaminated(r['bin_key_ns'])]
    dropped = len(rows) - len(kept)

    n1, s1 = pop + '_l1_n', pop + '_l1_sum_ns'
    mn1, mx1 = pop + '_l1_min_ns', pop + '_l1_max_ns'
    n2, s2 = pop + '_l2_n', pop + '_l2_sum_ns'
    mn2, mx2 = pop + '_l2_min_ns', pop + '_l2_max_ns'
    qs, qmn, qmx = pop + '_qlen_sum', pop + '_qlen_min', pop + '_qlen_max'

    live = [r for r in kept if r[n1] > 0]

    out = []
    out.append("  bins total          %d" % len(rows))
    out.append("  bins dropped        %d   (%ds floor + %d recovery window(s))"
               % (dropped, WARMUP_S, len(excl)))
    out.append("  bins kept           %d   (%d non-empty, %d with no published msg)"
               % (len(kept), len(live), len(kept) - len(live)))
    if not live:
        out.append("  NO MESSAGES after exclusion -- nothing to correlate")
        return "\n".join(out)

    msgs = sum(r[n1] for r in live)
    span_s = (max(r['bin_key_ns'] for r in live)
              - min(r['bin_key_ns'] for r in live)) / 1e9 + 0.1
    out.append("  messages            %d over %.0f s (%.0f/s)"
               % (msgs, span_s, msgs / span_s))

    # Pooled (message-weighted) stats. The denominator is on every line.
    l1_mean = sum(r[s1] for r in live) / msgs
    l1_min = min(r[mn1] for r in live)
    l1_max = max(r[mx1] for r in live)
    q_mean = sum(r[qs] for r in live) / msgs
    q_max = max(r[qmx] for r in live)
    q_min = min(r[qmn] for r in live)
    out.append("  leg1 ns  sock->pub  mean %-10.0f min %-10d max %-10d n=%d"
               % (l1_mean, l1_min, l1_max, msgs))

    # leg 2 excludes flush bins (the probe's own CSV write lands inside it).
    # min/max are min-of-mins and max-of-maxes across the kept bins, which is
    # exact for the sampled population -- the per-bin extremes are carried in
    # the CSV, not reconstructed.
    l2rows = [r for r in live
              if r[n2] > 0 and not (DROP_FLUSH_FOR_L2 and r['flush'])]
    n2m = sum(r[n2] for r in l2rows)
    if n2m:
        l2_mean = sum(r[s2] for r in l2rows) / n2m
        out.append("  leg2 ns  pub->hndl mean %-10.0f min %-10d max %-10d n=%d  "
                   "(flush bins excluded)"
                   % (l2_mean, min(r[mn2] for r in l2rows),
                      max(r[mx2] for r in l2rows), n2m))
        # Means add. Extremes do not -- the slowest leg1 and the slowest leg2
        # are not the same message, and the CSV keeps no per-message pair. So
        # only the mean of the total is reportable; there is no max total here.
        out.append("  total ns (mean only) %-9.0f  no min/max: the per-message "
                   "leg1/leg2 pair is not kept" % (l1_mean + l2_mean))
    else:
        out.append("  leg2 ns             NO SAMPLES after flush exclusion")
    out.append("  qlen msgs           mean %-10.3f min %-10d max %-10d n=%d"
               % (q_mean, q_min, q_max, msgs))

    nz_bins = sum(1 for r in live if r[qmx] > 0)
    out.append("  bins with qlen>0    %d / %d  (%.1f%%)"
               % (nz_bins, len(live), 100.0 * nz_bins / len(live)))

    # The regression itself.
    xs = [r[qs] / r[n1] for r in live]     # bin mean depth
    ys = [r[s1] / r[n1] for r in live]     # bin mean leg1
    r_p = pearson(xs, ys)
    r_s = spearman(xs, ys)
    out.append("")
    out.append("  bin-mean qlen  vs  bin-mean leg1:   pearson %s   spearman %s   n_bins=%d"
               % ("%.4f" % r_p if r_p is not None else "undefined (constant)",
                  "%.4f" % r_s if r_s is not None else "undefined (constant)",
                  len(live)))

    # Pearson and Spearman disagreeing is itself the finding. Pearson is a
    # least-squares fit, so a handful of bins far out on both axes can carry it
    # on their own; Spearman only cares about order and ignores how far. When
    # pearson >> spearman the linear number is leverage from a few points, not
    # a relationship that holds across the population. Say so, rather than let
    # the bigger number get quoted.
    if r_p is not None and r_s is not None and abs(r_p) - abs(r_s) > 0.15:
        out.append("    ^ pearson >> spearman: the linear r is carried by a few "
                   "extreme bins,")
        out.append("      not by a trend across bins. Read the table below, not "
                   "the r.")

    xs2 = [float(r[qmx]) for r in live]
    ys2 = [float(r[mx1]) for r in live]
    r_p2 = pearson(xs2, ys2)
    r_s2 = spearman(xs2, ys2)
    out.append("  bin-max  qlen  vs  bin-max  leg1:   pearson %s   spearman %s   n_bins=%d"
               % ("%.4f" % r_p2 if r_p2 is not None else "undefined (constant)",
                  "%.4f" % r_s2 if r_s2 is not None else "undefined (constant)",
                  len(live)))

    # Conditional means. More legible than r when the regressor is a small int.
    out.append("")
    out.append("  leg1 conditioned on the bin's max qlen:")
    out.append("    qlen_max   bins     msgs      min_leg1  mean_leg1  max_leg1")
    buckets = {}
    for r in live:
        k = r[qmx]
        k = k if k <= 4 else (5 if k <= 9 else (10 if k <= 49 else 50))
        b = buckets.setdefault(k, [0, 0, 0, 1 << 62, 0])
        b[0] += 1
        b[1] += r[n1]
        b[2] += r[s1]
        b[3] = min(b[3], r[mn1])
        b[4] = max(b[4], r[mx1])
    for k in sorted(buckets):
        b = buckets[k]
        lab = {5: "5-9", 10: "10-49", 50: ">=50"}.get(k, str(k))
        out.append("    %-10s %-8d %-9d %-9d %-10.0f %d"
                   % (lab, b[0], b[1], b[3], b[2] / b[1] if b[1] else 0, b[4]))

    out.append("")
    out.append("  scatter: x = bin mean qlen (msgs), y = bin mean leg1 (us)")
    out.extend(scatter(list(zip(xs, ys))))
    return "\n".join(out)


def main():
    d = sys.argv[1] if len(sys.argv) > 1 else "/home/vincent/perf/mdperf"
    log = find_log(sys.argv[2] if len(sys.argv) > 2 else None)
    files = sorted(glob.glob(os.path.join(d, "lat_*.csv")))
    if not files:
        print("no lat_*.csv in", d)
        return
    print("=" * 74)
    print("qlen vs latency   dir=%s" % d)
    print("=" * 74)
    if log:
        print(kh_recovery.report(log))
    else:
        # Refuse to imply the data is clean when nothing was checked.
        print("NO LOG FOUND -- recovery windows NOT excluded.")
        print("Only the %ds floor is applied. Do not quote these numbers as"
              % WARMUP_S)
        print("gap-free; there is no evidence either way.")
    print("=" * 74)

    for f in files:
        sym = os.path.basename(f)[4:-4]
        excl = kh_recovery.exclusions_for_symbol(log, sym) if log else []
        for pop in ("book", "trade"):
            print()
            print("-" * 74)
            print("%s  %s" % (sym, pop.upper()))
            print("-" * 74)
            print(analyse(f, pop, excl))


main()
