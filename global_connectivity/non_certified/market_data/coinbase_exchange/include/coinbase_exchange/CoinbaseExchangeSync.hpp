#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

#include "bfile/r_l3.hpp"
#include "coinbase_exchange/Num.hpp"

namespace coinbase_exchange
{
  /**
   * One Coinbase Exchange product as the handlers see it: its increments and
   * the unit its sizes are sent to OB in.
   *
   * Prices go to OB in ticks of the quote increment (pxd = price / increment,
   * an integer; the Kaspar asset's units are 1, as for the other crypto
   * feeds). Sizes are kept exactly, as integers of the base increment, and go
   * to OB as displayQty in units of `qty_unit`, rounded up so that no resting
   * order shows as 0. OB refuses an order of CHOPIN_MAX_ORD_SZ (10,000,000)
   * units or more (frame/mda/msg/Data.hpp, frame/ob/Order.hpp), so a larger
   * size is sent as kMaxDisplay (counted as saturated).
   */
  struct ProductSpec
  {
    static constexpr uint32_t kMaxDisplay = 9999999;

    std::string product;          // "BTC-USD"
    int32_t sec_id = 0;           // securityID in every record (the Kaspar asset id)
    char venue = 0;               // en::x value
    int price_decimals = 2;       // decimals of quote_increment
    int64_t price_increment = 1;  // quote_increment in 10^-price_decimals
    int size_decimals = 8;        // decimals of base_increment
    int64_t qty_unit = 1;         // OB display unit in 10^-size_decimals

    // From the strings of GET /products/<id> (quote_increment, base_increment)
    // and the chosen display unit ("0.000001"). nullopt with err on bad input.
    static std::optional<ProductSpec> make(const std::string &product, int32_t sec_id, char venue,
                                           const std::string &quote_increment,
                                           const std::string &base_increment, const std::string &qty_unit,
                                           std::string &err);

    // price string -> ticks; nullopt if malformed or not on the increment.
    std::optional<int64_t> ticks(std::string_view px) const;
    // size string -> base increments; nullopt if malformed.
    std::optional<int64_t> size(std::string_view sz) const { return scaled(sz, size_decimals); }
    // base increments -> OB display units, rounded up, at most kMaxDisplay;
    // `saturated` set if clipped.
    uint32_t display(int64_t size, bool *saturated = nullptr) const;
  };

  /**
   * CoinbaseExchangeSync -- the Coinbase Exchange order-by-order book of one
   * product, kept from the `full` (or `level3`) WebSocket channel and the
   * REST level-3 snapshot, and sent to OB as Kaspar L3 records. No network:
   * the feed actor hands it parsed messages and snapshots. feed.md section 5
   * gives the rules with their sources; the short form:
   *
   *   sync      subscribe, buffer the stream, GET /products/<id>/book?level=3
   *             (sequence S), drop buffered messages with sequence <= S, apply
   *             the rest. The first one applied must be S+1.
   *   sequence  per product, +1 per message. A message at or below the last
   *             applied is a duplicate (ignored). One above the next expected
   *             is held for up to `reorder_window` messages, since the venue
   *             says messages can arrive out of order; if the missing one does
   *             not come, it is a gap: a gap record goes to OB and the book is
   *             rebuilt from a new snapshot (recovery records).
   *   book      only `open` puts an order on the book; `done` and `change` for
   *             an order not on the book are ignored; `match` reduces the
   *             maker's size; `received`, `activate` and `noop` change
   *             nothing.
   *
   * Records (kaspar_mapping.md):
   *   open                      l3_mbo_v2_t add
   *   done (order on book)      l3_mbo_v2_t delete
   *   change (order on book)    l3_mbo_v2_t change (Coinbase keeps priority on a size
   *                             decrease and loses it on an increase or a price
   *                             change: OB's own rule)
   *   match                     l3_mbp_trd_t print, l3_mbo_trd_v2_t on the maker,
   *                             then change (size left) or delete (filled)
   *   snapshot                  l3_mbo_v2_t adds flagged recovery (OB clears first)
   *   gap / disconnect          l3_gap_v2_t
   *   product status            l3_sst_t
   * Every venue message is one event: its records share one transactTime,
   * which strictly increases from event to event, and the last book record
   * carries endOfEvent. Each event ends with l3_eop_t (the low 32 bits of the
   * sequence) and l3_eob_t.
   *
   * Order ids: the high 64 bits of the order's UUID (the same id the order
   * manager reports as the exchange order id), or a synthetic id with the top
   * bit set if two live orders share them (counted).
   */
  class CoinbaseExchangeSync
  {
  public:
    struct Config
    {
      ProductSpec spec;
      size_t reorder_window = 64;    // messages held waiting for a missing one
      size_t max_buffer = 4000000;   // messages buffered while waiting for a snapshot
      // false: a partial stream (the public `matches` channel), used only by the
      // public live check. Sequence jumps are not gaps and an unknown maker is
      // counted, not resynced.
      bool check_gaps = true;
    };

    using Emit = std::function<void(const bfile::l3_t &)>;

    enum class SnapResult
    {
      Applied,  // book rebuilt and the buffered messages applied
      TooOld,   // the buffer starts after S+1 or has a hole: fetch another
      Bad,      // malformed snapshot
    };

    // One venue message, normalised from either channel's form.
    struct Event
    {
      enum Type : uint8_t { Received, Open, Done, Match, Change, Activate, Noop, Other };
      Type type = Other;
      int64_t seq = -1;
      uint64_t time_ns = 0;
      uint64_t recv_ns = 0;
      Uuid order;           // open, done, change, received
      Uuid maker, taker;    // match
      char side = 0;        // 'b' buy, 's' sell, 0 not given
      bool has_px = false;
      int64_t px = 0;       // ticks: open, change (new), match
      bool has_size = false;
      int64_t size = 0;     // open remaining, change new size, match size
      int64_t trade_id = 0;
      std::string reason;   // done: filled/canceled; change: STP/modify_order
    };

    struct Order
    {
      uint64_t oid = 0;
      bool bid = false;
      int64_t px = 0;       // ticks
      int64_t size = 0;     // base increments
      uint32_t disp = 0;    // as last sent to OB
      Uuid uuid;
    };

    struct Level
    {
      int64_t size = 0;     // base increments
      int64_t disp = 0;     // sum of display units, as OB holds them
      uint32_t count = 0;
    };

    struct Stats
    {
      uint64_t messages = 0, applied = 0, duplicates = 0, reordered = 0, gaps = 0, resyncs = 0;
      uint64_t snapshots = 0, snapshots_too_old = 0, buffered_dropped = 0;
      uint64_t opens = 0, dones = 0, matches = 0, changes = 0, received = 0, other = 0;
      uint64_t ignored_done = 0, ignored_change = 0;
      uint64_t unknown_maker = 0, overfill = 0, bad_message = 0, match_px_mismatch = 0, dup_open = 0;
      uint64_t crossed_events = 0, id_collisions = 0, saturated = 0, zero_execs = 0;
      uint64_t hb_ahead = 0;
    };

    CoinbaseExchangeSync(Config cfg, Emit emit);

    // A message from the WebSocket: a `full` channel object, a level3 schema or
    // compact array, a heartbeat, a status, or anything else (ignored).
    void on_message(const nlohmann::json &m, uint64_t recv_ns);

    // GET /products/<id>/book?level=3, parsed.
    SnapResult on_snapshot(const nlohmann::json &snap, uint64_t recv_ns);

    // The connection is gone: what follows is a new stream. A gap record goes
    // to OB if the book was in sync, and a snapshot is needed again.
    void on_disconnect(uint64_t now_ns);

    // A product's status (the `status` channel entry or GET /products/<id>).
    // Sends l3_sst_t when the mapped Kaspar status changes.
    void on_product_status(const nlohmann::json &p, uint64_t now_ns);

    // Kaspar trading status for a product object (feed.md section 7).
    static uint8_t trading_status(const nlohmann::json &p);

    // Parse one message into an Event. false: not a book message.
    bool parse(const nlohmann::json &m, Event &ev);

    bool synced() const { return synced_; }
    bool need_snapshot() const { return need_snapshot_; }
    int64_t last_seq() const { return last_seq_; }
    const Stats &stats() const { return st_; }
    const ProductSpec &spec() const { return cfg_.spec; }

    // Inspection, for tests and the comparisons.
    const Order *find(const Uuid &u) const;
    const std::unordered_map<Uuid, Order, UuidHash> &all_orders() const { return orders_; }
    size_t orders() const { return orders_.size(); }
    int64_t best(bool bid) const;   // ticks; 0 when the side is empty
    const std::map<int64_t, Level> &side(bool bid) const { return bid ? bids_ : asks_; }
    Level level(bool bid, int64_t px) const;
    // The order ids at a level, in queue order (the order the book got them).
    std::vector<Uuid> queue(bool bid, int64_t px) const;

  private:
    Config cfg_;
    Emit emit_;
    Stats st_;

    std::unordered_map<Uuid, Order, UuidHash> orders_;
    std::unordered_set<uint64_t> live_ids_;
    std::map<int64_t, Level> bids_, asks_;
    // queue position: per-order arrival counter, for queue() and priority
    std::unordered_map<Uuid, uint64_t, UuidHash> prio_;
    uint64_t next_prio_ = 1;
    uint64_t next_synth_ = 1;

    bool synced_ = false;
    bool need_snapshot_ = true;
    bool started_ = false;
    int64_t last_seq_ = -1;
    std::map<int64_t, Event> pending_;   // out-of-order messages waiting for a missing one
    std::deque<Event> buffer_;           // messages while waiting for a snapshot
    nlohmann::json l3_schema_;           // the level3 channel's schema message
    uint8_t status_ = 0;                 // last Kaspar status sent; 0 = none

    // records of the open event
    std::vector<bfile::l3_t> out_;
    uint64_t event_tx_ = 0, last_tx_ = 0;

    void accept(Event &&ev);
    void apply(const Event &ev);
    void begin(const Event &ev);
    void end_event(int64_t seq, uint64_t send_time);
    uint64_t stamp(uint64_t tx);

    void add(const Event &ev, bool recovery, uint64_t recv_ns, uint64_t send_ns);
    void remove(Order &o, uint64_t recv_ns, uint64_t send_ns);
    void set_size(Order &o, int64_t new_size, uint64_t recv_ns, uint64_t send_ns);
    void move(Order &o, int64_t new_px, int64_t new_size, uint64_t recv_ns, uint64_t send_ns);
    void level_add(const Order &o);
    void level_sub(const Order &o);
    void push_mbo(uint8_t action, const Order &o, bool recovery, uint64_t recv_ns, uint64_t send_ns);
    uint64_t make_oid(const Uuid &u);
    void free_oid(const Order &o);
    void clear_book();
    void gap(const char *why, uint64_t now_ns);
    void send_status(uint8_t status, uint64_t now_ns);
    void check_cross();
  };
}
