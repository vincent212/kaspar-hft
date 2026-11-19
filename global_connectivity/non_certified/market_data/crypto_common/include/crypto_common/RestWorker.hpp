#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// RestWorker: one thread that runs posted jobs in order. A session whose
// WebSocket thread must keep reading (and answering the venue's keep-alive)
// hands its synchronous HTTPS work to one of these instead of blocking the
// socket on it. A job that throws ends with its exception swallowed: a job
// reports its own failure.

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace crypto_common
{
  class RestWorker
  {
  public:
    RestWorker() = default;
    ~RestWorker() { stop(); }
    RestWorker(const RestWorker &) = delete;
    RestWorker &operator=(const RestWorker &) = delete;

    void start()
    {
      std::lock_guard<std::mutex> l(mu_);
      if (!thread_.joinable())
        thread_ = std::thread([this] { run(); });
    }

    // Queued for the worker; dropped if the worker has been stopped.
    void post(std::function<void()> job)
    {
      {
        std::lock_guard<std::mutex> l(mu_);
        if (stop_)
          return;
        jobs_.push_back(std::move(job));
      }
      cv_.notify_one();
    }

    // Jobs not yet started are dropped; the one running completes.
    void stop()
    {
      {
        std::lock_guard<std::mutex> l(mu_);
        stop_ = true;
        jobs_.clear();
      }
      cv_.notify_all();
      if (thread_.joinable())
        thread_.join();
    }

    bool stopped() const
    {
      std::lock_guard<std::mutex> l(mu_);
      return stop_;
    }

  private:
    void run()
    {
      for (;;)
      {
        std::function<void()> job;
        {
          std::unique_lock<std::mutex> l(mu_);
          cv_.wait(l, [&] { return stop_ || !jobs_.empty(); });
          if (stop_)
            return;
          job = std::move(jobs_.front());
          jobs_.pop_front();
        }
        try
        {
          job();
        }
        catch (...)
        {
        }
      }
    }

    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::deque<std::function<void()>> jobs_;
    bool stop_ = false;
    std::thread thread_;
  };
}
