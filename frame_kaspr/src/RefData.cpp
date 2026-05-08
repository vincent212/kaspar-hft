/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "frame/ref/RefData.hpp"

using namespace frame::ref;

RefData *RefData::theRefData = 0;
std::string RefData::universe_fname;
std::string RefData::rounding_info_fname;
std::string RefData::alias_fname;
std::atomic<int> RefData::asset_id = 0;
std::shared_mutex RefData::mtx;
std::flat_map<std::pair<std::string, en::x>, const Asset*> RefData::sector_exchange_cache;
std::flat_map<std::pair<en::x, std::string>, const Asset*> RefData::exchange_sector_to_asset;
