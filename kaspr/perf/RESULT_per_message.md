# What actually governs market-data latency — per-message measurement

**2026-09-16, CME, live. 971,477 messages.**

Kaspar is a market-data recorder. It reads CME MDP3 multicast, decodes SBE,
maintains order books. This is a measurement of the time from the socket read
to the book being published, taken one message at a time, on a busy afternoon
tape.

The short version: **it is not queueing.** The wait that dominates is
deterministic and linear, the queue that queueing theory is about barely
exists, and on one instrument 0.40% of packets account for 29% of all the time
spent.

---

## Provenance

- host process `md_perf_meter` (a renamed `kaspr`), pid 3011394, started
  14:22 local
- window **14:24:00 onward**, which excludes the startup backlog. Startup is
  not a latency measurement — it is a snapshot replay, and it puts messages at
  8 ms and 76 ms into the file. It is cut, not winsorised.
- data: `/home/vincent/perf/mdperf/lat_{ESZ6,NQZ6,ZNZ6}_{book,trade}.msg`
- one 16-byte record per admitted message: `(t1, l1_ns, qlen, idx)`
- scripts: `kh_msg.py`, `kh_idx.py`, `kh_pkt.py`
- instruments: ES (E-mini S&P), NQ (E-mini Nasdaq), ZN (10-year Note)

**What is measured.** `t0` is taken in the socket reader when the UDP datagram
is handed up. `t1` is taken when the book update is published. So this is
decode + book maintenance + publish, per message. It is *not* wire-to-book:
`t0` is a software timestamp, so NIC-to-socket time is outside the measurement.
That bound is stated again at the end.

---

## 1. The queue does almost nothing

`qlen` is the number of packets sitting in front of yours in the ingress ring
when your message is processed. This is the thing a queueing model is about.

Conditional mean latency at each exact integer depth, over messages:

```
              depth 0   depth 1   depth 2   depth 3      share at 0
  ES book       7.6us     7.9us     8.6us     8.8us         93.2%
  NQ book       7.2us     6.2us     7.8us    25.6us         88.1%
  ZN book       8.2us     8.5us        -         -          ~95%
  ES trade      9.9us        -         -         -          99.6%
  NQ trade      9.3us        -         -         -          99.8%
  ZN trade     29.7us   111.7us        -         -          95.3%
```

On ES book, going three packets deep costs **1.2 µs**. On NQ book, depth 1 is
*faster* than depth 0 — which on its own falsifies "depth causes latency" at
the depths that actually occur.

Per-message regression, and the contribution at the observed mean depth:

```
  ES book    lat = 7.59 + 0.37*q     mean q 0.073   queue term 0.03us of 7.62us
  NQ book    lat = 6.94 + 1.47*q     mean q 0.124   queue term 0.18us of 7.13us
  ZN book    lat = 8.19 + 3.79*q     mean q 0.094   queue term 0.36us of 8.55us
```

**The queue contributes under half a microsecond.** There is no ρ/(1−ρ) here
because ρ is nowhere near 1: 88–99.8% of messages find an empty ring.

### A trap worth documenting

Aggregating to 100 ms bins and regressing mean depth on mean latency gives
completely different slopes:

```
              per-message   bin-level   ratio
  ES book        0.37         6.33       17x
  NQ book        1.47        11.44        8x
  ZN book        3.79        22.11        6x
```

The bin-level fit overstates the queue effect by **6–17×**. It is a textbook
ecological fallacy: bursts raise ring depth *and* packet size together, and the
bin-level regression hands the packet-size effect to the queue. Both variables
were in the same CPU register at the same instant and were being thrown away
into bin aggregates. Nothing downstream can recover a joint distribution from a
max and a sum.

An earlier version of this work grouped bins by `qlen_max` and was worse still
— the group was *selected* on the deepest message in the bin but *averaged*
over all of them, and messages-per-bin itself rises with depth (ZN book: 6.7 at
depth 0, 479.6 at depth 3–4). The effect was divided by a denominator that grew
with the effect. See `RESULT_qlen_vs_latency.md` for that history.

---

## 2. What does the work: your position inside the packet

One UDP datagram carries many SBE messages. They are decoded in order. If you
are the 12th message in the packet, you wait for 11 decodes before yours runs
— and that wait lands in your latency.

`idx` is that position. Restricting to `qlen == 0`, so nothing is queued ahead
of the packet and the *only* remaining wait is inside your own packet:

```
  idx        0     1     2     3     4     5     6     7     8     9   10+
  ES book  7.3   7.7   8.3   8.9   9.6  10.3  11.1  12.0  12.6  13.0  18.2
  NQ book  7.0   7.4   7.8   8.0   8.4   8.7   8.8   9.1   9.4   9.5  11.1
  ZN book  6.7   8.3   9.7  12.2  14.7  17.4  19.9  21.8  24.1  28.0  41.0
  ES trade   -   7.5   8.6   9.3  10.0  10.6  11.4  12.0  12.9  13.6  19.8
  NQ trade   -   7.6   8.7   9.0   9.7  10.2  11.0  10.1  10.2  10.9  16.9
  ZN trade   -   7.5   9.5  11.4  12.3  14.4  16.3  17.8  18.8  20.0  50.3
```

Monotone on every stream. ZN book runs 6.7 → 41.0 µs.

### It is a straight line

```
                slope      r2      fitted run    shape
  NQ book      291 ns    0.955    idx 0..16     convex in last 4 pts (n=43..76)
  NQ trade     479 ns    0.955    idx 1..12     straight
  ES book      732 ns    0.976    idx 0..22     convex in last 9 pts (n=42..201)
  ES trade     864 ns    0.995    idx 0..19     straight
  ZN trade    1407 ns    0.989    idx 0..26     straight
  ZN book     2284 ns    0.996    idx 0..33     straight
```

ZN book is the cleanest case — 34 points, ≥43 messages each, spanning a 10×
rise:

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

  first-half slope 2.255, second-half 2.104   ->  straight
  max |resid| 4.62us against a 72.00us rise across the run
```

Linear, not convex. **This is deterministic serialisation, not queueing.** The
backlog is fully known the instant the packet lands. Message *k* waits for *k*
decodes at a fixed cost, and the slope *is* that per-message cost. There is no
utilisation term and nothing to blow up.

The convexity flagged on ES and NQ book is confined to the last few `idx`
values where the sample drops to 43–76 messages. ZN, which has by far the most
data at high `idx`, shows none at all.

---

## 3. Packet size: the mean is a lie

Messages per packet. `batch` is this symbol's messages; `span` is the total
decode positions in the packet across all symbols, which is what a message
actually waits behind.

```
                packets    messages   batch mean   span p50  p99  p999   max
  ES book       301,492     339,643      1.13          1      6    15     45
  NQ book       389,018     455,958      1.17          1     10    13     36
  ZN book       119,231     137,101      1.15          1      5    33     36
  ES trade        9,484      27,858      2.94          3     17    37     70
  NQ trade        5,378       8,920      1.66          2     16    32     61
  ZN trade        1,995       8,977      4.50          2     67    85     85
```

Median packet on book carries **one** message. The p999 carries 13–33. Mean
1.15 describes neither.

ZN book by packet size:

```
  span           pkts    %pkts    msgs in    %msgs   mean lat
  1            110853   92.97%     110853   80.85%       7.1us
  2              3740    3.14%       6773    4.94%       7.3us
  3-4            2953    2.48%       6101    4.45%       7.8us
  5-9            1207    1.01%       4514    3.29%       9.4us
  10-19           234    0.20%       2426    1.77%      16.6us
  20-49           244    0.20%       6434    4.69%      52.2us
```

The last row: 0.20% of packets, 4.69% of messages, **52.2 µs mean**. Seven
times the floor.

### The concentration

```
  span >= 10           %packets   %messages   %of total leg-1 time
  ES book                 0.43        3.94         6.84
  NQ book                 1.33        4.05         5.19
  ZN book                 0.40        6.46        28.95
  NQ trade                4.65       18.85        27.89
  ES trade                6.47       25.11        35.06
  ZN trade               11.83       66.90        91.61
```

**ZN book: 0.40% of packets consume 29% of all processing time.**
**ZN trade: 11.8% of packets consume 92%.**

And it is arithmetic, not congestion. A span-34 packet costs its last message
33 × 2.284 µs = 75 µs above the floor. That is exactly what the table shows.

---

## 4. The floor, by three independent methods

`qlen == 0 AND idx == 0` — nothing queued ahead, nothing ahead inside the
packet. The hot path with both waits removed, measured directly rather than
extrapolated:

```
  ES book  7.29us   86,885 msgs (83.0%)   p50 7.11  p90 9.99  p99 14.73
  NQ book  7.03us  111,093 msgs (73.4%)   p50 7.00  p90 8.91  p99 11.82
  ZN book  6.72us   29,459 msgs (80.2%)   p50 5.83  p90 9.73  p99 15.68
```

Spread across three instruments: **0.57 µs**. Compare:

```
  method                                      ES     NQ     ZN
  direct, qlen==0 & idx==0 (this document)   7.29   7.03   6.72
  fitted zero-queue intercept (bin-level)    6.73   6.84   6.18
  floor@batch~1 control (Fed day, 14:00)     7.1    7.5    7.0
```

Three methods, three instruments, all inside 6.2–7.5 µs. The third was measured
during the FOMC release when packet rate rose 6.5× / 4.6× / 9.8× — and it moved
by at most +0.4 µs, falling on two of three. **The hot path is not
rate-sensitive.**

---

## 5. The slope varies 8× and tracks how busy the instrument is

Same binary, same decode path, same machine, same second:

```
              msgs at idx 0     slope
  NQ book         234,636       291 ns
  ES book         187,933       732 ns
  ZN book          53,329      2284 ns
```

The quiet instrument pays **8× per message**. This is the same effect seen on
Fed day, where ZN book got *faster* (12.4 → 7.7 µs) at ten times the packet
rate: pre-Fed ZN was slow because it was idle, and the burst warmed it up.

The natural reading is instruction and data cache residency — a path that runs
constantly stays hot. But ES, NQ and ZN also differ in book depth, tick
structure and message mix, so this data cannot separate "cold cache" from "ZN
messages are genuinely more work." That needs hardware counters (LLC misses per
message), not procfs. **Stated as an open question, not a finding.**

---

## 6. Things that were ruled out

**Preemption.** `/proc/<pid>/task/*/schedstat` field 2 — time runnable but
denied a core — sampled continuously on the same 100 ms grid. During the FOMC
burst, voluntary context switches went 44,028 → 272,190 per 10 s (6.2×). The
hot thread's runqueue wait stayed at **0.03%**.

```
  2850318  cpu=9634.8ms  rqwait=3.283ms  slices=  2193  wait%=0.03%
  2850323  cpu=4952.4ms  rqwait=0.067ms  slices=  1041  wait%=0.00%
```

Caveat: schedstat measures being *off core*. It cannot see an SMT sibling
stealing issue slots — 65 threads on 8 physical cores. Flat rqwait rules out
preemption; it does not rule out SMT contention.

**Recovery.** Zero gap recoveries in the measured window. Nothing here is a
gap-close duration.

---

## 7. Where the tail comes from

The arrival process is not Poisson, and not merely heavy-tailed:

- gap autocorrelation positive at all 7 lags on book
- Fano factor collapses **6–19×** under a seeded shuffle that preserves the
  marginal distribution exactly and destroys only ordering — so the burstiness
  is in the *ordering*, not the marginal
- Hurst H = 0.62–0.70 against a shuffled null of ~0.55
- implied Hawkes branching ratio ≤ 0.88–0.95
- c_a² = 13.9 / 17.1 / 28.9, against 1 for Poisson

So arrivals are long-range dependent and self-exciting. Kingman's bound
`E[W] ≈ (ρ/(1−ρ))·((c_a²+c_s²)/2)·E[S]` *understates* here, because the shuffle
proves correlation contributes beyond the marginal.

Putting it together:

> Self-exciting arrivals cluster. Clustered arrivals produce occasional large
> packets. A large packet costs its last message `slope × idx`, linearly, with
> no randomness at all. **The tail is burstiness converted into batch size,
> then converted into latency by a straight line.**

The queue in front of the packet — the thing a queueing model would focus on —
contributes 0.03–0.36 µs and is essentially irrelevant at these utilisations.

---

## 8. One thing the model does not explain

ESZ6 book, 2026-09-16, bin 14:01:59.7:

```
  14:01:59.6    176 msgs  batch 1.17  qmax 2    mean  10.1us   max    20.0us
  14:01:59.7     69 msgs  batch 1.19  qmax 1    mean 918.9us   max 12237.1us
  14:01:59.8     92 msgs  batch 1.48  qmax 1    mean   9.3us   max    21.3us
```

A 63 ms smear over 69 messages, at queue depth 1, in a *quiet* bin, with clean
neighbours on both sides and no recovery. Nothing in `qlen` or `batch` explains
it. It is a stall, not a queue. One bin in ~5,000; 69 messages in 213,538. It
moves no mean in this document and it is not covered by anything above.

---

## Limits

1. **`t0` is a software timestamp** at socket read. NIC-to-socket time is
   outside this measurement. Hardware RX timestamps (SO_TIMESTAMPING) would
   close that and have not been done.
2. **Cache is inferred, not measured.** Section 5 is a hypothesis with a
   confound named.
3. **Single box, single session, one afternoon.** Numbers are from this
   hardware and this tape.
4. **`idx` is decode position, not a causal instrument.** Large packets may
   differ from small ones in message *content*, not only position. The
   linearity and the constancy of the slope across a 10× range argue against
   that, but do not eliminate it.
5. **The startup window is excluded by a time cut.** Startup puts messages at
   8 ms and 76 ms in the file and would dominate every mean here.

---

## Reproducing

```
kaspr/perf/kh_msg.py  [HH:MM:SS]   # per-message qlen, idx, hot path
kaspr/perf/kh_idx.py  [HH:MM:SS]   # linearity test on the idx ladder
kaspr/perf/kh_pkt.py  [HH:MM:SS]   # packet size distribution + concentration
kaspr/perf/kh_scat.py [HH:MM:SS]   # the bin-level fit, for contrast
```

Instrumentation: `frame/perf/act/LatencyProbe.hpp`, `MsgRec` and
`msg_record()`. One 16-byte record per admitted message, emitted at the same
point and under the same admission rule as the CSV bin accumulators, so the
populations are identical and joinable on `bin_key_ns`.
