"""Latency ladder: published figures quoted in Part III, on one log axis.
Every bar is a figure stated in the survey text, with its source there."""
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

NS, US, MS = 1e-9, 1e-6, 1e-3
rows = [  # (label, low, high, category)
    ("STAC-T0: network in/out on FPGA (audited)",        24 * NS, 44 * NS, "net"),
    ("FPGA order-book update (papers)",                   26 * NS, 280 * NS, "book"),
    ("STAC-T1: FPGA tick-to-trade, CME (audited)",       115 * NS, 609 * NS, "path"),
    ("One small PCIe read (measured median)",            572 * NS, 572 * NS, "net"),
    ("Kaspar software socket-to-book (book medians)",    0.8 * US, 1.1 * US, "book"),
    ("Kernel-bypass network stack (measured median)",    946 * NS, 946 * NS, "net"),
    ("Small LSTM inference, sliding window (STAC-ML)",     2 * US, 35.2 * US, "model"),
    ("OS network stack (measured median)",               4.5 * US, 4.5 * US, "net"),
    ("Light in 1 km of fibre",                              5 * US, 5 * US, "net"),
    ("8-bit CNN for FI-2010 on FPGA",                      57 * US, 57 * US, "model"),
    ("Published LOB model inference (CPU/GPU)",            25 * US, 22.8 * MS, "model"),
]
colours = {"net": "#6baed6", "book": "#74c476", "path": "#9e9ac8", "model": "#fd8d3c"}
names = {"net": "network and transfer", "book": "book building",
         "path": "whole tick-to-trade path", "model": "model inference"}

fig, ax = plt.subplots(figsize=(9, 4.6))
for i, (lab, lo, hi, cat) in enumerate(rows):
    y = len(rows) - 1 - i
    if hi > lo:
        ax.plot([lo, hi], [y, y], lw=7, color=colours[cat], solid_capstyle="butt")
    else:
        ax.plot([lo], [y], "o", ms=8, color=colours[cat])
ax.set_xscale("log")
ax.set_yticks(range(len(rows)))
ax.set_yticklabels([r[0] for r in rows][::-1], fontsize=8)
ax.set_xlim(1e-8, 1e-1)
ticks = [10 * NS, 100 * NS, 1 * US, 10 * US, 100 * US, 1 * MS, 10 * MS, 100 * MS]
ax.set_xticks(ticks)
ax.set_xticklabels(["10 ns", "100 ns", "1 µs", "10 µs", "100 µs", "1 ms", "10 ms", "100 ms"], fontsize=8)
ax.grid(axis="x", which="major", color="#dddddd")
for c in ["top", "right"]:
    ax.spines[c].set_visible(False)
handles = [plt.Line2D([], [], color=colours[k], lw=6) for k in names]
ax.legend(handles, names.values(), fontsize=7.5, frameon=False, loc="upper right")
ax.set_xlabel("latency (log scale)", fontsize=8)
fig.tight_layout()
fig.savefig("figs/latency_ladder.png", dpi=200)
