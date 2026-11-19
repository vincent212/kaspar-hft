/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "coinbase_exchange/act/CoinbaseExchangeFeed.hpp"

#include <cmath>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include "enum/e_names.hpp"
#include "frame/mda/msg/Data.hpp"
#include "sim/WsNet.hpp"

namespace coinbase_exchange
{

using namespace sim::wsnet;

namespace
{
  // The heartbeat channel sends one message a second per product.
  constexpr auto kIdleTimeout = std::chrono::seconds(30);
  constexpr size_t kMaxMessage = 64 * 1024 * 1024;

  std::string fmt_px(int64_t ticks, const ProductSpec &s)
  {
    return format_scaled(ticks * s.price_increment, s.price_decimals);
  }

  // ---- production handlers ----------------------------------------------------------------

  class L3Handler : public FeedHandler
  {
  public:
    L3Handler(const CoinbaseExchangeSync::Config &cfg, CoinbaseExchangeSync::Emit emit) : sync_(cfg, std::move(emit))
    {
    }
    void on_frame(const nlohmann::json &m, uint64_t recv_ns) override { sync_.on_message(m, recv_ns); }
    void on_rest(int level, const nlohmann::json &body, uint64_t, uint64_t recv_ns) override
    {
      if (level == 3)
      {
        // A snapshot the sync did not ask for (a periodic fetch, or one that
        // was in flight when the sync caught up) would rebuild a book that is
        // in sync and could move its sequence backwards.
        if (!sync_.need_snapshot())
          return;
        auto r = sync_.on_snapshot(body, recv_ns);
        std::cerr << "CoinbaseExchangeSync " << sync_.spec().product << ": snapshot sequence "
                  << body.value("sequence", int64_t(-1)) << ": "
                  << (r == CoinbaseExchangeSync::SnapResult::Applied  ? "applied"
                      : r == CoinbaseExchangeSync::SnapResult::TooOld ? "too old, fetching another"
                                                                       : "malformed")
                  << std::endl;
      }
      else if (level == 0)
        sync_.on_product_status(body, recv_ns);
    }
    void on_disconnect(uint64_t now_ns) override { sync_.on_disconnect(now_ns); }
    bool need_snapshot() const override { return sync_.need_snapshot(); }
    std::string status_line() const override
    {
      const auto &s = sync_.stats();
      std::ostringstream o;
      o << sync_.spec().product << (sync_.synced() ? " synced" : " syncing") << " seq " << sync_.last_seq() << " bid "
        << fmt_px(sync_.best(true), sync_.spec()) << " ask " << fmt_px(sync_.best(false), sync_.spec()) << " orders "
        << sync_.orders() << " applied " << s.applied << " gaps " << s.gaps << " resyncs " << s.resyncs
        << " crossed " << s.crossed_events << " dups " << s.duplicates << " reordered " << s.reordered;
      return o.str();
    }

  private:
    CoinbaseExchangeSync sync_;
  };

  class L2Handler : public FeedHandler
  {
  public:
    L2Handler(const ProductSpec &spec, double chunk, size_t depth, std::function<void(const bfile::l3_t &)> emit)
        : spec_(spec),
          mbp_({spec.sec_id, spec.venue, double(spec.price_increment) * std::pow(10.0, -spec.price_decimals), chunk},
               std::move(emit)),
          sync_(spec, mbp_, depth)
    {
    }
    void on_frame(const nlohmann::json &m, uint64_t recv_ns) override { sync_.on_message(m, recv_ns); }
    void on_rest(int, const nlohmann::json &, uint64_t, uint64_t) override {}
    void on_disconnect(uint64_t) override { sync_.reset(); }
    bool need_snapshot() const override { return false; } // level2_batch sends its own snapshot
    std::string status_line() const override
    {
      const auto &s = sync_.stats();
      std::ostringstream o;
      o << spec_.product << (sync_.synced() ? " synced" : " syncing") << " bid " << fmt_px(sync_.best(true), spec_)
        << " ask " << fmt_px(sync_.best(false), spec_) << " updates " << s.updates << " trades " << s.trades
        << " trades missing " << s.trades_missing << " crossed " << s.crossed_updates;
      return o.str();
    }

  private:
    ProductSpec spec_;
    sim::Mbp2Mbo mbp_;
    CoinbaseExchangeL2Sync sync_;
  };
}

std::unique_ptr<FeedHandler> make_l3_handler(const CoinbaseExchangeSync::Config &cfg, CoinbaseExchangeSync::Emit emit)
{
  return std::make_unique<L3Handler>(cfg, std::move(emit));
}

std::unique_ptr<FeedHandler> make_l2_handler(const ProductSpec &spec, double chunk, size_t depth,
                                             std::function<void(const bfile::l3_t &)> emit)
{
  return std::make_unique<L2Handler>(spec, chunk, depth, std::move(emit));
}

// ---- configuration -------------------------------------------------------------------------

bool CoinbaseExchangeFeed::Config::from_ptree(const boost::property_tree::ptree &pt, Config &out, std::string &err)
{
  out.product = pt.get<std::string>("product", "");
  if (out.product.empty())
  {
    err = "product is required";
    return false;
  }
  auto mode = pt.get<std::string>("mode", "l2");
  if (mode == "full")
    out.mode = Mode::L3Full;
  else if (mode == "level3")
    out.mode = Mode::L3Level3;
  else if (mode == "l2")
    out.mode = Mode::L2Public;
  else
  {
    err = "mode must be full, level3 or l2";
    return false;
  }
  auto env = parse_environment(pt.get<std::string>("environment", "production"));
  if (!env)
  {
    err = "environment must be sandbox or production";
    return false;
  }
  out.environment = *env;
  out.ws_host = pt.get<std::string>("ws_host", "");
  out.rest_host = pt.get<std::string>("rest_host", "");
  out.key_file = pt.get<std::string>("key_file", "");
  out.record_dir = pt.get<std::string>("record_dir", "");
  if (out.mode != Mode::L2Public && out.key_file.empty())
    out.key_file = default_key_path();
  return true;
}

// ---- the actor ----------------------------------------------------------------------------

CoinbaseExchangeFeed::CoinbaseExchangeFeed(const Config &cfg, std::unique_ptr<FeedHandler> handler)
    : cfg_(cfg), handler_(std::move(handler))
{
  snprintf(name, sizeof(name), "CbxFeed_%s", cfg_.product.c_str());
  auto ep = endpoints(cfg_.environment);
  const bool signed_feed = cfg_.mode != Mode::L2Public;
  ws_host_ = !cfg_.ws_host.empty() ? cfg_.ws_host : signed_feed ? ep.ws_direct_host : ep.ws_host;
  rest_host_ = !cfg_.rest_host.empty() ? cfg_.rest_host : ep.rest_host;
  if (signed_feed && !cfg_.key_file.empty())
  {
    Credentials c;
    std::string err;
    if (load_credentials(cfg_.key_file, c, err))
      creds_ = c;
    else
      std::cerr << name << ": " << err << std::endl;
  }
  next_periodic_ns_.assign(cfg_.periodic_rest.size(), 0);
  MESSAGE_HANDLER(actors::msg::Start, start_handler);
  MESSAGE_HANDLER(actors::msg::Continue, continue_handler);
}

CoinbaseExchangeFeed::~CoinbaseExchangeFeed()
{
  if (rest_thread_.joinable())
    rest_thread_.join();
}

bool CoinbaseExchangeFeed::check(std::string &err) const
{
  if (cfg_.mode != Mode::L2Public && !creds_)
  {
    err = "the L3 channels need credentials (" + cfg_.key_file + ")";
    return false;
  }
  const Credentials *c = creds_ ? &*creds_ : nullptr;
  return host_allowed(cfg_.environment, ws_host_, c, err) && host_allowed(cfg_.environment, rest_host_, c, err);
}

void CoinbaseExchangeFeed::start_handler(const actors::msg::Start *) noexcept
{
  send(new actors::msg::Continue(), this);
}

std::string CoinbaseExchangeFeed::subscribe_message() const
{
  nlohmann::json sub{{"type", "subscribe"}, {"product_ids", nlohmann::json::array({cfg_.product})}};
  switch (cfg_.mode)
  {
  case Mode::L3Full: sub["channels"] = nlohmann::json::array({"full", "heartbeat"}); break;
  case Mode::L3Level3: sub["channels"] = nlohmann::json::array({"level3", "heartbeat"}); break;
  case Mode::L2Public: sub["channels"] = nlohmann::json::array({"level2_batch", "matches", "heartbeat"}); break;
  }
  if (cfg_.mode != Mode::L2Public && creds_)
  {
    // GET /users/self/verify signed as a REST request (websocket-feed/authentication)
    char ts[32];
    std::snprintf(ts, sizeof(ts), "%.3f", double(now_ns()) / 1e9);
    auto sig = sign(creds_->secret, rest_prehash(ts, "GET", "/users/self/verify", ""));
    sub["signature"] = sig ? *sig : std::string();
    sub["key"] = creds_->key;
    sub["passphrase"] = creds_->passphrase;
    sub["timestamp"] = ts;
  }
  return sub.dump();
}

void CoinbaseExchangeFeed::record_rest(const RestJob &j)
{
  if (!rest_rec_.is_open())
    return;
  rest_rec_ << j.sent_ns << '\t' << j.recv_ns << '\t' << cfg_.product << '\t' << j.level << '\t'
            << (j.error.empty() ? j.body : "#error " + j.error) << '\n';
}

void CoinbaseExchangeFeed::start_rest(int level)
{
  if (rest_busy_.exchange(true))
    return;
  if (rest_thread_.joinable())
    rest_thread_.join();
  std::string target = level == 0 ? "/products/" + cfg_.product : "/products/" + cfg_.product + "/book?level=" +
                                                                       std::to_string(level);
  rest_thread_ = std::thread([this, level, target] {
    RestJob j;
    j.level = level;
    j.sent_ns = now_ns();
    try
    {
      j.body = https_get(rest_host_, target);
    }
    catch (const std::exception &e)
    {
      j.error = e.what();
    }
    j.recv_ns = now_ns();
    std::lock_guard<std::mutex> l(rest_mu_);
    rest_done_.push_back(std::move(j));
    rest_busy_ = false;
  });
}

// On the actor's thread, between frames: hand finished REST answers to the
// handler, and start the next fetch that is due.
void CoinbaseExchangeFeed::poll_rest(uint64_t now)
{
  std::deque<RestJob> done;
  {
    std::lock_guard<std::mutex> l(rest_mu_);
    done.swap(rest_done_);
  }
  for (auto &j : done)
  {
    record_rest(j);
    if (!j.error.empty())
    {
      std::cerr << get_name() << ": REST level " << j.level << ": " << j.error << std::endl;
      if (j.level == 3)
        next_snapshot_ns_ = now + 5000000000ull;
      continue;
    }
    nlohmann::json body;
    try
    {
      body = nlohmann::json::parse(j.body);
    }
    catch (const std::exception &e)
    {
      std::cerr << get_name() << ": REST level " << j.level << ": bad JSON" << std::endl;
      continue;
    }
    const bool wanted = j.level == 3 && handler_->need_snapshot();
    handler_->on_rest(j.level, body, j.sent_ns, j.recv_ns);
    if (wanted)
    {
      if (handler_->need_snapshot())
      {
        // too old: the venue's snapshot predates the buffered stream
        backoff_s_ = backoff_s_ ? std::min(60, backoff_s_ * 2) : cfg_.snapshot_backoff_s;
        next_snapshot_ns_ = now + uint64_t(backoff_s_) * 1000000000ull;
      }
      else
        backoff_s_ = 0;
    }
  }
  if (rest_busy_)
    return;
  if (cfg_.mode != Mode::L2Public && handler_->need_snapshot() && now >= next_snapshot_ns_)
  {
    start_rest(3);
    return;
  }
  for (size_t i = 0; i < cfg_.periodic_rest.size(); ++i)
    if (now >= next_periodic_ns_[i])
    {
      next_periodic_ns_[i] = now + uint64_t(cfg_.periodic_rest[i].second) * 1000000000ull;
      start_rest(cfg_.periodic_rest[i].first);
      return;
    }
}

void CoinbaseExchangeFeed::continue_handler(const actors::msg::Continue *) noexcept
{
  std::string err;
  if (!check(err))
  {
    std::cerr << get_name() << ": " << err << std::endl;
    finished_ = true;
    return;
  }
  if (!cfg_.record_dir.empty())
  {
    ws_rec_.open(cfg_.record_dir + "/ws.txt", std::ios::app);
    rest_rec_.open(cfg_.record_dir + "/rest.txt", std::ios::app);
  }
  started_ns_ = now_ns();
  int delay_s = 1;
  while (!terminate_called && !finished_)
  {
    try
    {
      run_connection();
      delay_s = 1;
    }
    catch (const std::exception &e)
    {
      std::cerr << get_name() << ": " << e.what() << "; reconnecting in " << delay_s << " s" << std::endl;
      if (ws_rec_.is_open())
        ws_rec_ << now_ns() << "\t#close " << e.what() << '\n';
    }
    handler_->on_disconnect(now_ns());
    if (finished_)
      break;
    std::this_thread::sleep_for(std::chrono::seconds(delay_s));
    delay_s = std::min(60, delay_s * 2);
  }
  if (rest_thread_.joinable())
    rest_thread_.join();
  ws_rec_.flush();
  rest_rec_.flush();
  finished_ = true;
}

void CoinbaseExchangeFeed::run_connection()
{
  net::io_context io;
  auto tls = make_tls();
  WsStream ws(io, tls);
  ws.read_message_max(kMaxMessage);
  ws_connect(io, ws, ws_host_, "443", "/", kIdleTimeout);
  ws_write(io, ws, subscribe_message());
  std::cerr << get_name() << ": connected wss://" << ws_host_ << std::endl;
  if (ws_rec_.is_open())
    ws_rec_ << now_ns() << "\t#open " << ws_host_ << '\n';
  next_snapshot_ns_ = 0;
  backoff_s_ = 0;
  if (cfg_.mode != Mode::L2Public)
    start_rest(0); // product status, then the snapshot once the stream is buffering

  beast::flat_buffer buf;
  while (!terminate_called)
  {
    std::string frame = ws_read(io, ws, buf);
    const uint64_t t = now_ns();
    if (ws_rec_.is_open())
      ws_rec_ << t << '\t' << frame << '\n';
    nlohmann::json m;
    try
    {
      m = nlohmann::json::parse(frame);
    }
    catch (const std::exception &)
    {
      std::cerr << get_name() << ": frame is not JSON: " << frame.substr(0, 120) << std::endl;
      continue;
    }
    if (m.is_object() && m.value("type", std::string()) == "error")
      throw std::runtime_error("venue error: " + m.value("message", std::string()) + " " +
                               m.value("reason", std::string()));
    handler_->on_frame(m, t);
    poll_rest(t);
    if (t - last_dump_ns_ >= 1000000000ull)
    {
      last_dump_ns_ = t;
      if (cfg_.dump)
        std::cout << get_name() << " " << handler_->status_line() << std::endl;
      ws_rec_.flush();
      rest_rec_.flush();
    }
    if (cfg_.run_seconds > 0 && t - started_ns_ >= uint64_t(cfg_.run_seconds) * 1000000000ull)
    {
      finished_ = true;
      break;
    }
  }
  beast::error_code ec;
  ws.close(websocket::close_code::normal, ec);
}

}

actors::Actor *create_CoinbaseExchangeFeed(const coinbase_exchange::CoinbaseExchangeFeed::Config &cfg,
                                           const coinbase_exchange::ProductSpec &spec, actors::Actor *ob,
                                           double l2_chunk, size_t l2_depth)
{
  using namespace coinbase_exchange;
  auto sender = std::make_shared<actors::Actor *>(nullptr);
  auto emit = [ob, sender](const bfile::l3_t &r) {
    if (!ob)
      return;
    auto msg = new frame::mda::msg::Data(true);
    msg->l3 = r;
    ob->send(msg, *sender);
  };
  std::unique_ptr<FeedHandler> h;
  if (cfg.mode == CoinbaseExchangeFeed::Mode::L2Public)
    h = make_l2_handler(spec, l2_chunk, l2_depth, emit);
  else
    h = make_l3_handler(CoinbaseExchangeSync::Config{spec}, emit);
  auto feed = new CoinbaseExchangeFeed(cfg, std::move(h));
  std::string err;
  if (!feed->check(err))
  {
    std::cerr << "CoinbaseExchangeFeed: not created: " << err << std::endl;
    delete feed;
    return nullptr;
  }
  *sender = feed;
  return feed;
}
