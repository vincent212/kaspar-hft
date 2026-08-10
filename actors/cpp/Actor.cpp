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
  delete msgq;
}

Actor::Actor() : Actor(new BQueue<const Message *>(ACTOR_BQUEUE_SIZE))
{
}

Actor::Actor(Queue<const Message *> *mailbox)
{
  msgq = mailbox;
  handler_cache.resize(ACTOR_HANDLER_CACHE_SIZE, nullptr);
  dont_have_handler.resize(ACTOR_HANDLER_CACHE_SIZE, false);

  // Initialize name with typeid
  const char* type_name = typeid(*this).name();
  strncpy(name, type_name, sizeof(name) - 1);
  name[sizeof(name) - 1] = '\0';
}

void Actor::send(const Message *m, Actor *sender) noexcept
{
  assert(this != nullptr && "send to null actor");

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
  assert(this != nullptr && "no actor to handle message");

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

  assert(this != nullptr && "fast send to null actor");
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

  // Lever 1: drain the whole mailbox in one lock (msgq->pop_batch). Lever 2:
  // take the per-actor fast_send_mutex ONCE for the whole batch instead of per
  // message (process_message_internal locks it per call, so the body is inlined
  // here). fast_send shares this mutex, so a fast_send to this actor waits for
  // the batch to finish — the intended throughput/latency tradeoff.
  std::vector<const Message *> batch;
  while (true) {
    msgq->pop_batch(batch);
    bool stop = false;
    {
      std::lock_guard<std::mutex> lock(fast_send_mutex);
      for (size_t i = 0; i < batch.size(); ++i) {
        const Message *m = batch[i];
        m->last = (i + 1 == batch.size()); // last == queue was drained empty
        reply_to = m->sender;
        bool is_shutdown = m->get_message_id() == 5;

        msg_cnt++;
        using_fast_send = false;
        bool called = call_handler(m);
        if (!called)
          process_message(m);
        delete m;

        if (is_shutdown || terminated) {
          stop = true;
          // We drained the whole mailbox into `batch` but are terminating now.
          // Delete the messages we won't process, or they leak (the batch vector
          // only drops the pointers). Old one-at-a-time pop() never pre-fetched
          // these, so the batch change would otherwise leak every co-drained
          // message that followed a shutdown/terminate.
          for (size_t k = i + 1; k < batch.size(); ++k)
            delete batch[k];
          break;
        }
      }
    }
    if (stop)
      break;
  }

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
  msgq->push(m);
}

std::size_t Actor::queue_length() const noexcept
{
  return msgq->length();
}

const Message* Actor::peek() const
{
  return msgq->peek();
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
