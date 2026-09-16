/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// KASPR: LatencyProbe factory function

#include "frame/perf/act/LatencyProbe.hpp"
#include "interface/frame/perf/if/LatencyProbe.hpp"

actor_ptr create_LatencyProbe(actor_ptr ob,
                              int sym,
                              const char *tag,
                              int bin_ms,
                              const std::string &csv_path,
                              int flush_s,
                              std::size_t max_bins)
{
    return new frame::perf::act::LatencyProbe(ob, sym, tag, bin_ms,
                                              csv_path, flush_s, max_bins);
}
