/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// okx_verify: runs OkxSync on OKX's live public stream (or a recording) for
// one instrument and checks it continuously:
//
//   - seqId/prevSeqId continuity; resyncs and why
//   - the `checksum` field on every book message (deprecated by OKX, fixed at
//     0; any other value is checked against CRC32 of the top 25 levels)
//   - crossed books
//   - tradeId continuity on the trades channel (tradeId - count == previous)
//   - REST snapshots (GET /api/v5/market/books, sz=400) every 10 s: where the
//     snapshot's seqId is one the stream also delivered, the book at exactly
//     that seqId is compared level by level over all 400 levels; otherwise the
//     top 5 levels per side are compared with the stream's books just before
//     and just after that seqId
//   - with --ob, the full chain: OkxSync -> Mbp2Mbo -> OB, and every second
//     OB's size at each of the best window levels against the size the sync
//     class's book gives (in chunks), and OB's best bid and ask
//
//   okx_verify --inst BTC-USDT --minutes 30 [--ob] [--record FILE]
//   okx_verify --inst BTC-USDT --replay FILE [--ob]
//
// Venue code: OKX for spot, OKXF for swaps (en::x).

#include <atomic>
#include <chrono>
#include <cstdio>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>
#include <vector>

#include <boost/property_tree/ptree.hpp>
#include <nlohmann/json.hpp>

#include "bfile/r_l3.hpp"
#include "enum/e_names.hpp"
#include "frame/cons/msg/Get.hpp"
#include "frame/cons/msg/Page.hpp"
#include "frame/mda/msg/Data.hpp"
#include "frame/ob/act/OB.hpp"
#include "frame/ref/RefData.hpp"
#include "okx_md/OkxSync.hpp"
#include "okx_md/OkxWs.hpp"
#include "sim/Mbp2Mbo.hpp"
#include "unit_test/MockActor.hpp"
#include "unit_test/TestHelper.hpp"

using okx::md::OkxSync;

namespace
{
  struct TestableOB : public frame::ob::act::OB
  {
    using frame::ob::act::OB::OB;
    using frame::ob::act::OB::reply_to;
  };

  using Lv = std::vector<std::pair<int64_t, double>>; // best first

  struct Book
  {
    int64_t seq = 0;
    Lv b, a;
  };

  struct Opts
  {
    std::string inst, replay, record;
    double minutes = 30;
    bool ob = false;
    int rest_every = 10;
    size_t window = 50;
    double chunk = 0;
  };

  class Verifier
  {
  public:
    explicit Verifier(const Opts &o) : o_(o)
    {
      const bool swap = o.inst.find("-SWAP") != std::string::npos;
      venue_ = swap ? en::x::OKXF : en::x::OKX;
      sim::Mbp2Mbo::Config c;
      c.sec_id = 1;
      c.venue = char(venue_);
      c.tick = 0.1; // BTC-USDT and BTC-USDT-SWAP tickSz (GET /api/v5/public/instruments)
      c.chunk = o.chunk > 0 ? o.chunk : (swap ? 1.0 : 0.01); // 1 contract = 0.01 BTC
      m_ = std::make_unique<sim::Mbp2Mbo>(c, [this](const bfile::l3_t &r) { emit(r); });
      sync_ = std::make_unique<OkxSync>(o.inst, *m_, o.window);
      sync_->on_book = [this](int64_t seq) { on_book(seq); };
      if (o.ob)
        make_ob();
    }

    void connected(uint64_t ns)
    {
      ++connects_;
      sync_->reset();
      (void)ns;
    }

    // One WebSocket text frame. false: the connection must be reopened.
    bool frame(const std::string &text, uint64_t ns)
    {
      now_ = ns;
      if (text == "pong")
        return true;
      nlohmann::json msg;
      try
      {
        msg = nlohmann::json::parse(text);
      }
      catch (const std::exception &e)
      {
        ++parse_errors_;
        return true;
      }
      if (auto ev = msg.find("event"); ev != msg.end())
      {
        const auto e = ev->get<std::string>();
        if (e == "error" || e == "notice")
        {
          resync_reasons_.push_back(e + " " + msg.value("code", std::string()) + " " + msg.value("msg", std::string()));
          return false;
        }
        return true;
      }
      ++frames_;
      if (sync_->on_message(msg) == OkxSync::Status::Gap)
      {
        resync_reasons_.push_back("gap/checksum at seqId " + std::to_string(sync_->seq_id()));
        return false;
      }
      check_pending(false);
      return true;
    }

    void rest(const std::string &body, uint64_t ns)
    {
      now_ = ns;
      ++rest_total_;
      nlohmann::json j;
      try
      {
        j = nlohmann::json::parse(body);
      }
      catch (...)
      {
        ++rest_errors_;
        return;
      }
      if (j.value("code", std::string()) != "0" || !j.contains("data") || j["data"].empty())
      {
        ++rest_errors_;
        return;
      }
      const auto &d = j["data"][0];
      Book r;
      r.seq = d.at("seqId").get<int64_t>();
      for (const auto &l : d.at("bids"))
        r.b.emplace_back(m_->ticks(std::stod(l[0].get<std::string>())), std::stod(l[1].get<std::string>()));
      for (const auto &l : d.at("asks"))
        r.a.emplace_back(m_->ticks(std::stod(l[0].get<std::string>())), std::stod(l[1].get<std::string>()));
      pending_.push_back({ns, std::move(r)});
      check_pending(false);
    }

    void finish() { check_pending(true); }

    void report(std::ostream &os, bool final) const
    {
      const auto &s = sync_->stats();
      os << (final ? "FINAL " : "") << o_.inst << " frames " << frames_ << " snapshots " << s.snapshots << " updates "
         << s.updates << " heartbeats " << s.heartbeats << " seq_resets " << s.seq_resets << " gaps " << s.gaps
         << " connects " << connects_ << " checksum_zero " << s.checksum_zero << " checksum_checked "
         << s.checksum_checked << " checksum_mismatch " << s.checksum_mismatch << " crossed " << s.crossed
         << " max_levels " << s.max_bids << "/" << s.max_asks << " trade_msgs " << s.trade_msgs << " trade_id_gaps "
         << s.trade_id_gaps << " trades_before_book " << s.trades_before_book << " trades_in_book "
         << s.trades_in_book << "\n";
      os << (final ? "FINAL " : "") << o_.inst << " rest " << rest_total_ << " errors " << rest_errors_
         << " exact_seq " << exact_n_ << " exact_match " << exact_match_ << " exact_levels " << exact_levels_
         << " exact_level_mismatch " << exact_level_bad_ << " between " << between_n_ << " top5_before "
         << top5_before_ << " top5_after " << top5_after_ << " top5_either " << top5_either_ << " older_than_history "
         << too_old_ << " ahead_of_stream " << ahead_ << "\n";
      if (o_.ob)
        os << (final ? "FINAL " : "") << o_.inst << " ob_checks " << ob_checks_ << " ob_levels " << ob_levels_
           << " ob_level_mismatch " << ob_bad_ << " mbp2mbo_level_mismatch " << mm_bad_ << " ob_bbo_mismatch "
           << ob_bbo_bad_ << " records " << records_ << "\n";
      for (const auto &r : resync_reasons_)
        os << "  resync: " << r << "\n";
      for (const auto &r : notes_)
        os << "  note: " << r << "\n";
    }

  private:
    Opts o_;
    en::x venue_;
    std::unique_ptr<sim::Mbp2Mbo> m_;
    std::unique_ptr<OkxSync> sync_;
    std::deque<Book> hist_;
    std::deque<std::pair<uint64_t, Book>> pending_;
    uint64_t now_ = 0, frames_ = 0, connects_ = 0, parse_errors_ = 0;
    uint64_t rest_total_ = 0, rest_errors_ = 0, exact_n_ = 0, exact_match_ = 0, exact_levels_ = 0,
             exact_level_bad_ = 0;
    uint64_t between_n_ = 0, top5_before_ = 0, top5_after_ = 0, top5_either_ = 0, too_old_ = 0, ahead_ = 0;
    std::vector<std::string> resync_reasons_, notes_;

    // OB chain
    unit_test::MockActor probe_{"probe"};
    std::unique_ptr<TestableOB> ob_;
    uint64_t records_ = 0, ob_checks_ = 0, ob_levels_ = 0, ob_bad_ = 0, mm_bad_ = 0, ob_bbo_bad_ = 0;
    uint64_t last_ob_check_ = 0;

    void make_ob()
    {
      const std::string seed = "okx_verify_universe_seed.csv";
      {
        std::ofstream f(seed);
        f << "# generated by okx_verify\n";
      }
      frame::ref::RefData::set_universe(seed);
      frame::ref::RefData::inst();
      auto a = frame::ref::RefData::add_future_asset(o_.inst + "_VERIFY", venue_, 1, "", o_.inst, 1.0);
      a->maxpx = 1 << 30;
      boost::property_tree::ptree pt;
      pt.put("ladder_ticks", 524288);
      ob_ = std::make_unique<TestableOB>(nullptr, true, nullptr, a->id, pt);
    }

    void dispatch(const actors::Message *msg)
    {
      ob_->reply_to = &probe_;
      unit_test::TestHelper::invoke_handler(ob_.get(), msg, &probe_);
      ob_->reply_to = nullptr;
    }

    void emit(const bfile::l3_t &r)
    {
      ++records_;
      if (!ob_)
        return;
      frame::mda::msg::Data d(true);
      d.l3 = r;
      dispatch(&d);
    }

    int ob_qat(bool bid, int64_t t)
    {
      probe_.clear();
      frame::cons::msg::Get g("qat", {{"side", bid ? "B" : "S"}, {"px", std::to_string(t)}});
      dispatch(&g);
      auto page = probe_.get_message<frame::cons::msg::Page>(0);
      int sz = -1, cnt = -1, simq = -1;
      if (page)
        std::sscanf(page->val.c_str(), "QAT: %d %d %d", &sz, &cnt, &simq);
      return sz;
    }

    std::pair<int, int> ob_bbo()
    {
      probe_.clear();
      frame::cons::msg::Get g("bbbo");
      dispatch(&g);
      auto page = probe_.get_message<frame::cons::msg::Page>(0);
      int b = -1, a = -1;
      if (page)
        std::sscanf(page->val.c_str(), "BBBO: %d %d", &b, &a);
      return {b, a};
    }

    Book current() const
    {
      Book k;
      k.seq = sync_->seq_id();
      for (auto it = sync_->bids().rbegin(); it != sync_->bids().rend(); ++it)
        k.b.emplace_back(it->first, it->second.qty);
      for (const auto &[t, l] : sync_->asks())
        k.a.emplace_back(t, l.qty);
      return k;
    }

    void on_book(int64_t seq)
    {
      if (!hist_.empty() && hist_.back().seq == seq)
        hist_.back() = current(); // heartbeat: same seqId
      else
        hist_.push_back(current());
      while (hist_.size() > 400)
        hist_.pop_front();
      if (ob_ && now_ - last_ob_check_ >= 1000000000ull)
      {
        last_ob_check_ = now_;
        check_ob();
      }
    }

    void check_ob()
    {
      ++ob_checks_;
      size_t n = 0;
      for (auto it = sync_->bids().rbegin(); it != sync_->bids().rend() && n < o_.window; ++it, ++n)
        check_level(true, it->first, it->second.qty);
      n = 0;
      for (auto it = sync_->asks().begin(); it != sync_->asks().end() && n < o_.window; ++it, ++n)
        check_level(false, it->first, it->second.qty);
      if (!sync_->bids().empty() && !sync_->asks().empty())
      {
        auto [b, a] = ob_bbo();
        if (b != int(sync_->bids().rbegin()->first) || a != int(sync_->asks().begin()->first))
        {
          ++ob_bbo_bad_;
          if (notes_.size() < 20)
            notes_.push_back("OB bbo " + std::to_string(b) + "/" + std::to_string(a) + " sync " +
                             std::to_string(sync_->bids().rbegin()->first) + "/" +
                             std::to_string(sync_->asks().begin()->first));
        }
      }
    }

    void check_level(bool bid, int64_t t, double qty)
    {
      ++ob_levels_;
      const int want = int(m_->target(qty));
      if (int(m_->chunks(bid, t)) != want)
        ++mm_bad_;
      const int got = ob_qat(bid, t);
      if (got != want)
      {
        ++ob_bad_;
        if (notes_.size() < 20)
          notes_.push_back(std::string("OB level ") + (bid ? "bid " : "ask ") + std::to_string(t) + " has " +
                           std::to_string(got) + " chunks, sync " + std::to_string(want));
      }
    }

    static bool same(const Lv &x, const Lv &y, size_t n)
    {
      if (x.size() < n || y.size() < n)
        return x == y;
      for (size_t i = 0; i < n; ++i)
        if (x[i] != y[i])
          return false;
      return true;
    }

    void check_pending(bool final)
    {
      while (!pending_.empty())
      {
        auto &[ns, r] = pending_.front();
        if (hist_.empty())
        {
          if (!final)
            return;
          ++too_old_;
          pending_.pop_front();
          continue;
        }
        if (r.seq > hist_.back().seq)
        {
          // The stream has not reached this seqId yet; wait up to 5 s.
          if (!final && now_ - ns < 5000000000ull)
            return;
          ++ahead_;
          pending_.pop_front();
          continue;
        }
        if (r.seq < hist_.front().seq)
        {
          ++too_old_;
          pending_.pop_front();
          continue;
        }
        const Book *before = nullptr, *after = nullptr, *exact = nullptr;
        for (const auto &h : hist_)
        {
          if (h.seq == r.seq)
            exact = &h;
          else if (h.seq < r.seq)
            before = &h;
          else if (!after)
            after = &h;
        }
        if (exact)
        {
          ++exact_n_;
          uint64_t bad = 0;
          compare_all(r.b, exact->b, bad);
          compare_all(r.a, exact->a, bad);
          exact_levels_ += r.b.size() + r.a.size();
          exact_level_bad_ += bad;
          if (!bad)
            ++exact_match_;
          else if (notes_.size() < 20)
            notes_.push_back("REST seqId " + std::to_string(r.seq) + ": " + std::to_string(bad) +
                             " levels differ from the stream at the same seqId");
        }
        else
        {
          ++between_n_;
          const bool mb = before && same(r.b, before->b, 5) && same(r.a, before->a, 5);
          const bool ma = after && same(r.b, after->b, 5) && same(r.a, after->a, 5);
          top5_before_ += mb;
          top5_after_ += ma;
          top5_either_ += (mb || ma);
        }
        pending_.pop_front();
      }
    }

    static void compare_all(const Lv &rest, const Lv &stream, uint64_t &bad)
    {
      const size_t n = std::max(rest.size(), stream.size());
      for (size_t i = 0; i < n; ++i)
        if (i >= rest.size() || i >= stream.size() || rest[i] != stream[i])
          ++bad;
    }
  };

  Opts parse(int argc, char **argv)
  {
    Opts o;
    for (int i = 1; i < argc; ++i)
    {
      std::string a = argv[i];
      auto next = [&]() -> std::string {
        if (i + 1 >= argc)
          throw std::runtime_error(a + " needs a value");
        return argv[++i];
      };
      if (a == "--inst")
        o.inst = next();
      else if (a == "--minutes")
        o.minutes = std::stod(next());
      else if (a == "--replay")
        o.replay = next();
      else if (a == "--record")
        o.record = next();
      else if (a == "--ob")
        o.ob = true;
      else if (a == "--rest-every")
        o.rest_every = std::stoi(next());
      else if (a == "--window")
        o.window = std::stoul(next());
      else if (a == "--chunk")
        o.chunk = std::stod(next());
      else
        throw std::runtime_error("unknown argument " + a);
    }
    if (o.inst.empty())
      throw std::runtime_error("--inst is required");
    return o;
  }

  int replay(const Opts &o, Verifier &v)
  {
    std::ifstream f(o.replay);
    if (!f)
    {
      std::cerr << "cannot read " << o.replay << "\n";
      return 1;
    }
    std::string line;
    while (std::getline(f, line))
    {
      if (line.size() < 3)
        continue;
      const char kind = line[0];
      const size_t sp = line.find(' ', 2);
      const uint64_t ns = std::stoull(line.substr(2, sp - 2));
      const std::string rest = line.substr(sp + 1);
      if (kind == 'C' && rest.rfind("connected", 0) == 0)
        v.connected(ns);
      else if (kind == 'W')
      {
        if (!v.frame(rest, ns))
          v.connected(ns); // the live run reconnected here; a recording resumes with the next connect
      }
      else if (kind == 'R')
      {
        const size_t sp2 = rest.find(' ');
        if (rest.substr(0, sp2) == o.inst)
          v.rest(rest.substr(sp2 + 1), ns);
      }
    }
    v.finish();
    v.report(std::cout, true);
    return 0;
  }

  int live(const Opts &o, Verifier &v)
  {
    std::unique_ptr<std::ofstream> rec;
    if (!o.record.empty())
      rec = std::make_unique<std::ofstream>(o.record);
    std::mutex mtx;
    std::deque<std::pair<uint64_t, std::string>> rest_q;
    std::atomic<bool> stop{false};
    const uint64_t end = okx::now_ns() + uint64_t(o.minutes * 60e9);

    std::thread rest([&] {
      uint64_t next = okx::now_ns() + 3000000000ull;
      while (!stop)
      {
        if (okx::now_ns() >= next)
        {
          next += uint64_t(o.rest_every) * 1000000000ull;
          try
          {
            auto r = okx::https_request("www.okx.com", "GET", "/api/v5/market/books?instId=" + o.inst + "&sz=400");
            std::lock_guard<std::mutex> lk(mtx);
            rest_q.emplace_back(okx::now_ns(), std::move(r.body));
          }
          catch (const std::exception &e)
          {
            std::lock_guard<std::mutex> lk(mtx);
            rest_q.emplace_back(okx::now_ns(), std::string("{\"code\":\"error\",\"msg\":\"") + e.what() + "\"}");
          }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
    });

    uint64_t last_report = okx::now_ns();
    while (okx::now_ns() < end)
    {
      try
      {
        okx::WsClient ws("ws.okx.com", "443", "/ws/v5/public");
        ws.connect();
        ws.send(nlohmann::json{{"op", "subscribe"},
                               {"args",
                                {{{"channel", "books"}, {"instId", o.inst}},
                                 {{"channel", "trades"}, {"instId", o.inst}}}}}
                    .dump());
        const uint64_t c = okx::now_ns();
        v.connected(c);
        if (rec)
          *rec << "C " << c << " connected\n";
        bool ok = true;
        while (ok && okx::now_ns() < end)
        {
          auto text = ws.read();
          const uint64_t ns = okx::now_ns();
          if (rec)
          {
            for (auto &ch : text)
              if (ch == '\n')
                ch = ' ';
            *rec << "W " << ns << ' ' << text << '\n';
          }
          ok = v.frame(text, ns);
          std::deque<std::pair<uint64_t, std::string>> q;
          {
            std::lock_guard<std::mutex> lk(mtx);
            q.swap(rest_q);
          }
          for (auto &[rns, body] : q)
          {
            if (rec)
              *rec << "R " << rns << ' ' << o.inst << ' ' << body << '\n';
            v.rest(body, rns);
          }
          if (ns - last_report >= 60000000000ull)
          {
            last_report = ns;
            v.report(std::cerr, false);
          }
        }
        if (!ok)
        {
          if (rec)
            *rec << "C " << okx::now_ns() << " resubscribe\n";
          std::this_thread::sleep_for(std::chrono::seconds(1));
        }
      }
      catch (const std::exception &e)
      {
        std::cerr << "okx_verify " << o.inst << ": " << e.what() << "; reconnecting in 1 s\n";
        if (rec)
          *rec << "C " << okx::now_ns() << " error " << e.what() << "\n";
        std::this_thread::sleep_for(std::chrono::seconds(1));
      }
    }
    stop = true;
    rest.join();
    v.finish();
    v.report(std::cout, true);
    return 0;
  }
}

int main(int argc, char **argv)
{
  Opts o;
  try
  {
    o = parse(argc, argv);
  }
  catch (const std::exception &e)
  {
    std::cerr << e.what() << "\nusage: okx_verify --inst ID [--minutes N] [--ob] [--record FILE] | --replay FILE\n";
    return 2;
  }
  Verifier v(o);
  return o.replay.empty() ? live(o, v) : replay(o, v);
}
