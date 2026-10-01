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
 * An FPGA actor is a struct with an actor id, private state, and handlers:
 *
 *   struct PongActor {
 *     static constexpr kfpga::ActorId kId = 2;
 *     uint32_t pings = 0;
 *
 *     KFPGA_HANDLERS(KFPGA_ON(Ping, on_ping))
 *
 *     template <class Ctx>
 *     void on_ping(const Ping &m, Ctx &ctx) {
 *       ++pings;
 *       Pong r; r.count = m.count + 1000 * pings;
 *       ctx.reply(r);
 *     }
 *   };
 *
 * A handler talks through its Ctx:
 *   ctx.send(dst, msg)            msg to actor dst (on this FPGA or the CPU), written
 *                                 straight into the FIFO that leads to dst
 *   ctx.reply(msg)                answer the sender of the current message
 *   ctx.fast_send(b, msg, rep)    b is an actor object in this process: its
 *                                 handler runs now, inline; reply by value
 *   ctx.fast_send(dst, msg, rep)  dst is an actor id: an FPGA actor in another
 *                                 process, or a CPU actor through the host. The
 *                                 request goes straight into dst's fast_send FIFO
 *                                 from this actor; this actor's process waits on
 *                                 the reply FIFO from dst, then continues.
 *   ctx.sender()                  who sent the current message
 *
 * Handlers are templates on the Ctx type, because a Ctx knows at compile time
 * which process it belongs to: that is what lets every FIFO have one writer.
 *
 * Actors inside another actor's process (FusedPing's Pong, say) are listed with
 * KFPGA_INNER(member, ...); a message addressed to one of them, routed to that
 * process, is handled by it.
 *
 * Each actor runs as its own hardware process (actor_process), one message at a
 * time, serving its fast_send request FIFOs before anything else.
 */

#include <type_traits>

#include "actors_fpga/envelope.hpp"
#include "actors_fpga/links.hpp"
#include "actors_fpga/stream.hpp"

namespace kfpga {

// Messages an actor sends to itself: a small queue inside its own process (a FIFO
// cannot be written and read by the same process).
constexpr int kSelfDepth = 8;

struct SelfQueue
{
  Envelope q[kSelfDepth];
  uint8_t head = 0;
  uint8_t n = 0;

  bool push(const Envelope &e)
  {
    if (n == kSelfDepth)
      return false;
    q[(head + n) % kSelfDepth] = e;
    ++n;
    return true;
  }
  Envelope pop()
  {
    const Envelope e = q[head];
    head = static_cast<uint8_t>((head + 1) % kSelfDepth);
    --n;
    return e;
  }
};

// Per-process state the runtime keeps between steps.
template <int NE>
struct ActorPorts
{
  int last_fast = NE - 1;   // round-robin position over fast_send request FIFOs
  int last_msg = NE - 1;    // round-robin position over message FIFOs
  SelfQueue self;
};

// The handler's view of the runtime, for the actor process at endpoint K of a
// design with NE endpoints.
template <int NE, int K>
class Ctx
{
public:
  static constexpr int kHostEp = NE - 1;

  // from_ep: the endpoint the current message came from (K for the self queue).
  Ctx(Links<NE> &links, const DiscoveryTable &rt, SelfQueue &self_q, ActorId self, ActorId sender,
      int from_ep, bool fast)
    : L_(links), rt_(rt), selfq_(self_q), self_(self), sender_(sender), from_(from_ep),
      fast_(fast)
  {
  }

  ActorId self() const { return self_; }
  ActorId sender() const { return sender_; }

  template <class M>
  void send(ActorId dst, const M &m)
  {
    Envelope e;
    if (!pack(m, dst, self_, SEND, e))
    {
      error(make_error(ERR_PAYLOAD_FULL, self_, M::id, dst));
      return;
    }
    const int ep = endpoint<NE>(rt_, dst);
    if (ep == K)
      to_self(e);
    else if (ep < 0)
      error(make_error(ERR_NO_ROUTE, self_, M::id, dst));
    else
      write_to<NE, K>(L_.msg, ep, e);
  }

  // To a send(): a message to the sender, now, on the FIFO it came from.
  // To a fast_send(): held and returned to the waiting caller when the handler
  // returns, as on the CPU. Only the first reply to a fast_send counts.
  template <class M>
  void reply(const M &m)
  {
    if (fast_)
    {
      if (has_reply_)
        return;
      if (!pack(m, sender_, self_, FAST_REPLY, reply_))
      {
        error(make_error(ERR_PAYLOAD_FULL, self_, M::id, sender_));
        return;
      }
      has_reply_ = true;
      return;
    }
    Envelope e;
    if (!pack(m, sender_, self_, SEND, e))
    {
      error(make_error(ERR_PAYLOAD_FULL, self_, M::id, sender_));
      return;
    }
    if (from_ == K)
      to_self(e);
    else
      write_to<NE, K>(L_.msg, from_, e);
  }

  // fast_send to an actor object in the same process: its handler runs now,
  // inside this one. Returns true if it replied with a Rep.
  template <class B, class Req, class Rep,
            class = typename std::enable_if<std::is_class<B>::value>::type>
  bool fast_send(B &b, const Req &req, Rep &rep)
  {
    Envelope e;
    if (!pack(req, B::kId, self_, FAST, e))
    {
      error(make_error(ERR_PAYLOAD_FULL, self_, Req::id, B::kId));
      return false;
    }
    Ctx inner(L_, rt_, selfq_, B::kId, self_, K, true);
    if (!b.kfpga_dispatch(e, inner))
      error(make_error(ERR_NO_HANDLER, B::kId, Req::id, B::kId));
    if (!inner.has_reply_)
      return false;
    return take_reply<Rep>(inner.reply_, rep);
  }

  // fast_send to an actor by id in another process, on this chip or on the CPU:
  // write the request into that actor's fast_send FIFO from this process, then
  // wait on the reply FIFO from it. This actor handles nothing else meanwhile;
  // the rest of the chip keeps running. Returns true if it replied with a Rep.
  template <class Req, class Rep>
  bool fast_send(ActorId dst, const Req &req, Rep &rep)
  {
    const int ep = endpoint<NE>(rt_, dst);
    if (ep == K)
    {
      error(make_error(ERR_CYCLE, self_, Req::id, dst));
      return false;
    }
    if (ep < 0)
    {
      error(make_error(ERR_NO_ROUTE, self_, Req::id, dst));
      return false;
    }
    Envelope e;
    if (!pack(req, dst, self_, FAST, e))
    {
      error(make_error(ERR_PAYLOAD_FULL, self_, Req::id, dst));
      return false;
    }
    write_to<NE, K>(L_.freq, ep, e);
    const Envelope r = read_from<NE, K>(L_.frep, ep);
    if (r.id == Error::id)
      return false;   // the call failed; whoever produced the Error has reported it
    return take_reply<Rep>(r, rep);
  }

  bool has_reply() const { return has_reply_; }
  const Envelope &reply_envelope() const { return reply_; }

private:
  template <int, int> friend class Ctx;

  void error(const Envelope &e) { L_.msg[K][kHostEp].write(e); }

  void to_self(const Envelope &e)
  {
    if (!selfq_.push(e))
      error(make_error(ERR_SELF_FULL, self_, e.id, self_));
  }

  // A reply to this actor's fast_send: a Rep, no reply (id 0), or a message of
  // another type, which is reported to the host rather than dropped.
  template <class Rep>
  bool take_reply(const Envelope &r, Rep &rep)
  {
    if (r.id == Rep::id)
    {
      unpack(r, rep);
      return true;
    }
    if (r.id != 0)
      error(make_error(ERR_WRONG_REPLY, self_, r.id, r.src));
    return false;
  }

  Links<NE> &L_;
  const DiscoveryTable &rt_;
  SelfQueue &selfq_;
  ActorId self_;
  ActorId sender_;
  int from_;
  bool fast_;
  Envelope reply_;
  bool has_reply_ = false;
};

// Actors inside A's process, if A lists any with KFPGA_INNER.
template <class A, class C, class = void>
struct has_inner : std::false_type {};
template <class A, class C>
struct has_inner<A, C,
                 decltype((void)std::declval<A &>().kfpga_inner(std::declval<const Envelope &>(),
                                                                std::declval<C &>()))>
    : std::true_type {};

// -1: no inner actor has that id; 0: it has no handler; 1: handled.
template <class C>
int dispatch_inner(const Envelope &, C &)
{
  return -1;
}
template <class C, class B, class... R>
int dispatch_inner(const Envelope &e, C &ctx, B &b, R &...r)
{
  if (e.dst == B::kId)
    return b.kfpga_dispatch(e, ctx) ? 1 : 0;
  return dispatch_inner(e, ctx, r...);
}

template <class A, class C>
int dispatch_to(A &a, const Envelope &e, C &ctx, std::true_type)
{
  if (e.dst == A::kId)
    return a.kfpga_dispatch(e, ctx) ? 1 : 0;
  return a.kfpga_inner(e, ctx);
}
template <class A, class C>
int dispatch_to(A &a, const Envelope &e, C &ctx, std::false_type)
{
  if (e.dst == A::kId)
    return a.kfpga_dispatch(e, ctx) ? 1 : 0;
  return -1;
}

// One step of the actor process at endpoint K: take one message and run the
// handler of the actor it is addressed to (A, or an actor inside A's process).
// Order: a fast_send request from any endpoint, then a message the process sent
// itself, then a message from any endpoint; round-robin among endpoints.
// Returns false if nothing was waiting.
//
// The answer to a fast_send request (id 0 if the handler did not reply) goes
// back on the reply FIFO to the endpoint the request came from, with the
// request's tag, so the waiting caller is always released.
template <class A, int NE, int K>
bool actor_step(A &a, Links<NE> &L, const DiscoveryTable &rt, ActorPorts<NE> &st)
{
  Envelope e;
  int from;
  bool fast = false;
  const int f = pick_from<NE, K>(L.freq, st.last_fast);
  if (f >= 0)
  {
    e = read_from<NE, K>(L.freq, f);
    from = f;
    fast = true;
    st.last_fast = f;
  }
  else if (st.self.n > 0)
  {
    e = st.self.pop();
    from = K;
  }
  else
  {
    const int m = pick_from<NE, K>(L.msg, st.last_msg);
    if (m < 0)
      return false;
    e = read_from<NE, K>(L.msg, m);
    from = m;
    st.last_msg = m;
  }

  Ctx<NE, K> ctx(L, rt, st.self, e.dst, e.src, from, fast);
  const int r = dispatch_to(a, e, ctx, has_inner<A, Ctx<NE, K>>{});
  if (r < 0)
    L.msg[K][NE - 1].write(make_error(ERR_NOT_HERE, A::kId, e.id, e.dst));
  else if (r == 0)
    L.msg[K][NE - 1].write(make_error(ERR_NO_HANDLER, e.dst, e.id, e.dst));

  if (fast)
  {
    Envelope out = ctx.has_reply() ? ctx.reply_envelope() : make_no_reply(e);
    out.tag = e.tag;
    write_to<NE, K>(L.frep, from, out);
  }
  return true;
}

// The actor as a free-running hardware process.
template <class A, int NE, int K>
void actor_process(A &a, Links<NE> &L, const DiscoveryTable &rt)
{
  ActorPorts<NE> st;
  for (;;)
    actor_step<A, NE, K>(a, L, rt, st);
}

} // namespace kfpga

// Handler table. Expands to kfpga_dispatch(): a switch on the message id.
#define KFPGA_ON(msg_type, handler)        \
  case msg_type::id:                       \
  {                                        \
    msg_type m_;                           \
    ::kfpga::unpack(e_, m_);               \
    handler(m_, ctx_);                     \
    return true;                           \
  }

// Actors held as members of this one, inside its process, e.g. KFPGA_INNER(pong).
#define KFPGA_INNER(...)                                                     \
  template <class KfpgaCtx_>                                                 \
  int kfpga_inner(const ::kfpga::Envelope &e_, KfpgaCtx_ &ctx_)              \
  {                                                                          \
    return ::kfpga::dispatch_inner(e_, ctx_, __VA_ARGS__);                   \
  }

#define KFPGA_HANDLERS(...)                                                  \
  template <class KfpgaCtx_>                                                 \
  bool kfpga_dispatch(const ::kfpga::Envelope &e_, KfpgaCtx_ &ctx_)          \
  {                                                                          \
    switch (e_.id)                                                           \
    {                                                                        \
      __VA_ARGS__                                                            \
    default:                                                                 \
      return false;                                                          \
    }                                                                        \
  }
