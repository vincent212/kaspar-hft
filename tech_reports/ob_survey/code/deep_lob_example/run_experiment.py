"""Train DeepLOB on a synthetic book in which nothing is predictable,
once with the smoothed label and once with the leak-free label."""
import sys
import numpy as np
import torch
from synthetic_book import make_days
from lob_windows import smoothed_change, to_class, windows
from lob_normalise import normalise_days, split_by_time
from lob_model import DeepLOB
from lob_train import train, predict
from lob_evaluate import score

H, LEN = 20, 100
torch.manual_seed(0)
DEVICE = "mps" if torch.backends.mps.is_available() else "cpu"
days = normalise_days(make_days(n_days=20, n_events=3000, seed=1))   # 5 days used for statistics
tr, va, te = split_by_time(days, n_train=9, n_val=3)                    # 9 / 3 / 3 days

def build(part, past_average, theta):
    Xs, ys, rule, last = [], [], [], []
    for book, mid in part:
        t, ch = smoothed_change(mid, H, past_average)
        y = to_class(ch, theta)
        X, keep = windows(book, t, LEN)
        y, tk = y[keep], t[keep]
        # baselines, using only information available at time t
        past_mean = np.array([mid[i - H + 1:i + 1].mean() for i in tk])
        r = to_class((mid[tk] - past_mean) / past_mean, theta)
        prev = np.concatenate([np.full(H, 1), y[:-H]])   # label of t - H (flat before it exists)
        Xs.append(X); ys.append(y); rule.append(r); last.append(prev)
    cat = np.concatenate
    return (torch.tensor(cat(Xs), dtype=torch.float32), torch.tensor(cat(ys)),
            cat(rule), cat(last))

for past_average in (True, False):
    name = "smoothed label (past average)" if past_average else "leak-free label (current mid)"
    print(f"\n=== {name} ===")
    # threshold: set on the training days so that about a third of labels are 'flat'
    ch = np.concatenate([smoothed_change(m, H, past_average)[1] for _, m in tr])
    theta = np.quantile(np.abs(ch), 1 / 3)
    X_tr, y_tr, _, _ = build(tr, past_average, theta)
    X_va, y_va, _, _ = build(va, past_average, theta)
    X_te, y_te, rule_te, last_te = build(te, past_average, theta)
    print("class shares (test):", np.bincount(y_te.numpy(), minlength=3) / len(y_te))
    model = train(DeepLOB(), X_tr, y_tr, X_va, y_va, device=DEVICE)
    score("DeepLOB", y_te.numpy(), predict(model, X_te, DEVICE).numpy())
    score("past-only rule", y_te.numpy(), rule_te)
    score("repeat last known label", y_te.numpy(), last_te)
    score("always the most common class", y_te.numpy(),
          np.full(len(y_te), np.bincount(y_tr.numpy()).argmax()))
    sys.stdout.flush()
