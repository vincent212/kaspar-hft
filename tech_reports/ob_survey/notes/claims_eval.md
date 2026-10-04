# Claims verification — evaluation / manipulation / hardware

Verified 2026-10-02. BibTeX in `bib/refs_eval.bib`. No key collides with `refs.bib`.

---

## Part 1 — What each reference establishes

### Learning theory / model selection

**vapnik1998** — Vapnik, *Statistical Learning Theory*, Wiley 1998, ISBN 978-0-471-03003-4 (publisher page). Book on ERM, structural risk minimisation, SVMs. VC bound form as commonly cited. Source checked: Burges (1998) SVM tutorial, Eq. (3), citing Vapnik 1995. For 0/1 loss, with probability 1−η:
R(α) ≤ R_emp(α) + sqrt( [h(log(2l/h) + 1) − log(η/4)] / l ),
with h the VC dimension and l the sample size. **Caveat:** I did not check the equation number or exact typesetting in the 1998 book itself. The book also states a tighter, non-square-root form.

**bartlett2002** — JMLR 3:463–482 (PDF checked). It defines the Rademacher complexity R_n(F) = E sup_f |(2/n) Σ σ_i f(X_i)|. Theorem 5(b) (binary classification) says that with probability ≥ 1−δ:
P(Y≠f(X)) ≤ P̂_n(Y≠f(X)) + R_n(F)/2 + sqrt(ln(1/δ)/(2n)).
Theorem 8 (general loss L dominated by a cost φ in [0,1]) says:
E L(Y,f(X)) ≤ Ê_n φ(Y,f(X)) + R_n(φ̃∘F) + sqrt(8 ln(2/δ)/n).
The paper notes that Theorem 5 "can never be much worse than the VC results".

**akaike1974** — IEEE TAC 19(6):716–723, doi 10.1109/TAC.1974.1100705 (Crossref). Introduces AIC = (−2) log(maximum likelihood) + 2·(number of independently adjusted parameters), and the MAICE (minimum-AIC) estimate for model identification.

**schwarz1978** — Ann. Stat. 6(2):461–464, doi 10.1214/aos/1176344136 (Crossref/Project Euclid). Derives a large-sample model-dimension criterion from the leading terms of the asymptotic Bayes solution, independent of the prior. This is the origin of BIC (log-likelihood penalised by ½·k·log n). The penalty form is the standard statement; I did not quote it from the paper text.

**tibshirani1996** — JRSS-B 58(1):267–288, doi 10.1111/j.2517-6161.1996.tb02080.x (Crossref). The lasso minimises the residual sum of squares subject to Σ|β_j| ≤ t. The L1 constraint produces exact zeros and so performs variable selection.

**hansen2011mcs** — Econometrica 79(2):453–497, doi 10.3982/ECTA5771 (Crossref). The Model Confidence Set is a set of models built so that it contains the best model with a given confidence level, analogous to a confidence interval for a parameter. It is built by sequential equal-predictive-ability tests that eliminate models.

**bailey2014pseudo** — Notices AMS 61(5):458–471, doi 10.1090/noti1105 (Crossref; AMS PDF checked). It shows that the backtested Sharpe ratio of the best of N skill-less configurations grows with N. It introduces the Minimum Backtest Length (Theorem 2):
MinBTL ≈ ( [(1−γ)Z⁻¹(1−1/N) + γZ⁻¹(1−1/(N e))] / E[max_N] )² < 2 ln N / E[max_N]².
Quote: "if only five years of data are available, no more than forty-five independent model configurations should be tried or we are almost guaranteed to produce strategies with an annualized Sharpe ratio IS of 1 but an expected Sharpe ratio OOS of zero."

**bailey2014 (already in refs.bib) — exact formulas.** Source checked: SSRN working-paper PDF (davidhbailey.com/dhbpapers/deflated-sharpe.pdf), pp. 7–8. The equation numbers below are from that version; check against the JPM typeset version if you cite equation numbers.
- Eq. (1): E[max{ŜR_n}] ≈ E[{ŜR_n}] + sqrt(V[{ŜR_n}]) · ( (1−γ) Z⁻¹[1 − 1/N] + γ Z⁻¹[1 − (1/N) e⁻¹] ),
  "where γ (approx. 0.5772) is the Euler-Mascheroni constant, Z is the cumulative function of the standard Normal distribution, and e is Euler's number." Under the null (E[{ŜR_n}]=0) this becomes the user's form, sqrt(V)[(1−γ)Φ⁻¹(1−1/N) + γΦ⁻¹(1−1/(Ne))]. **Matches.**
- Eq. (2): DSR ≡ PSR(ŜR_0) = Z[ (ŜR − ŜR_0) sqrt(T − 1) / sqrt(1 − γ̂_3 ŜR + ((γ̂_4 − 1)/4) ŜR²) ],
  where ŜR_0 = sqrt(V[{ŜR_n}]) ( (1−γ)Z⁻¹[1 − 1/N] + γZ⁻¹[1 − (1/N)e⁻¹] ). T is the sample length, γ̂_3 the skewness and γ̂_4 the kurtosis (not excess kurtosis) of the selected strategy's returns.

**belkin2019** — PNAS 116(32):15849–15854, doi 10.1073/pnas.1903070116 (Crossref). Proposes the "double descent" risk curve. Past the interpolation threshold, increasing capacity lowers test risk again, which subsumes the classical U-shape. The paper gives evidence across many model families (e.g., random-feature models, neural networks, decision-tree ensembles). This is a direct caveat to "more capacity ⇒ more overfitting".

**zhang2017rethinking** — ICLR 2017, arXiv 1611.03530, OpenReview Sy8gdB9xx (dblp). Standard CNNs trained with SGD easily fit random labels, and even random-noise inputs. Explicit regularisation leaves this qualitatively unchanged. So classical capacity measures (VC, Rademacher) do not explain why these networks generalise. Extended version: CACM 64(3):107–115, 2021.

### Manipulation / hidden liquidity

**cartea2020spoof** — Appl. Math. Finance 27(1–2):67–98, doi 10.1080/1350486X.2020.1726783 (Crossref). Optimal-control model of a liquidating seller who posts spoof buy LOs to skew the volume imbalance, which raises the arrival rate of buy market orders. The model trades spoofing gains against an expected regulatory fine; when the fine is large enough the agent does not spoof. When the fine is low, spoofing "considerably increases the revenues from liquidating a position" (abstract, ORA).

**lee2013spoof** — J. Financial Markets 16(2):227–252, doi **10.1016/j.finmar.2012.05.004** (Crossref; note that a web-search tool suggested the wrong DOI 10.1016/j.jfm.2013.06.001). Empirical study of spoofing on the Korea Exchange: large orders placed away from the market and later withdrawn, used to create order-book imbalance. Specific numbers (0.81% of orders; the "2× size, ≥6 ticks" definition) appear only in secondary summaries that I could not attribute with certainty. **Not quoted. Read the paper before citing any number.**

**egginton2016** — Financial Management 45(3):583–608, doi 10.1111/fima.12126 (Crossref). Quote stuffing (episodic spikes in quote messages) is pervasive: "over 74% of US exchange-listed securities experienced at least one episode during 2010". During episodes, liquidity falls, trading costs rise, and short-term volatility increases.

**frey2017iceberg** — Q. J. Finance 7(3):1750007, doi 10.1142/S2010139217500070 (Crossref abstract). When market participants detect an iceberg order, they respond strongly with matching market orders. The more of an iceberg that executes, the smaller its price impact, which is consistent with liquidity rather than informed trading. Icebergs bring more trading (a positive externality) but create an adverse-selection cost for limit orders.

**christensen2013** — J. Trading 8(3):68–95, doi 10.3905/jot.2013.8.3.068 (Crossref). A GLOBEX-specific algorithm that detects iceberg orders and predicts their hidden remaining volume, implementable at millisecond latency in a feed handler. On 2011 E-mini S&P 500 data it estimates that about 9% of LOB volume is iceberg orders. The 9% figure comes from the publisher abstract via search summary; I did not check the PDF.

**cftc2020jpm** — CFTC Release 8260-20, 29 Sep 2020. JPMorgan (JPMorgan Chase & Co., JPMorgan Chase Bank, J.P. Morgan Securities) was ordered to pay $920.2 million for spoofing and manipulation in precious-metals and U.S. Treasury futures over at least eight years, with "hundreds of thousands of spoof orders". Breakdown: restitution $311,737,008, disgorgement $172,034,790, civil monetary penalty $436,431,811. This is the largest monetary relief ever imposed by the CFTC.

**cftc2015sarao** — CFTC Release 7156-15, 21 Apr 2015. Civil action (N.D. Ill.) against Navinder Singh Sarao and Nav Sarao Futures Ltd for manipulation and spoofing (layering) in the E-mini S&P 500 near-month contract for over five years. It alleges that the conduct contributed to the market conditions that led to the 6 May 2010 Flash Crash. Follow-ups: CFTC 7480-16 (proposed consent order) and 7486-16 (more than $38 million in sanctions). The DOJ guilty plea is at justice.gov (key doj2016sarao).

### HFT economics

**baron2019** — JFQA 54(3):993–1024, doi 10.1017/S0022109018001096 (Crossref). Differences in relative latency explain large differences in HFT firms' trading performance. Firms that improve their latency rank (e.g., through colocation upgrades) improve performance.

**menkveld2013** — JFM 16(4):712–740, doi 10.1016/j.finmar.2013.06.006 (Crossref). Studies one large HFT acting as a cross-market market maker. It loses on inventory but earns the spread. Its trade participation rate is 8.1% on the incumbent market and 64.4% on the entrant market, and about four of five trades are passive. These numbers are from the abstract via search summary.

### Hardware / systems

**amd_ul3524** — AMD product brief (footnotes checked). "Less than 3ns transceiver latency† and 7X performance vs. previous generation††". The card has 64 ultra-low-latency transceivers, a custom 16 nm Virtex UltraScale+ FPGA, and 1,680 DSP slices. The LUT count is given as 787K in the brief body; the press release says 780K.
- Footnote †: AMD lab test of 8/16/23. GTF near-end loopback in "RAW mode". It **excludes** protocol overhead, framing, programmable-logic latency, package flight time, and so on. It is therefore a SerDes figure, not a tick-to-trade figure.
- Footnote ††: based on simulation comparing GTY with GTF transceivers.
**amd_ul3422** — slim FHHL variant (launched 14 Oct 2024). Brief: "Less than 3 ns transceiver latency & up to 7X performance vs. previous generation". The 7X figure is a Synopsys VCS simulation from Feb 2022. Its STAC-T0 record claim is extrapolated from the UL3524 STAC-T0 test with Exegy nxFramework (STAC report AMD240422, already in refs as exegy_stac2024).

**algologic** — AMD/Algo-Logic solution brief, Alveo U50: "5th generation ... CME Tick-To-Trade (T2T) System is a sub-500 nanosecond trading solution"; "Ultra Low Tick-to-Trade Latency of <500nsec". Pre-built IP: CME feed handler, CME F&O order book, 10G TCP endpoint, ULL 10GE PHY+MAC. The brief carries no date. On its own PTRC page, Algo-Logic says only "well under one microsecond".

**exegy_enyx** — Exegy press release dated **10 May 2022**: "Exegy Incorporated ... today announced it has acquired Enyx". Enyx product lines named in secondary sources: nxAccess, nxFeed, nxLink, nxFramework.

**magmio** — Magmio product and home pages. Strategies are written in C++, inserted into a Magmio template, and compiled via HLS to the FPGA. Supported cards: Cisco Nexus V9P-3, V5P, K3P-S, K3P-Q, and AMD Alveo UL3524 and X3522PV. Homepage quotes: "latency below 100ns for trigger-based strategies", "below 350ns with book building and more complex strategies", "FPGA reduces cancellation latency to around 100 ns". These are vendor claims with no stated methodology.

**xelera_silva** — Xelera product page. Supports GBT models (XGBoost, LightGBM, CatBoost) and NN models (LSTM, MLP, Conv1D, single-head attention), fp16/bf16, batch size 1. Stated p99 latencies:
- inline IP core: "~250 ns (LightGBM, AMD UL3524)"
- FPGA card offload: 1.1–1.3 µs GBT
- CPU-only: 0.8–7.7 µs GBT
The model size is not stated for the 250 ns figure.

**napatech** — Napatech press release of 12 May 2025: Xelera Silva on Napatech FPGA SmartNICs for GBT inference. The press release gives no number. The Napatech partner page cites "1.55 μs" for XGBoost regression (100 features, 1000 trees, depth 8, batch 1) and refers to an NT200A02 benchmark report dated March 2025. A "30× faster than CPU" figure appeared only in a search summary; I could not attribute it, so it is **not used**.

**lighttrader** — HPCA 2023, pp. 1017–1030, doi 10.1109/HPCA56546.2023.10070930 (Crossref). Authors are from KAIST and Rebellions Inc. The system combines an FPGA with CGRA AI accelerators (TSMC 7 nm, 16 TFLOPS / 64 TOPS) and adds workload scheduling and DVFS scheduling. It was back-tested on CME data. Abstract: "13.92× and 7.28× speed-up of AI algorithm processing compared to existing GPU-based, FPGA-based systems, respectively"; "up to 99.5% response rates". The abstract also says the scheduler "relieves the miss rate from 17.1% to 23.1%" (verbatim, and internally odd as worded). A companion Hot Chips 34 (2022) talk exists: doi 10.1109/HCS55958.2022.9895619.

**famous** — Kabir et al., FPT 2024 (2-page poster, doi 10.1109/ICFPT64416.2024.11113430), arXiv 2409.14023. A dense multi-head-attention accelerator on Alveo U55C. Results: 328 GOPS (8 heads, d=768, tile 64), 3.28× faster than a Xeon Gold 5220R CPU and 2.6× faster than a V100 GPU. **Not trading-specific.**

**eliteformer** — Agostinelli, Bohm Agostini, Tumeo, arXiv 2607.03652 (4 Jul 2026). A ternary-weight, hybrid-linear-attention Transformer co-designed for FPGA (VCK5000 Versal, HLS). Results: 10× weight compression and 12.8× KV-cache compression vs LLaMA 3; up to 3.9× lower latency than LLaMA 3 on an A100 at long context. **Not trading-specific; it is an LLM accelerator.**

**openonload** — Solarflare/Arista joint solution brief (undated; it tested OpenOnload with the SFN5122F and the Arista DCS-7124SX). Half-round-trip, 64-byte messages:
- TCP: 3.1 µs back-to-back, 3.6 µs through the switch
- UDP: 2.9 µs back-to-back, 3.4 µs through the switch
- switch alone: 520 ns
These are old 10GbE-era figures. Current AMD X2522 brief says only "sub-microsecond hardware latency" and gives no Onload application-level number.

**vma** — Mellanox solution brief "Accelerating Electronic Trading" (©2017): "VMA provides low latency of 1.2 microseconds for a TCP socket-based application and 1.0 microseconds for UDP. In addition, VMA delivers more than 4 million ingress multicast packets in a single thread." The brief does not give a separate multicast latency number. A 2011 Mellanox white paper (WP_VMA_TCP_vs_Solarflare_Benchmark) has multicast latency plots; I did not extract numbers from it.

**fiberlatency** — ITU-T G.114 (05/2003), Table A.1: "Optical fibre cable system, digital transmission — 5 µs/km (Note 1)". Note 1: "This value is provisional and is under study." G.114 does not give a refractive index. **corning_smf28** — Corning SMF-28 Ultra (PI1424, 2013) gives an effective group index of 1.4676 at 1310 nm and 1.4682 at 1550 nm. With n_g/c this gives 4.895 and 4.897 ns/m.

**nvidia_numba2025** — NVIDIA Technical Blog, M. J. Bennett, 4 Mar 2025. Monte Carlo simulation of LOB dynamics (bid/ask levels, midprice SDE, alpha process) on an H200 with Numba. GPU-vs-CPU speedups: 14× (1-day), 38× (5-day), 114× (1-month). Parallelism is across paths, not time steps. CuPy is discussed but the post gives no CuPy-vs-Numba number. This is a blog post, not a peer-reviewed paper, and the simulation is not a matching-engine backtest.
**jaxlob2023** — ICAIF 2023, pp. 583–591, doi 10.1145/3604237.3626880. A GPU LOB simulator in JAX (not Numba/CuPy) that processes thousands of books in parallel. Its abstract gives no speedup number; a "75×" figure appeared only in a search summary and is not used.

**zahavy2026** — Tom Zahavy (Google DeepMind), "LLMs can't jump", position-paper preprint dated Jan 27th, 2026 (tomzahavy.com PDF). The paper argues that LLMs handle induction and, increasingly, deduction, but lack a mechanism for abduction (the Einstein "Jump" from experience to axioms). It uses General Relativity as a case study and proposes multimodal world models as the missing grounding. arXiv 2608.14397 (Balani & Panda) cites it as "Zahavy (2026)".

**mayeski2026agent** — "Using AI in Trading Strategy Development: the AI Quant Agent", Substack, published 2026-09-13. URL: https://vincentmayeski.substack.com/p/using-ai-in-trading-strategy-development (Substack archive API).
**mayeski2026abduction** — "Search Versus Abduction: Why LLMs Cannot Invent New Alpha", Substack, published 2026-09-17. URL: https://vincentmayeski.substack.com/p/search-versus-abduction-why-llms.
**Note:** neither full title is exactly the short title given in the request.

---

## Part 2 — Claim verdicts

| # | Claim | Verdict | Source / note |
|---|---|---|---|
| L1 | VC bound form (Vapnik) | VERIFIED | Form checked via Burges 1998 Eq. (3) citing Vapnik 1995; 1998 book page not checked |
| L2 | Rademacher bound form | VERIFIED | B&M 2002 Thm 5(b), Thm 8 (PDF) |
| L3 | E[max SR] ≈ sqrt(V)[(1−γ)Φ⁻¹(1−1/N)+γΦ⁻¹(1−1/(Ne))] | VERIFIED | Bailey & LdP DSR Eq. (1) with E[{SR_n}]=0 |
| L4 | DSR = PSR at SR_0 with skew/kurtosis terms | VERIFIED | Eq. (2), quoted above; γ̂_4 is raw kurtosis |
| L5 | Double descent as caveat to "capacity ⇒ overfit" | VERIFIED | Belkin et al. 2019 |
| L6 | Deep nets fit random labels | VERIFIED | Zhang et al. 2017 |
| M1 | JPMorgan 2020 CFTC $920M, precious metals + UST futures | VERIFIED | CFTC 8260-20; total $920.2M |
| M2 | Sarao: CFTC/DOJ, E-mini layering, 2010 flash crash | VERIFIED | CFTC 7156-15 ("contributed to the market conditions"); DOJ plea |
| M3 | Lee et al. 2013 bibliographic details | VERIFIED | Correct DOI 10.1016/j.finmar.2012.05.004 |
| H1 | UL3524 transceiver latency <3 ns | VERIFIED | Brief; GTF loopback, RAW mode, excludes PL/protocol |
| H1b | UL3422 exists, same <3 ns | VERIFIED | UL3422 brief |
| H2 | Algo-Logic CME T2T "<500 ns" on Alveo U50 | VERIFIED | AMD/Algo-Logic solution brief |
| H3 | Exegy acquired Enyx in **2021** | CONTRADICTED | Announced 10 May 2022 |
| H3b | Enyx nxFramework product | VERIFIED | Secondary summaries of the Exegy release; also named in UL3422 STAC endnote |
| H4 | Magmio FPGA trading platform exists | VERIFIED | Vendor site; <100 ns trigger, <350 ns with book building |
| H5 | Xelera Silva FPGA GBT inference, published latency | VERIFIED | ~250 ns p99 LightGBM inline on UL3524; 1.1–1.3 µs offload |
| H6 | Napatech SmartNIC + Xelera partnership | VERIFIED | Napatech PR 12 May 2025 |
| H7 | LightTrader paper (HPCA 2023) | VERIFIED | doi 10.1109/HPCA56546.2023.10070930 |
| H8 | FAMOUS FPGA MHA accelerator (2024) | VERIFIED | FPT 2024 / arXiv 2409.14023; not HFT |
| H9 | ELiTeFormer | VERIFIED | arXiv 2607.03652; LLM FPGA accelerator, not HFT |
| H10a | OpenOnload latency figures | VERIFIED | 2.9–3.6 µs half-RTT (SFN5122F era); no current official number |
| H10b | VMA multicast latency figures | UNVERIFIED | Official brief gives 1.0 µs UDP / 1.2 µs TCP and a 4M multicast pps rate, but no multicast-specific latency |
| H11 | Fibre ≈ 4.9–5 ns/m, n ≈ 1.47 | VERIFIED | ITU-T G.114: 5 µs/km (provisional); Corning group index 1.4676/1.4682 → 4.90 ns/m |
| H12 | GPU LOB/backtest sims with Numba/CuPy, ≥7× speedup (paper) | UNVERIFIED | No paper found. NVIDIA blog (Numba, 14–114×, Monte Carlo LOB dynamics) is the closest source; JAX-LOB uses JAX |
| H13 | Liu et al. 2026 INT8 1D-CNN LOB on KU040, 57 µs | UNVERIFIED | Not found |
| H14 | Alveo U50 bidirectional risk mgmt 518/530 ns | UNVERIFIED | Not found; Algo-Logic PTRC page says only "well under one microsecond" |
| H15 | Zahavy 2026 on LLMs and abduction | VERIFIED | "LLMs can't jump", Google DeepMind preprint, Jan 2026 |
| H16 | Mayeski Substack posts | VERIFIED | 2026-09-13 and 2026-09-17; full titles differ from the short names |
| H17 | Baron et al. JFQA 2019 | VERIFIED | 54(3):993–1024 |
| H18 | Menkveld 2013 JFM | VERIFIED | 16(4):712–740 |
