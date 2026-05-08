#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <limits>
#include <string>
#include <vector>
//#include <boost/algorithm/string/trim.hpp>
#include <boost/algorithm/string.hpp>
#include "chutil/ut.hpp"
#include "enum/e_names.hpp"
#include "nlohmann/json.hpp"

namespace frame::ref
{
    class RefData;  // Forward declaration

    struct Asset
    {
      private:
            double units = 0; // this is min price increment
            int bo_512 = 0; // this is min price increment * 512
            en::x exchange_trad=en::x::UNI;
            en::x exchange_md = en::x::UNI; // set by market data handler and from universe file
            mutable const Asset* btec_equiv = nullptr;  // cached pointer to btec equivalent asset

      public:

      void set_units(double units_) noexcept
      {
        units = units_;
        bo_512 = units * 512;
      }

      double get_units() const noexcept
      {
        return units;
      }

      Asset()
      {
        name = "not set";
        id = std::numeric_limits<unsigned>::max();
        units = 0;
        _typ = 'X';
        shift = 0;
        maxpx = -1;
        rng_adj = 0;
        max_limit_order_size = 0;
        contract_size = 0;
      }
      char _typ;
      std::string name; // symbol name
      std::string btec_equiv_name;
      unsigned id;
      int bo_spread;
      int shift;            // shift up so that min price is not negative for array lookup
      std::string mnemonic; // eg ZN asset
      int maxpx;
      int rng_adj;
      int max_limit_order_size;
      bool has_book = false; // whether it should be traded
      int contract_size;
      int n_rounding_digits = 0;
      std::string cfi_code; // eg for ZN FFDXSX
      std::string security_group;
      uint16_t cme_issue_date = 0;
      uint64_t cme_activation = 0;
      uint32_t sec_id = 0; // for fenics this is the locate
      uint32_t fenics_instrument_id = 0; // this is the id needed for order entry
      std::string exchange_md_str; // set by market data handler (debug only)
      std::string fenics_instrument_symbol;
      std::string cusip;
      uint64_t fenics_price_multiplier;
      bool is_in_good_trading_state = false;
      bool cme_has_book = false;
      bool currenex_has_book = false;
      bool bloomberg_stream_has_book = false;
      bool dlrweb_has_book = false;
      u_int16_t dlrweb_price_decimals;
      bool is_cusip_asset =false;
      short int currenex_id = 0;
      bool invisible = false; // not to be traded as it is for OB only for fenics global

      // OTR indicatives data from Fenics InstrumentDirectory
      uint32_t issue_date = 0;       // YYYYMMDD
      uint32_t maturity_date = 0;    // YYYYMMDD
      uint32_t settlement_date = 0;  // YYYYMMDD
      uint64_t coupon_rate = 0;      // scaled from Fenics

      // CME-specific fields from FDF/ODF/SDF messages
      double cme_disp_factor = 1.0;           // CME display factor for price conversion
      double cme_min_price_increment = 0.0;   // CME minPriceIncrement (raw)
      double cme_min_cab_price = 0.0;         // CME minCabPrice (options only)
      double cme_strike_price = 0.0;          // Option strike price
      uint8_t cme_put_or_call = 0;            // 0=put, 1=call
      uint8_t cme_maturity_month = 0;         // Contract maturity month
      uint16_t cme_maturity_year = 0;         // Contract maturity year

      Asset(uint _id) noexcept
      {
        id = _id;
        _typ = 'U';
        name = "UNK";
        btec_equiv_name = "UNK";
        mnemonic = "UNK";
        units = 0;
        bo_spread = 0;
        rng_adj = 0;
        maxpx = 0;
        max_limit_order_size = 0;
        has_book = 0;
        n_rounding_digits = 0;
      }
      Asset(uint _id, std::vector<std::string> &init) noexcept
      {
        ASSERTF(init.size() == 14, boost::format("wrong number of cols in universe size()=%d") % init.size());
        id = _id;
        _typ = init[0].c_str()[0];
        name = init[1];
        btec_equiv_name = init[2];
        mnemonic = init[3];
        units = chutil::to_double(init[4]);
        //bo_512 = units * 512;
        bo_spread = chutil::to_int(init[5]);
        rng_adj = chutil::to_int(init[6]);
        maxpx = chutil::to_int(init[7]);
        max_limit_order_size = 0;
        has_book = chutil::to_int(init[8]);
        contract_size = chutil::to_int(init[9]);
        cfi_code = init[10];
        security_group = init[11];
        sec_id = chutil::to_int(boost::algorithm::trim_right_copy(init[12]));
        exchange_md_str = init[13];
        exchange_md = en::x_index_of(exchange_md_str);
      }

      // Constructor from JSON (CME universe format)
      Asset(uint _id, const nlohmann::json& j) noexcept
      {
        id = _id;

        // Map CME type to Asset type
        std::string type = j.value("type", "");
        if (type == "FDF") {
          _typ = 'D';  // CME Future Definition
        } else if (type == "ODF") {
          _typ = 'O';  // CME Option Definition
        } else if (type == "SDF") {
          _typ = 'S';  // CME Spread Definition
        } else {
          _typ = 'U';  // Unknown
        }

        name = j.value("symbol", "");
        btec_equiv_name = j.value("asset", "");
        mnemonic = j.value("asset", "");

        // CME-specific fields
        cme_min_price_increment = j.value("minPriceIncrement", 0.0);
        cme_disp_factor = j.value("dispFactor", 1.0);
        cme_min_cab_price = j.value("minCabPrice", 0.0);
        cme_maturity_month = j.value("maturityMonth", 0);
        cme_maturity_year = j.value("maturityYear", 0);

        // Calculate display tick size
        if (type == "ODF" && cme_min_cab_price > 0) {
          // Options use minCabPrice * dispFactor
          units = cme_min_cab_price * cme_disp_factor;
        } else if (cme_min_price_increment > 0) {
          // Futures/Spreads use minPriceIncrement * dispFactor
          units = cme_min_price_increment * cme_disp_factor;
        } else {
          units = 1.0;  // Default
        }

        // Other fields
        sec_id = j.value("securityID", 0);
        cfi_code = j.value("cfiCode", "");
        security_group = j.value("asset", "");

        // Map venue to exchange
        std::string venue = j.value("venue", "");
        if (venue == "CMEMD" || venue == "G") {
          exchange_md_str = "CMEMDFUT";
          exchange_md = en::x::CMEMDFUT;
        } else {
          exchange_md_str = "CMEMDFUT";  // Default
          exchange_md = en::x::CMEMDFUT;
        }

        // Defaults
        bo_spread = 1;
        rng_adj = 0;
        maxpx = 100000;
        has_book = 1;
        contract_size = 1;
        max_limit_order_size = 0;
        n_rounding_digits = 0;

        // Options-specific
        if (type == "ODF") {
          cme_put_or_call = j.value("putOrCall", 0);
          cme_strike_price = j.value("strikePrice", 0.0);
        }
      }

      char typ() const { return _typ; }
      bool is_bond() const { return _typ == 'B'; }
      bool is_currency() const { return _typ == 'C'; }
      bool is_fut() const { return _typ == 'F'; }
      bool is_1000() const { return _typ == 'K'; }
      bool is_option() const { return _typ == 'O'; }
      bool is_fdf() const { return _typ == 'D'; }  // CME Future Definition Format
      bool is_sdf() const { return _typ == 'S'; }  // CME Spread Definition Format

      auto get_strike() const noexcept
      {
        if (!is_option())
          return std::make_tuple(0, double(0));
        std::vector<std::string> tokens;
        boost::split(tokens, name, boost::is_any_of(" "));
        if (tokens.size() != 2)
          return std::make_tuple(0, double(0));
        auto p_or_c = tokens[1].front();
        if (p_or_c != 'C' && p_or_c != 'P')
          return std::make_tuple(0, double(0));
        auto sig = 0;
        if (p_or_c == 'C')
          sig = 1;
        else
          sig = -1;
        std::string strike_handle = tokens[1].substr(1, 3);
        auto sh = chutil::to_double(strike_handle);
        auto last_char = tokens[1].back();
        double strike_dec = 0;
        switch (last_char)
        {
        case '0':
          strike_dec = 0;
          break;
        case '5':
          strike_dec = 0.5;
          break;
        case '7':
          strike_dec = 0.75;
          break;
        case '2':
          strike_dec = 0.25;
          break;
        default:
          return std::make_tuple(0, double(0));
        }
        auto strike = sh + strike_dec;
        return std::make_tuple(sig, strike);
      }

      int bo512() const noexcept
      {
        ASSERT(units > 0, "units not set");
        return bo_512;
      }

      #ifdef tradingvenueinasset

      bool has_trading_venue() const noexcept
      {
        return exchange_trad != en::x::UNI;
      }

      en::x get_trading_venue() const noexcept
      {
        ASSERT(exchange_trad != en::x::UNI, "exchange_trad not set");
        return exchange_trad;
      }

      bool is_exchange_trad_set() const noexcept
      {
        return exchange_trad != en::x::UNI;
      }

      void set_trading_venue(en::x trad)  noexcept
      {
        if (exchange_trad != trad && exchange_trad != en::x::UNI)
        {
          std::cerr << "ERR: asset " << name << " exchange_trad already set to "
                    << en::to_string(exchange_trad) << ", now setting to "
                    << en::to_string(trad) << std::endl;
          ERR("asset exchange_trad already set");
        }
        exchange_trad = trad;
      }

      #endif

      // todo: this isnt really exchange md, its just reference exchange,
      // it is used in creating hegers for one shot so that the hedger knows
      // where to source md and send orders based on this token
      // sometimes however it is used as exchange md
      en::x get_exchange_md() const noexcept
      {
        ASSERTF(exchange_md != en::x::UNI, boost::format("exchange_md not set for asset: %s") % name);
        return exchange_md;
      }

      bool is_exchange_md_set() const noexcept
      {
        return exchange_md != en::x::UNI;
      }

      void set_exchange_md(en::x md)  noexcept
      {
        if (exchange_md == md)
          return; // already set
        if (exchange_md != en::x::UNI)
        {
          std::cerr << "ERR: asset " << name << " exchange_md already set to "
                    << en::to_string(exchange_md) << ", now setting to "
                    << en::to_string(md) << std::endl;
        }
        ASSERTF(exchange_md == en::x::UNI, boost::format("exchange_md alrady set to: %s, attempting to set to: %s") 
          % en::to_string(exchange_md) % en::to_string(md));
        exchange_md = md;
      }

      void reset_exchange_md(en::x md) noexcept
      {
        exchange_md = md;
      }

      // Forward-declared, implemented after RefData is defined
      inline const Asset* btec_equiv_asset() const noexcept;

    };
}
