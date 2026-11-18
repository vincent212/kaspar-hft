/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "jnx_md/act/GlimpseClient.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <iostream>

#include "jnx_md/gen/glimpse.hpp"

namespace jnx_md::act
{
  namespace Soup = jnx::soup;
  namespace G = jnx::glimpse::from_jnx;

  GlimpseClient::GlimpseClient(const GlimpseConfig &cfg, actors::Actor *feed)
      : cfg_(cfg), name_(cfg.name + "Glimpse"), feed_(feed)
  {
    MESSAGE_HANDLER(msg::SnapshotRequest, request_handler);
  }

  void GlimpseClient::request_handler(const msg::SnapshotRequest *) noexcept
  {
    const std::string why = session();
    if (!why.empty())
    {
      std::cerr << name_ << ": " << why << std::endl;
      feed_->send(new msg::GlimpseFailed(why), this);
    }
  }

  std::string GlimpseClient::session()
  {
    using clock = std::chrono::steady_clock;
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
      return "socket failed";
    struct Closer
    {
      int fd;
      ~Closer() { ::close(fd); }
    } closer{fd};

    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(cfg_.port);
    if (::inet_pton(AF_INET, cfg_.host.c_str(), &to.sin_addr) != 1)
      return "bad GLIMPSE address " + cfg_.host;
    if (::connect(fd, reinterpret_cast<sockaddr *>(&to), sizeof(to)) != 0)
      return "connect to " + cfg_.host + " failed";

    auto put = [&](const char *b, size_t n) {
      return n && ::send(fd, b, n, 0) == ssize_t(n);
    };
    char out[64];
    {
      Soup::to_jnx::LoginRequestPacket l{};
      l.set_Username(cfg_.username);
      l.set_Password(cfg_.password);
      l.set_RequestedSession("");            // must be blank (GLIMPSE 4)
      l.set_RequestedSequenceNumber("0");    // as the sample client sent
      if (!put(out, Soup::to_jnx::encode(out, sizeof(out), l)))
        return "login send failed";
    }
    auto last_sent = clock::now(), last_read = clock::now();
    std::string buf;
    char in[65536];
    for (;;)
    {
      pollfd p{fd, POLLIN, 0};
      const int r = ::poll(&p, 1, 200);
      const auto now = clock::now();
      if (r > 0)
      {
        const ssize_t n = ::recv(fd, in, sizeof(in), 0);
        if (n <= 0)
          return "GLIMPSE closed the connection before End of Snapshot";
        last_read = now;
        feed_->send(new msg::GlimpseData(std::string(in, size_t(n))), this);
        // Follow the framing only to see End of Snapshot, Login Rejected and
        // End of Session; the feed decodes the messages.
        buf.append(in, size_t(n));
        size_t off = 0;
        for (;;)
        {
          const auto d = Soup::from_jnx::decode(buf.data() + off, buf.size() - off);
          if (d.st == jnx::status::incomplete || d.size == 0)
            break;
          off += d.size;
          if (std::holds_alternative<Soup::from_jnx::LoginRejectedPacket>(d.msg))
            return "GLIMPSE login rejected";
          if (std::holds_alternative<Soup::from_jnx::EndOfSessionPacket>(d.msg))
            return "GLIMPSE ended the session before End of Snapshot";
          if (std::holds_alternative<Soup::from_jnx::SequencedDataPacket>(d.msg) && d.tail_len &&
              d.tail[0] == G::EndOfSnapshotMessage::type)
          {
            Soup::to_jnx::LogoutRequestPacket o{};
            put(out, Soup::to_jnx::encode(out, sizeof(out), o));
            return {};
          }
        }
        buf.erase(0, off);
      }
      else if (r < 0)
        return "poll failed";
      if (now - last_read > std::chrono::seconds(15))
        return "GLIMPSE silent for 15 s";   // SoupBinTCP 4.3
      if (now - last_sent > std::chrono::seconds(1))
      {
        Soup::to_jnx::ClientHeartbeatPacket h{};
        if (!put(out, Soup::to_jnx::encode(out, sizeof(out), h)))
          return "heartbeat send failed";
        last_sent = now;
      }
    }
  }
}
