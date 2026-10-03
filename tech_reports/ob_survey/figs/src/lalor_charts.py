"""Re-plot of numbers reported by Lalor & Swishchuk (arXiv 2502.17417), plus shares we computed from their
Tables 1 and 3. Not our data. Sources: notes/claims_lalor.md."""
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
S = ["AAPL", "AMZN", "GOOG", "INTC", "MSFT"]
x = np.arange(5); w = 0.38
fig, ax = plt.subplots(1, 3, figsize=(13, 3.3))
# (a) aggressive share, computed from Tables 1 and 3
real = [0.161, 0.102, 0.163, 0.005, 0.006]; sim = [0.316, 0.200, 0.312, 0.223, 0.270]
ax[0].bar(x - w/2, real, w, color="#444444", label="real (Table 1)")
ax[0].bar(x + w/2, sim, w, color="#d94801", label="simulated (Table 3)")
ax[0].set_title("(a) Share of mid-moving (aggressive) events\namong all events — our computation", fontsize=9)
ax[0].legend(fontsize=7, frameon=False)
# (b) test accuracy vs majority share
acc = [0.4054, 0.3904, 0.3153, 0.4969, 0.5124]; maj = [0.227, 0.228, 0.208, 0.260, 0.260]
ax[1].bar(x - w/2, acc, w, color="#6a51a3", label="model, test (Table 2)")
ax[1].bar(x + w/2, maj, w, color="#bbbbbb", label="most frequent type, full day\n(our computation, Table 1)")
ax[1].axhline(1/12, ls="--", color="k", lw=0.7); ax[1].text(4.6, 1/12 + 0.01, "1/12", fontsize=7, ha="right")
ax[1].set_title("(b) Accuracy of predicting the type\nof the next event (12 types)", fontsize=9)
ax[1].legend(fontsize=7, frameon=False, loc="upper left"); ax[1].set_ylim(0, 0.65)
# (c) Hurst exponent, Table 4
hr = [0.3929, 0.3428, 0.3415, 0.1874, 0.2846]; hs = [0.5759, 0.6521, 0.5707, 0.3274, 0.5583]
ax[2].bar(x - w/2, hr, w, color="#444444", label="real"); ax[2].bar(x + w/2, hs, w, color="#d94801", label="simulated")
ax[2].axhline(0.5, ls="--", color="k", lw=0.7); ax[2].text(4.6, 0.51, "0.5: no memory", fontsize=7, ha="right")
ax[2].set_title("(c) Hurst exponent of mid log-returns\n(Table 4; below 0.5 = mean-reverting)", fontsize=9)
ax[2].legend(fontsize=7, frameon=False, loc="upper left"); ax[2].set_ylim(0, 0.8)
for a in ax:
    a.set_xticks(x, S); a.tick_params(labelsize=8); a.spines[["top", "right"]].set_visible(False)
fig.tight_layout(); fig.savefig("figs/lalor_reported.png", dpi=200)
