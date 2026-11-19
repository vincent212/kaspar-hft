/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "coinbase_exchange_pt/SimDropCopy.hpp"

#include <chrono>

#include "coinbase_exchange/Num.hpp"

namespace coinbase_exchange::pt
{
using namespace fix;

class SimDropCopy::Session : public oe::CbxSession
{
public:
  Session(SimDropCopy &s, uint64_t id, oe::SessionEvents &ev) : s_(s), id_(id), ev_(ev) {}
  ~Session() override
  {
    std::lock_guard<std::mutex> l(s_.mu_);
    s_.alive_.erase(id_);
    if (s_.live_id_ == id_)
    {
      s_.live_ = nullptr;
      s_.live_id_ = 0;
    }
  }
  void logon() override
  {
    uint64_t id = id_;
    s_.runner_.now([this, id] {
      std::lock_guard<std::mutex> l(s_.mu_);
      if (!s_.alive_.count(id))
        return;
      s_.live_ = &ev_;
      s_.live_id_ = id;
      ev_.logged_on();
    });
    s_.runner_.after(std::chrono::milliseconds(s_.cfg_.trade_ms), [this, id] { s_.tick(id); });
  }
  oe::CallStatus send(const FixMsg &) override { return oe::CallStatus::Disconnected; } // drop copy: nothing to send
  void logout() override {}

private:
  SimDropCopy &s_;
  uint64_t id_;
  oe::SessionEvents &ev_;
};

SimDropCopy::SimDropCopy(Config c) : cfg_(std::move(c)) {}
SimDropCopy::~SimDropCopy() = default;

int SimDropCopy::trades_sent() const
{
  std::lock_guard<std::mutex> l(mu_);
  return trades_;
}

oe::SessionFactory SimDropCopy::factory()
{
  return [this](const oe::SessionConfig &, oe::SessionEvents &ev) -> std::unique_ptr<oe::CbxSession> {
    std::lock_guard<std::mutex> l(mu_);
    uint64_t id = next_id_++;
    alive_.insert(id);
    return std::make_unique<Session>(*this, id, ev);
  };
}

void SimDropCopy::tick(uint64_t id)
{
  {
    std::lock_guard<std::mutex> l(mu_);
    if (!alive_.count(id) || live_id_ != id || trades_ >= cfg_.max_trades)
      return;
    const std::string sym = cfg_.symbols[size_t(trades_) % cfg_.symbols.size()];
    const std::string clid = to_string(make_uuid_v4(rng_(), rng_()));
    const std::string oid = to_string(make_uuid_v4(rng_(), rng_()));
    const std::string now = fix_time_ms(uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                     std::chrono::system_clock::now().time_since_epoch())
                                                     .count()));
    FixMsg n("8");
    n.set(ClOrdID, clid).set(OrderID, oid).set(ExecID, to_string(make_uuid_v4(rng_(), rng_())));
    n.set(OrdStatus, '0').set(ExecType, '0').set(Symbol, sym).set(Side, trades_ % 2 ? '2' : '1');
    n.set(OrdType, '2').set(Price, "64000.00").set(OrderQty, "0.01000000").set(TransactTime, now);
    live_->message(n);
    FixMsg f("8");
    f.set(ClOrdID, clid).set(OrderID, oid).set(ExecID, to_string(make_uuid_v4(rng_(), rng_())));
    f.set(OrdStatus, '2').set(ExecType, 'F').set(Symbol, sym).set(Side, trades_ % 2 ? '2' : '1');
    f.set(OrdType, '2').set(LastQty, "0.01000000").set(LastPx, "64000.00").set(CumQty, "0.01000000");
    f.set(LeavesQty, "0").set(AvgPx, "64000.00").set(TransactTime, now);
    f.set(TradeID, std::to_string(1135000000 + trades_)).set(AggressorIndicator, 'N');
    f.add(NoMiscFees, "1").add(MiscFeeAmt, "0.0004").add(MiscFeeCurr, "USD").add(MiscFeeType, "4").add(MiscFeeBasis, "2");
    live_->message(f);
    ++trades_;
    if (cfg_.resend_every > 0 && trades_ % cfg_.resend_every == 0)
    {
      f.set(PossDupFlag, 'Y');
      live_->message(f);
    }
  }
  runner_.after(std::chrono::milliseconds(cfg_.trade_ms), [this, id] { tick(id); });
}
}
