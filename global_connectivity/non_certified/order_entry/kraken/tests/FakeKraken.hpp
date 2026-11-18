/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#pragma once

// FakeKraken: the KrakenOM test double. Records every add_order and
// cancel_order; the test thread plays the session's thread and drives the
// events (logged_in, report, disconnected...) of the current session.

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "kraken_oe/KrakenSession.hpp"

namespace kraken::oe::test
{
  class FakeKraken
  {
  public:
    struct Cancel
    {
      std::string order_id, cl_ord_id;
    };

    bool auto_login = true;
    CallStatus next_status = CallStatus::Success;

    SessionFactory factory()
    {
      return [this](SessionEvents &ev) -> std::unique_ptr<KrakenSession> {
        std::lock_guard<std::mutex> l(mu_);
        ++sessions_;
        ev_ = &ev;
        return std::make_unique<Session>(*this, ev);
      };
    }

    int sessions()
    {
      std::lock_guard<std::mutex> l(mu_);
      return sessions_;
    }
    std::vector<NewOrder> orders()
    {
      std::lock_guard<std::mutex> l(mu_);
      return orders_;
    }
    std::vector<Cancel> cancels()
    {
      std::lock_guard<std::mutex> l(mu_);
      return cancels_;
    }
    int logouts()
    {
      std::lock_guard<std::mutex> l(mu_);
      return logouts_;
    }

    // Events of the current session.
    SessionEvents *ev()
    {
      std::lock_guard<std::mutex> l(mu_);
      return ev_;
    }
    void report(const Report &r)
    {
      if (auto e = ev())
        e->report(r);
    }
    void login() { ev()->logged_in(); }
    void drop(const std::string &why = "test") { ev()->disconnected(why); }

  private:
    class Session : public KrakenSession
    {
    public:
      Session(FakeKraken &f, SessionEvents &ev) : f_(f), ev_(ev) {}
      ~Session() override
      {
        std::lock_guard<std::mutex> l(f_.mu_);
        if (f_.ev_ == &ev_)
          f_.ev_ = nullptr;
      }
      void logon() override
      {
        if (f_.auto_login)
          ev_.logged_in();
      }
      CallStatus add_order(const NewOrder &o) override
      {
        std::lock_guard<std::mutex> l(f_.mu_);
        if (f_.next_status == CallStatus::Success)
          f_.orders_.push_back(o);
        return f_.next_status;
      }
      CallStatus cancel_order(const std::string &order_id, const std::string &cl_ord_id) override
      {
        std::lock_guard<std::mutex> l(f_.mu_);
        if (f_.next_status == CallStatus::Success)
          f_.cancels_.push_back({order_id, cl_ord_id});
        return f_.next_status;
      }
      void logout() override
      {
        std::lock_guard<std::mutex> l(f_.mu_);
        ++f_.logouts_;
      }

    private:
      FakeKraken &f_;
      SessionEvents &ev_;
    };

    std::mutex mu_;
    SessionEvents *ev_ = nullptr;
    int sessions_ = 0, logouts_ = 0;
    std::vector<NewOrder> orders_;
    std::vector<Cancel> cancels_;
  };
}
