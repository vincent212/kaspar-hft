#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/*
 * The CPU half of the cpu_fpga example: ordinary Kaspar actors (actors/cpp).
 * They use the messages declared once in messages.hpp, wrapped as CPU
 * messages by kfpga::CpuMsg, and reach the FPGA actors through ActorRefs that
 * a kfpga::CpuEnd hands out. Nothing here knows the peer is on a card:
 * send() and reply() are the usual calls.
 *
 *   CpuPing    plays `rounds` round trips against the FPGA PongActor
 *   CpuDriver  sends Start to the FPGA PingActor and waits for its Done
 *   CpuPong    answers the FPGA PingActor's Pings
 */

#include <cstdio>
#include <cstring>
#include <functional>

#include "actors/Actor.hpp"
#include "actors/ActorRef.hpp"
#include "actors/msg/Start.hpp"
#include "kfpga/cpu_link.hpp"
#include "messages.hpp"

namespace pingpong {
namespace cpu {

using Start = kfpga::CpuMsg<pingpong::Start>;
using Ping = kfpga::CpuMsg<pingpong::Ping>;
using Pong = kfpga::CpuMsg<pingpong::Pong>;
using Done = kfpga::CpuMsg<pingpong::Done>;

// The messages that cross the link, for the codec.
inline void add_messages(kfpga::Codec &codec)
{
  codec.add<pingpong::Start, pingpong::Ping, pingpong::Pong, pingpong::Done>();
}

// Called once by each driving actor with a result line and whether it passed.
using Report = std::function<void(const char *line, bool ok)>;

class CpuPing : public actors::Actor
{
  actors::ActorRef pong_;   // the FPGA PongActor
  uint32_t rounds_;
  uint32_t replies_ = 0;
  Report report_;

public:
  CpuPing(uint32_t rounds, Report report) : rounds_(rounds), report_(std::move(report))
  {
    std::strncpy(name, "CpuPing", sizeof(name) - 1);
    set_mailbox(MailboxKind::FpgaLanes);
    MESSAGE_HANDLER(actors::msg::Start, on_start);
    MESSAGE_HANDLER(Pong, on_pong);
  }

  void set_pong(actors::ActorRef pong) { pong_ = pong; }

private:
  void on_start(const actors::msg::Start *) { pong_.send(new Ping(1), this); }

  void on_pong(const Pong *m)
  {
    ++replies_;
    if (replies_ < rounds_)
    {
      pong_.send(new Ping(replies_ + 1), this);
      return;
    }
    char line[128];
    std::snprintf(line, sizeof(line), "CPU -> FPGA  CpuPing:  %u round trips with FPGA PongActor, last Pong(%u)",
                  replies_, m->count);
    report_(line, m->count == rounds_);
  }
};

class CpuDriver : public actors::Actor
{
  actors::ActorRef ping_;   // the FPGA PingActor
  uint32_t rounds_;
  Report report_;

public:
  CpuDriver(uint32_t rounds, Report report) : rounds_(rounds), report_(std::move(report))
  {
    std::strncpy(name, "CpuDriver", sizeof(name) - 1);
    set_mailbox(MailboxKind::FpgaLanes);
    MESSAGE_HANDLER(actors::msg::Start, on_start);
    MESSAGE_HANDLER(Done, on_done);
  }

  void set_ping(actors::ActorRef ping) { ping_ = ping; }

private:
  void on_start(const actors::msg::Start *) { ping_.send(new Start(rounds_), this); }

  void on_done(const Done *m)
  {
    char line[128];
    std::snprintf(line, sizeof(line), "FPGA -> CPU  PingActor: %u round trips with CPU CpuPong, last Pong(%u)",
                  m->rounds, m->final_count);
    report_(line, m->rounds == rounds_ && m->final_count == rounds_);
  }
};

class CpuPong : public actors::Actor
{
public:
  uint32_t pings = 0;

  CpuPong()
  {
    std::strncpy(name, "CpuPong", sizeof(name) - 1);
    set_mailbox(MailboxKind::FpgaLanes);
    MESSAGE_HANDLER(Ping, on_ping);
  }

private:
  void on_ping(const Ping *m)
  {
    ++pings;
    reply(new Pong(m->count));   // back out through the window, on this thread
  }
};

} // namespace cpu
} // namespace pingpong
