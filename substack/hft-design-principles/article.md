# Design Principles for HFT Systems

*Arrival clustering, the publisher period, and the conditions under which single-threaded receivers are optimal.*

## 1. Introduction

The conventional architecture for latency-critical market data receivers is a single-threaded event loop. The justification is that inter-thread communication and context switching introduce latency of the order of microseconds, and that any decomposition of the processing chain across threads therefore degrades performance. This principle is widely applied but rarely examined against measurements of the feed it is intended to serve.

In a recent paper, ["Packets, Transactions and Queues: Design Principles for HFT Systems from a Measurement Study of CME Market Data"](https://arxiv.org/abs/2609.32848), I examine the principle empirically. The study uses over one year of packet captures of the CME NQ front-month contract. Each packet is attributed to the matching-engine transaction that generated it by means of the exchange's own timestamps, and the derived statistics are validated against a production receiver operating on the live feed. The analysis shows that the single-threaded rule holds under a precise condition on the receiver's service time, and fails outside it. This article summarises the arrival model, the resulting design principles, and the implementation practices that follow from them.

## 2. Arrival clustering and the Hawkes model

The design principles depend on a property of the feed that is not captured by the Poisson model implicit in standard queueing analysis.

Under a Poisson process, inter-arrival intervals are independent and exponentially distributed, and the occurrence of an arrival carries no information about the timing of subsequent arrivals. The M/D/1 and related models used to size receivers assume this independence.

The measured packet process is instead self-exciting. It is well described by a Hawkes process, in which each arrival transiently increases the conditional intensity of future arrivals. The conditional intensity is

![Hawkes conditional intensity](figs/eq_hawkes.png)

where the baseline rate is constant, the counting process records the past arrivals, and the kernel is a non-negative function that decays with the time elapsed since each arrival. A transaction at the matching engine induces further order submissions, cancellations and replacements, each of which induces more; the resulting arrival process is composed of bursts separated by intervals of low activity. Figure 1 shows the qualitative difference between a Poisson process and a near-critical Hawkes process at the same mean rate.

![Figure 1](figs/rasters_by_fano.png)

*Figure 1. Four simulated arrival processes at the same mean rate of about 20 events per second. From top: a highly regular process, a Poisson process, a moderately self-exciting Hawkes process, and a near-critical Hawkes process. The near-critical process exhibits pronounced bursts and long quiet intervals despite having the same mean rate as the Poisson reference. Adapted from the paper, Figure 32.*

The degree of self-excitation is characterised by the branching ratio, the integral of the kernel:

![Branching ratio](figs/eq_branching.png)

This is the expected number of first-generation offspring arrivals attributable to a single arrival. A branching ratio of zero reduces to a homogeneous Poisson process. A branching ratio of one is the critical regime in which each arrival generates, on average, one further arrival and cascades do not terminate. The NQ feed exhibits a branching ratio of approximately 0.8 to 0.9: subcritical, but close to criticality. For a subcritical Hawkes process the expected total size of the cascade initiated by a single external event is

![Expected cluster size](figs/eq_cluster.png)

which at the measured values corresponds to clusters of roughly five to ten packets.

Clustering is quantified by the Fano factor, the variance-to-mean ratio of arrival counts in bins of fixed width:

![Fano factor](figs/eq_fano.png)

where the count is the number of arrivals in a bin of width τ. For a Poisson process the Fano factor is identically 1 at every bin width. On the NQ feed it is substantially greater than 1 and increases with bin width, the characteristic signature of self-excitation (Figure 2). This behaviour is observed in every measurement window in the corpus. The figure also shows that the packet stream and the transaction stream have nearly identical Fano factors at every bin width, which is the first indication that the packet stream inherits its clustering from the transactions.

![Figure 2](figs/tx_fano.png)

*Figure 2. Fano factor of event counts on the NQ feed, corpus median over 3512 thirty-minute windows, for counting windows from 1 ms to 1 s. Transactions (blue) and packets (black) are nearly identical and rise from about 8 to over 100. Shuffling the idle gaps between transactions (green) removes most of the growth. The Poisson value of 1 is the dashed line. Paper, Figure 7.*

To establish that the observed queueing behaviour is attributable to clustering and not to rate or to some other feature of the data, the real packet stream is rebuilt three ways, each removing one ingredient, and the single-thread receiver is simulated on each. The quantity compared is the tail excess, the p99 latency minus the service time. Figure 3 plots, for each service time, the tail excess on each rebuilt stream as a percentage of the tail excess on the real stream. The three bars are:

1. **Uniform (Poisson), orange.** The same number of packets per window, redrawn uniformly at random over the window. This removes all structure except the mean rate. It retains 0% of the tail at 8 and 16 μs, and 2 to 9% at 32 μs and above.
2. **Gap shuffle, green.** The real inter-arrival gaps, in random order. The distribution of gaps is preserved exactly, including the share of very short gaps, but the order in which they occur is destroyed. It retains 89% of the tail at 8 μs and 79% at 16 μs, falling to 23% at 128 μs.
3. **One-second binned, blue.** The real packet count in each one-second bin, with the packets redrawn uniformly within the bin. This keeps the slow variation in rate from second to second and destroys all microsecond structure. It retains 0% at 8 and 16 μs and 2 to 13% above.

The argument is by elimination. The real stream has a tail; call it 100%. Keep only the mean rate and the tail nearly vanishes, so the rate alone does not produce it. Keep the rate second by second and the tail still nearly vanishes, so slow variation in rate does not produce it either. Keep every real gap but scramble their order and most of the tail survives. What the first two streams remove and the third keeps is the microsecond-scale structure of the arrivals: an excess of very short gaps, and those short gaps occurring in runs. That structure is what clustering means operationally. Figure 3 therefore establishes that the queueing tail is caused by the clustering of arrivals and not by their rate.

The gap shuffle further separates the two components of clustering. At short service times most of the tail is carried by the short gaps themselves: a receiver with an 8 μs service time queues whenever one gap is shorter than 8 μs, and the shuffle keeps every such gap. At long service times a queue needs several short gaps in succession, and the shuffle breaks those runs, so the surviving share falls. Figure 2 shows that this structure is self-exciting and originates at the matching engine; together, Figures 2 and 3 are the case that the tail is clustering. A separate synthetic Hawkes stream at controlled branching ratio, not shown in this figure, reproduces the real tail and is used in the paper to confirm the sensitivity to the branching ratio.

![Figure 3](figs/nulls.png)

*Figure 3. Share of the real stream's single-thread tail excess (p99 minus service time) that survives when the stream is rebuilt three ways: packets redrawn uniformly over the window (orange, Poisson), the real gaps in random order (green, gap shuffle), and packets redrawn uniformly within each one-second bin (blue). The real stream is 100%. Paper, Figure 3.*

A further question is whether the clustering originates at the matching engine or is introduced by the publisher's packetisation. The data exclude the latter. Almost every matching-engine transaction is carried in exactly one packet, so packetisation contributes no additional structure. The clustering is a property of the sequence of matching-engine transactions, which are processed at intervals of microseconds. The publisher then emits these transactions at a bounded rate, which is the subject of the next section.

## 3. The exchange as a queueing system, and the publisher period

The receiver's arrivals are the output of the exchange. The paper treats the exchange as a queueing system observed from outside, through the only window the public feed opens on it: the timestamps it carries. It makes no claim about CME's internal design beyond what the MDP3 specification states. The assumed path is shown in Figure 4.

![Figure 4](figs/exchange_model.png)

*Figure 4. The assumed model of the path from order entry to the receiver's book. Orders reach the matching engine, whose transactions carry the engine timestamp. The publisher packs the resulting messages into packets and stamps each packet at send. The receiver adds a third timestamp at the socket read. Each transaction can therefore be followed through all three systems. Drawn from the paper, Section 5.*

Three systems produce and consume the stream. The matching engine processes orders into transactions; its timestamp marks the start of event processing for each one. The market-data publisher packs the resulting messages into packets and sends them; its timestamp is written into each packet at send. The receiver decodes the packets and applies them to its book, adding its own timestamp at the socket read. The feed does not carry the time an order entered the engine, and nothing in it shows the engine's or the publisher's internal queues directly; both are inferred from the two exchange timestamps. The network between publisher and receiver adds a delay that is close to constant, so the simulations of the paper use the publisher's timestamp as each packet's arrival time.

The publisher behaves as a queue. If it sent events on a fixed schedule, the delay from engine processing to packet send would not depend on how busy the engine is. It does: the median delay is about 100 μs and it grows about tenfold, from 86 μs to 912 μs at the median, as the number of transactions the engine processed in the preceding 50 μs rises from none to between eight and fifteen. The share of transactions sharing a packet with another rises with it, from 4% to 27%. Delay and packing growing together with the engine's load is the signature of a queue with a fixed service time per packet. That service time is the publisher period. Figure 5 shows both quantities against engine load.

![Figure 5](figs/publisher.png)

*Figure 5. The publisher as a queue, pooled over 20 trading days. Left: median delay from engine processing to packet send, by the number of other NQ transactions the engine processed in the preceding 50 μs; it grows about tenfold as the engine gets busier. Right: the share of transactions sent in a packet with another transaction grows from 4% to 27% over the same range. Both rising together is what a queue at the publisher looks like; a fixed publishing schedule would keep the delay flat. Paper, Figure 18.*

The publisher and the receiver are therefore two servers in series, a tandem whose first stage is at the exchange. When a burst at the engine builds a backlog at the publisher, the publisher emits packets back to back at its own period. A receiver whose service time is shorter than that period keeps up and queues nothing; a receiver whose service time is longer falls behind by the difference on every packet of the drain, and its backlog grows for as long as the drain lasts. The service time at which a receiver's tail appears is thus the service time of the exchange stage upstream of it.

While the matching engine processes consecutive transactions within microseconds of one another, the publisher transmits at most one packet per interval of approximately 7.5 μs, measured as the first-percentile gap between packets. Figure 6 shows the same consecutive transactions timed on the engine's clock and on the publisher's clock. On the engine's clock, one gap in six is shorter than the publisher period, and the tightest one percent are 0.18 μs apart. On the publisher's clock, almost none are. During a burst, packets therefore arrive as a train with inter-arrival spacing equal to one period.

![Figure 6](figs/engine_clock.png)

*Figure 6. Share of consecutive NQ transactions closer than 7.5, 16 and 32 μs, on the matching engine's clock (blue) and the publisher's (orange); whiskers span 40 trading days. Below the publisher period the engine has one gap in six and the publisher almost none. Paper, Figure 18.*

This interval, the publisher period, imposes a hard upper bound on the rate at which packets can reach a receiver, independent of market activity. The near-critical burst structure of the engine is preserved in the packet stream, but its temporal resolution is limited to the period. Figure 7 shows the resulting distribution of gaps between consecutive packets, with its sharp lower edge at the period.

![Figure 7](figs/tx_gaps.png)

*Figure 7. Gaps between consecutive packets, pooled over the corpus, on a logarithmic grid. Left: gaps between packets of a single split transaction (blue) against gaps between packets of different transactions (black). Right: the real stream against a Poisson stream at the same rate. The real stream has a hard edge at the publisher period and several times the Poisson share of gaps below 16 and 32 μs. Paper, Figure 8.*

The consequence for receiver design is that the relevant quantity is not the cost of a thread hop in isolation but the relation between the receiver's per-packet service time and the publisher period.

## 4. Design principles

The four principles below follow from the analysis and are each verified by simulation on the measured arrival process and by measurement on the production receiver. Figure 8 is the central result: the single-thread tail as a function of service time, with the publisher period marked.

![Figure 8](figs/onset.png)

*Figure 8. Left: the single-thread p99 latency minus the service time, by service time, corpus median over 3512 windows; the dotted line marks the publisher period and the dashed line the hop cost of one split (1.7 μs). Below the period the tail is exactly zero; above it the tail grows rapidly and exceeds the hop cost between 8 and 9 μs. Right: the share of windows with any tail (blue) and the share in which two stages beat one (orange). Paper, Figure 15.*

**Principle 1. A receiver whose service time is below the publisher period never queues.** If every packet is processed in less than one period, the next packet cannot arrive before service of the current packet is complete, irrespective of the clustering of arrivals. In Figure 8 this is the region left of the dotted line, where no window in the corpus exhibits any tail. In this regime a single thread is optimal, and any decomposition across threads adds inter-stage hop cost without a compensating reduction in queueing delay.

**Principle 2. Above the publisher period, a queueing tail appears that is driven by transaction timing, and a multi-stage receiver can outperform a single-threaded one.** Once the service time exceeds the period, clustered arrivals accumulate in the receive queue. The tail grows considerably faster than the service time: at a service time of 16 μs the 99th percentile latency is approximately twice the service time, and at 32 μs it is four to five times the service time. The Poisson null at the same service times exhibits almost no tail, so the entire tail is attributable to clustering. Decomposing the processing chain into N stages on separate cores, connected by mailboxes, reduces the per-stage service time to approximately T/N. Provided each stage then falls below the period, the reduction in tail latency exceeds the added hop cost. Figure 9 shows median and p99 latency against stage count for each service time.

![Figure 9](figs/tandem_curves.png)

*Figure 9. Corpus-median latency against stage count N for each service time T (one curve per T, labelled at N = 1), with hop cost 1.7 μs. Left: median latency, which rises by one hop per stage. Centre: p99 latency, Hawkes arrivals solid and Poisson null dashed; the Poisson curves are nearly flat, so the whole tail is clustering. Right: the fraction of the single-stage tail excess that survives N stages, against the theoretical bound of 1/N (dotted). Paper, Figure 2.*

**Principle 3. Only the slowest stage determines end-to-end performance.** For a tandem of N stages with constant service times, the paper proves that the system reduces exactly to a single-stage queue with the service time of the bottleneck stage. Optimisation of any other stage has no effect on end-to-end latency. Figure 10 shows the consequence: each halving of the stage length moves the tail excess to the single-thread value at half the service time, and once every stage is below the publisher period the excess is zero. A pool of cores to which packets are sharded performs equivalently to a dedicated tandem; the choice of topology is secondary to the bottleneck.

![Figure 10](figs/delta.png)

*Figure 10. Tail excess, defined as p99 minus p50, for one, two, four and eight stages at each service time, on a logarithmic scale. Each halving of the stage length moves the excess down to the single-thread value at half the service time, because only the largest stage matters. Bars marked 0 are cells in which every stage is below the publisher period. Paper, Figure 14.*

**Principle 4. Near the publisher period, the residual tail is driven by multi-message packets and service-time variance.** For a receiver operating just below the period, a residual tail remains. It arises from packets containing several messages, which are decoded sequentially, and from variation in service time between packets. Figure 11 decomposes the simulated tail excess into the part due to decoding the messages ahead in the same packet and the part due to queueing behind earlier packets. At and below the publisher period, the decode of long packets is the tail. Neither source is addressed by additional threads; the appropriate interventions are reduction of per-message decode cost and reduction of service-time variance.

![Figure 11](figs/span_share.png)

*Figure 11. Decomposition of the simulated p99 excess at each service time into the part due to decoding the messages ahead in the same packet (vermillion) and the part due to queueing behind earlier packets (blue). At and below the publisher period the decode of long packets is the tail. Paper, Figure 29.*

## 5. Implementation practices

The following practices are those used in the production receiver and in the accompanying open-source framework, [Kaspar-HFT](https://github.com/vincent212/kaspar-hft).

- **Core pinning and busy-waiting.** Each thread is pinned to a dedicated core and spins rather than blocking, so that no scheduler involvement occurs on the critical path. Receivers are pre-spun so that latency is stable at the onset of a burst rather than converging during it.
- **Synchronous handoff by default.** Inter-stage transfers are synchronous in-thread calls unless measurement at a particular boundary demonstrates that an asynchronous mailbox reduces end-to-end latency. Asynchronous boundaries are typically confined to the input and output edges of the pipeline.
- **Sequential decoding of multi-message packets on the reader thread.** The messages of a single packet are decoded in order on the thread that received the packet; they are not distributed across stages.
- **Measure-and-flip procedure.** The publisher period, or the equivalent rate bound of the feed in question, is measured directly. The single-threaded service time T is measured. If T is below the period, the receiver is single-threaded and effort is directed at per-message cost. If T exceeds the period, a tandem configuration is evaluated with stages on separate cores. At each pipeline boundary both the synchronous and asynchronous configurations are measured and the lower-latency configuration is retained.
- **Optimisation confined to the bottleneck.** By Principle 3, optimisation effort is directed exclusively at the slowest stage.

Two receiver configurations were measured on the live feed. The two-stage configuration, in which the socket reader hands each datagram to a decoder thread through an asynchronous mailbox, has a socket-to-book service floor of approximately 7 μs. This is not a target; it is the maximum per-packet service time a receiver can sustain before Principle 2 applies, and the two-stage receiver sits at that edge. On every stream measured, a one-message packet is served below that threshold and a two-message packet at or above it.

The single-threaded configuration, in which the socket reader decodes each packet and updates the book itself with no hand-off, is the one the principles recommend, and it is the production configuration. Its socket-to-book latency on the live feed, measured per message from socket read to book publish with Onload and threads not pinned, is given in Table 1. The median on the NQ book stream is 0.8 μs, roughly an order of magnitude below the publisher period. The two-stage receiver's 7 μs floor is therefore not the cost of decoding; it includes the cross-thread hand-off, which is the main structural difference between the two configurations. The two runs were taken separately and differ in other respects as well, so the gap of about 6 μs between them is not all hand-off, and the paper does not decompose it further. The tails that remain, up to 8.8 μs at the 99.9th percentile on NQ and considerably more on the trade streams, are the multi-message-packet tails of Principle 4: the busiest stream, NQ book, has the smallest tail, because the tails order by messages per packet and cost per message rather than by packet rate.

![Table 1](figs/table1.png)

*Table 1. Socket-to-book latency of the single-threaded Kaspar-HFT receiver on six live CME streams, in μs, measured 2026-10-02. Time in the NIC and socket buffer is not included. Paper, Table 25.*


## 6. Moving actors to the FPGA

The tails in Table 1 have a single cause that no CPU architecture removes. A software handler, with or without kernel bypass, processes a packet only after the NIC has delivered it in full, and then decodes its messages sequentially, so its service time grows with the length of the packet. Principle 4 identifies this as the residual tail. The companion report, ["Adapting the Actor Model of Concurrency to Hybrid CPU/FPGA Architectures"](https://github.com/vincent212/kaspar-hft/blob/main/tech_reports/fpga_actors.pdf), removes it by moving the actors on the critical path onto a network card whose port is connected to an FPGA, where a packet is decoded as its bytes arrive.

The actor model carries over unchanged. An FPGA actor is written in C++ in the same form as a CPU actor, with private state, handlers, send and reply, and is compiled to a circuit by high-level synthesis. Each actor runs one step per clock; a send is a write into a hardware FIFO; and across PCIe, CPU and FPGA actors exchange the same 64-byte messages, so neither side's handlers can tell whether a peer is on the card or on the CPU. The code an actor's author writes is the same in both places.

![Figure 12](figs/fpga_handler.png)

*Figure 12. The CME MDP3 handler partitioned between the AMD Alveo X3522PV and the host. Five actors on the card receive the frames, join the multicast group, decode SBE as the words arrive, keep the securities the strategy trades and pack the events into 64-byte lines; one actor on the CPU applies the lines to the books. FPGA report, Figure 10.*

**Which actors move.** The card takes the work that is proportional to bytes on the wire and requires no decision: Ethernet, IP and UDP headers, SBE decoding, filtering to the instruments traded, and packing into lines. The CPU keeps state, decisions and risk: the books, the strategy, and the order manager. The boundary is placed where the least data crosses, since a PCIe crossing costs several hundred nanoseconds against a few cycles between actors on the card. In Figure 12 that is after the filter: every byte of the feed is processed on the card, and only the events of the securities the strategy trades cross the bus. This is the same cost model as the thread-placement principles above, applied across the bus.

![Figure 13](figs/fpga_schedule.png)

*Figure 13. Schedule of one packet. Top: on the card, every stage processes the bytes as they arrive, full lines cross PCIe and are applied while the packet is still on the wire, and after the last byte only a fixed residual remains. Bottom: a software handler starts decoding after the whole packet has been delivered, and its decoding time grows with the packet. FPGA report, Figure 12.*

**Why the tail disappears.** The decoder on the card consumes 8 bytes per clock and finishes each book entry before the next has arrived on the wire, so by the time a packet's last byte lands, everything before it is already decoded and most of it already applied to the books on the CPU. What remains after the last byte is the same for every packet, about 100 ns on the card plus one PCIe crossing, and the strategy has the updated book under half a microsecond later regardless of packet length. Over a full CME session of 30 million packets, the card's work after the last byte never exceeded 8 clocks. That residual is far below the publisher period, so the card never queues during a burst: Principle 1, restated for hardware. The design meets 250 MHz on the card using under 1% of the chip.

![Figure 14](figs/fpga_hybrid.png)

*Figure 14. The full hybrid trading system. Both protocol endpoints, MDP3 in and iLink 3 out, are actors on the card; the books, the strategy, the risk checks and the iLink session are actors on the CPU. The thick arrows cross PCIe. FPGA report, Figure 13.*

**The hybrid system.** The same rule places order entry: the card encodes orders and decodes execution reports, which is per-byte work, and owns the TCP connection; the CPU keeps the session, sequence numbers, order state and risk checks, and the card never originates an order. After the last byte of an incoming packet, no step on the tick-to-trade path grows with that packet, no thread hands a message to another, and the path crosses PCIe once in each direction. Because the card's actors and the software actors they replace exchange the same messages, the same strategy runs unmodified in backtest, on the CPU alone, and with the card. The market-data handler is built, verified and placed and routed; the order-entry half is a proposed design, and the latency figures on the card are estimates until measured on hardware.

## 7. Factors of negligible effect

At service times characteristic of HFT receivers, the following have negligible effect on tail latency: the mean packet rate, the packet size, and the publisher's packetisation strategy. The data also do not support the general claim that avoidance of context switches justifies single-threading. The hop cost is justified whenever the reduction in tail latency exceeds the increase in median latency that the additional hops impose.

## 8. Limitations

The results apply to feeds with self-exciting, Hawkes-type arrival clustering. A single instrument, NQ, was studied in depth; other instruments were examined qualitatively. Heavy-tailed service-time distributions are not fully characterised. The mechanism of certain far-tail events observed on the live receiver is only partially resolved. The question of why the matching engine produces near-critical bursts is outside the scope of the study.

## 9. Conclusion

The appropriate threading architecture for a market data receiver is determined by two measurable quantities: the publisher period of the feed and the receiver's per-packet service time. Below the period, a single thread is optimal. Above it, the processing chain should be decomposed until each stage falls below the period, after which only the bottleneck stage warrants further optimisation. The residual tail that remains on a single thread below the period is the sequential decode of multi-message packets, and it is removed by moving the per-byte work onto an FPGA that decodes each packet as it arrives, with the same partitioning rule applied across the PCIe bus: the card performs the work proportional to bytes on the wire, and the CPU retains state, decisions and risk.

*Papers: "Packets, Transactions and Queues: Design Principles for HFT Systems from a Measurement Study of CME Market Data," arXiv:2609.32848, CC BY 4.0; "Adapting the Actor Model of Concurrency to Hybrid CPU/FPGA Architectures," Kaspar-HFT technical report, November 2026. Figures 1 to 3, 5 to 11 and Table 1 are reproduced from the first, Figures 12 to 14 from the second; Figure 4 is drawn for this article from Section 5 of the first. Kaspar-HFT, including the MDP3 FPGA handler, is open source at [github.com/vincent212/kaspar-hft](https://github.com/vincent212/kaspar-hft).*
