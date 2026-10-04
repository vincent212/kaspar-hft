import os, glob

# all_n > span_sum: the flagged bins split into two families by eye --
# differences of exactly 1 on the book streams, differences of 10+ on trade.
# One number decides it. If the diff is overwhelmingly 1, that is a packet
# straddling the bin edge: its messages land in two bins but its span is
# credited to one. If the diff is broad, span_sum counts something else.

DIR = '/home/vincent/perf/mdperf'


def load(path):
    hdr, rows = None, []
    for ln in open(path):
        ln = ln.rstrip('\n')
        if not ln or ln[0] == '#':
            continue
        if ln.startswith('bin_key_ns'):
            hdr = ln.split(',')
            continue
        if hdr is None:
            continue
        p = ln.split(',')
        if len(p) == len(hdr):
            rows.append([int(x) for x in p])
    return hdr, rows


print('%-12s %-6s %8s %8s %8s   %s'
      % ('sym', 'pop', 'bins', 'flagged', 'pct', 'diff histogram (all_n - span_sum)'))
for path in sorted(glob.glob(DIR + '/lat_*.csv')):
    sym = os.path.basename(path)[4:-4]
    hdr, rows = load(path)
    if not rows:
        continue
    for pop in ('book', 'trade'):
        i_an = hdr.index(pop + '_all_n')
        i_sp = hdr.index(pop + '_span_sum')
        h, tot, nb = {}, 0, 0
        for r in rows:
            if r[i_an] == 0:
                continue
            nb += 1
            d = r[i_an] - r[i_sp]
            if d > 0:
                tot += 1
                k = d if d <= 5 else (10 if d <= 10 else 99)
                h[k] = h.get(k, 0) + 1
        if not tot:
            continue
        lab = {1: '1', 2: '2', 3: '3', 4: '4', 5: '5', 10: '6-10', 99: '>10'}
        s = '  '.join('%s:%d(%.0f%%)' % (lab[k], h[k], 100.0 * h[k] / tot)
                      for k in sorted(h))
        print('%-12s %-6s %8d %8d %7.2f%%   %s'
              % (sym, pop, nb, tot, 100.0 * tot / nb, s))
