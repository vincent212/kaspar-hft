"""Stylised simulation for the compound Hawkes paragraph (Section 7.10). Not data.
Hawkes event times (exponential kernel, mu=0.5, alpha=1, omega=2, branching ratio 0.5, long-run rate 1/s);
three rules for turning events into mid-price moves; standard deviation of the price change over a window,
across simulated paths, against the diffusion-limit formula sigma_J * sqrt(rate * window)."""
from pathlib import Path
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
rng = np.random.default_rng(7)
mu, alpha, omega, burn, T = 0.5, 1.0, 2.0, 100.0, 400.0
rate = mu / (1 - alpha / omega)
def hawkes():
    t, lex, ev = 0.0, 0.0, []
    while True:
        lb = mu + lex; w = rng.exponential(1 / lb); t += w
        if t > T: return np.array(ev)
        lex *= np.exp(-omega * w)
        if rng.uniform() < (mu + lex) / lb:
            if t > burn: ev.append(t - burn)
            lex += alpha
p = 0.62
size_vals = np.array([1, 1, 1, 1, 2, 2, 3, 5])
def moves(n, kind):
    if kind == "a": return rng.choice([-1, 1], n)
    if kind == "b":
        s = np.empty(n); s[0] = rng.choice([-1, 1])
        for k in range(1, n): s[k] = s[k-1] if rng.uniform() < p else -s[k-1]
        return s
    return rng.choice([-1, 1], n) * rng.choice(size_vals, n)
sig2 = {"a": 1.0, "b": p / (1 - p), "c": float(np.mean(size_vals**2))}
lab = {"a": "(a) one tick, independent signs", "b": f"(b) one tick, signs persist ($p={p}$)",
       "c": "(c) 1--5 ticks, independent signs"}
col = {"a": "#444444", "b": "#d94801", "c": "#6a51a3"}
W = np.array([5, 10, 20, 40, 80, 160, 300])
npaths = 1500
res = {k: np.zeros((npaths, len(W))) for k in "abc"}
example = {}
for i in range(npaths):
    ev = hawkes()
    for k in "abc":
        m = np.cumsum(moves(len(ev), k))
        idx = np.searchsorted(ev, W, side="right")
        res[k][i] = np.where(idx > 0, m[np.maximum(idx - 1, 0)], 0.0)
        if i == 0: example[k] = (ev, m)
fig, ax = plt.subplots(1, 2, figsize=(11, 3.6))
for k in "abc":
    ev, m = example[k]
    ax[0].step(np.concatenate([[0], ev]), np.concatenate([[0], m]), where="post", color=col[k], lw=0.9, label=lab[k])
    sd = res[k].std(axis=0)
    ax[1].plot(W, sd, "o", color=col[k], ms=4, label=lab[k] + " (simulated)")
    ww = np.linspace(1, 300, 200)
    ax[1].plot(ww, np.sqrt(sig2[k] * rate * ww), "-", color=col[k], lw=1)
ax[0].set_title("(1) One simulated path for each rule,\nall driven by the same Hawkes event times", fontsize=9)
ax[0].set_xlabel("time (s)", fontsize=8); ax[0].set_ylabel("mid-price change (ticks)", fontsize=8)
ax[0].legend(fontsize=7, frameon=False, loc="upper left")
ax[1].set_title("(2) Spread of the price change across 1,500 paths:\ndots simulated, lines $\\sigma_J\\sqrt{\\bar\\lambda\\, w}$", fontsize=9)
ax[1].set_xlabel("window $w$ (s)", fontsize=8); ax[1].set_ylabel("standard deviation (ticks)", fontsize=8)
for a in ax: a.spines[["top", "right"]].set_visible(False); a.tick_params(labelsize=8)
fig.text(0.99, 0.005, "Stylised simulation; not data", ha="right", fontsize=7, style="italic")
fig.tight_layout(); fig.savefig(Path(__file__).resolve().parents[1] / "compound_hawkes.png", dpi=200)
for k in "abc": print(k, np.round(res[k].std(axis=0) / np.sqrt(sig2[k] * rate * W), 3))
