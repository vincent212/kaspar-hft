#include "nasdaq_md/Transport.hpp"

#include <arpa/inet.h>
#include <cstring>
#include <ctime>
#include <stdexcept>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace nasdaq_md
{
namespace
{
// A peer that closed must not kill the process on the next send.
#ifdef MSG_NOSIGNAL
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif
void no_sigpipe(int fd)
{
#ifdef SO_NOSIGPIPE
  const int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#else
  (void)fd;
#endif
}
} // namespace

// ---- SampleReader -----------------------------------------------------------
bool SampleReader::next(const char *&p, size_t &len)
{
  for (;;)
  {
    if (have_ - pos_ >= 2)
    {
      const size_t n = size_t((uint8_t(buf_[pos_]) << 8) | uint8_t(buf_[pos_ + 1])) + 2;
      if (have_ - pos_ >= n)
      {
        p = buf_.data() + pos_;
        len = n;
        pos_ += n;
        return true;
      }
    }
    if (eof_)
    {
      truncated_ = have_ > pos_;
      return false;
    }
    std::memmove(buf_.data(), buf_.data() + pos_, have_ - pos_);
    have_ -= pos_;
    pos_ = 0;
    const size_t r = fread(buf_.data() + have_, 1, buf_.size() - have_, f_);
    if (r == 0)
      eof_ = true;
    have_ += r;
    bytes_ += r;
  }
}

// ---- MoldPacketizer ---------------------------------------------------------
bool MoldPacketizer::add(const char *block, size_t len)
{
  bool sealed = false;
  if (count_ && body_.size() + len > max_)
  {
    seal();
    sealed = true;
  }
  body_.append(block, len);
  ++count_;
  if (!sealed && count_ >= per_)
  {
    seal();
    return true;
  }
  return sealed;
}

bool MoldPacketizer::flush()
{
  if (!count_)
    return false;
  seal();
  return true;
}

void MoldPacketizer::seal()
{
  pkt_ = header_only(session_, seq_, count_);
  pkt_ += body_;
  seq_ += count_;
  count_ = 0;
  body_.clear();
}

std::string MoldPacketizer::header_only(const std::string &session, uint64_t seq, uint16_t count)
{
  nasdaq::mold::from_nasdaq::DownstreamPacketHeader h{};
  nasdaq::set_str(h.Session, session);
  h.SequenceNumber = seq;
  h.MessageCount = count;
  return std::string(reinterpret_cast<const char *>(&h), sizeof h);
}

// ---- pcap -------------------------------------------------------------------
PcapReader::PcapReader(const std::string &path)
{
  try
  {
    pcap_ = std::make_unique<market_data::Pcap>(path);
    udp_ = pcap_->udp();
  }
  catch (const std::runtime_error &)
  {
    pcap_.reset();
  }
}

bool PcapReader::next(std::string &payload, uint16_t &dst_port, uint64_t &ts_ns)
{
  if (!pcap_ || pos_ >= udp_.size())
    return false;
  const auto &p = udp_[pos_++];
  payload.assign(p.payload.data(), p.payload.size());
  dst_port = p.dport;
  ts_ns = p.ts_ns;
  return true;
}

PcapWriter::PcapWriter(const std::string &path)
{
  f_ = fopen(path.c_str(), "wb");
  if (!f_)
    return;
  const uint32_t gh[6] = {0xa1b23c4d, 0x00040002u, 0, 0, 65535, 1};
  fwrite(gh, 4, 6, f_);
}

PcapWriter::~PcapWriter()
{
  if (f_)
    fclose(f_);
}

void PcapWriter::write(const std::string &p, uint16_t dst_port, uint64_t ts_ns)
{
  if (!f_)
    return;
  std::vector<uint8_t> rec(14 + 20 + 8 + p.size(), 0);
  rec[12] = 0x08;   // IPv4
  uint8_t *ip = rec.data() + 14;
  ip[0] = 0x45;
  const uint16_t tot = uint16_t(20 + 8 + p.size());
  ip[2] = uint8_t(tot >> 8);
  ip[3] = uint8_t(tot);
  ip[8] = 64;
  ip[9] = 17;
  ip[12] = 10, ip[15] = 1;           // 10.0.0.1 -> 233.54.12.1
  ip[16] = 233, ip[17] = 54, ip[18] = 12, ip[19] = 1;
  uint8_t *udp = ip + 20;
  udp[0] = 0x30, udp[1] = 0x39;
  udp[2] = uint8_t(dst_port >> 8);
  udp[3] = uint8_t(dst_port);
  const uint16_t ul = uint16_t(8 + p.size());
  udp[4] = uint8_t(ul >> 8);
  udp[5] = uint8_t(ul);
  std::memcpy(udp + 8, p.data(), p.size());
  const uint32_t rh[4] = {uint32_t(ts_ns / 1000000000ull), uint32_t(ts_ns % 1000000000ull),
                          uint32_t(rec.size()), uint32_t(rec.size())};
  fwrite(rh, 4, 4, f_);
  fwrite(rec.data(), 1, rec.size(), f_);
}

// ---- UdpRequester -----------------------------------------------------------
UdpRequester::UdpRequester(const std::string &host, uint16_t port)
{
  addrinfo hints{}, *res = nullptr;
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res)
    return;
  fd_ = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd_ >= 0 && connect(fd_, res->ai_addr, res->ai_addrlen) != 0)
  {
    close(fd_);
    fd_ = -1;
  }
  freeaddrinfo(res);
}

UdpRequester::~UdpRequester()
{
  if (fd_ >= 0)
    close(fd_);
}

void UdpRequester::request(const std::string &session, uint64_t seq, uint16_t count)
{
  if (fd_ < 0)
    return;
  nasdaq::mold::to_nasdaq::RequestPacket r{};
  r.set_Session(session);
  r.SequenceNumber = seq;
  r.RequestedMessageCount = count;
  char out[32];
  const size_t n = nasdaq::mold::to_nasdaq::encode(out, sizeof out, r);
  (void)::send(fd_, out, n, 0);
}

bool UdpRequester::receive(std::string &pkt, int timeout_ms)
{
  if (fd_ < 0)
    return false;
  pollfd pfd{fd_, POLLIN, 0};
  if (poll(&pfd, 1, timeout_ms) <= 0)
    return false;
  char b[65536];
  const ssize_t n = recv(fd_, b, sizeof b, 0);
  if (n <= 0)
    return false;
  pkt.assign(b, size_t(n));
  return true;
}

// ---- GlimpseClient ----------------------------------------------------------
GlimpseClient::~GlimpseClient()
{
  if (fd_ >= 0)
    close(fd_);
}

void GlimpseClient::stop() { stop_ = true; }

bool GlimpseClient::run(const std::function<void(const char *, size_t)> &on_bytes, int timeout_s)
{
  addrinfo hints{}, *res = nullptr;
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  if (getaddrinfo(host_.c_str(), std::to_string(port_).c_str(), &hints, &res) != 0 || !res)
    return false;
  fd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (fd_ >= 0)
    no_sigpipe(fd_);
  const bool connected = fd_ >= 0 && connect(fd_, res->ai_addr, res->ai_addrlen) == 0;
  freeaddrinfo(res);
  if (!connected)
    return false;
  const std::string login = glimpse_login(user_, pass_);
  if (::send(fd_, login.data(), login.size(), kSendFlags) != ssize_t(login.size()))
    return false;

  bool done = false, failed = false;
  SoupStream watch(SoupStream::Events{
      [&](const char *p, size_t n) {
        if (n && p[0] == 'G')
          done = true;   // End of Snapshot
      },
      {}, [&](char) { failed = true; },
      [&]() {
        // End of Session before End of Snapshot: the spin is incomplete.
        if (!done)
          failed = true;
      }});
  const time_t start = time(nullptr);
  time_t last_hb = start;
  char b[65536];
  while (!done && !failed && !stop_)
  {
    if (time(nullptr) - start > timeout_s)
      return false;
    if (time(nullptr) - last_hb >= 1)
    {
      const char hb[3] = {0, 1, 'R'};   // Client Heartbeat (Soup 2.3.3)
      (void)::send(fd_, hb, 3, kSendFlags);
      last_hb = time(nullptr);
    }
    pollfd pfd{fd_, POLLIN, 0};
    if (poll(&pfd, 1, 200) <= 0)
      continue;
    const ssize_t n = recv(fd_, b, sizeof b, 0);
    if (n <= 0)
      return false;
    watch.feed(b, size_t(n));
    on_bytes(b, size_t(n));
  }
  const char logout[3] = {0, 1, 'O'};   // Logout Request (Soup 2.3.4)
  (void)::send(fd_, logout, 3, kSendFlags);
  close(fd_);
  fd_ = -1;
  return done && !failed;
}
} // namespace nasdaq_md
