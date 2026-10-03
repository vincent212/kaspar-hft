"""Label leakage demonstration on a pure random walk (no predictability). Not data.
Smoothed label: mean of next k mid-prices minus mean of last k (k = 20). Rule using only the past:
sign(current price minus mean of last k). Compared with a label measured from the current price."""
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
rng = np.random.default_rng(1)
n, k = 2_000_000, 20
p = np.cumsum(rng.choice([-1, 1], n)).astype(float)
c = np.concatenate([[0], np.cumsum(p)])
i = np.arange(k, n - k)
mprev = (c[i + 1] - c[i + 1 - k]) / k
mnext = (c[i + 1 + k] - c[i + 1]) / k
smooth, fromnow, past = mnext - mprev, mnext - p[i], p[i] - mprev
def acc(pred, lab):
    s = (pred != 0) & (lab != 0); return np.mean(np.sign(pred[s]) == np.sign(lab[s]))
a1, a2 = acc(past, smooth), acc(past, fromnow)
print(round(a1, 3), round(a2, 3))
fig, ax = plt.subplots(1, 2, figsize=(10, 3.2), gridspec_kw={"width_ratios": [1.5, 1]})
t0 = 3000; seg = p[t0 - 40:t0 + 41]; tt = np.arange(-40, 41)
ax[0].plot(tt, seg, color="#444444", lw=1)
ax[0].hlines(seg[20:41].mean(), -19, 0, color="#2171b5", lw=3, label="average of the last 20 (known now)")
ax[0].hlines(seg[41:61].mean(), 1, 20, color="#d94801", lw=3, label="average of the next 20 (the future)")
ax[0].axvline(0, color="k", ls=":", lw=0.8); ax[0].text(0.5, seg.min(), " now", fontsize=7)
ax[0].set_xlabel("events relative to now", fontsize=8); ax[0].set_ylabel("mid-price (ticks)", fontsize=8)
ax[0].set_title("(1) The smoothed label compares two averages;\nthe gap already includes the moves since the blue average", fontsize=9)
ax[0].legend(fontsize=7, frameon=False, loc="upper left")
ax[1].bar(["smoothed label\n(next 20 vs last 20)", "label from the\ncurrent price"], [a1, a2], color=["#d94801", "#bbbbbb"])
ax[1].axhline(0.5, color="k", ls="--", lw=0.7); ax[1].text(1.45, 0.51, "chance", fontsize=7, ha="right")
for j, v in enumerate([a1, a2]): ax[1].text(j, v + 0.01, f"{v:.0%}", ha="center", fontsize=9)
ax[1].set_ylim(0, 0.85); ax[1].set_ylabel("direction predicted correctly", fontsize=8)
ax[1].set_title("(2) A rule that uses only the past,\non a market with no predictability", fontsize=9)
for a in ax: a.spines[["top", "right"]].set_visible(False); a.tick_params(labelsize=8)
fig.text(0.99, 0.005, "Stylised simulation: a random walk of 2 million steps; not data", ha="right", fontsize=7, style="italic")
fig.tight_layout(); fig.savefig("figs/label_leakage.png", dpi=200)
