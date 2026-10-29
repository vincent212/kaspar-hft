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
 *   actor   = state plus handlers, living in a process
 *   process = one circuit block, running forever (an hls::task on the chip)
 *   send    = one write into a FIFO that has one writer and one reader
 *   port    = one link end of a process: the FIFO it reads and the FIFO it
 *             writes, numbered 0, 1, ... in the order the top plugs them in
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
 * Every connection is fixed when the design is built; nothing stores a
 * pointer, because HLS cannot turn a pointer kept in an object into wires
 * (HLS 214-134). Instead:
 *
 *   - an ActorRef is a port number and the target's id. The top decides what
 *     each port is plugged into, so an actor still does not know where its
 *     peers are;
 *   - send() and reply() put the envelope and its port in the actor's outbox
 *     (at most kOutbox per handler). When the handler returns, the process
 *     writes each one to the FIFO of that port, a multiplexer in hardware.
 *     reply() uses the port the message came in on;
 *   - each actor type is one object in the design, kfpga::instance<T>(), held
 *     by exactly one process (a Group). That is how GroupRef<T> finds the
 *     target of a fast_send without a pointer.
 *
 * Several actors can share one process, a Group: one handler at a time, send
 * between members through the group's own queue, and fast_send between
 * members as an inline call. Group::bind checks at build time that the target
 * of a GroupRef is in the group, so a fast_send to another process does not
 * compile.
 *
 * Kept to C++14, the dialect Vitis HLS accepts. Built with -DKFPGA_HLS, a FIFO
 * is an hls::stream; otherwise it is a Ring (ring.hpp), and SoftCard
 * (soft_card.hpp) runs the processes in software.
 */

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "kfpga/envelope.hpp"

#ifdef KFPGA_HLS
#include <hls_stream.h>
#else
#include "kfpga/ring.hpp"
#endif

namespace kfpga {

#ifdef KFPGA_HLS
using Fifo = hls::stream<Envelope>;
#else
using Fifo = Ring;
#endif

constexpr int kOutbox = 4;               // sends + replies per handler, at most
constexpr uint8_t kOwnQueue = 0xff;      // the port number of a group's own queue
constexpr int kOwnQueueDepth = 64;       // size it for the largest burst between members

template <class... A> class Group;
template <class T> class GroupRef;

// The one object of actor type T in the design. Whichever group holds T is
// the only process that touches it.
template <class T>
T &instance()
{
  static T t;
  return t;
}

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

// One link end of a process: the FIFO it reads and the FIFO it writes.
struct Port
{
  Fifo &in;
  Fifo &out;
};

// A reference to another actor: the port its messages leave by, and its id.
// The port picks the FIFO, so the process; the id, in the envelope header,
// picks the member within it (and names the actor at the CPU end of a link).
class ActorRef
{
  uint8_t port_ = 0;
  ActorId id_ = 0;

public:
  ActorRef() = default;
  ActorRef(uint8_t port, ActorId id) : port_(port), id_(id) {}

  // Into the sender's outbox; written to the port's FIFO when the handler
  // returns. A full FIFO stalls the process, as a full hardware FIFO does.
  template <class M, class S>
  void send(const M &m, const S *from) const
  {
    const_cast<S *>(from)->kfpga_emit(encode(m, S::kId, id_), port_);
  }

  uint8_t port() const { return port_; }
  ActorId id() const { return id_; }
};

template <class Self>
class Actor
{
  template <class... A> friend class Group;
  template <class T> friend class GroupRef;

  Envelope out_[kOutbox];        // this handler's sends, in order
  uint8_t out_port_[kOutbox] = {};
  uint8_t n_out_ = 0;
  uint8_t in_port_ = 0;          // the port of the message being handled
  ActorId reply_id_ = 0;         // its sender
  bool in_fast_send_ = false;    // serving a fast_send: reply() goes to the caller
  bool replied_ = false;
  Envelope fast_reply_;

public:
  // Used by ActorRef::send. Overflow is a design error: raise kOutbox.
  void kfpga_emit(const Envelope &e, uint8_t port)
  {
    assert(n_out_ < kOutbox && "more sends in one handler than kOutbox");
    if (n_out_ < kOutbox)
    {
      out_[n_out_] = e;
      out_port_[n_out_] = port;
      ++n_out_;
    }
  }

protected:
  // Answer the message being handled: on the port it came in on, or, for a
  // fast_send, straight back to the caller.
  template <class M>
  void reply(const M &m)
  {
    const Envelope e = encode(m, Self::kId, reply_id_);
    if (in_fast_send_)
    {
      fast_reply_ = e;
      replied_ = true;
      return;
    }
    kfpga_emit(e, in_port_);
  }
};

// A reference for fast_send, to the actor T in the caller's own group. Only a
// group holding T can bind it (Group::bind), so a fast_send to an actor in
// another process does not compile.
template <class T>
class GroupRef
{
  template <class... A> friend class Group;
  bool bound_ = false;

public:
  // Runs T's handler now, inline: no FIFO, only the handler's own cycles.
  // Returns true and fills `out` if the handler replied with an R.
  template <class M, class S, class R>
  bool fast_send(const M &m, const S * /*from*/, R &out) const
  {
    assert(bound_ && "GroupRef not bound by its group");
    T &t = instance<T>();
    t.reply_id_ = S::kId;
    t.in_fast_send_ = true;
    t.replied_ = false;
    t.kfpga_dispatch(encode(m, S::kId, T::kId));
    t.in_fast_send_ = false;
    if (!t.replied_ || t.fast_reply_.msg != R::id)
      return false;
    out = decode<R>(t.fast_reply_);
    return true;
  }

  ActorId id() const { return T::kId; }
};

#ifndef KFPGA_HLS
// Two FIFOs between a client (the actor holding the reference) and a server.
// Software wiring; on the chip the top declares the two streams itself.
struct Link
{
  Fifo fwd;    // client -> server
  Fifo back;   // server -> client

  explicit Link(size_t depth = 16) : fwd(depth), back(depth) {}

  Port server() { return Port{fwd, back}; }   // the server's port on this link
  Port client() { return Port{back, fwd}; }   // the client's port on this link
};
#endif

namespace detail {
template <class T, class... A> struct contains;
template <class T> struct contains<T> { static constexpr bool value = false; };
template <class T, class H, class... R> struct contains<T, H, R...>
{
  static constexpr bool value = std::is_same<T, H>::value || contains<T, R...>::value;
};

template <class... A> struct distinct;
template <> struct distinct<> { static constexpr bool value = true; };
template <class H, class... R> struct distinct<H, R...>
{
  static constexpr bool value = !contains<H, R...>::value && distinct<R...>::value;
};
// A group's own queue, for send between members. A process of one actor has
// none: HLS rejects a stream that nothing writes.
template <bool Has> struct OwnQueue;
template <> struct OwnQueue<false>
{
  bool read_nb(Envelope &) { return false; }
  bool write_nb(const Envelope &) { return false; }
};
template <> struct OwnQueue<true>
{
#ifdef KFPGA_HLS
  hls::stream<Envelope, kOwnQueueDepth> q;
#else
  Ring q{kOwnQueueDepth};
#endif
  bool read_nb(Envelope &e) { return q.read_nb(e); }
  bool write_nb(const Envelope &e) { return q.write_nb(e); }
};
} // namespace detail

// Several actors as one process. Like a CPU Group:
//   - one handler at a time, run to completion;
//   - send between members goes through the group's own queue and is handled
//     on a later step;
//   - fast_send between members is an inline call;
//   - outside messages arrive on the ports.
// Each step takes one message, the group's own queue first, then the ports in
// round-robin order, hands it to the member named by the envelope's dst, and
// writes out what the handler sent. Order is kept per sender; across senders
// it follows the round-robin.
template <class... A>
class Group
{
  static_assert(detail::distinct<A...>::value, "an actor type appears twice in the group");

  detail::OwnQueue<(sizeof...(A) > 1)> own_;
  uint8_t next_ = 0;
  uint32_t unhandled_ = 0;

  template <class M>
  bool deliver_to(const Envelope &e, uint8_t port)
  {
#pragma HLS INLINE
    if (M::kId != e.dst)
      return false;
    M &m = instance<M>();
    m.in_port_ = port;
    m.reply_id_ = e.src;
    if (!m.kfpga_dispatch(e))
      ++unhandled_;
    return true;
  }

  template <int N>
  void route(const Envelope &e, uint8_t port, Port (&p)[N])
  {
#pragma HLS INLINE
    if (port == kOwnQueue)
    {
      const bool ok = own_.write_nb(e);   // a process cannot wait on its own queue
      assert(ok && "group queue full");
      (void)ok;
      return;
    }
    for (int k = 0; k < N; ++k)
    {
#pragma HLS UNROLL
      if (port == k)
        p[k].out.write(e);
    }
  }

  template <class M, int N>
  int flush(Port (&p)[N])
  {
#pragma HLS INLINE
    M &m = instance<M>();
    for (int j = 0; j < kOutbox; ++j)
    {
#pragma HLS UNROLL
      if (j < m.n_out_)
        route(m.out_[j], m.out_port_[j], p);
    }
    m.n_out_ = 0;
    return 0;
  }

public:
#ifndef KFPGA_HLS
  // A fresh design: every member back to its initial state.
  Group()
  {
    int unused[] = {0, (instance<A>() = A(), 0)...};
    (void)unused;
  }
#endif

  // A member, for wiring and for reading its state.
  template <class T>
  static T &member()
  {
    static_assert(detail::contains<T, A...>::value, "not a member of this group");
    return instance<T>();
  }

  // A member's reference to another member, for send.
  template <class T>
  static ActorRef ref()
  {
    static_assert(detail::contains<T, A...>::value, "not a member of this group");
    static_assert(sizeof...(A) > 1, "a process of one actor has no group queue");
    return ActorRef(kOwnQueue, T::kId);
  }

  // Bind a member's GroupRef, for fast_send. Fails to compile if T is not in
  // this group.
  template <class T>
  static void bind(GroupRef<T> &r)
  {
    static_assert(detail::contains<T, A...>::value,
                  "fast_send only within a group: the target is not a member of this group");
    r.bound_ = true;
  }

  // One message: read, handle, write out what the handler sent. Returns true
  // if a message was handled. Inlined into the process, so that each port
  // reference resolves to a fixed stream.
  template <int N>
  bool step(Port (&p)[N])
  {
#pragma HLS INLINE
    Envelope e;
    bool got = own_.read_nb(e);
    uint8_t from = kOwnQueue;
    // Round-robin from next_. Two passes with fixed indices, so each port is
    // a fixed FIFO in hardware, never one picked by a variable.
    for (int pass = 0; pass < 2; ++pass)
    {
      for (int k = 0; k < N; ++k)
      {
#pragma HLS UNROLL
        if (!got && (pass == 0 ? k >= next_ : k < next_) && p[k].in.read_nb(e))
        {
          got = true;
          from = k;
          next_ = (k + 1) % N;
        }
      }
    }
    if (!got)
      return false;

    bool found = false;
    int delivered[] = {0, (found = found || deliver_to<A>(e, from), 0)...};
    (void)delivered;
    if (!found)
      ++unhandled_;

    int flushed[] = {0, flush<A>(p)...};
    (void)flushed;
    return true;
  }

  // Messages with no member or no handler for them.
  uint32_t unhandled() const { return unhandled_; }
};

// One actor as a process: a group of one.
template <class A>
using Process = Group<A>;

} // namespace kfpga
