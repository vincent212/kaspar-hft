#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <string>
#include <cstring>
#include <limits>
#include <fstream>
#include <functional>
#include <set>
#include "chutil/Macros.hpp"
#include "actors/Actor.hpp"
#include "bfile/r_l3.hpp"
#include "chutil/Time.hpp"
#include "actors/msg/Start.hpp"
#include "actors/msg/Continue.hpp"
#include "actors/msg/Shutdown.hpp"
#include "frame/mda/msg/Data.hpp"
#include "logger/act/Logger.hpp"
#include "frame/ref/RefData.hpp"
#include "enum/e_names.hpp"
#include "frame/ob/act/OB.hpp"
#include "zlib.h"

namespace frame::mda::act
{

  // No-op factory for default behavior
  // Factory signature: (securityID, asset*, venue, minPriceIncrement, dispFactor)
  struct NoOpFactory {
    actors::Actor* operator()(int, const frame::ref::Asset*, en::x, double, double) const { return nullptr; }
  };

  template<bool TreasOnly = false, typename FactoryFunc = NoOpFactory>
  class BFA : public actors::Actor
  {

    [[maybe_unused]] bool flip_sym_asset = false;
    std::vector<std::unordered_map<int32_t, actor_ptr>> dataAdapter;
    actors::Actor *manager;
    FactoryFunc factory_;
    gzFile file;
    [[maybe_unused]] uint64_t prev_ts = 0;

    std::unordered_map<uint8_t, int> visgroupcnt;

    [[maybe_unused]] double time_warp;
    [[maybe_unused]] int tw_start_h = 0;
    [[maybe_unused]] int tw_start_m = 0;

    // Time filtering for debugging (by hour: 0-23)
    int start_hour = -1;
    int end_hour = -1;
    uint64_t filtered_before_start = 0;
    bool reached_end_time = false;

    const char* get_name() const override { return "BFA"; }

    struct msg_stats_t
    {
      std::unordered_map<int, int> add;
      int trd = 0;
    };

    std::map<en::x, msg_stats_t> msg_stats;

    std::unordered_map<uint64_t, bfile::l3_mbo_v2_t> mbo_unordered_map;
    std::unordered_map<char, std::unordered_map<uint64_t, int>> volume;

    std::ofstream
    som_file,
    fenics_visg_file,
    cnt_file,
    cme_def_file,
    fenics_def_file,
    dw_def_file,
    volume_file,
    qlen_file,
    interval_file;

    std::map<en::x, std::unordered_map<int, std::string>> venue_sec_id_to_name;

    // Optional hooks: invoked for every FDF / ODF definition message as it is read.
    // Default: not set (no-op). Set via set_fdf_callback / set_odf_callback.
    std::function<void(const bfile::l3_fdf_t&)> on_fdf_cb_;
    std::function<void(const bfile::l3_odf_t&)> on_odf_cb_;

  public:
    void set_fdf_callback(std::function<void(const bfile::l3_fdf_t&)> cb) { on_fdf_cb_ = std::move(cb); }
    void set_odf_callback(std::function<void(const bfile::l3_odf_t&)> cb) { on_odf_cb_ = std::move(cb); }

    // Accessor for per-venue, per-secID day traded volume (summed MBO trade qty).
    // Populated throughout the run; finalized by shutdown_handler.
    const std::unordered_map<char, std::unordered_map<uint64_t, int>>& get_volume() const { return volume; }
    const std::map<en::x, std::unordered_map<int, std::string>>& get_sec_id_to_name() const { return venue_sec_id_to_name; }

  public:
    BFA(
        const std::vector<std::unordered_map<int32_t, actor_ptr>> &dataAdapter,
        const std::string &infname,
        actor_ptr _manager,
        FactoryFunc factory = FactoryFunc(),
        bool flip_sym_asset = false,
        double _time_warp = .0,
        int tw_start_h = 0,
        int tw_start_m = 0,
        int start_h = -1,
        int end_h = -1)
        : manager(_manager),
          dataAdapter(dataAdapter),
          factory_(factory),
          time_warp(_time_warp),
          tw_start_h(tw_start_h),
          tw_start_m(tw_start_m),
          flip_sym_asset(flip_sym_asset),
          start_hour(start_h),
          end_hour(end_h)
    {
      file = gzopen(infname.c_str(), "rb");
      ASSERTF(file, boost::format("not open: %s") % infname);
      MESSAGE_HANDLER(actors::msg::Start, start_handler);
      MESSAGE_HANDLER(actors::msg::Shutdown, shutdown_handler);
      MESSAGE_HANDLER(actors::msg::Continue, continue_handler);

      auto initialize_afile = [](const std::string &filename) {
        std::ofstream afile;
        afile.open(filename, std::ios::out | std::ios::trunc);
        ASSERT(afile.is_open(), "Failed to open afile");
        return afile;
      };

      auto som_fname = (boost::format("som%d.out") % getpid()).str();
      som_file=initialize_afile(som_fname);
      som_file << infname << std::endl;

      auto fenics_visg_fname = (boost::format("fenics_visg_%d.out") % getpid()).str();
      fenics_visg_file=initialize_afile(fenics_visg_fname);
      fenics_visg_file << infname << std::endl;

      // Only volume_*.csv is still written; the other per-PID diagnostic
      // CSVs (cnt, cme_def, fenics_def, dw_def, qlen, interval) are disabled.
      // Their ofstream members stay default-constructed — writes to a
      // closed ofstream silently set failbit and create no file.
      auto volume_fname = (boost::format("volume_%d.csv") % getpid()).str();
      volume_file=initialize_afile(volume_fname);
      volume_file << infname << std::endl;

    }

    void start_handler(const actors::msg::Start *) noexcept
    {
      this->send(new actors::msg::Continue());
    }

    void continue_handler(const actors::msg::Continue *) noexcept
    {
      bfile::l3_t l3;
      uint64_t ts; // unused
      if (!bfile::read_l3(file, l3, ts))
      {
        log_inf("end of file");
        manager->terminate();
        return;
      }

      // Time filtering for debugging (by hour: 0-23)
      // Only filter MBO/trade messages, not FDF (which define instruments)
      if (start_hour >= 0 || end_hour >= 0) {
        uint64_t msg_time = 0;
        bool has_time = false;

        if (std::holds_alternative<bfile::l3_mbo_v2_t>(l3)) {
          msg_time = std::get<bfile::l3_mbo_v2_t>(l3).transactTime;
          has_time = true;
        } else if (std::holds_alternative<bfile::l3_mbo_trd_v2_t>(l3)) {
          msg_time = std::get<bfile::l3_mbo_trd_v2_t>(l3).transactTime;
          has_time = true;
        }

        if (has_time && msg_time > 0) {
          // Extract hour from nanosecond timestamp
          auto tim = chutil::Time::from_epoch(msg_time);
          int msg_hour = tim.hour();

          // Skip messages before start hour
          if (start_hour >= 0 && msg_hour < start_hour) {
            filtered_before_start++;
            if (filtered_before_start % 1000000 == 0) {
              std::cerr << "BFA: Filtered " << filtered_before_start << " messages before hour " << start_hour << std::endl;
            }
            this->send(new actors::msg::Continue());
            return;
          }

          // Terminate as soon as we cross end_hour — the .bin file is
          // chronologically ordered so no later message can be in-window.
          // (Assumes start_hour..end_hour does not wrap past midnight; the
          // file-start day is always before any rollover to 00:00.)
          if (end_hour >= 0 && msg_hour >= end_hour) {
            std::cerr << "BFA: reached end_hour " << end_hour
                      << " — terminating early at msg_hour " << msg_hour << std::endl;
            manager->terminate();
            return;
          }
        }
      }

      process(l3);
      this->send(new actors::msg::Continue());
    }

    void shutdown_handler(const actors::msg::Shutdown *) noexcept
    {
      // fenics visgroup stats
      for (auto [gid, cnt] : visgroupcnt)
      {
        fenics_visg_file << "group: " << int(gid) << " cnt: " << cnt << std::endl;
      }
      fenics_visg_file.close();

      // cnt / add stats
      for (auto &[venue, stats] : msg_stats)
      {
        cnt_file << en::to_string(en::x(venue)) <<  "," << stats.trd << std::endl;
        for (auto &[sym, cnt] : stats.add)
        {
          std::string symname = "UNK";
          try
          {
            symname = venue_sec_id_to_name.at(venue).at(sym);
          }
          catch (const std::out_of_range &oor)
          {
            std::cerr << "out of range: " << oor.what() << " venue:" << venue << " sym: " << sym << std::endl;
          }
          catch (const std::exception &e)
          {
            std::cerr << "exception: " << e.what() << " venue:" << venue << " sym: " << sym << std::endl;
          }
          cnt_file << symname << "," << sym << "," << cnt << std::endl;
        }
      }

      // volume
      for (auto &[venue, vol] : volume)
      {
        volume_file << en::to_string(en::x(venue)) << std::endl;
        for (auto &[sym, cnt] : vol)
        {
          std::string symname = "UNK";
          try
          {
            symname = venue_sec_id_to_name.at(venue).at(sym);
          }
          catch (const std::out_of_range &oor)
          {
            std::cerr << "out of range: " << oor.what() << " venue:" << venue << " sym: " << sym << std::endl;
          }
          catch (const std::exception &e)
          {
            std::cerr << "exception: " << e.what() << " venue:" << venue << " sym: " << sym << std::endl;
          }
          volume_file << en::to_string(en::x(venue)) << "," << symname << "," << sym << "," << cnt << std::endl;
        }
      }

      // Only som_file and volume_file were opened; closing a
      // default-constructed ofstream is a no-op but harmless, so we don't
      // bother calling close() on the disabled ones.
      som_file.close();
      volume_file.close();
    }

  private:
    void process(const bfile::l3_t &l3) noexcept
    {

      bool is_fut = false;

      if (std::holds_alternative<bfile::l3_som_t>(l3))
      {
        const auto &som = std::get<bfile::l3_som_t>(l3);
        som_file << som << std::endl;
        return;
      }
      else if (std::holds_alternative<bfile::l3_qlen_t>(l3))
      {
        const auto &qlen = std::get<bfile::l3_qlen_t>(l3);
        qlen_file << qlen << std::endl;
        return;
      }
      else if (std::holds_alternative<bfile::l3_interval_t>(l3))
      {
        const auto &interval = std::get<bfile::l3_interval_t>(l3);
        interval_file << interval << std::endl;
        return;
      }
      else if (std::holds_alternative<bfile::l3_fdf_t>(l3))
      {
        const auto &fdf = std::get<bfile::l3_fdf_t>(l3);
        if (on_fdf_cb_) on_fdf_cb_(fdf);
        // Convert fixed-size char arrays to std::string (they are NOT null-terminated)
        std::string fdf_sym(fdf.sym, strnlen(fdf.sym, sizeof(fdf.sym)));
        std::string fdf_asset(fdf.asset, strnlen(fdf.asset, sizeof(fdf.asset)));
        boost::algorithm::trim(fdf_sym);
        boost::algorithm::trim(fdf_asset);

        std::string sym = fdf_sym;
        if (sym.length() > 4)
        {
          sym = sym.substr(0, 4);
        }

        if (fdf.updateAction == 'A' || fdf.updateAction == 'M')
        {

          if (fdf_asset[0] == '#')
          {
            log_inf("skipping asset starting with #: %s", fdf_asset.c_str());
            return;
          }

          // if asset has substring "WI" skip
          if (fdf_sym.find("WI") != std::string::npos)
          {
            log_inf("skipping asset with WI: %s", fdf_sym.c_str());
            return;
          }

          std::set<std::string> cash_asset = {"UB02", "UB03", "UB05", "UB07", "UB10", "UB20", "UB30"};
          if (cash_asset.find( fdf_asset.substr(0,4) ) != cash_asset.end())
          is_fut = false;
          else
          is_fut = true;

          std::string sym_to_lookup;
          if (!is_fut)
          {
            sym_to_lookup = fdf_asset;
            log_inf("fdf cash asset: %s, sym: %s", fdf_asset.c_str(), fdf_sym.c_str());
          }
          else
          {
            sym_to_lookup = fdf_sym;
            log_inf("fdf fut asset: %s, sym: %s", fdf_asset.c_str(), fdf_sym.c_str());
            std::cerr << "BFA: fdf fut asset=" << fdf_asset << " sym=" << fdf_sym << " securityID=" << fdf.securityID << std::endl;
          }
          boost::algorithm::trim(sym_to_lookup);
          auto a = frame::ref::RefData::inst().get_asset(std::string(sym_to_lookup));
          if (a)
          {
            auto &a_ = const_cast<frame::ref::Asset &>(*a);
            auto &r = const_cast<frame::ref::RefData &>(frame::ref::RefData::inst());
            if (a_.sec_id != 0)
            {
              log_inf("we have already updated this asset sym: %s", sym_to_lookup);
              if (a_.cme_activation > fdf.activation)
              {
                log_inf("activation is older, skipping, current activation: %d, incoming activation: %d", a_.cme_activation, fdf.activation);
                return;
              }
              else
              {
                log_inf("activation is newer, updating, current activation: %d, incoming activation: %d", a_.cme_activation, fdf.activation);
              }
            }
            r.update_sec_id(fdf.securityID, &a_);
            a_.cfi_code = std::string(fdf.cfiCode, sizeof(fdf.cfiCode));
            a_.cme_activation = fdf.activation;
            a_.security_group = std::string(fdf.securityGroup, sizeof(fdf.securityGroup));
            double unit = fdf.minPriceIncrement;
            a_.set_units(unit);
            if constexpr (TreasOnly) {
              a_.maxpx = int(180 / unit);
            }
            a_.bo_spread = 1;
            log_inf("updated asset: %s, secid: %d, cfi: %s, group: %s, unit: %f, bospread: %d, subfraction: %d, mainfraction: %d, minpriceincrement: %f",
                    a_.name, a_.sec_id, std::string(a_.cfi_code), a_.security_group, a_.get_units(), a_.bo_spread, int(fdf.subFraction), int(fdf.mainFraction), fdf.minPriceIncrement);
            std::cerr << "BFA: updated asset: " << a_.name << ", id: " << a_.id << ", secid: " << a_.sec_id << ", cfi: " << std::string(a_.cfi_code) << ", group: " << a_.security_group
                      << ", unit: " << a_.get_units() << ", bospread: " << a_.bo_spread << ", subfraction: " << int(fdf.subFraction)
                      << ", mainfraction: " << int(fdf.mainFraction) << ", minpriceincrement: " << fdf.minPriceIncrement << std::endl;
            ASSERT(a_.name == std::string(sym_to_lookup), "sym mismatch");
            ASSERT(a_.sec_id == uint32_t(fdf.securityID), "secid mismatch");
            cme_def_file << a_.name << "," << a_.sec_id  << std::endl;

            // Create actor for this security ID — only if not already pre-created
            // from the master universe (which populates dataAdapter up front).
            en::x venue = en::x(fdf.venue);
            if (venue.value < dataAdapter.size() &&
                dataAdapter[venue.value].count(fdf.securityID) == 0) {
              if (auto actor = factory_(a_.id, &a_, venue, fdf.minPriceIncrement, fdf.dispFactor)) {
                dataAdapter[venue.value][fdf.securityID] = actor;
                std::cerr << "BFA: Factory created actor for secID=" << fdf.securityID
                          << " asset=" << a_.name << " venue=" << en::to_string(venue) << std::endl;
              }
            }

            if (is_fut)
              venue_sec_id_to_name[en::x::CMEMDFUT][a_.sec_id] = a_.name;
            else
              venue_sec_id_to_name[en::x::CMEMD][a_.sec_id] = a_.name;
          }
          else
          {
            // Asset not in RefData - create it dynamically from FDF
            en::x venue = en::x(fdf.venue);

            std::string cfi(fdf.cfiCode, strnlen(fdf.cfiCode, sizeof(fdf.cfiCode)));
            std::string secgroup(fdf.securityGroup, strnlen(fdf.securityGroup, sizeof(fdf.securityGroup)));
            boost::algorithm::trim(cfi);
            boost::algorithm::trim(secgroup);

            // actual tick size = minPriceIncrement * dispFactor
            double actual_tick = fdf.minPriceIncrement * fdf.dispFactor;

            auto new_asset = frame::ref::RefData::add_future_asset(
                sym_to_lookup,
                venue,
                fdf.securityID,
                cfi,
                secgroup,
                actual_tick   // store the real tick size (e.g. 0.25), not the native one (25)
            );

            cme_def_file << new_asset->name << "," << new_asset->sec_id << std::endl;

            if (is_fut)
              venue_sec_id_to_name[en::x::CMEMDFUT][fdf.securityID] = sym_to_lookup;
            venue_sec_id_to_name[en::x::CMEMD][fdf.securityID] = sym_to_lookup;

            std::cerr << "BFA: FDF for new symbol " << sym_to_lookup
                      << " (secID=" << fdf.securityID
                      << " assetID=" << new_asset->id
                      << " tick=" << actual_tick
                      << ") - created Asset" << std::endl;

            // Create actor using the new asset ID — only if not pre-created.
            if (venue.value < dataAdapter.size() &&
                dataAdapter[venue.value].count(fdf.securityID) == 0) {
              if (auto actor = factory_(new_asset->id, new_asset, venue, fdf.minPriceIncrement, fdf.dispFactor)) {
                dataAdapter[venue.value][fdf.securityID] = actor;
                std::cerr << "BFA: Factory created actor for secID=" << fdf.securityID
                          << " symbol=" << sym_to_lookup
                          << " assetID=" << new_asset->id
                          << " venue=" << en::to_string(venue) << std::endl;
              }
            }
          }
        }
      }
      else if (std::holds_alternative<bfile::l3_odf_t>(l3))
      {
        const auto &odf = std::get<bfile::l3_odf_t>(l3);
        if (on_odf_cb_) on_odf_cb_(odf);
        // Convert fixed-size char arrays to std::string (they are NOT null-terminated).
        // CME pads short symbols with non-printable bytes (e.g. 0x06) — strip them
        // so the log output isn't corrupted by control chars.
        auto sanitize_field = [](const char* buf, size_t n) {
          std::string s(buf, strnlen(buf, n));
          s.erase(std::remove_if(s.begin(), s.end(),
                                 [](unsigned char c){ return c < 0x20 || c > 0x7E; }),
                  s.end());
          boost::algorithm::trim(s);
          return s;
        };
        std::string odf_sym   = sanitize_field(odf.sym,   sizeof(odf.sym));
        std::string odf_asset = sanitize_field(odf.asset, sizeof(odf.asset));
        std::string sym = odf_sym;

        log_inf("BFA: ODF message: sym=%s asset=%s securityID=%d updateAction=%c putOrCall=%d strikePrice=%.2f expiration=%lu",
                odf_sym.c_str(), odf_asset.c_str(), odf.securityID, odf.updateAction, int(odf.putOrCall), odf.strikePrice, odf.expiration);
        std::cerr << "BFA: ODF message: sym=" << odf_sym << " asset=" << odf_asset
                  << " securityID=" << odf.securityID
                  << " updateAction=" << odf.updateAction
                  << " putOrCall=" << int(odf.putOrCall)
                  << " strikePrice=" << odf.strikePrice
                  << " expiration=" << odf.expiration
                  << std::endl;

        if (odf.updateAction == 'A' || odf.updateAction == 'M')
        {
          std::string sym_to_lookup = sym;
          boost::algorithm::trim(sym_to_lookup);

          auto a = frame::ref::RefData::inst().get_asset(sym_to_lookup);
          if (a)
          {
            // Asset exists in RefData - update it
            auto &a_ = const_cast<frame::ref::Asset &>(*a);
            auto &r = const_cast<frame::ref::RefData &>(frame::ref::RefData::inst());
            r.update_sec_id(odf.securityID, &a_);

            double unit = odf.minPriceIncrement;
            a_.set_units(unit);
            a_.bo_spread = 1;

            log_inf("BFA: updated asset from ODF: %s, secid: %d, unit: %f", a_.name, a_.sec_id, unit);
            std::cerr << "BFA: updated asset from ODF: " << a_.name << ", secid: " << a_.sec_id
                      << ", unit: " << unit << std::endl;

            cme_def_file << a_.name << "," << a_.sec_id << std::endl;

            // Create actor for this security ID — only if not pre-created.
            en::x venue = en::x(odf.venue);
            if (venue.value < dataAdapter.size() &&
                dataAdapter[venue.value].count(odf.securityID) == 0) {
              if (auto actor = factory_(a_.id, &a_, venue, odf.minPriceIncrement, odf.dispFactor)) {
                dataAdapter[venue.value][odf.securityID] = actor;
                log_inf("BFA: Factory created actor from ODF for secID=%d asset=%s venue=%s",
                        odf.securityID, a_.name, en::to_string(venue));
                std::cerr << "BFA: Factory created actor from ODF for secID=" << odf.securityID
                          << " asset=" << a_.name << " venue=" << en::to_string(venue) << std::endl;
              }
            }

            venue_sec_id_to_name[en::x::CMEMD][a_.sec_id] = a_.name;
          }
          else
          {
            // Asset not in RefData - CREATE Asset dynamically
            en::x venue = en::x(odf.venue);
            venue_sec_id_to_name[en::x::CMEMD][odf.securityID] = sym_to_lookup;

            log_inf("BFA: ODF for new option %s (secID=%d) - creating Asset dynamically", sym_to_lookup, odf.securityID);
            std::cerr << "BFA: ODF for new option " << sym_to_lookup
                      << " (secID=" << odf.securityID << ") - creating Asset dynamically" << std::endl;

            // Create dynamic Asset in RefData using add_option_asset
            double odf_actual_tick = odf.minPriceIncrement * odf.dispFactor;
            auto new_asset = frame::ref::RefData::add_option_asset(
                sym_to_lookup,           // symbol
                venue,                   // exchange
                odf.securityID,          // security_id
                "",                      // cfi_code (empty for now)
                odf_asset,               // security_group (use underlying asset)
                odf_actual_tick          // actual tick size (minPriceIncrement * dispFactor)
            );

            cme_def_file << new_asset->name << "," << new_asset->sec_id << std::endl;

            log_inf("BFA: Created dynamic Asset: id=%d name=%s secID=%d", new_asset->id, new_asset->name, new_asset->sec_id);
            std::cerr << "BFA: Created dynamic Asset: id=" << new_asset->id
                      << " name=" << new_asset->name << " secID=" << new_asset->sec_id << std::endl;

            // Now create actor using asset ID — only if not pre-created.
            if (venue.value < dataAdapter.size() &&
                dataAdapter[venue.value].count(odf.securityID) == 0) {
              if (auto actor = factory_(new_asset->id, new_asset, venue, odf.minPriceIncrement, odf.dispFactor)) {
                dataAdapter[venue.value][odf.securityID] = actor;
                log_inf("BFA: Factory created actor from ODF for secID=%d asset=%s (assetID=%d) venue=%s",
                        odf.securityID, new_asset->name, new_asset->id, en::to_string(venue));
                std::cerr << "BFA: Factory created actor from ODF for secID=" << odf.securityID
                          << " asset=" << new_asset->name << " (assetID=" << new_asset->id << ") venue=" << en::to_string(venue) << std::endl;
              }
            }
          }
        }
      }
      else if (std::holds_alternative<bfile::l3_fenics_bdf_t>(l3))
      {
        const auto &bdf = std::get<bfile::l3_fenics_bdf_t>(l3);
        std::string enhancedSymbol(bdf.EnhancedSymbol, sizeof(bdf.EnhancedSymbol));
        boost::algorithm::trim_right(enhancedSymbol);
        auto a = const_cast<frame::ref::Asset *>(frame::ref::RefData::get_asset(enhancedSymbol));
        auto &r = const_cast<frame::ref::RefData &>(frame::ref::RefData::inst());
        if (a)
        {
          // sym in universe
          ASSERT(bdf.DecimalPriceTick == bdf.FractionalPriceTick, "m.DecimalPriceTick != m.FractionalPriceTick");
          double unit = bdf.DecimalPriceTick / double(bdf.PriceMultiplier);
          r.update_sec_id(bdf.InstrumentLocate, a);
          a->fenics_instrument_id = bdf.InstrumentId;
          a->set_units(unit);
          a->bo_spread = 1;
          if constexpr (TreasOnly) {
            a->maxpx = int(180 / unit);
          }
          a->cusip = std::string(bdf.IndustryIdentifier, sizeof(bdf.IndustryIdentifier));
          a->fenics_price_multiplier = bdf.PriceMultiplier;
          boost::format f("name: %s, fenics sym: %s, unit: %f, industry identifier: %s, locate: %d, id: %d");
          f % a->name % enhancedSymbol % unit % bdf.IndustryIdentifier % bdf.InstrumentLocate % bdf.InstrumentId;
          std::cout << f << std::endl;
          log_inf("%s", f.str());
          fenics_def_file << a->name << "," << a->sec_id << std::endl;
          venue_sec_id_to_name[en::x::FENICS][a->sec_id] = a->name;
        }
        else
        {
          // cusip sym
          auto a_cusip = frame::ref::RefData::add_cusip_asset(en::x::FENICS, std::string(bdf.IndustryIdentifier, sizeof(bdf.IndustryIdentifier)));
          ASSERT(bdf.DecimalPriceTick == bdf.FractionalPriceTick, "m.DecimalPriceTick != m.FractionalPriceTick");
          double unit = bdf.DecimalPriceTick / double(bdf.PriceMultiplier);
          a_cusip->fenics_instrument_id = bdf.InstrumentId;
          a_cusip->sec_id = bdf.InstrumentLocate;
          a_cusip->set_units(unit);
          if constexpr (TreasOnly) {
            a_cusip->maxpx = int(180 / unit);
          }
          a_cusip->bo_spread = 1;
          a_cusip->cusip = std::string(bdf.IndustryIdentifier, sizeof(bdf.IndustryIdentifier));
          a_cusip->fenics_price_multiplier = bdf.PriceMultiplier;
          boost::format f("added cusip asset name: %s, fenics sym: %s, unit: %f, industry identifier: %s, locate: %d, id: %d");
          f % a_cusip->name.c_str() % enhancedSymbol % unit % bdf.IndustryIdentifier % bdf.InstrumentLocate % bdf.InstrumentId;
          std::cout << f << std::endl;
          log_inf("%s", f.str());
          fenics_def_file << a_cusip->name << "," << a_cusip->sec_id << std::endl;
          venue_sec_id_to_name[en::x::FENICS][a_cusip->sec_id] = a_cusip->name;
        }
      }
      else if (std::holds_alternative<bfile::l3_dealerweb_bookdir_t>(l3))
      {
        const auto &bookdir = std::get<bfile::l3_dealerweb_bookdir_t>(l3);
        std::string sym(bookdir.Symbol, sizeof(bookdir.Symbol));
        boost::algorithm::trim_right(sym);
        double unit = bookdir.PriceTickSize / std::pow(10, bookdir.PriceDecimals);
        auto a = const_cast<frame::ref::Asset *>(frame::ref::RefData::get_asset(sym));
        auto &r = const_cast<frame::ref::RefData &>(frame::ref::RefData::inst());
        if (a)
        {
          // sym in universe
          r.update_sec_id(bookdir.OrderBookID, a);
          a->set_units(unit);
          if constexpr (TreasOnly) {
            a->maxpx = int(180 / unit);
          }
          a->cusip = std::string(bookdir.CUSIP, 9);
          a->dlrweb_price_decimals = bookdir.PriceDecimals;
          log_inf("asset: %s, desc: %s, cusip: %s", a->name, bookdir.SecurityDescription, std::string(bookdir.CUSIP, sizeof(bookdir.CUSIP)));
          dw_def_file << a->name << "," << a->sec_id << std::endl;
          venue_sec_id_to_name[en::x::DEALERWEB][a->sec_id] = a->name;
        }
        else
        {
          // cusip sym
          auto a_cusip = frame::ref::RefData::add_cusip_asset(en::x::DEALERWEB, std::string(bookdir.CUSIP, sizeof(bookdir.CUSIP)));
          a_cusip->sec_id = bookdir.OrderBookID;
          a_cusip->set_units(unit);
          if constexpr (TreasOnly) {
            a_cusip->maxpx = int(180 / unit);
          }
          a_cusip->cusip = std::string(bookdir.CUSIP, sizeof(bookdir.CUSIP));
          a_cusip->dlrweb_price_decimals = bookdir.PriceDecimals;
          log_inf("added cusip asset: %s, desc: %s, cusip: %s", a_cusip->name, bookdir.SecurityDescription, std::string(bookdir.CUSIP, sizeof(bookdir.CUSIP)));
          dw_def_file << a_cusip->name << "," << a_cusip->sec_id << std::endl;
          venue_sec_id_to_name[en::x::DEALERWEB][a_cusip->sec_id] = a_cusip->name;
        }
      }

#ifdef TIMEWARP
      if (time_warp > 0)
      {
        if (std::holds_alternative<bfile::l3_mbo_v2_t>(l3))
        {
#ifdef PROPWARP
          auto &mbo = std::get<bfile::l3_mbo_v2_t>(l3);
          if (time_warp > 0 && mbo.sendingTime > 0)
          {
            if (prev_ts > 0)
            {
              auto tim = chutil::Time::from_epoch(mbo.sendingTime);
              bool tw_on =
                  ((tim.hour() < 17 + 5) && (tim.hour() > tw_start_h)) ||
                  (tim.hour() == tw_start_h && tim.minute() >= tw_start_m);
              if (tw_on)
              {
                int diff = ((mbo.sendingTime - prev_ts) / 1000LL);
                diff *= time_warp;
                auto maxdiff = 10000000;
                if (diff > maxdiff)
                {
                  diff = maxdiff;
                }
                if (diff > 0)
                  usleep(diff);
              }
            }
            prev_ts = mbo.sendingTime;
          }
#else
// #define USESLEEP
#ifdef USESLEEP
          usleep(300);
#endif
#endif
        }
      }
#endif

      if (std::holds_alternative<bfile::l3_mbo_v2_t>(l3))
      {
        auto &mbo = std::get<bfile::l3_mbo_v2_t>(l3);
        auto venue = mbo.venue;
        auto secid = mbo.securityID;
        auto visgroup = mbo.visibility_group;
        visgroupcnt[visgroup]++;
        auto update_action = mbo.orderUpdateAction;
        if (update_action == 0)
        {
          auto p = msg_stats.find(venue);
          if (p == msg_stats.end())
          {
            msg_stats[venue] = msg_stats_t();
            p = msg_stats.begin();
          }
          auto p2 = p->second.add.find(secid);
          if (p2 == p->second.add.end())
          {
            p->second.add[secid] = 1;
          }
          else
            p->second.add[secid]++;
        }
        // orderUpdateAction: 0=ADD, 1=MODIFY, 2=DELETE (CME MDP3).
        // We only keep the entry for the duration the order is live so MBO
        // trade messages can look up the originating securityID. Without the
        // erase on DELETE this map grows unboundedly (~96 B/node × millions
        // of orderIDs per day = multi-GB leak).
        if (update_action == 2) {
          mbo_unordered_map.erase(mbo.orderID);
        } else {
          mbo_unordered_map[mbo.orderID] = mbo;
        }
      }
      else if (std::holds_alternative<bfile::l3_mbo_trd_v2_t>(l3))
      {
        auto &mbot = std::get<bfile::l3_mbo_trd_v2_t>(l3);
        auto venue = mbot.venue;
        msg_stats[venue].trd++;

        auto p0 = mbo_unordered_map.find(mbot.orderID);
        if (p0 == mbo_unordered_map.end())
        {
          return;
        }

        auto &mbo = p0->second;
        auto secid = mbo.securityID;

        if (volume.find(venue) == volume.end())
        {
          volume[venue] = std::unordered_map<uint64_t, int>();
        }

        auto pv = volume.find(venue);
        if (pv->second.find(secid) == pv->second.end())
        {
          volume[venue][secid] = 0;
        }

        pv->second[secid] += mbot.lastQty;
      }

      auto hdr = reinterpret_cast<const bfile::l3_hdr_t *>(&l3);
      ASSERT(hdr->venue != en::x::UNI, "uni venue");
      en::x dest_venue = hdr->venue;
      auto ts0 = chutil::Time::get_ts();
      static std::set<int> venues_seen;
      if (venues_seen.find(hdr->venue) == venues_seen.end()) {
        venues_seen.insert(hdr->venue);
        std::cerr << "BFA: NEW venue seen: " << int(hdr->venue) << " (" << en::to_string(dest_venue) << ")"
                  << " dataAdapter[" << dest_venue.value << "].size()=" << dataAdapter[dest_venue.value].size()
                  << std::endl;
      }

      // Do NOT forward security-definition variants (FDF=11, ODF=12, SDF=13) to book
      // subscribers — BFA handles them itself above, and broadcasting them to every
      // TachBook2 just causes N "unknown message type" hits per definition.
      // On option-heavy channels (e.g. 311) this was dominating CPU.
      {
        auto idx = l3.index();
        if (idx == 11 || idx == 12 || idx == 13) {
          return;
        }
      }

      // Fast path: MBO messages carry a securityID, so dispatch directly to the
      // one book that owns that ID via the dataAdapter map (O(1) lookup).
      // All other message types (trades have orderID but no securityID,
      // channel-reset, gap, eob) must broadcast to every book.
      if (std::holds_alternative<bfile::l3_mbo_v2_t>(l3)) {
        const auto &mbo = std::get<bfile::l3_mbo_v2_t>(l3);
        auto &bucket = dataAdapter[uint(dest_venue)];
        auto it = bucket.find(mbo.securityID);
        if (it != bucket.end() && it->second) {
          auto msg = new frame::mda::msg::Data();
          msg->ts0 = ts0;
          msg->l3 = l3;
          msg->israw = true;
          it->second->send(msg, this);
        }
      } else {
        // Broadcast path: trades + resets + gaps + EOB etc.
        for (auto& [sec_id, a] : dataAdapter[uint(dest_venue)])
        {
          if (a) {
            auto msg = new frame::mda::msg::Data();
            msg->ts0 = ts0;
            msg->l3 = l3;
            msg->israw = true;
            a->send(msg, this);
          }
        }
      }

    } // process

    void init() override {};
    void end() override {};
  };
}
