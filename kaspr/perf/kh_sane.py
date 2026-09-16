import os, glob, struct

# INTEGRITY SCAN. No statistics, no controls, no stratification. Just: does
# every row obey the arithmetic it has to obey?
#
# Checks, per row per population:
#   n == 0 but sum != 0            -- latency banked with no sample
#   n > 0 and min > max            -- extremes crossed
#   mean outside [min, max]        -- sum does not belong to this n
#   sum/n negative                 -- only reachable if sum wrapped
#   ia: sumsq*n < sum*sum          -- Cauchy-Schwarz, so sumsq wrapped
#   ia: var < 0                    -- the same thing, stated as it is consumed
#   batch: var < 0
#   l1_n > all_n                   -- more admitted than arrived
#
# The Cauchy-Schwarz check is what found the ia_sumsq uint64 overflow. It is
# kept after the fix, not retired with it: it costs one multiply and it is
# the only thing that would catch the wrap coming back.
#
# Every violation is printed with the row so it can be looked at, not just
# counted.

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
            continue
        rows.append([int(x) for x in p])
    return hdr, rows


for path in sorted(glob.glob(DIR + '/lat_*.csv')):
    sym = os.path.basename(path)[4:-4]
    hdr, rows = load(path)
    if not rows:
        continue
    print('=' * 70)
    print('%s   %d rows, %d cols' % (sym, len(rows), len(hdr)))

    bad = {}
    def flag(k, r, extra=''):
        bad.setdefault(k, [])
        if len(bad[k]) < 3:
            bad[k].append((r[0], extra))

    for pop in ('book', 'trade'):
        i_n = hdr.index(pop + '_l1_n')
        i_s = hdr.index(pop + '_l1_sum_ns')
        i_mn = hdr.index(pop + '_l1_min_ns')
        i_mx = hdr.index(pop + '_l1_max_ns')
        i_an = hdr.index(pop + '_all_n')
        i_ian = hdr.index(pop + '_ia_n')
        i_ias = hdr.index(pop + '_ia_sum')
        # ia_sum is NANOSECONDS, ia_sumsq_us2 is MICROSECONDS SQUARED. The
        # two are deliberately in different units -- the mean keeps full ns
        # resolution while the second moment buys 10^6 of headroom against
        # the uint64 wrap. Any check spanning both must convert first.
        i_iaq = hdr.index(pop + '_ia_sumsq_us2')
        i_kn = hdr.index(pop + '_pkt_closed_n')
        i_bs = hdr.index(pop + '_batch_sum')
        i_bq = hdr.index(pop + '_batch_sumsq')
        i_sp = hdr.index(pop + '_span_sum')

        for r in rows:
            n, s, mn, mx = r[i_n], r[i_s], r[i_mn], r[i_mx]
            if n == 0 and s != 0:
                flag(pop + ': n==0 but sum!=0', r, 'sum=%d' % s)
            if n > 0:
                if mn > mx:
                    flag(pop + ': min>max', r, 'min=%d max=%d' % (mn, mx))
                mean = s / n
                if mean < mn or mean > mx:
                    flag(pop + ': mean outside [min,max]', r,
                         'n=%d mean=%.1f min=%d max=%d' % (n, mean, mn, mx))
            if r[i_an] < n:
                flag(pop + ': all_n < l1_n', r,
                     'all_n=%d l1_n=%d' % (r[i_an], n))
            # NOTE: all_n > span_sum is NOT a violation and is not checked.
            # span_sum banks a packet only when it CLOSES, and the bin's last
            # packet is still open when the bin is written, so all_n legally
            # runs ahead of span_sum by the open packet's records.

            a, b, c = r[i_ian], r[i_ias], r[i_iaq]
            if a > 1:
                b_us = b / 1000.0        # sum ns -> us, to match sumsq_us2
                if c * a < b_us * b_us:
                    flag(pop + ': ia_sumsq WRAPPED (cauchy-schwarz)', r,
                         'n=%d sum_ns=%d sumsq_us2=%d' % (a, b, c))
                elif c / a - (b_us / a) ** 2 < 0:
                    flag(pop + ': ia var<0', r, 'n=%d' % a)
            # n == 1 is skipped: var is identically 0 there, and float64 on
            # ~1e19 magnitudes returns a tiny negative instead. That is the
            # scanner's own rounding, not a defect in the data.
            k = r[i_kn]
            if k:
                m = r[i_bs] / k
                if r[i_bq] / k - m * m < 0:
                    flag(pop + ': batch var<0', r, 'k=%d' % k)

    if not bad:
        print('  clean -- no violations')
    for k in sorted(bad):
        print('  %-44s %s' % (k, bad[k]))
    print()
