/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "bybit_oe/SimBybit.hpp"

#include <chrono>
#include <cstdio>
#include <vector>

#include <nlohmann/json.hpp>

#include "frame/som/TimedRunner.hpp"

namespace bybit::oe
{

namespace
{
  std::string fmt(double v)
  {
    char b[64];
    std::snprintf(b, sizeof(b), "%.10f", v);
    std::string s(b);
    while (!s.empty() && s.back() == '0')
      s.pop_back();
    if (!s.empty() && s.back() == '.')
      s.pop_back();
    return s.empty() ? "0" : s;
  }

  uint64_t now_ms()
  {
    return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count());
  }
}

class SimBybit::Session : public BybitSession
{
public:
  Session(SimBybit &v, SessionEvents &ev) : v_(v), ev_(ev)
  {
    std::lock_guard<std::mutex> l(v_.mu_);
    id_ = v_.next_session_++;
  }
  ~Session() override { logout(); }

  void logon() override
  {
    {
      std::lock_guard<std::mutex> l(v_.mu_);
      v_.sessions_[id_] = &ev_;
      on_ = true;
    }
    auto id = id_;
    v_.runner_->now([id, &v = v_] {
      std::lock_guard<std::mutex> l(v.mu_);
      if (auto it = v.sessions_.find(id); it != v.sessions_.end())
        it->second->logged_in();
    });
  }
  CallStatus place(const PlaceRequest &r) override
  {
    if (!on_)
      return CallStatus::Disconnected;
    auto id = id_;
    v_.runner_->now([&v = v_, id, r] { v.do_place(id, r); });
    return CallStatus::Success;
  }
  CallStatus cancel(const CancelRequest &r) override
  {
    if (!on_)
      return CallStatus::Disconnected;
    auto id = id_;
    v_.runner_->now([&v = v_, id, r] { v.do_cancel(id, r); });
    return CallStatus::Success;
  }
  CallStatus query_order(const std::string &, const std::string &link) override
  {
    if (!on_)
      return CallStatus::Disconnected;
    auto id = id_;
    v_.runner_->now([&v = v_, id, link] {
      std::lock_guard<std::mutex> l(v.mu_);
      auto s = v.sessions_.find(id);
      if (s == v.sessions_.end())
        return;
      auto o = v.orders_.find(link);
      if (o != v.orders_.end())
      {
        nlohmann::json j = {{"category", o->second.category}, {"symbol", o->second.symbol},
                            {"orderId", o->second.order_id}, {"orderLinkId", link},
                            {"orderStatus", o->second.status}, {"cumExecQty", fmt(o->second.cum)},
                            {"leavesQty", fmt(o->second.qty - o->second.cum)}, {"qty", fmt(o->second.qty)},
                            {"rejectReason", "EC_NoError"}, {"cancelType", "UNKNOWN"}};
        s->second->order(j.dump());
      }
      s->second->response(RequestKind::QueryOrder, link, 0, "OK");
    });
    return CallStatus::Success;
  }
  CallStatus query_executions(const std::string &, const std::string &link) override
  {
    if (!on_)
      return CallStatus::Disconnected;
    auto id = id_;
    v_.runner_->now([&v = v_, id, link] {
      std::lock_guard<std::mutex> l(v.mu_);
      auto s = v.sessions_.find(id);
      if (s == v.sessions_.end())
        return;
      if (auto e = v.execs_.find(link); e != v.execs_.end())
        for (const auto &x : e->second)
          s->second->execution(x);
      s->second->response(RequestKind::QueryExecutions, link, 0, "OK");
    });
    return CallStatus::Success;
  }
  void logout() override
  {
    std::lock_guard<std::mutex> l(v_.mu_);
    v_.sessions_.erase(id_);
    on_ = false;
  }

private:
  SimBybit &v_;
  SessionEvents &ev_;
  uint64_t id_ = 0;
  std::atomic<bool> on_{false};
};

SimBybit::SimBybit(Config c) : cfg_(std::move(c)), runner_(std::make_unique<frame::som::TimedRunner>()) {}

SimBybit::~SimBybit()
{
  runner_.reset();
}

SessionFactory SimBybit::factory()
{
  return [this](const SessionConfig &, SessionEvents &ev) -> std::unique_ptr<BybitSession> {
    return std::make_unique<Session>(*this, ev);
  };
}

SessionFactory SimBybit::make_factory(Config c)
{
  auto sim = std::make_shared<SimBybit>(std::move(c));
  return [sim](const SessionConfig &sc, SessionEvents &ev) { return sim->factory()(sc, ev); };
}

uint64_t SimBybit::executions() const
{
  std::lock_guard<std::mutex> l(mu_);
  return exec_n_;
}

std::string SimBybit::next_order_id()
{
  const uint64_t n = next_id_++;
  if (cfg_.numeric_order_ids)
    return std::to_string(n);
  char b[64];
  std::snprintf(b, sizeof(b), "%08llx-%04llx-4%03llx-8%03llx-%012llx", (unsigned long long)(n >> 32) & 0xffffffffull,
                (unsigned long long)(n >> 16) & 0xffff, (unsigned long long)n & 0xfff,
                (unsigned long long)(n >> 4) & 0xfff, (unsigned long long)n & 0xffffffffffffull);
  return b;
}

void SimBybit::publish_order(const SimOrder &o, const std::string &reject, const std::string &cancel_type)
{
  auto s = sessions_.find(o.session);
  if (s == sessions_.end())
    return;
  nlohmann::json j = {{"category", o.category}, {"symbol", o.symbol}, {"orderId", o.order_id},
                      {"orderLinkId", o.link}, {"side", o.side}, {"orderType", o.type},
                      {"price", fmt(o.price)}, {"qty", fmt(o.qty)}, {"timeInForce", o.tif},
                      {"orderStatus", o.status}, {"cumExecQty", fmt(o.cum)},
                      {"leavesQty", fmt(o.qty - o.cum)}, {"rejectReason", reject},
                      {"cancelType", cancel_type}, {"updatedTime", std::to_string(now_ms())}};
  s->second->order(j.dump());
}

void SimBybit::do_place(uint64_t sid, PlaceRequest r)
{
  std::lock_guard<std::mutex> l(mu_);
  auto s = sessions_.find(sid);
  if (s == sessions_.end())
    return;
  if (orders_.count(r.order_link_id))
  {
    s->second->response(RequestKind::Place, r.order_link_id, 110072, "OrderLinkedID is duplicate");
    return;
  }
  SimOrder o;
  o.category = r.category;
  o.symbol = r.symbol;
  o.side = r.side;
  o.type = r.order_type;
  o.tif = r.time_in_force;
  o.link = r.order_link_id;
  o.order_id = next_order_id();
  o.price = r.order_type == "Market" ? std::stod(cfg_.market_price) : std::stod(r.price);
  o.qty = std::stod(r.qty);
  o.session = sid;
  s->second->response(RequestKind::Place, r.order_link_id, 0, "OK");
  if (cfg_.reject_symbols.count(r.symbol))
  {
    o.status = "Rejected";
    orders_[o.link] = o;
    publish_order(o, "EC_InvalidSymbolStatus");
    return;
  }
  o.status = "New";
  orders_[o.link] = o;
  const auto link = o.link;
  const bool immediate = r.order_type == "Market" || r.time_in_force == "IOC" || r.time_in_force == "FOK";
  runner_->after(std::chrono::milliseconds(cfg_.ack_delay_ms), [this, link, immediate] {
    std::lock_guard<std::mutex> l(mu_);
    auto it = orders_.find(link);
    if (it == orders_.end() || it->second.status != "New")
      return;
    publish_order(it->second);
    if (!cfg_.fill && immediate)
    {
      it->second.status = "Cancelled";
      publish_order(it->second, "EC_NoImmediateQtyToFill");
      return;
    }
    if (!cfg_.fill)
      return;
    const double part = it->second.qty / std::max(1, cfg_.fill_parts);
    for (int i = 0; i < std::max(1, cfg_.fill_parts); ++i)
      runner_->after(std::chrono::milliseconds(cfg_.fill_delay_ms * (i + 1)),
                     [this, link, part] { do_fill(link, part); });
  });
}

void SimBybit::do_fill(const std::string &link, double qty)
{
  std::lock_guard<std::mutex> l(mu_);
  auto it = orders_.find(link);
  if (it == orders_.end())
    return;
  auto &o = it->second;
  if (o.status != "New" && o.status != "PartiallyFilled")
    return;
  if (o.cum + qty > o.qty - 1e-12)
    qty = o.qty - o.cum;
  if (qty <= 0)
    return;
  o.cum += qty;
  o.status = o.cum >= o.qty - 1e-12 ? "Filled" : "PartiallyFilled";
  ++exec_n_;
  nlohmann::json x = {{"category", o.category}, {"symbol", o.symbol}, {"orderId", o.order_id},
                      {"orderLinkId", o.link}, {"side", o.side}, {"orderPrice", fmt(o.price)},
                      {"orderQty", fmt(o.qty)}, {"leavesQty", fmt(o.qty - o.cum)},
                      {"orderType", o.type}, {"execFee", "0"},
                      {"execId", "sim-exec-" + std::to_string(exec_n_)}, {"execPrice", fmt(o.price)},
                      {"execQty", fmt(qty)}, {"execType", "Trade"}, {"execValue", fmt(qty * o.price)},
                      {"execTime", std::to_string(now_ms())}, {"isMaker", o.type == "Limit"},
                      {"feeRate", "0"}, {"seq", exec_n_}};
  execs_[link].push_back(x.dump());
  if (auto s = sessions_.find(o.session); s != sessions_.end())
    s->second->execution(x.dump());
  publish_order(o);
}

void SimBybit::do_cancel(uint64_t sid, CancelRequest r)
{
  std::lock_guard<std::mutex> l(mu_);
  auto s = sessions_.find(sid);
  if (s == sessions_.end())
    return;
  auto it = orders_.find(r.order_link_id);
  if (it == orders_.end() || (it->second.status != "New" && it->second.status != "PartiallyFilled"))
  {
    s->second->response(RequestKind::Cancel, r.order_link_id, 110001, "order not exists or too late to cancel");
    return;
  }
  s->second->response(RequestKind::Cancel, r.order_link_id, 0, "OK");
  it->second.status = it->second.category == "spot" && it->second.cum > 0 ? "PartiallyFilledCanceled" : "Cancelled";
  it->second.session = sid;
  publish_order(it->second, "EC_PerCancelRequest", "CancelByUser");
}

}
