#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/*
 * SoftCard -- an FPGA design run in software, for functional work without a card.
 *
 * Each process of the design (host link, router, each actor) runs in its own
 * thread, repeatedly calling its step function, as it runs on its own on the
 * chip. The design's streams are the thread-safe hls::stream stand-in, and a
 * blocked read (an actor waiting for the reply to its own fast_send) blocks only
 * that process's thread, as it stalls only that process in hardware.
 *
 * It says nothing about timing: it exists so the CPU/FPGA semantics can be run
 * and tested. Latency comes from synthesis (see README.md, "Measuring").
 */

#include <atomic>
#include <functional>
#include <thread>
#include <vector>

#include "CardTransport.hpp"
#include "actors_fpga/stream.hpp"

namespace kfpga {

class SoftCard : public CardTransport
{
public:
  using Step = std::function<bool()>;   // one step of one process; false = idle

  SoftCard(std::vector<Step> steps, hls::stream<Envelope> &from_host,
           hls::stream<Envelope> &to_pcie)
    : steps_(std::move(steps)), from_host_(from_host), to_pcie_(to_pcie)
  {
  }

  ~SoftCard() override { stop(); }

  void start()
  {
    stop_ = false;
    for (auto &s : steps_)
      threads_.emplace_back([this, s] {
        while (!stop_.load(std::memory_order_relaxed))
          if (!s())
            std::this_thread::yield();
      });
  }

  // Joins the process threads. Call only when no actor is waiting on its own
  // fast_send, since such a thread cannot see the stop flag until its reply comes.
  void stop()
  {
    stop_ = true;
    for (auto &t : threads_)
      if (t.joinable())
        t.join();
    threads_.clear();
  }

  void write(const Envelope &e) override { from_host_.write(e); }
  bool read(Envelope &e) override { return to_pcie_.read_nb(e); }

private:
  std::vector<Step> steps_;
  hls::stream<Envelope> &from_host_;
  hls::stream<Envelope> &to_pcie_;
  std::vector<std::thread> threads_;
  std::atomic<bool> stop_{false};
};

} // namespace kfpga
