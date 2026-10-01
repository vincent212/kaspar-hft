# Discussion of each setup

Numbers quoted below are from the 2026-10-01 runs in the tables above. They are
front-month book updates, written ES / NQ / ZN, in microseconds. If the tables
are regenerated from new runs, re-check the numbers quoted here.

A "hop" means a message handed to another thread's mailbox. A hop into a
thread that is asleep costs a futex wakeup, about 2-4 us on this feed. The feed
is sparse: about 1 packet per ms on NQ and ZN, 1 per 10 ms on ES. So a
receiving thread is asleep for almost every packet.

## base (production path)

The socket reader hands each packet to MsgBuf (hop 1). MsgBuf, MessageProcessor,
decode and the handler then run inline on the MsgBuf thread. The handler copies
each book event and sends it to TachBook on its own thread (hop 2). Both
receiving threads sleep between packets. This is the path the live recorder
runs.

It is slow at the median (p50 6.6 / 6.3 / 7.3) because both hops pay a wakeup.
The decode and handler work itself is only 0.3-0.9 us. Its tails are among the
better ones with both feeds on (p999 65.6 / 21.9 / 138), for two reasons. Book
work never delays decode: a burst of book updates queues at TachBook, not in
front of the next packet. And no thread busy-polls, so there is little
competition for CPUs. It is the reference point, not a good target.

## fastsend (book inline)

Same as base, except the handler calls TachBook directly. The book update runs
on the MsgBuf thread, so hop 2 disappears. One hop remains, and it still pays a
wakeup.

The median drops by about 2 us (4.9 / 4.7 / 4.7), which is the cost of hop 2.
The price is in the tail on the books with heavier per-event work. ES p999 goes
from 66 to 80 and ZN from 138 to 179; NQ is unchanged (21). During a burst, the
next packet now waits behind the previous packet's book update. Pass 1 showed
this directly: packets that arrived 2-3 deep in the queue had a p50 of 30 us,
against 9.5 us on base. This is the median-vs-tail trade-off of inlining.

## mbspin (spinning MsgBuf)

Same as base, but MsgBuf's mailbox busy-polls instead of sleeping (LockFreeMPSC
consumer spin). Hop 1 still exists, but nothing has to be woken. It costs one
full CPU core per channel.

The socket -> MsgBuf stage drops from 2-4 us to about 0.8 us at the median.
End-to-end p50 improves by about 1-2 us (5.3 / 5.3 / 4.9). The tails are as
good as base or better (p999 34.6 / 17.2 / 153.6), because book work still runs
on its own thread. On an unshared host this would be the safest improvement.
Here the spinning thread is sometimes preempted, which is why its maxima stay
in the milliseconds.

## fastsend_mbspin (both)

One hop left, and it does not wake anything: the reader hands off to a spinning
MsgBuf, which runs decode, handler and book inline. It is the best of the
"one hop" designs.

The median is about 3x better than base (2.5 / 2.2 / 2.4), and NQ's tail is
good (p999 15). ZN shows the trade-off at its clearest: p999 290 against 154
for mbspin. ZN packets carry more book events, and they are all processed in
line before the next packet starts. It also spins one core per channel.

## rfs (reader fast_send: zero hops)

The socket reader calls MsgBuf directly. MsgBuf, MessageProcessor, decode,
handler and the book update all run on the reader thread. t0 and t1 are taken
on the same thread. With feeds A and B, the two reader threads take turns
through MsgBuf's lock. The second one to arrive drops the duplicate packet.

This is the fastest design measured (p50 1.6 / 1.3 / 1.4; p1 0.5-0.6). It had
no packet loss (Onload socket and NIC-ring drop counters all 0) and no gaps.
The exchange-to-t0 measurement shows no extra waiting in the socket buffer
compared with base. Its p999 (28.6 / 12.9 / 162.5) is better than base on ES and
NQ and similar on ZN. The risks: the reader is not reading its socket while it
works, so a heavy burst could build up in the socket buffer. And A and B
contend for one lock. This is one unpinned run on a quiet day; it needs repeats,
including on a high-volume day.

## p4s (parallel decode)

MessageProcessor copies each packet and sends it round-robin to 4 decode
workers per channel. Each worker decodes into a recording of handler calls. A
single HandlerIfActor replays the recordings into the handler in packet order,
then sends to the book. Workers and HandlerIfActor busy-poll. Hops: MsgBuf,
worker, HandlerIfActor, book.

It is the slowest design (p50 12.3 / 11.0 / 13.7) and has some of the worst
tails (p999 85 / 347 / 265). CME packets carry about one message each, so there
is nothing to split across workers. Each packet just pays for two extra hops,
two copies, and a heap-allocated recording per handler call (worker decode p99
on ZN is 9.1 us, against 2.0 for serial decode). Fifteen extra threads must
also be on a CPU at the right moment. In 61 of 84 ten-second windows a hot
thread was waiting for a CPU while a message took over 100 us. And one stalled
worker holds up every packet behind it.

## fsmb_pin and rfs_pin (pinned, not isolated)

These are fastsend_mbspin and rfs with every busy thread pinned to fixed CPUs
on NUMA node 2. All other kaspr threads are kept off node 2. In fsmb_pin, sock
A and sock B of a channel share one core as SMT siblings, and MsgBuf has a core
to itself. In rfs_pin, each socket reader has a core to itself. The CPUs are not
isolated from the kernel.

The medians are excellent (fsmb_pin 1.7 / 1.4 / 1.9; rfs_pin 1.3 / 1.1 / 1.3).
The tails are the worst measured (fsmb_pin p999 739 / 312 / 1283; rfs_pin 254 /
84 / 362). The pinned threads waited 22-51 ms per 10 s in the run queue. Those
CPUs also handle the storage controller's interrupts (mpi3mr0), and a pinned
thread cannot move away when kernel or interrupt work lands on it; an unpinned
one simply migrates. Pinning only pays when the CPUs are isolated (isolcpus /
nohz_full, IRQ affinity moved off them). That needs a reboot and was not
tested.

## *_A variants (feed B off)

Same as the named config, but socket reader B is never started. Each channel
reads feed A only, so there is no arbitration between A and B.

Medians are about the same as with both feeds. Tails are almost always shorter:

| config | with A+B (p999) | A only (p999) |
|---|---|---|
| fastsend_mbspin | 30 / 15 / 290 | 23 / 9 / 109 |
| rfs_pin | 254 / 84 / 362 | 43 / 16 / 101 |
| p4s | 85 / 347 / 265 | 50 / 31 / 164 |

Two reasons: one fewer busy-polling thread per channel competing for CPUs, and,
in rfs, no A/B contention on MsgBuf's lock. The cost is resilience. A packet
lost on feed A becomes a gap and a recovery instead of being filled from B.
None happened in 7 A-only runs (about 55 minutes), but that is a short and quiet
sample.

# How to reproduce these numbers

All code, configs and scripts are on kaspar-hft branch `md-latency-experiments`.
That branch is experimental and is not merged to main. The full guide is in
`../md_median_vs_tail_draft.md`, section "How to reproduce". In short:

1. **Host.** Live CME MDP3 multicast on two interfaces, with OpenOnload
   installed. Stop any other kaspr reading the same groups: runs are solo. Use a
   universe of contracts that are live on the run date
   (`kaspr/config/universe.csv`).
2. **Build.**
   - Rebuild all the libraries with `./build.sh`, then kaspr with
     `./build.sh -C kaspr/src USE_TACHBOOK=1`.
   - Rebuild everything after any header change: a stale library cost one run
     here.
   - If the link fails, set `BOOST_PATH=/usr/local/boost188`.
3. **Configs.** Copy `configs/<label>/` to `kaspr/config_<label>/` for each
   label in the tables. The pinned configs hard-code CPU ids for this host
   (EPYC 9374F, node 2 = CPUs 16-23 and 48-55); remap them for another machine.
4. **Run each config**, 8-10 minutes, interleaving configs and rotating their
   order between passes. `run_matrix3.sh` in this directory does all of this:

   ```bash
   KHPROJ=$PWD OUTDIR=$OUT kaspr/run_probe.sh --no-restart -t 480 -c ../config_<label>/md_perf.ini
   ```

   - Pinned configs: run under `taskset -c 0-15,24-47,56-63`.
   - Meanwhile: `schedsample.py <pid> $OUT/probe.out $OUT/sched.csv 10`.
   - After the window: save `onload_stackdump lots` for the process's stacks to
     `$OUT/onload.txt`, then `kill -9` the probe (known ZMQ shutdown hang;
     samples are already flushed) and copy the newest `kaspr/kaspr_log.*.log`
     to `$OUT/kaspr.log`.
   - Name each run directory `<pass>_<label>`, e.g. `p1_base` or `x2_rfs_pin_A`.
5. **Analyse.** `R=<parent of the run directories> bash paper_all.sh`
   regenerates this file, plus `stages.md`, `sched.md` and `health.md`. The
   first 120 s of each run are dropped. Percentiles are over every message of
   the run directories with that label.
