#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// QLen -- periodic sampler of per-actor mailbox depth, message count and
// thread context switches. Emits one l3_qlen_t per actor per tick into the
// bin recorder, so the samples land in the same L3 stream as the market data
// and can be joined against it on t0.
//
// WHAT THIS IS NOT: it is a *gauge*, sampled on a wall-clock grid. A burst
// that fills and drains a mailbox between two ticks is invisible to it. It
// does not measure per-message queue depth; that would require carrying the
// depth on the message at enqueue time.
//
// WHAT IS A RATE: `msg` is the actor's cumulative message counter. Differencing
// it across consecutive ticks gives per-actor messages/sec, which is a genuine
// throughput measurement and is not subject to the gauge caveat above.
//
// Ported from polonaise_/src/QLen.cpp, deleted in 9588337 (2026-01-23) during
// the polonaise -> frame rename. Changes from the original:
//   - Manager API renames: getQLengths -> get_queue_lengths,
//     getNumMessages -> get_message_counts (tuple, not pair).
//   - Default period 100 ms (10 Hz), was 1000 ms.
//   - wake_up_at instead of wake_up_in, so ticks snap to a wall-clock grid and
//     the sampling interval does not drift by the cost of the work.
//   - The /proc scrape is decimated (see ctx_every below).
//   - Optional name filter, to bound recorder load.
//   - The gperftools/ProfilerStart console hook is dropped: it was unrelated
//     to queue length and pulled in a link dependency.

#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <unistd.h>
#include <string>
#include <vector>
#include <map>
#include <cstring>
#include <algorithm>
#include <utility>

#include "chutil/Macros.hpp"
#include "chutil/Time.hpp"
#include "actors/Actor.hpp"
#include "actors/act/Manager.hpp"
#include "actors/act/Timer.hpp"
#include "actors/msg/Start.hpp"
#include "actors/msg/Shutdown.hpp"
#include "actors/msg/Continue.hpp"
#include "actors/msg/Timeout.hpp"
#include "frame/mda/msg/Data.hpp"
#include "frame/cons/msg/Get.hpp"
#include "frame/cons/msg/Page.hpp"

#include "bfile/r_l3.hpp"

namespace frame::qlen::act
{
  struct QLen : public actors::Actor
  {
    struct ThreadContextSwitches
    {
      unsigned long voluntary;
      unsigned long nonvoluntary;
    };

    const char *get_name() const override { return "QLen"; }

    actors::Manager *man;
    actor_ptr binrec;

    // Sampling grid, in milliseconds. 100 => 10 Hz.
    int period_ms;

    // Scrape /proc every Nth tick only. Reading
    // /proc/<pid>/task/<tid>/status for every thread costs one open+read+close
    // per thread per pass; measured at 5.4 ms/pass for 326 threads (Python
    // upper bound, C++ is faster but the syscalls dominate). At 10 Hz that is
    // ~3300 file opens/sec purely to observe a pair of slow-moving counters.
    // Queue depth, by contrast, is a map walk and costs microseconds.
    //
    // So the two are decoupled: depth is sampled every tick, context switches
    // every ctx_every ticks. The ctx fields on a record are therefore up to
    // (ctx_every-1)*period_ms stale. That is harmless for their intended use --
    // differencing a monotonic counter to get a rate -- but it does mean you
    // must NOT read the ctx delta between two adjacent records as the switches
    // that occurred in that 100 ms. Difference across refresh boundaries only.
    // Set ctx_every = 1 to scrape on every tick and pay the cost.
    int ctx_every;

    // Record only actors whose name starts with one of these prefixes. Empty
    // => record everything. This is a load control, not a filter of
    // convenience: at 10 Hz with 326 actors the recorder takes 3260 extra
    // messages/sec and ~290 KB/s of pre-gzip record, and the recorder is
    // itself one of the actors under measurement.
    std::vector<std::string> prefixes;

    uint64_t tick = 0;
    std::map<long, ThreadContextSwitches> tcs_cache;

    QLen(actors::Manager *_man,
         actor_ptr _binrec,
         int _period_ms = 100,
         int _ctx_every = 10,
         const std::vector<std::string> &_prefixes = {})
        : man(_man), binrec(_binrec), period_ms(_period_ms),
          ctx_every(_ctx_every < 1 ? 1 : _ctx_every), prefixes(_prefixes)
    {
      MESSAGE_HANDLER(actors::msg::Start, start_handler);
      MESSAGE_HANDLER(actors::msg::Shutdown, shutdown_handler);
      MESSAGE_HANDLER(actors::msg::Continue, continue_handler);
      MESSAGE_HANDLER(actors::msg::Timeout, timeout_handler);
      MESSAGE_HANDLER(frame::cons::msg::Get, get_handler);

      ASSERT(man, "manager not set");
      ASSERT(period_ms > 0, "period_ms must be positive");
    }

    bool wanted(const std::string &name) const
    {
      if (prefixes.empty())
        return true;
      for (const auto &p : prefixes)
        if (name.compare(0, p.size(), p) == 0)
          return true;
      return false;
    }

    // Per-thread voluntary / non-voluntary context switches for this process,
    // keyed by tid. Threads that appear and vanish between passes (the Timer
    // spawns a detached thread per tick) are simply absent from the map; they
    // are never looked up, since lookups are keyed by actor tid.
    std::map<long, ThreadContextSwitches> get_thread_context_switches()
    {
      std::map<long, ThreadContextSwitches> results;

      const std::string task_path = "/proc/" + std::to_string(getpid()) + "/task";

      std::error_code ec;
      if (!std::filesystem::exists(task_path, ec))
      {
        std::cerr << "QLEN: cannot access " << task_path << std::endl;
        return results;
      }

      for (const auto &entry : std::filesystem::directory_iterator(task_path, ec))
      {
        const std::string tid_str = entry.path().filename().string();
        long tid;
        try
        {
          tid = std::stol(tid_str);
        }
        catch (...)
        {
          continue;
        }

        std::ifstream status_file(task_path + "/" + tid_str + "/status");
        if (!status_file.is_open())
          continue; // thread exited between readdir and open -- expected

        ThreadContextSwitches t = {0, 0};
        int got = 0;
        std::string line;
        while (got < 2 && std::getline(status_file, line))
        {
          unsigned long *dst = nullptr;
          if (line.compare(0, 24, "voluntary_ctxt_switches:") == 0)
            dst = &t.voluntary;
          else if (line.compare(0, 27, "nonvoluntary_ctxt_switches:") == 0)
            dst = &t.nonvoluntary;
          if (!dst)
            continue;
          try
          {
            *dst = std::stoul(line.substr(line.find(':') + 1));
            ++got;
          }
          catch (...)
          {
          }
        }
        results[tid] = t;
      }

      return results;
    }

    void start_handler(const actors::msg::Start *) noexcept
    {
      // Let the system finish standing up before the first sample.
      actors::act::Timer::wake_up_in(this, 1, 0);
    }

    void shutdown_handler(const actors::msg::Shutdown *) {}
    void continue_handler(const actors::msg::Continue *) {}

    void timeout_handler(const actors::msg::Timeout *) noexcept
    {
      // Rearm first, on the wall-clock grid: wake_up_at snaps to the next
      // period_ms boundary, so the cost of the work below does not accumulate
      // into the sampling interval the way a rearm-by-delay would.
      actors::act::Timer::wake_up_at(this, period_ms);

      // NOTE ON RACES. get_queue_lengths()/get_message_counts() read each
      // actor's depth and msg_cnt from this thread while the owning threads
      // mutate them. Both are aligned scalars, so on x86-64 the read cannot
      // tear; it can only be stale by one message. That is exactly the
      // accuracy a sampler claims, so no synchronisation is added. What would
      // NOT be safe is iterating the manager's actor map while actors are
      // created or destroyed -- kaspr builds every actor before Start and
      // tears down at exit, so the map is stable for the life of the run.
      const auto ql = man->get_queue_lengths();
      const auto num = man->get_message_counts();

      if (tick % ctx_every == 0)
        tcs_cache = get_thread_context_switches();
      ++tick;

      const auto t0 = chutil::Time::epoch();

      for (const auto &[name, tid_msg] : num)
      {
        if (!wanted(name))
          continue;

        const auto [tid, msg] = tid_msg;

        std::size_t qlen = 0;
        if (auto it = ql.find(name); it != ql.end())
          qlen = it->second;

        unsigned long v_ctx = 0, nv_ctx = 0;
        if (auto it = tcs_cache.find(tid); it != tcs_cache.end())
        {
          v_ctx = it->second.voluntary;
          nv_ctx = it->second.nonvoluntary;
        }

        if (binrec)
        {
          bfile::l3_qlen_t l3;
          memset(&l3, 0, sizeof(l3));
          l3.typ = en::l3::QLEN;
          strncpy(l3.name, name.c_str(), sizeof(l3.name) - 1);
          l3.t0 = t0;
          l3.size = uint32_t(qlen);
          l3.msg = uint32_t(msg);
          l3.v_ctx = uint32_t(v_ctx);
          l3.nv_ctx = uint32_t(nv_ctx);

          auto recmsg = new frame::mda::msg::Data();
          recmsg->l3 = l3;
          binrec->send(recmsg, this);
        }
      }
    }

    // Console: `qlen` prints the current sample, deepest first.
    void get_handler(const frame::cons::msg::Get *m)
    {
      if (m->what != "qlen")
        return;

      const auto ql = man->get_queue_lengths();
      const auto num = man->get_message_counts();

      std::vector<std::pair<std::size_t, std::string>> rows;
      for (const auto &[name, len] : ql)
        if (wanted(name))
          rows.emplace_back(len, name);
      std::sort(rows.rbegin(), rows.rend());

      std::stringstream ss;
      ss << "period_ms=" << period_ms << " ctx_every=" << ctx_every
         << " actors=" << rows.size() << "\n";
      for (const auto &[len, name] : rows)
      {
        long long msg = 0;
        if (auto it = num.find(name); it != num.end())
          msg = std::get<1>(it->second);
        ss << name << "\t" << len << "\t" << msg << "\n";
      }
      reply(new frame::cons::msg::Page(ss.str()));
    }
  };
}
