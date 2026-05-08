#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <boost/format.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <unordered_map>
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <set>
#include <fstream>
#include "frame/ref/Asset.hpp"
#include "chutil/CSVFileReader.hpp"
#include "enum/e_names.hpp"
#include "nlohmann/json.hpp"
#include <zlib.h>
#include <map>
// std::flat_map is C++23 but not yet supported by clang, use std::map as fallback
#if __has_include(<flat_map>) && defined(__cpp_lib_flat_map)
#include <flat_map>
#else
namespace std {
  template<typename K, typename V>
  using flat_map = std::map<K, V>;
}
#endif

namespace frame::ref
{
  class RefData
  {
    RefData(
        const std::string &fname,
        const std::string &rounding_info_fname,
        const std::string &alias_fname) noexcept
    {

      std::unique_lock<std::shared_mutex> lock(mtx);

      id_to_asset.fill(nullptr);

      // read rounding info
      std::map<std::string, int> rounding_info;
      if (rounding_info_fname.size())
      {
        auto lines =
            *chutil::CSVFileReader::readFile(rounding_info_fname, ",");
        for (auto &line : lines)
        {
          auto asset = line[0];
          int n_digits = chutil::to_int(line[1]);
          rounding_info[asset] = n_digits;
        }
      }
      else
      {
        // ERR("no rounding info file");
        std::cerr << "no rounding info file, continuing" << std::endl;
      }

      if (fname.size())
      {
        // Check if file is JSON format (.json or .json.gz)
        bool is_json = (fname.find(".json") != std::string::npos);

        if (is_json)
        {
          // Read JSON universe file
          std::cerr << "Loading JSON universe: " << fname << std::endl;

          // Read file (handle .gz compression)
          std::string json_str;
          if (fname.find(".gz") != std::string::npos) {
            // Gzipped JSON
            gzFile gzf = gzopen(fname.c_str(), "rb");
            if (!gzf) {
              std::cerr << "ERROR: Failed to open gzipped JSON file: " << fname << std::endl;
              return;
            }
            char buffer[8192];
            int bytes_read;
            while ((bytes_read = gzread(gzf, buffer, sizeof(buffer))) > 0) {
              json_str.append(buffer, bytes_read);
            }
            gzclose(gzf);
          } else {
            // Regular JSON
            std::ifstream ifs(fname);
            if (!ifs) {
              std::cerr << "ERROR: Failed to open JSON file: " << fname << std::endl;
              return;
            }
            json_str.assign((std::istreambuf_iterator<char>(ifs)),
                           std::istreambuf_iterator<char>());
          }

          // Parse JSON
          nlohmann::json j = nlohmann::json::parse(json_str);

          // Create asset 0 (unknown)
          Asset *a0 = new Asset(get_next_id());
          id_to_asset[a0->id] = a0;
          name_to_asset[a0->name] = a0;

          // Load instruments from JSON
          num_sym = 1;  // Start at 1 (asset 0 already created)
          for (auto& inst : j["instruments"]) {
            Asset *a = new Asset(get_next_id(), inst);
            id_to_asset[a->id] = a;

            // Check for duplicates
            if (name_to_asset.find(a->name) != name_to_asset.end()) {
              std::cerr << "WARNING: duplicate asset " << a->name << ", skipping" << std::endl;
              continue;
            }

            name_to_asset[a->name] = a;
            sec_id_to_asset[a->sec_id] = a;

            // Apply rounding info if available
            auto p = rounding_info.find(a->mnemonic);
            if (p != rounding_info.end()) {
              a->n_rounding_digits = p->second;
            }

            num_sym++;
          }

          std::cerr << "Loaded " << (num_sym - 1) << " instruments from JSON" << std::endl;
        }
        else
        {
          // CSV format (existing code)
          auto lines =
              *chutil::CSVFileReader::readFile(fname, ",");
          num_sym = lines.size() + 1;
          for (std::size_t i = 0; i < num_sym; i++)
          {
            // asset ids start at 1, in data 0 indicates unknown
            Asset *a;
            if (i == 0)
            {
              a = new Asset(get_next_id());
            }
            else
            {
              a = new Asset(get_next_id(), lines[i - 1]);
            }
            id_to_asset[a->id] = a;
            ASSERTF(name_to_asset.find(a->name) == name_to_asset.end(), boost::format("asset %s already exists") % a->name);
            name_to_asset[a->name] = a;
            sec_id_to_asset[a->sec_id] = a;
            auto p = rounding_info.find(a->mnemonic);
            if (p != rounding_info.end())
            {
              a->n_rounding_digits = p->second;
            }
            else
            {
              if (i > 0)
              {
                std::cerr << "no rounding info for: " << a->name << std::endl;
                // ERRF(boost::format("no rounding info for: %s") % a->name);
              }
            }
          }
        }
      }

      if (alias_fname.size())
      {
        auto lines =
            *chutil::CSVFileReader::readFile(alias_fname, ",");
        for (auto &line : lines)
        {
          auto asset = line[0];
          auto p = name_to_asset.find(asset);
          if (p == name_to_asset.end())
          {
            std::cerr << "alias for unknown asset " << asset << std::endl;
            ERR("alias for unknown asset");
          }
          auto a = p->second;
          for (std::size_t i = 2; i < line.size(); i++)
          {
            auto alias = line[i];
            auto p = name_to_asset.find(alias);
            if (p != name_to_asset.end())
            {
              std::cerr << "alias already exists " << alias << std::endl;
              ERR("alias alrfady exists");
            }
            name_to_asset[alias] = a;
          }
        }
      }
    };

    static RefData *theRefData;
    using id_to_asset_t = std::array<Asset *, 100000>;
    id_to_asset_t id_to_asset;
    std::flat_map<std::string, const Asset *> name_to_asset;
    std::flat_map<uint32_t, const Asset *> sec_id_to_asset;
    std::flat_map<en::x, int> minimum_order_size;

    std::size_t num_sym;
    static std::string universe_fname, rounding_info_fname, alias_fname;

    static const RefData &init()
    {
      theRefData = new RefData(universe_fname, rounding_info_fname, alias_fname);
      return *theRefData;
    }

    static std::atomic<int> asset_id;

    static int get_next_id()
    {
      return asset_id++;
    }

    static std::shared_mutex mtx;
    static std::flat_map<std::pair<std::string, en::x>, const Asset*> sector_exchange_cache;
    static std::flat_map<std::pair<en::x, std::string>, const Asset*> exchange_sector_to_asset;

  public:
    static Asset *add_cusip_asset(en::x exchange, const std::string &cusip)
    {
      std::unique_lock<std::shared_mutex> lock(mtx);
      auto nam = std::string(en::to_string(exchange)) + "." + cusip;
      boost::algorithm::trim(nam);
      auto cusip_trim = cusip;
      boost::algorithm::trim(cusip_trim);
      auto p = theRefData->name_to_asset.find(nam);
      if (p != theRefData->name_to_asset.end())
      {
        // ERRF(boost::format("asset %s already exists") % nam);
        return const_cast<Asset *>(p->second);
      }
      auto a = new Asset(get_next_id());
      a->name = nam;
      a->set_exchange_md(exchange);
      a->cusip = cusip_trim;
      a->mnemonic = cusip_trim;
      a->bo_spread = 1;
      a->has_book = 0;
      a->maxpx = 0;
      a->max_limit_order_size = 0;
      a->contract_size = 0;
      a->is_cusip_asset = true;
      a->n_rounding_digits = 9;
      a->exchange_md_str = en::to_string(exchange);
      theRefData->id_to_asset[a->id] = a;
      theRefData->name_to_asset[a->name] = a;
      theRefData->sec_id_to_asset[a->sec_id] = a;
      return a;
    }

    /**
     * Dynamically add an option asset to RefData (for options not in universe.csv)
     * Called when we receive ODF messages for options
     */
    static Asset *add_option_asset(
        const std::string &symbol,
        en::x exchange,
        int32_t security_id,
        const std::string &cfi_code,
        const std::string &security_group,
        double min_price_increment)
    {
      std::unique_lock<std::shared_mutex> lock(mtx);

      // Check if already exists
      auto p = theRefData->name_to_asset.find(symbol);
      if (p != theRefData->name_to_asset.end())
      {
        return const_cast<Asset *>(p->second);
      }

      // Create new option asset
      auto a = new Asset(get_next_id());
      a->_typ = 'O';  // Option type
      a->name = symbol;
      a->set_exchange_md(exchange);
      a->cfi_code = cfi_code;
      a->security_group = security_group;
      a->set_units(min_price_increment);
      a->bo_spread = 1;
      a->has_book = true;  // Options have book data (MBP)
      a->maxpx = -1;
      a->rng_adj = 0;
      a->max_limit_order_size = 0;
      a->contract_size = 0;
      a->n_rounding_digits = 2;
      a->exchange_md_str = en::to_string(exchange);

      // Add to RefData maps
      theRefData->id_to_asset[a->id] = a;
      theRefData->name_to_asset[a->name] = a;
      theRefData->sec_id_to_asset[security_id] = a;
      a->sec_id = security_id;

      std::cerr << "RefData: dynamically created option asset: " << symbol
                << " id=" << a->id << " securityID=" << security_id << std::endl;

      return a;
    }

    static Asset *add_future_asset(
        const std::string &symbol,
        en::x exchange,
        int32_t security_id,
        const std::string &cfi_code,
        const std::string &security_group,
        double min_price_increment)
    {
      std::unique_lock<std::shared_mutex> lock(mtx);

      // Check if already exists
      auto p = theRefData->name_to_asset.find(symbol);
      if (p != theRefData->name_to_asset.end())
      {
        return const_cast<Asset *>(p->second);
      }

      auto a = new Asset(get_next_id());
      a->_typ = 'F';  // Future type
      a->name = symbol;
      a->set_exchange_md(exchange);
      a->cfi_code = cfi_code;
      a->security_group = security_group;
      a->set_units(min_price_increment);
      a->bo_spread = 1;
      a->has_book = true;
      a->maxpx = -1;
      a->rng_adj = 0;
      a->max_limit_order_size = 0;
      a->contract_size = 0;
      a->n_rounding_digits = 2;
      a->exchange_md_str = en::to_string(exchange);

      // Add to RefData maps
      theRefData->id_to_asset[a->id] = a;
      theRefData->name_to_asset[a->name] = a;
      theRefData->sec_id_to_asset[security_id] = a;
      a->sec_id = security_id;

      std::cerr << "RefData: dynamically created future asset: " << symbol
                << " id=" << a->id << " securityID=" << security_id << std::endl;

      return a;
    }

    static void duplicate_asset(const std::string &repl_name, const Asset *a)
    {
      std::unique_lock<std::shared_mutex> lock(mtx);
      auto p = theRefData->name_to_asset.find(repl_name);
      if (p != theRefData->name_to_asset.end())
        return;
      auto a_copy = new Asset(*a);
      a_copy->name = repl_name;
      a_copy->id = get_next_id();
      theRefData->id_to_asset[a_copy->id] = a_copy;
      theRefData->name_to_asset[a_copy->name] = a_copy;
      theRefData->sec_id_to_asset[a_copy->sec_id] = a_copy;
    }
    static bool is_asset(int i)
    {
      auto a = inst().asset(i);
      if (a)
        return true;
      return false;
    }
    static bool is_asset(const std::string &str)
    {
      auto a = inst().asset(str.c_str());
      if (a)
        return true;
      return false;
    }
    static const Asset *get_asset(const std::string &str)
    {
      auto a = inst().asset(str);
      return a;
    }
    static const Asset *get_asset(std::size_t id)
    {
      auto a = inst().asset(id);
      return a;
    }

    // should take venue as param as well
    static const Asset *get_asset_from_sec_id(uint32_t secid)
    {
      auto a = inst().asset_from_sec_id(secid);
      return a;
    }
    static const std::string get_asset_name(std::size_t id)
    {
      auto a = inst().asset(id);
      if (a)
        return a->name;
      ERR("Unknown");
      return "";
    }
    static const id_to_asset_t &getAssets()
    {
      return theRefData->id_to_asset;
    }
    static void set_universe(
        const std::string &_universe_fname,
        const std::string &_rounding_info_fname = "")
    {
      universe_fname = _universe_fname;
      rounding_info_fname = _rounding_info_fname;
    }
    static const RefData &inst()
    {
      if (theRefData)
        return *theRefData;
      else
        return init();
    }

    static std::size_t get_num_assets()
    {
      return inst().num_assets();
    }
    inline std::size_t num_assets() const { return num_sym; }

    // Set exchange->sector->asset mapping for fast O(1) lookup
    static void set_exchange_sector_mapping(en::x exchange, const std::string& sector, const Asset* asset) noexcept
    {
      std::unique_lock<std::shared_mutex> lock(mtx);
      auto key = std::make_pair(exchange, sector);
      exchange_sector_to_asset[key] = asset;

      if (asset)
      {
        std::cerr << "RefData: set_exchange_sector_mapping "
                  << en::to_string(exchange) << "[" << sector << "] -> "
                  << asset->name << std::endl;
      }
      else
      {
        std::cerr << "RefData: set_exchange_sector_mapping "
                  << en::to_string(exchange) << "[" << sector << "] -> nullptr"
                  << std::endl;
        SNGH;
      }
    }

    //
    // todo: may have to put locks around these eventually
    // so assets can be created without the file
    //
    inline const Asset *asset(std::size_t id) const
    {
#ifdef DEBUG
      ASSERTF(id < id_to_asset.size(), boost::format("request for bad asset id %d") % id);
#endif
      return id_to_asset[id];
    }
    inline const Asset *asset_from_sec_id(uint32_t id) const
    {
      std::shared_lock<std::shared_mutex> lock(mtx);
      auto p = sec_id_to_asset.find(id);
      if (p == sec_id_to_asset.end())
        return 0;
      // ASSERTF(p!=sec_id_to_asset.end(),boost::format("asset id: %d is not found") % id);
      return p->second;
    }
    inline const Asset *asset(const std::string &nam) const
    {
      std::shared_lock<std::shared_mutex> lock(mtx);
      auto it = name_to_asset.find(nam);
      if (it == name_to_asset.end())
      {
        return 0;
      }
      return it->second;
    }
    inline int min_ord_sz(en::x exch) const
    {
      auto it = minimum_order_size.find(exch);
      if (it == minimum_order_size.end())
        ERR("exchange not found");
      return it->second;
    }

    // should take venue as param as well
    void update_sec_id(int32_t sec_id, Asset *a)
    {
      std::unique_lock<std::shared_mutex> lock(mtx);
      sec_id_to_asset[sec_id] = a;
      a->sec_id = sec_id;
    }

    // Map-based O(1) lookup using pre-configured exchange_sector_to_asset map
    static const frame::ref::Asset *asset_from_sector_exchange_map(const std::string &sector, en::x exchange) noexcept
    {
      std::shared_lock<std::shared_mutex> lock(mtx);
      auto key = std::make_pair(exchange, sector);
      auto it = exchange_sector_to_asset.find(key);
      if (it != exchange_sector_to_asset.end())
      {
        return it->second;
      }
      return nullptr;
    }

#ifdef tradingvenueinasset
    // Original linear scan implementation (for validation)
    static const frame::ref::Asset *asset_from_sector_exchange_linear(const std::string &sector, en::x exchange) noexcept
    {
      check_valid_sectors();

      // Check cache first (with read lock)
      {
        std::shared_lock<std::shared_mutex> lock(mtx);
        auto cache_key = std::make_pair(sector, exchange);
        auto cache_it = sector_exchange_cache.find(cache_key);
        if (cache_it != sector_exchange_cache.end())
        {
          return cache_it->second;
        }
      }

      // Cache miss - do the linear search
      auto n = RefData::get_num_assets();
      for (uint i = 1; i < n; i++)
      {
        auto a = RefData::get_asset(i);
        if (!a)
          continue;
        if (!a->has_trading_venue())
          continue;
        if (a->invisible)
          continue;
        if (a->mnemonic == sector && a->get_trading_venue() == exchange)
        {
          // Store in cache before returning (with write lock)
          std::unique_lock<std::shared_mutex> lock(mtx);
          sector_exchange_cache[std::make_pair(sector, exchange)] = a;
          return a;
        }
      }

      // Also cache nullptr results to avoid repeated failed lookups (with write lock)
      std::unique_lock<std::shared_mutex> lock(mtx);
      sector_exchange_cache[std::make_pair(sector, exchange)] = nullptr;
      return nullptr;
    }
#endif

    // Validation wrapper - runs both implementations and compares results
    // Controlled by tradingvenueinasset define in mk/glob_begin.mk

    static const frame::ref::Asset *asset_from_sector_exchange(const std::string &sector, en::x exchange) noexcept
    {
#ifdef tradingvenueinasset
      const Asset* result_linear = asset_from_sector_exchange_linear(sector, exchange);
      const Asset* result_map = asset_from_sector_exchange_map(sector, exchange);

      if (result_linear != result_map)
      {
        std::cerr << "ERROR: asset_from_sector_exchange mismatch for "
                  << en::to_string(exchange) << "[" << sector << "]"
                  << " linear=" << (result_linear ? result_linear->name.c_str() : "nullptr")
                  << " map=" << (result_map ? result_map->name.c_str() : "nullptr")
                  << std::endl;
        // Return linear result as trusted baseline
        return result_linear;
      }

      // Match - return either result (they're the same)
      return result_linear;
#else
      // Production mode - use fast map lookup only
      return asset_from_sector_exchange_map(sector, exchange);
#endif
    }

#ifdef tradingvenueinasset
    // can only have one symbol per sector for an exchange
    static void check_valid_sectors() noexcept
    {

      static bool ran = false;
      if (ran) return;
      ran = true;

      std::flat_map<en::x, std::set<std::string>> venue_to_sect;
      auto n = RefData::get_num_assets();
      for (uint i = 1; i < n; i++)
      {
        auto a = RefData::get_asset(i);
        if (!a)
          continue;
        if (a->invisible)
          continue;
        if (!a->has_trading_venue())
          continue;
        auto ex = a->get_trading_venue();
        auto sect = a->mnemonic;
        auto p1 = venue_to_sect.find(ex);
        if (p1 == venue_to_sect.end())
        {
          venue_to_sect[ex] = std::set<std::string>();
          p1 = venue_to_sect.find(ex);
        }
        auto p2 = p1->second.find(sect);
        if (p2 != p1->second.end())
        {
          ERRF(boost::format("duplicate sector %s for exchange %s") % sect % ex);
        }
        venue_to_sect[ex].insert(sect);
      }
    }
#endif
  };

  // Implementation of Asset::btec_equiv_asset() - must be after RefData definition
  inline const Asset* Asset::btec_equiv_asset() const noexcept
  {
    if (!btec_equiv && !btec_equiv_name.empty() && btec_equiv_name != "UNK")
    {
      btec_equiv = RefData::get_asset(btec_equiv_name);
    }
    return btec_equiv;
  }

}
