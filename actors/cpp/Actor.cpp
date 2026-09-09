/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <list>
#include <string>
#include <typeinfo>
#include <exception>
#include <memory>
#include <iostream>
#include <cassert>
#include <thread>
#include "actors/Queue.hpp"
#include "actors/BQueue.hpp"
#include "actors/msg/Shutdown.hpp"
#include "actors/Actor.hpp"
#include "actors/ActorRef.hpp"

#include <unistd.h>
#include <sys/types.h>
#ifdef __linux__
#include <sys/syscall.h>
#endif
#ifdef __APPLE__
#include <pthread.h>
#endif

using namespace std;
using namespace actors;

Actor::~Actor()
{
  // msgq is a value member (std::variant) now — nothing to delete.
}

Actor::Actor()
  : msgq(std::in_place_type<BQueue<const Message *>>, ACTOR_BQUEUE_SIZE)
{
  handler_cache.resize(ACTOR_HANDLER_CACHE_SIZE, nullptr);
  dont_have_handler.resize(ACTOR_HANDLER_CACHE_SIZE, false);

  // Initialize name with typeid
  const char* type_name = typeid(*this).name();
  strncpy(name, type_name, sizeof(name) - 1);
  name[sizeof(name) - 1] = '\0';
}

void Actor::send(const Message *m, Actor *sender) noexcept
{
  // Guard against send() invoked on a null actor pointer. Compared via a copy
  // rather than `this` directly, so clang doesn't fold it away as tautological.
  [[maybe_unused]] const Actor *self = this;
  assert(self != nullptr && "send to null actor");

  if (terminated)
    return;

  assert(m != nullptr && "null message");
  assert(m->destination == nullptr && "cannot reuse message");

  m->is_fast = false;
  m->last = false;
  m->sender = sender;
  m->destination = this;

  if (is_part_of_group) {
    group->add_message_to_queue(m);
  } else {
    add_message_to_queue(m);
  }
}

bool Actor::call_handler(const Message *m) noexcept
{
  auto id = m->get_message_id();
  auto f0 = handler_cache[id];
  if (f0) {
    (this->*f0)(m);
    return true;
  }
  if (dont_have_handler[id]) {
    return false;
  }
  auto midx = std::type_index(typeid(*m));
  auto p = handlers.find(midx);
  if (p == handlers.end()) {
    dont_have_handler[id] = true;
    return false;
  }
  auto f = p->second;
  (this->*f)(m);
  handler_cache[id] = f;
  return true;
}

void Actor::process_message_internal(const Message *m, bool dontdel) noexcept
{
  std::lock_guard<std::mutex> lock(fast_send_mutex);
  [[maybe_unused]] const Actor *self = this;
  assert(self != nullptr && "no actor to handle message");

  msg_cnt++;
  using_fast_send = false;

  bool called = call_handler(m);
  if (!called)
    process_message(m);

  if (!dontdel) {
    delete m;
  }
}

std::unique_ptr<const Message> Actor::fast_send(const Message *m, Actor *sender) noexcept
{
  std::lock_guard<std::mutex> lock(fast_send_mutex);

  [[maybe_unused]] const Actor *self = this;
  assert(self != nullptr && "fast send to null actor");
  assert(m != nullptr && "fast send with no message");
  assert(this != sender && "fast send to itself");

  m->sender = sender;
  m->is_fast = true;
  m->last = true;
  reply_message = nullptr;
  using_fast_send = true;
  msg_cnt++;

  if (terminated)
    return std::unique_ptr<const Message>(reply_message);

  bool called = call_handler(m);
  if (!called)
    process_message(m);

  return std::unique_ptr<const Message>(reply_message);
}

namespace {
  // Only BQueueBatched drives the whole-mailbox batch drain; every other queue
  // type drains one message at a time.
  template <class Q> struct mailbox_batched : std::false_type {};
  template <class T> struct mailbox_batched<actors::BQueueBatched<T>> : std::true_type {};
}

template <class Q>
void Actor::run_loop(Q& q) noexcept
{
  if constexpr (mailbox_batched<Q>::value) {
    // Batch drain: one pop_batch + one fast_send_mutex hold for the whole
    // batch (N messages, one lock). fast_send waits for the batch — the
    // documented throughput/latency tradeoff of BQueueBatched.
    std::vector<const Message *> batch;
    while (true) {
      q.pop_batch(batch);
      bool stop = false;
      {
        std::lock_guard<std::mutex> lock(fast_send_mutex);
        for (size_t i = 0; i < batch.size(); ++i) {
          const Message *m = batch[i];
          m->last = (i + 1 == batch.size());
          reply_to = m->sender;
          bool is_shutdown = m->get_message_id() == msg::Shutdown::id;

          msg_cnt++;
          using_fast_send = false;
          if (!call_handler(m))
            process_message(m);
          delete m;

          if (is_shutdown || terminated) {
            stop = true;
            // Drained the whole mailbox but terminating now — delete the
            // co-drained tail we won't process, or it leaks.
            for (size_t k = i + 1; k < batch.size(); ++k)
              delete batch[k];
            break;
          }
        }
      }
      if (stop) break;
    }
  } else {
    // Single-message drain (unchanged semantics from the pointer-based loop).
    while (true) {
      auto r = q.pop();
      const Message *m = std::get<0>(r);
      m->last = std::get<1>(r);
      reply_to = m->sender;

      bool is_shutdown = m->get_message_id() == msg::Shutdown::id;

      process_message_internal(m);

      if (is_shutdown || terminated)
        break;
    }
  }
}

void Actor::operator()() noexcept
{
#ifdef __linux__
  tid = syscall(SYS_gettid);
#elif defined(__APPLE__)
  uint64_t tid64;
  pthread_threadid_np(nullptr, &tid64);
  tid = static_cast<long>(tid64);
#else
  tid = 0;
#endif
  std::cerr << endl << get_name() << " tid: " << tid << endl;
  init();

  // Visit ONCE to resolve the concrete queue type, then run the whole drain
  // loop against it — the loop is monomorphic and inlinable, no per-message
  // vtable indirection.
  std::visit([this](auto& q) { this->run_loop(q); }, msgq);

  terminated = true;
  end();
}

void Actor::reply(const Message *m) noexcept
{
  if (using_fast_send) {
    m->sender = this;
    reply_message = m;
  } else {
    assert(reply_to != nullptr && "no return address");
    reply_to->send(m, this);
  }
}

void Actor::terminate() noexcept
{
  terminate_called = true;
  this->send(new msg::Shutdown());
  sleep(3);
}

void Actor::fast_terminate() noexcept
{
  terminate_called = true;
  this->fast_send(new msg::Shutdown(), nullptr);
}

void Actor::add_message_to_queue(const Message *m)
{
  std::visit([m](auto& q) { q.push(m); }, msgq);
}

std::size_t Actor::queue_length() const noexcept
{
  return std::visit([](const auto& q) { return q.length(); }, msgq);
}

const Message* Actor::peek() const
{
  return std::visit([](const auto& q) { return q.peek(); }, msgq);
}

void Actor::set_group(Actor *pgroup)
{
  is_part_of_group = true;
  group = pgroup;
}

Actor* Actor::get_group() const
{
  assert(is_part_of_group && "not part of group");
  return group;
}

// Stub for RemoteActorRef::send (ZMQ not implemented yet)
void RemoteActorRef::send(const Message* /*m*/, Actor* /*sender*/) {
  // TODO: Implement ZMQ send
}
