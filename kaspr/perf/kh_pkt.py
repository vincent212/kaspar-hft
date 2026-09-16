import sys, os, glob, struct, time

# MESSAGES PER PACKET -- the distribution, not the mean.
#
# The mean is ~1.1 on book and ~3 on trade and it is useless on its own,
# because the latency cost of a packet falls on its LAST message and rises
# linearly with position (kh_idx.py: 291-2284 ns per message ahead). A stream
# whose packets are 1 message 95% of the time and 34 messages 0.1% of the time
# has the same mean as one that is always 1.2, and a completely different tail.
#
# Two different counts, both reported, because they answer different questions:
#
#   batch = messages OF THIS SYMBOL in the packet. Consecutive records in the
#           .msg file between idx resets. This is what the CSV's batch_sum
#           counts.
#
#   span  = max(idx) + 1 in the packet = total decode positions, ALL symbols.
#           idx is pkt_entry_idx, the decode counter for the whole packet, so a
#           symbol's own first message can land at idx 5 if five messages for
#           other symbols were decoded ahead of it. span is what actually
#           determines the wait, and it is >= batch.
#
# PACKET BOUNDARY RULE. idx is non-decreasing inside a packet, so a boundary is
# any record whose idx is <= the previous one. This can only MERGE two packets,
# never split one, and only if a packet's first record for this symbol has a
# strictly greater idx than the last record of the previous packet -- which
# needs the decode order to jump backwards, which it does not.
#
# usage: kh_pkt.py [HH:MM:SS] [HH:MM:SS]

DIR = os.environ.get('MDPERF_DIR', '/home/vincent/perf/mdperf')
HDR, REC, MAGIC = 64, 16, b'KHMSGV01'


def _ns(hms):
    lt = time.localtime()
    h, m, s = [int(x) for x in hms.split(':')]
    return int(time.mktime((lt.tm_year, lt.tm_mon, lt.tm_mday,
                            h, m, s, 0, 0, -1))) * 1000000000


T0 = _ns(sys.argv[1]) if len(sys.argv) > 1 else 0
T1 = _ns(sys.argv[2]) if len(sys.argv) > 2 else (1 << 62)


def load(path):
    recs = []
    with open(path, 'rb') as f:
        blob = f.read()
    off, n = 0, len(blob)
    while off + HDR <= n and blob[off:off + 8] == MAGIC:
        if struct.unpack_from('<I', blob, off + 8)[0] != REC:
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


def pct(v, p):
    return v[min(len(v) - 1, int(p * (len(v) - 1)))]


print('=' * 76)
print('MESSAGES PER PACKET -- distribution. The mean hides the tail: the cost')
print('of a packet lands on its last message, linearly in position.')
print('=' * 76)

for path in sorted(glob.glob(DIR + '/lat_*_*.msg')):
    b = os.path.basename(path)[4:-4]
    sym, _, pop = b.rpartition('_')
    recs = [r for r in load(path) if T0 <= r[0] < T1]
    if len(recs) < 2000:
        continue

    # Split into packets on an idx reset.
    pkts = []          # (batch, span, last_lat_us, max_lat_us)
    cur = []
    prev_ix = -1
    for t1, l1, q, ix in recs:
        if ix <= prev_ix and cur:
            pkts.append(cur)
            cur = []
        cur.append((l1, ix))
        prev_ix = ix
    if cur:
        pkts.append(cur)
    if len(pkts) < 200:
        continue

    batches = sorted(len(p) for p in pkts)
    spans = sorted(p[-1][1] + 1 for p in pkts)
    nmsg = len(recs)
    npkt = len(pkts)

    print()
    print('%s %s   %d packets, %d messages' % (sym, pop, npkt, nmsg))
    print('  batch (this symbol)  mean %.2f   p50 %d  p90 %d  p99 %d  p999 %d  max %d'
          % (nmsg / float(npkt), pct(batches, .5), pct(batches, .9),
             pct(batches, .99), pct(batches, .999), batches[-1]))
    print('  span  (all symbols)  mean %.2f   p50 %d  p90 %d  p99 %d  p999 %d  max %d'
          % (sum(spans) / float(npkt), pct(spans, .5), pct(spans, .9),
             pct(spans, .99), pct(spans, .999), spans[-1]))

    # Histogram of span, with the MESSAGE share beside the PACKET share.
    # These two diverge hard and the divergence is the whole point: a tiny
    # fraction of packets holds a large fraction of messages, and every
    # message in them pays for its position.
    print()
    print('  %-10s %8s %8s %10s %8s %10s'
          % ('span', 'pkts', '%pkts', 'msgs in', '%msgs', 'mean lat'))
    edges = [(1, 1), (2, 2), (3, 4), (5, 9), (10, 19), (20, 49), (50, 1 << 30)]
    for lo, hi in edges:
        sel = [p for p in pkts if lo <= p[-1][1] + 1 <= hi]
        if not sel:
            continue
        m = sum(len(p) for p in sel)
        lat = [r[0] / 1e3 for p in sel for r in p]
        print('  %-10s %8d %7.2f%% %10d %7.2f%% %9.1fus'
              % ('%d' % lo if lo == hi
                 else ('%d-%d' % (lo, hi) if hi < 1 << 30 else '%d+' % lo),
                 len(sel), 100.0 * len(sel) / npkt, m, 100.0 * m / nmsg,
                 sum(lat) / len(lat)))

    # The tail, stated as a share. This is the sentence the paper needs:
    # x% of packets carry y% of messages and z% of the total latency.
    # recs holds (t1, l1, q, ix) but the per-packet lists hold (l1, ix), so
    # the latency field is at a different index in the two. Summing r[0] here
    # would sum epoch timestamps against a numerator in nanoseconds and report
    # 0.00% for everything.
    tot_lat = sum(r[1] for r in recs)
    big = [p for p in pkts if p[-1][1] + 1 >= 10]
    if big:
        bm = sum(len(p) for p in big)
        bl = sum(r[0] for p in big for r in p)
        print('  span>=10: %.2f%% of packets, %.2f%% of messages, %.2f%% of'
              ' total leg-1 time'
              % (100.0 * len(big) / npkt, 100.0 * bm / nmsg,
                 100.0 * bl / tot_lat))

print()
print("""batch is this symbol's share; span is the whole packet's decode count and
is what a message actually waits behind. Read span against the kh_idx.py slope:
a span-34 packet costs its last message 33 * slope on top of the floor, which
on ZN book is 33 * 2.284 = 75us. That is the tail, and it is arithmetic, not
congestion.""")
