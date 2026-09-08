#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 *
 * Shared helpers for the actor-framework microbenchmarks: a nanosecond clock,
 * a latency-sample accumulator with percentile stats, and a small results
 * table. New benches (throughput, contention, mailbox variants, ...) should
 * reuse these so their output is comparable.
 */

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace perf
{

using clk = std::chrono::steady_clock;

inline uint64_t now_ns() noexcept
{
  return static_cast<uint64_t>(
    std::chrono::duration_cast<std::chrono::nanoseconds>(clk::now().time_since_epoch()).count());
}

// Percentile summary of a set of latency samples (in nanoseconds).
struct LatencyStats
{
  std::string label;
  size_t count = 0;
  double p50 = 0, p90 = 0, p99 = 0, p999 = 0, min = 0, max = 0, mean = 0;
  // Amortized ns/op: total wall time of the measured phase / count, captured
  // with a SINGLE clock read pair. Free of the per-sample clock-read overhead
  // and quantization, so it resolves costs below the ~40ns clock tick.
  double amortized = 0;

  // Consumes (sorts) the sample buffer.
  static LatencyStats from(const std::string& label, std::vector<uint64_t>& samples)
  {
    LatencyStats s;
    s.label = label;
    s.count = samples.size();
    if (samples.empty())
      return s;
    std::sort(samples.begin(), samples.end());
    auto pct = [&](double q) -> double {
      // nearest-rank on a 0-based sorted array
      const size_t idx = static_cast<size_t>(std::llround(q * (samples.size() - 1)));
      return static_cast<double>(samples[idx]);
    };
    s.min = static_cast<double>(samples.front());
    s.max = static_cast<double>(samples.back());
    s.p50 = pct(0.50);
    s.p90 = pct(0.90);
    s.p99 = pct(0.99);
    s.p999 = pct(0.999);
    long double sum = 0;
    for (uint64_t v : samples)
      sum += v;
    s.mean = static_cast<double>(sum / samples.size());
    return s;
  }
};

// Prints a fixed-width table of round-trip latencies (all columns in ns).
inline void print_header(const char* title, size_t n, size_t warmup)
{
  std::printf("\n=== %s (measured=%zu, warmup=%zu) ===\n", title, n, warmup);
  std::printf("%-18s %9s %9s %9s %9s %9s %9s %11s %11s\n", "mode", "p50", "p90", "p99", "p99.9",
              "min", "max", "mean", "amort");
}

inline void print_row(const LatencyStats& s)
{
  std::printf("%-18s %9.0f %9.0f %9.0f %9.0f %9.0f %9.0f %11.1f %11.1f\n", s.label.c_str(), s.p50,
              s.p90, s.p99, s.p999, s.min, s.max, s.mean, s.amortized);
}

} // namespace perf
