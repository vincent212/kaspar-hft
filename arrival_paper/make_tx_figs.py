"""Figures and tables for the transaction section, from qsim_tx outputs (tx_hists.npz + parquet).

    python3 -m arrival_paper.make_tx_figs --dir arrival_paper/figs/qsim_grid_tx --out arrival_paper/figs/tx
Writes: tx_counts.pdf (messages/packet, packets/transaction, messages/transaction),
        tx_gaps.pdf (packet gaps inside vs between transactions, all packet gaps vs Poisson),
        tx_starts.pdf (transaction start gaps: real vs gaps-shuffled vs Poisson; multi-packet duration),
        and prints LaTeX rows for the count tables and the tight-gap table.
"""
import argparse
from pathlib import Path
import numpy as np, matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt

def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--dir", required=True); ap.add_argument("--out", required=True)
    a = ap.parse_args(); z = np.load(Path(a.dir) / "tx_hists.npz"); out = Path(a.out); out.mkdir(parents=True, exist_ok=True)
    ce = z["cnt_edges"]; ge = z["gap_edges_ns"] / 1e3; gm = np.sqrt(np.maximum(ge[:-1], 1e-3) * ge[1:])
    # counts
    fig, ax = plt.subplots(1, 3, figsize=(13, 3.6))
    for x, key, lab in zip(ax, ["mpp", "ppt", "mpt"], ["messages per packet", "packets per transaction", "messages per transaction"]):
        h = z[key].astype(float); p = h / h.sum(); k = ce[:-1]
        x.bar(k[:60], p[:60], width=0.8, color="0.3"); x.set_yscale("log"); x.set_xlabel(lab); x.set_ylabel("share")
        x.set_title(f"n = {int(h.sum()):,}; overflow >200: {p[-1]:.1e}", fontsize=9)
    fig.tight_layout(); fig.savefig(out / "tx_counts.pdf"); fig.savefig(out / "tx_counts.png", dpi=70); plt.close(fig)
    # gaps
    def dens(h): h = h.astype(float); return h / h.sum() / np.diff(np.log10(np.maximum(ge, 1e-4)))
    fig, ax = plt.subplots(1, 2, figsize=(12, 4))
    ax[0].plot(gm, dens(z["gpi"]), label="inside one transaction"); ax[0].plot(gm, dens(z["gpe"]), label="between transactions")
    ax[1].plot(gm, dens(z["gpall"]), label="all packet gaps (real)"); ax[1].plot(gm, dens(z["gpP"]), "--", label="rate-matched Poisson")
    for x in ax:
        x.set_xscale("log"); x.set_xlim(0.5, 1e6); x.set_xlabel("gap between consecutive packets (µs)"); x.set_ylabel("density per decade")
        for v in (7.5, 16, 32): x.axvline(v, color="0.6", lw=0.6, ls=":")
        x.legend(fontsize=8)
    fig.tight_layout(); fig.savefig(out / "tx_gaps.pdf"); fig.savefig(out / "tx_gaps.png", dpi=70); plt.close(fig)
    fig, ax = plt.subplots(1, 2, figsize=(12, 4))
    ax[0].plot(gm, dens(z["gtx"]), label="real"); ax[0].plot(gm, dens(z["gtxTG"]), label="idle gaps shuffled"); ax[0].plot(gm, dens(z["gtxP"]), "--", label="rate-matched Poisson")
    ax[0].set_xlabel("gap between consecutive transaction starts (µs)")
    ax[1].plot(gm, dens(z["gdur"]), color="0.2"); ax[1].set_xlabel("multi-packet transaction duration, first to last packet (µs)")
    for x in ax:
        x.set_xscale("log"); x.set_xlim(0.5, 1e6); x.set_ylabel("density per decade")
        for v in (7.5, 16, 32): x.axvline(v, color="0.6", lw=0.6, ls=":")
    ax[0].legend(fontsize=8); fig.tight_layout(); fig.savefig(out / "tx_starts.pdf"); fig.savefig(out / "tx_starts.png", dpi=70); plt.close(fig)
    # tables
    def rows(key, ks=(1, 2, 3, 4, 5)):
        h = z[key].astype(float); p = h / h.sum() * 100
        cells = [f"{p[k-1]:.3f}" for k in ks] + [f"{p[5:10].sum():.3f}", f"{p[10:20].sum():.3f}", f"{p[20:].sum():.4f}"]
        return " & ".join(cells)
    print("% counts table: 1 & 2 & 3 & 4 & 5 & 6-10 & 11-20 & 21+ (percent)")
    for key, lab in (("mpp", "messages per packet"), ("ppt", "packets per transaction"), ("mpt", "messages per transaction")):
        print(f"{lab} & {rows(key)} \\\\")
    print("% tight gaps: threshold & share of all packet gaps & share inside one transaction & Poisson share")
    for thr in (7.5, 16, 32):
        k = np.searchsorted(ge[1:], thr, side="right")
        a_, e_ = z["gpi"][:k+1].sum(), z["gpe"][:k+1].sum(); tot = z["gpi"].sum() + z["gpe"].sum()
        pp = z["gpP"][:k+1].sum() / z["gpP"].sum()
        print(f"{thr:g} & {(a_+e_)/tot*100:.2f} & {a_/(a_+e_)*100:.1f} & {pp*100:.2f} \\\\   % bin edge {ge[k+1]:.2f} us")
main()
