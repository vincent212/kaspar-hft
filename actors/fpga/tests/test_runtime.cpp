/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// FPGA actor runtime: unit tests, run on the host with plain C++.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "../examples/ping_pong/design.hpp"

using namespace kfpga;
using namespace pingpong;

namespace {

template <class M>
Envelope env(const M &m, ActorId dst, ActorId src = kHost, uint8_t kind = SEND)
{
  Envelope e;
  EXPECT_TRUE(pack(m, dst, src, kind, e));
  return e;
}

std::vector<Envelope> drain(hls::stream<Envelope> &s)
{
  std::vector<Envelope> out;
  while (!s.empty())
    out.push_back(s.read());
  return out;
}

template <class M>
M as(const Envelope &e)
{
  EXPECT_EQ(e.id, M::id);
  M m;
  unpack(e, m);
  return m;
}

Start start(uint32_t rounds)
{
  Start s;
  s.rounds = rounds;
  return s;
}

Ping ping(uint32_t c)
{
  Ping p;
  p.count = c;
  return p;
}

enum class Side : uint8_t { Bid = 1, Ask = 2 };

struct Wide
{
  static constexpr MsgId id = 390;
  uint64_t order_id = 0;
  int64_t px = 0;
  int8_t exp = 0;
  bool last = false;
  char c = 0;
  Side side = Side::Bid;
  int32_t qty = 0;
  KFPGA_FIELDS(order_id, px, exp, last, c, side, qty)
};

struct TooBig
{
  static constexpr MsgId id = 391;
  uint64_t a = 0, b = 0, c = 0, d = 0, e = 0, f = 0, g = 0;   // 14 words > 12
  KFPGA_FIELDS(a, b, c, d, e, f, g)
};

} // namespace

TEST(Envelope, RoundTripsEveryFieldKind)
{
  Wide w;
  w.order_id = 0xFEDCBA9876543210ULL;
  w.px = -5000000000123LL;
  w.exp = -9;
  w.last = true;
  w.c = '1';
  w.side = Side::Ask;
  w.qty = -7;
  Envelope e = env(w, 5, 6);
  EXPECT_EQ(e.dst, 5);
  EXPECT_EQ(e.src, 6);
  EXPECT_EQ(e.id, 390);
  EXPECT_EQ(e.n, 2 + 2 + 1 + 1 + 1 + 1 + 1);
  Wide r = as<Wide>(e);
  EXPECT_EQ(r.order_id, w.order_id);
  EXPECT_EQ(r.px, w.px);
  EXPECT_EQ(r.exp, w.exp);
  EXPECT_EQ(r.last, w.last);
  EXPECT_EQ(r.c, w.c);
  EXPECT_EQ(r.side, w.side);
  EXPECT_EQ(r.qty, w.qty);
}

TEST(Envelope, OversizeMessageIsRejected)
{
  Envelope e;
  EXPECT_FALSE(pack(TooBig{}, 1, 2, SEND, e));
}

// Ping sends to Pong over the FIFO between them, three round trips, then reports Done.
TEST(PingPong, SendBetweenProcesses)
{
  Design d;
  hls::stream<Envelope> from_host, to_pcie;
  from_host.write(env(start(3), kPing));
  d.run(from_host, to_pcie);

  auto out = drain(to_pcie);
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].dst, kHost);
  EXPECT_EQ(out[0].src, kPing);
  Done done = as<Done>(out[0]);
  EXPECT_EQ(done.rounds, 3u);
  EXPECT_EQ(done.last, 3u + 1000u * 3u);   // Pong's state counted three pings
  EXPECT_EQ(d.pong.pings, 3u);
}

// FusedPing calls its own Pong with fast_send: same answers, no FIFO traffic.
TEST(PingPong, FusedFastSend)
{
  Design d;
  hls::stream<Envelope> from_host, to_pcie;
  from_host.write(env(start(3), kFusedPing));
  d.run(from_host, to_pcie);

  auto out = drain(to_pcie);
  ASSERT_EQ(out.size(), 1u);
  Done done = as<Done>(out[0]);
  EXPECT_EQ(done.rounds, 3u);
  EXPECT_EQ(done.last, 3u + 1000u * 3u);
  EXPECT_EQ(d.fused.pong.pings, 3u);
  EXPECT_EQ(d.pong.pings, 0u);   // the routed Pong was never involved
}

// The host fast_sends to Pong: the answer comes back as a FAST_REPLY to the host.
TEST(PingPong, HostFastSend)
{
  Design d;
  hls::stream<Envelope> from_host, to_pcie;
  from_host.write(env(ping(7), kPong, kHost, FAST));
  d.run(from_host, to_pcie);

  auto out = drain(to_pcie);
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].kind, FAST_REPLY);
  EXPECT_EQ(out[0].src, kPong);
  EXPECT_EQ(out[0].dst, kHost);
  EXPECT_EQ(as<Pong>(out[0]).count, 7u + 1000u);
}

// A fast_send is served before messages already waiting.
TEST(ActorStep, FastPortBeforeMailbox)
{
  constexpr int H = kEndpoints - 1;
  Links<kEndpoints> L;
  const DiscoveryTable rt = discovery();
  ActorPorts<kEndpoints> st;
  PongActor<kPong> pong;
  L.msg[H][1].write(env(ping(1), kPong));
  L.msg[H][1].write(env(ping(2), kPong));
  L.freq[H][1].write(env(ping(9), kPong, kHost, FAST));

  ASSERT_TRUE((actor_step<PongActor<kPong>, kEndpoints, 1>(pong, L, rt, st)));
  auto fr = drain(L.frep[1][H]);
  ASSERT_EQ(fr.size(), 1u);
  EXPECT_EQ(fr[0].kind, FAST_REPLY);
  EXPECT_EQ(as<Pong>(fr[0]).count, 9u + 1000u);   // first message Pong handled
  EXPECT_EQ(L.msg[H][1].size(), 2u);               // the waiting messages untouched

  while (actor_step<PongActor<kPong>, kEndpoints, 1>(pong, L, rt, st)) {}
  auto sent = drain(L.msg[1][H]);
  ASSERT_EQ(sent.size(), 2u);
  EXPECT_EQ(as<Pong>(sent[0]).count, 1u + 2000u);
  EXPECT_EQ(as<Pong>(sent[1]).count, 2u + 3000u);
}

// A send goes straight into the FIFO from sender to receiver: after only the
// sender has run, the message is waiting at the receiver.
TEST(Links, SendIsOneFifoHop)
{
  constexpr int H = kEndpoints - 1;
  Links<kEndpoints> L;
  const DiscoveryTable rt = discovery();
  ActorPorts<kEndpoints> st;
  PingActor ping_actor;
  L.msg[H][0].write(env(start(1), kPing));
  ASSERT_TRUE((actor_step<PingActor, kEndpoints, 0>(ping_actor, L, rt, st)));
  ASSERT_EQ(L.msg[0][1].size(), 1u);   // Ping -> Pong, nothing in between
  EXPECT_EQ(as<Ping>(L.msg[0][1].read()).count, 1u);
  for (int r = 0; r < kEndpoints; ++r)
    if (r != 1)
      EXPECT_TRUE(L.msg[0][r].empty());
}

namespace {
constexpr ActorId kCaller = 10;
constexpr ActorId kCallee = 11;
struct RemoteCaller
{
  static constexpr ActorId kId = kCaller;
  KFPGA_HANDLERS(KFPGA_ON(Ping, on_ping))
  template <class Ctx>
  void on_ping(const Ping &m, Ctx &ctx)
  {
    Pong r;
    if (ctx.fast_send(kCallee, m, r))
      ctx.reply(r);
  }
};
DiscoveryTable two_actor_discovery()
{
  DiscoveryTable rt;
  for (int i = 0; i < kMaxActors; ++i)
    rt.port[i] = kNoRoute;
  rt.port[kHost] = kToHost;
  rt.port[kCaller] = 0;
  rt.port[kCallee] = 1;
  return rt;
}
} // namespace

// fast_send between two processes: the request goes straight into the callee's
// fast_send FIFO from the caller, and the reply straight back.
TEST(Links, FastSendIsOneFifoHopEachWay)
{
  constexpr int NE = 3, H = 2;
  Links<NE> L;
  const DiscoveryTable rt = two_actor_discovery();
  ActorPorts<NE> caller_st, callee_st;
  RemoteCaller caller;
  PongActor<kCallee> callee;
  L.msg[H][0].write(env(ping(4), kCaller));

  // The caller's process blocks waiting for the reply, as it does in hardware.
  std::thread t([&] { actor_step<RemoteCaller, NE, 0>(caller, L, rt, caller_st); });
  for (int i = 0; i < 2000 && L.freq[0][1].empty(); ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  ASSERT_EQ(L.freq[0][1].size(), 1u);   // request at the callee, nothing in between

  ASSERT_TRUE((actor_step<PongActor<kCallee>, NE, 1>(callee, L, rt, callee_st)));
  t.join();
  EXPECT_TRUE(L.frep[1][0].empty());    // the caller took its reply
  auto out = drain(L.msg[0][H]);
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(as<Pong>(out[0]).count, 4u + 1000u);
}

// fast_send to oneself would wait on itself: reported, not a hang.
TEST(Errors, FastSendToSelfIsReported)
{
  constexpr int NE = 3, H = 2;
  Links<NE> L;
  DiscoveryTable rt = two_actor_discovery();
  rt.port[kCallee] = 0;   // the callee id lives in the caller's own process
  ActorPorts<NE> st;
  RemoteCaller caller;
  L.msg[H][0].write(env(ping(1), kCaller));
  ASSERT_TRUE((actor_step<RemoteCaller, NE, 0>(caller, L, rt, st)));
  auto out = drain(L.msg[0][H]);
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(as<Error>(out[0]).code, ERR_CYCLE);
}

// Messages from one sender to one receiver arrive in order.
TEST(PingPong, OrderPerSenderReceiver)
{
  Design d;
  hls::stream<Envelope> from_host, to_pcie;
  for (uint32_t i = 1; i <= 5; ++i)
    from_host.write(env(ping(i), kPong));
  d.run(from_host, to_pcie);

  auto out = drain(to_pcie);
  ASSERT_EQ(out.size(), 5u);
  for (uint32_t i = 0; i < 5; ++i)
    EXPECT_EQ(as<Pong>(out[i]).count, (i + 1) + 1000u * (i + 1));
}

// Nothing is dropped silently: each failure reaches the host as an Error.
TEST(Errors, NoRoute)
{
  Design d;
  hls::stream<Envelope> from_host, to_pcie;
  from_host.write(env(ping(1), 9));   // no actor 9 on this FPGA
  d.run(from_host, to_pcie);
  auto out = drain(to_pcie);
  ASSERT_EQ(out.size(), 1u);
  Error err = as<Error>(out[0]);
  EXPECT_EQ(err.code, ERR_NO_ROUTE);
  EXPECT_EQ(err.dst, 9u);
}

TEST(Errors, FastSendNoRouteReleasesTheCaller)
{
  Design d;
  hls::stream<Envelope> from_host, to_pcie;
  from_host.write(env(ping(1), 9, kHost, FAST));
  d.run(from_host, to_pcie);
  auto out = drain(to_pcie);
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].kind, FAST_REPLY);
  EXPECT_EQ(as<Error>(out[0]).code, ERR_NO_ROUTE);
}

TEST(Errors, NoHandler)
{
  Design d;
  hls::stream<Envelope> from_host, to_pcie;
  from_host.write(env(start(1), kPong));   // Pong has no Start handler
  d.run(from_host, to_pcie);
  auto out = drain(to_pcie);
  ASSERT_EQ(out.size(), 1u);
  Error err = as<Error>(out[0]);
  EXPECT_EQ(err.code, ERR_NO_HANDLER);
  EXPECT_EQ(err.actor, kPong);
  EXPECT_EQ(err.msg, Start::id);
}

TEST(Errors, FastSendWithNoHandlerIsAnsweredWithTheError)
{
  Design d;
  hls::stream<Envelope> from_host, to_pcie;
  from_host.write(env(start(1), kPong, kHost, FAST));   // Pong has no Start handler
  d.run(from_host, to_pcie);
  auto out = drain(to_pcie);
  ASSERT_EQ(out.size(), 1u);   // the answer carries the error; nothing else is sent
  EXPECT_EQ(out[0].kind, FAST_REPLY);
  EXPECT_EQ(as<Error>(out[0]).code, ERR_NO_HANDLER);
}

// A handler that runs but does not reply still releases the caller: id 0.
TEST(Errors, FastSendWithoutReplyStillAnswers)
{
  Design d;
  hls::stream<Envelope> from_host, to_pcie;
  from_host.write(env(start(1), kPing, kHost, FAST));   // Ping handles Start, replies nothing
  d.run(from_host, to_pcie);
  bool saw_empty_reply = false;
  for (const auto &e : drain(to_pcie))
    if (e.kind == FAST_REPLY && e.id == 0)
      saw_empty_reply = true;
  EXPECT_TRUE(saw_empty_reply);
}

// An inline fast_send answered with the wrong message type is reported, not dropped.
namespace {
struct WrongReplier
{
  static constexpr ActorId kId = 7;
  KFPGA_HANDLERS(KFPGA_ON(Ping, on_ping))
  template <class Ctx>
  void on_ping(const Ping &m, Ctx &ctx) { ctx.reply(start(m.count)); }   // a Start, not a Pong
};
struct CallsWrongReplier
{
  static constexpr ActorId kId = 8;
  WrongReplier callee;
  bool got = true;
  KFPGA_HANDLERS(KFPGA_ON(Ping, on_ping))
  template <class Ctx>
  void on_ping(const Ping &m, Ctx &ctx)
  {
    Pong r;
    got = ctx.fast_send(callee, m, r);
  }
};
} // namespace

TEST(Errors, WrongReplyTypeIsReported)
{
  constexpr int NE = 2, H = 1;
  Links<NE> L;
  DiscoveryTable rt;
  for (int i = 0; i < kMaxActors; ++i)
    rt.port[i] = kNoRoute;
  rt.port[kHost] = kToHost;
  rt.port[CallsWrongReplier::kId] = 0;
  ActorPorts<NE> st;
  CallsWrongReplier a;
  L.msg[H][0].write(env(ping(1), CallsWrongReplier::kId));
  ASSERT_TRUE((actor_step<CallsWrongReplier, NE, 0>(a, L, rt, st)));
  EXPECT_FALSE(a.got);
  auto out = drain(L.msg[0][H]);
  ASSERT_EQ(out.size(), 1u);
  Error err = as<Error>(out[0]);
  EXPECT_EQ(err.code, ERR_WRONG_REPLY);
  EXPECT_EQ(err.msg, Start::id);
}

// An actor inside another actor's process receives messages addressed to it.
TEST(PingPong, InnerActorIsAddressable)
{
  Design d;
  hls::stream<Envelope> from_host, to_pcie;
  from_host.write(env(ping(5), kFusedPong));   // FusedPing's inner Pong
  d.run(from_host, to_pcie);
  auto out = drain(to_pcie);
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].src, kFusedPong);
  EXPECT_EQ(as<Pong>(out[0]).count, 5u + 1000u);
  EXPECT_EQ(d.fused.pong.pings, 1u);
}

// A reply from the host has its own input: it reaches the waiting actor even
// while messages wait, unread, on the other input.
TEST(Links, HostReplyHasItsOwnInput)
{
  constexpr int H = kEndpoints - 1;
  Links<kEndpoints> L;
  const DiscoveryTable rt = discovery();
  hls::stream<Envelope> from_host, from_host_reply;
  for (int i = 0; i < 4; ++i)
    from_host.write(env(ping(1), kPong));
  Pong p;
  p.count = 7;
  from_host_reply.write(env(p, kPing, kHost, FAST_REPLY));
  ASSERT_TRUE(host_in_reply_step(from_host_reply, L, rt));
  ASSERT_EQ(L.frep[H][0].size(), 1u);
  EXPECT_EQ(from_host.size(), 4u);
}

// A fast_send reply carries the request's tag.
TEST(ActorStep, ReplyCarriesTheRequestTag)
{
  constexpr int H = kEndpoints - 1;
  Links<kEndpoints> L;
  const DiscoveryTable rt = discovery();
  ActorPorts<kEndpoints> st;
  PongActor<kPong> pong;
  Envelope req = env(ping(1), kPong, kHost, FAST);
  req.tag = 4242;
  L.freq[H][1].write(req);
  ASSERT_TRUE((actor_step<PongActor<kPong>, kEndpoints, 1>(pong, L, rt, st)));
  auto r = drain(L.frep[1][H]);
  ASSERT_EQ(r.size(), 1u);
  EXPECT_EQ(r[0].tag, 4242u);
}
