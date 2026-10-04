"""Exponential density of Poisson inter-arrival gaps at mu = 300/s (redrawn; same content as the
figure in Maciejewski 2026a, with labels placed inside the axes)."""
import numpy as np, matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from pathlib import Path
OUT = Path(__file__).resolve().parents[1]
mu = 300.0                                   # arrivals per second
x = np.linspace(0, 20, 400)                  # gap in ms
dens = (mu/1000) * np.exp(-mu/1000 * x)      # density per ms
fig, ax = plt.subplots(figsize=(6.4, 3.3))
ax.plot(x, dens, color="C0", lw=2.2)
for q, lab, y in [(0.5, "median", 0.20), (0.9, "90th pct", 0.13), (0.99, "99th pct", 0.13)]:
    g = -np.log(1 - q) / mu * 1000
    ax.axvline(g, color="0.5", ls="--", lw=0.8)
    ax.text(g + 0.25, y, f"{lab}\n{g:.2f} ms", fontsize=8, color="0.25", va="bottom")
ax.set_xlabel("gap between arrivals (ms)"); ax.set_ylabel("probability density (per ms)")
ax.set_xlim(0, 20); ax.set_ylim(0, 0.32)
ax.spines["top"].set_visible(False); ax.spines["right"].set_visible(False)
ax.set_title(r"Poisson arrivals at $\mu = 300$ per second: mean gap $3.33$ ms", fontsize=9)
fig.tight_layout(); fig.savefig(OUT / "exponential_gaps.png", dpi=200); plt.close(fig)
