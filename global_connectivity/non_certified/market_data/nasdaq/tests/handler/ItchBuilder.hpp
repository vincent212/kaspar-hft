#pragma once
// Test helpers: ITCH 5.0 messages written field by field from the
// specification's tables (as tests/nasdaq_decode_test.cpp does), framed with
// the 2-byte MoldUDP64 length; a recording Sink; a registrar that routes
// every live symbol without RefData.
#include "nasdaq_md/Dispatcher.hpp"
#include "nasdaq_md/Instruments.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nasdaq_md::test
{
struct Msg
{
  std::string b;
  Msg &u(uint64_t v, int n)
  {
    for (int i = n - 1; i >= 0; --i)
      b.push_back(char((v >> (8 * i)) & 0xff));
    return *this;
  }
  Msg &c(char ch)
  {
    b.push_back(ch);
    return *this;
  }
  Msg &s(std::string_view v, size_t n)
  {
    for (size_t i = 0; i < n; ++i)
      b.push_back(i < v.size() ? v[i] : ' ');
    return *this;
  }
  Msg &hdr(char type, uint16_t locate, uint64_t ts) { return c(type).u(locate, 2).u(0, 2).u(ts, 6); }
  // With the 2-byte length that precedes it in MoldUDP64 and the sample files.
  std::string framed() const
  {
    std::string f;
    f.push_back(char(b.size() >> 8));
    f.push_back(char(b.size() & 0xff));
    return f + b;
  }
};

inline std::string SysEvent(char code, uint64_t ts) { return Msg().hdr('S', 0, ts).c(code).framed(); }
inline std::string Dir(uint16_t loc, std::string_view sym, uint64_t ts = 1, char auth = 'P')
{
  return Msg().hdr('R', loc, ts).s(sym, 8).c('Q').c('N').u(100, 4).c('N').c('C').s("Z", 2).c(auth)
      .c('N').c(' ').c('1').c('N').u(0, 4).c('N').framed();
}
inline std::string Action(uint16_t loc, std::string_view sym, char state, uint64_t ts)
{
  return Msg().hdr('H', loc, ts).s(sym, 8).c(state).c(' ').s("", 4).framed();
}
inline std::string OpHalt(uint16_t loc, std::string_view sym, char market, char action, uint64_t ts)
{
  return Msg().hdr('h', loc, ts).s(sym, 8).c(market).c(action).framed();
}
inline std::string Add(uint16_t loc, uint64_t ref, char side, uint32_t sh, uint32_t px, uint64_t ts,
                       std::string_view sym = "TEST")
{
  return Msg().hdr('A', loc, ts).u(ref, 8).c(side).u(sh, 4).s(sym, 8).u(px, 4).framed();
}
inline std::string AddMPID(uint16_t loc, uint64_t ref, char side, uint32_t sh, uint32_t px, uint64_t ts)
{
  return Msg().hdr('F', loc, ts).u(ref, 8).c(side).u(sh, 4).s("TEST", 8).u(px, 4).s("GSCO", 4).framed();
}
inline std::string Exec(uint16_t loc, uint64_t ref, uint32_t sh, uint64_t ts, uint64_t match = 1)
{
  return Msg().hdr('E', loc, ts).u(ref, 8).u(sh, 4).u(match, 8).framed();
}
inline std::string ExecPx(uint16_t loc, uint64_t ref, uint32_t sh, char printable, uint32_t px,
                          uint64_t ts)
{
  return Msg().hdr('C', loc, ts).u(ref, 8).u(sh, 4).u(2, 8).c(printable).u(px, 4).framed();
}
inline std::string Cancel(uint16_t loc, uint64_t ref, uint32_t sh, uint64_t ts)
{
  return Msg().hdr('X', loc, ts).u(ref, 8).u(sh, 4).framed();
}
inline std::string Delete(uint16_t loc, uint64_t ref, uint64_t ts)
{
  return Msg().hdr('D', loc, ts).u(ref, 8).framed();
}
inline std::string Replace(uint16_t loc, uint64_t oref, uint64_t nref, uint32_t sh, uint32_t px,
                           uint64_t ts)
{
  return Msg().hdr('U', loc, ts).u(oref, 8).u(nref, 8).u(sh, 4).u(px, 4).framed();
}
inline std::string TradeP(uint16_t loc, uint32_t sh, uint32_t px, uint64_t ts)
{
  return Msg().hdr('P', loc, ts).u(0, 8).c('B').u(sh, 4).s("TEST", 8).u(px, 4).u(3, 8).framed();
}
inline std::string CrossQ(uint16_t loc, uint64_t sh, uint32_t px, uint64_t ts, char type = 'O')
{
  return Msg().hdr('Q', loc, ts).u(sh, 8).s("TEST", 8).u(px, 4).u(4, 8).c(type).framed();
}
inline std::string Broken(uint16_t loc, uint64_t match, uint64_t ts)
{
  return Msg().hdr('B', loc, ts).u(match, 8).framed();
}
inline std::string Noii(uint16_t loc, uint64_t ts)
{
  return Msg().hdr('I', loc, ts).u(1, 8).u(2, 8).c('B').s("TEST", 8).u(1, 4).u(1, 4).u(1, 4).c('O').c(' ')
      .framed();
}

struct RecSink : Sink
{
  std::vector<std::pair<int32_t, bfile::l3_t>> book;
  std::vector<bfile::l3_t> rec;
  void to_book(int32_t a, const bfile::l3_t &r) override { book.emplace_back(a, r); }
  void to_recorder(const bfile::l3_t &r) override { rec.push_back(r); }
  // Records to books other than end-of-burst markers.
  std::vector<std::pair<int32_t, bfile::l3_t>> data() const
  {
    std::vector<std::pair<int32_t, bfile::l3_t>> r;
    for (auto &x : book)
      if (!std::holds_alternative<bfile::l3_eob_t>(x.second))
        r.push_back(x);
    return r;
  }
  void clear()
  {
    book.clear();
    rec.clear();
  }
};

// Routes every live symbol; asset = 1000 + locate; tick from the symbol: a
// name starting with "SUB" gets 0.0001, others 0.01.
inline Instruments::Registrar test_registrar()
{
  return [](Instrument &i) -> int32_t {
    if (i.authenticity != 'P')
      return -1;
    if (i.symbol.rfind("NOROUTE", 0) == 0)
      return -1;
    i.tick = i.symbol.rfind("SUB", 0) == 0 ? 1 : 100;
    return 1000 + int32_t(i.locate);
  };
}

constexpr uint64_t kMidnight = 1781236800000000000ull;   // 2026-06-12 00:00 ET

inline Dispatcher::Config test_config()
{
  Dispatcher::Config c;
  c.venue = char(en::x::NASDAQ);
  c.market_code = 'Q';
  c.midnight_ns = kMidnight;
  return c;
}
} // namespace nasdaq_md::test
