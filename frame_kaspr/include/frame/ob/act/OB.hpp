#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <vector>
#include <list>
#include <set>
#include <queue>
#include <iostream>
#include <fstream>
#include "actors/Actor.hpp"
#include "actors/act/Manager.hpp"
#include "frame/ob/act/OB_Abstract.hpp"
#include "actors/msg/Shutdown.hpp"
#include "actors/msg/Start.hpp"
#include "chutil/Time.hpp"
#include "frame/mda/msg/Data.hpp"
#include "frame/mda/msg/Subscribe.hpp"
#include <boost/circular_buffer.hpp>
#include "frame/ob/msg/CheckBook.hpp"
#include "frame/ob/msg/CheckSim.hpp"
#include "chutil/places.hpp"
#include "actors/msg/Set.hpp"
#include "zlib.h"
#include "chutil/accumulators.hpp"
#include "chutil/hash_map.hpp"
#include "frame/cons/msg/Get.hpp"
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/info_parser.hpp>
// std::flat_map is C++23 but not yet supported by clang, use std::map as fallback
#if __has_include(<flat_map>) && defined(__cpp_lib_flat_map)
#include <flat_map>
#else
#include <map>
namespace std {
  template<typename K, typename V>
  using flat_map = std::map<K, V>;
}
#endif

using payload_ptr_t = boost::intrusive_ptr<const frame::mda::msg::data_pay_load>;

namespace frame
{
  namespace ob
  {

    class OrderQ;
    class Order;

    namespace act
    {
      class OB : public actors::Actor, public OB_Abstract
      {

        uint num_exec = 0;
        uint num_add = 0;

        // pred

        bool has_pred=false;

        bool is_option = false;

        std::vector<double> latency;

        struct order_info_t
        {
          uint64_t chopid;
          uint64_t xoid;
          double px;
          uint32_t sz;
          en::bs side;
        };

        uint64_t data_handler_enter = 0;
        uint32_t data_handler_cnt_trad = 0;
        uint32_t data_handler_cnt_eob = 0;
        static constexpr uint interval = 4096;
        static constexpr uint interval_short = 256;

        uint32_t bookoid = 0;
        uint32_t prevrecid = 0;

        uint64_t txtim_epoch = 0, sendtim_epoch = 0, handlerendtim = 0;
        uint64_t last_bbo_notify_tim = 0;  // Track last BBO notification time for throttling 


        int32_t volumefound = 0, volumembp = 0, volumeall = 0;

        bool is_in_recovery = false;
        bool have_new_payload = false;


        std::flat_map<uint64_t, order_info_t> ordermap;

        typedef std::vector<OrderQ *> qvec_t;
        qvec_t askqs;
        qvec_t bidqs;

        int best_bid; // best of best bid
        int best_ask; // best of best ask

        bfile::l3_mbo_v2_t current_mbo;

        int incr;
        uint64_t start_debug = 0;
        bool debug = false, print_stats = false;
        int_fast64_t shufflecntr = 0, lowpriodatacnt = 0;
        uint64_t currtim = 0; // not needed?
        uint64_t last_tx_tim =0, last_recovery_tx_tim=0;
        std::list<Actor *> bbbosubs;
        std::vector<Actor *> hiprio_datasubs, lowprio_datasubs;
        //std::list<frame::mda::msg::point::bin_point> points;
        // order book output for debug
        std::ofstream ob_file;
        // en::x venue; // hack -- should be priv access through msg only
        int ex_sym_id = 0;
        uint sym = 0;
        char name[256];
        // ref::Asset* a;
        bool do_cross_check;

        std::ofstream obfile;

        uint64_t prev_xoid; // to keep track of triggers

        //std::vector<pricers::abs_pricer *> pricers;
        //void price(mda::msg::data_pay_load *) noexcept;
        bool has_pricers;
        std::string stats_fname;
        int maxprice;

        std::map<uint64_t, std::queue<payload_ptr_t>> exec_slate;

        // vwap_acc vacc;
        // presh_acc pacc;
        // rng_acc racc;

#ifdef USEPOINTPLACES
        std::array<pint, NLEVELS>
#else
        std::array<int, NLEVELS>
#endif
            bid_px, ask_px, bid_sz, ask_sz, cbid_sz, cask_sz;

#ifdef USEPOINTPLACES
        std::array<pdouble, NLEVELS>
#else
        std::array<double, NLEVELS>
#endif
            ba;

        actor_ptr binrec;
        actors::ActorRef som;
        actors::Manager *man;

        //
        // MBP order book
        //
        std::array<uint32_t, 10> mbp_bid_sz, mbp_ask_sz;
        std::array<int32_t, 10> mbp_bid_px, mbp_ask_px;
        std::array<uint32_t, 10> impl_bid_sz, impl_ask_sz;
        std::array<int32_t, 10> impl_bid_px, impl_ask_px;

        //
        // last good payload
        //
        payload_ptr_t last_good_payload;

        int ex_sym_id_ = -1;

      public:

        inline int get_ex_id(int sym) override
        {
          if CHUNLIKELY (ex_sym_id_ < 0)
          {
            auto a = ref::RefData::get_asset(sym);
            ASSERT(a, "asset not found");
            ex_sym_id_ = a->sec_id;
            return ex_sym_id_;
          }
          else
            return ex_sym_id_;
        };

        inline uint get_sym() const override {
          return sym; }

        inline int get_ex_sym_id() override {
          if CHUNLIKELY (!ex_sym_id)
          {
            ex_sym_id=get_ex_id(sym);
          }
          return ex_sym_id; 
        }

        void set_ex_sym_id(uint id) override {
          ex_sym_id=id;
        }

        // int get_ex_sym_id() {
        //   ASSERT(ex_sym_id, "ex_sym_id not set");
        //   return ex_sym_id; 
        // }

        OB(
            actor_ptr binrec,
            bool do_cross_check,
            actors::Manager *man,
            uint _sym,
            boost::property_tree::ptree &_pt
            );

        ~OB();

        // 0 means no debug
        void
        set_debug(uint64_t _start_debug = 0)
        {
          if (_start_debug)
            debug = true;
          start_debug = _start_debug;
        }

        void set_delay(int _d)
        {
          delay = _d;
        }

        void
        set_print_stats(const std::string &_stats_fname)
        {
          stats_fname = _stats_fname;
          print_stats = true;
        }

        void set_handler(const actors::msg::Set *m)
        {
          SNGH;
          if (m->varname == "set_print_stats")
          {
            set_print_stats(std::any_cast<std::string>(m->value));
          }
          else if (m->varname == "debug")
          {
            debug = true;
          }
          else if (m->varname == "printbook")
          {
            debug_print_book_(5);
          }
          else
            SNGH;
        }

      private:
        int delay = 1000; // micros
        uint32_t exec_orders_not_found = 0;
        uint32_t volume_executed = 0;
        std::list<
            std::tuple<
                boost::intrusive_ptr<const mda::msg::data_pay_load>,
                actors::Actor *>>
            del_q;
        std::vector<boost::intrusive_ptr<const mda::msg::data_pay_load>> crossed_data;
        boost::circular_buffer<boost::intrusive_ptr<const mda::msg::data_pay_load>> recent_dat;
        boost::circular_buffer<boost::intrusive_ptr<const mda::msg::data_pay_load>> recent_tx;

        int theta =0; // the last trade
        int num_trad = 0;
        int last_tx_px = 0;
        boost::circular_buffer<int> tx_px_3;
        boost::circular_buffer<int> tx_px_5;
        boost::circular_buffer<int> tx_px_10;
        boost::circular_buffer<int> tx_px_15;
        boost::circular_buffer<int> tx_px_20;
        boost::circular_buffer<int> tx_px_25;
        boost::circular_buffer<int> tx_px_30;
        boost::circular_buffer<int> tx_px_50;
        boost::circular_buffer<int> tx_px_100;
        boost::circular_buffer<int> tx_px_200;
        boost::circular_buffer<int> tx_px_500;

        boost::circular_buffer<int> tx_sz_3;
        boost::circular_buffer<int> tx_sz_5;
        boost::circular_buffer<int> tx_sz_10;
        boost::circular_buffer<int> tx_sz_15;
        boost::circular_buffer<int> tx_sz_20;
        boost::circular_buffer<int> tx_sz_25;
        boost::circular_buffer<int> tx_sz_30;
        boost::circular_buffer<int> tx_sz_50;
        boost::circular_buffer<int> tx_sz_100;
        boost::circular_buffer<int> tx_sz_200;
        boost::circular_buffer<int> tx_sz_500;

        void clear();
        void add(
            uint64_t tx_time,
            unsigned long long id,
            en::bs side, int px, int sz,
            actors::Actor *sender, en::ot ot,
            int owner, uint64_t exordid,
            en::x venue
            ) noexcept;
        void mod(
            actors::Actor *sender,
            uint64_t tx_time,
            unsigned long long id,
            en::mt modtyp,
            en::bs side,
            int px,
            int sz,
            int disp_sz,
            en::x mkt,
            uint64_t exordid) noexcept;
        void do_canc(frame::ob::OrderQ *q,
                     frame::ob::Order *o,
                     int32_t px,
                     int32_t sz,
                     int32_t dispsz,
                     en::mt modtyp,
                     en::x mkt) noexcept;

        void fill(OrderQ *q, Order *s, int sz, int px, uint64_t) noexcept;
        void notifybbbosubs(
                            uint64_t tx_time,
                            en::bs side, 
                            en::x venue);
        void process_market_data(boost::intrusive_ptr<const mda::msg::data_pay_load>, actors::Actor *);
        void process_message(const actors::Message*) noexcept override;
        void process_add_or_mod(boost::intrusive_ptr<const mda::msg::data_pay_load>, actors::Actor *) noexcept;
        void debug_print_book(unsigned long long id, uint64_t tim, char msg, en::mt typ,
                              en::bs side, int px, int sz);
        void process_q(const mda::msg::data_pay_load *);
        void debug_print_book_(int nlevels);
        void debug_print_book_mbp();
        void end() override;
        void check_bbbo();
        void cross_check(boost::intrusive_ptr<const mda::msg::data_pay_load>);
        std::size_t index_of_darr(
            const double[], std::size_t,
            double) const noexcept;

        void index_of_darr_test() const;

        void check_handler(const frame::ob::msg::CheckBook *) noexcept;
        void check_sim_handler(const frame::ob::msg::CheckSim *) noexcept;
        void data_handler(const frame::mda::msg::Data *) noexcept;
        void start_handler(const actors::msg::Start *) noexcept;
        void shutdown_handler(const actors::msg::Shutdown *) noexcept;
        void get_handler(const frame::cons::msg::Get *) noexcept;

      public:
        const char* get_name() const override { return name; }
      };
    }
  }
}
