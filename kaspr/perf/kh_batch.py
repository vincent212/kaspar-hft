import os, glob

# IS THE BATCH THE REASON, OR JUST A CORRELATE?
#
# A packet's k messages share ONE arrival stamp t0. If each costs s to
# process and they are done in order, message i is published at about i*s
# after t0, so the MEAN leg 1 over the batch is
#
#       E[leg1] = s * (k + 1) / 2
#
# That is a real prediction with a free parameter. Divide the measured mean
# by (k+1)/2 and what is left is s, the PER-MESSAGE cost. If batching is the
# whole story, s is the same for populations that share a code path and
# differ only in k. If s still differs, something else is going on and batch
# was only a correlate.
#
# Two tests:
#   1. ACROSS populations: implied s for each. book and trade of the same
#      symbol run the same decode and publish on the same thread, so a gap
#      between their s values is NOT explained by batching.
#   2. WITHIN one population: bucket bins by their own mean batch and read
#      the latency. This holds the instrument fixed, so it cannot be
#      confounded by anything about the symbol.
#
# Test 2 is the one that matters. Test 1 can be faked by any per-symbol
# effect that happens to line up with batch size.

exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       'kh_report.py')).read().split("store, drops = {}, {}")[0])

rowsets = {}
for path in sorted(glob.glob(DIR + '/lat_*.csv')):
    sym = os.path.basename(path)[4:-4]
    hdr, rows = load(path)
    if not rows:
        continue
    rows, nd = drop_startup(hdr, rows, startup_pkts(sym))
    rowsets[sym] = (hdr, rows)

print('=' * 78)
print('TEST 1  --  implied per-message cost s = mean / ((batch+1)/2)')
print('=' * 78)
print('%-6s %-6s %10s %8s %10s %10s' %
      ('sym', 'pop', 'msgs', 'batch', 'mean leg1', 'implied s'))
for sym, (hdr, rows) in rowsets.items():
    for pop in ('book', 'trade'):
        n = sum(r[hdr.index(pop + '_l1_n')] for r in rows)
        s = sum(r[hdr.index(pop + '_l1_sum_ns')] for r in rows)
        k = sum(r[hdr.index(pop + '_pkt_closed_n')] for r in rows)
        b = sum(r[hdr.index(pop + '_batch_sum')] for r in rows)
        if not n or not k:
            continue
        mean_us = s / n / 1e3
        batch = b / k
        print('%-6s %-6s %10d %8.2f %9.1fus %9.1fus' %
              (sym, pop, n, batch, mean_us, mean_us / ((batch + 1) / 2)))
print()
print('If batching explains it, book and trade of the SAME symbol -- same')
print('code, same thread -- should show the same implied s.')

print()
print('=' * 78)
print('TEST 2  --  within one population, bins bucketed by their own batch')
print('=' * 78)
BK = [(1.0, 1.05), (1.05, 1.25), (1.25, 1.75), (1.75, 2.5),
      (2.5, 4.0), (4.0, 1e9)]
for sym, (hdr, rows) in rowsets.items():
    for pop in ('book', 'trade'):
        i_n = hdr.index(pop + '_l1_n')
        i_s = hdr.index(pop + '_l1_sum_ns')
        i_k = hdr.index(pop + '_pkt_closed_n')
        i_b = hdr.index(pop + '_batch_sum')
        i_q = hdr.index(pop + '_qlen_max')
        pts = [r for r in rows if r[i_n] and r[i_k]]
        if sum(p[i_n] for p in pts) < 2000:
            continue
        print()
        print('%s %s' % (sym, pop))
        print('  %-12s %8s %10s %11s %9s' %
              ('batch/pkt', 'bins', 'msgs', 'mean leg1', 'qlen_max'))
        for lo, hi in BK:
            sel = [p for p in pts if lo <= p[i_b] / p[i_k] < hi]
            if not sel:
                continue
            m = sum(p[i_n] for p in sel)
            if m < 100:
                continue
            lat = sum(p[i_s] for p in sel)
            # qlen shown alongside so the two explanations can be told apart:
            # if latency rises with batch while qlen stays flat, it is batch.
            print('  %-12s %8d %10d %9.1fus %9d' %
                  ('%.2f-%.2f' % (lo, hi) if hi < 1e9 else '%.1f+' % lo,
                   len(sel), m, lat / m / 1e3,
                   max(p[i_q] for p in sel)))
print()
print('=' * 78)
print("""Instrument is held fixed inside each block, so a rise down a block is not
a symbol effect. The qlen_max column is there to separate the two stories:
if latency climbs with batch while qlen stays flat, the waiting is INSIDE
the packet, not in front of it.""")
