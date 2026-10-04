# Commercial replay tools: documentation check (pass 3)

Date: 2026-10-04. Supplements `claims_simulators2.md` sections C1-C5 and Table `tab:replay-tools`
(`sections/p3_sim_hw.tex`). No .tex file was edited.

Rules: every value below comes from a page actually read in this pass. "documentation silent (checked: ...)" means
the listed pages were read and do not say. "docs unreadable (tried: ...)" means the pages could not be read.
Quotes are verbatim.

Access notes:
- library.tradingtechnologies.com blocks plain HTTP clients (Cloudflare) and has no Wayback snapshots of the pages
  below. The body text was read through the site's WordPress REST API
  (`https://library.tradingtechnologies.com/wp-json/wp/v2/doc/<id>`); the page URLs are given with the IDs.
- ninjatrader.com redirects non-US clients to /eu/; NinjaTrader help pages were read from Wayback snapshots
  (timestamps given).
- Correction to claims_simulators2.md C1: the TT "Considerations" page is at
  `.../tt-backtesting/backtesting-reference/tt-backtesting-considerations/` (the `tt-backtesting-overview/` path
  returns 404).

---

## 1. Trading Technologies: TT Backtesting (ADL) and the TT Simulation environment

Pages read:
- [T1] Introduction, doc 6212: https://library.tradingtechnologies.com/tt-backtesting/tt-backtesting-overview/introduction-2/
- [T2] How TT Backtesting works, doc 6214: https://library.tradingtechnologies.com/tt-backtesting/tt-backtesting-overview/how-tt-backtesting-works/
- [T3] TT Backtesting Considerations, doc 6213: https://library.tradingtechnologies.com/tt-backtesting/backtesting-reference/tt-backtesting-considerations/
- [T4] Running a backtest, doc 6217: https://library.tradingtechnologies.com/tt-backtesting/backtesting-algos/running-a-backtest/
- [T5] Displaying backtest results, doc 6218: https://library.tradingtechnologies.com/tt-backtesting/backtesting-algos/displaying-backtest-results/
- [T6] ttbacktest REST documentation, doc 7944: https://library.tradingtechnologies.com/apis/tt-rest-api-2-0/api-reference-tt-rest-api-2-0/ttbacktest-documentation/
- [T7] Position in Queue (PIQ) Overview, doc 6667: https://library.tradingtechnologies.com/trade/viewing-market-data/position-in-queue-piq/position-in-queue-piq/
- [T8] Changing trading environments, doc 6727: https://library.tradingtechnologies.com/trade/overview/workspace-windows/task-workspace-windows/changing-trading-environments/
- [T9] ADL Estimated Position In Queue (EPIQ), doc 5403: https://library.tradingtechnologies.com/adl/adl-overview/advanced-concepts/description/estimated-position-in-queue-epiq/

Findings:
- Matching: the backtest matches algo orders "against the historical market data stream using the same simulation
  matching engine used in the TT production simulation environment" [T2]. In the Simulation environment "All orders
  are matched by our internal matching engine and never submitted to the exchange" [T8].
- Data: "actual book data consisting of the bids / offers as well as the trades", replayed "in the same time sequence
  as it occurred"; data from "approximately late 2018" [T3]. Price-level vs order-level: not stated. At high replay
  speed the Algo Server conflates: the algo "will receive the latest snapshot of the instrument's bids/asks along with
  a list of all trades", and "your ADL algo may not behave the same in a backtest as it does in production if you set
  the replay speed too high" [T3]. Maximum one trading day per backtest [T4].
- Queue: "In the Simulation environment, PIQ is estimated for all markets." Estimated PIQ is "calculated by TT based
  on the quantity of trades occurring in front of the order, and is a conservative estimate"; a cancel at the level
  does not improve it, but it is capped at the level's total quantity [T7]. Actual PIQ is available in production
  from "CME (Market by Order)", ICE and BIST feeds [T7]. Whether the simulation matching engine fills orders according
  to the estimated PIQ: not stated in [T1-T8]. [T9] describes the same conservative estimate as a user-built ADL
  pattern.
- Latency: not stated [T1-T8].
- Market impact: not stated [T1-T8].
- Determinism: not stated. [T3] says results depend on replay speed through conflation.
- Same code live: the backtest runs the ADL algo itself, with up to ten parameter instances [T2, T4].

## 2. NinjaTrader 8: Playback connection (Market Replay)

Pages read (NinjaTrader 8 Help Guide, Wayback snapshots):
- [N1] Playback, snapshot 20260314082855: https://ninjatrader.com/support/helpGuides/nt8/playback.htm
- [N2] Playback Connection > Set up, snapshot 20260420062733: https://ninjatrader.com/support/helpGuides/nt8/set_up12.htm
- [N3] Simulator, snapshot 20260515101238: https://ninjatrader.com/support/helpGuides/nt8/simulation.htm
- [N4] Options > Trading, snapshot 20260421082238: https://ninjatrader.com/support/helpGuides/nt8/options_trading.htm
- [N5] Understanding Historical Fill Processing, snapshot 20240417065559: https://ninjatrader.com/support/helpGuides/nt8/understanding_historical_fill_.htm
- [N6] IsFillLimitOnTouch, snapshot 20240521190631: https://ninjatrader.com/support/helpGuides/nt8/isfilllimitontouch.htm
- [N7] Tick Replay, snapshot 20260512130544: https://ninjatrader.com/support/helpGuides/nt8/tick_replay.htm
- [N8] Trading in Simulation, Sim101 account, Live/Simulation Environment, Global Simulation Mode (snapshots 2026):
  trading_in_simulation.htm, the_sim101_account.htm, live_simulation_environment.htm, global_simulation_mode.htm
- Not found in Wayback (404): simulator.htm, options_simulator.htm, simulator_options.htm, market_replay.htm,
  recording_live_data.htm.

Findings:
- Data: "Market Replay data is the most accurate and holds both level I and level II (market depth) data"; the file
  keeps them "perfectly in sync per instrument"; timestamps down to 100 ns but limited to the provider's granularity
  [N1]. Without replay data, Playback can use historical tick data, with no level II [N1, N2]. MBP vs MBO: not stated.
- Bid/ask sizes: "Ask and Bid Volume during playback with Market Replay or historical data will be simulated and set
  to "1" except for Equities and Forex" [N1].
- Queue: the live Simulator (Sim101) uses "ask/bid volume, trade volume, time (to simulate order queue position), and
  random time delays" [N3]. Whether Playback101 uses that queue model: not stated [N1-N4, N8]. Options: "Enforce
  immediate fills" bypasses the "advanced simulation fill engine"; "Enforce partial fills" forces partials [N4].
- "Liberal" fill / fill on touch: `IsFillLimitOnTouch` is for "back-testing purposes only"; default fills a limit
  "once price has penetrated the limit price", true fills it when price "has reached the limit price" [N6]. The
  Strategy Analyzer (backtest, not Playback) fills on bars split into "three virtual bars", or on a finer secondary
  series with "High" order fill resolution [N5]. Tick Replay "is not intended to function in NinjaScript strategy
  backtests" [N7].
- Latency: "Playback101 and Sim101 work differently when executing, since simulated internet latency delay simulation
  is not present in playback" [N1]. Not configurable (no setting found).
- Market impact: not stated [N1-N4, N8].
- Determinism: Playback101 orders "are processed immediately and synchronously. This enables reproducible results for
  strategy developers that run a strategy on the playback connection" [N1].
- Same code live: NinjaScript strategies run on the Playback connection [N1, tip on strategy testing]; the same
  platform routes to live and sim accounts side by side [N8 live_simulation_environment.htm].
- Feeds: replay files recorded from the user's live data provider, or downloaded from NinjaTrader ("Only the most
  common instruments are currently available") [N2].

## 3. Deltix (EPAM): QuantOffice, Exchange Simulator, TimeBase

Pages read:
- [D1] QuantOffice (current vendor page): https://www.deltixlab.com/quantoffice
- [D2] Exchange Simulator (archived vendor page, snapshot 20240208125153): https://www.deltixlab.com/products/exchange-simulator/
- [D3] QuantOffice (archived vendor page, snapshot 20240617084202): https://www.deltixlab.com/products/quantoffice/
- [D4] QuantOffice Product Sheet (PDF, archived): http://www.deltixlab.com/wp-content/uploads/2015/08/QuantOfficeProductSheet.pdf
- [D5] Execution Server (archived, snapshot 20230927175618): https://www.deltixlab.com/products/execution-server/
- [D6] QuantOffice Cloud (archived, snapshot 20250122095335): https://deltix.io/products/quantoffice.html
- Also read, not relevant: "QuantOffice - Portfolio Backtesting and Simulation Solution" PDF (2007),
  http://www.deltixlab.com/files/BacktestingAndSimulation.pdf (portfolio/bar level; no fill model).
- Unreadable: https://kb.quantoffice.cloud/ ("Some of our content is exclusively available to authorized users");
  kb.deltixlab.com, docs.deltixlab.com, docs.deltix.io (no DNS / no snapshot); docs.deltixhub.com (HTTP 401 in the
  2021 snapshot).

Findings:
- Data: "a variety of simulators, from coarse bar-based to substantially more precise L2 (MBP and MBO) simulators"
  [D1]. Exchange Simulator: "market data adapters for Level 1, Level 2 and Level 3" [D2].
- Matching (Exchange Simulator): "The simulator maintains an order book for each instrument. Orders sent for
  simulated execution ... are filled according to the volume available in the order book (at the appropriate price
  levels in the case of a limit order)" [D2].
- Queue: not stated [D1-D6]. [D2] says fills follow "the volume available in the order book" but does not say how a
  resting order's place in the queue is treated.
- Latency: Exchange Simulator: "Latencies can be dialed up and down to simulate the actual production environment"
  [D2]. QuantOffice trading simulator: "specifying the number of ticks which elapse between order creation and
  execution, percentage order completion" [D3, D4].
- Market impact: yes, the replayed book is altered. "The removal of liquidity from the order book by the simulated
  execution is reflected in the state of the order book"; "Market data modified by simulated orders is published via
  FIX or Deltix multi-cast API" [D2].
- Determinism: not stated [D1-D6].
- Same code live: "The same trading strategy created in QuantOffice is deployed in production for live trading "as
  is". The switch between the trading simulators, paper trading and live trading is transparent to the trading
  strategy" [D4]. "switched from Paper trading to Live trading using QuantOffice trading connectors" [D1].
- Feeds: "All Deltix products support Equities, Futures, Bonds, ETF, FX, and Synthetics" [D2]; data from TimeBase
  [D3]. Specific exchanges/feed protocols: not stated.

## 4a. MultiCharts: Data Playback / Simulated Trading / Bar Magnifier

Pages read (Wayback):
- [M1] Data Playback, snapshot 20240725042729: https://www.multicharts.com/trading-software/index.php/Data_Playback
- [M2] How to Use Simulated Trading, snapshot 20240910050832: https://www.multicharts.com/trading-software/index.php?title=How_to_Use_Simulated_Trading
- [M3] Bar Magnifier, snapshot 20240413161022: https://www.multicharts.com/trading-software/index.php/Bar_Magnifier
- [M4] Auto Trading, snapshot 20240422041247: https://www.multicharts.com/trading-software/index.php?title=Auto_Trading
- Direct fetches of the wiki return HTTP 403.

Findings:
- "In MultiCharts 15 data playback was completely redesigned and transformed into the trading simulator" [M1].
  Simulated Trading plays back "historical data on charts and in DOM windows" with "manual and auto trading during
  playback"; "Advanced Simulated Trading uses Level 2 data as well as Level 1" [M2].
- Bar Magnifier is a bar-backtest feature that replays how a bar was formed from finer bars or ticks [M3].
- Fill rule, queue, latency, market impact, determinism: not stated [M1-M3].
- Same code live: strategies auto-trade through a broker profile [M4]; auto trading during playback [M2].
- Feeds: Level 1/2 "from any supported data provider or from our servers using Market Data Sim feed"; Market Data
  Sim covers "Futures, Crypto, Stock, Forex, and Index instruments: several months of minute and one week of tick
  Level 1 and Level 2 data" [M2].

## 4b. Rithmic: R|Trader Pro / Exchange Simulator

Pages read: https://www.rithmic.com/products/exchange-simulator and https://www.rithmic.com/rtraderpro are a
JavaScript app; the text was read from the site bundle `https://www.rithmic.com/assets/index-C3BFXBna.js`
(fetched 2026-10-04).
- "Exchange Simulator is not a replay system or a synthetic data feed. It connects to live exchange data in real time
  ... orders are filled against a simulated matching engine rather than the actual exchange."
- The only "Market Replay" text in the bundle is for Optimus Flow, a third-party platform ("True tick-for-tick
  historical replay using Rithmic data").
- Result: Rithmic documents no historical-replay simulator of its own. Out of scope for the table.
  Fill/queue rules of the paper-trading matcher: not stated.

## 4c. CQG

- Wayback CDX for help.cqg.com lists `cqgic/Documents/backtesting.htm` (read, snapshot 20160926115942): trade-system
  backtesting is a chart study display ("entry and exit points ... profits or losses"). No replay or fill model is
  described there.
- `cqgic/Documents/demotrading.htm`: snapshot is a 404 page.
- Result: docs unreadable / silent for a replay fill simulator (tried: help.cqg.com via Wayback CDX, the two pages
  above). Leave out.

## 5. Bookmap: Replay mode with the Bookmap simulator

Pages read (Bookmap Knowledge Base, live, 2026-10-04):
- [B1] Select run mode: https://bookmap.com/knowledgebase/docs/KB-GettingStarted-SelectRunMode
- [B2] Orders Management (Order Queue Approximation): https://bookmap.com/knowledgebase/docs/KB-Trading-Orders-Management
- [B3] Trading Simulator Rules: https://bookmap.com/knowledgebase/docs/KB-Trading-Simulator-Rules
- [B4] Trading Simulator (add-on): https://bookmap.com/knowledgebase/docs/Addons-Trading-Simulator
- [B5] Export/Import Bookmap files: https://bookmap.com/knowledgebase/docs/KB-SettingUpAndOperating-ExportImportBookmapFiles
- [B6] API general info: https://bookmap.com/knowledgebase/docs/KB-API-GeneralInfo
- [B7] Python API: https://bookmap.com/knowledgebase/docs/Addons-Python-API
- The KB sitemap (https://bookmap.com/knowledgebase/sitemap.xml, 127 English pages) has no other replay or
  simulator page.

Findings:
- Replay: "Replay mode lets you replay previously recorded market depth data files"; "Using the Bookmap simulator,
  traders can execute simulated orders" [B1]. Feed files (.bmf) "Contain recorded market data for an instrument,
  including market depth and the Best Bid and Offer (BBO)" [B5]. MBP vs MBO: not stated.
- Queue: Bookmap displays "an approximation of a trader's order position in the queue ... according to a FIFO
  matching algorithm using a more pessimistic scenario relating to canceled orders" (One-Click Trading add-on) [B2].
  Whether the simulator's fills in Replay use this approximation, or any queue model: not stated [B1-B5].
- Simulator rules: "We do not currently support partial fill" [B3]. [B3, B4] describe the real-time Trading
  Simulator ("continuous trading with real-time data", CME ES, MES, NQ, MNQ [B4]); whether the same rules apply in
  Replay: not stated.
- Latency, market impact, determinism: not stated [B1-B6].
- Same code live: add-ons "can be used in real time and in replay" [B6]; the Python API "supports all L1 API
  features, except replay mode" [B7].
- Feeds: replay of .bmf files recorded from the user's connection (recording checkbox in [B1]) or downloaded.

## 6. Sierra Chart: Trade Simulation Mode during Chart Replay

Pages read (live, 2026-10-04):
- [S1] Trade Simulation: https://www.sierrachart.com/index.php?page=doc/TradeSimulation.php
- [S2] Replaying Charts: https://www.sierrachart.com/index.php?page=doc/ReplayChart.html
- [S3] ACSIL Trading: https://www.sierrachart.com/index.php?page=doc/ACSILTrading.html

Findings (corrects claims_simulators2.md C4, which said fills come "from the High/Low/Last values"):
- Fill rule: "Limit orders are filled based upon the Bid and Ask prices. A Buy Limit order will only be filled when
  the Ask price is equal to or less than the limit price. And it will be filled at the Ask price" (sell side
  symmetric) [S1]. A resting limit fills only when the opposite best quote reaches its price, not when the same-side
  quote or a trade touches it.
- Bid/ask during replay: Method 1, with 1-tick intraday storage, "the actual Bid and Ask prices during a replay are
  set to what they actually were during the real-time trading" (data from June 23, 2014); Method 2, otherwise,
  "derived from the High, Low, Close/Last values of the underlying data records", always 1 tick apart [S1]. So the
  High/Low/Last rule applies only when actual bid/ask were not recorded.
- Queue: "Estimated Position in Queue Tracking ... This option only applies to non-replaying charts" [S1]. "it is not
  known where in the order queue your order exists" (back-testing caveat) [S2]. Replay fills: no queue.
- Sizes: "the Bid Size and Ask Size values will always be 1 within the chart during a replay"; replayed market depth
  levels keep actual quantities [S2]. Depth download "only supported with certain exchanges (CME, CBOT, NYMEX, COMEX,
  EUREX, CFE, NYSE, NASDAQ, AMEX) and only when using the Denali Exchange Data Feed or the Delayed Exchange Data
  Feed" [S2].
- Latency: not stated [S1-S3]. During replay the fill timestamp is "the ending time of the most recent chart bar" [S1].
- Market impact: not stated [S1, S2].
- Determinism: Accurate Trading System Back Test Mode "will give a consistent result every time you perform a back
  test of your trading system" [S2].
- Same code live: ACSIL trading functions run in simulation and live; `sc.SendOrdersToTradeService` and the global
  Trade Simulation Mode switch between simulated and non-simulated orders ("Going from Simulation Mode to Live
  Trading") [S3].
- The server-based Simulated Trading Service "cannot be used during chart replays" [S1].

## 7. ATAS: Replay Trading Simulator (added)

Page read: [A1] https://help.atas.net/en/support/solutions/articles/72000662219-replay-trading-simulator-in-atas
(modified "Tue, 22 Sep"; found via the help-site search for "market replay").
- Data: three replay data types: bars only ("Best bid and ask prices are calculated approximately"), every trade, and
  "Every trade plus the full order book" (Ticks and DOM) [A1].
- Fills: "Orders in Replay are filled perfectly: at the specified price and immediately in full. In live trading,
  slippage, queue position in the order book, partial fills, and broker-specific behavior can affect execution" [A1].
  So no queue and no latency.
- Market impact, determinism: not stated. Same code live: [A1] describes manual trading only ("Place orders the same
  way as in live trading"); strategies: not stated. Feeds: "Charts that work through dxFeed cannot be replayed" [A1].

## 8. Other commercial tools with exact queue position in recorded order-level data

None found in this pass. The only commercial documentation read that mentions exchange-reported (actual) queue
position is TT's PIQ page, and that is for live trading on CME MBO, ICE and BIST feeds; TT's Simulation environment
estimates PIQ for all markets [T7]. Web search was unavailable in this pass (session search budget exhausted), so
tools not already listed in claims_simulators2.md were not searched for.

---

# Rows for Table tab:replay-tools

Columns: Tool | Data | Own-order queue position | Latency | Recorded market unchanged | Same code live | Feeds

| Tool | Data | Own-order queue position | Latency | Recorded market unchanged | Same code live | Feeds |
|---|---|---|---|---|---|---|
| TT Backtesting (`tt_backtesting`, `tt_piq`, `tt_sim_env`) | "book data ... bids / offers as well as the trades"; conflated to snapshots plus trades at high replay speed [T3]. MBP vs MBO: documentation silent (checked: T1-T6) | estimated: "In the Simulation environment, PIQ is estimated for all markets"; estimate = quantity ahead reduced by trades, not by cancels [T7]. Whether the matcher fills by it: documentation silent (checked: T1-T8) | documentation silent (checked: T1-T8) | documentation silent (checked: T1-T8) | yes: the ADL algo itself [T2, T4] | historical data "back to approximately late 2018" [T3]; exchange list documentation silent (checked: T1-T6) |
| NinjaTrader 8 Playback (`ninjatrader_playback`, `ninjatrader_sim`, `ninjatrader_opts`) | level I + level II (market depth), recorded or downloaded [N1, N2]; bid/ask volume "set to "1"" in playback [N1]; MBP vs MBO documentation silent (checked: N1, N2) | Playback: documentation silent (checked: N1-N4, N8). Sim101 (live sim) uses "time (to simulate order queue position)" [N3] | none: "simulated internet latency delay simulation is not present in playback" [N1] | documentation silent (checked: N1-N4, N8) | yes: NinjaScript strategies on Playback connection [N1]; live and sim accounts side by side [N8] | recorded from user's data provider; NinjaTrader download for "the most common instruments" [N2] |
| Deltix QuantOffice / Exchange Simulator (`deltix_quantoffice`, `deltix_exsim`, `deltix_qo_sheet`) | bar to "L2 (MBP and MBO)" simulators [D1]; Exchange Simulator adapters "Level 1, Level 2 and Level 3" [D2] | documentation silent (checked: D1-D6); fills "according to the volume available in the order book" [D2]; kb.quantoffice.cloud login-only | configurable: "Latencies can be dialed up and down" [D2]; ticks between order creation and execution [D3, D4] | no: "removal of liquidity ... is reflected in the state of the order book" [D2] | yes: deployed "as is"; switch "transparent to the trading strategy" [D4] | asset classes "Equities, Futures, Bonds, ETF, FX" [D2]; data via TimeBase [D3]; venues documentation silent (checked: D1-D6) |
| Bookmap Replay (`bookmap_kb`, `bookmap_replay`) | recorded "market depth and the Best Bid and Offer" (.bmf) [B5]; MBP vs MBO documentation silent (checked: B1-B5) | displayed estimate ("FIFO ... pessimistic scenario relating to canceled orders") [B2]; used for fills: documentation silent (checked: B1-B5) | documentation silent (checked: B1-B6) | documentation silent (checked: B1-B6) | yes: add-ons "can be used in real time and in replay" [B6] (Python API excluded from replay [B7]) | user-recorded feed files [B1, B5]; real-time simulator CME ES/MES/NQ/MNQ [B4] |
| Sierra Chart replay (`sierrachart_tradesim`, `sierrachart_replay`, `sierrachart_acsil`) | trades with actual bid/ask (1-tick storage) or bid/ask estimated from High/Low/Last; depth replayed for display; bid/ask size 1 [S1, S2] | none: limit fills when opposite quote reaches the price; queue tracking "only applies to non-replaying charts" [S1] | documentation silent (checked: S1-S3) | documentation silent (checked: S1, S2) | yes: ACSIL, `sc.SendOrdersToTradeService` [S3]. Determinism: "consistent result every time" in Accurate Trading System Back Test Mode [S2] | depth download for CME, CBOT, NYMEX, COMEX, EUREX, CFE, NYSE, NASDAQ, AMEX via Denali [S2] |
| MultiCharts Simulated Trading (`multicharts_sim`) | Level 1 and Level 2 [M2] | documentation silent (checked: M1-M3) | documentation silent (checked: M1-M3) | documentation silent (checked: M1-M3) | auto trading during playback [M2] and to broker [M4] | any supported provider or Market Data Sim (futures, crypto, stocks, forex, indices) [M2] |
| ATAS Replay (`atas_replay`) | bars, trades, or trades + full order book [A1] | none: "filled perfectly: at the specified price and immediately in full" [A1] | none (immediate fills) [A1] | documentation silent (checked: A1) | manual trading only described; strategies documentation silent (checked: A1) | subscription-dependent; dxFeed charts cannot be replayed [A1] |
| Rithmic | out of scope: Exchange Simulator "is not a replay system" (rithmic.com JS bundle) | -- | -- | -- | -- | -- |
| CQG | docs unreadable for a replay fill simulator (tried: help.cqg.com Wayback CDX; cqgic/Documents/backtesting.htm; cqgic/Documents/demotrading.htm) | -- | -- | -- | -- | -- |

Determinism summary: stated for NinjaTrader Playback ("reproducible results" [N1]) and Sierra Chart Accurate Trading
System Back Test Mode [S2]; documentation silent for TT, Deltix, Bookmap, MultiCharts, ATAS.

---

# Summary

- No commercial tool read documents filling simulated orders by exact position in a recorded order-level queue.
- TT: the backtest uses TT's simulation matching engine, whose documented queue position is an estimate for all
  markets. Data level, latency and impact are not documented.
- NinjaTrader Playback: depth replayed but bid/ask volume set to 1; no latency in playback; reproducible; queue
  handling in Playback not documented. The "liberal" fill-on-touch option (`IsFillLimitOnTouch`) applies to
  Strategy Analyzer backtests only.
- Deltix: the only commercial tool read with configurable latency and order-level (L3) input documented; its
  Exchange Simulator removes the simulated order's liquidity from the replayed book, so the recorded market is
  altered. Queue handling not documented; the knowledge base needs a login.
- Sierra Chart: correction: replay limit fills come from the bid/ask (actual if recorded at 1 tick, else estimated
  from High/Low/Last), with no queue model in replay.
- ATAS (new): replay fills are immediate and complete at the order price.
- Bookmap and MultiCharts: depth replay with simulated orders; fill and queue rules not documented.
- Rithmic has no replay simulator (its Exchange Simulator runs on live data). CQG: nothing readable.
- New BibTeX entries are in `bib/refs_sim3.bib`.
