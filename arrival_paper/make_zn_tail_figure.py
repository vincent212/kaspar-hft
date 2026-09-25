import matplotlib; matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.ticker import FixedLocator, FixedFormatter, NullFormatter
SURF="#fcfcfb"; INK="#0b0b0b"; INK2="#52514e"; GRID="#e6e5e2"; C1="#2a78d6"; C2="#eb6834"
pcts=["p10","p50","p90","p99","p99.9"]
allx=[0,1,2,3,4]; ally=[5.04,7.11,11.81,63.30,273.69]
hotx=[0,1,3,4];   hoty=[5.09,6.99,15.46,26.37]   # p90 not reported for the hot path
plt.rcParams.update({"font.family":"serif","font.size":9,"axes.edgecolor":INK2,
  "axes.labelcolor":INK2,"xtick.color":INK2,"ytick.color":INK2,"text.color":INK})
fig,ax=plt.subplots(figsize=(4.6,3.0),facecolor=SURF); ax.set_facecolor(SURF)
ax.set_yscale("log"); ax.yaxis.grid(True,color=GRID,linewidth=1,linestyle="-",zorder=0); ax.set_axisbelow(True)
for s in ("top","right"): ax.spines[s].set_visible(False)
for s in ("left","bottom"): ax.spines[s].set_linewidth(0.8)
kw=dict(linewidth=2,solid_joinstyle="round",solid_capstyle="round",marker="o",markersize=8,markeredgecolor=SURF,markeredgewidth=2,zorder=3)
ax.plot(allx,ally,color=C1,markerfacecolor=C1,label="all messages",**kw)
ax.plot(hotx,hoty,color=C2,markerfacecolor=C2,label="empty queue, first in packet",**kw)
ax.annotate("all messages",(allx[-1],ally[-1]),xytext=(7,0),textcoords="offset points",va="center",fontsize=8.5,color=INK2)
ax.annotate("empty queue,\nfirst in packet",(hotx[-1],hoty[-1]),xytext=(7,0),textcoords="offset points",va="center",fontsize=8.5,color=INK2)
ax.set_xticks(allx); ax.set_xticklabels(pcts); ax.set_xlim(-0.35,len(pcts)-1+1.6)
yt=[5,10,20,50,100,200,500]
ax.yaxis.set_major_locator(FixedLocator(yt)); ax.yaxis.set_major_formatter(FixedFormatter([str(t) for t in yt]))
ax.yaxis.set_minor_formatter(NullFormatter()); ax.tick_params(which="minor",length=0); ax.tick_params(which="major",length=3,width=0.8)
ax.set_ylim(4,600); ax.set_title("ZN book, serial decode",loc="left",fontsize=10,color=INK,pad=6)
ax.set_xlabel("percentile",color=INK2); ax.set_ylabel("wire-to-book latency (µs, log)",color=INK2)
ax.legend(loc="upper left",frameon=False,fontsize=8.5,labelcolor=INK2,handlelength=1.6)
fig.tight_layout()
out="/Users/vm/kaspar-hft/arrival_paper/figs/zn_serial_tail"
fig.savefig(out+".pdf",facecolor=SURF); fig.savefig(out+".png",dpi=200,facecolor=SURF); print("wrote",out)
