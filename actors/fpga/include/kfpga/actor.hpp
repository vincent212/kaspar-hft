#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/*
 * FPGA actors.
 *
 *   actor   = a process: one circuit block with its own state, running forever
 *   send    = one write into a FIFO that has one writer and one reader
 *   mailbox = the process's input FIFOs, read round-robin
 *
 * An actor derives from kfpga::Actor<Self> (no virtual functions), lists its
 * handlers in KFPGA_HANDLERS, sends through the ActorRefs it holds, and
 * answers the message it is handling with reply():
 *
 *   struct PongActor : kfpga::Actor<PongActor>
 *   {
 *     static constexpr kfpga::ActorId kId = 2;
 *     uint32_t pings = 0;
 *     KFPGA_HANDLERS(KFPGA_ON(Ping, on_ping))
 *     void on_ping(const Ping &m) { ++pings; Pong r; r.count = m.count; reply(r); }
 *   };
 *
 * Two actors that talk are joined by a Link, a pair of FIFOs: one from the
 * actor holding the reference to the other, one back. reply() writes to the
 * back FIFO of the message being handled, so no sender ids or routing are
 * needed on the chip. All wiring is fixed when the design is built.
 *
 * Kept to C++14, the dialect Vitis HLS accepts. In software a FIFO is a Ring
 * (ring.hpp). Not yet synthesized.
 */

#include <array>
#include <cstddef>
#include <cstdint>

#include "kfpga/envelope.hpp"
#include "kfpga/ring.hpp"

namespace kfpga {

using Fifo = Ring;

template <class A, int N> class Process;

// The handler table: a switch on the message id, a multiplexer in hardware.
#define KFPGA_HANDLERS(...)                                                     \
  bool kfpga_dispatch(const ::kfpga::Envelope &kfpga_e)                         \
  {                                                                             \
    switch (kfpga_e.msg)                                                        \
    {                                                                           \
      __VA_ARGS__                                                               \
    default:                                                                    \
      return false;                                                             \
    }                                                                           \
  }

#define KFPGA_ON(M, handler)                                                    \
  case M::id:                                                                   \
    handler(::kfpga::decode<M>(kfpga_e));                                       \
    return true;

// A reference to another actor: the FIFO toward it and its id. The id rides
// in the envelope header for the CPU end of a link; on the chip the FIFO alone
// decides where a message goes.
class ActorRef
{
  Fifo *to_ = nullptr;
  ActorId id_ = 0;

public:
  ActorRef() = default;
  ActorRef(Fifo &to, ActorId id) : to_(&to), id_(id) {}

  // One FIFO write. Stalls only if the FIFO is full.
  template <class M, class S>
  void send(const M &m, const S * /*from: its type gives the source id*/) const
  {
    to_->write(encode(m, S::kId, id_));
  }

  ActorId id() const { return id_; }
};

template <class Self>
class Actor
{
  template <class A, int N> friend class Process;

  Fifo *reply_to_ = nullptr;   // the back FIFO of the message being handled
  ActorId reply_id_ = 0;

protected:
  // Answer the message being handled, on the link it arrived on.
  template <class M>
  void reply(const M &m)
  {
    reply_to_->write(encode(m, Self::kId, reply_id_));
  }
};

// One input of a process: a FIFO it reads, and the FIFO its replies go to.
struct Input
{
  Fifo *from;
  Fifo *reply;
};

// Two FIFOs between a client (the actor holding the reference) and a server.
struct Link
{
  Fifo fwd;    // client -> server
  Fifo back;   // server -> client

  explicit Link(size_t depth = 16) : fwd(depth), back(depth) {}

  ActorRef ref(ActorId server) { return ActorRef(fwd, server); }               // client's ref to server
  ActorRef back_ref(ActorId client) { return ActorRef(back, client); }        // server's ref to client
  Input server_input() { return Input{&fwd, &back}; }
  Input client_input() { return Input{&back, &fwd}; }
};

// An actor as a process. Each step takes at most one message, from its inputs
// in round-robin order, and runs its handler to completion.
template <class A, int N>
class Process
{
  A &actor_;
  std::array<Input, N> in_;
  int next_ = 0;
  uint32_t unhandled_ = 0;

public:
  Process(A &actor, const std::array<Input, N> &in) : actor_(actor), in_(in) {}

  // Returns true if a message was handled.
  bool step()
  {
    for (int k = 0; k < N; ++k)
    {
      const int i = (next_ + k) % N;
      Envelope e;
      if (in_[i].from->read_nb(e))
      {
        next_ = (i + 1) % N;
        actor_.reply_to_ = in_[i].reply;
        actor_.reply_id_ = e.src;
        if (!actor_.kfpga_dispatch(e))
          ++unhandled_;
        return true;
      }
    }
    return false;
  }

  // Messages with no handler in this actor.
  uint32_t unhandled() const { return unhandled_; }
};

} // namespace kfpga
