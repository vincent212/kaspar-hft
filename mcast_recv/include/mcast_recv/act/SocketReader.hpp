#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <cstdio>
#include "chutil/Macros.hpp"
#include "actors/Actor.hpp"
#include "actors/msg/Start.hpp"
#include "actors/msg/Shutdown.hpp"
#include "actors/msg/Continue.hpp"
#include "chutil/udp_socket.hpp"
#include <map>
#include <chrono>
#include "mcast_recv/message_buffer.hpp"
#include "mcast_recv/msg/ProcessQ.hpp"
#include "logger/act/Logger.hpp"

#include "actors/act/Timer.hpp"
#include "actors/msg/Timeout.hpp"
#include <queue>
#include <boost/endian/conversion.hpp>
#include <boost/circular_buffer.hpp>
#include "actors/HybridBuffer.hpp"

namespace mcast_recv
{

  template <typename seqnumT, uint8_t N>
  class SocketReader : public actors::Actor
  {
    static constexpr size_t PREALLOC_QUEUE_SIZE = 1024;

  private:
    const char* get_name() const { return name; }
    char name[256];
    std::string chan_nam;

    actors::Actor *msg_processor;
    in_port_t port;
    int sock;
    struct sockaddr_in addr;
    std::size_t addrlen;
    std::string group, interface, desc;

    // for cme its uint32_t, for fenics its uint64_t
    // seqnumT seq_num = 0;
    const uint8_t seq_num_offset = N; // for cme its 0, for fenics its 10

    std::vector<double> read_cnt;
    char chan;
    bool newloop = false;
    uint32_t num_gaps = 0;
    bool read_loop = true;
    bool fastsend;
    uint64_t last_ts = 0;
    bool big_endian;
    // ts_cnt is gone with the subsample in read(). Left as a note because a
    // stride counter here is the obvious thing to reach for again, and the
    // measurement that says not to is in read()'s comment.

    void read(message_buffer *m) const noexcept
    {
      size_t nrec = 0;
      while (!nrec)
      {
        nrec = chutil::mcast::receive(sock, &m->message[0], mcast_recv ::msgsz, addr, addrlen);
        ASSERT(nrec<=mcast_recv::msgsz, "message too big");
      }
      m->len = nrec;
      auto seq_num = *(seqnumT *)(&m->message[0] + seq_num_offset);
      if (big_endian)
        seq_num = boost::endian::big_to_native(seq_num);
      m->seqnum = seq_num;
      m->chan = chan;
      // EVERY PACKET IS STAMPED. There is no subsample any more. 16 then 5;
      // now 1, because the reason for the divisor did not survive being
      // measured.
      //
      // The old comment here argued "clock_gettime on every packet is a cost
      // on this loop, and this loop is what keeps the socket drained." That is
      // false on this box, and the measurements are:
      //
      //   clocksource                  tsc   (/sys/devices/system/clocksource/
      //                                       clocksource0/current_clocksource)
      //
      // tsc is the condition for the vDSO fast path, so system_clock::now()
      // resolves in userspace against the TSC and NEVER ENTERS THE KERNEL. It
      // is not a system call. strace -c over 2,100,000 calls counted 66
      // syscalls total, every one of them process startup, and zero
      // clock_gettime. That is the proof; the rest is cost:
      //
      //   Time::epoch()        p50    20.25 ns   (p99 21.87, max 22.55)
      //   recvfrom, loopback   p50      510 ns   (min 240)
      //
      // Loopback recvfrom has no driver, no NAPI and no wire, so 510 ns is a
      // hard LOWER BOUND on the read it is being compared against. The clock
      // is 4% of that floor and a smaller fraction of the real thing.
      //
      // In absolute terms: be generous and call the whole read loop 500k
      // packets/sec in a burst. 500k x 20 ns = 10 ms/sec, i.e. 1% of one core,
      // to stamp everything. The 1-in-5 was buying back 0.8% of a core.
      //
      // WHAT IT COST TO BUY IT. A packet with recv_ts == 0 carries no t0, so
      // handler_if stamps handlerendtim = 0 and LatencyProbe::sample() threw it
      // into rej_zero_t0 before recording either leg or the qlen -- four fifths
      // of all arrivals discarded. Over 44.8k bins on 2026-09-16 that left the
      // median non-empty bin at n=1 for ESZ6 and n=0 for ZNZ6: a "bin mean"
      // computed from a single observation, or none. It also destroyed every
      // quantile and the whole distribution shape, and it made the per-bin
      // min/max bounds on a thinned subset rather than on the bin.
      //
      // Consequences of stamping all, so nothing downstream reads as a fault:
      //   - rej_zero_t0 should now be 0. Non-zero means a genuinely unstamped
      //     record is reaching a subscriber, which is a real finding again
      //     instead of the expected case.
      //   - the probe's sample_ratio (all_n / l1_n) should now be 1.0, not ~5.
      //   - leg 1 now includes this 20 ns in every measurement rather than in
      //     one fifth of them. It was always inside the interval; it is now
      //     uniformly inside it, which is the honest version.
      m->recv_ts = chutil::Time::epoch();
    }

#ifdef DEBUGSOCKET
#endif



  public:
    SocketReader(
        const std::string &chan_nam,
        bool blocksock,
        const char chan,
        actors::Actor *msg_processor,
        in_port_t port,
        const char *group,
        const char *interface,
        const char *desc,
        bool big_endian = false)
        : newloop(blocksock),
          chan(chan),
          port(port),
          group(group),
          interface(interface),
          desc(desc),
          msg_processor(msg_processor),
          chan_nam(chan_nam),
          big_endian(big_endian)
    {
      // Initialize name once in constructor
      snprintf(name, sizeof(name), "%sSocketReader %c", chan_nam.c_str(), chan);

      MESSAGE_HANDLER(actors::msg::Start, start_handler);
      MESSAGE_HANDLER(actors::msg::Continue, continue_handler);
      MESSAGE_HANDLER(actors::msg::Timeout, timeout_handler);
    }

//#define DEBUGSOCKET

  private:
    void start_handler(const actors::msg::Start *) noexcept
    {
      // port a and b are mc ports
      // group a and b are mc groups
      chutil::mcast::create_udp_socket(port, sock, addr, addrlen);
      chutil::mcast::join_group(sock, group.c_str(), interface.c_str());
      send(new actors::msg::Continue(), 0);
      log_inf("joined : %s, interfce: %s, port: %d, name: %s", group, interface, port, desc);
      actors::act::Timer::wake_up_in(this, 60);
#ifdef DEBUGSOCKET
std::cerr << get_name() << " started on port: " << port
          << ", group: " << group
          << ", interface: " << interface
          << ", desc: " << desc
          << ", sock: " << sock
          << std::endl;
#endif
    }

    // make sure we start using continue_handler_new
    void continue_handler(const actors::msg::Continue *) noexcept
    {
      if (!newloop) //
      {
        continue_handler_old(nullptr);
        ERR("old loop in Socket Reader");
      }
      else
      {
        continue_handler_new(nullptr);
      }
    }

    void continue_handler_old(const actors::msg::Continue *) noexcept
    {
      while (read_loop)
      {
        auto msg = new msg::ProcessQ<seqnumT>();
        read(&msg->buf); // block
        msg_processor->send(msg, this);
      }
    }

    // this implementation pre-dates the message pool
    // do we still need to pre-allocate?
    //
    // No. Traced 2026-09-16; the answer is in the loop below.
    //
    // pre_alloc_q is a one-way drain. A buffer taken at the top is handed to
    // msg_processor->send() and ownership goes with it -- nothing ever pushes
    // it back. So the 1024 buffers allocated at entry are spent on the first
    // 1024 packets and the queue is empty for the rest of the run. Every packet
    // after that is served by the refill below, i.e. a fresh `new`, which since
    // ProcessQ gained MemoryPool<...,16,16,4096> is a pool hit anyway. The
    // pre-allocation therefore buys one burst at startup and nothing after.
    //
    // What it still does, permanently, is cap the read batch. Once empty:
    //   read loop (cond `!pre_alloc_q.empty()`) exits having read 0
    //   refill                                  +4 buffers
    //   drain q, send downstream
    //   has_more(sock) -> goto L1               read <= 4, refill +4, repeat
    // The refill sits after the read loop and before the drain, and L1 is above
    // both, so every pass through L1 hits it. The loop settles at 4 packets per
    // pass and stays there.
    //
    // It does NOT stall and it does NOT stop draining the socket -- an earlier
    // reading of this code claimed silent packet loss under burst and that was
    // wrong, the reader keeps reading. The cost is batching efficiency only:
    // 4 reads + 4 pool allocs + 4 sends per pass instead of one large batch.
    // Still worth removing, since a knob that looks like a pre-allocation and
    // actually behaves like a rate limiter is the kind of thing that gets
    // mis-tuned. See the tracking issue.
    void continue_handler_new(const actors::msg::Continue *) noexcept
    {
      actors::HybridBuffer<msg::ProcessQ<seqnumT> *> q(256);
      boost::circular_buffer<msg::ProcessQ<seqnumT> *> pre_alloc_q(PREALLOC_QUEUE_SIZE);
      for (size_t i = 0; i < PREALLOC_QUEUE_SIZE; i++)
      {
        pre_alloc_q.push_back(new msg::ProcessQ<seqnumT>());
      }
      while (read_loop)
      {
#ifdef DEBUGSOCKET
std::cerr << get_name() << " read loop: " << port
          << ", group: " << group
          << ", interface: " << interface
          << ", desc: " << desc
          << ", sock: " << sock
          << std::endl;
#endif
        chutil::mcast::wait_for_data(sock); // block/spin
      L1:
        bool just_got_data = true;
        while ((just_got_data || chutil::mcast::has_more(sock)) && CHLIKELY(!pre_alloc_q.empty()))
        {
          just_got_data = false;
          auto msg = pre_alloc_q.back();
          pre_alloc_q.pop_back();
          read(&msg->buf); // block/spin
          q.push_back(msg);
        }
        if (pre_alloc_q.size() < PREALLOC_QUEUE_SIZE)
        {
          for (int i = 0; i < 4 && !pre_alloc_q.full(); i++)
          {
            pre_alloc_q.push_back(new msg::ProcessQ<seqnumT>());
          }
        }
        while (!q.empty())
        {
          auto msg = q.front();
          q.pop_front();
          msg_processor->send(msg, this);
          if (chutil::mcast::has_more(sock))
          {
            goto L1;
          }
        }
      }
    }

    void end()
    {
      read_loop = false;
      close(sock);
      log_inf("end: closed socket");
    }

    void timeout_handler(const actors::msg::Timeout *) noexcept
    {
      if (last_ts)
      {
        auto currtim = chutil::Time::epoch();
        auto interval = (double(currtim) - last_ts) / 1e9;
        if (interval > 31)
        {
          log_err("last message was more than 30s %f", interval);
          end();
          start_handler(0);
        }
      }
      actors::act::Timer::wake_up_in(this, 31);
    }
  };

}
