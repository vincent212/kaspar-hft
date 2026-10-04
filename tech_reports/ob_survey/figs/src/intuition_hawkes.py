"""Stylised two-type Hawkes simulation for the intuition subsection of Section 7. Not data.
Types: buy market orders (B) and sell market orders (S). Exponential kernels, decay omega.
Branching matrix Gamma[i][j] = expected type-i children of one type-j event."""
from pathlib import Path
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
rng = np.random.default_rng(11)
mu = np.array([0.3, 0.3]); omega = 3.0
G = np.array([[0.5, 0.15], [0.25, 0.4]])   # rows: excited type (B,S); cols: exciting type (B,S)
alpha = G * omega
T = 12.0
t, ex, ev = 0.0, np.zeros(2), []
while True:
    lb = mu.sum() + ex.sum(); w = rng.exponential(1 / lb); t += w
    if t > T: break
    ex *= np.exp(-omega * w)
    lam = mu + ex
    if rng.uniform() < lam.sum() / lb:
        k = 0 if rng.uniform() < lam[0] / lam.sum() else 1
        ev.append((t, k)); ex += alpha[:, k]
grid = np.linspace(0, T, 3000)
lam = np.tile(mu[:, None], (1, grid.size)).astype(float)
for (te, k) in ev:
    m = grid > te
    lam[:, m] += alpha[:, k:k+1] * np.exp(-omega * (grid[m] - te))
fig, ax = plt.subplots(2, 1, figsize=(8, 4.2), sharex=True, gridspec_kw={"height_ratios": [1, 2.2]})
cB, cS = "#2171b5", "#d94801"
for (te, k) in ev:
    ax[0].vlines(te, 0.55 if k == 0 else 0.05, 0.95 if k == 0 else 0.45, color=cB if k == 0 else cS, lw=1.2)
ax[0].set_yticks([0.75, 0.25], ["buys", "sells"]); ax[0].set_ylim(0, 1)
ax[0].set_title("Events: buy and sell market orders", fontsize=9, loc="left")
ax[1].plot(grid, lam[0], color=cB, lw=1.2, label="intensity of buys $\\lambda_B(t)$")
ax[1].plot(grid, lam[1], color=cS, lw=1.2, label="intensity of sells $\\lambda_S(t)$")
ax[1].axhline(mu[0], color="gray", ls=":", lw=0.8); ax[1].text(T, mu[0] + 0.05, "background rate", fontsize=7, ha="right", color="gray")
# annotate first buy that is followed by visible bumps
tb = next(te for te, k in ev if k == 0 and te > 1.0)
i = np.searchsorted(grid, tb) + 2
ax[1].annotate("a buy: big jump in the buy rate\n(self-excitation)", xy=(grid[i], lam[0][i]), xytext=(tb + 0.6, lam[0][i] + 1.0),
               fontsize=7, arrowprops=dict(arrowstyle="->", lw=0.7))
ax[1].annotate("the same buy: smaller jump\nin the sell rate (cross-excitation)", xy=(grid[i], lam[1][i]), xytext=(tb + 0.9, lam[1][i] + 0.25),
               fontsize=7, arrowprops=dict(arrowstyle="->", lw=0.7))
ax[1].set_ylabel("events per second", fontsize=8); ax[1].set_xlabel("time (s)", fontsize=8)
ax[1].legend(fontsize=7, frameon=False, loc="upper right")
for a in ax: a.spines[["top", "right"]].set_visible(False); a.tick_params(labelsize=8)
fig.text(0.99, 0.005, "Stylised simulation; not data", ha="right", fontsize=7, style="italic")
fig.tight_layout(); fig.savefig(Path(__file__).resolve().parents[1] / "intuition_hawkes.png", dpi=200)
print(len(ev), "events; first annotated buy at", round(tb, 2))
