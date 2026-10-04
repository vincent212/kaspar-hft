"""Illustrative intensity shapes for Section 7.17 (neural point processes). Hand-specified curves, not fitted models."""
from pathlib import Path
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
sp = lambda y: np.log1p(np.exp(y))
t = np.linspace(0, 4, 1200)
fig, ax = plt.subplots(1, 3, figsize=(12, 3.2))
mu = 1.0
# (a) Hawkes: three events, bumps add
ev = [1.0, 1.3, 1.6]; a, w = 1.5, 2.5
lam = mu + sum(np.where(t > e, a * np.exp(-w * (t - e)), 0) for e in ev)
ax[0].plot(t, lam, color="#444444")
for e in ev: ax[0].axvline(e, color="#bbbbbb", lw=0.8, ls=":")
ax[0].axhline(mu, color="gray", lw=0.6, ls="--")
ax[0].set_title("(a) Hawkes: every event adds the same bump,\nwhich can only decay back down", fontsize=9)
# (b) one memory cell of a continuous-time LSTM: relaxes to a target that can be above or below
def cell(start, target, d, e=1.0):
    c = np.where(t < e, 0.0, target + (start - target) * np.exp(-d * (t - e)))
    return c
base = sp(np.zeros_like(t)) * 0 + mu
up = mu + np.where(t < 1.0, 0, sp(cell(-1.5, 1.2, 1.5)) - sp(np.zeros_like(t)))
down = mu + np.where(t < 1.0, 0, sp(cell(-0.2, -2.0, 1.5)) - sp(np.zeros_like(t)))
ax[1].plot(t, up, color="#d94801", label="target above: rate rises after the event")
ax[1].plot(t, down, color="#2171b5", label="target below: rate falls (inhibition)")
ax[1].axvline(1.0, color="#bbbbbb", lw=0.8, ls=":"); ax[1].axhline(mu, color="gray", lw=0.6, ls="--")
ax[1].set_title("(b) Neural Hawkes memory cell: after an event it\nrelaxes towards a target set by the network", fontsize=9)
ax[1].legend(fontsize=7, frameon=False, loc="upper right")
# (c) interaction: the same three events, sum of bumps vs a learned response to the sequence
lam_sum = lam
extra = np.where(t > 1.6, 4.0 * np.exp(-3.0 * (t - 1.6)), 0)
ax[2].plot(t, lam_sum, color="#444444", label="Hawkes: sum of three separate bumps")
ax[2].plot(t, lam_sum + extra, color="#6a51a3", label='learned: "trade, cancel, cancel"\nread as a pattern')
for e, lab in zip(ev, ["trade", "cancel", "cancel"]):
    ax[2].axvline(e, color="#bbbbbb", lw=0.8, ls=":"); ax[2].text(e, 0.15, lab, rotation=90, fontsize=6.5, ha="right", va="bottom")
ax[2].axhline(mu, color="gray", lw=0.6, ls="--")
ax[2].set_title("(c) Interaction: a pattern of events can\nmatter more than its parts", fontsize=9)
ax[2].legend(fontsize=7, frameon=False, loc="upper right")
for x in ax:
    x.set_xlabel("time", fontsize=8); x.set_ylabel("intensity", fontsize=8); x.set_ylim(0, 8)
    x.spines[["top", "right"]].set_visible(False); x.tick_params(labelsize=7)
fig.text(0.99, 0.005, "Illustrative shapes, not fitted models; dashed: background rate", ha="right", fontsize=7, style="italic")
fig.tight_layout(); fig.savefig(Path(__file__).resolve().parents[1] / "npp_intuition.png", dpi=200)
