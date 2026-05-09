#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <boost/format.hpp>
#include "frame/ref/RefData.hpp"
#include <math.h>

//#define POLONAISE_CHECK_PRICE_VALIDITY

namespace frame
{
  namespace ref
  {
    class Price
    {

      long double pxf;
      int px;
      // below is not const to make assignment operator work
      /*const*/ const Asset* a;

    public:

      const Asset* get_asset() const { return a; }

      Price()
        :px(0),pxf(0)
      {}

      Price(
        int _px,
        const Asset*_a)
        :px(_px),a(_a)
      {
        pxf=px*a->get_units();
        // if (!shifted)
        //   px-=a->shift;
#ifdef POLONAISE_CHECK_PRICE_VALIDITY
        ASSERT(is_valid(), "not valid");
#endif
      }

/*
      Price(
        long double _pxf, 
        const Asset*_a)
        :pxf(_pxf),a(_a)
      {
        px=int(pxf/a->units);
        // if (!shifted)
        //   px-=a->shift;
#ifdef POLONAISE_CHECK_PRICE_VALIDITY
        ASSERT(is_valid(), "not valid");
#endif
      }
      */

     static Price from_double(long double pxf, const Asset*a)
      {
        Price p;
        p.pxf=pxf;
        p.a=a;
        p.px=int(std::round(pxf/a->get_units()+.0000001));
        return p;
      }

      Price(
        int _px,
        uint _asset_id)
        :px(_px),a(RefData::get_asset(_asset_id))
      {
        ASSERT(a,"No such asset");
        pxf=px*a->get_units();
        // if (!shifted)
        //   px-=a->shift;
// #ifdef POLONAISE_CHECK_PRICE_VALIDITY
//         ASSERT(is_valid(), "not valid");
// #endif
      }

      Price(
        long double _pxf,
        uint _asset_id)
        :pxf(_pxf),a(RefData::get_asset(_asset_id))
      {
        ASSERT(a,"No such asset");
        px=int(std::round(pxf/a->get_units()+.0000001));
      }

      static
      auto to_decimal(double px, const Asset*a)
      {
        auto px_d = px*a->get_units();
        return px_d;
      }

      bool is_valid() const
      {
        return (a && px<a->maxpx);
      }

      Price(const Price&) = default;

      explicit operator int() const {return px;}
      explicit operator long double() const {return pxf;}
      explicit operator double() const = delete;

      inline int to_int() const {return px;}
      uint to_uint() const {ASSERT(px>0,"px"); return uint(px);}
      double to_double1() const {return pxf;}
      long double to_double() const {return pxf;}


    private:
      Price operator++(int); // = delete;
      Price operator--(int); // = delete;
    public:

      Price &operator++() 
      {
        px+=a->bo_spread; 
        pxf=(px+a->shift)*a->get_units();
        return *this;
      }

      Price &operator--() 
      {
        px-=a->bo_spread; 
        pxf=(px+a->shift)*a->get_units();
        return *this;
      }

      Price &operator+=(int incr) 
      {
        px+=a->bo_spread*incr;
        pxf=px*a->get_units();
        return *this;
      }

      bool operator>(const Price&p) const 
      {
        ASSERT(a==p.a,"wrong asset");
        return px>p.px;
      }

      bool operator<(const Price&p) const 
      {
        ASSERT(a==p.a,"wrong asset");
        return px<p.px;
      }

      bool operator==(const Price&p) const 
      {
        ASSERT(a==p.a,"wrong asset");
        return px==p.px;
      }

      bool operator<=(const Price&p) const 
      {
        ASSERT(a==p.a,"wrong asset");
        return px<=p.px;
      }

      bool operator>=(const Price&p) const 
      {
        ASSERT(a==p.a,"wrong asset");
        return px>=p.px;
      }

      Price &operator-=(int incr) 
      {
        px-=a->bo_spread*incr; 
        pxf=(px+a->shift)*a->get_units();
        return *this;
      }

      Price operator-(int incr) const 
      {
        Price p(px-a->bo_spread*incr,a); 
        return p;
      }

      Price operator+(int incr) const 
      {
        Price p(px+a->bo_spread*incr,a);
        return p;
      }

      int operator-(const Price&p) const 
      {
        return (px-p.px)/a->bo_spread;
      }

      Price&operator=(const Price&) = default;

      std::string to_string()
      {

        if ((a->is_bond() || a->is_fut()) && pxf==pxf)
          return to_32_string_f(pxf*256);

        return (boost::format("%10.5f")%pxf).str();
      }

      static std::string to_string(long double px,const Asset*a)
      {
        if (px!=px)
          return "nan";
        return Price(px,a).to_string();
      }

      static std::string to_string(int px, const Asset*a)
      {
        return Price(px,a).to_string();
      }

    private:

      static std::string to_32_string_f(double px) // assume price in 256
      {
        auto whole=std::round(px/256);
        auto rem=px-whole*256;
        auto a32=std::round(rem/256*32);
        auto a8=std::round(rem-a32*8);
        auto frac_eights=px-whole*256-a32*8-a8;
        auto eights=a8+frac_eights;
        std::string ret;

        if (whole==0)
        {
          if (a32==0)
          {
            ret = (boost::format("%04.2f") % eights).str();
          }
          else 
          {
            ret = (boost::format("%2d-%04.2f") % a32 % fabs(eights)).str();
          }
        }
        else
          ret = (boost::format("%3d-%02d-%04.2f") % whole % abs(a32) % fabs(eights)).str();
        return ret;
      }
    };
  }
}
