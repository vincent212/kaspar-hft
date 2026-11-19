/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "okx_pt/SimFills.hpp"

#include <chrono>

namespace okx::pt
{
  using json = nlohmann::json;

  SimFills::SimFills(const Config &c, FillsEvents &ev) : cfg_(c), ev_(ev), thread_([this] { run(); }) {}

  SimFills::~SimFills()
  {
    {
      std::lock_guard<std::mutex> l(mu_);
      stop_ = true;
    }
    cv_.notify_all();
    thread_.join();
  }

  FillsSessionFactory SimFills::make_factory(Config c)
  {
    return [c](const FillsConfig &, FillsEvents &ev) { return std::make_unique<SimFills>(c, ev); };
  }

  void SimFills::logon()
  {
    {
      std::lock_guard<std::mutex> l(mu_);
      up_ = true;
    }
    ev_.logged_in();
  }

  CallStatus SimFills::reconcile(uint64_t begin_ms)
  {
    std::lock_guard<std::mutex> l(mu_);
    if (!up_)
      return CallStatus::Disconnected;
    pass_ = true;
    pass_begin_ = begin_ms;
    cv_.notify_all();
    return CallStatus::Success;
  }

  void SimFills::logout()
  {
    std::lock_guard<std::mutex> l(mu_);
    up_ = false;
  }

  void SimFills::run()
  {
    using namespace std::chrono;
    std::unique_lock<std::mutex> l(mu_);
    auto next = steady_clock::now() + milliseconds(cfg_.fill_every_ms > 0 ? cfg_.fill_every_ms : 3600000);
    uint64_t n = 0;
    while (!stop_)
    {
      cv_.wait_until(l, next, [&] { return stop_ || pass_; });
      if (stop_)
        break;
      if (pass_)
      {
        pass_ = false;
        size_t rows = 0;
        for (const auto &f : made_)
          if (std::stoull(f["fillTime"].get<std::string>()) >= pass_begin_)
          {
            ev_.fill(f, true);
            ++rows;
          }
        ev_.reconciled(rows, true, "");
        continue;
      }
      if (cfg_.fill_every_ms <= 0 || steady_clock::now() < next)
        continue;
      next += milliseconds(cfg_.fill_every_ms);
      ++n;
      const uint64_t ms = uint64_t(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
      json f = {{"instId", cfg_.inst_id},
                {"tradeId", std::to_string(900000 + n)},
                {"ordId", std::to_string(700000 + n)},
                {"clOrdId", std::to_string(n)},
                {"side", n % 2 ? "buy" : "sell"},
                {"fillSz", "0.0001"},
                {"fillPx", "63123.4"},
                {"fillTime", std::to_string(ms)},
                {"state", "filled"}};
      made_.push_back(f);
      if (up_ && !(cfg_.drop_every > 0 && n % uint64_t(cfg_.drop_every) == 0))
        ev_.fill(f, false);
    }
  }
}
