#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// SimOkx: an in-process OKX behind OkxSession, for --sim runs and demos.
// It answers as the private WebSocket does (op results with sCode and ordId,
// orders-channel updates with state, fillSz, fillPx, tradeId), on its own
// thread, after configurable delays:
//
//   logon             logged_in after ack_ms
//   place             op result after ack_ms, then state "live"; if
//                     fill_after_ms > 0, a full fill at the order's price
//                     that much later (state "filled", one tradeId)
//   cancel            op result, then state "canceled", unless already filled
//                     (then sCode 51401, as OKX answers for a finished order)
//   recover           the order's fills and state again, as REST would
//
// It does not match against a book: fills are at the limit price.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <string>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include "okx_oe/OkxSession.hpp"

namespace okx::oe
{
  class SimOkx : public OkxSession
  {
  public:
    struct Config
    {
      int ack_ms = 5;
      int fill_after_ms = 0;   // 0: orders rest until canceled
      int reject_over_units = 0; // reject orders whose sz is above this many OKX sz units (0: never)
    };

    SimOkx(const Config &cfg, SessionEvents &ev);
    ~SimOkx() override;

    void logon() override;
    CallStatus place(const std::string &req_id, const nlohmann::json &arg) override;
    CallStatus cancel(const std::string &req_id, const nlohmann::json &arg) override;
    CallStatus recover(const std::string &inst_id, const std::string &ord_id, const std::string &cl_ord_id) override;
    void logout() override;

    static SessionFactory make_factory(Config cfg);

  private:
    struct Order
    {
      nlohmann::json arg;
      std::string ord_id, state = "live";
      std::vector<nlohmann::json> fills;
    };

    void at(int ms, std::function<void()> f);
    void run();
    nlohmann::json update(const Order &o) const;

    Config cfg_;
    SessionEvents &ev_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::multimap<std::chrono::steady_clock::time_point, std::function<void()>> due_;
    std::map<std::string, Order> orders_; // clOrdId -> order
    std::atomic<bool> logged_in_{false};
    bool stop_ = false;
    uint64_t next_ord_ = 700000000000000000ull;
    uint64_t next_trade_ = 1;
    std::thread thread_;
  };
}
