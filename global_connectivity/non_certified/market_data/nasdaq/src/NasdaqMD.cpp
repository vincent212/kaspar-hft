#include "nasdaq_md/act/NasdaqMD.hpp"

#include "actors/act/Timer.hpp"
#include "chutil/Time.hpp"
#include "frame/mda/msg/Data.hpp"
#include "logger/act/Logger.hpp"

#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace nasdaq_md
{
// ---- configuration ----------------------------------------------------------
FeedConfig config_from_ini(const boost::property_tree::ptree &s)
{
  FeedConfig c;
  const std::string market = s.get<std::string>("market", "NASDAQ");
  if (market == "NASDAQ")
    c.dispatch.venue = char(en::x::NASDAQ), c.dispatch.market_code = 'Q';
  else if (market == "NASDAQBX")
    c.dispatch.venue = char(en::x::NASDAQBX), c.dispatch.market_code = 'B';
  else if (market == "NASDAQPSX")
    c.dispatch.venue = char(en::x::NASDAQPSX), c.dispatch.market_code = 'X';
  else
    throw std::runtime_error("nasdaq_md: unknown market " + market);
  c.name = market;
  const std::string date = s.get<std::string>("date");
  c.dispatch.midnight_ns = et_midnight_epoch_ns(date.c_str());
  if (!c.dispatch.midnight_ns)
    throw std::runtime_error("nasdaq_md: date must be YYYYMMDD, got " + date);
  for (std::string sym : {s.get<std::string>("symbols", "")})
  {
    size_t b = 0;
    while (b < sym.size())
    {
      size_t e = sym.find(',', b);
      if (e == std::string::npos)
        e = sym.size();
      if (e > b)
        c.symbols.insert(sym.substr(b, e - b));
      b = e + 1;
    }
  }
  c.include_test_issues = s.get<bool>("include_test_issues", false);
  c.asset_prefix = s.get<std::string>("asset_prefix", "");
  c.sec_id_base = s.get<int32_t>("sec_id_base", c.sec_id_base);
  c.group_a = s.get<std::string>("group_a", "");
  c.group_b = s.get<std::string>("group_b", "");
  c.port_a = s.get<uint16_t>("port_a", 0);
  c.port_b = s.get<uint16_t>("port_b", 0);
  c.interface = s.get<std::string>("interface", "");
  c.rerequest_host = s.get<std::string>("rerequest_host", "");
  c.rerequest_port = s.get<uint16_t>("rerequest_port", 0);
  c.glimpse_host = s.get<std::string>("glimpse_host", "");
  c.glimpse_port = s.get<uint16_t>("glimpse_port", 0);
  c.glimpse_user = s.get<std::string>("glimpse_user", "");
  c.glimpse_password = s.get<std::string>("glimpse_password", "");
  c.glimpse_retry_ms = s.get<uint64_t>("glimpse_retry_ms", 1000);
  c.glimpse_retry_max_ms = s.get<uint64_t>("glimpse_retry_max_ms", 30000);
  c.sequencer.retry_ns = s.get<uint64_t>("rerequest_retry_ms", 50) * 1000000ull;
  c.sequencer.max_tries = s.get<int>("rerequest_max_tries", 5);
  c.sequencer.max_hole = s.get<uint64_t>("max_hole", 100000);
  c.sequencer.max_buffered = s.get<size_t>("max_buffered", 100000);
  c.sequencer.snapshot_on_late_join = s.get<bool>("snapshot_on_late_join", true);
  c.fast_send = s.get<bool>("fast_send", true);
  return c;
}

namespace act
{
NasdaqMD::NasdaqMD(const FeedConfig &cfg, std::vector<actors::Actor *> books,
                   actors::Actor *binrec)
    : cfg_(cfg), books_(std::move(books)), binrec_(binrec),
      ins_(refdata_registrar(cfg.dispatch.venue, cfg.symbols, cfg.include_test_issues,
                             cfg.asset_prefix, cfg.sec_id_base))
{
  snprintf(name_, sizeof name_, "NasdaqMD_%s", cfg_.name.c_str());
  glimpse_backoff_ns_ = cfg_.glimpse_retry_ms * 1000000ull;
  Dispatcher::Config dc = cfg_.dispatch;
  dc.log = [this](const std::string &s) { log_wrn("%s: %s", name_, s.c_str()); };
  if (!cfg_.rerequest_host.empty())
    req_ = std::make_unique<UdpRequester>(cfg_.rerequest_host, cfg_.rerequest_port);
  feed_ = std::make_unique<NasdaqFeed>(dc, cfg_.sequencer, ins_, static_cast<Sink &>(*this),
                                       req_ ? static_cast<Requester *>(this) : nullptr,
                                       cfg_.glimpse_host.empty() ? nullptr
                                                                 : static_cast<SnapshotSource *>(this));
  MESSAGE_HANDLER(actors::msg::Start, start_handler);
  MESSAGE_HANDLER(actors::msg::Shutdown, shutdown_handler);
  MESSAGE_HANDLER(mcast_recv::msg::ProcessQ<uint64_t>, packet_handler);
  MESSAGE_HANDLER(msg::GlimpseBytes, glimpse_handler);
  MESSAGE_HANDLER(actors::msg::Timeout, timeout_handler);
}

NasdaqMD::~NasdaqMD()
{
  running_ = false;
  if (glimpse_)
    glimpse_->stop();
  if (req_thread_.joinable())
    req_thread_.join();
  if (glimpse_thread_.joinable())
    glimpse_thread_.join();
}

void NasdaqMD::start_handler(const actors::msg::Start *) noexcept
{
  running_ = true;
  if (req_ && req_->ok())
  {
    // Retransmissions come back to the requesting socket (Mold p.5).
    req_thread_ = std::thread([this] {
      std::string pkt;
      while (running_)
        if (req_->receive(pkt, 200))
        {
          auto *m = new mcast_recv::msg::ProcessQ<uint64_t>();
          const size_t n = std::min(pkt.size(), m->buf.message.size());
          std::memcpy(m->buf.message.data(), pkt.data(), n);
          m->buf.len = n;
          m->buf.recv_ts = chutil::Time::epoch();
          send(m, this);
        }
    });
  }
  actors::act::Timer::wake_up_in(this, 0, 10);
  log_inf("%s started", name_);
}

void NasdaqMD::shutdown_handler(const actors::msg::Shutdown *) noexcept
{
  running_ = false;
  if (glimpse_)
    glimpse_->stop();
}

void NasdaqMD::packet_handler(const mcast_recv::msg::ProcessQ<uint64_t> *m) noexcept
{
  feed_->on_packet(m->buf.message.data(), m->buf.len, m->buf.recv_ts, chutil::Time::epoch());
}

void NasdaqMD::glimpse_handler(const msg::GlimpseBytes *m) noexcept
{
  if (!feed_->spinning() || m->session != feed_->spin_session())
    return;   // a spin no longer waited for (another session, or given up)
  if (m->failed)
  {
    log_err("%s: GLIMPSE session %s failed; going on without it", name_, m->session.c_str());
    feed_->on_glimpse_failed(m->session);
    schedule_glimpse_retry(chutil::Time::epoch());
    return;
  }
  feed_->on_glimpse_bytes(m->bytes.data(), m->bytes.size(), m->session);
  if (!feed_->spinning())
    glimpse_backoff_ns_ = cfg_.glimpse_retry_ms * 1000000ull;   // the spin completed
}

void NasdaqMD::schedule_glimpse_retry(uint64_t now_ns)
{
  glimpse_retry_at_ = now_ns + glimpse_backoff_ns_;
  log_wrn("%s: GLIMPSE spin again in %llu ms", name_,
          (unsigned long long)(glimpse_backoff_ns_ / 1000000ull));
  glimpse_backoff_ns_ = std::min(glimpse_backoff_ns_ * 2, cfg_.glimpse_retry_max_ms * 1000000ull);
}

void NasdaqMD::on_timer(uint64_t now_ns)
{
  feed_->on_timer(now_ns);
  if (glimpse_retry_at_ && now_ns >= glimpse_retry_at_)
  {
    glimpse_retry_at_ = 0;
    if (!feed_->recover())
      schedule_glimpse_retry(now_ns);
  }
}

void NasdaqMD::timeout_handler(const actors::msg::Timeout *) noexcept
{
  on_timer(chutil::Time::epoch());
  if (running_)
    actors::act::Timer::wake_up_in(this, 0, 10);
}

void NasdaqMD::to_book(int32_t asset, const bfile::l3_t &r)
{
  if (asset < 0 || size_t(asset) >= books_.size() || !books_[size_t(asset)])
    return;
  auto *book = books_[size_t(asset)];
  if (cfg_.fast_send)
  {
    frame::mda::msg::Data d(true);
    d.l3 = r;
    book->fast_send(d, this);
  }
  else
  {
    auto *d = new frame::mda::msg::Data(true);
    d->l3 = r;
    book->send(d, this);
  }
}

void NasdaqMD::to_recorder(const bfile::l3_t &r)
{
  if (!binrec_)
    return;
  auto *d = new frame::mda::msg::Data(true);
  d->l3 = r;
  binrec_->send(d, this);
}

void NasdaqMD::request(const std::string &session, uint64_t seq, uint16_t count)
{
  log_wrn("%s: re-request %s %llu +%u", name_, session.c_str(), (unsigned long long)seq, count);
  req_->request(session, seq, count);
}

bool NasdaqMD::start(const std::string &session)
{
  cancel();   // a spin still running is not the one wanted
  glimpse_ = std::make_unique<GlimpseClient>(cfg_.glimpse_host, cfg_.glimpse_port,
                                             cfg_.glimpse_user, cfg_.glimpse_password);
  log_wrn("%s: GLIMPSE spin from %s:%u for session %s", name_, cfg_.glimpse_host.c_str(),
          cfg_.glimpse_port, session.c_str());
  GlimpseClient *client = glimpse_.get();
  glimpse_thread_ = std::thread([this, client, session] {
    const bool ok = client->run([this, &session](const char *p, size_t n) {
      auto *m = new msg::GlimpseBytes();
      m->session = session;
      m->bytes.assign(p, n);
      send(m, this);
    });
    if (!ok)
    {
      auto *m = new msg::GlimpseBytes();
      m->session = session;
      m->failed = true;
      send(m, this);
    }
  });
  return true;
}

void NasdaqMD::cancel()
{
  if (glimpse_)
    glimpse_->stop();
  if (glimpse_thread_.joinable())
    glimpse_thread_.join();
}
} // namespace act
} // namespace nasdaq_md
