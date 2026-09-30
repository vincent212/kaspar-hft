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
 *     void on_ping(const Ping &m, kfpga::Ctx &ctx) {
 *       ++pings;
 *       Pong r; r.count = m.count + 1000 * pings;
 *       ctx.reply(r);
 *     }
 *   };
 *
 * A handler talks through its Ctx:
 *   ctx.send(dst, msg)            queue msg for actor dst (on this FPGA or the CPU)
 *   ctx.reply(msg)                answer the sender of the current message
 *   ctx.fast_send(b, msg, rep)    b is an actor object in this process: its
 *                                 handler runs now, inline; reply by value
 *   ctx.fast_send(dst, msg, rep)  dst is an actor id, typically a CPU actor: the
 *                                 request goes out, this actor's process waits
 *                                 for the reply, then continues. An RPC.
 *   ctx.sender()                  who sent the current message
 *
 * Each actor runs as its own hardware process (actor_process): one mailbox FIFO,
 * one fast_send port that is always served first, one message at a time.
 */

#include <type_traits>

#include "actors_fpga/envelope.hpp"
#include "actors_fpga/stream.hpp"

namespace kfpga {

class Ctx
{
public:
  // out:        where this actor's messages leave (to the router)
  // call_reply: where the reply to this actor's remote fast_send arrives
  Ctx(ActorId self, ActorId sender, bool fast, hls::stream<Envelope> &out,
      hls::stream<Envelope> &call_reply)
    : self_(self), sender_(sender), fast_(fast), out_(out), call_reply_(call_reply)
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
      out_.write(make_error(ERR_PAYLOAD_FULL, self_, M::id, dst));
      return;
    }
    out_.write(e);
  }

  // To a send(): a message to the sender, now, as on the CPU.
  // To a fast_send(): held and returned to the waiting caller when the handler
  // returns, as on the CPU. Only the first reply to a fast_send counts.
  template <class M>
  void reply(const M &m)
  {
    if (!fast_)
    {
      send(sender_, m);
      return;
    }
    if (has_reply_)
      return;
    if (!pack(m, sender_, self_, FAST_REPLY, reply_))
    {
      out_.write(make_error(ERR_PAYLOAD_FULL, self_, M::id, sender_));
      return;
    }
    has_reply_ = true;
  }

  // fast_send to an actor object in the same process: its handler runs now,
  // inside this one. Returns true if it replied with a Rep.
  template <class B, class Req, class Rep,
            class = decltype(&B::kfpga_dispatch)>
  bool fast_send(B &b, const Req &req, Rep &rep)
  {
    Envelope e;
    if (!pack(req, B::kId, self_, FAST, e))
    {
      out_.write(make_error(ERR_PAYLOAD_FULL, self_, Req::id, B::kId));
      return false;
    }
    Ctx inner(B::kId, self_, true, out_, call_reply_);
    if (!b.kfpga_dispatch(e, inner))
      out_.write(make_error(ERR_NO_HANDLER, B::kId, Req::id, B::kId));
    if (!inner.has_reply_ || inner.reply_.id != Rep::id)
      return false;
    unpack(inner.reply_, rep);
    return true;
  }

  // fast_send to an actor by id, outside this process (a CPU actor, or an FPGA
  // actor in another process, relayed through the host): send the request, then
  // wait for the reply on call_reply. This actor handles nothing else meanwhile;
  // the rest of the chip keeps running. Returns true if it replied with a Rep.
  template <class Req, class Rep>
  bool fast_send(ActorId dst, const Req &req, Rep &rep)
  {
    Envelope e;
    if (!pack(req, dst, self_, FAST, e))
    {
      out_.write(make_error(ERR_PAYLOAD_FULL, self_, Req::id, dst));
      return false;
    }
    out_.write(e);
    const Envelope r = call_reply_.read();
    if (r.id == Rep::id)
    {
      unpack(r, rep);
      return true;
    }
    if (r.id == Error::id)
    {
      // The call failed (no such actor, no handler...): tell the host.
      Envelope err = r;
      err.dst = kHost;
      err.kind = SEND;
      out_.write(err);
    }
    return false;
  }

  bool has_reply() const { return has_reply_; }
  const Envelope &reply_envelope() const { return reply_; }

private:
  ActorId self_;
  ActorId sender_;
  bool fast_;
  hls::stream<Envelope> &out_;
  hls::stream<Envelope> &call_reply_;
  Envelope reply_;
  bool has_reply_ = false;
};

// One step of an actor's process: take one message, fast_send port first, and
// run its handler. Returns false if both inputs were empty.
//
//   fast_in     fast_send requests from the host, served before the mailbox
//   mbox        the mailbox: messages routed to this actor
//   call_reply  replies to this actor's own remote fast_sends
//   to_router   everything this actor sends, its replies to send()s, errors
//   fast_reply  the answer to each fast_send request (id 0 if the handler did
//               not reply), so the waiting caller is always released
template <class A>
bool actor_step(A &a, hls::stream<Envelope> &fast_in, hls::stream<Envelope> &mbox,
                hls::stream<Envelope> &call_reply, hls::stream<Envelope> &to_router,
                hls::stream<Envelope> &fast_reply)
{
  Envelope e;
  bool fast;
  if (!fast_in.empty())
  {
    e = fast_in.read();
    fast = true;
  }
  else if (!mbox.empty())
  {
    e = mbox.read();
    fast = false;
  }
  else
  {
    return false;
  }

  Ctx ctx(A::kId, e.src, fast, to_router, call_reply);
  if (!a.kfpga_dispatch(e, ctx))
    to_router.write(make_error(ERR_NO_HANDLER, A::kId, e.id, A::kId));

  if (fast)
  {
    if (ctx.has_reply())
      fast_reply.write(ctx.reply_envelope());
    else
    {
      Envelope none = {};
      none.dst = e.src;
      none.src = A::kId;
      none.id = 0;
      none.kind = FAST_REPLY;
      fast_reply.write(none);
    }
  }
  return true;
}

// The actor as a free-running hardware process.
template <class A>
void actor_process(A &a, hls::stream<Envelope> &fast_in, hls::stream<Envelope> &mbox,
                   hls::stream<Envelope> &call_reply, hls::stream<Envelope> &to_router,
                   hls::stream<Envelope> &fast_reply)
{
  for (;;)
    actor_step(a, fast_in, mbox, call_reply, to_router, fast_reply);
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

#define KFPGA_HANDLERS(...)                                         \
  bool kfpga_dispatch(const ::kfpga::Envelope &e_, ::kfpga::Ctx &ctx_) \
  {                                                                 \
    switch (e_.id)                                                  \
    {                                                               \
      __VA_ARGS__                                                   \
    default:                                                        \
      return false;                                                 \
    }                                                               \
  }
