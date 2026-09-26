# Objections that need the author's decision or the server's data

These three came out of the consistency review of the merged paper (commit
`41b003e` and later). Each contradicts the paper's own tables or code, so they
cannot be fixed by wording alone. Everything else from that review has been
applied in the `.tex`. Line numbers refer to `paper_v2.tex` at the time of the
review and may have drifted by a few lines.

## 1. The headline attribution is contradicted by the paper's own gap-shuffle result

**What the paper claims.** Abstract (L54–55, 68–72), 1.3, 4.2 (L1286–1290), 4.3
(L1827–1831) and the Conclusion (L3773–3796): "the tail is produced by the
self-excitation of the matching engine's transaction stream, and by nothing else
that was tested"; the surplus of near-simultaneous transactions is "itself a
product of the self-excitation, since a renewal stream with the same rate has a
quarter to a seventeenth as many tight gaps".

**What the tables say.**
- 4.2 / `tab:nulls`: at T = 8–16 µs, 79–88 % of the single-stage p99 excess
  survives the gap shuffle (G) — a renewal stream with the real gap marginal
  and **no self-excitation**. At 64–128 µs only 23–37 % survives. So at HFT
  service times most of the tail is reproduced by a process with no ordering
  at all.
- `tab:tx-arms`: TG (transactions intact, idle gaps permuted) behaves the same
  way; TP (transaction times redrawn uniformly) still keeps 25 % at T = 16.
- The comparison in "a renewal stream with the same rate has a quarter to a
  seventeenth as many tight gaps" is to the **Poisson** stream, not to a
  renewal stream: a renewal stream with the empirical gap distribution (that is
  G) has exactly as many tight gaps. As written the sentence is false. (The
  three "renewal" → "Poisson" word fixes have been applied; the logical gap
  remains.)
- The paper's own text elsewhere says the opposite of the headline: 3.2
  (L1076–1082) defines the ordering as "what distinguishes a self-exciting
  process from a renewal process" and says the flat Fano residual is
  "contributed by the gap marginal itself"; 4.8 (L2322–2325) says the tight end
  is "a property of the CME gateway rather than of the market state"; the 4.9
  box (L3657) and the Conclusion (L3830) say the tail is invariant to the
  branching ratio at HFT service times; Appendix E (L4499–4501) says "it cannot
  be read off the fitted parameters".

**What the data support.** At 8–32 µs the tail comes from the heavy left tail of
the (transaction) gap distribution — the surplus of near-simultaneous
transactions — whose origin (gateway spacing, matching-engine processing, or
market feedback) is not identified by any arm that was run. Only the runs
channel at ≥ 64 µs is demonstrably self-excitation.

**Options.**
- (a) Reword the claim everywhere it appears: "the timing of transactions: at
  8–32 µs through the tight end of the gap distribution, whose origin is not
  identified here; at ≥ 64 µs through runs, which is the self-exciting
  component." Then make 4.8, the 4.9 box and Appendix E say the same thing (at
  present they say "gateway", which is one of the candidate origins).
- (b) Keep the claim and add the test that can separate the candidates:
  simulate each window's fitted (μ, α, β) exponential-kernel Hawkes process,
  with and without the 7.5 µs floor, and check whether it reproduces
  `tab:tx-gaps` (20.6 % of gaps below 16 µs) and the survival profile under the
  gap shuffle; and regress each window's tight-gap surplus on its fitted n.
  Appendix E already shows the fitted model over-packs the floor (28–81 %
  against 1 %), which is evidence against (b).
- Decide before touching the abstract again; the abstract's first-question
  answer, 1.3's answer and the Conclusion's "Question 1" paragraph all hinge on
  it, and the subtitle ("How Matching-Engine Transactions Cause Long Tails…")
  survives either way, since both readings are about transaction timing.

### 1a. How the contradiction arose

There is no contradiction in the data. The contradiction is in one inferential step that got written in when the two results were merged.

**What each section shows**

- Section 4.3 (the server's transaction work) shows two things. Transactions are bunched: 20.6% of gaps are under 16 µs against 1.24% for Poisson, and the tight gaps lie between transactions, not inside one. And transaction starts are self-exciting in the Hawkes sense: branching ratio 0.795, count variance 105 times Poisson at one second, runs at 64 µs and above.
- Section 4.2 (the gap shuffle) shows that at 8 to 32 µs the tail needs only the first of those two things. A stream with the same gap distribution and no ordering keeps 79% of the tail at 16 µs. The ordering, which is what a Hawkes fit measures, matters only from 64 µs.

Both are true at once. "Bunched" and "self-exciting" are not the same property. Bunched means many near-simultaneous pairs. Self-exciting means one event raises the probability of the next, which shows up as runs and as excess count variance at long scales. The shuffle keeps the pairs and destroys the runs, and the tail at HFT service times stays.

**Where the contradiction was written in**

The merge joined the two with this sentence, which appears in 3.6, the tx-gaps caption, 4.3 and the Conclusion: the surplus of tight gaps "is itself a product of the self-excitation, since a Poisson stream at the same rate has a quarter to a seventeenth as many". That inference does not hold. Having more tight gaps than Poisson shows the gap distribution is not exponential. It does not show what put the mass there. The gap shuffle itself is a non-Poisson stream with exactly that surplus and no self-excitation at all, and it reproduces the tail. So the evidence for self-excitation (the Hawkes fit, the Fano factor, the runs) and the evidence for what carries the tail at 8 to 32 µs (the tight-gap surplus) point at different properties, and the paper labelled the second with the name of the first.

**What the data say about the origin of the surplus**

Section 4.9 is the one place that bears on it, and it points away from self-excitation. The tight end of the gap distribution is invariant across a 4.7 times range of packet rate and across the branching-ratio quintiles. The p1 gap moves by 0.3%. If a transaction triggering the next within 10 µs were the origin, windows with higher fitted n should have more tight gaps. They do not. The paper's own text there says the tight end is "bounded by the gateway's minimum separation between successive events", which is the alternative origin. No arm that was run can separate gateway spacing, matching-engine output timing, and market feedback as the source of the surplus.

**What that means for objection 1**

The subtitle and the transaction result survive: the tail is produced by the timing of matching-engine transactions, and packetisation, message count and rate are ruled out. What does not survive is the word "self-excitation" as the cause at 8 to 32 µs. The honest statement is the one in option (a) above: at HFT service times the tail comes from the tight end of the transaction gap distribution, whose origin is not identified here; at 64 µs and above it comes from runs, which is the self-exciting component. That is a sentence-level fix in the abstract, 3.6, 4.3 and the Conclusion, plus deleting the "since a Poisson stream has fewer" clause. It has not been made, because it changes the headline.

**Transactions are self-exciting; the question is which slice of that a receiver sees.**
Self-excitation is a property of the process across all timescales. A receiver
with service time T sees one slice of it: the gaps shorter than T, and whether
those gaps come in runs. At 16 µs the tail is a one-gap wait (the transaction
before arrived less than 16 µs earlier); the shuffle keeps every such gap and
79% of the tail stays. At 128 µs the tail is a run wait (ten transactions within
a few hundred microseconds); the shuffle destroys the runs and 77% of the tail
goes. A Hawkes process with an exponential kernel raises the intensity for a
time of order 1/β after each event; if 1/β is of order a millisecond, the
self-excitation produces "more transactions in the next millisecond", which is
the runs and the Fano excess, and says nothing about why two transactions land
8 µs apart rather than 50 µs apart.

That is the whole of it. Transactions are self-exciting, and that self-excitation is what a receiver at 64 µs and above queues behind. At 8 to 32 µs the receiver queues behind near-simultaneous pairs, and the paper has no test that says what makes those pairs. The sentence that got written in during the merge claimed it was the self-excitation, on the grounds that Poisson has fewer tight gaps, and that does not follow.

One fact the paper does not report would settle at which lag the self-excitation acts: the fitted kernel timescale 1/β. The paper reports n = α/β but never β itself. If the corpus-median 1/β is hundreds of microseconds or longer, the fitted self-excitation cannot be what produces 8 µs gaps, and the two-regime statement above is established rather than argued. If 1/β is of order 10 µs, the tight gaps could be part of it and the 4.9 invariance would need another explanation. That number is in the per-window fit results on the server.

### 1b. TODO: measurements that answer "what makes the tight gaps"

All on the server; all from the existing per-window caches and the MBO tapes.

1. **Fitted kernel timescale.** Report the corpus median and 5th–95th range of
   1/β from the per-window exponential-kernel fits, on packets and on
   transaction starts. Add it to 3.2 and to `tab:tx-fano`. This decides whether
   the fitted self-excitation acts at the microsecond scale at all.
2. **Composition of tight-gap pairs.** For every consecutive pair of
   transactions with gap < 16 µs, and separately for pairs with gap in
   [100 µs, 1 ms], tabulate the MDP3 message-type pair (trade → cancel,
   trade → new order, cancel → cancel, …), same/different security, and same/
   opposite side. If tight pairs are dominated by trade-then-cancel/replace on
   the same instrument, the pairs are order-flow reaction at microsecond scale
   (a fast reflexive component, distinct from the fitted kernel). If the
   composition of tight pairs matches that of loose pairs, the pairs are pacing
   (gateway or matching engine) and carry no market information.
3. **Tight-gap share against market variables the fit does not see.** Per
   window: share of gaps < 16 µs against trade rate, cancel rate, number of
   active order ids, and volatility. 4.9 already shows invariance to packet
   rate and to fitted n; this extends it.
4. **Size clustering.** Autocorrelation of transaction message count and of
   packet span at lags 1–100, per window, against the shuffled sequence; and
   the mean span of a transaction conditional on the preceding gap being
   < 16 µs versus > 1 ms. Section 6.6 has the packet-level version of the
   second (weakly negative); the transaction-level version is missing.
5. **Recompute 1–4 under transactTime-keyed grouping** (objection 2), so that a
   result cannot be an artefact of the block definition.

### 1c. Relation to the published Hawkes results on CME futures

The self-excitation picture the paper leans on is the published one, cited in 3.2:

- Filimonov, V., Sornette, D. (2012). Quantifying reflexivity in financial markets:
  toward a prediction of flash crashes. *Physical Review E* 85, 056108
  (arXiv:1201.3572). E-mini S&P 500 mid-price changes, CME, 1998–2010, Hawkes
  fit; attributes the clustering to endogenous feedback ("reflexivity") and
  reports the endogenous share rising to more than 70% of price changes after
  2007, i.e. n > 0.7.
- Hardiman, S. J., Bercot, N., Bouchaud, J.-P. (2013). Critical reflexivity in
  financial markets: a Hawkes process analysis. *European Physical Journal B* 86,
  442 (arXiv:1302.1405). E-mini S&P mid-price changes, 1998–2011; power-law
  kernel, exponent about −1.15 at lags below ~10³ s and −1.45 for 10³–10⁶ s;
  kernel integrates to about one (n ≈ 1, criticality) in every period.
- Filimonov, V., Sornette, D. (2015). Apparent criticality and calibration
  issues in the Hawkes self-excited point process model: application to
  high-frequency financial data. *Quantitative Finance* 15(8), 1293–1314
  (arXiv:1308.6756). Shows that regime shifts in the parameters or in the
  generating process bias n upward, and gives "special care to the decrease of
  quality of the timestamps of tick data due to latency and grouping of messages
  to packets by the stock exchange".

**Does the paper contradict them?** Not on what they measured. Their event
stream is mid-price changes, their kernels are fitted on lags from seconds to
days, and their claim is that most of that activity is endogenous. The paper's
own fit on NQ packets and transactions returns n ≈ 0.8, the same regime, and
the runs and the Fano excess at 1–60 s are exactly the ordering their model
describes. Nothing in the corpus disagrees with them at their scales.

**What the paper adds that they did not measure, and where it cuts against the
reflexive reading.** None of the three papers examines the microsecond end of
the gap distribution. On this corpus that end (the 7.5 µs floor and the 20.6%
of gaps under 16 µs) is:

- invariant to packet rate across a 4.7× range and to the fitted n across its
  quintiles (4.9), whereas a reflexive mechanism operating at that lag would
  make the tight-gap share rise with n;
- reproduced in full by a renewal stream with no self-excitation (4.2, the gap
  shuffle);
- located at the gateway's minimum separation between distinct transactions,
  and made of gaps between different transactions, not inside one (4.3).

So the clustering that carries the receiver's tail at HFT service times is not
the reflexive self-excitation of Filimonov–Sornette and Hardiman–Bercot–Bouchaud;
it is a property of the transaction stream at a scale below what they fitted,
and its origin (gateway pacing, matching-engine output timing, or feedback
faster than 10 µs) is not identified by any arm run here. This is consistent
with, and is microsecond-scale evidence for, the caution in Filimonov–Sornette
2015 that exchange-side timestamp and packet-grouping effects at the shortest
lags contaminate Hawkes estimates: a power-law kernel fitted down to those lags
would absorb this non-reflexive mass into n.

**How significant this is.** It is a finding worth stating, but it is not a
refutation of the published results. The honest framing is: the paper confirms
their near-critical branching ratio on a different instrument and event type,
and shows that it does not extend to the microsecond scale, where the
clustering that matters for a receiver is of a different kind. To claim more
(that their n is inflated by this effect) would need their fits rerun with the
sub-floor structure removed, which the paper has not done and cannot do on
mid-price changes with the data it has.

**If the author wants to make this claim in the paper**, the places are: 3.2
(after the filimonovsornette2015 caveat, one sentence saying the fitted n
describes the ordering at ≥ 64 µs and not the tight end), 4.9 (already says the
tight end is invariant to n; add that this separates it from the reflexive
component), the Conclusion's Question 1 paragraph, and Contribution (1.4) as a
listed contribution. The abstract's "self-excitation" wording then has to
change per option (a).

### 1d. Two hypotheses for what shortens the gaps, checked against the tables

**Hypothesis A: the gaps shorten because one transaction arrives in several
packets.** Under the paper's current grouping the tables refute it:

| statistic | value | where |
|---|---|---|
| transactions that are one packet | 98.3% | tab:tx-counts |
| median gap between packets of a split transaction | 15 µs | tab:tx-gaps |
| gaps below the 7.5 µs floor that are inside one transaction | 0.1% | tab:tx-gaps |
| gaps below 16 µs that are inside one transaction | 5.0% | tab:tx-gaps |
| tail change when each transaction is merged into one packet (TM) | within 15% | tab:tx-arms |

95% of the gaps under 16 µs are between two different transactions, split
transactions are paced at 15 µs (above the floor), and removing the splitting
altogether (TM) leaves the tail. The only way Hypothesis A survives is if the
grouping is wrong, i.e. if what the code counts as two transactions 8 µs apart
is one matching-engine event. **The author's position is that it is not:
transactTime identifies exactly one transaction, no two transactions share a
transactTime, and MDP3 marks the last message of a transaction with the
EndOfEvent bit of MatchEventIndicator.** The L3 records already carry that bit
(`endOfEvent` in `chutil/include/bfile/r_l3.hpp`; `fill_tape.py` reads it,
`qsim_tx.py` does not). Regrouping by EndOfEvent and comparing with the block
grouping (TODO item 6 below) settles Hypothesis A and objection 2 at once. If
the two groupings agree, A is refuted by the table above and the paper's
Main-caveats sentence "whether the matching engine can publish one economic
event under more than one transactTime is not known" is deleted.

**Hypothesis B: the self-excitation is in transaction size, not arrival
time; something makes transactions large repeatedly.** (Rewritten 2026-09-26
after the author's objection; the first version dismissed this on the wrong
grounds. See objection 4 for the full treatment.) What the paper has measured:
the linear service model S = T(1 + r(span − 1)) is already in the simulator
(6.6) and in the TG–TW arms, so the span-dependent tables are the real-life
measurement and the constant-service tables are the null device. On NQ the
span-shuffle control of 6.6 (spans permuted across packets, arrival times
kept, which destroys both size autocorrelation and size-to-burst association)
changes p99 by 0.0% at T ≤ 8 µs and by −1% to +2% above, so on NQ size
clustering does not move the tail. What the paper has not measured: the same
test on an instrument where size is large. NQ has 4% multi-message packets;
ZN trade has 11% of packets carrying 66% of messages and 86% of decode time,
and live mean span rises with queue depth on ZN and falls on NQ. The
instrument where the size dimension dominates the tail is the one on which
the span-shuffle test has never been run, because there is no ZN pcap corpus.
The paper's "the channels do not compound" is an NQ result read as a general
one. Beyond that, size is the only place fast self-excitation can appear,
because packet gaps are censored at the gateway floor (objection 4).

**What the regimes are, on the current evidence.**

- Below the floor (about 7.5 µs): no queueing tail from anything; the only
  tail is in-packet serialisation.
- Floor to about 32 µs: a queueing tail carried by near-simultaneous pairs of
  distinct transactions. Origin not identified.
- From 64 µs: a queueing tail carried by runs, which is the self-excitation the
  Hawkes fit measures.

So the author's summary is right as far as it goes: as service time goes to
zero the transaction self-excitation does not cause the tail; at long service
times it does. What remains open is the middle regime, and the candidates for
what puts two distinct small transactions 8–16 µs apart are (i) order-flow
reaction at microsecond scale (a trade at t; many participants cancel or
replace within 5–20 µs; each a separate transaction), a fast reflexive
component distinct from the millisecond-scale kernel the fit captures, or (ii)
pacing by the gateway or matching engine, carrying no market information. 4.9
argues against the slow kernel (the tight-gap share does not move with n or
rate) but does not rule out a fast component of constant strength. TODO item 2
(composition of tight-gap pairs) separates (i) from (ii).

## 2. "Transaction" in the code is a sendingTime-keyed block, not a matching-engine event

**Author's statement (2026-09-26).** transactTime identifies exactly one
transaction; no two transactions share a transactTime. MDP3 packets carry the
end of a transaction explicitly: the EndOfEvent bit (bit 7) of
MatchEventIndicator is set on the last message of a match event. **Request to
the server team: regroup transactions by EndOfEvent and check the block
grouping against it.** Concretely, per session: number of transactions by
EndOfEvent; number of blocks; number of blocks containing more than one
EndOfEvent (blocks that merged several transactions); number of transactions
spanning more than one block (should be zero); packets per EndOfEvent
transaction; and whether any transactTime group contains more than one
EndOfEvent (which would contradict uniqueness). Then recompute tab:tx-counts,
tab:tx-gaps, tab:tx-fano and the TG–TW arms on the EndOfEvent grouping. If the
numbers match the block numbers to the reported precision, objection 2 closes
and the text only needs to state the grouping and the check. If they differ,
the EndOfEvent numbers replace them.

**What the code does.** `qsim_tx.py` (docstring): packets are grouped into
"transaction BLOCKS: consecutive packets whose transactTime ranges overlap …
A block is one transaction, or a few transactions that share a packet." The
"3.24 × 10⁹ transactions" (L1572, 1590) is the block count; transaction start
times are the **sendingTime of the block's first packet**. transactTime is never
used as a clock for the transaction process.

**Why it matters.**
- The counts only add up for blocks: 3.24e9 blocks × 1.0172 packets/block ≈
  3.296e9 ≈ the packet count, which leaves no room for the "3.0 % of packets
  carry parts of two transactions" (L1573) — that would need ≈ 3.40e9
  memberships. `tab:tx-counts` shows the same thing: the one-message share per
  transaction (95.362 %) is *lower* than per packet (95.897 %), which cannot be
  if 77 % of multi-message packets hold more than one transaction.
- "98.3 % of transactions are one packet" is a statement about blocks.
- Transactions closer together than the gateway's emission interval are merged
  into one block, so the transaction-start gap distribution has the gateway
  floor built in; the "same 7.5 µs edge" (L1719–1722) and the equalities 0.795
  vs 0.798 and "same Fano at every scale" are close to automatic when 98.3 % of
  blocks are single packets. They do not by themselves show the clustering
  "belongs to the market" (L1690–1692).
- Three different samples with two different groupings feed overlapping
  claims: 178.7 M packets (3.1, transactTime-equality groups: 3.18 % shared,
  groups to 66, spread to 51 ms — the 51 ms is implausible for one event and
  suggests ties across distinct events); 20 sessions / 2.45 × 10⁸ packets (6.5,
  `transaction_stats.py --sessions 20`: 3.1 % / 77 %); the full 3.30 × 10⁹
  (4.3, range-overlap blocks: 3.0 %). 3.1 has been rewritten to remove the
  "routinely split" claim and the 178.7 M figure, but the 3.18 % / 66 / 51 ms
  numbers should be recomputed on the full corpus with the block definition or
  dropped.
- `make_spans.py` (L73–76) falls back to sendingTime when transactTime fails to
  parse, mixing clocks in X0/X1, and a transactTime of 0 is not filtered.
  Report how many messages are affected.

**What to do.** State the block definition in 3.3 and name the quantity
"transaction blocks" where that is what is counted; report the exact
transactTime-keyed counts alongside (transactions, packets per transaction,
gaps between distinct transactTimes, Fano and Hawkes n on transactTime);
reconcile 3.0 % / 3.1 % / 77 % with `tab:tx-counts`; state ties, the security
filter and the parse-fallback rate; say why matching-engine output time is not
itself shaped by queueing at the matching engine (which would produce
floor-level spacing without any market feedback).

## 3. The equal-core result is uncaveated where it matters, and the ordering argument is wrong

**Caveat missing.** The abstract (L82–84: "a dispatch arrangement that ignores
ordering matches or beats the tandem on latency, so the tandem's case is that it
keeps the stream in order") and 1.3 (L261–263) state the result with no mention
that **no ingress hop, egress hop or resequencing cost was charged**. 4.4
(L1924–1928) and the Conclusion (L3818–3819) do say it. A costed dispatch needs
at least an ingress and an egress hop, so its median is about T + 2h, which
loses to the tandem's T + h at N = 2.

**Overclaims in 4.4.** "What the table settles is that the tandem's case does
not rest on latency" (L1929) settles nothing of the kind. "A lower bound on what
dispatch could achieve" (L1863) is the wrong way round: it is an optimistic
bound (a lower bound on dispatch's latency).

**The ordering argument is wrong under the model used.** `qsim_run.mdn_latency`
is FIFO, earliest-free server, identical deterministic service: departures leave
in arrival order, so no resequencing wait arises under constant service.
Reordering appears only under span-dependent service. The real obstacle to
dispatch is **state**: order-book application cannot be dispatched, but the
decode stage — which the paper recommends placing first at ingress
(L3739–3747) — is stateless and could be. The recommendation to cut the chain
at the largest stage is therefore not defended against its obvious rival.

**Sections 9–10 never mention dispatch.** "Adding threads along the chain
reduces it at a median cost of (N−1)h" (L3689), "a stage above the floor
should be split" (L3711), design-flow step 3/8, "one cut at a natural boundary
reduces the p99 tail by more than any per-message optimisation" (L3844): a
reader who skips to the recommendations gets the pre-revision message.

**What to do.** Add "(no ingress, hop or resequencing cost charged; an
optimistic bound)" to the abstract and 1.3; remove "settles"; replace the
ordering argument with a statefulness argument; add to 9.2 and to the design
flow: "where the stream must stay in order or the stage carries state; a
stateless stage on a stream that tolerates reordering is served at least as
well by dispatching whole packets to N cores (Section 4.4), at a cost not
charged here." Future work already lists the costed dispatch model as open
(L3921); keep that.

## 4. Self-excitation in size is untested where it matters, and the gap statistics are censored below the floor

**The censoring.** Two transactions closer together than the gateway's
emission interval do not appear as a tight packet gap; they appear as one
packet with span 2 (6.5: 77% of multi-message packets hold more than one
transaction). So any self-excitation faster than about 7.5 µs is invisible to
every gap statistic in the paper (tab:tx-gaps, the p1 gap of 4.9, the gap
shuffle) and shows up only in span, i.e. in message count and transactions per
packet. Three consequences:

- **4.9's invariance is partly automatic.** The p1 gap is the gateway floor,
  so it cannot move with n or rate whatever the market does. The paper reads
  its invariance as "the tight end is a property of the feed, not of market
  state" and, in objection 1, as evidence against a fast reflexive component.
  Both readings are weaker than stated: if a fast component exists, its
  n-dependence sits in span, and nobody has checked whether transactions per
  packet or span per window scale with n or with rate. The 4.9 table has to be
  redone with span columns (mean span, share of packets with span ≥ 2,
  transactions per packet) by rate quintile and by n quintile.
- **The transaction fit is censored the same way.** n = 0.795 on transaction
  starts is a fit on blocks (objection 2), which merge coalesced transactions
  into one event timed by sendingTime. The fast component is removed from that
  stream before the fit sees it. Under EndOfEvent grouping with transactTime
  as the clock, coalesced transactions are separate events at their true
  spacing and the fit (and the Fano factor, and the gap histogram) can see
  them. This is the same regrouping TODO 1 asks for; it should be run with
  transactTime as the clock as well as sendingTime.
- **Span is where the real system pays.** With linear service, a packet of
  span k costs T + (k − 1) r T. On ZN a single packet of 30+ messages is the
  p999, and it opens a queue behind itself (6.7). If large packets cluster in
  time (a burst of coalesced transactions followed by another), the queue a
  large packet opens is not yet drained when the next arrives, and the two
  channels compound. That is exactly what "span rises with queue depth on ZN"
  in the live data suggests, and it is the case Future work leaves open.

**What has been measured on NQ.** The span-shuffle control (6.6): real/
shuffled p99 = 1.000 at T ≤ 8, 0.99–1.02 above. Span against the preceding
gap: correlation −0.014; mean span 1.010 after the tightest decile of gaps
against 1.043 after the loosest. TS (shapes permuted across starts): within
15%. All on NQ, where 96% of packets carry one message. None of it transfers
to ZN.

**Data availability (author, 2026-09-26).** There is no ZN data to run any of
this on now: no ZN pcap corpus, and the live per-message ZN records are not
retained (the paper's ZN numbers come from the report
`tech_reports/serial_vs_parallel_decode.md`, not from raw records). ZN would
have to be recorded first. Items 1, 4 and 5 below are therefore future work.
The paper answers the question on NQ, states that the answer is NQ's, and
comes back to ZN or another instrument later.

**What to measure.**

1. (Future work; needs a ZN recording.) On live ZN records with span and t0
   per message: span autocorrelation at lags 1–100
   against the shuffled sequence; mean span conditional on the preceding gap
   (tight decile vs loose decile); run statistics of span ≥ 2 packets; share
   of p99.9 messages whose packet was preceded within 100 µs by another
   packet of span ≥ 2. This is the ZN version of the 6.6 tests and needs no
   pcap.
2. On the NQ corpus: the 4.9 conditioning table with span columns (mean span,
   share of span ≥ 2, transactions per packet) by rate quintile and by n
   quintile. If span scales with n where the p1 gap does not, the fast
   component is real and the objection-1 wording changes accordingly.
3. On the NQ corpus under EndOfEvent grouping with transactTime as the clock:
   the gap histogram, tab:tx-gaps, the Fano table and the Hawkes fit, so that
   coalesced transactions are seen at their true spacing. Report n and 1/β on
   that stream beside the sendingTime-clock values.
4. (Future work; needs a ZN recording.) A span-aware ZN simulation. Without a ZN pcap corpus the nearest thing is
   the live ZN record stream itself replayed through the span-aware Lindley
   (t0 as arrival, span as size, ZN floor and slope), single stage and
   two-stage, with and without the span shuffle. That gives the compounding
   test on the instrument where it matters, on the data the paper already
   has.
5. (Future work; needs a ZN recording.) A ZN pcap corpus, channel 344, and
   the full Part II sweep on it, span-aware. List it as the first replication
   in Future work, ahead of "ES, BTC".

**Where the paper changes if size clustering is found on ZN.** 6.6's title
and closing paragraph, the Conclusion's "does not compound" sentence, and
Section 10's design rule, which would then need a span term: a stage whose
service is below the floor for span-1 packets but above it for the spans the
feed actually delivers in bursts is above the threshold in practice. 6.3
already shows every live stream crosses the threshold at span 2.

## Also for you (numbers I could not settle from here)

- **"8.29 million messages" (6.1, twice)** against `tab:crossval-span`, which
  sums to 6,810,473. I changed the text to cite the table sum (6.81 M). If
  8.29 M was the admitted-message count and the span table is a subset, say
  what the other 1.48 M are and restore the number.
- **The survival shares are ratios of corpus medians** (`(median(arm)−T)/(median(H)−T)`),
  with no interval and no per-window distribution, and the abstract's "88 %"
  comes from the T = 8 cell whose excess is 0.61 µs, which 4.3 itself calls
  "not meaningful". I changed the abstract to lead with the 16 µs figure. A
  session-clustered bootstrap (276 clusters) on the headline shares would
  answer the obvious referee question.

## Consolidated TODO and what the paper does not yet answer

### TODO (server, in priority order)

1. **Regroup transactions by EndOfEvent** and check the block grouping against
   it (objection 2, request above). Recompute tab:tx-counts, tab:tx-gaps,
   tab:tx-fano and the TG–TW arms on that grouping. Settles objection 2 and
   Hypothesis A of 1d.
2. **Fitted kernel timescale 1/β**, corpus median and 5th–95th range, on
   packets and on transaction starts (1b item 1). Decides whether the fitted
   self-excitation acts at the microsecond scale at all.
3. **Composition of tight-gap pairs** (gap < 16 µs) against loose pairs
   (100 µs–1 ms): message-type pair, same/different security, same/opposite
   side (1b item 2). Separates order-flow reaction from pacing.
4. **Tight-gap share against trade rate, cancel rate, active order ids,
   volatility**, per window (1b item 3).
5. **Size clustering**: autocorrelation of transaction message count and packet
   span; mean span conditional on preceding gap < 16 µs vs > 1 ms, at
   transaction level (1b item 4).
6. **Session-clustered bootstrap** (276 clusters) on the headline survival
   shares of tab:nulls and tab:tx-arms, at least at T = 16, 32, 64, 128 µs.
7. **Costed dispatch arm**: ingress hop, hop to/from N servers, in-order
   resequencer, under constant and span-dependent service (objection 3;
   already listed in Future work).
8. **Numbers to settle**: 8.29 M vs 6.81 M messages in 6.1; the 3.18% / 66 /
   51 ms transactTime-group figures (recompute on the full corpus under
   EndOfEvent grouping or drop); how many messages hit the sendingTime
   fallback in `make_spans.py` and how many have transactTime = 0.
9. **Pre-spinning and pre-warming** measured directly (Section 9 says both are
   predictions, not measurements).
10. **Size clustering on ZN** (objection 4, items 1, 4, 5): future work. No
    ZN data exist now, pcap or per-message; ZN has to be recorded first.
    Listed here so it is not forgotten, not as a task for this revision.
11. **4.9 conditioning table with span columns** by rate and n quintile
    (objection 4, item 2).
12. **Transaction statistics with transactTime as the clock** under EndOfEvent
    grouping, so coalesced transactions are seen at their true spacing
    (objection 4, item 3). Same run as TODO 1, second clock.
13. **Scope every general statement to NQ** (paper edit, no data needed).
    The paper has one instrument, NQ channel 318, and several sentences state
    NQ results as properties of "the feed" or of CME. Each must say: this is
    what we see on NQ; on ZN, where the live measurement shows a different
    span-versus-queue-depth behaviour, it may differ, and that is future work.
    The places, by current line in `paper_v2.tex`:
    - abstract L72 and Conclusion L3828, "two channels whose weight depends on
      the service time": add "on NQ";
    - 6.6 preamble L2954 "the two channels do not compound at the packet
      level", the paragraph at L3098 and its close at L3133: scope to NQ and
      add one sentence that on ZN the live data suggest otherwise and the
      test has not been run;
    - Conclusion L3845 "the threshold is a property of the feed" and the 4.9
      box L2389 "the threshold is a constant of the feed": "of this feed, NQ
      channel 318; measured on one instrument";
    - Appendix E L4533 and L4549 "a property of the feed": same;
    - Future work L3975–3976: already scoped to NQ; add that ZN must be
      recorded (pcap and per-message records) before the compounding test
      can be run, and move it to the top of the list;
    - Main caveats: replace "ES and BTC replications are follow-on work" with
      ZN first, for the reason above.
    Rule for the edit: a result may be called a property of "the feed" only
    where the paper has evidence on more than one instrument (the floor
    crossing at span 2 in 6.3 is on six streams and can stay); everything
    else is "on NQ".

### What the paper does not yet answer, and what it will say after the tests

| open question | where the paper currently stands | answered by |
|---|---|---|
| What puts two distinct transactions 8–16 µs apart | attributed to self-excitation; the attribution does not follow (objection 1) | TODO 2, 3, 4 |
| Is the code's "transaction" a matching-engine transaction | block grouping, unverified against EndOfEvent | TODO 1 |
| Does one transaction arriving in several packets shorten the gaps | refuted under block grouping (1d, Hypothesis A) | TODO 1 confirms or reopens |
| Do transaction sizes cluster in sequence | not measured; cannot explain the constant-service tail (1d, Hypothesis B) | TODO 5 |
| At which lag does the fitted self-excitation act | not reported (n = α/β given, β not) | TODO 2 |
| Uncertainty on the survival shares | none reported; ratios of corpus medians | TODO 6 |
| Does dispatch beat the tandem once its hops and resequencing are charged | uncharged bound only; tandem wins on ordering (objection 3) | TODO 7 |
| Do the queueing and serialisation channels compound on ZN | open (Future work); NQ says no at the packet level; live ZN suggests yes | TODO 10, after ZN is recorded |
| Which results are NQ-only and which are properties of the feed | paper generalises from one instrument | TODO 13 (edit) |
| Does fast self-excitation exist below the floor, hidden in span | not measurable from gaps (censored); never checked in span | TODO 11, 12 |
| Is the microsecond clustering the reflexivity of the published Hawkes work | no on current evidence, stated as a finding not a refutation (1c) | TODO 2, 3 |

**What the paper will say once TODO 1–3 are in**, in the three places that
carry the headline (abstract, Conclusion Question 1, 1.3): the queueing tail
behind a single-threaded receiver is produced by the timing of matching-engine
transactions and by nothing else tested. Below the tight-gap floor there is no
queueing tail. From the floor to about 32 µs the tail is carried by
near-simultaneous pairs of distinct transactions, whose origin is [order-flow
reaction at microsecond scale | gateway or matching-engine pacing], per TODO 3.
From 64 µs it is carried by runs of transactions, which is the self-excitation
the Hawkes fit measures (n = 0.795, kernel timescale 1/β = [TODO 2]). The
message count per packet is the same clustering delivered inside a datagram.
The subtitle stands under every outcome.

## Status after the Conclusion restructure (2026-09-26, server)

**Scope decision (author).** The paper is about queueing and tails in the receiving
software, not about why the market behaves as it does. Why transactions land within
microseconds of each other is stated as an open question and moved to Future work; the
Aquilina–Budish–O'Neill races are named as the leading hypothesis (mechanism (b), common
reaction), without a test.

**Applied, no data needed:**
- Objection 1: the inference "the surplus of tight gaps is itself a product of the
  self-excitation, since Poisson has fewer" is removed from 3.6, 4.2, 4.3 and the
  abstract. The abstract, 1.1, 1.3 and 4.3 now say: the tail is produced by the timing of
  transactions; near-simultaneous pairs carry it at 16–32 µs, runs (the self-exciting
  component) from 64 µs; what places two transactions microseconds apart is not
  identified. "Belongs to the market" wording removed.
- Objection 3: abstract, 1.3, 4.4 and the Conclusion call the dispatch arm an optimistic
  bound with no ingress, egress or resequencing charged; "lower bound" and "settles"
  removed; the ordering argument is replaced by the state argument (FIFO earliest-free
  with constant service releases in order; the order-book stage cannot be dispatched,
  decode could).
- Conclusion split into 11.1 what the paper establishes about the cause (seven numbered
  claims), 11.2 what it does not establish (why transactions land together, with
  mechanisms (a)–(c) and the entry-time data that would separate them; self-excitation at
  HFT service times; censoring below the floor; block grouping vs EndOfEvent; beyond NQ),
  11.3 how to reduce the tail (the split rule, the threshold rule, dispatch with its state
  limit, practical consequences), 11.4 revision of the prior design context, 11.5 caveats.
- Future work split into "why transactions land together" (market questions: entry-time
  data, tight-pair composition, 1/β and power-law kernel, sub-floor excitation in span)
  and "the queue in the receiver" (EndOfEvent regrouping, ZN recording first, costed
  dispatch, bootstrap, closed-form slack, h(ρ), cache pollution, overnight, own pcap).

**Still open (data):** TODO 1 (EndOfEvent regroup), 2 (1/β), 3 (tight-pair composition),
6 (bootstrap), 7 (costed dispatch), 8, 11, 12 above. TODO 13 (scope every general
statement to NQ) is partly applied: the abstract, the Conclusion and Future work are
scoped; 4.9's box, 6.6 and Appendix E still say "the feed" in places.

## Status after the server runs (2026-09-26, later)

**Run and written in:**
- TODO 1 / objection 2 — `msgtape --eoe` extracts the MDP3 end-of-event bit; all 281 NQ tapes
  re-extracted (`tapes/318/message_eoe/`). `eoe_check.py` on 40 sessions: transactTime groups
  straddle or contain several end-of-event flags in 0.002%; 85% of NQ events end on another
  security's message, so the flag cannot key grouping on single-instrument tapes; exact
  transactTime grouping gives 98.36% one-packet transactions (98.30% under blocks), 4.8% more
  transactions than blocks. **Objection 2 closed** (Section 5.2).
- New Section 5 (exchange side): on the engine clock 17.1% of consecutive NQ transactions are
  < 7.5 us apart (14.4–22.9% across 40 sessions), p1 gap 0.18 us; on the publisher clock 1.04%,
  p1 7.46 us. `gateway_delay.py`: sendingTime − transactTime median 99 us, p99 3.6 ms; rises from
  86 us to 912 us median with engine burst intensity, coalescing 4% → 27%. The publisher is a
  queue with ~7.5 us service; the floor is its output spacing; the receiver is stage 2 of a
  tandem starting at the exchange. **Objection 4 (censoring) answered with numbers.**
- TODO 2 — `kernel_timescale.py`: 1/beta median 155–171 us on packets, blocks and engine clock;
  ~4% of kernel mass within 7.5 us. **Objection 1 settled**: self-excitation causes the runs
  (>= 64 us); the 16–32 us pairs are the publisher draining backlogs of near-simultaneous engine
  arrivals, whose origin is the open market question (races, per Aquilina–Budish–O'Neill, stated
  as hypothesis). Author's publisher-as-speed-limit conjecture written as Section 5.7 with
  qualifications.
- TODO 6 — `bootstrap_shares.py`: session bootstrap, all intervals within about ±3 points; in
  captions of tab:nulls and tab:tx-arms.
- TODO 7 / objection 3 — `qsim_dispatch.py`, full corpus: costed dispatch median T + 2h at any N,
  resequencing ≤ 0.14% of packets, p99 wait 0; level with a two-stage cut, better from N = 3.
  **Design rule revised**: cut stateful chains, dispatch stateless stages with ≥ 3 cores
  (tab:dispatch-costed, 4.4, 11.3, design flow).
- TODO 11 — `span_conditioning.py`: span and multi-transaction share do not rise with n
  (Q5/Q1 0.89, 0.99, 0.97). In 4.9 and 11.2.
- TODO 13 — NQ scoping applied to the 4.9 box and 6.6.

**Still running / open:** TODO 3 (tight-pair composition) — running; ZN (TODO 10) blocked on data;
order-entry times (races vs reaction) need data the public feed does not carry.

## Status after the tight-pair and packet-size runs (2026-09-26, latest)

- TODO 3 — `tight_pairs.py`, 40 sessions, 5.12e8 consecutive transaction pairs on the engine clock:
  tight pairs (< 16 us) have the same mix of new orders and deletes as loose pairs (0.1–1 ms); only
  2.0% start with a trade. After a trade, the next is a trade in 11.7% of tight pairs vs 2.2% of
  loose, a delete in 37.4% vs 23.5% — the race signature. Rejected race orders never reach the public
  feed, so the share of racing is not measured. `tab:tight-pairs`, Section 5.6. Security of the
  paired transaction across the channel not measured (open question).
- New `drain_span.py`, 20 sessions, 2.43e8 packets: packets get bigger as the publisher falls behind
  (mean NQ span 1.000 at < 100 us delay, 1.603 at > 5 ms), but packets sent at the 7.5 us floor stay
  single-message even > 5 ms behind (1.114); the floor packets are always adjacent in the channel's
  order/trade packet index. Section 5.5, three tables.
- NQ-filter objection (packet statistics count NQ front-month only; a channel receiver sees every
  packet): stated as open question 1 in the new Future-work subsection "Open questions this paper
  raises", with the measurement (pcap bytes/messages per packet joined on sendingTime; sweep rerun
  with every channel packet as an arrival). Author's decision: leave to a shorter follow-up paper.
- ZN cross-excitation along the yield curve: open question 2, footnote in 5.5, Conclusion "Anything
  beyond NQ".

**Not run (by decision or data):** TODO 4 (tight-gap share vs market variables), TODO 5 at
transaction level (autocorrelation of size), TODO 8 fallback/zero-transactTime counts, TODO 9
(pre-spinning measured), TODO 10 (ZN, no data).

## Objection 1 wording: "why they arrive together" (2026-09-26, author)

Author: transactions do not arrive "together" at the receiver; they are 7.5 us apart. Checked with
`clock_pairs.py` (20 sessions, 2.52e8 consecutive transaction pairs, engine gap vs publisher gap of the
carrying packets): of pairs the engine processed < 1 us apart, 15.7% share a packet and 84.3% arrive in
separate packets (43.0% at 7.5–10 us, 16.1% at 10–16 us, 21.5% beyond 16 us). 64% of the receiver's
7.5–10 us gaps are pairs the engine processed < 7.5 us apart. The paper now says: near-simultaneity is
at the engine's input; the receiver sees trains of packets at the publisher's spacing. The open market
question is why orders reach the engine within a microsecond of each other. New `tab:two-clocks` and
Finding box in 5.3; abstract, 1.3, 3.x, 4.x, 5.6, 11.1, 11.2 and Future work reworded.

## Span and service variability (2026-09-26, author)

Author: the conclusions lost that much of the live tail is the linear decode of long packets, and
the constant-service assumption was not tempered. Applied: abstract, 1.3, 1.x contributions, 3
(simulator: service is constant or span-linear, never random; P-K note), 6.3/6.6 preamble and
title, 6.8 (publisher trains, not gateway coalescing; new "How the live tail forms"), Conclusion
summary + design-rule box (compare upper quantiles of per-packet service with the 7.5 us spacing;
production regime = long packets + slow services, levers are per-message cost and service
spread), claims 1/4/5 scoped by service time, 11.x "not established" (+ service variability,
+ how the ZN far tail divides), caveats, open questions (+ service spread, random-service sweep
and ZN slope, retain live records, in-packet parallel decode — preliminary unvalidated
experiment only). Stage-count result unchanged: a stage cut does not shorten a long packet.
