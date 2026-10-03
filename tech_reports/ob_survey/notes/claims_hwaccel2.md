# Claims & sources: hardware acceleration, second pass (vendors, FPGA papers, in-network, STAC-ML, accelerators, CPU)

Compiled 2026-10-04. Extends `claims_hwaccel.md`; does not repeat items already in `p3_sim_hw.tex`
(AMD UL3524/UL3422, Exegy/Enyx STAC-T0 AMD240422, Algo-Logic, Magmio, Xelera Silva, Napatech+Xelera,
LightTrader, Corsair, FAMOUS, ELiTeFormer, hls4ml, popov2026ssm, JAX-LOB, NVIDIA Numba MC, Graphcore IPU training,
DPDK/OpenOnload/VMA, leber2011, lockwood2012, khoda2023rnn, ESE, FlashAttention, roofline, Pope et al., CUDA graphs).

BibTeX: `bib/refs_hwaccel2.bib` (112 new entries).
**Reused existing keys:** leber2011, lockwood2012, hong2026lobin, tran2019tabl, denholm2014arb, exegy_stac2024,
exegy_enyx, lighttrader, khoda2023rnn, zhang2019, popov2026ssm.

## Method and coverage limits (read first)

- The session's WebSearch budget was exhausted, so sources were found via direct URLs, the STAC listing pages
  (docs.stacresearch.com /nio, /ml, /t1, /a2), Crossref, arXiv/Semantic Scholar/OpenAlex APIs (often rate-limited),
  vendor doc sites, GitHub and the Wayback Machine. **"Not found" below means not found with these tools, not
  "does not exist".**
- **Full STAC reports are login-gated.** Every STAC number here comes from STAC's public SUT page or news post.
  Many say "<vendor> wished to highlight", so the vendor chose which audited results are shown. Full result tables
  are UNVERIFIED.
- Evidence labels: **AUDIT** = independently audited STAC benchmark; **PR-meas** = peer-reviewed, measured on hardware;
  **PR-est** = peer-reviewed, synthesis/simulation/cycle-count estimate; **PR-unclear** = peer-reviewed, text read does
  not say whether measured; **PRE** = preprint; **VENDOR** = vendor claim (datasheet, product page, blog);
  **VENDOR-REF** = vendor reference design/IP doc; **TALK** = practitioner conference talk; **JOB** = job advert.
- Raw working files (per-agent notes with longer quotes): scratchpad `hw2/A_vendors.md` ... `E_accel_cpu.md`
  (session scratch, not in repo).

---

## 1. Audited benchmarks (STAC) beyond AMD240422

### 1.1 STAC-T0 ("tick-to-trade network I/O")
STAC definition (XLX200514 news post): "'Actionable' latency is the time from the last bit of inbound data needed to
make a trading decision to the first bit of the simulated outbound order."

| SUT | Figure (quoted) | Date | Source |
|---|---|---|---|
| XLX200514: LDA MAC/PCS + LightSpeed v2 TCP on LDA SBM09P-3 (VU9P-3) | "For 68-byte frames at all ingress rates, actionable latency had a minimum of 24.2 nanoseconds ... mean of 27.9 to 29.2 nanoseconds depending on ingress rate" | 2020-05-25 | https://docs.stacresearch.com/news/XLX200514 |
| EXA181106: Exablaze ExaNIC V5P + FDK (now Cisco Nexus V5P) | "Max actionable latency was just 44 nanoseconds across all message sizes and message rates"; "Minimum actionable latency was as low as 31 nanoseconds" | 2019-10-07 | Wayback copy of stacresearch.com/news/2019/10/08/EXA181106 |
| SFC170831: Solarflare SFN8522 + LDA LightSpeed on Alpha Data KU3 (first STAC-T0) | "At 1Gbps, maximum Actionable Latency was just 98 nanoseconds for 68-byte frames and 109 nanoseconds for 507-byte frames"; "uncertainty of just +/-2 nanoseconds" | 2017-10-12 | Wayback copy of stacresearch.com/news/2017/10/13/SFC170831 |

Accelerates: network I/O only (FPGA MAC/PCS/TCP), not book building or model inference. Keys: stac_xlx200514,
stac_exa181106, stac_sfc170831.

### 1.2 STAC-T1 (full tick-to-trade, CME)
- **ADHC240918**, ADHOC Teknoloji HFFT-02A on AMD Versal Premium, "First STAC-T1 with a FPGA-based Tick-to-Trade
  Appliance". At 1x market rate, SOM-to-SOF "115.07 nanoseconds (mean) 140.04 nanoseconds (99th percentile) 163.88
  nanoseconds (MAX)"; 8x rate mean 115.35 ns, max 401.88 ns. STAC: version C "cannot be fairly compared to previous
  STAC-T1.EMINI results". AUDIT. https://docs.stacresearch.com/news/ADHC240918 (stac_adhc240918)
- **EXG191010**, Exegy Xero 1.0.3 FPGA card: SOM-to-SOF "0.552 microseconds (mean) 0.609 microseconds (99th
  percentile) 0.789 microseconds (MAX)"; "All logic to consume market data, decide whether to send an order, and send
  orders ran within the FPGA." AUDIT. https://docs.stacresearch.com/news/EXG191010 (stac_exg191010)
- Accelerates: feed decode + book/state + decision + order entry (whole path), with trivial decision logic as defined
  by the benchmark (the decision rule itself was not read; UNVERIFIED what logic is required).

### 1.3 STAC-N1 (host network stack)
- **N1-20260201**, AMD Solarflare X4542-PLUS 100GbE, Onload, Xeon Gold 6558Q: at 100k msg/s, 66-byte "mean 1.59µs,
  median 1.59µs, P99 1.70µs, max 3.79µs"; direct-attach copper, no switch; "the first 100GbE solution tested under
  STAC-N1". Posted 2026-09-26. AUDIT. https://docs.stacresearch.com/N1-20260801 (stac_n1_20260201)
- **LMS240510a/b**, Liquid-Markets-Solutions ÜberNIC CXL on BittWare IA-440i (Intel Agilex): "the first test of an
  all FPGA-based UDP stack and the first test of a stack using CXL"; public page gives rankings only. AUDIT (rank
  only); numbers UNVERIFIED. (stac_lms240510)
- **Solarflare X2522** entries AMD230414, AMD231005, AMD240924: rankings/throughput only public. (stac_amd230414 etc.)
- **MMK150531**, Metamako MetaApp 32 (now Arista 7130): "Per-hop latency ... was 0.06 microseconds"; round trip
  agg + L1 "mean incremental latency of 0.1 microseconds"; harness could only measure mean and s.d. AUDIT, 2015.
  (stac_mmk150531)

### 1.4 STAC-ML Markets (Inference) — the only audited benchmark of model inference for trading
**What it measures** (STAC public text; stac_ml_wg, stac_ml_limits2025):
- "the benchmarks test the latency, throughput, energy efficiency, space efficiency, and algorithm quality of a
  technology stack across three model sizes and different numbers of model instances."
- "The objective is to measure the upper bound of inference-only performance, isolating it from other parts of a
  typical production pipeline such as data ingestion, parsing, or feature generation."
- Suites: "**Sumaco** models a use case where inference is triggered by external events. Each full window of data is
  transmitted as a contiguous block, with no reuse of prior computations ... **Tacana** models continuous inference on a
  sliding window, such as inference on every tick or bar of market data. This suite allows re-use of computations and
  reduced data transfer overhead". Models: LSTM_A, LSTM_B, LSTM_C; headline metric 99th-percentile latency; "Error:
  deviation from a quality reference model implementation".
- A GBT suite ("El Popo", GBT_A/B/C) was added later (XLRA260312 page).
- Model sizes: only from NVIDIA's blog (VENDOR): LSTM_A "less than 200,000 parameters", LSTM_C "30 million
  parameters". LSTM_B size, layer/hidden/window dimensions and input data: UNVERIFIED (spec gated).
- Note for the survey: these are generic LSTMs on benchmark data, not DeepLOB or another published LOB model.

**Audited results (public highlights):**

| SUT | Hardware | Suite | 99p latency, 1 model instance | Source |
|---|---|---|---|---|
| NVDA221118b (2023-02) | 1x NVIDIA A100 80GB PCIe, CUDA 11.7 | Tacana, latency-opt. | LSTM_A 35.2 µs; LSTM_B 68.5 µs; LSTM_C 640 µs | stacresearch.com/news/nvda221118 (stac_ml_nvda2023) |
| NVDA221118a | same | Sumaco, throughput-opt. | LSTM_A throughput 1.629–1.707M inf/s | same |
| GROQ221014 (2022-10) | GroqNode, 8x GroqCard | Sumaco | LSTM_A 55.9–56.4 µs (NMI 1–4); LSTM_C text self-contradictory (2.27 ms vs 2.72–2.77 ms) → UNVERIFIED | stacresearch.com/news/groq221014 (stac_ml_groq2022) |
| MRTL221125 (2022-12) | Myrtle VOLLO, 4x BittWare IA-840f (Intel Agilex AGF027) | Sumaco | LSTM_A 24.0–24.1 µs; LSTM_B 64.8 µs; LSTM_C 1.35 ms | stacresearch.com/news/mrtl221125 (stac_ml_mrtl2022) |
| MRTL230426 (2023-05) | VOLLO, same 4x Agilex | Tacana | LSTM_A 5.07 µs; LSTM_B 6.89 µs; LSTM_C 31.0 µs | stacresearch.com/news/mrtl230426 (stac_ml_mrtl2023) |
| SMC250910 (2025-10) | NVIDIA GH200 Grace Hopper, Supermicro, CUDA 12.9 | Tacana-comparable | LSTM_A 4.70 µs; LSTM_B 7.10 µs; LSTM_C 15.8 µs; max/median up to 9.65 (38.3/3.97 µs, LSTM_A NMI 8) | docs.stacresearch.com/SMC250910 (stac_ml_smc2025) |
| MRTL260323 (2026-04) | VOLLO Rev C, Silicom Artena (AMD Versal VP1802) | Tacana | "First sub 2μs 99p latency for LSTM_A; First sub 3μs ... LSTM_B; First sub 8μs ... LSTM_C" (bounds only) | docs.stacresearch.com/MRTL260323 (stac_ml_mrtl2026) |
| XLRA260312 (2026-05) | Xelera Silva, AMD Alveo V80 | GBT "El Popo" | GBT_A/B ≤1.95 µs; GBT_C 2.88 µs; max ≤12.3 µs | docs.stacresearch.com/XLRA260312 (stac_ml_xlra2026) |

- GROQ231106 (1x GroqCard, Tacana) is **unaudited** (STAC Vault), no public figures.
- Not on the public STAC-ML listing: H100, Intel AMX vendor-optimised, Graphcore, Dell/HPE/Lenovo LSTM submissions.
  STAC's own naive ONNX runs on Azure/GCP CPUs exist (STAC2210xx, STAC22050x) but their figures were not read.
- Timeline of Tacana LSTM_A 99p at one instance: A100 35.2 µs (2023) → VOLLO/Agilex 5.07 µs (2023) → GH200 4.70 µs
  (2025) → VOLLO/Versal < 2 µs (2026). AUDIT throughout; inference only (excludes ingestion and features).
- NVIDIA blog (VENDOR, nvidia2026stacml, 2026-04-02): repeats GH200 figures; says the "precomputation step ... is
  excluded from timing measurements" and the timer starts when input is in host memory (NIC ingress excluded).

### 1.5 STAC-A2 (for contrast; not order book, not inference)
"Monte Carlo estimation of Heston-based Greeks for a path-dependent, multi-asset option with early exercise."
NVDA231030 (8x H100 SXM5): 561 options/s, baseline Greeks warm 7.40 ms. NVDA230721 (8x H100 PCIe): warm 8.9 ms,
cold 38 ms. AUDIT. (stac_a2_nvda2023, stac_a2_nvda2023b)

---

## 2. Commercial FPGA / NIC / layer-1 platforms (vendor claims)

| Item | Accelerates | Figure (quoted) + conditions | Source |
|---|---|---|---|
| Arista 7130 Connect S (ex-Metamako) | L1 fan-out/patch | "4 ns deterministic latency (7130-16/48G3S)", "6 ns ... (7130-96S)", "Less than 100 ps jitter"; datasheet 2024-11-22 | arista_7130connects |
| Arista 7135LB (VU9P-3) | L1 + FPGA apps | "5 ns - Layer 1 Latency at 25G"; MetaMux "Data aggregation in 39 nanoseconds"; Inline "Sub-200ns passthrough latency" | arista_7135lb |
| Arista MetaMux | order aggregation | "average of 39 ns" on 7130L; "+/- 7 ns" without contention | arista_metamux |
| Arista MultiAccess | mux + filter | avg 52.7 ns (1x47_ll, L board); filter 89 ns | arista_multiaccess |
| Arista MetaWatch | tap/timestamp | "<1 ns" timestamp precision; 5 ns pass-through | arista_metawatch |
| Cisco Nexus 3550-F (ex-Exablaze) | L1, mux, L2 app | "L1 Tap/Patch: 3ns minimum – 5ns maximum"; "FastMux: 39ns minimum – 48ns maximum"; L2 app 95–126 ns | cisco_n3550f |
| Cisco Nexus 3550-H | L1 switch | "3.2ns (min) port latency" | cisco_n3550h |
| Cisco Nexus 3550-T (VU35P + 8 GB HBM) | programmable switch | no latency figure in datasheet | cisco_n3550t |
| Cisco Nexus K35-S / K3P-S SmartNIC | host NIC I/O | raw 64 B median wire-userspace-wire 780 / 696 ns; UDP ½RTT 880 / 810 ns (Ivy Bridge 3.5 GHz, libexanic / sockperf) | cisco_k35s, cisco_k3ps |
| Cisco Nexus V5P / V9P | in-FPGA I/O | PCS/MAC "6.2ns (min)"; trigger "34ns (min)"; loopback "50ns (min)" | cisco_v5p, cisco_v9p |
| AMD Solarflare X4 / X3 / X2 | host NIC I/O | "X4 590 ns X3 796 ns X2 918 ns" on EPYC 9575F, 4-byte payload, "AMD Internal Testing as of 9/15/2025 ... Onload benchmarking tool"; metric (½RTT or not) not stated | amd_solarflarex4 |
| AMD X4 brief | NIC ASIC | "CUSTOM DESIGNED LOW-LATENCY ASIC"; "Express data path ... with CTPIO" | amd_x4brief |
| AMD Alveo X3522 / X3522PV | NIC; user FPGA (XCUX35) | no latency figure in brief or DS1002/UG1522/UG1607; Onload, TCPDirect, ef_vi; X3522PV "can be used for trading acceleration or compute offload" | amd_alveox3, amd2026x3522 |
| Exegy nxFeed | feed decode + **book build** on FPGA NIC | "arbitrate, decode, normalize and build order books"; "Average <1.2 µs Maximum <8 µs SOP-to-SOP latency measured on the switch" (venue/feed not stated) | exegy_nxfeed |
| Exegy nxAccess | tick-to-trade | "Hardware Tick-to-Trade w/ Pattern matcher 389 ns ... w/o Pattern matcher 624 ns ... Software Tick-to-Trade 1.5 µs", CME, SOP-to-SOP on switch; mean/median not stated | exegy_nxaccess |
| Exegy nxFramework IP | MAC/TCP/UDP | 10G MAC/PCS "29 ns RTT"; "10G full TCP stack 53 ns RTT"; UDP 43 ns RTT @322 MHz; ref. designs "Sub 100 ns RTT" T2T, risk gateway "Sub 1 µs RTT" | exegy_nxframework |
| Exegy XTP ticker plant | feed handling | FPGA-based; no latency figure | — |
| NovaSparks (acquired by Exegy, announced 2026-01-14) | feed decode + book build | "order book building and multi-feed consolidation with sub microsecond processing latency"; "2 to 5 times faster on average and 10 to 50 times during market peaks" vs software (archived site, 2025-01-10) | exegy_novasparks, novasparks_speed |
| LDA LightSpeed V2 TOE | TCP offload | "less than five nanoseconds" (3 TX / 4 RX clock ticks at 644 MHz) | lda_lightspeed |
| LDA MAC/PCS | Ethernet | "Round Trip Latency 21.8 nanoseconds, Including AMD FPGA Transceivers" (16-bit) | lda_macpcs |
| LDA NeoMUX Ultimate 10G | mux / L1 | "Mux: 30ns minimum, 34ns average. Layer 1: 2ns" | lda_neomux |
| LDA Lynx (VU2P GTF) | board I/O | "~0.65ns total avg path" | lda_lynx |
| Altera AN 849 (Stratix 10) | Ethernet MAC+PHY | "round-trip latency of 171.0 ns compared to the 10GBASE-R Ethernet design example ... with 246.5 ns" (2018) | altera_an849 |
| Achronix Speedster7t / VectorPath | — | lists "Financial analysis and high-frequency trading" as an application; **no trading or finance-ML figure found** | achronix_compute |
| Fixnetix iX-eCute | pre-trade risk | "enforces pre-market risks at nanosecond time frames" (2013 PR, no number) | fixnetix_ixecute |
| Myrtle VOLLO SDK (vendor docs) | LSTM/MLP inference on FPGA | batch 1, seq 1 (one timestep), V80 99p: lstm_tiny (268K params) 1.4 µs ... lstm_large (22.2M) 7.4 µs; MLP 295K params 1.8 µs; PCIe round trip with no compute 64 B/64 B mean 1.47 µs, p99 1.75 µs | myrtle_vollo_sdk |

Not found / not documented: Intel/Altera HFT reference design or finance page (intel.com/altera.com returned 403);
STAC-T0 on an Intel FPGA; NVIDIA ConnectX/Rivermax trading figures newer than VMA; "Hyannis", "Orthogonal", "Atomic"
as trading FPGA vendors; "Arista 7135 Lightning" / "Cisco Triton ASIC" (names not documented — do not use).

---

## 3. Trading ASICs
- No commercial trading-ASIC product with a published latency was found. Arista 7130/7135 and Cisco 3550-F/T are
  documented as FPGA-based.
- AMD Solarflare X4 NIC is documented as a "CUSTOM DESIGNED LOW-LATENCY ASIC" (VENDOR).
- In-house ASIC teams (JOB, existence only, no performance): Jane Street "ASIC Engineer" — "As part of our Ultra Low
  Latency team ... work on both FPGA-based and ASIC-based technologies" (updated 2026-09-28); Jump Trading "Campus
  ASIC Engineer (Intern)" — "our ASIC team that is building next-generation, ultra-low-latency systems to power trading
  with machine learning and other algorithms" (updated 2026-08-18). (janestreet_asic, jump_asic)
- Press reports on HFT ASICs: NOT VERIFIED; none cited.

---

## 4. Peer-reviewed FPGA papers (feed, book, trading systems, LOB inference)

### 4.1 Corrections to the two papers the survey cites
- **leber2011 — full text read** (author PDF https://people.ucsc.edu/~hlitz/papers/hft_fpga.pdf). The word "book"
  does not occur. Measured: standard NIC path "aggregated latency of 12.8 us"; FPGA with FAST decode "a latency of
  only 2.6 us, for reception and decoding of FAST packets ... a 4 times latency reduction". Virtex-4 FX100. PR-meas.
  **Feed decode only, no book.**
- **lockwood2012 — paper not read (paywalled); HOTI 2012 slides read**
  (http://www.hoti.org/hoti20/slides/Lockwood_AlgoLogic.pdf via Wayback). Demonstrated application is a FIX
  "Position & Exposure Monitor"; "Latency (Logic processing inside FPGA) 200ns"; "10GbE PHY delay 400ns (one-way)";
  "Total Latency (pin-to-pin) 1µs"; NetFPGA-10G. No order book in the slides. Dvorak & Korenek 2014 (full text) call
  their own work "the first hardware realization of the book handling". Paper content UNVERIFIED.

### 4.2 Order-book construction on FPGA

| Key | What | Figure + conditions | Evidence |
|---|---|---|---|
| dvorak2014book (DDECS 2014, pp. 175–178) | options depth of book (5+5 price levels, ~1100 bits/instrument), cuckoo-hash lookup, QDR-II SRAM; hash table built in software | "With average latency of 253 ns the proposed architecture is able to handle 119 275 instruments while using only 144 Mbit QDR SRAM"; d=2: 200/227/253 ns min/avg/max; Virtex-5 LX155T at 150 MHz; latencies computed from cycle counts, memory latency (19 cycles) measured in hardware | PR-est, full text |
| liu2024book (ASAP 2024, pp. 71–76) | book build + snapshot; price levels mapped to DRAM addresses, two-level BBO cache | "latency of the FPGA solution ranges from 26 to 209 ns, which is 3.5 to 37.6 times faster than the software solution"; ">3.26 million messages per second"; device/measurement not in abstract | PR-unclear, abstract |
| boutros2017build (ReConFig 2017) | full HLS HFT system: network stack, parsing, book handling, strategy | "Xilinx Kintex Ultrascale FPGA running at 156 MHz. Our on-board measurements show an end-to-end round-trip latency less than 870ns" | PR-meas, abstract |
| kao2022trading (VLSI-DAT 2022) | 10GbE PHY (25 ns), stack, partial decode, book, strategy | "latency from the internal market packet analysis to the ordering packet triggered is approximately 433 ns"; Taiwan futures (Yuanta) | PR-unclear, abstract |
| mohamedasanbasiri2021order (iSES 2021) | book storage, 45 nm CMOS / Artix-7 BRAM | no latency in abstract | PR, abstract |
| wray2010exploring (ASAP 2010) | algorithmic trading engine | "133 times faster than the corresponding software implementation"; Virtex-5 xc5vlx30 | PR-unclear, abstract |
| gupta2024fpga (ICACRS 2024) | unspecified | 480 ns avg, 150k orders/s, no conditions | PR-unclear; weak venue — not recommended |

### 4.3 Feed decode, arbitration, event matching

| Key | Figure + conditions | Evidence |
|---|---|---|
| morris2009fpga (HOTI 2009) | OPRA FAST on Celoxica AMDC, 2x GbE: "process up to 3.5 M messages per second ... rebroadcasts at least 99% of packets with a latency of less than 26 us. The hardware portion ... constant latency ... of 4 us" | PR-meas, abstract |
| tang2016scalable (ISCC 2016) | **decoding, not order book**: HLS decoders, Chinese A-share templates, Kintex-7, "0.5~1.3us per message on average" | PR-unclear, abstract |
| litz2011dsl (WHPCF 2011) | FAST decode engine programmed from a DSL; Virtex-6 156 MHz; repeats 2.6 µs | PR-meas, full text |
| dou2019accelerator (JCSC 2019) | FAST/FIX decode, "average latency of 447 ns"; device n/s | PR-unclear, abstract |
| denholm2015network (IEICE 2015) | A/B arbitration, Virtex-6: low-latency mode 6 ns (ITCH) / 5.25 ns (OPRA, ARCA); high-reliability 42 / 36.75 ns; "We measure our packet processing latency by analysing our design and making use of the formulae", cycle counts checked on a Virtex-5 NIC; PowerEN software "takes 150ns" | PR-est, full text |
| sadoghi2010efficient (PVLDB 2010 demo) | matching market events against strategies (pub/sub), NetFPGA Virtex-II Pro 125 MHz: hardware-only 3.22 µs vs PC 53.94 µs at 250 strategies; hybrid 6.47 µs (250) to 1,307 µs (100K); hardware-only "feasible only for smaller workloads" | PR-meas, full text |
| zhang2025speculative (TechRxiv) | ITCH parser, 1-cycle parse, Cocotb simulation only | PRE |
| pottathuparambil2011low, zhou2015fpga, denholm2013application, li2014fast, pasetto2011ultra, sadoghi2011towards | metadata only | UNVERIFIED |

### 4.4 FPGA inference of an LOB model
- **liu2026microsecond** (IEEE Access 14:141192–141215, 2026; abstract only). INT8 residual 1D-CNN, 86,211 params,
  FI-2010 feature vectors (no feed/book in hardware). "Synthesized, placed and routed on a Xilinx Kintex UltraScale
  KU040, a fully-unrolled latency-optimized architecture reaches a deterministic 57 µs on-chip inference-core latency
  with zero run-to-run jitter, at a directly measured whole-board power of 3.11 W. On-board execution is bit-exact with
  the PyTorch INT8 reference"; "5.98-10.44x lower latency ... than an NVIDIA RTX 5070 Laptop GPU". Whether 57 µs is an
  on-board timer measurement or a post-route cycle count: not stated in the abstract. PR-unclear (latency), PR-meas
  (power). This is the closest peer-reviewed FPGA LOB-model implementation found; it is a CNN on FI-2010 features, not
  DeepLOB/TransLOB/TLOB.

### 4.5 Surveys
- **loveless2013online** (CACM 56(10), 2013; full text via ACM Queue version): **no hardware content** ("FPGA",
  "hardware", "ASIC", "GPU" absent). Usable for online/streaming algorithms over LOB data only.
- **deschryver2015fpga** (Springer edited book, 2015): metadata only; chapter titles suggest pricing/risk. UNVERIFIED.
- No peer-reviewed survey dedicated to hardware for HFT or FPGA order books was found (Crossref title searches only).

---

## 5. In-network computing and SmartNICs

| Key | Accelerates | Figure + conditions | Evidence |
|---|---|---|---|
| jepsen2018packet (Camus, HotNets 2018) | ITCH feed filtering by symbol on a 32-port Tofino | "For the Nasdaq trace, all messages arrived within 50us with Camus, compared to 300us for the baseline. For the synthetic workload, 99.5% of the messages arrived within 20us with Camus, compared to 96.5% with the baseline." Baseline: DPDK subscriber filtering the whole feed in software; Nasdaq trace 2017-08-30 | PR-meas, full text |
| jepsen2020forwarding (CoNEXT 2020) | same + multi-switch content routing | same ITCH result; "The latency of the pipeline, which depends on the application, is less than 1µs"; 6.5 Tbps; ITCH 100K filters use 3.7% SRAM, 4.17% TCAM | PR-meas, full text |
| jepsen2022forwarding (TNET 2022) | journal extension | line rate 6.5 Tbps | PR, abstract |
| jepsen2019fast (PPS, SOSR 2019) | payload string search on PISA | no figure read | PR, abstract; figures UNVERIFIED |
| zheng2024planter / zheng2022automating (Planter) | in-switch feature extraction + ML inference; finance use case on NASDAQ ITCH and Jane Street data; Tofino BF6064X | latency only relative to switch.p4 (NDA): "When only ML models are deployed ... the latency is lower than 22% of switch.p4 in most cases ... combined with switch.p4, there is an overhead of less than 4.7%" (arXiv v1) | PR (CCR 2024) + PRE (numbers from arXiv v1) |
| xiong2019switches, zheng2024iisy | in-network classification (not finance) | backend load −70% (TNET abstract) | PR, abstract |
| hong2026lobin (existing key) | in-switch LOB build + tree models | relative latency only, Tofino under NDA | PRE (already in survey) |
| zilberman2017where (PAM 2017) | latency budget measurement, HFT-motivated | Table 1 medians: PCIe read RTT (64 B) 572 ns; kernel host stack 4.5 µs (99.9th 21 µs); kernel bypass 946 ns; Exablaze X10 NIC loopback 834 ns; SFN8522 985 ns; L1 switch ~2.7 ns ("within DAG measurement error-range"); Arista 7124FX L2 ~534 ns; "4.9ns per meter" fibre. Xeon E5-2637 v4, 2016–17 hardware. "there is no single source of latency" | PR-meas, full text |
| nvidia2021smartnic | SmartNIC | "Financial analysts use low latency SmartNICs for high frequency trading." No figure | VENDOR |

Not found: peer-reviewed P4 full order-book build or in-switch matching (beyond hong2026lobin); BlueField, Intel IPU,
Pensando trading benchmarks. Bressana et al. 2020 "Trading Latency for Compute in the Network" is **not about trading**
(storage deduplication) — do not cite for trading.

---

## 6. GPUs: low-latency networking and runtimes
- **GPUDirect RDMA** (nvidia_gpudirect_rdma_doc): "enables a direct path for data exchange between the GPU and a
  third-party peer device using standard features of PCI Express." No latency figure.
- **DOCA GPUNetIO** (nvidia_doca_gpunetio_doc; blog agostini2022gpunetio): "a GPU-centric solution that removes the CPU
  from the critical path"; blog gives throughput only (~100 Gbps, ~11.97 Mpps, 1 KB packets, A100X/ConnectX-6 Dx).
  No latency.
- **NVSHMEM IBGDA / GPUDirect Async** (markthub2022ibgda): "up to 9.5x higher throughput for NVSHMEM block-put
  operations with message sizes less than 1 KiB"; 180 MOPS; 4x DGX-A100. Latency numbers seen only in a summariser
  output (~64 µs) are not in the page text → UNVERIFIED, do not use. HPC, not trading.
- **Rivermax** (nvidia_rivermax): no figures.
- **TensorRT docs** (nvidia_tensorrt_optimization, v11.3.0, 2026-09-08): "each launch costs roughly 5-15 microseconds
  of host time. For models with many small kernels, that launch overhead can exceed the actual GPU work"; "If the batch
  size is one or small, this size can often be the performance-limiting dimension"; CUDA graphs cannot capture
  "loops, conditionals, and layers requiring data-dependent shapes". No official LSTM batch-1 latency.
- **NVIDIA RTX PRO 6000 Blackwell open-source run** (nvidia2026stacml, VENDOR, not audited): P99 "Ping Pong 2.5 [µs],
  Small 4.3, Medium 5.4, Large 14.2".
- **Published GPU tick-to-trade with measured latency:** none found (search limited).

---

## 7. Other AI accelerators (batch-1 / tail latency)

| Item | Figure + conditions | Evidence | Trading/LOB tested? |
|---|---|---|---|
| Google TPU v1, jouppi2017datacenter (ISCA 2017, full text) | MLP0 99th-pct limit 7 ms incl. host: Haswell batch 16 7.2 ms; K80 batch 16 6.7 ms; TPU batch 200 7.0 ms. "The TPU's deterministic execution model is a better match to the 99th-percentile response-time requirement"; LSTM1 limit "was 10 ms in 2014, but shrank it to 7 ms". No batch-1 figure | PR-meas | no |
| Groq TSP, abts2020think (ISCA 2020, abstract) | "20.4K processed images per second (IPS) with a batch-size of one" ResNet-50, "early results"; determinism by "eliminating all reactive elements in the hardware (e.g. arbiters, and caches)" | PR (vendor-authored) | no |
| Groq, ahmed2022answer (ASAP 2022, abstract) | "deterministic tail latency of 130 us for a batch-1 inference through BERT-base" | PR (vendor-authored) | no |
| Groq, abts2022software (ISCA 2022, abstract) | 10,440 TSPs, global memory "in less than 3 microseconds of end-to-end system latency" | PR (vendor-authored) | no |
| Groq STAC-ML GROQ221014 | Sumaco LSTM_A 99p 55.9–56.4 µs | AUDIT | yes (STAC-ML LSTMs) |
| golden2026xpuathalon (arXiv 2604.10852, accepted ISPASS 2026, full text) | Llama-3.1-8B low batch, latency/token vs H100: Cerebras 22.89%, Groq 30.03%, SambaNova SN-40 48.61% (Groq, SN-40 via vendor API); Groq mm 14.42x faster than H100; Gaudi, TPUv5e, MI300X also measured | PRE (independent) | no |
| AWS Inferentia2 (aws_inferentia2) | bert-base, batch 1, seq 128, Inf2.xlarge, Neuron 2.26: P50 0.999 ms, P99 1.040 ms | VENDOR data | no |
| Cerebras, SambaNova, Gaudi | only via golden2026xpuathalon | — | no |
| Tenstorrent, Graphcore IPU inference | nothing verified (Graphcore results page 404) | — | none found |

---

## 8. CPU-side inference and software order books

### 8.1 CPU batch-1 runtimes
- **OpenVINO 2026.3** (intel_openvino; data file in openvinotoolkit/openvino): "All models are executed using a batch
  size of 1", synchronous mode including pre/post-processing. Xeon Platinum 8480+: bert-base-cased seq128 INT8 3.63 ms,
  BF16 4.99 ms; resnet-50 INT8 1.03 ms. ISA path (AMX vs VNNI) not stated; no percentile stated. VENDOR.
- **Intel AMX** (intel_amx): tiles + TMUL, BF16/INT8; no numbers. VENDOR.
- **ONNX Runtime** (microsoft_onnxruntime, 2020 blog): BERT-SQUAD seq128 batch 1 on V100 fp16 "1.7 ms" (12-layer);
  CPU only relative ("17x latency speed up"). VENDOR.
- **MLPerf Single Stream** (mlcommons_inferencerules): 1 sample/query, "90%-ile early-stopping latency estimate" —
  standard definition of a batch-1 tail metric. Benchmark rule, no result.
- **LOB-model CPU timing — tran2019tabl** (TNNLS 2019, full text, existing key): Table III "average time (in
  millisecond) taken by the forward pass ... of a single sample", "CPU core i7-4790": C(BL) 0.0253, C(TABL) 0.0254,
  CNN 0.0613, LSTM 0.2291 ms. Framework/threads not stated; averages. PR-meas.
- No paper measuring DeepLOB (or similar) under ONNX Runtime, OpenVINO, TensorRT or quantisation on CPU was found.

### 8.2 Software order-book data structures
- **gross2024nanoseconds** (Optiver, CppCon 2024 slides; TALK): per-update median latency read from chart legends:
  std::map 33 ns (63 ns with "randomized allocs"); std::vector + lower_bound 34 ns; reversed vector 32 ns; branchless
  lower_bound 29 ns; linear search 22 ns. Hardware not stated for these slides. "A typical stock order book has ~1000
  price levels 'per side'"; "#1 Most of the time, you don't want node containers".
- **cook2017microsecond** (Optiver, CppCon 2017 slides; TALK): "A very good minimum time (wire to wire) for a
  software-based trading system is around 2.5us"; std::unordered_map find 14–24 ns vs custom array_map 7–9 ns (10 to
  10k elements, 32 x 2.89 GHz).
- **jericevich2022cointossx** (SoftwareX 2022, full text): Java matching engine; 90th-percentile latency 106–248 ns
  (server, up to 10 clients), 123–393 ns (4-CPU Azure VM); 1M orders single client p90 735 ns / 964 ns. Exact timed
  span not stated. PR (software journal).
- **liquibook** README: "2.0 million to 2.5 million inserts per second", hardware unstated, "rough order-of-magnitude
  estimate". Project claim. (Author field in bib set to "Liquibook project"; maintainer attribution not checked.)
- Existing: krapivensky2025glass (industry preprint). Still no peer-reviewed measurement of CPU book-update cost
  beyond CoinTossX's whole-engine latency.

---

## 9. Consolidated table

| Item | Category | Accelerates | Figure + conditions | Evidence type | Status |
|---|---|---|---|---|---|
| LDA LightSpeed + MAC/PCS, VU9P (stac_xlx200514) | FPGA IP | network I/O | STAC-T0 actionable min 24.2 ns, mean 27.9–29.2 ns, 68 B | AUDIT | VERIFIED |
| Exablaze ExaNIC V5P (stac_exa181106) | FPGA NIC | network I/O | STAC-T0 actionable max 44 ns, min 31 ns | AUDIT | VERIFIED (Wayback) |
| Solarflare + LDA on KU3 (stac_sfc170831) | FPGA + NIC | network I/O | STAC-T0 max 98 ns (68 B), 109 ns (507 B), 1 Gbps | AUDIT | VERIFIED (Wayback) |
| ADHOC HFFT-02A, Versal (stac_adhc240918) | FPGA appliance | full tick-to-trade, CME | STAC-T1 SOM-to-SOF mean 115.07, p99 140.04, max 163.88 ns (1x) | AUDIT | VERIFIED |
| Exegy Xero (stac_exg191010) | FPGA card | full tick-to-trade, CME | STAC-T1 mean 0.552, p99 0.609, max 0.789 µs | AUDIT | VERIFIED |
| AMD Solarflare X4542 (stac_n1_20260201) | NIC ASIC | host network stack | STAC-N1 100GbE 66 B @100k msg/s: mean 1.59, p99 1.70, max 3.79 µs | AUDIT | VERIFIED |
| LMS ÜberNIC, Agilex (stac_lms240510) | FPGA NIC | host UDP stack | rankings only | AUDIT | numbers UNVERIFIED |
| Metamako MetaApp 32 (stac_mmk150531) | L1 + FPGA mux | aggregation | 0.06 µs per hop (mean) | AUDIT | VERIFIED (Wayback) |
| STAC-ML spec (stac_ml_wg, stac_ml_limits2025) | benchmark | LSTM inference | Sumaco/Tacana × LSTM_A/B/C × NMI; 99p; inference only | STAC public text | VERIFIED; spec details UNVERIFIED |
| A100 (stac_ml_nvda2023) | GPU | LSTM inference | Tacana 99p A 35.2, B 68.5, C 640 µs | AUDIT | VERIFIED |
| Groq 8x GroqCard (stac_ml_groq2022) | ASIC | LSTM inference | Sumaco LSTM_A 99p 55.9–56.4 µs | AUDIT | VERIFIED (LSTM_C UNVERIFIED) |
| VOLLO, 4x Agilex (stac_ml_mrtl2022) | FPGA (Intel) | LSTM inference | Sumaco 99p A 24.0, B 64.8 µs, C 1.35 ms | AUDIT | VERIFIED |
| VOLLO, 4x Agilex (stac_ml_mrtl2023) | FPGA (Intel) | LSTM inference | Tacana 99p A 5.07, B 6.89, C 31.0 µs | AUDIT | VERIFIED |
| GH200 (stac_ml_smc2025) | GPU | LSTM inference | Tacana 99p A 4.70, B 7.10, C 15.8 µs | AUDIT | VERIFIED |
| VOLLO, Versal VP1802 (stac_ml_mrtl2026) | FPGA (AMD) | LSTM inference | Tacana 99p A <2, B <3, C <8 µs | AUDIT | bounds VERIFIED |
| Xelera Silva, Alveo V80 (stac_ml_xlra2026) | FPGA | GBT inference | 99p ≤1.95 µs (GBT_A/B), 2.88 µs (GBT_C) | AUDIT | VERIFIED |
| STAC-A2 H100 (stac_a2_nvda2023, _b) | GPU | MC Greeks (risk) | 7.40 ms / 8.9 ms warm baseline | AUDIT | VERIFIED |
| VOLLO SDK docs (myrtle_vollo_sdk) | FPGA | LSTM/MLP inference | V80 batch 1 99p 1.4 µs (268K) – 7.4 µs (22.2M); PCIe RTT mean 1.47 µs | VENDOR | VERIFIED as claim |
| Arista 7130 / 7135LB / MetaMux / MultiAccess | L1 + FPGA | L1, mux | 4–6 ns L1; 5 ns @25G; mux avg 39 ns; 52.7 ns | VENDOR | VERIFIED |
| Cisco Nexus 3550-F / -H | L1 + FPGA | L1, mux | L1 3–5 ns / 3.2 ns; FastMux 39–48 ns | VENDOR | VERIFIED |
| Cisco Nexus K35-S / K3P-S | FPGA NIC | host I/O | raw 64 B median 780 / 696 ns wire-user-wire | VENDOR | VERIFIED |
| Cisco Nexus V5P / V9P | FPGA NIC | in-FPGA I/O | trigger 34 ns min; loopback 50 ns min | VENDOR | VERIFIED |
| AMD Solarflare X4/X3/X2 | NIC | host I/O | 590 / 796 / 918 ns, 4 B, Onload, metric unspecified | VENDOR | VERIFIED |
| Exegy nxFeed | FPGA NIC | feed decode + book build | avg <1.2 µs, max <8 µs SOP-to-SOP | VENDOR | VERIFIED |
| Exegy nxAccess | FPGA | tick-to-trade CME | 389 ns / 624 ns HW; 1.5 µs SW | VENDOR | VERIFIED |
| Exegy nxFramework | FPGA IP | MAC/TCP/UDP | 29 / 53 / 43 ns RTT | VENDOR-REF | VERIFIED |
| NovaSparks | FPGA | feed + book build | "sub microsecond"; 2–5x avg vs SW | VENDOR | VERIFIED (qualitative) |
| LDA LightSpeed / MAC/PCS / NeoMUX | FPGA IP/appliance | TCP, Ethernet, mux | <5 ns; 21.8 ns RTT; mux 34 ns avg | VENDOR | VERIFIED |
| Altera AN 849, Stratix 10 | FPGA ref design | Ethernet | 171.0 ns RTT vs 246.5 ns | VENDOR-REF | VERIFIED |
| Achronix Speedster7t/VectorPath | FPGA | — | no trading figure | VENDOR | NO FIGURE |
| Fixnetix iX-eCute | FPGA | pre-trade risk | "nanosecond time frames" | VENDOR | VERIFIED (no number) |
| AMD X4 NIC ASIC | ASIC | NIC datapath | "custom designed low-latency ASIC" | VENDOR | VERIFIED |
| Jane Street / Jump ASIC teams | in-house ASIC | ULL trading, ML | job ads | JOB | existence only |
| leber2011 | FPGA paper | FAST feed decode (no book) | 2.6 µs vs 12.8 µs, Virtex-4 | PR-meas | VERIFIED (full text) |
| lockwood2012 | FPGA paper | FIX position/exposure monitor (slides) | 200 ns logic, 1 µs pin-to-pin | slides | paper UNVERIFIED |
| dvorak2014book | FPGA paper | depth-of-book lookup/update | avg 227–280 ns; Virtex-5 150 MHz | PR-est | VERIFIED (full text) |
| liu2024book | FPGA paper | book build + snapshots | 26–209 ns; >3.26 M msg/s | PR-unclear | VERIFIED (abstract) |
| boutros2017build | FPGA paper | HLS full HFT system | <870 ns round trip, on-board, KU 156 MHz | PR-meas | VERIFIED (abstract) |
| kao2022trading | FPGA paper | tick-to-order incl. book | ~433 ns; Taiwan futures | PR-unclear | VERIFIED (abstract) |
| morris2009fpga | FPGA paper | OPRA FAST feed | HW 4 µs constant; system <26 µs; 3.5 M msg/s | PR-meas | VERIFIED (abstract) |
| tang2016scalable | FPGA paper | market-data decode (not book) | 0.5–1.3 µs/msg, Kintex-7 | PR-unclear | VERIFIED (abstract) |
| denholm2015network | FPGA paper | A/B arbitration | 5.25–42 ns (cycle analysis) | PR-est | VERIFIED (full text) |
| sadoghi2010efficient | FPGA paper | event/strategy matching | 3.22 µs HW vs 53.94 µs PC | PR-meas | VERIFIED (full text) |
| dou2019accelerator | FPGA paper | FAST/FIX decode | 447 ns avg | PR-unclear | VERIFIED (abstract) |
| liu2026microsecond | FPGA paper | LOB-model inference (INT8 1D-CNN, FI-2010) | 57 µs deterministic core; 3.11 W; KU040 | PR-unclear | VERIFIED (abstract) |
| loveless2013online | overview | none (no hardware) | — | PR | VERIFIED (full text) |
| jepsen2018packet / jepsen2020forwarding | P4 switch | ITCH feed filtering | all msgs <50 µs vs 300 µs SW; pipeline <1 µs | PR-meas | VERIFIED (full text) |
| zheng2024planter | P4 switch | in-network ML on ITCH/Jane Street | relative latency only (NDA) | PR + PRE | VERIFIED |
| zilberman2017where | measurement | host/NIC/switch latency budget | kernel 4.5 µs; bypass 946 ns; PCIe 572 ns; L1 ~2.7 ns | PR-meas | VERIFIED (full text) |
| GPUDirect RDMA / GPUNetIO / Rivermax | GPU networking | NIC↔GPU | no latency figures | VENDOR docs | VERIFIED (no figure) |
| TensorRT docs | GPU runtime | — | 5–15 µs host time per kernel launch | VENDOR docs | VERIFIED |
| jouppi2017datacenter | ASIC (TPU) | inference | 7 ms p99 limit; batch 16 CPU/GPU vs 200 TPU | PR-meas | VERIFIED (full text) |
| ahmed2022answer | ASIC (Groq) | inference | BERT-base batch-1 tail 130 µs | PR (vendor) | VERIFIED (abstract) |
| golden2026xpuathalon | cross-vendor | LLM inference | Cerebras/Groq/SN-40 latency/token 23%/30%/49% of H100 | PRE | VERIFIED (full text) |
| aws_inferentia2 | ASIC | inference | BERT-base b1 P99 1.040 ms | VENDOR | VERIFIED |
| intel_openvino | CPU runtime | inference | Xeon 8480+ BERT-base INT8 b1 3.63 ms | VENDOR | VERIFIED |
| tran2019tabl | CPU | LOB-model inference | TABL 0.0254, CNN 0.0613, LSTM 0.2291 ms/sample, i7-4790 | PR-meas | VERIFIED (full text) |
| gross2024nanoseconds | software LOB | book update | median 22–34 ns (vector/linear); std::map 33/63 ns | TALK | VERIFIED (slides) |
| cook2017microsecond | software | lookup; wire-to-wire | ~2.5 µs SW wire-to-wire minimum; hash 7–9 vs 14–24 ns | TALK | VERIFIED (slides) |
| jericevich2022cointossx | software | matching engine | p90 106–393 ns | PR | VERIFIED (full text) |
| GPU tick-to-trade paper; P4 full book/matching; BlueField trading; DeepLOB under ONNX/OpenVINO; HFT-ASIC press | — | — | — | — | NOT FOUND |

---

## 10. Corrections to the current `sections/p3_sim_hw.tex`

Line numbers are as of 2026-10-04 and may drift.

1. **Table `tab:lob-latency`, zhang2019 row (line ~947): the LSTM and CNN values appear mislabelled in the source.**
   I checked both papers myself. DeepLOB (arXiv 1808.03668v6, Table III) lists "CNN-I [26] 0.025", "LSTM [28]
   0.061", "C(TABL) [25] 0.229" ms. The TABL paper it cites, [25] = tran2019tabl, Table III gives "C(TABL) 0.0254",
   "CNN 0.0613", "LSTM 0.2291" ms forward pass per sample, "measured on the same machine with CPU core i7-4790". The
   three DeepLOB numbers are the TABL numbers with the model labels rotated. So the survey's "LSTM 0.061 ms; CNN
   0.025 ms" cannot be relied on. The LSTM and CNN rows come from another paper and a 2014 desktop CPU, so they are
   not comparable to DeepLOB's 0.253 ms, whose device is not stated. Suggested fix: drop the LSTM/CNN entries from
   the zhang2019 row and add a tran2019tabl row ("TABL 0.025 ms, CNN 0.061 ms, LSTM 0.229 ms; i7-4790 CPU, single
   sample, mean"). Whether the mismatch is a transcription error or [26]/[28] really report those values: [26] and
   [28] were not read, so this is UNVERIFIED. The labels plainly do not match [25].
2. **"Vendor-audited benchmarks of FPGA trigger paths" (line ~995):** STAC audits are independent third-party audits,
   not vendor audits. Suggest "Independently audited (STAC) benchmarks of FPGA network paths". STAC calls T0
   "tick-to-trade network I/O".
3. **Book building attributed to leber2011/lockwood2012** (line ~992, "Field-programmable gate arrays have long been
   used for the first [book building]: decoding market-data feeds in hardware is well documented"): Leber 2011 (full
   text) has no order book, and the Lockwood 2012 slides show a FIX position/exposure monitor with no book. Keep both
   for feed decoding. For the book itself, cite dvorak2014book (full text; options depth of book, 227–280 ns average,
   cycle-count estimate), liu2024book (26–209 ns, abstract), boutros2017build (<870 ns on-board, whole HLS system
   with book) and, as vendor, exegy_nxfeed. The "What has to be accelerated" item 1 ("Decoding and the book update
   ... FPGAs handle them well") currently has no book-specific citation, so the same keys apply there.
4. **"We found no published FPGA implementation of DeepLOB, TransLOB or TLOB with a measured latency. The closest is
   ... popov2026ssm"** (end of §What has to be accelerated): still true for those three models. Closer evidence now
   exists: liu2026microsecond (peer-reviewed IEEE Access, INT8 1D-CNN on FI-2010, 57 µs deterministic on KU040,
   abstract only), and audited STAC-ML LSTM inference on FPGA (VOLLO: 5.07 µs 99p for LSTM_A on Agilex, <2 µs on
   Versal). The STAC models are generic LSTMs, not LOB architectures.
5. **"For single-event, latency-critical inference, batch-oriented accelerators are at a disadvantage"** (§GPUs:
   throughput for research): too strong as stated. In audited STAC-ML Tacana results a GH200 GPU reached 4.70 µs 99p
   for LSTM_A at one model instance, lower than the 2023 VOLLO FPGA result (5.07 µs), and 15.8 µs vs 31.0 µs for
   LSTM_C. The 2026 VOLLO Versal result (<2 µs) is lower again. Conditions: inference only, ingestion and features
   excluded, sliding-window suite that allows reuse of earlier computation. The A100 in 2023 was 35.2 µs. A fair
   statement is that GPUs reach single-digit microseconds on small LSTMs only in the sliding-window setting with
   vendor-optimised code. (The khoda2023rnn V100 batch-1 figure in §why slow is not contradicted; it is a different,
   unoptimised setting.)
6. **Table `tab:vendors` caption: "the one independently audited result is the STAC-T0 benchmark"** (line ~1008).
   This is true only of the table's contents. Other audited results exist: STAC-T0 (LDA 24.2 ns min, ExaNIC 31–44
   ns), STAC-T1 (ADHOC 115 ns mean, Exegy Xero 0.552 µs mean, both full CME tick-to-trade), STAC-ML (above), and
   **Xelera itself now has an audited STAC-ML GBT result** (XLRA260312: ≤1.95 µs 99p GBT_A/B, 2.88 µs GBT_C on Alveo
   V80), so the Xelera row need not rely only on vendor claims.
7. **Research agenda, "Public, auditable benchmarks that cover book building and inference together"**: still
   valid, but the text should acknowledge that STAC-T1 audits full tick-to-trade (feed + book/state + order) and
   STAC-ML audits inference alone. Neither combines a book with a learned model. STAC-ML explicitly excludes
   "data ingestion, parsing, or feature generation".
8. **§Why slow, point 4 (launch overhead):** consistent with the official TensorRT docs ("each launch costs roughly
   5-15 microseconds of host time"). This could be added as a second, official source next to the 2019 blog.
9. **§Networks and distance:** no error found. zilberman2017where gives peer-reviewed measured medians (kernel stack
   4.5 µs, kernel bypass 946 ns, PCIe 64 B read RTT 572 ns, fibre 4.9 ns/m) that could replace or back the vendor
   figures (openonload, vma).
10. **ASIC sentence (kuon2007):** no error. Documented trading-relevant ASIC evidence is limited to the AMD
    Solarflare X4 NIC ASIC (vendor) and Jane Street/Jump ASIC job adverts (existence only). No performance figures
    are published.

---

## 11. Summary: what is new and most relevant

- **STAC-ML Markets (Inference) is the key addition.** It is the only independently audited benchmark of model
  inference for trading. Audited 99th-percentile latency for the small LSTM at one instance, sliding-window suite:
  A100 35.2 µs, VOLLO/Agilex FPGA 5.07 µs, GH200 GPU 4.70 µs, VOLLO/Versal FPGA <2 µs. Also audited: Groq (fixed
  window, 56 µs) and Xelera GBT on V80 (≤1.95 µs). It measures inference only (no ingestion, no features) on generic
  LSTMs, not LOB architectures.
- **Audited tick-to-trade figures exist**: STAC-T1 ADHOC Versal (CME) 115 ns mean and Exegy Xero 0.552 µs mean, plus
  STAC-T0 network-I/O results from 24 to 98 ns.
- **Citation correction:** leber2011/lockwood2012 do not show FPGA book building. dvorak2014book (full text),
  liu2024book and boutros2017build do.
- **Table correction:** the zhang2019 LSTM/CNN latencies in `tab:lob-latency` do not match the TABL source they cite
  (labels rotated; CPU i7-4790).
- **New peer-reviewed items:** liu2026microsecond (FPGA LOB-model inference, 57 µs); Camus (P4 ITCH filtering, 50 vs
  300 µs); Planter (in-switch ML on ITCH, relative latency only); zilberman2017where (measured latency budget);
  jouppi2017datacenter (99th-percentile deadlines drive accelerator design); CoinTossX (matching engine p90
  106–393 ns).
- **Vendor landscape filled in** (claims only): Arista 7130, Cisco Nexus SmartNIC/3550, AMD Solarflare X2/X3/X4,
  Exegy nxFeed/nxAccess/nxFramework, NovaSparks (now Exegy), LDA, Altera AN 849, Myrtle VOLLO. Achronix has no trading
  figure, and no trading ASIC product is documented.
- **Gaps:** no GPU tick-to-trade paper, no P4 full-book or matching paper, no BlueField trading figure, no DeepLOB
  under ONNX/OpenVINO/quantisation, and no peer-reviewed hardware-for-HFT survey were found. Search was limited, so
  these are not proofs of absence.
