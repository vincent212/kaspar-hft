#!/usr/bin/env python3
# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
#
# Licensed under the MIT License. See LICENSE file in the project root.
"""Turn a grid run into the tables that go in the paper.

    ./aggregate_grid.py /vast/home/vmayeski/gridruns/full2 [--csv out.csv]

Three rules are enforced here rather than left to whoever writes the next awk,
because breaking any of them silently produced a wrong headline number during
development:

  * Filter on outcome == ok. A leg that never filled carries vwap = 0; 38 such
    rows in 3,056 moved a mean by four orders of magnitude and once made the
    result read as risk-free profit.
  * Participation is reported as a MEDIAN with percentiles, plus the aggregate
    (total filled / total volume). The mean of per-fire ratios is dominated by
    fires with tiny denominators and reverses the sign of the size effect.
  * Survivorship is always shown, but the probe no longer emits a short row per
    missed fire, so a RATIO of ok rows is not the control it used to be. A
    window now stays open until both legs fill, which means a slow config
    produces FEWER windows rather than more failed ones -- so windows-per-
    session is the number that exposes it, and it is reported alongside the
    ok-rate. A config yielding 3 windows where its neighbours yield 24 is the
    same warning that a 25% completion rate used to be.
"""
import argparse, csv, glob, math, os, statistics as st, sys

COLS = None  # resolved from the header

def pct(xs, p):
    if not xs: return float('nan')
    xs = sorted(xs); k = min(len(xs) - 1, max(0, int(p * len(xs))))
    return xs[k]

def ci95(xs):
    n = len(xs)
    if n < 2: return float('nan')
    return 1.96 * st.stdev(xs) / math.sqrt(n)

def load(cfg_dir):
    """Every fire for one config, plus the completion tally."""
    fires, total, by_outcome = [], 0, {}
    for path in glob.glob(os.path.join(cfg_dir, '2025*.csv')):
        with open(path) as fh:
            for row in csv.DictReader(fh):
                total += 1
                oc = row.get('outcome', '?')
                by_outcome[oc] = by_outcome.get(oc, 0) + 1
                if oc != 'ok':
                    continue
                try:
                    # Built COMPLETE in a local and appended only once every key
                    # is set. Writing the optional keys into fires[-1] after the
                    # append meant a bad value in the 2nd, 3rd or 4th of them --
                    # a truncated final line from a killed run, a stray comma --
                    # left a half-keyed row in the list, and summarise() then
                    # died with KeyError over an entire multi-hour grid instead
                    # of skipping the one bad row.
                    f = {k: float(row[k]) for k in (
                        'slip_buy_ticks', 'slip_sel_ticks', 'slip_paired_ticks',
                        'buy_filled', 'sel_filled',
                        'buy_leg_mkt_vol', 'sel_leg_mkt_vol',
                        'buy_part', 'sel_part', 'buy_ns', 'sel_ns',
                        'buy_fills', 'parent_sz')}
                    # Optional: added after the first runs, and drift replaced
                    # slip_legsum (which had become algebraically identical to
                    # slip_paired once both legs began quoting together). Absent
                    # in an older CSV, where None means "not measured" and is
                    # kept distinct from a measured 0.0.
                    for k in ('slip_vs_vwap', 'slip_buy_vs_vwap',
                              'slip_vs_touch', 'slip_buy_vs_touch',
                              'buy_drift_ticks', 'sel_drift_ticks',
                              'drift_ticks'):
                        v = row.get(k)
                        f[k] = float(v) if v not in (None, '') else None
                    fires.append(f)
                except (KeyError, ValueError):
                    continue
    return fires, total, by_outcome

def summarise(name, params, fires, total, by_outcome):
    if not fires:
        return None
    g = lambda k: [f[k] for f in fires]
    # None means the column was absent from that CSV. 0.0 means it was measured
    # and came out zero -- a perfectly ordinary result for drift over a quiet
    # window, and one that truthiness silently discarded from both headline
    # benchmark columns.
    have = lambda k: [f[k] for f in fires if f.get(k) is not None]
    paired = g('slip_paired_ticks')
    drift  = have('drift_ticks')
    # Participation is recomputed here, NOT taken from the CSV's buy_part.
    #
    # SlippageProbe computes buy_part as filled / mkt_vol, and mkt_vol counts
    # only MARKET trades -- our own fills never enter it, because we fill
    # against resting exchange orders and the replayed feed knows nothing about
    # our orders. That makes the column our volume as a multiple of everyone
    # else's, not a share of the total, and it is unbounded: Q20 produced a
    # maximum of 166%, which is 20 lots filled in a window where 12 lots traded.
    #
    # A participation rate means ours / (ours + market), bounded by 100%, which
    # is what a POV algorithm and the paper both mean by the word. Both
    # ingredients are in every row, so this is recoverable from data already
    # written -- no rerun, and rows from before and after the fix aggregate
    # identically.
    #
    # Still optimistic, and the formula cannot fix it: in reality our fills
    # would have DISPLACED someone else's rather than adding to the day's
    # volume, so the true denominator is smaller again. That is the zero-impact
    # assumption, fine at 2% and strained at 15%.
    def part(f, side='buy'):
        ours, mkt = f[f'{side}_filled'], f[f'{side}_leg_mkt_vol']
        return ours / (ours + mkt) if (ours + mkt) > 0 else 0.0

    # A window where NOTHING ELSE TRADED is not 100% participation, it is an
    # undefined ratio, and it belongs in neither the distribution nor the
    # aggregate. It happens when the leg is short enough that no market trade
    # lands beside it -- 4 of 741 windows at 10 lots, but the dominant case at
    # 1 lot, where the leg completes in 0.76s off a single child order. Left in,
    # it puts a 1.0 in the tail: the p90 for a 1-lot parent reads 100% while the
    # same config at 10 lots reads 10.5%, and the median hides the difference
    # entirely (5.6% vs 4.7%). The p90 is the honest statistic here, so it must
    # not be an artefact.
    measurable = [f for f in fires if f['buy_leg_mkt_vol'] > 0]
    dropped = len(fires) - len(measurable)

    bpart = [part(f) for f in measurable if part(f) > 0]
    agg_num = sum(f['buy_filled'] for f in measurable)
    agg_den = sum(f['buy_filled'] + f['buy_leg_mkt_vol'] for f in measurable)
    return {
        'config': name, **params,
        'sessions': len({0}),  # filled in by caller
        'fires_ok': len(fires), 'fires_total': total,
        'completion': len(fires) / total if total else 0.0,
        'slip_paired': st.mean(paired), 'slip_paired_ci': ci95(paired),
        # Drift over the window, measured forward from the common arrival. This
        # is what slip_legsum used to expose indirectly, back when the two legs
        # had separate arrivals to difference.
        'drift': st.mean(drift) if drift else float('nan'),
        'drift_ci': ci95(drift) if drift else float('nan'),
        'slip_buy': st.mean(g('slip_buy_ticks')),
        'slip_sel': st.mean(g('slip_sel_ticks')),
        'part_median': pct(bpart, 0.5), 'part_p10': pct(bpart, 0.1),
        'part_p90': pct(bpart, 0.9),
        'part_aggregate': agg_num / agg_den if agg_den else float('nan'),
        # Windows with no market volume beside the leg -- participation is
        # undefined for these, so they are excluded above and counted here.
        'part_undefined': dropped,
        # Only fires where the benchmark exists: 0 means the leg had no market
        # volume alongside it (vwap) or no touch recorded (touch), not a
        # zero-cost execution.
        'slip_vwap': (lambda v: st.mean(v) if v else float('nan'))(have('slip_vs_vwap')),
        'slip_touch': (lambda v: st.mean(v) if v else float('nan'))(have('slip_vs_touch')),
        'buy_leg_s': st.mean(g('buy_ns')) / 1e9,
        'child_fills': st.mean(g('buy_fills')),
        'outcomes': by_outcome,
    }

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('rundir')
    ap.add_argument('--csv', help='also write the summary as CSV')
    a = ap.parse_args()

    grid = {}
    tsv = os.path.join(a.rundir, 'grid.tsv')
    if os.path.exists(tsv):
        for line in open(tsv):
            p = line.rstrip('\n').split('\t')
            if len(p) >= 8:
                grid[p[0]] = dict(grid=p[1], rate_bp=p[2], size=p[3],
                                  ord_sz=p[4], delay_us=p[5], max_dist=p[7])

    rows = []
    for cfg_dir in sorted(glob.glob(os.path.join(a.rundir, 'csv', '*'))):
        name = os.path.basename(cfg_dir)
        fires, total, by_outcome = load(cfg_dir)
        r = summarise(name, grid.get(name, {}), fires, total, by_outcome)
        if r:
            r['sessions'] = len(glob.glob(os.path.join(cfg_dir, '2025*.csv')))
            rows.append(r)

    if not rows:
        print('no completed configs yet', file=sys.stderr); return 1

    # Grid ids come from the data, not a hardcoded list. GRID_SET=2 emits B2 and
    # C2, which ('A','B','C','D') silently skipped -- the aggregator printed
    # nothing at all for that run and looked like it had no data, when in fact
    # it had thousands of fires. Sorted so the ordering is stable across runs.
    for gid in sorted({r.get('grid') for r in rows if r.get('grid')}):
        sel = [r for r in rows if r.get('grid') == gid]
        if not sel: continue
        print(f"\n=== grid {gid} " + "=" * 96)
        print(f"{'config':<16}{'sess':>5}{'fires':>7}{'compl':>7}"
              f"{'slip_paired':>14}{'drift':>14}{'vs_vwap':>10}{'vs_touch':>10}{'part_med':>10}"
              f"{'p10-p90':>16}{'part_agg':>10}{'leg_s':>8}{'fills':>7}")
        for r in sorted(sel, key=lambda x: x['config']):
            print(f"{r['config']:<16}{r['sessions']:>5}{r['fires_ok']:>7}"
                  f"{r['completion']*100:>6.1f}%"
                  f"{r['slip_paired']:>+9.4f}±{r['slip_paired_ci']:<4.3f}"
                  f"{r['drift']:>+9.4f}±{r['drift_ci']:<4.3f}"
                  f"{r['slip_vwap']:>+10.4f}{r['slip_touch']:>+10.4f}"
                  f"{r['part_median']:>10.5f}"
                  f"{r['part_p10']:>8.5f}-{r['part_p90']:<7.5f}"
                  f"{r['part_aggregate']:>10.5f}{r['buy_leg_s']:>8.1f}"
                  f"{r['child_fills']:>7.1f}")

    # Two different survivorship signals, and BOTH are needed now.
    #
    # A low ok-rate still means rows were thrown away. But a config whose legs
    # fill slowly no longer produces failed rows at all -- its windows just stay
    # open longer, so it produces FEWER of them, at a completion rate near 100%.
    # Flag any config yielding far fewer windows per session than its peers.
    low = [r for r in rows if r['completion'] < 0.9]
    if low:
        print("\n!! configs completing under 90% of fires -- their cost numbers "
              "are survivorship-biased and must be reported with the rate:")
        for r in sorted(low, key=lambda x: x['completion']):
            oc = ', '.join(f"{k}={v}" for k, v in sorted(r['outcomes'].items()) if k != 'ok')
            print(f"   {r['config']:<16} {r['completion']*100:5.1f}%   {oc}")

    wps = [(r['config'], r['fires_ok'] / r['sessions'], r['outcomes'])
           for r in rows if r.get('sessions')]
    if len(wps) > 2:
        med = st.median([w for _, w, _ in wps])
        thin = [(c, w, oc) for c, w, oc in wps if med > 0 and w < 0.5 * med]
        if thin:
            print("\n!! configs producing far fewer windows per session than the "
                  f"grid median ({med:.1f}) -- a leg that fills slowly holds its "
                  "window open, so this is the shape survivorship takes now:")
            for c, w, oc in sorted(thin, key=lambda x: x[1]):
                short = ', '.join(f"{k}={v}" for k, v in sorted(oc.items())
                                  if k.endswith('_short'))
                print(f"   {c:<16} {w:5.1f} windows/session   {short}")

    # The same configuration reached from two grids: if these disagree the sweep
    # is not deterministic and nothing else in it can be trusted.
    #
    # The pairs are DISCOVERED, not named. This check used to hardcode 'lat500'
    # and 'rate300_sz100'; when the axes moved to rate {50,100,200,400} and the
    # latency sweep dropped 500, neither cell existed any more and the check
    # silently stopped running -- and a check that never runs is indistinguish-
    # able from one that passes. Match on the parameters instead, so it follows
    # the axes wherever they go.
    def params_of(r):
        return (r.get('rate_bp'), r.get('size'), r.get('delay_us'),
                r.get('ord_sz'), r.get('max_dist'))

    seen, pairs = {}, []
    for r in rows:
        k = params_of(r)
        if None in k:                     # no grid.tsv metadata, nothing to match on
            continue
        if k in seen:
            pairs.append((seen[k], r))
        else:
            seen[k] = r

    if not pairs:
        print("\nconsistency check: no cell appears in two grids -- nothing to "
              "cross-check. This is NOT a pass.")
    for a_, b_ in pairs:
        d = a_['slip_paired'] - b_['slip_paired']
        print(f"\nconsistency check (identical parameters, two grids): "
              f"{a_['config']} {a_['slip_paired']:+.4f} vs "
              f"{b_['config']} {b_['slip_paired']:+.4f}  delta {d:+.4f}"
              + ("  OK" if abs(d) < 1e-6 else "  <-- MISMATCH"))

    if a.csv:
        keys = [k for k in rows[0] if k != 'outcomes']
        with open(a.csv, 'w', newline='') as fh:
            w = csv.DictWriter(fh, fieldnames=keys, extrasaction='ignore')
            w.writeheader(); w.writerows(rows)
        print(f"\nwrote {a.csv}")
    return 0

if __name__ == '__main__':
    sys.exit(main())
