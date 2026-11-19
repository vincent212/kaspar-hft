/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "kraken_pt/act/KrakenFillsClient.hpp"

#include "kraken_pt/if/KrakenFillsClient.hpp"

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <iostream>
#include <mutex>

#include "logger/act/Logger.hpp"

namespace kraken::pt
{
  using msg::FillsEvent;

  namespace
  {
    uint64_t now_ns()
    {
      using namespace std::chrono;
      return uint64_t(duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count());
    }

    // A thread that sleeps `ms` (or until stopped) and then posts `make()`.
    template <class F>
    std::jthread timer(int ms, F post)
    {
      return std::jthread([ms, post](std::stop_token st) {
        std::mutex m;
        std::condition_variable_any cv;
        std::unique_lock<std::mutex> l(m);
        if (!cv.wait_for(l, st, std::chrono::milliseconds(ms), [] { return false; }) && !st.stop_requested())
          post();
      });
    }
  }

  class KrakenFillsClient::Events : public FillsEvents
  {
  public:
    Events(KrakenFillsClient *c, uint64_t s) : c_(c), s_(s) {}
    void logged_in() override { post(new FillsEvent(FillsEvent::LOGGED_IN, s_)); }
    void login_failed(const std::string &why) override { post(text(FillsEvent::LOGIN_FAILED, why)); }
    void disconnected(const std::string &why) override { post(text(FillsEvent::DISCONNECTED, why)); }
    void fill(const nlohmann::json &f, const std::string &key, Source src) override
    {
      auto e = new FillsEvent(FillsEvent::FILL, s_);
      e->body = f;
      e->key = key;
      e->src = src;
      post(e);
    }
    void reconciled(size_t rows, bool ok, const std::string &err) override
    {
      auto e = text(FillsEvent::RECONCILED, err);
      e->rows = rows;
      e->ok = ok;
      post(e);
    }

  private:
    FillsEvent *text(FillsEvent::Kind k, const std::string &t)
    {
      auto e = new FillsEvent(k, s_);
      e->text = t;
      return e;
    }
    void post(FillsEvent *e) { c_->send(e, nullptr); }
    KrakenFillsClient *c_;
    uint64_t s_;
  };

  KrakenFillsClient::KrakenFillsClient(FillsConfig cfg, actors::Actor *subscriber, FillsSessionFactory factory)
      : cfg_(std::move(cfg)), sub_(subscriber), factory_(std::move(factory))
  {
    strncpy(name, "KrakenFillsClient", sizeof(name) - 1);
    MESSAGE_HANDLER(actors::msg::Start, start_handler);
    MESSAGE_HANDLER(actors::msg::Shutdown, shutdown_handler);
    MESSAGE_HANDLER(FillsEvent, event_handler);
  }

  KrakenFillsClient::~KrakenFillsClient()
  {
    stopping_ = true;
    reconnect_timer_ = {};
    reconcile_timer_ = {};
    drop();
  }

  void KrakenFillsClient::connect() noexcept
  {
    ++session_no_;
    events_ = std::make_unique<Events>(this, session_no_);
    try
    {
      session_ = factory_(*events_);
      ++sessions_created_;
      session_->logon();
    }
    catch (const std::exception &e)
    {
      session_.reset();
      log_err("Kraken fills: cannot create a session: %s", e.what());
      schedule_reconnect();
    }
  }

  void KrakenFillsClient::drop() noexcept
  {
    ready_ = false;
    in_pass_ = false;
    session_.reset();
    events_.reset();
  }

  void KrakenFillsClient::schedule_reconnect() noexcept
  {
    if (stopping_ || refused_)
      return;
    auto s = session_no_;
    reconnect_timer_ = timer(cfg_.reconnect_ms, [this, s] { send(new FillsEvent(FillsEvent::RECONNECT, s), nullptr); });
  }

  void KrakenFillsClient::start_handler(const actors::msg::Start *) noexcept
  {
    send(new FillsEvent(FillsEvent::RECONNECT, session_no_), nullptr);
  }

  void KrakenFillsClient::shutdown_handler(const actors::msg::Shutdown *) noexcept
  {
    stopping_ = true;
    reconnect_timer_ = {};
    reconcile_timer_ = {};
    if (session_)
      session_->logout();
    drop();
    log_omg("Kraken fills: %lu stream fills, %lu REST rows, %lu emitted, %lu duplicates, %lu unmapped, %lu added by "
            "REST, %lu missing from REST, %lu passes (%lu failed)",
            st_.stream_fills, st_.rest_rows, st_.emitted, st_.duplicates, st_.unmapped, st_.added_by_rest,
            st_.missing_from_rest, st_.passes, st_.failed_passes);
  }

  void KrakenFillsClient::reconcile() noexcept
  {
    if (!session_ || !ready_ || in_pass_)
      return;
    pass_end_ns_ = now_ns();
    pass_start_ns_ = pass_end_ns_ - uint64_t(cfg_.reconcile_lookback_s) * 1'000'000'000ull;
    pass_rest_.clear();
    if (session_->reconcile(pass_start_ns_))
      in_pass_ = true;
  }

  void KrakenFillsClient::on_fill(const FillsEvent &e) noexcept
  {
    const bool rest = from_rest(e.src);
    ++(rest ? st_.rest_rows : st_.stream_fills);
    post_trade::msg::TradeCapture t;
    std::string err;
    if (!to_trade_capture(e.body, e.key, e.src, cfg_, t, err))
    {
      ++st_.unmapped;
      log_err("Kraken fill not mapped (%s): %s", err, e.body.dump());
      return;
    }
    if (rest && in_pass_)
      pass_rest_.insert(t.trade_id);
    if (!rest)
    {
      // Fill time as ns since the epoch, for the missing-from-REST check.
      struct tm tm{};
      tm.tm_year = int(t.trade_date / 10000) - 1900;
      tm.tm_mon = int(t.trade_date / 100 % 100) - 1;
      tm.tm_mday = int(t.trade_date % 100);
      stream_seen_[t.trade_id] = uint64_t(timegm(&tm)) * 1'000'000'000ull + uint64_t(t.trade_time_ns);
    }
    auto r = ledger_.apply(t);
    if (r.emit())
    {
      ++st_.emitted;
      if (rest)
      {
        ++st_.added_by_rest;
        log_wrn("Kraken fill %s %s %ld @ %s came from REST only: the stream missed it", t.trade_id, t.symbol, t.qty,
                std::to_string(t.price()));
      }
      if (sub_)
        sub_->send(new post_trade::msg::TradeCapture(r.out), this);
    }
    else if (r.what == post_trade::Applied::Duplicate)
      ++st_.duplicates;
    else
      log_err("Kraken fill %s: ledger says %s", t.trade_id, post_trade::to_string(r.what));
  }

  void KrakenFillsClient::event_handler(const FillsEvent *m) noexcept
  {
    if (m->kind == FillsEvent::RECONNECT)
    {
      if (!stopping_ && !refused_ && !session_ && m->session == session_no_)
        connect();
      return;
    }
    if (m->kind == FillsEvent::RECONCILE)
    {
      reconcile();
      if (cfg_.reconcile_every_s > 0 && !stopping_)
        reconcile_timer_ = timer(cfg_.reconcile_every_s * 1000,
                                 [this] { send(new FillsEvent(FillsEvent::RECONCILE, 0), nullptr); });
      return;
    }
    if (m->session != session_no_ || !session_)
      return;
    switch (m->kind)
    {
    case FillsEvent::LOGGED_IN:
      ready_ = true;
      log_omg("Kraken fills: logged on (session %lu)", session_no_);
      send(new FillsEvent(FillsEvent::RECONCILE, 0), nullptr);
      return;
    case FillsEvent::LOGIN_FAILED:
      log_err("Kraken fills: logon refused: %s. Not retried.", m->text);
      refused_ = true;
      drop();
      return;
    case FillsEvent::DISCONNECTED:
      log_err("Kraken fills: connection lost: %s", m->text);
      drop();
      schedule_reconnect();
      return;
    case FillsEvent::FILL: on_fill(*m); return;
    case FillsEvent::RECONCILED:
    {
      in_pass_ = false;
      ++st_.passes;
      if (!m->ok)
      {
        ++st_.failed_passes;
        log_err("Kraken fills: reconciliation failed after %zu rows: %s", m->rows, m->text);
        return;
      }
      for (const auto &[id, ns] : stream_seen_)
        // A fill in the last 5 s before the request may not be in REST yet.
        if (ns >= pass_start_ns_ && ns + 5'000'000'000ull <= pass_end_ns_ && !pass_rest_.count(id))
        {
          ++st_.missing_from_rest;
          log_err("Kraken fill %s from the stream is not in the REST history", id);
        }
      log_omg("Kraken fills: reconciliation read %zu rows; %lu added by REST so far", m->rows, st_.added_by_rest);
      return;
    }
    default: return;
    }
  }
}

actors::Actor *create_KrakenFillsClient(kraken::pt::FillsConfig cfg, actors::Actor *subscriber,
                                        kraken::pt::FillsSessionFactory factory)
{
  std::string err;
  if (!cfg.valid(err))
  {
    std::cerr << "KrakenFillsClient: not created: " << err << std::endl;
    return nullptr;
  }
  if (!factory)
  {
    std::cerr << "KrakenFillsClient: not created: no session factory" << std::endl;
    return nullptr;
  }
  return new kraken::pt::KrakenFillsClient(std::move(cfg), subscriber, std::move(factory));
}
