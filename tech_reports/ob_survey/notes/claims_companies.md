# Claims & sources: nine companies the author asked about

Compiled 2026-10-04. All pages below were fetched on 2026-10-04 unless a different date is given.
BibTeX: `bib/refs_companies.bib`. Reused existing key: `corsair2025` (no new entry for it).
None of the nine companies appears anywhere in `sections/*.tex`, `refs.bib` or `bib/*.bib` before this pass.

## Method and limits (read first)

- WebSearch was unavailable (session budget exhausted). Sources were found from the URLs the author gave,
  links on those pages, the Internet Archive (Wayback CDX), and direct fetches. "Not found" below means
  "not found on the pages read", not "does not exist".
- Pages were read either as raw HTML converted to text (curl) or through WebFetch, which returns a
  model-generated summary; quotes obtained through WebFetch are marked **[WF]**. For [WF] quotes I asked for
  verbatim text, but they are one step removed from the source and should be re-checked before being
  quoted in the paper. Quotes without [WF] were read in the raw page text.
- bmlltech.com returned "Access Denied" to curl, so all BMLL quotes are [WF].
- Evidence labels: **VENDOR** = the company's own claim (website, press release, product page);
  **VENDOR-PR** = press release; **CUSTOMER** = named customer statement reported by a third party;
  **PARTNER** = a partner company's own measurement (not independent); **PRESS** = trade-press article;
  **AUDIT** = audited STAC benchmark; **UNVERIFIED** = not read.

### STAC check (all nine companies)

I paged through every public listing on docs.stacresearch.com for STAC-AI (/ai, 4 pages), STAC-ML (/ml,
6 pages), STAC-A3 backtesting (/a3, 3 pages), STAC-M3 (/m3, 26 pages), STAC-T1 (/t1, 2 pages), STAC network
I/O (/nio, 10 pages), STAC-A2 (/a2, 21 pages), STAC-TS (/ts), plus /cloud, /fpga, /n2, /cloudSIG,
/datacenters, and searched the HTML for the strings Options Technology, options-it, CloudQuant, SpiderRock,
BMLL, Positron, d-Matrix/dmatrix, Corsair, Crusoe, Valour, Wallaroo. **No hit.** None of the nine has a
public STAC result on those listings.
One related item: STAC "Research Note: Inference Hardware on the Edge" (STAC-ML/STAC-AI, posted
2025-12-19, https://docs.stacresearch.com/ai-20251217) "examines the rapidly evolving ecosystem of inference
hardware for edge deployment in capital markets". The report itself is login-gated; whether it covers
Positron or d-Matrix is **UNVERIFIED**.

---

## 1. CloudQuant

**What it is now:** a data-access platform ("CloudQuant Data Liberator"), not a simulator.
**Category:** data delivery / data marketplace. **Relevance:** not relevant to §12 or §13 as it exists today;
at most a footnote as a reseller of BMLL Level-3 data.

Verified (VENDOR):
- Homepage (https://www.cloudquant.com/, "© 2026 CloudQuant, LLC"): "CloudQuant Data Liberator's zero-copy
  architecture queries data where it lives — no pipelines, no movement, no replication." "Every deployment
  ships with an MCP server and granular data controls". "Time Series Native ... Optimized queries, temporal
  joins, and point-in-time accuracy."
- Docs intro (https://knowledge.cloudquant.com/introduction, docs version 2.5): "CloudQuant provides
  institutional-grade financial data infrastructure through the CloudQuant Data Liberator platform — a simple
  point-in-time data access API for live or historical time series data." Lists "70+ connectors".
- Data catalog (https://knowledge.cloudquant.com/data-catalog/overview.md): lists "**BMLL Technologies** |
  Level 3 order book data", "**SpiderRock Options & Futures** | Options and futures market data",
  "**Exegy Hidden Order Flow** | Hidden and dark pool order flow".
- No backtesting, simulation, fill model, queue position, latency or parallel-run feature appears on the
  current homepage or docs introduction/catalog.

Historical (Internet Archive, VENDOR):
- Snapshot 2021-09-25 of https://www.cloudquant.com/mariner/
  (http://web.archive.org/web/20210925233945/https://www.cloudquant.com/mariner/), title "Free Stock Market
  Backtester | Mariner - CloudQuant": "TRADING SIMULATION AND BACK TESTING ... CloudQuant Mariner, our free and
  secure back testing and algorithm development environment allows you to develop a trading strategy and then
  test it in a highly accurate stock market simulator." Data: "Market Data (Ticks, Bars, Imbalances, NBBO,
  Time and Sales)"; "Trading simulation with Tick Level market data"; "microsecond timestamped simulated
  trades"; "Freemium limited to 5,000 days of backtesting per month"; Premium: "Unlimited simulations",
  "Prioritized Simulations".
- Blog post "Backtesting Trading Strategies" dated January 12, 2018 (archived 2021-09-26,
  http://web.archive.org/web/20210926015603/https://www.cloudquant.com/backtesting/): "A simulated matching
  engine must accurately estimate how inbound orders will be processed against what has happened
  historically." It describes the crowd model: "ask CloudQuant for a capital allocation and license your
  strategy".
- What Mariner did **not** state on the pages read: no depth-of-book (only NBBO / ticks), no queue position,
  no latency model, no fill-model description. Its fill logic is **UNVERIFIED**. Whether Mariner still exists
  is unknown; the current site does not mention it.

Published figures: none (no latency/throughput numbers on any page read).

---

## 2. SpiderRock

**What it is:** a cloud-hosted options/equities/futures trading, risk and analytics platform, a FINRA
broker-dealer (SpiderRock EXS), an options ATS with electronic auctions, and a market-data/analytics vendor.
**Category:** execution (algorithmic routing, broker-dealer, ATS) and market data.
**Relevance:** marginal. No simulator, no order-book model, no hardware product found. Possible one-line
mention only if §12 discusses execution venues or US options data; otherwise not relevant.

Verified (VENDOR), pages https://spiderrock.net/, /platform/, /data/, /exs/, /exs/ats/:
- "SpiderRock has developed a large scale, high message rate core services trading and risk platform that is
  currently in its seventh generation. This platform operates at market scale and integrates all clients and
  all markets into a single high-performance system comprised of dozens of machines, thousands of active
  cores, and very low latency data transport." (/platform/)
- Routing: "our machine learning framework enables our system to continually monitor and take advantage of
  market micro-dynamic shifts, allowing for precise order routing based on a user-defined level of
  aggression." (/platform/)
- Auctions: "Clients can choose to run Flash auctions (<100 ms) or Block auctions for a longer time
  duration" (/platform/); ATS page: Block auction duration "15 – 300 seconds", Flash "Immediate auction".
- Data: "SpiderRock delivers raw and normalized US-based equity, options, and futures data feeds via
  multicast channels." Analytics feed includes "normalized OPRA and CME, NBBO implied volatility quotes,
  theoretical surface prices, Greeks, and trade prints." (/data/)
- EXS: "EXS can facilitate execution of all U.S. listed equities, equity options, and futures via a low
  latency infrastructure directly connected to exchanges or other providers' DMA systems." "fully compliant
  15c3-5 risk layer". (/exs/)
- "SRSE is comprised of 8 MySQL live databases" (/platform/).

Published figures: only the "<100 ms" Flash-auction duration and 15–300 s Block-auction duration (auction
design parameters, not system latency). No latency measurements, no simulation or backtesting product, no
STAC result.

---

## 3. BMLL Technologies

**What it is:** vendor of harmonised historical Level 1/2/3 order-book data and pre-computed analytics, with
a hosted research environment (Data Lab, on Spark/EMR) and a no-code app (Vantage).
**Category:** order-book data (historical L3) + research compute.
**Relevance:** relevant to the survey as an **order-book data source** — Part I/§12 where data sources are
discussed (the survey already cites LOBSTER, glossary.tex:105; p3_sim_hw.tex:479 says simulators "reach CME
data only through a data vendor's files"). Not a simulator: no page read claims a matching engine or fill
simulator.

Verified (VENDOR, all [WF]; bmlltech.com blocks curl):
- Homepage (https://www.bmlltech.com/): "BMLL delivers nanosecond-precision historical Level 3 order book data
  and analytics"; "Level 1, 2 & 3 Data 140+ Venues AI-Ready Pre-Engineered Analytics"; "the BMLL Level 3, 2, 1
  Data is harmonised into a consistent, lossless global format"; "reconstruct the exact state of the market
  around every trade"; a use-case tile "Queue Position & Fill Analysis"; tiles "Strategy Backtesting" and
  "Simulation & Market Replication".
- Quant research page (https://www.bmlltech.com/industry/quant-research-backtesting): "Simulate systematic
  strategies using high-fidelity historical order book data to validate model robustness." "Granular queue
  dynamics, cancel/replace message tracking, and hidden liquidity indicators." "Nanosecond-precision
  timestamps for accurate sequencing and latency modelling." Execution Modelling: "Quantify slippage, market
  impact, and queue dynamics." "scales across CPU and GPU". WebFetch reported the page does **not** claim a
  matching engine or fill simulator.
- Data Lab (https://www.bmlltech.com/products/bmll-data-lab): "elastic MapReduce (EMR)" to "run distributed
  data processing frameworks like Apache Spark"; instances "up to 1.5TB" (enterprise); "run back-tests on
  petabyte-scale datasets in a fully managed service".
- Coverage (https://www.bmlltech.com/market-data-coverage): page says "120+ venues" (homepage says
  "140+"); asset classes Equities & ETFs, Futures, Options, Prediction Markets. Futures venues visible to
  WebFetch: ASX 24 (L1/L2/L3 from Mar-2018), B3 Futures (L1/L2/L3 from Aug-2023). **Whether CME is covered:
  not found / UNVERIFIED.**
- Execution analytics page (https://www.bmlltech.com/industry/execution-analytics-at-scale): no queue, fill
  or simulation text found [WF].

Interpretation (mine, not BMLL's): "Queue Position & Fill Analysis" and "queue dynamics" here are analytics
on reconstructed L3 data (where an order sat, what filled), not a simulator that inserts hypothetical orders.
No page read describes how hypothetical-order queue position or fills are modelled.

Published figures: venue counts (120+/140+, inconsistent across pages), "nanosecond" timestamps,
"petabytes". No latency/throughput benchmarks. No STAC result.

---

## 4. Options Technology (Options-IT)

**What it is:** managed infrastructure provider for financial firms: colocation/hosting, global network
(AtlasFabric), normalized market-data feed (AtlasFeed), packet capture and latency monitoring (AtlasInsight,
"Powered by Packets2Disk"), and a GPU-based private AI platform (PrivateMind).
**Category:** trading infrastructure / colocation / networks / market data.
**Relevance:** marginal for §13 "Networks and distance" and latency measurement. The figures are unaudited
vendor claims; no STAC result found. No FPGA or hardware-acceleration product for inference found.

Verified (VENDOR):
- Homepage (https://www.options-it.com/): "The premier managed infrastructure platform for the global
  financial sector"; "Proprietary normalized data streams, third-party vendor concierge setup, and
  ultra-low-latency infrastructure and exchange colocation."
- AtlasFeed (https://www.options-it.com/products/atlas/atlasfeed/): "AtlasFeed represents the pinnacle of
  speed and efficiency, offering a consolidated feed with up to single-digit microsecond latency." "Whether
  you're running ultra-low latency applications that leverage multicast delivery and the latest network adapter
  kernel bypass capabilities, or lightweight applications ... over TCP". "All normalized data streams are
  captured tick-by-tick, unconflated through TickLogger".
- AtlasFabric (/products/atlas/atlasfabric/): "Designed with a high-capacity, 100Gb backbone"; "Ultra-low-latency
  Layer 1 connectivity for real-time and historical market data".
- AtlasInsight (/products/atlas/atlasinsight-powered-by-packets2disk/): "Sustained 200 Gbps lossless capture
  with nanosecond timestamp accuracy"; "Analyze tick-to-trade, trade-to-tick, and order-entry latency across
  venues and feeds"; "Drill down to the sub-microsecond level"; "Free from proprietary hardware".
- PrivateMind (/products/privatemind/): "Built on NVIDIA GPU infrastructure and confidential compute,
  PrivateMind supports advanced AI workflows including retrieval-augmented generation, fine-tuning, and
  inference"; "Deployed in strategic hubs such as Iceland".

Published figures and conditions:
- "up to single-digit microsecond latency" (AtlasFeed). Conditions not stated: no definition of the measured
  interval (exchange packet to client API?), no hardware, no percentile, no message rate. VENDOR.
- 200 Gbps sustained lossless capture, nanosecond timestamp accuracy (AtlasInsight). VENDOR; no test
  conditions given.
- 100Gb backbone (AtlasFabric). VENDOR.

---

## 5. Positron AI

**What it is:** maker of Transformer-inference hardware. Shipping product **Atlas** is an inference server
with 8 "Archer" accelerator cards that are **FPGA-based** (Altera Agilex-7M, per EE Times). Next-generation
**Asimov** is custom silicon (ASIC), "Coming in 2027"; **Titan** is a 4- or 8-Asimov system.
**Category:** inference hardware (LLM/Transformer, data-centre).
**Relevance:** relevant to §13 "Accelerators between FPGAs and GPUs" — it is an FPGA-based Transformer
inference product with a named HFT firm (Jump Trading) as investor and tester. But all public numbers are
LLM tokens/s, not single-event order-book inference; no batch-1 microsecond latency is published.

Verified (VENDOR), https://www.positron.ai/, /atlas, /asimov, /titan, /vision, /press:
- Atlas: "Transformer Inference Server"; "8x Positron Archer Transformer Accelerators", "32 GB HBM Each",
  "256 GB" total, "Dual AMD EPYC Genoa 9374F", "Shipping Today". Headline claims ">4x Performance per Watt vs
  GPUs", ">3x Performance per Dollar vs NVIDIA Hopper", "3x Lower Latency in Production Workloads".
- Atlas head-to-head, stated condition "(Llama 3.1 8B with BF16 compute, no speculation or paged attention)":
  NVIDIA DGX H200 5900 W, 182 tokens/sec/user; Positron Atlas 2000 W, 280 tokens/sec/user; Perf/Dollar 3.08x,
  Perf/Watt 4.54x. Batch size / concurrency not stated.
- Asimov: "Custom AI Accelerator Silicon", "Coming in 2027", "288GB to 2.3TB Memory per Chip", "Realizable
  Memory Bandwidth 2.76 TB/s", "Clock Frequency 2.0 GHz", "TDP ~400W", "LPDDR5x over High Bandwidth Memory",
  "a 512×128 systolic array running at 2 GHz", "keep the common path in dedicated hardware for deterministic
  latency". Homepage chart footnote: "Asimov performance is based on cycle-accurate simulations." (i.e. all
  Asimov numbers are simulated, not measured).
- Vision page: "8 months from company founding to FPGA prototype; 7 months from prototype to first shipped
  product".
- Press page lists "Positron Announces Partnership with Altera for Silicon Powering Faster LLM Inference"
  (YouTube) and the EE Times headline "Positron's $230M Funding Led By Financial Trading Firms".

Third-party (PRESS + CUSTOMER), EE Times, Sally Ward-Foxton, 2026-02-04,
https://www.eetimes.com/positron-230-million-funding-led-by-financial-trading-firms/ [WF]:
- "The company's first-generation product, Atlas, uses FPGAs—specifically, the Agilex-7M with HBM and DDR5."
- "Positron's Atlas FPGA cards need 150-200 W each."
- Series B led by "Arena Private Wealth and Jump Trading, both Chicago-based financial trading firms".
- Jump Trading CTO Alex Davies: "For the workloads we care about, the bottlenecks are increasingly memory and
  power—not theoretical compute ... In our testing, Positron Atlas delivered roughly 3× lower end-to-end
  latency than a comparable H100-based system on the inference workloads we evaluated, in an air-cooled,
  production-ready footprint with a supply chain we can plan around." Workloads, model, batch size and
  latency values are **not disclosed**. This is an investor/customer statement, not an independent benchmark.
- "Asimov is due to tape out towards the end of the third quarter, with samples coming at the end of the first
  quarter of 2027."

Not read / UNVERIFIED: BusinessWire Series B release (HTTP 403), Bloomberg, WSJ, Reuters articles.
Batch-1 latency in microseconds/milliseconds: **not published** on any page read. No STAC result.

---

## 6. d-Matrix (Corsair)

**What it is:** maker of Corsair, a PCIe inference accelerator using SRAM-based digital in-memory compute
chiplets (TSMC N6), plus JetStream (accelerator-to-accelerator NIC), Aviator (SDK), SquadRack (rack system).
Acquired Wallaroo.ai (2026-08-03). **Category:** inference hardware (LLM, data-centre) + serving software.
**Relevance:** already cited in §13 "Accelerators between FPGAs and GPUs" as `corsair2025` (IEEE Micro 45(5),
2025). p3_sim_hw.tex:1168 already states "it is not a trading product". Nothing found that changes that: no
finance or trading use case, no STAC result. Optional additions: production status and a partner-measured
latency figure.

Verified (VENDOR):
- Homepage (https://www.d-matrix.ai/): "d-Matrix brings compute directly into memory itself"; Corsair has "an
  industry-standard PCIe form factor".
- Product page (/product/): "Corsair's chiplet-based design allows SRAM to scale beyond traditional limits";
  "models up to 100B parameters".
- JetStream (/jetstream-accelerator/): "Accelerator-to-accelerator connectivity with bandwidth of up to 400 GB/s."
- Press release 2025-11-12 (/announcements/d-matrix-raises-275-million-to-power-the-age-of-ai-inference/):
  "deliver 10× faster performance, 3× lower cost, and 3–5× better energy efficiency than GPU-based systems.
  Solutions powered by d-Matrix's software can produce up to 30K tokens per second at 2ms per token on a
  Llama 70B model." Conditions (number of cards, batch, precision, context length) **not stated**.
- Press release 2026-06-09 (/announcements/d-matrix-corsair-ai-inference-platform-enters-full-production-to-meet-customer-demand/):
  "Manufactured in partnership with TSMC and Alchip Technologies on TSMC's established N6 process node";
  "SRAM-based in-memory compute chiplet architecture — built on organic substrates, rather than HBM-based
  CoWoS packaging"; "Combined with LP-DDR5 memory"; "GPUs dominate the compute-intensive prefill portion of the
  workload, while Corsair excels at the decode phase."
- Demo Cloud blog 2026-09-15 (/introducing-d-matrix-demo-cloud-...): example deployment "SpecDec inference mode
  with a Qwen3 1.7B draft model on 2 Corsair cards and a Qwen3 235B target model on 4 H200 GPUs".
- Wallaroo acquisition release 2026-08-03 (/announcements/d-matrix-acquires-wallaroo/): see §9.

Partner measurement (PARTNER, not independent: Gimlet Labs is a d-Matrix partner, d-Matrix links it from its
own release), Gimlet Labs blog 2026-03-11, https://gimletlabs.ai/blog/low-latency-spec-decode-corsair:
- "A single Corsair card contains a whopping 2GB of on-chip SRAM and delivers up to 150 TB/s memory bandwidth".
- Setup: speculative decoding with "a 1.6B parameter draft model" on Corsair (2 cards), prefill/verify on an
  unnamed GPU ("the exact SKU omitted"), "input sequence length of 8K and an output sequence length of 1K
  tokens", coding workload with high acceptance rate. "Note that we used a combination of measured and modeled
  data to produce these results."
- Result: "2-10X interactivity improvements over a homogeneous speculative decoding setup for the same energy
  efficiency". d-Matrix's 2026-06-09 release summarises this as "a baseline 24-second response time was
  reduced to less than two seconds" and calls it "Independent testing"; the Gimlet post itself says
  partly modeled.

Finance/low-latency trading use: **none found** on any page read. No STAC result.

---

## 7. Crusoe

**What it is:** AI cloud and data-centre company (Crusoe Cloud): rents NVIDIA (H100, H200, B200, GB200 NVL72)
and AMD (MI300X, MI355X) GPUs; Managed Inference service; builds large data-centre campuses.
**Category:** inference serving / GPU cloud. **Relevance:** not relevant beyond generic "GPU cloud for
training and batch simulation", which the survey does not need a vendor for. No trading-specific content,
no STAC result.

Verified (VENDOR), https://www.crusoe.ai/:
- GPU list as above (homepage menu text). "Features high-performance NVIDIA & AMD compute, accelerated
  storage, and optimized RDMA networking to deploy models up to 20x faster and cut costs by up to 81%."
  (no conditions stated).
- Managed Inference release, 2025-11-20
  (https://www.crusoe.ai/resources/newsroom/crusoe-launches-managed-inference-delivering-breakthrough-speed-for-production-ai):
  "Achieve up to 9.9x faster TTFT* ... MemoryAlloy, a cluster-wide KV cache"; "Process up to 5x tokens per
  second* for workloads with frequent prefix re-use"; "*Compared to vLLM for Llama 3.3 70B model".
- Release 2025-03-28: Crusoe says it was rated "Gold" in SemiAnalysis's GPU Cloud ClusterMAX rating. The
  SemiAnalysis article itself (https://semianalysis.com/2025/03/26/the-gpu-cloud-clustermax-rating-system-how-to-rent-gpus/)
  was **not read — UNVERIFIED**.

---

## 8. Valour (valour.ai)

**What it is:** "ValourAI", a software/AI consultancy ("ValourAI designs and develops scalable software, AI,
automation, data and cloud solutions") that also sells a retail "Valour AI Trading Suite": "Automated trading
robots for stocks, ETFs, forex and crypto", "Crypto agents for Binance, Bybit and Coinbase", "Copy Trading &
Signals", "Trading Academy". Fintech services list "Trading systems", "Risk-management software".
**Category:** IT consultancy + retail trading bots. **Relevance: not relevant.** No order-book model,
simulator, research, hardware, or published figures. The homepage widgets "Model Accuracy 97.4%" and
"Pipeline Uptime 99.99%" are decorative dashboard graphics with no stated model, task or data.
Source: https://valour.ai/ (VENDOR).

Only valour.ai (the URL given) was checked; other companies with similar names were not.

---

## 9. Wallaroo.AI

**What it is:** ML/LLM inference deployment, serving and observability platform; **now part of d-Matrix**.
Homepage (https://wallaroo.ai/, "© 2026 Wallaroo.AI"): "Wallaroo has joined d-Matrix. Wallaroo.AI is now part of
d-Matrix — the inference compute leader for ultra low latency AI for data centers."
d-Matrix release, 2026-08-03 (https://www.d-matrix.ai/announcements/d-matrix-acquires-wallaroo/): "d-Matrix ...
today announced the acquisition of Wallaroo.ai, a leader in AI inference deployment and orchestration
software."
**Category:** inference serving software. **Relevance:** not relevant to §12/§13 (no trading-specific serving,
no latency numbers).

Verified (VENDOR):
- Platform page (https://wallaroo.ai/platform/): "Ultra-Low Latency & High Throughput: Delivering
  ultra-high-speed performance inference latency, & throughput" — qualitative only, no figure.
- Docs (https://docs.wallaroo.ai/, "Version 2026.1"): runtimes include ONNX, TensorFlow, vLLM, SGLang; tutorial
  list includes "ARM Financial Services Cybersecurity" (the only finance item seen; not read).
- Case-studies page returned only navigation text after the acquisition; any finance case studies:
  **not found**.

Published figures: none found.

---

## Summary table

| Company | Category | Relevant section | Key verified fact | Evidence type |
|---|---|---|---|---|
| CloudQuant | Data-access platform (formerly also Mariner backtester) | Not relevant (today); at most footnote | Current product is Data Liberator, a "point-in-time data access API"; catalog resells BMLL L3. Mariner (2021 archive) was tick/NBBO equity backtester, no depth/queue stated | VENDOR; archived VENDOR |
| SpiderRock | Options/equity/futures execution platform, broker-dealer, ATS, market data | Not relevant (no simulator/model/hardware) | "seventh generation" platform of "dozens of machines, thousands of active cores"; Flash auctions "<100 ms" | VENDOR |
| BMLL | Historical L1/L2/L3 order-book data + Spark research environment | Order-book data sources (Part I data discussion / §12 data inputs) | "nanosecond-precision historical Level 3 order book data", 120+/140+ venues; "Queue Position & Fill Analysis" as analytics; no fill simulator claimed; CME coverage not found | VENDOR [WF] |
| Options Technology | Colocation, managed infrastructure, network, normalized feed, packet capture | §13 Networks and distance (marginal) | AtlasFeed "up to single-digit microsecond latency" (no conditions); AtlasInsight "200 Gbps lossless capture with nanosecond timestamp accuracy" | VENDOR |
| Positron AI | Transformer inference hardware: FPGA-based Atlas (shipping), Asimov ASIC (2027) | §13 Accelerators between FPGAs and GPUs | Atlas uses Altera Agilex-7M FPGAs (EE Times); Llama 3.1 8B BF16: 280 vs 182 tokens/s/user vs DGX H200; Jump Trading co-led Series B and reports "roughly 3× lower end-to-end latency" vs H100 on undisclosed workloads | VENDOR; PRESS; CUSTOMER |
| d-Matrix | In-memory-compute inference accelerator (Corsair) | §13 Accelerators between FPGAs and GPUs (already cited, corsair2025) | TSMC N6, SRAM chiplets, in full production (June 2026); "30K tokens per second at 2ms per token on a Llama 70B" (no conditions); 2 GB SRAM/card, 150 TB/s (Gimlet) | VENDOR-PR; PARTNER (partly modeled); peer-reviewed paper already in survey |
| Crusoe | GPU cloud + managed LLM inference | Not relevant | NVIDIA H100–GB200 and AMD MI300X/MI355X; "9.9x faster TTFT" vs vLLM on Llama 3.3 70B | VENDOR |
| Valour (valour.ai) | IT consultancy + retail trading bots | Not relevant | Sells "Automated trading robots for stocks, ETFs, forex and crypto" | VENDOR |
| Wallaroo.AI | ML inference serving software; acquired by d-Matrix 2026-08-03 | Not relevant | "Wallaroo.AI is now part of d-Matrix"; no latency figures published | VENDOR; VENDOR-PR |

None of the nine has a public STAC benchmark result (all public STAC listings checked, see top).

## Recommendation

Add:
1. **Positron AI → §13 "Accelerators between FPGAs and GPUs"** (p3_sim_hw.tex ~l.1161-1171). It is the
   clearest fit: a shipping FPGA-based (Agilex-7M) Transformer inference server, with a trading firm (Jump)
   as lead investor reporting ~3x lower end-to-end latency than H100 on undisclosed workloads. State plainly
   that the published numbers are LLM tokens/s (Llama 3.1 8B, BF16) from the vendor, the Jump figure is an
   investor statement with no disclosed workload, and the Asimov ASIC numbers are from cycle-accurate
   simulation. Cite `positron_atlas`, `eetimes2026positron`.
2. **BMLL → wherever the survey lists order-book data sources** (next to LOBSTER; e.g. p1_problem.tex Level-3
   definition or p3_sim_hw.tex:479 "data vendor's files"). A commercial cross-venue L3 historical source,
   vendor-described. Do not describe it as a simulator: it provides data and analytics, and claims "queue
   dynamics" analytics, not a matching engine. Cite `bmll_home`, `bmll_quant`.
3. **d-Matrix**: already cited (`corsair2025`). Optional one sentence: Corsair entered full production on
   TSMC N6 in June 2026 (`dmatrix2026production`) — only if the paper wants product status. The vendor's
   "2 ms per token" and Gimlet's partly-modeled speed-ups are LLM-serving figures and do not bear on
   order-book inference.

Optional / marginal:
4. **Options Technology → §13 "Networks and distance"** only if the paper wants an example of a managed
   colocation/normalized-feed provider; its "single-digit microsecond" feed latency has no stated conditions,
   so it should not be quoted as a measurement. Cite `optionsit_atlasfeed`.

Do not add: CloudQuant (now a data-access layer; its old Mariner backtester was NBBO/tick-level with no
stated queue model and is no longer advertised), SpiderRock (execution/analytics, no simulator or model),
Crusoe (generic GPU cloud), Valour (retail bots/consultancy), Wallaroo (generic serving; now part of d-Matrix).
