#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "chutil/Macros.hpp"

#include <string>
#include <deque>
#include <memory>
#include <boost/format.hpp>
#include "chutil/ut.hpp"
#include "chutil/Time.hpp"
#include "frame/mda/OrderID.hpp"
#include "actors/Message.hpp"
#include "actors/MemoryPool.hpp"
#include "enum/e_names.hpp"
#include "frame/ref/Price.hpp"
#include "frame/ref/RefData.hpp"
#include "frame/mda/msg/point.hpp"
#include "bfile/r_l3.hpp"


#include "boost/intrusive_ptr.hpp"
#include "boost/smart_ptr/intrusive_ref_counter.hpp"

namespace frame
{
  namespace mda
  {
    namespace msg
    {
#define PAYLOAD_STR_SZ 8
#define CHOPIN_MAX_ORD_SZ 10000000

      struct data_pay_load : public boost::intrusive_ref_counter<data_pay_load,boost::thread_safe_counter>  , public actors::MemoryPool<data_pay_load>
      {

        mutable uint32_t references=0;

        uint64_t ts0;
        uint64_t
            tim, // trasact time
            send_tim;

        uint64_t txtim_epoch = 0, sendtim_epoch = 0, hndl_tim_epoch = 0;

        en::x mkt;
        //char mkt_str[PAYLOAD_STR_SZ];
        uint sym;
        //char sym_str[PAYLOAD_STR_SZ];
        ref::Price px;
        int sz;
        int disp_sz;
        en::bs side;
        en::mt action;
        en::md mev;
        en::ot ot;
        unsigned long long order_ref;
        int owner;
        uint64_t ex_order_id;
        bool eoe;
        bool recovery;


        //TODO: no need to copy all the calc arrays into every
        // point they are not used #accumulators
        point point_;

        data_pay_load(const data_pay_load& other) = default;

        data_pay_load()
        {
          init();
        }

        void init()
        {
          //memset(mkt_str, 0, PAYLOAD_STR_SZ);
          //memset(sym_str, 0, PAYLOAD_STR_SZ);
          sz = -1;
          disp_sz = 0;
          order_ref = 0;
          mkt = en::x::UNI;
          side = en::bs::UNI;
          action = en::mt::UNI;
          mev = en::md::UNI;
          ot = en::ot::UNI;
          owner = 0;
          ex_order_id = 0;
          eoe = false;
          recovery = false;
        }

        bool is_valid() const
        {
          if (mev == en::md::CLEAR)
            return true;
          if (mev == en::md::EOP)
            return true;
          if (mev == en::md::GAP)
            return true;
          if (mev == en::md::EOBURST)
            return true;
          if ((mev == en::md::TEST) * (sz > 0))
            return true;
          if ((sz < 0) + (sz > CHOPIN_MAX_ORD_SZ) + (disp_sz < 0))
            ERRF(boost::format("innvalid size %d %d %d") % sz % CHOPIN_MAX_ORD_SZ % disp_sz);
          ASSERTF(en::is_valid(mkt), boost::format("invalid mkt %d") % mkt);
          ASSERT(en::is_valid(side), "invalid side");
          if (mev != en::md::ADD)
          {
            ASSERT(en::is_valid(action), "invalid act");
          }
          ASSERT(en::is_valid(mev), "invalid mev");
          if (!recovery && !is_sim())
          {
            ASSERT(send_tim > 0, "invalid time");
          }
          return true;
        }

        inline bool is_trade() const
        {
          return ((mev==en::md::MOD)*(action==en::mt::EXEC));
        }

        inline bool is_add() const
        {
          return (mev==en::md::ADD);
        }

        inline bool is_canc() const
        {
          return (((mev==en::md::MOD)+(mev==en::md::DEL))
            *
            ((action==en::mt::CANC)+(action==en::mt::CANCD)));
        }

        inline bool is_cancd() const
        {
          return (((mev==en::md::MOD)+(mev==en::md::DEL))
            *
            (action==en::mt::CANCD));
        }

        inline bool is_hit() const
        {
          return (is_trade()*(side==en::bs::BUY));
        }

        inline bool is_tak() const
        {
          return (is_trade()*(side==en::bs::SEL));
        }

        inline bool is_sim() const
        {
          return OrderID::exchid(order_ref) == en::x::SIM;
        }

        // bool operator<(const data_pay_load& d) const
        // {
        //   if (tim == d.tim)
        //   {
        //     if (is_sim() && !d.is_sim())
        //       return false;
        //     else
        //       return true;
        //   }
        //   else
        //     return tim < d.tim;
        // }

        ~data_pay_load()
        {
        }

        std::string to_string() const
        {
          boost::format fmt("exid: %d, cid: %d, id: %d, mkt: %d, time: %llu, sym: %s, symid: %d, px: %d, sz: %d, mev: %d, otyp: %d, atyp: %d");
          fmt%
            this->ex_order_id %
            frame::mda::OrderID::id(this->order_ref) % this->order_ref % static_cast<int>(this->mkt) %
            static_cast<unsigned long long>(this->tim) %
            ref::RefData::get_asset_name(this->sym) %
            this->sym % this->px.to_int() % this->sz % static_cast<int>(this->mev) % static_cast<int>(this->side) % static_cast<int>(this->action);
          
          std::ostringstream os;
          os << this->point_;

          return fmt.str() + " {point: " + os.str() + "}";
        }

        friend std::ostream& operator<<(std::ostream& out, const data_pay_load& d)
        {
          out << d.to_string();
          return out;
        }

        static
          boost::intrusive_ptr<msg::data_pay_load>
          make_payload(
            uint64_t _ts0,
            en::bs side,
            uint64_t ex_order_id,
            uint64_t order_ref,
            int sym,
            en::md mev,
            en::mt act,
            uint sz,
            uint disp_sz,
            double px,
            uint64_t txtim,
            uint64_t sendtim,
            en::x venue,
            bool eoe,
            bool recovery
            )
        {
          //auto& sym_s = ref::RefData::inst().get_asset_name(sym);
          auto payload = boost::intrusive_ptr<msg::data_pay_load>(new msg::data_pay_load());
          payload->ts0 = _ts0;
          payload->mkt = venue;
          //strcpy(payload->mkt_str, en::to_string(venue));
          payload->sym = sym;
          //strcpy(payload->sym_str, sym_s.c_str());
          payload->ex_order_id = ex_order_id;
          payload->mev = mev;
          payload->action = act;
          payload->tim = txtim;
          payload->send_tim = sendtim;
          payload->order_ref = order_ref;
          payload->sz = sz;
          payload->disp_sz = disp_sz;
          payload->side = side;
          payload->eoe = eoe;
          payload->recovery = recovery;
          if (px > 0)
          {
            auto px_ = ref::Price((long double)px, sym);
            payload->px = px_;
          }
          else
          {
            SNGH;
          }
          ASSERT(payload->is_valid(), "reconstructed message not valid");
          return payload;
        }

        static
          boost::intrusive_ptr<msg::data_pay_load>
          make_payload2(
            en::bs side,
            int oid,
            int sym,
            en::md mev,
            en::mt act,
            uint sz,
            uint disp_sz,
            int px,
            uint64_t tim,
            en::x venue)
        {
          //auto& sym_s = ref::RefData::inst().get_asset_name(sym);
          auto payload = boost::intrusive_ptr<msg::data_pay_load>(new msg::data_pay_load());
          payload->mkt = venue;
          //strcpy(payload->mkt_str, en::to_string(venue));
          payload->sym = sym;
          //strcpy(payload->sym_str, sym_s.c_str());
          payload->mev = mev;
          payload->action = act;
          payload->tim = tim;
          payload->order_ref = mda::OrderID::longid(venue, oid);
          payload->sz = sz;
          payload->disp_sz = disp_sz;
          payload->side = side;
          payload->px = ref::Price(px, sym);
          ASSERT(payload->is_valid(), "reconstructed message not valid");
          return payload;
        }
      
        // JSON streaming helper: use payload.as_json() with operator<<
        struct json_out {
          const data_pay_load& d;
        };

        inline json_out as_json() const { return json_out{*this}; }

        friend std::ostream& operator<<(std::ostream& os, const json_out& w) {
          const auto& x = w.d;
          os << '{'
             << "\"ts0\":" << x.ts0
             << ",\"tim\":" << x.tim
             << ",\"send_tim\":" << x.send_tim
             << ",\"txtim_epoch\":" << x.txtim_epoch
             << ",\"sendtim_epoch\":" << x.sendtim_epoch
             << ",\"hndl_tim_epoch\":" << x.hndl_tim_epoch
             << ",\"mkt\":" << static_cast<int>(x.mkt)
             << ",\"sym\":" << x.sym
             << ",\"px\":" << x.px.to_int()
             << ",\"sz\":" << x.sz
             << ",\"disp_sz\":" << x.disp_sz
             << ",\"side\":" << static_cast<int>(x.side)
             << ",\"action\":" << static_cast<int>(x.action)
             << ",\"mev\":" << static_cast<int>(x.mev)
             << ",\"ot\":" << static_cast<int>(x.ot)
             << ",\"order_ref\":" << x.order_ref
             << ",\"owner\":" << x.owner
             << ",\"ex_order_id\":" << x.ex_order_id
             << ",\"eoe\":" << (x.eoe ? "true" : "false")
             << ",\"recovery\":" << (x.recovery ? "true" : "false")
             << ",\"point\":" << x.point_
             << '}';
          return os;
        }

        bool data_only_equals(const data_pay_load& rhs) const noexcept {
          bool result = true;

          if (ts0 != rhs.ts0) {
            std::cerr << "data_only_equals: ts0 differs: " << ts0 << " vs " << rhs.ts0 << std::endl;
            result = false;
          }
          if (tim != rhs.tim) {
            std::cerr << "data_only_equals: tim differs: " << tim << " vs " << rhs.tim << std::endl;
            result = false;
          }
          if (send_tim != rhs.send_tim) {
            std::cerr << "data_only_equals: send_tim differs: " << send_tim << " vs " << rhs.send_tim << std::endl;
            result = false;
          }
          if (txtim_epoch != rhs.txtim_epoch) {
            std::cerr << "data_only_equals: txtim_epoch differs: " << txtim_epoch << " vs " << rhs.txtim_epoch << std::endl;
            result = false;
          }
          if (sendtim_epoch != rhs.sendtim_epoch) {
            std::cerr << "data_only_equals: sendtim_epoch differs: " << sendtim_epoch << " vs " << rhs.sendtim_epoch << std::endl;
            result = false;
          }
          if (hndl_tim_epoch != rhs.hndl_tim_epoch) {
            std::cerr << "data_only_equals: hndl_tim_epoch differs: " << hndl_tim_epoch << " vs " << rhs.hndl_tim_epoch << std::endl;
            result = false;
          }
          if (mkt != rhs.mkt) {
            std::cerr << "data_only_equals: mkt differs: " << static_cast<int>(mkt) << " vs " << static_cast<int>(rhs.mkt) << std::endl;
            result = false;
          }
          if (sym != rhs.sym) {
            std::cerr << "data_only_equals: sym differs: " << sym << " vs " << rhs.sym << std::endl;
            result = false;
          }
          if (px.to_int() != rhs.px.to_int()) {
            std::cerr << "data_only_equals: px differs: " << px.to_int() << " vs " << rhs.px.to_int() << std::endl;
            result = false;
          }
          if (sz != rhs.sz) {
            std::cerr << "data_only_equals: sz differs: " << sz << " vs " << rhs.sz << std::endl;
            result = false;
          }
          if (disp_sz != rhs.disp_sz) {
            std::cerr << "data_only_equals: disp_sz differs: " << disp_sz << " vs " << rhs.disp_sz << std::endl;
            result = false;
          }
          if (side != rhs.side) {
            std::cerr << "data_only_equals: side differs: " << static_cast<int>(side) << " vs " << static_cast<int>(rhs.side) << std::endl;
            result = false;
          }
          // action is commented out in original comparison
          if (mev != rhs.mev) {
            std::cerr << "data_only_equals: mev differs: " << static_cast<int>(mev) << " vs " << static_cast<int>(rhs.mev) << std::endl;
            result = false;
          }
          if (ot != rhs.ot) {
            std::cerr << "data_only_equals: ot differs: " << static_cast<int>(ot) << " vs " << static_cast<int>(rhs.ot) << std::endl;
            result = false;
          }
          // order_ref is commented out in original comparison
          if (owner != rhs.owner) {
            std::cerr << "data_only_equals: owner differs: " << owner << " vs " << rhs.owner << std::endl;
            result = false;
          }
          if (ex_order_id != rhs.ex_order_id) {
            std::cerr << "data_only_equals: ex_order_id differs: " << ex_order_id << " vs " << rhs.ex_order_id << std::endl;
            result = false;
          }
          if (eoe != rhs.eoe) {
            std::cerr << "data_only_equals: eoe differs: " << eoe << " vs " << rhs.eoe << std::endl;
            result = false;
          }
          if (recovery != rhs.recovery) {
            std::cerr << "data_only_equals: recovery differs: " << recovery << " vs " << rhs.recovery << std::endl;
            result = false;
          }
          // point_ comparison excluded
          return result;
        }

        bool operator==(const data_pay_load& rhs) const noexcept {
          bool result = true;

#define DEBUG_PAYLOAD_COMPARE

#ifdef DEBUG_PAYLOAD_COMPARE
          if (ts0 != rhs.ts0) {
            std::cerr << "ts0 differs: " << ts0 << " vs " << rhs.ts0 << std::endl;
            result = false;
          }
          if (tim != rhs.tim) {
            std::cerr << "tim differs: " << tim << " vs " << rhs.tim << std::endl;
            result = false;
          }
          if (send_tim != rhs.send_tim) {
            std::cerr << "send_tim differs: " << send_tim << " vs " << rhs.send_tim << std::endl;
            result = false;
          }
          if (txtim_epoch != rhs.txtim_epoch) {
            std::cerr << "txtim_epoch differs: " << txtim_epoch << " vs " << rhs.txtim_epoch << std::endl;
            result = false;
          }
          if (sendtim_epoch != rhs.sendtim_epoch) {
            std::cerr << "sendtim_epoch differs: " << sendtim_epoch << " vs " << rhs.sendtim_epoch << std::endl;
            result = false;
          }
          if (hndl_tim_epoch != rhs.hndl_tim_epoch) {
            std::cerr << "hndl_tim_epoch differs: " << hndl_tim_epoch << " vs " << rhs.hndl_tim_epoch << std::endl;
            result = false;
          }
          if (mkt != rhs.mkt) {
            std::cerr << "mkt differs: " << static_cast<int>(mkt) << " vs " << static_cast<int>(rhs.mkt) << std::endl;
            result = false;
          }
          if (sym != rhs.sym) {
            std::cerr << "sym differs: " << sym << " vs " << rhs.sym << std::endl;
            result = false;
          }
          if (px.to_int() != rhs.px.to_int()) {
            std::cerr << "px differs: " << px.to_int() << " vs " << rhs.px.to_int() << std::endl;
            result = false;
          }
          if (sz != rhs.sz) {
            std::cerr << "sz differs: " << sz << " vs " << rhs.sz << std::endl;
            result = false;
          }
          if (disp_sz != rhs.disp_sz) {
            std::cerr << "disp_sz differs: " << disp_sz << " vs " << rhs.disp_sz << std::endl;
            result = false;
          }
          if (side != rhs.side) {
            std::cerr << "side differs: " << static_cast<int>(side) << " vs " << static_cast<int>(rhs.side) << std::endl;
            result = false;
          }
          // action is commented out in original comparison
          if (mev != rhs.mev) {
            std::cerr << "mev differs: " << static_cast<int>(mev) << " vs " << static_cast<int>(rhs.mev) << std::endl;
            result = false;
          }
          if (ot != rhs.ot) {
            std::cerr << "ot differs: " << static_cast<int>(ot) << " vs " << static_cast<int>(rhs.ot) << std::endl;
            result = false;
          }
          // order_ref is commented out in original comparison
          if (owner != rhs.owner) {
            std::cerr << "owner differs: " << owner << " vs " << rhs.owner << std::endl;
            result = false;
          }
          if (ex_order_id != rhs.ex_order_id) {
            std::cerr << "ex_order_id differs: " << ex_order_id << " vs " << rhs.ex_order_id << std::endl;
            result = false;
          }
          if (eoe != rhs.eoe) {
            std::cerr << "eoe differs: " << eoe << " vs " << rhs.eoe << std::endl;
            result = false;
          }
          if (recovery != rhs.recovery) {
            std::cerr << "recovery differs: " << recovery << " vs " << rhs.recovery << std::endl;
            result = false;
          }
          if (!(point_ == rhs.point_)) {
            std::cerr << "point_ differs" << std::endl;
            result = false;
          }
          return result;
#else
          return ts0 == rhs.ts0 &&
                 tim == rhs.tim &&
                 send_tim == rhs.send_tim &&
                 txtim_epoch == rhs.txtim_epoch &&
                 sendtim_epoch == rhs.sendtim_epoch &&
                 hndl_tim_epoch == rhs.hndl_tim_epoch &&
                 mkt == rhs.mkt &&
                 sym == rhs.sym &&
                 px.to_int() == rhs.px.to_int() &&
                 sz == rhs.sz &&
                 disp_sz == rhs.disp_sz &&
                 side == rhs.side &&
                 //action == rhs.action &&
                 mev == rhs.mev &&
                 ot == rhs.ot &&
                 // TachBook does not use order_ref
                 //order_ref == rhs.order_ref &&
                 owner == rhs.owner &&
                 ex_order_id == rhs.ex_order_id &&
                 eoe == rhs.eoe &&
                 recovery == rhs.recovery
                 && point_ == rhs.point_;
#endif
        }

      };


      //
      // Data is a wrapper message for payload
      // there is nothing in data
      // this was done so that multiple data messages could be sent
      // with the same payload. Payload is also a shared ptr that
      // way it can be stored unlike the data message which will
      // get automaticaly deleted usually
      //
      struct Data : public  actors::Message_N<17>  , public actors::MemoryPool<Data, 16, 16, 2048>
      {
        Data(bool israw = false) : 
          israw(israw), ts0(sys_nanoseconds{})
           {
            
        }

        // payload is a boost intrusive ptr
        // https://baptiste-wicht.com/posts/2011/11/boost-intrusive_ptr.html
        // but make sure that it is the single threaded version
        // need to use the thread_unsafe_counter policy
        // https://www.boost.org/doc/libs/1_60_0/libs/smart_ptr/intrusive_ref_counter.html
        // the idea is to derive from a helper object that implements the 
        // add ref and delete ref function calls
        //

        boost::intrusive_ptr<const data_pay_load> payload = nullptr;

        bfile::l3_t l3;
        sys_nanoseconds ts0; // arrival time
        bool israw = false;

#ifdef unused

        static
          msg::Data*
          make_data(boost::intrusive_ptr<const msg::data_pay_load> p)
        {
          auto d = new msg::Data();
          d->payload = p;
          return d;
        }

        static
          msg::Data *
          make_data(const msg::data_pay_load* p) {
          auto d=new msg::Data();
          d->payload=boost::intrusive_ptr<const msg::data_pay_load>(p);
          return d;
        }

        static 
          const msg::Data*
          make_add(uint32_t oid, int sym, en::bs side, en::x venue, uint64_t tim, int px, int sz)
        {
          auto p = msg::data_pay_load::make_payload2(side, oid, sym, en::md::ADD, en::mt::NONE, sz, sz, px, tim, venue);
          auto d = make_data(p);
          return d;
        }

        static 
          const msg::Data *
          make_canc(uint32_t oid, int sym, en::bs side, en::x venue, uint64_t tim, int px, int sz, int dispsz)
        {
          auto p = msg::data_pay_load::make_payload2(side, oid, sym, en::md::MOD, en::mt::CANC, sz, dispsz, px, tim, venue);
          auto d = make_data(p);
          return d;
        }

        static 
          const msg::Data *
          make_cancd(uint32_t oid, int sym, en::bs side, en::x venue, uint64_t tim, int px)
        {
          auto p = msg::data_pay_load::make_payload2(side, oid, sym, en::md::MOD, en::mt::CANCD, 0, 0, px, tim, venue);
          auto d = make_data(p);
          return d;
        }

        static 
          const msg::Data *
          make_tx(uint32_t oid, int sym, en::bs side, en::x venue, int px, uint64_t tim, int sz)
        {
          auto p = msg::data_pay_load::make_payload2(side, oid, sym, en::md::MOD, en::mt::EXEC, sz, 0, px, tim, venue);
          auto d = make_data(p);
          return d;
        }

        static 
          const msg::Data *
          make_test(int sym, en::bs side, en::x venue, int px, int sz, uint64_t tim)
        {
          auto p = msg::data_pay_load::make_payload2(side, 0, sym, en::md::TEST, en::mt::NONE, sz, 0, px, tim, venue);
          auto d = make_data(p);
          return d;
        }

        static 
          const msg::Data *
          make_clear(int sym, en::x venue, uint64_t tim)
        {
          //auto& sym_s = ref::RefData::inst().get_asset_name(sym);
          auto payload = boost::intrusive_ptr<msg::data_pay_load>(new msg::data_pay_load());
          payload->mkt = venue;
          //strcpy(payload->mkt_str, en::to_string(venue));
          payload->sym = sym;
          //strcpy(payload->sym_str, sym_s.c_str());
          payload->mev = en::md::CLEAR;
          payload->tim = tim;
          auto d = make_data(payload);
          return d;
        }

#endif

      };

    }
  }
}



