# The Queue Is Inside the Packet

*Market-data latency measured one message at a time: the queue barely matters,
and the thing that does is not random.*

Here is a number from my market-data recorder, measured this afternoon on live
CME multicast: from the socket read to the order book being published,
**8.0 microseconds** at the median.

Here is another number from the same instrument, the same feed, the same minute,
the same line of code: **81.4 microseconds**.

Neither is an outlier. Both are conditional medians — 474 messages and 119
messages — on the ZN trade stream with the ring buffer empty in both cases.

The difference between them is not load, not jitter, not a context switch,
and not congestion. It is one integer — your position inside the UDP datagram
you arrived in — and once you know it, the latency is predictable to within a
microsecond.

This is how I found it, including the **four** things I got wrong on the way,
each of which was the same mistake at a different level of aggregation.

## The setup

Kaspar is my C++20 HFT framework. The piece under measurement here is the
market-data path: read a UDP datagram off CME MDP3 multicast, decode the SBE
messages inside it, apply them to an order book, publish. I instrumented two
timestamps — `t0` when the datagram comes up from the socket, `t1` when the
book update is published — and recorded the difference for every message.

Three instruments, both populations:

- **ES** — E-mini S&P 500
- **NQ** — E-mini Nasdaq 100
- **ZN** — 10-year Treasury Note

**5.87 million messages** across six streams, 2026-09-16, 14:21 to 15:00 ET,
a busy afternoon tape. Every table is cut from that one window.

One caveat up front, because it bounds everything below: `t0` is a *software*
timestamp taken at the socket read. Time spent in the NIC and the kernel before
that is outside this measurement. This is not wire-to-book. It is socket-to-book.

## Attempt one: ask the obvious question

The obvious hypothesis is queueing. Packets arrive, they wait in a ring buffer,
the consumer drains it. If arrivals bunch up, the ring gets deep, and messages
wait. Textbook M/G/1: expected wait goes as ρ/(1−ρ), which is convex and blows
up as utilisation approaches one.

So I recorded `qlen` — the number of packets queued ahead of yours —
alongside the latency, aggregated both into 100 ms bins, and regressed.

It worked beautifully:

```
  ES book    lat =  6.86 + 10.10*q    r2=0.106
  NQ book    lat =  6.47 +  8.24*q    r2=0.303
  ZN book    lat =  7.17 + 16.09*q    r2=0.154
```

Three instruments, three separate multicast channels, three independent fits,
and the intercepts agree to within 0.70 µs. That intercept is the hot path
extrapolated to zero queue: **6.5–7.2 µs**. A slope of 8 to 16 µs per packet
queued ahead. Clean, monotone, publishable.

It is also wrong by a factor of 4 to 7.

## Attempt two: the ecological fallacy

Here is the problem with the regression above. The unit of observation is a
100 ms bin, not a message. The x-axis is the *mean* qlen in the bin and the
y-axis is the *mean* latency in the bin. A slope on that says:

> bins in which messages saw deeper queues had higher mean latency

It does **not** say:

> a message at qlen *d* costs a + b·*d*

Those are different claims, and they come apart whenever something else varies
along with qlen. Something else does: when arrivals burst, the ring gets
deeper **and the packets get bigger**, simultaneously. The bin-level regression
cannot tell those apart, so it assigns the packet-size effect to the queue.

I only found this because I stopped aggregating. The fix was embarrassingly
small — the queue length and the latency were *already in the same CPU register
at the same instant*, in the function that updates the bin counters. I was
folding them into sums and a max and throwing the pairing away. A max and a sum
do not identify a joint distribution. Once 166 messages collapse to
`(qlen_max, l1_sum, n)`, no arithmetic downstream recovers which message was
deep.

So: one 16-byte record per message, `(t1, latency, qlen, idx)`, written to a
side file. 32 KB/s. Then ask the same question again, properly.

## What the queue actually costs

Conditional median latency at each exact integer qlen, over messages:

```
               qlen 0    qlen 1    qlen 2    qlen 3     share at 0
  ES book       7.2us     6.5us     7.2us     8.5us         93.0%
  NQ book       7.4us     6.1us     7.1us     8.4us         87.4%
  ZN book       7.2us     6.3us    10.6us   107.6us         88.1%
  ES trade      9.0us     8.4us        -         -          99.7%
  NQ trade      8.4us     7.8us   100.7us        -          99.7%
  ZN trade     14.7us    32.5us   276.8us   221.1us         95.3%
```

On ES book, going three packets deep costs **1.3 µs**. On five of the six
streams `qlen 1` is *faster* than `qlen 0` — which on its own kills the causal
story at the queue lengths that actually occur.

Side by side with the bin-level fit that looked so good, per packet queued
ahead:

```
                per-message   bin-level   overstated by
  ES book          1.42         10.10          7.1x
  NQ book          1.86          8.24          4.4x
  ZN book          2.96         16.09          5.4x
```

The per-message column is the median rise per packet queued ahead, with position
inside the packet held fixed — the next section is how it is computed and why
the raw version of it cannot be used. The bin-level column is the slope from
the regression above.

The queue contributes **1.4 to 3.0 µs per packet queued** at the per-message
level, against 8 to 16 from the aggregate. And it is multiplied by almost
nothing: there is no ρ/(1−ρ) term doing any work, because ρ is nowhere near 1.
Between 87% and 99.7% of all messages arrive to find the ring completely empty,
so the queue's contribution to the *mean* latency is under half a microsecond.

The intercepts survived. The slopes did not.

Except that this table is wrong too — mildly on ES and NQ, by a factor of forty
on ZN. The next section is why.

## Attempt three: the same trap, one level down

I published the table above and then realised it has the same disease, milder.

Grouping by `qlen` and averaging latency inside each group is only a clean
measure of the queue if nothing *else* varies across those groups. Something
might: a burst that fills the ring may also produce bigger packets, in which
case messages at high `qlen` also sit further inside their own packet, and the
rise I just attributed to the queue is partly position.

The test is the same move again — stop averaging over the other variable. Hold
`idx == 0`, so the message is first in its own packet and no in-packet
serialisation is left in its latency, and vary `qlen`. Whatever still moves is
the queue.

One estimator note first, because it decides what the table says. On a
39-minute window the *mean* latency inside a `qlen` cell is not a measure of
the queue. A cell can hold 38 messages, and one multi-millisecond stall
anywhere in it moves the mean by tens of microseconds. NQ book's mean at
`qlen 0` is 280 µs against a median of 7.4 µs — the mean is describing the
stalls, which are a separate phenomenon with their own section below. Every
table from here on is **medians**. The means are printed alongside so the gap
is visible.

Here is every stream. `m.span` is the average size of the packet those
messages arrived in, and `m.idx` their average position inside it — the
confounder, measured rather than assumed. `med@idx0` is the deconfounded
number: first in your own packet, so only the queue is left.

```
  ESZ6 book        msgs    median    m.span   m.idx      n@idx0    med@idx0
    qlen 0    2,107,611     7.2us      2.14    0.59   1,832,975       7.0us
    qlen 1      147,086     6.5us      1.70    0.35     132,284       6.3us  (-0.7)
    qlen 2       10,632     7.2us      1.38    0.18      10,140       7.1us  (+0.1)
    qlen 3        1,377     8.5us      1.20    0.06       1,322       8.4us  (+1.4)
    qlen 4          294    10.0us      1.17    0.06         280       9.9us  (+2.9)
    qlen 5           83    12.3us      1.49    0.14          79      11.7us  (+4.7)
    qlen 6           38    15.8us      1.18    0.08          36      15.5us  (+8.5)

  NQZ6 book        msgs    median    m.span   m.idx      n@idx0    med@idx0
    qlen 0    2,437,399     7.4us      2.39    0.79   1,906,234       7.2us
    qlen 1      337,911     6.1us      2.12    0.59     269,835       6.0us  (-1.3)
    qlen 2       11,289     7.1us      1.93    0.35       9,988       7.0us  (-0.2)
    qlen 3        1,169     8.4us      1.45    0.10       1,081       8.3us  (+1.1)
    qlen 4          213    11.8us      1.54    0.24         188      11.2us  (+4.0)
    qlen 5           90    16.9us      1.44    0.22          82      16.2us  (+8.9)
    qlen 6           41    17.9us      1.20    0.15          36      17.6us (+10.4)
    qlen 7           47    19.2us      1.57    0.40          37      20.2us (+13.0)

  ZNZ6 book        msgs    median    m.span   m.idx      n@idx0    med@idx0
    qlen 0      820,162     7.2us      3.11    1.11     686,799       6.9us
    qlen 1      104,270     6.3us      3.34    1.19      88,120       6.0us  (-0.9)
    qlen 2        4,115    10.6us     11.90    5.43       2,359       7.6us  (+0.7)
    qlen 3        1,019   107.6us     19.97    9.49         376       9.4us  (+2.5)
    qlen 4          443   131.1us     24.93   11.96         108      11.5us  (+4.6)
    qlen 5          195   258.0us     27.91   13.46          34      21.7us (+14.7)
    qlen 6           67   164.1us     17.99    8.49          29         n/a
    qlen 7          112   194.5us     27.34   13.17          18         n/a

  ESZ6 trade       msgs    median    m.span   m.idx      n@idx0    med@idx0
    qlen 0      224,598     9.0us      8.37    4.18       1,008       7.6us
    qlen 1          760     8.4us      9.53    5.13           7         n/a

  NQZ6 trade       msgs    median    m.span   m.idx      n@idx0    med@idx0
    qlen 0       79,369     8.4us      6.91    3.56         307       8.3us
    qlen 1          205     7.8us      5.41    2.66           3         n/a
    qlen 2           65   100.7us     83.72   41.58           0         n/a

  ZNZ6 trade       msgs    median    m.span   m.idx      n@idx0    med@idx0
    qlen 0       90,810    14.7us     31.13   15.54         491       8.1us
    qlen 1        3,461    32.5us     29.41   14.68          85      73.8us (+65.7)
    qlen 2          574   276.8us     69.42   35.27           7         n/a
    qlen 3           62   221.1us     78.31   51.56           0         n/a
```

Two things in that table, and the first one is the answer to the objection
that started this section.

**The queue is convex.** Read `med@idx0` down each book. ES: 7.0, 6.3, 7.1,
8.4, 9.9, 11.7, 15.5. The successive increments are −0.7, +0.8, +1.3, +1.5,
+1.8, +3.8 — each step costs more than the last. NQ: −1.3, +1.3, +2.2, +2.9,
+5.0. ZN: −0.9, +1.6, +1.8, +2.1, +10.2. Three independent multicast channels,
three convex curves, with the confounder held fixed.

That is what a queue is supposed to look like, and it is the one place in this
whole measurement where the textbook shape actually shows up. It is also
*small*: +8.5 µs at ES `qlen 6`, on 36 messages out of 2.27 million. The shape
is right and the magnitude is negligible, which is the least satisfying
possible combination.

**Now read `m.span`.** It splits the streams into two kinds, and the split is
the opposite of what I expected.

**On ZN, the packets do get bigger as the ring fills.** ZN book goes 3.1 → 3.3 →
11.9 → 20.0 → 24.9 → 27.9 messages per packet across `qlen` 0 to 5. A ninefold
growth. So a message at `qlen 3` is not just behind three packets, it is sitting
around position 9 of a 20-message packet. Both effects land on it at once, and
the naive `qlen` table charges the whole thing to the queue. Deconfounded:

```
  ZN book     raw rise    at idx 0    overstated by
    qlen 1      -0.9        -0.9        (both negative)
    qlen 2      +3.4        +0.7            4.9x
    qlen 3    +100.4        +2.5             40x
    qlen 4    +123.9        +4.6             27x
    qlen 5    +250.8       +14.7             17x
```

ZN book at `qlen 1` is not slower than an empty ring, it is 0.9 µs *faster*.
The headline 108 µs at `qlen 3` is 9.4 µs once you stand at the front of the
packet. Forty times.

**On ES and NQ book, packets get *smaller* as the ring fills** — ES 2.14 down to
1.18, NQ 2.39 down to 1.20. There is no upward confounding to remove, and the
deconfounded column duly tracks the raw one: ES `med@idx0` +0.1/+1.4/+2.9
against raw +0.0/+1.3/+2.8. Those rows were honest all along.

Why the opposite signs? ES and NQ book run fast with a median packet of one
message, so their ring backs up with *many small* packets — being queued there
says the arrival rate is high, not that your packet is fat. ZN is quiet, and
about the only thing that fills its ring is a genuine burst, which makes packets
big at the same time. Same mechanism, different regime. That is a reading of the
pattern, not a separate measurement.

What survives, and it is a better result than the one it replaces: **the queue
has exactly the shape the textbook says, and a magnitude nobody would care
about.** Convex on all three books, and worth under 15 µs at queue lengths that
occur a few dozen times in two million messages. On ZN it is *forty times*
smaller than the naive table said.

The lesson repeats at every level of aggregation. I wrote a section about the
ecological fallacy and then left a milder version of it in the table directly
above — and then a third version of it, using means where the tail made them
meaningless.

## The thing that was actually doing the work

One UDP datagram carries many SBE messages. They are decoded in order. If you
are the 12th message in the packet, you wait for 11 decodes before yours runs,
and that wait is inside your latency.

Call that position `idx`. Now restrict to `qlen == 0` — nothing queued ahead of
your packet — so the *only* remaining wait is inside your own packet. Median
latency at each position:

```
  idx        0     2     4     6     8    10    12    16    20    24    30
  ES book  7.01  7.93  8.97 10.13 11.29 12.37 13.41 16.29 19.27 22.55 26.74
  NQ book  7.24  7.97  8.48  8.94  9.35  9.75 10.32 14.07 17.07 21.67 29.42
  ZN book  6.89  8.10  9.65 11.70 14.54 17.13 19.30 23.14 26.92 30.25 35.01
  ES trade    -  8.30  9.65 11.14 12.62 14.09 15.39 18.10 21.29 24.04 27.85
  NQ trade 8.30  8.72  9.23  9.81 10.36 11.14 12.51 15.12 18.20 21.24 24.56
  ZN trade 7.96  9.14 10.42 12.31 14.55 16.97 19.02 23.14 27.19 31.21 36.91
```

Monotone on every stream, no exceptions. Six streams, 5.7 million messages,
`qlen == 0` throughout.

## Attempt four: the ladder was confounded too

The line above is where I claimed the result was linear, on r² between 0.955
and 0.996, and called it deterministic serialisation. Two things were wrong
with that, and finding them took a longer sample.

**First, the estimator.** That fit was on conditional *means*. On a
39-minute window the mean at a given `idx` is not a per-message cost, it is a
stall detector. Refit the same script on the larger sample and it falls apart:

```
                 pooled fit on MEANS              r2
  ES book    lat =  19.33 +   0.307*idx        0.088
  ES trade   lat =  11.43 +   1.557*idx        0.763
  NQ book    lat = 319.13 + -26.079*idx        0.626
  NQ trade   lat = 111.38 +  -0.006*idx        0.000
  ZN book    lat =  41.98 +  -0.436*idx        0.063
  ZN trade   lat =  13.97 +   1.092*idx        0.959
```

A 319 µs intercept and a *negative* 26 µs-per-message slope. Most of the
messages sit at low `idx`, so most of the stalls land at low `idx` too, and the
fit tilts backwards. Nothing about that describes a decode loop. Refit on
medians and it is clean again:

```
                 fit on MEDIANS                 r2     shape
  ES book    med = 6.98 + 0.566*idx           0.989    straight
  ES trade   med = 6.81 + 0.714*idx           0.999    concave 0.85x
  NQ book    med = 7.23 + 0.312*idx           0.890    convex  2.69x
  NQ trade   med = 7.14 + 0.526*idx           0.971    convex  1.28x
  ZN book    med = 6.83 + 0.966*idx           0.994    concave 0.81x
  ZN trade   med = 6.81 + 0.965*idx           0.998    concave 0.86x
```

Six independent intercepts inside **0.42 µs** — 6.81 to 7.23. That is a
stronger floor result than anything in the original, from six separate
multicast channels with nothing shared but the binary.

The shape column is the honest version of "it is a straight line". Two streams
bend up, three bend down, one is straight. r² does not see any of it: NQ book
is 2.69× convex at r² = 0.890.

**Second, the ladder has the same confound the `qlen` table had.** `idx` 50 can
only occur inside a packet of span ≥ 51. So the high end of the pooled ladder is
built *entirely* from large packets and the low end is dominated by single-message
ones. If large packets cost more per message for a reason unrelated to position
— a different message mix, a book rebuild touching many levels, more cache
footprint — the pooled ladder bends, and the bend is composition, not marginal
cost.

Same disease, third occurrence. Same cure: pick packets of one exact span, and
walk `idx` inside that fixed span. Every point then comes from packets of
identical size.

```
  ESZ6 book    span 24, 58 pkts   lat = 10.60 + 0.517*idx  r2=0.994  straight
               span 22, 67 pkts   lat = 10.68 + 0.615*idx  r2=0.990  concave 0.63x
  ESZ6 trade   span 21, 53 pkts   lat =  9.35 + 0.686*idx  r2=0.990  concave 0.67x
               span 19, 89 pkts   lat =  8.33 + 0.671*idx  r2=0.995  concave 0.82x
               span 18,119 pkts   lat =  9.15 + 0.691*idx  r2=0.990  concave 0.74x
  NQZ6 trade   span 15,124 pkts   lat =  9.16 + 0.372*idx  r2=0.833  concave 0.10x
  ZNZ6 book    span 34,719 pkts   lat = 44.72 + 0.672*idx  r2=0.998  concave 0.81x
  ZNZ6 trade   span 83, 80 pkts   lat = 14.90 + 0.911*idx  r2=0.997  concave 0.80x
               span 21, 62 pkts   lat =  8.18 + 0.815*idx  r2=0.998  straight
```

Eighteen rows across six streams. **Not one is convex.** At fixed packet size
the cost per message is flat or *falling* as the packet is worked — warm-up,
presumably, though I have not measured it. So the convexity in the pooled
ladder was packet-size composition all along.

**What is left is still serialisation, and that is still the answer.** Message
*k* waits for *k* decodes at a roughly fixed cost, the backlog is known the
instant the packet lands, and there is no utilisation term. But "roughly fixed"
is doing real work in that sentence: the per-message cost varies by 3× across
streams, and large packets are intrinsically dearer per message than small ones
for reasons this data cannot name.

## The mean packet size is a lie

```
                packets    messages   mean   p50   p90   p99   p999   max
  ES book     2,123,042   2,415,610   1.19     1     1     7     16     45
  NQ book     2,506,205   3,010,036   1.59     1     2    11     14     36
  ZN book       824,536     965,334   1.25     1     1     6     34     45
  ES trade       83,140     235,601   4.10     3     8    17     41     83
  NQ trade       50,279      83,233   3.47     2     7    16     38     85
  ZN trade       22,864     100,659   5.86     2    11    71     85     85
```

The median book packet carries **one** message. The 99.9th percentile carries
14 to 34. "Mean 1.19" describes neither of them, and it is the only number most
systems report.

And the concentration, which is the whole point:

```
  span >= 10           %packets   %messages   %of total leg-1 time
  ES book                 0.52        4.55            5.34
  NQ book                 1.68        4.87            2.57
  ZN book                 0.47        7.29            7.29
  NQ trade                5.34       19.31           16.16
  ES trade                6.69       26.53           40.10
  ZN trade               11.31       66.18           85.54
```

**On ZN trade, 11.3% of packets carry 66% of the messages and consume 86% of
all processing time. On ES trade, 6.7% of packets consume 40%.**

One correction to the earlier version of this table, which claimed ZN *book* at
"0.40% of packets, 29% of processing time." That number was computed from a sum
of latencies, so the multi-millisecond stalls counted as processing time and
inflated it. On the larger sample ZN book is 7.29%, and NQ book is 2.57% —
*lower* than its message share, because NQ's stalls happen to land on
single-message packets. The trade streams are where the concentration is real,
and there it is very real.

None of that is congestion. It is arithmetic. A 34-message packet costs its last
message 33 × 0.97 µs on top of the floor, every single time, with no randomness
whatsoever.

## Three ways to measure the floor

If the queue and the batch are both removed, what is left is the hot path. I can
isolate it directly: `qlen == 0 AND idx == 0`.

```
  stream     median    n at qlen0 & idx0
  ES book    7.01us    1,781,767
  NQ book    7.24us    1,841,447
  ZN book    6.89us      671,271
  ES trade   7.62us        1,008
  NQ trade   8.30us          298
  ZN trade   7.96us          474
```

Six streams, spread **1.41 µs**; the three book streams, spread **0.35 µs**.
Against the other two methods:

```
  method                                     ES     NQ     ZN
  direct median, qlen==0 and idx==0        7.01   7.24   6.89
  fitted idx==0 intercept (median ladder)  6.98   7.23   6.83
  floor at batch~1 during the FOMC release  7.1    7.5    7.0
```

The third is the interesting one. It was measured during the Fed statement, when
the packet rate rose **6.5× / 4.6× / 9.8×** in the space of a second. The floor
moved by at most +0.4 µs, and *fell* on two of the three.

The hot path is not rate-sensitive. Three methods, three instruments, all inside
6.8–7.5 µs. This is the most robust number in the article: it barely moved when
the sample tripled, when the estimator changed from mean to median, or when the
packet rate went up tenfold.

## A 3× difference that should not exist

Look at the median slopes again:

```
              n at idx 0     slope per message
  NQ book      1,841,447         312 ns
  NQ trade           298         526 ns
  ES book      1,781,767         566 ns
  ES trade         1,008         714 ns
  ZN trade           474         965 ns
  ZN book        671,271         966 ns
```

Same binary. Same decode path. Same machine. Same second. ZN pays **three times
as much per message** as NQ.

An earlier version of this section said eight times, off mean-based slopes of
291 / 732 / 2284 ns. On medians over a larger sample the spread is 3.1×, not 8×.
The ordering is unchanged and the effect is real; the magnitude was inflated by
the same stalls that wrecked the fits.

This also showed up on Fed day from the other direction: ZN book got *faster*
(12.4 → 7.7 µs) at ten times the packet rate. Pre-Fed ZN was slow because it was
idle, and the burst warmed it up.

The natural reading is cache residency — a code path that runs constantly stays
hot in i-cache and d-cache, one that fires a few times a second does not. But ES,
NQ and ZN also differ in book depth, tick structure and message mix, and this
data cannot separate "cold cache" from "ZN messages are genuinely more work."
That needs hardware counters, not procfs. **It is an open question here, not a
finding.**

## Things that are not the answer

**Preemption.** I sampled `/proc/<pid>/task/*/schedstat` field 2 — nanoseconds
the thread was runnable but denied a core — continuously, on the same 100 ms grid
as the latency bins, so it can be read in the exact bin that produced a tail.
During the FOMC burst, voluntary context switches went from 44,028 to 272,190
per 10 seconds, a 6.2× jump. The hot thread's runqueue wait stayed at **0.03%**.

The honest caveat: schedstat measures being *off core*. It cannot see an SMT
sibling stealing issue slots, and there are 65 threads on 8 physical cores here.
Flat runqueue wait rules out preemption. It does not rule out SMT contention.

**Gap recovery.** Zero recoveries in the measured window. Nothing above is a
retransmission wait.

## Where the tail actually comes from

The arrival process is not Poisson, and not merely heavy-tailed:

- gap autocorrelation is positive at all seven lags tested
- the Fano factor collapses **6–19×** under a seeded shuffle that preserves the
  marginal distribution *exactly* and destroys only the ordering — so the
  burstiness lives in the ordering, not in the marginal
- Hurst exponent 0.62–0.70, against a shuffled null of ~0.55
- implied Hawkes branching ratio ≤ 0.88–0.95
- squared coefficient of variation of interarrivals: 13.9 / 17.1 / 28.9, against
  1.0 for Poisson

So arrivals are long-range dependent and self-exciting. Kingman's bound
`E[W] ≈ (ρ/(1−ρ))·((c_a²+c_s²)/2)·E[S]` *understates* here, because the shuffle
proves that correlation contributes beyond what the marginal can explain.

Which closes the loop:

> Self-exciting arrivals cluster. Clustered arrivals produce occasional large
> packets. A large packet costs its last message `slope × idx`, linearly, with
> no randomness at all.
>
> **The tail is burstiness converted into batch size, then converted into
> latency by a straight line.**

The queue in front of the packet — the thing the model would have you focus on —
is convex, exactly as the textbook says, and worth under 15 µs at queue lengths
that occur a few dozen times in two million messages. It has the right shape and
a negligible size.

## One thing I cannot explain

ESZ6 book, 14:01:59.7:

```
  14:01:59.6    176 msgs  batch 1.17  qmax 2    mean  10.1us   max    20.0us
  14:01:59.7     69 msgs  batch 1.19  qmax 1    mean 918.9us   max 12237.1us
  14:01:59.8     92 msgs  batch 1.48  qmax 1    mean   9.3us   max    21.3us
```

A 63 millisecond smear across 69 messages, at qlen 1, in a *quiet* bin,
with clean neighbours on both sides and no recovery. Nothing in the qlen
or the batch size explains it. It is a stall, not a queue.

One bin in roughly 5,000; 69 messages out of 213,538. It moves no mean in this
article. It is the residual the model does not cover, and I would rather print it
than bury it.

## What I would take away

1. **Measure the pair, not the aggregate.** The variables I needed were in the
   same register at the same instant and I averaged them apart. Bin-level
   regression then gave me a slope that was wrong by 4–7× and looked *better*
   than the truth — cleaner, more monotone, three instruments agreeing.
   Aggregation does not just lose precision; it manufactures relationships.

2. **Check the shape before reaching for the model — but check it on a robust
   estimator.** Convex means queueing; linear means serialisation; they call for
   completely different fixes. I got that right and then measured the shape with
   conditional *means* on a tail-heavy distribution, which produced a 319 µs
   intercept and a negative slope. r² was 0.626 while that was happening. r²
   does not detect curvature and it does not detect nonsense.

3. **Report the concentration.** "Mean 5.86 messages per packet" and "11.3% of
   packets consume 86% of all processing time" are the same ZN trade dataset.
   Only one of them tells you where to look.

4. **Hold the other variable fixed, then do it again.** Every wrong answer here
   came from averaging over a variable that moved with the one being studied:
   bins over messages, `qlen` over `idx`, `idx` over packet span. Each fix
   revealed the next instance one level down. I have no reason to think the
   fourth one is the last.

---

## Reproducing

Readers and instrumentation live on the `perf/live-wire-to-book` branch:

```
kaspr/perf/kh_msg.py  [HH:MM:SS]   # per-message qlen, idx, hot path
kaspr/perf/kh_qidx.py [HH:MM:SS]   # qlen with idx held at 0  (attempt three)
kaspr/perf/kh_imed.py [HH:MM:SS]   # idx ladder on medians    (attempt four, a)
kaspr/perf/kh_isp.py  [HH:MM:SS]   # idx ladder at fixed span (attempt four, b)
kaspr/perf/kh_idx.py  [HH:MM:SS]   # the mean-based ladder, kept for contrast
kaspr/perf/kh_pkt.py  [HH:MM:SS]   # packet size distribution and concentration
kaspr/perf/kh_scat.py [HH:MM:SS]   # the bin-level fit, kept for contrast
kaspr/perf/kh_proc.py [HH:MM:SS]   # arrival process: Fano, Hurst, branching
```

`kh_idx.py` and `kh_scat.py` are the two wrong answers. They are still in the
tree because the article is partly about how convincing they look.

Instrumentation is `MsgRec` / `msg_record()` in
`frame/perf/act/LatencyProbe.hpp`. One 16-byte record per admitted message,
emitted at the same point and under the same admission rule as the bin
accumulators, so the two populations are identical and joinable.

## Limits

1. `t0` is a software timestamp at the socket read. NIC and kernel time are
   outside the measurement. Hardware RX timestamps would close that gap and have
   not been done.
2. The cache explanation in the 3× section is inferred, not measured, and is
   confounded with message mix.
3. One box, one session, **39 minutes**. Numbers in the first version of this
   article, taken from a 12-minute cut of the same session, did not all survive
   the longer window — the `idx` slopes moved by up to 2.4× and the
   packet-size time-share moved by 4×. A 39-minute window is not obviously
   enough either.
4. `idx` is a decode position, not a randomised treatment. Large packets differ
   from small ones in message *content*, not only in position, and the
   fixed-span test shows they are intrinsically dearer per message. Holding span
   fixed removes the composition effect from the *shape* of the ladder. It does
   not tell you why a span-34 packet costs more per message than a span-2 one.
5. Medians answer "what does the typical message pay." They deliberately say
   nothing about the tail. The stalls in the section above are real, they are
   the largest latencies in the dataset, and nothing here explains them.
6. The startup window is excluded by a time cut. Startup is a snapshot replay,
   not a latency measurement, and it puts messages at 8 ms and 76 ms into the
   file.
