/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// FPGA actor runtime: unit tests, run on the host with plain C++.

#include <gtest/gtest.h>

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

// Ping sends to Pong through the router, three round trips, then reports Done.
TEST(PingPong, SendThroughRouter)
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

// FusedPing calls its own Pong with fast_send: same answers, no router traffic.
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

// A fast_send is served before messages already waiting in the mailbox.
TEST(ActorStep, FastPortBeforeMailbox)
{
  PongActor<kPong> pong;
  hls::stream<Envelope> fast_in, mbox, call_reply, to_router, fast_reply;
  mbox.write(env(ping(1), kPong));
  mbox.write(env(ping(2), kPong));
  fast_in.write(env(ping(9), kPong, kHost, FAST));

  ASSERT_TRUE(actor_step(pong, fast_in, mbox, call_reply, to_router, fast_reply));
  auto fr = drain(fast_reply);
  ASSERT_EQ(fr.size(), 1u);
  EXPECT_EQ(as<Pong>(fr[0]).count, 9u + 1000u);   // first message Pong handled
  EXPECT_EQ(mbox.size(), 2u);                      // mailbox untouched

  while (actor_step(pong, fast_in, mbox, call_reply, to_router, fast_reply)) {}
  auto sent = drain(to_router);
  ASSERT_EQ(sent.size(), 2u);
  EXPECT_EQ(as<Pong>(sent[0]).count, 1u + 2000u);
  EXPECT_EQ(as<Pong>(sent[1]).count, 2u + 3000u);
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

TEST(Errors, FastSendWithoutReplyStillAnswers)
{
  Design d;
  hls::stream<Envelope> from_host, to_pcie;
  from_host.write(env(start(1), kPong, kHost, FAST));   // no handler, so no reply
  d.run(from_host, to_pcie);
  auto out = drain(to_pcie);
  ASSERT_EQ(out.size(), 2u);
  bool saw_error = false, saw_empty_reply = false;
  for (const auto &e : out)
  {
    if (e.id == Error::id && e.kind == SEND)
      saw_error = true;
    if (e.kind == FAST_REPLY && e.id == 0)
      saw_empty_reply = true;
  }
  EXPECT_TRUE(saw_error);
  EXPECT_TRUE(saw_empty_reply);
}
