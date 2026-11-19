#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// SimFills: an in-process Deribit behind FillsSession, for --sim runs. It makes
// one fill every fill_every_ms on the configured instrument (alternating buy
// and sell, trade_ids counting up), pushes it as a user.trades element,
// and answers reconcile() with every fill it has made, as REST does. With
// drop_every > 0 every drop_every-th fill is not pushed, so only the
// reconciliation reports it.

#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "deribit_pt/DeribitFills.hpp"

namespace deribit::pt
{
  class SimFills : public FillsSession
  {
  public:
    struct Config
    {
      std::string instrument = "BTC-PERPETUAL";
      int fill_every_ms = 0; // 0: no generated fills
      int drop_every = 0;
    };

    SimFills(const Config &c, FillsEvents &ev);
    ~SimFills() override;
    void logon() override;
    CallStatus reconcile(uint64_t begin_ms, uint64_t end_ms) override;
    void logout() override;

    static FillsSessionFactory make_factory(Config c);

  private:
    void run();

    Config cfg_;
    FillsEvents &ev_;
    std::mutex mu_;
    std::condition_variable cv_;
    bool stop_ = false, up_ = false, pass_ = false;
    uint64_t pass_begin_ = 0, pass_end_ = 0;
    std::vector<nlohmann::json> made_;
    std::thread thread_;
  };
}
