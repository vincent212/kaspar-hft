#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "logger/act/Logger.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace mdp3
{
    // Fixed-bucket latency histogram for per-stage timing: 100 ns bins to 50 us,
    // 1 us bins to 2 ms, then one overflow bin. O(1) to record, cheap to
    // summarize, so logging it does not stall the thread that owns it.
    struct StageHist
    {
        static constexpr uint32_t kFine = 500, kCoarse = 1950, kBins = kFine + kCoarse + 1;
        std::vector<uint64_t> bins = std::vector<uint64_t>(kBins, 0);
        uint64_t n = 0, max = 0;

        void add(int64_t ns)
        {
            if (ns < 0) ns = 0;
            uint64_t v = uint64_t(ns);
            uint32_t b = v < 50000 ? uint32_t(v / 100)
                       : v < 2000000 ? kFine + uint32_t((v - 50000) / 1000)
                       : kBins - 1;
            ++bins[b];
            ++n;
            if (v > max) max = v;
        }

        // Upper edge of the bin holding quantile p, in us.
        double q(double p) const
        {
            if (!n) return 0;
            uint64_t want = uint64_t(p * double(n - 1)) + 1, c = 0;
            for (uint32_t b = 0; b < kBins; ++b)
                if ((c += bins[b]) >= want)
                    return b < kFine ? (b + 1) * 0.1
                         : b < kFine + kCoarse ? 50.0 + (b - kFine + 1)
                         : double(max) / 1000.0;
            return double(max) / 1000.0;
        }

        void clear() { std::fill(bins.begin(), bins.end(), 0); n = max = 0; }
    };

    // A named set of StageHists, logged and cleared every `period_ns` of the
    // caller's clock. Log line: "<owner> STAGE <name> n=.. p1=.. ... max=.. us".
    template <int N>
    struct StageSet
    {
        StageHist h[N];
        uint64_t last_flush = 0;

        void maybe_flush(uint64_t now, const char *owner, const char *const (&names)[N],
                         uint64_t period_ns = 10000000000ULL)
        {
            if (!last_flush)
            {
                last_flush = now;
                return;
            }
            if (now - last_flush <= period_ns)
                return;
            for (int s = 0; s < N; ++s)
                log_inf("%s STAGE %s n=%lu p1=%.1f p10=%.1f p50=%.1f p90=%.1f p99=%.1f p999=%.1f max=%.1f us",
                        owner, names[s], h[s].n, h[s].q(.01), h[s].q(.10), h[s].q(.5),
                        h[s].q(.9), h[s].q(.99), h[s].q(.999), double(h[s].max) / 1000.0);
            for (auto &x : h)
                x.clear();
            last_flush = now;
        }
    };
}
