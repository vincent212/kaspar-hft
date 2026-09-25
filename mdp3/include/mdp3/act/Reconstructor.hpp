#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <cstdio>
#include <map>
#include <vector>
#include <variant>
#include <boost/unordered/unordered_flat_map.hpp>

#include "chutil/Macros.hpp"
#include "actors/Actor.hpp"
#include "bfile/r_l3.hpp"
#include "enum/e_names.hpp"
#include "frame/mda/msg/Data.hpp"
#include "frame/ref/RefData.hpp"
#include "mdp3/msg/ParsedMsg.hpp"
#include "mdp3/msg/AssetMap.hpp"
#include "mdp3/msg/ResetMBO.hpp"

namespace mdp3
{
  // The single serialization point for the parallel decode path.
  //
  // Warm DecodeWorkers parse hot MBO messages in parallel and send their entries
  // here as ParsedMsg batches, tagged with a dense per-message order_seq. The
  // Reconstructor:
  //   1. RESEQUENCES: drains order_seq = expected, expected+1, ... (buffering
  //      out-of-order batches) so entries apply in exact wire order.
  //   2. OWNS the order-dependent state -- orderid_to_securityid -- and does all
  //      routing (asset lookup -> BOOKSEND to TachBook[sym]). Being single-
  //      threaded IS the synchronization; no locks.
  //
  // Consistency assumptions (see parallel-mbo-decode-arch memory):
  //   - book/trade NEVER take the inline path (all-hot packets always parallel),
  //     so this map is the sole owner of orderid_to_securityid.
  //     Enforced by ONE assert, in DataDecoder::on_decode_packet. An earlier
  //     version of this comment claimed handler_if's MBO handlers assert it too;
  //     they do not -- handler_if's only asserts are two "sym mismatch" checks.
  //     KNOWN VIOLATION, measured on live ES chan 310 over 80,000 packets: 3.03%
  //     of packets (10.21% of messages) mix hot with MDIncrementalRefreshVolume37
  //     (1,419 of 1,419 mixed packets). So this assumption is false on the real
  //     feed and the parallel path cannot run until the packet split lands.
  //   - securityid_to_asset_id + mbo_order_books are populated at startup/recovery
  //     and read-only during trading -> shared here by const ref (safe reads).
  //
  // TODO (required before live): EndOfBurst. handler_if emits BurstEnd to the
  // books at end-of-event; this path must do the same on l3.endOfEvent, else
  // downstream (lights/SOM) never react. Not wired yet -- flagged.
  class Reconstructor : public actors::Actor
  {
  public:
    // chan (not venue) keys the name. The old "Reconstructor_%d" was keyed on
    // the VENUE, and channels 310/318/344 share one venue -- so a second CME
    // channel collided in Manager. "_P" marks the parallel path, matching
    // DataDecoder_P_310 / DecodeWorker_P_310_0.
    Reconstructor(const std::vector<actor_ptr> &mbo_order_books,
                  en::x xchg,
                  uint32_t chan,
                  const char *path_tag = "P")
        : mbo_order_books_(mbo_order_books),
          xchg_(xchg)
    {
      snprintf(name_, sizeof(name_), "Reconstructor_%s_%u", path_tag, chan);
      MESSAGE_HANDLER(msg::ParsedMsg, on_parsed);
      MESSAGE_HANDLER(msg::AssetMap, on_asset_map);
      MESSAGE_HANDLER(msg::ResetMBO, on_reset);
    }

    const char *get_name() const override { return name_; }

  private:
    void on_parsed(const msg::ParsedMsg *pm) noexcept
    {
      if (pm->order_seq == expected_seq_)
      {
        apply(pm->entries);
        ++expected_seq_;
        drain();
      }
      else
      {
        // out of order -> buffer (copy: the message is freed after this handler)
        buffer_[pm->order_seq] = pm->entries;
      }
    }

    // Apply any buffered batches that are now contiguous with expected_seq_.
    void drain() noexcept
    {
      while (!buffer_.empty() && buffer_.begin()->first == expected_seq_)
      {
        apply(buffer_.begin()->second);
        buffer_.erase(buffer_.begin());
        ++expected_seq_;
      }
    }

    void apply(const std::vector<bfile::l3_t> &entries) noexcept
    {
      for (const auto &e : entries)
      {
        if (std::holds_alternative<bfile::l3_mbo_v2_t>(e))
          apply_book(std::get<bfile::l3_mbo_v2_t>(e));
        else if (std::holds_alternative<bfile::l3_mbo_trd_v2_t>(e))
          apply_trade(std::get<bfile::l3_mbo_trd_v2_t>(e));
        else if (std::holds_alternative<bfile::l3_fdf_t>(e))
          apply_def(std::get<bfile::l3_fdf_t>(e));
        else if (std::holds_alternative<bfile::l3_chr_v2_t>(e))
          orderid_to_securityid_.clear(); // ChannelReset, applied in wire order
      }
    }

    // Order add/modify/delete: mutate the orderid map, then route by securityID.
    // Mirrors handler_if::MDIncrementalRefreshBook's stateful tail.
    void apply_book(const bfile::l3_mbo_v2_t &l3) noexcept
    {
      if (l3.orderUpdateAction == 0) // New
        orderid_to_securityid_[l3.orderID] = l3.securityID;
      else if (l3.orderUpdateAction == 2) // Delete
        orderid_to_securityid_.erase(l3.orderID);

      route(l3.securityID, l3);
      // TODO: if (l3.endOfEvent) emit_burstend();
    }

    // Trade carries only orderID: resolve securityID via the map (populated by a
    // prior New), then route. Mirrors handler_if::MDIncrementalRefreshTradeSummary.
    void apply_trade(const bfile::l3_mbo_trd_v2_t &l3) noexcept
    {
      auto oit = orderid_to_securityid_.find(l3.orderID);
      if (oit == orderid_to_securityid_.end()) [[unlikely]]
        return; // order not known -> not in universe
      route(oit->second, l3);
      // TODO: if (l3.endOfEvent) emit_burstend();
    }

    // Instrument definition in the ordered stream: resolve the asset off the
    // RefData singleton (same lookup handler_if does) and update THIS actor's
    // asset_map_, in wire order with the book stream. This replaces the
    // out-of-band AssetMap message for the parallel path.
    //
    // Minimal by design: it establishes securityID -> asset_id for routing. It
    // does NOT re-run handler_if's full RefData configuration (price units, cfi,
    // security group). On the live incremental feed definitions do not appear
    // (census: 2 all-cold of 120,000 packets; every mixed cold was Volume37) --
    // they arrive during instrument recovery, which still runs through handler_if
    // and configures RefData there. So this only has to keep routing correct.
    void apply_def(const bfile::l3_fdf_t &l3) noexcept
    {
      if (l3.updateAction != 'A' && l3.updateAction != 'M')
        return;
      auto a = frame::ref::RefData::inst().get_asset(std::string(l3.sym));
      if (a)
        asset_map_[l3.securityID] = a->id;
    }

    // A definition mapped securityID -> asset_id (sent by handler_if, e.g. during
    // instrument recovery). Update our own copy on THIS actor's thread so route()
    // never touches the shared map.
    void on_asset_map(const msg::AssetMap *m) noexcept
    {
      asset_map_[m->securityID] = m->asset_id;
    }

    // ChannelReset: the exchange cleared the book. Drop the orderID map so a
    // reused orderID cannot misroute. Leave asset_map_ (definitions persist) and
    // the resequence state (order_seq/expected_seq/buffer_) untouched -- those
    // stay in lockstep with DataDecoder, so clearing them would stall the stream.
    void on_reset(const msg::ResetMBO *) noexcept
    {
      orderid_to_securityid_.clear();
    }

    // securityID -> asset_id -> TachBook[asset_id]; send the l3 as a raw Data.
    template <class L3>
    void route(int32_t securityID, const L3 &l3) noexcept
    {
      auto it = asset_map_.find(securityID);
      if (it == asset_map_.end()) [[unlikely]]
        return; // not in universe (yet)
      const int32_t asset_id = it->second;
      if (asset_id < 0 || static_cast<size_t>(asset_id) >= mbo_order_books_.size()) [[unlikely]]
        return;
      const auto book = mbo_order_books_[asset_id];
      if (!book) [[unlikely]]
        return;
      auto *d = new frame::mda::msg::Data(/*israw=*/true);
      d->l3 = l3;
      book->send(d, this);
    }

    // Owned: mutated only here (single-threaded), so no lock.
    boost::unordered_flat_map<uint64_t, int32_t> orderid_to_securityid_;
    // Owned: fed by handler_if via AssetMap messages and applied on THIS actor's
    // thread -- no shared-map race with the inline definition path.
    boost::unordered_flat_map<int32_t, int32_t> asset_map_;
    // Shared read-only: set once at startup (kaspr), never written afterwards.
    const std::vector<actor_ptr> &mbo_order_books_;

    // Resequence buffer: order_seq -> that message's entries. std::map keeps keys
    // sorted so the smallest buffered order_seq is begin().
    std::map<uint64_t, std::vector<bfile::l3_t>> buffer_;
    uint64_t expected_seq_ = 0;

    en::x xchg_;
    char  name_[256];
  };
}
