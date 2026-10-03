"""Stylised kernel shapes for small and large trades: separable (size scales the bump) vs non-separable
(size changes the shape). Hand-specified, not fitted."""
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
u = np.linspace(0, 5, 500)
fig, ax = plt.subplots(1, 2, figsize=(9.5, 3.0), sharey=True)
ax[0].plot(u, 1.0 * np.exp(-2 * u), color="#9ecae1", lw=2, label="1-lot trade")
ax[0].plot(u, 2.5 * np.exp(-2 * u), color="#08519c", lw=2, label="100-lot trade")
ax[0].set_title("(a) Multiplicative mark: a bigger trade gives\nthe same bump, scaled up", fontsize=9)
ax[1].plot(u, 1.0 * np.exp(-2 * u), color="#9ecae1", lw=2, label="1-lot trade")
ax[1].plot(u, 1.6 * np.exp(-0.7 * u), color="#08519c", lw=2, label="100-lot trade")
ax[1].set_title("(b) Size changes the shape: a bigger trade\nexcites more and for longer", fontsize=9)
for a in ax:
    a.set_xlabel("time since the trade", fontsize=8); a.legend(fontsize=7, frameon=False)
    a.spines[["top", "right"]].set_visible(False); a.tick_params(labelsize=8)
ax[0].set_ylabel("extra intensity (kernel)", fontsize=8)
fig.text(0.99, 0.005, "Illustrative shapes, not fitted", ha="right", fontsize=7, style="italic")
fig.tight_layout(); fig.savefig("figs/marks_kernels.png", dpi=200)
