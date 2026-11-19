#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// SimFillSource: an in-process Bybit for --sim runs and demos. Logs in at
// once; every fill_every_ms it makes one Trade execution in one of the
// configured instruments (alternating sides, 1 to 5 units, around a fixed
// price) and delivers it as the stream would; reconcile() delivers every
// execution it has made in the window again, as REST would, so the client's
// duplicate handling runs on every reconciliation.

#include <mutex>
#include <thread>
#include <vector>

#include "bybit_post_trade/BybitFills.hpp"
#include "bybit_post_trade/FillSource.hpp"

namespace bybit_pt
{
  class SimFillSource : public FillSource
  {
  public:
    SimFillSource(SimConfig cfg, Instruments ins, FillEvents &ev);
    ~SimFillSource() override;
    void logon() override;
    void reconcile(const std::string &category, uint64_t from_ms, uint64_t to_ms) override;
    void logout() override;

    static SourceFactory make_factory(SimConfig cfg, Instruments ins);

  private:
    struct Made
    {
      std::string category, json;
      uint64_t ms;
    };
    SimConfig cfg_;
    Instruments ins_;
    FillEvents &ev_;
    std::mutex mu_;
    std::vector<Made> made_;
    std::jthread gen_;
    uint64_t n_ = 0;
  };
}
