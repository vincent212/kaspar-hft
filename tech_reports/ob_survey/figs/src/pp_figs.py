"""Point-process figures for Section 7 (redrawn in the survey's notation: branching ratio Gamma,
decay rate omega, Fano factor D, window length w). Exponential-kernel Hawkes processes are simulated
by Ogata thinning; all figures are stylised simulations."""
import numpy as np, matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from pathlib import Path
OUT = Path(__file__).resolve().parents[1]
plt.rcParams.update({"font.size": 8})

def hawkes(mu, Gamma, omega, T, rng, burn=None, full=False):
    """Exponential kernel phi(u) = alpha e^{-omega u}, alpha = Gamma*omega. Returns event times in [0, T].
    A burn-in period is simulated first and discarded, so the process starts in its stationary regime."""
    if burn is None: burn = 50.0/(omega*(1-Gamma))
    T = T + burn
    alpha = Gamma*omega; t = 0.0; excit = 0.0; ev = []
    while True:
        lam_bar = mu + excit                      # intensity only decays until next event
        w = rng.exponential(1/lam_bar); t += w; excit *= np.exp(-omega*w)
        if t > T: break
        if rng.random() <= (mu + excit)/lam_bar:
            ev.append(t); excit += alpha
    ev = np.array(ev) - burn
    return (ev[ev >= 0], ev) if full else ev[ev >= 0]

def intensity_path(ev, mu, Gamma, omega, T, n=3000):
    tt = np.linspace(0, T, n); lam = np.full(n, mu)
    for e in ev[ev > -50.0/omega]:                                 # includes burn-in history (negative times)
        m = tt >= e; lam[m] += Gamma*omega*np.exp(-omega*(tt[m]-e))
    return tt, lam

def fano(ev, T, w):
    k = int(T//w); c = np.histogram(ev, bins=k, range=(0, k*w))[0]
    return c.var(ddof=1)/c.mean(), c

# ---------- Figure: 3x3 grid
rng = np.random.default_rng(1)
T, omega = 15.0, 1.0
lbars, Gammas = [2, 10, 50], [0.30, 0.60, 0.90]
fig = plt.figure(figsize=(7.2, 10.0))
outer = fig.add_gridspec(3, 3, hspace=0.25, wspace=0.28)
for r, lb in enumerate(lbars):
    for c, G in enumerate(Gammas):
        mu = lb*(1-G)
        ev, ev_all = hawkes(mu, G, omega, T, rng, full=True)
        inner = outer[r, c].subgridspec(3, 1, hspace=0.12, height_ratios=[0.6, 1, 1])
        a0, a1, a2 = (fig.add_subplot(inner[k]) for k in range(3))
        a0.vlines(ev, 0, 1, color="C0", lw=0.5); a0.set_yticks([]); a0.set_xlim(0, T); a0.set_xticklabels([])
        a1.step(np.r_[0, ev], np.arange(len(ev)+1), where="post", color="C1", lw=0.9)
        a1.set_xlim(0, T); a1.set_xticklabels([]); a1.set_ylabel(r"$N(t)$", fontsize=7)
        tt, lam = intensity_path(ev_all, mu, G, omega, T)
        a2.plot(tt, lam, color="C2", lw=0.8); a2.set_xlim(0, T); a2.set_ylabel(r"$\lambda(t)$", fontsize=7)
        for a in (a1, a2): a.tick_params(labelsize=6)
        if r == 2: a2.set_xlabel("time (s)", fontsize=7)
        else: a2.set_xticklabels([])
        if r == 0: a0.set_title(rf"$\Gamma = {G:.2f}$", fontsize=9)
        if c == 0: a0.set_ylabel(rf"$\bar\lambda = {lb}$/s", fontsize=8, rotation=0, ha="right", va="center")
fig.suptitle(r"Exponential-kernel Hawkes processes: long-run rate $\bar\lambda$ (rows), branching ratio $\Gamma$ (columns)"
             "\n" r"each cell: events (top), count $N(t)$ (middle), intensity $\lambda(t)$ (bottom); decay rate $\omega = 1$/s",
             fontsize=8.5)
fig.savefig(OUT / "hawkes_grid_3x3.png", dpi=200, bbox_inches="tight"); plt.close(fig)

# ---------- Four processes at ~20/s: regular, Poisson, moderate Hawkes, near-critical Hawkes
rate = 20.0
def regular(T, rng):  # renewal process with gamma(25) gaps: much more regular than Poisson
    g = rng.gamma(25, 1/(25*rate), int(T*rate*1.2)); ev = np.cumsum(g); return ev[ev < T]
def poisson(T, rng):  return np.sort(rng.uniform(0, T, rng.poisson(rate*T)))
def hk(G):            return lambda T, rng: hawkes(rate*(1-G), G, 1.0, T, rng)
procs = [("regular (renewal)", regular, "C2"), ("Poisson", poisson, "C0"),
         (r"Hawkes, $\Gamma = 0.6$", hk(0.6), "C1"), (r"Hawkes, $\Gamma = 0.9$", hk(0.9), "C3")]
rng = np.random.default_rng(3)
T60 = 60.0
runs = [(name, f(T60, rng), col) for name, f, col in procs]
fig, axes = plt.subplots(4, 1, figsize=(7.2, 5.6), sharex=True)
D1 = {}
for ax, (name, ev, col) in zip(axes, runs):
    d, c = fano(ev, T60, 1.0); D1[name] = (d, c)
    ax.vlines(ev, 0, 1, color=col, lw=0.4); ax.set_yticks([])
    ax.set_title(rf"{name}: $D(1\,\mathrm{{s}}) = {d:.2f}$, mean count per second $= {c.mean():.1f}$", fontsize=8, loc="left")
axes[-1].set_xlabel("time (s)"); axes[-1].set_xlim(0, T60)
fig.tight_layout(); fig.savefig(OUT / "rasters_by_fano.png", dpi=200); plt.close(fig)

# ---------- Count histograms, 1-second windows
from math import lgamma
fig, axes = plt.subplots(1, 4, figsize=(7.2, 2.4), sharey=True)
for ax, (name, ev, col) in zip(axes, runs):
    d, c = D1[name]; m = c.mean()
    hi = max(40, c.max()+2)
    ax.hist(c, bins=np.arange(-0.5, hi+0.5, 1), color=col, alpha=0.8)
    k = np.arange(0, hi); pmf = np.exp(k*np.log(m) - m - np.array([lgamma(x+1) for x in k]))
    ax.plot(k, pmf*len(c), color="k", lw=0.9)
    ax.set_title(f"{name}\n$D = {d:.2f}$", fontsize=7.5); ax.set_xlabel("count in a 1-s window", fontsize=7)
axes[0].set_ylabel("number of windows")
fig.tight_layout(); fig.savefig(OUT / "count_hists_by_fano.png", dpi=200); plt.close(fig)

# ---------- Fano factor against window length (longer runs)
rng = np.random.default_rng(11)
Tlong = 3000.0
ws = np.array([0.1, 0.2, 0.5, 1, 2, 5, 10, 20])
fig, ax = plt.subplots(figsize=(5.4, 3.4))
slopes = {}
for name, f, col in procs:
    ev = f(Tlong, rng)
    Ds = np.array([fano(ev, Tlong, w)[0] for w in ws])
    ax.loglog(ws, np.maximum(Ds, 1e-4), "o-", color=col, ms=3.5, lw=1.2, label=name)
    slopes[name] = np.polyfit(np.log(ws[:5]), np.log(np.maximum(Ds[:5], 1e-9)), 1)[0]
ax.axhline(1, color="0.5", ls="--", lw=0.8)
ax.set_xlabel(r"window length $w$ (s)"); ax.set_ylabel(r"Fano factor $D(w)$")
ax.legend(fontsize=7, frameon=False)
fig.tight_layout(); fig.savefig(OUT / "fano_scaling.png", dpi=200); plt.close(fig)
print("D(1s):", {k: round(v[0], 2) for k, v in D1.items()})
print("slopes 0.1-2s:", {k: round(v, 2) for k, v in slopes.items()})
