# Latency probe analysis

Reads the per-100ms CSVs that `frame/perf/act/LatencyProbe.hpp` writes and
reports leg latency against ingress queue depth.

    python3 kaspr/perf/kh_corr.py [csv_dir] [kaspr_log]

Defaults: `csv_dir=/home/vincent/perf/mdperf`, log = newest
`kaspr/kaspr_log.*.log`. Env `WARMUP_S` (default 30) sets the floor.

## What is measured

Three timestamps, two legs:

| | |
|---|---|
| `t0` | `pl->hndl_tim_epoch` — socket read. The name lies; it is not handler time. |
| `t1` | `pl->publish_ts` — book published. |
| `t2` | `Time::epoch()` at handler entry. |

`leg1 = t1 - t0`, `leg2 = t2 - t1`. Four series: 2 legs x 2 populations
(book, trade).

Leg 1 is five components with no stamp between them — socket read loop,
MsgBuf mailbox wait, decode, a second mailbox hop into TachBook, book build.
Only the second of those is in `ingress_qlen`. A large leg 1 does not on its
own say which component it came from.

## Known limits — read before quoting a number

**Means add, extremes do not.** The slowest leg1 and the slowest leg2 are not
the same message and no per-message pair is kept. There is a mean total. There
is no max total.

**min/max are over a subsample.** `LatencyProbe.hpp:580` says 1 packet in 5.
This is unconfirmed and conflicts with a separate note saying 1 in 16. The
means are unaffected. The maxes are: a sparser sample sees fewer tail events,
so treat the max column as a floor on the real tail.

**Within-bin pairing is lost.** The CSV carries `sum(latency)` and `sum(qlen)`
per bin, not the pair per message. This is a bin-level regression. If depth
swings inside one 100 ms bin, averaging both sides destroys the covariance. A
weak `r` is therefore not evidence that depth does not matter. A strong `r` is
evidence that it does.

**Flush bins are dropped from leg 2 only.** The probe's own CSV write runs on
the probe thread, so it lands inside leg 2. Leg 1 is upstream of the probe and
keeps them.

## Recovery exclusion

`kh_recovery.py` parses the kaspr log for gap and recovery events and returns
per-channel exclusion windows, which `kh_corr.py` subtracts.

This is not cosmetic. During recovery `processq()` early-returns, so live
packets pile up in `msg_q`. `enddatarecovery_handler`'s `processq(0,0)` is a
no-op — `in_data_recovery` is still true on that line. The backlog drains on
the *next* arriving packet, using that packet's `recv_ts` as `t0` for every
queued packet. Every packet after the first in the drain is charged the whole
drain time as leg 1: tens of ms against a ~9 us baseline. Those samples are not
measurements of anything.

Scope is per channel, not per instrument: one MessageProcessor per channel and
`qseq_num` is channel-wide, so a gap on any instrument stalls every instrument
on that channel. Channel map is in `CHAN_SYMS`; update it when the universe
changes.

The logger's line prefix is `00/00/0000 00:00:00.000000000` — `Logger::rt` is
false in kaspr, and `curr_tim` is only advanced by `mtim::msg::Alarm`. Do not
key off the prefix. The parser uses the `tim:` that individual messages carry
in their body, which is `chutil::Time::now_utc()`, the same clock as
`bin_key_ns`.

If the log reports events it cannot place on the timeline, the report says so
rather than returning a clean answer. A parser that cannot see an event must
not report "no events".

Run it standalone to see the windows only:

    python3 kaspr/perf/kh_recovery.py <kaspr_log>
