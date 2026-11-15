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
 * filter table, one WantSecurity a security, each answered by a WantAck. Then it
 * takes the card's lines, and builds the books as the packet arrives: the events of a
 * full line are applied the moment the line comes in, while the rest of the packet is
 * still on the wire, and the End line brings the last 0 to 2. At the End line the
 * strategy gets the packet, books and events, in one call on this thread, so it never
 * sees a book halfway through a packet. Only this actor touches the books in between.
 * After a packet's last byte, then, the work left here is at most two events, however
 * long the packet. A packet with no events only advances the sequence check, which is here:
 * every packet's End line carries its number. Nothing sleeps and nothing is woken:
 * the mailbox spins.
 *
 * The strategy here is the PacketSink interface. Turning a packet into the
 * messages light22 takes today is the next piece, on this interface.
 */

#include <array>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

#include "actors/Actor.hpp"
#include "actors/ActorRef.hpp"
#include "actors/msg/Start.hpp"
#include "kfpga/cpu_link.hpp"
#include "mdp3fpga/books.hpp"

namespace mdp3fpga {

// Who gets a packet. `events` are the packet's events, in order, for every
// security; `books` already has them applied. `gap` stays set from the first
// sequence gap.
struct PacketSink
{
  virtual ~PacketSink() = default;
  virtual void on_packet(const Books &books, const Event *events, size_t n, uint32_t seq, bool gap) = 0;
};

namespace cpu {

using Line = kfpga::CpuMsg<EventLine>;
template <int N>
using End = kfpga::CpuMsg<EndLine<N>>;
using Want = kfpga::CpuMsg<WantSecurity>;
using Ack = kfpga::CpuMsg<WantAck>;

inline void add_messages(kfpga::Codec &codec)
{
  codec.add<EventLine, EndLine<0>, EndLine<1>, EndLine<2>, WantSecurity, WantAck>();
}

} // namespace cpu

class FpgaFeed : public actors::Actor
{
  // A packet holds about 44 entries at most; the table of the design below.
  static constexpr size_t kMaxEvents = 128;
  // How far behind the expected sequence number a packet counts as late rather
  // than as the start of a restarted channel.
  static constexpr uint32_t kLateWindow = 1024;

  std::vector<int32_t> securities_;
  Books books_;
  PacketSink &sink_;
  std::function<void()> on_ready_;
  actors::ActorRef filter_;
  std::array<Event, kMaxEvents> events_;
  size_t n_ = 0;
  bool applied_ = false;   // this packet's full lines are already in the books
  uint32_t acks_ = 0;
  bool gap_ = false, seen_ = false;
  uint32_t next_seq_ = 0;

  void hold(uint64_t lo, uint64_t hi)
  {
    if (n_ == kMaxEvents)
    {
      ++overflows;
      return;
    }
    events_[n_++] = Event{lo, hi};
  }

  // A full line: apply its three events now, while the packet is still arriving, and
  // keep them for the strategy's call at the End line.
  void on_line(const cpu::Line *m)
  {
    ++lines;
    const Event e[3] = {{m->lo0, m->hi0}, {m->lo1, m->hi1}, {m->lo2, m->hi2}};
    for (const Event &ev : e)
    {
      books_.apply(ev);
      hold(ev.lo, ev.hi);
    }
    applied_ = true;
  }

  // The End line: the sequence check, the packet's last 0 to 2 events (`tail`), and
  // the strategy's call.
  //
  // Ahead of the expected number: a gap, latched. Behind it by more than kLateWindow:
  // the channel has restarted its numbering, so follow it without a gap. Behind it by
  // up to kLateWindow: a duplicate or a late packet, counted and ignored. Its End line
  // events are dropped; but if it had full lines, their events were applied as they
  // arrived, before its number was known, so the books may now be wrong, and the gap
  // flag is set. Most packets fit in their End line, so this is rare.
  void packet_end(uint32_t seq, const Event *tail, size_t k)
  {
    ++lines;
    if (seen_ && seq != next_seq_)
    {
      if (seq > next_seq_)
      {
        gap_ = true;
        ++gaps;
        missing += seq - next_seq_;
      }
      else if (next_seq_ - seq <= kLateWindow)
      {
        ++stale;
        if (applied_)
        {
          gap_ = true;
          ++stale_applied;
        }
        n_ = 0;
        applied_ = false;
        return;
      }
      else
        ++restarts;
    }
    seen_ = true;
    next_seq_ = seq + 1;
    for (size_t i = 0; i < k; ++i)
    {
      books_.apply(tail[i]);
      hold(tail[i].lo, tail[i].hi);
    }
    applied_ = false;
    if (n_ == 0)
      return;
    sink_.on_packet(books_, events_.data(), n_, seq, gap_);
    n_ = 0;
  }

  void on_end0(const cpu::End<0> *m) { packet_end(m->seq, nullptr, 0); }

  void on_end1(const cpu::End<1> *m)
  {
    const Event t[1] = {{m->lo0, m->hi0}};
    packet_end(m->seq, t, 1);
  }

  void on_end2(const cpu::End<2> *m)
  {
    const Event t[2] = {{m->lo0, m->hi0}, {m->lo1, m->hi1}};
    packet_end(m->seq, t, 2);
  }

  void on_start(const actors::msg::Start *)
  {
    for (uint32_t i = 0; i < securities_.size(); ++i)
      filter_.send(new cpu::Want(i, securities_[i]), this);
  }

  // Ready once every entry is acked as stored. An entry the card refused leaves the
  // feed not ready, and is counted in `refused`.
  void on_ack(const cpu::Ack *m)
  {
    if (!m->ok)
      ++refused;
    if (++acks_ == securities_.size() && refused == 0 && on_ready_)
      on_ready_();
  }

  static std::vector<int32_t> check_size(std::vector<int32_t> s)
  {
    if (s.size() > static_cast<size_t>(kSecurities))
      throw std::length_error("FpgaFeed: " + std::to_string(s.size()) + " securities, but the card's filter table holds " +
                              std::to_string(kSecurities));
    return s;
  }

public:
  uint64_t lines = 0, gaps = 0, missing = 0, overflows = 0, refused = 0;
  uint64_t stale = 0;      // duplicate or late packets, ignored
  uint64_t stale_applied = 0;   // of those, ones whose full lines were already applied: gap set
  uint64_t restarts = 0;   // the channel's numbering started again

  // `on_ready` runs on this thread once the card has acked the whole table.
  // Throws std::length_error if there are more securities than the card's filter
  // table holds (kSecurities): the card could not pass the rest.
  FpgaFeed(std::vector<int32_t> securities, PacketSink &sink, std::function<void()> on_ready)
    : securities_(check_size(std::move(securities))), books_(securities_.size()), sink_(sink),
      on_ready_(std::move(on_ready))
  {
    std::strncpy(name, "FpgaFeed", sizeof(name) - 1);
    set_mailbox(MailboxKind::FpgaLanes);
    MESSAGE_HANDLER(actors::msg::Start, on_start);
    MESSAGE_HANDLER(cpu::Ack, on_ack);
    MESSAGE_HANDLER(cpu::Line, on_line);
    using E0 = cpu::End<0>;
    using E1 = cpu::End<1>;
    using E2 = cpu::End<2>;
    MESSAGE_HANDLER(E0, on_end0);
    MESSAGE_HANDLER(E1, on_end1);
    MESSAGE_HANDLER(E2, on_end2);
  }

  void set_filter(actors::ActorRef filter) { filter_ = filter; }
};

} // namespace mdp3fpga
