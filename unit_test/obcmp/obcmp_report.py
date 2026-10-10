#!/usr/bin/env python3
#
# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Licensed under the MIT License. See LICENSE file in the project root.
#
# obcmp_report.py -- compare OB and TachBook output written by obcmp, check
# every trade print against the bid/offer, and chart it.
#
#   python3 obcmp_report.py <obcmp output dir> <tick> <title>
#
# Reference market: obcmp's own minimal book (levels_ref.csv), built from
# the same MBO records independently of both books under test. For each
# print, the market used is the last book state from an EARLIER
# transaction, i.e. the book the trade executed against.
#
# A print is OUTSIDE the bid/offer when either
#   - it is on the wrong side of the market (a hit -- resting bid filled --
#     above the best bid, or a take -- resting offer filled -- below the best
#     offer), or
#   - it is deeper than the touch and the same transaction did NOT print at
#     every level from the touch to it (a real sweep walks the book level by
#     level; a stale price jumps there).
#
import sys
from collections import Counter
from pathlib import Path

import numpy as np
import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.dates as mdates

BLUE, ORANGE, AQUA, YELLOW = "#2a78d6", "#eb6834", "#1baf7a", "#eda100"
CRITICAL = "#d03b3b"
SURFACE, INK, INK2 = "#fcfcfb", "#0b0b0b", "#52514e"
TZ = "America/New_York"


def load(d):
    tb = pd.read_csv(d / "trades_tachbook.csv")
    ob = pd.read_csv(d / "trades_ob.csv")
    lv = pd.read_csv(d / "levels_ref.csv").sort_values("timestamp", kind="stable")
    bbo = pd.read_csv(d / "bbo_ob.csv")
    for t in (tb, ob):
        # resting BUY filled -> the bid was hit -> aggressor SOLD
        t["aggr"] = np.where(t.resting_side == "BUY", "SELL", "BUY")
    return tb, ob, lv, bbo


def classify(trades, lv, tick):
    """Pre-trade market for each print, and whether it is outside the bid/offer."""
    ts = lv.timestamp.values
    j = np.searchsorted(ts, trades.txtim.values, side="left") - 1
    ok = j >= 0
    trades = trades.copy()
    trades["bid"] = np.where(ok, lv.bid.values[np.clip(j, 0, None)], np.nan)
    trades["ask"] = np.where(ok, lv.ask.values[np.clip(j, 0, None)], np.nan)
    eps = tick / 4
    # levels printed per transaction and side, for the sweep test
    printed = trades.groupby(["txtim", "resting_side"]).px.apply(
        lambda s: set(np.round(s.values / tick).astype(int))).to_dict()
    verdict, depth = [], []
    for r in trades.itertuples(index=False):
        if np.isnan(r.bid) or r.bid >= r.ask:
            verdict.append("no market"); depth.append(0); continue
        if r.resting_side == "BUY":          # hit: must be at or below the best bid
            if r.px > r.bid + eps:
                verdict.append("OUTSIDE (wrong side)"); depth.append(0); continue
            d = int(round((r.bid - r.px) / tick))
            touch = int(round(r.bid / tick))
            need = set(range(touch - d, touch + 1))
        else:                                # take: must be at or above the best offer
            if r.px < r.ask - eps:
                verdict.append("OUTSIDE (wrong side)"); depth.append(0); continue
            d = int(round((r.px - r.ask) / tick))
            touch = int(round(r.ask / tick))
            need = set(range(touch, touch + d + 1))
        depth.append(d)
        if d == 0:
            verdict.append("at touch")
        elif need <= printed[(r.txtim, r.resting_side)]:
            verdict.append("sweep")
        else:
            verdict.append("OUTSIDE (skipped levels)")
    trades["verdict"] = verdict
    trades["depth"] = depth
    return trades


def compare_trades(tb, ob):
    key = lambda t: Counter(zip(t.txtim, np.round(t.px, 9), t.aggr, t.qty))
    a, b = key(tb), key(ob)
    both = sum((a & b).values())
    return both, sum((a - b).values()), sum((b - a).values()), a - b, b - a


def compare_bbo(bbo, lv, tick):
    """OB's own (throttled) best bid/ask vs TachBook's at the same moment."""
    if bbo.empty:
        return 0, 0
    ts = lv.timestamp.values
    j = np.searchsorted(ts, bbo.txtim.values, side="right") - 1
    ok = j >= 0
    tbb = lv.bid.values[np.clip(j, 0, None)]
    tba = lv.ask.values[np.clip(j, 0, None)]
    # OB ticks -> price: scale fitted from the data (OB ints are price / unit)
    obb = bbo.bid_ticks.values * tick
    oba = bbo.ask_ticks.values * tick
    same = ok & (np.abs(obb - tbb) < tick / 4) & (np.abs(oba - tba) < tick / 4)
    return int(same.sum()), int(ok.sum())


def chart(tb_c, ob_c, lv, title, out):
    plt.rcParams.update({"font.size": 9, "axes.edgecolor": INK2, "axes.labelcolor": INK2,
                         "xtick.color": INK2, "ytick.color": INK2})
    lvt = pd.to_datetime(lv.timestamp, unit="ns", utc=True).dt.tz_convert(TZ)

    def panel(ax, tr, name, t0=None, t1=None, small=False):
        m = slice(None)
        L = lv
        T = lvt
        if t0 is not None:
            mk = (lvt >= t0) & (lvt <= t1)
            L, T = lv[mk.values], lvt[mk.values]
        ax.step(T, L.bid, where="post", color=BLUE, lw=1.2, label="best bid")
        ax.step(T, L.ask, where="post", color=ORANGE, lw=1.2, label="best offer")
        tt = pd.to_datetime(tr.txtim, unit="ns", utc=True).dt.tz_convert(TZ)
        if t0 is not None:
            keep = ((tt >= t0) & (tt <= t1)).values
            tr, tt = tr[keep], tt[keep]
        s = 10 if small else 30
        buy = (tr.aggr == "BUY").values
        ax.scatter(tt[buy], tr.px[buy], s=s, marker="^", color=AQUA, edgecolors=SURFACE,
                   linewidths=0.6, label="trade, buyer aggressor", zorder=3)
        ax.scatter(tt[~buy], tr.px[~buy], s=s, marker="v", color=YELLOW, edgecolors=SURFACE,
                   linewidths=0.6, label="trade, seller aggressor", zorder=3)
        bad = tr.verdict.str.startswith("OUTSIDE").values
        ax.scatter(tt[bad], tr.px[bad], s=90, marker="x", color=CRITICAL, linewidths=2,
                   label="OUTSIDE bid/offer (%d)" % bad.sum(), zorder=4)
        ax.set_title(name, loc="left", color=INK, fontsize=10)
        ax.grid(axis="y", color="#e6e5e0", lw=0.6)
        ax.set_facecolor(SURFACE)
        ax.yaxis.set_major_formatter(plt.FuncFormatter(
            lambda v, _: "%d-%04.1f" % (int(v), (v - int(v)) * 32)))
        ax.xaxis.set_major_formatter(mdates.DateFormatter("%H:%M:%S" if t0 is not None else "%H:%M", tz=lvt.dt.tz))
        for sp in ("top", "right"):
            ax.spines[sp].set_visible(False)

    # zoom: the busiest minute of the hour
    tt = pd.to_datetime(tb_c.txtim, unit="ns", utc=True).dt.tz_convert(TZ)
    busiest = tt.dt.floor("1min").value_counts().idxmax()
    z0, z1 = busiest, busiest + pd.Timedelta(seconds=60)

    fig, axes = plt.subplots(3, 1, figsize=(13, 13), facecolor=SURFACE,
                             gridspec_kw={"height_ratios": [1.2, 1, 1], "hspace": 0.32})
    panel(axes[0], tb_c, "%s — full hour, TachBook (fixed)" % title, small=True)
    panel(axes[1], tb_c, "TachBook — busiest minute %s–%s ET" % (z0.strftime("%H:%M"), z1.strftime("%H:%M")), z0, z1)
    panel(axes[2], ob_c, "OB — same minute", z0, z1)
    axes[0].legend(loc="upper left", fontsize=8, frameon=False, ncol=5)
    axes[2].set_xlabel("time (ET)")
    for ax in axes:
        ax.set_ylabel("price (handle-32nds)")
    fig.savefig(out, dpi=130, bbox_inches="tight", facecolor=SURFACE)
    return z0, z1


def main():
    d = Path(sys.argv[1])
    tick = float(sys.argv[2])
    title = sys.argv[3]
    tb, ob, lv, bbo = load(d)
    both, only_tb, only_ob, xtb, xob = compare_trades(tb, ob)
    print("TRADES   TachBook %d prints / %d lots    OB %d prints / %d lots"
          % (len(tb), tb.qty.sum(), len(ob), ob.qty.sum()))
    print("         identical (time, price, aggressor, qty): %d    only TachBook: %d    only OB: %d"
          % (both, only_tb, only_ob))
    for nm, x in (("only TachBook", xtb), ("only OB", xob)):
        for k, n in list(x.items())[:5]:
            print("           %s: tx %d px %.6f %s qty %d  x%d" % (nm, k[0], k[1], k[2], k[3], n))
    same, n = compare_bbo(bbo, lv, tick)
    print("BID/ASK  OB's own best bid/ask (throttled, %d updates) equal to TachBook's: %d of %d" % (len(bbo), same, n))
    tb_c, ob_c = classify(tb, lv, tick), classify(ob, lv, tick)
    print("\nPRINTS vs the bid/offer before the trade:")
    print("  %-28s %12s %12s" % ("", "TachBook", "OB"))
    for v in ["at touch", "sweep", "OUTSIDE (wrong side)", "OUTSIDE (skipped levels)", "no market"]:
        print("  %-28s %12d %12d" % (v, (tb_c.verdict == v).sum(), (ob_c.verdict == v).sum()))
    for nm, c in (("TachBook", tb_c), ("OB", ob_c)):
        bad = c[c.verdict.str.startswith("OUTSIDE")]
        for r in bad.head(6).itertuples(index=False):
            print("    %s OUTSIDE: tx %d %s aggressor px %.6f qty %d  market %.6f / %.6f  (%s)"
                  % (nm, r.txtim, r.aggr, r.px, r.qty, r.bid, r.ask, r.verdict))
    tb_c.to_csv(d / "classified_tachbook.csv", index=False)
    ob_c.to_csv(d / "classified_ob.csv", index=False)
    z0, z1 = chart(tb_c, ob_c, lv, title, d / "zn_trades_bidask.png")
    print("\nchart: %s  (zoom %s-%s ET)" % (d / "zn_trades_bidask.png", z0.strftime("%H:%M"), z1.strftime("%H:%M")))


if __name__ == "__main__":
    main()
