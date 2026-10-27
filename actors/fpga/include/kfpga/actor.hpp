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
 * back FIFO of the message being handled, so no routing is needed on the chip.
 *
 * Several actors can share one process, a Group, declared in the top: one
 * handler at a time, send between members through the group's own queue, and
 * fast_send between members as an inline call. fast_send goes through a
 * GroupRef, which only the group can bind and only to one of its members, so
 * a fast_send to another process does not compile. All wiring is fixed when
 * the design is built.
 *
 * Kept to C++14, the dialect Vitis HLS accepts. In software a FIFO is a Ring
 * (ring.hpp). Not yet synthesized.
 */

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>

#include "kfpga/envelope.hpp"
#include "kfpga/ring.hpp"

namespace kfpga {

using Fifo = Ring;

template <int N, class... A> class Group;
template <class T> class GroupRef;

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

// A reference to another actor: the FIFO toward it and its id. The FIFO
// decides which process gets the message; the id, in the envelope header,
// picks the member within it (and names the actor at the CPU end of a link).
class ActorRef
{
  Fifo *to_ = nullptr;
  ActorId id_ = 0;
  bool own_queue_ = false;   // the sender's own group queue

public:
  ActorRef() = default;
  ActorRef(Fifo &to, ActorId id, bool own_queue = false)
    : to_(&to), id_(id), own_queue_(own_queue) {}

  // One FIFO write. Stalls only if the FIFO is full; a process cannot wait on
  // its own queue, so a full group queue is a design error (size it for the
  // largest burst of sends between members).
  template <class M, class S>
  void send(const M &m, const S * /*from: its type gives the source id*/) const
  {
    const Envelope e = encode(m, S::kId, id_);
    if (own_queue_)
    {
      const bool ok = to_->write_nb(e);
      assert(ok && "group queue full");
      (void)ok;
      return;
    }
    to_->write(e);
  }

  ActorId id() const { return id_; }
};

template <class Self>
class Actor
{
  template <int N, class... A> friend class Group;
  template <class T> friend class GroupRef;

  Fifo *reply_to_ = nullptr;     // the back FIFO of the message being handled
  ActorId reply_id_ = 0;
  Envelope *captured_ = nullptr;   // set while serving a fast_send
  bool replied_ = false;

protected:
  // Answer the message being handled: on the link it arrived on, or, for a
  // fast_send, straight back to the caller.
  template <class M>
  void reply(const M &m)
  {
    const Envelope e = encode(m, Self::kId, reply_id_);
    if (captured_)
    {
      *captured_ = e;
      replied_ = true;
      return;
    }
    reply_to_->write(e);
  }
};

// A reference for fast_send, to an actor in the caller's own group. Only the
// group can bind it (Group::bind), and only to one of its own members, so a
// fast_send to an actor in another process does not compile.
template <class T>
class GroupRef
{
  template <int N, class... A> friend class Group;
  T *target_ = nullptr;

public:
  // Runs T's handler now, inline: no FIFO, only the handler's own cycles.
  // Returns true and fills `out` if the handler replied with an R.
  template <class M, class S, class R>
  bool fast_send(const M &m, const S * /*from*/, R &out) const
  {
    assert(target_ && "GroupRef not bound by its group");
    Envelope rep;
    target_->reply_id_ = S::kId;
    target_->captured_ = &rep;
    target_->replied_ = false;
    target_->kfpga_dispatch(encode(m, S::kId, T::kId));
    target_->captured_ = nullptr;
    if (!target_->replied_ || rep.msg != R::id)
      return false;
    out = decode<R>(rep);
    return true;
  }

  ActorId id() const { return T::kId; }
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

namespace detail {
template <class T, class... A> struct contains;
template <class T> struct contains<T> { static constexpr bool value = false; };
template <class T, class H, class... R> struct contains<T, H, R...>
{
  static constexpr bool value = std::is_same<T, H>::value || contains<T, R...>::value;
};
} // namespace detail

// Several actors as one process, declared in the top. Like a CPU Group:
//   - one handler at a time, run to completion;
//   - send between members goes through the group's own queue and is handled
//     on a later step;
//   - fast_send between members is an inline call;
//   - outside messages arrive on the N input FIFOs.
// Each step takes one message, the group's own queue first, then the inputs in
// round-robin order, and hands it to the member named by the envelope's dst.
// Order is kept per sender; across senders it follows the round-robin.
template <int N, class... A>
class Group
{
  std::tuple<A *...> members_;
  std::array<Input, N> in_;
  Fifo own_;
  int next_ = 0;
  uint32_t unhandled_ = 0;

  template <class M>
  bool deliver_to(M &m, const Envelope &e, Fifo *reply)
  {
    if (M::kId != e.dst)
      return false;
    m.reply_to_ = reply;
    m.reply_id_ = e.src;
    if (!m.kfpga_dispatch(e))
      ++unhandled_;
    return true;
  }

  void deliver(const Envelope &e, Fifo *reply)
  {
    bool found = false;
    int unused[] = {0, (found = found || deliver_to(*std::get<A *>(members_), e, reply), 0)...};
    (void)unused;
    if (!found)
      ++unhandled_;
  }

public:
  Group(const std::array<Input, N> &in, A &...members, size_t own_depth = 64)
    : members_(&members...), in_(in), own_(own_depth) {}

  // A member's reference to another member, for send.
  template <class T>
  ActorRef ref()
  {
    static_assert(detail::contains<T, A...>::value, "not a member of this group");
    return ActorRef(own_, T::kId, true);
  }

  // Bind a member's GroupRef, for fast_send. Fails to compile if T is not in
  // this group.
  template <class T>
  void bind(GroupRef<T> &r)
  {
    static_assert(detail::contains<T, A...>::value,
                  "fast_send only within a group: the target is not a member of this group");
    r.target_ = std::get<T *>(members_);
  }

  // Returns true if a message was handled.
  bool step()
  {
    Envelope e;
    if (own_.read_nb(e))
    {
      deliver(e, &own_);
      return true;
    }
    for (int k = 0; k < N; ++k)
    {
      const int i = (next_ + k) % N;
      if (in_[i].from->read_nb(e))
      {
        next_ = (i + 1) % N;
        deliver(e, in_[i].reply);
        return true;
      }
    }
    return false;
  }

  // Messages with no member or no handler for them.
  uint32_t unhandled() const { return unhandled_; }
};

// One actor as a process: a group of one.
template <class A, int N>
class Process : public Group<N, A>
{
public:
  Process(A &actor, const std::array<Input, N> &in) : Group<N, A>(in, actor, 2) {}
};

} // namespace kfpga
