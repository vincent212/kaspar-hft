"""Illustrative: expected depletion time of the best ask after a burst of buying, Poisson vs Hawkes view.
Hand-specified numbers, not data. Queue 200 lots, average event size 5 lots."""
from pathlib import Path
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
v, vbar = 200.0, 5.0
base = {"L": 10.0, "C": 8.0, "M": 4.0}          # ask limit orders, ask cancels, market buys (per second)
peak = {"L": 10.0, "C": 14.0, "M": 20.0}        # just after the burst
omega = 0.4                                      # excitation decays at rate 0.4/s
t = np.linspace(0, 12, 600)
lam = {k: base[k] + (peak[k] - base[k]) * np.exp(-omega * t) for k in base}
rin = vbar * lam["L"]; rout = vbar * (lam["M"] + lam["C"])
tau_h = v / (rout - rin)
tau_p = v / (vbar * (base["M"] + base["C"]) - vbar * base["L"])
fig, ax = plt.subplots(1, 2, figsize=(10, 3.2))
ax[0].plot(t, lam["M"], color="#d94801", label="market buys $\\lambda^{M,b}$")
ax[0].plot(t, lam["C"], color="#6a51a3", label="ask cancellations $\\lambda^{C,a}$")
ax[0].plot(t, lam["L"], color="#2171b5", label="ask limit orders $\\lambda^{L,a}$")
ax[0].set_title("(1) Intensities after a burst of buying at $t=0$", fontsize=9)
ax[0].set_xlabel("seconds after the burst", fontsize=8); ax[0].set_ylabel("events per second", fontsize=8)
ax[0].legend(fontsize=7, frameon=False)
ax[1].plot(t, tau_h, color="#444444", label="Hawkes: current intensities, held fixed")
ax[1].axhline(tau_p, color="gray", ls="--", label="Poisson: uses average intensities")
ax[1].axhline(2.0, color="#d94801", ls=":", lw=1); ax[1].text(11.8, 2.4, "example threshold: 2 s", fontsize=7, ha="right", color="#d94801")
ax[1].set_ylim(0, 22)
ax[1].set_title("(2) Expected time until the best ask (200 lots) is used up", fontsize=9)
ax[1].set_xlabel("seconds after the burst", fontsize=8); ax[1].set_ylabel("expected depletion time (s)", fontsize=8)
ax[1].legend(fontsize=7, frameon=False, loc="center right")
for a in ax: a.spines[["top", "right"]].set_visible(False); a.tick_params(labelsize=8)
fig.text(0.99, 0.005, "Illustrative numbers, not data", ha="right", fontsize=7, style="italic")
fig.tight_layout(); fig.savefig(Path(__file__).resolve().parents[1] / "hawkes_depletion.png", dpi=200)
print(round(tau_h[0], 2), round(tau_p, 2))
