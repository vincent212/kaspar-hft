"""Bar charts that show what the paper's tables say. Every number below is copied from the table
named beside it in paper_v2.tex (which in turn came from the script named in its caption).

    python3 -m arrival_paper.make_bar_figs --out arrival_paper/figs/bars

Colours (Okabe-Ito, colour-blind safe), one meaning each across all charts:
engine clock / tandem / tight gaps = blue #0072B2; publisher clock / dispatch = orange #E69F00;
loose gaps = grey; nulls: uniform = vermillion, gap shuffle = green, 1 s-binned = sky;
ordered bins use one hue from light to dark.
"""
import argparse
from pathlib import Path
import numpy as np, matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt

BLUE, ORANGE, GREEN, VERM, SKY, PURPLE, GREY = "#0072B2", "#E69F00", "#009E73", "#D55E00", "#56B4E9", "#CC79A7", "#8C8C8C"
plt.rcParams.update({"font.size": 14, "axes.titlesize": 14, "axes.labelsize": 14, "xtick.labelsize": 13,
                     "ytick.labelsize": 13, "legend.fontsize": 12, "axes.spines.top": False, "axes.spines.right": False})

def labels(ax, bars, fmt="{:.0f}%", dy=1.0, fs=11):
    for b in bars:
        h = b.get_height()
        ax.text(b.get_x() + b.get_width() / 2, h + dy, fmt.format(h), ha="center", va="bottom", fontsize=fs)

def save(fig, out, name):
    fig.tight_layout(); fig.savefig(out / f"{name}.pdf"); fig.savefig(out / f"{name}.png", dpi=80); plt.close(fig)

def engine_clock(out):   # tab:engine-clock (median session, 40 sessions)
    cats = ["< 7.5 µs", "< 16 µs", "< 32 µs"]
    eng, eng_lo, eng_hi = [17.1, 25.4, 35.4], [14.4, 22.3, 31.5], [22.9, 31.8, 40.7]
    pub, pub_lo, pub_hi = [1.04, 17.8, 32.1], [0.44, 15.5, 28.6], [1.52, 22.2, 36.9]
    x = np.arange(3); w = 0.38
    fig, ax = plt.subplots(figsize=(8, 4.6))
    b1 = ax.bar(x - w/2, eng, w, color=BLUE, label="matching engine's clock",
                yerr=[np.subtract(eng, eng_lo), np.subtract(eng_hi, eng)], capsize=4, ecolor="0.3")
    b2 = ax.bar(x + w/2, pub, w, color=ORANGE, label="publisher's clock",
                yerr=[np.subtract(pub, pub_lo), np.subtract(pub_hi, pub)], capsize=4, ecolor="0.3")
    for bars, hi in ((b1, eng_hi), (b2, pub_hi)):
        for b, h in zip(bars, hi):
            ax.text(b.get_x() + b.get_width() / 2, h + 0.8, f"{b.get_height():.1f}%", ha="center", va="bottom", fontsize=11)
    ax.set_xticks(x, cats); ax.set_ylabel("share of consecutive transactions"); ax.set_xlabel("gap between consecutive transactions")
    ax.set_ylim(0, 50); ax.legend(loc="upper left", frameon=False)
    save(fig, out, "engine_clock")

def two_clocks(out):     # tab:two-clocks (row %)
    rows = ["< 1 µs", "1–7.5 µs", "7.5–16 µs", "16–100 µs", "> 100 µs"]
    cols = ["same packet", "< 7.5 µs", "7.5–10 µs", "10–16 µs", "16–100 µs", "> 100 µs"]
    M = np.array([[15.7, 3.7, 43.0, 16.1, 19.9, 1.7], [10.0, 3.8, 32.1, 20.2, 29.9, 4.1],
                  [8.8, 2.1, 21.5, 13.6, 48.7, 5.3], [4.9, 0.5, 4.9, 4.0, 71.1, 14.7], [0.8, 0.0, 0.1, 0.2, 15.1, 83.7]])
    colors = [PURPLE] + list(plt.cm.Blues(np.linspace(0.25, 0.95, 5)))
    fig, ax = plt.subplots(figsize=(10.5, 4.8)); y = np.arange(len(rows))[::-1]; left = np.zeros(len(rows))
    for j, c in enumerate(cols):
        ax.barh(y, M[:, j], left=left, color=colors[j], edgecolor="white", linewidth=1.5, label=c)
        for i in range(len(rows)):
            if M[i, j] >= 6: ax.text(left[i] + M[i, j] / 2, y[i], f"{M[i, j]:.0f}", ha="center", va="center", fontsize=11,
                                    color="white" if j >= 3 or j == 0 else "black")
        left += M[:, j]
    ax.set_yticks(y, rows); ax.set_ylabel("gap at the matching engine"); ax.set_xlabel("% of cases: gap between the packets that carry them (publisher's clock)")
    ax.set_xlim(0, 100); ax.legend(ncol=6, loc="upper center", bbox_to_anchor=(0.5, 1.2), frameon=False, fontsize=11, handlelength=1.2, columnspacing=0.8)
    save(fig, out, "two_clocks")

def publisher(out):      # tab:publisher
    cats = ["0", "1", "2–3", "4–7", "8–15"]
    p50 = [86, 113, 205, 406, 912]; coal = [4.2, 10.0, 15.2, 20.8, 26.6]
    fig, ax = plt.subplots(1, 2, figsize=(12, 4.4)); x = np.arange(5)
    b = ax[0].bar(x, p50, color=ORANGE); labels(ax[0], b, "{:.0f}", 15)
    ax[0].set_ylabel("median delay, engine to packet (µs)"); ax[0].set_ylim(0, 1050)
    b = ax[1].bar(x, coal, color=ORANGE); labels(ax[1], b, "{:.0f}%", 0.5)
    ax[1].set_ylabel("sent sharing a packet\nwith another transaction"); ax[1].set_ylim(0, 31)
    for a in ax: a.set_xticks(x, cats); a.set_xlabel("other NQ transactions in the previous 50 µs")
    save(fig, out, "publisher")

def drain(out):          # tab:drain-delay, tab:drain-gap, tab:drain-floor
    dcat = ["< 0.1", "0.1–0.2", "0.2–0.5", "0.5–1", "1–5", "> 5"]; dspan = [1.000, 1.016, 1.237, 1.278, 1.440, 1.603]
    gcat = ["< 7.5", "7.5–10\n(floor)", "10–16", "16–32", "32–\n100", "100–\n1000"]; gspan = [1.006, 1.007, 1.096, 1.141, 1.073, 1.106]
    gadj = [100, 100, 95.7, 36.2, 20.6, 12.9]
    fcat = ["< 0.1", "0.1–0.5", "0.5–5", "> 5"]; fspan = [1.000, 1.006, 1.026, 1.114]
    fig, ax = plt.subplots(1, 3, figsize=(16, 4.8), gridspec_kw={"width_ratios": [6, 6, 4]})
    b = ax[0].bar(range(6), dspan, color=plt.cm.Oranges(np.linspace(0.35, 0.9, 6))); labels(ax[0], b, "{:.2f}", 0.01)
    ax[0].set_xticks(range(6), dcat); ax[0].set_xlabel("publisher delay (ms)"); ax[0].set_title("all packets, by backlog")
    b = ax[1].bar(range(6), gspan, color=[BLUE if i == 1 else "#9ecae1" for i in range(6)]); labels(ax[1], b, "{:.2f}", 0.01)
    for i, (v, h) in enumerate(zip(gadj, gspan)):
        ax[1].text(i, h + 0.05, f"adj.\n{v:.0f}%", ha="center", va="bottom", fontsize=10, color="0.35")
    ax[1].set_xticks(range(6), gcat); ax[1].set_xlabel("gap to previous NQ packet (µs)"); ax[1].set_title("all packets, by gap before them")
    b = ax[2].bar(range(4), fspan, color=plt.cm.Oranges(np.linspace(0.35, 0.9, 4))); labels(ax[2], b, "{:.2f}", 0.01)
    ax[2].set_xticks(range(4), fcat); ax[2].set_xlabel("publisher delay (ms)"); ax[2].set_title("floor packets only (gap 7.5–10 µs)")
    for a in ax: a.set_ylim(0.9, 1.7); a.set_ylabel("mean NQ messages per packet")
    save(fig, out, "drain")

def tight_gaps(out):     # tab:tight-pairs (40 sessions)
    cats = ["first is\na trade", "first is\na delete", "first is\na new order", "first is\na change", "after a trade:\nnext is a trade",
            "after a trade:\nnext is a delete", "after a trade:\nnext is a new", "same\nprice"]
    tight = [2.0, 41.2, 46.0, 10.8, 11.7, 37.4, 45.5, 20.5]; loose = [3.5, 39.8, 39.5, 17.2, 2.2, 23.5, 68.2, 17.3]
    x = np.arange(len(cats)); w = 0.4
    fig, ax = plt.subplots(figsize=(14, 4.8))
    b1 = ax.bar(x - w/2, tight, w, color=BLUE, label="tight gap (< 16 µs)"); b2 = ax.bar(x + w/2, loose, w, color=GREY, label="loose gap (0.1–1 ms)")
    labels(ax, b1, "{:.1f}", 0.8, 10); labels(ax, b2, "{:.1f}", 0.8, 10)
    ax.axvline(3.5, color="0.5", lw=0.8, ls=":")
    ax.set_xticks(x, cats, fontsize=11); ax.set_ylabel("% of gaps between consecutive transactions"); ax.set_ylim(0, 78)
    ax.legend(loc="upper left", frameon=False)
    save(fig, out, "tight_gaps")

def nulls(out):          # tab:nulls, N = 1; share of the p99 excess (p99 - T) that survives
    T = [8, 16, 32, 64, 128]
    real = [8.61, 34.59, 146.14, 820.70, 5316.85]; U = [8.00, 16.00, 42.02, 106.73, 237.65]
    G = [8.54, 30.75, 99.61, 340.62, 1306.63]; B = [8.00, 16.00, 47.36, 112.62, 246.03]
    sh = lambda arm: [100 * (a - t) / (r - t) for a, r, t in zip(arm, real, T)]
    x = np.arange(5); w = 0.27
    fig, ax = plt.subplots(figsize=(10, 4.6))
    for k, (arm, c, lab) in enumerate([(U, VERM, "uniform (Poisson)"), (G, GREEN, "gap shuffle"), (B, SKY, "1 s-binned")]):
        b = ax.bar(x + (k - 1) * w, sh(arm), w, color=c, label=lab); labels(ax, b, "{:.0f}%", 1.2, 11)
    ax.axhline(100, color="black", lw=1, ls="--"); ax.text(4.45, 101.5, "real stream = 100%", ha="right", fontsize=11)
    ax.set_xticks(x, [f"T = {t} µs" for t in T]); ax.set_ylabel("share of the single-thread\np99 excess that survives"); ax.set_ylim(0, 112)
    ax.legend(loc="upper center", ncol=3, frameon=False, bbox_to_anchor=(0.5, 1.13))
    save(fig, out, "nulls")

def tx_arms(out):        # tab:tx-arms (values in parentheses = % of H)
    T = [16, 32, 64, 128]
    arms = {"TG: idle gaps shuffled": ([80, 60, 37, 22], GREEN), "TP: times redrawn uniformly": ([25, 18, 7, 2], VERM),
            "TS: sizes decoupled": ([114, 108, 101, 95], PURPLE), "TM: merged into one packet": ([101, 100, 95, 92], SKY),
            "TW: packets spread": ([104, 101, 100, 100], ORANGE)}
    x = np.arange(4); w = 0.16
    fig, ax = plt.subplots(figsize=(12, 4.8))
    for k, (lab, (v, c)) in enumerate(arms.items()):
        b = ax.bar(x + (k - 2) * w, v, w, color=c, label=lab); labels(ax, b, "{:.0f}", 1.2, 9)
    ax.axhline(100, color="black", lw=1, ls="--")
    ax.set_xticks(x, [f"T = {t} µs" for t in T]); ax.set_ylabel("p99 as % of the real stream's"); ax.set_ylim(0, 135)
    ax.legend(loc="upper center", ncol=3, frameon=False, bbox_to_anchor=(0.5, 1.2), fontsize=11)
    save(fig, out, "tx_arms")

def dispatch(out):       # tab:dispatch-costed, p99
    tand = {16: [19.59, 22.48, 29.28], 32: [52.84, 39.86, 46.66], 64: [181.08, 89.10, 81.42], 128: [902.82, 250.40, 162.28]}
    disp = {16: [20.78, 20.78, 20.78], 32: [51.61, 38.16, 38.16], 64: [175.74, 79.99, 72.92], 128: [883.83, 227.94, 142.45]}
    fig, ax = plt.subplots(1, 4, figsize=(16, 4.4)); x = np.arange(3); w = 0.38
    for a, T in zip(ax, [16, 32, 64, 128]):
        b1 = a.bar(x - w/2, tand[T], w, color=BLUE, label="tandem (serial cut into N stages)")
        b2 = a.bar(x + w/2, disp[T], w, color=ORANGE, label="dispatch pool of N cores, all costs charged")
        m = max(tand[T]); labels(a, b1, "{:.0f}", m * 0.01, 10); labels(a, b2, "{:.0f}", m * 0.01, 10)
        a.set_xticks(x, ["N = 2", "N = 4", "N = 8"]); a.set_title(f"T = {T} µs"); a.set_ylim(0, m * 1.15)
    ax[0].set_ylabel("p99 latency (µs)")
    fig.legend(*ax[0].get_legend_handles_labels(), loc="lower center", ncol=2, frameon=False, bbox_to_anchor=(0.5, 0.0))
    fig.tight_layout(rect=(0, 0.1, 1, 1)); fig.savefig(out / "dispatch.pdf"); fig.savefig(out / "dispatch.png", dpi=80); plt.close(fig)

def span_share(out):     # serialisation-share table in the span-aware rerun (sec:crossval-spanrun)
    T = [2, 4, 8, 16, 32, 64, 128]; ser = [100, 100, 66, 6, 1, 1, 2]; que = [100 - s for s in ser]
    x = np.arange(7)
    fig, ax = plt.subplots(figsize=(10, 4.6))
    ax.bar(x, ser, color=VERM, label="decode of the messages ahead in the same packet")
    ax.bar(x, que, bottom=ser, color=BLUE, label="queueing behind earlier packets")
    for i, s in enumerate(ser): ax.text(i, s / 2 if s > 8 else s + 2, f"{s}%", ha="center", va="center" if s > 8 else "bottom",
                                        color="white" if s > 8 else "black", fontsize=12)
    ax.axvspan(1.5, 2.5, color="0.9", zorder=0); ax.text(2, 103, "production\nreceiver ≈ 7 µs", ha="center", va="bottom", fontsize=11)
    ax.set_xticks(x, [str(t) for t in T]); ax.set_xlabel("service time T (µs)"); ax.set_ylabel("share of the p99 excess")
    ax.set_ylim(0, 125); ax.legend(loc="upper right", frameon=False, bbox_to_anchor=(1.0, 1.0), fontsize=11)
    save(fig, out, "span_share")

def onset(out):          # table in sec:results-threshold (fine sweep, 3512 windows)
    T = [5, 6, 7, 8, 9, 10, 12, 14, 16]
    excess = [0.00, 0.00, 0.00, 0.61, 1.85, 3.52, 7.83, 12.68, 18.59]
    tail = [0.0, 0.0, 4.5, 98.6, 100, 100, 100, 100, 100]; split = [0.0, 0.0, 0.0, 0.0, 67.0, 99.7, 100, 100, 100]
    x = np.arange(len(T)); w = 0.4
    fig, ax = plt.subplots(1, 2, figsize=(15, 4.8))
    b = ax[0].bar(x, excess, color=BLUE); labels(ax[0], b, "{:.2f}", 0.3, 10)
    ax[0].axhline(1.7, color=VERM, lw=1.5, ls="--"); ax[0].text(0, 2.2, "hop cost h = 1.7 µs", color=VERM, fontsize=12)
    ax[0].set_ylabel("single-thread p99 minus T (µs)"); ax[0].set_ylim(0, 21); ax[0].set_title("how large the tail is")
    b1 = ax[1].bar(x - w/2, tail, w, color=BLUE, label="windows with any tail")
    b2 = ax[1].bar(x + w/2, split, w, color=ORANGE, label="windows where two stages beat one")
    for bars in (b1, b2):
        for bb in bars:
            h = bb.get_height()
            if 0 < h < 100: ax[1].text(bb.get_x() + bb.get_width() / 2, h + 1.5, f"{h:.1f}", ha="center", va="bottom", fontsize=10)
    ax[1].set_ylabel("% of the 3512 windows"); ax[1].set_ylim(0, 138); ax[1].set_yticks([0, 20, 40, 60, 80, 100]); ax[1].set_title("how often it appears, and how often splitting pays")
    ax[1].legend(loc="upper left", frameon=False, fontsize=11, ncol=2)
    for a in ax:
        a.set_xticks(x, [str(t) for t in T]); a.set_xlabel("service time T (µs)")
        a.axvline(2.5, color="0.4", lw=1, ls=":")
    ax[0].text(2.45, 19.5, "7.5 µs spacing ", ha="right", fontsize=11, color="0.3")
    save(fig, out, "onset")

def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--out", required=True); a = ap.parse_args()
    out = Path(a.out); out.mkdir(parents=True, exist_ok=True)
    for f in (onset, engine_clock, two_clocks, publisher, drain, tight_gaps, nulls, tx_arms, dispatch, span_share): f(out)
    print("wrote", sorted(p.name for p in out.glob("*.pdf")))
main()
