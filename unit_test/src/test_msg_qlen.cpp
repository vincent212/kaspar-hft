/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 *
 * Message::qlen -- the per-message mailbox depth stamped at enqueue.
 *
 * WHY THIS EXISTS. QLen samples depth on a 100 ms grid, so a burst that fills
 * and drains a mailbox between two ticks is invisible to it, and its own
 * sample timestamp carries the scheduler's wake jitter -- which grows under
 * exactly the load being studied. Carrying the depth on the message removes
 * both problems. These tests pin the contract that the number is actually
 * written and actually counts what we claim.
 *
 * None of these start an actor thread. The sink actor is constructed but never
 * run, so nothing drains its mailbox and the backlog is deterministic: the Nth
 * send sees exactly N-1 messages ahead of it. The queued messages leak at the
 * end of each test (Actor's destructor does not drain), which is deliberate --
 * draining would need the run loop and the determinism is worth more here.
 */

#include <gtest/gtest.h>

#include "actors/Actor.hpp"
#include "actors/Message.hpp"

namespace
{
  struct QProbe : public actors::MessageT<QProbe>
  {
    int seq;
    explicit QProbe(int s) : seq(s) {}
  };

  // Never managed, never started: its mailbox only ever fills.
  struct Sink : public actors::Actor
  {
    const char *get_name() const override { return "qlen_sink"; }
  };
}

// The contract: message i sees exactly i messages already queued.
TEST(MessageQLen, StampsBacklogAtEnqueue)
{
  Sink sink;
  for (int i = 0; i < 8; ++i)
  {
    auto *m = new QProbe(i);
    EXPECT_EQ(m->qlen, 0u) << "qlen must be zero before enqueue";
    sink.send(m, nullptr);
    EXPECT_EQ(m->qlen, static_cast<uint32_t>(i))
      << "message " << i << " should have queued behind " << i << " others";
  }
}

// fast_send runs the handler inline on the caller's thread. No queue is
// involved, so there is no backlog to report and the field must stay zero
// rather than reporting a stale or unrelated depth.
TEST(MessageQLen, FastSendLeavesQlenZero)
{
  Sink sink;
  QProbe m(0);
  auto reply = sink.fast_send(&m, nullptr);
  EXPECT_EQ(m.qlen, 0u);
}

// The ring was 64 slots. Past that, BQueue::push falls back to a heap
// allocation into overflow_ on the PRODUCER thread under the mailbox mutex --
// on the feed-handler thread, during exactly the bursts being measured. The
// ring is ACTOR_BQUEUE_SIZE, so a burst up to that depth allocates nothing.
//
// n is derived from ACTOR_BQUEUE_SIZE rather than hardcoded, so retuning the
// ring cannot silently turn this into a test of the overflow path.
TEST(MessageQLen, BurstPastOldRingSizeStillCountsUp)
{
  ASSERT_GT(ACTOR_BQUEUE_SIZE, 64) << "ring must exceed the old default";

  Sink sink;
  const int n = ACTOR_BQUEUE_SIZE;   // fill the ring exactly, do not overflow
  for (int i = 0; i < n; ++i)
  {
    auto *m = new QProbe(i);
    sink.send(m, nullptr);
    ASSERT_EQ(m->qlen, static_cast<uint32_t>(i))
      << "depth stopped tracking at " << i << " -- ring overflowed";
  }
  EXPECT_EQ(sink.queue_length(), static_cast<std::size_t>(n));
  EXPECT_EQ(sink.circ_buf_len(), static_cast<std::size_t>(n));
}

// Past the ring, circ_buf_len() cannot see overflow_ (std::deque::size() is
// not safe to read unlocked -- see BQueue.hpp), so qlen SATURATES at the ring
// size instead of continuing to count. Nothing is dropped: length() keeps
// growing, only the reported number stops.
//
// This is the censoring limit of the instrument. A qlen histogram with a spike
// at exactly ACTOR_BQUEUE_SIZE is not a real mode -- it is every deeper burst
// piled into the last bin, and any fit through it is wrong.
TEST(MessageQLen, SaturatesAtRingSizeOnceOverflowStarts)
{
  Sink sink;
  const int n = ACTOR_BQUEUE_SIZE + 50;
  for (int i = 0; i < n; ++i)
  {
    auto *m = new QProbe(i);
    sink.send(m, nullptr);
    const uint32_t want = static_cast<uint32_t>(
        i < ACTOR_BQUEUE_SIZE ? i : ACTOR_BQUEUE_SIZE);
    ASSERT_EQ(m->qlen, want) << "at i=" << i;
  }
  // The ring is pinned at capacity; the true depth is only in length().
  EXPECT_EQ(sink.circ_buf_len(), static_cast<std::size_t>(ACTOR_BQUEUE_SIZE));
  EXPECT_EQ(sink.queue_length(), static_cast<std::size_t>(n));
}

// circ_buf_len() is the unlocked read used by the stamp and by QLen. With no
// overflow it must agree exactly with the locked length(); that equality is
// what makes the cheap read usable as a substitute.
TEST(MessageQLen, CircBufLenAgreesWithLengthBelowCapacity)
{
  Sink sink;
  EXPECT_EQ(sink.circ_buf_len(), 0u);
  EXPECT_EQ(sink.queue_length(), 0u);

  for (int i = 0; i < 100; ++i)
    sink.send(new QProbe(i), nullptr);

  EXPECT_EQ(sink.circ_buf_len(), sink.queue_length());
  EXPECT_EQ(sink.circ_buf_len(), 100u);
}

// A copied message has not been enqueued, so it must not inherit the
// original's depth. This matters: the BOOKSEND macro heap-COPIES a stack
// message before sending it (mdp3/handler_if.hpp), so the copy is what gets
// stamped, and a leaked value from the source would be silently wrong.
TEST(MessageQLen, CopyDoesNotInheritDepth)
{
  Sink sink;
  sink.send(new QProbe(0), nullptr);
  sink.send(new QProbe(1), nullptr);

  QProbe src(2);
  sink.send(&src, nullptr);
  EXPECT_EQ(src.qlen, 2u);

  QProbe copy(src);          // copy ctor
  EXPECT_EQ(copy.qlen, 0u);

  QProbe assigned(9);
  assigned.destination = nullptr;
  assigned = src;            // copy assignment
  EXPECT_EQ(assigned.qlen, 0u);

  // ...and the copy picks up its own depth when it is actually enqueued.
  auto *heap_copy = new QProbe(src);
  sink.send(heap_copy, nullptr);
  EXPECT_EQ(heap_copy->qlen, 3u);
}
