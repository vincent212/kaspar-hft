# Crypto exchange connectivity survey for Kaspar

Researched 2026-11-18 from public documentation and public unauthenticated data. No accounts were created and no authenticated endpoint was called. Every fact in the per-exchange sections carries the URL it came from. The matrix summarises those sections and has no citations of its own. "Not confirmed" means the fact could not be found in official documentation. Where two official pages contradict each other, both are given.

What kaspar-hft already has (`sim/BINANCE.md`, `sim/CRYPTO.md`): live market data for Binance spot and USD-M (`BinanceFeed`/`BinanceSync`, REST snapshot + diff), Coinbase Advanced Trade `level2` and Hyperliquid `l2Book`. All three are price-level feeds that `Mbp2Mbo` cuts into synthetic per-order chunks before OB. There is no crypto order entry or post-trade handler.

What a handler must produce (`build-feed-handler/kaspar_mapping.md`, `order_entry.md`, `post_trade.md`): L3 records (`l3_mbo_v2_t` add/change/delete, `l3_mbo_trd_v2_t` per-order executions, `l3_mbp_trd_t` prints with aggressor side, `l3_gap_v2_t`, `l3_chr_v2_t`). An order manager behind the SOM (`Order`/`Cancel` → `Ack`/`Fill`/`Reject`/`CancAck`/`CancReject`) built on `BrokerOrderBook`. A post-trade client emitting one normalized trade message, idempotent on a stable trade id.

## Summary matrix

Abbreviations: **L3** = order-by-order; **FD** = full-depth price levels; **N-lvl** = top N price levels; **seq** = sequence number; **COD** = cancel-on-disconnect; **DMS** = dead-man's switch (countdown timer); **DC** = drop copy.

| Exchange / product | Book granularity, transport | Sync and gap rule | Trade side | Order entry API, auth | COD / DMS | Post-trade | Test env (self-serve? OE + fills?) | Free historical data | Machine-readable docs | Volume (CoinGecko, 2026-11-18) | In kaspar-hft |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Binance Spot | FD (5000-level snapshot) JSON WS, SBE WS (20 ms diffs, Ed25519 key), FIX 4.4 MD, FIX-SBE. No L3 | REST snapshot + diffs; gap if `U` > last `u`+1. No checksum | `m` = buyer is maker; FIX gives AggressorSide | REST, WS API (JSON or SBE), FIX 4.4 / FIX-SBE. Ed25519 / HMAC / RSA; FIX Ed25519 only | None documented | WS API user data stream (JSON/SBE); FIX DC (1 s delayed); `myTrades` | Testnet: free, REST/WS/SBE/FIX; own book, not the live one | Trades, aggTrades, klines (data.binance.vision); no book | SBE XML, QuickFIX XML, OpenAPI, C++ SBE sample | #1 spot | MD (JSON L2) |
| Binance USD-M / COIN-M | FD (1000-level snapshot), JSON WS only. RPI orders excluded from depth | Snapshot + diffs; `pu` == previous `u` | aggTrade only (100 ms), `m` = buyer is maker | REST, WS API; HMAC/RSA, Ed25519 on WS | `countdownCancelAll` per symbol | listenKey user stream; `userTrades` (3 months) | Demo `demo-fapi`/`demo-dapi`; realism not confirmed | Trades, aggTrades, bookTicker (2023-05..2024-03 only), bookDepth (±% buckets, not a book) | Web docs + llms export; no futures OpenAPI file found | #1 derivatives | MD USD-M (JSON L2) |
| Coinbase Exchange (spot) | **L3** WS `full`/`level3` (auth) and FIX 5.0 SP2 MD L3; L2 WS | REST `book?level=3` + discard seq <= snapshot seq; per-product seq; FIX `RptSeq` per symbol | WS `match.side` = maker; FIX AggressorSide | REST (HMAC), FIX 5.0 SP2 (HMAC logon). No WS OE | FIX tag 8013 (session or profile) | WS `user`; FIX DC; REST `/fills` | Sandbox: self-serve, WS + FIX OE/MD/DC; subset of products | None found | FIX XML dictionaries, OpenAPI | Large spot, rank not confirmed | No (Advanced Trade L2 instead) |
| Coinbase Advanced Trade | N-lvl/FD L2 JSON WS | WS snapshot + updates; `sequence_num` scope contradicts across pages | maker side | REST, JWT (ES256; Ed25519 contradicted) | Not confirmed | WS `user`; REST fills | Sandbox is static mocked responses | None | OpenAPI + AsyncAPI | Shares Coinbase spot (not confirmed) | MD (L2) |
| Coinbase Derivatives (CDE, US futures) | **L3 SBE multicast** (CH4 cross-connect), FIX 4.4 MD L3 | Packet seq never reset; per-instrument `instrSeqNum`; retransmit service | aggressor | FIX 4.4; SBE OE for lead MMs | Optional; SBE implicit | FIX 4.4 DC (with busts/corrections) | Integration env needs certification | None | SBE XML, FIX XML, OpenAPI | Not confirmed | No |
| Hyperliquid | Public: 20-level L2 snapshots (5 with `fast`), no seq. L4 per-order diffs only from own node | Full snapshot each push; no seq | aggressor | HTTP/WS `exchange`, EIP-712 wallet signing, agent wallets | `scheduleCancel` (>= 5 s, 10/day); no COD | `userFills`, `orderUpdates`; REST fills (2000-10000 cap) | Testnet: faucet needs a mainnet-deposit address | S3 requester-pays: hourly L2 snapshots, fills, all L1 txs | GitBook, Python SDK; no OpenAPI | ~80% of DeFi perps (2025) | MD (L2) |
| Bybit V5 | FD `orderbook.full` (200 ms, REST 10,000-level snapshot), 1/50/200/1000 levels; SBE MD on MM gateway only | `u` contiguous, `seq` cross-sequence; `u`=1 reset | taker | REST, WS trade; FIX 4.4 spot by whitelist (RSA); SBE OE MM only | DCP (institutional only) | WS `execution`; `/v5/execution/list` (2 y) | Testnet + demo (demo has no WS trade) | **200-level book snapshot+deltas with `u`/`seq`**, trades | SBE XML in pages, FIX XML | #7 deriv by OI | No |
| OKX V5 | 400-level tbt (10 ms) needs VIP4; `books` 400 @ 100 ms; SBE MD (VIP4) | `prevSeqId` == previous `seqId`, per instrument; checksum deprecated (0) | taker | REST, WS; HMAC. No FIX | Cancel-All-After 10-120 s | WS `orders` (`fills` VIP4); fills-history 3 months | Demo (header), self-serve | **400-level snapshot+updates (~10 ms), 5000-level**, trades; no seqId in files | SBE XML download | #2 derivatives | No |
| Bitget | V3 full book up to 1000 levels (50 ms), SBE Depth50 public | `pseq` == previous `seq` | not confirmed | REST, WS; HMAC/RSA. FIX only for CFDs | Countdown 5-60 s (UTA) | Fill / fast-fill channels | Demo (header), needs KYC | 500-level snapshots ~21 s apart, trades | SBE XML in pages | #6 spot (trust), #13 deriv | No |
| Kraken Spot | **L3** WS `level3` (needs API key; depth 10/100/1000), L2 `book` up to 1000; FIX 4.4 MD | No seq; CRC32 checksum of top 10 (L3 checksum includes queue order) | taker | REST, WS v2, FIX 4.4 (onboarding); HMAC-SHA512 | FIX COD on by default; WS `cancel_all_orders_after` | WS `executions`; FIX replay; `TradesHistory` | No spot testnet; FIX UAT by onboarding | **Time & Sales CSV, all pairs** (quarterly) | Web, OpenAPI for REST | Trust rank 3 | No |
| Kraken Futures / US Perps | Futures: L2 WS with `seq`. US Perps: **L3 SBE** with per-instrument seq and `orderPriority` | SBE: buffer + snapshot, no replay | taker / aggressor | REST, FIX; Unified WS | DMS | Fills REST; US Perps FIX DC | **Futures demo: free, self-serve** | Not confirmed | SBE per page | Not confirmed | No |
| Deribit | JSON-RPC FD L2 (`raw` needs auth). **L3 SBE multicast (Starbase)**, LD4 or PrivateLink, explicit `sortOrderId` | JSON: `prev_change_id` == previous `change_id`. SBE: per-channel seq, snapshot channel, retransmit | taker | JSON-RPC WS/HTTP, FIX 4.4, Starbase SBE TCP; HMAC / Ed25519 / RSA | Opt-in COD (connection or account) | `user.trades`; `get_user_trades_*`; Starbase FIX 5.0 DC | test.deribit.com with OE; free sign-up not confirmed | Trades via history.deribit.com (undocumented); **free Starbase MD PCAP** | **SBE XML, OpenAPI, AsyncAPI, FIX50 XML** | ~122k BTC/day, options venue | No |
| Bitstamp | **L3** WS `live_orders`; REST full book with order ids (`group=2`); FIX MD L2 (20) | No seq documented; gap-recovery endpoint (auth) by MarketEventID since 2026-02 | not confirmed | REST, FIX 4.4 (no 35=G); HMAC | FIX cancel on logout | WS `my_trades`; `user_transactions` | sandbox.bitstamp.net (own book); sign-up not confirmed | None | OpenAPI (REST) | Trust rank 7, ~4.2k BTC | No |
| Gemini | FD L2 WS (`U`/`u`), FIX MD full depth. No L3 | Snapshot on connect; gap if `U` > last `u` | `m` = buyer is maker | REST, WS, FIX 4.4; HMAC-SHA384. No amend | WS and FIX COD; REST heartbeat | WS orders; FIX DC (trades only) | Sandbox self-serve, bot-driven book | None | OpenAPI + AsyncAPI | Trust rank 25, ~382 BTC | No |
| Bitfinex | **L3 top-N** (R0, max 250 orders), P0-P4 aggregated | WS snapshot; CRC32 top 25; `SEQ_ALL` beta, per connection | aggressor | Auth WS / REST; HMAC-SHA384. Public FIX deprecated | `dms:4` (account-wide on socket close) | WS channel 0; trades/hist | Paper sub-account (TEST pairs) | None | readme.io with OpenAPI | Trust rank 62, ~2.8k BTC | No |
| Crypto.com Exchange | 10/50 levels WS, FIX MD 150 (PrivateLink only) | `pu` == previous `u`, per instrument | taker | REST, WS, FIX 4.4 (PrivateLink); HMAC | WS and FIX COD | WS `user.trade`; FIX DC; FIX `MatchIndex` queue position | UAT invitation-only | None | FIX XML dictionary | Trust rank 10, ~15.1k BTC | No |
| dYdX v4 | **L3** gRPC/WS stream from own full node | Snapshot flag then increments; optimistic updates can roll back | not confirmed | Signed Cosmos txs (not verified) | not confirmed | ClobMatch in stream | not confirmed | not confirmed | Web docs | not confirmed | No |
| Bitso | **L3** WS `diff-orders`, REST full book with `oid` and `sequence` | Sync rule not stated | not confirmed | not surveyed | not surveyed | not surveyed | not surveyed | not surveyed | Web docs | ~301 BTC | No |
| Others with FIX or binary | LMAX Digital (FIX, ITCH, LD4; specs not public), Bullish (FIX OE/DC, MD L1/L2 WS), Bitvavo (FIX OE/DC with XML dictionary; non-conflated L2), MEXC (protobuf L2) | | | | | | | | | | No |

## Binance

### Spot market data
- Transports: JSON WS `stream.binance.com`, SBE WS `stream-sbe.binance.com`, FIX 4.4 MD `fix-md.binance.com:9000`, FIX-SBE on ports 9001 (FIX in, SBE out) and 9002 (SBE both ways) (https://github.com/binance/binance-spot-api-docs/blob/master/web-socket-streams.md, .../sbe-market-data-streams.md, .../fix-api.md).
- No L3 for any Binance product. FIX `MarketDataRequest` accepts only `AggregatedBook=Y`, "one book entry per side per price" (https://github.com/binance/binance-spot-api-docs/blob/master/fix-api.md). No multicast or colocation feed is documented (https://developers.binance.com/en/docs/llms-full.txt).
- SBE streams need an Ed25519 API key in `X-MBX-APIKEY`, with no signature. Timestamps are in µs. Messages: Trades (10000), BestBidAsk (10001), DepthSnapshot top 20 (10002), DepthDiff (10003) (https://github.com/binance/binance-spot-api-docs/blob/master/sbe-market-data-streams.md, .../sbe/schemas/stream_1_0.xml).
- Update rates:
  - JSON diffs: 1000 ms or 100 ms.
  - SBE diffs: 20 ms since 2026-08-04 (https://github.com/binance/binance-spot-api-docs/blob/master/CHANGELOG.md).
  - SBE `@bestBidAsk` drops older undelivered events for a symbol under load (sbe-market-data-streams.md above).
- Sync rule (corrected 2025-11-12):
  1. Buffer the diff stream.
  2. Fetch `GET /api/v3/depth?limit=5000`; if `lastUpdateId` < the first buffered `U`, fetch again.
  3. Drop events with `u` <= `lastUpdateId`.
  4. A gap is `U` > local id + 1: discard the book and restart.
  5. Quantities are absolute.
  6. There is no checksum.
  7. The snapshot caps at 5000 levels per side, so deeper levels are unknown until they change.

  (https://github.com/binance/binance-spot-api-docs/blob/master/web-socket-streams.md#how-to-manage-a-local-order-book-correctly)
- Whether the update-id space is per symbol is not stated (not confirmed).
- Trade side:
  - JSON `m` = "is the buyer the market maker", so the aggressor sold when `m` is true. `@trade` is one message per trade (web-socket-streams.md above).
  - SBE carries `isBuyerMaker`.
  - FIX carries `AggressorSide(2446)` (fix-api.md above).

### Spot order entry
- APIs: REST, WS API `ws-api.binance.com/ws-api/v3` (JSON or SBE responses), FIX 4.4 `fix-oe.binance.com:9000`, FIX-SBE on 9001/9002 (https://github.com/binance/binance-spot-api-docs/blob/master/web-socket-api.md, .../fix-api.md).
- SBE schemas:
  - REST/WS response schema 3:5 is in production; 3:6 was announced 2026-11-11 and is on testnet.
  - FIX-SBE schema 1:1 is in production (https://github.com/binance/binance-spot-api-docs/blob/master/sbe/schemas/sbe_schema_lifecycle_prod.json).
- Auth:
  - Ed25519 (recommended), HMAC-SHA256 or RSA (https://github.com/binance/binance-spot-api-docs/blob/master/faqs/api_key_types.md).
  - FIX and WS `session.logon` accept Ed25519 only (fix-api.md, web-socket-api.md above).
- Order types and TIF:
  - Types LIMIT, MARKET, STOP_LOSS(_LIMIT), TAKE_PROFIT(_LIMIT) and LIMIT_MAKER (post-only). TIF GTC, IOC, FOK (https://github.com/binance/binance-spot-api-docs/blob/master/enums.md).
  - FIX also has pegged orders (fix-api.md above).
- Amend:
  - `order/amend/keepPriority` (REST, WS and FIX `XAK`) only reduces quantity and keeps time priority and orderId (https://github.com/binance/binance-spot-api-docs/blob/master/faqs/order_amend_keep_priority.md).
  - `cancelReplace` / FIX `XCN` loses priority (same page).
- STP modes: NONE, EXPIRE_TAKER, EXPIRE_MAKER, EXPIRE_BOTH, DECREMENT, TRANSFER (https://github.com/binance/binance-spot-api-docs/blob/master/enums.md).
- Client order id:
  - Regex `^[a-zA-Z0-9-_]{1,36}$` (fix-api.md above).
  - It must be unique among open orders (https://github.com/binance/binance-spot-api-docs/blob/master/rest-api.md).
- Rate limits:
  - Live `exchangeInfo`: 100 orders per 10 s and 200,000 per day per account; 6000 request weight per minute per IP (https://data-api.binance.vision/api/v3/exchangeInfo?symbol=BTCUSDT).
  - FIX OE: 10,000 messages per 10 s and 10 connections per account (fix-api.md above).
- FIX session:
  - `ResetSeqNumFlag=Y` is required and ResendRequest is not supported (fix-api.md above).
  - A timeout (-1007) means the execution status is unknown (fix-api.md above).
- Cancel-on-disconnect and dead-man's switch: none documented for spot (fix-api.md, web-socket-api.md, rest-api.md searched).

### Spot post-trade
- User data now comes only through a WS API subscription (`userDataStream.subscribe`), in JSON or SBE. The listenKey stream was deprecated 2025-04-07 (https://github.com/binance/binance-spot-api-docs/blob/master/user-data-stream.md, .../CHANGELOG.md).
- `executionReport` carries `t` trade id, `I` execution id, `m` maker flag, `T` transaction time (user-data-stream.md above).
- FIX drop copy at `fix-dc.binance.com:9000`:
  - Data is delayed by 1 s.
  - Limit is 60 messages per 60 s.

  (https://github.com/binance/binance-spot-api-docs/blob/master/fix-api.md#fix-api-drop-copy-sessions)
- Reconciliation:
  - `GET /api/v3/myTrades` takes `fromId` = tradeId, has a 24 h window and returns at most 1000 rows (https://github.com/binance/binance-spot-api-docs/blob/master/rest-api.md).
  - Trade ids and order ids are exchange integers; no statement on stability across replays (not confirmed).

### Spot test environments
- Spot testnet `testnet.binance.vision` (https://github.com/binance/binance-spot-api-docs/blob/master/testnet/general-info.md):
  - Free; log in and generate a key.
  - Covers REST, WS API, WS streams, SBE streams and FIX OE/DC/MD including FIX-SBE.
  - Funds are virtual and data resets about monthly.
  - Its prices and books are independent of the live exchange (https://github.com/binance/binance-spot-api-docs/blob/master/demo-mode/general-info.md).
- Demo Mode (`demo-api.binance.com`, `demo-fix-*`) needs a Binance account. Its books are "realistic", "not equal to real" (demo-mode/general-info.md above).

### USD-M and COIN-M futures
- JSON WS and REST only; no FIX, SBE or drop copy for futures (https://developers.binance.com/en/docs/llms-full.txt; fix-api.md: FIX is "only ... SPOT").
- Routing:
  - `fstream` is split into `/public` (depth, bookTicker), `/market` (aggTrade, markPrice) and `/private`.
  - Legacy URLs were due to be retired 2026-04-23.

  (https://developers.binance.com/en/docs/products/derivatives-trading-usds-futures/websocket-market-streams/Important-WebSocket-Change-Notice)
- Sync rule:
  1. Buffer events and fetch `/fapi/v1/depth?limit=1000`.
  2. Drop events with `u` < `lastUpdateId`.
  3. The first applied event must have `U` <= `lastUpdateId` <= `u`.
  4. After that, each event's `pu` must equal the previous `u`.

  (https://developers.binance.com/en/docs/products/derivatives-trading-usds-futures/websocket-market-streams/How-to-manage-a-local-order-book-correctly)
- Update rates: diffs at 100, 250 or 500 ms.
- RPI orders:
  - Excluded from `/fapi/v1/depth` and the depth streams, and shown only in `@rpiDepth@500ms`.
  - RPI fills are folded into aggTrade without a tag.

  (https://developers.binance.com/en/docs/catalog/core-trading-derivatives-trading-usd-s-m-futures/api/ws-streams/public, .../ws-streams/market)
- Trades: only `@aggTrade` is published, aggregated over 100 ms (same pages).
- Order entry:
  - REST and WS API `ws-fapi.binance.com/ws-fapi/v1` (https://developers.binance.com/en/docs/products/derivatives-trading-usds-futures/websocket-api-general-info).
  - TIF GTC, IOC, FOK, GTX (post-only), GTD (https://developers.binance.com/docs/derivatives/usds-margined-futures/trade/rest-api).
  - Modify works on LIMIT orders only, and "modified orders will be reordered in the match queue", with no exception for a size decrease (https://developers.binance.com/docs/derivatives/usds-margined-futures/trade/rest-api/Modify-Order).
  - Client id regex `^[\.A-Z\:/a-z0-9_-]{1,36}$`, unique among open orders (rest-api above).
- Dead-man's switch: `countdownCancelAll`, per symbol (https://developers.binance.com/docs/derivatives/usds-margined-futures/trade/rest-api/Auto-Cancel-All-Open-Orders).
- Post-trade:
  - listenKey user stream with `ORDER_TRADE_UPDATE` (`t` trade id, `m` maker) (https://developers.binance.com/en/docs/products/derivatives-trading-usds-futures/user-data-streams).
  - `userTrades` covers 7 days per query and 3 months of history (https://developers.binance.com/docs/derivatives/usds-margined-futures/trade/rest-api/Account-Trade-List).
- COIN-M was merged onto the USD-M architecture by 2026-06-30. The two share rate-limit pools and the STP setting, and use the same sync rule (https://developers.binance.com/en/docs/products/derivatives-trading-coin-futures/Important-CM-UM-Integration-Notice).
- Test environment: `demo-fapi`/`demo-fstream` and `demo-dapi`. "Most of the endpoints can be used". Whether the book mirrors live is not confirmed (https://developers.binance.com/en/docs/products/derivatives-trading-usds-futures/general-info).

### Historical data, documentation, volume
- data.binance.vision (https://github.com/binance/binance-public-data/blob/master/README.md; bucket listing https://s3-ap-northeast-1.amazonaws.com/data.binance.vision?delimiter=/&prefix=data/futures/um/daily/):
  - Spot: trades, aggTrades and klines. Timestamps are in µs from 2025.
  - Futures: also `bookTicker`, which exists for BTCUSDT only from 2023-05-16 to 2024-03-30, and `bookDepth`, which holds cumulative depth at ±0.2–5% from mid about every 30 s and is not a book.
  - No diffs and no L3.
- Documentation (https://github.com/binance/binance-spot-api-docs, https://github.com/binance/binance-api-swagger, https://github.com/binance/binance-sbe-cpp-sample-app):
  - SBE XML schemas, QuickFIX dictionaries (`fix/schemas/spot-fix-oe.xml`, `spot-fix-md.xml`) and OpenAPI `spot_api.yaml`.
  - SBE sample apps in C++, Java and Rust.
- Volume: #1 spot and #1 derivatives on CoinGecko (https://www.coingecko.com/en/exchanges, https://www.coingecko.com/en/exchanges/derivatives).

## Coinbase

Coinbase runs separate venues with separate APIs. Coinbase International Exchange has been read-only since 2026-10-01. Its perpetuals moved to a Deribit-powered "Global Derivatives" gateway reached through Advanced Trade (https://docs.cdp.coinbase.com/international-exchange/introduction/welcome.md, https://docs.cdp.coinbase.com/coinbase-app/advanced-trade-apis/guides/derivatives/overview.md).

### Coinbase Exchange (institutional spot): market data
- Transports: WS JSON (`ws-feed.exchange.coinbase.com`, authenticated `ws-direct`) and FIX 5.0 SP2 MD (`fix-md.exchange.coinbase.com:6121` with snapshots, `:6122` without). No binary or multicast (https://docs.cdp.coinbase.com/exchange/websocket-feed/overview, https://docs.cdp.coinbase.com/exchange/fix-api/connectivity.md).
- L3 channels (https://docs.cdp.coinbase.com/exchange/websocket-feed/channels.md):
  - The WS `full` channel is order by order: `received`, `open`, `done`, `match`, `change`, `activate`.
  - `level3` is the same data in compact form.
  - Both need authentication.
- Sync rule:
  1. Subscribe to the stream and queue its messages.
  2. Fetch `GET /products/{id}/book?level=3`.
  3. Discard queued messages with sequence <= the snapshot sequence, then apply the rest.

  (channels.md above; https://docs.cdp.coinbase.com/api-reference/exchange-api/rest-api/products/get-product-book, which also warns that polling level 3 can get access limited.)
- Book-building rules: only `open` puts an order on the book. `done` and `change` for orders that never opened are ignored. `change` carries `reason` STP or `modify_order` (channels.md above).
- Sequence numbers are per product and increase by 1 per message. Gaps and out-of-order delivery can happen. The heartbeat channel carries `sequence` and `last_trade_id` (https://docs.cdp.coinbase.com/exchange/websocket-feed/overview).
- FIX MD L3 (https://docs.cdp.coinbase.com/exchange/fix-api/market-data):
  - `MarketDepth` 0 gives order-level data, with `MDEntryID` = order id.
  - `RptSeq` is per symbol. After a snapshot, apply only incrementals with `RptSeq` greater than the snapshot's.
  - No resend. One connection per API key.
- Trade side:
  - WS `match.side` is the maker's side (channels.md above).
  - FIX gives `AggressorSide`; `TradeID` was added 2025-10-31 (fix-api/market-data above; https://docs.cdp.coinbase.com/exchange/changes/changelog).
- Timestamps: WS uses ISO 8601. FIX L3 `TransactTime` is in µs (same pages).

### Coinbase Exchange: order entry, post-trade, test
- Order entry: REST, and FIX 5.0 SP2 at `fix-ord.exchange.coinbase.com:6121`. FIX 4.2 was deprecated 2025-06-03. No WS order entry (https://docs.cdp.coinbase.com/exchange/fix-api/connectivity.md, changelog above).
- Auth:
  - REST: HMAC-SHA256 with passphrase (https://docs.cdp.coinbase.com/exchange/rest-api/authentication).
  - FIX Logon: HMAC-SHA256 in `RawData(96)` (https://docs.cdp.coinbase.com/exchange/fix-api/order-entry-messages/order-entry-messages5).
- Order types and TIF:
  - Limit, market and stop; TIF GTC, GTT, IOC, FOK; post-only; iceberg via `max_floor` (https://docs.cdp.coinbase.com/api-reference/exchange-api/rest-api/orders/create-new-order).
  - FIX post-only is `ExecInst=A` (order-entry-messages5 above).
- Client order id: `client_oid` / `ClOrdID` must be a lowercase UUIDv4. Uniqueness scope is not confirmed (same pages).
- Replace (FIX 35=G): a quantity decrease keeps queue priority. A quantity increase or any price change loses it (order-entry-messages5 above).
- STP: dc, co, cn, cb (create-new-order above).
- Cancel-on-disconnect: FIX Logon tag 8013, `S` for the session's orders or `Y` for the profile's (order-entry-messages5 above).
- Rate limits:
  - REST private: 15 rps, burst 30 (https://docs.cdp.coinbase.com/exchange/rest-api/rate-limits).
  - FIX: 100 requests/s, disconnect above 200 msg/s, batches of 15 orders (https://docs.cdp.coinbase.com/exchange/fix-api/rate-limits).
- Post-trade:
  - WS `user` channel (channels.md above).
  - FIX drop copy at `fix-dc.exchange.coinbase.com:6122` covers FIX and REST orders. Since 2025-10 it also reports cancels and order changes (https://docs.cdp.coinbase.com/exchange/fix-api/drop-copy, changelog above).
  - `ExecID` is a UUID; `TradeID(1003)` identifies the fill.
  - REST `/fills` returns `trade_id`, `order_id` and liquidity (https://docs.cdp.coinbase.com/api-reference/exchange-api/rest-api/orders/get-all-fills).
- Sandbox (https://docs.cdp.coinbase.com/exchange/introduction/sandbox):
  - Self-serve (https://public.sandbox.exchange.coinbase.com).
  - Unlimited test funds; everything except transfers.
  - A subset of production books, with WS and FIX OE/MD/DC endpoints.
  - Whether sandbox liquidity is real is not confirmed.
- Historical data: none found.
- Documentation:
  - FIX 5.0 XML dictionaries, latest 2026-06-12 (https://docs.cdp.coinbase.com/exchange/fix-api/dictionary-downloads.md).
  - OpenAPI `exchange-spec.yaml` (https://docs.cdp.coinbase.com/llms.txt).
  - TS and Go SDKs.

### Coinbase Advanced Trade (retail; what kaspar-hft uses now)
- Data: `level2` snapshot then absolute updates. No L3, FIX or binary (https://docs.cdp.coinbase.com/api-reference/advanced-trade-api/websocket/level2).
- Sequence scope: the docs contradict each other. The overview says `sequence_num` is per product (https://docs.cdp.coinbase.com/coinbase-app/advanced-trade-apis/websocket/websocket-overview). The level2 reference page says per connection (level2 page above).
- `market_trades.side` is the maker's side (https://docs.cdp.coinbase.com/api-reference/advanced-trade-api/websocket/market-trades).
- Order entry:
  - REST with a per-request JWT.
  - On key types, one page says ES256 only and not Ed25519 (https://docs.cdp.coinbase.com/coinbase-app/authentication-authorization/api-key-authentication). Another recommends Ed25519 (https://docs.cdp.coinbase.com/coinbase-app/advanced-trade-apis/guides/derivatives/technical.md).
  - `client_order_id` is idempotent (https://docs.cdp.coinbase.com/api-reference/advanced-trade-api/rest-api/orders/create-order).
- Sandbox: static, predefined responses with no matching (https://docs.cdp.coinbase.com/coinbase-app/advanced-trade-apis/sandbox).
- Documentation: OpenAPI and AsyncAPI specs (https://docs.cdp.coinbase.com/api-reference/advanced-trade-api/rest-api/introduction.md).
- Global Derivatives gateway (Deribit "Starbase"; https://docs.cdp.coinbase.com/coinbase-app/advanced-trade-apis/guides/derivatives/technical.md):
  - JSON-RPC 2.0 over HTTP and WS (`drb.coinbase.com`).
  - Opt-in cancel-on-disconnect.
  - `label` is not guaranteed unique.
  - Book and sequence rules are not confirmed from Coinbase docs.

### Coinbase Derivatives (CDE, CFTC-regulated futures)
- Market data transports:
  - UDP multicast with SBE: A/B incremental feeds, A/B snapshot feeds and a unicast retransmit service.
  - Cross-connect only, at Equinix CH4.
  - FIX 4.4 MD over the internet or PrivateLink.

  (https://docs.cdp.coinbase.com/derivatives/udp/overview.md, https://docs.cdp.coinbase.com/derivatives/introduction/connectivity.md)
- L3 SBE schema (https://docs.cdp.coinbase.com/derivatives/downloads/cde-sbe-xml-schemas-latest.zip):
  - `OrderPut`/`OrderDelete` by orderId; `Trade` carries buy and sell order ids; `TradeAmend` and `TradeBust`.
  - Transaction start/end and clearBook flags.
  - Trade side is the aggressor's.
  - Timestamps in ns.
- Sequencing:
  - Packet `SeqNum` is never reset (https://docs.cdp.coinbase.com/derivatives/udp/message-structure.md).
  - Per-instrument `instrSeqNum` resets daily.
- Order entry:
  - FIX 4.4, plus SBE order entry for lead market makers on cross-connect (https://docs.cdp.coinbase.com/derivatives/sbe/overview.md).
  - A quantity decrease keeps priority (https://docs.cdp.coinbase.com/derivatives/fix/order-entry.md).
  - Cancel-on-disconnect is optional, and implicit for SBE (https://docs.cdp.coinbase.com/derivatives/introduction/runbook.md).
- Post-trade: FIX 4.4 drop copy with trade, correction and cancel reports, deduplicated by ExecID (https://docs.cdp.coinbase.com/derivatives/fix/drop-copy.md).
- Test environment: the integration environment needs certification, so it is not self-serve (runbook above).
- Documentation: SBE XML, FIX XML and OpenAPI downloads (https://docs.cdp.coinbase.com/derivatives/introduction/downloads.md).

## Hyperliquid

- Public market data (https://hyperliquid.gitbook.io/hyperliquid-docs/for-developers/api/websocket/subscriptions.md, .../websocket.md):
  - WS `l2Book` sends a full snapshot on each push, 20 levels (5 with `fast`), with no sequence numbers and no checksum.
  - `nSigFigs` 2–5 or null (https://hyperliquid.gitbook.io/hyperliquid-docs/for-developers/api/info-endpoint.md).
  - Missed data during a reconnect is in the snapshot ack.
- Per-order data exists only from your own non-validating node (https://hyperliquid.gitbook.io/hyperliquid-docs/for-developers/nodes/l1-data-schemas.md, https://github.com/hyperliquid-dex/order_book_server):
  - The node writes `node_raw_book_diffs` per order (new / update / remove, with oid and user), about 100 GB/day.
  - Hyperliquid's `order_book_server` builds an `l4Book` from it but is "not actively maintained".
- Queue semantics relevant to the simulator:
  - Within a block, cancels run before GTC/IOC orders (https://hyperliquid.gitbook.io/hyperliquid-docs/hypercore/order-book.md).
  - ALO orders placed in the last 400 ms at a level are re-sorted by priority fee (https://hyperliquid.gitbook.io/hyperliquid-docs/for-developers/api/priority-fees.md).
- Trades: `side` is the aggressing side (https://hyperliquid.gitbook.io/hyperliquid-docs/for-developers/api/notation.md). `tid` is a hash; the docs say to use (block_time, coin, tid) as the unique key (subscriptions.md above).
- Order entry:
  - `POST /exchange` or WS `post` (https://hyperliquid.gitbook.io/hyperliquid-docs/for-developers/api/exchange-endpoint.md).
  - Auth is an EIP-712 secp256k1 signature over msgpack(action) + nonce, made by agent (API) wallets (https://hyperliquid.gitbook.io/hyperliquid-docs/for-developers/api/signing.md, https://github.com/hyperliquid-dex/hyperliquid-python-sdk/blob/master/hyperliquid/utils/signing.py).
  - Nonces: the 100 highest are kept per signer (https://hyperliquid.gitbook.io/hyperliquid-docs/for-developers/api/nonces-and-api-wallets.md).
  - TIF Alo (post-only), Ioc, Gtc, plus TP/SL triggers (exchange-endpoint.md above).
  - `cloid` is 128-bit hex.
  - `modify` is described as cancel plus place; whether priority is kept on a size decrease is not confirmed.
  - The response arrives after block commit; about 380 ms end to end for a cancel or ALO (https://hyperliquid.gitbook.io/hyperliquid-docs/for-developers/api/optimizing-latency.md).
- Dead-man's switch: `scheduleCancel`, at least 5 s ahead, at most 10 per day. No cancel-on-disconnect (exchange-endpoint.md above).
- Post-trade:
  - WS `userFills` (with `isSnapshot`), `orderUpdates`, `userEvents` (subscriptions.md above).
  - REST `userFillsByTime` returns at most the 10,000 most recent (info-endpoint.md above).
  - Complete fills history is in the node and S3.
- Testnet: same API. Its faucet only pays addresses that have deposited on mainnet (https://hyperliquid.gitbook.io/hyperliquid-docs/onboarding/testnet-faucet.md).
- Historical data, S3 requester-pays (https://hyperliquid.gitbook.io/hyperliquid-docs/historical-data.md):
  - `hyperliquid-archive/market_data/<date>/<hour>/l2Book/<coin>.lz4`, uploaded about monthly "with no guarantee".
  - `hl-mainnet-node-data/node_fills_by_block`, `replica_cmds` (all L1 transactions).
  - No raw book diffs.
- Documentation: GitBook (markdown via `.md`) and the official Python SDK; no OpenAPI (https://hyperliquid.gitbook.io/hyperliquid-docs/for-developers/api.md).
- Volume: 80% of DeFi perp volume as of May 2025 (https://www.theblock.co/amp/post/355863/hyperliquid-claims-80-of-defi-perp-market-challenging-centralized-giants-with-decentralized-edge). A current rank is not confirmed.

## Bybit (V5)

- Market data, JSON (https://bybit-exchange.github.io/docs/v5/websocket/public/orderbook, https://bybit-exchange.github.io/docs/changelog/v5):
  - Depths and push intervals: 1 @ 10 ms, 50 @ 20 ms, 200 @ 100 ms, 1000 @ 200 ms (spot and futures).
  - Snapshot then deltas; `u`=1 means a service restart and the snapshot overwrites the book.
  - `seq` is the cross-sequence across depths.
  - No checksum.
  - Level 500 was removed 2025-09-11.
- Full-depth book `orderbook.full.{symbol}` (https://bybit-exchange.github.io/docs/v5/websocket/public/full-ob, https://bybit-exchange.github.io/docs/v5/market/full-ob):
  - Pushes every 200 ms, deltas only. Spot since 2025-12-16, futures since 2026-08-11.
  - Sync: snapshot from `GET /v5/market/full_orderbook` (up to 10,000 levels); discard deltas with `seq` below the snapshot's; then `u` must equal the previous `u` + 1.
- RPI orders are excluded from the normal topics and shown in `orderbook.rpi` (https://bybit-exchange.github.io/docs/v5/websocket/public/orderbook-rpi).
- No L3 in any section.
- SBE (https://bybit-exchange.github.io/docs/v5/sbe/sbe-basic-info, https://bybit-exchange.github.io/docs/v5/sbe/market/level-50):
  - BBO, 50 levels @ 20 ms and trades, with µs timestamps.
  - Only on the Market Maker WebSocket, for institutional clients and market makers.
- Trades: `S` is the taker side; `T` is fill time in ms (https://bybit-exchange.github.io/docs/v5/websocket/public/trade).
- Order entry APIs:
  - REST and WS trade `stream.bybit.com/v5/trade`, with batches of 20 (https://bybit-exchange.github.io/docs/v5/websocket/trade/guideline).
  - SBE order entry on the MM gateway only (https://bybit-exchange.github.io/docs/v5/sbe/trade/order-entry).
  - FIX 4.4, spot only, by whitelist, with RSA keys. No ResendRequest; seq restarts at 1 each connection (https://bybit-exchange.github.io/docs/fix-api/guide).
- Auth: HMAC-SHA256 or RSA (https://bybit-exchange.github.io/docs/v5/guide).
- Order entry rules:
  - TIF GTC, IOC, FOK, PostOnly, RPI (https://bybit-exchange.github.io/docs/v5/order/create-order).
  - `orderLinkId` is at most 36 characters from `[0-9A-Za-z_-]`.
  - Priority after a REST/WS amend is not confirmed. On FIX `XAR`, a size decrease keeps queue position (https://bybit-exchange.github.io/docs/fix-api/amend-order).
- Cancel-on-disconnect: DCP is tied to the private WS, 3–300 s, institutional clients only (https://bybit-exchange.github.io/docs/v5/order/dcp).
- Post-trade:
  - WS `execution` / `execution.fast` (https://bybit-exchange.github.io/docs/v5/websocket/private/execution).
  - `GET /v5/execution/list` covers 2 years, 7 days per query (https://bybit-exchange.github.io/docs/v5/order/execution).
- Test environments:
  - Testnet `api-testnet.bybit.com` with WS trade and FIX testnet (https://bybit-exchange.github.io/docs/v5/guide).
  - Demo `api-demo.bybit.com` uses mainnet public data and is self-serve from a mainnet account, but has no WS trade (https://bybit-exchange.github.io/docs/v5/demo).
- Historical data, free:
  - Daily trades at https://public.bybit.com/trading/ and /spot/.
  - The order-book archive is the recorded `orderbook.200` WS stream: snapshot then 100 ms deltas carrying `u`, `seq` and `cts`. Example file: `https://quote-saver.bycsi.com/orderbook/linear/BTCUSDT/2026-11-15_BTCUSDT_ob200.data.zip` (1.57 GB unzipped). This was found by inspecting public files; the history page is https://www.bybit.com/derivatives/en/history-data.
- Documentation: SBE XML embedded in the pages; FIX XML linked from the FIX guide; SDKs in 8 languages (fix-api/guide and v5/guide above).
- Volume: #7 derivatives by OI, about 219k BTC 24 h (https://www.coingecko.com/en/exchanges/derivatives).

## OKX (V5)

- Book channels (https://www.okx.com/docs-v5/en/#order-book-trading-market-data-ws-order-book-channel):
  - `books` 400 levels @ 100 ms; `books5`; `bbo-tbt` 10 ms; `books50-l2-tbt` and `books-l2-tbt` (400 levels) @ 10 ms.
  - The two tbt books need VIP4+.
  - Each level carries an order count. No L3.
- Sync rule:
  - `seqId`/`prevSeqId` are per instrument and identical across connections. Each update's `prevSeqId` must equal the previous `seqId`.
  - Heartbeat updates have `prevSeqId` == `seqId`.
  - On maintenance, `seqId` < `prevSeqId`.
  - The checksum is deprecated and fixed at 0.

  (same page)
- SBE market data (https://www.okx.com/docs-v5/en/#order-book-trading-sbe-market-data):
  - `ws/v5/public-sbe` carries trades, bbo-tbt and books-l2-tbt, behind login and VIP4+.
  - The book stream has no initial snapshot. Use the REST `books-sbe` snapshot with the rule `prevSeqId` <= snapshot `seqId` < `seqId`.
  - Schema XML: https://www.okx.com/docs-v5/log_en/xml/okx_sbe_1_0.xml.
- Trades: `side` is the taker side. `trades` aggregates per taker order; `trades-all` is one trade per message (https://www.okx.com/docs-v5/en/#order-book-trading-market-data-ws-trades-channel).
- Order entry:
  - REST and WS (`order`, `batch-orders`, `amend-order`); HMAC-SHA256 with passphrase. FIX is not in the V5 docs (https://www.okx.com/docs-v5/en/).
  - Types: limit, market, post_only, fok, ioc, mmp, rpi (https://www.okx.com/docs-v5/en/#order-book-trading-trade-post-place-order).
  - Amend is in place (`newSz` is the new total, `cxlOnFail`). Priority on a size decrease is not confirmed (https://www.okx.com/docs-v5/en/#order-book-trading-trade-post-amend-order).
  - `clOrdId`: at most 32 alphanumerics, unique among pending orders.
  - Rate limit: 60 per 2 s per UID+instrument (https://www.okx.com/docs-v5/en/#overview-rate-limits).
- Dead-man's switch: Cancel All After, 10–120 s. No connection-bound cancel (https://www.okx.com/docs-v5/en/#order-book-trading-trade-post-cancel-all-after).
- Post-trade:
  - The WS `fills` channel needs VIP4; others use `orders` (https://www.okx.com/docs-v5/en/#order-book-trading-trade-ws-fills-channel).
  - `fills-history` covers 3 months. `tradeId` is shared with the public feed and archive (https://www.okx.com/docs-v5/en/#order-book-trading-trade-get-transaction-details-last-3-months).
- Demo trading: header `x-simulated-trading: 1` and `wspap.okx.com`; self-serve keys from an OKX account (https://www.okx.com/docs-v5/en/#overview-demo-trading-services).
- Historical data, free (https://www.okx.com/docs-v5/en/#public-data-rest-api-get-historical-market-data):
  - Via `GET /api/v5/public/market-data-history`: trades; a 400-level book (snapshot then updates about every 10 ms, NDJSON, no `seqId` in the files, found by inspection); a 5000-level book from 2025-11-01.
- Documentation: one HTML page, SBE XML, `python-okx` SDK.
- Volume: #2 derivatives (https://www.coingecko.com/en/exchanges/derivatives).

## Bitget

- API versions: V2 Classic is in maintenance mode; V3 (UTA) is current (https://www.bitget.com/docs/uta/Introduction).
- V3 books (https://www.bitget.com/docs/uta/websocket/public/Order-Book-Channel):
  - `books1` 1 ms, `books5` 10 ms, `books50` 20 ms, full `books` (up to 1000 levels) 50 ms.
  - `pseq` must equal the previous `seq`.
- SBE Depth50, BBO and trades on `v3/ws/public/sbe`; no access restriction stated (https://www.bitget.com/docs/uta/websocket/sbe/sbe-intro).
- No L3.
- Trade side: `S` is documented only as "Fill side"; taker is not confirmed (https://www.bitget.com/docs/uta/websocket/public/New-Trades-Channel).
- Order entry:
  - REST and WS (`place-order`, `modify-order`); HMAC or RSA.
  - FIX only for the CFD product (https://www.bitget.com/docs/uta/websocket/private/Place-Order-Channel, https://www.bitget.com/blog/articles/institutional-cfd-liquidity-stp-fix-api).
  - `clientOid` matches `^[0-9A-Za-z_:#\-+\s]{1,32}$` and must be unique among open orders.
  - Modify is asynchronous, with no failure push (https://www.bitget.com/docs/uta/websocket/private/Modify-Order-Channel).
- Dead-man's switch: countdown cancel-all, 5–60 s, UTA only (https://www.bitget.com/api-doc/uta/trade/CountDown-Cancel-All).
- Post-trade: fill and fast-fill channels with `execId` and `tradeScope` (https://www.bitget.com/docs/uta/websocket/private/Fill-Channel).
- Demo: header `paptrading: 1`; KYC required (https://www.bitget.com/docs/uta/demo-trading/rest-api).
- Historical data: https://www.bitget.com/data-download has trades and 500-level depth. Inspection showed the depth files are snapshots about 21 s apart, not diffs.
- SDKs: https://github.com/BitgetLimited/v3-bitget-api-sdk.
- Volume: #6 spot by trust score; #13 derivatives by OI (https://www.coingecko.com/en/exchanges).

## Kraken

- Spot L3 (https://docs.kraken.com/exchange/api-reference/spot-websocket-v2/level3):
  - WS `level3` on `ws-l3.kraken.com/v2` needs an API token, so it needs a Kraken API key.
  - Depth 10/100/1000; events `add`/`modify`/`delete`; per-order `order_id`, `limit_price`, `order_qty`, ns timestamp.
  - "No sequencing is required".
- Integrity: a CRC32 over the top 10 levels, iterating orders in queue order, so it also checks queue priority (https://docs.kraken.com/exchange/guides/websockets/l3-checksum-v2).
- Sequence numbers: the L3 guide says the channel provides them (https://docs.kraken.com/exchange/guides/general/l3-data); the channel reference has none.
- Excluded from L3: in-flight orders, untriggered stops, hidden iceberg quantity (l3-data above).
- Spot L2 `book` (https://docs.kraken.com/exchange/api-reference/spot-websocket-v2/book, https://docs.kraken.com/exchange/guides/websockets/book-checksum-v2):
  - Depth 10–1000, with a CRC32 of the top 10 and no seq.
  - When a level falls out of the subscribed depth, no zero-quantity update is sent; you truncate the book yourself.
- Trades: `side` is the taker's (https://docs.kraken.com/exchange/api-reference/spot-websocket-v2/trade).
- FIX MD:
  - FIX 4.4, spot L2 port 4000 and L3 port 4005 (https://docs.kraken.com/exchange/guides/fix/introduction).
  - The MarketDataRequest page says L3 is "only available for derivatives", which contradicts the port list (https://docs.kraken.com/exchange/api-reference/unified-fix/mdr).
- Kraken US Perps SBE (https://docs.kraken.com/exchange/guides/us-perps/api, https://docs.kraken.com/exchange/api-reference/sbe/order-book-incremental, https://docs.kraken.com/exchange/api-reference/sbe):
  - L3 with per-instrument contiguous `orderBookSeq` and an `orderPriority` field.
  - Sync: buffer the live feed, take a snapshot from a separate connection; no replay.
  - SBE for Spot or Futures is not confirmed.
- Colocation: Equinix London, about 200 µs; requires VIP status and an NDA (https://docs.kraken.com/exchange/guides/general/colocation).
- Derivatives WS: `futures.kraken.com/ws/v1` shuts down 2027-03-01. The Unified WS carries Derivatives now and Spot from Q1 2027 (https://docs.kraken.com/exchange/api-reference/futures-websocket, https://docs.kraken.com/exchange/api-reference/unified-websocket).
- Order entry:
  - Spot REST, WS v2 and FIX 4.4 by onboarding (https://docs.kraken.com/exchange/guides/general/api-comparison).
  - Auth: HMAC-SHA512 (https://docs.kraken.com/exchange/guides/fix/authentication).
  - Types: limit, market, iceberg, stops, trailing; TIF gtc, gtd, ioc, fok; post_only (https://docs.kraken.com/exchange/api-reference/spot-websocket-v2/add_order).
- Amend (https://docs.kraken.com/exchange/guides/general/amends):
  - In place. A quantity decrease keeps position; an increase or any price change goes to the back.
  - FIX guarantees in-order processing of amends; WS does not.
- Client order id `cl_ord_id`: a UUID or free text of at most 18 ASCII characters, unique among open orders (https://docs.kraken.com/exchange/guides/general/clordid).
- Rate limits: a per-pair counter shared across REST, WS and FIX (https://docs.kraken.com/exchange/guides/general/ratelimits).
- Cancel-on-disconnect: FIX COD is on by default (tag 8674) (https://docs.kraken.com/exchange/api-reference/unified-fix/logon). WS has `cancel_all_orders_after` (https://docs.kraken.com/exchange/api-reference/spot-websocket-v2/cancel_after).
- Post-trade:
  - WS `executions` with `exec_id`, `trade_id`, `liquidity_ind` (https://docs.kraken.com/exchange/api-reference/spot-websocket-v2/executions).
  - FIX replays ExecutionReports by seq; seq resets daily at 22:00 UTC (fix/introduction above).
  - REST `TradesHistory` (https://docs.kraken.com/api-reference/account-data/get-trades-history).
  - US Perps FIX drop copy is deduplicated by ExecID (https://docs.kraken.com/exchange/api-reference/drop-copy).
- Test environments:
  - Futures demo `demo-futures.kraken.com` is free and self-serve, with the same API as production (https://docs.kraken.com/exchange/guides/futures/introduction).
  - No spot testnet; the spot FIX UAT is by onboarding (fix/introduction above).
- Historical data: free Time & Sales CSVs for all pairs, including side and trade_id, plus OHLCVT (https://support.kraken.com/hc/articles/360047543791-downloadable-historical-market-data-time-and-sales-). The developer docs say no bulk dump exists (https://docs.kraken.com/exchange/guides/general/historical-data), which contradicts the support article.
- Documentation and SDKs: OpenAPI-generated REST reference; SDKs in Go and Rust (https://docs.kraken.com/home/sdks).
- Volume: CoinGecko trust rank 3, about 22.6k BTC 24 h (https://api.coingecko.com/api/v3/exchanges/kraken).

## Deribit

- JSON-RPC book (https://docs.deribit.com/subscriptions/orderbook/bookinstrument_nameinterval):
  - `book.{instrument}.{interval}` sends a full-depth L2 snapshot, then new/change/delete updates.
  - `prev_change_id` must equal the previous `change_id`, per instrument.
  - `raw` (1 ms) requires authorisation; `100ms` does not.
- Trades: `direction` is the taker's; `trade_seq` is per instrument (https://docs.deribit.com/subscriptions/trades/tradesinstrument_nameinterval).
- Starbase SBE multicast (https://docs.deribit.com/starbase/multicast-channels, https://docs.deribit.com/starbase/order-book-maintenance):
  - Market-by-order only; A/B feeds sharded by product group.
  - Each order carries `sortOrderId`, the explicit queue priority, which never changes. An amount reduction keeps priority.
  - Sync: buffer incrementals, read the snapshot channel, apply messages with seq greater than `incrementalSeqNum`.
  - Seq is per channel, with a Retransmit Gateway.
  - Timestamps are ns matching-engine time.
  - Trades: TradeSummary(30) carries the taker order and aggressor side, then Trade(31) per maker fill with `makerOrderId` (https://docs.deribit.com/starbase/trades).
  - Access is LD4 colo or AWS PrivateLink only (https://docs.deribit.com/starbase/overview).
  - The legacy multicast feed is deprecated at the end of 2026 (https://docs.deribit.com/articles/market-data-collection-best-practices).
- Speed bump: 10 ms on aggressive orders, except non-linear BTC/ETH (https://docs.deribit.com/starbase/speed-bumps).
- Spot is routed to Coinbase Exchange (https://docs.deribit.com/starbase/overview).
- Order entry APIs:
  - JSON-RPC, FIX 4.4 (`fix.deribit.com:9881/9883`) and Starbase SBE over TCP (https://docs.deribit.com/fix-api/production/overview).
  - Auth: HMAC `client_signature`, or Ed25519 / RSA keys (https://docs.deribit.com/articles/authentication, https://docs.deribit.com/articles/asymmetric-api-keys).
- Order entry rules:
  - TIF GTC, GTD, FOK, IOC; `post_only` reprices and `reject_post_only` rejects (https://docs.deribit.com/api-reference/trading/private-buy).
  - Client id is `label`, at most 64 characters; uniqueness not confirmed.
  - Starbase `clientOrderId` is int64, unique per portfolio (https://docs.deribit.com/starbase/placing-new-order).
  - `edit` is in place. A quantity decrease keeps priority; an increase or price change goes to the back (https://docs.deribit.com/articles/order-management-best-practices).
  - Rate limits are tiered by volume, 5–30 req/s (https://docs.deribit.com/articles/rate-limits).
- Cancel-on-disconnect:
  - JSON-RPC `enable_cancel_on_disconnect`, scope connection or account; it does not fire on logout (https://docs.deribit.com/api-reference/session-management/private-enable_cancel_on_disconnect).
  - Starbase COD is opt-in (https://docs.deribit.com/starbase/cancel-on-disconnect).
- Post-trade:
  - `user.trades.*` with `trade_id` (unique per currency) (https://docs.deribit.com/subscriptions/user/usertradesinstrument_nameinterval).
  - `get_user_trades_*` with `historical:true` returns older records (https://docs.deribit.com/articles/accessing-historical-trades-orders).
  - Starbase FIX 5.0 SP2 drop copy (https://docs.deribit.com/starbase/fix-drop-copy-api).
- Test environment:
  - test.deribit.com and fix-test.deribit.com, with separate accounts, support order entry (https://docs.deribit.com/articles/deribit-quickstart).
  - Whether sign-up is free and self-serve is not confirmed in the docs.
  - The Starbase test environment needs private connectivity.
- Historical data:
  - A free 5.3 MB Starbase market-data PCAP: http://statics.deribit.com/files/starbase-market-data.pcap (https://docs.deribit.com/starbase/binary-api-reference).
  - `history.deribit.com` serves public trades but is not documented on docs.deribit.com.
- Documentation (https://docs.deribit.com/starbase/binary-api-reference, https://docs.deribit.com/llms.txt):
  - SBE XML for the order API and market data: https://docs.deribit.com/specifications/deribit-sbe-xmls/deribit-sbe-market-data-api.xml.
  - OpenAPI and AsyncAPI.
  - FIX 5.0 dictionary for drop copy.
  - Starbase SDK zips.
- Volume: about 122k BTC 24 h and 33.7k BTC OI (https://api.coingecko.com/api/v3/derivatives/exchanges/deribit).

## Bitstamp

The WS and FIX pages block automated fetches. Their content below comes from the archived copies of 2025-05-22 (https://web.archive.org/web/20250522095700/https://www.bitstamp.net/websocket/v2/, https://web.archive.org/web/20250522095649/https://www.bitstamp.net/fix/v2/). The REST page was read live (https://www.bitstamp.net/api/).

- Market data:
  - WS `live_orders_[pair]` is order by order: `order_created`/`changed`/`deleted` with id, price, amount, side and µs timestamp.
  - Other channels: `diff_order_book` (full book), `detail_order_book` (top 100 with order ids), `live_trades` (buy and sell order ids).
  - No sequence numbers and no documented sync rule.
- REST `order_book?group=2` returns the full book with order ids (https://www.bitstamp.net/api/).
- Gap recovery: authenticated endpoints added 2026-02-16 (`/api/v2/order_data/`) return public order events by MarketEventID range. Whether the WS messages carry that id is not confirmed (https://www.bitstamp.net/api/).
- Trade `type` as the taker side: not confirmed.
- FIX MD: FIX 4.4, aggregated, depth up to 20.
- Order entry:
  - REST with HMAC-SHA256. Flags: daily, IOC, FOK, MOC (post-only), GTD (https://www.bitstamp.net/api/).
  - `replace_order` (2026-02-05) is an atomic cancel plus new order.
  - `client_order_id` is at most 180 characters and not unique-checked.
  - Rate limit: 400 req/s.
  - FIX 4.4 `fixserver.bitstamp.net:5001` has no 35=G. Orders cancel on logout by default.
- Post-trade:
  - WS `private-my_trades` and REST `user_transactions` (since_id; time filters cover 30 days).
  - FIX trade id in tag 914.
- Sandbox: https://sandbox.bitstamp.net is listed for every REST endpoint. Its public book showed prices different from production on 2026-11-18. Sign-up is not confirmed.
- Historical archive: none. Commercial use of the data needs a licence (https://www.bitstamp.net/api/).
- Volume: trust rank 7, about 4.2k BTC (https://api.coingecko.com/api/v3/exchanges/bitstamp).

## Gemini

- Docs are now at developer.gemini.com. The v1/v2 market-data WebSockets are archived (https://developer.gemini.com/websocket/introduction).
- Market data (https://developer.gemini.com/websocket/streams, https://developer.gemini.com/websocket.yaml, https://developer.gemini.com/tools/typescript-sdk/deep-dives/order-book):
  - L2 only: `@depth` diffs (1 s or 100 ms), `@depth5/10/20`, `@bookTicker`.
  - Connect with `?snapshot=-1` for the full book.
  - Sync: drop `u` <= last, apply when `U` <= last < `u`, and treat `U` > last as a gap.
- The archived v1 feed has per-level `reason` events with no order id, so it is not L3 (https://developer.gemini.com/websocket/archived/v1).
- Trades: `m` = buyer is maker. Event time is in ns (streams page above).
- FIX MD: full depth at price-level granularity (https://developer.gemini.com/trading/fix/market-data/exchange-bound-messages/market-data-request).
- Colocation: NY5 cross-connect (https://developer.gemini.com/trading/fix/overview/connecting).
- Order entry:
  - REST, WS (`order.place`/`cancel`) and FIX 4.4; HMAC-SHA384 (https://developer.gemini.com/websocket/authentication).
  - No amend in FIX or WS (https://developer.gemini.com/sitemap.xml).
  - TIF GTC, IOC, FOK, MOC (post-only).
  - FIX ClOrdID is not unique-enforced; a cancel by OrigClOrdID cancels every match (https://developer.gemini.com/trading/fix/order-entry/identifiers/client-supplied-identifiers).
  - Cancel-on-disconnect: WS `?cancelOnDisconnect=true` and FIX tag 9001, which defaults to on (https://developer.gemini.com/trading/fix/overview/session-level-messages/logon).
- Post-trade:
  - WS `orders@account`.
  - FIX drop copy is TradeCaptureReport only, with a globally unique `TradeReportID` (https://developer.gemini.com/trading/fix/drop-copy/introduction).
  - REST `/v1/mytrades`.
- Sandbox (https://developer.gemini.com/get-started/sandbox):
  - Self-serve; its book is driven by bots.
  - The FIX sandbox is enabled by email.
- Historical archive: none.
- Documentation: OpenAPI 3.0.3 and AsyncAPI 3.0.0 (https://developer.gemini.com/api-specifications).
- Volume: trust rank 25, about 382 BTC (https://api.coingecko.com/api/v3/exchanges?per_page=250).

## Bitfinex

- Raw book `R0` (https://docs.bitfinex.com/reference/ws-public-raw-books):
  - Entries are `[ORDER_ID, PRICE, AMOUNT]`; price 0 deletes the order.
  - Capped at `len` 1/25/100/250 orders, so it covers the top of book only, not full depth.
- Aggregated books P0–P4 are offered at F0 (real time) or F1 (2 s) (https://docs.bitfinex.com/reference/ws-public-books).
- Checksum: CRC32 over the top 25 per side (https://docs.bitfinex.com/docs/ws-websocket-checksum).
- Sequence numbers: `SEQ_ALL` is beta and counts per connection (https://docs.bitfinex.com/docs/ws-general, https://github.com/bitfinexcom/bfx-api-node-plugin-seq-audit/blob/master/lib/ws/message.js).
- Trades: the sign of the amount gives the taker side (https://docs.bitfinex.com/reference/ws-public-trades).
- Order entry (https://docs.bitfinex.com/docs/ws-auth, https://docs.bitfinex.com/reference/ws-auth-input-order-new):
  - Authenticated WS `on`/`ou`/`oc`, using HMAC-SHA384.
  - Post-only is flag 4096.
  - `cid` is an integer, keyed by (cid, UTC date) and not unique-enforced.
  - The `dms:4` dead-man switch cancels all account orders when the socket closes.
  - The public FIX gateway is deprecated (https://github.com/bitfinexcom/bfxfixgw).
- Amend: `ou` updates in place. Priority rules are not confirmed (https://docs.bitfinex.com/reference/ws-auth-input-order-update).
- Post-trade: `trades/hist` with `ID`, `ORDER_ID`, `MAKER`, `CID` (https://docs.bitfinex.com/reference/rest-auth-trades).
- Test: paper sub-accounts with TEST pairs; requires a main account (https://support.bitfinex.com/hc/en-us/articles/900001525006).
- Historical archive: none.
- Volume: trust rank 62, about 2.8k BTC.

## Crypto.com Exchange

- Market data:
  - WS `book.{instrument}.{10|50}`, updated every 10, 100 or 500 ms (https://exchange-developer.crypto.com/exchange/v1/docs/api/websocket/ws-channel-book-instrument-name-depth).
  - FIX MD over AWS PrivateLink only, depth 150, aggregated (https://exchange-developer.crypto.com/exchange/v1/docs/api/fix/fix-market-data-request).
  - No L3.
- Sync rule: apply an update only when `pu` equals the last `u`; ids are per instrument. This rule appears only on the old page, cited from the 2026-01-15 archive (https://web.archive.org/web/20260115172920/https://exchange-docs.crypto.com/exchange/v1/rest-ws/index.html).
- Trades: `s` is the taker side.
- Order entry:
  - REST, WS user API and FIX 4.4 over PrivateLink; HMAC-SHA256 (https://exchange-developer.crypto.com/exchange/v1/docs/api/websocket-common-api-reference).
  - Amend is "cancel and then create", losing priority unless only the quantity goes down (https://exchange-developer.crypto.com/exchange/v1/docs/api/websocket/ws-user-api-private-amend-order).
  - FIX 35=G returns a new OrderID.
  - `client_oid` is at most 36 characters.
  - Cancel-on-disconnect on WS (connection scope) and FIX (https://exchange-developer.crypto.com/exchange/v1/docs/api/fix/fix-logon).
- Post-trade:
  - The FIX ExecutionReport `MatchIndex(20102)` gives a maker fill's 0-based queue position within the level (https://exchange-developer.crypto.com/exchange/v1/docs/api/fix/fix-execution-report). This is a direct measurement against which the queue simulator could be checked.
  - FIX drop copy.
  - REST `get-trades` with ns timestamps.
- UAT (`uat-api.3ona.co`) is by invitation, for institutional accounts only (https://help.crypto.com/en/articles/3511424-api-faq).
- Historical archive: none.
- Documentation: QuickFIX XML dictionary (https://exchange-developer.crypto.com/exchange/v1/fix-client-xml).
- Volume: trust rank 10, about 15.1k BTC.

## Other venues with L3, binary or FIX

- **dYdX v4**: a full node's gRPC (9090) or WS (9092) stream carries L3 OrderPlace/OrderRemove/OrderUpdate and ClobMatch. Updates before finalisation are optimistic and can roll back. You must run your own node (https://docs.dydx.xyz/nodes/full-node-streaming).
- **Bitso**: WS `diff-orders` is per order with a `sequence`. REST `order_book?aggregate=false` is the full book with order ids. Sync rule not stated. About 301 BTC/day (https://docs.bitso.com/bitso-api/docs/diff-orders-channel, https://docs.bitso.com/bitso-api/docs/list-order-book).
- **LMAX Digital**: full-book data via FIX 4.2/4.4 or ITCH, LD4/LD6 cross-connect. The specs are not public; whether ITCH is order by order is not confirmed (https://www.lmaxdigital.com/documents/LMAXDigital-Institutional-Factsheet.pdf).
- **Bullish**: FIX order entry and drop copy, with cancel-on-disconnect per trading account. Market data is WS L1/L2 only (https://docs.exchange.bullish.com/fix/introduction, https://docs.exchange.bullish.com/websocket/public/market-data/orderbook).
- **Bitvavo**: FIX 4.4 order entry (port 8001) and drop copy (8005) with a public UAT and an XML dictionary (https://docs.bitvavo.com/docs/fix-api/introduction/, https://docs.bitvavo.com/api-specs/2026-04-28.fix.xml). Its WS market data is non-conflated L2 (https://docs.bitvavo.com/docs/ws-market-data-pro-sync/).
- **MEXC**: protobuf WS market data, L2 (https://www.mexc.com/api-docs/spot-v3/websocket-market-streams/protocol-buffers-integration).
- **Checked, nothing confirmed**:
  - KuCoin L3 was deprecated 2022-04-28 (https://www.kucoin.com/announcement/en-deprecate-the-kucoin-spot-futures-api).
  - BitMEX `orderBookL2` ids are per level (https://blog.bitmex.com/api_announcement/removing-id-schema-on-orderbook-feeds).
  - Cboe Digital, Paradigm, HTX, Gate, Upbit, Backpack, Vertex: no L3, binary or FIX found.

## Ranking

Scores are my reading of the sections above: 2 = strong, 1 = partial, 0 = none or not confirmed. Each criterion is defined as follows:
- **(a) Book fidelity:** order-by-order or full-depth data with a precise sync rule.
- **(b) Test environment:** a free, self-serve test environment covering order entry and fills.
- **(c) Machine-readable docs:** schemas, binary protocols, FIX dictionaries.
- **(d) Replay data:** public historical data usable for replay.
- **(e) Market relevance:** volume.

| Exchange | (a) book fidelity | (b) self-serve test env | (c) machine-readable | (d) replay data | (e) volume | Total | Notes |
|---|---|---|---|---|---|---|---|
| Coinbase Exchange | 2 (L3, per-product seq, REST L3 snapshot; FIX L3 with RptSeq) | 2 (sandbox: WS, FIX OE/MD/DC) | 2 (FIX XML, OpenAPI) | 0 | 1 | **7** | L3 needs an API key (sandbox or production) |
| Binance Spot | 1 (FD, precise rule, 5000-level cap) | 2 (testnet: REST/WS/SBE/FIX) | 2 (SBE XML, QuickFIX XML, OpenAPI, C++ SBE sample) | 1 (trades only) | 2 | **8** | No COD on spot |
| Deribit | 2 (Starbase L3 SBE with explicit priority; JSON FD with change_id) | 1 (testnet with OE; free sign-up not confirmed; Starbase test needs private link) | 2 (SBE XML, OpenAPI, AsyncAPI, FIX50) | 1 (L3 PCAP sample, public trades) | 1 | **7** | Live L3 needs LD4 or PrivateLink |
| Kraken | 2 (spot L3 with queue-order checksum, no seq) | 1 (futures demo free; no spot testnet) | 1 (OpenAPI; SBE per page for US Perps) | 1 (Time & Sales CSVs) | 1 | **6** | L3 needs an API key |
| Bybit | 1 (FD `orderbook.full`, precise `u`/`seq`) | 2 (testnet + demo) | 1 (SBE XML in pages, MM-only; FIX XML) | 2 (200-level deltas with `u`/`seq`, trades) | 2 | **8** | Best free L2 replay archive |
| OKX | 1 (400-level, `prevSeqId`; tbt needs VIP4) | 2 (demo, self-serve) | 1 (SBE XML, VIP4) | 2 (400-level ~10 ms updates, 5000-level, trades) | 2 | **8** | Archive lacks seqId |
| Binance USD-M / COIN-M | 1 (FD, `pu` chain; RPI hidden) | 1 (demo, realism not confirmed) | 0 | 1 | 2 | **5** | JSON only |
| Hyperliquid | 0 public (20-level snapshots); 2 via own node | 1 (testnet needs mainnet deposit) | 0 | 1 (L2 snapshots, fills; requester-pays) | 1 | **3** | Wallet signing, block-time latency |
| Bitstamp | 1 (L3 but no seq or sync rule) | 1 (sandbox, sign-up not confirmed) | 1 (OpenAPI REST) | 0 | 0 | **3** | |
| Bitfinex | 1 (L3 capped at 250 orders) | 1 (paper sub-account) | 0 | 0 | 0 | **2** | |
| Bitget | 1 (full book to 1000, `pseq`) | 1 (demo, KYC) | 1 (SBE XML in pages) | 1 (21 s snapshots) | 1 | **5** | |
| Gemini | 1 (FD, `U`/`u`) | 2 (sandbox, bot book) | 2 (OpenAPI, AsyncAPI) | 0 | 0 | **5** | No amend |
| Crypto.com | 0 (50 levels) | 0 (invitation only) | 1 (FIX XML) | 0 | 1 | **2** | FIX `MatchIndex` gives queue position |
| Coinbase Derivatives | 2 (L3 SBE multicast) | 0 (certification) | 2 | 0 | 0 | **4** | Colo only |

The totals weight the criteria equally. Weighting (a) more, because Kaspar's simulator is queue-position based, moves Coinbase Exchange, Deribit and Kraken above Bybit and OKX.

## Recommended first wave

1. **Coinbase Exchange: market data (L3) first, then order entry (FIX 5.0 SP2), then post-trade (FIX drop copy).**
   - **Why first:**
     - It is the only venue surveyed with all of these: an order-by-order feed with a precise per-product sequence rule; a REST L3 snapshot to sync against; and a free self-serve sandbox that carries the same WS and FIX order-entry, market-data and drop-copy endpoints.
     - The `full` channel maps directly to `l3_mbo_v2_t` and `l3_mbo_trd_v2_t`. Its event types are `open` (add), `change` (change, with the `modify_order` reason), `done` (delete) and `match` (execution against the maker order). That removes the `Mbp2Mbo` chunking for this venue.
     - Order entry and drop copy are FIX 5.0 SP2 with published XML dictionaries, so they fit the QuickFIX pattern in `ttom/` and `post_trade.md`.
     - Replace priority is documented: a size decrease keeps it. This matches OB's rule.
   - **Mapping notes:**
     - WS `match.side` is the maker's side; invert it for `aggresorSide`, or use FIX `AggressorSide`.
     - Client order ids must be UUIDv4.
     - Cancel-on-disconnect is FIX tag 8013.
   - **Today:** kaspar-hft has Coinbase Advanced Trade `level2` (L2). The Exchange L3 feed and all order entry and post-trade are new.
   - **Cost:** the `full`/`level3` channels need an API key.
2. **Binance Spot: order entry first (FIX 4.4, then FIX-SBE), post-trade second (WS API user data stream in SBE, `myTrades` for reconciliation), and an SBE market-data upgrade third.**
   - **Why second:**
     - Binance has the highest volume.
     - Its free testnet covers every interface: REST, WS API, SBE, and FIX order entry, drop copy and market data.
     - It publishes SBE XML and QuickFIX dictionaries, which fit `binary_codegen.md` and QuickFIX.
   - **Interface caveats:**
     - FIX drop copy is delayed 1 s, so post-trade should use the user data stream.
     - Spot has no cancel-on-disconnect, so the OM must cancel on its own disconnect path.
     - FIX does not support ResendRequest, so recovery after a reconnect is by order query.
     - FIX auth is Ed25519 only.
   - **Market data:**
     - kaspar-hft already has Binance spot and USD-M market data (JSON L2).
     - The SBE diff stream (20 ms, µs timestamps) is an upgrade of the existing feed, not a new feed, and needs an Ed25519 key.
     - Binance has no L3, so `Mbp2Mbo` stays.
   - **Futures:** USD-M/COIN-M order entry (WS API, `countdownCancelAll`) can follow on the same code, but it is JSON only.
3. **Deribit: market data first (Starbase SBE L3 decoder, built and tested offline), then order entry (JSON-RPC or FIX 4.4 against test.deribit.com), then post-trade (`user.trades` and `get_user_trades_*`).**
   - **Why:**
     - Deribit publishes SBE XML schemas and a free market-data PCAP. The L3 SBE handler can therefore be written and tested before any connectivity exists.
     - Its book carries an explicit, immutable queue-priority key (`sortOrderId`). That is a ground truth for OB's queue model no other public spec gives.
     - Trades come as TradeSummary plus one Trade per maker order. This matches `l3_mbp_trd_t` plus `l3_mbo_trd_v2_t`.
     - Deribit runs the Coinbase Global Derivatives gateway (JSON-RPC), so its order-entry code can be reused there.
   - **Limits:**
     - Live Starbase data needs LD4 colo or AWS PrivateLink.
     - The internet-reachable JSON-RPC book is full-depth L2 with a `change_id` chain, and its 1 ms `raw` interval needs auth.
     - Free sign-up for the testnet is not confirmed.
   - **Today:** not in kaspar-hft.
4. **Kraken Spot: market data first (WS `level3`), then order entry (WS v2, or FIX 4.4 by onboarding), then post-trade (WS `executions`).**
   - **Why:**
     - A second internet-reachable L3 feed.
     - Its CRC32 checksum walks orders in queue order, so every update verifies the book's queue state, not just its levels.
     - Amend priority rules are documented and match OB's.
     - Free Time & Sales archives give trade-side replay data.
   - **Limits:**
     - `level3` needs an API key, and the feed has no sequence numbers; integrity rests on the checksum.
     - The only free test environment is the Futures demo; spot has none.
     - The Derivatives WS shuts down 2027-03-01; derivatives work should target the Unified WS.
   - **Today:** not in kaspar-hft.

**Not in the first wave:**
- **Bybit and OKX:** they score as high on the equal-weight total and have the best free L2 replay archives. Bybit's 200-level deltas carry `u`/`seq`; OKX has 400 levels at about 10 ms. They are the natural second wave: L2 market data through `Mbp2Mbo`, archive replay into the simulator, then WS order entry on their demo environments.
- **Hyperliquid:** it keeps its existing L2 feed. L3 requires running a node, order entry requires EIP-712 wallet signing, and the testnet faucet requires a mainnet deposit, so it scores low on (a)–(c) for now.
