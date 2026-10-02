#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/*
 * The CPU half: ordinary Kaspar actors and messages.
 *
 * The message classes carry the same ids and fields as the FPGA structs in
 * fpga_side.hpp; that is what lets the bridge convert between them.
 *
 *   CpuPong    (id 8)  replies to each Ping, like FpgaPong
 *   CpuDriver  (id 9)  runs the four cases:
 *                        1. CPU -> FPGA fast_send   Ping(1) to FpgaPong
 *                        2. CPU -> FPGA send        Ping(2) to FpgaPong
 *                        3. FPGA -> CPU send        Go(rounds 3, send) to FpgaCaller
 *                        4. FPGA -> CPU fast_send   Go(rounds 3, fast_send) to FpgaCaller
 */

#include <cstdio>
#include <cstring>
#include <functional>

#include "actors/Actor.hpp"
#include "actors/ActorRef.hpp"
#include "actors/msg/Start.hpp"
#include "actors_fpga/envelope.hpp"

namespace cpu_side {

struct Go : public actors::Message_N<300>
{
  uint32_t rounds = 0;
  uint32_t mode = 0;
  Go() = default;
  Go(uint32_t r, uint32_t m) : rounds(r), mode(m) {}
  KFPGA_FIELDS(rounds, mode)
};
struct Ping : public actors::Message_N<301>
{
  uint32_t count = 0;
  Ping() = default;
  explicit Ping(uint32_t c) : count(c) {}
  KFPGA_FIELDS(count)
};
struct Pong : public actors::Message_N<302>
{
  uint32_t count = 0;
  Pong() = default;
  explicit Pong(uint32_t c) : count(c) {}
  KFPGA_FIELDS(count)
};
struct Done : public actors::Message_N<303>
{
  uint32_t last = 0;
  uint32_t rounds = 0;
  KFPGA_FIELDS(last, rounds)
};

class CpuPong : public actors::Actor
{
public:
  CpuPong()
  {
    std::strncpy(name, "CpuPong", sizeof(name) - 1);
    MESSAGE_HANDLER(Ping, on_ping);
  }
  uint32_t pings = 0;

private:
  void on_ping(const Ping *m)
  {
    ++pings;
    reply(new Pong(m->count + 1000 * pings));   // back to whoever sent the Ping
  }
};

class CpuDriver : public actors::Actor
{
public:
  // report(line) receives each result line; done() is called at the end.
  CpuDriver(actors::ActorRef fpga_pong, actors::ActorRef fpga_caller,
            std::function<void(const char *)> report, std::function<void()> done)
    : fpga_pong_(fpga_pong), fpga_caller_(fpga_caller), report_(report), done_(done)
  {
    std::strncpy(name, "CpuDriver", sizeof(name) - 1);
    MESSAGE_HANDLER(actors::msg::Start, on_start);
    MESSAGE_HANDLER(Pong, on_pong);
    MESSAGE_HANDLER(Done, on_done);
  }

private:
  void line(const char *fmt, unsigned a, unsigned b = 0)
  {
    char buf[160];
    std::snprintf(buf, sizeof(buf), fmt, a, b);
    report_(buf);
  }

  void on_start(const actors::msg::Start *)
  {
    // 1. CPU -> FPGA fast_send: FpgaPong handles it now; the reply is returned.
    Ping p(1);
    auto r = fpga_pong_.fast_send(&p, this);   // nullptr if FpgaPong did not reply
    if (r)
      line("1 CPU->FPGA fast_send  Ping(1) -> Pong(%u)", static_cast<const Pong *>(r.get())->count);
    else
      line("1 CPU->FPGA fast_send  Ping(1) -> no reply", 0);

    // 2. CPU -> FPGA send: the reply arrives later, in on_pong.
    fpga_pong_.send(new Ping(2), this);
  }

  void on_pong(const Pong *m)
  {
    line("2 CPU->FPGA send       Ping(2) -> Pong(%u)", m->count);
    // 3. FPGA -> CPU send: FpgaCaller plays with CpuPong using send.
    fpga_caller_.send(new Go(3, 0), this);
  }

  void on_done(const Done *m)
  {
    if (++dones_ == 1)
    {
      line("3 FPGA->CPU send       %u round trips, last Pong(%u)", m->rounds, m->last);
      // 4. FPGA -> CPU fast_send: FpgaCaller calls CpuPong and waits each time.
      fpga_caller_.send(new Go(3, 1), this);
      return;
    }
    line("4 FPGA->CPU fast_send  %u round trips, last Pong(%u)", m->rounds, m->last);
    done_();
  }

  actors::ActorRef fpga_pong_;
  actors::ActorRef fpga_caller_;
  std::function<void(const char *)> report_;
  std::function<void()> done_;
  int dones_ = 0;
};

} // namespace cpu_side
