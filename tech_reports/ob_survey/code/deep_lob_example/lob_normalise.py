import numpy as np

def normalise_days(days, lookback=5):
    """z-score each day with the mean and std of the previous `lookback` days.
    The first `lookback` days only supply statistics and are dropped."""
    out = []
    for d in range(lookback, len(days)):
        past = np.concatenate([days[k][0] for k in range(d - lookback, d)])
        mu, sd = past.mean(axis=0), past.std(axis=0) + 1e-8
        book, mid = days[d]
        out.append(((book - mu) / sd, mid))
    return out

def split_by_time(items, n_train, n_val):
    """Train on the earliest days, validate on the next ones, test on the rest. No shuffling."""
    return items[:n_train], items[n_train:n_train + n_val], items[n_train + n_val:]
