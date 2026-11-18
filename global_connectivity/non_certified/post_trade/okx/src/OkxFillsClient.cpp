/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "okx_pt/act/OkxFillsClient.hpp"

#include <condition_variable>
#include <iostream>
#include <mutex>

#include "frame/ref/RefData.hpp"
#include "logger/act/Logger.hpp"
#include "okx_pt/SimFills.hpp"

namespace okx::pt
{
  using json = nlohmann::json;
  using msg::FillsReport;

  namespace
  {
    uint64_t epoch_ms()
    {
      using namespace std::chrono;
      return uint64_t(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
    }

    uint64_t fill_ms(const json &f)
    {
      for (const char *k : {"fillTime", "ts"})
        if (f.contains(k) && f[k].is_string() && !f[k].get<std::string>().empty())
          return std::stoull(f[k].get<std::string>());
      return 0;
    }
  }

  class OkxFillsClient::Events : public FillsEvents
  {
  public:
    Events(OkxFillsClient *c, uint64_t s) : c_(c), s_(s) {}
    void logged_in() override { post(new FillsReport(FillsReport::LOGGED_IN, s_)); }
    void login_rejected(const std::string &code, const std::string &m) override
    {
      auto r = new FillsReport(FillsReport::LOGIN_REJECTED, s_);
      r->text = code + " " + m;
      post(r);
    }
    void disconnected(const std::string &why) override
    {
      auto r = new FillsReport(FillsReport::DISCONNECTED, s_);
      r->text = why;
      post(r);
    }
    void fill(const json &f, bool from_rest) override
    {
      auto r = new FillsReport(FillsReport::FILL, s_);
      r->body = f;
      r->from_rest = from_rest;
      post(r);
    }
    void reconciled(size_t rows, bool ok, const std::string &err) override
    {
      auto r = new FillsReport(FillsReport::RECONCILED, s_);
      r->rows = rows;
      r->ok = ok;
      r->text = err;
      post(r);
    }

  private:
    void post(FillsReport *r) { c_->send(r, nullptr); }
    OkxFillsClient *c_;
    uint64_t s_;
  };

  OkxFillsClient::OkxFillsClient(FillsConfig cfg, actors::Actor *subscriber, FillsSessionFactory factory)
      : cfg_(std::move(cfg)), sub_(subscriber), factory_(std::move(factory))
  {
    strncpy(name, "OkxFillsClient", sizeof(name) - 1);
    MESSAGE_HANDLER(actors::msg::Start, start_handler);
    MESSAGE_HANDLER(actors::msg::Shutdown, shutdown_handler);
    MESSAGE_HANDLER(FillsReport, report_handler);
  }

  OkxFillsClient::~OkxFillsClient()
  {
    stopping_ = true;
    reconnect_timer_ = {};
    reconcile_timer_ = {};
    drop();
  }

  void OkxFillsClient::connect() noexcept
  {
    ++session_no_;
    events_ = std::make_unique<Events>(this, session_no_);
    try
    {
      session_ = factory_(cfg_, *events_);
      ++sessions_created_;
      session_->logon();
    }
    catch (const std::exception &e)
    {
      session_.reset();
      log_err("cannot create the OKX fills session: %s", e.what());
      schedule_reconnect();
    }
  }

  void OkxFillsClient::drop() noexcept
  {
    ready_ = false;
    in_pass_ = false;
    session_.reset();
    events_.reset();
  }

  void OkxFillsClient::schedule_reconnect() noexcept
  {
    if (stopping_ || refused_)
      return;
    const auto expected = session_no_;
    const auto ms = cfg_.reconnect_ms;
    reconnect_timer_ = std::jthread([this, expected, ms](std::stop_token st) {
      std::mutex m;
      std::condition_variable_any cv;
      std::unique_lock<std::mutex> l(m);
      if (!cv.wait_for(l, st, std::chrono::milliseconds(ms), [] { return false; }) && !st.stop_requested())
        send(new FillsReport(FillsReport::RECONNECT, expected), nullptr);
    });
  }

  void OkxFillsClient::start_handler(const actors::msg::Start *) noexcept
  {
    if (!factory_)
    {
      log_err("no OKX fills session factory");
      return;
    }
    send(new FillsReport(FillsReport::RECONNECT, session_no_), nullptr);
    if (cfg_.reconcile_every_s > 0)
    {
      const int every = cfg_.reconcile_every_s;
      reconcile_timer_ = std::jthread([this, every](std::stop_token st) {
        std::mutex m;
        std::condition_variable_any cv;
        std::unique_lock<std::mutex> l(m);
        while (!cv.wait_for(l, st, std::chrono::seconds(every), [] { return false; }) && !st.stop_requested())
          send(new FillsReport(FillsReport::RECONCILE, 0), nullptr);
      });
    }
  }

  void OkxFillsClient::shutdown_handler(const actors::msg::Shutdown *) noexcept
  {
    stopping_ = true;
    reconnect_timer_ = {};
    reconcile_timer_ = {};
    if (session_)
      session_->logout();
    drop();
  }

  void OkxFillsClient::reconcile() noexcept
  {
    if (!session_ || !ready_ || in_pass_)
      return;
    pass_end_ms_ = epoch_ms();
    pass_begin_ms_ = pass_end_ms_ - uint64_t(cfg_.reconcile_lookback_s) * 1000ull;
    pass_rest_.clear();
    if (session_->reconcile(pass_begin_ms_) == CallStatus::Success)
      in_pass_ = true;
  }

  void OkxFillsClient::report_handler(const FillsReport *m) noexcept
  {
    if (m->kind == FillsReport::RECONNECT)
    {
      if (!stopping_ && !refused_ && m->session == session_no_ && !session_)
        connect();
      return;
    }
    if (m->kind == FillsReport::RECONCILE)
      return reconcile();
    if (m->session != session_no_ || !session_)
      return;
    switch (m->kind)
    {
    case FillsReport::LOGGED_IN:
      ready_ = true;
      log_omg("OKX fills: logged in (%s channel); reconciling the last %d s", cfg_.channel, cfg_.reconcile_lookback_s);
      reconcile();
      return;
    case FillsReport::LOGIN_REJECTED:
      log_err("OKX fills: login rejected: %s; not retrying", m->text);
      refused_ = true;
      drop();
      return;
    case FillsReport::DISCONNECTED:
      log_err("OKX fills: connection lost: %s; reconnecting in %d ms", m->text, cfg_.reconnect_ms);
      drop();
      schedule_reconnect();
      return;
    case FillsReport::FILL: on_fill(m->body, m->from_rest); return;
    case FillsReport::RECONCILED:
    {
      in_pass_ = false;
      if (!m->ok)
      {
        ++st_.failed_passes;
        log_err("OKX fills: reconciliation failed: %s", m->text);
        return;
      }
      ++st_.passes;
      // WebSocket fills inside the pass's range that REST did not return.
      // The last 5 s are left out: REST may lag the WebSocket.
      uint64_t missing = 0;
      for (const auto &[id, ms] : ws_seen_)
        if (ms >= pass_begin_ms_ && ms + 5000 <= pass_end_ms_ && !pass_rest_.count(id))
        {
          ++missing;
          log_err("OKX fills: %s came on the WebSocket but not from REST", id);
        }
      st_.missing_from_rest += missing;
      log_omg("OKX fills: reconciliation read %zu REST rows; %lu fills the WebSocket had missed in total, %lu "
              "WebSocket fills missing from REST in this pass",
              m->rows, st_.added_by_rest, missing);
      return;
    }
    default: return;
    }
  }

  void OkxFillsClient::on_fill(const json &f, bool from_rest) noexcept
  {
    post_trade::msg::TradeCapture tc;
    std::string err;
    if (from_rest)
      ++st_.rest_rows;
    else
      ++st_.ws_fills;
    if (!to_trade_capture(f, cfg_, from_rest, tc, err))
    {
      if (err != "not a fill")
      {
        ++st_.unmapped;
        log_err("OKX fills: cannot map %s: %s: CHECK POSITIONS", f.dump(), err);
      }
      return;
    }
    if (from_rest)
      pass_rest_.insert(tc.trade_id);
    else
      ws_seen_[tc.trade_id] = fill_ms(f);
    auto r = ledger_.apply(tc);
    if (r.what == post_trade::Applied::Duplicate)
    {
      ++st_.duplicates;
      return;
    }
    if (!r.emit())
    {
      log_wrn("OKX fills: %s: %s", tc.trade_id, post_trade::to_string(r.what));
      return;
    }
    if (from_rest)
    {
      ++st_.added_by_rest;
      log_wrn("OKX fills: %s came from REST only (missed on the WebSocket)", tc.trade_id);
    }
    ++st_.emitted;
    if (sub_)
      sub_->send(new post_trade::msg::TradeCapture(r.out), this);
  }
}

actors::Actor *create_OkxFillsClient(okx::pt::FillsConfig cfg, actors::Actor *subscriber,
                                     okx::pt::FillsSessionFactory factory)
{
  std::string err;
  for (auto &i : cfg.instruments)
    if (!i.asset.empty())
    {
      auto a = frame::ref::RefData::get_asset(i.asset);
      i.sym = a ? int(a->id) : -1;
    }
  if (!cfg.valid(err))
  {
    std::cerr << "OkxFillsClient: not created, bad config: " << err << std::endl;
    return nullptr;
  }
  if (!factory)
    factory = cfg.sim ? okx::pt::SimFills::make_factory({}) : okx::pt::ws_fills_factory();
  return new okx::pt::OkxFillsClient(std::move(cfg), subscriber, std::move(factory));
}
