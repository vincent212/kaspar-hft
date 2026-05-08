#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <list>
#include "chutil/Macros.hpp"
#include "enum/e_names.hpp"

namespace frame
{
  namespace pos
  {
    struct Position
    {

      struct PosEntry // this is an actual trade
      {

        PosEntry(
          en::bs _trade_type,
          double _trade_price,
          int _sz) :
          trade_type(_trade_type),
          trade_price(_trade_price),
          sz(_sz)
        {
        }

        PosEntry()
        {
          trade_price=sz=-1;
        }

        en::bs trade_type;
        double trade_price;
        int sz;

      };

      struct Swap
      {
        double old_price;
        int old_size;
        double new_price;
        int new_size;
      };

      std::list<PosEntry> open_positions;
      double realized_pnl;
      int nb_sell_shares;
      int nb_buy_shares;
      double last_trade_price_val;
      int nb_trades;

      Position()
      {
        realized_pnl = 0;
        nb_sell_shares = 0;
        nb_buy_shares = 0;
        last_trade_price_val = -1;
        nb_trades = 0;
      }

      inline double pnl() const
      {
        return realized_pnl;
      }

      inline int sell_shares() const
      {
        return nb_sell_shares;
      }

      inline int buy_shares() const
      {
        return nb_buy_shares;
      }

      inline double last_trade_price() const
      {
        return last_trade_price_val;
      }

      en::bs get_side() const
      {
        en::bs side;
        int sign=0;
        for(const auto&pe:open_positions)
        {
          side=pe.trade_type;
          if (pe.trade_type == en::bs::SEL)
          {
            ASSERT(sign <= 0, "sign flipped");
            sign = -1;
          }
          else if (pe.trade_type == en::bs::BUY)
          {
            ASSERT(sign >= 0, "sign flipped");
            sign = 1;
          }
        }
        return side;
      }

      // returns negative if short
      inline int totalSize() const
      {
        int sz = 0;
        int sign = 0;
        for (auto i = open_positions.begin();
          i != open_positions.end();
          ++i)
        {
          const PosEntry &pe = *i;
          ASSERT(pe.sz >= 0, "bad trade size");
          if (pe.trade_type == en::bs::SEL)
          {
            ASSERT(sign <= 0, "sign flipped");
            sign = -1;
            sz += -pe.sz;
          }
          else if (pe.trade_type == en::bs::BUY)
          {
            ASSERT(sign >= 0, "sign flipped");
            sign = 1;
            sz += pe.sz;
          }
          else
            SNGH;
        }
        return sz;
      }

      void do_swap(const Swap&s)
      {
        // close out current position
        auto curr_pos=totalSize();
        if (curr_pos==0)
          return;
        auto side=(*open_positions.begin()).trade_type;
        auto otherside=side==en::bs::BUY?en::bs::SEL:en::bs::BUY;
        addToPosition(PosEntry(otherside,s.old_price,curr_pos));
        // enter a new position with the new price and size
        auto new_pos=int(curr_pos*s.new_size/double(s.old_size)+.5);
        addToPosition(PosEntry(side,s.new_price,new_pos));
      }

      double unrealPnl(double bid, double off) const
      {
        double pnl = 0;
        for (auto i = open_positions.begin();
          i != open_positions.end();
          ++i)
        {
          const auto &pe = *i;
          ASSERT(pe.sz >= 0, "bad trade size");
          if (pe.trade_type == en::bs::SEL)
            pnl += (pe.trade_price - off) * pe.sz;
          else if (pe.trade_type == en::bs::BUY)
            pnl += (bid - pe.trade_price) * pe.sz;
          else
            SNGH;
        }
        return pnl;
      }

      inline double addToPosition(const PosEntry &e)
      {
        if (e.trade_type == en::bs::BUY)
        {
          nb_buy_shares += e.sz;
        }
        else
        {
          nb_sell_shares += e.sz;
        }

        nb_trades++;

        last_trade_price_val = e.trade_price;

        double pnl = 0;
        if (open_positions.empty())
          open_positions.push_front(e);
        else if (e.trade_type == (*open_positions.begin()).trade_type)
          open_positions.push_front(e);
        else
          pnl = closeOutPositions(e);
        realized_pnl += pnl;
        return pnl;
      }


    private:

      double closeOutPositions(PosEntry e)
      {
        auto i = open_positions.begin();
        ASSERT(i != open_positions.end(), "cant close out position");

        ASSERT((*i).sz >= 0, "unit pos");
        ASSERT(e.sz >= 0, "unit pos");

        double pnl = calcRealizedPnl(*i,e);
        if ((*i).sz > e.sz)
        {
          // partially closed out
          (*i).sz -= e.sz;
        }
        else if ((*i).sz == e.sz)
        {
          // last position fully closed out
          i = open_positions.erase(i);
        }
        else
        {
          // last pos fully closed out but more to go
          e.sz -= (*i).sz;
          i = open_positions.erase(i);
          if (open_positions.empty())
            open_positions.push_front(e);
          else
            pnl += closeOutPositions(e);
        }
        return pnl;
      }

      inline double calcRealizedPnl(PosEntry &first_trade, PosEntry &second_trade)
      {
        ASSERT(first_trade.trade_type != second_trade.trade_type,
          "trade types mixed up");
        double pnl = 0;
        int sz = second_trade.sz > first_trade.sz ? first_trade.sz : second_trade.sz;
        if (first_trade.trade_type == en::bs::BUY)
        {
          double pdiff = second_trade.trade_price - first_trade.trade_price;
          pnl = sz * pdiff;
        }
        else if (first_trade.trade_type == en::bs::SEL)
        {
          double pdiff = second_trade.trade_price - first_trade.trade_price;
          pnl = sz * (-pdiff);
        }
        else
          SNGH;
        return pnl;
      }

    };
  }
}
