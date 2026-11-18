import csv, glob, math, os
import numpy as np, matplotlib
matplotlib.use('Agg'); import matplotlib.pyplot as plt
D_OUT=os.path.dirname(os.path.abspath(__file__))

def cse(y,dy):
    m=~np.isnan(y); y,dy=y[m],dy[m]
    if len(y)<2: return math.nan
    g=np.unique(dy); mu=y.mean(); n=len(y)
    if len(g)<2: return y.std(ddof=1)/math.sqrt(n)
    return math.sqrt(sum((y[dy==k]-mu).sum()**2 for k in g)*len(g)/(len(g)-1))/n
def mk(pat):
    L={'1':[],'5':[],'30':[]}; D=[]
    for fn in glob.glob(pat):
        d=os.path.basename(fn).split('.')[0]
        for r in csv.DictReader(open(fn)):
            for h in ('1','5','30'):
                v=r.get(f'markout_{h}s'); L[h].append(float(v) if v else math.nan)
            D.append(d)
    D=np.array(D)
    return [(np.nanmean(np.array(L[h])), 1.96*cse(np.array(L[h]),D)) for h in ('1','5','30')]
def slip(pat):
    rows=[]
    for fn in glob.glob(pat):
        d=os.path.basename(fn).split('.')[0]
        for r in csv.DictReader(open(fn)):
            if r.get('outcome')=='ok': r['_day']=d; rows.append(r)
    day=np.array([r['_day'] for r in rows]); y=np.array([float(r['slip_paired_ticks']) for r in rows])
    return (y.mean(), 1.96*cse(y,day))

P_MK = mk('/vast/home/vmayeski/gridruns/mk_year_md3/mkt/rate500_sz100/*.csv')
A_MK = mk('/vast/home/vmayeski/gridruns/aggr_zero_mk/mkt/aggr200/*.csv')
P_SL = slip('/vast/home/vmayeski/gridruns/mk_year_md3/csv/rate500_sz100/*.csv')
A_SL = slip('/vast/home/vmayeski/gridruns/aggr_zero_mk/csv/aggr200/*.csv')
PC, AC = '#1f4e9c', '#b03030'; H=[1,5,30]

# FIGURE 1 -- mark-out, both arms
f,ax = plt.subplots(figsize=(6.0,3.4))
for lab,M,col,m in (('Shadow-PPOV (passive)',P_MK,PC,'o'),('Aggressive POV',A_MK,AC,'s')):
    ax.errorbar(H,[v[0] for v in M],yerr=[v[1] for v in M],fmt=m+'-',color=col,lw=1.7,ms=6,capsize=3,label=lab)
ax.set_xscale('log'); ax.set_xticks(H); ax.set_xticklabels(['1 s','5 s','30 s'])
ax.set_xlim(0.7,45); ax.set_ylim(0,0.14); ax.axhline(0,color='k',lw=.6)
ax.set_xlabel('horizon after the fill'); ax.set_ylabel('ticks per contract')
ax.legend(fontsize=8.5,frameon=False)
f.tight_layout(); f.savefig(f'{D_OUT}/markout_arms.pdf')

# FIGURE 2 -- slippage, both arms
f,ax = plt.subplots(figsize=(4.6,3.4))
ax.bar([0,1],[P_SL[0],A_SL[0]],0.5,yerr=[P_SL[1],A_SL[1]],capsize=4,color=[PC,AC],alpha=.85)
ax.set_xticks([0,1]); ax.set_xticklabels(['Shadow-PPOV\n(passive)','Aggressive\nPOV'],fontsize=9)
ax.set_ylim(0,0.14); ax.axhline(0,color='k',lw=.6); ax.set_ylabel('ticks per contract')
for i,v in enumerate([P_SL,A_SL]): ax.text(i,v[0]+v[1]+0.005,f'{v[0]:+.3f}',ha='center',fontsize=9)
f.tight_layout(); f.savefig(f'{D_OUT}/slippage_arms.pdf')

# FIGURE 3 -- mark-out against slippage, both arms
f,ax = plt.subplots(figsize=(6.4,3.4))
x=np.arange(2); w=0.2
for i,(lab,v,e,c) in enumerate([
    ('relative slippage', [P_SL[0],A_SL[0]],       [P_SL[1],A_SL[1]],       '#7f7f7f'),
    ('mark-out 1 s',  [P_MK[0][0],A_MK[0][0]], [P_MK[0][1],A_MK[0][1]], '#9ecae1'),
    ('mark-out 5 s',  [P_MK[1][0],A_MK[1][0]], [P_MK[1][1],A_MK[1][1]], '#4292c6'),
    ('mark-out 30 s', [P_MK[2][0],A_MK[2][0]], [P_MK[2][1],A_MK[2][1]], '#08519c')]):
    ax.bar(x+(i-1.5)*w, v, w, yerr=e, capsize=3, color=c, label=lab)
ax.set_xticks(x); ax.set_xticklabels(['Shadow-PPOV (passive)','Aggressive POV'],fontsize=9.5)
ax.set_ylim(0,0.15); ax.axhline(0,color='k',lw=.6); ax.set_ylabel('ticks per contract')
ax.legend(fontsize=8,frameon=False,ncol=2)
f.tight_layout(); f.savefig(f'{D_OUT}/markout_vs_slippage.pdf')
for n in ('markout_arms','slippage_arms','markout_vs_slippage'): print('wrote', n+'.pdf')

# FIGURE 5 -- relative slippage against latency, both styles (the Table 5 numbers).
# The zero-delay cells come from separate zero-latency runs: the latency grid has
# no pas_0/agg_0 of its own (pas_0 exists but is header-only).
LAT_US = [0, 500, 1000, 2500, 5000]
LP = [slip('/vast/home/vmayeski/gridruns/year_md3/csv/rate2000_sz100/*.csv')] + \
     [slip(f'/vast/home/vmayeski/gridruns/latency/pas_{d}/*.w.csv') for d in LAT_US[1:]]
LA = [slip('/vast/home/vmayeski/gridruns/aggr_zero_mk/csv/aggr100/*.csv')] + \
     [slip(f'/vast/home/vmayeski/gridruns/latency/agg_{d}/*.w.csv') for d in LAT_US[1:]]
X = [d/1000 for d in LAT_US]
f,ax = plt.subplots(figsize=(6.0,3.6))
for lab,S,col,m in (('Shadow-PPOV (passive)',LP,PC,'o'),('Aggressive POV',LA,AC,'s')):
    ax.errorbar(X,[v[0] for v in S],yerr=[v[1] for v in S],fmt=m+'-',color=col,
                lw=1.7,ms=6,capsize=3,label=lab)
ax.set_xticks(X); ax.set_xticklabels(['0','0.5','1','2.5','5'])
ax.set_xlim(-0.2,5.2); ax.set_ylim(0,0.33)
ax.set_xlabel('delay on order, cancel and feed (ms)')
ax.set_ylabel('relative slippage (ticks per contract)')
ax.legend(fontsize=8.5,frameon=False,loc='upper left')
f.tight_layout(); f.savefig(f'{D_OUT}/latency_slippage.pdf')
print('wrote latency_slippage.pdf')

# FIGURE 6 -- aggregate participation against latency. Aggregate means
# sum(ours)/sum(market) over every side of every window, not the mean of
# per-window ratios, which small denominators dominate.
def part(pat):
    bf=sf=bv=sv=0.0
    for fn in glob.glob(pat):
        for r in csv.DictReader(open(fn)):
            bf+=float(r['buy_filled']); sf+=float(r['sel_filled'])
            bv+=float(r['buy_leg_mkt_vol']); sv+=float(r['sel_leg_mkt_vol'])
    return (bf+sf)/(bv+sv)*100
PP = [part('/vast/home/vmayeski/gridruns/year_md3/csv/rate2000_sz100/*.csv')] + \
     [part(f'/vast/home/vmayeski/gridruns/latency/pas_{d}/*.w.csv') for d in LAT_US[1:]]
PA = [part('/vast/home/vmayeski/gridruns/aggr_zero_mk/csv/aggr100/*.csv')] + \
     [part(f'/vast/home/vmayeski/gridruns/latency/agg_{d}/*.w.csv') for d in LAT_US[1:]]
f,ax = plt.subplots(figsize=(6.0,3.6))
for lab,S,col,m in (('Shadow-PPOV (passive)',PP,PC,'o'),('Aggressive POV',PA,AC,'s')):
    ax.plot(X,S,m+'-',color=col,lw=1.7,ms=6,label=lab)
ax.set_xticks(X); ax.set_xticklabels(['0','0.5','1','2.5','5'])
ax.set_xlim(-0.2,5.2); ax.set_ylim(0,7.6)
ax.set_xlabel('delay on order, cancel and feed (ms)')
ax.set_ylabel('aggregate participation (%)')
ax.legend(fontsize=8.5,frameon=False,loc='center right')
f.tight_layout(); f.savefig(f'{D_OUT}/latency_participation.pdf')
print('wrote latency_participation.pdf', [round(v,2) for v in PP], [round(v,2) for v in PA])
