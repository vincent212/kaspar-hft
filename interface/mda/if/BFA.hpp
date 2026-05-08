#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/Actor.hpp"
#include "enum/e_names.hpp"
#include "frame/ref/RefData.hpp"
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <string>
#include "frame/mda/act/BFA.hpp"

// Factory used by sim-style callers: BFA invokes it once per FDF/ODF with
// (asset_id, asset*, venue, ...). We look up the pre-created OrderBook in
// the caller's dense `order_books[venue][asset_id]` vector and return it so
// BFA can key its sparse dataAdapter by securityID. Without this, every MBO
// fails the securityID lookup and no order books ever receive updates.
struct SimOrderBookFactory {
    const std::vector<std::vector<cfsmp>>* order_books;
    actors::Actor* operator()(int asset_id,
                              const frame::ref::Asset*,
                              en::x venue,
                              double /*minPriceIncrement*/,
                              double /*dispFactor*/) const {
        if (!order_books) return nullptr;
        if (static_cast<size_t>(venue.value) >= order_books->size()) return nullptr;
        const auto& row = (*order_books)[venue.value];
        if (asset_id < 0 || static_cast<size_t>(asset_id) >= row.size()) return nullptr;
        return row[asset_id];
    }
};

// create_BFA — sim entry point. Internally converts the caller's dense
// vector<vector<cfsmp>> order_books layout into the sparse map that BFA now
// requires, and plugs in a factory so BFA can populate its securityID →
// order-book adapter as FDFs arrive.
//
// Without the factory (NoOpFactory default), bins that carry real CME
// securityIDs (e.g. Databento output) don't route any MBO to any book.
template<bool TreasOnly = false>
cfsmp create_BFA(
    const std::vector<std::vector<cfsmp>>& order_books,
    const std::string& data_file,
    cfsmp manager)
{
    std::vector<std::unordered_map<int32_t, cfsmp>> sparse(order_books.size());
    for (size_t venue = 0; venue < order_books.size(); ++venue) {
        const auto& row = order_books[venue];
        auto& m = sparse[venue];
        for (size_t sec_id = 0; sec_id < row.size(); ++sec_id) {
            if (row[sec_id]) {
                m.emplace(static_cast<int32_t>(sec_id), row[sec_id]);
            }
        }
    }
    SimOrderBookFactory factory{&order_books};
    return new frame::mda::act::BFA<TreasOnly, SimOrderBookFactory>(
        sparse, data_file, manager, factory);
}
