"""Illustrations of attention. Hand-specified vectors, not a trained model.
Each event type has a 2-number key: ask cancellation (2,0), buy trade (1,1), limit order (0,1).
Queries: a buy sweep looks for (1,0) 'sellers leaving'; a quiet limit order looks for (0,1) 'other limit orders'."""
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
def softmax(z): e = np.exp(z - z.max()); return e / e.sum()
fig = plt.figure(figsize=(12.5, 3.9))
gs = fig.add_gridspec(1, 3, width_ratios=[1.05, 1.35, 1.0])
# (a) worked example
ax = fig.add_subplot(gs[0])
keys = np.array([[2, 0], [0, 1], [1, 1]]); q = np.array([1, 0]); vals = np.array([1, 0, 0.5])
sc = keys @ q; w = softmax(sc); out = w @ vals
x = np.arange(3)
ax.bar(x - 0.2, sc, 0.38, color="#bbbbbb", label="match score (query · key)")
ax.bar(x + 0.2, w, 0.38, color="#d94801", label="weight after softmax")
for i in range(3):
    ax.text(i - 0.2, sc[i] + 0.05, f"{sc[i]:.0f}", ha="center", fontsize=8)
    ax.text(i + 0.2, w[i] + 0.05, f"{w[i]:.2f}", ha="center", fontsize=8)
ax.set_xticks(x, ["event 1\nkey (2,0)\nvalue 1", "event 2\nkey (0,1)\nvalue 0", "event 3\nkey (1,1)\nvalue 0.5"], fontsize=7)
ax.set_ylim(0, 2.6); ax.legend(fontsize=7, frameon=False, loc="upper right")
ax.set_title(f"(a) Query (1,0): scores, weights, and the\noutput = weighted average of values = {out:.2f}", fontsize=9)
# (b) same history, two queries
ax = fig.add_subplot(gs[1])
hist = ["limit\nbid", "cancel\nask", "limit\nask", "cancel\nask", "trade\nbuy", "limit\nbid", "cancel\nask", "limit\nbid"]
kmap = {"cancel": np.array([2, 0]), "trade": np.array([1, 1]), "limit": np.array([0, 1])}
K = np.array([kmap[h.split("\n")[0]] for h in hist])
for qv, col, lab, off in [(np.array([1, 0]), "#d94801", "current event: a buy sweep\n(query looks for sellers leaving)", -0.2),
                          (np.array([0, 1]), "#2171b5", "current event: a quiet limit order\n(query looks for other limit orders)", 0.2)]:
    ww = softmax(K @ qv)
    ax.bar(np.arange(len(hist)) + off, ww, 0.38, color=col, label=lab)
ax.set_xticks(np.arange(len(hist)), hist, fontsize=7)
ax.set_ylabel("attention weight", fontsize=8); ax.set_ylim(0, 0.42)
ax.legend(fontsize=7, frameon=False, loc="upper left")
ax.set_title("(b) The same eight earlier events, weighted differently\ndepending on what the current event is", fontsize=9)
# (c) attention matrix, causal
ax = fig.add_subplot(gs[2])
seq = ["limit bid", "cancel ask", "limit ask", "cancel ask", "trade buy", "limit bid", "cancel ask", "trade buy"]
Ks = np.array([kmap[s.split()[0]] for s in seq])
qmap = {"trade": np.array([1, 0]), "cancel": np.array([1, 0.5]), "limit": np.array([0, 1])}
Qs = np.array([qmap[s.split()[0]] for s in seq])
L = len(seq); A = np.full((L, L), np.nan)
for i in range(L):
    A[i, :i + 1] = softmax(Ks[:i + 1] @ Qs[i])
im = ax.imshow(A, cmap="Oranges", vmin=0, vmax=1)
ax.set_xticks(range(L), seq, rotation=90, fontsize=6.5); ax.set_yticks(range(L), seq, fontsize=6.5)
ax.set_xlabel("earlier event (key)", fontsize=8); ax.set_ylabel("current event (query)", fontsize=8)
ax.set_title("(c) Who attends to whom: each row sums to one;\nblank: the future is masked", fontsize=9)
for a in fig.axes[:2]:
    a.spines[["top", "right"]].set_visible(False); a.tick_params(labelsize=7)
fig.text(0.99, 0.005, "Hand-specified illustrative vectors, not a trained model", ha="right", fontsize=7, style="italic")
fig.tight_layout(rect=[0, 0.05, 1, 1]); fig.savefig("figs/attention_illustrated.png", dpi=200)
print(np.round(w, 2), round(out, 3))
