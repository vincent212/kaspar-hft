/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "nasdaq_post_trade/act/NasdaqDropClient.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <sstream>

#include <boost/property_tree/info_parser.hpp>

#include "logger/act/Logger.hpp"
#include "nasdaq_post_trade/if/NasdaqDropClient.hpp"

namespace nasdaq_post_trade::act
{
  // ---------------------------------------------------------------- Forwarder
  // Session thread: no actor state is read or written here.

  void NasdaqDropClient::Forwarder::on_login_accepted(const std::string &session, uint64_t next_seq)
  {
    a_->send(new msg::LoginAccepted(session, next_seq), nullptr);
  }

  void NasdaqDropClient::Forwarder::on_login_rejected(char reason)
  {
    a_->send(new msg::LoginRejected(reason), nullptr);
  }

  void NasdaqDropClient::Forwarder::on_line(const std::string &session, uint64_t seq,
                                            std::string_view payload)
  {
    auto *m = new msg::Report;
    m->session = session;
    m->seq = seq;
    m->raw = std::string(payload);
    m->ok = parse_line(payload, m->report, &m->error);
    a_->send(m, nullptr);
  }

  void NasdaqDropClient::Forwarder::on_end_of_session(const std::string &session)
  {
    a_->send(new msg::EndOfSession(session), nullptr);
  }

  void NasdaqDropClient::Forwarder::on_disconnected(const std::string &reason, bool will_retry)
  {
    a_->send(new msg::Disconnected(reason, will_retry), nullptr);
  }

  // -------------------------------------------------------------------- actor

  NasdaqDropClient::NasdaqDropClient(ClientConfig cfg, std::unique_ptr<DropSession> session,
                                     actors::Actor *subscriber,
                                     std::unique_ptr<SimDropServer> sim)
      : cfg_(std::move(cfg)), sim_(std::move(sim)), session_(std::move(session)),
        subscriber_(subscriber)
  {
    MESSAGE_HANDLER(actors::msg::Start, on_start);
    MESSAGE_HANDLER(actors::msg::Shutdown, on_shutdown);
    MESSAGE_HANDLER(msg::LoginAccepted, on_login_accepted);
    MESSAGE_HANDLER(msg::LoginRejected, on_login_rejected);
    MESSAGE_HANDLER(msg::Report, on_report);
    MESSAGE_HANDLER(msg::EndOfSession, on_end_of_session);
    MESSAGE_HANDLER(msg::Disconnected, on_disconnected);
  }

  NasdaqDropClient::~NasdaqDropClient()
  {
    if (session_)
      session_->stop();
  }

  void NasdaqDropClient::on_start(const actors::msg::Start *)
  {
    // Started here, not in the constructor, so that no session event can
    // reach the actor before it is wired (as TTFIXOM does).
    state_ = State::Connecting;
    log_inf("starting DROP session, requested session '%s', sequence %d",
            cfg_.login.session, (long)cfg_.login.next_seq);
    session_->start(cfg_.login, &fwd_);
  }

  void NasdaqDropClient::on_shutdown(const actors::msg::Shutdown *)
  {
    session_->stop();
    state_ = State::Idle;
    log_inf("DROP session stopped: %d lines, %d trade events sent, %d duplicates",
            (long)stats_.lines, (long)stats_.emitted, (long)stats_.duplicates);
  }

  void NasdaqDropClient::on_login_accepted(const msg::LoginAccepted *m)
  {
    ++stats_.logins;
    if (!day_session_.empty() && m->session != day_session_)
    {
      // A new SoupBinTCP session is a new trading day: its sequence numbers
      // and match numbers start again.
      log_inf("DROP session changed from %s to %s: new trading day, ledger cleared",
              day_session_, m->session);
      ledger_.clear();
      max_seq_seen_ = 0;
      trade_date_ = 0;
    }
    day_session_ = m->session;
    if (trade_date_ == 0)
    {
      const auto now = std::chrono::system_clock::now().time_since_epoch();
      trade_date_ = cfg_.trade_date
                        ? cfg_.trade_date
                        : eastern_date(std::chrono::duration_cast<std::chrono::seconds>(now).count());
    }
    expected_next_ = m->next_seq;
    state_ = State::LoggedIn;
    log_inf("DROP login accepted: session %s, next sequence %d", m->session, (long)m->next_seq);
  }

  void NasdaqDropClient::on_login_rejected(const msg::LoginRejected *m)
  {
    state_ = State::Rejected;
    log_err("DROP login rejected, reason %c (A not authorized, S session not available); stopped",
            m->reason);
  }

  void NasdaqDropClient::on_end_of_session(const msg::EndOfSession *m)
  {
    state_ = State::Ended;
    log_inf("DROP end of session %s after sequence %d; stopped", m->session,
            (long)(expected_next_ ? expected_next_ - 1 : 0));
  }

  void NasdaqDropClient::on_disconnected(const msg::Disconnected *m)
  {
    ++stats_.disconnects;
    state_ = m->will_retry ? State::Down : State::Ended;
    log_wrn("DROP connection lost: %s%s", m->reason, m->will_retry ? "; reconnecting" : "; stopped");
  }

  void NasdaqDropClient::on_report(const msg::Report *m)
  {
    ++stats_.lines;
    if (expected_next_ != 0 && m->seq > expected_next_)
    {
      ++stats_.gaps;
      log_err("DROP sequence gap: expected %d, got %d", (long)expected_next_, (long)m->seq);
    }
    const bool replayed = m->seq <= max_seq_seen_;
    expected_next_ = m->seq + 1;
    if (m->seq > max_seq_seen_)
      max_seq_seen_ = m->seq;

    if (!m->ok)
    {
      ++stats_.parse_errors;
      log_err("DROP line %d not parsed (%s): '%s'", (long)m->seq, m->error, m->raw);
      return;
    }

    MapContext c{m->session, m->seq, trade_date_, replayed, &cfg_.assets};
    auto tc = to_trade_capture(m->report, m->raw, c);
    if (!tc)
    {
      ++stats_.order_events;
      return;
    }

    auto res = ledger_.apply(*tc);
    switch (res.what)
    {
    case post_trade::Applied::Added:
    case post_trade::Applied::Amended:
    case post_trade::Applied::Removed:
      ++stats_.emitted;
      subscriber_->send(new post_trade::msg::TradeCapture(res.out), this);
      break;
    case post_trade::Applied::Duplicate:
      ++stats_.duplicates;
      break;
    case post_trade::Applied::Stale:
      ++stats_.stale;
      log_wrn("DROP %s report %s for trade %s is stale: not forwarded",
              post_trade::to_string(tc->event), tc->report_id, tc->trade_id);
      break;
    case post_trade::Applied::Orphan:
      ++stats_.orphans;
      log_err("DROP %s report %s for trade %s, which this client has not seen: not forwarded",
              post_trade::to_string(tc->event), tc->report_id, tc->trade_id);
      break;
    case post_trade::Applied::Invalid:
      ++stats_.invalid;
      log_err("DROP report %s for trade %s does not fit the ledger: '%s'", tc->report_id,
              tc->trade_id, m->raw);
      break;
    }
  }
}

// ------------------------------------------------------------------ factory

namespace nasdaq_post_trade
{
  namespace
  {
    template <class T>
    bool get_num(const boost::property_tree::ptree &p, const char *key, T &v, T lo, T hi,
                 std::string *err)
    {
      auto o = p.get_optional<std::string>(key);
      if (!o)
        return true; // keep the default
      try
      {
        size_t used = 0;
        long long x = std::stoll(*o, &used);
        if (used != o->size() || x < (long long)lo || x > (long long)hi)
          throw std::out_of_range(key);
        v = T(x);
        return true;
      }
      catch (...)
      {
        *err = std::string(key) + ": '" + *o + "' is not a number in [" + std::to_string(lo) +
               ", " + std::to_string(hi) + "]";
        return false;
      }
    }

    bool valid_date(uint32_t d)
    {
      const uint32_t m = d / 100 % 100, day = d % 100;
      return d == 0 || (d / 10000 >= 2000 && m >= 1 && m <= 12 && day >= 1 && day <= 31);
    }
  }

  std::optional<ClientConfig> parse_config(const boost::property_tree::ptree &p,
                                           const std::string &base_dir, std::string *err)
  {
    std::string e;
    std::string &why = err ? *err : e;
    ClientConfig c;

    c.sim = p.get<std::string>("sim.enabled", "false") == "true";
    if (!get_num(p, "sim.trade_ms", c.sim_trade_ms, 0, 3'600'000, &why))
      return std::nullopt;
    {
      std::istringstream s(p.get<std::string>("sim.symbols", ""));
      for (std::string sym; s >> sym;)
        c.sim_symbols.push_back(sym);
    }

    if (!get_num(p, "heartbeat_ms", c.soup.heartbeat_ms, 10, 60'000, &why) ||
        !get_num(p, "server_timeout_ms", c.soup.server_timeout_ms, 100, 600'000, &why) ||
        !get_num(p, "reconnect_ms", c.soup.reconnect_ms, 10, 600'000, &why) ||
        !get_num(p, "connect_timeout_ms", c.soup.connect_timeout_ms, 10, 600'000, &why) ||
        !get_num(p, "trade_date", c.trade_date, 0u, 99'991'231u, &why))
      return std::nullopt;
    if (!valid_date(c.trade_date))
    {
      why = "trade_date: " + std::to_string(c.trade_date) + " is not YYYYMMDD";
      return std::nullopt;
    }
    if (c.soup.server_timeout_ms <= c.soup.heartbeat_ms)
    {
      why = "server_timeout_ms must be longer than heartbeat_ms";
      return std::nullopt;
    }

    const std::string replay = p.get<std::string>("replay_from_start", "true");
    if (replay != "true" && replay != "false")
    {
      why = "replay_from_start: '" + replay + "' is not true or false";
      return std::nullopt;
    }
    c.login.next_seq = replay == "true" ? 1 : 0;
    c.login.session = p.get<std::string>("session", "");
    if (c.login.session.size() > 10)
    {
      why = "session: more than 10 characters";
      return std::nullopt;
    }

    if (auto a = p.get_child_optional("assets"))
      for (const auto &[sym, v] : *a)
      {
        int id = -1;
        if (!get_num(*a, sym.c_str(), id, 0, 1'000'000'000, &why))
          return std::nullopt;
        c.assets[sym] = id;
      }

    if (c.sim)
    {
      const SimDropConfig s;
      c.soup.host = "127.0.0.1";
      c.login.username = s.username;
      c.login.password = s.password;
      return c;
    }

    c.soup.host = p.get<std::string>("host", "");
    if (c.soup.host.empty())
    {
      why = "host: missing";
      return std::nullopt;
    }
    if (!p.get_optional<std::string>("port"))
    {
      why = "port: missing";
      return std::nullopt;
    }
    if (!get_num(p, "port", c.soup.port, 1, 65535, &why))
      return std::nullopt;

    std::string cred = p.get<std::string>("credentials", "");
    if (cred.empty())
    {
      why = "credentials: missing (a file with username and password)";
      return std::nullopt;
    }
    std::filesystem::path cp(cred);
    if (cp.is_relative())
      cp = std::filesystem::path(base_dir) / cp;
    boost::property_tree::ptree cr;
    try
    {
      boost::property_tree::read_info(cp.string(), cr);
    }
    catch (const std::exception &ex)
    {
      why = "credentials: cannot read " + cp.string() + ": " + ex.what();
      return std::nullopt;
    }
    c.login.username = cr.get<std::string>("username", "");
    c.login.password = cr.get<std::string>("password", "");
    // SoupBinTCP 3.00, 2.3.1: Username 6 bytes, Password 10 bytes
    if (c.login.username.empty() || c.login.username.size() > 6)
    {
      why = "credentials: username must be 1 to 6 characters";
      return std::nullopt;
    }
    if (c.login.password.empty() || c.login.password.size() > 10)
    {
      why = "credentials: password must be 1 to 10 characters";
      return std::nullopt;
    }
    return c;
  }
}

actors::Actor *create_NasdaqDropClient(const boost::property_tree::ptree &cfg,
                                       actors::Actor *subscriber, const std::string &base_dir)
{
  using namespace nasdaq_post_trade;
  if (!subscriber)
  {
    std::cerr << "create_NasdaqDropClient: no subscriber" << std::endl;
    return nullptr;
  }
  std::string err;
  auto c = parse_config(cfg, base_dir, &err);
  if (!c)
  {
    std::cerr << "create_NasdaqDropClient: bad config: " << err << std::endl;
    return nullptr;
  }
  std::unique_ptr<SimDropServer> sim;
  if (c->sim)
  {
    sim = std::make_unique<SimDropServer>(SimDropConfig{});
    if (!sim->start(&err))
    {
      std::cerr << "create_NasdaqDropClient: simulator: " << err << std::endl;
      return nullptr;
    }
    c->soup.port = sim->port();
    if (c->sim_trade_ms > 0)
      sim->start_trading(std::chrono::milliseconds(c->sim_trade_ms), c->sim_symbols);
  }
  auto session = std::make_unique<SoupSession>(c->soup);
  return new act::NasdaqDropClient(std::move(*c), std::move(session), subscriber, std::move(sim));
}

actors::Actor *create_NasdaqDropClient(const std::string &config_path, actors::Actor *subscriber)
{
  boost::property_tree::ptree pt;
  try
  {
    boost::property_tree::read_info(config_path, pt);
  }
  catch (const std::exception &ex)
  {
    std::cerr << "create_NasdaqDropClient: cannot read " << config_path << ": " << ex.what()
              << std::endl;
    return nullptr;
  }
  auto sec = pt.get_child_optional("nasdaq_drop");
  if (!sec)
  {
    std::cerr << "create_NasdaqDropClient: " << config_path << " has no nasdaq_drop section"
              << std::endl;
    return nullptr;
  }
  const auto dir = std::filesystem::path(config_path).parent_path();
  return create_NasdaqDropClient(*sec, subscriber, dir.empty() ? "." : dir.string());
}
