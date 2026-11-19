#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// FakeCbx: the test double for CbxSession. It records every application
// message the OM sends, logs sessions on (or rejects the logon), and lets the
// test deliver reports, disconnects and busy/available from the test thread,
// which plays the session's thread.

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "coinbase_exchange_oe/CbxSession.hpp"

namespace coinbase_exchange::oe::test
{
  class FakeCbxServer
  {
  public:
    std::atomic<bool> auto_logon{true};
    std::atomic<bool> reject_logon{false};
    std::atomic<CallStatus> send_status{CallStatus::Success};

    struct SessionInfo
    {
      SessionConfig cfg;
      std::thread::id created_on;
    };

    std::vector<fix::FixMsg> sent()
    {
      std::lock_guard<std::mutex> l(mu_);
      return sent_;
    }
    std::vector<fix::FixMsg> sent(const std::string &type)
    {
      std::lock_guard<std::mutex> l(mu_);
      std::vector<fix::FixMsg> v;
      for (auto &m : sent_)
        if (m.type() == type)
          v.push_back(m);
      return v;
    }
    std::vector<SessionInfo> sessions()
    {
      std::lock_guard<std::mutex> l(mu_);
      return sessions_;
    }
    int logouts()
    {
      std::lock_guard<std::mutex> l(mu_);
      return logouts_;
    }

    // ---- the venue speaking (test thread) --------------------------------------------------
    bool deliver(const fix::FixMsg &m)
    {
      std::lock_guard<std::mutex> l(mu_);
      if (!live_)
        return false;
      live_->message(m);
      return true;
    }
    void disconnect(const std::string &why = "test disconnect")
    {
      std::lock_guard<std::mutex> l(mu_);
      if (live_)
        live_->disconnected(why);
      live_ = nullptr;
    }
    void busy()
    {
      std::lock_guard<std::mutex> l(mu_);
      if (live_)
        live_->busy();
    }
    void available()
    {
      std::lock_guard<std::mutex> l(mu_);
      if (live_)
        live_->available();
    }
    bool connected()
    {
      std::lock_guard<std::mutex> l(mu_);
      return live_ != nullptr;
    }

    SessionFactory factory()
    {
      return [this](const SessionConfig &c, SessionEvents &ev) -> std::unique_ptr<CbxSession> {
        std::lock_guard<std::mutex> l(mu_);
        sessions_.push_back({c, std::this_thread::get_id()});
        return std::make_unique<Session>(*this, ev);
      };
    }

  private:
    class Session : public CbxSession
    {
    public:
      Session(FakeCbxServer &s, SessionEvents &ev) : s_(s), ev_(ev) {}
      ~Session() override
      {
        std::lock_guard<std::mutex> l(s_.mu_);
        if (s_.live_ == &ev_)
          s_.live_ = nullptr;
      }
      void logon() override
      {
        std::lock_guard<std::mutex> l(s_.mu_);
        if (s_.reject_logon)
        {
          ev_.logon_rejected("Logout: invalid signature");
          return;
        }
        s_.live_ = &ev_;
        if (s_.auto_logon)
          ev_.logged_on();
      }
      CallStatus send(const fix::FixMsg &m) override
      {
        std::lock_guard<std::mutex> l(s_.mu_);
        if (s_.live_ != &ev_)
          return CallStatus::Disconnected;
        auto st = s_.send_status.load();
        if (st == CallStatus::Success)
          s_.sent_.push_back(m);
        return st;
      }
      void logout() override
      {
        std::lock_guard<std::mutex> l(s_.mu_);
        ++s_.logouts_;
      }

    private:
      FakeCbxServer &s_;
      SessionEvents &ev_;
    };

    std::mutex mu_;
    SessionEvents *live_ = nullptr;
    std::vector<fix::FixMsg> sent_;
    std::vector<SessionInfo> sessions_;
    int logouts_ = 0;
  };
}
