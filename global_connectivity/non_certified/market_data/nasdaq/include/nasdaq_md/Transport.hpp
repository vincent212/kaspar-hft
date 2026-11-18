#pragma once
// Packet sources and the two client transports of the feed.
//
//   SampleReader    Nasdaq's public sample files: ITCH messages, each after a
//                   2-byte big-endian length, no MoldUDP64 (feed.md section 3).
//   MoldPacketizer  wraps such messages into MoldUDP64 packets (N per packet),
//                   the stand-in for the multicast stream in replay and tests.
//   PcapReader      UDP payloads of a classic pcap file (Ethernet or raw IPv4).
//   PcapWriter      writes MoldUDP64 packets as a pcap, for the pcap path's test.
//   UdpRequester    MoldUDP64 Request Packets by UDP unicast; replies are read
//                   from the same socket (Mold p.5).
//   GlimpseClient   GLIMPSE over SoupBinTCP: connect, log in for sequence 1,
//                   read the spin, send client heartbeats.
//
// The multicast receiver is mcast_recv's SocketReader<uint64_t, 10>
// (create_SocketReader_64_10), which the actor wires to itself.
#include "moldudp64.hpp"
#include "nasdaq_md/NasdaqFeed.hpp"

#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace nasdaq_md
{
class SampleReader
{
public:
  explicit SampleReader(FILE *f) : f_(f), buf_(1 << 22) {}
  // The next message block (length prefix included), or false at the end.
  // A trailing partial message is counted in truncated().
  bool next(const char *&p, size_t &len);
  uint64_t bytes() const { return bytes_; }
  bool truncated() const { return truncated_; }

private:
  FILE *f_;
  std::vector<char> buf_;
  size_t have_ = 0, pos_ = 0;
  uint64_t bytes_ = 0;
  bool truncated_ = false, eof_ = false;
};

class MoldPacketizer
{
public:
  MoldPacketizer(std::string session, uint64_t first_seq, unsigned per_packet,
                 size_t max_payload = 1400)
      : session_(std::move(session)), seq_(first_seq), per_(per_packet), max_(max_payload) {}

  // Adds one message block; returns true when a packet is complete (packet()).
  bool add(const char *block, size_t len);
  // Completes the current packet, if it holds anything.
  bool flush();
  const std::string &packet() const { return pkt_; }
  uint64_t next_seq() const { return seq_ + count_; }
  unsigned pending() const { return count_; }   // messages not yet in a packet
  void set_session(std::string s) { session_ = std::move(s); }
  void set_seq(uint64_t s) { seq_ = s; }

  // A packet with no messages: heartbeat (count 0) or end of session (0xFFFF).
  static std::string header_only(const std::string &session, uint64_t seq, uint16_t count);

private:
  std::string session_;
  uint64_t seq_;
  unsigned per_;
  size_t max_;
  std::string body_, pkt_;
  uint16_t count_ = 0;
  void seal();
};

class PcapReader
{
public:
  explicit PcapReader(const std::string &path);
  ~PcapReader();
  bool ok() const { return f_ != nullptr; }
  // The next UDP payload; dst_port in host order; ts in epoch ns.
  bool next(std::string &payload, uint16_t &dst_port, uint64_t &ts_ns);

private:
  FILE *f_ = nullptr;
  bool swap_ = false, nsec_ = false;
  uint32_t linktype_ = 1;
};

class PcapWriter
{
public:
  explicit PcapWriter(const std::string &path);
  ~PcapWriter();
  void write(const std::string &udp_payload, uint16_t dst_port, uint64_t ts_ns);

private:
  FILE *f_ = nullptr;
};

class UdpRequester : public Requester
{
public:
  UdpRequester(const std::string &host, uint16_t port);
  ~UdpRequester() override;
  bool ok() const { return fd_ >= 0; }
  void request(const std::string &session, uint64_t seq, uint16_t count) override;
  // Waits up to timeout_ms for one reply packet.
  bool receive(std::string &pkt, int timeout_ms);
  int fd() const { return fd_; }

private:
  int fd_ = -1;
};

class GlimpseClient
{
public:
  GlimpseClient(std::string host, uint16_t port, std::string user, std::string password)
      : host_(std::move(host)), port_(port), user_(std::move(user)), pass_(std::move(password)) {}
  ~GlimpseClient();
  // Connects, logs in and reads until End of Snapshot's packet has been
  // passed on and the server ends the session, or the connection fails.
  // Returns false on failure. on_bytes gets the raw SoupBinTCP stream.
  bool run(const std::function<void(const char *, size_t)> &on_bytes, int timeout_s = 120);
  void stop();

private:
  std::string host_;
  uint16_t port_;
  std::string user_, pass_;
  int fd_ = -1;
  volatile bool stop_ = false;
};
} // namespace nasdaq_md
