/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "nasdaq_post_trade/SimDropServer.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "frame/som/TimedRunner.hpp"
#include "nasdaq_post_trade/drop_msgs.hpp"

namespace nasdaq_post_trade
{
  namespace soup_in = nasdaq::soup::from_nasdaq;
  namespace soup_out = nasdaq::soup::to_nasdaq;
  using Clock = std::chrono::steady_clock;

  namespace
  {
#ifdef MSG_NOSIGNAL
    constexpr int SEND_FLAGS = MSG_NOSIGNAL;
#else
    constexpr int SEND_FLAGS = 0;
#endif

    // A server packet: the generated struct with its length and type set,
    // then the variable-length tail.
    template <class M>
    std::string pack(M m, std::string_view tail = {})
    {
      m.MessageLength = uint16_t(sizeof(M) - 2 + tail.size());
      m.MessageType = M::type;
      std::string s(reinterpret_cast<const char *>(&m), sizeof m);
      s.append(tail);
      return s;
    }

    std::string lower(std::string_view s)
    {
      std::string r(s);
      for (auto &c : r) c = char(std::tolower((unsigned char)c));
      return r;
    }
  }

  SimDropServer::SimDropServer(SimDropConfig c) : cfg_(std::move(c)) {}

  SimDropServer::~SimDropServer()
  {
    runner_.reset();
    if (thread_.joinable())
    {
      thread_.request_stop();
      thread_.join();
    }
    close_client();
    if (listen_fd_ >= 0)
      ::close(listen_fd_);
  }

  bool SimDropServer::start(std::string *err)
  {
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0)
    {
      if (err) *err = std::string("socket: ") + std::strerror(errno);
      return false;
    }
    int one = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // loopback only
    a.sin_port = htons(uint16_t(cfg_.port));
    if (::bind(listen_fd_, reinterpret_cast<sockaddr *>(&a), sizeof a) != 0 ||
        ::listen(listen_fd_, 4) != 0)
    {
      if (err) *err = std::string("bind/listen: ") + std::strerror(errno);
      ::close(listen_fd_);
      listen_fd_ = -1;
      return false;
    }
    socklen_t len = sizeof a;
    ::getsockname(listen_fd_, reinterpret_cast<sockaddr *>(&a), &len);
    port_ = ntohs(a.sin_port);
    thread_ = std::jthread([this](std::stop_token st) { run(st); });
    return true;
  }

  void SimDropServer::publish(const std::string &line)
  {
    std::string l = line;
    while (!l.empty() && (l.back() == '\n' || l.back() == '\r'))
      l.pop_back();
    std::lock_guard<std::mutex> g(mu_);
    journal_.push_back(std::move(l));
  }

  void SimDropServer::drop_connection() { drop_ = true; }
  void SimDropServer::end_session() { ended_ = true; }

  std::vector<SimDropServer::Login> SimDropServer::logins() const
  {
    std::lock_guard<std::mutex> g(mu_);
    return logins_;
  }

  size_t SimDropServer::journal_size() const
  {
    std::lock_guard<std::mutex> g(mu_);
    return journal_.size();
  }

  std::string SimDropServer::line(uint64_t seq) const
  {
    std::lock_guard<std::mutex> g(mu_);
    return seq >= 1 && seq <= journal_.size() ? journal_[seq - 1] : std::string();
  }

  void SimDropServer::close_client()
  {
    if (client_fd_ >= 0)
      ::close(client_fd_);
    client_fd_ = -1;
    inbuf_.clear();
    logged_in_ = false;
  }

  bool SimDropServer::send_packet(const std::string &p)
  {
    size_t off = 0;
    while (off < p.size())
    {
      ssize_t n = ::send(client_fd_, p.data() + off, p.size() - off, SEND_FLAGS);
      if (n > 0)
      {
        off += size_t(n);
        continue;
      }
      if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
      {
        pollfd pf{client_fd_, POLLOUT, 0};
        if (::poll(&pf, 1, 1000) == 1)
          continue;
      }
      close_client();
      return false;
    }
    last_sent_ = Clock::now();
    return true;
  }

  void SimDropServer::on_client_bytes()
  {
    size_t off = 0;
    while (client_fd_ >= 0 && inbuf_.size() - off >= 3)
    {
      const auto *b = reinterpret_cast<const unsigned char *>(inbuf_.data() + off);
      const size_t total = size_t((b[0] << 8) | b[1]) + 2;
      if (inbuf_.size() - off < total)
        break;
      const char type = char(b[2]);
      std::string_view pkt(inbuf_.data() + off, total);
      off += total;

      if (type == soup_out::LoginRequest::type && pkt.size() >= sizeof(soup_out::LoginRequest))
      {
        soup_out::LoginRequest req;
        std::memcpy(&req, pkt.data(), sizeof req);
        Login lg{std::string(req.get_Username()), std::string(req.get_RequestedSession()), 0, 0};
        uint64_t want = 0;
        for (char c : req.get_RequestedSequenceNumber())
          want = std::isdigit((unsigned char)c) ? want * 10 + uint64_t(c - '0') : want;
        lg.requested_seq = want;

        char reject = 0;
        if (lower(req.get_Username()) != lower(cfg_.username) ||
            lower(req.get_Password()) != lower(cfg_.password))
          reject = 'A';
        else if (!lg.session.empty() && lg.session != cfg_.session)
          reject = 'S';
        if (reject)
        {
          {
            std::lock_guard<std::mutex> g(mu_);
            logins_.push_back(lg);
          }
          soup_in::LoginRejected j{};
          j.RejectReasonCode = reject;
          send_packet(pack(j));
          close_client();
          return;
        }

        uint64_t n;
        {
          std::lock_guard<std::mutex> g(mu_);
          n = journal_.size();
          if (want == 0)
            cursor_ = n == 0 ? 1 : n; // the most recently generated message
          else
            cursor_ = std::min<uint64_t>(want, n + 1);
          lg.accepted_seq = cursor_;
          logins_.push_back(lg);
        }
        soup_in::LoginAccepted a{};
        a.set_Session(cfg_.session);
        a.set_SequenceNumber(std::to_string(cursor_));
        if (send_packet(pack(a)))
          logged_in_ = true;
      }
      else if (type == soup_out::ClientHeartbeat::type)
        ++heartbeats_;
      else if (type == soup_out::LogoutRequest::type)
      {
        ++logouts_;
        close_client();
        return;
      }
      // Unsequenced Data and anything else: ignored, as DROP takes no input.
    }
    if (client_fd_ >= 0)
      inbuf_.erase(0, off);
  }

  void SimDropServer::run(std::stop_token st)
  {
    char chunk[4096];
    while (!st.stop_requested())
    {
      pollfd pf[2] = {{listen_fd_, POLLIN, 0}, {client_fd_, POLLIN, 0}};
      const int nfds = client_fd_ >= 0 ? 2 : 1;
      ::poll(pf, nfds, 20);

      if (pf[0].revents & POLLIN)
      {
        int fd = ::accept(listen_fd_, nullptr, nullptr);
        if (fd >= 0)
        {
          if (client_fd_ >= 0)
            ::close(fd); // one client at a time
          else
          {
#ifdef SO_NOSIGPIPE
            int one = 1;
            ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
            ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);
            client_fd_ = fd;
            ++connections_;
            last_sent_ = Clock::now();
          }
        }
      }
      if (nfds == 2 && client_fd_ >= 0 && (pf[1].revents & (POLLIN | POLLHUP | POLLERR)))
      {
        ssize_t n = ::recv(client_fd_, chunk, sizeof chunk, 0);
        if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR))
          close_client();
        else if (n > 0)
        {
          inbuf_.append(chunk, size_t(n));
          on_client_bytes();
        }
      }
      if (drop_.exchange(false))
        close_client();
      if (client_fd_ < 0 || !logged_in_)
        continue;

      // Sequenced Data from the cursor to the end of the journal.
      for (;;)
      {
        std::string l;
        {
          std::lock_guard<std::mutex> g(mu_);
          if (cursor_ > journal_.size())
            break;
          l = journal_[cursor_ - 1];
        }
        if (!send_packet(pack(soup_in::SequencedData{}, l)))
          break;
        ++cursor_;
      }
      if (client_fd_ < 0)
        continue;
      if (ended_)
      {
        send_packet(pack(soup_in::EndOfSession{}));
        close_client();
        continue;
      }
      if (Clock::now() - last_sent_ >= std::chrono::milliseconds(cfg_.heartbeat_ms))
        send_packet(pack(soup_in::ServerHeartbeat{}));
    }
  }

  void SimDropServer::start_trading(std::chrono::milliseconds every, std::vector<std::string> symbols)
  {
    {
      std::lock_guard<std::mutex> g(gen_mu_);
      symbols_ = std::move(symbols);
      every_ = every;
      if (symbols_.empty())
        symbols_ = {"SIMA"};
    }
    if (!runner_)
      runner_ = std::make_unique<frame::som::TimedRunner>();
    auto *r = runner_.get();
    r->after(every_, [this, r] { generate(r); });
  }

  // r, not runner_: while the runner is being destroyed runner_ is already
  // null, but r stays valid until its worker thread has been joined.
  void SimDropServer::generate(frame::som::TimedRunner *r)
  {
    std::vector<DropReport> out;
    {
      std::lock_guard<std::mutex> g(gen_mu_);
      const auto now = std::chrono::system_clock::now().time_since_epoch();
      const int64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count() %
                         (86400LL * 1'000'000'000);
      const std::string &sym = symbols_[n_exec_ % symbols_.size()];

      DropReport a;
      a.type = 'A';
      a.time_ns = ns / 1'000'000 * 1'000'000;
      a.source = "SIM01";
      a.order_token = "SIMU,T" + std::to_string(next_ref_);
      a.side = n_exec_ % 2 ? 'S' : 'B';
      a.shares = 100 * int64_t(1 + n_exec_ % 5);
      a.stock = sym;
      a.price_e4 = 100'0000 + int64_t(n_exec_ % 50) * 100;
      a.firm = "SIMF";
      a.reference = next_ref_++;
      a.match_tif = 0; // TIF: day
      a.capacity = 'A';
      a.clearing = ' ';
      out.push_back(a);

      DropReport e = a;
      e.type = 'E';
      e.match_tif = next_match_++;
      e.liquidity = 'R';
      out.push_back(e);
      executions_.push_back(e);
      ++n_exec_;

      if (n_exec_ % 7 == 0 && executions_.size() >= 2)
      {
        DropReport b = executions_[executions_.size() - 2];
        b.type = 'B';
        b.time_ns = a.time_ns;
        out.push_back(b);
      }
      if (n_exec_ % 11 == 0)
      {
        DropReport c = e;
        c.type = 'C';
        c.price_e4 += 100;
        out.push_back(c);
      }
    }
    for (const auto &rep : out)
      publish(rep);
    r->after(every_, [this, r] { generate(r); });
  }
}
