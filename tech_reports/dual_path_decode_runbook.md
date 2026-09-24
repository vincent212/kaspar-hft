# Runbook — serial vs parallel market-data decode, side by side

Goal: run ONE kaspr process that decodes every CME packet **twice** — once
serially, once across a worker fleet — into two independent sets of order
books, and compare the two. Same packets, same `recv_ts`, same sequencing,
same machine, same instant.

Why one process and not two runs: two runs at two different times see two
different markets. Nothing you measure across them separates "the parallel
decoder is faster" from "the second run was quieter". The tee is the only
construction that holds the input fixed.

    SocketReader -> MsgBuf -> MessageProcessor --fast_send--> DataDecoder_S (serial)
                                               \                 -> TachBook set S -> lat_<SYM>_S.*
                                                \--send-------> DataDecoder_P (N workers)
                                                                 -> TachBook set P -> lat_<SYM>_P.*

The primary is **forced serial** when the tee is on. That is deliberate: the
primary is the leg that drives recovery, and recovery must not depend on the
path under test.

---

## 0. What this run can and cannot answer

Read this before quoting any number out of it.

**It answers:** do both decoders see the same packets and agree? That is a
per-RUN check — equal `packets_`, equal failure counts at shutdown — not a
per-message check. Once the shadow became asynchronous there is no instant at
which both decoders have finished the same packet, so no per-packet assertion
is possible.

**It answers:** what does the parallel path's latency distribution look like
under a real arrival process, per contract.

**It does not answer:** "which path is faster", not cleanly. The two legs are
not symmetric and never will be:

- The shadow **copies the packet** (`DecodePacket`'s owning constructor,
  1500 B) before queueing; the primary decodes the borrowed buffer in place.
  That memcpy is on the shadow's side of the ledger.
- The shadow's messages travel through a mailbox; the primary's do not.
- The two decoders write into two different `TachBook` sets on two different
  threads, so cache behaviour differs.

**Historical note — do not reuse old numbers.** Before commit `mdp3/dual-path-tee`
BOTH legs were `fast_send`, meaning both ran inline on the MessageProcessor
thread with the primary first. The `_P` latencies of any run older than that
CONTAIN the entire `_S` decode. That is why `_P` p50 came out at almost exactly
2x `_S` p50 — it was serialisation, not a property of parallel decode. Any
`lat_*_P.*` file dated before this change is measuring that artifact.

---

## 1. Build

The tee is compiled out by default. Two independent switches must BOTH be on:

| switch | set in | gives you |
|---|---|---|
| `VERIFY_TEE=1` | `mk_kaspr/glob_begin.mk` → `-DMDP3_VERIFY_TEE`, **global** | the tee in `MessageProcessor`, the owning `DecodePacket` ctor, `set_async_input` |
| `USE_TACHBOOK=1` | `kaspr/src/Makefile` (per-directory) | `frame::ob::act::TachBook`, which the shadow books are made of |

`kaspr.hpp` derives `KASPR_VERIFY_TEE` from the conjunction, and `#error`s if
you set `VERIFY_TEE` without `USE_TACHBOOK`. That combination would otherwise
build, start, run, and write no `_P` samples at all.

`MDP3_VERIFY_TEE` is **global, not per-directory, and that is load-bearing.**
`mdp3::MessageProcessor` is compiled into `libmdp3.a` and into the unit tests,
but not into `kaspr.cpp`. A per-directory define would give the class a
different member layout per translation unit: links clean, corrupts at run
time, and no ODR diagnostic fires across a static library.

No `.P` dependency file tracks `glob_begin.mk`, so **flipping the define
rebuilds nothing**. You must clean.

```bash
export KSPRPROJ=$(pwd)
eval "$(mk_kaspr/detect_paths.sh)"        # BOOST_PATH etc; the link needs them
export VERIFY_TEE=1 USE_TACHBOOK=1

./build.sh clean
./build.sh                                 # libraries only -- see below
make -C kaspr/src                          # the binary. NOT built by build.sh.
```

`./build.sh` alone does **not** produce the kaspr binary, despite what
`build.sh --help` line 35 says. `install: … libo` → `loop: lib1 … lib11 lib14`,
and `kaspr/src` is not in that list. Build it by hand.

Confirm you got what you asked for, rather than assuming:

```bash
strings kaspr/src/kaspr | grep -c cme_verify_parallel   # tee build: >0. default: 0
```

## 2. Config

`kaspr/config/md_perf.ini`. The keys that matter:

```ini
tachbook              true     ; the probe subscribes to TachBook
perf_probe            true     ; without it kaspr is an ordinary recorder, no samples
perf_route_tachbook   true     ; route the feed into TachBook instead of OB
cme_verify_parallel   true     ; THE TEE. general section, not per-channel.
perf_bin_ms           100      ; 10 Hz CSV bins
perf_csv_dir          /home/vincent/perf/mdperf
mqport                7778     ; not 7777 -- do not collide with the live recorder
```

`perf_route_tachbook true` is **destructive**: the OBs go dark, so
lights/SOM/DB/MTD see a dead market for the length of the window. Correct for
a latency run, wrong for anything else. It must never appear in `kaspr.ini`.

**Worker count.** Leave `cme_decode_workers` ABSENT from `cme.ini`. With the
tee on, absent means 2. Setting it would also flip the PRIMARY to parallel on
any config where the tee is off. It must be a power of two — `set_workers`
masks with `nworkers-1`, so 12 would silently run 8.

**Two workers is not an arbitrary starting point, and more may buy nothing.**
Worker choice is `workers_[h & worker_mask_]` where `h` counts hot messages
*within one packet*. Worker k therefore cannot receive any work unless a packet
carries at least k+1 hot messages. Measured ZN book traffic runs ~1.14
messages/packet, so at that mix workers 2 and 3 of a 4-worker fleet are idle by
construction. Raising the count without first measuring msgs/packet measures
nothing.

## 3. Pre-flight

```bash
pgrep -ax kaspr md_perf_meter
```

Both must be empty. A stale `md_perf_meter` from an earlier window holds
udp/14310, 14318, 15310, 15318 and the new run dies with `EADDRINUSE`. This has
already been misdiagnosed once as a privilege problem; it is not. Kill it:

```bash
kill -TERM <pid>; sleep 3; kill -KILL <pid> 2>/dev/null
```

`SIGTERM` first — the shutdown handlers flush and close the bin recorder, and a
`SIGKILL` truncates the samples. Then `SIGKILL`, always: the process will not
exit on its own. `Manager::end()` joins SocketReader threads parked in an
untimed `select()`, and there is no path out of that. The flush has already
completed, so the samples are safe; the kill is the documented procedure, not a
last resort. See `teardown_hang.md`.

Do not skip it and walk away. Under onload a parked reader **busy-polls**, so an
abandoned probe holds its ports and burns ~2.4 cores until someone notices.

Check the universe file the ini points at is current. A probe that builds books
for an expired contract measures nothing and *looks like a result*.

## 4. Run

The supported mode is **solo**: nothing else bound to the multicast groups, so
there is no question of the kernel load-balancing datagrams away from us, and
no second busy-spinning process to contend with. `EF_POLL_USEC=3000` with
`EF_INT_DRIVEN=0` means spin; two spinners measure each other.

Use the same onload profile as the live recorder — `EF_POLL_USEC` and
`EF_INT_DRIVEN` set how long a thread spins before blocking on an interrupt,
and that interval lands *inside* the `t0` being measured. The recording profile
would measure a different machine.

```bash
kaspr/run_probe.sh -t 300
```

`run_probe.sh` stops the live recorder, runs the window, and restarts it from
an `EXIT` trap. **Two cautions.** Its restart trap is known to start the wrong
binary (open defect), and it is hardcoded to `KHPROJ=/home/vincent/kh-probe`.
To run a *different* tree's binary, invoke it directly and skip the trap
entirely — see `/tmp/run_tee.sh` for the form. If you do that, you own
restarting the recorder yourself.

End the window with `SIGTERM`, never `SIGKILL`: shutdown is what writes the
`decode packets=… decode_failures=… worker_failures=…` line for each decoder,
and that line is the verdict.

## 5. Read the result

**First, the agreement check.** In the log:

```bash
grep 'decode packets=' kaspr/kaspr_log.*.log
```

Two lines, one per decoder. Pass condition: equal `packets_`, and both failure
counts zero. `worker_failures_` is counted per MESSAGE and is *not* included in
`decode_failures_`; they are different denominators and must not be summed.

A `_P` record count materially below `_S` is expected right now and is a known
defect, not a decode failure: `VERIFY shadow INVALIDATED` fires ~3x at startup
because the shadow books never receive the recovery snapshot, costing roughly
9% of `_P` records. Until that is fixed, do not read a `_P`/`_S` count ratio as
a decode result.

**Then the latency tables.** Per contract, never lumped — ES, NQ and ZN have
different message mixes and lumping them hides the thing you are looking for.

```bash
python3 kaspr/perf/kh_msg.py          # per-message qlen vs latency, from .msg
python3 kaspr/perf/kh_corr.py         # 100ms-bin leg1/leg2 vs ingress depth
```

`kh_msg.py` is the one to trust for a latency distribution: it reads the
16-byte per-message records (`t1`, `l1_ns`, `qlen`, `idx`) so the qlen/latency
pair is intact. The CSV path carries `sum(latency)` and `sum(qlen)` per bin,
which destroys the within-bin covariance — a weak `r` there is not evidence
that depth does not matter.

Discard the first 30 s (`WARMUP_S`). Recovery traffic is not steady state.

## 6. Known defects that affect this run

Do not spend time rediscovering these.

| | |
|---|---|
| `_P` `.arr` files are never written | arrival logs exist only on the `_S` path |
| `_P` `idx` column is always 0 | a worker decodes one message, so `EndOfPacket` resets `pkt_entry_idx_` every message. Telemetry defect, not a decode defect |
| shadow books miss the recovery snapshot | `VERIFY shadow INVALIDATED` x3 at startup; ~9% fewer `_P` records |
| the probe never exits on SIGTERM | not a "hang after flushing". `Manager::end()` joins readers parked in an untimed `select()`. TERM then KILL, always. WONTFIX by decision — see `teardown_hang.md` |
| `run_probe.sh` restart trap | starts the wrong binary |
| committed `cme.ini` genconfig path | relative, broken for anyone not in `kaspr/` |
