// itch_filter: reads a Nasdaq ITCH 5.0 sample stream (each message preceded by
// a 2-byte big-endian length, the format of Nasdaq's public sample files) from
// stdin and writes a smaller stream in the same format:
//
//   - every message with no security (System Event, MWCB),
//   - every Stock Directory, Stock Trading Action, Operational Halt and Reg SHO,
//   - every order, trade and NOII message of the selected securities.
//
// Selected: the symbols named with --symbols, and every Kth live (Authenticity
// P) directory entry with --every K. Stops after the first message at or past
// --stop-ns (nanoseconds since midnight).
//
// It also measures, for every security in the input (not only the selected
// ones), the Cross Trade check of review.md decision 4: Q Shares against the
// C-Printable-N shares of the cross, and E messages inside a cross.
//
// Prints progress and the counts to stderr.
#include "itch50.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace itch = nasdaq::itch::from_nasdaq;

namespace
{
struct CrossAcc
{
  uint64_t cn_shares = 0;      // C-N shares since the last Q
  uint64_t cn_msgs = 0;
  uint64_t first_cn_ts = 0;    // timestamp of the first C-N since the last Q
  uint64_t e_inside = 0;       // E messages between the first C-N and the Q
  uint64_t e_inside_shares = 0;
  uint64_t e_same_ts = 0;      // E messages with the Q's timestamp (checked at Q)
  uint64_t last_e_ts = 0, last_e_ts_shares = 0, last_e_ts_n = 0;
};

std::string fmt_ts(uint64_t ns)
{
  char b[32];
  uint64_t s = ns / 1000000000ull;
  snprintf(b, sizeof b, "%02llu:%02llu:%02llu.%09llu", (unsigned long long)(s / 3600),
           (unsigned long long)(s / 60 % 60), (unsigned long long)(s % 60),
           (unsigned long long)(ns % 1000000000ull));
  return b;
}
} // namespace

int main(int argc, char **argv)
{
  std::set<std::string> symbols;
  int every = 0;
  uint64_t stop_ns = ~0ull;
  const char *out_path = nullptr;
  for (int i = 1; i < argc; ++i)
  {
    std::string a = argv[i];
    if (a == "--symbols" && i + 1 < argc)
    {
      std::stringstream ss(argv[++i]);
      std::string s;
      while (std::getline(ss, s, ','))
        symbols.insert(s);
    }
    else if (a == "--every" && i + 1 < argc)
      every = std::atoi(argv[++i]);
    else if (a == "--stop-ns" && i + 1 < argc)
      stop_ns = std::strtoull(argv[++i], nullptr, 10);
    else if (a == "--out" && i + 1 < argc)
      out_path = argv[++i];
    else
    {
      std::cerr << "usage: itch_filter --out FILE [--symbols A,B] [--every K] [--stop-ns NS] < itch\n";
      return 2;
    }
  }
  FILE *out = out_path ? fopen(out_path, "wb") : nullptr;
  if (out_path && !out)
  {
    perror(out_path);
    return 1;
  }

  std::vector<bool> keep(65536, false);
  std::vector<std::string> sym_of(65536);
  std::vector<CrossAcc> acc(65536);
  std::map<char, uint64_t> counts;
  std::map<std::string, uint64_t> cross_stats;   // by cross type and outcome
  std::vector<std::string> cross_mismatch, e_inside_cases;
  uint64_t live_dir = 0, selected = 0, in_bytes = 0, out_msgs = 0, last_ts = 0;
  uint64_t next_progress = 1ull << 28;

  std::vector<char> buf(1 << 22);
  size_t have = 0;
  bool stop = false;
  while (!stop)
  {
    const size_t n = fread(buf.data() + have, 1, buf.size() - have, stdin);
    if (n == 0)
    {
      if (have)
        std::cerr << "input ends inside a message (" << have << " bytes left)\n";
      break;
    }
    have += n;
    in_bytes += n;
    size_t pos = 0;
    while (!stop)
    {
      auto d = itch::decode(buf.data() + pos, have - pos);
      if (d.st == nasdaq::status::incomplete)
        break;
      if (!d)
      {
        std::cerr << "decode error at input offset ~" << in_bytes - have + pos << "\n";
        return 1;
      }
      const char *m = buf.data() + pos;
      const char type = m[2];
      const uint16_t locate = uint16_t((uint8_t(m[3]) << 8) | uint8_t(m[4]));
      uint64_t ts = 0;
      for (int k = 0; k < 6; ++k)
        ts = (ts << 8) | uint8_t(m[7 + k]);
      last_ts = ts;
      ++counts[type];
      bool w = false;
      switch (type)
      {
      case 'R':
      {
        const auto &r = std::get<itch::StockDirectory>(d.msg);
        const std::string s(r.get_Stock());
        sym_of[locate] = s;
        bool k = symbols.count(s) > 0;
        if (r.Authenticity == 'P')
        {
          ++live_dir;
          if (every > 0 && live_dir % every == 0)
            k = true;
        }
        if (k && !keep[locate])
        {
          keep[locate] = true;
          ++selected;
        }
        w = true;
        break;
      }
      case 'S': case 'V': case 'W': case 'H': case 'h': case 'Y': case 'K':
        w = true;
        break;
      case 'C':
      {
        const auto &c = std::get<itch::OrderExecutedWithPrice>(d.msg);
        if (c.Printable == 'N')
        {
          auto &a = acc[locate];
          if (a.cn_msgs == 0)
            a.first_cn_ts = ts;
          a.cn_shares += c.ExecutedShares;
          ++a.cn_msgs;
        }
        else
          ++cross_stats["C-Y messages"];
        w = keep[locate];
        break;
      }
      case 'E':
      {
        auto &a = acc[locate];
        const auto &e = std::get<itch::OrderExecuted>(d.msg);
        if (a.cn_msgs)
        {
          ++a.e_inside;
          a.e_inside_shares += e.ExecutedShares;
        }
        if (a.last_e_ts != ts)
        {
          a.last_e_ts = ts;
          a.last_e_ts_shares = 0;
          a.last_e_ts_n = 0;
        }
        a.last_e_ts_shares += e.ExecutedShares;
        ++a.last_e_ts_n;
        w = keep[locate];
        break;
      }
      case 'Q':
      {
        const auto &q = std::get<itch::CrossTrade>(d.msg);
        auto &a = acc[locate];
        const std::string ct(1, q.CrossType);
        ++cross_stats["Q type " + ct];
        const uint64_t qs = q.Shares;
        if (qs == 0)
          ++cross_stats["Q type " + ct + " zero shares"];
        if (qs == a.cn_shares)
          ++cross_stats["Q type " + ct + " shares == C-N shares since last Q"];
        else
        {
          ++cross_stats["Q type " + ct + " shares != C-N shares since last Q"];
          if (cross_mismatch.size() < 40)
          {
            std::ostringstream o;
            o << fmt_ts(ts) << " " << sym_of[locate] << " type " << ct << " Q=" << qs
              << " C-N=" << a.cn_shares << " (" << a.cn_msgs << " msgs)"
              << " E-inside=" << a.e_inside << "/" << a.e_inside_shares << "sh"
              << " E-at-Q-ts=" << (a.last_e_ts == ts ? a.last_e_ts_n : 0) << "/"
              << (a.last_e_ts == ts ? a.last_e_ts_shares : 0) << "sh";
            cross_mismatch.push_back(o.str());
          }
        }
        if (qs < a.cn_shares)
          ++cross_stats["Q type " + ct + " shares < C-N shares since last Q"];
        if (a.e_inside)
        {
          ++cross_stats["Q type " + ct + " with E between first C-N and Q"];
          std::ostringstream o;
          o << fmt_ts(ts) << " " << sym_of[locate] << " type " << ct << " Q=" << qs
            << " C-N=" << a.cn_shares << " (" << a.cn_msgs << " msgs, first at "
            << fmt_ts(a.first_cn_ts) << ") E-inside=" << a.e_inside << "/" << a.e_inside_shares
            << "sh, E-at-Q-ts=" << (a.last_e_ts == ts ? a.last_e_ts_n : 0);
          e_inside_cases.push_back(o.str());
        }
        if (a.last_e_ts == ts)
          ++cross_stats["Q type " + ct + " with E at the Q timestamp"];
        if (a.cn_msgs && a.first_cn_ts != ts)
          ++cross_stats["Q type " + ct + " C-N not all at the Q timestamp"];
        a = CrossAcc{};
        w = keep[locate];
        break;
      }
      default:
        w = locate == 0 || keep[locate];
      }
      if (w && out)
      {
        fwrite(m, 1, d.size, out);
        ++out_msgs;
      }
      pos += d.size;
      if (ts >= stop_ns)
        stop = true;
    }
    std::memmove(buf.data(), buf.data() + pos, have - pos);
    have -= pos;
    if (in_bytes >= next_progress)
    {
      std::cerr << "progress: " << (in_bytes >> 20) << " MiB in, ts " << fmt_ts(last_ts)
                << ", out " << out_msgs << " msgs\n";
      next_progress += 1ull << 28;
    }
  }
  if (out)
    fclose(out);
  std::cerr << "input bytes ~" << in_bytes << ", last ts " << fmt_ts(last_ts) << ", selected "
            << selected << " securities, wrote " << out_msgs << " messages\n";
  for (auto &[t, n] : counts)
    std::cerr << "count " << t << " " << n << "\n";
  for (auto &[k, n] : cross_stats)
    std::cerr << "cross " << k << ": " << n << "\n";
  for (auto &s : cross_mismatch)
    std::cerr << "cross mismatch: " << s << "\n";
  for (auto &x : e_inside_cases)
    std::cerr << "cross with E inside: " << x << "\n";
  std::cerr << "selected:";
  for (int l = 0; l < 65536; ++l)
    if (keep[l])
      std::cerr << " " << sym_of[l];
  std::cerr << "\n";
  return 0;
}
