# Latency probe analysis

Reads the per-100ms CSVs that `frame/perf/act/LatencyProbe.hpp` writes and
reports leg latency against ingress queue depth.

    python3 kaspr/perf/kh_corr.py [csv_dir] [kaspr_log]

Defaults: `csv_dir=/home/vincent/perf/mdperf`, log = newest
`kaspr/kaspr_log.*.log`. Env `WARMUP_S` (default 30) sets the floor.

---

## The pipeline

```
  CME multicast (live)                         .pcap capture
         |                                          |
         v                                          v
  kaspr  (perf_probe true, USE_TACHBOOK=1)    dbento_pcap_parse/
  driven by kaspr/run_probe.sh                  dbento_pcap_to_bin
         |                                      merge_bins / filter_bin
         |                                          |  -> .bin
         |                                          v
         |                                    sim/src/main.cpp  (replay)
         v
  perf_csv_dir/          <-- one directory per run; NEVER share between variants
    lat_<sym>.csv          100 ms bins, 40 cols   (aggregate)
    lat_<sym>_<pop>.msg    per MESSAGE: t1,l1_ns,qlen,idx   <- percentiles
    lat_<sym>_<pop>.arr    per PACKET:  t0,seq,batch,span   <- arrivals; SERIAL ONLY
    env_<ts>.txt           onload env + kernel, captured at launch
         |
         v
  kaspr/perf/kh_*.py     MDPERF_DIR=<dir> python3 kh_<x>.py [HH:MM:SS HH:MM:SS]
         |
         v
  tech_reports/*.md      write-ups; RESULT_*.md here are the raw result records
```

Where the code lives:

| piece | path |
|---|---|
| probe actor (writes all three files) | `frame_kaspr/include/frame/perf/act/LatencyProbe.hpp` |
| probe wiring (`create_probes()`) | `kaspr/src/kaspr.cpp` |
| run driver | `kaspr/run_probe.sh` |
| config template | `kaspr/config/md_perf.ini` |
| analysis scripts | `kaspr/perf/kh_*.py` |
| decode paths under test | `mdp3/include/mdp3/DataDecoder.hpp` |

## Script index

All take `MDPERF_DIR` (default `/home/vincent/perf/mdperf`); most accept an
optional `HH:MM:SS HH:MM:SS` window in **ET**. Every script carries a header
comment stating what it answers and what it refuses to claim — read that before
quoting its output.

**Start here**

| script | question |
|---|---|
| `kh_sane.py` | integrity scan. Is the data self-consistent at all? **Run first.** |
| `kh_msg.py` | **per-message qlen vs latency — exact pairs. The percentile source.** |
| `kh_report.py` | counts, sums and extremes, each beside its own denominator |
| `kh_recovery.py` | recovery windows from the log, per channel |

**Queue depth (the independent variable)**

| script | question |
|---|---|
| `kh_qlat.py` | qlen vs mean leg-1, both populations |
| `kh_qlen.py` | qlen distribution |
| `kh_qidx.py` | is the qlen table itself confounded by `idx`? |
| `kh_qtail.py` | the tail of the deconfounded qlen cells |
| `kh_corr.py` | leg latency vs depth, log-driven exclusion |
| `kh_scat.py` | per-bin mean qlen vs per-bin mean latency (**bin-level — see limits**) |

**Packet position and batching (the main confounder)**

| script | question |
|---|---|
| `kh_idx.py` | is the `idx` ladder linear, and what is its slope? |
| `kh_imed.py` | the same ladder on medians, not means |
| `kh_isp.py` | is the ladder curved, or is it composition? |
| `kh_batch.py` | is batch size the reason, or just a correlate? |
| `kh_pkt.py` | messages per packet — the distribution, not the mean |

**Arrival process (the paper's question)**

| script | question |
|---|---|
| `kh_proc.py` | what is the arrival process, actually? |
| `kh_gap.py` | interarrival distribution vs the Poisson it is not |
| `kh_arrread.py` | reader/validator for the `.arr` log |

**Everything else**

| script | question |
|---|---|
| `kh_ctxrec.py` | context switches, aligned to the latency bins |
| `kh_event.py` | before/after a scheduled event |
| `kh_prac.py` | backing arithmetic for the practitioner section |

## Running a measurement, end to end

### 1. Build — `USE_TACHBOOK=1` is mandatory

```bash
./build.sh install USE_TACHBOOK=1       # libraries
./build.sh -C kaspr/src USE_TACHBOOK=1  # the binary
```

Both lines are needed. `./build.sh` alone builds the libraries and exits 0
**without** building the kaspr binary, so a missing executable looks like
success. And without `USE_TACHBOOK=1` the probe is not compiled in at all —
`create_probes()` is inside `#ifdef USE_TACHBOOK`, so the run starts cleanly,
records nothing, and leaves no error. Check for this line on startup:

    Kaspr: Creating LatencyProbes (bin 100 ms)

### 2. Config

The probe is the ordinary kaspr binary with `perf_probe true`. Required keys:

| key | value | why |
|---|---|---|
| `perf_probe` | `true` | creates the probes |
| `tachbook` | `true` | the probe subscribes to TachBook |
| `perf_route_tachbook` | `true` | routes the feed into TachBook instead of OB — **destructive: the OBs go dark for the run** |
| `mqport` | `7778` | not 7777; avoids the live recorder |
| `perf_bin_ms` | `100` | the CSV accumulation grid |
| `perf_csv_dir` | a path | **use a distinct directory per variant or runs overwrite each other** |

`kaspr/config/md_perf.ini` is the template. Point `universe` and `cme_ini` at
**absolute** paths — a relative `genconfig/mdp3_prod.info` breaks on CWD.

If you are comparing two configurations, they must be separate config
**directories**, not renamed files: `kaspr.cpp` resolves `cme.ini`, `som.ini`
and `light.ini` by fixed name against the main config's own directory, so a
`cme_variant.ini` sitting beside it is silently ignored and you measure the
default while believing otherwise.

### 3. Run

```bash
KHPROJ=/home/vincent/kaspar-hft \
PROBE_CFG=../config_mine/md_perf.ini \
OUTDIR=/home/vincent/perf/mdperf_mine \
  kaspr/run_probe.sh -t 900
```

`run_probe.sh` copies `kaspr` → `md_perf_meter` first: `stop_kaspr.sh` matches
`pgrep -x kaspr`, so a probe running under that name would be killed by the
Sunday cron, or worse, killed while the script believed it had stopped the
recorder.

Solo mode (the default) **stops the live recorder for the window** and restarts
it from a `trap EXIT`, so it comes back on Ctrl+C, on crash and on error. Flags:

| flag | effect |
|---|---|
| `-t N` | window seconds (default 300) |
| `--no-restart` | leave the recorder down afterwards |
| `-a` | run alongside the recorder, pinned off its cores — only valid once the multicast fan-out question is settled |
| `-n` | onload off (kernel sockets), for an A/B |

Use the **trading** onload profile (`EF_POLL_USEC=3000`, `EF_INT_DRIVEN=0`), which
the script sets. The recording profile spins for a different interval, and that
interval lands inside `t0` — it measures a different machine.

**Two costs to plan for.** The recording gap for the length of the window; and a
restart wipes the in-memory option books, so restarting during illiquid hours
blanks thin products for hours with no log line.

### 4. Sanity-check before analysing

The first ~5 minutes are not measurement data — the three recovery actors share
one cpu during startup. And confirm what you actually ran:

```
Kaspr: chan 310 SERIAL decode (inline)          # cme_decode_workers 0
Kaspr: chan 310 PARALLEL decode, 8 workers      # cme_decode_workers 8
```

### 5. Output — three files per instrument

| file | record | use |
|---|---|---|
| `lat_<sym>.csv` | 100 ms bins, 40 columns | `kh_corr.py`, `kh_report.py`, `kh_qlat.py` |
| `lat_<sym>_<pop>.msg` | **per message**: `t1`, `l1_ns`, `qlen`, `idx` (16 B) | `kh_msg.py` — percentiles |
| `lat_<sym>_<pop>.arr` | **per packet**: `t0`, `seq`, `batch`, `span` (16 B) | interarrival, `drop_startup` |

```bash
MDPERF_DIR=/home/vincent/perf/mdperf_mine python3 kh_msg.py [HH:MM:SS HH:MM:SS]
```

**Quote percentiles from `.msg`, not the binned CSV.** A "p99" of bin maxima
overstates the true per-message p99 by ~1.7× (measured: 68.48 vs 39.81 µs) — the
same ecological fallacy documented in `RESULT_qlen_vs_latency.md`, which
overstated the queue slope by 6–17×.

`.arr` is written on the **serial path only**, so `drop_startup` and interarrival
analysis are unavailable for parallel-decode runs.

---

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
