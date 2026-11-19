#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// SimFills: an in-process Kraken fills venue for --sim runs and tests. The
// venue keeps a fill history; a fill is "made" with make(), which adds it to
// the history and, unless the link is down or stream_drops says otherwise,
// delivers it on the stream. Reconciliation returns the history in the range,
// so a fill the stream missed comes back from it.

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "kraken_md/KrakenJson.hpp"
#include "kraken_pt/KrakenFills.hpp"

namespace kraken::pt
{
  class SimFills
  {
  public:
    struct Venue
    {
      std::mutex mu;
      Source stream_src = Source::FuturesPoll, rest_src = Source::FuturesRest;
      std::vector<std::pair<nlohmann::json, std::string>> history; // (fill, REST key)
      FillsEvents *live = nullptr;
      bool link_up = true;
      int stream_drops = 0; // the next n fills are not delivered on the stream
      int sessions = 0;
    };

    static FillsSessionFactory make_factory(std::shared_ptr<Venue> v)
    {
      return [v](FillsEvents &ev) -> std::unique_ptr<FillsSession> { return std::make_unique<Session>(v, ev); };
    }

    // A fill: futures /fills format when the venue is futures, spot executions
    // format (with its REST TradesHistory twin) when spot.
    static void make(Venue &v, const nlohmann::json &stream_form, const nlohmann::json &rest_form = {},
                     const std::string &key = "")
    {
      std::lock_guard<std::mutex> l(v.mu);
      v.history.emplace_back(rest_form.is_null() ? stream_form : rest_form, key);
      if (v.stream_drops > 0)
      {
        --v.stream_drops;
        return;
      }
      if (v.live && v.link_up)
        v.live->fill(stream_form, "", v.stream_src);
    }

    static void drop_link(Venue &v)
    {
      std::lock_guard<std::mutex> l(v.mu);
      v.link_up = false;
      if (v.live)
      {
        auto *e = v.live;
        v.live = nullptr;
        e->disconnected("simulated link failure");
      }
    }

  private:
    class Session : public FillsSession
    {
    public:
      Session(std::shared_ptr<Venue> v, FillsEvents &ev) : v_(std::move(v)), ev_(ev) {}
      ~Session() override
      {
        std::lock_guard<std::mutex> l(v_->mu);
        if (v_->live == &ev_)
          v_->live = nullptr;
      }
      void logon() override
      {
        std::lock_guard<std::mutex> l(v_->mu);
        ++v_->sessions;
        v_->link_up = true;
        v_->live = &ev_;
        ev_.logged_in();
      }
      bool reconcile(uint64_t start_ns) override
      {
        std::lock_guard<std::mutex> l(v_->mu);
        if (v_->live != &ev_)
          return false;
        size_t n = 0;
        for (auto &[f, key] : v_->history)
        {
          ++n;
          ev_.fill(f, key, v_->rest_src);
        }
        ev_.reconciled(n, true, "");
        (void)start_ns;
        return true;
      }
      void logout() override {}

    private:
      std::shared_ptr<Venue> v_;
      FillsEvents &ev_;
    };
  };
}
