# Parallel historical-replay runs: documentation check

Date: 2026-10-04. Question: can each tool run many historical-replay backtests at once (many days, or many
parameter settings) on the user's own machine or cloud instance, and does its licence or architecture restrict that?
Comparison point (user description, not verified here): Kaspar replays one trading day per process (one PCAP or one
decoded file), single-threaded and deterministic, so many days run as independent processes on one large machine or
cloud instance, with no per-run licence limit.

No .tex file was edited. New BibTeX entries are in `bib/refs_parallel.bib`.

Rules: every statement below comes from a page, licence text or repository file read in this pass. Quotes are
verbatim, including the vendors' typos. "documentation silent (checked: ...)" means the listed pages were read and
do not address the point.

Access notes:
- TT Help Library: page bodies read through the site's WordPress REST API
  (`https://library.tradingtechnologies.com/wp-json/wp/v2/doc/<id>`), as in `claims_simulators3.md`. Plain browser
  requests are blocked by Cloudflare.
- ninjatrader.com redirects non-US clients to `/eu/` (this includes WebFetch), so NinjaTrader help pages were read
  from web.archive.org snapshots (timestamps given).
- The MultiCharts wiki returns HTTP 403 to direct requests and was read from web.archive.org snapshots.
- The Deltix knowledge base (`kb.quantoffice.cloud`) needs a login: "Some of our content is exclusively available to
  authorized users."
- WebSearch was not available (session search budget used up). Pages were found through site sitemaps, site search
  endpoints, the TT WordPress search API and the Wayback CDX index.
- Repositories: shallow clones at HEAD on 2026-10-04. Commits: hftbacktest 5f3ec40 (2025-12-23); nautilus_trader
  b0a9868 (2026-10-04); lobsim 0cb48ed (2026-02-03); abides-sim/abides c4bf157 (2020-11-19); abides-jpmc-public
  f9cbe51 (2023-12-13); KangOxford/jax-lob d1f5966 (2023-10-22). Nothing was built or run.

---

## 1. Trading Technologies: TT Backtesting (ADL)

Pages read (TT Help Library, REST API, 2026-10-04; "modified" dates from the API):
- [T1] Introduction, doc 6212 (mod. 2026-01-23): https://library.tradingtechnologies.com/tt-backtesting/tt-backtesting-overview/introduction-2/
- [T2] How TT Backtesting works, doc 6214 (mod. 2026-01-06): https://library.tradingtechnologies.com/tt-backtesting/tt-backtesting-overview/how-tt-backtesting-works/
- [T3] Considerations, doc 6213 (mod. 2025-12-09): https://library.tradingtechnologies.com/tt-backtesting/backtesting-reference/tt-backtesting-considerations/
- [T4] Running a backtest, doc 6217 (mod. 2026-01-23): https://library.tradingtechnologies.com/tt-backtesting/backtesting-algos/running-a-backtest/
- [T5] Displaying backtest results, doc 6218: https://library.tradingtechnologies.com/tt-backtesting/backtesting-algos/displaying-backtest-results/
- [T6] ttbacktest REST documentation, doc 7944 (mod. 2026-01-05): https://library.tradingtechnologies.com/apis/tt-rest-api-2-0/api-reference-tt-rest-api-2-0/ttbacktest-documentation/
- [T7] Getting started, doc 6216: https://library.tradingtechnologies.com/tt-backtesting/backtesting-algos/getting-started-3/
- [T8] Enabling TT Backtesting (beta), doc 7576: https://library.tradingtechnologies.com/setup/company-administration/advanced-features/description-advanced-features/enabling-tt-backtesting-beta/
- [T9] Troubleshooting, doc 6219: https://library.tradingtechnologies.com/tt-backtesting/backtesting-algos/troubleshooting/
- [T10] Dashboard, doc 6215: https://library.tradingtechnologies.com/tt-backtesting/tt-backtesting-overview/tt-backtesting-dashboard/
- Not found: a TT pricing page (`tradingtechnologies.com/resources/pricing/` returns 404; the WordPress page object
  has empty content).

Findings:
- Where it runs: documented only as a TT-hosted service. "TT Backtesting can only be accessed in the simulation
  environment. Therefore, all requests to the ttbacktest service of the TT REST API use the following base URL:
  https://ttrestapi.trade.tt/ttbacktest/ext_prod_sim" [T6]. Status values include "PENDING = the backtest is
  connecting to the TT backend and preparing to launch", "LAUNCHING = the backtest instance has spun up", and
  "PRICE_DOWNLOAD = the backtest is downloading the market data playback files" [T6]. The backtest "Downloads the
  historical data for the specified period" [T2]. No installable or self-hosted version is described [T1-T10].
- Billing: "You will be still be charged for the time a backtest ran if you stop a backtest manually" [T4]. The
  pre-test checks "occur before TT Backtest expends the resources for which you are charged" [T9].
- Entitlement: "A TT Pro license" and "TT Backtesting Package enabled" are required [T7]; "you or your company must
  have a direct billing agreement with TT" [T8].
- Parameter runs per backtest: "For each backtest, you can run up to ten different parameter configurations, called
  algo instances" [T2]; "Note: You can backtest a maximum of ten instances" [T4].
- Days per backtest: "Currently, a backtest's range cannot be greater than one trading day" [T4]; "In the first
  version of TT Backtesting, you will only be able to choose a maximum of one day's worth of data to be replayed"
  [T3].
- Several backtests at once: the dashboard lets you "view the progress of any currently running backtest" [T10].
  Whether several backtests (for example several days) may run concurrently, and any cap on that: documentation
  silent (checked: T1-T10).
- Verdict: multi-day or multi-parameter runs happen on TT's infrastructure, metered by run time, at most ten
  parameter instances and one trading day per backtest. Running the replay on the user's own hardware or cloud: not
  offered in the documentation read.

Aside, not the ADL tool: TT Strategy Studio (press release, 13 May 2025) [T11]
https://tradingtechnologies.com/news-releases/trading-technologies-tt-strategy-studio-introduced-broadly-to-meet-algorithmic-trading-needs-of-sophisticated-professional-trading-firms-hedge-funds/
says it offers "full tick-by-tick backtesting with complete depth of market", that clients "can leverage our hosted
ultra-low-latency infrastructure", that proprietary code is "a separate component that can leverage TT Strategy
Studio from within their own server", and that "The software can run all risk/reward ratios and millions of
iterations on a strategy through its high-performance calculations". Whether backtests run on client hardware, and
any licence limit on parallel runs: not stated. This is a press release, not documentation.

## 2. NinjaTrader 8: Playback (Market Replay) and Strategy Analyzer

Pages read (NinjaTrader 8 Help Guide, Wayback snapshots):
- [N1] Playback, snapshot 20260314082855: https://ninjatrader.com/support/helpGuides/nt8/playback.htm
- [N2] Optimize a Strategy, snapshot 20260421070432: https://ninjatrader.com/support/helpGuides/nt8/optimize_a_strategy.htm
- [N3] Backtest a Strategy, snapshot 20260512124844: https://ninjatrader.com/support/helpGuides/nt8/backtest_a_strategy.htm
- [N4] Multiple Connections, snapshot 20250418163110: https://ninjatrader.com/support/helpGuides/nt8/multiple_connections.htm
- [N5] Playback Connection (Configuration), snapshot 20250808110456: https://ninjatrader.com/support/helpGuides/nt8/playback_connecting_connection.htm
- [N6] Licensing/User Authentication, snapshot 20250208213200: https://ninjatrader.com/support/helpGuides/nt8/licensing_user_authentication.htm
- [N7] Installation Guide, snapshot 20260411042437: https://ninjatrader.com/support/helpGuides/nt8/installation_guide.htm
- Also read: Optimizer (NinjaScript), Walk Forward Optimize, Multi-Threading (NinjaScript), Strategy Analyzer index;
  EULA snapshot 20030819031947 (http://ninjatrader.com/EULA.htm; from 2003, not used).
- Not readable: the live help pages (geo-redirect to /eu/); `connecting.htm` snapshot was binary/garbled.

Findings:
- Playback: one Playback connection with a single play head. "The Playback control is set up much like a DVD
  player"; "Slide control Selects a point in time to start replay"; "Playback from start Market Replay data is
  played back for every day between the start point of the slider and the end point of the slider" [N1]. "The
  Playback connection is a default connection installed with NinjaTrader" [N5]. Several days are therefore replayed
  in sequence in one session. Running several Playback sessions at the same time (on one PC or several):
  documentation silent (checked: N1, N4, N5, N7).
- Strategy Analyzer (bar/tick backtest, not the level II Market Replay): optimisation is multi-core and NinjaTrader
  advises against running separate tests side by side. "Running multiple tests at a time You will not get more done
  in a smaller time frame by separating multiple tests out manually and running them at the same time on the same PC.
  NinjaTrader will efficiently use all CPU cores for any optimization for fastest possible testing" [N2]. Cloud use
  is mentioned: "If you are using a virtual or cloud server as basis for your setup when running optimization
  testing in the Strategy Analyzer ... NinjaTrader will still take advantage of all available threads" [N2]. The
  backtest needs "Access to historical data" and an instrument plus "Data Series" (bar type) [N3]; use of Market
  Replay level II data in the Strategy Analyzer: not stated (checked: N2, N3).
- Licence: the help guide's only licensing page is for third-party vendors ("Licenses are exclusively tied to a
  combination of user-defined prefix + PC machine ID value") [N6]. The platform's own licence terms (machines,
  concurrent sessions): documentation silent (checked: N6, N7; current EULA not found in Wayback CDX).
- Verdict: parallel multi-core runs documented for Strategy Analyzer optimisation on bar/tick data, including on a
  cloud server. For the depth replay (Playback), parallel runs are not documented; the documented mode is one
  sequential replay per session.

## 3. Deltix (EPAM): QuantOffice / TimeBase

Pages read:
- [D1] QuantOffice (live, 2026-10-04): https://www.deltixlab.com/quantoffice
- [D2] QuantOffice Studio (live): https://www.deltixlab.com/quantoffice/architecture/quantoffice-studio
- [D3] QuantOffice architecture, Why QuantOffice, Execution Server (live): https://www.deltixlab.com/quantoffice/architecture,
  https://www.deltixlab.com/quantoffice/why-quantoffice, https://www.deltixlab.com/quantoffice/architecture/execution-server
- [D4] QuantOffice Cloud (archived, snapshot 20250122095335): https://deltix.io/products/quantoffice.html
- [D5] QuantOffice (archived, snapshot 20240617084202): https://www.deltixlab.com/products/quantoffice/
- [D6] TimeBase (live): https://www.deltixlab.com/timebase
- [D7] TimeBase Community Edition repo metadata: https://github.com/finos/TimeBase-CE (gh api: Apache-2.0, pushed
  2026-09-08)
- Unreadable: https://kb.quantoffice.cloud/ (login), https://kb.timebase.info/ (no body text returned).

Findings:
- Where it runs: "QuantOffice Studio is a desktop application where you can develop and backtest strategies (based
  on the historical market data provided by TimeBase" [D2]. A SaaS form also exists: "QuantOffice ... is getting its
  second life as a Cloud-Based SaaS" [D4].
- Parallel runs: the archived QuantOffice Cloud page lists "Parallel experiments runner and parameter tuning
  framework" and a testing engine "capable of playing up to a million price changes per second per core" [D4]. The
  desktop product: "Optimizer, another QuantOffice Studio extension, can run Brute Force or Genetic Optimization
  processes" and "The Multi-Strategy Runner extension of QuantOffice Studio backtests strategy assemblies and
  portfolios" [D1]. Whether these runs use several cores or machines, and whether many simulator runs (e.g. one per
  day) can be launched concurrently on the user's own hardware: documentation silent (checked: D1-D5).
- Data store: TimeBase "can consist of a single process and be scaled up to 10 nodes" and runs "in both standalone and
  cluster mode" [D6]; a Community Edition is Apache-2.0 [D7]. This concerns the database, not the backtester.
- Licence terms (per seat, per core, per machine): documentation silent (checked: D1-D6; KB login-only).
- Verdict: a parallel experiments runner is advertised for the cloud product (archived vendor page); licence and
  self-hosted parallelism are not documented in readable pages.

## 4. Bookmap

Pages read (Bookmap Knowledge Base, live, 2026-10-04; full sitemap https://bookmap.com/knowledgebase/sitemap.xml,
127 English pages, scanned by title):
- [B1] System Requirements (last updated Jul 27, 2026): https://bookmap.com/knowledgebase/docs/KB-IntroductionToBookmap-SystemRequirements
- [B2] General Errors and Crashes (last updated Apr 28, 2026): https://bookmap.com/knowledgebase/docs/KB-Errors-General-Errors-And-Crashes
- [B3] Select Run Mode (last updated Sep 21, 2026): https://bookmap.com/knowledgebase/docs/KB-GettingStarted-SelectRunMode
- [B4] Export and Import of Bookmap files: https://bookmap.com/knowledgebase/docs/KB-SettingUpAndOperating-ExportImportBookmapFiles
- [B5] Subscribe and Download: https://bookmap.com/knowledgebase/docs/KB-GettingStarted-SubscribeDownload
- Also read: FAQs Basics, FAQs Performance, FAQs Strategies, Automated Strategies, Versions, Other Issues.

Findings:
- Licence / machines: "You can install Bookmap on multiple computers, but it can only run on one at a time.
  However, the Bookmap Global+ version enables access to Replay Mode on a secondary machine" [B1].
- Same machine: "Another Instance Is Still Running This message is displayed if you try running multiple Bookmaps in
  parallel on the same computer" [B2].
- Replay unit: "Replay mode lets you replay previously recorded market depth data files"; "You can open Replay mode
  by double-clicking a recorded data file" [B3]; ".bmf files can be opened only in Replay Mode" [B4].
- Tier: "Certain modes may be unavailable for Digital Free users, as this subscription tier does not include support
  for Trading or Replay modes" [B3].
- Verdict: restricted. One running copy per licence, no parallel copies on one computer; Global+ adds Replay on one
  secondary machine.

## 5. MultiCharts

Pages read (MultiCharts wiki, Wayback):
- [M1] How to use your license on two computers at the same time, snapshot 20181008214116 (page "last modified on
  30 May 2017"): https://www.multicharts.com/trading-software/index.php/How_to_use_your_license_on_two_computers_at_the_same_time
- [M2] Performing Optimization, snapshot 20210426073811: https://www.multicharts.com/trading-software/index.php/Performing_Optimization
- [M3] Understanding Optimization, snapshot 20240725044524: https://www.multicharts.com/trading-software/index.php/Understanding_Optimization
- [M4] Portfolio Trader, snapshot 20240415200718: https://www.multicharts.com/trading-software/index.php/Portfolio_Trader
- [M5] Portfolio Optimization, snapshot 20160923221746: https://www.multicharts.com/trading-software/index.php/Portfolio_Optimization
- [M6] How to Use Simulated Trading, snapshot 20240910050832: https://www.multicharts.com/trading-software/index.php?title=How_to_Use_Simulated_Trading

Findings:
- Licence: "a MultiCharts license can be running in online mode only on one PC at a time. In order to get real time
  data and trade, MultiCharts needs to be authorized and running in online mode. You can use your license on PC#2 in
  offline mode for writing strategies and backtesting using locally stored data while your PC#1 is in online mode"
  [M1]. The workaround must be repeated "every 30 days" [M1]. The page dates from 2017.
- Optimisation: "Since MultiCharts 10 that is possible to pause the optimization process in order to temporary
  decrease the load on the CPU" [M2]. Use of several cores, or of several instances on one PC: documentation silent
  (checked: M2-M5).
- Simulated Trading (depth playback): several concurrent playback sessions: documentation silent (checked: M6).
- Verdict: licence documented as one online PC plus one offline PC for backtesting on local data (2017 page). Parallel
  replay runs: documentation silent.

## 6. Sierra Chart

Pages read (live, 2026-10-04):
- [S1] Purchase Information, "Licensing / Number of Systems": https://www.sierrachart.com/index.php?page=doc/PurchaseInformation.php
- [S2] Using DTC Server for Data and Trading in Another Sierra Chart Instance (New Instances): https://www.sierrachart.com/index.php?page=doc/NewInstance.php
- [S3] Auto Trade System Back Testing: https://www.sierrachart.com/index.php?page=doc/Backtesting.php
- [S4] Replaying Charts: https://www.sierrachart.com/index.php?page=doc/ReplayChart.html
- Also read: Trade Simulation (TradeSimulation.php), Service Packages (Packages.php).

Findings:
- Licence, same machine: "You can install Sierra Chart multiple times (no restrictions on the number of
  installations/copies) on the same computer system and each of those copies can be used concurrently without
  restriction"; "You can have many tens of copies of Sierra Chart on the same system running at the same time with a
  single license" [S1].
- Licence, several machines: "each Sierra Chart account can by default be used on 2 computer systems at the same time
  or concurrently . This may be up to 3 systems depending upon the Service Package"; "If you wish to use Sierra Chart
  on more than 2 or 3 computer systems at the same time , you will need a separate Sierra Chart account"; "An
  installation of Sierra Chart is not linked in any way to a particular computer system" [S1].
- Data restriction (stated as an exchange rule): "Due to exchange rules, market data can only be accessed on one
  computer system at a time unless you pay more in exchange fees" [S1]. Sharing data between instances through the
  DTC Server is limited to one computer: "these other installations of Sierra Chart can only be used on the same
  computer system" [S1]; "the sharing of market data across a network is prohibited" [S2].
- Multi-core use: "One reason to use additional instance of Sierra Chart is to distribute processing load among
  instances which run as independent processes which can utilize additional CPU cores" [S2].
- Within one instance: "Multiple charts can be replayed at the same time" [S4]; "All Charts in Chartbook" replays the
  charts together, synchronised on one replay clock [S4]. "Trade >> Auto Trade System Replay Back Test ... replays
  the entire chart from the beginning at a high speed. Sierra Chart will be busy during the Back Test" [S3]. Separate
  back tests are kept apart by trade account: "It is supported to run a Back Test multiple times for a trading
  system and maintain the results of each of those tests separately . This is accomplished by using a different Trade
  Account" [S3].
- Running replay back tests of different days in separate instances at the same time: not described as a procedure;
  the licence text above permits concurrent copies on one machine.
- Cloud: documentation silent (checked: S1-S4).
- Verdict: allowed by licence on one machine ("many tens of copies"), two or three machines per account; real-time
  market data is limited to one machine by exchange rules.

## 7. ATAS

Pages read (help.atas.net, live, 2026-10-04):
- [A1] Authorization Window and Platform Version Selection (modified "Mon, 24 Aug"): https://help.atas.net/en/support/solutions/articles/72000602319-authorization-window-and-platform-version-selection
- [A2] Replay Trading Simulator in ATAS (modified "Tue, 22 Sep"): https://help.atas.net/en/support/solutions/articles/72000662219-replay-trading-simulator-in-atas
- Also read: License Information tab (72000602526), Installing and running the platform (72000602246), How to launch
  ATAS on VMware for Mac users (72000639842). Help-site searches for "computers", "license", "several computers",
  "two computers", "multiple instances", "replay", "optimization", "strategy tester", "backtest".

Findings:
- Licence: "You can use the same ATAS account on multiple computers, but only one active session is allowed at a
  time" [A1].
- Replay: "Replay can play a recording in several such windows at the same time"; with several windows "Replay will
  play the recording in them synchronously using the same clock" [A2]. "To trade live, exit Replay or open a second
  copy of ATAS" [A2]. "A broker connection is not required for Replay" [A2].
- Several independent replays (different days) at once: documentation silent (checked: A1, A2 and the pages above).
- Verdict: one active session per account; within it, one replay clock. Parallel independent replays: not documented.

---

## 8. Open source

### hftbacktest (MIT; key `hftbacktest`)
- [H1] `hftbacktest/examples/5_backtest.py` (date_from = 20240501, date_to = 20240531): "Sets the number of
  processors for parallel processing during backtesting. The backtesting program itself doesn't use
  multiprocessing, but it runs multiple backtests for each pair in parallel to speed up the process." It launches
  the compiled Rust backtester as a subprocess per symbol with `multiprocessing.Pool(num_processors)`.
- [H2] `hftbacktest/examples/6_gridsearch.py`: the same comment; runs a `ParameterGrid` over `symbol`,
  `rel_half_spread`, `grid_num` with `multiprocessing.Pool`.
- [H3] `examples/Making Multiple Markets.ipynb`: "By utilizing multiprocessing, backtesting of multiple assets can be
  conducted simultaneously" (`with Pool(16) as p`). Also `Pool(16)` in `High-Frequency Grid Trading.ipynb` and
  `Probability Queue Models.ipynb` (one pool run per queue model).
- [H4] Within a run: `parallel_load` "Sets whether to load the next data in parallel with backtesting"
  (`py-hftbacktest/src/lib.rs`).
- Parallelism in the examples is per symbol or per parameter set, each process covering a date range; one process per
  day is not shown but nothing in the code or MIT licence prevents it.
- Verdict: VERIFIED. Parallel independent backtest processes on the user's machine are the documented usage pattern.

### NautilusTrader (LGPL-3.0; key `nautilustrader`)
- [NT1] `docs/concepts/architecture.md`, "One node per process": "Running multiple `LiveNode` or `BacktestNode`
  instances **concurrently** in the same process is not supported because their runtime state is not isolated";
  "For parallel execution or workload isolation, run each node in its own separate process."
- [NT2] `docs/concepts/python.md`: "Dispose one node before starting the next, or use separate processes for parallel
  execution."
- [NT3] `docs/concepts/backtesting/apis-and-runs.md`: "`BacktestNode` accepts one `BacktestRunConfig`. To run
  independent configurations, create and dispose one node at a time" (example loops over configs sequentially).
- Verdict: VERIFIED. Parallel runs are supported as separate processes, not inside one process.

### lobsim, kpetridis24 (Apache-2.0; key `lobsim`)
- README: "fast, deterministic L3 limit order book replay + paper execution simulator". grep for
  `parallel|multiprocess|thread|concurren` in README, Python and C++ sources finds only benchmark threads
  (`benchmark/lobsim_udp_main.cpp`) and `Threads::Threads` in CMake.
- Verdict: running several replays in parallel: documentation silent; no licence restriction (Apache-2.0).

### ABIDES (abides-sim/abides, BSD-3-Clause per LICENSE.txt; keys `byrd2020abides`, `abides_repo`)
- [AB1] `config/parallel.py`: "Main config to run multiple ABIDES simulations in parallel"; arguments
  `--num_simulations` and `--num_parallel` ("Number of simulations to run in parallel"); each run is
  `python -u abides.py -c {config} ... -s {seed}` in a `multiprocessing.Pool`.
- [AB2] `scripts/parallel.sh`: "Example script to run multiple ABIDES simulations in parallel" (10 simulations, 5 in
  parallel).
- [AB3] `scripts/marketreplay.sh`: loops over a list of dates and starts one background process per date:
  `nohup python -u abides.py -c marketreplay -t $ticker -d $d -s $seed -l marketreplay_${ticker}_${d} &` (a
  commented-out list holds 20 IBM dates, June 2019; the active list is one MSFT date).
- [AB4] `Kernel.py`: "Instead we have been running multiple simulations with coarse parallelization from a shell
  script." The same comment is in `abides-jpmc-public/abides-core/abides_core/kernel.py` (BSD-3-Clause, J.P. Morgan
  Chase).
- Verdict: VERIFIED. One process per seed or per replay date, run in parallel by script, on the user's machine.

### JAX-LOB (key `frey2023`; repo has no LICENSE file)
- [J1] arXiv 2308.13289 abstract: "We showcase the first GPU-enabled LOB simulator designed to process thousands of
  books in parallel, with a notably reduced per-message processing time."
- [J2] §4.3: "to achieve parallelism we process multiple books in parallel using the vmap operator"; Table 4 uses
  "N_books = 1000 identical order books in parallel".
- [J3] Rollout cost section: "we run different numbers of environments in parallel on an Nvidia 2080 Ti GPU"; "a
  factor 5 over our CPU implementation for 1000 parallel environments. Increasing the number of environments
  further is a feasible possibility ... but such economies of scale are eventually limited due to memory."
- Code: `jax.vmap(env.reset ...)` over `config["NUM_ENVS"]` (`gymnax_exchange/jaxrl/onlySteps_noRL.py`).
- Wording check: the paper says "thousands of books in parallel" and measures up to "1000 parallel environments";
  it does not say "thousands of parallel environments".
- Verdict: VERIFIED. Parallelism is inside one process on one GPU (vmap), not separate processes per day.

---

# Summary table

| Tool | Parallel runs on own hardware/cloud | Restriction | Evidence/URL | VERIFIED/UNVERIFIED |
|---|---|---|---|---|
| Kaspar (user description) | yes: one process per day, many processes per machine | none stated | user description | not verified here |
| TT Backtesting (ADL) | no: runs on TT's backend; no self-hosted option documented | architecture (TT-hosted, sim environment only, charged by run time); ≤10 algo instances and ≤1 trading day per backtest; TT Pro + Backtesting package + direct billing; concurrent-backtest cap: silent | T2, T4, T6, T7, T8, T9 (library.tradingtechnologies.com, docs 6214, 6217, 7944, 6216, 7576, 6219) | VERIFIED (hosting, 10 instances, 1 day); concurrency cap UNVERIFIED (silent) |
| NinjaTrader 8 Playback (Market Replay) | not documented: one Playback connection, sequential "DVD player" replay | concurrent sessions and platform licence: silent | N1, N5 (helpGuides/nt8/playback.htm, playback_connecting_connection.htm, Wayback) | VERIFIED (sequential single replay); parallel UNVERIFIED (silent) |
| NinjaTrader 8 Strategy Analyzer (bar/tick) | yes: optimisation "will efficiently use all CPU cores"; cloud/virtual server mentioned | advises against separate side-by-side tests on one PC; not the level II replay | N2 (helpGuides/nt8/optimize_a_strategy.htm, Wayback 20260421070432) | VERIFIED |
| Deltix QuantOffice | cloud product: "Parallel experiments runner" (archived); desktop Studio: Optimizer, Multi-Strategy Runner; self-hosted parallel runs: silent | licence terms: silent (KB login-only) | D1, D2, D4 (deltixlab.com/quantoffice; deltix.io/products/quantoffice.html, Wayback 20250122095335) | VERIFIED (vendor claim); licence UNVERIFIED |
| Bookmap Replay | restricted | "can only run on one at a time" across computers; no parallel copies on one computer; Global+ adds Replay on a secondary machine | B1, B2 (KB-IntroductionToBookmap-SystemRequirements; KB-Errors-General-Errors-And-Crashes) | VERIFIED |
| MultiCharts | not documented | licence online on one PC at a time; second PC offline for backtesting on local data (2017 page); multi-core/multi-instance: silent | M1 (wiki, Wayback 20181008214116); M2-M6 silent | VERIFIED (licence, 2017); parallel UNVERIFIED |
| Sierra Chart replay / back test | yes by licence: "many tens of copies ... on the same system running at the same time with a single license"; instances use more CPU cores | 2 (up to 3) computers per account; real-time market data on one computer unless extra exchange fees; cloud: silent | S1, S2 (PurchaseInformation.php; NewInstance.php) | VERIFIED |
| ATAS Replay | not documented | "only one active session is allowed at a time" per account; one replay clock across windows | A1, A2 (help.atas.net articles 72000602319, 72000662219) | VERIFIED (session limit); parallel UNVERIFIED (silent) |
| hftbacktest | yes: examples run backtests in parallel with `multiprocessing.Pool` (per symbol, per parameter set) | none (MIT) | H1-H3 (github.com/nkaz001/hftbacktest: hftbacktest/examples/5_backtest.py, 6_gridsearch.py; examples/Making Multiple Markets.ipynb) | VERIFIED |
| NautilusTrader | yes: "run each node in its own separate process" | not concurrently in one process; LGPL-3.0 | NT1-NT3 (docs/concepts/architecture.md, python.md, backtesting/apis-and-runs.md) | VERIFIED |
| lobsim (kpetridis24) | documentation silent | none (Apache-2.0) | README, source grep | VERIFIED absence of docs; no restriction found |
| ABIDES | yes: `config/parallel.py`, `scripts/parallel.sh` (seeds); `scripts/marketreplay.sh` (one process per date) | none (BSD-3-Clause) | AB1-AB4 (github.com/abides-sim/abides) | VERIFIED |
| JAX-LOB | yes, on one GPU: "thousands of books in parallel"; "1000 parallel environments" | GPU memory ("limited due to memory"); no LICENSE file in repo | J1-J3 (arXiv 2308.13289) | VERIFIED |

---

# Summary

- Documented to allow many replay runs at once on the user's own hardware: Sierra Chart (licence: "many tens of
  copies" on one system; two or three systems per account), hftbacktest (parallel `multiprocessing.Pool` examples),
  NautilusTrader (one node per process; parallel runs as separate processes), ABIDES (parallel script by seed; one
  background process per replay date), JAX-LOB (vmap over up to 1000 environments, "thousands of books", on one GPU).
  NinjaTrader documents multi-core optimisation, including on a cloud server, but only for the bar/tick Strategy
  Analyzer, not for level II Playback.
- Restricted: TT Backtesting runs only on TT's backend, metered by run time, with at most ten parameter instances and
  one trading day per backtest. Bookmap runs on one computer at a time and refuses parallel copies on one computer
  (Global+ adds Replay on one secondary machine). ATAS allows one active session per account. MultiCharts' 2017 page
  allows one online PC and one offline backtesting PC.
- Silent: NinjaTrader Playback concurrency and platform licence terms; Deltix licence terms and self-hosted
  parallelism (KB login-only; the cloud product advertises a "Parallel experiments runner"); MultiCharts multi-core
  or multi-instance runs; ATAS concurrent replays; TT's cap on concurrent backtests; lobsim (no docs, Apache-2.0).
- New BibTeX entries: `bib/refs_parallel.bib`. Reused keys: `tt_backtesting`, `ninjatrader_playback`,
  `deltix_quantoffice`, `bookmap_replay`, `multicharts_sim`, `atas_replay`, `sierrachart_replay`, `hftbacktest`,
  `nautilustrader`, `lobsim`, `byrd2020abides`, `frey2023`.
