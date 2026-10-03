import numpy as np
from synthetic_book import TICK

def smoothed_change(mid, H, past_average):
    """Relative change of the average of the next H mids, measured from
    the current mid (leak-free) or from the average of the last H mids
    (smoothed)."""
    T = len(mid)
    c = np.concatenate([[0.0], np.cumsum(mid)])
    t = np.arange(H - 1, T - H)              # need H past and H future mids
    m_plus = (c[t + 1 + H] - c[t + 1]) / H   # mean of mid[t+1 .. t+H]
    m_minus = (c[t + 1] - c[t + 1 - H]) / H  # mean of mid[t-H+1 .. t]
    ref = m_minus if past_average else mid[t]
    return t, (m_plus - ref) / ref

def to_class(change, theta):
    # 0 down, 1 flat, 2 up
    return np.where(change > theta, 2, np.where(change < -theta, 0, 1))

def windows(book, t, length=100):
    """Input window for each time t: the last `length` snapshots,
    a length x 40 matrix."""
    keep = t >= length - 1
    idx = t[keep][:, None] + np.arange(-length + 1, 1)[None, :]
    return book[idx], keep                   # shape (N, length, 40)

def relative_prices(X, raw_windows):
    """Replace the price columns of each window by (price - mid at the
    window's last event), in ticks. Sizes keep their day-level
    normalisation."""
    X = X.copy()
    last = raw_windows[:, -1, :]
    mid_last = (last[:, 0] + last[:, 2]) / 2  # (best ask + best bid) / 2
    for col in range(0, 40, 2):               # columns 0, 2, ... are prices
        X[:, :, col] = (raw_windows[:, :, col] - mid_last[:, None]) / TICK
    return X
