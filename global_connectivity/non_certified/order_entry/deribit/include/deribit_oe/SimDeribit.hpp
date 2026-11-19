#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// SimDeribit: an in-process Deribit behind DeribitSession, for --sim runs and
// demos. It answers as the JSON-RPC WebSocket does (feed.md 4), on its own
// thread, after configurable delays:
//
//   logon             logged_in after ack_ms
//   private/buy|sell  result {order (open), trades []} after ack_ms and a
//                     user.orders "open"; if fill_after_ms > 0, a full fill
//                     at the order's price that much later (user.trades,
//                     then user.orders "filled")
//                     amount above reject_over_amount: error 10009
//                     not_enough_funds
//   private/cancel    result {order (cancelled)} and user.orders "cancelled",
//                     or error 11044 not_open_order once filled
//   cancel_by_label   result 1 or 0 (the number cancelled)
//   recover           the order's trades and state again, as REST would
//
// It does not match against a book: fills are at the limit price.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "deribit_oe/DeribitSession.hpp"

namespace deribit::oe
{
  class SimDeribit : public DeribitSession
  {
  public:
    struct Config
    {
      int ack_ms = 5;
      int fill_after_ms = 0;          // 0: orders rest until cancelled
      double reject_over_amount = 0;  // 0: never
    };

    SimDeribit(const Config &cfg, SessionEvents &ev);
    ~SimDeribit() override;

    void logon() override;
    CallStatus call(int64_t id, const std::string &method, const nlohmann::json &params) override;
    CallStatus recover(const std::string &order_id, const std::string &label, const std::string &currency) override;
    void logout() override;

    static SessionFactory make_factory(Config cfg);

  private:
    struct Order
    {
      nlohmann::json o;   // the order object as Deribit returns it
      std::vector<nlohmann::json> trades;
    };

    void at(int ms, std::function<void()> f);
    void run();
    Order *by_label(const std::string &label);

    Config cfg_;
    SessionEvents &ev_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::multimap<std::chrono::steady_clock::time_point, std::function<void()>> due_;
    std::map<std::string, Order> orders_;   // order_id -> order
    std::atomic<bool> logged_in_{false};
    bool stop_ = false;
    uint64_t next_order_ = 31000000000ull;
    uint64_t next_trade_ = 1;
    std::thread thread_;
  };
}
