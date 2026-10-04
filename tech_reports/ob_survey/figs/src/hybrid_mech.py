"""Stylised illustrations for Section 9 mechanisms. Not data."""
from pathlib import Path
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

# ---------- Hawkes-biased attention ----------
t = np.array([0.3, 0.9, 1.4, 1.6, 2.2, 2.9, 3.3, 3.8])   # earlier events
tn = 4.0                                                   # current event
content = np.array([2.0, 0.4, 1.8, 0.2, 0.9, 0.3, 0.6, 0.5])  # query-key scores (illustrative)
omega = 1.2
def softmax(z): e = np.exp(z - z.max()); return e / e.sum()
w_plain = softmax(content)
kern = np.exp(-omega * (tn - t))
w_hawk = softmax(content + np.log(kern))

fig, ax = plt.subplots(3, 1, figsize=(6.4, 5.2), sharex=True)
def stems(a, y, col, title):
    a.vlines(t, 0, y, color=col, lw=5)
    a.axvline(tn, color="k", ls=":", lw=1); a.text(tn, a.get_ylim()[1]*0.9 if a.get_ylim()[1] else 0.9, "", fontsize=7)
    a.set_title(title, fontsize=9, loc="left"); a.spines[["top", "right"]].set_visible(False)
    a.tick_params(labelsize=8)
stems(ax[0], w_plain, "#4292c6", "(1) Standard attention: weight from content only (query vs key)")
ax[0].set_ylabel("weight", fontsize=8)
u = np.linspace(0, tn, 300)
ax[1].plot(u, np.exp(-omega * (tn - u)), color="#d94801")
ax[1].vlines(t, 0, kern, color="#d94801", lw=1, ls="--")
ax[1].axvline(tn, color="k", ls=":", lw=1)
ax[1].set_title(r"(2) Hawkes kernel $\phi(t_n - t_{n'}) = e^{-\omega (t_n - t_{n'})}$: older events count less", fontsize=9, loc="left")
ax[1].spines[["top", "right"]].set_visible(False); ax[1].tick_params(labelsize=8)
ax[1].set_ylabel(r"$\phi$", fontsize=8)
stems(ax[2], w_hawk, "#6a51a3", "(3) Hawkes-biased attention: content weight × kernel, renormalised")
ax[2].set_ylabel("weight", fontsize=8)
ax[2].set_xlabel("time of earlier event $t_{n'}$   (dotted line: current event $t_n$)", fontsize=8)
for a in ax: a.set_xlim(0, 4.3)
ax[2].annotate("strong content match,\nbut long ago: weight cut", xy=(0.3, w_hawk[0]), xytext=(0.5, 0.35),
               fontsize=7, arrowprops=dict(arrowstyle="->", lw=0.7))
ax[2].set_ylim(0, max(w_hawk.max(), 0.45) * 1.1)
fig.text(0.99, 0.005, "Stylised; not data", ha="right", fontsize=7, style="italic")
fig.tight_layout()
fig.savefig(Path(__file__).resolve().parents[1] / "mech_hawkes_attn.png", dpi=200)

# ---------- intensity-conditioned memory ----------
rng = np.random.default_rng(3)
T = 12.0
rate = lambda s: np.where((s > 5) & (s < 7), 12.0, 1.0)
# events by thinning
ev = []; s = 0.0
while s < T:
    s += rng.exponential(1/12.0)
    if s < T and rng.uniform() < rate(s) / 12.0: ev.append(s)
ev = np.array(ev)
grid = np.linspace(0, T, 2000)
lam = rate(grid)
cum = np.concatenate([[0], np.cumsum((lam[1:] + lam[:-1]) / 2 * np.diff(grid))])
a_ev = 1/4.0    # forget after ~4 events
tau = 2.0       # fixed memory, seconds

fig, ax = plt.subplots(2, 1, figsize=(6.4, 4.4), gridspec_kw={"height_ratios": [1, 1.6]})
ax[0].plot(grid, lam, color="#444444")
ax[0].vlines(ev, 0, 1.2, color="#4292c6", lw=0.6)
ax[0].set_title("(1) Event stream (ticks) and its intensity (line): quiet, busy burst, quiet", fontsize=9, loc="left")
ax[0].set_ylabel("events / s", fontsize=8)
for q, col in [(6.8, "#d94801"), (11.5, "#6a51a3")]:
    i = np.searchsorted(grid, q); g = grid[:i+1]
    w_fixed = np.exp(-(q - g) / tau)
    w_int = np.exp(-a_ev * (cum[i] - cum[:i+1]))
    ax[1].plot(g, w_int, color=col, lw=2, label=f"intensity-scaled memory, query at t={q}")
    ax[1].plot(g, w_fixed, color=col, lw=1, ls="--", label=f"fixed memory ({tau:.0f} s), query at t={q}")
    ax[0].axvline(q, color=col, ls=":", lw=1); ax[1].axvline(q, color=col, ls=":", lw=1)
ax[1].axvspan(5, 7, color="gray", alpha=0.12)
ax[1].set_title("(2) How much weight the model gives to the past, seen from two moments", fontsize=9, loc="left")
ax[1].set_xlabel("time (s)", fontsize=8); ax[1].set_ylabel("weight on past input", fontsize=8)
ax[1].legend(fontsize=6.5, frameon=False, loc="upper left")
for a in ax: a.spines[["top", "right"]].set_visible(False); a.tick_params(labelsize=8); a.set_xlim(0, T)
fig.text(0.99, 0.005, "Stylised; not data", ha="right", fontsize=7, style="italic")
fig.tight_layout()
fig.savefig(Path(__file__).resolve().parents[1] / "mech_ssm_memory.png", dpi=200)
