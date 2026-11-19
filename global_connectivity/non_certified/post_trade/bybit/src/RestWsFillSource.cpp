/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// RestWsFillSource: FillSource over Bybit's private WebSocket (`execution`)
// and GET /v5/execution/list. NOT CERTIFIED; never connected to Bybit by the
// agent that wrote it (no testnet keys were supplied). Two threads:
//   stream  connects /v5/private, authenticates, subscribes to `execution`,
//           delivers each data element; pings every 20 s
//   rest    runs queued reconciliations: pages of 100 by nextPageCursor,
//           page_pause_ms apart. The cursor comes back URL-encoded and is
//           sent as it came, so the query string signed is the one sent.

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

#include "bybit_oe/BybitPrivate.hpp"
#include "bybit_post_trade/FillSource.hpp"

namespace bybit_pt
{
namespace
{
  class RestWsFillSource : public FillSource
  {
  public:
    RestWsFillSource(const SourceConfig &c, FillEvents &ev) : cfg_(c), ev_(ev)
    {
      bybit::priv::guard(c.testnet, c.rest_host, c.ws_host);
    }
    ~RestWsFillSource() override { stop(); }

    void logon() override
    {
      rest_ = std::thread([this] { rest_loop(); });
      stream_ = std::thread([this] { stream_loop(); });
    }
    void reconcile(const std::string &category, uint64_t from_ms, uint64_t to_ms) override
    {
      {
        std::lock_guard<std::mutex> l(mu_);
        q_.push_back({category, from_ms, to_ms});
      }
      cv_.notify_all();
    }
    void logout() override { stop(); }

  private:
    struct Job
    {
      std::string category;
      uint64_t from, to;
    };
    SourceConfig cfg_;
    FillEvents &ev_;
    std::mutex mu_, ws_mu_;
    std::condition_variable cv_;
    std::deque<Job> q_;
    std::atomic<bool> stopping_{false}, down_{false};
    std::thread rest_, stream_;
    std::unique_ptr<bybit::priv::PrivateWs> ws_;

    void stop()
    {
      stopping_ = true;
      cv_.notify_all();
      {
        std::lock_guard<std::mutex> l(ws_mu_);
        if (ws_)
          ws_->close();
      }
      if (stream_.joinable())
        stream_.join();
      if (rest_.joinable())
        rest_.join();
    }

    void stream_loop()
    {
      try
      {
        {
          std::lock_guard<std::mutex> l(ws_mu_);
          ws_ = std::make_unique<bybit::priv::PrivateWs>(cfg_.credentials, cfg_.ws_host, cfg_.testnet);
        }
        auto err = ws_->open({"execution"});
        if (!err.empty())
        {
          down_ = true;
          ev_.login_rejected(err);
          return;
        }
        ev_.logged_in();
        while (!stopping_)
        {
          auto j = nlohmann::json::parse(ws_->read());
          if (j.value("topic", std::string()).rfind("execution", 0) == 0)
            for (const auto &d : j.at("data"))
              ev_.execution(d.dump(), d.value("category", std::string()), false);
        }
      }
      catch (const std::exception &e)
      {
        if (!down_.exchange(true) && !stopping_)
          ev_.disconnected(std::string("private stream: ") + e.what());
      }
    }

    void rest_loop()
    {
      std::unique_ptr<bybit::priv::RestClient> rc;
      try
      {
        rc = std::make_unique<bybit::priv::RestClient>(cfg_.credentials, cfg_.rest_host, cfg_.testnet,
                                                       cfg_.recv_window_ms);
      }
      catch (const std::exception &e)
      {
        if (!down_.exchange(true))
          ev_.disconnected(std::string("REST: ") + e.what());
        return;
      }
      while (true)
      {
        Job job;
        {
          std::unique_lock<std::mutex> l(mu_);
          cv_.wait(l, [&] { return stopping_ || !q_.empty(); });
          if (stopping_)
            return;
          job = q_.front();
          q_.pop_front();
        }
        int pages = 0, count = 0;
        std::string cursor, why;
        bool ok = true;
        do
        {
          if (pages)
          {
            std::unique_lock<std::mutex> l(mu_);
            if (cv_.wait_for(l, std::chrono::milliseconds(cfg_.page_pause_ms), [&] { return bool(stopping_); }))
              return;
          }
          std::string q = "category=" + job.category + "&startTime=" + std::to_string(job.from) +
                          "&endTime=" + std::to_string(job.to) + "&limit=100";
          if (!cursor.empty())
            q += "&cursor=" + cursor;
          auto r = rc->get("/v5/execution/list", q);
          ++pages;
          if (!r.ok())
          {
            ok = false;
            why = "HTTP " + std::to_string(r.http) + " retCode " + std::to_string(r.ret_code) + " " + r.ret_msg;
            break;
          }
          for (const auto &d : r.result.value("list", nlohmann::json::array()))
          {
            ev_.execution(d.dump(), job.category, true);
            ++count;
          }
          cursor = r.result.value("nextPageCursor", std::string());
        } while (!cursor.empty() && !stopping_);
        ev_.reconciled(job.category, pages, count, ok, why);
      }
    }
  };
}

SourceFactory rest_ws_source_factory()
{
  return [](const SourceConfig &c, FillEvents &ev) -> std::unique_ptr<FillSource> {
    return std::make_unique<RestWsFillSource>(c, ev);
  };
}

}
