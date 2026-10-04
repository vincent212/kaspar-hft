"""Charts for Section 6.2 (Cont-de Larrard), computed from the model's closed-form expressions:
Prop. 1 (duration until the next price change), Prop. 2 (probability of an up move, balanced case),
the heavy-traffic arctan form (Cont & de Larrard 2012) and the volatility scaling sigma ~ sqrt(rate/depth).
Rates are stylised: time is measured in units of the mean time between departures, 1/(r_M + r_C)."""
import numpy as np, matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from scipy import integrate, special
from pathlib import Path
OUT = Path(__file__).resolve().parents[1]
plt.rcParams.update({"font.size": 9, "axes.spines.top": False, "axes.spines.right": False})

def surv_one(n, r_in, r_out, t):
    """P(a single queue of n orders has not emptied by time t): first-passage law of a random walk."""
    c = 2*np.sqrt(r_in*r_out)
    g = lambda u: (n/u) * special.ive(n, c*u) * np.exp(c*u - u*(r_in + r_out))
    return (r_out/r_in)**(n/2) * integrate.quad(g, t, np.inf, limit=500)[0]

def phi(n, p):
    """Prop. 2: probability that the next move is up, balanced case, n orders at bid, p at ask."""
    f = lambda t: (2-np.cos(t)-np.sqrt((2-np.cos(t))**2-1))**p * np.sin(n*t)*np.cos(t/2)/np.sin(t/2)
    return integrate.quad(f, 1e-12, np.pi, limit=500)[0]/np.pi

# 1. Waiting time until the next price change
t = np.logspace(-0.5, 3.5, 60)
fig, ax = plt.subplots(figsize=(5.4, 3.5))
for ratio, ls, lab in [(1.0, "-", r"balanced, $r_L = r_M + r_C$"),
                       (0.95, "--", r"$r_L = 0.95\,(r_M + r_C)$"),
                       (0.6, ":", r"$r_L = 0.6\,(r_M + r_C)$")]:
    S = [surv_one(3, ratio, 1.0, x)**2 for x in t]          # two independent queues of 3 orders
    ax.loglog(t, S, ls, color="C0", label=lab)
    if ratio < 1:
        ustar = 1/(1 - np.sqrt(ratio))**2                     # time scale of the exponential cut-off
        ax.axvline(ustar, color="C0", ls=ls, lw=0.6, alpha=0.6)
ax.loglog(t, 0.9/t, color="0.6", lw=0.8); ax.text(1500, 0.9/1500*1.4, r"$\propto 1/u$", fontsize=8, color="0.4")
ax.set_ylim(1e-5, 1.2)
ax.set_xlabel(r"time $u$ (units of $1/(r_M + r_C)$)")
ax.set_ylabel("P(no price change by time $u$)")
ax.legend(fontsize=7, frameon=False, loc="lower left")
ax.set_title("3 orders at the bid and 3 at the ask; thin vertical lines: cut-off time", fontsize=8)
fig.tight_layout(); fig.savefig(OUT / "cdl_duration.png", dpi=200); plt.close(fig)

# 2. Probability of an up move: exact (balanced) vs arctan, as a map over queue sizes
N = 15
P = np.array([[phi(n, p) for p in range(1, N+1)] for n in range(1, N+1)])
fig, axes = plt.subplots(1, 2, figsize=(7.2, 3.1))
im = axes[0].imshow(P, origin="lower", extent=[0.5, N+.5, 0.5, N+.5], cmap="RdBu_r", vmin=0, vmax=1)
cs = axes[0].contour(np.arange(1, N+1), np.arange(1, N+1), P, levels=[0.2, 0.35, 0.5, 0.65, 0.8], colors="k", linewidths=0.7)
axes[0].clabel(cs, fontsize=6)
axes[0].set_xlabel("orders at the best ask"); axes[0].set_ylabel("orders at the best bid")
axes[0].set_title("exact, balanced case: lines of equal\nprobability fan out from the origin", fontsize=8)
fig.colorbar(im, ax=axes[0], fraction=0.046, pad=0.03, label="P(next move up)")
for p, c in [(1, "C0"), (3, "C1"), (8, "C2")]:
    n = np.arange(1, N+1)
    axes[1].plot(n, [phi(k, p) for k in n], "o", color=c, ms=3.5, label=f"exact, ask = {p}")
    x = np.linspace(0.5, N, 200); axes[1].plot(x, 2/np.pi*np.arctan(x/p), "-", color=c, lw=1)
axes[1].set_xlabel("orders at the best bid"); axes[1].set_ylabel("P(next move up)"); axes[1].set_ylim(0, 1)
axes[1].set_title(r"dots: exact; lines: $(2/\pi)\arctan(\mathrm{bid}/\mathrm{ask})$", fontsize=8)
axes[1].legend(fontsize=6.5, frameon=False, loc="lower right")
fig.tight_layout(); fig.savefig(OUT / "cdl_pup.png", dpi=200); plt.close(fig)

# 3. Volatility from order flow: sigma proportional to sqrt(rate / depth)
fig, axes = plt.subplots(1, 2, figsize=(7.2, 2.7), sharey=True)
rate = np.linspace(0.1, 4, 200)
for D, c in [(4, "C0"), (16, "C1"), (64, "C2")]:
    axes[0].plot(rate, np.sqrt(rate/D)/np.sqrt(1/4), color=c, label=rf"$\Theta = {D}$")
axes[0].set_xlabel(r"order arrival rate $r_L$ (relative)"); axes[0].set_ylabel("volatility (relative)")
axes[0].legend(fontsize=7, frameon=False, title="depth after a move", title_fontsize=7)
D = np.linspace(1, 100, 200)
for r_, c in [(0.5, "C3"), (1, "C4"), (2, "C5")]:
    axes[1].plot(D, np.sqrt(r_/D)/np.sqrt(1/4), color=c, label=rf"$r_L = {r_}$")
axes[1].set_xlabel(r"depth after a price change, $\Theta$"); axes[1].legend(fontsize=7, frameon=False)
fig.tight_layout(); fig.savefig(OUT / "cdl_vol.png", dpi=200); plt.close(fig)
print("done")
