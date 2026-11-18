/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "nasdaq_post_trade/SoupSession.hpp"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <variant>

#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

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
    constexpr int SEND_FLAGS = 0; // SO_NOSIGPIPE is set on the socket instead
#endif

    template <class M>
    std::string encoded(M m)
    {
      char buf[64];
      const size_t n = soup_out::encode(buf, sizeof buf, m);
      return std::string(buf, n);
    }

    bool parse_seq(std::string_view s, uint64_t &v)
    {
      if (s.empty()) return false;
      v = 0;
      for (char c : s)
      {
        if (c < '0' || c > '9') return false;
        v = v * 10 + uint64_t(c - '0');
      }
      return true;
    }
  }

  std::string SoupSession::encode_login(const LoginParams &p)
  {
    soup_out::LoginRequest m{};
    m.set_Username(p.username);
    m.set_Password(p.password);
    m.set_RequestedSession(p.session);
    m.set_RequestedSequenceNumber(std::to_string(p.next_seq));
    return encoded(m);
  }

  std::string SoupSession::encode_heartbeat() { return encoded(soup_out::ClientHeartbeat{}); }
  std::string SoupSession::encode_logout() { return encoded(soup_out::LogoutRequest{}); }

  void SoupSession::start(const LoginParams &p, DropSessionListener *l)
  {
    stop();
    login_ = p;
    listener_ = l;
    session_ = p.session;
    next_seq_ = p.next_seq;
    thread_ = std::jthread([this](std::stop_token st) { run(st); });
  }

  void SoupSession::stop()
  {
    if (!thread_.joinable())
      return;
    {
      std::lock_guard<std::mutex> g(mu_);
      thread_.request_stop();
    }
    cv_.notify_all();
    thread_.join();
  }

  int SoupSession::connect_socket(std::string &err) const
  {
    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    const std::string port = std::to_string(cfg_.port);
    if (int rc = ::getaddrinfo(cfg_.host.c_str(), port.c_str(), &hints, &res); rc != 0)
    {
      err = std::string("resolve ") + cfg_.host + ": " + ::gai_strerror(rc);
      return -1;
    }
    int fd = -1;
    for (addrinfo *a = res; a; a = a->ai_next)
    {
      fd = ::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
      if (fd < 0)
        continue;
#ifdef SO_NOSIGPIPE
      int one = 1;
      ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
      int nd = 1;
      ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nd, sizeof nd);
      ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);
      int rc = ::connect(fd, a->ai_addr, a->ai_addrlen);
      if (rc != 0 && errno == EINPROGRESS)
      {
        pollfd p{fd, POLLOUT, 0};
        rc = ::poll(&p, 1, cfg_.connect_timeout_ms) == 1 ? 0 : -1;
        int so = 0;
        socklen_t sl = sizeof so;
        if (rc == 0 && (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &so, &sl) != 0 || so != 0))
          rc = -1, errno = so;
      }
      if (rc == 0)
        break;
      err = std::string("connect ") + cfg_.host + ":" + port + ": " + std::strerror(errno);
      ::close(fd);
      fd = -1;
    }
    ::freeaddrinfo(res);
    return fd;
  }

  bool SoupSession::send_all(int fd, const std::string &bytes)
  {
    size_t off = 0;
    while (off < bytes.size())
    {
      ssize_t n = ::send(fd, bytes.data() + off, bytes.size() - off, SEND_FLAGS);
      if (n > 0)
      {
        off += size_t(n);
        continue;
      }
      if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
      {
        pollfd p{fd, POLLOUT, 0};
        if (::poll(&p, 1, cfg_.server_timeout_ms) == 1)
          continue;
      }
      return false;
    }
    return true;
  }

  void SoupSession::run(std::stop_token st)
  {
    while (!st.stop_requested())
    {
      std::string err;
      int fd = connect_socket(err);
      End end = End::Lost;
      if (fd >= 0)
      {
        end = connection(st, fd);
        ::close(fd);
      }
      if (end == End::Stop || st.stop_requested())
        return;
      if (end == End::Final)
        return;
      listener_->on_disconnected(fd < 0 ? err : "connection lost", true);
      std::unique_lock<std::mutex> l(mu_);
      cv_.wait_for(l, st, std::chrono::milliseconds(cfg_.reconnect_ms), [] { return false; });
    }
  }

  SoupSession::End SoupSession::connection(std::stop_token st, int fd)
  {
    // After the first login, log in again to the session we were in, at the
    // next sequence number not yet delivered.
    LoginParams p = login_;
    p.session = session_;
    p.next_seq = next_seq_;
    if (!send_all(fd, encode_login(p)))
      return End::Lost;

    auto last_sent = Clock::now(), last_recv = Clock::now();
    bool logged_in = false;
    std::string buf;
    char chunk[65536];

    for (;;)
    {
      if (st.stop_requested())
      {
        if (logged_in)
          send_all(fd, encode_logout());
        return End::Stop;
      }

      pollfd pf{fd, POLLIN, 0};
      int pr = ::poll(&pf, 1, 100);
      auto now = Clock::now();
      if (pr < 0 && errno != EINTR)
        return End::Lost;
      if (pr > 0)
      {
        ssize_t n = ::recv(fd, chunk, sizeof chunk, 0);
        if (n == 0)
          return End::Lost; // closed by the server
        if (n < 0)
        {
          if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
            return End::Lost;
        }
        else
        {
          last_recv = now;
          buf.append(chunk, size_t(n));
        }
      }

      size_t off = 0;
      while (off < buf.size())
      {
        auto d = soup_in::decode(buf.data() + off, buf.size() - off);
        if (d.st == nasdaq::status::incomplete)
          break;
        if (d.st != nasdaq::status::ok)
        {
          if (d.size == 0)
            return End::Lost; // cannot find the next packet
          off += d.size;      // unknown packet type: skip it
          continue;
        }
        off += d.size;

        if (std::holds_alternative<soup_in::SequencedData>(d.msg))
        {
          if (!logged_in)
            continue; // not possible from a conforming server
          listener_->on_line(session_, next_seq_, std::string_view(d.tail, d.tail_len));
          ++next_seq_;
        }
        else if (auto *a = std::get_if<soup_in::LoginAccepted>(&d.msg))
        {
          uint64_t seq = 0;
          if (!parse_seq(a->get_SequenceNumber(), seq))
          {
            listener_->on_disconnected("Login Accepted with a bad sequence number", false);
            return End::Final;
          }
          session_ = std::string(a->get_Session());
          next_seq_ = seq;
          logged_in = true;
          listener_->on_login_accepted(session_, next_seq_);
        }
        else if (auto *j = std::get_if<soup_in::LoginRejected>(&d.msg))
        {
          listener_->on_login_rejected(j->RejectReasonCode);
          return End::Final;
        }
        else if (std::holds_alternative<soup_in::EndOfSession>(d.msg))
        {
          listener_->on_end_of_session(session_);
          return End::Final;
        }
        // ServerHeartbeat and Debug: nothing beyond last_recv
      }
      buf.erase(0, off);

      if (logged_in && now - last_sent >= std::chrono::milliseconds(cfg_.heartbeat_ms))
      {
        if (!send_all(fd, encode_heartbeat()))
          return End::Lost;
        last_sent = now;
      }
      if (now - last_recv >= std::chrono::milliseconds(cfg_.server_timeout_ms))
        return End::Lost;
    }
  }
}
