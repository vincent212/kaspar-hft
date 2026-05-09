#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <vector>
#include <map>
#include <set>
#include <cstring>
#include <boost/algorithm/string.hpp>

#include "actors/Actor.hpp"
#include "chutil/Table.hpp"
#include "logger/act/Logger.hpp"
#include "frame/ref/RefData.hpp"

#include "frame/som/msg/Fill.hpp"
#include "frame/cons/msg/Get.hpp"
#include "frame/cons/msg/Page.hpp"
#include "frame/som/msg/Ack.hpp"
#include "frame/som/msg/Cancel.hpp"
#include "frame/som/msg/CancAck.hpp"
#include "frame/som/msg/Reject.hpp"
#include "frame/som/msg/Order.hpp"
#include "frame/som/msg/FillSub.hpp"
#include "frame/ob/msg/BBBOSub.hpp"
#include "frame/ob/msg/BBBOChg.hpp"
#include "actors/msg/Start.hpp"
#include "frame/som/msg/Start.hpp"
#include "frame/som/msg/Stop.hpp"
#include "chutil/price_convert.hpp"

namespace frame::mtd::act
{

  class MTD : public actors::Actor
  {
    std::vector<som::msg::Fill> fills;
    std::map<uint, som::msg::Order> orders;
    std::set<uint> cancack;
    std::set<uint> cancels;
    std::set<uint> rejects;
    std::set<uint> fillbyid;
    std::map<en::x, actor_ptr> som;
    std::vector<std::vector<actor_ptr>> obs;
    std::vector<std::vector<ob::msg::BBBOChg>> bbbo;
    char name[256];

  public:
    const char* get_name() const { return name; }
    MTD(std::string _name,
        const std::map<en::x, actor_ptr> &_som,
        const std::vector<std::vector<actor_ptr>> &_obs)
        :
          som(_som),
          obs(_obs)
    {
      strncpy(name, _name.c_str(), sizeof(name) - 1);
      name[sizeof(name) - 1] = '\0';
      bbbo.resize(en::x_num_syms());
      auto n = ref::RefData::inst().num_assets();
      for (std::size_t i = 0; i < en::x_num_syms(); i++)
      {
        bbbo[i].resize(n);
        for (auto &bbbochg : bbbo[i])
        {
          bbbochg.venue = en::x::UNI;
          bbbochg.best_bid = 0;
          bbbochg.best_ask = 0;
          bbbochg.sym = 0;
          bbbochg.side = static_cast<en::bs>(0);
          bbbochg.tx_time = 0;
        }
      }

      MESSAGE_HANDLER(ob::msg::BBBOChg, bbbochg_handler);
      MESSAGE_HANDLER(actors::msg::Start, start_handler);
      MESSAGE_HANDLER(cons::msg::Get, get_handler);
      MESSAGE_HANDLER(som::msg::Fill, fill_handler);
      MESSAGE_HANDLER(som::msg::CancAck, cancack_handler);
      MESSAGE_HANDLER(som::msg::Reject, reject_handler);
    }

    void start_handler(const actors::msg::Start *) noexcept
    {
      for (const auto &_b : obs)
      {
        for (auto ob : _b)
        {
          if (ob)
          {
            log_inf("sending subscribe to ob %s", ob->get_name());
            ob->send(new ob::msg::BBBOSub(), this);
          }
        }
      }
      for (auto [venue, s] : som)
      {
        if (!s)
        {
          log_err("no som for venue %s", en::to_string(venue));
          continue;
        }
        log_inf("sending fill sub to som %s", s->get_name());
        s->send(new som::msg::FillSub(), this);
      }
    }

    void bbbochg_handler(const frame::ob::msg::BBBOChg *m) noexcept
    {
      bbbo[m->venue][m->sym] = *m;
    }

    void fill_handler(const som::msg::Fill *m) noexcept
    {
      log_fil("got partial fill from som id: %d, sz: %d, sym: %d, side: %s, stbf: %d, px: %d",
              m->id, m->sz, m->sym, en::to_string(m->side), m->still_to_be_filled, m->px.to_int());
      fills.push_back(*m);
      fillbyid.insert(m->id);
    }

    void cancack_handler(const som::msg::CancAck *m) noexcept
    {
      log_opr("got canc ack for id: %d", m->id);
      cancack.insert(m->id);
    }

    void reject_handler(const som::msg::Reject *m) noexcept
    {
      log_rej("got reject for id: %d", m->id);
      rejects.insert(m->id);
    }

    void get_handler(const cons::msg::Get *m) noexcept
    {
        if (m->what == "pos")
        {
          std::string fname;
          try
          {
            fname = m->kv.at("fname");
          }
          catch (std::out_of_range &e)
          {
            log_err("fname not found");
            reply(new frame::cons::msg::Page("fname not found"));
            return;
          }

          std::ifstream file(fname);
          if (!file)
          {
            log_err("File does not exist");
            reply(new frame::cons::msg::Page("File does not exist"));
            return;
          }

          chutil::CSVFileReader reader;
          auto lines = reader.readFile2(fname, ",");
          std::vector<std::string> labels = {"sym", "pos"};
          chutil::Table t("POS", labels);
          for (auto &line : lines)
          {
            t.add_row();
            t.set_value(line[0]);
            t.set_value(line[1]);
          }
          reply(new frame::cons::msg::Page(t.to_string()));
        }
        else if (m->what == "ping")
        {
          reply(new cons::msg::Page("OK\n"));
          return;
        }
        else if (m->what == "prices")
        {
          std::vector<std::string> labels{
              "sym", "bid", "ask"};
          chutil::Table t("MD", labels);

          for (std::size_t j = 1; j < ref::RefData::inst().num_assets(); j++)
          {
            auto a = ref::RefData::inst().get_asset(j);
            if (!a || !a->is_exchange_md_set())
              continue;
            auto venue = a->get_exchange_md();
            const auto &bid = ref::Price(bbbo[venue][j].best_bid, a->id);
            const auto &ask = ref::Price(bbbo[venue][j].best_ask, a->id);
            t.add_row();
            t.set_value(a->name);
            t.set_value(bid.to_int());
            t.set_value(ask.to_int());
          }

          reply(new cons::msg::Page(t.to_string()));
          return;
        }
        else if (m->what == "bbbo")
        {
          std::vector<std::string> labels{
              "sym", "bid32", "bid", "ask", "ask32"};
          chutil::Table t("BBBO", labels);

          for (std::size_t j = 1; j < ref::RefData::inst().num_assets(); j++)
          {
            auto a = ref::RefData::inst().get_asset(j);
            if (!a || !a->is_exchange_md_set())
                continue;
            auto venue = a->get_exchange_md();
            const auto &bid = ref::Price(bbbo[venue][j].best_bid, a->id);
            const auto &ask = ref::Price(bbbo[venue][j].best_ask, a->id);
            t.add_row();
            t.set_value(a->name);
            t.set_value(chutil::convert_price_to_32nds(bid.to_double1()));
            t.set_value(bid.to_int());
            t.set_value(ask.to_int());
            t.set_value(chutil::convert_price_to_32nds(ask.to_double1()));
          }
          reply(new cons::msg::Page(t.to_string()));
          return;
        }
        else if (m->what == "stopom")
        {
          for (auto &[venue, s] : som)
          {
            if (s)
              s->send(new som::msg::Stop(), 0);
          }
          reply(new cons::msg::Page("sent stop request to som\n"));
        }
        else if (m->what == "startom")
        {
          for (auto &[venue, s] : som)
          {
            if (s)
              s->send(new som::msg::Start(), 0);
          }
          reply(new cons::msg::Page("sent start request to som\n"));
        }
        else if (m->what == "assets")
        {
          chutil::Table t("Assets", {"id", "name", "mnem", "units", "sec_id", "exch", "maxpx", "has_book"});
          const auto &assets = ref::RefData::inst().getAssets();
          for(auto a: assets)
          {
            if (!a)
              continue;
            t.add_row();
            t.set_value(a->id);
            t.set_value(a->name);
            t.set_value(a->mnemonic);
            t.set_value(a->get_units());
            t.set_value(a->sec_id);
            t.set_value(a->exchange_md_str);
            t.set_value(a->maxpx);
            t.set_value(a->has_book);
          }
          reply(new cons::msg::Page(t.to_string()));
          return;
        }
        else if (m->what == "order")
        {
          auto sym_it = m->kv.find("sym");
          auto sz_it = m->kv.find("sz");
          auto bs_it = m->kv.find("bs");
          auto px_it = m->kv.find("px");
          auto x_it = m->kv.find("x");
          auto vis_it = m->kv.find("vis");

          uint8_t visibility_group = 0;
          if (vis_it != m->kv.end())
          {
            try
            {
              visibility_group = chutil::to_uint(vis_it->second.c_str());
            }
            catch (...)
            {
              reply(new cons::msg::Page("could not convert visibility group"));
              return;
            }
          }

          if (sym_it == m->kv.end())
          {
            reply(new cons::msg::Page("key sym not found"));
            return;
          }
          else if (sz_it == m->kv.end())
          {
              reply(new cons::msg::Page("key sz not found"));
              return;
          }
          else if (bs_it == m->kv.end())
          {
            reply(new cons::msg::Page("key bs not found"));
            return;
          }
          else if (px_it == m->kv.end())
          {
            reply(new cons::msg::Page("key px not found"));
            return;
          }
          else if (x_it == m->kv.end())
          {
            reply(new cons::msg::Page("key x not found"));
            return;
          }
          auto sym = sym_it->second;

          auto a = ref::RefData::get_asset(sym);
          if (!a)
          {
            reply(new cons::msg::Page((boost::format("symbol %s not found") %
                                       sym_it->second)
                                          .str()));
            return;
          }

          double sz;
          try
          {
            sz = chutil::to_double(sz_it->second.c_str());
          }
          catch (...)
          {
            reply(new cons::msg::Page("could not convert size"));
            return;
          }
          int px;
          try
          {
            px = chutil::to_int(px_it->second.c_str());
          }
          catch (...)
          {
            reply(new cons::msg::Page("could not convert px"));
            return;
          }
          en::bs bs;
          if (!en::bs_is_valid(bs_it->second.c_str()))
          {
            reply(new cons::msg::Page("could not convert bs string"));
            return;
          }
          bs = en::bs_index_of(bs_it->second);

          en::x venue;
          if (!en::x_is_valid(x_it->second.c_str()))
          {
            reply(new cons::msg::Page("could not parse exchange code string"));
            return;
          }
          venue = en::x_index_of(x_it->second);

          auto m = new som::msg::Order(
            "RFQ",
              venue,
              a->id,
              sz, px, bs,
              en::trader::BYHAND,
              visibility_group);

          auto future = som.at(venue)->fast_send(m, this);
          auto rep = future.get();
          auto ack = dynamic_cast<const som::msg::Ack *>(rep);
          auto id = ack->id;
          orders[id] = *m;
          reply(new cons::msg::Page((boost::format("got order id: %d\n") % id).str()));
        }
        else if (m->what == "cancel")
        {
          auto id_it = m->kv.find("id");
          if (id_it == m->kv.end())
          {
            reply(new cons::msg::Page("key id not found"));
            return;
          }
          uint id;
          try
          {
            id = chutil::to_uint(id_it->second.c_str());
          }
          catch (...)
          {
            reply(new cons::msg::Page("could not convert id"));
            return;
          }
          auto x_it = m->kv.find("x");
          if (x_it == m->kv.end())
          {
            reply(new cons::msg::Page("key x not found"));
            return;
          }
          en::x venue;
          if (!en::x_is_valid(x_it->second.c_str()))
          {
            reply(new cons::msg::Page("could not parse exchange code string"));
            return;
          }
          venue = en::x_index_of(x_it->second);
          som.at(venue)->send(new som::msg::Cancel(id), this);
          cancels.insert(id);
          reply(new cons::msg::Page("cancel sent\n"));
        }
        else if (m->what == "fills")
        {
          std::vector<std::string> labels;
          labels.push_back("ID");
          labels.push_back("venue");
          labels.push_back("sym");
          labels.push_back("side");
          labels.push_back("sz");
          labels.push_back("stbf");
          labels.push_back("px");
          chutil::Table t("Fills", labels);
          for (const auto &f : fills)
          {
            auto a = ref::RefData::inst().asset(f.sym);
            t.add_row();
            t.set_value(f.id);
            t.set_value(std::string(en::to_string(f.venue)));
            t.set_value(a->name);
            t.set_value(std::string(en::to_string(f.side)));
            t.set_value(f.sz);
            t.set_value(f.still_to_be_filled);
            t.set_value(f.px.to_double1());
          }
          reply(new cons::msg::Page(t.to_string()));
        }
        else if (m->what == "get_orders")
        {
          std::vector<std::string> labels;
          labels.push_back("ID");
          labels.push_back("venue");
          labels.push_back("sym");
          labels.push_back("side");
          labels.push_back("sz");
          labels.push_back("stbf");
          labels.push_back("px");
          labels.push_back("fills");
          labels.push_back("canc");
          labels.push_back("cancack");
          labels.push_back("reject");
          chutil::Table t("Orders", labels);
          for (auto f : orders)
          {
            auto a = ref::RefData::inst().asset(f.second.sym);
            t.add_row();
            t.set_value(f.first);
            t.set_value(std::string(en::to_string(f.second.venue)));
            t.set_value(a->name);
            t.set_value(std::string(en::to_string(f.second.side)));
            t.set_value(f.second.sz);
            t.set_value(f.second.still_to_be_filled);
            t.set_value(f.second.px);
            auto p1 = fillbyid.find(f.first);
            if (p1 == fillbyid.end())
              t.set_value("N");
            else
              t.set_value("Y");
            auto p2 = cancels.find(f.first);
            if (p2 == cancels.end())
              t.set_value("N");
            else
              t.set_value("Y");
            auto p3 = cancack.find(f.first);
            if (p3 == cancack.end())
              t.set_value("N");
            else
              t.set_value("Y");
            auto p4 = rejects.find(f.first);
            if (p4 == rejects.end())
              t.set_value("N");
            else
              t.set_value("Y");
          }
          reply(new cons::msg::Page(t.to_string()));
        }
    }
  };
}
