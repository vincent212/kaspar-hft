#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
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
    mutable int ts_cnt = 0;

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
      if (ts_cnt++ % 16 == 0)
        m->recv_ts = chutil::Time::epoch();
      else
        m->recv_ts = 0;
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
