#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// SimDropCopy: an in-process Coinbase Exchange drop-copy session for --sim
// runs and the client's tests. After the Logon it sends, every trade_ms, an
// order's New followed by a Trade report (and every resend_every-th trade a
// second copy of it with PossDupFlag=Y, as a resend would). Fields as the
// drop-copy page lists for ExecutionReport. Its thread is a TimedRunner.

#include <cstdint>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "coinbase_exchange_oe/CbxSession.hpp"
#include "frame/som/TimedRunner.hpp"

namespace coinbase_exchange::pt
{
  class SimDropCopy
  {
  public:
    struct Config
    {
      int trade_ms = 20;
      int max_trades = 10;           // then stop
      int resend_every = 3;          // 0: never
      std::vector<std::string> symbols{"BTC-USD"};
    };
    explicit SimDropCopy(Config c);
    ~SimDropCopy();
    oe::SessionFactory factory();
    int trades_sent() const;

  private:
    class Session;
    void tick(uint64_t id);
    Config cfg_;
    mutable std::mutex mu_;
    oe::SessionEvents *live_ = nullptr;
    uint64_t live_id_ = 0, next_id_ = 1;
    std::set<uint64_t> alive_;
    int trades_ = 0;
    std::mt19937_64 rng_{99};
    frame::som::TimedRunner runner_;
  };
}
