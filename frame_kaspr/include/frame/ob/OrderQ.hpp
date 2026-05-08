#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <iostream>
#include <map>
#include "frame/ob/Order.hpp"
#include "boost/utility.hpp"
#include "logger/act/Logger.hpp"
#include "chutil/hash_map.hpp"
#include <unordered_map>
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


// #define DBG_ORDQ

namespace frame
{
  namespace ob
  {
    class OrderQ : boost::noncopyable
    {

    public:

      std::flat_map<uint64_t, Order *> qordermap;


    private:
      Order *head;
      Order *tail;
      int size_of_book_cnt;
      int orders_in_book;

      const char* get_name() const { return "OrderQ"; }

    public:
      Order *get_head() const noexcept { return head; }

      Order *get_head_no_sim() const noexcept
      {
        OrderQNode *p = head;
        Order *o = static_cast<Order *>(p);
        while (o && o->issim())
        {
          p = p->next;
          o = static_cast<Order *>(p);
        }
        return static_cast<Order *>(p);
      }

      OrderQ()
      {
        head = tail = 0;
        size_of_book_cnt = 0;
        orders_in_book = 0;
      }

      ~OrderQ()
      {
        qordermap.clear();
      }

      inline uint32_t get_size_of_book_cnt() const
      {
        return size_of_book_cnt;
      }

      inline uint32_t get_orders_in_book() const noexcept
      {

#ifdef FAST_SZ_BOOK
        return orders_in_book;
#endif

        OrderQNode *p = head;
        int sz = 0, cnt = 0;
        while (p)
        {
          Order *o = static_cast<Order *>(p);
          if (!o->issim())
          {
            sz += o->sz;
            ++cnt;
          }
          p = p->next;
        }
        ASSERT(sz == orders_in_book, "sz");
        ASSERT(cnt == size_of_book_cnt, "cnt");
        return orders_in_book;
      }

      inline int get_sim_in_book() const noexcept
      {
        OrderQNode *p = head;
        int sz = 0;
        while (p)
        {
          Order *o = static_cast<Order *>(p);
          if (o->issim())
          {
            sz += o->sz;
          }
          p = p->next;
        }
        return sz;
      }

#ifndef FAST_SZ_BOOK

      inline std::pair<int, int> size_of_book() const
      {
        OrderQNode *p = head;
        uint32_t sz = 0, cnt = 0;
        while (p)
        {
          sz += static_cast<Order *>(p)->sz;
          ++cnt;
          p = p->next;
        }
        ASSERT(cnt == get_size_of_book_cnt(), "sz of book cnt");
        ASSERT(sz == get_orders_in_book(), "orders in book");
        return std::pair<int, int>(cnt, sz);
      }

#else
      inline std::pair<int, int> size_of_book() const noexcept
      {
        return std::make_pair(get_size_of_book_cnt(), get_orders_in_book());
      }
#endif

      inline bool isempty() const noexcept
      {
        return head == 0;
      }

      inline bool isempty_or_allsim() const noexcept
      {
        if (isempty())
          return true;
        if (has_all_sim())
          return true;
        return false;
      }

      inline bool has_all_sim() const noexcept
      {
        Order *p = head;
        if (!p)
          return false;
        while (p)
        {
          if (!static_cast<Order *>(p)->issim())
            return false;
          p = static_cast<Order *>(p->next);
        }
        return true;
      }

      inline void append(Order *n) noexcept
      {

#ifdef DBG_ORDQ
        Order *const *tmp;
        auto p = ordermap.get(n->id, tmp);
        if (p)
        {
          std::cout << "order id " << n->id << " " << frame::mda::OrderID::id(n->id) << " added twice\n";
          SNGH;
        }
        get_orders_in_book();
#endif
        ASSERT(n->sz > 0, "order with no sz");
        if (!head)
        {
          ASSERT(!tail, "bad tail");
          head = tail = n;
        }
        else
        {
          n->prev = tail;
          n->next = 0;
          tail->next = n;
          tail = n;
        }
        // #define DBG_ORDQ2
#if defined(DBG_ORDQ) || defined(DBG_ORDQ2)
        ASSERT(ordermap.find(n->id) == ordermap.end(), "already there");
#endif
        // TODO: should not allow double insertions
        //ordermap.insert(n->id, n);
        qordermap[n->id] = n;
        ASSERT(tail->next == 0, "bad next");
        ASSERT(head->prev == 0, "bad prev");
        if (n != tail)
          ASSERT(n->next, "no next");
        if (n != head)
          ASSERT(n->prev, "no prev")
        if (!n->issim())
        {
          orders_in_book += n->sz;
          size_of_book_cnt++;
        }
#ifdef DBG_ORDQ
        get_orders_in_book();
#endif
      }

    private:
      inline void remove_order(Order *n) noexcept
      {
        ASSERT(!n->deleted, "alrady deleted");
        ASSERT(head, "no head");
        ASSERT(tail, "no tail");

        ASSERT(tail->next == 0, "bad next");
        ASSERT(head->prev == 0, "bad prev");

        if (head == tail)
        {
          ASSERT(head == n, "head");
          ASSERT(tail == n, "tail");
          head = tail = 0;
        }
        else if (head == n)
        {
          head = static_cast<Order *>(n->next);
          head->prev = 0;
          n->next = n->prev = 0;
        }
        else if (tail == n)
        {
          tail = static_cast<Order *>(n->prev);
          tail->next = 0;
          n->next = n->prev = 0;
        }
        else
        {
          ASSERT(n->prev, "prev");
          n->prev->next = n->next;
          ASSERT(n->next, "next");
          n->next->prev = n->prev;
        }
        n->prev = n->next = 0;
        n->deleted = true;
        if (!n->issim())
          size_of_book_cnt--;
        // orders_in_book is decremented on canc and fill
        delete n;
        n=nullptr;
      }

      inline friend std::ostream &operator<<(std::ostream &out, const OrderQ &q)
      {

        auto order = q.head;
        while (order)
        {
          auto id = mda::OrderID::id(order->get_id());
          auto ex = mda::OrderID::exchid(order->get_id());
          auto exordid = order->get_exordid();
          if (ex == en::x::SIM)
            out << id << "S:";
          else
            out << id << ":" << exordid << ":";
          out << order->get_sz();
          out << " ";
          order = static_cast<Order *>(order->next);
        }
        return out;
      }

    public:

      // used to clear the book
      void canc_notify_all() noexcept
      {

        while (true)
        {
          auto ptr = qordermap.begin();
          if (ptr == qordermap.end())
            return;
          auto o = ptr->second;
          canc_notify(o, o->sz, 0, en::mt::CANCD);
        }
      }

      void
      canc_notify(Order *o, int sz, int disp_sz, en::mt modtyp) noexcept
      {
        ASSERT(o, "no order");
        ASSERT(!o->cancelled, "cancelling twice");
        ASSERT(!o->deleted, "cancelling a deleted order");
        ASSERT(!o->filled, "cancelling a filled order");
        ASSERT(modtyp == en::mt::CANC || modtyp == en::mt::CANCD, "invalid mod typ");
        ASSERT(disp_sz >= 0, "bad disp_sz");

        int sz_to_canc;
        if (o->issim())
          sz_to_canc = o->sz;
        else if (modtyp == en::mt::CANCD)
          sz_to_canc = std::max(sz, o->sz);
        else
          sz_to_canc = sz;
        // ASSERT(sz_to_canc > 0, "nothing to canc");

        auto sz_canced = o->canc_notify(modtyp, disp_sz, sz_to_canc);

        if (!o->issim())
          orders_in_book -= sz_canced;

        ASSERT(o->sz >= 0, "size went negative");
        ASSERT(orders_in_book >= 0, "size went negative");
        if (o->sz == 0 || modtyp == en::mt::CANCD)
        {
          if (!o->issim())
            orders_in_book -= std::max(o->sz, 0);
          qordermap.erase(o->id);
          remove_order(o);
        }
#ifdef DBG_ORDQ
        get_orders_in_book();
#endif
      }

      int
      fill_notify(
          Order *o,
          int sz,
          int disp_sz,
          uint px,
          en::mt modtyp,
          uint64_t tim) noexcept
      {
        log_dbg("filling order id: %d", mda::OrderID::id(o->id));
#ifdef DBG_ORDQ
        get_orders_in_book();
#endif
        ASSERT(tim > 0, "no tim");
        ASSERT(o, "no such order");
        ASSERT(!o->cancelled, "filling a cancelled order");
        ASSERT(!o->deleted, "filling a deleted order");
        ASSERT(!o->filled, "filling a filled order");
        ASSERT(o->issim(), "filling a non sim order");
        ASSERT(o->sz > 0, "cant have negative size");
        ASSERT(modtyp == en::mt::EXEC || modtyp == en::mt::EXECD, "invalid mod typ");
        if (modtyp == en::mt::EXECD)
          ASSERT(disp_sz != 0, "bad disp sz");
        auto orig_sz = o->sz;
        o->fill_notify(sz, disp_sz, px, modtyp, tim);
        auto sz_filled = orig_sz - o->sz;
        ASSERT(sz_filled > 0, "bad fill notify");

        // if sims are visible hve to change this
        // and one other place (this is not the only place)
#ifdef SIMS_ARE_INVIS
        if (modtyp == en::mt::EXECD)
        {
          orders_in_book -= orig_sz;
        }
        else
        {
          orders_in_book -= sz_filled;
        }
#endif
        if (o->sz == 0 || modtyp == en::mt::EXECD)
        {
          //ordermap.del(o->id);
          qordermap.erase(o->id);
          remove_order(o);
        }
#ifdef DBG_ORDQ
        get_orders_in_book();
#endif
        return sz_filled;
      }

      static int // this is just to gen fills for sim orders or arrival
      fill_notify_s(Order *o, int sz, int disp_sz, uint px, en::mt modtyp, uint64_t tim) noexcept {
        ASSERT(modtyp == en::mt::EXEC || modtyp == en::mt::EXECD, "invalid mod type");
        return o->fill_notify(sz, disp_sz, px, modtyp, tim);
      }
    };

  }
}
