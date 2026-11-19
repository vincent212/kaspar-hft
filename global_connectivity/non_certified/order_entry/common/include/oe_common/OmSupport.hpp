#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// What every crypto order manager (Bybit, OKX, Kraken, Deribit) does the same
// way, over frame::som::BrokerOrderBook:
//
//   ReconnectTimer      fires once after a delay, on its own thread; cancelled
//                       by cancel() or destruction
//   TokenBucket         the per-second new-order budget
//   reject, cancel_reject
//                       the SOM's Reject / CancReject for a request refused
//                       before it reached the venue, logged
//   late_cancel_reason  the CancReject reason for a cancel of a done order
//   abandon_in_flight   what a lost connection does to the book: every cancel
//                       in flight is refused (CancReject DISCONNECTED, the
//                       order stays working); the new orders in flight are
//                       returned, their outcome unknown until the venue has
//                       been asked after the reconnect
//   reject_unknown      after that recovery, a new order the venue does not
//                       know never reached it: Reject DISCONNECTED
//
// All of it runs on the order manager's actor thread, except the timer's
// callback, which may only send() to the actor.

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include "actors/Actor.hpp"
#include "frame/som/BrokerOrderBook.hpp"
#include "frame/som/msg/CancReject.hpp"
#include "frame/som/msg/Reject.hpp"
#include "logger/act/Logger.hpp"

namespace oe_common
{
  namespace som = frame::som::msg;

  class ReconnectTimer
  {
  public:
    // f runs on the timer's thread, ms from now, unless cancelled first.
    // Arming again cancels the previous timer.
    void arm(int ms, std::function<void()> f)
    {
      timer_ = std::jthread([ms, f = std::move(f)](std::stop_token st) {
        std::mutex m;
        std::condition_variable_any cv;
        std::unique_lock<std::mutex> l(m);
        if (!cv.wait_for(l, st, std::chrono::milliseconds(ms), [] { return false; }) && !st.stop_requested())
          f();
      });
    }
    void cancel() { timer_ = {}; }

  private:
    std::jthread timer_;
  };

  class TokenBucket
  {
  public:
    // One token for a new order; the bucket holds at most `rate` and refills
    // at `rate` per second. rate <= 0: no limit.
    bool take(double rate)
    {
      if (rate <= 0)
        return true;
      const auto now = std::chrono::steady_clock::now();
      if (refilled_ == std::chrono::steady_clock::time_point{})
        tokens_ = rate;
      else
        tokens_ = std::min(rate, tokens_ + rate * std::chrono::duration<double>(now - refilled_).count());
      refilled_ = now;
      if (tokens_ < 1)
        return false;
      tokens_ -= 1;
      return true;
    }

  private:
    double tokens_ = 0;
    std::chrono::steady_clock::time_point refilled_{};
  };

  inline void reject(actors::Actor *om, actors::Actor *to, int oid, int reason, const char *why)
  {
    polonaise::logger::act::log(polonaise::logger::msg::Log::Level::_ERROR_, om->get_name(), __FILE__, __LINE__,
                                "rejecting order oid=%d: %s", oid, why);
    if (to)
      to->send(new som::Reject(uint(oid), reason), om);
  }

  inline void cancel_reject(actors::Actor *om, actors::Actor *to, int oid, int reason, const char *why)
  {
    polonaise::logger::act::log(polonaise::logger::msg::Log::Level::_ERROR_, om->get_name(), __FILE__, __LINE__,
                                "rejecting cancel of oid=%d: %s", oid, why);
    if (to)
      to->send(new som::CancReject(uint(oid), reason), om);
  }

  inline int late_cancel_reason(frame::som::State s)
  {
    switch (s)
    {
    case frame::som::State::Filled: return som::CancReject::FILLEDALREADY;
    case frame::som::State::Canceled: return som::CancReject::CANCELLEDALREADY;
    case frame::som::State::Rejected: return som::CancReject::REJECTEDPREVIOUSLY;
    default: return som::CancReject::NOTFOUND;
    }
  }

  // cids: the client ids of the orders this manager has sent (any range of
  // frame::som::ClientId). Returns the ids still PendingNew.
  template <class Cids>
  std::vector<frame::som::ClientId> abandon_in_flight(frame::som::BrokerOrderBook &book, const Cids &cids,
                                                      actors::Actor *om)
  {
    std::vector<frame::som::ClientId> unconfirmed;
    for (const auto cid : cids)
    {
      const frame::som::Entry *e = book.find_cid(cid);
      if (!e)
        continue;
      if (e->state == frame::som::State::PendingNew)
        unconfirmed.push_back(cid);
      if (e->state != frame::som::State::PendingCancel || e->pending_cid == 0)
        continue;
      auto res = book.send_failed(e->pending_cid);
      if (res.outcome == frame::som::Outcome::Ok && res.entry->sender)
        res.entry->sender->send(new som::CancReject(uint(res.entry->oid), som::CancReject::DISCONNECTED), om);
      polonaise::logger::act::log(polonaise::logger::msg::Log::Level::_REJECT_, om->get_name(), __FILE__, __LINE__,
                                  "cancel of oid=%d abandoned: connection lost (%s)", res.entry ? res.entry->oid : -1,
                                  frame::som::to_string(res.outcome));
    }
    return unconfirmed;
  }

  // true when the order was PendingNew and is now Rejected.
  inline bool reject_unknown(frame::som::BrokerOrderBook &book, frame::som::ClientId cid, actors::Actor *om)
  {
    const frame::som::Entry *e = book.find_cid(cid);
    if (!e || e->state != frame::som::State::PendingNew)
      return false;
    auto res = book.on_rejected(cid);
    if (res.outcome != frame::som::Outcome::Ok)
      return false;
    if (res.entry->sender)
      res.entry->sender->send(new som::Reject(uint(res.entry->oid), uint64_t(0), som::Reject::DISCONNECTED), om);
    polonaise::logger::act::log(polonaise::logger::msg::Log::Level::_REJECT_, om->get_name(), __FILE__, __LINE__,
                                "order oid=%d never reached the venue (unknown after reconnect): rejected",
                                res.entry->oid);
    return true;
  }
}
