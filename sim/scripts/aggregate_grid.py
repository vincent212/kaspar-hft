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

  * Filter on outcome == ok. A leg that never filled carries vwap = 0 and
    mid_sell = 0; 38 such rows in 3,056 moved a mean by four orders of
    magnitude and once made the result read as risk-free profit.
  * Participation is reported as a MEDIAN with percentiles, plus the aggregate
    (total filled / total volume). The mean of per-fire ratios is dominated by
    fires with tiny denominators and reverses the sign of the size effect.
  * Completion rate is always shown. Deep or slow configs finish fewer fires,
    and the ones that finish are the easy ones -- a cost that looks good next
    to a 25% completion rate is survivorship, not skill.
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
                    fires.append({k: float(row[k]) for k in (
                        'slip_buy_ticks', 'slip_sel_ticks', 'slip_paired_ticks',
                        'slip_legsum_ticks', 'buy_filled', 'sel_filled',
                        'buy_leg_mkt_vol', 'sel_leg_mkt_vol',
                        'buy_part', 'sel_part', 'buy_ns', 'sel_ns',
                        'buy_fills', 'parent_sz')})
                except (KeyError, ValueError):
                    continue
    return fires, total, by_outcome

def summarise(name, params, fires, total, by_outcome):
    if not fires:
        return None
    g = lambda k: [f[k] for f in fires]
    paired, legsum = g('slip_paired_ticks'), g('slip_legsum_ticks')
    bpart = [f['buy_part'] for f in fires if f['buy_part'] > 0]
    agg_num = sum(f['buy_filled'] for f in fires)
    agg_den = sum(f['buy_leg_mkt_vol'] for f in fires)
    return {
        'config': name, **params,
        'sessions': len({0}),  # filled in by caller
        'fires_ok': len(fires), 'fires_total': total,
        'completion': len(fires) / total if total else 0.0,
        'slip_paired': st.mean(paired), 'slip_paired_ci': ci95(paired),
        'slip_legsum': st.mean(legsum), 'slip_legsum_ci': ci95(legsum),
        'slip_buy': st.mean(g('slip_buy_ticks')),
        'slip_sel': st.mean(g('slip_sel_ticks')),
        'part_median': pct(bpart, 0.5), 'part_p10': pct(bpart, 0.1),
        'part_p90': pct(bpart, 0.9),
        'part_aggregate': agg_num / agg_den if agg_den else float('nan'),
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

    for gid in ('A', 'B', 'C', 'D'):
        sel = [r for r in rows if r.get('grid') == gid]
        if not sel: continue
        print(f"\n=== grid {gid} " + "=" * 96)
        print(f"{'config':<16}{'sess':>5}{'fires':>7}{'compl':>7}"
              f"{'slip_paired':>14}{'slip_legsum':>14}{'part_med':>10}"
              f"{'p10-p90':>16}{'part_agg':>10}{'leg_s':>8}{'fills':>7}")
        for r in sorted(sel, key=lambda x: x['config']):
            print(f"{r['config']:<16}{r['sessions']:>5}{r['fires_ok']:>7}"
                  f"{r['completion']*100:>6.1f}%"
                  f"{r['slip_paired']:>+9.4f}±{r['slip_paired_ci']:<4.3f}"
                  f"{r['slip_legsum']:>+9.4f}±{r['slip_legsum_ci']:<4.3f}"
                  f"{r['part_median']:>10.5f}"
                  f"{r['part_p10']:>8.5f}-{r['part_p90']:<7.5f}"
                  f"{r['part_aggregate']:>10.5f}{r['buy_leg_s']:>8.1f}"
                  f"{r['child_fills']:>7.1f}")

    low = [r for r in rows if r['completion'] < 0.9]
    if low:
        print("\n!! configs completing under 90% of fires -- their cost numbers "
              "are survivorship-biased and must be reported with the rate:")
        for r in sorted(low, key=lambda x: x['completion']):
            oc = ', '.join(f"{k}={v}" for k, v in sorted(r['outcomes'].items()) if k != 'ok')
            print(f"   {r['config']:<16} {r['completion']*100:5.1f}%   {oc}")

    # the same configuration reached from two grids: if these disagree the
    # sweep is not deterministic and nothing else in it can be trusted
    x = {r['config']: r for r in rows}
    if 'lat500' in x and 'rate300_sz100' in x:
        d = x['lat500']['slip_paired'] - x['rate300_sz100']['slip_paired']
        print(f"\nconsistency check (same config via grids A and C): "
              f"lat500 {x['lat500']['slip_paired']:+.4f} vs "
              f"rate300_sz100 {x['rate300_sz100']['slip_paired']:+.4f}  "
              f"delta {d:+.4f}" + ("  OK" if abs(d) < 1e-6 else "  <-- MISMATCH"))

    if a.csv:
        keys = [k for k in rows[0] if k != 'outcomes']
        with open(a.csv, 'w', newline='') as fh:
            w = csv.DictWriter(fh, fieldnames=keys, extrasaction='ignore')
            w.writeheader(); w.writerows(rows)
        print(f"\nwrote {a.csv}")
    return 0

if __name__ == '__main__':
    sys.exit(main())
