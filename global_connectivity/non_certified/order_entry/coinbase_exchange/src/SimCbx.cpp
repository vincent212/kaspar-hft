/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "coinbase_exchange_oe/SimCbx.hpp"

#include <chrono>

#include "coinbase_exchange/Num.hpp"

namespace coinbase_exchange::oe
{
using namespace fix;

class SimCbx::Session : public CbxSession
{
public:
  Session(SimCbx &sim, uint64_t id, SessionEvents &ev) : sim_(sim), id_(id), ev_(ev) {}
  ~Session() override { sim_.detach(id_); }
  void logon() override
  {
    sim_.submit(id_, 0, [this] {
      sim_.live_ = &ev_;
      sim_.live_id_ = id_;
      ev_.logged_on();
    });
  }
  CallStatus send(const FixMsg &m) override
  {
    const auto t = m.type();
    if (t == "D")
      return sim_.submit(id_, sim_.cfg_.ack_delay_ms, [this, m] { sim_.do_new(id_, m); });
    if (t == "F")
      return sim_.submit(id_, 0, [this, m] { sim_.do_cancel(id_, m); });
    if (t == "H")
      return sim_.submit(id_, 0, [this, m] { sim_.do_status(id_, m); });
    return sim_.submit(id_, 0, [this, m] {
      FixMsg r("j");
      r.set(RefMsgType, m.type()).set(BusinessRejectReason, "2").set(Text, "Unsupported Message Type");
      sim_.emit(r);
    });
  }
  void logout() override
  {
    std::lock_guard<std::mutex> l(sim_.mu_);
    if (sim_.live_id_ == id_)
    {
      sim_.live_ = nullptr;
      sim_.live_id_ = 0;
    }
  }

private:
  SimCbx &sim_;
  uint64_t id_;
  SessionEvents &ev_;
};

SimCbx::SimCbx(Config c) : cfg_(std::move(c)) {}
SimCbx::~SimCbx() = default;

uint64_t SimCbx::reports() const
{
  std::lock_guard<std::mutex> l(mu_);
  return reports_;
}

SessionFactory SimCbx::factory()
{
  return [this](const SessionConfig &, SessionEvents &ev) -> std::unique_ptr<CbxSession> {
    std::lock_guard<std::mutex> l(mu_);
    uint64_t id = next_id_++;
    alive_.insert(id);
    return std::make_unique<Session>(*this, id, ev);
  };
}

SessionFactory SimCbx::make_factory(Config c)
{
  auto sim = std::make_shared<SimCbx>(std::move(c));
  return [sim](const SessionConfig &cfg, SessionEvents &ev) { return sim->factory()(cfg, ev); };
}

CallStatus SimCbx::submit(uint64_t id, int delay_ms, std::function<void()> f)
{
  {
    std::lock_guard<std::mutex> l(mu_);
    if (!alive_.count(id))
      return CallStatus::Disconnected;
    if (live_id_ && live_id_ != id)
      return CallStatus::Disconnected;
  }
  runner_.after(std::chrono::milliseconds(delay_ms), [this, id, f = std::move(f)] {
    std::lock_guard<std::mutex> l(mu_);
    if (alive_.count(id))
      f();
  });
  return CallStatus::Success;
}

void SimCbx::detach(uint64_t id)
{
  std::lock_guard<std::mutex> l(mu_);
  alive_.erase(id);
  if (live_id_ == id)
  {
    live_ = nullptr;
    live_id_ = 0;
  }
}

std::string SimCbx::uuid()
{
  return to_string(make_uuid_v4(rng_(), rng_()));
}

void SimCbx::emit(FixMsg m)
{
  ++reports_;
  if (live_)
    live_->message(m);
}

FixMsg SimCbx::report(const SimOrder &o, char exec_type, char ord_status)
{
  FixMsg r("8");
  r.set(ClOrdID, o.clordid).set(OrderID, o.order_id).set(ExecID, uuid()).set(OrdStatus, ord_status);
  r.set(ExecType, exec_type).set(Symbol, o.symbol).set(Side, o.side).set(OrdType, o.ord_type);
  if (!o.price.empty())
    r.set(Price, o.price);
  r.set(OrderQty, o.qty).set(TimeInForce, o.tif);
  r.set(CumQty, format_scaled(o.filled_e8, 8));
  r.set(LeavesQty, format_scaled(o.live ? o.qty_e8 - o.filled_e8 : 0, 8));
  r.set(TransactTime, fix_time_ms(uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                               std::chrono::system_clock::now().time_since_epoch())
                                               .count())));
  return r;
}

void SimCbx::do_new(uint64_t, FixMsg m)
{
  SimOrder o;
  o.clordid = m.get(ClOrdID);
  o.order_id = uuid();
  o.symbol = m.get(Symbol);
  o.side = m.get_char(Side, '1');
  o.ord_type = m.get_char(OrdType, '2');
  o.tif = m.get_char(TimeInForce, '1');
  o.price = m.get(Price);
  o.qty = m.get(OrderQty);
  o.qty_e8 = scaled(o.qty, 8).value_or(0);
  if (cfg_.reject_symbols.count(o.symbol) || o.qty_e8 <= 0 || !is_uuid_v4_lower(o.clordid))
  {
    auto r = report(o, '8', '8');
    r.set(OrdRejReason, cfg_.reject_symbols.count(o.symbol) ? "1" : "0");
    r.set(Text, cfg_.reject_symbols.count(o.symbol) ? "Unknown Symbol" : "Invalid order");
    emit(r);
    return;
  }
  o.live = true;
  orders_[o.order_id] = o;
  by_clordid_[o.clordid] = o.order_id;
  emit(report(o, '0', '0'));
  if (!cfg_.fill)
  {
    if (o.tif == '3' || o.tif == '4')
    {
      auto &s = orders_[o.order_id];
      s.live = false;
      emit(report(s, 'C', 'C'));
    }
    return;
  }
  const int parts = std::max(1, cfg_.fill_parts);
  int64_t each = o.qty_e8 / parts;
  for (int i = 0; i < parts; ++i)
  {
    int64_t q = i + 1 == parts ? o.qty_e8 - each * (parts - 1) : each;
    std::string id = o.order_id;
    runner_.after(std::chrono::milliseconds(cfg_.fill_delay_ms * (i + 1)), [this, id, q] {
      std::lock_guard<std::mutex> l(mu_);
      do_fill(id, q);
    });
  }
}

void SimCbx::do_fill(std::string order_id, int64_t qty_e8)
{
  auto it = orders_.find(order_id);
  if (it == orders_.end() || !it->second.live)
    return;
  auto &o = it->second;
  o.filled_e8 += qty_e8;
  if (o.filled_e8 >= o.qty_e8)
    o.live = false;
  auto r = report(o, 'F', o.live ? '1' : '2');
  std::string px = o.ord_type == '1' ? cfg_.market_price : o.price;
  r.set(LastQty, format_scaled(qty_e8, 8)).set(LastPx, px).set(AvgPx, px);
  r.set(TradeID, std::to_string(1000000 + reports_)).set(AggressorIndicator, 'N');
  r.add(NoMiscFees, "1").add(MiscFeeAmt, "0.001").add(MiscFeeCurr, "USD").add(MiscFeeType, "4").add(MiscFeeBasis, "2");
  emit(r);
}

void SimCbx::do_cancel(uint64_t, FixMsg m)
{
  std::string oid = m.get(OrderID);
  if (oid.empty())
  {
    auto c = by_clordid_.find(m.get(OrigClOrdID));
    if (c != by_clordid_.end())
      oid = c->second;
  }
  auto it = orders_.find(oid);
  if (it == orders_.end() || !it->second.live)
  {
    FixMsg r("9");
    r.set(ClOrdID, m.get(ClOrdID)).set(OrigClOrdID, m.get(OrigClOrdID)).set(OrdStatus, '8');
    if (!oid.empty())
      r.set(OrderID, oid);
    r.set(CxlRejReason, "1").set(CxlRejResponseTo, '1').set(Text, it == orders_.end() ? "Unknown order" : "Order done");
    emit(r);
    return;
  }
  auto &o = it->second;
  o.live = false;
  auto r = report(o, '4', '4');
  r.set(ClOrdID, m.get(ClOrdID)).set(OrigClOrdID, o.clordid);
  emit(r);
}

void SimCbx::do_status(uint64_t, FixMsg m)
{
  auto c = by_clordid_.find(m.get(ClOrdID));
  std::string oid = c != by_clordid_.end() ? c->second : m.get(OrderID);
  auto it = orders_.find(oid);
  if (it == orders_.end())
  {
    FixMsg r("8");
    r.set(ClOrdID, m.get(ClOrdID)).set(OrderID, "00000000-0000-0000-0000-000000000000").set(ExecID, uuid());
    r.set(OrdStatus, '8').set(ExecType, 'I').set(Symbol, m.get(Symbol)).set(OrdRejReason, "5");
    emit(r);
    return;
  }
  const auto &o = it->second;
  char st = o.live ? (o.filled_e8 ? '1' : '0') : (o.filled_e8 >= o.qty_e8 ? '2' : '4');
  auto r = report(o, 'I', st);
  if (o.filled_e8)
    r.set(AvgPx, o.ord_type == '1' ? cfg_.market_price : o.price);
  emit(r);
}
}
