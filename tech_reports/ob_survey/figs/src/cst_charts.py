"""Charts for Section 6.1 (Cont-Stoikov-Talreja), drawn from the published
tables in Cont, Stoikov & Talreja (2010) as reproduced in Tables 7-9 of the survey.
Only the race-to-zero chart uses simulation (stylised, seeded)."""
import numpy as np, matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from pathlib import Path
OUT = Path(__file__).resolve().parents[1]
plt.rcParams.update({"font.size": 9, "axes.spines.top": False, "axes.spines.right": False})

# Table 8: rows = bid 1..5, cols = ask 1..5 ; (empirical, model)
EMP = np.array([[.512,.304,.263,.242,.226],[.691,.502,.444,.376,.359],[.757,.601,.533,.472,.409],
                [.806,.672,.580,.529,.484],[.822,.731,.640,.714,.606]])
MOD = np.array([[.500,.336,.259,.216,.188],[.664,.500,.407,.348,.307],[.741,.593,.500,.437,.391],
                [.784,.652,.563,.500,.452],[.812,.693,.609,.548,.500]])
# Table 9: P(order at back of best bid filled before mid moves), model
FILL = np.array([[.497,.641,.709,.749,.776],[.302,.449,.535,.591,.631],[.206,.336,.422,.483,.528],
                 [.152,.263,.344,.404,.452],[.118,.213,.287,.346,.393]])
q = np.arange(1, 6)
cols = plt.cm.viridis(np.linspace(0, 0.85, 5))

# 1. P(up) vs bid size, one line per ask size
fig, ax = plt.subplots(figsize=(5.2, 3.4))
for j in range(5):
    ax.plot(q, MOD[:, j], "-", color=cols[j], label=f"ask = {j+1}")
    ax.plot(q, EMP[:, j], "o", color=cols[j], ms=4)
ax.axhline(0.5, color="grey", lw=0.6, ls=":")
ax.set_xlabel("orders at the best bid"); ax.set_ylabel("P(next mid move is up)")
ax.set_xticks(q); ax.set_ylim(0, 1)
ax.legend(title="lines: model; dots: data", fontsize=7, title_fontsize=7, frameon=False, ncol=1)
fig.tight_layout(); fig.savefig(OUT / "cst_pup_lines.png", dpi=200); plt.close(fig)

# 2. Fill-before-move vs P(up), one point per (bid, ask) cell (model values)
fig, ax = plt.subplots(figsize=(5.2, 3.4))
for j in range(5):
    ax.scatter(MOD[:, j], FILL[:, j], color=cols[j], s=18, label=f"ask = {j+1}")
for i in range(5):
    for j in range(5):
        ax.annotate(f"{i+1},{j+1}", (MOD[i, j], FILL[i, j]), fontsize=5.5, xytext=(2, 2),
                    textcoords="offset points", color="0.35")
r = np.corrcoef(MOD.ravel(), FILL.ravel())[0, 1]
ax.set_xlabel("P(next mid move is up)")
ax.set_ylabel("P(bid order filled before the mid moves)")
ax.set_title(f"labels: (bid, ask) orders; correlation {r:.2f}", fontsize=8)
ax.legend(fontsize=7, frameon=False)
fig.tight_layout(); fig.savefig(OUT / "cst_fill_vs_pup.png", dpi=200); plt.close(fig)

# 5. Fill probability vs place in queue
fig, ax = plt.subplots(figsize=(5.2, 3.2))
for j in range(5):
    ax.plot(q, FILL[:, j], "o-", color=cols[j], ms=3.5, label=f"ask = {j+1}")
ax.set_xlabel("orders at the best bid, including ours (we are last)")
ax.set_ylabel("P(filled before the mid moves)")
ax.set_xticks(q); ax.set_ylim(0, 0.85); ax.legend(fontsize=7, frameon=False)
fig.tight_layout(); fig.savefig(OUT / "cst_fill_lines.png", dpi=200); plt.close(fig)

# 6. CST model vs Cont-de Larrard arctan formula vs data
fig, axes = plt.subplots(1, 3, figsize=(7.2, 2.6), sharey=True)
for k, j in enumerate([0, 2, 4]):
    ax = axes[k]
    b = np.linspace(0.5, 5.5, 200)
    ax.plot(b, 2/np.pi*np.arctan(b/(j+1)), "--", color="C3", label="Cont-de Larrard")
    ax.plot(q, MOD[:, j], "-", color="C0", label="CST model")
    ax.plot(q, EMP[:, j], "o", color="k", ms=3.5, label="data")
    ax.set_title(f"ask = {j+1} order" + ("s" if j else ""), fontsize=8); ax.set_xticks(q)
    ax.set_xlabel("orders at the best bid"); ax.set_ylim(0, 1)
axes[0].set_ylabel("P(next mid move is up)")
axes[0].legend(fontsize=6.5, frameon=False, loc="lower right")
fig.tight_layout(); fig.savefig(OUT / "cst_vs_cdl.png", dpi=200); plt.close(fig)

# 3. Two counters racing to zero (stylised simulation with Table 7 rates at distance 1)
rL, rM, rc = 1.85, 0.94, 0.71          # per minute
def race(b0, a0, rng):
    t, b, a = 0.0, b0, a0
    T, B, A = [0.0], [b], [a]
    while b > 0 and a > 0:
        rates = np.array([rL, rM + rc*b, rL, rM + rc*a])   # bid +, bid -, ask +, ask -
        t += rng.exponential(1/rates.sum())
        k = rng.choice(4, p=rates/rates.sum())
        b += (1, -1, 0, 0)[k]; a += (0, 0, 1, -1)[k]
        T.append(t); B.append(b); A.append(a)
    return np.array(T), np.array(B), np.array(A)
# same starting book (3 bid, 2 ask); seeds chosen so the panels show both outcomes
paths = []
seed = 0
while len(paths) < 3:
    T, B, A = race(3, 2, np.random.default_rng(seed)); seed += 1
    up = A[-1] == 0
    want = [True, False, True][len(paths)]
    if up == want and 6 <= len(T) <= 40:
        paths.append((T, B, A))
fig, axes = plt.subplots(1, 3, figsize=(7.2, 2.5), sharey=True)
for ax, (T, B, A) in zip(axes, paths):
    ax.step(T, B, where="post", color="C2", label="best bid")
    ax.step(T, A, where="post", color="C3", label="best ask")
    ax.axhline(0, color="grey", lw=0.6)
    winner = "ask emptied: mid up" if A[-1] == 0 else "bid emptied: mid down"
    ax.plot(T[-1], 0, "kx"); ax.set_title(winner, fontsize=8)
    ax.set_xlabel("minutes")
axes[0].set_ylabel("orders in queue"); axes[0].legend(fontsize=7, frameon=False)
fig.tight_layout(); fig.savefig(OUT / "cst_race_paths.png", dpi=200); plt.close(fig)
print("ok", r)

# 7. Adverse selection by queue position (stylised simulation, Table 7 rates, unit orders)
def adverse_by_position(n_bid=6, n_ask=3, n_paths=15000, seed=11):
    rng = np.random.default_rng(seed)
    out = []
    for pos in range(1, n_bid + 1):
        filled = down_next = 0
        for _ in range(n_paths):
            ahead, behind, ask = pos - 1, n_bid - pos, n_ask
            # phase 1: until our order fills or the ask empties (price moves up, away from us)
            while True:
                r = np.array([rM, rc*ahead, rc*behind, rL, rL, rM + rc*ask])
                k = rng.choice(6, p=r/r.sum())
                if k == 0:
                    if ahead > 0: ahead -= 1
                    else: break                      # our order is hit: filled
                elif k == 1: ahead -= 1
                elif k == 2: behind -= 1
                elif k == 3: behind += 1             # new bid joins behind us
                elif k == 4: ask += 1
                else:
                    ask -= 1
                    if ask == 0: break
            if ask == 0 and not (k == 0 and ahead == 0):
                continue                             # price moved up before we were filled
            filled += 1
            # phase 2: which way does the mid move next, from (behind, ask)?
            b = behind
            if b == 0:
                down_next += 1; continue
            a = ask
            while b > 0 and a > 0:
                r = np.array([rL, rM + rc*b, rL, rM + rc*a])
                k = rng.choice(4, p=r/r.sum())
                b += (1, -1, 0, 0)[k]; a += (0, 0, 1, -1)[k]
            down_next += (b == 0)
        out.append((pos, filled/n_paths, down_next/max(filled, 1)))
    return np.array(out)

res = adverse_by_position()
rng_u = np.random.default_rng(5)
def p_down(b, a, n=20000):
    d = 0
    for _ in range(n):
        while b > 0 and a > 0:
            r = np.array([rL, rM + rc*b, rL, rM + rc*a]); k = rng_u.choice(4, p=r/r.sum())
            b += (1, -1, 0, 0)[k]; a += (0, 0, 1, -1)[k]
        d += (b == 0); b, a = b0, a0
    return d/n
b0, a0 = 6, 3
base = p_down(b0, a0)
fig, axes = plt.subplots(1, 2, figsize=(7.2, 2.8))
axes[0].bar(res[:, 0], res[:, 1], color="C0")
axes[0].set_xlabel("our position in the bid queue (1 = front)")
axes[0].set_ylabel("P(filled before the mid moves)"); axes[0].set_ylim(0, 1)
axes[1].bar(res[:, 0], res[:, 2], color="C3", label="given that we were filled")
axes[1].axhline(base, color="k", ls="--", lw=1, label="unconditional, same book")
axes[1].set_xlabel("our position in the bid queue (1 = front)")
axes[1].set_ylabel("P(next mid move is down)"); axes[1].set_ylim(0, 1)
axes[1].legend(fontsize=7, frameon=False, loc="lower right")
fig.tight_layout(); fig.savefig(OUT / "cst_adverse_position.png", dpi=200); plt.close(fig)
print("adverse:", np.round(res, 3), "base", round(base, 3))

# 8. Queue-reactive vs CST: rate shapes and the long-run queue-size distribution (stylised)
x = np.arange(0, 61)
rL_qr = np.where(x == 0, 0.6, 1.0)                     # roughly constant arrivals, lower when empty
rC_qr = 0.12*np.minimum(x, 25)**0.7                    # concave, flat beyond ~25
rM_qr = 0.6*np.exp(-x/6.0)                             # market orders hit short queues
rL_cst = np.full_like(x, 1.0, dtype=float)
rC_cst = 0.045*x                                       # proportional cancellation
rM_cst = np.full_like(x, 0.15, dtype=float)
def invariant(rin, rcan, rmkt, xmax=60):
    # birth-death balance: pi(x+1)/pi(x) = r_L(x) / (r_C(x+1) + r_M(x+1))
    pi = np.ones(xmax+1)
    for k in range(xmax):
        pi[k+1] = pi[k]*rin[k]/(rcan[k+1] + rmkt[k+1])
    return pi/pi.sum()
pq, pc = invariant(rL_qr, rC_qr, rM_qr), invariant(rL_cst, rC_cst, rM_cst)
fig, axes = plt.subplots(1, 2, figsize=(7.2, 2.8))
ax = axes[0]
ax.plot(x, rL_qr, color="C2", label="limit orders"); ax.plot(x, rC_qr, color="C1", label="cancellations")
ax.plot(x, rM_qr, color="C3", label="market orders")
ax.plot(x, rC_cst, color="C1", ls="--", lw=0.9, label="cancellations, CST (proportional)")
ax.plot(x, rM_cst, color="C3", ls="--", lw=0.9, label="market orders, CST (constant)")
ax.set_xlabel("queue size $x$"); ax.set_ylabel("rate (per unit time)"); ax.set_ylim(0, 2.2)
ax.legend(fontsize=6, frameon=False, loc="upper left")
ax.set_title("rate shapes (solid: queue-reactive)", fontsize=8)
ax = axes[1]
ax.bar(x, pq, color="C0", alpha=0.7, label="queue-reactive rates")
ax.step(x, pc, where="mid", color="k", lw=1, label="CST-style rates")
ax.set_xlabel("queue size $x$"); ax.set_ylabel("long-run probability"); ax.set_xlim(-0.5, 45)
ax.legend(fontsize=7, frameon=False); ax.set_title("long-run distribution of the queue size", fontsize=8)
fig.tight_layout(); fig.savefig(OUT / "qr_rates_invariant.png", dpi=200); plt.close(fig)
print("qr means", (x*pq).sum(), (x*pc).sum())

# 9. A queue-reactive simulator run (stylised rates of chart 8, two levels per side)
def qr_rates(q):
    # same shapes as chart 8, scaled to short queues so that price moves are visible
    rl = 0.6 if q == 0 else 1.0
    rc = 0.30*min(q, 8)**0.7
    rm = 0.8*np.exp(-q/3.0) if q > 0 else 0.0
    return rl, rc, rm
xs = np.arange(0, 41)
pi_q = invariant(np.array([qr_rates(k)[0] for k in xs]), np.array([qr_rates(k)[1] for k in xs]),
                 np.array([qr_rates(k)[2] for k in xs]), xmax=40)
def draw_queue(rng): return rng.choice(len(pi_q), p=pi_q)
def simulate_qr(T=600.0, pi_move=0.7, pi_reinit=0.85, seed=4):
    rng = np.random.default_rng(seed)
    # queues: bid level 1,2 and ask level 1,2 ; price in ticks (mid)
    bid = [draw_queue(rng), draw_queue(rng)]; ask = [draw_queue(rng), draw_queue(rng)]
    t, price = 0.0, 0.0
    ts, b1, a1, px, resets = [0.0], [bid[0]], [ask[0]], [0.0], []
    while t < T:
        ev = []
        for side, Q in (("b", bid), ("a", ask)):
            for lvl in (0, 1):
                rl, rc, rm = qr_rates(Q[lvl])
                ev += [(side, lvl, +1, rl), (side, lvl, -1, rc*(Q[lvl] > 0))]
                if lvl == 0: ev.append((side, 0, -1, rm))
        rates = np.array([e[3] for e in ev]); t += rng.exponential(1/rates.sum())
        side, lvl, d, _ = ev[rng.choice(len(ev), p=rates/rates.sum())]
        Q = bid if side == "b" else ask
        Q[lvl] = max(Q[lvl] + d, 0)
        if lvl == 0 and Q[0] == 0 and rng.random() < pi_move:
            price += 1 if side == "a" else -1          # ask emptied: up; bid emptied: down
            if rng.random() < pi_reinit:
                bid = [draw_queue(rng), draw_queue(rng)]; ask = [draw_queue(rng), draw_queue(rng)]
                resets.append(t)
            else:                                       # shift: next level becomes best
                if side == "a": ask = [ask[1], draw_queue(rng)]; bid = [draw_queue(rng), bid[0]]
                else: bid = [bid[1], draw_queue(rng)]; ask = [draw_queue(rng), ask[0]]
        ts.append(t); b1.append(bid[0]); a1.append(ask[0]); px.append(price)
    return np.array(ts), np.array(b1), np.array(a1), np.array(px), np.array(resets)
for seed in range(4, 60):
    ts, b1, a1, px, resets = simulate_qr(seed=seed)
    d = np.diff(px)
    if (d > 0).sum() >= 4 and (d < 0).sum() >= 4 and np.count_nonzero(d) > len(resets): break
fig, axes = plt.subplots(2, 1, figsize=(7.2, 3.8), sharex=True, gridspec_kw={"height_ratios": [1.3, 1]})
axes[0].step(ts, b1, where="post", color="C2", lw=0.6, label="best bid queue")
axes[0].step(ts, a1, where="post", color="C3", lw=0.6, label="best ask queue")
axes[0].set_ylabel("orders"); axes[0].legend(fontsize=7, frameon=False, ncol=2, loc="upper right")
axes[1].step(ts, px, where="post", color="k", lw=0.9)
for r in resets: axes[1].axvline(r, color="C0", lw=0.4, alpha=0.5)
axes[1].set_ylabel("price (ticks)"); axes[1].set_xlabel("time (simulated units)")
axes[1].set_title("price; thin blue lines: price moves that came with a reset of the book", fontsize=8)
fig.tight_layout(); fig.savefig(OUT / "qr_sim_path.png", dpi=200); plt.close(fig)
print("qr sim events", len(ts), "moves", np.count_nonzero(np.diff(px)), "resets", len(resets))
