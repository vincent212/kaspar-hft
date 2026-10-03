"""Train DeepLOB on a synthetic book in which nothing is predictable,
once with the smoothed label and once with the leak-free label."""
import sys
import numpy as np
import torch
from synthetic_book import make_days
from lob_windows import smoothed_change, to_class, windows, relative_prices
from lob_normalise import normalise_days, split_by_time
from lob_model import DeepLOB
from lob_train import train, predict
from lob_evaluate import score

H, LEN, LOOKBACK = 20, 100, 5
torch.manual_seed(0)
DEVICE = "mps" if torch.backends.mps.is_available() else "cpu"
RELATIVE = len(sys.argv) > 1 and sys.argv[1] == "relative"  # price input
raw = make_days(n_days=20, n_events=3000, seed=1)
days = normalise_days(raw, LOOKBACK)   # first days: statistics only
# keep raw prices for the relative-price variant
days = [(b, m, rb) for (b, m), (rb, _) in zip(days, raw[LOOKBACK:])]
tr, va, te = split_by_time(days, n_train=9, n_val=3)   # 9 / 3 / 3 days

def build(part, past_average, theta):
    Xs, ys, rule, last = [], [], [], []
    for book, mid, raw_book in part:
        t, ch = smoothed_change(mid, H, past_average)
        y = to_class(ch, theta)
        X, keep = windows(book, t, LEN)
        if RELATIVE:
            X = relative_prices(X, windows(raw_book, t, LEN)[0])
        y, tk = y[keep], t[keep]
        # baselines, using only information available at time t
        c = np.concatenate([[0.0], np.cumsum(mid)])
        past_mean = (c[tk + 1] - c[tk + 1 - H]) / H   # mean of last H mids
        r = to_class((mid[tk] - past_mean) / past_mean, theta)
        # label of t - H (flat before it exists)
        prev = np.concatenate([np.full(H, 1), y[:-H]])
        Xs.append(X); ys.append(y); rule.append(r); last.append(prev)
    cat = np.concatenate
    return (torch.tensor(cat(Xs), dtype=torch.float32),
            torch.tensor(cat(ys)), cat(rule), cat(last))

for past_average in (True, False):
    name = ("smoothed label (past average)" if past_average
            else "leak-free label (current mid)")
    prices = "relative to last mid" if RELATIVE else "z-scored by day"
    print(f"\n=== {name}; prices {prices} ===")
    # threshold: set on the training days so that about a third of
    # labels are 'flat'
    ch = np.concatenate([smoothed_change(m, H, past_average)[1]
                         for _, m, _ in tr])
    theta = np.quantile(np.abs(ch), 1 / 3)
    X_tr, y_tr, _, _ = build(tr, past_average, theta)
    X_va, y_va, _, _ = build(va, past_average, theta)
    X_te, y_te, rule_te, last_te = build(te, past_average, theta)
    shares = np.bincount(y_te.numpy(), minlength=3) / len(y_te)
    print("class shares (test):", shares)
    model = train(DeepLOB(), X_tr, y_tr, X_va, y_va, device=DEVICE)
    score("DeepLOB", y_te.numpy(), predict(model, X_te, DEVICE).numpy())
    score("past-only rule", y_te.numpy(), rule_te)
    score("repeat last known label", y_te.numpy(), last_te)
    score("always the most common class", y_te.numpy(),
          np.full(len(y_te), np.bincount(y_tr.numpy()).argmax()))
    sys.stdout.flush()
