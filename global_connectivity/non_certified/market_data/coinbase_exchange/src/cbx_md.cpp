/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

// cbx_md: Coinbase Exchange market data from the command line.
//
//   cbx_md dump   --product BTC-USD [--mode l2|full|level3] [--env production|sandbox]
//                 [--key-file F] [--record DIR] [--seconds N]
//       The production feed handler without OB, printing its book once a
//       second (the --dump monitor). The L3 modes need credentials.
//
//   cbx_md live   --product BTC-USD,ETH-USD --minutes 35 --record DIR [--no-ob]
//                 [--l2-period 10] [--l3-period 60]
//       The public live check (testing ladder step 2): level2_batch, matches
//       and heartbeat, with REST level-2 books every --l2-period seconds and
//       REST level-3 books every --l3-period seconds per product, compared as
//       src/Verifier.hpp describes. Prints a JSON report per product.
//
//   cbx_md check-l3 --product BTC-USD [--mode full|level3] [--env sandbox] [--key-file F]
//                 [--minutes 30] [--l3-period 60] [--record DIR]
//       The exact L3 check (testing ladder step 3, needs the key file): the
//       signed `full` (or `level3`) stream, and every --l3-period seconds a
//       REST level-3 book compared order by order with the stream's book at
//       the same sequence (src/Verifier.hpp, L3Checker).
//
//   cbx_md replay --dir DIR --product BTC-USD [--no-ob]
//       The same check on a recording (ws.txt, rest.txt).
//
// Product increments come from config/products.ini; live and dump also check
// them against GET /products/<id>.

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include "actors/act/Manager.hpp"
#include "coinbase_exchange/act/CoinbaseExchangeFeed.hpp"
#include "enum/e_names.hpp"
#include "Verifier.hpp"

using namespace coinbase_exchange;

namespace
{
  struct Args
  {
    std::string cmd;
    std::map<std::string, std::string> kv;
    std::string get(const std::string &k, const std::string &d = "") const
    {
      auto it = kv.find(k);
      return it == kv.end() ? d : it->second;
    }
    bool has(const std::string &k) const { return kv.count(k) > 0; }
  };

  Args parse_args(int argc, char **argv)
  {
    Args a;
    if (argc > 1)
      a.cmd = argv[1];
    for (int i = 2; i < argc; ++i)
    {
      std::string k = argv[i];
      if (k.rfind("--", 0) != 0)
        continue;
      k = k.substr(2);
      if (i + 1 < argc && std::string(argv[i + 1]).rfind("--", 0) != 0)
        a.kv[k] = argv[++i];
      else
        a.kv[k] = "1";
    }
    return a;
  }

  std::vector<std::string> split(const std::string &s, char c)
  {
    std::vector<std::string> v;
    std::stringstream ss(s);
    std::string x;
    while (std::getline(ss, x, c))
      if (!x.empty())
        v.push_back(x);
    return v;
  }

  std::string here_config()
  {
    const char *p = std::getenv("KSPRPROJ");
    std::string root = p ? p : ".";
    return root + "/global_connectivity/non_certified/market_data/coinbase_exchange/config/products.ini";
  }

  struct ProductCfg
  {
    ProductSpec spec;
    double chunk = 0;
  };

  bool load_product(const std::string &ini, const std::string &product, ProductCfg &out, std::string &err)
  {
    boost::property_tree::ptree pt;
    try
    {
      boost::property_tree::read_ini(ini, pt);
    }
    catch (const std::exception &e)
    {
      err = e.what();
      return false;
    }
    auto sec = pt.get_child_optional(product);
    if (!sec)
    {
      err = product + " is not in " + ini;
      return false;
    }
    auto s = ProductSpec::make(product, sec->get<int32_t>("sec_id"), char(en::x::COINBASEX),
                               sec->get<std::string>("quote_increment"), sec->get<std::string>("base_increment"),
                               sec->get<std::string>("qty_unit"), err);
    if (!s)
      return false;
    out.spec = *s;
    out.chunk = sec->get<double>("chunk", 0.0001);
    return true;
  }

  // The configured increments must be the venue's.
  bool check_product(const ProductCfg &p, const std::string &rest_host)
  {
    try
    {
      auto j = nlohmann::json::parse(https_get(rest_host, "/products/" + p.spec.product));
      auto q = j.value("quote_increment", std::string());
      auto b = j.value("base_increment", std::string());
      if (decimals_of(q) != p.spec.price_decimals || decimals_of(b) != p.spec.size_decimals ||
          scaled(q, p.spec.price_decimals).value_or(-1) != p.spec.price_increment)
      {
        std::cerr << p.spec.product << ": products.ini increments differ from the venue's (" << q << ", " << b
                  << ")" << std::endl;
        return false;
      }
      std::cerr << p.spec.product << ": status " << j.value("status", std::string()) << ", quote_increment " << q
                << ", base_increment " << b << std::endl;
      return true;
    }
    catch (const std::exception &e)
    {
      std::cerr << p.spec.product << ": cannot read the product: " << e.what() << std::endl;
      return false;
    }
  }

  struct ToolManager : public actors::Manager
  {
    ToolManager() : Manager("cbx_md") {}
    void add(actors::Actor *a) { add_to_manage_q(a); }
  };

  int run_feeds(std::vector<CoinbaseExchangeFeed *> feeds, std::vector<verify::Verifier *> verifiers)
  {
    ToolManager mgr;
    for (auto *f : feeds)
      mgr.add(f);
    mgr.init();
    for (;;)
    {
      bool all = true;
      for (auto *f : feeds)
        all = all && f->finished();
      if (all)
        break;
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    for (auto *v : verifiers)
    {
      v->finish();
      std::cout << v->report().dump(2) << std::endl;
    }
    std::cout.flush();
    std::_Exit(0);
  }

  int cmd_dump(const Args &a)
  {
    ProductCfg p;
    std::string err;
    if (!load_product(a.get("config", here_config()), a.get("product", "BTC-USD"), p, err))
    {
      std::cerr << err << std::endl;
      return 1;
    }
    CoinbaseExchangeFeed::Config c;
    c.product = p.spec.product;
    auto mode = a.get("mode", "l2");
    c.mode = mode == "full" ? CoinbaseExchangeFeed::Mode::L3Full
             : mode == "level3" ? CoinbaseExchangeFeed::Mode::L3Level3
                                : CoinbaseExchangeFeed::Mode::L2Public;
    auto env = parse_environment(a.get("env", "production"));
    if (!env)
    {
      std::cerr << "--env must be production or sandbox" << std::endl;
      return 1;
    }
    c.environment = *env;
    c.key_file = a.get("key-file", c.mode == CoinbaseExchangeFeed::Mode::L2Public ? "" : default_key_path());
    c.record_dir = a.get("record", "");
    c.dump = true;
    c.run_seconds = std::atoi(a.get("seconds", "0").c_str());
    if (!check_product(p, endpoints(c.environment).rest_host))
      return 1;
    std::unique_ptr<FeedHandler> h;
    if (c.mode == CoinbaseExchangeFeed::Mode::L2Public)
      h = make_l2_handler(p.spec, p.chunk, 1000, [](const bfile::l3_t &) {});
    else
      h = make_l3_handler(CoinbaseExchangeSync::Config{p.spec}, [](const bfile::l3_t &) {});
    auto feed = new CoinbaseExchangeFeed(c, std::move(h));
    if (!feed->check(err))
    {
      std::cerr << "cbx_md: " << err << std::endl;
      return 1;
    }
    return run_feeds({feed}, {});
  }

  int cmd_live(const Args &a)
  {
    std::vector<CoinbaseExchangeFeed *> feeds;
    std::vector<verify::Verifier *> verifiers;
    const int minutes = std::atoi(a.get("minutes", "30").c_str());
    const int l2p = std::atoi(a.get("l2-period", "10").c_str());
    const int l3p = std::atoi(a.get("l3-period", "60").c_str());
    const std::string rec = a.get("record", "");
    for (const auto &prod : split(a.get("product", "BTC-USD"), ','))
    {
      ProductCfg p;
      std::string err;
      if (!load_product(a.get("config", here_config()), prod, p, err))
      {
        std::cerr << err << std::endl;
        return 1;
      }
      if (!check_product(p, endpoints(Environment::Production).rest_host))
        return 1;
      auto v = new verify::Verifier(p.spec, p.chunk, !a.has("no-ob"));
      CoinbaseExchangeFeed::Config c;
      c.product = prod;
      c.mode = CoinbaseExchangeFeed::Mode::L2Public;
      c.environment = Environment::Production;
      c.periodic_rest = {{2, l2p}, {3, l3p}};
      c.run_seconds = minutes * 60;
      c.dump = true;
      if (!rec.empty())
      {
        c.record_dir = rec + "/" + prod;
        std::system(("mkdir -p '" + c.record_dir + "'").c_str());
      }
      // the verifier is owned by the feed; keep a pointer for the report
      struct Borrowed : public FeedHandler
      {
        verify::Verifier *v;
        explicit Borrowed(verify::Verifier *x) : v(x) {}
        void on_frame(const nlohmann::json &m, uint64_t t) override { v->on_frame(m, t); }
        void on_rest(int l, const nlohmann::json &b, uint64_t s, uint64_t r) override { v->on_rest(l, b, s, r); }
        void on_disconnect(uint64_t t) override { v->on_disconnect(t); }
        bool need_snapshot() const override { return false; }
        std::string status_line() const override { return v->status_line(); }
      };
      feeds.push_back(new CoinbaseExchangeFeed(c, std::make_unique<Borrowed>(v)));
      verifiers.push_back(v);
    }
    return run_feeds(feeds, verifiers);
  }

  int cmd_check_l3(const Args &a)
  {
    ProductCfg p;
    std::string err;
    if (!load_product(a.get("config", here_config()), a.get("product", "BTC-USD"), p, err))
    {
      std::cerr << err << std::endl;
      return 1;
    }
    CoinbaseExchangeFeed::Config c;
    c.product = p.spec.product;
    c.mode = a.get("mode", "full") == "level3" ? CoinbaseExchangeFeed::Mode::L3Level3 : CoinbaseExchangeFeed::Mode::L3Full;
    auto env = parse_environment(a.get("env", "sandbox"));
    if (!env)
    {
      std::cerr << "--env must be sandbox or production" << std::endl;
      return 1;
    }
    c.environment = *env;
    c.key_file = a.get("key-file", default_key_path());
    c.record_dir = a.get("record", "");
    if (!c.record_dir.empty())
      std::system(("mkdir -p '" + c.record_dir + "'").c_str());
    c.dump = true;
    c.run_seconds = std::atoi(a.get("minutes", "30").c_str()) * 60;
    c.periodic_rest = {{3, std::max(60, std::atoi(a.get("l3-period", "60").c_str()))}};
    if (!check_product(p, endpoints(c.environment).rest_host))
      return 1;
    auto checker = new verify::L3Checker(p.spec);
    struct Borrowed : public FeedHandler
    {
      verify::L3Checker *v;
      explicit Borrowed(verify::L3Checker *x) : v(x) {}
      void on_frame(const nlohmann::json &m, uint64_t t) override { v->on_frame(m, t); }
      void on_rest(int l, const nlohmann::json &b, uint64_t s, uint64_t r) override { v->on_rest(l, b, s, r); }
      void on_disconnect(uint64_t t) override { v->on_disconnect(t); }
      bool need_snapshot() const override { return v->need_snapshot(); }
      std::string status_line() const override { return v->status_line(); }
    };
    auto feed = new CoinbaseExchangeFeed(c, std::make_unique<Borrowed>(checker));
    if (!feed->check(err))
    {
      std::cerr << "cbx_md: " << err << std::endl;
      return 1;
    }
    ToolManager mgr;
    mgr.add(feed);
    mgr.init();
    while (!feed->finished())
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    std::cout << checker->report().dump(2) << std::endl;
    std::cout.flush();
    std::_Exit(0);
  }

  // Replay a recording: ws.txt "<recv_ns>\t<frame>" and rest.txt
  // "<sent_ns>\t<recv_ns>\t<product>\t<level>\t<body>", merged by receive time.
  int cmd_replay(const Args &a)
  {
    ProductCfg p;
    std::string err;
    const std::string prod = a.get("product", "BTC-USD");
    if (!load_product(a.get("config", here_config()), prod, p, err))
    {
      std::cerr << err << std::endl;
      return 1;
    }
    const std::string dir = a.get("dir", ".");
    std::ifstream ws(dir + "/ws.txt"), rest(dir + "/rest.txt");
    if (!ws)
    {
      std::cerr << "no " << dir << "/ws.txt" << std::endl;
      return 1;
    }
    verify::Verifier v(p.spec, p.chunk, !a.has("no-ob"));
    std::string wl, rl;
    uint64_t wt = 0, rt = 0;
    bool have_w = false, have_r = false;
    auto next_w = [&]() {
      have_w = false;
      while (std::getline(ws, wl))
      {
        auto tab = wl.find('\t');
        if (tab == std::string::npos)
          continue;
        wt = std::stoull(wl.substr(0, tab));
        wl = wl.substr(tab + 1);
        have_w = true;
        return;
      }
    };
    struct R
    {
      uint64_t sent, recv;
      std::string product;
      int level;
      std::string body;
    } r;
    auto next_r = [&]() {
      have_r = false;
      while (rest && std::getline(rest, rl))
      {
        std::vector<std::string> f;
        size_t pos = 0;
        for (int i = 0; i < 4; ++i)
        {
          auto tab = rl.find('\t', pos);
          if (tab == std::string::npos)
            break;
          f.push_back(rl.substr(pos, tab - pos));
          pos = tab + 1;
        }
        if (f.size() < 4 || f[2] != prod)
          continue;
        r = {std::stoull(f[0]), std::stoull(f[1]), f[2], std::stoi(f[3]), rl.substr(pos)};
        if (r.body.rfind("#", 0) == 0)
          continue;
        rt = r.recv;
        have_r = true;
        return;
      }
    };
    next_w();
    next_r();
    uint64_t frames = 0, rests = 0, last_print = 0;
    while (have_w || have_r)
    {
      if (have_w && (!have_r || wt <= rt))
      {
        if (wl.rfind("#", 0) == 0)
        {
          if (wl.rfind("#close", 0) == 0 || wl.rfind("#open", 0) == 0)
            v.on_disconnect(wt);
        }
        else
        {
          try
          {
            v.on_frame(nlohmann::json::parse(wl), wt);
          }
          catch (const std::exception &e)
          {
            std::cerr << "bad frame: " << e.what() << std::endl;
          }
          ++frames;
        }
        next_w();
      }
      else
      {
        try
        {
          v.on_rest(r.level, nlohmann::json::parse(r.body), r.sent, r.recv);
          ++rests;
        }
        catch (const std::exception &e)
        {
          std::cerr << "bad REST body: " << e.what() << std::endl;
        }
        next_r();
      }
      if (frames - last_print >= 200000)
      {
        last_print = frames;
        std::cerr << "frames " << frames << " rest " << rests << " | " << v.status_line() << std::endl;
      }
    }
    v.finish();
    std::cout << v.report().dump(2) << std::endl;
    return 0;
  }
}

int main(int argc, char **argv)
{
  Args a = parse_args(argc, argv);
  if (a.cmd == "dump")
    return cmd_dump(a);
  if (a.cmd == "live")
    return cmd_live(a);
  if (a.cmd == "replay")
    return cmd_replay(a);
  if (a.cmd == "check-l3")
    return cmd_check_l3(a);
  std::cerr << "usage: cbx_md dump|live|replay [options] (see the comment at the top of src/cbx_md.cpp)" << std::endl;
  return 2;
}
