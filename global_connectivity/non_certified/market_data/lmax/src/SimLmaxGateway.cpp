/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "lmax_fix/SimLmaxGateway.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <sstream>

#include "quickfix/Exceptions.h"
#include "quickfix/Log.h"
#include "quickfix/MessageStore.h"
#include "quickfix/Session.h"
#include "quickfix/SessionSettings.h"
#include "quickfix/ThreadedSocketAcceptor.h"

#include "lmax_fix/LmaxFix.hpp"
#include "lmax_fix/RedactLog.hpp"

namespace lmax::fix
{
  namespace
  {
    int free_port()
    {
      int s = ::socket(AF_INET, SOCK_STREAM, 0);
      sockaddr_in a{};
      a.sin_family = AF_INET;
      a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
      a.sin_port = 0;
      ::bind(s, (sockaddr *)&a, sizeof a);
      socklen_t len = sizeof a;
      ::getsockname(s, (sockaddr *)&a, &len);
      int p = ntohs(a.sin_port);
      ::close(s);
      return p;
    }

    std::string num(double v, int decimals = 6)
    {
      char b[64];
      std::snprintf(b, sizeof b, "%.*f", decimals, v);
      std::string s = b;
      while (s.size() > 1 && s.back() == '0')
        s.pop_back();
      if (s.back() == '.')
        s.pop_back();
      return s;
    }

    double field_d(const FIX::FieldMap &m, int tag, double def = 0)
    {
      return m.isSetField(tag) ? std::strtod(m.getField(tag).c_str(), nullptr) : def;
    }
    std::string field_s(const FIX::FieldMap &m, int tag)
    {
      return m.isSetField(tag) ? m.getField(tag) : std::string();
    }
    bool on_grid(double v, double step)
    {
      int64_t n;
      return to_steps(v, step, n) && n > 0;
    }
  }

  SimLmaxGateway::SimLmaxGateway(Config cfg) : cfg_(std::move(cfg)), rng_(cfg_.seed)
  {
    for (auto &i : cfg_.instruments)
    {
      Book b;
      b.inst = i;
      b.bid = i.mid - i.tick;
      b.ask = i.mid + i.tick;
      books_[i.security_id] = b;
    }
  }

  SimLmaxGateway::~SimLmaxGateway() { stop(); }

  void SimLmaxGateway::start()
  {
    port_ = cfg_.port ? cfg_.port : free_port();
    std::ostringstream s;
    s << "[DEFAULT]\nConnectionType=acceptor\nSocketAcceptPort=" << port_
      << "\nSocketReuseAddress=Y\nStartTime=00:00:00\nEndTime=00:00:00\nUseDataDictionary=Y\n"
      << "ValidateUserDefinedFields=Y\nAllowUnknownMsgFields=N\nPersistMessages=Y\nHeartBtInt=30\n"
      << "\n[SESSION]\nBeginString=FIX.4.4\nSenderCompID=" << cfg_.md_comp << "\nTargetCompID="
      << cfg_.client_md_comp << "\nDataDictionary=" << cfg_.md_dictionary << "\n"
      << "\n[SESSION]\nBeginString=FIX.4.4\nSenderCompID=" << cfg_.trade_comp << "\nTargetCompID="
      << cfg_.client_trade_comp << "\nDataDictionary=" << cfg_.trading_dictionary << "\n";
    std::istringstream in(s.str());
    settings_ = std::make_unique<FIX::SessionSettings>(in);
    md_sid_ = FIX::SessionID("FIX.4.4", cfg_.md_comp, cfg_.client_md_comp);
    trade_sid_ = FIX::SessionID("FIX.4.4", cfg_.trade_comp, cfg_.client_trade_comp);
    store_ = std::make_unique<FIX::MemoryStoreFactory>();
    if (!cfg_.log_dir.empty())
      log_ = std::make_unique<RedactLogFactory>(cfg_.log_dir);
    else
      log_ = std::make_unique<FIX::ScreenLogFactory>(false, false, false);
    acceptor_ = std::make_unique<FIX::ThreadedSocketAcceptor>(*this, *store_, *settings_, *log_);
    acceptor_->start();
    md_thread_ = std::jthread([this](std::stop_token st) { md_loop(st); });
  }

  void SimLmaxGateway::stop()
  {
    md_thread_ = {};
    if (acceptor_)
    {
      try
      {
        acceptor_->stop(true);
      }
      catch (...)
      {
      }
      acceptor_.reset();
    }
  }

  bool SimLmaxGateway::md_logged_on() const
  {
    auto *s = FIX::Session::lookupSession(md_sid_);
    return s && s->isLoggedOn();
  }
  bool SimLmaxGateway::trading_logged_on() const
  {
    auto *s = FIX::Session::lookupSession(trade_sid_);
    return s && s->isLoggedOn();
  }

  std::vector<std::string> SimLmaxGateway::received(const std::string &t)
  {
    std::lock_guard<std::recursive_mutex> l(mu_);
    std::vector<std::string> v;
    auto r = received_.equal_range(t);
    for (auto it = r.first; it != r.second; ++it)
      v.push_back(it->second);
    return v;
  }

  size_t SimLmaxGateway::working_orders()
  {
    std::lock_guard<std::recursive_mutex> l(mu_);
    size_t n = 0;
    for (auto &[k, o] : orders_)
      n += !o.done;
    return n;
  }

  std::string SimLmaxGateway::now_ts()
  {
    const uint64_t ns = now_ns();
    time_t t = time_t(ns / 1000000000ULL);
    tm g;
    gmtime_r(&t, &g);
    char b[32];
    std::snprintf(b, sizeof b, "%04d%02d%02d-%02d:%02d:%02d.%03d", g.tm_year + 1900, g.tm_mon + 1, g.tm_mday,
                  g.tm_hour, g.tm_min, g.tm_sec, int(ns / 1000000 % 1000));
    return b;
  }

  std::string SimLmaxGateway::today() { return now_ts().substr(0, 8); }

  std::string SimLmaxGateway::next_exec_id()
  {
    char b[32];
    std::snprintf(b, sizeof b, "E%015llu", (unsigned long long)++exec_seq_);
    return b;
  }

  SimLmaxGateway::Book *SimLmaxGateway::book(const std::string &id)
  {
    auto it = books_.find(id);
    return it == books_.end() ? nullptr : &it->second;
  }

  // ---- session ---------------------------------------------------------------------------

  void SimLmaxGateway::onLogon(const FIX::SessionID &sid)
  {
    if (sid == md_sid_)
      ++n_.md_logons;
    else
      ++n_.trade_logons;
  }

  void SimLmaxGateway::onLogout(const FIX::SessionID &sid)
  {
    std::lock_guard<std::recursive_mutex> l(mu_);
    if (sid == md_sid_)
    {
      subs_.clear();
      return;
    }
    // Cancel on disconnect, unless the order asked to be reinstated
    // (ExecInst H; LMAX Broker Trading spec, Cancel on Disconnect).
    tcr_subs_.clear();
    for (auto &[k, o] : orders_)
      if (!o.done && !o.reinstate)
      {
        o.done = true;
        o.status = '4';
        send_exec(o, '4', {}, 0, 0, {}, {}, {}, "cancelled on disconnect");
      }
  }

  void SimLmaxGateway::fromAdmin(const FIX::Message &m, const FIX::SessionID &sid)
  {
    const std::string type = m.getHeader().getField(35);
    if (type == "A")
    {
      if (field_s(m, 553) != cfg_.username || field_s(m, 554) != cfg_.password)
      {
        ++n_.logon_rejects;
        throw FIX::RejectLogon("invalid username or password");
      }
      if (sid == md_sid_ && field_s(m, 141) != "Y")
      {
        ++n_.logon_rejects;
        throw FIX::RejectLogon("market data logon must reset sequence numbers (141=Y)");
      }
    }
    else if (type == "2" && sid == md_sid_)
    {
      ++n_.resend_requests;
      if (cfg_.ignore_resend)
        throw FIX::IncorrectTagValue(7); // stands in for "no recovery": the gap is never filled
    }
  }

  void SimLmaxGateway::fromApp(const FIX::Message &m, const FIX::SessionID &sid)
  {
    const std::string type = m.getHeader().getField(35);
    {
      std::lock_guard<std::recursive_mutex> l(mu_);
      std::string text = m.toString();
      std::replace(text.begin(), text.end(), '\x01', '|');
      received_.emplace(type, text);
    }
    if (sid == md_sid_)
    {
      if (type == "V")
        on_md_request(m);
      return;
    }
    if (type == "D")
      on_new_order(m);
    else if (type == "F")
      on_cancel(m);
    else if (type == "G")
      on_replace(m);
    else if (type == "H")
      on_status(m);
    else if (type == "AD")
      on_tcr_request(m);
  }

  // ---- market data -----------------------------------------------------------------------

  void SimLmaxGateway::on_md_request(const FIX::Message &m)
  {
    std::lock_guard<std::recursive_mutex> l(mu_);
    ++n_.md_requests;
    Sub s;
    s.req_id = field_s(m, 262);
    s.depth = int(field_d(m, 264, 0));
    if (s.depth == 0 || s.depth > 20)
      s.depth = 20;
    const int ntypes = int(field_d(m, 267));
    for (int i = 1; i <= ntypes; ++i)
    {
      FIX::Group g(267, 269);
      m.getGroup(i, g);
      if (field_s(g, 269) == "2")
        s.trades = true;
    }
    FIX::Group sym(146, 48);
    m.getGroup(1, sym);
    s.security_id = field_s(sym, 48);
    auto reject = [&](char reason, const std::string &text) {
      ++n_.md_request_rejects;
      FIX::Message y;
      y.getHeader().setField(35, "Y");
      y.setField(262, s.req_id);
      y.setField(281, std::string(1, reason));
      y.setField(58, text);
      FIX::Session::sendToTarget(y, md_sid_);
    };
    bool valid_id = !s.req_id.empty() && s.req_id.size() <= 16 && s.req_id[0] != '0' &&
                    std::all_of(s.req_id.begin(), s.req_id.end(), [](char c) { return std::isdigit((unsigned char)c) || (c >= 'a' && c <= 'f'); });
    if (!valid_id)
      return reject('1', "invalid MDReqID");
    if (field_s(m, 263) == "2")
    {
      subs_.erase(std::remove_if(subs_.begin(), subs_.end(), [&](auto &x) { return x.req_id == s.req_id; }),
                  subs_.end());
      return;
    }
    if (!book(s.security_id))
      return reject('0', "Unknown symbol");
    for (auto &x : subs_)
      if (x.req_id == s.req_id)
        return reject('1', "Duplicate MDReqID");
    subs_.push_back(s);
    send_snapshot(subs_.back(), *book(s.security_id));
  }

  void SimLmaxGateway::step_book(Book &b)
  {
    const double t = b.inst.tick;
    std::uniform_int_distribution<int> mv(-2, 2), sz(1, 40);
    double mid = std::round((b.bid + b.ask) / 2 / t) * t + mv(rng_) * t;
    if (mid <= 10 * t)
      mid = 10 * t;
    const int spread = 1 + int(rng_() % 3);
    b.bid = mid - t * (spread / 2 + 1);
    b.ask = b.bid + t * spread;
    b.bids.clear();
    b.asks.clear();
    for (int k = 0; k < cfg_.levels; ++k)
    {
      b.bids.emplace_back(b.bid - k * t, sz(rng_) * b.inst.qty_step);
      b.asks.emplace_back(b.ask + k * t, sz(rng_) * b.inst.qty_step);
    }
  }

  void SimLmaxGateway::send_snapshot(const Sub &s, Book &b)
  {
    if (b.bids.empty())
      step_book(b);
    static const int order[] = {269, 270, 271, 272, 273, 0};
    FIX::Message w;
    w.getHeader().setField(35, "W");
    w.setField(262, s.req_id);
    w.setField(48, s.security_id);
    w.setField(22, "8");
    auto add = [&](char type, double px, double size, const std::string &date = {}, const std::string &time = {}) {
      FIX::Group g(268, 269, order);
      g.setField(269, std::string(1, type));
      g.setField(270, num(px, 8));
      g.setField(271, num(size, 4));
      if (!date.empty())
      {
        g.setField(272, date);
        g.setField(273, time);
      }
      w.addGroup(g);
    };
    for (int k = 0; k < int(b.bids.size()) && k < s.depth; ++k)
      add('0', b.bids[k].first, b.bids[k].second);
    for (int k = 0; k < int(b.asks.size()) && k < s.depth; ++k)
      add('1', b.asks[k].first, b.asks[k].second);
    if (s.trades)
    {
      for (auto &[px, size, d, tm] : b.trades)
        add('2', px, size, d, tm);
      if (cfg_.repeat_trades && b.trades.empty())
        for (auto &[px, size, d, tm] : b.last_trades)
          add('2', px, size, d, tm);
    }
    if (FIX::Session::sendToTarget(w, md_sid_))
      ++n_.snapshots;
  }

  void SimLmaxGateway::md_loop(std::stop_token st)
  {
    std::uniform_int_distribution<int> coin(0, 3);
    while (!st.stop_requested())
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(cfg_.md_interval_ms));
      if (md_paused_ || !md_logged_on())
        continue;
      std::lock_guard<std::recursive_mutex> l(mu_);
      for (auto &[id, b] : books_)
      {
        auto prev_bid = b.bid, prev_ask = b.ask;
        bool had = !b.bids.empty();
        if (b.trades.empty() && had && coin(rng_) == 0)
        {
          // A market trade at the previous best price; the new book follows.
          const std::string ts = now_ts();
          const bool buy = coin(rng_) < 2;
          b.trades.emplace_back(buy ? prev_ask : prev_bid, b.inst.qty_step * (1 + coin(rng_)), ts.substr(0, 8),
                                ts.substr(9));
        }
        step_book(b);
        for (auto &s : subs_)
          if (s.security_id == id)
            send_snapshot(s, b);
        if (!b.trades.empty())
          b.last_trades = std::move(b.trades);
        b.trades.clear();
      }
    }
  }

  void SimLmaxGateway::inject_md_gap(int n)
  {
    if (auto *s = FIX::Session::lookupSession(md_sid_))
      s->setNextSenderMsgSeqNum(s->getExpectedSenderNum() + n);
  }

  void SimLmaxGateway::disconnect_md()
  {
    if (auto *s = FIX::Session::lookupSession(md_sid_))
      s->disconnect();
  }

  void SimLmaxGateway::disconnect_trading()
  {
    if (auto *s = FIX::Session::lookupSession(trade_sid_))
      s->disconnect();
  }

  void SimLmaxGateway::set_market(const std::string &id, double bid, double ask)
  {
    std::lock_guard<std::recursive_mutex> l(mu_);
    if (auto *b = book(id))
    {
      b->bid = bid;
      b->ask = ask;
    }
  }

  void SimLmaxGateway::trade_print(const std::string &id, double px, double size)
  {
    std::lock_guard<std::recursive_mutex> l(mu_);
    if (auto *b = book(id))
    {
      const std::string ts = now_ts();
      b->trades.emplace_back(px, size, ts.substr(0, 8), ts.substr(9));
    }
  }

  // ---- trading ---------------------------------------------------------------------------

  void SimLmaxGateway::send_exec(const Ord &o, char exec_type, const std::string &orig, double last_qty,
                                 double last_px, const std::string &exec_id, const std::string &secondary,
                                 const std::string &status_req_id, const std::string &text)
  {
    FIX::Message e;
    e.getHeader().setField(35, "8");
    e.setField(37, o.order_id.empty() ? "0" : o.order_id);
    e.setField(527, secondary.empty() ? "0" : secondary);
    e.setField(11, o.clordid);
    if (!orig.empty())
      e.setField(41, orig);
    if (!status_req_id.empty())
      e.setField(790, status_req_id);
    e.setField(17, exec_id.empty() ? next_exec_id() : exec_id);
    e.setField(150, std::string(1, exec_type));
    e.setField(39, std::string(1, o.status));
    if (!o.account.empty())
      e.setField(1, o.account);
    e.setField(48, o.security_id);
    e.setField(22, "8");
    e.setField(54, std::string(1, o.side));
    e.setField(38, num(o.qty, 4));
    e.setField(40, std::string(1, o.ord_type));
    if (o.ord_type == '2')
      e.setField(44, num(o.px, 8));
    e.setField(59, std::string(1, o.tif));
    if (exec_type == 'F')
    {
      e.setField(32, num(last_qty, 4));
      e.setField(31, num(last_px, 8));
    }
    e.setField(151, num(o.done ? 0 : o.qty - o.cum, 4));
    e.setField(14, num(o.cum, 4));
    e.setField(6, num(o.cum > 0 ? o.notional / o.cum : 0, 8));
    e.setField(60, now_ts());
    if (!text.empty())
      e.setField(58, text);
    FIX::Session::sendToTarget(e, trade_sid_);
  }

  void SimLmaxGateway::send_reject(const FIX::Message &m, int reason, const std::string &text)
  {
    Ord o;
    o.clordid = field_s(m, 11);
    o.security_id = field_s(m, 48);
    o.side = field_s(m, 54).empty() ? '1' : field_s(m, 54)[0];
    o.qty = field_d(m, 38);
    o.ord_type = field_s(m, 40).empty() ? '2' : field_s(m, 40)[0];
    o.px = field_d(m, 44);
    o.tif = field_s(m, 59).empty() ? '0' : field_s(m, 59)[0];
    o.status = '8';
    o.done = true;
    send_exec(o, '8', {}, 0, 0, {}, {}, {}, text);
    (void)reason;
  }

  void SimLmaxGateway::send_cancel_reject(const std::string &clordid, const std::string &orig, char response_to,
                                         int reason, const std::string &order_id, char status)
  {
    FIX::Message r;
    r.getHeader().setField(35, "9");
    r.setField(37, order_id.empty() ? "NONE" : order_id);
    r.setField(11, clordid);
    r.setField(41, orig);
    r.setField(39, std::string(1, status));
    r.setField(434, std::string(1, response_to));
    r.setField(102, std::to_string(reason));
    FIX::Session::sendToTarget(r, trade_sid_);
  }

  void SimLmaxGateway::fill(Ord &o, double qty, double px)
  {
    o.cum += qty;
    o.notional += qty * px;
    if (o.cum >= o.qty - 1e-9)
    {
      o.cum = o.qty;
      o.done = true;
      o.status = '2';
    }
    else
      o.status = '1';
    Fill f;
    f.exec_id = next_exec_id();
    char sec[32];
    std::snprintf(sec, sizeof sec, "M%015llu", (unsigned long long)exec_seq_);
    f.secondary = sec;
    f.order_id = o.order_id;
    f.clordid = o.clordid;
    f.security_id = o.security_id;
    f.account = o.account;
    f.side = o.side;
    f.qty = qty;
    f.px = px;
    f.transact_time = now_ts();
    f.trade_date = today();
    fills_.push_back(f);
    send_exec(o, 'F', {}, qty, px, f.exec_id, f.secondary);
    for (auto &req : tcr_subs_)
      send_tcr(f, req, false);
  }

  void SimLmaxGateway::match(Ord &o)
  {
    Book *b = book(o.security_id);
    if (!b || o.done)
      return;
    const double best = o.side == '1' ? b->ask : b->bid;
    const bool marketable = o.ord_type == '1' || (o.side == '1' ? o.px >= best - 1e-12 : o.px <= best + 1e-12);
    if (marketable)
    {
      const double left = o.qty - o.cum;
      if (cfg_.partial_fills && left > b->inst.qty_step * 1.5)
      {
        double half = std::floor(left / 2 / b->inst.qty_step) * b->inst.qty_step;
        fill(o, half, best);
        fill(o, o.qty - o.cum, best);
      }
      else
        fill(o, left, best);
    }
    else if (o.tif == '3' || o.tif == '4')
    {
      o.done = true;
      o.status = '4';
      send_exec(o, '4', {}, 0, 0, {}, {}, {}, "IOC/FOK not filled");
    }
  }

  void SimLmaxGateway::on_new_order(const FIX::Message &m)
  {
    std::lock_guard<std::recursive_mutex> l(mu_);
    ++n_.new_orders;
    Ord o;
    o.clordid = field_s(m, 11);
    o.security_id = field_s(m, 48);
    o.account = field_s(m, 1);
    o.side = field_s(m, 54)[0];
    o.qty = field_d(m, 38);
    o.ord_type = field_s(m, 40)[0];
    o.px = field_d(m, 44);
    o.tif = field_s(m, 59).empty() ? '0' : field_s(m, 59)[0];
    o.reinstate = field_s(m, 18).find('H') != std::string::npos;
    Book *b = book(o.security_id);
    if (o.clordid.empty() || o.clordid.size() > 20)
      return send_reject(m, 99, "ClOrdID must be 1 to 20 characters");
    if (orders_.count(o.clordid) && !orders_[o.clordid].done)
      return send_reject(m, 6, "duplicate ClOrdID");
    if (!b)
      return send_reject(m, 1, "unknown SecurityID");
    if (!on_grid(o.qty, b->inst.qty_step))
      return send_reject(m, 13, "quantity off the grid");
    if (o.ord_type == '2' && !on_grid(o.px, b->inst.tick))
      return send_reject(m, 99, "price off the grid");
    char id[32];
    std::snprintf(id, sizeof id, "%llu", 1000000000ULL + ++order_seq_);
    o.order_id = id;
    o.status = '0';
    auto &ref = orders_[o.clordid] = o;
    send_exec(ref, '0');
    match(ref);
  }

  void SimLmaxGateway::on_cancel(const FIX::Message &m)
  {
    std::lock_guard<std::recursive_mutex> l(mu_);
    ++n_.cancels;
    const std::string cl = field_s(m, 11), orig = field_s(m, 41);
    auto it = orders_.find(orig);
    if (it == orders_.end())
      return send_cancel_reject(cl, orig, '1', 1, {}, '8');
    Ord &o = it->second;
    if (o.done)
      return send_cancel_reject(cl, orig, '1', 0, o.order_id, o.status);
    o.done = true;
    o.status = '4';
    Ord moved = o;
    moved.clordid = cl;
    orders_.erase(it);
    auto &ref = orders_[cl] = moved;
    send_exec(ref, '4', orig);
  }

  void SimLmaxGateway::on_replace(const FIX::Message &m)
  {
    std::lock_guard<std::recursive_mutex> l(mu_);
    ++n_.replaces;
    const std::string cl = field_s(m, 11), orig = field_s(m, 41);
    auto it = orders_.find(orig);
    if (it == orders_.end())
      return send_cancel_reject(cl, orig, '2', 1, {}, '8');
    Ord o = it->second;
    if (o.done)
      return send_cancel_reject(cl, orig, '2', 0, o.order_id, o.status);
    const double qty = field_d(m, 38), px = field_d(m, 44);
    Book *b = book(o.security_id);
    if (!on_grid(qty, b->inst.qty_step) || qty <= o.cum + 1e-9 || !on_grid(px, b->inst.tick))
      return send_cancel_reject(cl, orig, '2', 99, o.order_id, o.status);
    o.qty = qty;
    o.px = px;
    o.clordid = cl;
    o.reinstate = field_s(m, 18).find('H') != std::string::npos;
    orders_.erase(it);
    auto &ref = orders_[cl] = o;
    send_exec(ref, '5', orig);
    match(ref);
  }

  void SimLmaxGateway::on_status(const FIX::Message &m)
  {
    std::lock_guard<std::recursive_mutex> l(mu_);
    ++n_.status_requests;
    const std::string cl = field_s(m, 11);
    auto it = orders_.find(cl);
    if (it == orders_.end())
    {
      Ord o;
      o.clordid = cl;
      o.security_id = field_s(m, 48);
      o.side = field_s(m, 54)[0];
      o.status = '8';
      o.done = true;
      return send_exec(o, 'I', {}, 0, 0, {}, {}, field_s(m, 790), "unknown order");
    }
    send_exec(it->second, 'I', {}, 0, 0, {}, {}, field_s(m, 790));
  }

  bool SimLmaxGateway::force_fill(const std::string &clordid, double qty)
  {
    std::lock_guard<std::recursive_mutex> l(mu_);
    auto it = orders_.find(clordid);
    if (it == orders_.end() || it->second.done)
      return false;
    fill(it->second, std::min(qty, it->second.qty - it->second.cum), it->second.px);
    return true;
  }

  bool SimLmaxGateway::unsolicited_cancel(const std::string &clordid)
  {
    std::lock_guard<std::recursive_mutex> l(mu_);
    auto it = orders_.find(clordid);
    if (it == orders_.end() || it->second.done)
      return false;
    it->second.done = true;
    it->second.status = '4';
    send_exec(it->second, '4', {}, 0, 0, {}, {}, {}, "cancelled by LMAX");
    return true;
  }

  void SimLmaxGateway::send_tcr(const Fill &f, const std::string &req_id, bool last)
  {
    static const int order[] = {54, 37, 11, 1, 0};
    FIX::Message r;
    r.getHeader().setField(35, "AE");
    r.setField(568, req_id);
    r.setField(912, last ? "Y" : "N");
    r.setField(17, f.exec_id);
    r.setField(527, f.secondary);
    r.setField(48, f.security_id);
    r.setField(22, "8");
    r.setField(32, num(f.qty, 4));
    r.setField(31, num(f.px, 8));
    r.setField(75, f.trade_date);
    r.setField(60, f.transact_time);
    FIX::Group g(552, 54, order);
    g.setField(54, std::string(1, f.side));
    g.setField(37, f.order_id);
    g.setField(11, f.clordid);
    if (!f.account.empty())
      g.setField(1, f.account);
    r.addGroup(g);
    FIX::Session::sendToTarget(r, trade_sid_);
  }

  void SimLmaxGateway::on_tcr_request(const FIX::Message &m)
  {
    std::lock_guard<std::recursive_mutex> l(mu_);
    ++n_.tcr_requests;
    const std::string id = field_s(m, 568);
    std::string from, to;
    const int nd = int(field_d(m, 580));
    for (int i = 1; i <= nd; ++i)
    {
      FIX::Group g(580, 60);
      m.getGroup(i, g);
      (i == 1 ? from : to) = field_s(g, 60);
    }
    std::vector<const Fill *> hits;
    for (auto &f : fills_)
      if (f.transact_time >= from && (to.empty() || f.transact_time <= to))
        hits.push_back(&f);
    FIX::Message ack;
    ack.getHeader().setField(35, "AQ");
    ack.setField(568, id);
    ack.setField(569, "1");
    if (m.isSetField(263))
      ack.setField(263, field_s(m, 263));
    if (hits.empty())
      ack.setField(748, "0");
    ack.setField(749, "0");
    ack.setField(750, "0");
    FIX::Session::sendToTarget(ack, trade_sid_);
    for (size_t i = 0; i < hits.size(); ++i)
      send_tcr(*hits[i], id, i + 1 == hits.size());
    if (field_s(m, 263) == "1")
      tcr_subs_.insert(id);
  }
}
