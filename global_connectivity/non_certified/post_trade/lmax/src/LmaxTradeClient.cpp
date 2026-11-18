/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "lmax_post_trade/act/LmaxTradeClient.hpp"

#include <iostream>

#include "lmax_fix/LmaxFix.hpp"
#include "logger/act/Logger.hpp"

namespace lmax::pt
{
  using om::msg::LmaxReport;
  using post_trade::Applied;

  class LmaxTradeClient::Events : public om::SessionEvents
  {
  public:
    Events(LmaxTradeClient *c, uint64_t s) : c_(c), s_(s) {}
    void logged_on() override { post(new LmaxReport(LmaxReport::LOGGED_ON, s_)); }
    void logged_out(const std::string &t) override
    {
      auto r = new LmaxReport(LmaxReport::LOGGED_OUT, s_);
      r->text = t;
      post(r);
    }
    void exec(const om::ExecReport &e) override
    {
      auto r = new LmaxReport(LmaxReport::EXEC, s_);
      r->exec = e;
      post(r);
    }
    void cancel_reject(const om::CancelReject &) override {}
    void trade_capture(const om::TradeCaptureReport &t) override
    {
      auto r = new LmaxReport(LmaxReport::TRADE_CAPTURE, s_);
      r->tcr = t;
      post(r);
    }
    void tcr_ack(const om::TcrAck &a) override
    {
      auto r = new LmaxReport(LmaxReport::TCR_ACK, s_);
      r->ack = a;
      post(r);
    }
    void session_reject(const std::string &t) override
    {
      auto r = new LmaxReport(LmaxReport::SESSION_REJECT, s_);
      r->text = t;
      post(r);
    }
    void unparsed(const std::string &t) override
    {
      auto r = new LmaxReport(LmaxReport::UNPARSED, s_);
      r->text = t;
      post(r);
    }

  private:
    void post(LmaxReport *r) { c_->send(r, nullptr); }
    LmaxTradeClient *c_;
    uint64_t s_;
  };

  LmaxTradeClient::LmaxTradeClient(TradeClientConfig cfg, om::SessionFactory factory, actors::Actor *subscriber)
      : cfg_(std::move(cfg)), factory_(std::move(factory)), sub_(subscriber)
  {
    strncpy(name, "LmaxTradeClient", sizeof(name) - 1);
    MESSAGE_HANDLER(actors::msg::Start, start_handler);
    MESSAGE_HANDLER(actors::msg::Shutdown, shutdown_handler);
    MESSAGE_HANDLER(LmaxReport, report_handler);
    MESSAGE_HANDLER(om::msg::ExecCopy, copy_handler);
  }

  LmaxTradeClient::~LmaxTradeClient()
  {
    session_.reset();
    events_.reset();
  }

  void LmaxTradeClient::start_handler(const actors::msg::Start *) noexcept
  {
    if (!factory_)
      return; // ExecCopy input only
    ++session_no_;
    events_ = std::make_unique<Events>(this, session_no_);
    try
    {
      session_ = factory_(*events_);
      session_->start();
    }
    catch (const std::exception &e)
    {
      session_.reset();
      log_err("LMAX trade capture session not started: %s", e.what());
      std::cerr << "LmaxTradeClient: session not started: " << e.what() << std::endl;
    }
  }

  void LmaxTradeClient::shutdown_handler(const actors::msg::Shutdown *) noexcept
  {
    if (session_)
      session_->stop();
    session_.reset();
    events_.reset();
  }

  void LmaxTradeClient::apply(const post_trade::msg::TradeCapture &t) noexcept
  {
    auto res = ledger_.apply(t);
    switch (res.what)
    {
    case Applied::Duplicate: ++st_.duplicates; break;
    case Applied::Stale: ++st_.stale; break;
    case Applied::Orphan:
      ++st_.orphans;
      log_wrn("%s of trade %s not seen: kept as a tombstone", post_trade::to_string(t.event), t.trade_id.c_str());
      break;
    case Applied::Invalid:
      ++st_.invalid;
      log_err("trade report %s does not fit the ledger", t.report_id.c_str());
      break;
    default: break;
    }
    if (res.emit())
    {
      ++st_.emitted;
      if (sub_)
        sub_->send(new post_trade::msg::TradeCapture(res.out), this);
    }
  }

  void LmaxTradeClient::copy_handler(const om::msg::ExecCopy *m) noexcept
  {
    ++st_.execs;
    post_trade::msg::TradeCapture t;
    std::string err;
    if (!from_exec(m->exec, cfg_.map, t, err))
    {
      ++st_.unmapped;
      log_err("trade report not mapped: %s", err.c_str());
      return;
    }
    apply(t);
  }

  void LmaxTradeClient::report_handler(const LmaxReport *r) noexcept
  {
    if (r->session != session_no_)
      return;
    switch (r->kind)
    {
    case LmaxReport::LOGGED_ON:
    {
      ++st_.logons;
      const uint64_t now = now_ ? now_() : fix::now_ns();
      const uint64_t day = trading_day_start_ns(now);
      if (day != day_start_)
      {
        if (day_start_)
          log_inf("new LMAX trading day: ledger cleared");
        ledger_.clear();
        day_start_ = day;
      }
      om::TcrReq q;
      q.req_id = "T" + std::to_string(next_req_++); // at most 16 characters
      q.account = cfg_.account;
      q.subscribe = cfg_.subscribe;
      q.from = utc_timestamp(day);
      if (session_ && session_->send_tcr_request(q) == om::CallStatus::Success)
        ++st_.requests;
      break;
    }
    case LmaxReport::LOGGED_OUT: break;
    case LmaxReport::TCR_ACK:
      ++st_.acks;
      if (r->ack.status != 0 || r->ack.result != 0)
      {
        ++st_.request_rejected;
        log_err("TradeCaptureReportRequest %s rejected: result %d status %d %s", r->ack.req_id.c_str(),
                r->ack.result, r->ack.status, r->ack.text.c_str());
      }
      break;
    case LmaxReport::TRADE_CAPTURE:
    {
      ++st_.tcr;
      post_trade::msg::TradeCapture t;
      std::string err;
      if (!from_tcr(r->tcr, cfg_.map, t, err))
      {
        ++st_.unmapped;
        log_err("TradeCaptureReport not mapped: %s", err.c_str());
        return;
      }
      apply(t);
      break;
    }
    case LmaxReport::EXEC:
    {
      // Execution reports on this session (orders placed on it, if any).
      if (r->exec.exec_type != 'F' && r->exec.exec_type != 'G' && r->exec.exec_type != 'H')
        return;
      ++st_.execs;
      post_trade::msg::TradeCapture t;
      std::string err;
      if (!from_exec(r->exec, cfg_.map, t, err))
      {
        ++st_.unmapped;
        log_err("trade report not mapped: %s", err.c_str());
        return;
      }
      apply(t);
      break;
    }
    case LmaxReport::SESSION_REJECT: log_err("session Reject from LMAX: %s", r->text.c_str()); break;
    case LmaxReport::UNPARSED:
      ++st_.unmapped;
      log_err("LMAX report not parsed: %s", r->text.c_str());
      break;
    default: break;
    }
  }
}
