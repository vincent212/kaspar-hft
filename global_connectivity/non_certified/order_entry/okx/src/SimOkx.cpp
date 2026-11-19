/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "okx_oe/SimOkx.hpp"

#include "okx_oe/OkxConfig.hpp"

namespace okx::oe
{
  using json = nlohmann::json;

  namespace
  {
    std::string now_ms()
    {
      using namespace std::chrono;
      return std::to_string(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
    }
  }

  SimOkx::SimOkx(const Config &cfg, SessionEvents &ev) : cfg_(cfg), ev_(ev), thread_([this] { run(); }) {}

  SimOkx::~SimOkx()
  {
    {
      std::lock_guard<std::mutex> l(mu_);
      stop_ = true;
    }
    cv_.notify_all();
    thread_.join();
  }

  SessionFactory SimOkx::make_factory(Config cfg)
  {
    return [cfg](const OkxOMConfig &, SessionEvents &ev) { return std::make_unique<SimOkx>(cfg, ev); };
  }

  void SimOkx::at(int ms, std::function<void()> f)
  {
    {
      std::lock_guard<std::mutex> l(mu_);
      due_.emplace(std::chrono::steady_clock::now() + std::chrono::milliseconds(ms), std::move(f));
    }
    cv_.notify_all();
  }

  void SimOkx::run()
  {
    std::unique_lock<std::mutex> l(mu_);
    while (!stop_)
    {
      if (due_.empty())
      {
        cv_.wait(l);
        continue;
      }
      auto it = due_.begin();
      if (cv_.wait_until(l, it->first) == std::cv_status::no_timeout && !stop_)
        continue; // woken early: recheck the earliest
      if (stop_)
        break;
      it = due_.begin();
      if (it->first > std::chrono::steady_clock::now())
        continue;
      auto f = std::move(it->second);
      due_.erase(it);
      f(); // runs under mu_; callbacks only queue messages
    }
  }

  json SimOkx::update(const Order &o) const
  {
    return {{"instIdCode", o.arg.value("instIdCode", int64_t(0))},
            {"ordId", o.ord_id},
            {"clOrdId", o.arg.value("clOrdId", "")},
            {"side", o.arg.value("side", "")},
            {"px", o.arg.value("px", "")},
            {"sz", o.arg.value("sz", "")},
            {"ordType", o.arg.value("ordType", "")},
            {"state", o.state},
            {"fillSz", "0"},
            {"tradeId", ""},
            {"uTime", now_ms()}};
  }

  void SimOkx::logon()
  {
    at(cfg_.ack_ms, [this] {
      logged_in_ = true;
      ev_.logged_in();
    });
  }

  CallStatus SimOkx::place(const std::string &req_id, const json &arg)
  {
    if (!logged_in_)
      return CallStatus::Disconnected;
    at(cfg_.ack_ms, [this, req_id, arg] {
      const std::string cl = arg.value("clOrdId", "");
      json res = {{"id", req_id}, {"op", "order"}, {"code", "0"}, {"msg", ""}};
      if (cfg_.reject_over_units > 0 && std::stod(arg.value("sz", "0")) > cfg_.reject_over_units)
      {
        res["code"] = "1";
        res["data"] = json::array({{{"clOrdId", cl}, {"ordId", ""}, {"sCode", "51008"},
                                    {"sMsg", "Order failed. Insufficient balance (SimOkx)"}}});
        ev_.op_result(res);
        return;
      }
      Order o;
      o.arg = arg;
      o.ord_id = std::to_string(next_ord_++);
      res["data"] = json::array({{{"clOrdId", cl}, {"ordId", o.ord_id}, {"sCode", "0"}, {"sMsg", "Order placed"}}});
      ev_.op_result(res);
      ev_.order_update(update(o));
      orders_[cl] = o;
      if (cfg_.fill_after_ms > 0)
        due_.emplace(std::chrono::steady_clock::now() + std::chrono::milliseconds(cfg_.fill_after_ms), [this, cl] {
          auto it = orders_.find(cl);
          if (it == orders_.end() || it->second.state != "live")
            return;
          auto &o = it->second;
          o.state = "filled";
          json u = update(o);
          u["fillSz"] = o.arg.value("sz", "0");
          u["fillPx"] = o.arg.value("px", "0");
          u["tradeId"] = std::to_string(next_trade_++);
          u["fillTime"] = now_ms();
          u["accFillSz"] = o.arg.value("sz", "0");
          u["execType"] = "M";
          o.fills.push_back(u);
          ev_.order_update(u);
        });
    });
    return CallStatus::Success;
  }

  CallStatus SimOkx::cancel(const std::string &req_id, const json &arg)
  {
    if (!logged_in_)
      return CallStatus::Disconnected;
    at(cfg_.ack_ms, [this, req_id, arg] {
      const std::string cl = arg.value("clOrdId", "");
      auto it = orders_.find(cl);
      json res = {{"id", req_id}, {"op", "cancel-order"}, {"msg", ""}};
      if (it == orders_.end() || it->second.state != "live")
      {
        res["code"] = "1";
        res["data"] = json::array({{{"clOrdId", cl}, {"ordId", it == orders_.end() ? "" : it->second.ord_id},
                                    {"sCode", "51401"}, {"sMsg", "Order has been completed or canceled"}}});
        ev_.op_result(res);
        return;
      }
      res["code"] = "0";
      res["data"] = json::array({{{"clOrdId", cl}, {"ordId", it->second.ord_id}, {"sCode", "0"}, {"sMsg", ""}}});
      ev_.op_result(res);
      it->second.state = "canceled";
      json u = update(it->second);
      u["cancelSource"] = "1";
      ev_.order_update(u);
    });
    return CallStatus::Success;
  }

  CallStatus SimOkx::recover(const std::string &, const std::string &, const std::string &cl)
  {
    if (!logged_in_)
      return CallStatus::Disconnected;
    at(0, [this, cl] {
      auto it = orders_.find(cl);
      if (it == orders_.end())
      {
        ev_.order_unknown(cl);
        return;
      }
      for (const auto &f : it->second.fills)
        ev_.order_update(f);
      ev_.order_update(update(it->second));
    });
    return CallStatus::Success;
  }

  void SimOkx::logout() { logged_in_ = false; }
}
