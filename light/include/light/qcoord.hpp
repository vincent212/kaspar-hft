#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <tuple>
#include <map>
#include <unordered_map>
#include <set>
#include <list>
#include <mutex>
#include "chutil/Macros.hpp"
#include "enum/e_names.hpp"
#include "logger/act/Logger.hpp"
#include "chutil/cache_array.hpp"

namespace light
{

  struct PCoord
  {
  private:

    std::recursive_mutex pcoord_mutex;
    int position = 0;

    std::recursive_mutex incr_mutex;

    public:

    int hedge_pos_size_breach = 0;
    int directional_pos_breach = 0;
    int out_px_breach = 0;
    int exceeded_reset_dist = 0;
    int expectation_breach = 0;
    int zero_pos = 0;
    int no_out_px_ = 0;
    int following = 0;
    int last_trade_px_buy = 0;
    int last_trade_px_sel = 0;
    int canc_to_aggr = 0;
    int delay_skip = 0;
    int attached_order_id_match = 0;

    PCoord() : incr_mutex(), pcoord_mutex()
    {
    }

    virtual ~PCoord() = default;

    const char* get_name() const { return "pcoord"; }

    //
    // thus must keep track of position at price
    // add assertion to order book to make sure it doesn fill sim orders
    // on arrival for more than available on the best level of stack
    //

    virtual void add_position(en::bs side, int sz)
    {
      log_dbg("add_position side: %s, sz: %d",
        en::to_string(side), sz);
      std::lock_guard<std::recursive_mutex> guard(pcoord_mutex);
      if (sz < 1)
      {
        log_err("bad sz: %d", sz);
        return;
      }
      ASSERT(sz > 0, "bad sz");
      if (side == en::bs::BUY)
        position += sz;
      else if (side == en::bs::SEL)
        position -= sz;
      else
        SNGH;
    }

    virtual int get_position() noexcept
    {
      std::lock_guard<std::recursive_mutex> guard(pcoord_mutex);
      return position;
    }

    void incr_no_outpx() noexcept
    {
      no_out_px_++;
    }

    void incr_following() noexcept
    {
      following++;
    };

    void incr_zero_pos() noexcept
    {
      zero_pos++;
    };

    void incr_delay_skip() noexcept
    {
      delay_skip++;
    };

    void incr_hedge_pos_size_breach() noexcept
    {
      hedge_pos_size_breach++;
    };

    void incr_directional_pos_breach() noexcept
    {
      directional_pos_breach++;
    };

    void incr_out_px_breach() noexcept
    { 
      out_px_breach++;
    };

    void incr_exceeded_reset_dist() noexcept
    {
      exceeded_reset_dist++;
    };

    void incr_expectation_breach() noexcept
    {
      expectation_breach++;
    };

    void incr_canc_to_aggr() noexcept
    {
      canc_to_aggr++;
    };

    void incr_attached_order_id_match() noexcept
    {
      attached_order_id_match++;
    };

    auto get_incrs() const noexcept
    { 
      return std::make_tuple(
        hedge_pos_size_breach,
        directional_pos_breach,
        out_px_breach,
        exceeded_reset_dist,
        expectation_breach,
        zero_pos,
        canc_to_aggr
      );
    }

    void set_last_trade_px(int px, en::bs side) noexcept
    {
      std::lock_guard<std::recursive_mutex> guard(incr_mutex);
      if (side == en::bs::BUY)
        last_trade_px_buy = px;
      else if (side == en::bs::SEL)
        last_trade_px_sel = px;
      else
        SNGH;
    }

    auto get_last_trade_px(en::bs side) noexcept
    {
      std::lock_guard<std::recursive_mutex> guard(incr_mutex);
      if (side == en::bs::BUY)
        return last_trade_px_buy;
      else if (side == en::bs::SEL)
        return last_trade_px_sel;
      else
        SNGH;
    }
    
  };


  /*
    * QCoord is a class that manages the orders at a given price level.
    * It keeps track of the size of orders at each price level and the
    * last order id for each price level. It also provides methods to add
    * and remove orders, as well as to get the size and last order id at a
    * given price level.
    * 
    * this class is not thread safe, it is used in a single thread context
    * 
  */
  struct QCoord
  {

    const char* get_name() const { return "qcoord"; }

    chutil::cache_array<int,256> cache_sz_at_px;
    chutil::cache_array<int,256> chache_cum_sz;
    chutil::cache_array<int,256> last_in_at_px;

    std::recursive_mutex qcoord_mutex;

    int num_canc = 0;

    QCoord() {
    }

    virtual ~QCoord() = default;

    struct ord
    {
      int id, sz;
      ord(int _id, int _sz) : id(_id), sz(_sz) {}
      ord() {}
    };

    std::map<int,std::map<int,ord>> px_mmid_ord;

    uint64_t mmid_orders;

    int tot_sz_cache=0;

    uint64_t attached_order_id = 0;

    uint64_t get_attached_order_id() noexcept
    {
      std::lock_guard<std::recursive_mutex> guard(qcoord_mutex);
      return attached_order_id;
    }

    void set_attached_order_id(uint64_t id) noexcept
    {
      std::lock_guard<std::recursive_mutex> guard(qcoord_mutex);
      attached_order_id = id;
    }

    void add_order(int px, int sz, int mmid, int id, int mxsz) noexcept
    {

      std::lock_guard<std::recursive_mutex> guard(qcoord_mutex);

      log_inf("add px: %d, sz: %d, mmid: %d, id: %d, mxsz: %d",
        px, sz, mmid, id, mxsz);
      ASSERT(sz > 0 && mxsz > 0, "bad sz");
      //ASSERT(!(mmid_orders & (1ULL << mmid)), "this mm id already has an order");
      auto p = px_mmid_ord.find(px);
      if (p != px_mmid_ord.end())
      {
        auto p2 = p->second.find(mmid);
        // make sure an mmid is managing only one order
        ASSERT(p2 == p->second.end(), "already have this mm id");
        p->second[mmid] = ord(id, sz);
      }
      else
        px_mmid_ord[px][mmid] = ord(id, sz);
      mmid_orders |= 1ULL << mmid;
      last_in_at_px.insert(px, mmid, true);
      if (sz_at_px(px) > mxsz)
      {
        auto _ss_ = sz_at_px(px);
        std::cerr <<  _ss_ << " px: " << px << " sz: " << sz_at_px(px) << " mxsz: " << mxsz << std::endl;
        log_err("px: %d, sz: %d, mxsz: %d", px, sz_at_px(px), mxsz);
      }
      ASSERT(sz_at_px(px) <= mxsz, "max size exceeded");
      tot_sz_cache+=sz;
      cache_sz_at_px.clear();
      chache_cum_sz.clear();
    }

#define QCOORDMULTIREMOVE

    int remove_order(int px, int sz, int mmid, int id, bool all = false) noexcept
    {

      std::lock_guard<std::recursive_mutex> guard(qcoord_mutex);

      log_inf("remove px: %d, sz: %d, mmid: %d, id: %d, all: %d",
        px, sz, mmid, id, all);
      if (!all) ASSERT(sz > 0, "bad sz");
#ifdef QCOORDMULTIREMOVE
      if (!(mmid_orders & (1ULL << mmid)))
        return 0;
#else
      ASSERT((mmid_orders & (1ULL << mmid)), "this mm id has no order");
#endif
      auto p = px_mmid_ord.find(px);
      ASSERT(p != px_mmid_ord.end(), "no such price");
      auto p2 = p->second.find(mmid);
      ASSERT(p2 != p->second.end(), "no such mmid to fill");
      ASSERT(p2->second.id == id, "order ids do not match");
      ASSERT(sz <= p2->second.sz, "cannot remove that much size");
      p2->second.sz -= sz;
      if (p2->second.sz == 0 || all)
      {
        tot_sz_cache-=p2->second.sz;
        tot_sz_cache-=sz;
        p->second.erase(p2);
        mmid_orders &= ~(1ULL << mmid);
        cache_sz_at_px.clear();
        chache_cum_sz.clear();
        return 0;
      }
      tot_sz_cache-=sz;
      cache_sz_at_px.clear();
      chache_cum_sz.clear();
      return p2->second.sz;
    }

    virtual int sz_at_px(int px) noexcept
    {

      std::lock_guard<std::recursive_mutex> guard(qcoord_mutex);

      int const *ret;
      auto rc=cache_sz_at_px.get(px,ret);
      if(rc) return *ret;

      auto p = px_mmid_ord.find(px);
      if (p == px_mmid_ord.end())
        return 0;
      int sz = 0;
      for (const auto& p2 : p->second)
      {
        sz += p2.second.sz;
      }

      cache_sz_at_px.insert(px,sz);

      log_dbg("sz_at_px px: %d, sz: %d", px, sz);

      return sz;

    }

    int last_in(int px) noexcept
    {

      std::lock_guard<std::recursive_mutex> guard(qcoord_mutex);

      const int *v;
      auto p = last_in_at_px.get(px, v);
      if (!p)
          return -1;
      else
          return *v;;
    }

    int cum_sz_upto_px(en::bs side, int px, bool use_cache = true) noexcept
    {

      std::lock_guard<std::recursive_mutex> guard(qcoord_mutex);

      struct idx_t {
        en::bs side;
        int px;
      };

      union u_idx_t {
        uint64_t iidx;
        idx_t sidx;
        u_idx_t() {
          iidx=0;
        }
      };

      u_idx_t idx;
      idx.sidx.side=side;
      idx.sidx.px=px;

      if (use_cache)
      {
        int const *ret;
        auto rc=chache_cum_sz.get(idx.iidx,ret);
        if(rc) return *ret;
      }

      int sz = 0;
      if (side == en::bs::BUY)
      {
        auto p = px_mmid_ord.rbegin();
        while (p != px_mmid_ord.rend())
        {
          if (p->first >= px)
          {
            for (const auto& p2 : p->second)
            {
              sz += p2.second.sz;
            }
          }
          ++p;
        }
      }
      else if (side == en::bs::SEL)
      {
        auto p = px_mmid_ord.begin();
        while (p != px_mmid_ord.end())
        {
          if (p->first <= px)
          {
            for (const auto& p2 : p->second)
            {
              sz += p2.second.sz;
            }
          }
          ++p;
        }
      }

      if (use_cache)
        chache_cum_sz.insert(idx.iidx,sz);

      return sz;
    }

    virtual int total_sz() noexcept
    {
      return tot_sz_cache;
    }

    int lowest_price() noexcept
    {

      std::lock_guard<std::recursive_mutex> guard(qcoord_mutex);

      if (px_mmid_ord.empty())
        return std::numeric_limits<int>::min();
      auto p = px_mmid_ord.begin();
      while (p->second.empty() && p != px_mmid_ord.end())
        ++p;
      if (p == px_mmid_ord.end())
        return std::numeric_limits<int>::min();
      return p->first;
    }

    auto highest_price() noexcept
    {

      std::lock_guard<std::recursive_mutex> guard(qcoord_mutex);

      if (px_mmid_ord.empty())
        return std::numeric_limits<int>::max();
      auto p = px_mmid_ord.rbegin();
      while (p->second.empty() && p != px_mmid_ord.rend())
        ++p;
      if (p == px_mmid_ord.rend())
        return std::numeric_limits<int>::max();
      return p->first;
    }

    auto incr_num_canc() noexcept
    {
      std::lock_guard<std::recursive_mutex> guard(qcoord_mutex);
      num_canc++;
      return num_canc;
    }

    void clear_num_canc() noexcept
    {
      std::lock_guard<std::recursive_mutex> guard(qcoord_mutex);
      num_canc = 0;
    }
  
  };

}
