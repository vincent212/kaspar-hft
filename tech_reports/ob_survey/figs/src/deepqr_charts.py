"""Values reported by Bodor & Carlier (arXiv 2501.08822), re-plotted. Not our data.
See notes/claims_deepqr.md for the source of every number."""
from pathlib import Path
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

C = {"Real": "#444444", "QR": "#9ecae1", "SAQR": "#4292c6", "DQR": "#fd8d3c", "MDQR": "#d94801", "DeepLOB": "#bbbbbb"}
fig, ax = plt.subplots(1, 4, figsize=(13, 3.2))

# (a) excitation: P(next type = previous type), Fig. 1
types = ["cancel", "limit", "trade"]
vals = {"Real": [.73, .76, .30], "QR/SAQR": [.44, .53, .04], "DQR": [.66, .74, .25]}
cols = [C["Real"], C["SAQR"], C["DQR"]]
x = np.arange(3); w = 0.26
for i, (k, v) in enumerate(vals.items()):
    ax[0].bar(x + (i - 1) * w, v, w, label=k, color=cols[i])
ax[0].set_xticks(x, [f"{t}→{t}" for t in types]); ax[0].set_ylim(0, 1)
ax[0].set_title("(a) Same event type twice in a row\n(transition probability, Fig. 1)", fontsize=9)
ax[0].legend(fontsize=7, frameon=False)

# (b) best bid / best ask volume correlation, Fig. 12
m = ["Real", "QR", "SAQR", "MDQR"]; v = [-0.54, -0.22, -0.43, -0.54]
ax[1].bar(m, v, color=[C[k] for k in m]); ax[1].axhline(0, color="k", lw=0.6)
ax[1].set_ylim(-0.65, 0.05)
ax[1].set_title("(b) Correlation of best-bid and\nbest-ask volumes (Fig. 12)", fontsize=9)
for i, y in enumerate(v): ax[1].text(i, y - 0.04, f"{y:.2f}", ha="center", fontsize=8)

# (c) gamma shape of best-ask volume, Table 7
v = [1.35, 3.08, 1.91, 1.30]; e = [0.18, 0.13, 0.06, 0.13]
ax[2].bar(m, v, yerr=e, capsize=3, color=[C[k] for k in m])
ax[2].set_title("(c) Gamma shape $\\alpha$ of best-ask\nvolume (Table 7; ± std)", fontsize=9)

# (d) mid-price direction, balanced accuracy, Table 6
m2 = ["DeepLOB", "QR", "SAQR", "MDQR"]; v = [0.54, 0.56, 0.58, 0.63]; e = [0.01, 0.01, 0.03, 0.02]
ax[3].bar(m2, v, yerr=e, capsize=3, color=[C[k] for k in m2]); ax[3].set_ylim(0.3, 0.7)
ax[3].axhline(1/3, ls="--", color="k", lw=0.7); ax[3].text(3.45, 1/3 + 0.006, "1/3", fontsize=7, ha="right")
ax[3].set_title("(d) Next-500-event mid direction,\nbalanced accuracy (Table 6)", fontsize=9)
for a in ax:
    a.tick_params(labelsize=8); a.spines[["top", "right"]].set_visible(False)
fig.tight_layout()
fig.savefig(Path(__file__).resolve().parents[1] / "deepqr_reported.png", dpi=200)
