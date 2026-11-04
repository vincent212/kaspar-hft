#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/*
 * FpgaFeed, the CPU end of the MDP3 handler: an ordinary Kaspar actor on an
 * FpgaLanes mailbox (see host/kfpga/cpu_link.hpp). At start it loads the card's
 * filter table, one WantContract a contract, each answered by a WantAck. Then it
 * takes the card's lines: each is copied to a buffer, and at the line marked End
 * the whole packet is applied to the books and handed to the strategy, in one
 * call on this thread. Nothing sleeps and nothing is woken: the mailbox spins.
 *
 * The strategy here is the PacketSink interface. Turning a packet into the
 * messages light22 takes today is the next piece, on this interface.
 */

#include <array>
#include <cstring>
#include <functional>
#include <vector>

#include "actors/Actor.hpp"
#include "actors/ActorRef.hpp"
#include "actors/msg/Start.hpp"
#include "kfpga/cpu_link.hpp"
#include "mdp3fpga/books.hpp"

namespace mdp3fpga {

// Who gets a packet. `events` are the packet's events, in order, for every
// contract; `books` already has them applied. `gap` stays set after a SeqGap.
struct PacketSink
{
  virtual ~PacketSink() = default;
  virtual void on_packet(const Books &books, const Event *events, size_t n, bool gap) = 0;
};

namespace cpu {

template <int N, bool End>
using Line = kfpga::CpuMsg<EventLine<N, End>>;
using Gap = kfpga::CpuMsg<SeqGap>;
using Want = kfpga::CpuMsg<WantContract>;
using Ack = kfpga::CpuMsg<WantAck>;

inline void add_messages(kfpga::Codec &codec)
{
  codec.add<EventLine<1, false>, EventLine<2, false>, EventLine<3, false>, EventLine<1, true>, EventLine<2, true>,
            EventLine<3, true>, SeqGap, WantContract, WantAck>();
}

} // namespace cpu

class FpgaFeed : public actors::Actor
{
  // A packet holds about 44 entries at most; the table of the design below.
  static constexpr size_t kMaxEvents = 128;

  std::vector<int32_t> contracts_;
  Books books_;
  PacketSink &sink_;
  std::function<void()> on_ready_;
  actors::ActorRef filter_;
  std::array<Event, kMaxEvents> events_;
  size_t n_ = 0;
  uint32_t acks_ = 0;
  bool gap_ = false;

  template <int N, bool End>
  void on_line(const cpu::Line<N, End> *m)
  {
    const uint64_t lo[3] = {m->lo0, m->lo1, m->lo2}, hi[3] = {m->hi0, m->hi1, m->hi2};
    for (int i = 0; i < N; ++i)
    {
      if (n_ == kMaxEvents)
      {
        ++overflows;
        break;
      }
      events_[n_++] = Event{lo[i], hi[i]};
    }
    ++lines;
    if (End)
    {
      for (size_t i = 0; i < n_; ++i)
        books_.apply(events_[i]);
      sink_.on_packet(books_, events_.data(), n_, gap_);
      n_ = 0;
    }
  }

  using L1 = cpu::Line<1, false>;
  using L2 = cpu::Line<2, false>;
  using L3 = cpu::Line<3, false>;
  using E1 = cpu::Line<1, true>;
  using E2 = cpu::Line<2, true>;
  using E3 = cpu::Line<3, true>;
  void on_l1(const L1 *m) { on_line(m); }
  void on_l2(const L2 *m) { on_line(m); }
  void on_l3(const L3 *m) { on_line(m); }
  void on_e1(const E1 *m) { on_line(m); }
  void on_e2(const E2 *m) { on_line(m); }
  void on_e3(const E3 *m) { on_line(m); }

  void on_gap(const cpu::Gap *)
  {
    gap_ = true;
    ++gaps;
  }

  void on_start(const actors::msg::Start *)
  {
    for (uint32_t i = 0; i < contracts_.size(); ++i)
      filter_.send(new cpu::Want(i, contracts_[i]), this);
  }

  void on_ack(const cpu::Ack *)
  {
    if (++acks_ == contracts_.size() && on_ready_)
      on_ready_();
  }

public:
  uint64_t lines = 0, gaps = 0, overflows = 0;

  // `on_ready` runs on this thread once the card has acked the whole table.
  FpgaFeed(std::vector<int32_t> contracts, PacketSink &sink, std::function<void()> on_ready)
    : contracts_(std::move(contracts)), books_(contracts_.size()), sink_(sink), on_ready_(std::move(on_ready))
  {
    std::strncpy(name, "FpgaFeed", sizeof(name) - 1);
    set_mailbox(MailboxKind::FpgaLanes);
    MESSAGE_HANDLER(actors::msg::Start, on_start);
    MESSAGE_HANDLER(cpu::Ack, on_ack);
    MESSAGE_HANDLER(cpu::Gap, on_gap);
    MESSAGE_HANDLER(L1, on_l1);
    MESSAGE_HANDLER(L2, on_l2);
    MESSAGE_HANDLER(L3, on_l3);
    MESSAGE_HANDLER(E1, on_e1);
    MESSAGE_HANDLER(E2, on_e2);
    MESSAGE_HANDLER(E3, on_e3);
  }

  void set_filter(actors::ActorRef filter) { filter_ = filter; }
};

} // namespace mdp3fpga
