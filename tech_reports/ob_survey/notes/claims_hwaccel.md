# Claims & paper notes — hardware acceleration of order-book models (why batch-1 inference is slow)

Verified 2026-10-03. Sources were read as arXiv PDFs (text-extracted with pdftotext), Crossref /
NeurIPS / MLSys metadata pages, OpenAlex abstracts, and NVIDIA documentation pages. Quotes are
copied from the extracted text. Anything not read is marked UNVERIFIED. "Abstract only" means
only the abstract was read.

BibTeX: new keys in `bib/refs_hwaccel.bib`.

**Reused existing keys (not duplicated):** vaswani2017, zhang2019, hochreiter1997, jacob2018,
hls4ml, leber2011, lockwood2012, lighttrader, famous, exegy_stac2024, popov2026ssm,
pauliano_hftfpga, hedges2026frontier, hong2026lobin, berti2025tlob, makinde2026tkan,
zhangzohren2021.

**New keys:** williams2009roofline, dao2022flashattention, pope2023scaling, kim2023fullstack,
gholami2022quant, han2017ese, khoda2023rnn, nvidia_cudagraphs_blog, nvidia_cuda_guide,
nvidia_cuda_bpg, neugebauer2018pcie, krapivensky2025glass, denholm2014arb.

---

## 0. What p7_outlook.tex currently lists (hardware sections)

- Sections: Determinism; Book-building vs inference latency; Commercial FPGA landscape (Table
  tab:vendors); Between FPGA and GPU; Transformers on FPGAs; GPUs: throughput for research;
  Networks and distance; Workload placement (Table tab:placement); Hardware cannot extend signal
  lifetime; Open questions in hardware.
- FPGA products: AMD Alveo UL3524 / UL3422 (amd_ul3524, amd_ul3422), Exegy incl. Enyx
  (exegy_enyx, exegy_stac2024), Algo-Logic (algologic), Magmio (magmio), Xelera Silva
  (xelera_silva), Napatech + Xelera (napatech).
- Between FPGA and GPU: LightTrader (lighttrader), Corsair (corsair2025), FPGA network cards (napatech).
- Transformers on FPGAs: FAMOUS (famous), ELiTeFormer (eliteformer), quantisation (jacob2018).
- FPGA tool flows / models: hls4ml (hls4ml), popov2026ssm; book building: leber2011, lockwood2012.
- GPUs: JAX-LOB (frey2023), NVIDIA Numba Monte Carlo (nvidia_numba2025), IPUs (zhangzohren2021).
- Networks: dpdk, openonload, vma, corning_smf28, fiberlatency, baron2019, budish2015, kuon2007.
- Other: bochud2007 (Hawkes kernels in hardware).

---

## 1. Self-attention cost is quadratic in sequence length

| # | Claim | Exact quote / number | Source | Type |
|---|---|---|---|---|
| A1 | Per-layer complexity of self-attention is O(n²·d); recurrent O(n·d²); convolutional O(k·n·d²). n = sequence length, d = representation dimension. | Table 1: "Self-Attention O(n2 · d), Sequential Operations O(1)"; "Recurrent O(n · d2), O(n)"; "Convolutional O(k · n · d2), O(1)". Caption: "n is the sequence length, d is the representation dimension, k is the kernel size of convolutions". | vaswani2017, arXiv 1706.03762, Table 1, p.6. https://arxiv.org/abs/1706.03762 | Peer-reviewed (NIPS 2017), full text |
| A2 | Self-attention is cheaper than recurrence only when n < d. | "self-attention layers are faster than recurrent layers when the sequence length n is smaller than the representation dimensionality d" | vaswani2017, §4 | Full text |
| A3 | Attention time AND memory are quadratic in sequence length. | Abstract: "Transformers are slow and memory-hungry on long sequences, since the time and memory complexity of self-attention are quadratic in sequence length." | dao2022flashattention, arXiv 2205.14135v2. https://arxiv.org/abs/2205.14135 ; venue NeurIPS 2022 pp. 16344–16359 (proceedings.neurips.cc metadata) | Peer-reviewed, full text |
| A4 | Standard attention writes the N×N matrices to GPU memory. | "Standard attention implementations materialize the matrices S and P to HBM, which takes O(N²) memory." | dao2022flashattention §2.2 | Full text |
| A5 | Attention is limited by memory traffic, not arithmetic; FlashAttention fixes this by tiling into on-chip SRAM. | "most operations in Transformers are bottlenecked by memory accesses"; "As some or most of the operations are memory-bound (e.g., softmax), the large number of memory accesses translates to slow wall-clock time." A100: HBM "1.5-2.0TB/s", on-chip SRAM "estimated around 19TB/s", "an order of magnitude faster than HBM but many orders of magnitude smaller in size". | dao2022flashattention §1, §2.1, §2.2 | Full text |
| A6 | Definitions of compute-bound vs memory-bound (for undergrads). | "Compute-bound: the time taken by the operation is determined by how many arithmetic operations there are … Memory-bound: the time taken by the operation is determined by the number of memory accesses … Examples include … elementwise (e.g., activation, dropout), and reduction (e.g., sum, softmax, batch norm, layer norm)." | dao2022flashattention §2.1 | Full text |
| A7 | Kernel fusion is the standard fix for memory-bound operations. | "The most common approach to accelerate memory-bound operations is kernel fusion: if there are multiple operations applied to the same input, the input can be loaded once from HBM, instead of multiple times for each operation." | dao2022flashattention §2.1 | Full text |
| A8 | In BERT, attention's act-to-act matmuls grow quadratically and lower arithmetic intensity at long sequence length. | "the cost of act-to-act matmuls in the MHA module grow quadratically with the increase in sequence length, leading to a reduction in arithmetic intensity" | kim2023fullstack, arXiv 2302.14017v1 §2.2. https://arxiv.org/abs/2302.14017 | Survey preprint (arXiv only; no journal version checked), full text |

Note for LOB use: DeepLOB/TransLOB inputs are 100 snapshots × 40 features (claims_dl.md), so n = 100. Whether n < d for any specific LOB Transformer depends on its d; not checked here.

## 2. Small-batch inference is memory-bandwidth bound (roofline)

| # | Claim | Exact quote / number | Source | Type |
|---|---|---|---|---|
| B1 | Roofline: attainable performance is the minimum of peak compute and memory bandwidth × operational intensity. | "Attainable GFlops/sec = Min(Peak Floating Point Performance, Peak Memory Bandwidth x Operational Intensity)"; operational intensity = "operations per byte of DRAM traffic". | williams2009roofline. Read: authors' preprint PDF https://people.eecs.berkeley.edu/~kubitron/cs252/handouts/papers/RooflineVyNoYellow.pdf (title there: "...for Floating-Point Programs and Multicore Architectures"). CACM metadata from Crossref doi 10.1145/1498765.1498785. The CACM final text was NOT read (ACM returned 403); wording may differ slightly. | Peer-reviewed; preprint full text |
| B2 | Kernels with low operational intensity are memory bound. | "either it hits the flat part of the roof, which means performance is compute bound, or it hits the slanted part of the roof, which means performance is ultimately memory bound." | williams2009roofline (preprint) | Full text |
| B3 | At small batch, time to load the weights dominates inference. | "At small batch sizes and sequence lengths, the time to load weights dominates. At larger batch sizes and sequence lengths (e.g. 2048+ tokens with batch size 512+), the time to load the KV cache dominates." | pope2023scaling, arXiv 2211.05102v1 §2.1. https://arxiv.org/abs/2211.05102 ; MLSys 2023 vol. 5 pp. 606–624 (proceedings.mlsys.org metadata) | Peer-reviewed, full text |
| B4 | Batch-1 matrix–vector work does ~1–2 operations per parameter loaded → memory-bandwidth bound. | "for a single matrix-vector operation, we perform roughly one multiplication and addition per parameter loaded since the loads cannot be shared across tokens. This leads to performing roughly 2 operations per parameter loaded … This makes its performance memory bandwidth-bound" | kim2023fullstack §2.2.1 (about GPT-2 decoding one token at a time) | Survey preprint, full text |
| B5 | Batch-1 LSTM on a GPU uses the hardware poorly; batching recovers throughput. | QuickDraw LSTM: "Tests of the batch 1 inference for the same model using an Nvidia Tesla V100 GPU yield a throughput of 660 events/sec. Increasing the batch size to 10 increases the throughput to 7700 events/sec … approximately 30000 if the batch size is increased to 100." FPGA (estimated from II): "between 4300 to 9700 events/sec". "many physics tasks are inherently low-batch problems." | khoda2023rnn, arXiv 2207.00559v1 §5; Mach. Learn.: Sci. Technol. 4(2):025004 (2023), Crossref. https://arxiv.org/abs/2207.00559 | Peer-reviewed, full text (arXiv v1) |
| B6 | With no batching, sparse LSTM is faster on CPU and GPU because memory bandwidth is saved. | "With no batching, we observed both CPU and GPU are faster for the sparse LSTM because the saving of memory bandwidth is more salient." | han2017ese, arXiv 1612.00694 §6.3; FPGA '17 pp. 75–84, doi 10.1145/3020078.3021745 | Peer-reviewed, full text |

Caveat: B3/B4 are about large language models, not LOB models. A 60k-parameter DeepLOB fits entirely in GPU on-chip memory, so the "weight loading dominates" argument is not shown to apply to it. No source found that measures arithmetic intensity of DeepLOB-size models. UNVERIFIED for LOB models.

## 3. Recurrent networks are sequential across time steps

| # | Claim | Exact quote / number | Source | Type |
|---|---|---|---|---|
| C1 | RNN hidden state h_t depends on h_{t-1}; this blocks parallelism within an example. | "they generate a sequence of hidden states ht, as a function of the previous hidden state ht−1 and the input for position t. This inherently sequential nature precludes parallelization within training examples … The fundamental constraint of sequential computation, however, remains." | vaswani2017 §1 | Full text |
| C2 | Recurrent layer needs O(n) sequential operations; self-attention O(1). | "a self-attention layer connects all positions with a constant number of sequentially executed operations, whereas a recurrent layer requires O(n) sequential operations." | vaswani2017 §4, Table 1 | Full text |
| C3 | On an FPGA, a single reused RNN block has initiation interval (= latency) growing linearly with sequence length; unrolling per time step cuts II by the sequence length but multiplies resources by it. | "the initiation interval (II) of the design increases linearly with the length of the sequence since a new RNN inference cannot begin until the previous inference is complete; in other words, the II is equal to the latency." Non-static mode: "resource utilization that is a factor of the sequence length larger"; "This reduces the II by a factor of the length of the sequence". | khoda2023rnn §3 | Full text |
| C4 | FPGA LSTM latencies of ~1.5–1.7 µs (top-quark tagging model, ≤20-step sequences). | Table 5: LSTM static latency "1.6–1.6" µs, non-static "1.5–1.5" µs; Table 2 LSTM "1.7–1.7" µs to "8.3–12.4" µs depending on reuse factor. These are **Vivado HLS synthesis estimates at 200 MHz** ("Vivado HLS 2019.2 is used for HLS synthesis with the synthesis clock frequency set to 200 MHz"), not on-board measurements. | khoda2023rnn §5, Tables 2 and 5 | Full text |
| C5 | Popov & Huber claim recurrent architectures have "inherent sequential bottlenecks". | Abstract: "Traditional recurrent architectures face inherent sequential bottlenecks that prevent real-time deployment in latency-critical environments." | popov2026ssm, abstract via Crossref (doi 10.71465/csb211). Full text download failed. | Small journal, abstract only |

## 4. GPU overheads for small workloads

| # | Claim | Exact quote / number | Source | Type |
|---|---|---|---|---|
| D1 | Per-operation GPU submission overhead is at microsecond scale. | "the time taken by each GPU operation (e.g. kernel or memory copy) is now measured in microseconds. However, there are overheads associated with the submission of each operation to the GPU – also at the microsecond scale" | nvidia_cudagraphs_blog (A. Gray, 5 Sep 2019). https://developer.nvidia.com/blog/cuda-graphs/ | Vendor technical blog |
| D2 | Measured: 2.9 µs kernel costs 9.6 µs per launch with synchronisation, 3.8 µs with overlapped launches, 3.4 µs with CUDA Graphs (V100, CUDA 10.1). Graph creation ~400 µs, one-off. | "We can use the profiler to measure the time taken to be 2.9μs, where we are running on an NVIDIA Tesla V100 GPU using CUDA 10.1"; "9.6μs per kernel (including overheads)"; "3.8μs (vs 2.9μs kernel execution time)"; "gives 3.4μs (vs 2.9μs kernel execution time)"; "the time to create and instantiate the graph is relatively large at around 400μs". | nvidia_cudagraphs_blog | Vendor blog, one microbenchmark |
| D3 | Per-kernel launch overhead can be a large share of short kernels; CUDA Graphs reduce CPU launch cost. | "These operations, necessary for setting up and launching the kernel, are an overhead cost which must be paid for each kernel that is issued. For a GPU kernel with a short execution time, this overhead cost can be a significant fraction of the overall end-to-end execution time." "first, CPU launch costs are reduced compared to streams, because much of the setup is done in advance". | nvidia_cuda_guide (CUDA C++ Programming Guide 12.4, §3.2.8.7). https://docs.nvidia.com/cuda/archive/12.4.0/cuda-c-programming-guide/index.html | Official docs |
| D4 | Graphs pay off only when reused. | "In situations where the workflow is not changing, the overhead of definition and instantiation can be amortized over many executions, and graphs provide a clear advantage over streams." | nvidia_cuda_guide §3.2.8.7 | Official docs |
| D5 | Host↔device bandwidth is far below on-device bandwidth; minimise transfers; batch small transfers. | "The peak theoretical bandwidth between the device memory and the GPU is much higher (898 GB/s on the NVIDIA Tesla V100, for example) than the peak theoretical bandwidth between host memory and device memory (16 GB/s on the PCIe x16 Gen3)." "because of the overhead associated with each transfer, batching many small transfers into one larger transfer performs significantly better than making each transfer separately". | nvidia_cuda_bpg (CUDA C++ Best Practices Guide 12.4). https://docs.nvidia.com/cuda/archive/12.4.0/cuda-c-best-practices-guide/index.html | Official docs |
| D6 | Measured PCIe latency is hundreds of ns per transaction. | NIC round trip for 128 B: "around 1000 ns with PCIe contributing around 900 ns." 64 B DMA reads on Xeon E5: "minimum of 520ns and a median of 547ns. The maximum latency out of 2 million transactions was 947ns." | neugebauer2018pcie, SIGCOMM '18 pp. 327–341, doi 10.1145/3230543.3230560. PDF: https://www.cl.cam.ac.uk/research/srg/netos/projects/pcie-bench/neugebauer2018understanding.pdf | Peer-reviewed, full text |

Note: D6 measures NIC/FPGA DMA over PCIe, not a GPU cudaMemcpy. No measured GPU-specific small-transfer latency paper was read. A measured cudaMemcpy latency for small transfers is UNVERIFIED.

## 5. Order-book building

| # | Claim | Exact quote / number | Source | Type |
|---|---|---|---|---|
| E1 | Leber et al.: FPGA decoding of Ethernet/IP/UDP and FAST market feeds, 4× lower latency than software. | Abstract: "enables hardware decoding of Ethernet, IP and UDP as well as of the FAST protocol … Our approach shows a 4x latency reduction in comparison to the conventional Software based approach." | leber2011, FPL 2011, doi 10.1109/FPL.2011.64. Abstract via OpenAlex (https://api.openalex.org/works/https://doi.org/10.1109/FPL.2011.64) | Peer-reviewed, abstract only |
| E2 | Lockwood et al.: FPGA IP library with fixed 1 µs end-to-end latency at 10 Gb/s line rate. | Abstract: "The application sustains 10Gb/s Ethernet line rate with a fixed end-to-end latency of 1μs - up to two orders of magnitude lower than comparable software implementations." Also: "The high and unpredictable latency of these systems has led the trading world to explore alternative 'hybrid' architectures with hardware acceleration." | lockwood2012, HOTI 2012, doi 10.1109/HOTI.2012.15. Abstract via OpenAlex | Peer-reviewed, abstract only |
| E3 | **Flag for p7:** neither the leber2011 nor the lockwood2012 abstract mentions order-book maintenance. Leber is feed decoding; Lockwood is "networking, I/O, memory interfaces and financial protocol parsers". The p7 sentence "feed handling and book maintenance in hardware are well documented [leber2011, lockwood2012]" is supported for feed handling only. Book maintenance in those papers is UNVERIFIED (full texts not read). | — | — | — |
| E4 | Order-book price levels are an ordered set; standard implementations (std::map, red-black trees) are O(log n); a trie with cache table and BMI2 instructions is reported 9–15× faster than std::map on real market data. | "Typically all operations are O(log n)"; "std::set and std::map, which are typically implemented via red-black trees"; "speedups over C++'s standard std::map container: 6x—20x on modifying operations, 30x on lookup operations, 9x—15x on real market data". On cache locality: "The main reason for the introduction of index-pointers is cache locality". | krapivensky2025glass, arXiv 2506.13991v1. https://arxiv.org/abs/2506.13991 | Industry preprint (AKB System), not peer-reviewed, full text |
| E5 | In-switch LOB construction: each price/quantity comparison costs a pipeline stage. | "LOB construction and maintenance require a lot of price and order volume comparisons … On hardware, each comparison consumes a processing stage and reduces scalability." Absolute Tofino latency not given: "The latency of Tofino is under NDA"; results are relative latency ("over a 10% reduction in latency compared to the NASDAQ order-matching server benchmark"). | hong2026lobin, arXiv 2608.02424v1 (full text read this time; claims_deep2.md said abstract only). https://arxiv.org/abs/2608.02424 | Preprint, full text |
| E6 | Exegy/AMD STAC-T0: minimum "actionable latency" 13.9 ns (507-byte frames) and 14.1 ns (68-byte frames). Measures last inbound bit needed for a decision to first outbound bit; it is a trigger, not a model or book build. | "Minimum of 13.9 nanoseconds"; "Minimum of 14.1 nanoseconds"; "Actionable Latency, which is the time from the last bit of inbound data needed to make a trading decision to the first bit of the simulated outbound order." Page dated 25 June 2024. | exegy_stac2024, https://docs.stacresearch.com/news/AMD240422 (read via WebFetch summariser; quotes from that output) | Audited benchmark summary |
| E7 | FPGA A/B feed arbitration: latencies 10× lower than an FPGA commercial design and 4.1× lower than IBM PowerEN. | Abstract: "We offer latencies 10 times lower than an FPGA-based commercial design and 4.1 times lower than the hardware-accelerated IBM PowerEN processor, with throughputs more than double the required 10Gbps line rate." | denholm2014arb, ASAP 2014 pp. 36–40, doi 10.1109/ASAP.2014.6868628. PDF: http://www.doc.ic.ac.uk/~wl/papers/14/asap14sd.pdf | Peer-reviewed, full text |
| E8 | pauliano_hftfpga's "444 ns" is a synthesis/simulation figure, not a hardware measurement. | README: "we verified the cycle-by-cycle latency of the full pipeline. By utilizing a 250MHz clock … in just 111 clock cycles" under heading "(Synthesis)"; jitter section labelled "(Simulation)". | pauliano_hftfpga, https://github.com/pauliano22/hft-fpga README | GitHub repo, self-reported |

Not found / UNVERIFIED: a peer-reviewed source that measures hash-lookup or pointer-chasing cost in CPU order-book building. Morris, Thomas & Luk 2009 ("FPGA Accelerated Low-Latency Market Data Feed Processing", HOTI 2009, doi 10.1109/HOTI.2009.17) and Tang et al. 2016 (ISCC 2016, doi 10.1109/ISCC.2016.7543802) exist (Crossref) but their abstracts/full text were not obtainable; no claims taken from them; not added to bib.

## 6. Published latency of LOB deep models

| # | Claim | Exact quote / number | Source | Type |
|---|---|---|---|---|
| F1 | DeepLOB forward pass 0.253 ms, 60k parameters. | Table III "AVERAGE COMPUTATION TIME OF STATE-OF-THE-ART MODELS": "DeepLOB 0.253 [ms] 60k". Other rows: BoF 0.972, N-BoF 0.524, CNN-I 0.025, LSTM 0.061, C(TABL) 0.229 ms. Text: "it is swift to make predictions, making it possible for high frequency trading." | zhang2019, arXiv 1808.03668v6 §V-B, Table III. https://arxiv.org/abs/1808.03668 | Peer-reviewed (IEEE TSP), arXiv full text |
| F2 | **Caveats on F1:** the paper does not state the batch size or device used for the forward-pass timing. The only hardware statement is for training: "we train them using a single NVIDIA Tesla P100 GPU". Training batch is 32 ("We train with mini-batches of size 32"). Whether 0.253 ms is per sample at batch 1 is UNVERIFIED. The other rows cite other papers ([24]–[28]) and may come from different hardware — not stated. The IEEE TSP version was not read. | — | zhang2019 | — |
| F3 | TLOB paper: inference times on an RTX 3090 — MLP 0.08 ms, LSTM 0.21, CNN 0.36, CTABL 0.48, DAIN-MLP 0.50, CNNLSTM 0.49, AxialLOB 1.91, DLA 0.23, DeepLOB 1.31, BiNCTABL 0.71, MLPLOB 4.79, TLOB 2.24 ms. | Table 2 "Number of parameters and inference time for each model"; "All the experiments were carried out using an RTX 3090." Batch size not stated. Note DeepLOB is 1.31 ms and 1.4·10⁵ parameters here vs 0.253 ms and 60k in zhang2019 — same model name, different implementation/hardware. | berti2025tlob, arXiv 2502.15757v3. https://arxiv.org/abs/2502.15757 | Preprint, full text |
| F4 | Batch-one LOB model latencies are milliseconds in eager PyTorch on a laptop CPU/GPU: MLPLOB P50 2101 µs (CPU), TLOB H16 P50 5089 µs (MPS) / 22846 µs (CPU); FastBiNLOB H96 1604 µs (CPU). | Table 5 "Batch-one latency percentiles, µs" (P50/P90/P95/P99): MLPLOB CPU 2101/2400/2509/2794; MPS 2834/3252/3369/3728. TLOB H16 CPU 22846/24727/25248/25897; MPS 5089/7194/7725/8883. H96 mean CPU 1604/2002/2199/2985. H120 taper MPS 2124/2310/2396/2898. Setup: "10,000 timed single-observation calls after 256 warmup rows … forward-only plus softmax; it excludes feature preprocessing and data transfer … Apple M3 Max … PyTorch 2.12.0, eager FP32". | hedges2026frontier, arXiv 2606.25986v1 §5. https://arxiv.org/abs/2606.25986 | Preprint, full text. (claims_deep2.md said Table 5 did not extract; it did extract with pdftotext -layout.) |
| F5 | Latency does not track FLOPs/structural work: CatBoost median work 12,276 but 281 µs; histogram GBM work 528 but 4.34 ms. | "CatBoost rows have median structural work 12,276 but median latency only 281𝜇s. Histogram gradient boosting has lower median structural work, 528, but much higher median latency, 4.34ms." | hedges2026frontier §4.4 | Preprint, full text |
| F6 | LightTrader: AI inference latency is the obstacle to AI in HFT. | Abstract: "it is challenging to integrate the computationally intensive AI algorithm into the existing trading pipeline due to its excessively long latency and insufficient throughput"; "13.92× and 7.28× speed-up of AI algorithm processing compared to existing GPU-based, FPGA-based systems". | lighttrader, HPCA 2023, abstract via Semantic Scholar API | Peer-reviewed, abstract only |
| F7 | Popov & Huber: FPGA SSM on crypto order books, 247 µs end-to-end; 15.2× throughput vs GPU LSTM; Stratix V. | Abstract: "15.2× throughput improvement versus graphics processing unit (GPU)-optimized long short-term memory (LSTM) implementations … 247 microsecond end to-end latency for 100-dimensional order book state vectors … Stratix V FPGA". | popov2026ssm, abstract via Crossref | Small journal, abstract only |
| F8 | T-KAN does **not** report an FPGA implementation; FPGA suitability is stated as future work. | Abstract: "uniquely optimized for low-latency FPGA implementation via High level Synthesis (HLS)"; §5.3: "This structure is highly compatible with High-Level Synthesis (HLS) for **FPGA (Field Programmable Gate Array) implementation **. Future work should focus on mapping T-KAN onto hardware in order to achieve sub-…". No latency or resource numbers found in the text. | makinde2026tkan, arXiv 2601.02310v2 | Student journal / preprint, full text |
| F9 | ESE (speech LSTM, 1024 hidden units, not LOB): FPGA 82.7 µs vs GPU 240.2/287.4 µs (dense/sparse) vs CPU 6017.3/3569.9 µs. | "Processing the LSTM with 1024 hidden elements, ESE takes 82.7 us, CPU takes 6017.3/3569.9 us (dense/sparse), and GPU takes 240.2/287.4 us (dense/sparse)." Text then says "With batch=32, …" — whether the Table 8 CPU/GPU times are at batch 32 or batch 1 is ambiguous in the text (UNVERIFIED). Table 8 lists GPU dense total as 240.3. | han2017ese §6.3, Table 8 | Peer-reviewed, full text |
| F10 | FAMOUS (attention layer on Alveo U55C, not LOB): latency 0.597–0.94 ms per MHA layer; the V100 comparison number (1.5578 ms) is taken from another paper ([7]), not measured by the authors. | Table II: FAMOUS latency "0.94" ms (64,768,8) and "0.597" ms (64,512,8); "NVIDIA V100 GPU [7]" 1.5578 ms (64,512,4). Text: "Table II compared FAMOUS with some GPUs and CPUs running approximately at 1.5GHz". | famous, arXiv 2409.14023v3 | Peer-reviewed (FPT 2024 two-page poster), arXiv full text |
| F11 | hls4ml jet-tagging MLP: latency "on the scale of 100 ns" (HLS estimates). | Abstract: "we fit well within the available resources of modern FPGAs with a latency on the scale of 100 ns." §3.2 is titled "Latency and resource estimates in HLS". | hls4ml, arXiv 1804.06913v3 | Peer-reviewed, full text |

**Flag for p7 "Transformers on FPGAs":** p7 says small quantised attention models can run on programmable logic "at latencies of microseconds". FAMOUS (the cited example) reports 0.6–0.94 **milliseconds** for one dense MHA layer (sequence 64, d 512–768). No FPGA attention result at microsecond latency was read. The "microseconds" statement is UNVERIFIED by the cited sources.

No FPGA implementation of DeepLOB, TransLOB or TLOB with a measured latency was found. arXiv API searches (abs:"order book" AND abs:FPGA; abs:"limit order book" AND inference AND latency; abs:"order book" AND hardware) returned only makinde2026tkan, hedges2026frontier, hong2026lobin, zhangzohren2021 (IPU, training only) and arXiv 2206.09041 (training time, not read in detail).

## 7. Quantisation, pruning, distillation

| # | Claim | Exact quote / number | Source | Type |
|---|---|---|---|---|
| G1 | Quantising to ≤4-bit integers can cut memory footprint and latency up to 16×; 4–8× is common in practice. | Abstract: "Moving from floating-point representations to low-precision fixed integer values represented in four bits or less holds the potential to reduce the memory footprint and latency by a factor of 16x; and, in fact, reductions of 4x to 8x are often realized in practice in these applications." | gholami2022quant, arXiv 2103.13630v3; book chapter in Low-Power Computer Vision (Chapman and Hall/CRC, 2022) pp. 291–326, doi 10.1201/9781003162810-13 (Crossref) | Survey, full text (arXiv) |
| G2 | The survey groups efficiency methods as: efficient architectures, HW/NN co-design, pruning, knowledge distillation, quantisation. | §I headings: "a) Designing efficient NN model architectures", "b) Co-designing NN architecture and hardware together", "c) Pruning", "d) Knowledge distillation", "e) Quantization". Pruning: "neurons with small saliency (sensitivity) are removed, resulting in a sparse computational graph." Distillation: "training a large model and then using it as a teacher to train a more compact model." | gholami2022quant §I | Full text |
| G3 | Pruning + quantisation compressed a speech LSTM 20× with negligible accuracy loss; FPGA ran 43× faster than CPU and 3× faster than GPU. | Abstract: "compress the LSTM model size by 20x (10x from pruning and 2x from quantization) with negligible loss of the prediction accuracy"; "ESE is 43x and 3x faster than Core i7 5930k CPU and Pascal Titan X GPU implementations." | han2017ese | Full text |
| G4 | Pope et al. used int8 weight quantisation for low-batch latency. | Abstract: "we achieve a low-batch-size latency of 29ms per token during generation (using int8 weight quantization)". | pope2023scaling | Full text |
| G5 | Fixed-point is used in hls4ml RNNs because float is costly on FPGAs. | "32-bit floating-point calculations are often not required for optimal network inference, and are costly to implement on FPGAs." | khoda2023rnn §5.1 | Full text |

(jacob2018 — integer-only inference — already in refs.bib; not re-read here.)

---

## Summary table

| Topic | Status |
|---|---|
| Quadratic attention cost (time and memory) | VERIFIED — vaswani2017 Table 1; dao2022flashattention abstract and §2 |
| Attention is memory-bound / IO-aware | VERIFIED — dao2022flashattention §1–2 |
| Roofline model | VERIFIED from authors' preprint; CACM final text not read |
| Weight loading dominates at small batch | VERIFIED for LLMs (pope2023scaling, kim2023fullstack); UNVERIFIED for 60k-parameter LOB models |
| LSTM sequential across time | VERIFIED — vaswani2017; khoda2023rnn (FPGA II grows linearly with sequence length) |
| GPU batch-1 inefficiency | VERIFIED for one LSTM on V100 (khoda2023rnn: 660 vs 7700 vs ~30000 events/s at batch 1/10/100) |
| Kernel launch overhead (µs) and CUDA Graphs | VERIFIED — NVIDIA blog (V100 microbenchmark) and CUDA Programming Guide 12.4 |
| PCIe transfer cost | VERIFIED — NVIDIA Best Practices Guide (bandwidth); neugebauer2018pcie (≈0.5–1 µs per DMA, NIC/FPGA not GPU) |
| leber2011 / lockwood2012 support "book maintenance" | NOT SUPPORTED by abstracts (feed handling only); full texts not read |
| Order-book data-structure cost | Only an industry preprint (krapivensky2025glass); no peer-reviewed measurement of hash/pointer-chasing cost found |
| DeepLOB 0.253 ms | VERIFIED as printed; batch size and device for timing NOT stated |
| LOB model batch-1 latencies | VERIFIED — hedges2026frontier Table 5 (ms-scale, M3 Max eager PyTorch); berti2025tlob Table 2 (RTX 3090, batch not stated) |
| FPGA implementation of DeepLOB/TransLOB/TLOB with measured latency | NOT FOUND |
| p7 "small attention on FPGA at microseconds" | UNVERIFIED — FAMOUS reports 0.6–0.94 ms per MHA layer |
| Quantisation/pruning/distillation survey | VERIFIED — gholami2022quant |
