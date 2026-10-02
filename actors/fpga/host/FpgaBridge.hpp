#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

/*
 * FpgaBridge -- joins the CPU actor runtime (actors/cpp) to an FPGA actor runtime.
 *
 * CPU -> FPGA. A CPU actor holds an ActorRef to an FPGA actor (bridge->ref(id)).
 *   ref.send(m, this)       m is encoded and written to the card; the calling
 *                           thread returns at once. A reply comes back to this
 *                           actor's mailbox.
 *   ref.fast_send(&m, this) m is written to the card as a fast_send; the calling
 *                           thread waits for the reply and returns it. Several
 *                           threads may have calls outstanding at once.
 *
 * FPGA -> CPU. The reader thread reads everything the card sends and delivers it:
 *   SEND        rebuilt as a Kaspar message and delivered with target->send(),
 *               with a stand-in for the FPGA actor as the sender, so the CPU
 *               handler's reply() goes back to the FPGA actor
 *   FAST        an FPGA actor's fast_send: handed to that FPGA actor's worker
 *               thread, which calls target->fast_send() (the CPU handler runs on
 *               the worker) and writes the reply back to the card, where the FPGA
 *               actor waits
 *   FAST_REPLY  the answer to a CPU fast_send: handed to the waiting thread,
 *               found by the tag the request carried
 *   Error       reported through on_error
 *
 * The reader thread never runs a CPU handler, so it is always free to deliver
 * replies. Each calling FPGA actor has its own worker (an FPGA actor has at most
 * one fast_send outstanding), so one CPU handler that is waiting does not hold up
 * calls from other FPGA actors.
 *
 * Addressing. FPGA actors and CPU actors share one id space, 1..kMaxActors-1
 * (0 is the host itself: messages from an unregistered CPU sender carry it). A
 * CPU actor that FPGA actors may address, or that expects replies from them, is
 * registered with add_cpu_actor(id, actor); the card's discovery table sends those
 * ids to the host.
 *
 * Messages. Each message that crosses is a Kaspar message class with the same
 * id and fields as its FPGA struct, e.g.
 *   struct Ping : actors::Message_N<301> { uint32_t count = 0; KFPGA_FIELDS(count) };
 * and is registered once, before start(): register_messages<Ping, Pong>().
 *
 * Failures. A fast_send from the CPU that fails (no route, no handler, no codec,
 * an error from the FPGA, a bridge that is not running) throws. Called from a
 * handler, that ends the process: failures are never silent. A send while the
 * bridge is not running is reported and dropped. A card write that fails on one
 * of the bridge's own threads is reported and aborts the process: the FPGA actor
 * waiting on it could never be released. stop() finishes the CPU handlers already
 * running for FPGA actors, answers new calls from the FPGA with an error, and
 * releases CPU callers still waiting, with no reply, reporting each one.
 *
 * Deadlock: fast_send is a blocking call in both directions. A cycle of
 * fast_sends that returns to an actor already waiting deadlocks, exactly as a
 * fast_send cycle does inside the CPU runtime.
 */

#include <array>
#include <atomic>
#include <unordered_map>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

#include "actors/Actor.hpp"
#include "actors/ActorRef.hpp"

#include "CardTransport.hpp"
#include "actors_fpga/envelope.hpp"

namespace kfpga {

class FpgaBridge : public actors::FpgaLink, public std::enable_shared_from_this<FpgaBridge>
{
public:
  // Message ids that may cross: Message_N ids are below 512.
  static constexpr int kMaxMsgId = 512;

  explicit FpgaBridge(std::shared_ptr<CardTransport> card) : card_(std::move(card))
  {
    on_error = [](const std::string &what) { std::fprintf(stderr, "FpgaBridge: %s\n", what.c_str()); };
    for (int i = 0; i < kMaxActors; ++i)
    {
      proxies_[i].reset(new Proxy(this, static_cast<ActorId>(i)));
      cpu_by_id_[i].store(nullptr);
    }
  }

  ~FpgaBridge() override { stop(); }

  // Where problems are reported (default: stderr). Called from the bridge's
  // threads and from callers' threads, one call at a time. Nothing is dropped
  // silently.
  std::function<void(const std::string &)> on_error;

  // Kaspar message classes that cross to or from the card. Before start().
  template <class... M>
  void register_messages()
  {
    if (started_)
      throw std::logic_error("FpgaBridge: register_messages after start()");
    int dummy[] = {0, (register_one<M>(), 0)...};
    (void)dummy;
  }

  // A CPU actor reachable from the card under `id`.
  void add_cpu_actor(ActorId id, actors::Actor *a)
  {
    if (id == kHost)
      throw std::invalid_argument("FpgaBridge: actor id 0 is the host itself");
    if (id >= kMaxActors)
      throw std::out_of_range("FpgaBridge: actor id " + std::to_string(id) + " >= kMaxActors");
    cpu_by_id_[id].store(a, std::memory_order_release);
  }

  // An ActorRef for an FPGA actor. The bridge must be owned by a shared_ptr.
  actors::ActorRef ref(ActorId fpga_actor)
  {
    return actors::ActorRef(actors::FpgaActorRef(fpga_actor, shared_from_this()));
  }

  void start()
  {
    if (reader_.joinable())
      throw std::logic_error("FpgaBridge: start() called twice");
    started_ = true;
    stop_ = false;
    accepting_ = true;
    {
      std::lock_guard<std::mutex> lk(pending_mu_);
      running_ = true;
    }
    reader_ = std::thread([this] { run(); });
  }

  // Shuts the bridge down, in an order that never strands a handler mid-call:
  //  1. new fast_sends from the FPGA are answered with an error;
  //  2. the CPU handlers already queued or running for FPGA actors finish (the
  //     reader still delivers the replies they may be waiting for);
  //  3. the reader stops;
  //  4. CPU callers still waiting for a reply are released, with no reply, and
  //     each is reported.
  // A CPU handler that never returns cannot be joined.
  void stop()
  {
    accepting_ = false;
    for (auto &w : workers_)
    {
      if (!w)
        continue;
      {
        std::lock_guard<std::mutex> lk(w->mu);
        w->stop = true;
      }
      w->cv.notify_one();
      if (w->thread.joinable())
        w->thread.join();
      w.reset();
    }
    stop_ = true;
    if (reader_.joinable())
      reader_.join();
    {
      std::lock_guard<std::mutex> lk(pending_mu_);
      running_ = false;
      for (auto &p : pending_)
      {
        p.second->stopped = true;
        p.second->ready = true;
        p.second->cv.notify_one();
      }
      pending_.clear();
    }
  }

  uint64_t errors() const { return errors_.load(); }

  // ---- actors::FpgaLink: CPU -> FPGA ------------------------------------------

  void send(uint16_t fpga_actor, const actors::Message *m, actors::Actor *sender) override
  {
    if (!running_.load(std::memory_order_acquire))
    {
      report("send of message " + std::to_string(m->get_message_id()) + " to FPGA actor " +
             std::to_string(fpga_actor) + " while the bridge is not running; dropped");
      delete m;
      return;
    }
    Envelope e;
    const bool ok = encode(m, fpga_actor, id_of(sender), SEND, e);
    delete m;   // send() owns the message, as a mailbox would
    if (ok)
      write_or_abort(e, "send");
  }

  std::unique_ptr<const actors::Message> fast_send(uint16_t fpga_actor, const actors::Message *m,
                                                   actors::Actor *sender) override
  {
    if (fpga_actor >= kMaxActors)
      throw std::runtime_error("FpgaBridge: fast_send to actor id " + std::to_string(fpga_actor) +
                               ", outside 0.." + std::to_string(kMaxActors - 1));
    Envelope e;
    if (!encode(m, fpga_actor, id_of(sender), FAST, e))
      throw std::runtime_error("FpgaBridge: cannot encode message id " +
                               std::to_string(m->get_message_id()));

    // The reply carries the request's tag back; the waiter is found by tag. The
    // card write happens outside the lock, so a write that waits for room on the
    // card never stops the reader from delivering replies.
    Waiter w;
    e.tag = next_tag_.fetch_add(1, std::memory_order_relaxed);
    {
      std::lock_guard<std::mutex> lk(pending_mu_);
      if (!running_)
        throw std::runtime_error("FpgaBridge: fast_send to FPGA actor " +
                                 std::to_string(fpga_actor) + " while the bridge is not running");
      pending_[e.tag] = &w;
    }
    try
    {
      card_->write(e);
    }
    catch (...)
    {
      std::lock_guard<std::mutex> lk(pending_mu_);
      pending_.erase(e.tag);
      throw;
    }
    {
      std::unique_lock<std::mutex> lk(pending_mu_);
      w.cv.wait(lk, [&w] { return w.ready; });
    }

    if (w.stopped)
    {
      report("bridge stopped while a fast_send to FPGA actor " + std::to_string(fpga_actor) +
             " was waiting; it returns no reply");
      return nullptr;
    }
    const Envelope &r = w.reply;
    if (r.id == 0)
      return nullptr;   // the FPGA actor did not reply
    if (r.id == Error::id)
    {
      Error err;
      unpack(r, err);
      throw std::runtime_error("FpgaBridge: fast_send to FPGA actor " + std::to_string(fpga_actor) +
                               " failed: " + describe(err));
    }
    actors::Message *rep = decode(r);
    if (!rep)
      throw std::runtime_error("FpgaBridge: fast_send to FPGA actor " + std::to_string(fpga_actor) +
                               ": no registered class for reply id " + std::to_string(r.id));
    return std::unique_ptr<const actors::Message>(rep);
  }

  std::string name(uint16_t fpga_actor) const override
  {
    return "fpga:" + std::to_string(fpga_actor);
  }

private:
  // Stands in for an FPGA actor on the CPU side, so a CPU handler's reply() to a
  // message from the FPGA goes back to the card.
  class Proxy : public actors::Actor
  {
  public:
    Proxy(FpgaBridge *b, ActorId id) : bridge_(b), id_(id)
    {
      std::snprintf(name, sizeof(name), "fpga:%u", static_cast<unsigned>(id));
    }
    void send(const actors::Message *m, actors::Actor *sender = nullptr) noexcept override
    {
      bridge_->send(id_, m, sender);
    }

    // A CPU fast_send to this stand-in (Actor::fast_send finds no handler and
    // calls process_message): forward it to the FPGA actor and return its reply.
    //
    // This path goes through Actor::fast_send, so it holds this stand-in's lock
    // for the whole round trip: calls through one stand-in run one at a time, and
    // a failure, thrown inside Actor::fast_send (noexcept), ends the process. For
    // concurrent calls, or to catch a failure, use bridge->ref(id) instead.
    void process_message(const actors::Message *m) override
    {
      auto r = bridge_->fast_send(id_, m, m->sender);
      if (r)
        reply(r.release());
    }

  private:
    FpgaBridge *bridge_;
    ActorId id_;
  };

  struct Codec
  {
    bool (*encode)(const actors::Message *, ActorId dst, ActorId src, uint8_t kind, Envelope &) = nullptr;
    actors::Message *(*decode)(const Envelope &) = nullptr;
  };

  // A CPU thread waiting for the reply to its fast_send.
  struct Waiter
  {
    std::condition_variable cv;
    bool ready = false;
    bool stopped = false;   // released by stop(), not by a reply
    Envelope reply{};
  };

  // Runs CPU handlers for the fast_sends of one FPGA actor.
  struct Worker
  {
    std::thread thread;
    std::mutex mu;
    std::condition_variable cv;
    std::deque<Envelope> q;
    bool stop = false;

    void post(const Envelope &e)
    {
      {
        std::lock_guard<std::mutex> lk(mu);
        q.push_back(e);
      }
      cv.notify_one();
    }
  };

  template <class M>
  static bool encode_as(const actors::Message *m, ActorId dst, ActorId src, uint8_t kind, Envelope &e)
  {
    return pack(*static_cast<const M *>(m), dst, src, kind, e);
  }

  template <class M>
  static actors::Message *decode_as(const Envelope &e)
  {
    M *m = new M();
    unpack(e, *m);
    return m;
  }

  template <class M>
  void register_one()
  {
    static_assert(std::is_base_of<actors::Message, M>::value, "a Kaspar message class");
    static_assert(M::id > 15 && M::id < kMaxMsgId,
                  "message ids that cross must be 16..511: 0 means 'no reply' and 1..15 are "
                  "the FPGA runtime's own (Error is 1)");
    codecs_[M::id] = Codec{&encode_as<M>, &decode_as<M>};
  }

  // Codecs are fixed before start(), so they are read without a lock.
  const Codec *codec(int id) const
  {
    if (id < 0 || id >= kMaxMsgId || !codecs_[id].encode)
      return nullptr;
    return &codecs_[id];
  }

  bool encode(const actors::Message *m, ActorId dst, ActorId src, uint8_t kind, Envelope &e)
  {
    const Codec *c = codec(m->get_message_id());
    if (!c)
    {
      report("no registered class for message id " + std::to_string(m->get_message_id()));
      return false;
    }
    if (!c->encode(m, dst, src, kind, e))
    {
      report("message id " + std::to_string(m->get_message_id()) + " does not fit in an envelope");
      return false;
    }
    return true;
  }

  actors::Message *decode(const Envelope &e)
  {
    const Codec *c = codec(e.id);
    if (!c)
    {
      report("no registered class for message id " + std::to_string(e.id) + " from FPGA actor " +
             std::to_string(e.src));
      return nullptr;
    }
    return c->decode(e);
  }

  actors::Actor *cpu_actor(ActorId id) const
  {
    return id < kMaxActors ? cpu_by_id_[id].load(std::memory_order_acquire) : nullptr;
  }

  // The id a CPU sender is registered under: a scan of kMaxActors slots, no lock.
  ActorId id_of(actors::Actor *a) const
  {
    if (a)
      for (int i = 0; i < kMaxActors; ++i)
        if (cpu_by_id_[i].load(std::memory_order_acquire) == a)
          return static_cast<ActorId>(i);
    return kHost;
  }

  actors::Actor *proxy(ActorId fpga_actor) const
  {
    return fpga_actor < kMaxActors ? proxies_[fpga_actor].get() : nullptr;
  }

  static std::string describe(const Error &err)
  {
    static const char *names[] = {"?",
                                  "no handler",
                                  "no route",
                                  "?",
                                  "payload too big",
                                  "no codec",
                                  "wrong reply type",
                                  "fast_send to itself",
                                  "self queue full",
                                  "no such actor in that process",
                                  "the CPU side is stopping"};
    const char *n = err.code < 11 ? names[err.code] : "?";
    return std::string(n) + " (actor " + std::to_string(err.actor) + ", message " +
           std::to_string(err.msg) + ", destination " + std::to_string(err.dst) + ")";
  }

  void report(const std::string &what)
  {
    ++errors_;
    std::lock_guard<std::mutex> lk(report_mu_);
    on_error(what);
  }

  // A write from one of the bridge's own threads. If it fails, the FPGA actor
  // that would receive it may be waiting for it and could never be released, so
  // the failure is reported and the process ends.
  void write_or_abort(const Envelope &e, const char *what)
  {
    try
    {
      card_->write(e);
    }
    catch (const std::exception &ex)
    {
      report(std::string("card write failed (") + what + "): " + ex.what() + "; aborting");
      std::abort();
    }
    catch (...)
    {
      report(std::string("card write failed (") + what + "); aborting");
      std::abort();
    }
  }

  // ---- the reader thread: everything from the card ------------------------------

  void run()
  {
    Envelope e;
    while (!stop_.load(std::memory_order_relaxed))
    {
      if (card_->read(e))
        route(e);
      else
        std::this_thread::yield();   // the card is polled: CardTransport::read does not block
    }
  }

  void route(const Envelope &e)
  {
    if (e.kind == FAST_REPLY)
    {
      deliver_reply(e);
      return;
    }

    if (e.kind == SEND && e.id == Error::id)
    {
      Error err;
      unpack(e, err);
      report("FPGA error: " + describe(err));
      return;
    }

    actors::Actor *target = cpu_actor(e.dst);

    if (e.kind == FAST)
    {
      if (!target || e.src >= kMaxActors)
      {
        report("fast_send from FPGA actor " + std::to_string(e.src) + " to unknown CPU actor " +
               std::to_string(e.dst));
        write_or_abort(make_fast_reply_error(e, ERR_NO_ROUTE), "fast_send error reply");
        return;
      }
      if (!accepting_.load(std::memory_order_acquire))
      {
        report("fast_send from FPGA actor " + std::to_string(e.src) + " to CPU actor " +
               std::to_string(e.dst) + " while the bridge is stopping");
        write_or_abort(make_fast_reply_error(e, ERR_STOPPING), "fast_send error reply");
        return;
      }
      worker(e.src).post(e);
      return;
    }

    // SEND to a CPU actor
    if (!target)
    {
      report("message " + std::to_string(e.id) + " from FPGA actor " + std::to_string(e.src) +
             " to unknown CPU actor " + std::to_string(e.dst));
      return;
    }
    actors::Message *m = decode(e);
    if (m)
      target->send(m, proxy(e.src));
  }

  // A reply to a CPU fast_send: the caller waiting on its tag.
  void deliver_reply(const Envelope &e)
  {
    bool found = false;
    {
      std::lock_guard<std::mutex> lk(pending_mu_);
      auto it = pending_.find(e.tag);
      if (it != pending_.end())
      {
        Waiter *w = it->second;
        pending_.erase(it);
        w->reply = e;
        w->ready = true;
        w->cv.notify_one();
        found = true;
      }
    }
    if (!found)
      report("reply from FPGA actor " + std::to_string(e.src) + " with no caller waiting");
  }

  // ---- workers: CPU handlers called by FPGA actors ------------------------------

  // Created on first use, by the reader thread only.
  Worker &worker(ActorId fpga_actor)
  {
    auto &slot = workers_[fpga_actor];
    if (!slot)
    {
      slot.reset(new Worker());
      Worker *w = slot.get();
      w->thread = std::thread([this, w] { work(*w); });
    }
    return *slot;
  }

  void work(Worker &w)
  {
    for (;;)
    {
      Envelope e;
      {
        std::unique_lock<std::mutex> lk(w.mu);
        w.cv.wait(lk, [&w] { return w.stop || !w.q.empty(); });
        if (w.q.empty())
          return;
        e = w.q.front();
        w.q.pop_front();
      }
      serve_fast(e);
    }
  }

  // Run the CPU handler for an FPGA actor's fast_send and answer it.
  void serve_fast(const Envelope &e)
  {
    std::unique_ptr<actors::Message> req(decode(e));
    if (!req)
    {
      write_or_abort(make_fast_reply_error(e, ERR_NO_CODEC), "fast_send error reply");
      return;
    }
    auto rep = cpu_actor(e.dst)->fast_send(req.get(), proxy(e.src));   // CPU handler runs here
    Envelope out;
    if (!rep)
      out = make_no_reply(e);
    else if (!encode(rep.get(), e.src, e.dst, FAST_REPLY, out))
      out = make_fast_reply_error(e, ERR_NO_CODEC);
    out.tag = e.tag;
    write_or_abort(out, "fast_send reply");
  }

  std::shared_ptr<CardTransport> card_;
  std::thread reader_;
  std::atomic<bool> stop_{false};
  std::atomic<bool> accepting_{false};   // false: new fast_sends from the FPGA are refused
  bool started_ = false;
  std::atomic<uint64_t> errors_{0};
  std::mutex report_mu_;

  std::array<Codec, kMaxMsgId> codecs_{};
  std::array<std::atomic<actors::Actor *>, kMaxActors> cpu_by_id_;
  std::array<std::unique_ptr<Proxy>, kMaxActors> proxies_;
  std::array<std::unique_ptr<Worker>, kMaxActors> workers_;

  std::mutex pending_mu_;
  std::atomic<bool> running_{false};               // written under pending_mu_
  std::unordered_map<uint32_t, Waiter *> pending_;  // by tag
  std::atomic<uint32_t> next_tag_{1};
};

} // namespace kfpga
