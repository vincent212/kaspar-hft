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
 *                           thread waits for the reply and returns it.
 *
 * FPGA -> CPU. The bridge thread reads everything the card sends and routes it:
 *   SEND        rebuilt as a Kaspar message and delivered with target->send(),
 *               with a stand-in for the FPGA actor as the sender, so the CPU
 *               handler's reply() goes back to the FPGA actor
 *   FAST        an FPGA actor's fast_send: the bridge thread calls
 *               target->fast_send() (the CPU handler runs on the bridge thread)
 *               and writes the reply back to the card, where the FPGA actor waits
 *   FAST_REPLY  the answer to a CPU fast_send: handed to the waiting thread
 *   Error       reported through on_error
 *
 * Addressing. FPGA actors and CPU actors share one id space. A CPU actor that
 * FPGA actors may address, or that expects replies from them, is registered
 * with add_cpu_actor(id, actor); the card's route table sends those ids to the
 * host.
 *
 * Messages. Each message that crosses is a Kaspar message class with the same
 * id and fields as its FPGA struct, e.g.
 *   struct Ping : actors::Message_N<301> { uint32_t count = 0; KFPGA_FIELDS(count) };
 * and is registered once: register_messages<Ping, Pong>().
 *
 * Deadlock: fast_send is a blocking call in both directions. A cycle of
 * fast_sends that returns to an actor already waiting deadlocks, exactly as a
 * fast_send cycle does inside the CPU runtime.
 */

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <functional>
#include <map>
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
  explicit FpgaBridge(std::shared_ptr<CardTransport> card) : card_(std::move(card))
  {
    on_error = [](const std::string &what) { std::fprintf(stderr, "FpgaBridge: %s\n", what.c_str()); };
  }

  ~FpgaBridge() override { stop(); }

  // Where problems are reported (default: stderr). Nothing is dropped silently.
  std::function<void(const std::string &)> on_error;

  // Kaspar message classes that cross to or from the card.
  template <class... M>
  void register_messages()
  {
    int dummy[] = {0, (register_one<M>(), 0)...};
    (void)dummy;
  }

  // A CPU actor reachable from the card under `id`.
  void add_cpu_actor(ActorId id, actors::Actor *a)
  {
    std::lock_guard<std::mutex> lk(map_mu_);
    cpu_by_id_[id] = a;
    id_by_cpu_[a] = id;
  }

  // An ActorRef for an FPGA actor. The bridge must be owned by a shared_ptr.
  actors::ActorRef ref(ActorId fpga_actor)
  {
    return actors::ActorRef(actors::FpgaActorRef(fpga_actor, shared_from_this()));
  }

  void start()
  {
    stop_ = false;
    thread_ = std::thread([this] { run(); });
  }

  void stop()
  {
    stop_ = true;
    if (thread_.joinable())
      thread_.join();
  }

  uint64_t errors() const { return errors_.load(); }

  // ---- actors::FpgaLink: CPU -> FPGA ------------------------------------------

  void send(uint16_t fpga_actor, const actors::Message *m, actors::Actor *sender) override
  {
    Envelope e;
    const bool ok = encode(m, fpga_actor, id_of(sender), SEND, e);
    delete m;   // send() owns the message, as a mailbox would
    if (ok)
      card_->write(e);
  }

  std::unique_ptr<const actors::Message> fast_send(uint16_t fpga_actor, const actors::Message *m,
                                                   actors::Actor *sender) override
  {
    if (std::this_thread::get_id() == thread_.get_id())
      throw std::logic_error("FpgaBridge: fast_send to the FPGA from a handler the FPGA called "
                             "with fast_send would wait on itself");
    Envelope e;
    if (!encode(m, fpga_actor, id_of(sender), FAST, e))
      throw std::runtime_error("FpgaBridge: cannot encode message id " +
                               std::to_string(m->get_message_id()));

    std::lock_guard<std::mutex> one_at_a_time(call_mu_);
    {
      std::lock_guard<std::mutex> lk(reply_mu_);
      waiting_ = true;
      reply_ready_ = false;
    }
    card_->write(e);
    Envelope r;
    {
      std::unique_lock<std::mutex> lk(reply_mu_);
      reply_cv_.wait(lk, [this] { return reply_ready_; });
      r = reply_;
      waiting_ = false;
    }
    if (r.id == 0)
      return nullptr;   // the FPGA actor did not reply
    if (r.id == Error::id)
    {
      Error err;
      unpack(r, err);
      throw std::runtime_error("FpgaBridge: fast_send to FPGA actor " + std::to_string(fpga_actor) +
                               " failed: " + describe(err));
    }
    return std::unique_ptr<const actors::Message>(decode(r));
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

  private:
    FpgaBridge *bridge_;
    ActorId id_;
  };

  struct Codec
  {
    bool (*encode)(const actors::Message *, ActorId dst, ActorId src, uint8_t kind, Envelope &);
    actors::Message *(*decode)(const Envelope &);
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
    std::lock_guard<std::mutex> lk(map_mu_);
    codecs_[static_cast<MsgId>(M::id)] = Codec{&encode_as<M>, &decode_as<M>};
  }

  bool encode(const actors::Message *m, ActorId dst, ActorId src, uint8_t kind, Envelope &e)
  {
    const Codec *c = codec(static_cast<MsgId>(m->get_message_id()));
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

  const Codec *codec(MsgId id)
  {
    std::lock_guard<std::mutex> lk(map_mu_);
    auto it = codecs_.find(id);
    return it == codecs_.end() ? nullptr : &it->second;
  }

  actors::Actor *cpu_actor(ActorId id)
  {
    std::lock_guard<std::mutex> lk(map_mu_);
    auto it = cpu_by_id_.find(id);
    return it == cpu_by_id_.end() ? nullptr : it->second;
  }

  ActorId id_of(actors::Actor *a)
  {
    if (!a)
      return kHost;
    std::lock_guard<std::mutex> lk(map_mu_);
    auto it = id_by_cpu_.find(a);
    return it == id_by_cpu_.end() ? kHost : it->second;
  }

  actors::Actor *proxy(ActorId fpga_actor)
  {
    std::lock_guard<std::mutex> lk(map_mu_);
    auto &p = proxies_[fpga_actor];
    if (!p)
      p.reset(new Proxy(this, fpga_actor));
    return p.get();
  }

  static std::string describe(const Error &err)
  {
    static const char *names[] = {"?", "no handler", "no route", "outbox full", "payload too big",
                                  "no codec"};
    const char *n = err.code < 6 ? names[err.code] : "?";
    return std::string(n) + " (actor " + std::to_string(err.actor) + ", message " +
           std::to_string(err.msg) + ", destination " + std::to_string(err.dst) + ")";
  }

  void report(const std::string &what)
  {
    ++errors_;
    on_error(what);
  }

  // Answer a waiting FPGA actor with an error, so it is not left blocked.
  void fail_call(const Envelope &req, uint32_t code)
  {
    Envelope err = make_error(code, req.dst, req.id, req.dst);
    err.kind = FAST_REPLY;
    err.dst = req.src;
    err.src = req.dst;
    card_->write(err);
  }

  // ---- the bridge thread: FPGA -> CPU --------------------------------------------

  void run()
  {
    Envelope e;
    while (!stop_.load(std::memory_order_relaxed))
    {
      if (card_->read(e))
        route(e);
      else
        std::this_thread::yield();
    }
  }

  void route(const Envelope &e)
  {
    if (e.kind == FAST_REPLY)
    {
      if (e.dst == kHost || cpu_actor(e.dst))
      {
        std::lock_guard<std::mutex> lk(reply_mu_);
        if (!waiting_)
        {
          ++errors_;
          on_error("reply from FPGA actor " + std::to_string(e.src) + " with no caller waiting");
          return;
        }
        reply_ = e;
        reply_ready_ = true;
        reply_cv_.notify_one();
      }
      else
      {
        card_->write(e);   // answer to an FPGA actor's call to another FPGA actor: relay
      }
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
      if (!target)
      {
        if (e.dst == kHost)
          fail_call(e, ERR_NO_ROUTE);
        else
          card_->write(e);   // a call from one FPGA actor to another: relay it back
        return;
      }
      std::unique_ptr<actors::Message> req(decode(e));
      if (!req)
      {
        fail_call(e, ERR_NO_CODEC);
        return;
      }
      auto rep = target->fast_send(req.get(), proxy(e.src));   // CPU handler runs here
      Envelope out;
      if (!rep)
      {
        out = {};
        out.dst = e.src;
        out.src = e.dst;
        out.kind = FAST_REPLY;
      }
      else if (!encode(rep.get(), e.src, e.dst, FAST_REPLY, out))
      {
        fail_call(e, ERR_NO_CODEC);
        return;
      }
      card_->write(out);
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

  std::shared_ptr<CardTransport> card_;
  std::thread thread_;
  std::atomic<bool> stop_{false};
  std::atomic<uint64_t> errors_{0};

  std::mutex map_mu_;
  std::map<MsgId, Codec> codecs_;
  std::map<ActorId, actors::Actor *> cpu_by_id_;
  std::map<actors::Actor *, ActorId> id_by_cpu_;
  std::map<ActorId, std::unique_ptr<Proxy>> proxies_;

  std::mutex call_mu_;   // one CPU -> FPGA fast_send at a time
  std::mutex reply_mu_;
  std::condition_variable reply_cv_;
  bool waiting_ = false;
  bool reply_ready_ = false;
  Envelope reply_{};
};

} // namespace kfpga
