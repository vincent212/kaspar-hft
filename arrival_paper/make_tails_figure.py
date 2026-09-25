# Copyright (c) 2026 Vincent Mayeski / M2 Tech.  Licensed under the MIT License.
"""Section 8.3 figure: serial wire-to-book latency by percentile, six live streams.
Data: tech_reports/serial_vs_parallel_decode.md, section 2 (serial rows), one
900 s window per stream, recovery excluded. p10..p99.9; the report has no p1."""
import matplotlib; matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.ticker import FixedLocator, FixedFormatter, NullFormatter
SURF="#fcfcfb"; INK="#0b0b0b"; INK2="#52514e"; GRID="#e6e5e2"
COL={"ES":"#2a78d6","NQ":"#eb6834","ZN":"#1baf7a"}
pcts=["p10","p50","p90","p99","p99.9"]; x=list(range(5))
book ={"ES":[4.65,6.65,10.47,19.10,39.25],"NQ":[4.29,6.36,8.85,12.35,18.94],"ZN":[5.04,7.11,11.81,63.30,273.69]}
trade={"ES":[5.76,8.87,16.11,33.62,67.65],"NQ":[4.96,7.33,11.10,22.15,42.47],"ZN":[6.77,17.19,95.61,268.04,310.13]}
plt.rcParams.update({"font.family":"serif","font.size":9,"axes.edgecolor":INK2,"axes.labelcolor":INK2,
                     "xtick.color":INK2,"ytick.color":INK2,"text.color":INK})
fig,axes=plt.subplots(1,2,figsize=(6.6,3.0),sharey=True,facecolor=SURF)
yt=[5,10,20,50,100,200,500]
for ax,(name,d) in zip(axes,(("book streams",book),("trade streams",trade))):
    ax.set_facecolor(SURF); ax.set_yscale("log")
    ax.yaxis.grid(True,color=GRID,linewidth=1,linestyle="-",zorder=0); ax.set_axisbelow(True)
    for s in ("top","right"): ax.spines[s].set_visible(False)
    for s in ("left","bottom"): ax.spines[s].set_linewidth(0.8)
    # end-labels: order by last value; nudge apart with leader offsets if within a factor 1.25
    last=sorted(((d[k][-1],k) for k in d))
    offs={}; prev=None
    for v,k in last:
        offs[k]=0
        if prev is not None and v/prev[0]<1.25: offs[k]=offs[prev[1]]+9
        prev=(v,k)
    for k in ("ES","NQ","ZN"):
        y=d[k]
        ax.plot(x,y,color=COL[k],linewidth=2,solid_joinstyle="round",solid_capstyle="round",
                marker="o",markersize=8,markerfacecolor=COL[k],markeredgecolor=SURF,markeredgewidth=2,label=k,zorder=3)
        ax.annotate(k,(x[-1],y[-1]),xytext=(8,offs[k]),textcoords="offset points",va="center",ha="left",
                    fontsize=8.5,color=INK2,arrowprops=dict(arrowstyle="-",color=GRID,lw=0.8) if offs[k] else None)
    ax.set_xticks(x); ax.set_xticklabels(pcts); ax.set_xlim(-0.35,4+1.0)
    ax.yaxis.set_major_locator(FixedLocator(yt)); ax.yaxis.set_major_formatter(FixedFormatter([str(t) for t in yt]))
    ax.yaxis.set_minor_formatter(NullFormatter()); ax.tick_params(which="minor",length=0); ax.tick_params(which="major",length=3,width=0.8)
    ax.set_ylim(4,600); ax.set_title(name,loc="left",fontsize=10,color=INK,pad=6); ax.set_xlabel("percentile",color=INK2)
axes[0].set_ylabel("wire-to-book latency (µs, log)",color=INK2)
axes[0].legend(loc="upper left",frameon=False,fontsize=8.5,labelcolor=INK2,handlelength=1.6)
fig.tight_layout(w_pad=2.0)
out="/Users/vm/kaspar-hft/arrival_paper/figs/tails_six_streams"
fig.savefig(out+".pdf",facecolor=SURF); fig.savefig(out+".png",dpi=200,facecolor=SURF); print("wrote",out)
