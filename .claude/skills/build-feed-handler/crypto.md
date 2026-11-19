# Crypto exchanges: generating and testing handlers

Crypto exchanges differ from the exchange feeds in `binary_codegen.md` in two ways
that change the work. Their APIs are JSON over WebSocket and REST, so decoders are
written by hand rather than generated from a field table (a few offer binary or
SBE streams; use `genschema/` or a field table for those). And their market data is
public and live, and most run free testnets for order entry, so a handler can be
checked against the venue itself, not only against a specification.

## Templates

| Part | Template |
|---|---|
| WebSocket client (TLS, reconnect, ping) | `sim/src/BinanceFeed.cpp`, `CoinbaseFeed.cpp`, `HyperliquidFeed.cpp` (Boost.Beast) |
| book synchronisation: REST snapshot plus diff stream with a sequence rule | `sim/src/BinanceSync.cpp` |
| full snapshot on subscribe, then updates with a per-connection sequence | `sim/src/CoinbaseSync.cpp` |
| periodic whole books, diffed | `sim/src/HyperliquidSync.cpp` |
| price levels to synthetic orders for OB | `sim/src/Mbp2Mbo.cpp`; `sim/src/LevelWindow.cpp` when the venue sends more levels than are kept |
| per-order (L3) books | the MBO records of `kaspar_mapping.md` directly, as below |
| tests from recorded JSON | `unit_test/src/test_binance_sync.cpp`, `test_crypto_sync.cpp`, `test_mbp2mbo.cpp`, `test_mbp2mbo_ob.cpp` |
| order manager | `order_entry.md`, on `frame::som::BrokerOrderBook` |
| fills and trade capture | `post_trade.md` |

Keep the parsing and sequencing in a plain class (`<Venue>Sync`) with no network,
as the templates do, so that it is tested from recorded messages; the actor only
owns the connection.

## Per-order (L3) books

Some exchanges publish every order (Coinbase's `full` and `level3` channels). Such
a feed maps to `l3_mbo_v2_t` records without `Mbp2Mbo`, and differs from exchange
MBO feeds in three ways:

- Order ids are strings (UUIDs). The sync class maps each to a 64-bit `orderID`
  when the order is added and forgets it on its delete; a snapshot rebuilds the
  map.
- The snapshot is the whole book, prices far from the market included, so OB's
  ladder must cover it: set `ladder_ticks` (a ring around the best prices; orders
  outside it are kept and placed when they move inside) rather than a full ladder
  of `maxpx` ticks at a fine tick.
- Sizes are fractional: scale them by the instrument's unit (`kaspar_mapping.md`,
  quantities).

## What to extract

The feed description (`feed_template.md`) applies. For crypto, section 5 must state
precisely:

- how a book is built: REST snapshot plus buffered diffs (and the rule that aligns
  them, which differs between products of one exchange: Binance spot `U <= L+1 <= u`,
  futures `U <= L <= u`), or a WebSocket snapshot followed by updates;
- the sequence fields, their scope (per symbol, per connection, per channel) and the
  gap rule;
- any checksum the venue sends over the top of the book (OKX, Kraken, Bitfinex and
  others send a CRC32 of the top levels), its exact string format, and when it is
  computed;
- whether updates are absolute sizes or deltas, and what a size of zero means;
- the trade side (aggressor or maker);
- depth and update-rate options, and which are chosen;
- connection limits: ping interval, idle timeout, forced reconnects, rate limits.

The order-entry and post-trade sections also cover the authentication scheme
(HMAC-SHA256, Ed25519, RSA, wallet signatures), the clock tolerance on signed
requests, client order id rules, amend semantics, and how fills arrive (private
WebSocket stream, REST history) with their stable identifiers.

## Testing ladder

A handler is checked in this order; each step is reported with its numbers.

1. **Recorded messages.** Unit tests from messages recorded from the live public
   stream (a recorder that writes raw frames to a file is part of the handler) and
   from the documentation's examples: snapshot alignment, gap detection and resync,
   absolute versus delta updates, trade side, every message type.
2. **Live public market data, no account.** Run the sync class on the live stream
   for at least 30 minutes per product and check, continuously:
   - the venue's checksum on every update, where it sends one (a mismatch is a bug);
   - against REST snapshots fetched every 10 seconds: where the snapshot carries a
     sequence number that the stream also carries (Binance `lastUpdateId`), compare
     the book at exactly that sequence, level by level, over the snapshot's depth;
     where it does not, compare the top levels allowing for the time between them,
     and report the share that match;
   - no crossed book, sequence continuity, and the number of resyncs and why.
   Then run the full `bnsim`-style chain (sync, `Mbp2Mbo`, OB, lights, SOM) on the
   live stream and check OB's levels against the sync class's book.

   The snapshot interval is the venue's REST limit where that is stricter than 10
   seconds (a full L3 snapshot is limited to a few requests a minute on some
   venues); the report states the interval used. REST books may be served from a
   CDN cache: a snapshot is used only if its sequence number or timestamp is newer
   than the previous one's.

   Where the per-order channel needs a key (Coinbase `level3` and `full`) and none
   has been supplied, check what public data allows: take a public REST snapshot,
   apply the public trades that follow it to the book as the handler would, and
   compare the levels the trades touched with the next public snapshot. This tests
   trade handling and snapshot parsing, not the order stream, and the report says
   so.
3. **Testnet order entry and fills.** With testnet or demo keys the owner supplies:
   logon, one order of each supported type far from the market, amend, cancel, an
   order that fills against the testnet book, and a reconnect; check every SOM
   message the order manager emits against the venue's own order and fill history
   fetched over REST, and the post-trade client's trade capture against the same.
4. **Production.** Never by the agent. A production account is used only by the
   owner, after review.

## Keys

- Keys are never written into the repository, a commit, a log, or a report. They
  are read at run time from a file outside the tree, by default
  `~/.kaspar/keys/<venue>.json`, or from environment variables, and the handler's
  README says which.
- The agent asks the owner for testnet keys; it never creates an account.
- Code that signs requests is tested against the documentation's signature examples
  before it sends anything. Where the documentation gives none, the test vectors are
  computed independently of the handler's code (with `openssl dgst` or Python's
  `hmac` and `hashlib`, from the documented string-to-sign), and the report says
  the vectors are not the venue's.
- A market-data channel that needs a key is run live only with a key the owner
  supplied, as order entry is.

## Rules

- Public market data may be read live without asking: it needs no account.
- Order entry runs only against a testnet or demo environment, with keys the owner
  supplied for it. The base URL of the testnet is fixed in the configuration, and
  the handler refuses to start if it is pointed at a production host while
  `testnet = true`.
- Respect the venue's rate limits in every test, including the REST snapshot
  comparison.
