"""Synthetic order book in which nothing about the future is predictable.

The mid-price is a random walk in event time: at each event it moves up one tick
with probability 0.1, down one tick with probability 0.1, and otherwise stays.
The spread is one tick. Sizes at every level are independent random integers.
Each snapshot has 40 numbers, in DeepLOB's order:
(p_a1, v_a1, p_b1, v_b1, ..., p_a10, v_a10, p_b10, v_b10).
"""
import numpy as np

TICK = 0.25

def make_day(n_events, start_mid, rng):
    steps = rng.choice([-1, 0, 1], size=n_events, p=[0.1, 0.8, 0.1])
    mid = start_mid + TICK * np.cumsum(steps)
    best_ask, best_bid = mid + TICK / 2, mid - TICK / 2
    book = np.empty((n_events, 40))
    for lvl in range(10):
        book[:, 4 * lvl + 0] = best_ask + lvl * TICK           # ask price
        book[:, 4 * lvl + 1] = rng.integers(1, 200, n_events)  # ask size
        book[:, 4 * lvl + 2] = best_bid - lvl * TICK           # bid price
        book[:, 4 * lvl + 3] = rng.integers(1, 200, n_events)  # bid size
    return book, mid

def make_days(n_days, n_events, seed=0):
    rng = np.random.default_rng(seed)
    days, start = [], 4000.0
    for _ in range(n_days):
        book, mid = make_day(n_events, start, rng)
        days.append((book, mid))
        start = mid[-1]
    return days
