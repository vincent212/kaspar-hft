/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "bybit_post_trade/SimFillSource.hpp"

#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>

namespace bybit_pt
{

SimFillSource::SimFillSource(SimConfig cfg, Instruments ins, FillEvents &ev)
    : cfg_(cfg), ins_(std::move(ins)), ev_(ev)
{
}

SimFillSource::~SimFillSource() { logout(); }

void SimFillSource::logon()
{
  ev_.logged_in();
  if (cfg_.fill_every_ms <= 0 || ins_.empty())
    return;
  gen_ = std::jthread([this](std::stop_token st) {
    std::mutex m;
    std::condition_variable_any cv;
    std::unique_lock<std::mutex> l(m);
    while (!cv.wait_for(l, st, std::chrono::milliseconds(cfg_.fill_every_ms), [] { return false; }) &&
           !st.stop_requested())
    {
      const uint64_t k = ++n_;
      auto it = ins_.begin();
      std::advance(it, (k + cfg_.seed) % ins_.size());
      const auto &in = it->second;
      const uint64_t ms = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::system_clock::now().time_since_epoch())
                                       .count());
      char qty[32], px[32], id[48];
      int dec = 0; // decimals of qty_unit: Bybit sends plain decimals, never exponents
      for (double u = in.qty_unit; dec < 12 && std::fabs(u - std::round(u)) > 1e-9; u *= 10)
        ++dec;
      std::snprintf(qty, sizeof qty, "%.*f", dec, double(1 + k % 5) * in.qty_unit);
      std::snprintf(px, sizeof px, "%.1f", 90000.0 + double(k % 7) * 0.5);
      std::snprintf(id, sizeof id, "sim-%llu-%llu", (unsigned long long)cfg_.seed, (unsigned long long)k);
      nlohmann::json e = {{"category", in.category}, {"symbol", in.symbol}, {"execId", id},
                          {"orderId", "sim-order-" + std::to_string(k)}, {"orderLinkId", ""},
                          {"side", k % 2 ? "Buy" : "Sell"}, {"execPrice", px}, {"execQty", qty},
                          {"execType", "Trade"}, {"execTime", std::to_string(ms)}, {"isMaker", k % 3 == 0}};
      {
        std::lock_guard<std::mutex> g(mu_);
        made_.push_back({in.category, e.dump(), ms});
      }
      ev_.execution(e.dump(), in.category, false);
    }
  });
}

void SimFillSource::reconcile(const std::string &category, uint64_t from_ms, uint64_t to_ms)
{
  std::vector<std::string> out;
  {
    std::lock_guard<std::mutex> g(mu_);
    for (auto it = made_.rbegin(); it != made_.rend(); ++it) // newest first, as Bybit
      if (it->category == category && it->ms >= from_ms && it->ms <= to_ms)
        out.push_back(it->json);
  }
  for (const auto &j : out)
    ev_.execution(j, category, true);
  ev_.reconciled(category, 1, int(out.size()), true, "");
}

void SimFillSource::logout() { gen_ = {}; }

SourceFactory SimFillSource::make_factory(SimConfig cfg, Instruments ins)
{
  return [cfg, ins](const SourceConfig &, FillEvents &ev) -> std::unique_ptr<FillSource> {
    return std::make_unique<SimFillSource>(cfg, ins, ev);
  };
}

}
