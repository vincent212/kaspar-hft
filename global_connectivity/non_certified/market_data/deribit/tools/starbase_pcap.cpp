/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// starbase_pcap: decode a Starbase market-data capture and check the books.
//
//   starbase_pcap <file.pcap> [--dump] [--ob] [--quiet]
//
//   (default)  decode every packet, rebuild every instrument's book with
//              StarbaseBook, print the decode counts and the invariant counts
//   --dump     one line per packet and per message, every field, in the
//              format tools/sdk_dump.py writes from Deribit's own Python SDK
//              (`make sdk-check` compares the two)
//   --ob       also route every non-combo instrument that has incremental book
//              messages in the capture to its own OB, and after each packet
//              compare OB's best bid and offer and every level's size and
//              order count with StarbaseBook's
//
// Ethernet (with or without one VLAN tag), IPv4, UDP; classic pcap, micro- or
// nanosecond timestamps.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <boost/property_tree/ptree.hpp>

#include "deribit_md/Starbase.hpp"
#include "deribit_md/StarbaseBook.hpp"
#include "enum/e_names.hpp"
#include "frame/cons/msg/Get.hpp"
#include "frame/cons/msg/Page.hpp"
#include "frame/mda/msg/Data.hpp"
#include "frame/ob/act/OB.hpp"
#include "frame/ref/RefData.hpp"

using namespace deribit::starbase;

namespace
{
  struct Udp
  {
    uint32_t dst = 0;
    uint16_t dport = 0;
    const uint8_t *p = nullptr;
    size_t n = 0;
  };

  // Every UDP payload in the capture, in file order.
  std::vector<Udp> read_pcap(const std::vector<uint8_t> &d)
  {
    std::vector<Udp> out;
    if (d.size() < 24)
      return out;
    size_t off = 24;
    while (off + 16 <= d.size())
    {
      uint32_t incl;
      std::memcpy(&incl, &d[off + 8], 4);
      off += 16;
      if (off + incl > d.size())
        break;
      const uint8_t *f = &d[off];
      off += incl;
      size_t o = 14;
      if (incl < 14)
        continue;
      uint16_t et = uint16_t(f[12] << 8 | f[13]);
      if (et == 0x8100 && incl >= 18)
      {
        et = uint16_t(f[16] << 8 | f[17]);
        o = 18;
      }
      if (et != 0x0800 || incl < o + 20)
        continue;
      const size_t ihl = size_t(f[o] & 15) * 4;
      if (f[o + 9] != 17 || incl < o + ihl + 8)
        continue;
      Udp u;
      u.dst = uint32_t(f[o + 16]) << 24 | uint32_t(f[o + 17]) << 16 | uint32_t(f[o + 18]) << 8 | f[o + 19];
      u.dport = uint16_t(f[o + ihl + 2] << 8 | f[o + ihl + 3]);
      const size_t ulen = size_t(f[o + ihl + 4] << 8 | f[o + ihl + 5]);
      u.p = f + o + ihl + 8;
      u.n = std::min(ulen >= 8 ? ulen - 8 : 0, incl - (o + ihl + 8));
      out.push_back(u);
    }
    return out;
  }

  class Probe : public actors::Actor
  {
  public:
    Probe() { strncpy(name, "probe", sizeof(name) - 1); }
    std::string last;
    void send(const actors::Message *m, actors::Actor *) noexcept override
    {
      if (auto p = dynamic_cast<const frame::cons::msg::Page *>(m))
        last = p->val;
    }
  };

  struct TestableOB : public frame::ob::act::OB
  {
    using frame::ob::act::OB::OB;
    using frame::ob::act::OB::reply_to;
  };

  void dispatch(actors::Actor *a, const actors::Message *m, actors::Actor *sender)
  {
    const_cast<actors::Message *>(m)->sender = sender;
    auto it = a->handlers.find(std::type_index(typeid(*m)));
    if (it != a->handlers.end())
      (a->*(it->second))(m);
  }

  std::string ask(TestableOB &ob, Probe &probe, const char *cmd, std::map<std::string, std::string> args = {})
  {
    frame::cons::msg::Get g(cmd, args);
    probe.last.clear();
    ob.reply_to = &probe;
    dispatch(&ob, &g, &probe);
    ob.reply_to = nullptr;
    return probe.last;
  }
}

int main(int argc, char **argv)
{
  if (argc < 2)
  {
    std::cerr << "usage: starbase_pcap <file.pcap> [--dump] [--ob] [--quiet]" << std::endl;
    return 2;
  }
  bool dump_mode = false, ob_mode = false, quiet = false;
  for (int i = 2; i < argc; ++i)
  {
    const std::string a = argv[i];
    if (a == "--dump") dump_mode = true;
    else if (a == "--ob") ob_mode = true;
    else if (a == "--quiet") quiet = true;
  }
  std::ifstream f(argv[1], std::ios::binary);
  std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  const auto udps = read_pcap(data);
  if (udps.empty())
  {
    std::cerr << "no UDP packets in " << argv[1] << std::endl;
    return 1;
  }

  if (dump_mode)
  {
    for (const auto &u : udps)
    {
      PacketHeader ph;
      std::vector<Message> msgs;
      decode_packet(u.p, u.n, ph, msgs);
      std::cout << dump_packet_header(ph) << "\n";
      size_t off = kPacketHeaderSize;
      for (const auto &m : msgs)
      {
        std::cout << dump(u.p + off, m.length, m.seq) << "\n";
        off += m.length;
      }
    }
    return 0;
  }

  // Decode everything once.
  struct Pkt
  {
    PacketHeader ph;
    std::vector<Message> msgs;
  };
  std::vector<Pkt> pkts;
  std::map<Status, uint64_t> status;
  std::map<uint16_t, uint64_t> by_template;
  uint64_t unknown_templates = 0;
  std::set<int64_t> with_incremental;
  for (const auto &u : udps)
  {
    Pkt p;
    ++status[decode_packet(u.p, u.n, p.ph, p.msgs)];
    for (const auto &m : p.msgs)
    {
      ++by_template[m.templateId];
      if (std::holds_alternative<Unknown>(m.body))
        ++unknown_templates;
      if ((p.ph.type & INCREMENTAL) && (std::holds_alternative<Put>(m.body) ||
                                        std::holds_alternative<QtyReduced>(m.body) ||
                                        std::holds_alternative<Delete>(m.body) ||
                                        std::holds_alternative<Trade>(m.body)))
        std::visit([&](const auto &x) {
          if constexpr (requires { x.instrumentId; })
            with_incremental.insert(x.instrumentId);
        }, m.body);
    }
    pkts.push_back(std::move(p));
  }

  // Optional chain: one OB per routed instrument.
  Probe probe;
  std::map<int32_t, std::unique_ptr<TestableOB>> obs;   // sec_id -> OB
  std::map<int32_t, int> sym_of;
  int32_t next_sec = 5000;
  if (ob_mode)
  {
    const char *k = std::getenv("KSPRPROJ");
    frame::ref::RefData::set_universe(std::string(k ? k : ".") + "/unit_test/config/universe.csv", "");
    frame::ref::RefData::inst();
  }
  uint64_t ob_records = 0;
  StarbaseBook::Config cfg;
  cfg.venue = char(en::x::DERIBIT);
  cfg.route = [&](const InstrumentDefinition &d) -> int32_t {
    // Combos can trade at zero or negative prices; OB cannot hold them.
    if (!ob_mode || d.type == 3 || d.type == 4 || d.type == 2 || !with_incremental.count(d.instrumentId))
      return 0;
    const int32_t sec = next_sec++;
    auto a = frame::ref::RefData::add_future_asset(d.name + "_SB", en::x::DERIBIT, sec, "", d.name, 1.0);
    if (!a)
      return 0;
    a->maxpx = 1 << 30;
    sym_of[sec] = a->id;
    boost::property_tree::ptree pt;
    pt.put("ladder_ticks", 1 << 14);
    obs[sec] = std::make_unique<TestableOB>(nullptr, true, nullptr, a->id, pt);
    return sec;
  };
  cfg.emit = [&](int32_t sec, const bfile::l3_t &r) {
    auto it = obs.find(sec);
    if (it == obs.end())
      return;
    ++ob_records;
    frame::mda::msg::Data d(true);
    d.l3 = r;
    dispatch(it->second.get(), &d, &probe);
  };
  StarbaseBook book(cfg);

  uint64_t ob_checks = 0, ob_bbo_mismatch = 0, ob_levels = 0, ob_level_mismatch = 0;
  for (const auto &p : pkts)
  {
    book.on_packet(p.ph, p.msgs);
    if (!ob_mode || !(p.ph.type & INCREMENTAL) || p.msgs.empty())
      continue;
    for (const auto &[sec, ob] : obs)
    {
      const Instrument *in = nullptr;
      for (const auto &[id, x] : book.instruments())
        if (x.sec_id == sec)
          in = &x;
      if (!in || !in->synced)
        continue;
      ++ob_checks;
      const auto bl = in->levels(true), al = in->levels(false);
      int b = -1, a = -1;
      std::sscanf(ask(*ob, probe, "bbbo").c_str(), "BBBO: %d %d", &b, &a);
      const int eb = bl.empty() ? b : int(book.ticks(*in, bl[0].price9));
      const int ea = al.empty() ? a : int(book.ticks(*in, al[0].price9));
      if ((!bl.empty() && b != eb) || (!al.empty() && a != ea))
      {
        ++ob_bbo_mismatch;
        if (!quiet)
          std::cerr << in->def.name << ": OB bbo " << b << " " << a << " book " << eb << " " << ea << std::endl;
      }
      for (int s = 0; s < 2; ++s)
        for (const auto &l : (s == 0 ? bl : al))
        {
          ++ob_levels;
          int sz = -1, cnt = -1, sim = -1;
          std::sscanf(ask(*ob, probe, "qat", {{"side", s == 0 ? "B" : "S"},
                                               {"px", std::to_string(book.ticks(*in, l.price9))}})
                          .c_str(),
                      "QAT: %d %d %d", &sz, &cnt, &sim);
          // OB's size is the sum of each order's displayQty.
          int expect = 0;
          const auto &side = s == 0 ? in->bids : in->asks;
          for (const auto &[so, id] : side.at(l.price9))
            expect += int(book.units(*in, in->orders.at(id).qty));
          if (sz != expect || cnt != int(l.orders))
          {
            ++ob_level_mismatch;
            if (!quiet)
              std::cerr << in->def.name << (s == 0 ? " bid " : " ask ") << l.price9 << ": OB " << sz
                        << "/" << cnt << " book " << expect << "/" << l.orders << std::endl;
          }
        }
    }
  }

  const auto &s = book.stats();
  std::cout << "udp packets " << udps.size() << "\n";
  for (const auto &[st, n] : status)
    std::cout << "decode " << to_string(st) << " " << n << "\n";
  std::cout << "unknown templates " << unknown_templates << "\ntemplates";
  for (const auto &[t, n] : by_template)
    std::cout << " " << t << ":" << n;
  size_t synced = 0, orders = 0, with_def = 0;
  for (const auto &[id, in] : book.instruments())
  {
    synced += in.synced;
    orders += in.orders.size();
    with_def += in.have_def;
  }
  std::cout << "\ninstruments " << book.instruments().size() << " with_definition " << with_def
            << " synced " << synced << " resting_orders " << orders << "\n"
            << "packets " << s.packets << " messages " << s.messages << " heartbeats " << s.heartbeats
            << " inc_messages " << s.inc_messages << " snap_messages " << s.snap_messages << "\n"
            << "applied " << s.applied << " buffered " << s.buffered << " dropped_before_sync "
            << s.dropped_before_sync << " duplicates " << s.duplicates << " gaps " << s.gaps
            << " missing " << s.missing << " snap_gaps " << s.snap_gaps << "\n"
            << "snapshots " << s.snapshots << " cycles " << s.cycles << " syncs " << s.syncs
            << " resyncs " << s.resyncs << "\n"
            << "snap_compared " << s.snap_compared << " (after changes " << s.snap_compared_changed
            << ") snap_match " << s.snap_match
            << " snap_mismatch " << s.snap_mismatch << " snap_not_comparable " << s.snap_not_comparable
            << " snap_trailer_mismatch " << s.snap_trailer_mismatch << " snap_priority_disorder "
            << s.snap_priority_disorder << "\n"
            << "crossed " << s.crossed << " unknown_ref " << s.unknown_ref << " joined_ahead "
            << s.joined_ahead << " reduce_not_smaller " << s.reduce_not_smaller << "\n"
            << "put_new " << s.put_new << " put_smaller " << s.put_smaller << " put_same " << s.put_same
            << " put_larger_same_sort " << s.put_larger_same_sort << " put_reprice_same_sort "
            << s.put_reprice_same_sort << " put_new_sort " << s.put_new_sort << " put_side_change "
            << s.put_side_change << "\n"
            << "qty_off_step " << s.qty_off_step << " px_off_tick " << s.px_off_tick
            << " px_not_positive " << s.px_not_positive << "\n"
            << "trades " << s.trades << " trade_summaries " << s.trade_summaries << " block_trades "
            << s.block_trades << " implied_fills " << s.implied_fills << " fill_px_mismatch "
            << s.fill_px_mismatch << " fill_consistent " << s.fill_consistent << " fill_inconsistent "
            << s.fill_inconsistent << " txn_unclosed " << s.txn_unclosed << "\n"
            << "records " << s.records << "\n";
  if (ob_mode)
    std::cout << "ob books " << obs.size() << " records " << ob_records << " checks " << ob_checks
              << " bbo_mismatch " << ob_bbo_mismatch << " levels " << ob_levels << " level_mismatch "
              << ob_level_mismatch << "\n";
  return 0;
}
