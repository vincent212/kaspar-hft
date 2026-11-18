#include "nasdaq_md/Transport.hpp"

#include <arpa/inet.h>
#include <cstring>
#include <ctime>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace nasdaq_md
{
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
namespace
{
uint32_t sw32(uint32_t v, bool s) { return s ? __builtin_bswap32(v) : v; }
} // namespace

PcapReader::PcapReader(const std::string &path)
{
  f_ = fopen(path.c_str(), "rb");
  if (!f_)
    return;
  uint32_t gh[6];
  if (fread(gh, 4, 6, f_) != 6)
  {
    fclose(f_);
    f_ = nullptr;
    return;
  }
  switch (gh[0])
  {
  case 0xa1b2c3d4: break;
  case 0xd4c3b2a1: swap_ = true; break;
  case 0xa1b23c4d: nsec_ = true; break;
  case 0x4d3cb2a1: swap_ = nsec_ = true; break;
  default:
    fclose(f_);
    f_ = nullptr;
    return;
  }
  linktype_ = sw32(gh[5], swap_);
}

PcapReader::~PcapReader()
{
  if (f_)
    fclose(f_);
}

bool PcapReader::next(std::string &payload, uint16_t &dst_port, uint64_t &ts_ns)
{
  std::vector<uint8_t> rec;
  for (;;)
  {
    uint32_t rh[4];
    if (!f_ || fread(rh, 4, 4, f_) != 4)
      return false;
    const uint32_t incl = sw32(rh[2], swap_);
    rec.resize(incl);
    if (fread(rec.data(), 1, incl, f_) != incl)
      return false;
    ts_ns = uint64_t(sw32(rh[0], swap_)) * 1000000000ull +
            uint64_t(sw32(rh[1], swap_)) * (nsec_ ? 1 : 1000);
    size_t off = 0;
    if (linktype_ == 1)   // Ethernet, optional 802.1Q tags
    {
      if (incl < 14)
        continue;
      uint16_t et = uint16_t((rec[12] << 8) | rec[13]);
      off = 14;
      while (et == 0x8100 && incl >= off + 4)
      {
        et = uint16_t((rec[off + 2] << 8) | rec[off + 3]);
        off += 4;
      }
      if (et != 0x0800)
        continue;
    }
    else if (linktype_ != 101 && linktype_ != 228)   // raw IPv4
      continue;
    if (incl < off + 20 || (rec[off] >> 4) != 4 || rec[off + 9] != 17)
      continue;
    const size_t ihl = size_t(rec[off] & 15) * 4;
    off += ihl;
    if (incl < off + 8)
      continue;
    dst_port = uint16_t((rec[off + 2] << 8) | rec[off + 3]);
    const size_t ulen = size_t((rec[off + 4] << 8) | rec[off + 5]);
    off += 8;
    if (ulen < 8 || off + ulen - 8 > incl)
      continue;
    payload.assign(reinterpret_cast<const char *>(rec.data() + off), ulen - 8);
    return true;
  }
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
  const bool connected = fd_ >= 0 && connect(fd_, res->ai_addr, res->ai_addrlen) == 0;
  freeaddrinfo(res);
  if (!connected)
    return false;
  const std::string login = glimpse_login(user_, pass_);
  if (::send(fd_, login.data(), login.size(), 0) != ssize_t(login.size()))
    return false;

  bool done = false, failed = false;
  SoupStream watch(SoupStream::Events{
      [&](const char *p, size_t n) {
        if (n && p[0] == 'G')
          done = true;   // End of Snapshot
      },
      {}, [&](char) { failed = true; }, [&]() { done = true; }});
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
      (void)::send(fd_, hb, 3, 0);
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
  (void)::send(fd_, logout, 3, 0);
  close(fd_);
  fd_ = -1;
  return done && !failed;
}
} // namespace nasdaq_md
