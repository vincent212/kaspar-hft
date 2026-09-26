"""Composition of near-simultaneous transaction pairs against loose pairs (objections.md TODO 3).

Transactions = distinct transactTime values on the NQ-front --eoe tape. For each transaction: has a trade,
actions present (0 new, 1 change, 2 delete), sides, price levels touched. Consecutive pairs are binned by
their gap on the engine clock (transactTime) and, separately, on the gateway clock (first sendingTime):
tight < 16 us, mid 16-100 us, loose 100 us - 1 ms. Reported per bin: pair types (first has trade -> second
is trade / delete / new / change), same side, same price level, and the share of second transactions that
touch a price level the first touched.

    python3 -m arrival_paper.tight_pairs --tapes .../message_eoe --n 20
"""
import argparse, glob, os, numpy as np, pandas as pd
from multiprocessing import Pool

def kind(g):
    if (g.typ == 'T').any(): return 'trade'
    a = set(g.action.dropna().astype(str))
    if '2' in a: return 'delete'
    if '0' in a: return 'new'
    if '1' in a: return 'change'
    return 'other'

def one(path):
    d = pd.read_csv(path, usecols=['transactTime', 'sendingTime', 'typ', 'action', 'side', 'pxd'], dtype={'action': str, 'side': str})
    d = d[d.transactTime > 0]
    if len(d) < 100_000: return None
    d['trade'] = d.typ == 'T'; d['del'] = d.action == '2'; d['new'] = d.action == '0'; d['chg'] = d.action == '1'
    g = d.groupby('transactTime', sort=True)
    tx = pd.DataFrame({'t_st': g.sendingTime.min(), 'trade': g.trade.any(), 'dl': g['del'].any(), 'nw': g.new.any(), 'ch': g.chg.any(),
                       'side': g.side.agg(lambda s: s.dropna().iloc[0] if s.notna().any() else ''),
                       'px': g.pxd.agg(lambda s: s.dropna().iloc[0] if s.notna().any() else np.nan)}).reset_index()
    tx['kind'] = np.select([tx.trade, tx.dl, tx.nw, tx.ch], ['trade', 'delete', 'new', 'change'], 'other')
    a, b = tx.iloc[:-1].reset_index(drop=True), tx.iloc[1:].reset_index(drop=True)
    gap_eng = (b.transactTime - a.transactTime).values; gap_gw = (b.t_st - a.t_st).values
    pair = pd.DataFrame({'k1': a.kind, 'k2': b.kind, 'same_side': (a.side == b.side) & (a.side != ''),
                         'same_px': np.isclose(a.px, b.px) & a.px.notna(), 'gap_eng': gap_eng, 'gap_gw': gap_gw})
    return pair

def summarise(p, col, lab):
    bins = [(-1, 16_000, 'tight <16us'), (16_000, 100_000, 'mid 16-100us'), (100_000, 1_000_000, 'loose 0.1-1ms')]
    print(f"\n=== pairs binned by gap on the {lab} ===")
    rows = []
    for lo, hi, name in bins:
        q = p[(p[col] > lo) & (p[col] <= hi)]
        r = {'bin': name, 'n': len(q), 'share of pairs': len(q) / len(p) * 100}
        for k in ('trade', 'delete', 'new', 'change'):
            r[f'first={k}'] = (q.k1 == k).mean() * 100
        for k in ('trade', 'delete', 'new', 'change'):
            r[f'2nd={k}|1st=trade'] = (q[q.k1 == 'trade'].k2 == k).mean() * 100 if (q.k1 == 'trade').any() else np.nan
        r['same side'] = q.same_side.mean() * 100; r['same price'] = q.same_px.mean() * 100
        r['trade->trade'] = ((q.k1 == 'trade') & (q.k2 == 'trade')).mean() * 100
        r['trade->delete'] = ((q.k1 == 'trade') & (q.k2 == 'delete')).mean() * 100
        rows.append(r)
    print(pd.DataFrame(rows).set_index('bin').T.round(2).to_string())

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--tapes', required=True); ap.add_argument('--n', type=int, default=20); ap.add_argument('--jobs', type=int, default=10)
    a = ap.parse_args(); files = sorted(glob.glob(os.path.join(a.tapes, '*.csv'))); files = [f for f in files if os.path.getsize(f) > 50_000_000]
    files = files[::max(1, len(files) // a.n)][:a.n]
    with Pool(a.jobs) as pool: p = pd.concat([x for x in pool.map(one, files) if x is not None], ignore_index=True)
    print(f"{len(files)} sessions, {len(p):,} consecutive transaction pairs")
    summarise(p, 'gap_eng', 'engine clock (transactTime)'); summarise(p, 'gap_gw', 'gateway clock (sendingTime)')
main()
