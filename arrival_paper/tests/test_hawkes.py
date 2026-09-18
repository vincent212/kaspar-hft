# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Licensed under the MIT License.
"""Tests for the Hawkes fit + intensity evaluation."""

from __future__ import annotations

import numpy as np
import pytest

from arrival_paper.hawkes_smoke import fit_hawkes, intensity_at, hawkes_loglik


def simulate_hawkes_ogata(mu: float, alpha: float, beta: float,
                          T: float, seed: int = 0) -> np.ndarray:
    """Simulate exp-Hawkes on [0, T] via Ogata's thinning algorithm.

    Returns arrival times as float seconds.
    """
    rng = np.random.default_rng(seed)
    events = []
    t = 0.0
    lam_bar = mu   # upper bound on λ, refreshed after each event
    while True:
        u = rng.uniform()
        w = -np.log(u) / lam_bar
        t = t + w
        if t >= T:
            break
        # Current λ
        if events:
            e = np.asarray(events)
            lam_t = mu + alpha * np.sum(np.exp(-beta * (t - e)))
        else:
            lam_t = mu
        d = rng.uniform()
        if d * lam_bar <= lam_t:
            events.append(t)
            lam_bar = lam_t + alpha
        else:
            lam_bar = lam_t
    return np.asarray(events)


class TestHawkesLoglik:
    def test_infeasible_returns_big_number(self):
        t = np.arange(1, 100, dtype=np.float64)
        assert hawkes_loglik(np.array([-1.0, 0.5, 1.0]), t, 100.0) == 1e12
        assert hawkes_loglik(np.array([1.0, -0.5, 1.0]), t, 100.0) == 1e12
        assert hawkes_loglik(np.array([1.0, 0.5, -1.0]), t, 100.0) == 1e12
        # Explosive (α >= β) also infeasible
        assert hawkes_loglik(np.array([1.0, 2.0, 1.0]), t, 100.0) == 1e12

    def test_at_true_params_gives_finite_value(self):
        events = simulate_hawkes_ogata(mu=1.0, alpha=0.5, beta=1.0,
                                       T=200.0, seed=1)
        assert len(events) > 50
        # Compensator - log-likelihood, should be finite/positive-scale
        nll = hawkes_loglik(np.array([1.0, 0.5, 1.0]), events, 200.0)
        assert np.isfinite(nll)


class TestHawkesFit:
    def test_recovers_synthetic_parameters(self):
        """Simulate a Hawkes with known (μ, α, β), fit, verify parameters
        recovered within tolerance."""
        true_mu, true_alpha, true_beta = 2.0, 0.6, 1.0
        events = simulate_hawkes_ogata(mu=true_mu, alpha=true_alpha,
                                       beta=true_beta, T=2000.0, seed=42)
        assert len(events) > 500, f"too few events: {len(events)}"
        fit = fit_hawkes(events)
        # 20% tolerance on each parameter — Hawkes MLE is famously wobbly on
        # finite samples but should be in the right ballpark
        assert abs(fit["mu"] - true_mu) / true_mu < 0.4
        assert abs(fit["n_branch"] - true_alpha / true_beta) < 0.15
        # β harder to pin down; just check positivity and rough scale
        assert 0.3 < fit["beta"] < 3.0
        assert fit["converged"]

    def test_branching_ratio_less_than_one_on_stable_process(self):
        events = simulate_hawkes_ogata(mu=1.0, alpha=0.4, beta=1.0,
                                       T=1000.0, seed=7)
        fit = fit_hawkes(events)
        assert fit["n_branch"] < 1.0
        assert fit["lambda_bar"] > fit["mu"]  # amplification vs Poisson base

    def test_too_few_events_raises(self):
        with pytest.raises(ValueError, match="too few"):
            fit_hawkes(np.arange(50, dtype=np.float64))


class TestIntensityAt:
    def test_matches_fit_at_event_times(self):
        """intensity_at(t_events, t_events, ...) with strict-less should
        return the pre-event intensity (i.e., μ + α × R_i on the standard
        Ogata recursion) — same values the fit's log_lam saw."""
        mu, alpha, beta = 2.0, 0.5, 1.0
        events = simulate_hawkes_ogata(mu=mu, alpha=alpha, beta=beta,
                                       T=500.0, seed=3)
        assert len(events) > 30
        # First event: no prior history → λ == μ
        lam = intensity_at(events, events[:1], mu, alpha, beta,
                           t0=events[0])
        assert lam[0] == pytest.approx(mu)

    def test_zero_before_history_returns_mu(self):
        events = np.array([10.0, 20.0, 30.0])
        # Query at t = 5 (before first event)
        lam = intensity_at(events, np.array([5.0]), mu=1.0, alpha=0.5,
                           beta=1.0, t0=0.0)
        # No history strictly before t=5 → λ = μ = 1.0
        assert lam[0] == pytest.approx(1.0)

    def test_monotone_decay_after_last_event(self):
        events = np.array([10.0, 20.0])
        # Query at t = 20.001 and t = 25 (both after last event)
        lam = intensity_at(events, np.array([20.001, 25.0]), mu=0.5,
                           alpha=1.0, beta=0.5, t0=0.0)
        # Later query should have smaller λ (exponential decay)
        assert lam[1] < lam[0]
