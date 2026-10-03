"""Stylised demonstration: a Poisson stream with NO self-excitation, whose background rate switches between quiet
and busy periods, fitted with a constant-background exponential Hawkes model. Not data."""
import numpy as np
from scipy.optimize import minimize
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
rng = np.random.default_rng(5)
def sim_switch(T, lo, hi, period):
    ev = []; t = 0.0
    while t < T:
        rate = hi if int(t // period) % 2 else lo
        end = (int(t // period) + 1) * period
        n = rng.poisson(rate * (min(end, T) - t))
        ev.extend(np.sort(rng.uniform(t, min(end, T), n))); t = end
    return np.array(ev)
def negll(p, ev, T):
    mu, a, w = np.exp(p)          # a = jump, w = decay, Gamma = a/w
    A = 0.0; ll = 0.0; prev = None
    for t in ev:
        if prev is not None: A = np.exp(-w * (t - prev)) * (A + 1)
        ll += np.log(mu + a * A); prev = t
    comp = mu * T + (a / w) * np.sum(1 - np.exp(-w * (T - ev)))
    return -(ll - comp)
def fit(ev, T):
    best = None
    for g in [0.2, 0.5, 0.8]:
        x0 = np.log([len(ev) / T * (1 - g), g * 2.0, 2.0])
        r = minimize(negll, x0, args=(ev, T), method="Nelder-Mead", options={"maxiter": 4000, "xatol": 1e-6, "fatol": 1e-6})
        if best is None or r.fun < best.fun: best = r
    mu, a, w = np.exp(best.x); return mu, a / w, w
T = 3600.0
rows = []
for period in [5, 30, 120]:
    ev = sim_switch(T, 1.0, 5.0, period)
    mu, G, w = fit(ev, T)
    rows.append((period, len(ev), G, 1 / w)); print(period, len(ev), round(G, 3), round(1 / w, 2))
ev0 = rng.poisson(3.0 * T); ev0 = np.sort(rng.uniform(0, T, ev0))
mu0, G0, w0 = fit(ev0, T); print("const", len(ev0), round(G0, 3))
fig, ax = plt.subplots(1, 2, figsize=(10, 3.0))
evd = sim_switch(300, 1.0, 5.0, 30)
ax[0].vlines(evd, 0, 1, lw=0.4, color="#2171b5")
for k in range(0, 300, 60): ax[0].axvspan(k + 30, k + 60, color="orange", alpha=0.15)
ax[0].set_yticks([]); ax[0].set_xlabel("time (s)", fontsize=8)
ax[0].set_title("(1) No excitation at all: the background rate switches\nbetween 1/s (white) and 5/s (shaded) every 30 s", fontsize=9)
labels = ["constant\nrate"] + [f"switch\nevery {p} s" for p, *_ in rows]
vals = [G0] + [r[2] for r in rows]
ax[1].bar(labels, vals, color=["#bbbbbb", "#fd8d3c", "#e6550d", "#a63603"])
for i, v in enumerate(vals): ax[1].text(i, v + 0.02, f"{v:.2f}", ha="center", fontsize=8)
ax[1].set_ylim(0, 1); ax[1].set_ylabel("fitted branching ratio", fontsize=8)
ax[1].set_title("(2) Branching ratio fitted by an exponential Hawkes model\nwith a constant background rate (true value: 0)", fontsize=9)
for a in ax: a.spines[["top", "right"]].set_visible(False); a.tick_params(labelsize=8)
fig.text(0.99, 0.005, "Stylised simulation; one hour of events per bar; not data", ha="right", fontsize=7, style="italic")
fig.tight_layout(); fig.savefig("figs/branching_trap.png", dpi=200)
