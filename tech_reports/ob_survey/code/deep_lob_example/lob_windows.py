import numpy as np

def smoothed_change(mid, H, past_average):
    """Relative change of the average of the next H mids, measured from the
    current mid (leak-free) or from the average of the last H mids (smoothed)."""
    T = len(mid)
    c = np.concatenate([[0.0], np.cumsum(mid)])
    t = np.arange(H - 1, T - H)                     # need H past and H future mids
    m_plus = (c[t + 1 + H] - c[t + 1]) / H          # mean of mid[t+1 .. t+H]
    m_minus = (c[t + 1] - c[t + 1 - H]) / H         # mean of mid[t-H+1 .. t]
    ref = m_minus if past_average else mid[t]
    return t, (m_plus - ref) / ref

def to_class(change, theta):
    return np.where(change > theta, 2, np.where(change < -theta, 0, 1))   # 0 down, 1 flat, 2 up

def windows(book, t, length=100):
    """Input window for each time t: the last `length` snapshots, a length x 40 matrix."""
    keep = t >= length - 1
    idx = t[keep][:, None] + np.arange(-length + 1, 1)[None, :]
    return book[idx], keep                           # shape (N, length, 40)
