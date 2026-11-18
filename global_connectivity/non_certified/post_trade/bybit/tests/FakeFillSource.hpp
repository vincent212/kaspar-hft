#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// FakeFillSource: the test plays Bybit. Each source the client creates is
// recorded; the test drives the latest one (log in, push executions, answer
// reconciliations, disconnect).

#include <mutex>
#include <string>
#include <vector>

#include "bybit_post_trade/FillSource.hpp"

namespace bybit_pt::test
{
  struct ReconcileCall
  {
    std::string category;
    uint64_t from, to;
  };

  class FakeFills
  {
  public:
    bool auto_login = true;
    int sources = 0;

    SourceFactory factory()
    {
      return [this](const SourceConfig &, FillEvents &ev) -> std::unique_ptr<FillSource> {
        std::lock_guard<std::mutex> l(mu_);
        ++sources;
        ev_ = &ev;
        return std::make_unique<Src>(this, ev);
      };
    }
    std::vector<ReconcileCall> calls()
    {
      std::lock_guard<std::mutex> l(mu_);
      return calls_;
    }
    void logged_in() { ev()->logged_in(); }
    void disconnect() { ev()->disconnected("test"); }
    void exec(const std::string &json, const std::string &cat = "linear", bool rest = false)
    {
      ev()->execution(json, cat, rest);
    }
    void reconciled(const std::string &cat, int count, bool ok = true)
    {
      ev()->reconciled(cat, 1, count, ok, ok ? "" : "HTTP 200 retCode 10006 rate limit");
    }

  private:
    struct Src : FillSource
    {
      Src(FakeFills *f, FillEvents &ev) : f_(f), ev_(ev) {}
      void logon() override
      {
        if (f_->auto_login)
          ev_.logged_in();
      }
      void reconcile(const std::string &c, uint64_t a, uint64_t b) override
      {
        std::lock_guard<std::mutex> l(f_->mu_);
        f_->calls_.push_back({c, a, b});
      }
      void logout() override {}
      FakeFills *f_;
      FillEvents &ev_;
    };
    FillEvents *ev()
    {
      std::lock_guard<std::mutex> l(mu_);
      return ev_;
    }
    std::mutex mu_;
    FillEvents *ev_ = nullptr;
    std::vector<ReconcileCall> calls_;
  };
}
