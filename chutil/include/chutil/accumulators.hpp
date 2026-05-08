#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <tuple>
#include "enum/buy_sell.hpp"
#include "boost/circular_buffer.hpp"
#include <sstream>


struct vwap_acc {

  boost::circular_buffer<std::tuple<int,uint32_t,uint64_t,int64_t>> px;
  double px_sz_sum=0;
  int64_t sz_sum=0;

  vwap_acc(std::size_t n) { px.set_capacity(n); }

  double acc(int _px,uint32_t _sz,uint64_t ns) {
    uint64_t mult=_px*_sz;
    if(px.full()) {
      const auto &f=px.front();
      auto sz0=std::get<1>(f);
      auto pxsz0=std::get<2>(f);
      px_sz_sum-=pxsz0;
      sz_sum-=sz0;
      px_sz_sum+=mult;
      sz_sum+=_sz;
    } else {
      px_sz_sum+=_px*_sz;
      sz_sum+=_sz;
    }
    px.push_back(std::make_tuple(_px,_sz,mult,ns));
    return px_sz_sum/sz_sum;
  }

  std::string to_string() {
    std::stringstream ss;
    for(std::size_t i=0;i<px.size();i++) {
      ss<<std::get<3>(px[i])<<": "
        <<std::get<0>(px[i])<<" "
        <<std::get<1>(px[i])<<std::endl;
    }
    return ss.str();
  }

};

struct presh_acc {

  presh_acc(uint32_t n) { orders.set_capacity(n); }

  int32_t actb_sum=0,acta_sum=0;
  int32_t bcnt_sum=0,acnt_sum=0;

  boost::circular_buffer<std::tuple<en::bs,int32_t>> orders;
  
  std::tuple<double,double> acc_addcanc(bool isadd, en::bs _side,int32_t _sz) {
    
    if(orders.full()) {
      const auto &ord=orders.front();
      auto side=std::get<0>(ord);

      // remove old
      if(side==en::bs::BUY) {
        actb_sum--;
        if(isadd) {
          bcnt_sum--;
        } else {
          bcnt_sum++;
        }
      } else {
        acta_sum--;
        if(isadd) {
          acnt_sum--;
        } else {
          acnt_sum++;
        }
      }
    }

    // accumulate
    if(_side==en::bs::BUY) {
      actb_sum++;
      if(isadd) {
        bcnt_sum++;
      } else {
        bcnt_sum--;
      }
    } else {
      acta_sum++;
      if(isadd) {
        acnt_sum++;
      } else {
        acnt_sum--;
      }
    }

    orders.push_back(std::make_tuple(_side,_sz));
    return std::make_tuple(
      (double(actb_sum-acta_sum)/orders.size()),
      (double(bcnt_sum-acnt_sum)/orders.size())
    );
  }
};

struct rng_acc {

  rng_acc(std::size_t n) { px.set_capacity(n); }

  uint32_t num_max=0, num_min=0;
  boost::circular_buffer<std::tuple<int,uint64_t>> px;
  bool just_loaded=true;
  int mn=std::numeric_limits<int>::max();
  int mx=std::numeric_limits<int>::min();

  std::string to_string() const {
    std::stringstream ss;
    ss<<"mn: "<<mn<<" num: "<<num_min<<std::endl;
    ss<<"mx: "<<mx<<" num: "<<num_max<<std::endl;
    for(const auto &xx:px) {
      ss<<std::get<1>(xx)<<" "<<std::get<0>(xx)<<std::endl;
    }
    return ss.str();
  }

  void rescan() {
    mn=std::numeric_limits<int>::max();
    mx=std::numeric_limits<int>::min();
    num_min=num_max=0;
    auto p=px.begin();
    while (p!=px.end())
    {
      const auto &xx=*p;
      auto x=std::get<0>(xx);
      if(x>mx) {
        mx=x;num_max=0;
      }
      if(x<mn) {
        mn=x;num_min=0;
      }
      if(x==mx)
        num_max++;
      if(x==mn) 
        num_min++;
      ++p;
    }
  }

  std::tuple<int,int> get() const {
    return std::make_tuple(mn,mx);
  }

  std::tuple<int, int> acc(int _px, uint64_t ns) {

    if(px.full()) {
      if(just_loaded) {
        just_loaded=false;
        px.push_back(std::make_tuple(_px,ns));
        rescan();
        return std::make_tuple(mn,mx);
      }
      if(_px>mx) {
        mx=_px;num_max=0;
      }
      if(_px<mn) {
        mn=_px;num_min=0;
      }
      if(_px==mx) 
        num_max++;
      if(_px==mn) 
        num_min++;
      auto f=std::get<0>(px.front());
      if(f==mx) 
        num_max>0?num_max--:num_max;
      if(f==mn) 
        num_min>0?num_min--:num_min;

      px.push_back(std::make_tuple(_px,ns));
      if(num_max==0||num_min==0)
        rescan();

//#define DBGACC
#ifdef DBGACC

      std::cout<<"---------------------\n";
      std::cout<<mn<<": "<<num_min<<std::endl;
      std::cout<<mx<<": "<<num_max<<std::endl;
      std::cout<<to_string()<<std::endl;

      //auto sve_mn=mn,sve_mx=mx;
      //if(sve_mn==31652&&sve_mx==31660&&num_min==1&&num_max==11) {
      //  std::cout<<"bug\n";
      //}
      //rescan();
      //ASSERT(sve_mx==mx&&sve_mn==mn,"rescan");

#endif

    } else {
      px.push_back(std::make_tuple(_px,ns));
    }

    return std::make_tuple(mn,mx);
  }

};




#ifdef UNUSED

struct trade_side_acc {

  chutil::cb<std::tuple<bool,uint32_t>,8> trade;

  // call this every time there is a trade
  void acc(bool tak,uint32_t ms) {
    trade.prepend(std::make_tuple(tak,ms));
  }

  // pass in true to test for takes
  bool last_n_th(bool target, uint32_t n,uint32_t currms, uint32_t tlim) {
    auto idx=trade.begin;
    ASSERT(n<=trade.size(),"n");
    ASSERT(trade.full(),"not full yet");
    for(std::size_t i=0; i<n; i++) {
      const auto &t=trade.arr[idx];
      auto tak=std::get<0>(t);
      if(tak!=target) {
        return false;
      }
      auto ts=std::get<1>(t);
      auto tdiff=currms-ts;
      if(tdiff>tlim) {
        return false;
      }
      trade.incr_ptr(idx);
    }
  }

};

// this may not offer a speedup
struct trade_idx_acc {
  //
  // TODO: this is not an accumulator, it just uses
  // trade_side_acc 
  // 
};

#endif

