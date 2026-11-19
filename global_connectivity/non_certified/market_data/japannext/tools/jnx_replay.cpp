/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// jnx_replay: replays a Japannext ITCH MoldUDP64 capture through the handler
// into real OB instances (one per instrument), checks invariants, and reports.
//
//   jnx_replay --udp ITCH.UDP.pcap --glimpse GLIMPSE.pcap [--scenario NAME]... [--all]
//              [--levels 5] [--sample-every 2000] [--max-ticks 400] [--no-ob]
//
// The directory (tick tables, Orderbook Directory, trading states) comes from
// the GLIMPSE capture: the public UDP capture starts after the day's directory
// was sent, and the GLIMPSE capture is truncated before End of Snapshot, so it
// is preloaded (Handler::preload_directory), not applied as a snapshot.
//
// Scenarios (each a fresh handler and fresh OBs; the expected result of every
// injected one is the baseline's final book):
//   baseline   the capture as it is
//   dup        every 50th data packet twice, every 97th pair of packets swapped
//   rerequest  runs of packets dropped; the re-request server is simulated from
//              the capture (one reply packet per request, as MOLD 4 allows)
//   snapshot   packets dropped, no re-request server: gap record, GLIMPSE
//              snapshot written from a reference handler at sequence N, which
//              falls inside a buffered packet
//   reset      a channel reset forced mid-stream, recovered by snapshot
//   latejoin   the handler starts mid-capture and joins by snapshot
//   eos        End of Session injected after the capture: every book empty

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "BookCheck.hpp"
#include "frame/ref/Price.hpp"
#include "frame/ref/RefData.hpp"
#include "jnx_md/Handler.hpp"
#include "jnx_md/Pcap.hpp"
#include "jnx_md/Writer.hpp"
#include "jnx_md/gen/moldudp64.hpp"

using namespace jnx_md;

namespace
{
  struct Options
  {
    std::string udp, glimpse;
    uint16_t glimpse_port = 15002;
    std::vector<std::string> scenarios;
    size_t levels = 5;
    size_t sample_every = 2000;
    int max_ticks = 400;
    bool ob = true;
    bool abort_on_cross = true;
    std::string dump;   // SICC.GROUP: print the handler's and OB's book after each snapshot
  };

  struct Pkt
  {
    std::string bytes;
    uint64_t ts = 0, seq = 0;
    uint16_t count = 0;
  };

  std::vector<Pkt> g_pkts;
  std::string g_glimpse;
  uint64_t g_midnight = 0;

  void load(const Options &o)
  {
    Pcap u(o.udp);
    for (const auto &p : u.udp())
    {
      if (p.payload.size() < 20)
        continue;
      Pkt k;
      k.bytes.assign(p.payload);
      k.ts = p.ts_ns;
      jnx::moldudp64::from_jnx::DownstreamPacketHeader h;
      std::memcpy(&h, k.bytes.data(), sizeof(h));
      k.seq = h.SequenceNumber;
      k.count = h.MessageCount;
      g_pkts.push_back(std::move(k));
    }
    Pcap gl(o.glimpse);
    g_glimpse = gl.tcp_stream(o.glimpse_port);
    g_midnight = jst_midnight_ns(session_date(g_pkts.front().ts));
  }

  Handler::Config config(Handler::Join join)
  {
    Handler::Config c;
    c.join = join;
    c.dispatch.midnight_ns = g_midnight;
    c.rerequest_timeout_ns = 2000000000ull;
    return c;
  }

  // A sink that checks every price OB will be given, then forwards.
  struct CheckingSink : Sink
  {
    Sink &next;
    Handler *h = nullptr;
    uint64_t prices = 0, price_off_ladder = 0, price_mismatch = 0;
    explicit CheckingSink(Sink &n) : next(n) {}
    void book(int32_t asset, const bfile::l3_t &r) override
    {
      if (const auto *m = std::get_if<bfile::l3_mbo_v2_t>(&r); m && m->orderUpdateAction != 2)
      {
        ++prices;
        const Instrument *i = h ? h->instruments().by_asset(asset) : nullptr;
        // OB's own conversion (OB.cpp: ref::Price((long double)pxd, sym).to_int()).
        const int t = frame::ref::Price((long double)m->pxd, uint(asset)).to_int();
        if (!(t > 0 && i && t < i->maxpx))
          ++price_off_ladder;
        const int64_t raw = std::llround(m->pxd * 10.0);
        if (i && (raw % i->unit || t != raw / i->unit))
          ++price_mismatch;
      }
      if (trace && h)
        if (const Instrument *i = h->instruments().by_asset(asset); i && i->sicc == trace_sicc)
          std::visit([&](const auto &x) {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, bfile::l3_mbo_v2_t>)
              std::printf("TRACE mbo a=%d oid=%llu px=%.1f q=%u side=%c rec=%d tx=%llu\n",
                          int(x.orderUpdateAction), (unsigned long long)x.orderID, x.pxd,
                          x.displayQty, x.side, int(x.recovery), (unsigned long long)x.transactTime);
            else if constexpr (std::is_same_v<T, bfile::l3_mbo_trd_v2_t>)
              std::printf("TRACE exec oid=%llu q=%d tx=%llu\n", (unsigned long long)x.orderID,
                          x.lastQty, (unsigned long long)x.transactTime);
            else if constexpr (std::is_same_v<T, bfile::l3_sst_t>)
              std::printf("TRACE sst %d tx=%llu\n", int(x.tradingStatus), (unsigned long long)x.txtim);
          }, r);
      next.book(asset, r);
    }
    bool trace = std::getenv("JNX_TRACE") != nullptr;
    std::string trace_sicc = trace ? std::getenv("JNX_TRACE") : "";
    void all_books(const bfile::l3_t &r) override { next.all_books(r); }
    void recorder(const bfile::l3_t &r) override { next.recorder(r); }
  };

  struct NullSink : Sink
  {
    void book(int32_t, const bfile::l3_t &) override {}
    void all_books(const bfile::l3_t &) override {}
  };

  // The handler's book state after packets [0, upto) and the first `partial`
  // messages of packet upto: the reference a snapshot is written from.
  std::unique_ptr<Handler> reference(size_t upto, size_t partial, NullSink &sink)
  {
    auto h = std::make_unique<Handler>(config(Handler::Join::FirstPacket), sink);
    h->instruments();
    h->preload_directory(g_glimpse.data(), g_glimpse.size());
    for (size_t k = 0; k < upto; ++k)
      h->on_packet(g_pkts[k].bytes.data(), g_pkts[k].bytes.size(), g_pkts[k].ts);
    if (partial)
    {
      const auto &p = g_pkts[upto];
      auto msgs = mold_messages(p.bytes);
      msgs.resize(partial);
      const std::string cut = mold_packet(std::string_view(p.bytes.data(), 10), p.seq, msgs);
      h->on_packet(cut.data(), cut.size(), p.ts);
    }
    return h;
  }

  struct Result
  {
    std::string name;
    double secs = 0;
    Stats st;
    uint64_t ob_books = 0, ob_records = 0, ob_bad_px = 0;
    uint64_t prices = 0, price_off_ladder = 0, price_mismatch = 0;
    check::ObCompare sampled, final_cmp, after_snapshot;
    check::HandlerCompare vs_baseline;
    bool compared_to_baseline = false;
    uint64_t snapshots_written = 0, rerequest_replies = 0, injected = 0;
    size_t nonempty_books = 0, resting_orders = 0;
    std::string note;
  };

  std::unique_ptr<Handler> g_baseline;
  NullSink g_null;

  Result run(const std::string &name, const Options &o)
  {
    Result r;
    r.name = name;
    const auto t0 = std::chrono::steady_clock::now();
    check::ObSink obs(o.abort_on_cross);
    NullSink none;
    Sink &inner = o.ob ? static_cast<Sink &>(obs) : static_cast<Sink &>(none);
    CheckingSink sink(inner);

    // Requests from the handler, answered between packets.
    std::deque<std::pair<uint64_t, uint32_t>> requests;
    bool want_snapshot = false;
    Handler::Hooks hooks;
    const bool server = name == "rerequest" || name == "dup";
    hooks.rerequest = [&](std::string_view, uint64_t seq, uint32_t n) {
      if (!server)
        return false;
      requests.emplace_back(seq, n);
      return true;
    };
    hooks.request_snapshot = [&] { want_snapshot = true; };

    const Handler::Join join = name == "latejoin" ? Handler::Join::Snapshot
                                                  : Handler::Join::FirstPacket;
    Handler h(config(join), sink, hooks);
    sink.h = &h;
    h.preload_directory(g_glimpse.data(), g_glimpse.size());
    want_snapshot = false;

    const size_t n = g_pkts.size();
    // Data packets only, as positions for injections.
    std::vector<size_t> data;
    for (size_t k = 0; k < n; ++k)
      if (g_pkts[k].count && g_pkts[k].count != 0xFFFF)
        data.push_back(k);
    std::set<size_t> drop;
    size_t start = 0, reset_at = SIZE_MAX;
    if (name == "rerequest")
      for (double f : {0.10, 0.35, 0.60, 0.85})
        for (size_t k = 0; k < 3; ++k)
          drop.insert(data[size_t(f * double(data.size())) + k]);
    if (name == "snapshot")
      for (size_t k = 0; k < 4; ++k)
        drop.insert(data[size_t(0.45 * double(data.size())) + k]);
    if (name == "reset")
      reset_at = data[size_t(0.55 * double(data.size()))];
    if (name == "latejoin")
      start = data[size_t(0.40 * double(data.size()))];

    auto deliver = [&](const std::string &b, uint64_t ts) {
      h.on_packet(b.data(), b.size(), ts);
    };
    // Answer re-requests: one reply packet each, from the request's first
    // message to the end of the original packet holding it.
    auto answer = [&](uint64_t ts) {
      while (!requests.empty())
      {
        auto [seq, cnt] = requests.front();
        requests.pop_front();
        // The data packet holding seq.
        auto it = std::partition_point(data.begin(), data.end(), [&](size_t k) {
          return g_pkts[k].seq + g_pkts[k].count <= seq;
        });
        if (it == data.end() || g_pkts[*it].seq > seq)
          continue;
        const auto &p = g_pkts[*it];
        auto msgs = mold_messages(p.bytes);
        msgs.erase(msgs.begin(), msgs.begin() + long(seq - p.seq));
        if (msgs.size() > cnt)
          msgs.resize(cnt);
        ++r.rerequest_replies;
        deliver(mold_packet(std::string_view(p.bytes.data(), 10), seq, msgs), ts);
      }
    };
    // Write the snapshot from the reference at N inside packet j (j buffered).
    auto snapshot = [&](size_t j) {
      want_snapshot = false;
      const auto &p = g_pkts[j];
      const size_t partial = p.count > 1 ? 1 : 0;
      const uint64_t N = p.seq + partial;
      auto ref = reference(j, partial, g_null);
      const std::string snap = glimpse_snapshot(*ref, N);
      ++r.snapshots_written;
      r.note += " snapshot at N=" + std::to_string(N) + " (" + std::to_string(snap.size()) +
                " bytes)";
      // In chunks, as TCP would deliver them.
      for (size_t off = 0; off < snap.size(); off += 1400)
        h.on_glimpse_bytes(snap.data() + off, std::min<size_t>(1400, snap.size() - off));
      if (o.ob)
        for (const auto &[a, ob] : obs.obs())
          if (const Instrument *i = h.instruments().by_asset(a))
            check::compare_ob(*i, *ob, 1000000, o.max_ticks, r.after_snapshot);
    };

    std::vector<std::string> swapped;
    size_t since_sample = 0;
    for (size_t k = start; k < n; ++k)
    {
      const auto &p = g_pkts[k];
      if (k == reset_at)
      {
        h.force_recovery("injected");
        ++r.injected;
      }
      if (drop.count(k))
      {
        ++r.injected;
        continue;
      }
      if (name == "dup" && p.count && p.count != 0xFFFF)
      {
        const size_t idx = size_t(std::lower_bound(data.begin(), data.end(), k) - data.begin());
        if (idx % 97 == 0 && k + 1 < n && g_pkts[k + 1].count && g_pkts[k + 1].count != 0xFFFF)
        {
          // Swap with the next packet: it arrives first (a gap, closed by this one).
          deliver(g_pkts[k + 1].bytes, g_pkts[k + 1].ts);
          deliver(p.bytes, p.ts);
          answer(p.ts);
          ++k;
          r.injected += 1;
          continue;
        }
        if (idx % 50 == 0)
        {
          deliver(p.bytes, p.ts);
          ++r.injected;
        }
      }
      deliver(p.bytes, p.ts);
      answer(p.ts);
      if (want_snapshot)
      {
        // The packet that started recovery is buffered; if the reset came
        // between packets, this one is.
        size_t j = k;
        while (j + 1 < n && !(g_pkts[j].count && g_pkts[j].count != 0xFFFF))
        {
          ++j;
          deliver(g_pkts[j].bytes, g_pkts[j].ts);
        }
        snapshot(j);
        k = j;
        answer(p.ts);
      }
      if (o.ob && ++since_sample >= o.sample_every)
      {
        since_sample = 0;
        for (int32_t a : obs.take_touched())
          if (auto *ob = obs.ob(a))
            if (const Instrument *i = h.instruments().by_asset(a))
              check::compare_ob(*i, *ob, o.levels, o.max_ticks, r.sampled);
      }
    }
    if (name == "eos")
    {
      const auto &last = g_pkts.back();
      deliver(mold_packet(std::string_view(last.bytes.data(), 10), h.next_seq(), {}, 0xFFFF),
              last.ts + 1);
      ++r.injected;
    }
    r.secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    r.st = h.stats();
    r.prices = sink.prices;
    r.price_off_ladder = sink.price_off_ladder;
    r.price_mismatch = sink.price_mismatch;
    for (const auto &i : h.instruments().all())
      if (!i->book.empty())
        ++r.nonempty_books;
    r.resting_orders = h.dispatcher().orders().size();
    if (o.ob)
    {
      r.ob_books = obs.obs().size();
      r.ob_records = obs.records;
      for (const auto &[a, ob] : obs.obs())
        if (const Instrument *i = h.instruments().by_asset(a))
          check::compare_ob(*i, *ob, 1000000, o.max_ticks, r.final_cmp);
      r.ob_bad_px = obs.shutdown_bad_prices();
    }
    if (name == "baseline")
    {
      // Keep the baseline handler for the others to compare with: rerun without
      // OBs (the OB-fed one goes with its sink).
      g_baseline = std::make_unique<Handler>(config(Handler::Join::FirstPacket), g_null);
      g_baseline->preload_directory(g_glimpse.data(), g_glimpse.size());
      for (const auto &q : g_pkts)
        g_baseline->on_packet(q.bytes.data(), q.bytes.size(), q.ts);
      r.vs_baseline = check::compare_handlers(*g_baseline, h);
      r.compared_to_baseline = true;
    }
    else if (g_baseline && name != "eos")
    {
      r.vs_baseline = check::compare_handlers(*g_baseline, h);
      r.compared_to_baseline = true;
    }
    return r;
  }

  void report(const Result &r)
  {
    const Stats &s = r.st;
    std::printf("\n=== %s (%.1f s)%s\n", r.name.c_str(), r.secs, r.note.c_str());
    std::printf("packets %llu: data %llu, heartbeats %llu, end of session %llu, dup %llu, "
                "short %llu; injected %llu\n",
                (unsigned long long)s.packets, (unsigned long long)s.data_packets,
                (unsigned long long)s.heartbeats, (unsigned long long)s.end_of_session,
                (unsigned long long)s.dup_packets, (unsigned long long)s.short_packets,
                (unsigned long long)r.injected);
    std::printf("messages %llu (old/duplicate %llu), unknown %llu, malformed %llu, empty %llu\n",
                (unsigned long long)s.messages, (unsigned long long)s.old_messages,
                (unsigned long long)s.unknown, (unsigned long long)s.malformed,
                (unsigned long long)s.empty_blocks);
    std::printf("by type:");
    for (auto [c, k] : s.by_type)
      std::printf(" %c %llu", c, (unsigned long long)k);
    std::printf("\nsnapshot messages %llu:", (unsigned long long)s.snapshot_messages);
    for (auto [c, k] : s.snapshot_by_type)
      std::printf(" %c %llu", c, (unsigned long long)k);
    std::printf("\nsequencing: gaps %llu (%llu messages), re-requests %llu (replies %llu), "
                "escalations %llu, snapshots %llu/%llu (written %llu), buffered packets %llu, "
                "resets %llu, session changes %llu\n",
                (unsigned long long)s.gaps, (unsigned long long)s.gap_messages,
                (unsigned long long)s.rerequests, (unsigned long long)r.rerequest_replies,
                (unsigned long long)s.escalations, (unsigned long long)s.snapshots_done,
                (unsigned long long)s.snapshots_started, (unsigned long long)r.snapshots_written,
                (unsigned long long)s.buffered_packets, (unsigned long long)s.resets,
                (unsigned long long)s.session_changes);
    std::printf("references: unknown exec %llu, delete %llu, replace %llu, book %llu; "
                "dup add %llu, replace onto live number %llu, over-exec %llu, >58-bit numbers %llu\n",
                (unsigned long long)s.unknown_exec, (unsigned long long)s.unknown_delete,
                (unsigned long long)s.unknown_replace, (unsigned long long)s.unknown_book,
                (unsigned long long)s.dup_add, (unsigned long long)s.replace_to_existing,
                (unsigned long long)s.over_exec, (unsigned long long)s.wide_order_number);
    std::printf("timestamps going back %llu (largest %.6f s)\n",
                (unsigned long long)s.time_backsteps, double(s.max_backstep_ns) / 1e9);
    std::printf("replaces keeping place %llu, losing it %llu; reference prices %llu; "
                "seconds wraps %llu\n",
                (unsigned long long)s.replace_keep_place, (unsigned long long)s.replace_lose_place,
                (unsigned long long)s.reference_prices, (unsigned long long)s.seconds_wraps);
    std::printf("prices: %llu sent; off OB's ladder %llu, conversion mismatch %llu; off the "
                "Kaspar tick %llu, off the venue tick %llu, outside limits %llu; OB badpx %llu\n",
                (unsigned long long)r.prices, (unsigned long long)r.price_off_ladder,
                (unsigned long long)r.price_mismatch, (unsigned long long)s.px_off_unit,
                (unsigned long long)s.px_off_tick, (unsigned long long)s.px_out_of_limits,
                (unsigned long long)r.ob_bad_px);
    std::printf("handler book: crossed checks %llu, crossed in status 17 %llu, crossed in "
                "other status %llu, locked in 17 %llu; %zu books with orders, %zu orders resting\n",
                (unsigned long long)s.crossed_checks, (unsigned long long)s.crossed_at_17,
                (unsigned long long)s.crossed_other, (unsigned long long)s.locked_at_17,
                r.nonempty_books, r.resting_orders);
    std::printf("records:");
    for (auto [t, k] : s.records)
      std::printf(" %s %llu", en::l3_names[t], (unsigned long long)k);
    std::printf("\n");
    if (r.ob_books)
    {
      std::printf("OB: %llu books, %llu records; sampled: %llu checks, %llu levels, %llu "
                  "mismatched levels, %llu bbo mismatches; final (all levels): %llu books, "
                  "%llu levels, %llu mismatched levels, %llu bbo mismatches\n",
                  (unsigned long long)r.ob_books, (unsigned long long)r.ob_records,
                  (unsigned long long)r.sampled.instruments, (unsigned long long)r.sampled.levels,
                  (unsigned long long)r.sampled.mismatched_levels,
                  (unsigned long long)r.sampled.bbo_mismatches,
                  (unsigned long long)r.final_cmp.instruments,
                  (unsigned long long)r.final_cmp.levels,
                  (unsigned long long)r.final_cmp.mismatched_levels,
                  (unsigned long long)r.final_cmp.bbo_mismatches);
      if (r.after_snapshot.instruments)
        std::printf("OB right after each snapshot: %llu books, %llu levels, %llu mismatched "
                    "levels, %llu bbo mismatches\n",
                    (unsigned long long)r.after_snapshot.instruments,
                    (unsigned long long)r.after_snapshot.levels,
                    (unsigned long long)r.after_snapshot.mismatched_levels,
                    (unsigned long long)r.after_snapshot.bbo_mismatches);
      for (const auto &e : r.after_snapshot.examples)
        std::printf("  after snapshot: %s\n", e.c_str());
      for (const auto &e : r.sampled.examples)
        std::printf("  sampled: %s\n", e.c_str());
      for (const auto &e : r.final_cmp.examples)
        std::printf("  final: %s\n", e.c_str());
    }
    if (r.compared_to_baseline)
    {
      std::printf("vs baseline handler: %llu books, %llu orders compared, %llu mismatches\n",
                  (unsigned long long)r.vs_baseline.instruments,
                  (unsigned long long)r.vs_baseline.orders,
                  (unsigned long long)r.vs_baseline.mismatches);
      for (const auto &e : r.vs_baseline.examples)
        std::printf("  %s\n", e.c_str());
    }
    std::fflush(stdout);
  }
}

int main(int argc, char **argv)
{
  Options o;
  for (int k = 1; k < argc; ++k)
  {
    const std::string a = argv[k];
    auto val = [&] { return k + 1 < argc ? std::string(argv[++k]) : std::string(); };
    if (a == "--udp") o.udp = val();
    else if (a == "--glimpse") o.glimpse = val();
    else if (a == "--glimpse-port") o.glimpse_port = uint16_t(std::stoi(val()));
    else if (a == "--scenario") o.scenarios.push_back(val());
    else if (a == "--all")
      o.scenarios = {"baseline", "dup", "rerequest", "snapshot", "reset", "latejoin", "eos"};
    else if (a == "--levels") o.levels = std::stoul(val());
    else if (a == "--sample-every") o.sample_every = std::stoul(val());
    else if (a == "--max-ticks") o.max_ticks = std::stoi(val());
    else if (a == "--no-ob") o.ob = false;
    else if (a == "--no-abort") o.abort_on_cross = false;
    else
    {
      std::cerr << "usage: jnx_replay --udp ITCH.UDP.pcap --glimpse GLIMPSE.pcap "
                   "[--scenario baseline|dup|rerequest|snapshot|reset|latejoin|eos]... [--all] "
                   "[--levels N] [--sample-every N] [--max-ticks N] [--no-ob]\n";
      return 2;
    }
  }
  if (o.udp.empty() || o.glimpse.empty())
  {
    std::cerr << "jnx_replay: --udp and --glimpse are required\n";
    return 2;
  }
  if (o.scenarios.empty())
    o.scenarios = {"baseline"};
  if (o.scenarios.front() != "baseline")
    o.scenarios.insert(o.scenarios.begin(), "baseline");

  // RefData must exist before assets are added; seed it empty, as bnsim does.
  const auto seed = std::filesystem::temp_directory_path() / "jnx_universe_seed.csv";
  std::ofstream(seed) << "# generated by jnx_replay\n";
  frame::ref::RefData::set_universe(seed.string());
  frame::ref::RefData::inst();

  load(o);
  std::printf("capture: %zu UDP packets, sequence %llu..%llu; GLIMPSE stream %zu bytes; "
              "session date %d\n",
              g_pkts.size(), (unsigned long long)g_pkts.front().seq,
              (unsigned long long)(g_pkts.back().seq + (g_pkts.back().count == 0xFFFF ? 0 : g_pkts.back().count)),
              g_glimpse.size(), session_date(g_pkts.front().ts));
  {
    // The instrument loader's choice of Kaspar tick, over the sample directory.
    NullSink none;
    Handler d(config(Handler::Join::FirstPacket), none);
    d.preload_directory(g_glimpse.data(), g_glimpse.size());
    std::map<uint32_t, size_t> units;
    std::map<uint32_t, size_t> tables;
    int max_slots = 0;
    uint64_t slots = 0;
    std::string widest;
    for (const auto &i : d.instruments().all())
    {
      ++units[i->unit];
      ++tables[i->table];
      slots += uint64_t(i->maxpx);
      if (i->maxpx > max_slots)
      {
        max_slots = i->maxpx;
        widest = i->sicc + " (" + std::to_string(i->lower / 10) + "-" +
                 std::to_string(i->upper / 10) + " JPY)";
      }
    }
    std::printf("directory: %zu books; Kaspar tick (venue units):", d.instruments().all().size());
    for (auto [u, k] : units)
      std::printf(" %u:%zu", u, k);
    std::printf("; tables:");
    for (auto [t, k] : tables)
      std::printf(" %u:%zu", t, k);
    std::printf("; OB ladder slots per side: largest %d (%s), mean %.0f\n", max_slots,
                widest.c_str(), double(slots) / double(d.instruments().all().size()));
  }
  for (const auto &s : o.scenarios)
    report(run(s, o));
  return 0;
}
