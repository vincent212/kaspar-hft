# The Queue Is Inside the Packet

*Market-data latency measured one message at a time: what governs it is not
the queue, and it is not random.*

Here is a number from my market-data recorder, measured this afternoon on live
CME multicast: from the socket read to the order book being published,
**7.0 microseconds** at the median.

Here is another number from the same instrument, the same minute, the same line
of code: **79.1 microseconds**.

Neither is an outlier. Both are conditional means over tens of thousands of
messages. The difference between them is not load, not jitter, not a context
switch, and not a queue in any sense a queueing theorist would recognise. It is
one integer — and once you know that integer, the latency is predictable to
within a few hundred nanoseconds.

This is how I found it, including the two ways I got it wrong first.

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

**971,477 messages**, 2026-09-16, a busy afternoon tape.

One caveat up front, because it bounds everything below: `t0` is a *software*
timestamp taken at the socket read. Time spent in the NIC and the kernel before
that is outside this measurement. This is not wire-to-book. It is socket-to-book.

## Attempt one: ask the obvious question

The obvious hypothesis is queueing. Packets arrive, they wait in a ring buffer,
the consumer drains it. If arrivals bunch up, the ring gets deep, and messages
wait. Textbook M/G/1: expected wait goes as ρ/(1−ρ), which is convex and blows
up as utilisation approaches one.

So I recorded the ring depth — `qlen`, the number of packets ahead of yours —
alongside the latency, aggregated both into 100 ms bins, and regressed.

It worked beautifully:

```
  ES book    lat =  6.73 + 10.19*q
  NQ book    lat =  6.84 +  4.59*q
  ZN book    lat =  6.18 + 13.66*q
```

Three instruments, three separate multicast channels, three independent fits,
and the intercepts agree to within 0.66 µs. That intercept is the hot path
extrapolated to zero queue: **6.2–6.8 µs**. A slope of 10 µs per unit of queue
depth. Clean, monotone, publishable.

It is also wrong by a factor of 6 to 17.

## Attempt two: the ecological fallacy

Here is the problem with the regression above. The unit of observation is a
100 ms bin, not a message. The x-axis is the *mean* depth in the bin and the
y-axis is the *mean* latency in the bin. A slope on that says:

> bins in which messages saw deeper queues had higher mean latency

It does **not** say:

> a message at depth *d* costs a + b·*d*

Those are different claims, and they come apart whenever something else varies
along with depth. Something else does: when arrivals burst, the ring gets
deeper **and the packets get bigger**, simultaneously. The bin-level regression
cannot tell those apart, so it assigns the packet-size effect to the queue.

I only found this because I stopped aggregating. The fix was embarrassingly
small — the queue depth and the latency were *already in the same CPU register
at the same instant*, in the function that updates the bin counters. I was
folding them into sums and a max and throwing the pairing away. A max and a sum
do not identify a joint distribution. Once 166 messages collapse to
`(qlen_max, l1_sum, n)`, no arithmetic downstream recovers which message was
deep.

So: one 16-byte record per message, `(t1, latency, qlen, idx)`, written to a
side file. 32 KB/s. Then ask the same question again, properly.

## What the queue actually costs

Conditional mean latency at each exact integer depth, over messages:

```
              depth 0   depth 1   depth 2   depth 3      at depth 0
  ES book       7.6us     7.9us     8.6us     8.8us         93.2%
  NQ book       7.2us     6.2us     7.8us    25.6us         88.1%
  ZN book       8.2us     8.5us        -         -          ~95%
  ES trade      9.9us        -         -         -          99.6%
  NQ trade      9.3us        -         -         -          99.8%
  ZN trade     29.7us   111.7us        -         -          95.3%
```

On ES book, going three packets deep costs **1.2 µs**. On NQ book, depth 1 is
*faster* than depth 0 — which on its own kills the causal story at the depths
that actually occur.

Per-message, the slope and its real contribution:

```
  ES book    lat = 7.59 + 0.37*q    mean q 0.073   queue term 0.03us of 7.62us
  NQ book    lat = 6.94 + 1.47*q    mean q 0.124   queue term 0.18us of 7.13us
  ZN book    lat = 8.19 + 3.79*q    mean q 0.094   queue term 0.36us of 8.55us
```

Side by side with the bin-level fit that looked so good:

```
              per-message   bin-level   overstated by
  ES book        0.37         6.33          17x
  NQ book        1.47        11.44           8x
  ZN book        3.79        22.11           6x
```

The queue contributes **under half a microsecond**. There is no ρ/(1−ρ) term
because ρ is nowhere near 1: between 88% and 99.8% of all messages arrive to
find the ring completely empty.

The intercepts survived. The slopes did not.

## The thing that was actually doing the work

One UDP datagram carries many SBE messages. They are decoded in order. If you
are the 12th message in the packet, you wait for 11 decodes before yours runs,
and that wait is inside your latency.

Call that position `idx`. Now restrict to `qlen == 0` — nothing queued ahead of
your packet — so the *only* remaining wait is inside your own packet:

```
  idx        0     1     2     3     4     5     6     7     8     9   10+
  ES book  7.3   7.7   8.3   8.9   9.6  10.3  11.1  12.0  12.6  13.0  18.2
  NQ book  7.0   7.4   7.8   8.0   8.4   8.7   8.8   9.1   9.4   9.5  11.1
  ZN book  6.7   8.3   9.7  12.2  14.7  17.4  19.9  21.8  24.1  28.0  41.0
  ES trade   -   7.5   8.6   9.3  10.0  10.6  11.4  12.0  12.9  13.6  19.8
  NQ trade   -   7.6   8.7   9.0   9.7  10.2  11.0  10.1  10.2  10.9  16.9
  ZN trade   -   7.5   9.5  11.4  12.3  14.4  16.3  17.8  18.8  20.0  50.3
```

Monotone on every stream, no exceptions.

## It is a straight line, and that matters

If this were queueing, the curve would be convex. It is not. It is linear, on
all six streams, with r² between 0.955 and 0.996.

ZN book is the cleanest case — 34 points, at least 43 messages each, spanning a
tenfold rise:

```
  idx       msgs      mean    linear     resid
  0        53329     7.10us     7.10us    -0.00us
  5          329    18.90us    18.52us    +0.38us
  10         134    33.71us    29.94us    +3.77us
  15         101    42.26us    41.36us    +0.89us
  20          76    52.16us    52.79us    -0.62us
  25          57    63.54us    64.21us    -0.67us
  30          46    74.40us    75.63us    -1.23us
  33          43    79.10us    82.48us    -3.39us

  slope over first half 2.255, over second half 2.104
  max |residual| 4.62us against a 72.00us rise
```

That is the 7 µs and the 79 µs from the opening. Same instrument, same minute,
same code. The only difference is where in the packet you landed.

**This is deterministic serialisation, not queueing.** The backlog is completely
known the instant the packet arrives. Message *k* waits for *k* decodes at a
fixed cost each, and the slope of that line *is* the per-message cost. There is
no utilisation term and nothing to explode. It is D/D/1, not M/G/1.

## The mean packet size is a lie

```
                packets    messages   mean   p50   p99   p999   max
  ES book       301,492     339,643   1.13     1     6     15     45
  NQ book       389,018     455,958   1.17     1    10     13     36
  ZN book       119,231     137,101   1.15     1     5     33     36
  ES trade        9,484      27,858   2.94     3    17     37     70
  NQ trade        5,378       8,920   1.66     2    16     32     61
  ZN trade        1,995       8,977   4.50     2    67     85     85
```

The median book packet carries **one** message. The 99.9th percentile carries
13 to 33. "Mean 1.15" describes neither of them, and it is the only number most
systems report.

Break ZN book down by packet size:

```
  span           pkts    %pkts    msgs in    %msgs   mean lat
  1            110853   92.97%     110853   80.85%       7.1us
  2              3740    3.14%       6773    4.94%       7.3us
  3-4            2953    2.48%       6101    4.45%       7.8us
  5-9            1207    1.01%       4514    3.29%       9.4us
  10-19           234    0.20%       2426    1.77%      16.6us
  20-49           244    0.20%       6434    4.69%      52.2us
```

That last row is 0.20% of packets at a **52.2 µs mean** — seven times the floor.

And the concentration, which is the whole point:

```
  span >= 10           %packets   %messages   %of total processing time
  ES book                 0.43        3.94         6.84
  NQ book                 1.33        4.05         5.19
  ZN book                 0.40        6.46        28.95
  NQ trade                4.65       18.85        27.89
  ES trade                6.47       25.11        35.06
  ZN trade               11.83       66.90        91.61
```

**On ZN book, 0.40% of packets consume 29% of all processing time. On ZN trade,
11.8% of packets consume 92%.**

None of that is congestion. It is arithmetic. A 34-message packet costs its last
message 33 × 2.284 µs on top of the floor, every single time, with no randomness
whatsoever.

## Three ways to measure the floor

If the queue and the batch are both removed, what is left is the hot path. I can
isolate it directly: `qlen == 0 AND idx == 0`.

```
  ES book  7.29us   86,885 msgs (83.0%)   p50 7.11  p90 9.99   p99 14.73
  NQ book  7.03us  111,093 msgs (73.4%)   p50 7.00  p90 8.91   p99 11.82
  ZN book  6.72us   29,459 msgs (80.2%)   p50 5.83  p90 9.73   p99 15.68
```

Three instruments, spread **0.57 µs**. Against the other two methods:

```
  method                                        ES     NQ     ZN
  direct, qlen==0 and idx==0                   7.29   7.03   6.72
  fitted zero-queue intercept (bin-level)      6.73   6.84   6.18
  floor at batch~1 during the FOMC release     7.1    7.5    7.0
```

The third is the interesting one. It was measured during the Fed statement, when
the packet rate rose **6.5× / 4.6× / 9.8×** in the space of a second. The floor
moved by at most +0.4 µs, and *fell* on two of the three.

The hot path is not rate-sensitive. Three methods, three instruments, all inside
6.2–7.5 µs.

## An 8× difference that should not exist

Look at the slopes again:

```
              msgs at idx 0     slope
  NQ book         234,636       291 ns
  ES book         187,933       732 ns
  ZN book          53,329      2284 ns
```

Same binary. Same decode path. Same machine. Same second. The quiet instrument
pays **eight times as much per message**.

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
contributes 0.03 to 0.36 µs, and is essentially irrelevant at these utilisations.

## One thing I cannot explain

ESZ6 book, 14:01:59.7:

```
  14:01:59.6    176 msgs  batch 1.17  qmax 2    mean  10.1us   max    20.0us
  14:01:59.7     69 msgs  batch 1.19  qmax 1    mean 918.9us   max 12237.1us
  14:01:59.8     92 msgs  batch 1.48  qmax 1    mean   9.3us   max    21.3us
```

A 63 millisecond smear across 69 messages, at queue depth 1, in a *quiet* bin,
with clean neighbours on both sides and no recovery. Nothing in the queue depth
or the batch size explains it. It is a stall, not a queue.

One bin in roughly 5,000; 69 messages out of 213,538. It moves no mean in this
article. It is the residual the model does not cover, and I would rather print it
than bury it.

## What I would take away

1. **Measure the pair, not the aggregate.** The variables I needed were in the
   same register at the same instant and I averaged them apart. Bin-level
   regression then gave me a slope that was wrong by 6–17× and looked *better*
   than the truth — cleaner, more monotone, three instruments agreeing.
   Aggregation does not just lose precision; it manufactures relationships.

2. **Check the shape before reaching for the model.** Convex means queueing.
   Linear means serialisation. They call for completely different fixes:
   queueing wants more service capacity, serialisation wants a cheaper
   per-message path or a way to not be last.

3. **Report the concentration.** "Mean 1.15 messages per packet" and "0.40% of
   packets consume 29% of all processing time" are the same dataset. Only one of
   them tells you where to look.

---

## Reproducing

Readers and instrumentation live on the `perf/live-wire-to-book` branch:

```
kaspr/perf/kh_msg.py  [HH:MM:SS]   # per-message qlen, idx, hot path
kaspr/perf/kh_idx.py  [HH:MM:SS]   # linearity test on the idx ladder
kaspr/perf/kh_pkt.py  [HH:MM:SS]   # packet size distribution and concentration
kaspr/perf/kh_scat.py [HH:MM:SS]   # the bin-level fit, kept for contrast
kaspr/perf/kh_proc.py [HH:MM:SS]   # arrival process: Fano, Hurst, branching
```

Instrumentation is `MsgRec` / `msg_record()` in
`frame/perf/act/LatencyProbe.hpp`. One 16-byte record per admitted message,
emitted at the same point and under the same admission rule as the bin
accumulators, so the two populations are identical and joinable.

## Limits

1. `t0` is a software timestamp at the socket read. NIC and kernel time are
   outside the measurement. Hardware RX timestamps would close that gap and have
   not been done.
2. The cache explanation in the 8× section is inferred, not measured, and is
   confounded with message mix.
3. One box, one session, one afternoon.
4. `idx` is a decode position, not a randomised treatment. Large packets may
   differ from small ones in message *content*, not only in position. The
   linearity and the constancy of the slope across a tenfold range argue against
   it, but do not eliminate it.
5. The startup window is excluded by a time cut. Startup is a snapshot replay,
   not a latency measurement, and it puts messages at 8 ms and 76 ms into the
   file.
