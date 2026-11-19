// nasdaq_replay: replays a Nasdaq ITCH 5.0 sample stream (each message after a
// 2-byte length, Nasdaq's public sample format) through the handler, wrapped
// in synthetic MoldUDP64 packets, into real OB instances (one per selected
// instrument), and reports.
//
//   nasdaq_replay --in FILE|- --date YYYYMMDD [--symbols A,B,..] [options]
//
// What runs:
//   oracle   a Dispatcher fed every message directly, in order: the
//            uninterrupted book (no OB), used for the GLIMPSE spins and as the
//            reference after injected faults
//   handler  NasdaqFeed (MoldSequencer + Dispatcher) fed the packets, after
//            injection; its records go to one frame::ob::act::OB per routed
//            instrument (do_cross_check on: a crossed book while matching aborts)
//
// Checks (reported at the end, non-zero exit on a failure):
//   - no OB abort (the process would die)
//   - own book: no cross while status 17, every E C X D U refers to a known
//     order, no execution larger than the order, timestamps never decrease
//   - sequence continuity: gaps, duplicates, lost messages
//   - OB vs the handler's own book: best bid/ask and size and order count of
//     the top --levels levels per side, every --sample messages and at the end
//   - handler vs oracle (with injections): every level of every security and
//     the queue order of every level
//   - queue-position analysis of executions (review.md decision 3)
//
// Injection:
//   --drop-every P      drop one packet in every P; a fake re-request server
//                       (history of the last 4M messages) answers the requests
//   --dup-every P       send one packet in every P twice
//   --reset-at SEQ      from SEQ on, packets carry another MoldUDP64 session
//                       (a channel reset mid-stream); the handler takes a GLIMPSE
//                       snapshot (late-join rule)
//   --glimpse-at SEQ    drop --glimpse-len packets from SEQ with re-requests
//                       unanswered: the handler gives up and takes a snapshot
//   --spin-delay K      live packets that arrive between a snapshot request and
//                       the spin (default 50)
#include "actors/Actor.hpp"
#include "bfile/r_l3.hpp"
#include "enum/e_names.hpp"
#include "frame/cons/msg/Get.hpp"
#include "frame/cons/msg/Page.hpp"
#include "frame/mda/msg/Data.hpp"
#include "frame/ob/act/OB.hpp"
#include "frame/ref/RefData.hpp"
#include "nasdaq_md/NasdaqFeed.hpp"
#include "nasdaq_md/Time.hpp"
#include "nasdaq_md/Transport.hpp"
#include "unit_test/MockActor.hpp"

#include <boost/property_tree/ptree.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>

using namespace nasdaq_md;
namespace itch = nasdaq::itch::from_nasdaq;

namespace
{
struct TestableOB : public frame::ob::act::OB
{
  using frame::ob::act::OB::OB;
  using frame::ob::act::OB::reply_to;
};

// Sink into real OBs, created when the registrar routes an instrument.
struct OBSink : Sink
{
  std::unordered_map<int32_t, std::unique_ptr<TestableOB>> books;
  unit_test::MockActor probe{"probe"};
  int ladder = 1 << 16;
  uint64_t sent = 0;

  TestableOB *create(int32_t asset)
  {
    boost::property_tree::ptree pt;
    pt.put("ladder_ticks", ladder);
    auto ob = std::make_unique<TestableOB>(nullptr, true, nullptr, uint(asset), pt);
    auto *p = ob.get();
    books[asset] = std::move(ob);
    return p;
  }

  static void invoke(actors::Actor *a, const actors::Message *m, actors::Actor *sender)
  {
    const_cast<actors::Message *>(m)->sender = sender;
    auto it = a->handlers.find(std::type_index(typeid(*m)));
    if (it != a->handlers.end())
      (a->*(it->second))(m);
  }

  void to_book(int32_t asset, const bfile::l3_t &r) override
  {
    auto it = books.find(asset);
    if (it == books.end())
      return;
    frame::mda::msg::Data d(true);
    d.l3 = r;
    it->second->reply_to = &probe;
    invoke(it->second.get(), &d, &probe);
    it->second->reply_to = nullptr;
    ++sent;
  }

  std::string ask(TestableOB *ob, const frame::cons::msg::Get &g)
  {
    probe.clear();
    ob->reply_to = &probe;
    invoke(ob, &g, &probe);
    ob->reply_to = nullptr;
    auto page = probe.get_message<frame::cons::msg::Page>(0);
    return page ? page->val : "";
  }
  std::pair<long, long> bbo(TestableOB *ob)
  {
    long b = -1, a = -1;
    std::sscanf(ask(ob, frame::cons::msg::Get("bbbo")).c_str(), "BBBO: %ld %ld", &b, &a);
    return {b, a};
  }
  struct Q { long sz = -1, cnt = -1, sim = -1; };
  Q qat(TestableOB *ob, bool bid, uint32_t px)
  {
    Q q;
    frame::cons::msg::Get g("qat", {{"side", bid ? "B" : "S"}, {"px", std::to_string(px)}});
    std::sscanf(ask(ob, g).c_str(), "QAT: %ld %ld %ld", &q.sz, &q.cnt, &q.sim);
    return q;
  }
};

// Records sent by the handler, by type (the sink sees only routed ones).
struct CountingSink : Sink
{
  Sink *inner;
  std::map<std::string, uint64_t> by_type;
  explicit CountingSink(Sink *s) : inner(s) {}
  void to_book(int32_t a, const bfile::l3_t &r) override
  {
    std::visit([&](const auto &x) { ++by_type[en::l3_names[x.typ]]; }, r);
    inner->to_book(a, r);
  }
  void to_recorder(const bfile::l3_t &r) override
  {
    if (std::holds_alternative<bfile::l3_eop_t>(r))
      ++by_type["EOP (recorder only)"];
  }
};

struct NullSink : Sink
{
  void to_book(int32_t, const bfile::l3_t &) override {}
};

// Queue position of executions (decision 3): for every E and C, is the
// executed order at the head of its level under the handler's queue model?
struct QueueCheck
{
  enum Origin : uint8_t { ADD, U_SAME_DOWN, U_SAME_EQ, U_SAME_UP, U_NEW_PX, SPIN };
  struct Key
  {
    uint16_t loc;
    char side;
    uint32_t px;
    bool operator<(const Key &o) const
    {
      return std::tie(loc, side, px) < std::tie(o.loc, o.side, o.px);
    }
  };
  struct Ord { Key k; uint64_t prio; Origin origin; uint32_t shares; };
  std::unordered_map<uint64_t, Ord> ords;
  std::map<Key, std::set<std::pair<uint64_t, uint64_t>>> q;   // (prio, ref)
  uint64_t prio = 0;
  // [origin of executed][at head?]
  uint64_t exec[6][2] = {};
  // executions not at the head where an order ahead came from U same-price size-down
  uint64_t behind_u_down = 0;
  // non-head executions that fully empty the order ahead? kept simple

  void add(uint64_t ref, Key k, uint32_t sh, Origin o)
  {
    ords[ref] = Ord{k, ++prio, o, sh};
    q[k].insert({prio, ref});
  }
  void remove(uint64_t ref)
  {
    auto it = ords.find(ref);
    if (it == ords.end())
      return;
    auto &s = q[it->second.k];
    s.erase({it->second.prio, ref});
    if (s.empty())
      q.erase(it->second.k);
    ords.erase(it);
  }
  // E (0) and C (1) separately; for non-head E: were all orders ahead odd
  // lots (under 100 shares)?
  uint64_t exec_e[6][2] = {};   // E only, by origin
  uint64_t by_type[2][2] = {}, nonhead_e_ahead_odd = 0, nonhead_e_ahead_mixed = 0;
  uint64_t nonhead_e_rank_sum = 0, cur_ts = 0, e_after_open[2] = {};
  void execute(uint64_t ref, uint32_t n, int type = 0)
  {
    auto it = ords.find(ref);
    if (it == ords.end())
      return;
    auto &s = q[it->second.k];
    const bool head = !s.empty() && s.begin()->second == ref;
    ++exec[it->second.origin][head ? 1 : 0];
    ++by_type[type][head ? 1 : 0];
    if (type == 0)
      ++exec_e[it->second.origin][head ? 1 : 0];
    if (type == 0 && cur_ts >= 34200000000000ull)   // 09:30 ET
      ++e_after_open[head ? 1 : 0];
    if (!head && type == 0)
    {
      bool all_odd = true;
      uint64_t rank = 0;
      for (auto &[p, r] : s)
      {
        if (r == ref)
          break;
        ++rank;
        if (ords[r].shares >= 100)
          all_odd = false;
      }
      nonhead_e_rank_sum += rank;
      ++(all_odd ? nonhead_e_ahead_odd : nonhead_e_ahead_mixed);
    }
    if (!head)
      for (auto &[p, r] : s)
      {
        if (r == ref)
          break;
        if (ords[r].origin == U_SAME_DOWN)
        {
          ++behind_u_down;
          break;
        }
      }
    if (n >= it->second.shares)
      remove(ref);
    else
      it->second.shares -= n;
  }
  void reduce(uint64_t ref, uint32_t n)
  {
    auto it = ords.find(ref);
    if (it == ords.end())
      return;
    if (n >= it->second.shares)
      remove(ref);
    else
      it->second.shares -= n;
  }
  void on(const char *block, size_t len)
  {
    auto d = itch::decode(block, len);
    if (!d)
      return;
    cur_ts = 0;
    for (int k = 0; k < 6; ++k)
      cur_ts = (cur_ts << 8) | uint8_t(block[7 + k]);
    std::visit(
        [&](const auto &m) {
          using M = std::decay_t<decltype(m)>;
          if constexpr (M::type == 'A' || M::type == 'F')
            add(m.OrderReferenceNumber, Key{uint16_t(m.StockLocate), m.BuySellIndicator, uint32_t(m.Price)},
                m.Shares, ADD);
          else if constexpr (M::type == 'E')
            execute(m.OrderReferenceNumber, m.ExecutedShares, 0);
          else if constexpr (M::type == 'C')
            execute(m.OrderReferenceNumber, m.ExecutedShares, 1);
          else if constexpr (M::type == 'X')
            reduce(m.OrderReferenceNumber, m.CancelledShares);
          else if constexpr (M::type == 'D')
            remove(m.OrderReferenceNumber);
          else if constexpr (M::type == 'U')
          {
            auto it = ords.find(m.OriginalOrderReferenceNumber);
            if (it == ords.end())
              return;
            const Ord o = it->second;
            remove(m.OriginalOrderReferenceNumber);
            Origin org = U_NEW_PX;
            if (uint32_t(m.Price) == o.k.px)
              org = m.Shares < o.shares ? U_SAME_DOWN : m.Shares == o.shares ? U_SAME_EQ : U_SAME_UP;
            add(m.NewOrderReferenceNumber, Key{o.k.loc, o.k.side, uint32_t(m.Price)}, m.Shares, org);
          }
        },
        d.msg);
  }
};

// Every level and every queue of two own books.
struct BookDiff
{
  uint64_t levels = 0, level_mismatch = 0, queues = 0, queue_mismatch = 0;
  std::vector<std::string> first;
};
BookDiff diff_books(const OwnBook &a, const OwnBook &b, const Instruments &ins)
{
  BookDiff d;
  for (uint32_t loc = 1; loc < 65536; ++loc)
  {
    const auto *x = a.book(uint16_t(loc));
    const auto *y = b.book(uint16_t(loc));
    auto cmp = [&](const auto &mx, const auto &my, const char *side) {
      auto bad = [&](uint32_t px) {
        if (d.level_mismatch++ < 5)
          d.first.push_back(ins.at(uint16_t(loc)).symbol + " " + side + " level differs at " +
                            std::to_string(px));
      };
      for (const auto &[px, l] : mx)
      {
        ++d.levels;
        auto it = my.find(px);
        if (it == my.end() || !(it->second == l))
          bad(px);
      }
      for (const auto &[px, l] : my)
        if (!mx.count(px))
        {
          ++d.levels;
          bad(px);
        }
    };
    static const OwnBook::LocBook empty;
    cmp(x ? x->bids : empty.bids, y ? y->bids : empty.bids, "B");
    cmp(x ? x->asks : empty.asks, y ? y->asks : empty.asks, "S");
  }
  const auto qa = a.queues(), qb = b.queues();
  for (const auto &[k, v] : qa)
  {
    ++d.queues;
    auto it = qb.find(k);
    bool same = it != qb.end() && it->second.size() == v.size();
    for (size_t i = 0; same && i < v.size(); ++i)
      same = v[i].first == it->second[i].first && v[i].second.shares == it->second[i].second.shares;
    if (!same && d.queue_mismatch++ < 5)
      d.first.push_back(ins.at(k.locate).symbol + " queue order differs at " + std::to_string(k.px));
  }
  if (qb.size() != qa.size())
    d.queue_mismatch += qb.size() > qa.size() ? qb.size() - qa.size() : 0;
  return d;
}

std::string fmt_ts(uint64_t ns)
{
  char b[32];
  const uint64_t s = ns / 1000000000ull;
  snprintf(b, sizeof b, "%02llu:%02llu:%02llu", (unsigned long long)(s / 3600),
           (unsigned long long)(s / 60 % 60), (unsigned long long)(s % 60));
  return b;
}
} // namespace

int main(int argc, char **argv)
{
  std::string in = "-", date, symbols_arg;
  unsigned per_packet = 8, levels = 5, spin_delay = 50;
  uint64_t sample = 500000, max_msgs = ~0ull, drop_every = 0, dup_every = 0, reset_at = 0,
           glimpse_at = 0, glimpse_len = 200, own_only = 0;
  int every_sym = 0;
  for (int i = 1; i < argc; ++i)
  {
    const std::string a = argv[i];
    auto next = [&]() -> std::string {
      if (i + 1 >= argc)
      {
        std::cerr << "missing value for " << a << "\n";
        std::exit(2);
      }
      return argv[++i];
    };
    if (a == "--in") in = next();
    else if (a == "--date") date = next();
    else if (a == "--symbols") symbols_arg = next();
    else if (a == "--every-symbol") every_sym = std::stoi(next());
    else if (a == "--per-packet") per_packet = unsigned(std::stoul(next()));
    else if (a == "--levels") levels = unsigned(std::stoul(next()));
    else if (a == "--sample") sample = std::stoull(next());
    else if (a == "--max-msgs") max_msgs = std::stoull(next());
    else if (a == "--drop-every") drop_every = std::stoull(next());
    else if (a == "--dup-every") dup_every = std::stoull(next());
    else if (a == "--reset-at") reset_at = std::stoull(next());
    else if (a == "--glimpse-at") glimpse_at = std::stoull(next());
    else if (a == "--glimpse-len") glimpse_len = std::stoull(next());
    else if (a == "--spin-delay") spin_delay = unsigned(std::stoul(next()));
    else if (a == "--own-only") own_only = 1;
    else
    {
      std::cerr << "unknown option " << a << "\n";
      return 2;
    }
  }
  const uint64_t midnight = et_midnight_epoch_ns(date.c_str());
  if (!midnight)
  {
    std::cerr << "--date YYYYMMDD required\n";
    return 2;
  }
  std::set<std::string> symbols;
  {
    std::stringstream ss(symbols_arg);
    std::string s;
    while (std::getline(ss, s, ','))
      if (!s.empty())
        symbols.insert(s);
  }
  FILE *f = in == "-" ? stdin : fopen(in.c_str(), "rb");
  if (!f)
  {
    perror(in.c_str());
    return 1;
  }

  // RefData from an empty universe; instruments come from the directory.
  {
    const std::string u = "/tmp/nasdaq_replay_universe.csv";
    std::ofstream(u) << "# empty\n";
    frame::ref::RefData::set_universe(u, "");
    frame::ref::RefData::inst();
  }

  OBSink obs;
  CountingSink counting(&obs);
  uint64_t live_dir = 0;
  // Tick per symbol (not in the feed, feed.md section 8): 0.0001 for a stock
  // whose median Add price in the input is below $1 (sub-penny quoting,
  // Rule 612), else 0.01; production takes this from configuration (the
  // previous close). A first pass over the file.
  std::map<std::string, double> ticks;
  if (in != "-")
  {
    std::vector<std::string> sym(65536);
    std::vector<uint64_t> below(65536, 0), total(65536, 0);
    SampleReader pre(f);
    const char *b;
    size_t l;
    while (pre.next(b, l))
    {
      if (l < 3)
        continue;
      if (b[2] == 'R' && l >= 2 + 39)
        sym[(uint8_t(b[3]) << 8) | uint8_t(b[4])] =
            std::string(nasdaq::get_str(reinterpret_cast<const itch::StockDirectory *>(b)->Stock));
      else if ((b[2] == 'A' || b[2] == 'F') && l >= 38)
      {
        const auto *a = reinterpret_cast<const itch::AddOrder *>(b);
        const uint16_t loc = a->StockLocate;
        ++total[loc];
        below[loc] += uint32_t(a->Price) < 10000;
      }
    }
    for (int loc = 0; loc < 65536; ++loc)
      if (total[loc] && below[loc] * 2 > total[loc])
        ticks[sym[loc]] = 0.0001;
    std::cerr << "tick scan: " << ticks.size() << " symbols quoted mostly below $1 get tick 0.0001\n";
    rewind(f);
  }
  auto base = refdata_registrar(char(en::x::NASDAQ), {}, false, "", 1900000000, 0.01, ticks);
  Instruments ins([&](Instrument &i) -> int32_t {
    if (own_only || i.authenticity != 'P')
      return -1;
    ++live_dir;
    const bool pick = symbols.count(i.symbol) || (every_sym > 0 && live_dir % uint64_t(every_sym) == 0);
    if (!pick)
      return -1;
    const int32_t a = base(i);
    if (a >= 0)
      obs.create(a);
    return a;
  });
  Instruments oracle_ins(Instruments::Registrar{});

  std::vector<std::string> log;
  Dispatcher::Config dc;
  dc.venue = char(en::x::NASDAQ);
  dc.market_code = 'Q';
  dc.midnight_ns = midnight;
  dc.log = [&](const std::string &s) {
    if (log.size() < 200)
      log.push_back(s);
  };
  NullSink null_sink;
  Dispatcher::Config oc = dc;
  oc.log = {};
  Dispatcher oracle(oc, oracle_ins, null_sink);

  // Fake re-request server and GLIMPSE source.
  struct Fake : Requester, SnapshotSource
  {
    std::deque<std::pair<uint64_t, std::string>> hist;   // seq -> block
    std::deque<std::string> replies;
    std::string session;
    bool answer = true;
    uint64_t requests = 0, served = 0, unanswered = 0;
    bool spin_wanted = false;
    uint64_t spins = 0;
    void request(const std::string &s, uint64_t seq, uint16_t count) override
    {
      ++requests;
      if (!answer || hist.empty() || seq < hist.front().first)
      {
        ++unanswered;
        return;
      }
      // One reply packet, as many messages as fit (Mold p.5).
      std::string body;
      uint16_t n = 0;
      for (size_t i = size_t(seq - hist.front().first); n < count && i < hist.size(); ++n, ++i)
      {
        if (body.size() + hist[i].second.size() > 1400)
          break;
        body += hist[i].second;
      }
      if (n)
        replies.push_back(MoldPacketizer::header_only(s, seq, n) + body);
      ++served;
    }
    bool start(const std::string &) override
    {
      spin_wanted = true;
      ++spins;
      return true;
    }
  } fake;

  MoldSequencer::Config sc;
  sc.retry_ns = 1;            // replay time: retry on the next timer call
  sc.max_tries = 3;
  sc.snapshot_on_late_join = true;
  sc.max_hole = 100000;
  NasdaqFeed feed(dc, sc, ins, counting, &fake, &fake);
  QueueCheck qc;

  SampleReader reader(f);
  std::string session = "TEST000001";
  MoldPacketizer pz(session, 1, per_packet);
  uint64_t seq = 0, msgs = 0, pkts = 0, dropped = 0, duped = 0, glimpse_dropped = 0;
  uint64_t samples = 0, ob_checks = 0, ob_mismatch = 0, oracle_checks = 0;
  std::vector<std::string> ob_first;
  unsigned spin_countdown = 0;
  bool sample_due = false, last_dropped = false;
  uint64_t outside_ring = 0;
  uint64_t last_ts = 0, spin_n = 0;
  BookDiff total_diff;

  auto deliver = [&](const std::string &pkt) {
    feed.on_packet(pkt.data(), pkt.size(), 0, ++seq * 1000);
    // replies to requests made while handling the packet, and retries
    for (int round = 0; round < 50; ++round)
    {
      if (fake.replies.empty())
      {
        feed.on_timer(seq * 1000 + 500 + uint64_t(round));
        if (fake.replies.empty())
          break;
      }
      while (!fake.replies.empty())
      {
        auto r = std::move(fake.replies.front());
        fake.replies.pop_front();
        feed.on_packet(r.data(), r.size(), 0, seq * 1000 + 100);
      }
    }
    if (fake.spin_wanted)
    {
      if (!spin_countdown)
        spin_countdown = spin_delay + 1;
      if (--spin_countdown == 0)
      {
        // The snapshot reflects every message the oracle has applied: N is
        // the next one (decision 1).
        fake.spin_wanted = false;
        spin_n = pz.next_seq();
        SpinWriter w;
        w.login_accepted("GLIMPSE001");
        w.spin(oracle.book(), oracle_ins, spin_n, last_ts, 'Q');
        // in uneven chunks, as TCP would deliver them
        const auto &b = w.bytes();
        for (size_t o = 0; o < b.size(); o += 1000 + o % 777)
          feed.on_glimpse_bytes(b.data() + o, std::min(b.size() - o, size_t(1000 + o % 777)));
      }
    }
  };

  auto compare_ob = [&]() {
    ++samples;
    const auto &own = feed.dispatcher().book();
    for (auto loc : ins.routed())
    {
      auto it = obs.books.find(ins.asset(loc));
      if (it == obs.books.end())
        continue;
      TestableOB *ob = it->second.get();
      const auto [bb, ba] = obs.bbo(ob);
      const long tick = long(ins.at(loc).tick);
      const long ob_ask_empty = long(frame::ref::RefData::get_asset(size_t(ins.asset(loc)))->maxpx) - 1;
      const long ob_bid = bb * tick, ob_ask = ba == ob_ask_empty ? 0 : ba * tick;
      ++ob_checks;
      // The own book's levels on the asset's tick (off-tick stubs are not sent).
      // OB's ring ladder holds ladder_ticks prices from the best of each side
      // (OB.hpp, ring_mask): deeper stubs (e.g. offers at $199,999) are not in
      // it, and are skipped here.
      auto on_tick = [&](bool bid) {
        std::vector<std::pair<uint32_t, Level>> r;
        long best = -1;
        for (const auto &x : own.top(loc, bid, 1000000))
        {
          if (x.first % uint32_t(tick) || r.size() >= levels)
            continue;
          if (best < 0)
            best = long(x.first);
          if (std::labs(long(x.first) - best) / tick >= long(obs.ladder))
          {
            ++outside_ring;
            continue;
          }
          r.push_back(x);
        }
        return r;
      };
      const auto tb = on_tick(true), ta = on_tick(false);
      const long own_bid = tb.empty() ? 0 : long(tb[0].first), own_ask = ta.empty() ? 0 : long(ta[0].first);
      bool bad = ob_bid != own_bid || ob_ask != own_ask;
      std::string why;
      if (bad)
        why = "bbo OB " + std::to_string(ob_bid) + "/" + std::to_string(ob_ask) + " own " +
              std::to_string(own_bid) + "/" + std::to_string(own_ask);
      for (bool bid : {true, false})
        for (const auto &[px, lv] : (bid ? tb : ta))
        {
          ++ob_checks;
          const auto q = obs.qat(ob, bid, uint32_t(px / tick));
          if (q.sz != long(lv.shares) || q.cnt != long(lv.count))
          {
            if (why.empty())
              why = std::string(bid ? "bid " : "ask ") + std::to_string(px) + " OB " +
                    std::to_string(q.sz) + "/" + std::to_string(q.cnt) + " own " +
                    std::to_string(lv.shares) + "/" + std::to_string(lv.count);
            bad = true;
          }
        }
      if (bad)
      {
        ++ob_mismatch;
        if (ob_first.size() < 10)
          ob_first.push_back(fmt_ts(last_ts) + " " + ins.at(loc).symbol + ": " + why);
      }
    }
  };

  const auto t0 = std::chrono::steady_clock::now();
  const char *blk;
  size_t len;
  while (msgs < max_msgs && reader.next(blk, len))
  {
    ++msgs;
    const uint64_t this_seq = pz.next_seq();
    if (len >= 13)
    {
      uint64_t ts = 0;
      for (int k = 0; k < 6; ++k)
        ts = (ts << 8) | uint8_t(blk[7 + k]);
      last_ts = ts;
    }
    oracle.on_itch(blk, len, this_seq);
    qc.on(blk, len);
    fake.hist.emplace_back(this_seq, std::string(blk, len));
    if (fake.hist.size() > 4000000)
      fake.hist.pop_front();
    if (reset_at && this_seq == reset_at && !pz.packet().empty())
    {
      pz.flush();
      session = "TEST000002";
    }
    if (reset_at && this_seq >= reset_at)
      pz.set_session(session);
    if (pz.add(blk, len))
    {
      ++pkts;
      const std::string &pkt = pz.packet();
      const uint64_t first = pkt.size() >= 20 ? this_seq : 0;
      (void)first;
      bool drop = drop_every && pkts % drop_every == 0;
      if (glimpse_at && this_seq >= glimpse_at && glimpse_dropped < glimpse_len)
      {
        drop = true;
        ++glimpse_dropped;
        fake.answer = false;
      }
      else if (glimpse_at && fake.spins > 0)
        fake.answer = true;   // re-requests go unanswered until the handler gives up
      last_dropped = drop;
      if (drop)
        ++dropped;
      else
      {
        deliver(pkt);
        if (dup_every && pkts % dup_every == 0)
        {
          ++duped;
          deliver(pkt);
        }
      }
    }
    if (sample && msgs % sample == 0)
      sample_due = true;
    // Compare at a packet boundary only: the oracle is ahead of the handler by
    // the messages of the packet being built.
    // (and not right after a dropped packet: the hole shows with the next one)
    if (sample_due && pz.pending() == 0 && !last_dropped)
    {
      sample_due = false;
      if (!own_only)
        compare_ob();
      if (drop_every || dup_every || reset_at || glimpse_at)
      {
        if (!feed.sequencer().in_hole() && !feed.sequencer().awaiting_snapshot())
        {
          ++oracle_checks;
          auto d = diff_books(feed.dispatcher().book(), oracle.book(), ins);
          total_diff.levels += d.levels;
          total_diff.level_mismatch += d.level_mismatch;
          total_diff.queues += d.queues;
          total_diff.queue_mismatch += d.queue_mismatch;
          for (auto &s : d.first)
            if (total_diff.first.size() < 10)
              total_diff.first.push_back(fmt_ts(last_ts) + " " + s);
        }
      }
      std::cerr << "  " << msgs << " msgs, ts " << fmt_ts(last_ts) << ", own orders "
                << feed.dispatcher().book().size() << "\n";
    }
  }
  if (pz.flush())
  {
    ++pkts;
    deliver(pz.packet());
  }
  // end of session, and let outstanding requests and spins finish
  for (int k = 0; k < int(spin_delay) + 2; ++k)
    deliver(MoldPacketizer::header_only(session, pz.next_seq(), 0));
  if (!own_only)
    compare_ob();
  if (drop_every || dup_every || reset_at || glimpse_at)
  {
    ++oracle_checks;
    auto d = diff_books(feed.dispatcher().book(), oracle.book(), ins);
    total_diff.levels += d.levels;
    total_diff.level_mismatch += d.level_mismatch;
    total_diff.queues += d.queues;
    total_diff.queue_mismatch += d.queue_mismatch;
    for (auto &s : d.first)
      if (total_diff.first.size() < 10)
        total_diff.first.push_back("end " + s);
  }
  const double secs =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

  // ---- report -----------------------------------------------------------------
  const auto &st = feed.dispatcher().stats();
  const auto &ost = oracle.stats();
  const auto &sq = feed.sequencer().stats();
  std::cout << "input: " << reader.bytes() << " bytes, " << msgs << " messages"
            << (reader.truncated() ? " (last message cut)" : "") << ", last ts " << fmt_ts(last_ts)
            << ", " << secs << " s\n";
  std::cout << "packets: " << pkts << " built (" << per_packet << " msgs max), " << dropped
            << " dropped, " << duped << " duplicated\n";
  std::cout << "messages by type (handler):";
  for (int t = 0; t < 128; ++t)
    if (st.msgs[t])
      std::cout << " " << char(t) << "=" << st.msgs[t];
  std::cout << "\n";
  std::cout << "routed instruments: " << ins.routed().size() << " (OB instances " << obs.books.size()
            << ")\n";
  std::cout << "records to books:";
  for (auto &[k, n] : counting.by_type)
    std::cout << " " << k << "=" << n;
  std::cout << "\n";
  auto unk = [](const DispatcherStats &s) {
    std::string r;
    for (char t : {'E', 'C', 'X', 'D', 'U'})
      r += std::string(" ") + t + "=" + std::to_string(s.unknown_ref[uint8_t(t)]);
    return r;
  };
  std::cout << "own book (handler): unknown refs" << unk(st) << "; exec>remaining " << st.exec_over
            << "; add dup " << st.add_duplicate << "; price 0 " << st.price_zero
            << "; off tick " << st.off_tick << "; crossed@17 " << st.crossed_17 << "; locked@17 " << st.locked_17
            << "; ts backwards " << st.ts_backwards << "; broken " << st.broken
            << "; Q capped " << st.cross_capped << "; ts clamped " << st.ts_clamped
            << "; resets " << st.resets << "; recoveries " << st.recoveries
            << "; gaps " << st.gaps << "; boundary suspects " << st.boundary_suspect
            << "; orders at end " << feed.dispatcher().book().size() << "\n";
  std::cout << "own book (oracle, every message): unknown refs" << unk(ost) << "; exec>remaining "
            << ost.exec_over << "; crossed@17 " << ost.crossed_17 << "; locked@17 " << ost.locked_17
            << "; ts backwards " << ost.ts_backwards << "; orders at end "
            << oracle.book().size() << "\n";
  std::cout << "sequencer: packets " << sq.packets << ", applied msgs " << sq.applied_messages
            << ", heartbeats " << sq.heartbeats << ", dup packets " << sq.dup_packets
            << ", dup msgs " << sq.dup_messages << ", gaps " << sq.gaps << ", requests "
            << sq.requests << " (" << sq.requested_messages << " msgs), lost " << sq.lost_messages
            << ", snapshots " << sq.snapshots << " (discarded " << sq.snapshot_discarded
            << " buffered msgs), sessions " << sq.sessions << ", buffered peak "
            << sq.buffered_peak << "\n";
  std::cout << "fake re-request server: requests " << fake.requests << ", served " << fake.served
            << ", unanswered " << fake.unanswered << "; GLIMPSE spins " << fake.spins
            << " (last N " << spin_n << ")\n";
  if (!own_only)
    std::cout << "OB vs own book: " << samples << " samples, " << ob_checks << " checks, "
              << ob_mismatch << " book mismatches (" << outside_ring
              << " own levels beyond OB's ring window not compared)\n";
  for (auto &s : ob_first)
    std::cout << "  mismatch: " << s << "\n";
  if (oracle_checks)
    std::cout << "handler vs oracle: " << oracle_checks << " comparisons, " << total_diff.levels
              << " levels (" << total_diff.level_mismatch << " differ), " << total_diff.queues
              << " queues (" << total_diff.queue_mismatch << " differ)\n";
  for (auto &s : total_diff.first)
    std::cout << "  differs: " << s << "\n";
  static const char *on[] = {"A/F", "U same px smaller", "U same px same size", "U same px larger",
                             "U new px", "spin"};
  std::cout << "queue position of executions (head / not head):";
  for (int o = 0; o < 5; ++o)
    std::cout << " [" << on[o] << " " << qc.exec[o][1] << "/" << qc.exec[o][0] << "]";
  std::cout << "; non-head with a same-px smaller U ahead: " << qc.behind_u_down << "\n";
  std::cout << "  E only (head / not head):";
  for (int o = 0; o < 5; ++o)
    std::cout << " [" << on[o] << " " << qc.exec_e[o][1] << "/" << qc.exec_e[o][0] << "]";
  std::cout << "\n";
  std::cout << "  by message: E head/not " << qc.by_type[0][1] << "/" << qc.by_type[0][0]
            << ", C head/not " << qc.by_type[1][1] << "/" << qc.by_type[1][0]
            << "; non-head E: every order ahead an odd lot " << qc.nonhead_e_ahead_odd
            << ", some round lot ahead " << qc.nonhead_e_ahead_mixed << ", mean orders ahead "
            << (qc.by_type[0][0] ? double(qc.nonhead_e_rank_sum) / double(qc.by_type[0][0]) : 0.0)
            << "; E from 09:30 head/not " << qc.e_after_open[1] << "/" << qc.e_after_open[0] << "\n";
  for (auto &s : log)
    std::cout << "log: " << s << "\n";

  const bool fail = st.crossed_17 || ost.crossed_17 || ob_mismatch || total_diff.level_mismatch ||
                    total_diff.queue_mismatch || st.exec_over || st.decode_errors ||
                    (!(drop_every || reset_at || glimpse_at) &&
                     (st.unknown_ref['E'] + st.unknown_ref['C'] + st.unknown_ref['X'] +
                      st.unknown_ref['D'] + st.unknown_ref['U']));
  std::cout << (fail ? "RESULT: FAIL\n" : "RESULT: PASS\n");
  return fail ? 1 : 0;
}
