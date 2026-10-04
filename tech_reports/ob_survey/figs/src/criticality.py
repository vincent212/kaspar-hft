"""Near-criticality for an exponential Hawkes process. Exact formulas, not data.
(a) Size of the family (cascade) started by one outside event. With Poisson(Gamma) children per event the total
    family size n has the Borel distribution P(n) = exp(-Gamma n) (Gamma n)^(n-1) / n!. Shown: P(size >= n).
(b) Expected extra activity after one outside event: proportional to exp(-omega (1 - Gamma) t), omega = 1/s."""
from pathlib import Path
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from math import lgamma, log, exp
cols = {0.5: "#9ecae1", 0.9: "#4292c6", 0.99: "#08306b"}
N = np.arange(1, 100001)
fig, ax = plt.subplots(1, 2, figsize=(11, 3.6))
for G in [0.5, 0.9, 0.99]:
    logp = np.array([-G * n + (n - 1) * log(G * n) - lgamma(n + 1) for n in N])
    p = np.exp(logp); surv = 1 - np.concatenate([[0], np.cumsum(p)[:-1]])
    mean = 1 / (1 - G)
    ax[0].loglog(N, np.clip(surv, 1e-7, 1), color=cols[G], lw=1.6,
                 label=f"$\\Gamma={G}$: mean {mean:.0f}; median {int(N[np.searchsorted(np.cumsum(p), 0.5)])}")
    print(G, "P(n=1)", round(p[0], 3), "P(n>=100)", round(surv[99], 4), "P(n>=1000)", round(surv[999], 5))
ax[0].set_xlabel("family size $n$ (events started by one outside event, itself included)", fontsize=8)
ax[0].set_ylabel("probability of a family at least this large", fontsize=8)
ax[0].set_ylim(1e-6, 1.5)
ax[0].set_title("(a) Most families stay small, but near one\na few become enormous", fontsize=9)
ax[0].legend(fontsize=7, frameon=False, loc="lower left")
t = np.linspace(0, 60, 600)
for G in [0.5, 0.9, 0.99]:
    ax[1].plot(t, np.exp(-(1 - G) * t), color=cols[G], lw=1.6, label=f"$\\Gamma={G}$: fades in about {1/(1-G):.0f} s")
ax[1].set_xlabel("seconds after one outside event", fontsize=8)
ax[1].set_ylabel("expected extra activity\n(relative to its start)", fontsize=8)
ax[1].set_title("(b) The extra activity fades at rate $\\omega(1-\\Gamma)$:\nslower and slower as $\\Gamma$ approaches one", fontsize=9)
ax[1].legend(fontsize=7, frameon=False)
for a in ax: a.spines[["top", "right"]].set_visible(False); a.tick_params(labelsize=8)
fig.text(0.99, 0.005, "Exact formulas for Poisson numbers of children and an exponential kernel with decay rate 1/s; not data", ha="right", fontsize=7, style="italic")
fig.tight_layout(); fig.savefig(Path(__file__).resolve().parents[1] / "criticality.png", dpi=200)
