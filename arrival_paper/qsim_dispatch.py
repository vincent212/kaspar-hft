"""Dispatch with its costs charged, against the tandem (objections.md objection 3 / TODO 7).

Per window, real packet arrivals, service constant (T) or span-dependent at the NQ slope
(S_i = T (1 + r (sigma_i - 1)), r = 0.312/7.23):
  tandem  : N stages of S_i/N joined by N-1 hops h (qsim_span.tandem_var);
  mdn     : one queue, N whole-packet servers, no hops, no resequencing (the uncharged bound of 4.4);
  disp    : ingress stage (service s_in) -> hop h -> N least-loaded whole-packet servers -> hop h ->
            in-order resequencer (qsim_marked.dispatch_full); resequencing wait reported separately.
h = 1.7 us throughout; s_in in {0, 0.5 us}.

    python3 -m arrival_paper.qsim_dispatch --cache arrival_paper/figs/qsim_cache --spans arrival_paper/figs/qsim_spans_tr \
        --out-dir arrival_paper/figs/qsim_grid_dispatch
"""
import argparse, os, sys, time, numpy as np, pandas as pd
from multiprocessing import Pool
from numba import njit
from arrival_paper.qsim_span import tandem_var
from arrival_paper.qsim_marked import dispatch_full
TS = [8, 16, 32, 64, 128]; NS = [2, 4, 8]; H = 1700.0; R = 0.312 / 7.23; SIN = [0.0, 500.0]

@njit(cache=True)
def mdn_var(arr, svc, N):
    n = arr.shape[0]; base = arr[0]; lat = np.empty(n); free = np.zeros(N)
    for i in range(n):
        a = float(arr[i] - base); j = 0; fm = free[0]
        for k in range(1, N):
            if free[k] < fm: fm = free[k]; j = k
        st = a if a > fm else fm; free[j] = st + svc[i]; lat[i] = free[j] - a
    return lat

def q(x): return float(np.quantile(x, .5)) / 1e3, float(np.quantile(x, .99)) / 1e3

def one(args):
    cache, spans, session = args; out = []
    za = np.load(f'{cache}/arrivals/{session}.npz'); zs = np.load(f'{spans}/{session}.npz')
    for k in zs.files:
        if not k.endswith('_S'): continue
        w = k[:-2]; t = za[w + '_H']; S = zs[k].astype(np.float64)
        if len(t) != len(S): continue
        row = dict(session=session, window_id=int(w[1:]))
        for T in TS:
            Tn = T * 1000.0
            for model, svc in (('const', np.full(len(t), Tn)), ('span', Tn * (1 + R * (S - 1)))):
                for N in NS:
                    tag = f'T{T}_{model}_N{N}'
                    row[f'{tag}_tandem_p50'], row[f'{tag}_tandem_p99'] = q(tandem_var(t, svc, N, int(H)))
                    row[f'{tag}_mdn_p50'], row[f'{tag}_mdn_p99'] = q(mdn_var(t, svc, N))
                    for s_in in SIN:
                        lat, rq = dispatch_full(t, svc, N, s_in, H, H)
                        tg = f'{tag}_disp{int(s_in)}'
                        row[f'{tg}_p50'], row[f'{tg}_p99'] = q(lat)
                        row[f'{tg}_reseq_p99'] = float(np.quantile(rq, .99)) / 1e3; row[f'{tg}_reseq_share'] = float((rq > 0).mean())
                row[f'T{T}_{model}_N1_p50'], row[f'T{T}_{model}_N1_p99'] = q(tandem_var(t, svc, 1, int(H)))
        out.append(row)
    return out

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--cache', required=True); ap.add_argument('--spans', required=True); ap.add_argument('--out-dir', required=True)
    ap.add_argument('--jobs', type=int, default=20); ap.add_argument('--sessions', type=int, default=0); a = ap.parse_args()
    meta = pd.read_parquet(f'{a.cache}/metadata.parquet'); sess = list(meta.session.unique())
    if a.sessions: sess = sess[::max(1, len(sess) // a.sessions)][:a.sessions]
    _ = mdn_var(np.array([0, 100, 200], dtype=np.int64), np.full(3, 100.0), 2)
    t0 = time.time(); rows = []
    with Pool(a.jobs) as p:
        for i, rs in enumerate(p.imap_unordered(one, [(a.cache, a.spans, s) for s in sess])):
            rows.extend(rs)
            if (i + 1) % 25 == 0: print(f'[disp] {i+1}/{len(sess)} {time.time()-t0:.0f}s', file=sys.stderr)
    d = pd.DataFrame(rows); os.makedirs(a.out_dir, exist_ok=True); d.to_parquet(os.path.join(a.out_dir, 'qsim_grid_dispatch.parquet'))
    m = d.median(numeric_only=True)
    for model in ('const', 'span'):
        print(f"\n== {model} service: corpus-median p50 / p99 (us), h = 1.7 us, {len(d)} windows ==")
        print(f"{'T':>4} {'N':>2} | {'single':>13} | {'tandem':>13} | {'M/D/N uncharged':>15} | {'dispatch s_in=0':>15} {'reseq p99':>9} {'reseq>0':>7} | {'dispatch s_in=0.5':>17}")
        for T in TS:
            for N in NS:
                g = f'T{T}_{model}_N{N}'
                print(f"{T:>4} {N:>2} | {m[f'T{T}_{model}_N1_p50']:>6.2f}/{m[f'T{T}_{model}_N1_p99']:<6.2f} | {m[g+'_tandem_p50']:>6.2f}/{m[g+'_tandem_p99']:<6.2f} | "
                      f"{m[g+'_mdn_p50']:>7.2f}/{m[g+'_mdn_p99']:<7.2f} | {m[g+'_disp0_p50']:>7.2f}/{m[g+'_disp0_p99']:<7.2f} {m[g+'_disp0_reseq_p99']:>9.2f} {m[g+'_disp0_reseq_share']*100:>6.2f}% | "
                      f"{m[g+'_disp500_p50']:>8.2f}/{m[g+'_disp500_p99']:<8.2f}")
main()
