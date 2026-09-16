# The Queue Is Inside the Packet

*Market-data latency measured one message at a time: the queue is rare but
sharp, the batch is constant and smooth, and neither is random.*

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

**8.29 million messages** across six streams, 2026-09-16, 14:25 to 15:18 ET,
53 minutes of a busy afternoon tape. Every latency table is cut from that one
window, which starts after the startup snapshot replay has finished. The two
arrival-process tables near the end are the exception and say so in place.

One caveat up front, because it bounds everything below: `t0` is a *software*
timestamp taken at the socket read. Time spent in the NIC and the kernel before
that is outside this measurement. This is not wire-to-book. It is socket-to-book.

## The whole distribution, before conditioning on anything

Everything after this section cuts the data by queue length and by packet
position. So here is the unconditional answer first — every admitted message on
each stream, no filter, no grouping.

```
                  messages     mean     p50     p90      p99     p999       max
  ES book        2,861,519    7.6us   7.1us  10.6us   18.5us   41.2us   1148.4us
  NQ book        3,792,033    7.5us   7.3us   9.9us   13.6us   24.7us   5567.0us
  ZN book        1,119,746    9.3us   7.1us  12.1us   57.0us  180.9us   2458.2us
  ES trade         296,736   10.6us   9.0us  15.3us   38.0us  127.7us    421.5us
  NQ trade         110,003    9.4us   8.3us  12.5us   31.2us   83.7us    694.2us
  ZN trade         114,154   30.5us  15.4us  69.3us  219.4us  409.5us    504.8us
```

Three things to take from it, and they set up the rest of the article.

**The median is boring, and that is the point.** The three book streams sit at
7.1, 7.3 and 7.1 µs. Three separate multicast channels, three different
instruments, three different message mixes — and the typical message costs the
same on all of them to within 0.2 µs. Whatever varies in this system, it does
not vary at the median.

**The p99 is where they come apart.** Same three streams: 18.5, 13.6 and
57.0 µs. ZN book's p99 is **4.2× NQ book's** while their medians are within
0.2 µs of each other. Expressed as the amplification from median to p99:

```
                  p99/p50    p999/p50
  NQ book           1.9x        3.4x
  ES book           2.6x        5.8x
  ES trade          4.2x       14.2x
  NQ trade          3.8x       10.1x
  ZN book           8.0x       25.5x
  ZN trade         14.2x       26.6x
```

A single median number for this system — "8 microseconds" — would be true and
would tell you nothing about the thing you actually care about. The rest of the
article is an attempt to say *which* messages are in that right-hand column and
*why*, and the answer turns out to be two mechanisms rather than one.

**The max is not the same phenomenon.** NQ book's maximum is 5.6 ms against a
p999 of 24.7 µs — 225× further out. Nothing in queue length or packet size
explains a jump like that, and I do not explain it here either; it has its own
section near the end. Read the max column as a separate population that happens
to share a file with the others.

Note also that the mean exceeds the median on every single stream, by 1.4× on ZN
book and 2.0× on ZN trade. That gap is the tail pulling the average, and it is
why every table from here on is quantiles rather than means.

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

One estimator note first, because it decides what the table says. The *mean*
latency inside a `qlen` cell is not a measure of the queue. A cell can hold 49
messages, and one multi-millisecond stall anywhere in it moves the mean by tens
of microseconds — on an earlier cut NQ book's mean at `qlen 0` was 280 µs
against a median of 7.4 µs. The mean was describing stalls, which are a
separate phenomenon with their own section below. Every table from here on is
**medians and percentiles**, never means.

The tables below have eight columns and every one of them is load-bearing, so
here is what each means before you read any numbers.

**`qlen`** — the row label. The number of packets already sitting in the ring
buffer, waiting to be decoded, at the instant *this* message's packet was
handed to the consumer. `qlen 0` means the ring was empty and your packet was
picked up immediately. `qlen 3` means three other packets were in front of
yours. This is the treatment variable: the thing the queueing model says should
drive latency.

**`msgs`** — how many messages in the whole sample saw that exact `qlen`. This
is the denominator for the `median` column, and it is the first thing to look
at, because it collapses fast. ES book has 2.6 million messages at `qlen 0` and
49 at `qlen 6`. Any statement about `qlen 6` is a statement about 49 messages.

**`median`, `p90`, `p99`** — the latency distribution of those `msgs`, pooled
over every position inside the packet. This is the *naive* answer: group by
queue length, report the quantiles. These are the columns that are wrong, and
the rest of the table exists to show why. They are wrong in a specific
direction — they are an **upper bound**, because a message at high `qlen` is
also, on some streams, sitting deeper inside a bigger packet, and this column
cannot tell the two apart. Compare them against the `@idx0` columns on the same
row to see how much of the number is queue and how much is position.

**`m.span`** — the mean *packet span* of those messages: how many total SBE
messages were in the UDP datagram each of them arrived in. A packet carrying
one message has span 1; a packet carrying twenty has span 20. This is the
suspected confounder. If `m.span` climbs as `qlen` climbs, then messages deep
in the queue also arrived in fatter packets, and the `median` column is
charging both effects to the queue.

**`m.idx`** — the mean *position* of those messages inside their own packet.
`idx 0` is first to be decoded, `idx 19` is twentieth. This is the confounder
expressed as the quantity that actually costs time: if you are at `idx 9`, nine
decodes ran before yours, and that wait is inside your measured latency.
`m.span` and `m.idx` move together by construction — a bigger packet has more
positions in it — and `m.idx` is roughly `m.span / 2`.

**`n@idx0`** — the subset of `msgs` that were **first in their own packet**.
This is the deconfounded sample. For these messages there is by definition zero
in-packet serialisation, so whatever latency they show above the floor cannot
be position. It is the denominator for the last three columns, and it is
*smaller* than `msgs` — sometimes much smaller, which is the whole problem with
the high-`qlen` rows.

**`med@idx0`** — the median latency of that deconfounded subset. **This is the
answer.** Position held at zero, queue varying: the rise down this column is
the queue and nothing else. The bracketed number is the change from the
`qlen 0` row, so it reads as "what did the queue cost me."

**`p90@idx0`, `p99@idx0`** — the 90th and 99th percentile of the *same*
deconfounded subset. Same messages, same filter, further out in the
distribution. `med@idx0` says what a typical message paid; these say what an
unlucky one paid. They are suppressed with a `-` where `n@idx0` is too small
for the percentile to mean anything — below 200 samples the 99th percentile is
just the largest sample with a decimal point on it, and printing it would look
like a measurement.

A row reads: *of the N messages that saw this queue length, the typical one
paid `median` and the unlucky ones paid `p90`/`p99`; they were sitting at
position `m.idx` of a `m.span`-message packet on average; of the subset that
were first in their packet, the same three numbers are `med@idx0`,
`p90@idx0`, `p99@idx0`.*

The left three quantiles and the right three answer different questions. The
left ones say **what a message at this queue length actually paid** — that is
the operational number, and it includes the packet it happened to arrive in.
The right ones say **what the queue itself cost**, with packet position held
at zero. The gap between them is the confounder.

```
  ESZ6 book       msgs   median       p90       p99  m.span   m.idx      n@idx0        med@idx0  p90@idx0  p99@idx0
    qlen 0     2664628    7.1us     10.4us    17.9us    2.20    0.62     2303948           6.9us      9.8us    13.9us
    qlen 1      181719    6.5us     12.3us    24.2us    1.78    0.39      162136     6.3us (-0.6)     11.4us    17.9us
    qlen 2       12790    7.2us     12.8us    42.2us    1.45    0.21       12175     7.1us (+0.2)     12.3us    22.2us
    qlen 3        1619    8.6us     16.5us    44.6us    1.20    0.06        1547     8.5us (+1.5)     15.7us    35.0us
    qlen 4         376   10.2us     92.8us   309.7us    4.17    1.57         320     9.8us (+2.8)     21.8us    79.2us
    qlen 5         134   15.3us    348.6us         -    9.64    4.26          97    11.5us (+4.6)     27.5us         -
    qlen 6          51   17.0us          -         -    1.18    0.08          48    16.6us (+9.7)         -         -
    qlen 7          90  398.0us    445.2us         -   26.00   12.50          22             n/a         -         -
    qlen 8          41  459.7us          -         -   14.46    6.73          18             n/a         -         -

  NQZ6 book       msgs   median       p90       p99  m.span   m.idx      n@idx0        med@idx0  p90@idx0  p99@idx0
    qlen 0     3310403    7.4us      9.9us    13.5us    2.40    0.80     2586119           7.2us      9.5us    12.4us
    qlen 1      463440    6.1us      9.0us    13.6us    2.11    0.58      371010     5.9us (-1.3)      8.6us    12.9us
    qlen 2       15274    7.0us     11.6us    25.8us    1.92    0.36       13508     6.9us (-0.3)     11.4us    20.5us
    qlen 3        1641    8.6us     17.8us   168.5us    2.82    0.79        1457     8.3us (+1.1)     14.3us    34.3us
    qlen 4         340   12.1us     91.8us   456.4us    3.39    1.17         276    11.2us (+4.0)     27.6us   456.4us
    qlen 5         121   17.7us    384.8us         -    1.39    0.21         109    16.4us (+9.2)    382.0us         -
    qlen 6          72   16.8us    393.5us         -    1.19    0.12          64    16.8us (+9.5)    414.5us         -
    qlen 7          57   19.4us          -         -    1.51    0.35          46    22.8us (+15.5)         -         -
    qlen 8          36   95.4us          -         -    2.06    0.50          29             n/a         -         -
    qlen 9          42   74.0us          -         -    1.95    0.55          34    67.7us (+60.5)         -         -

  ZNZ6 book       msgs   median       p90       p99  m.span   m.idx      n@idx0        med@idx0  p90@idx0  p99@idx0
    qlen 0      988149    7.2us     11.9us    43.9us    3.13    1.12      829350           6.9us     10.2us    15.4us
    qlen 1      124649    6.3us     12.6us    88.8us    3.41    1.22      105162     6.0us (-0.9)      9.4us    17.0us
    qlen 2        4831   10.8us     97.3us   266.6us   12.02    5.49        2740     7.6us (+0.7)     14.2us    72.8us
    qlen 3        1088   84.9us    214.0us   307.4us   18.83    8.92         430     9.4us (+2.5)     27.3us   160.3us
    qlen 4         477  128.2us    228.5us   458.5us   23.33   11.18         130    11.6us (+4.7)     69.4us         -
    qlen 5         206  256.0us    326.8us   334.5us   26.50   12.74          44    21.7us (+14.8)         -         -
    qlen 6          70  163.3us    191.5us         -   17.21    8.11          31    18.3us (+11.4)         -         -
    qlen 7         115  193.4us    342.0us         -   26.65   12.83          21             n/a         -         -
    qlen 8          50  150.2us          -         -   20.56    9.78          12             n/a         -         -

  ESZ6 trade      msgs   median       p90       p99  m.span   m.idx      n@idx0        med@idx0  p90@idx0  p99@idx0
    qlen 0      295337    9.0us     15.2us    34.6us    8.51    4.25        1522           7.9us     11.2us    15.7us
    qlen 1        1242   10.3us    128.7us   202.7us   22.48   11.41          14             n/a         -         -
    qlen 2         129  144.7us    216.8us         -   57.64   28.36           2             n/a         -         -

  NQZ6 trade      msgs   median       p90       p99  m.span   m.idx      n@idx0        med@idx0  p90@idx0  p99@idx0
    qlen 0      109541    8.3us     12.5us    29.6us    7.06    3.63         416           8.2us     10.7us    32.7us
    qlen 1         387    8.5us     74.7us    93.9us   16.07    7.91           5             n/a         -         -
    qlen 2          65  100.7us    139.7us         -   83.72   41.58           0             n/a         -         -

  ZNZ6 trade      msgs   median       p90       p99  m.span   m.idx      n@idx0        med@idx0  p90@idx0  p99@idx0
    qlen 0      108706   15.0us     61.0us   142.4us   32.03   16.00         649           8.4us     62.9us   111.4us
    qlen 1        4213   50.5us    191.2us   285.9us   30.68   15.28         107    76.8us (+68.4)    170.5us         -
    qlen 2         755  219.5us    385.0us   415.2us   71.95   36.29           9             n/a         -         -
    qlen 3         139  234.8us    259.5us         -   68.78   39.65           2             n/a         -         -
```

Three things in that table, and the first one is the answer to the objection
that started this section.

**The queue is convex.** Read `med@idx0` down each book. ES: 6.9, 6.3, 7.1,
8.5, 9.8, 11.5, 16.6. The successive increments are −0.6, +0.8, +1.4, +1.3,
+1.7, +5.1 — each step costs more than the last. NQ: −1.3, +1.0, +1.4, +2.9,
+5.2. ZN: −0.9, +1.6, +1.8, +2.2, +10.1. Three independent multicast channels,
three convex curves, with the confounder held fixed.

That is what a queue is supposed to look like, and it is the one place in this
whole measurement where the textbook shape actually shows up.

**But the convexity is far stronger in the tail than at the median**, and this
is the part I missed in the first two versions of this article. Compare what
three queued packets cost at each quantile:

```
  qlen 0 -> qlen 3, idx held at 0
                          p50      p90      p99
    ES book             1.23x    1.60x    2.52x
    NQ book             1.15x    1.51x    2.77x
    ZN book             1.36x    2.68x   10.41x
```

Monotone in all three instruments. At the median, three packets of queue cost
ES **+1.5 µs** and I was ready to call that negligible. At p99 the same three
packets cost ES **+21 µs** and ZN **+145 µs**.

The same sweep on the *confounded* columns — every message at that `qlen`,
whatever packet position it had — is the number an operator actually
experiences, and it is larger again:

```
  qlen 0 -> qlen 3, pooled over idx
                          p50      p90      p99
    ES book             1.21x    1.59x    2.49x
    NQ book             1.16x    1.80x   12.48x
    ZN book            11.79x   17.98x    7.00x
```

Two things worth saying about that second table. On ES the two versions agree
almost exactly, because ES packets do *not* get bigger with `qlen` — there is
no confounder to remove. On ZN they disagree wildly, because ZN's packets grow
eightfold across these rows, and the pooled number is charging the packet to
the queue. And the ZN p99 column is *smaller* than its p50 column in ratio
terms only because ZN's `qlen 0` p99 is already 43.9 µs — the baseline is
contaminated by big packets too.

So the honest statement is not "the queue is negligible." It is: **the queue
barely moves the typical message and strongly moves the unlucky one.** A median
is a statement about the centre, and I had been reading a centre-statistic as
though it bounded the whole distribution. It does not. If you care about p99 —
and in this business you do — the queue is worth an order of magnitude more
than the median says.

What stays true is the *frequency*: ES book sees `qlen >= 3` on 2,311 messages
out of 2,861,519, which is 0.08%. The queue is a large effect on a rare
event. That is a different claim from "small effect", and it is the one the
data supports.

**Now read `m.span`.** It splits the streams into two kinds, and the split is
the opposite of what I expected.

**On ZN, the packets do get bigger as the ring fills.** ZN book goes 3.13 →
3.41 → 12.02 → 18.83 → 23.33 → 26.50 messages per packet across `qlen` 0 to 5.
An eightfold growth. So a message at `qlen 3` is not just behind three packets,
it is sitting around position 9 of a 19-message packet. Both effects land on it
at once, and the naive `qlen` table charges the whole thing to the queue.
Deconfounded:

```
  ZN book     raw rise    at idx 0    overstated by
    qlen 1      -0.9        -0.9        (both negative)
    qlen 2      +3.6        +0.7            5.1x
    qlen 3     +77.7        +2.5             31x
    qlen 4    +121.0        +4.7             26x
    qlen 5    +248.8       +14.8             17x
```

ZN book at `qlen 1` is not slower than an empty ring, it is 0.9 µs *faster*.
The headline 85 µs at `qlen 3` is 9.4 µs once you stand at the front of the
packet. Thirty-one times.

**On ES and NQ book, packets get *smaller* as the ring fills** — ES 2.20 down
to 1.20 and NQ 2.40 down to 1.19 over the rows that carry real weight. There is
no upward confounding to remove, and the deconfounded column duly tracks the
raw one: ES `med@idx0` +0.2/+1.5 against raw +0.1/+1.5. Those rows were honest
all along.

Two caveats on that claim, because the columns are not as clean as I would
like. ES `m.span` is not monotone: it falls to 1.20 at `qlen 3` and then jumps
to 4.17 and 9.64 at `qlen` 4 and 5. Those two rows hold 376 and 134 messages,
so the jump is a handful of bursts, not a regime. And NQ `qlen 3` sits at span
2.82 against 2.40 at `qlen 0` — a mild rise, not a fall. The clean statement is
that ES and NQ do not show ZN's eightfold packet growth, not that their span
falls monotonically.

Why the opposite signs? ES and NQ book run fast with a median packet of one
message, so their ring backs up with *many small* packets — being queued there
says the arrival rate is high, not that your packet is fat. ZN is quiet, and
about the only thing that fills its ring is a genuine burst, which makes packets
big at the same time. Same mechanism, different regime. That is a reading of the
pattern, not a separate measurement.

What survives, and it is a better result than the one it replaces: **the queue
has exactly the shape the textbook says, on a rare event, with a magnitude that
depends entirely on which quantile you ask about.** Convex on all three books.
Worth +1.5 µs at the median and +21 to +145 µs at p99, at queue lengths that
occur on 0.08% of messages. On ZN the naive table overstated it by *thirty-one
times* at the median — but the naive table was also, by accident, closer to the
right answer for the tail than the median was.

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

Monotone on every stream, no exceptions. Six streams, 7.4 million messages at
`qlen == 0`.

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

Everything above says the same thing: latency is driven by how many messages
arrive at once. So the question becomes what the arrival process actually is.

"Not Poisson" is two separate claims, and they are usually conflated. A Poisson
process needs **both** of these to hold:

1. **the marginal** — interarrival gaps are exponentially distributed
2. **the order** — successive gaps are independent

Both fail here, and they fail for different reasons and with different
consequences.

One sampling note before the numbers, because it differs from every other table
in this article. These two tests read the raw packet arrival stamps from a
separate side file — arrival times only, no latency — and they are **not** cut
to the 14:25–15:18 window. They cover the whole capture from the end of the
startup replay, so `n` is larger here than anywhere else and the rates differ
slightly from the latency tables. Cutting the window would not help: Fano at a
5-second lag needs every second it can get.

### 1. The marginal is not exponential

The only fair null is an exponential with the **same mean**, because then it has
the identical average arrival rate and every difference is shape rather than
level. For `Exp(1/m)` the p-th quantile is exactly `−m·ln(1−p)`.

```
  ESZ6 book   n=4,154,158   mean gap 1793.6us   (rate 557.5/s)
    quantile     measured   Exp(same mean)   ratio
      p10          7.22us       188.97us     0.04x
      p50         74.60us      1243.22us     0.06x
      p90       3196.83us      4129.88us     0.77x
      p99      32182.96us      8259.77us     3.90x
      p99.9   119057.18us     12389.65us     9.61x
```

Read the ratio column. It is **below 1 everywhere up to p90 and above 1 in the
tail**, crossing once. That is not a scale error and it is not heavy-tailedness
on its own — it is the exact signature of a burst process. Most packets arrive
*far closer together* than a Poisson of the same rate would put them, and that
is paid for by rare, very long idles.

The single number that matters for a ring buffer is the share of packets that
arrive nearly on top of the one before. For an exponential this is a constant:
`P(gap < mean/10) = 1 − e^(−0.1) = 9.52%`, whatever the rate.

```
                      CV     CV^2    P(gap < mean/10)   vs Poisson 9.52%
  ES book           10.97   120.4        73.94%              7.8x
  NQ book           28.97   839.1        63.11%              6.6x
  ZN book           13.60   184.9        84.82%              8.9x
  ES trade           4.10    16.8        56.01%              5.9x
  NQ trade           4.78    22.8        39.97%              4.2x
  ZN trade           6.11    37.4        84.20%              8.8x
```

On ZN book, **85% of packets arrive within one tenth of a mean gap of their
predecessor.** Poisson says 9.5%. Those are the back-to-back runs that fill the
ring and fatten the packet, and a Poisson source at the identical average rate
would essentially never deliver them.

An earlier version of this article reported the middle column as `CV²` when the
figures were in fact `CV`. Both are printed above with their definitions,
because it is exactly the quantity people quote squared by mistake, and I did.

The cleanest way to state the consequence: the decoder never experiences the
average rate.

```
                mean rate   rate implied by the median gap   ratio
  ES book         557.5/s            13,405/s                24.1x
  NQ book         806.0/s            11,555/s                14.3x
  ZN book         240.9/s            29,028/s               120.5x
```

For an exponential that ratio is `1/ln 2 = 1.44×`, always. ZN book is quoted at
241 packets per second and the typical back-to-back pair arrives at an
instantaneous rate of twenty-nine thousand.

### 2. The order is not independent either

The marginal alone would still allow a *renewal* process — heavy-tailed gaps
drawn independently. It is not that either. The control is a seeded
Fisher–Yates shuffle of the gap sequence, which preserves the marginal
**exactly** and destroys only the ordering. If burstiness lived in the marginal,
the shuffle would change nothing.

```
                  gap ACF L1   Fano(5s)   shuffled   collapse    H      H shuffled
  ES book           +0.040       1024.2      55.3      18.5x    0.743     0.622
  NQ book           +0.005       1260.0     103.1      12.2x    0.702     0.660
  ZN book           +0.052       1133.5      69.4      16.3x    0.711     0.624
  ES trade          +0.131         65.6      12.8       5.1x    0.680     0.604
  NQ trade          +0.065         44.3       7.1       6.2x    0.695     0.600
  ZN trade          +0.284         73.0      25.7       2.8x    0.643     0.615
```

Gap autocorrelation is positive at **all seven lags tested on all six streams**
— 42 of 42 outside the white-noise band. Long gaps follow long gaps; short
follow short.

The Fano factor — variance of the count in a window over its mean — is 1.0 for
Poisson at every window size. Here it reaches 1024–1260 on the book streams at a
5-second window, and **collapses 2.8× to 18.5× under the shuffle**. That
collapse is the result: the burstiness lives in the *ordering*, not in the
marginal, because the shuffle left the marginal untouched and still removed most
of it.

Fano scaling gives `Fano(T) ~ T^(2H−1)`. Measured `H` is 0.643–0.743 against a
shuffled null of 0.600–0.660, and 0.5 for Poisson or any renewal process. If the
generating process is Hawkes, the Fano ceiling implies a branching ratio
`n ≤ 1 − 1/√Fano` of **0.850 to 0.972** — each arrival triggering close to one
further arrival on average, which is a system sitting just under criticality.

Two honest caveats. The shuffle is not a control for a slowly drifting rate,
which shuffling also destroys; this sample cannot separate self-excitation from
non-stationarity, and both read as clustering. And the shuffled null is
**0.60–0.66, not 0.5** — finite-sample bias in the Hurst estimator at these
window counts — so the effect is the *gap* between 0.74 and 0.62, not the
distance from 0.5.

### 3. Why this lengthens the tail beyond what Poisson would predict

The mechanism is mechanical, and it is the reason the whole article ends up
being about packet size rather than about queueing theory.

> Clustered arrivals mean several packets land inside one decode interval.
> CME coalesces what it can into a single datagram, so clustering converts
> directly into **span** — more SBE messages per UDP packet. And span converts
> into latency by a straight line: message *k* pays `k × slope`, measured at
> 0.31–0.97 µs per message, with no randomness at all.
>
> **The tail is burstiness converted into batch size, then converted into
> latency by multiplication.**

A Poisson source at 557 packets/second would put 1.79 ms between packets and
deliver a span-1 packet essentially every time. The measured stream delivers a
74 µs median gap and span-45 packets, and the last message in one of those pays
44 × 0.57 = **25 µs of pure serialisation** on top of a 7 µs floor.

This is also why the standard queueing correction is not enough. Kingman's
approximation `E[W] ≈ (ρ/(1−ρ))·((c_a²+c_s²)/2)·E[S]` takes the burstiness in
through `c_a²` — the squared CV of the marginal — and so it *understates* here
in two independent ways. First, the shuffle proves that correlation contributes
beyond anything the marginal can explain, and `c_a²` cannot see ordering.
Second, `ρ` is nowhere near 1 on this box: 87–99.7% of messages find the ring
empty, so the `ρ/(1−ρ)` term is doing almost no work. The queue term is small
*and* the variability term is blind to the thing that actually causes the
problem.

And the queue in front of the packet — the thing the model would have you focus
on — is convex, exactly as the textbook says. It has the right shape, it fires on
0.08% of messages, and when it fires it costs +1.5 µs at the median and +21 to
+145 µs at p99. It is a rare, sharp effect sitting on top of a common, smooth
one. The batch explains where the bulk of the tail mass comes from; the queue
explains why the far tail is worse than the batch alone predicts.

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

5. **The estimator is a modelling choice, and one estimator is not enough.**
   Means got destroyed by stalls, so I switched to medians — and then read the
   median as though it described the distribution. It describes the centre. The
   queue looks negligible at p50 and looks like a 2.5–10× amplifier at p99, and
   both of those are the same data with the same filter. Report a quantile
   sweep, not a point. And carry `n` next to every quantile, because the number
   of samples needed to estimate p99 is two orders of magnitude larger than the
   number needed for p50 — most of the interesting cells here cannot support a
   p99 at all, and the honest thing is to leave the cell blank.

---

## Reproducing

Readers and instrumentation live on the `perf/live-wire-to-book` branch:

```
kaspr/perf/kh_msg.py  [HH:MM:SS]   # per-message qlen, idx, hot path
kaspr/perf/kh_qidx.py [HH:MM:SS]   # qlen with idx held at 0  (attempt three)
kaspr/perf/kh_qtail.py [HH:MM:SS]  # p90/p99/p999 of those same cells, with n
kaspr/perf/kh_imed.py [HH:MM:SS]   # idx ladder on medians    (attempt four, a)
kaspr/perf/kh_isp.py  [HH:MM:SS]   # idx ladder at fixed span (attempt four, b)
kaspr/perf/kh_idx.py  [HH:MM:SS]   # the mean-based ladder, kept for contrast
kaspr/perf/kh_pkt.py  [HH:MM:SS]   # packet size distribution and concentration
kaspr/perf/kh_scat.py [HH:MM:SS]   # the bin-level fit, kept for contrast
kaspr/perf/kh_proc.py [HH:MM:SS]   # arrival ORDER: ACF, Fano, shuffle, Hurst
kaspr/perf/kh_gap.py               # arrival MARGINAL: gaps vs Exp(same mean)
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
3. One box, one session, **53 minutes**. Numbers in the first version of this
   article, taken from a 12-minute cut of the same session, did not all survive
   the longer window — the `idx` slopes moved by up to 2.4× and the
   packet-size time-share moved by 4×. A 53-minute window is not obviously
   enough either.
4. `idx` is a decode position, not a randomised treatment. Large packets differ
   from small ones in message *content*, not only in position, and the
   fixed-span test shows they are intrinsically dearer per message. Holding span
   fixed removes the composition effect from the *shape* of the ladder. It does
   not tell you why a span-34 packet costs more per message than a span-2 one.
5. The p90/p99 columns are sound only where `n@idx0` is large. Below 200
   samples the 99th percentile is the largest sample or the one beneath it, so
   it is suppressed rather than printed. That means the tail of the *deep*
   queue rows — `qlen` 5 and above — is not measured at all here. The p99
   amplification result rests on `qlen` 0 to 3 on the three book streams, where
   the cells hold 1,400 messages or more.
6. p999 is not reported anywhere in this article. At `qlen 3` it would rest on
   one or two samples. An earlier draft of this analysis quoted p999 figures of
   67 ms that turned out to be the startup snapshot replay leaking past a
   missing time cut — they were flat across every `qlen`, which is what gave it
   away, since a queue effect that does not vary with queue length is not a
   queue effect.
7. The stalls in the section above are real, they are the largest latencies in
   the dataset, and nothing here explains them.
8. The startup window is excluded by a time cut. Startup is a snapshot replay,
   not a latency measurement, and it puts messages at 8 ms and 76 ms into the
   file.
9. The arrival-process section runs on the uncut capture, so it spans a regime
   change the latency tables do not — a quiet stretch and a busy one pooled
   together read as clustering whether or not the process is self-exciting. The
   shuffle control does not separate those, and neither does anything else here.
   Splitting the sample by time and re-running is the obvious next test and has
   not been done. Until it is, "self-exciting" is the *reading*, and "not
   Poisson in either the marginal or the ordering" is the measurement.
