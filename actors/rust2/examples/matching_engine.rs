// Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
// Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
//
// Licensed under the MIT License. See LICENSE file in the project root.

//! # How to write a matching engine in Rust with the actor framework
//!
//! A **single-actor**, per-symbol spot limit-order-book matching engine with
//! **price-time (FIFO) priority** and first-class partial fills. It demonstrates
//! the intended shape of an exchange matching engine on `actors2`:
//!
//! - The **core** ([`OrderBook`]) is a plain, framework-agnostic, deterministic
//!   struct: `place`/`cancel`/`replace` mutate an in-memory book and **return the
//!   resulting events**. This is where correctness lives — see the tests at the
//!   bottom (`cargo test --example matching_engine`).
//! - The **actor** ([`MatchingEngine`]) is a thin adapter: it owns the book, its
//!   mailbox serializes all order events (so it is the deterministic single
//!   writer), and it routes each returned event to the right `ActorRef` (the
//!   order's originator, or the market-data feed).
//!
//! The data-structure design is carried from the C++ `OB`/`OrderQ`/`Order` in
//! `frame_kaspr` (price levels + per-level FIFO queues + id→location index +
//! best-bid/ask tracking + per-order fill emission).
//!
//! Run the demo:  `cargo run --release --example matching_engine`
//! Run the tests: `cargo test --example matching_engine`

use std::collections::{BTreeMap, HashMap, VecDeque};

use actors2::{
    define_message, handle_messages, ActorContext, ActorRef, Manager, ThreadConfig,
};

// ===========================================================================
// Core matching engine (framework-agnostic, deterministic, unit-tested)
// ===========================================================================

/// Order side. Buyers cross the ask book; sellers cross the bid book.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Side {
    Buy,
    Sell,
}

/// An event produced by the book. `H` is the "originator handle" — the tests use
/// a `&str` client name; the actor uses an [`ActorRef`]. This keeps the matching
/// logic decoupled from message transport.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Event<H> {
    /// Order accepted and (partly) resting.
    Ack { to: H, order_id: u64, resting_qty: u64 },
    /// A trade. Emitted once per side (maker and taker) at the **maker's price**.
    Fill {
        to: H,
        order_id: u64,
        side: Side,
        price: u64,
        qty: u64,
        leaves_qty: u64,
        counter_order_id: u64,
        trade_id: u64,
    },
    /// A resting order was cancelled.
    Canceled { to: H, order_id: u64, canceled_qty: u64 },
    /// A resting order was replaced (then re-placed; see `replace`).
    Replaced { to: H, order_id: u64, new_price: u64, new_qty: u64 },
    /// A request was rejected (bad input / unknown order). The book is unchanged.
    Rejected { to: H, order_id: u64, reason: &'static str },
    /// Top-of-book changed. `(price, aggregate_qty)` per side.
    Bbo { bid: Option<(u64, u64)>, ask: Option<(u64, u64)> },
}

/// A resting order (a node in a price level's FIFO queue). Mirrors C++ `Order`.
struct Order<H> {
    order_id: u64,
    side: Side,
    price: u64,
    qty: u64,
    originator: H,
}

/// In-memory limit order book for one symbol. Prices/quantities are integers
/// (ticks / smallest unit) — exact compare and partial-fill accounting.
///
/// Bids/asks are `BTreeMap<price, FIFO queue>`: best bid = max key, best ask =
/// min key (O(log L) best-price, O(1) FIFO at a level). A production hot-path
/// engine can swap the maps for direct-indexed price arrays over a bounded tick
/// band (the C++ `OB` choice) — the interface is identical.
/// One price level: a FIFO queue of orders + a running total size, so
/// best-of-book aggregate size is O(1) instead of O(level depth). (The C++
/// `OrderQ` keeps the same `orders_in_book` counter for exactly this reason.)
struct Level<H> {
    orders: VecDeque<Order<H>>,
    total: u64,
}

impl<H> Default for Level<H> {
    fn default() -> Self {
        Level { orders: VecDeque::new(), total: 0 }
    }
}

pub struct OrderBook<H> {
    bids: BTreeMap<u64, Level<H>>,
    asks: BTreeMap<u64, Level<H>>,
    /// order_id -> (side, price), for O(1) cancel/replace lookup (C++ `ordermap`).
    index: HashMap<u64, (Side, u64)>,
    next_trade_id: u64,
}

impl<H> Default for OrderBook<H> {
    fn default() -> Self {
        OrderBook {
            bids: BTreeMap::new(),
            asks: BTreeMap::new(),
            index: HashMap::new(),
            next_trade_id: 1,
        }
    }
}

impl<H: Clone> OrderBook<H> {
    pub fn new() -> Self {
        Self::default()
    }

    /// Best bid / best ask as `(price, aggregate_qty)`.
    fn bbo(&self) -> (Option<(u64, u64)>, Option<(u64, u64)>) {
        let bid = self.bids.iter().next_back().map(|(px, lvl)| (*px, lvl.total));
        let ask = self.asks.iter().next().map(|(px, lvl)| (*px, lvl.total));
        (bid, ask)
    }

    /// Place a limit order: match what crosses (price-time FIFO), rest the rest.
    pub fn place(&mut self, order_id: u64, side: Side, price: u64, qty: u64, who: H) -> Vec<Event<H>> {
        let mut ev = Vec::new();
        if qty == 0 || price == 0 {
            ev.push(Event::Rejected { to: who, order_id, reason: "price and qty must be positive" });
            return ev;
        }
        if self.index.contains_key(&order_id) {
            ev.push(Event::Rejected { to: who, order_id, reason: "duplicate order_id" });
            return ev;
        }

        let before = self.bbo();
        let mut remaining = qty;

        // --- 1) Match while marketable, best price then time (FIFO) ---
        while remaining > 0 {
            // Best crossing opposite level, if any.
            let cross_px = match side {
                Side::Buy => self.asks.keys().next().copied().filter(|a| *a <= price),
                Side::Sell => self.bids.keys().next_back().copied().filter(|b| *b >= price),
            };
            let Some(px) = cross_px else { break };

            // One fill against the head (earliest) order at that level. The inner
            // block scopes the level borrow so we can touch other fields after.
            let (fill_qty, exec_px, head_id, head_side, head_orig, head_leaves) = {
                let opp = match side {
                    Side::Buy => &mut self.asks,
                    Side::Sell => &mut self.bids,
                };
                let lvl = opp.get_mut(&px).expect("crossing level exists");
                let head = lvl.orders.front_mut().expect("level non-empty");
                let x = remaining.min(head.qty);
                head.qty -= x;
                let head_qty = head.qty;
                let tuple = (x, head.price, head.order_id, head.side, head.originator.clone(), head_qty);
                lvl.total -= x;
                if head_qty == 0 {
                    lvl.orders.pop_front();
                }
                if lvl.orders.is_empty() {
                    opp.remove(&px);
                }
                tuple
            };

            remaining -= fill_qty;
            let trade_id = self.next_trade_id;
            self.next_trade_id += 1;
            if head_leaves == 0 {
                self.index.remove(&head_id);
            }
            // Maker fill (resting order), then taker fill (aggressor).
            ev.push(Event::Fill {
                to: head_orig,
                order_id: head_id,
                side: head_side,
                price: exec_px,
                qty: fill_qty,
                leaves_qty: head_leaves,
                counter_order_id: order_id,
                trade_id,
            });
            ev.push(Event::Fill {
                to: who.clone(),
                order_id,
                side,
                price: exec_px,
                qty: fill_qty,
                leaves_qty: remaining,
                counter_order_id: head_id,
                trade_id,
            });
        }

        // --- 2) Rest the remainder (append at tail = time priority) ---
        if remaining > 0 {
            let book = match side {
                Side::Buy => &mut self.bids,
                Side::Sell => &mut self.asks,
            };
            let lvl = book.entry(price).or_default();
            lvl.orders.push_back(Order {
                order_id,
                side,
                price,
                qty: remaining,
                originator: who.clone(),
            });
            lvl.total += remaining;
            self.index.insert(order_id, (side, price));
            ev.push(Event::Ack { to: who, order_id, resting_qty: remaining });
        }

        let after = self.bbo();
        if after != before {
            ev.push(Event::Bbo { bid: after.0, ask: after.1 });
        }
        ev
    }

    /// Cancel a resting order. `who` is the requester (for reject routing); the
    /// `Canceled` goes to the original order's originator.
    pub fn cancel(&mut self, order_id: u64, who: H) -> Vec<Event<H>> {
        let mut ev = Vec::new();
        let Some(&(side, px)) = self.index.get(&order_id) else {
            ev.push(Event::Rejected { to: who, order_id, reason: "cancel: unknown order" });
            return ev;
        };
        let before = self.bbo();
        let ord = self.remove_resting(side, px, order_id);
        ev.push(Event::Canceled { to: ord.originator, order_id, canceled_qty: ord.qty });
        let after = self.bbo();
        if after != before {
            ev.push(Event::Bbo { bid: after.0, ask: after.1 });
        }
        ev
    }

    /// Cancel-replace: remove the old order (emit `Replaced`) and place a fresh
    /// one with the same id/side/originator. Re-queues at the tail — **loses time
    /// priority** — and may immediately match.
    pub fn replace(&mut self, order_id: u64, new_price: u64, new_qty: u64, who: H) -> Vec<Event<H>> {
        let mut ev = Vec::new();
        let Some(&(side, px)) = self.index.get(&order_id) else {
            ev.push(Event::Rejected { to: who, order_id, reason: "replace: unknown order" });
            return ev;
        };
        if new_price == 0 || new_qty == 0 {
            ev.push(Event::Rejected { to: who, order_id, reason: "replace: price and qty must be positive" });
            return ev;
        }
        let old = self.remove_resting(side, px, order_id);
        ev.push(Event::Replaced { to: old.originator.clone(), order_id, new_price, new_qty });
        ev.extend(self.place(order_id, side, new_price, new_qty, old.originator));
        ev
    }

    /// Unlink a resting order from its level (and the index). Panics only on an
    /// internal invariant break (index and book out of sync).
    fn remove_resting(&mut self, side: Side, px: u64, order_id: u64) -> Order<H> {
        let book = match side {
            Side::Buy => &mut self.bids,
            Side::Sell => &mut self.asks,
        };
        let lvl = book.get_mut(&px).expect("indexed level exists");
        let pos = lvl.orders.iter().position(|o| o.order_id == order_id).expect("indexed order exists");
        let ord = lvl.orders.remove(pos).expect("position valid");
        lvl.total -= ord.qty;
        if lvl.orders.is_empty() {
            book.remove(&px);
        }
        self.index.remove(&order_id);
        ord
    }

    /// Print the best `depth` levels per side (asks highest→best, then bids
    /// best→lowest) with aggregate size — a simple L2 book snapshot.
    pub fn print_book(&self, depth: usize) {
        let mut asks: Vec<_> = self.asks.iter().take(depth).collect();
        asks.reverse(); // print highest ask first, best ask just above the spread
        for (px, lvl) in asks {
            println!("      ASK {px:>5} x {}", lvl.total);
        }
        println!("      ----- spread -----");
        for (px, lvl) in self.bids.iter().rev().take(depth) {
            println!("      BID {px:>5} x {}", lvl.total);
        }
    }
}

// ===========================================================================
// Framework messages (integer ids; ids < 16 reserved for the framework)
// ===========================================================================

// Inbound (order entry)
pub struct PlaceOrder {
    pub order_id: u64,
    pub side: Side,
    pub price: u64,
    pub qty: u64,
}
define_message!(PlaceOrder, 20);

pub struct CancelOrder {
    pub order_id: u64,
}
define_message!(CancelOrder, 21);

pub struct ReplaceOrder {
    pub order_id: u64,
    pub new_price: u64,
    pub new_qty: u64,
}
define_message!(ReplaceOrder, 22);

// Outbound (execution reports)
pub struct Ack {
    pub order_id: u64,
    pub resting_qty: u64,
}
define_message!(Ack, 30);

pub struct FillMsg {
    pub order_id: u64,
    pub side: Side,
    pub price: u64,
    pub qty: u64,
    pub leaves_qty: u64,
    pub counter_order_id: u64,
    pub trade_id: u64,
}
define_message!(FillMsg, 31);

pub struct Canceled {
    pub order_id: u64,
    pub canceled_qty: u64,
}
define_message!(Canceled, 32);

pub struct Replaced {
    pub order_id: u64,
    pub new_price: u64,
    pub new_qty: u64,
}
define_message!(Replaced, 33);

pub struct Rejected {
    pub order_id: u64,
    pub reason: &'static str,
}
define_message!(Rejected, 34);

pub struct BboUpdate {
    pub bid: Option<(u64, u64)>,
    pub ask: Option<(u64, u64)>,
}
define_message!(BboUpdate, 35);

// ===========================================================================
// The matching-engine actor (thin adapter over OrderBook<ActorRef>)
// ===========================================================================

pub struct MatchingEngine {
    book: OrderBook<ActorRef>,
    md_feed: Option<ActorRef>,
}

impl MatchingEngine {
    pub fn new(md_feed: Option<ActorRef>) -> Self {
        MatchingEngine { book: OrderBook::new(), md_feed }
    }

    fn on_place(&mut self, m: &PlaceOrder, ctx: &mut ActorContext) {
        let Some(who) = ctx.sender() else { return };
        let events = self.book.place(m.order_id, m.side, m.price, m.qty, who);
        self.route(events);
    }

    fn on_cancel(&mut self, m: &CancelOrder, ctx: &mut ActorContext) {
        let Some(who) = ctx.sender() else { return };
        let events = self.book.cancel(m.order_id, who);
        self.route(events);
    }

    fn on_replace(&mut self, m: &ReplaceOrder, ctx: &mut ActorContext) {
        let Some(who) = ctx.sender() else { return };
        let events = self.book.replace(m.order_id, m.new_price, m.new_qty, who);
        self.route(events);
    }

    /// Route each core event to the right ActorRef via async `send`.
    fn route(&mut self, events: Vec<Event<ActorRef>>) {
        for e in events {
            match e {
                Event::Ack { to, order_id, resting_qty } => {
                    to.send(Box::new(Ack { order_id, resting_qty }), None)
                }
                Event::Fill { to, order_id, side, price, qty, leaves_qty, counter_order_id, trade_id } => to.send(
                    Box::new(FillMsg { order_id, side, price, qty, leaves_qty, counter_order_id, trade_id }),
                    None,
                ),
                Event::Canceled { to, order_id, canceled_qty } => {
                    to.send(Box::new(Canceled { order_id, canceled_qty }), None)
                }
                Event::Replaced { to, order_id, new_price, new_qty } => {
                    to.send(Box::new(Replaced { order_id, new_price, new_qty }), None)
                }
                Event::Rejected { to, order_id, reason } => {
                    to.send(Box::new(Rejected { order_id, reason }), None)
                }
                Event::Bbo { bid, ask } => {
                    if let Some(md) = &self.md_feed {
                        md.send(Box::new(BboUpdate { bid, ask }), None);
                    }
                }
            }
        }
    }
}

handle_messages!(MatchingEngine,
    PlaceOrder   => on_place,
    CancelOrder  => on_cancel,
    ReplaceOrder => on_replace,
);

// ===========================================================================
// Demo: a client that prints execution reports, driven through the engine.
// ===========================================================================

struct Client {
    name: &'static str,
}
impl Client {
    fn on_ack(&mut self, m: &Ack, _c: &mut ActorContext) {
        println!("  [{}] ACK   order {} resting {}", self.name, m.order_id, m.resting_qty);
    }
    fn on_fill(&mut self, m: &FillMsg, _c: &mut ActorContext) {
        println!(
            "  [{}] FILL  order {} {:?} {}@{} (leaves {}, vs {}) trade {}",
            self.name, m.order_id, m.side, m.qty, m.price, m.leaves_qty, m.counter_order_id, m.trade_id
        );
    }
    fn on_canceled(&mut self, m: &Canceled, _c: &mut ActorContext) {
        println!("  [{}] CXL   order {} canceled {}", self.name, m.order_id, m.canceled_qty);
    }
    fn on_rejected(&mut self, m: &Rejected, _c: &mut ActorContext) {
        println!("  [{}] REJ   order {}: {}", self.name, m.order_id, m.reason);
    }
    fn on_bbo(&mut self, m: &BboUpdate, _c: &mut ActorContext) {
        println!("  [{}] BBO   bid {:?} ask {:?}", self.name, m.bid, m.ask);
    }
}
handle_messages!(Client,
    Ack       => on_ack,
    FillMsg   => on_fill,
    Canceled  => on_canceled,
    Rejected  => on_rejected,
    BboUpdate => on_bbo,
);

/// Perf benchmark for the core `OrderBook` (single thread; matching only —
/// excludes network/serialization). Run: `... --example matching_engine -- bench`.
fn run_bench() {
    use std::hint::black_box;
    use std::time::Instant;
    #[inline]
    fn lcg(s: u64) -> u64 {
        s.wrapping_mul(6364136223846793005).wrapping_add(1442695040888963407)
    }

    println!("=== matching engine perf (core OrderBook, release, single thread) ===");

    // Scenario A: resting inserts, no crossing — book grows to N live orders.
    {
        let n: u64 = 2_000_000;
        let mut book = OrderBook::<u64>::new();
        let mut s = 0x1234_5678_9abc_def0u64;
        for i in 0..50_000u64 {
            s = lcg(s);
            let (side, px) = if i & 1 == 0 { (Side::Buy, 1000 + s % 500) } else { (Side::Sell, 1500 + s % 500) };
            black_box(book.place(i + 1, side, px, 10, 0));
        }
        let t = Instant::now();
        for i in 0..n {
            s = lcg(s);
            let (side, px) = if i & 1 == 0 { (Side::Buy, 1000 + s % 500) } else { (Side::Sell, 1500 + s % 500) };
            black_box(book.place(100_000 + i + 1, side, px, 10, 0));
        }
        let el = t.elapsed();
        println!(
            "insert (no cross): {n} orders  {:.1} ns/order  {:.2} M orders/s  (book ~{n} live orders, ~1000 levels)",
            el.as_nanos() as f64 / n as f64,
            n as f64 / el.as_secs_f64() / 1e6,
        );
    }

    // Scenario B: fully-crossing orders — each generates one trade.
    {
        let m: u64 = 1_000_000;
        let mut book = OrderBook::<u64>::new();
        for i in 0..m {
            black_box(book.place(i + 1, Side::Sell, 1500, 1, 0)); // rest m asks @ 1500
        }
        let t = Instant::now();
        for i in 0..m {
            black_box(book.place(m + 1 + i, Side::Buy, 1500, 1, 0)); // each fills the front ask
        }
        let el = t.elapsed();
        println!(
            "match  (cross):    {m} orders  {:.1} ns/order  {:.2} M orders/s  {:.2} M trades/s",
            el.as_nanos() as f64 / m as f64,
            m as f64 / el.as_secs_f64() / 1e6,
            m as f64 / el.as_secs_f64() / 1e6,
        );
    }
    println!("(matching only; excludes network/serialization; each place() allocates its event Vec)");
}

/// The narrative scenario, on the core book, printing each step and the book.
fn run_scenario() {
    println!("=== matching engine scenario ===");
    let mut b = OrderBook::<&str>::new();
    fn show(label: &str, ev: &[Event<&str>]) {
        if !label.is_empty() {
            println!("{label}");
        }
        for e in ev {
            println!("      -> {e:?}");
        }
    }
    show("add bid 100 x1 (o1)", &b.place(1, Side::Buy, 100, 1, "c1"));
    show("add bid 100 x1 (o2)", &b.place(2, Side::Buy, 100, 1, "c2"));
    show("add bid 101 x2 (o3)", &b.place(3, Side::Buy, 101, 2, "c3"));
    show("add ask 102 x3 (o4)", &b.place(4, Side::Sell, 102, 3, "c4"));
    println!("\nbook now:");
    b.print_book(5);
    println!("\nadd ask 101 x1 (o5)  <- crosses best bid 101, expect a FILL:");
    show("", &b.place(5, Side::Sell, 101, 1, "c5"));
    println!("\nbook after fill:");
    b.print_book(5);
    println!("\ncancel o1 (bid 100 x1):");
    show("", &b.cancel(1, "c1"));
    println!("\ncancel-replace o3 (bid 101, remaining 1) -> bid 100 x5 (loses time priority):");
    show("", &b.replace(3, 100, 5, "c3"));
    println!("\nfinal book:");
    b.print_book(5);
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.iter().any(|a| a == "bench") {
        run_bench();
        return;
    }
    if args.iter().any(|a| a == "actor") {
        run_actor_demo();
        return;
    }
    run_scenario();
}

/// The actor-path demo: two client actors + the engine, driven via fast_send.
fn run_actor_demo() {
    println!("=== matching engine actor demo ===");
    let mut mgr = Manager::new();

    // Market-data printer, two clients (market maker + taker).
    let md = mgr.manage("MD", Box::new(Client { name: "MD" }), ThreadConfig::default());
    let mm = mgr.manage("MM", Box::new(Client { name: "MM" }), ThreadConfig::default());
    let tk = mgr.manage("TK", Box::new(Client { name: "TK" }), ThreadConfig::default());

    // Engine registered but driven inline via fast_send (so all resulting sends
    // land in the client mailboxes before we shut the clients down).
    let engine = mgr.manage("ENG", Box::new(MatchingEngine::new(Some(md))), ThreadConfig::default());
    mgr.init();

    println!("MM posts two asks at 5 (100 each):");
    engine.fast_send(&PlaceOrder { order_id: 1, side: Side::Sell, price: 5, qty: 100 }, Some(mm.clone()));
    engine.fast_send(&PlaceOrder { order_id: 2, side: Side::Sell, price: 5, qty: 100 }, Some(mm.clone()));

    println!("TK buys 150 @ 5 (crosses; 100 vs order 1, 50 vs order 2):");
    engine.fast_send(&PlaceOrder { order_id: 3, side: Side::Buy, price: 5, qty: 150 }, Some(tk.clone()));

    println!("TK cancels nothing; MM cancels the remainder of order 2:");
    engine.fast_send(&CancelOrder { order_id: 2 }, Some(mm.clone()));

    // Drain: end() queues Shutdown behind the already-delivered reports (FIFO)
    // and joins, so every printed line above is flushed.
    mgr.end();
    println!("=== done ===");
}

// ===========================================================================
// Tests — drive the deterministic core and assert exact event sequences.
// Run: cargo test --example matching_engine
// ===========================================================================

#[cfg(test)]
mod tests {
    use super::*;

    // Convenience: a book whose originator handle is a client name.
    fn book() -> OrderBook<&'static str> {
        OrderBook::new()
    }

    #[test]
    fn non_marketable_rests_with_ack_and_bbo() {
        let mut b = book();
        let ev = b.place(1, Side::Buy, 5, 100, "mm");
        assert_eq!(
            ev,
            vec![
                Event::Ack { to: "mm", order_id: 1, resting_qty: 100 },
                Event::Bbo { bid: Some((5, 100)), ask: None },
            ]
        );
    }

    #[test]
    fn price_time_priority_fills_earliest_first() {
        let mut b = book();
        b.place(1, Side::Sell, 5, 100, "a"); // earlier
        b.place(2, Side::Sell, 5, 100, "b"); // later, same price
        let ev = b.place(3, Side::Buy, 5, 150, "t");
        // 100 vs order 1 (earlier), then 50 vs order 2.
        assert_eq!(
            ev,
            vec![
                Event::Fill { to: "a", order_id: 1, side: Side::Sell, price: 5, qty: 100, leaves_qty: 0, counter_order_id: 3, trade_id: 1 },
                Event::Fill { to: "t", order_id: 3, side: Side::Buy, price: 5, qty: 100, leaves_qty: 50, counter_order_id: 1, trade_id: 1 },
                Event::Fill { to: "b", order_id: 2, side: Side::Sell, price: 5, qty: 50, leaves_qty: 50, counter_order_id: 3, trade_id: 2 },
                Event::Fill { to: "t", order_id: 3, side: Side::Buy, price: 5, qty: 50, leaves_qty: 0, counter_order_id: 2, trade_id: 2 },
                // taker fully filled -> no Ack; top-of-book ask went 200 -> 50.
                Event::Bbo { bid: None, ask: Some((5, 50)) },
            ]
        );
    }

    #[test]
    fn partial_fill_then_rests() {
        let mut b = book();
        b.place(1, Side::Sell, 5, 40, "a");
        let ev = b.place(2, Side::Buy, 5, 100, "t"); // buys 40, rests 60 @ 5
        assert_eq!(
            ev,
            vec![
                Event::Fill { to: "a", order_id: 1, side: Side::Sell, price: 5, qty: 40, leaves_qty: 0, counter_order_id: 2, trade_id: 1 },
                Event::Fill { to: "t", order_id: 2, side: Side::Buy, price: 5, qty: 40, leaves_qty: 60, counter_order_id: 1, trade_id: 1 },
                Event::Ack { to: "t", order_id: 2, resting_qty: 60 },
                Event::Bbo { bid: Some((5, 60)), ask: None },
            ]
        );
    }

    #[test]
    fn taker_gets_price_improvement_at_maker_price() {
        let mut b = book();
        b.place(1, Side::Sell, 5, 100, "a"); // best ask 5
        let ev = b.place(2, Side::Buy, 9, 100, "t"); // willing to pay 9, fills at 5
        let exec = ev.iter().find_map(|e| match e {
            Event::Fill { to: "t", price, .. } => Some(*price),
            _ => None,
        });
        assert_eq!(exec, Some(5), "taker executes at the maker's price");
    }

    #[test]
    fn cancel_removes_and_reports() {
        let mut b = book();
        b.place(1, Side::Buy, 5, 100, "a");
        let ev = b.cancel(1, "a");
        assert_eq!(
            ev,
            vec![
                Event::Canceled { to: "a", order_id: 1, canceled_qty: 100 },
                Event::Bbo { bid: None, ask: None },
            ]
        );
        // Nothing left to hit.
        assert_eq!(b.place(2, Side::Sell, 5, 10, "t"), vec![
            Event::Ack { to: "t", order_id: 2, resting_qty: 10 },
            Event::Bbo { bid: None, ask: Some((5, 10)) },
        ]);
    }

    #[test]
    fn cancel_unknown_is_rejected() {
        let mut b = book();
        assert_eq!(
            b.cancel(999, "x"),
            vec![Event::Rejected { to: "x", order_id: 999, reason: "cancel: unknown order" }]
        );
    }

    #[test]
    fn replace_loses_time_priority() {
        let mut b = book();
        b.place(1, Side::Sell, 5, 100, "a"); // earlier at 5
        b.place(2, Side::Sell, 5, 100, "b"); // later at 5
        // Replace order 1 to the same price 5: it re-queues behind order 2.
        let rep = b.replace(1, 5, 100, "a");
        assert_eq!(rep[0], Event::Replaced { to: "a", order_id: 1, new_price: 5, new_qty: 100 });
        // Now a buyer for 100 should hit order 2 first (it is now earlier).
        let ev = b.place(3, Side::Buy, 5, 100, "t");
        let first_maker = ev.iter().find_map(|e| match e {
            Event::Fill { to, side: Side::Sell, .. } => Some(*to),
            _ => None,
        });
        assert_eq!(first_maker, Some("b"), "replaced order lost its time priority");
    }

    #[test]
    fn bad_input_and_duplicate_id_rejected() {
        let mut b = book();
        assert_eq!(
            b.place(1, Side::Buy, 0, 100, "a"),
            vec![Event::Rejected { to: "a", order_id: 1, reason: "price and qty must be positive" }]
        );
        assert_eq!(
            b.place(1, Side::Buy, 5, 0, "a"),
            vec![Event::Rejected { to: "a", order_id: 1, reason: "price and qty must be positive" }]
        );
        b.place(7, Side::Buy, 5, 100, "a");
        assert_eq!(
            b.place(7, Side::Buy, 6, 100, "a"),
            vec![Event::Rejected { to: "a", order_id: 7, reason: "duplicate order_id" }]
        );
    }

    // -------- book integrity / crossing --------

    fn lcg(s: u64) -> u64 {
        s.wrapping_mul(6364136223846793005).wrapping_add(1442695040888963407)
    }

    /// Assert the book invariants hold. THE key one: the book is **never crossed**
    /// (a resting bid is never >= a resting ask) — any cross must have produced
    /// fills on entry, not rested. Plus: no empty levels linger, every resting
    /// order is indexed to its (side, price), and the index count matches.
    fn assert_book_ok<H>(b: &OrderBook<H>) {
        if let (Some(bid), Some(ask)) = (b.bids.keys().next_back(), b.asks.keys().next()) {
            assert!(bid < ask, "BOOK CROSSED: best_bid {bid} >= best_ask {ask}");
        }
        let mut counted = 0usize;
        for (px, lvl) in b.bids.iter() {
            assert!(!lvl.orders.is_empty(), "empty bid level {px}");
            let mut sum = 0u64;
            for o in lvl.orders.iter() {
                assert!(o.qty > 0, "bid order {} has qty 0", o.order_id);
                assert_eq!(b.index.get(&o.order_id), Some(&(Side::Buy, *px)), "index mismatch bid {}", o.order_id);
                counted += 1;
                sum += o.qty;
            }
            assert_eq!(sum, lvl.total, "bid level {px} running total out of sync");
        }
        for (px, lvl) in b.asks.iter() {
            assert!(!lvl.orders.is_empty(), "empty ask level {px}");
            let mut sum = 0u64;
            for o in lvl.orders.iter() {
                assert!(o.qty > 0, "ask order {} has qty 0", o.order_id);
                assert_eq!(b.index.get(&o.order_id), Some(&(Side::Sell, *px)), "index mismatch ask {}", o.order_id);
                counted += 1;
                sum += o.qty;
            }
            assert_eq!(sum, lvl.total, "ask level {px} running total out of sync");
        }
        assert_eq!(counted, b.index.len(), "index size {} != resting order count {counted}", b.index.len());
    }

    #[test]
    fn a_cross_always_generates_fills_and_never_rests_crossed() {
        let mut b = book();
        b.place(1, Side::Buy, 10, 100, "buyer"); // resting bid @ 10
        assert_book_ok(&b);
        // A SELL @ 8 is marketable against the bid @ 10 (8 <= 10). It must MATCH,
        // producing fills at the MAKER's price (10) — not sit in the book crossed.
        let ev = b.place(2, Side::Sell, 8, 100, "seller");
        let fills = ev.iter().filter(|e| matches!(e, Event::Fill { .. })).count();
        assert_eq!(fills, 2, "a cross must generate fills on both sides");
        assert!(ev.iter().any(|e| matches!(e, Event::Fill { to: "seller", price: 10, qty: 100, .. })));
        assert!(ev.iter().any(|e| matches!(e, Event::Fill { to: "buyer", price: 10, qty: 100, .. })));
        assert!(!ev.iter().any(|e| matches!(e, Event::Ack { .. })), "fully filled -> nothing rests");
        assert!(b.bids.is_empty() && b.asks.is_empty(), "book empty after full match");
        assert_book_ok(&b);
    }

    #[test]
    fn aggressive_order_sweeps_multiple_levels_fills_propagate() {
        let mut b = book();
        b.place(1, Side::Sell, 5, 50, "m5");
        b.place(2, Side::Sell, 6, 50, "m6");
        b.place(3, Side::Sell, 7, 50, "m7");
        assert_book_ok(&b);
        // Aggressive buy 120 @ 7 sweeps 5(50) + 6(50) + 7(20); each maker filled at
        // its own price; taker's total = 120; level 7 keeps 30 resting.
        let ev = b.place(4, Side::Buy, 7, 120, "taker");
        assert!(ev.iter().any(|e| matches!(e, Event::Fill { to: "m5", price: 5, qty: 50, .. })));
        assert!(ev.iter().any(|e| matches!(e, Event::Fill { to: "m6", price: 6, qty: 50, .. })));
        assert!(ev.iter().any(|e| matches!(e, Event::Fill { to: "m7", price: 7, qty: 20, .. })));
        let taker: u64 = ev.iter().filter_map(|e| match e {
            Event::Fill { to: "taker", qty, .. } => Some(*qty),
            _ => None,
        }).sum();
        assert_eq!(taker, 120, "aggressor fully filled across levels");
        assert!(b.bids.is_empty(), "taker fully filled, nothing rests");
        assert_eq!(b.asks.get(&7).map(|l| l.total), Some(30));
        assert_book_ok(&b);
    }

    #[test]
    fn book_integrity_over_20k_random_ops() {
        // Send a long, crossing-heavy pseudo-random stream of place/cancel and
        // assert the invariants after EVERY op — in particular that the book is
        // never left crossed.
        let mut b = book();
        let mut s = 0x00C0FFEE_1234_5678u64;
        let mut next_id = 1u64;
        let mut live: Vec<u64> = Vec::new();
        for _ in 0..20_000 {
            s = lcg(s);
            if s % 3 == 0 && !live.is_empty() {
                let idx = (lcg(s) as usize) % live.len();
                let oid = live.swap_remove(idx);
                let _ = b.cancel(oid, "o"); // may be already-filled -> harmless Rejected
            } else {
                let side = if s & 1 == 0 { Side::Buy } else { Side::Sell };
                // Overlapping price band 50..69 for BOTH sides -> lots of crossings.
                let price = 50 + (lcg(s) % 20);
                let qty = 1 + (lcg(s ^ 0x9E37) % 10);
                let id = next_id;
                next_id += 1;
                let ev = b.place(id, side, price, qty, "o");
                if ev.iter().any(|e| matches!(e, Event::Ack { order_id, .. } if *order_id == id)) {
                    live.push(id);
                }
            }
            assert_book_ok(&b);
        }
    }

    #[test]
    fn scenario_add_bids_asks_cross_fill_cancel_replace() {
        let mut b = book();
        b.place(1, Side::Buy, 100, 1, "c1");
        b.place(2, Side::Buy, 100, 1, "c2");
        b.place(3, Side::Buy, 101, 2, "c3");
        b.place(4, Side::Sell, 102, 3, "c4");
        assert_book_ok(&b);
        assert_eq!(b.bids.keys().next_back(), Some(&101), "best bid 101");
        assert_eq!(b.asks.keys().next(), Some(&102), "best ask 102 (not crossed)");

        // Aggressive ask 101 x1 crosses the best bid (101) -> FILL at the maker price 101.
        let ev = b.place(5, Side::Sell, 101, 1, "c5");
        assert!(ev.iter().any(|e| matches!(e,
            Event::Fill { to: "c3", order_id: 3, price: 101, qty: 1, leaves_qty: 1, .. })),
            "resting bid o3 filled 1 at 101");
        assert!(ev.iter().any(|e| matches!(e,
            Event::Fill { to: "c5", order_id: 5, price: 101, qty: 1, leaves_qty: 0, .. })),
            "aggressor o5 filled 1 at 101");
        assert!(!ev.iter().any(|e| matches!(e, Event::Ack { .. })), "ask fully filled -> does not rest");
        assert_book_ok(&b);

        // Cancel o1 (bid 100 x1) — level 100 still holds o2.
        let c = b.cancel(1, "c1");
        assert!(c.iter().any(|e| matches!(e, Event::Canceled { order_id: 1, canceled_qty: 1, .. })));
        assert_book_ok(&b);

        // Cancel-replace o3 (bid 101, remaining 1) -> bid 100 x5 (loses time priority).
        let r = b.replace(3, 100, 5, "c3");
        assert!(r.iter().any(|e| matches!(e, Event::Replaced { order_id: 3, new_price: 100, new_qty: 5, .. })));
        assert_book_ok(&b);
        assert_eq!(b.bids.keys().next_back(), Some(&100), "101 level gone; best bid now 100");
        assert_eq!(b.asks.keys().next(), Some(&102));
    }
}

// ===========================================================================
// Actor-level tests — drive the MatchingEngine ACTOR end-to-end and assert the
// exact outbound execution-report sequence. All events route to one recording
// actor (so ordering is deterministic); `fast_send` drives the engine inline and
// `mgr.end()` flushes every emitted message before we assert.
// Run: cargo test --example matching_engine
// ===========================================================================

#[cfg(test)]
mod actor_tests {
    use super::*;
    use std::sync::{Arc, Mutex};

    /// A normalized record of every outbound message the engine emits.
    #[derive(Clone, Debug, PartialEq, Eq)]
    enum Rec {
        Ack { order_id: u64, resting_qty: u64 },
        Fill { order_id: u64, price: u64, qty: u64, leaves: u64, cpty: u64 },
        Canceled { order_id: u64, canceled_qty: u64 },
        Rejected { order_id: u64, reason: &'static str },
        Bbo { bid: Option<(u64, u64)>, ask: Option<(u64, u64)> },
    }

    /// Records every execution report / BBO it receives into a shared log.
    struct Recorder {
        log: Arc<Mutex<Vec<Rec>>>,
    }
    impl Recorder {
        fn on_ack(&mut self, m: &Ack, _c: &mut ActorContext) {
            self.log.lock().unwrap().push(Rec::Ack { order_id: m.order_id, resting_qty: m.resting_qty });
        }
        fn on_fill(&mut self, m: &FillMsg, _c: &mut ActorContext) {
            self.log.lock().unwrap().push(Rec::Fill {
                order_id: m.order_id, price: m.price, qty: m.qty, leaves: m.leaves_qty, cpty: m.counter_order_id,
            });
        }
        fn on_canceled(&mut self, m: &Canceled, _c: &mut ActorContext) {
            self.log.lock().unwrap().push(Rec::Canceled { order_id: m.order_id, canceled_qty: m.canceled_qty });
        }
        fn on_rejected(&mut self, m: &Rejected, _c: &mut ActorContext) {
            self.log.lock().unwrap().push(Rec::Rejected { order_id: m.order_id, reason: m.reason });
        }
        fn on_bbo(&mut self, m: &BboUpdate, _c: &mut ActorContext) {
            self.log.lock().unwrap().push(Rec::Bbo { bid: m.bid, ask: m.ask });
        }
    }
    handle_messages!(Recorder,
        Ack       => on_ack,
        FillMsg   => on_fill,
        Canceled  => on_canceled,
        Rejected  => on_rejected,
        BboUpdate => on_bbo,
    );

    /// Build engine + one recorder (as originator AND md feed), run a closure that
    /// drives the engine via fast_send, flush, and return the recorded sequence.
    fn drive(f: impl FnOnce(&ActorRef, &ActorRef)) -> Vec<Rec> {
        let log = Arc::new(Mutex::new(Vec::new()));
        let mut mgr = Manager::new();
        let rec = mgr.manage("rec", Box::new(Recorder { log: log.clone() }), ThreadConfig::default());
        let eng = mgr.manage("eng", Box::new(MatchingEngine::new(Some(rec.clone()))), ThreadConfig::default());
        mgr.init();
        f(&eng, &rec);
        mgr.end(); // FIFO flush + join: every emitted report is processed before we read the log.
        let out = log.lock().unwrap().clone();
        out
    }

    #[test]
    fn end_to_end_price_time_priority_and_partial_fill() {
        let got = drive(|eng, rec| {
            eng.fast_send(&PlaceOrder { order_id: 1, side: Side::Sell, price: 5, qty: 100 }, Some(rec.clone()));
            eng.fast_send(&PlaceOrder { order_id: 2, side: Side::Sell, price: 5, qty: 100 }, Some(rec.clone()));
            eng.fast_send(&PlaceOrder { order_id: 3, side: Side::Buy, price: 5, qty: 150 }, Some(rec.clone()));
        });
        assert_eq!(
            got,
            vec![
                Rec::Ack { order_id: 1, resting_qty: 100 },
                Rec::Bbo { bid: None, ask: Some((5, 100)) },
                Rec::Ack { order_id: 2, resting_qty: 100 },
                Rec::Bbo { bid: None, ask: Some((5, 200)) },
                Rec::Fill { order_id: 1, price: 5, qty: 100, leaves: 0, cpty: 3 },
                Rec::Fill { order_id: 3, price: 5, qty: 100, leaves: 50, cpty: 1 },
                Rec::Fill { order_id: 2, price: 5, qty: 50, leaves: 50, cpty: 3 },
                Rec::Fill { order_id: 3, price: 5, qty: 50, leaves: 0, cpty: 2 },
                Rec::Bbo { bid: None, ask: Some((5, 50)) },
            ]
        );
    }

    #[test]
    fn end_to_end_cancel() {
        let got = drive(|eng, rec| {
            eng.fast_send(&PlaceOrder { order_id: 1, side: Side::Buy, price: 5, qty: 100 }, Some(rec.clone()));
            eng.fast_send(&CancelOrder { order_id: 1 }, Some(rec.clone()));
        });
        assert_eq!(
            got,
            vec![
                Rec::Ack { order_id: 1, resting_qty: 100 },
                Rec::Bbo { bid: Some((5, 100)), ask: None },
                Rec::Canceled { order_id: 1, canceled_qty: 100 },
                Rec::Bbo { bid: None, ask: None },
            ]
        );
    }

    #[test]
    fn end_to_end_reject_bad_order() {
        let got = drive(|eng, rec| {
            eng.fast_send(&PlaceOrder { order_id: 1, side: Side::Buy, price: 0, qty: 100 }, Some(rec.clone()));
            eng.fast_send(&CancelOrder { order_id: 999 }, Some(rec.clone()));
        });
        assert_eq!(
            got,
            vec![
                Rec::Rejected { order_id: 1, reason: "price and qty must be positive" },
                Rec::Rejected { order_id: 999, reason: "cancel: unknown order" },
            ]
        );
    }
}
