#pragma once
// Synthetic Aquis packets for the handler tests, written byte by byte at the
// offsets of the Aquis Market Data Technical Specification v4.2 (MD), not
// through the generated structs.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace aquis_test
{

struct W
{
  std::string b;
  W &u(uint64_t v, int n)
  {
    for (int i = 0; i < n; ++i)
      b.push_back(char(uint8_t(v >> (8 * i))));
    return *this;
  }
  W &s(std::string_view v, size_t n)
  {
    for (size_t i = 0; i < n; ++i)
      b.push_back(i < v.size() ? v[i] : '\0');
    return *this;
  }
};

// 6-byte header (MD p.11, 3.3.1); the length is patched in by done().
inline W hdr(int type, uint32_t seq)
{
  W w;
  w.u(type, 1).u(0, 1).u(seq, 4);
  return w;
}
inline std::string done(W w)
{
  w.b[1] = char(uint8_t(w.b.size()));
  return w.b;
}

inline std::string heartbeat(uint32_t next) { return done(hdr(1, next)); }
inline std::string bundle_start(uint32_t seq) { return done(hdr(34, seq)); }
inline std::string bundle_end(uint32_t seq) { return done(hdr(35, seq)); }

// MD p.12, 3.4.3
inline std::string add(uint32_t seq, uint16_t sec, int side, uint32_t qty, uint64_t px, uint32_t ref,
                       uint64_t ts, uint64_t pub = 0)
{
  W w = hdr(2, seq);
  w.u(sec, 2).u(side, 1).u(qty, 4).u(px, 8).u(ref, 4).u(ts, 8).u(0, 1).u(pub, 8);
  return done(w);
}
// MD p.12, 3.4.4
inline std::string cancel(uint32_t seq, uint16_t sec, uint32_t ref, uint64_t ts)
{
  W w = hdr(3, seq);
  w.u(sec, 2).u(ref, 4).u(ts, 8).u(0, 1).u(0, 8);
  return done(w);
}
// MD p.12-13, 3.4.5
inline std::string modify(uint32_t seq, uint16_t sec, uint32_t qty, uint64_t px, uint32_t ref, uint64_t ts)
{
  W w = hdr(4, seq);
  w.u(sec, 2).u(qty, 4).u(px, 8).u(ref, 4).u(ts, 8).u(0, 1).u(0, 8);
  return done(w);
}
// MD p.13, 3.4.6
inline std::string trade(uint32_t seq, uint16_t sec, int type, uint32_t qty, uint64_t px, uint32_t ref,
                         uint32_t tref, uint64_t ts, uint64_t pub = 0)
{
  W w = hdr(5, seq);
  w.u(sec, 2).u(type, 1).u(qty, 4).u(px, 8).u(ref, 4).u(tref, 4).u(ts, 8).s("AQXE", 4).u(0, 4).u(0, 1)
      .u(pub, 8);
  return done(w);
}
// MD p.14, 3.4.7
inline std::string bust(uint32_t seq, uint16_t sec, uint32_t qty, uint64_t px, uint32_t tref, uint64_t ts)
{
  W w = hdr(6, seq);
  w.u(sec, 2).u(qty, 4).u(px, 8).u(tref, 4).u(ts, 8).u(0, 4).u(0, 8);
  return done(w);
}
// MD p.14-15, 3.4.8
inline std::string tick_band(uint32_t seq, int table, std::string_view name, uint64_t thr, uint64_t tick)
{
  W w = hdr(7, seq);
  w.u(table, 1).s(name, 10).u(thr, 8).u(tick, 8);
  return done(w);
}
// MD p.15-16, 3.4.9
inline std::string definition(uint32_t seq, uint16_t sec, std::string_view umtf, std::string_view isin,
                              int tick_table)
{
  W w = hdr(8, seq);
  w.u(sec, 2).s(umtf, 6).s(isin, 12).s("GBX", 3).s("XLON", 4).u(tick_table, 1).u(0, 2).s(umtf, 20)
      .u(1000000000, 8).u(9, 1).u(0, 8).u(1, 2);
  return done(w);
}
// MD p.16-17, 3.4.10
inline std::string status(uint32_t seq, uint16_t sec, int trading_status, int market_flags, uint64_t ts)
{
  W w = hdr(9, seq);
  w.u(sec, 2).u(trading_status, 1).u(market_flags, 1).u(ts, 8).u(market_flags & 1, 1).u(0, 8);
  return done(w);
}
// MD p.17-18, 3.5.1
inline std::string aod_update(uint32_t seq, uint16_t sec, uint64_t px, uint32_t vol, uint64_t ts)
{
  W w = hdr(17, seq);
  w.u(sec, 2).u(px, 8).u(vol, 4).u(ts, 8).u(0, 8);
  return done(w);
}
// MD p.19, 3.7.1-3.7.3
inline std::string snap_start(uint32_t stream_seq, uint16_t count, uint64_t ts)
{
  W w = hdr(10, 0);
  w.u(stream_seq, 4).u(count, 2).u(ts, 8);
  return done(w);
}
inline std::string book_status(uint16_t sec, int st, int flags, uint16_t entries)
{
  W w = hdr(11, 0);
  w.u(sec, 2).u(st, 1).u(flags, 1).u(entries, 2).u(0, 4).u(0, 4).u(0, 8);
  return done(w);
}
inline std::string book_entry(uint16_t sec, int side, uint32_t qty, uint64_t px, uint32_t ref)
{
  W w = hdr(12, 0);
  w.u(sec, 2).u(side, 1).u(qty, 4).u(px, 8).u(ref, 4);
  return done(w);
}
// MD p.21, 3.8.3
inline std::string replay_response(int code)
{
  W w = hdr(15, 0);
  w.u(code, 1);
  return done(w);
}

// A packet: 1-byte message count, then the messages (MD p.10, 3.2).
inline std::string packet(const std::vector<std::string> &msgs)
{
  std::string p(1, char(uint8_t(msgs.size())));
  for (const auto &m : msgs)
    p += m;
  return p;
}

} // namespace aquis_test
