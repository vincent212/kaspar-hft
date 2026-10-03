"""Illustrations of attention. Hand-specified vectors, not a trained model.
Each event type has a 2-number key: ask cancellation (2,0), buy trade (1,1), limit order (0,1).
Queries: a buy sweep looks for (1,0) 'sellers leaving'; a quiet limit order looks for (0,1) 'other limit orders'."""
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
def softmax(z): e = np.exp(z - z.max()); return e / e.sum()
# (a) worked example
fig, ax = plt.subplots(figsize=(6.5, 3.6))
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
ax.set_title(f"Query (1,0): scores, weights, and the output = weighted average of values = {out:.2f}", fontsize=10)
ax.spines[["top", "right"]].set_visible(False)
fig.tight_layout(); fig.savefig("figs/attention_scores.png", dpi=200)
# (b) same history, two queries
fig, ax = plt.subplots(figsize=(8.5, 3.8))
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
ax.set_title("The same eight earlier events, weighted differently depending on what the current event is", fontsize=10)
ax.spines[["top", "right"]].set_visible(False)
fig.tight_layout(); fig.savefig("figs/attention_two_queries.png", dpi=200)
# (c) attention matrix, causal
fig, ax = plt.subplots(figsize=(5.8, 5.4))
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
ax.set_title("Who attends to whom: each row sums to one; blank: the future is masked", fontsize=10)
for i in range(L):
    for j in range(i + 1):
        ax.text(j, i, f"{A[i, j]:.2f}", ha="center", va="center", fontsize=6.5)
fig.colorbar(im, ax=ax, fraction=0.04, label="attention weight")
fig.tight_layout(); fig.savefig("figs/attention_matrix.png", dpi=200)
print(np.round(w, 2), round(out, 3))
