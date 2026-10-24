#!/usr/bin/env python3
# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
#
# Licensed under the MIT License. See LICENSE file in the project root.
"""Slippage report for one or more sim runs: one row per trading day, plus the total.

    ./slippage_report.py probe.csv [probe2.csv ...] [--csv report.csv]

Reads the probe.csv files the simulator writes (--probe-out) and applies the
same rules as aggregate_grid.py, by calling its parse_row() and summarise(): only
windows with outcome == ok are costed, participation is ours / (ours + market),
and windows where nothing else traded are left out of participation.

All slippage is in TICKS of the contract, signed so that positive is a cost:
  slip_paired  (buy_vwap - sel_vwap) / 2: half the spread captured, negative
               = we earned it
  slip_buy     buy_vwap - mid at the window's start
  slip_sel     mid at the window's start - sel_vwap
  drift        where the mid went over the window
  vs_vwap      against the market VWAP over each leg
  vs_touch     against crossing the spread at the window's start
"""
import argparse, collections, csv, datetime, math, os, re, sys
from zoneinfo import ZoneInfo

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from aggregate_grid import parse_row, summarise   # noqa: E402

ET = ZoneInfo('America/New_York')


def day_of(row):
    return datetime.datetime.fromtimestamp(int(row['fire_ts']) / 1e9, ET).strftime('%Y-%m-%d %a')


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('probe_csv', nargs='+', help='probe.csv from one or more runs')
    ap.add_argument('--csv', help='also write the report as CSV')
    a = ap.parse_args()

    # day -> (completed fires, total rows, outcome counts)
    days = collections.defaultdict(lambda: ([], [0], collections.Counter()))
    sym = None
    for path in a.probe_csv:
        with open(path) as fh:
            for row in csv.DictReader(fh):
                sym = sym or row.get('sym')
                fires, total, oc = days[day_of(row)]
                total[0] += 1
                oc[row.get('outcome', '?')] += 1
                if row.get('outcome') == 'ok':
                    f = parse_row(row)
                    if f is not None:
                        fires.append(f)
    if not days:
        print('no rows in ' + ', '.join(a.probe_csv), file=sys.stderr)
        return 1

    rows = []
    for d in sorted(days):
        fires, total, oc = days[d]
        r = summarise(d, {}, fires, total[0], dict(oc))
        rows.append(r if r else {'config': d, 'fires_ok': 0, 'fires_total': total[0],
                                 'outcomes': dict(oc)})
    all_fires = [f for d in days for f in days[d][0]]
    all_oc = collections.Counter()
    for d in days:
        all_oc.update(days[d][2])
    total = summarise('TOTAL', {}, all_fires, sum(days[d][1][0] for d in days), dict(all_oc))

    def fmt(v, spec):
        """format(v, spec), or a '-' of the same width and alignment for NaN."""
        if isinstance(v, (int, float)) and not math.isnan(v):
            return format(v, spec)
        m = re.match(r'([<>^]?)[+-]?(\d*)', spec)
        return format('-', (m.group(1) or '>') + m.group(2))

    parent = int(all_fires[0]['parent_sz']) if all_fires else 0
    print(f"slippage report: {sym}, parent {parent} lots per leg, ticks (positive = cost)\n")
    hdr = (f"{'day':<16}{'ok/all':>8}{'slip_paired':>16}{'slip_buy':>10}{'slip_sel':>10}"
           f"{'drift':>9}{'vs_vwap':>9}{'vs_touch':>10}{'part_med':>10}{'part_agg':>10}"
           f"{'leg_s':>7}{'fills':>7}")
    print(hdr)
    print('-' * len(hdr))
    for r in rows + ([total] if total else []):
        if r is total:
            print('-' * len(hdr))
        if 'slip_paired' not in r:
            print(f"{r['config']:<16}{r['fires_ok']:>3}/{r['fires_total']:<4}  (no completed window)")
            continue
        print(f"{r['config']:<16}{r['fires_ok']:>3}/{r['fires_total']:<4}"
              f"{fmt(r['slip_paired'], '+8.3f')}±{fmt(r['slip_paired_ci'], '<6.3f')}"
              f"{fmt(r['slip_buy'], '>+10.3f')}{fmt(r['slip_sel'], '>+10.3f')}"
              f"{fmt(r['drift'], '>+9.3f')}{fmt(r['slip_vwap'], '>+9.3f')}"
              f"{fmt(r['slip_touch'], '>+10.3f')}"
              f"{fmt(r['part_median'] * 100, '>9.3f')}%{fmt(r['part_aggregate'] * 100, '>9.3f')}%"
              f"{r['buy_leg_s']:>7.1f}{r['child_fills']:>7.1f}")

    bad = {k: v for k, v in all_oc.items() if k != 'ok'}
    if bad:
        print('\nnot costed (leg did not fill before the data ended): '
              + ', '.join(f'{k}={v}' for k, v in sorted(bad.items())))

    if a.csv:
        keys = ['config', 'fires_ok', 'fires_total', 'slip_paired', 'slip_paired_ci', 'slip_buy',
                'slip_sel', 'drift', 'slip_vwap', 'slip_touch', 'part_median', 'part_p10',
                'part_p90', 'part_aggregate', 'buy_leg_s', 'child_fills']
        with open(a.csv, 'w', newline='') as fh:
            w = csv.writer(fh)
            w.writerow(['day' if k == 'config' else k for k in keys])
            for r in rows + ([total] if total else []):
                w.writerow([r.get(k, '') for k in keys])
        print(f'\nwrote {a.csv}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
