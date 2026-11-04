#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/*
 * The CPU end of a link between a CPU actor and an FPGA actor. No routing
 * thread: every crossing is written by the thread that has the message and
 * read by the thread that will handle it.
 *
 *   CPU -> FPGA  the CPU actor's own thread encodes the message into a 64-byte
 *                envelope on its stack and writes it into its window to the
 *                card (on hardware: one write-combined store and a fence).
 *   FPGA -> CPU  the card writes into a lane in host memory owned by the CPU
 *                actor's thread; its FpgaLanes mailbox polls the lane, decodes
 *                onto the stack and runs the handler.
 *
 * A link has one window and one lane, each with one writer and one reader. The
 * FPGA actor's stand-in on the CPU is the sender of every message from the
 * lane, so reply() goes back out through the window, still on the CPU actor's
 * thread.
 *
 * send only. fast_send across the link would be a send plus a blocking wait;
 * a request and its reply are two messages.
 */

#include <atomic>
#include <cassert>
#include <cstdio>
#include <vector>

#include "actors/Actor.hpp"
#include "actors/ActorRef.hpp"
#include "actors/FpgaLanes.hpp"
#include "kfpga/actor.hpp"

namespace kfpga {

// The CPU class for a message declared once for both sides: an ordinary Kaspar
// message (Message_N<id>) carrying the shared fields.
//   namespace cpu { using Ping = kfpga::CpuMsg<pingpong::Ping>; }
//   ref.send(new cpu::Ping(7), this);
template <class F>
struct CpuMsg : actors::Message_N<F::id>, F
{
  static constexpr int id = F::id;
  // No arguments, or one per field, in KFPGA_FIELDS order.
  template <class... A>
  explicit CpuMsg(A... fields)
  {
    if constexpr (sizeof...(A) > 0)
      this->kfpga_fields([&](auto &...v) {
        static_assert(sizeof...(v) == sizeof...(A), "one argument per field");
        ((v = static_cast<std::decay_t<decltype(v)>>(fields)), ...);
      });
  }
};

// Converts between CPU messages and envelopes, by message id.
class Codec
{
  struct Entry
  {
    void (*encode)(const actors::Message *, Envelope &) = nullptr;
    void (*deliver)(const Envelope &, actors::Actor *, actors::Actor *) = nullptr;
  };
  std::vector<Entry> table_ = std::vector<Entry>(actors::detail::kFirstDynamicMessageId);

  template <class F>
  static void encode_as(const actors::Message *m, Envelope &e)
  {
    put_fields(static_cast<const F &>(*static_cast<const CpuMsg<F> *>(m)), e);
  }

  // Decode onto the stack and run the handler on this (the target's) thread.
  template <class F>
  static void deliver_as(const Envelope &e, actors::Actor *target, actors::Actor *peer)
  {
    CpuMsg<F> m;
    get_fields(e, static_cast<F &>(m));
    target->deliver_external(&m, peer);
  }

public:
  template <class... F>
  void add()
  {
    int unused[] = {0, (table_[F::id] = Entry{&encode_as<F>, &deliver_as<F>}, 0)...};
    (void)unused;
  }

  bool encode(const actors::Message *m, Envelope &e) const
  {
    const int id = m->get_message_id();
    if (id < 0 || id >= static_cast<int>(table_.size()) || !table_[id].encode)
      return false;
    e.msg = static_cast<MsgId>(id);
    table_[id].encode(m, e);
    return true;
  }

  bool deliver(const Envelope &e, actors::Actor *target, actors::Actor *peer) const
  {
    if (e.msg >= table_.size() || !table_[e.msg].deliver)
      return false;
    table_[e.msg].deliver(e, target, peer);
    return true;
  }
};

class CpuEnd : public actors::ExternalLane
{
  // The FPGA actor's stand-in on the CPU. Never runs a thread of its own:
  // its send() runs on the thread of the CPU actor that calls it.
  class Peer : public actors::Actor
  {
    CpuEnd &end_;

  public:
    Peer(CpuEnd &end, ActorId fpga_id) : end_(end)
    {
      std::snprintf(name, sizeof(name), "fpga:%u", fpga_id);
    }
    void send(const actors::Message *m, actors::Actor *) noexcept override
    {
      end_.write(m);
      delete m;
    }
  };

  Fifo &window_;   // CPU -> card
  Fifo &lane_;     // card -> CPU
  actors::Actor *cpu_;
  ActorId cpu_id_, fpga_id_;
  const Codec &codec_;
  Peer peer_;
  std::atomic<uint32_t> errors_{0};

  void write(const actors::Message *m) noexcept
  {
    Envelope e;
    if (!codec_.encode(m, e))
    {
      errors_.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    e.src = cpu_id_;
    e.dst = fpga_id_;
    window_.write(e);
  }

  CpuEnd(Fifo &window, Fifo &lane, actors::Actor *cpu, ActorId cpu_id, ActorId fpga_id,
         const Codec &codec)
    : window_(window), lane_(lane), cpu_(cpu), cpu_id_(cpu_id), fpga_id_(fpga_id),
      codec_(codec), peer_(*this, fpga_id)
  {
    cpu_->add_lane(this);
  }

public:
  // The CPU actor holds the reference: it sends on link.fwd, the card answers
  // on link.back.
  static CpuEnd *client(Link &link, actors::Actor *cpu, ActorId cpu_id, ActorId fpga_id,
                        const Codec &codec)
  {
    return new CpuEnd(link.fwd, link.back, cpu, cpu_id, fpga_id, codec);
  }

  // The FPGA actor holds the reference: the card sends on link.fwd, the CPU
  // actor answers on link.back.
  static CpuEnd *server(Link &link, actors::Actor *cpu, ActorId cpu_id, ActorId fpga_id,
                        const Codec &codec)
  {
    return new CpuEnd(link.back, link.fwd, cpu, cpu_id, fpga_id, codec);
  }

  // The CPU actor's reference to the FPGA actor.
  actors::ActorRef ref() { return actors::ActorRef(actors::ExternalActorRef(&peer_)); }

  // Runs on the CPU actor's thread, from its FpgaLanes mailbox.
  bool poll() noexcept override
  {
    Envelope e;
    if (!lane_.read_nb(e))
      return false;
    if (!codec_.deliver(e, cpu_, &peer_))
      errors_.fetch_add(1, std::memory_order_relaxed);
    return true;
  }

  // Messages with no codec entry, in either direction.
  uint32_t errors() const { return errors_.load(std::memory_order_relaxed); }
};

} // namespace kfpga
