"""Re-plot of Table 1 of Shi & Cartlidge (KDD 2022). Not our data."""
from pathlib import Path
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
S = ["MSFT", "INTC", "JPM"]
rows = ["Hawkes\n(classical)", "LSTM\n(no structure)", "CT-LSTM\n(one unit)", "PCT-LSTM\nwithout state", "PCT-LSTM\n(full)"]
col = ["#444444", "#bbbbbb", "#9ecae1", "#fd8d3c", "#d94801"]
acc = {"MSFT": [42.67, 47.64, 46.81, 48.79, 49.66], "INTC": [40.10, 46.04, 44.22, 48.16, 48.38], "JPM": [48.20, 58.05, 59.62, 60.43, 60.97]}
nll = {"MSFT": [0.67, None, -0.81, -0.81, -0.87], "INTC": [0.80, None, -0.52, -0.60, -0.61], "JPM": [-0.41, None, -2.03, -2.07, -2.09]}
fig, ax = plt.subplots(1, 2, figsize=(12, 3.4))
x = np.arange(3); w = 0.16
for r in range(5):
    ax[0].bar(x + (r - 2) * w, [acc[s][r] for s in S], w, color=col[r], label=rows[r].replace("\n", " "))
    v = [nll[s][r] for s in S]
    if v[0] is not None:
        ax[1].bar(x + (r - 2) * w, v, w, color=col[r])
ax[0].axhline(25, ls="--", color="k", lw=0.7); ax[0].text(2.45, 25.8, "1/4", fontsize=7, ha="right")
ax[0].set_ylim(20, 65); ax[0].set_ylabel("accuracy (%)", fontsize=8)
ax[0].set_title("(a) Next event type, 4 classes (time of the event given)", fontsize=9)
ax[0].legend(fontsize=7, frameon=False, ncol=3, loc="upper left")
ax[1].axhline(0, color="k", lw=0.6)
ax[1].set_title("(b) Negative log-likelihood per event (lower is better;\nthe plain LSTM has no likelihood)", fontsize=9)
for a in ax:
    a.set_xticks(x, S); a.tick_params(labelsize=8); a.spines[["top", "right"]].set_visible(False)
fig.tight_layout(); fig.savefig(Path(__file__).resolve().parents[1] / "shi_reported.png", dpi=200)
