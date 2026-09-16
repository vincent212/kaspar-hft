import struct, sys, glob, os

# Reader for the Tier-1 arrival log. Verifies the file is self-consistent and
# prints the statistics the binned CSV could not give: the interarrival
# distribution at full resolution, and Fano at an arbitrary scale.

def load(p):
    b = open(p, 'rb').read()
    if len(b) < 64:
        return None          # header still in the writer's buffer
    assert b[:8] == b'KHARRIV1', 'bad magic in %s' % p
    recsz, ver = struct.unpack_from('<II', b, 8)
    assert recsz == 16, 'recsz=%d' % recsz
    tag = b[16:48].split(b'\0')[0].decode()
    t_open, = struct.unpack_from('<Q', b, 48)
    body = b[64:]
    assert len(body) % 16 == 0, 'ragged tail: %d bytes' % (len(body) % 16)
    recs = [struct.unpack_from('<QIHH', body, i) for i in range(0, len(body), 16)]
    return tag, ver, t_open, recs

def pct(v, q):
    if not v:
        return 0
    k = int(q * (len(v) - 1))
    return v[k]

for p in sorted(glob.glob('/home/vincent/perf/mdperf/*.arr')):
    name = os.path.basename(p)
    got = load(p)
    if got is None:
        print('%-24s (no header yet)' % name)
        continue
    tag, ver, t_open, recs = got

    # STARTUP BACKLOG. At start the socket buffer already holds a backlog; it
    # is drained in one read and every record in it carries the SAME
    # hndl_tim_epoch. That is the recorder catching up, not an arrival
    # process, and left in it dominates every variance below. Drop the leading
    # run of identical t0 -- the same contamination as CSV row 0.
    k = 1
    while k < len(recs) and recs[k][0] == recs[0][0]:
        k += 1
    dropped = k if k > 1 else 0
    recs = recs[dropped:]

    n = len(recs)
    if n < 2:
        print('%-24s %s  n=%d  (too few)' % (name, tag, n))
        continue

    t = [r[0] for r in recs]
    seq = [r[1] for r in recs]
    batch = [r[2] for r in recs]
    span = [r[3] for r in recs]

    # monotone arrival time? a non-monotone t0 means the record order is not
    # the arrival order and every interarrival below is suspect.
    nonmono = sum(1 for i in range(1, n) if t[i] < t[i - 1])

    ia = sorted(t[i] - t[i - 1] for i in range(1, n))
    m = sum(ia) / len(ia)
    var = sum((x - m) ** 2 for x in ia) / len(ia)
    cv = (var ** 0.5) / m if m else 0

    dur = (t[-1] - t[0]) / 1e9
    bm = sum(batch) / n
    sm = sum(span) / n

    print('%-24s %s' % (name, tag))
    print('   n=%-7d dur=%.1fs  rate=%.1f pkt/s  (startup burst dropped: %d)'
          % (n, dur, n / dur if dur else 0, dropped))
    print('   zero-gap pairs after drop: %d (%.2f%%)'
          % (sum(1 for x in ia if x == 0), 100.0 * sum(1 for x in ia if x == 0) / len(ia)))
    qs = [pct(ia, q) / 1e3 for q in (.01, .10, .50, .90, .99, .999)] + [ia[-1] / 1e3]
    print('   ia us: p1=%.1f p10=%.1f p50=%.1f p90=%.1f p99=%.1f p999=%.1f max=%.1f'
          % tuple(qs))
    print('   ia mean=%.1fus  CV=%.2f   (CV=1 => Poisson)' % (m / 1e3, cv))
    # span is (max pkt_entry_idx in the packet + 1). pkt_entry_idx is the
    # decode counter for the WHOLE PACKET -- every instrument on the channel,
    # MBO and MBOT sharing one counter. batch is what this ONE symbol+pop
    # actually received. So batch/span is this symbol's SHARE of the packet,
    # NOT dedup suppression. It reads low on trade precisely because a packet
    # carrying one trade also carries book records and other instruments.
    print('   batch mean=%.2f  span mean=%.2f  sym share of pkt=%.1f%%'
          % (bm, sm, 100.0 * bm / sm if sm else 0))
    print('   seq monotone-violations=%d  nonmono-t0=%d' % (
        sum(1 for i in range(1, n) if seq[i] <= seq[i - 1]), nonmono))

    # Fano at several scales, straight off the raw stamps. No rebuild needed.
    out = []
    for w_ms in (1, 10, 100, 1000, 10000):
        w = w_ms * 1000000
        base = t[0]
        nb = int((t[-1] - base) // w) + 1
        if nb < 10:
            continue
        c = [0] * nb
        for x in t:
            c[int((x - base) // w)] += 1
        mu = sum(c) / nb
        v = sum((y - mu) ** 2 for y in c) / nb
        out.append('%dms:%.2f' % (w_ms, v / mu if mu else 0))
    print('   Fano ' + '  '.join(out))
    print()
