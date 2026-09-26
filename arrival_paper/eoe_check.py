"""Check the transaction grouping against CME's end-of-event flag (objections.md, objection 2 / TODO 1).

On each sampled session's --eoe message tape (NQ front, RTH):
  * transactTime groups: messages sharing one transactTime;
  * EoE events: consecutive messages (tape order) up to and including one with eoe = 1;
  * how many transactTime groups carry exactly one / zero / several eoe = 1 messages (a group with zero
    means the event's last message was for another security or outside the filter);
  * how many transactTime groups straddle an EoE boundary;
  * block counts as in qsim_tx (overlapping transactTime ranges across packets) vs transactTime groups;
  * packets per transactTime group, and gaps between successive transactTime values (the matching-engine
    clock) vs gaps between successive block starts on sendingTime: share below 7.5 / 16 / 32 us.

    python3 -m arrival_paper.eoe_check --tapes /vast/home/vmayeski/out/arrival_paper/tapes/318/message_eoe --n 20
"""
import argparse, glob, os, numpy as np, pandas as pd
from multiprocessing import Pool

def one(path):
    d = pd.read_csv(path, usecols=['transactTime', 'sendingTime', 'packet_seq', 'eoe'])
    d = d[(d.transactTime > 0) & (d.sendingTime > 0)]
    if len(d) < 100_000:
        return None                      # holiday / half-day stub tape
    tt = d.transactTime.values; eoe = d.eoe.values; pk = d.packet_seq.values
    g = d.groupby('transactTime')
    n_eoe = g.eoe.sum(); n_pkt = g.packet_seq.nunique(); n_msg = g.size()
    # EoE event id in tape order
    ev = np.r_[0, np.cumsum(eoe[:-1])]
    straddle = pd.Series(ev).groupby(tt).nunique()
    ev_tt = pd.Series(tt).groupby(ev).nunique()
    # blocks as in qsim_tx: per packet min/max transactTime, chain overlaps
    p = d.groupby('packet_seq').agg(x0=('transactTime', 'min'), x1=('transactTime', 'max'), t=('sendingTime', 'min')).sort_values('t')
    x0 = p.x0.values; x1 = p.x1.values; blk = np.zeros(len(p), int); cur = x1[0]; k = 0
    for i in range(1, len(p)):
        if x0[i] <= cur: cur = max(cur, x1[i])
        else: k += 1; cur = x1[i]
        blk[i] = k
    first = np.r_[0, np.flatnonzero(np.diff(blk)) + 1]; bs = p.t.values[first]
    u = np.sort(np.unique(tt)); gtt = np.diff(u); gbs = np.diff(bs); gpk = np.diff(p.t.values)
    thr = np.array([7500, 16000, 32000])
    return dict(session=os.path.basename(path), n_msg=len(d), n_packets=len(p), n_tt=len(n_eoe), n_blocks=k + 1,
                n_eoe_events=int(eoe.sum()),
                tt_eoe1=int((n_eoe == 1).sum()), tt_eoe0=int((n_eoe == 0).sum()), tt_eoe2=int((n_eoe >= 2).sum()),
                tt_straddle=int((straddle > 1).sum()), ev_multi_tt=int((ev_tt > 1).sum()),
                tt_1pkt=int((n_pkt == 1).sum()), tt_2pkt=int((n_pkt == 2).sum()), tt_3pkt=int((n_pkt >= 3).sum()),
                tt_msg1=int((n_msg == 1).sum()),
                **{f'gap_tt_lt{t//1000}': float((gtt < t).mean()) for t in thr},
                **{f'gap_blk_lt{t//1000}': float((gbs < t).mean()) for t in thr},
                **{f'gap_pkt_lt{t//1000}': float((gpk < t).mean()) for t in thr},
                gap_tt_p1_us=float(np.quantile(gtt, .01) / 1e3), gap_blk_p1_us=float(np.quantile(gbs, .01) / 1e3))

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--tapes', required=True); ap.add_argument('--n', type=int, default=20); ap.add_argument('--jobs', type=int, default=10)
    a = ap.parse_args(); files = sorted(glob.glob(os.path.join(a.tapes, '*.csv')))
    files = [f for f in files if os.path.getsize(f) > 50_000_000]; step = max(1, len(files) // a.n); files = files[::step][:a.n]
    with Pool(a.jobs) as p: r = pd.DataFrame([x for x in p.map(one, files) if x])
    s = r.sum(numeric_only=True); ntt = s.n_tt
    print(f"{len(r)} sessions: {int(s.n_msg):,} messages, {int(s.n_packets):,} packets, {int(ntt):,} transactTime groups, "
          f"{int(s.n_blocks):,} blocks (qsim_tx grouping), {int(s.n_eoe_events):,} end-of-event flags")
    print(f"transactTime groups with exactly one EoE flag {s.tt_eoe1/ntt*100:.3f}%, none {s.tt_eoe0/ntt*100:.3f}%, several {s.tt_eoe2/ntt*100:.4f}%")
    print(f"transactTime groups straddling an EoE boundary {s.tt_straddle/ntt*100:.4f}%;  EoE events holding >1 transactTime {s.ev_multi_tt/max(s.n_eoe_events,1)*100:.4f}%")
    print(f"blocks / transactTime groups = {s.n_blocks/ntt:.4f}   packets / transactTime groups = {s.n_packets/ntt:.4f}")
    print(f"packets per transactTime group: 1 {s.tt_1pkt/ntt*100:.3f}%  2 {s.tt_2pkt/ntt*100:.3f}%  3+ {s.tt_3pkt/ntt*100:.4f}%;  one message {s.tt_msg1/ntt*100:.3f}%")
    m = r.median(numeric_only=True)
    print("gap shares (median session): below 7.5 / 16 / 32 us")
    for lab, c in (('between transactTime values (engine clock)', 'gap_tt'), ('between block starts (sendingTime)', 'gap_blk'), ('between packets (sendingTime)', 'gap_pkt')):
        print(f"  {lab:>45}: {m[c+'_lt7']*100:6.2f}%  {m[c+'_lt16']*100:6.2f}%  {m[c+'_lt32']*100:6.2f}%")
    print(f"p1 gap: transactTime {m.gap_tt_p1_us:.2f} us   block starts {m.gap_blk_p1_us:.2f} us")
    print("across sessions, 5th-95th percentile:")
    for c in ('gap_tt_lt7', 'gap_tt_lt16', 'gap_tt_lt32', 'gap_pkt_lt7', 'gap_pkt_lt16', 'gap_pkt_lt32', 'gap_tt_p1_us', 'gap_blk_p1_us'):
        v = r[c]; sc = 1 if c.endswith('_us') else 100
        print(f"  {c:>14}: median {v.median()*sc:.2f}  5-95% {v.quantile(.05)*sc:.2f}-{v.quantile(.95)*sc:.2f}  min {v.min()*sc:.2f} max {v.max()*sc:.2f}")
    r.to_csv(os.path.join(os.path.dirname(a.tapes.rstrip('/')), 'eoe_check_sessions.csv'), index=False)
main()
