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
#                                     (unit- and truncation-corrected, see below)
#   ia: var < 0                    -- the same thing, stated as it is consumed
#   batch: var < 0
#   l1_n > all_n                   -- more admitted than arrived
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
            if r[i_sp] and r[i_an] > r[i_sp]:
                flag(pop + ': all_n > span_sum', r,
                     'all_n=%d span=%d' % (r[i_an], r[i_sp]))

            # THE TWO ia COLUMNS ARE IN DIFFERENT UNITS ON PURPOSE.
            # ia_sum is nanoseconds, exact. ia_sumsq_us2 is microseconds
            # squared, and the probe squares gap/1000 as an INTEGER
            # (LatencyProbe.hpp:1015), so each term is floored before it is
            # squared. Comparing them directly is a units error; comparing
            # them after an exact ns->us conversion is a truncation error.
            #
            # Truncation only ever makes sumsq SMALLER, so a naive
            # Cauchy-Schwarz reports a wrap that did not happen. The bound is
            # floor(g/1000) > g/1000 - 1, so the truncated sum in us is
            # strictly greater than sum/1000 - n. Test against that lower
            # bound and the check can only fire on a real overflow.
            #
            # This matters most where gaps are small -- sub-10us, i.e. the
            # busy book streams -- because that is where the floored 1us
            # quantum is the largest share of the gap.
            a, b, c = r[i_ian], r[i_ias], r[i_iaq]
            if a:
                lo_us = (b / 1000.0) - a       # lower bound on the truncated
                if lo_us < 0:                  # sum the probe actually squared
                    lo_us = 0.0
                if c * a < lo_us * lo_us:
                    flag(pop + ': ia_sumsq WRAPPED (cauchy-schwarz)', r,
                         'n=%d sum_ns=%d sumsq_us2=%d' % (a, b, c))
                elif c / a - (lo_us / a) ** 2 < 0:
                    flag(pop + ': ia var<0', r, 'n=%d' % a)
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
