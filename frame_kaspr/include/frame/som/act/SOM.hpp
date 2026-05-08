#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <map>
#include <list>
#include <unordered_map>
#include <vector>
#include <memory>
#include "actors/Actor.hpp"
#include "actors/msg/Start.hpp"
#include "actors/msg/Shutdown.hpp"
#include "frame/pos/Position.hpp"
#include "frame/som/msg/Order.hpp"
#include "frame/som/msg/Cancel.hpp"
#include "frame/som/msg/Ack.hpp"
#include "frame/som/msg/Start.hpp"
#include "frame/som/msg/Stop.hpp"
#include "frame/som/msg/CancAck.hpp"
#include "frame/som/msg/CancReject.hpp"
#include "frame/som/msg/Fill.hpp"
#include "frame/som/msg/FillSub.hpp"
#include "frame/som/msg/Reject.hpp"
#include "frame/som/msg/GetPNL.hpp"
#include "frame/som/msg/UnStash.hpp"
#include "frame/som/msg/ExitPos.hpp"
#include "frame/som/msg/RiskInfo.hpp"
#include "frame/ob/msg/BBBOChg.hpp"
#include "frame/som/msg/SetExchManager.hpp"
#include "frame/cons/msg/Get.hpp"
#include "frame/cons/msg/Page.hpp"
#include "frame/som/msg/PositionResponse.hpp"
#include "boost/circular_buffer.hpp"
#include "chutil/hash_set.hpp"
#include "chutil/hash_map.hpp"
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/info_parser.hpp>
#include <boost/unordered/unordered_flat_map.hpp>

namespace frame::som::act
{

  class SOM : public actors::Actor
  {

    int64_t diff_sum = 0;
    int64_t diff_cnt = 0;

  private:

    bool allow_unacked_cancels = true;

    char name[256];
    boost::unordered_flat_map<uint, msg::Order> orders; //
    std::list<actors::Actor *> subs;
    uint curr_id;
    actor_ptr db = 0;
    uint64_t currtim;
    uint64_t currts = 0; //ack_delay = 0;
    std::map<int,uint64_t> add_ts;

    // using this for simulation (but this must be set in live mode as well)
    std::vector<std::vector<actor_ptr>> order_books; // order books

    // using this for real trading
    std::vector<actor_ptr> order_managers;

    // need these for PNL and position
    en::x mmVenue;
    std::vector<int> best_bid;
    std::vector<int> best_ask;
    bool sim_mode;

    std::vector<int> pos_limit, size_limit;

    std::vector<int>
        fill_volume,
        order_volume,
        num_canc,
        num_ords,
        num_cr,
        num_fills,
        num_icnrej,
        num_xorej;
    int num_ican, num_xcnrej;


    // was used for stashing
    std::map<int, msg::Order> buy_orders_q, sel_orders_q;
    std::map<int, msg::Cancel> not_found_cancel_q, buy_cancel_q, sel_cancel_q;

    boost::property_tree::ptree pt;
    //double maxloss = 0;

    std::vector<std::vector<pos::Position>> pos; // indexed by owner and sym

  public:
    SOM(
        en::x venue,
        actor_ptr _db,
        const boost::property_tree::ptree &pt,
        const std::vector<std::vector<actor_ptr>> &_order_books,
        bool _sim_mode = true,
        bool _spin = false,
        bool _reset_positions = false,
        actor_ptr _fill_subscriber = nullptr  // Local fill subscriber (like DB)
        );

    // Add local subscriber directly (no message passing)
    void add_fill_subscriber(actor_ptr subscriber);

  private:
    std::tuple<
        std::vector<int>,
        std::vector<int>,
        std::vector<int>>
    get_pnl(int owner) const noexcept;

    std::tuple<int,int,int>
    get_pnl_all(int sym) const noexcept;

    std::tuple<int,int,int>
    get_pnl_all_all() const noexcept;

    std::string
    get_ord_string_assets() const noexcept;

    std::string
    get_pnl_string_assets(en::trader) const noexcept;

    std::string
    get_vol_string_assets() const noexcept;

    std::string
    get_pnl_string(en::trader) const noexcept;

    std::string
    get_long_pnl_string(en::trader) const noexcept;

    std::string
    get_pos_string(en::trader) const noexcept;

    std::string
    get_status(en::trader) const noexcept;

    int get_pos(int sym, en::trader trader) const noexcept;

    uint32_t get_working_orders_sz(int sym, en::bs side, en::trader trader) const noexcept;

    void cancel_all_orders() noexcept;

    int tot_pos(en::trader) const noexcept;
    double tot_pnl(en::trader) const noexcept;

    bool stopped;
    bool stashing;
    bool reset_positions;  // Flag to reset positions on startup (for forward testing)

    void stash(const msg::Cancel *) noexcept;
    void stash(const msg::Order *) noexcept;
    void unstash(
        std::map<int, frame::som::msg::Order> &orders_q,
        std::map<int, frame::som::msg::Cancel> &cancels_q) noexcept;

    void read_limits();

  public:
    virtual const char* get_name() const { return name; }

  protected:
    void canc_handler(const msg::Cancel *) noexcept;
    bool internal_reject(int id, const msg::Order *&) noexcept;
    void cancel_order(uint id, const msg::Order &ord) noexcept;
    void order_handler(const msg::Order *) noexcept;
    void canc_ack_handler(const msg::CancAck *) noexcept;
    void bbbochg_handler(const ob::msg::BBBOChg *) noexcept;
    void shutdown_handler(const actors::msg::Shutdown *) noexcept;
    void canc_rej_handler(const msg::CancReject *) noexcept;
    void fill_handler(const msg::Fill *) noexcept;
    void fill_sub_handler(const msg::FillSub *) noexcept;
    void stop_trading_handler(const som::msg::Stop *) noexcept;
    void start_trading_handler(const som::msg::Start *) noexcept;
    void start_handler(const actors::msg::Start *) noexcept;
    void ack_handler(const som::msg::Ack *) noexcept;
    void rej_handler(const som::msg::Reject *) noexcept;
    void get_pnl_handler(const som::msg::GetPNL *) noexcept;
    void unstash_handler(const som::msg::UnStash *) noexcept;
    void exit_handler(const som::msg::ExitPos *) noexcept;
    void riskinfo_handler(const som::msg::RiskInfo *) noexcept;
    void get_handler(const cons::msg::Get *) noexcept;
    void set_exch_manager_handler(const msg::SetExchManager *) noexcept;
    void position_response_handler(const frame::som::msg::PositionResponse *) noexcept;
  };
}
