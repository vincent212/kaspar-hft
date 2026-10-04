# Claims: market impact (checked 2026-10-04)

Source types: **FT** = full text read; **FT-WP** = full text of a working-paper or preprint version, not the published version; **OCR** = full text of a scanned PDF read through OCR, so symbols may be garbled; **PREVIEW** = publisher's first-page preview only; **ABS** = abstract only.

Existing keys reused: `almgren2001`, `kyle1985`, `bouchaud2018` (refs.bib); `bouchaud2004`, `obizhaeva2013` (bib/refs_classic.bib).
New keys are in `bib/refs_impact.bib`: `toth2011`, `almgren2005`, `brunnermeier2005`, `moro2009`, `bacry2015impact`.
(`bacry2015` already exists in refs.bib for a different paper.)

---

## 1. Almgren and Chriss: permanent and temporary impact; cost versus risk

**almgren2001** — Almgren, R., Chriss, N. "Optimal Execution of Portfolio Transactions." *J. Risk* 3(2):5–39, 2001. DOI 10.21314/JOR.2001.041 (Crossref confirmed).
Read: **FT-WP**, preprint dated December 2000 (https://www.smallake.kr/wp-content/uploads/2016/03/optliq.pdf).

- Abstract: "We consider the execution of portfolio transactions with the aim of minimizing a combination of volatility risk and transaction costs arising from permanent and temporary market impact."
- Abstract: "... the concept of Liquidity-adjusted VAR, or L-VaR, that explicitly considers the best tradeoff between volatility risk and liquidation costs."
- p. 8: "We distinguish two kinds of market impact. Temporary impact refers to temporary imbalances in supply in demand caused by our trading leading to temporary price movements away from equilibrium. Permanent impact means changes in the 'equilibrium' price due to our trading, which remain at least for the life of our liquidation."

**Supports claim 1 in full.**

---

## 2. Kyle: prices linear in net order flow, slope λ

**kyle1985** — Kyle, A. S. "Continuous Auctions and Insider Trading." *Econometrica* 53(6):1315–1336 (pages per the JSTOR cover page), 1985. DOI 10.2307/1913210.
Read: **OCR** of a JSTOR scan (https://people.duke.edu/~qc2/BA532/1985%20EMA%20Kyle.pdf). OCR renders λ as "A", β as "B" and Σ as "%" or "5". I have written the Greek letters back in below. The JSTOR cover page gives pp. 1315–1336.

- Abstract (p. 1315): "The model has three kinds of traders: a single risk neutral insider, random noise traders, and competitive risk neutral market makers."
- p. 1315: "... the market makers set a price, and trade the quantity which makes markets clear. When doing so, their information consists of observations of the current and past aggregate quantities traded by the insider and noise traders combined. We call these aggregate quantities the 'order flow.'"
- p. 1319, Theorem 1: "There exists a unique equilibrium in which X and P are linear functions. ... the equilibrium P and X are given by (2.3) X(ṽ) = β(ṽ − p₀), P(x̃ + ũ) = p₀ + λ(x̃ + ũ)." The definitions of β and λ are garbled in the OCR. Eq. (2.6), 1/β = 2λ, reads cleanly.
- p. 1319: "The quantity 1/λ measures the 'depth' of the market, i.e. the order flow necessary to induce prices to rise or fall by one dollar."
- Sequential model, Theorem 2, eq. (3.12): Δp_n = λ_n(Δx_n + Δu_n). λ_n "measure the depth of the market (with small λ_n corresponding to a deep market)."

**Supports claim 2.** Two details for the survey:
- Kyle does not use the name "Kyle's lambda".
- The agents are one insider, noise traders and market makers. Noise traders are part of the mechanism; it is not just an informed trader plus market makers.

Bib note: `kyle1985` in refs.bib has `pages = {1315}`. Crossref also lists only 1315. I did not edit refs.bib.

---

## 3. Square-root law of metaorder impact

**toth2011** — Tóth, B., Lempérière, Y., Deremble, C., de Lataillade, J., Kockelkoren, J., Bouchaud, J.-P. "Anomalous Price Impact and the Critical Nature of Liquidity in Financial Markets." *Phys. Rev. X* 1(2):021006, 2011. DOI 10.1103/PhysRevX.1.021006. arXiv:1105.1694.
Read: **FT** (arXiv v3).

- p. 2: "the average relative price change Δ between the first and the last trade of a metaorder of size Q is well described by the so-called 'square-root' law: Δ(Q) = Y σ √(Q/V), where σ is the daily volatility of the asset, and V the daily traded volume ... The numerical constant Y is of order unity."
- p. 2: "the Q dependence is more generally described as a power-law relation Δ(Q) ∝ Q^δ, with δ in the range 0.4 to 0.7 ... We show in Fig. 1 our own proprietary data corresponding to nearly 500,000 trades on a variety of futures contracts, which yields δ ≈ 0.5 for small tick contracts and δ ≈ 0.6 for large tick contracts, for Q/V ranging from a few 10⁻⁴ to a few %."
- p. 2: "it is quite remarkable that the square-root impact law appears to hold approximately in all cases."

**Supports claim 3, with empirical data.** The paper reports CFM's own futures metaorders, and its δ is approximate: 0.5–0.6 in its data, 0.4–0.7 across the literature.

**moro2009** — Moro, E., Vicente, J., Moyano, L. G., Gerig, A., Farmer, J. D., Vaglica, G., Lillo, F., Mantegna, R. N. "Market Impact and Trading Profile of Hidden Orders in Stock Markets." *Phys. Rev. E* 80(6):066102, 2009. DOI 10.1103/PhysRevE.80.066102. arXiv:0908.0202.
Read: **FT** (arXiv v1, titled "...of large trading orders in stock markets").

- Abstract: "We find that market impact is strongly concave, approximately increasing as the square root of order size."

**Supports claim 3** for Spanish Stock Market and LSE metaorders, which the paper calls "hidden orders".

**almgren2005** — Almgren, R., Thum, C., Hauptmann, E., Li, H. "Direct Estimation of Equity Market Impact." *Risk*, 2005 (venue as given in the request; not confirmed).
Read: **FT-WP**, preprint dated May 10, 2005 (https://web.archive.org/web/2016/http://www.cims.nyu.edu/~almgren/papers/costestim.pdf).

- Abstract: "We analyse a large data set from the Citigroup US equity trading desks ... We reject the common square-root model for temporary impact as function of trade rate, in favor of a 3/5 power law across the range of order sizes considered."
- §4: "α = 0.891 ± 0.10, δ = 0.267 ± 0.22, β = 0.600 ± 0.038." Then: "At the 95% confidence level, the square-root model β = 1/2 is rejected. We will therefore fix on the temporary cost exponent β = 3/5." Also: "The value α = 1, for linear permanent impact, cannot reliably be rejected."

**Supports a concave exponent of about 0.6, but NOT "square-root".** The authors explicitly reject β = 1/2. Their exponent applies to *temporary impact as a function of trading rate*, not to total metaorder impact as a function of size Q, and they take permanent impact as linear. Cite it as "a related concave estimate (β ≈ 3/5)", not as evidence for the square-root law. Tóth et al. describe it as "Almgren et al. [4] extract δ ≈ 0.6".

**bouchaud2018** — Bouchaud, J.-P., Bonart, J., Donier, J., Gould, M. *Trades, Quotes and Prices*, CUP 2018. DOI 10.1017/9781316659335. Ch. 12, "The Impact of Metaorders", pp. 229–244, DOI 10.1017/9781316659335.018.
Read: **PREVIEW** of ch. 12 (first page, cambridge.org).

- "Naively, it might seem intuitive that the impact of a metaorder should scale linearly in its total size Q. ... Perhaps surprisingly, empirical analysis reveals that in real markets, this scaling is not linear, but rather is approximately square-root. Throughout this chapter, we present this square-root law of impact and discuss several of its important consequences."

**Supports claim 3** as a textbook pointer. I read only the preview, not the chapter body.

---

## 4. Impact decays after a metaorder ends (transient impact, propagator, resilience)

**bouchaud2004** — Bouchaud, J.-P., Gefen, Y., Potters, M., Wyart, M. "Fluctuations and Response in Financial Markets: The Subtle Nature of 'Random' Price Changes." *Quant. Finance* 4(2):176–190, 2004. DOI 10.1080/14697680400000022. arXiv:cond-mat/0307332.
Read: **FT** (arXiv v2).

- Abstract: "We define and study a model where the price, at any instant, is the result of the impact of all past trades, mediated by a non constant 'propagator' in time that describes the response of the market to a single trade."
- §3.1, eq. (12): p_n = Σ_{n'<n} G₀(n − n′) ε_{n′} ln V_{n′} + Σ η_{n′}, "where G₀(.) is the 'bare' impact function (or propagator) of a single trade".
- §3.1: "The only way out of this conundrum is (within the proposed model) that the bare impact function G₀(ℓ) itself should decay with time, in such a way to offset the amplification effect due to the trade correlations."

**Supports "impact is transient (the propagator decays)"** for single trades (Paris Bourse market orders). It is **not** about metaorders or about decay after a metaorder ends. Use it for the propagator/transient-impact idea, not for post-metaorder relaxation.

**obizhaeva2013** — Obizhaeva, A. A., Wang, J. "Optimal Trading Strategy and Supply/Demand Dynamics." *J. Financial Markets* 16(1):1–32, 2013. DOI 10.1016/j.finmar.2012.09.001.
Read: **FT-WP**, NBER WP 11444, June 2005 (https://www.nber.org/papers/w11444). claims_classic.md had this source as abstract-only.

- Introduction: "the speed at which the limit order book rebuilds itself after being hit by a trade, which is also referred to as the resilience of the book, plays a critical role in determining the optimal execution strategy and the cost it saves."
- §3, eqs. (5)–(6): "λx₀ gives the permanent price impact the trade x₀ has ... We assume that the limit order book converges to its steady state exponentially: ... A_t = V_t + s/2 + x₀κe^{−ρt}, κ = 1/q − λ, and ρ ≥ 0 gives the convergence speed ... which measures the 'resilience' of the LOB."

**Supports claim 4** as a model: impact has a permanent part (λ) and a transient part that decays exponentially at rate ρ. It is theoretical, not empirical. Quotes come from the 2005 WP, not the 2013 journal version.

**moro2009** (see above), **FT**:
- Abstract: "as a given order is executed, the impact grows in time according to a power-law; after the order is finished, it reverts to a level of about 0.5 − 0.7 of its value at its peak."
- Conclusion: "the price reverts after the completion of the hidden order in such a way that the permanent impact is equal to roughly 0.5 − 0.7 of the temporary impact."

**Directly supports empirical decay after a metaorder ends.**

**bacry2015impact** — Bacry, E., Iuga, A., Lasnier, M., Lehalle, C.-A. "Market Impacts and the Life Cycle of Investors Orders." *Market Microstructure and Liquidity* 1(2):1550009, 2015. DOI 10.1142/S2382626615500094. arXiv:1412.0217.
Read: **FT** (arXiv v2). Quotes below are from the abstract.

- "we use a database of around 400,000 metaorders issued by investors and electronically traded on European markets in 2010 ... At the intraday scale we confirm a square root temporary impact in the daily participation"
- "The intraday decay seems to exhibit two regimes (though hard to identify precisely): a 'slow' regime right after the execution of the meta-order followed by a faster one. At the daily time scale, we show price moves after a metaorder can be split between realizations of expected returns that have triggered the investing decision and an idiosynchratic impact that slowly decays to zero."

**Supports claim 4 (empirical post-metaorder decay) and claim 3 (square-root temporary impact).**

---

## 5. Predatory trading

**brunnermeier2005** — Brunnermeier, M. K., Pedersen, L. H. "Predatory Trading." *J. Finance* 60(4):1825–1863, 2005. DOI 10.1111/j.1540-6261.2005.00781.x.
Read: **FT** (published version; https://www.princeton.edu/~markus/research/papers/predatory_trading.pdf).

- Abstract: "This paper studies predatory trading, trading that induces and/or exploits the need of other investors to reduce their positions. We show that if one trader needs to sell, others also sell and subsequently buy back the asset. This leads to price overshooting and a reduced liquidation value for the distressed trader."
- Introduction: "We provide a new framework for studying the strategic interaction among large traders who have market impact. ... Some of the traders may end up in financial difficulty, and the resulting need to liquidate is known by the other strategic traders."

**Supports claim 5.** In the paper, the large trader is *distressed* and forced to liquidate, and the predators *know* that need. It is not about any large trader's execution in general.

---

## 6. Single small orders versus metaorders (optional)

**No source read says that a single small order's impact is "small/mechanical" in those words.** Here is what the sources say instead:

- **toth2011** (FT), p. 2: "One should first carefully distinguish the total impact of a given metaorder of size Q from other measures of impact ... One is the immediate impact of an individual market order of size q, which has been studied by various authors and is also strongly concave as a function of q, i.e. q^α with α ≈ 0.2, or even ln q."
- **toth2011** (FT), p. 3: "We believe that the emphasis should rather be placed on the anomalous high impact of small trades." This is *per unit volume* and goes against a "small orders have negligible impact" framing.
- **bouchaud2018** ch. 12 (PREVIEW): "understanding the impact of a single market order is only the first step towards understanding the impact of trading more generally. To develop a more thorough understanding, we must also consider the impact of metaorders."
- **bouchaud2018** ch. 11, "The Impact of Market Orders", pp. 208–228, DOI 10.1017/9781316659335.017 (PREVIEW): "how much of a trade's impact is permanent, how much decays over time, and how does this transient behaviour unfold?"

**What can be supported:** single-order impact and metaorder impact are different quantities, and single-order impact is strongly concave in size (toth2011). "Small/mechanical" is not supported as worded.

---

## Summary

| Claim | Status | Keys |
|---|---|---|
| 1. A&C permanent + temporary impact; cost vs. risk | Verified (FT-WP) | almgren2001 |
| 2. Kyle: linear price in order flow, slope λ | Verified (OCR). Noise traders are part of the setup; "Kyle's lambda" is not Kyle's own name for it | kyle1985 |
| 3. Square-root law | Verified empirically: toth2011 (FT), moro2009 (FT), bacry2015impact (FT); bouchaud2018 (PREVIEW). almgren2005 **rejects** β = 1/2 in favor of 3/5, for temporary impact versus rate | toth2011, moro2009, bacry2015impact, bouchaud2018, almgren2005 |
| 4. Decay after metaorder ends | Empirical: moro2009, bacry2015impact (FT). Model: obizhaeva2013 (FT-WP, resilience). bouchaud2004 is a single-trade propagator, not post-metaorder decay | moro2009, bacry2015impact, obizhaeva2013, bouchaud2004 |
| 5. Predatory trading | Verified (FT). It concerns distressed, forced liquidation that others know about | brunnermeier2005 |
| 6. Small single orders vs metaorders | "Small/mechanical" NOT found. Found instead: the two are distinct, and single-order impact is concave (q^0.2 or ln q) | toth2011, bouchaud2018 |

Unverified fields: almgren2005 venue, volume and pages (no DOI; the preprint gives no venue).
