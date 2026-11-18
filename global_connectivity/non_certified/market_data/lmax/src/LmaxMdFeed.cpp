/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "lmax_md/act/LmaxMdFeed.hpp"

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>

#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include "enum/e_names.hpp"
#include "frame/mda/msg/Data.hpp"
#include "logger/act/Logger.hpp"
#include "quickfix/Application.h"
#include "quickfix/Session.h"

namespace lmax::md
{
  using msg::MdEvent;

  // ---- configuration -------------------------------------------------------------------

  bool MdFeedConfig::from_file(const std::string &path, MdFeedConfig &c, std::string &err)
  {
    try
    {
      boost::property_tree::ptree pt;
      boost::property_tree::read_ini(path, pt);
      if (!fix::parse_env(pt.get<std::string>("environment", ""), c.session.env))
        return err = "environment must be loopback, demo or uat", false;
      c.session.host = pt.get<std::string>("host", "");
      c.session.port = pt.get<int>("port", 443);
      c.session.tls = pt.get<bool>("tls", true);
      c.session.heartbeat_s = pt.get<int>("heartbeat_s", 30);
      c.session.reconnect_s = pt.get<int>("reconnect_s", 5);
      c.session.dictionary = pt.get<std::string>("dictionary", "");
      c.session.log_path = pt.get<std::string>("log_path", "");
      c.session.reset_on_logon = true; // LMAX expects a reset on every market-data logon
      c.depth = pt.get<int>("depth", 20);
      c.trades = pt.get<bool>("trades", true);
      c.gap_timeout_ms = pt.get<int>("gap_timeout_ms", c.gap_timeout_ms);
      c.stall_ms = pt.get<int>("stall_ms", c.stall_ms);
      const std::string venue = pt.get<std::string>("venue", "");
      if (!venue.empty())
      {
        bool found = false;
        for (auto &[code, name] : en::x_name_hash)
          if (venue == name)
          {
            c.venue = char(code);
            found = true;
          }
        if (!found)
          return err = "venue '" + venue + "' is not an en::x code", false;
      }
      if (c.depth < 1 || c.depth > 20)
        return err = "depth must be 1 to 20 (LMAX's maximum)", false;
      if (!load_instruments(pt.get<std::string>("instruments", ""), c.instruments, err))
        return false;
      const std::string key = pt.get<std::string>("key_file", "");
      if (c.session.env == fix::Env::Loopback)
      {
        c.username = pt.get<std::string>("username", "");
        c.password = pt.get<std::string>("password", "");
        c.session.sender = pt.get<std::string>("sender_comp_id", "");
        c.session.target = pt.get<std::string>("target_comp_id", "");
      }
      else
      {
        fix::Credentials cr;
        if (!fix::load_credentials(key, cr, err))
          return false;
        if (cr.env != c.session.env)
          return err = std::string("key file is for ") + fix::to_string(cr.env) + ", config for " +
                       fix::to_string(c.session.env),
                 false;
        c.username = cr.username;
        c.password = cr.password;
        c.session.sender = cr.md_sender;
        c.session.target = cr.md_target;
      }
      std::string text;
      return fix::settings_text(c.session, text, err); // runs the host guard
    }
    catch (const std::exception &e)
    {
      return err = path + ": " + e.what(), false;
    }
  }

  // ---- QuickFIX thread -> mailbox --------------------------------------------------------

  class LmaxMdFeed::App : public FIX::Application
  {
  public:
    App(LmaxMdFeed *feed, std::string user, std::string pass)
        : feed_(feed), user_(std::move(user)), pass_(std::move(pass))
    {
    }
    void onCreate(const FIX::SessionID &) override {}
    void onLogon(const FIX::SessionID &) override { post(MdEvent::LOGON); }
    void onLogout(const FIX::SessionID &) override { post(MdEvent::LOGOUT); }

    void toAdmin(FIX::Message &m, const FIX::SessionID &) override
    {
      const std::string &type = m.getHeader().getField(35);
      if (type == "A")
      {
        m.setField(553, user_);
        m.setField(554, pass_);
      }
      else if (type == "2")
      {
        auto e = new MdEvent(MdEvent::RESEND_SENT);
        e->seq = std::strtoull(m.getField(7).c_str(), nullptr, 10);
        e->seq2 = std::strtoull(m.getField(16).c_str(), nullptr, 10);
        feed_->send(e, nullptr);
      }
    }
    void toApp(FIX::Message &, const FIX::SessionID &) override {}

    void fromAdmin(const FIX::Message &m, const FIX::SessionID &) override
    {
      seq(m);
      std::string type = m.getHeader().getField(35);
      if (type == "3" || type == "5")
      {
        auto e = new MdEvent(MdEvent::SESSION_REJECT);
        if (type == "5")
          e->kind = MdEvent::LOGOUT;
        e->text = m.isSetField(58) ? m.getField(58) : std::string();
        if (type == "3" && m.isSetField(373))
          e->text = "reason " + m.getField(373) + ": " + e->text;
        if (type == "5")
          e->seq = 1; // a Logout from LMAX, with its text; onLogout follows
        feed_->send(e, nullptr);
      }
    }

    // Never throws: an exception here would make QuickFIX reject the message
    // to LMAX.
    void fromApp(const FIX::Message &m, const FIX::SessionID &) override
    {
      const uint64_t now = fix::now_ns();
      try
      {
        seq(m);
        const std::string &type = m.getHeader().getField(35);
        std::string err;
        if (type == "W")
        {
          auto e = new MdEvent(MdEvent::SNAPSHOT);
          e->recv_ns = now;
          if (!parse_snapshot(m, e->snap, err))
          {
            e->kind = MdEvent::BAD_MESSAGE;
            e->text = err;
          }
          feed_->send(e, nullptr);
        }
        else if (type == "Y")
        {
          auto e = new MdEvent(MdEvent::MD_REJECT);
          if (!parse_md_reject(m, e->reject, err))
          {
            e->kind = MdEvent::BAD_MESSAGE;
            e->text = err;
          }
          feed_->send(e, nullptr);
        }
      }
      catch (const std::exception &ex)
      {
        auto e = new MdEvent(MdEvent::BAD_MESSAGE);
        e->text = ex.what();
        feed_->send(e, nullptr);
      }
    }

  private:
    void post(MdEvent::Kind k) { feed_->send(new MdEvent(k), nullptr); }
    void seq(const FIX::Message &m)
    {
      auto e = new MdEvent(MdEvent::SEQ);
      e->seq = std::strtoull(m.getHeader().getField(34).c_str(), nullptr, 10);
      e->text = m.getHeader().getField(35);
      e->possdup = m.getHeader().isSetField(43) && m.getHeader().getField(43) == "Y";
      e->next = e->seq + 1;
      if (e->text == "4" && m.isSetField(36))
        e->next = std::strtoull(m.getField(36).c_str(), nullptr, 10);
      feed_->send(e, nullptr);
    }
    LmaxMdFeed *feed_;
    std::string user_, pass_;
  };

  // ---- lifecycle -----------------------------------------------------------------------------

  LmaxMdFeed::LmaxMdFeed(MdFeedConfig cfg, std::vector<actors::Actor *> obs, MdTap *tap)
      : cfg_(std::move(cfg)), obs_(std::move(obs)), tap_(tap)
  {
    strncpy(name, "LmaxMdFeed", sizeof(name) - 1);
    obs_.resize(cfg_.instruments.size(), nullptr);
    for (size_t i = 0; i < cfg_.instruments.size(); ++i)
    {
      const auto &in = cfg_.instruments[i];
      chunks_.push_back(std::make_unique<sim::Mbp2Mbo>(
          sim::Mbp2Mbo::Config{in.sec_id, cfg_.venue, in.tick, in.chunk},
          [this, i](const bfile::l3_t &r) { emit(i, r); }));
      syncs_.push_back(std::make_unique<LmaxMdSync>(
          in, *chunks_.back(), size_t(cfg_.depth), [this, i](const bfile::l3_t &r) { emit(i, r); }, cfg_.venue));
    }
    MESSAGE_HANDLER(actors::msg::Start, start_handler);
    MESSAGE_HANDLER(actors::msg::Shutdown, shutdown_handler);
    MESSAGE_HANDLER(MdEvent, event_handler);
  }

  LmaxMdFeed::~LmaxMdFeed()
  {
    ticker_ = {};
    fix_.reset();
  }

  void LmaxMdFeed::emit(size_t i, const bfile::l3_t &r) noexcept
  {
    ++c_.records;
    if (tap_)
      tap_->on_record(i, r);
    if (obs_[i])
    {
      auto m = new frame::mda::msg::Data(true);
      m->l3 = r;
      obs_[i]->send(m, this);
    }
  }

  void LmaxMdFeed::start_handler(const actors::msg::Start *) noexcept
  {
    try
    {
      app_ = std::make_unique<App>(this, cfg_.username, cfg_.password);
      fix_ = std::make_unique<fix::FixInitiator>(cfg_.session, *app_);
      fix_->start();
      log_inf("LMAX market data: connecting to %s:%d (%s)", cfg_.session.host.c_str(), cfg_.session.port,
              fix::to_string(cfg_.session.env));
    }
    catch (const std::exception &e)
    {
      log_err("LMAX market data not started: %s", e.what());
      std::cerr << "LmaxMdFeed: not started: " << e.what() << std::endl;
      if (tap_)
        tap_->on_session(std::string("not started: ") + e.what());
      return;
    }
    ticker_ = std::jthread([this](std::stop_token st) {
      std::mutex m;
      std::condition_variable_any cv;
      std::unique_lock<std::mutex> l(m);
      while (!st.stop_requested())
      {
        cv.wait_for(l, st, std::chrono::milliseconds(200), [] { return false; });
        if (!st.stop_requested())
          send(new MdEvent(MdEvent::TICK), nullptr);
      }
    });
  }

  void LmaxMdFeed::shutdown_handler(const actors::msg::Shutdown *) noexcept
  {
    ticker_ = {};
    if (fix_)
      fix_->stop();
  }

  // ---- events --------------------------------------------------------------------------------

  void LmaxMdFeed::subscribe() noexcept
  {
    for (auto &in : cfg_.instruments)
    {
      const std::string id = md_req_id(next_req_++);
      if (!fix_->send(md_request(id, in.security_id, cfg_.depth, cfg_.trades)))
        log_err("MarketDataRequest for %s not sent", in.symbol.c_str());
    }
  }

  void LmaxMdFeed::gap_to_books() noexcept
  {
    for (size_t i = 0; i < syncs_.size(); ++i)
    {
      bfile::l3_gap_v2_t g{};
      g.typ = en::l3::GAP_V2;
      g.venue = cfg_.venue;
      emit(i, g);
    }
  }

  void LmaxMdFeed::reset_books() noexcept
  {
    for (auto &s : syncs_)
      s->reset();
  }

  void LmaxMdFeed::event_handler(const MdEvent *e) noexcept
  {
    const uint64_t now = fix::now_ns();
    switch (e->kind)
    {
    case MdEvent::LOGON:
      ++c_.logons;
      logged_on_ = true;
      logon_ns_ = last_w_ns_ = now;
      gap_deadline_ns_ = last_resend_ns_ = 0;
      log_inf("LMAX market data: logged on; subscribing %zu instruments", cfg_.instruments.size());
      if (tap_)
        tap_->on_session("logon");
      subscribe();
      break;
    case MdEvent::LOGOUT:
      if (e->seq == 1) // LMAX's Logout message, with its text
      {
        log_wrn("LMAX market data: logout from LMAX: %s", e->text.c_str());
        if (tap_)
          tap_->on_session("logout from LMAX: " + e->text);
        break;
      }
      ++c_.logouts;
      logged_on_ = false;
      gap_deadline_ns_ = 0;
      next_seq_ = 0;
      gap_to_books();
      reset_books();
      if (tap_)
        tap_->on_session("logout");
      break;
    case MdEvent::SEQ:
      ++c_.messages;
      // A Logon restarts the count (141=Y); every other message QuickFIX
      // delivers must carry the number expected, a SequenceReset moving the
      // expectation to its NewSeqNo.
      if (e->text != "A" && !e->possdup && next_seq_ != 0 && e->seq != next_seq_)
        ++c_.seq_breaks;
      if (e->text == "A" || e->next > next_seq_)
        next_seq_ = e->next;
      break;
    case MdEvent::SNAPSHOT:
    {
      last_w_ns_ = now;
      for (size_t i = 0; i < syncs_.size(); ++i)
        if (syncs_[i]->instrument().security_id == e->snap.security_id)
        {
          const uint64_t tx = e->snap.sending_ns ? e->snap.sending_ns : e->recv_ns;
          syncs_[i]->on_snapshot(e->snap, tx);
          if (tap_)
            tap_->on_snapshot(i, e->snap, *syncs_[i]);
          break;
        }
      break;
    }
    case MdEvent::MD_REJECT:
      ++c_.md_rejects;
      log_err("MarketDataRequestReject %s: reason %c %s", e->reject.md_req_id.c_str(),
              e->reject.reason ? e->reject.reason : '?', e->reject.text.c_str());
      if (tap_)
        tap_->on_session("MarketDataRequestReject " + e->reject.md_req_id + ": " + e->reject.text);
      break;
    case MdEvent::SESSION_REJECT:
      ++c_.session_rejects;
      log_err("session Reject from LMAX: %s", e->text.c_str());
      if (tap_)
        tap_->on_session("session reject: " + e->text);
      break;
    case MdEvent::BAD_MESSAGE:
      ++c_.bad_messages;
      log_err("LMAX market data message not parsed: %s", e->text.c_str());
      break;
    case MdEvent::RESEND_SENT:
      ++c_.resend_requests;
      log_wrn("MsgSeqNum gap: ResendRequest %lu..%lu sent; LMAX does not recover market data",
              (unsigned long)e->seq, (unsigned long)e->seq2);
      if (tap_)
        tap_->on_session("gap: ResendRequest " + std::to_string(e->seq) + ".." + std::to_string(e->seq2));
      // QuickFIX may send a new ResendRequest for later messages while the
      // gap is open: the gap is timed from the first one.
      last_resend_ns_ = now;
      if (!gap_deadline_ns_)
      {
        gap_to_books();
        gap_deadline_ns_ = now + 2 * uint64_t(cfg_.gap_timeout_ms) * 1000000ULL;
      }
      break;
    case MdEvent::TICK:
      if (!fix_ || !logged_on_)
        break;
      // A gap is closed once books have arrived and no ResendRequest has gone
      // out for gap_timeout_ms; one still open at its deadline (twice that
      // from the first ResendRequest) forces a reconnect.
      if (gap_deadline_ns_ && last_w_ns_ > last_resend_ns_ &&
          now - last_resend_ns_ > uint64_t(cfg_.gap_timeout_ms) * 1000000ULL)
      {
        gap_deadline_ns_ = 0;
        if (tap_)
          tap_->on_session("gap closed");
      }
      if ((gap_deadline_ns_ && now > gap_deadline_ns_) ||
          (cfg_.stall_ms > 0 && last_w_ns_ && now - last_w_ns_ > uint64_t(cfg_.stall_ms) * 1000000ULL))
      {
        ++c_.forced_reconnects;
        log_wrn("LMAX market data: %s; reconnecting", gap_deadline_ns_ ? "gap not filled" : "no book");
        if (tap_)
          tap_->on_session(gap_deadline_ns_ ? "gap not filled: reconnecting" : "stalled: reconnecting");
        gap_deadline_ns_ = 0;
        last_w_ns_ = 0;
        fix_->reconnect();
      }
      break;
    }
  }
}

actors::Actor *create_LmaxMdFeed(const std::string &config_path, std::vector<actors::Actor *> obs)
{
  lmax::md::MdFeedConfig cfg;
  std::string err;
  if (!lmax::md::MdFeedConfig::from_file(config_path, cfg, err))
  {
    std::cerr << "create_LmaxMdFeed: " << err << std::endl;
    return nullptr;
  }
  return new lmax::md::LmaxMdFeed(std::move(cfg), std::move(obs));
}
