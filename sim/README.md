<!--
    Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
    Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
    Licensed under the MIT License. See LICENSE file in the project root.
-->

# Shadow algorithm simulator: quickstart

Replay one recorded CME session, rebuild the order book for one contract, and run
the shadow execution algorithm (`light22`) against it, with fills from the
simulated order manager (SOM). Everything is deterministic: the same file and
the same arguments give the same result.

```
recording (.bin) ──► BFA ──► order book (OB) ──► shadow lights ──► SOM (simulated fills)
                                   ▲                    ▲
                     universe.json (instruments)   SlippageProbe (gives the lights work)
```

You need four things: the build, a recording, its universe file, and one command.

---

## 1. Build

From the repository root. `KSPRPROJ` is set by `build.sh`; no exports needed.

```bash
cd ~/kaspar-hft
./build.sh schema                                       # once: generate the CME SBE codecs
./build.sh                                              # libraries
./build.sh -C sim/src                                   # the simulator: sim/src/sim
./build.sh -C dbento_pcap_parse/build_universe/src      # the universe extractor
```

After pulling new code, if the build fails with `No rule to make target ... .hpp`,
delete the stale dependency files and build again:

```bash
find . -name '*.P' -delete
```

**Check:** both binaries exist.

```bash
ls -l sim/src/sim dbento_pcap_parse/build_universe/src/build_universe
```

---

## 2. Get a recording

Recordings are on the M2 Tech SFTP server, `m2te.ch`: user `data`, password
`data` (a read-only account).

Put the files in a data directory of your own, one per channel, and download
into it:

```bash
mkdir -p ~/recdata/344
cd ~/recdata/344
sftp data@m2te.ch
```

At the `sftp>` prompt (enter the password when asked):

```
sftp> ls                    # list the recordings on the server
sftp> get 344.*.bin         # download every channel-344 recording
sftp> bye
```

To get one file only, name it: `get 344.20261018.1792363121000041934.22.38.1.bin`.

Non-interactive (scripts and agents): `sftp -b` cannot prompt for a password,
so pass it with `sshpass` (a separate package; check `which sshpass` first, and
use the interactive form above if it is not installed):

```bash
printf 'get 344.*.bin\nbye\n' > get.sftp
sshpass -p data sftp -b get.sftp data@m2te.ch
```

**About the files**

- Name: `<channel>.<date>.<start-ns>.<hh>.<mm>.<n>.bin`, e.g.
  `344.20261018.1792363121000041934.22.38.1.bin`.
  - `channel`: CME MDP3 channel. 344 = Treasury futures, 310 = ES, 318 = NQ.
  - `date`: the date the recording **started**, in UTC (Sunday evening).
  - `hh.mm`: start time, UTC.
- Despite the `.bin` name, the files are **gzip-compressed**. Do not unzip them;
  the tools read them as they are.
- **One file is one week**: Sunday evening's pre-open through Friday's close
  (about 3 GB for channel 344). `344.20261011...bin` holds the trading days
  2026-10-12 to 2026-10-16.

---

## 3. Build the universe file

The simulator needs the instrument definitions (symbol, securityID, tick size,
price limits). `build_universe` extracts them from the recording itself. It
writes into the **current directory**.

```bash
cd ~/recdata/344
~/kaspar-hft/dbento_pcap_parse/build_universe/src/build_universe \
    344.20261018.1792363121000041934.22.38.1.bin
```

Takes about 30 seconds. It writes:

```
universe.344.20261018.1792363121000041934.json.gz
universe.344.20261018.1792363121000041934.csv.gz
```

The simulator reads **plain JSON**, so unzip a copy into the run directory
(step 4).

**Price limits.** The order book sizes its price ladder from `high_limit_px`.
CME publishes no daily high limit for Treasury futures (the feed carries the SBE
null), so for those `build_universe` writes a default of **twice the median
traded price** in the recording. Each instrument says where its value came from:

```json
"high_limit_px": 211.09375,
"high_limit_source": "default",        // "cme", "default", or null (never traded)
"max_price_variation": 0.5,            // CME's price band, from the feed
"median_traded_px": 105.546875,
"max_traded_px": 105275.0              // can be junk; the median is what is used
```

---

## 4. Run the shadow test

One directory per run. The simulator writes its logs into the directory it is
started from.

```bash
RUN=~/recdata/344/runs/20261019-20261023
mkdir -p $RUN && cd $RUN
gunzip -c ~/recdata/344/universe.344.20261018.1792363121000041934.json.gz > universe.json

~/kaspar-hft/sim/src/sim \
    --datafile ~/recdata/344/344.20261018.1792363121000041934.22.38.1.bin \
    --universe universe.json \
    --contract ZNZ6 \
    --config ~/kaspar-hft/sim/config_a \
    --probe-size 10 \
    --probe-date 20261019-20261023 \
    --probe-out probe.csv \
    --quiet > out.txt 2> err.txt
echo "exit $?"
```

Takes about 2–3 minutes for the whole week: one replay of the file, with the
shadow algorithm trading 09:29–16:00 ET on every day in the `--probe-date` range.

Tested 2026-10-24 on both October recordings with exactly this command:

| `--probe-date` | orders | fills | cancels | probe windows (Mon, Tue, Wed, Thu, Fri) |
|---|---:|---:|---:|---|
| 20261012-20261016 | 4,483 | 551 | 4,217 | 38 (1, 6, 8, 9, 14) |
| 20261019-20261023 | 3,179 | 399 | 2,999 | 30 (6, 6, 6, 10, 2) |

The number of windows per day varies: a window closes only when its minimum
(10 min) has passed **and** both legs have filled, so a thin day gives fewer,
longer windows. 2026-10-12 was Columbus Day (cash bond market closed).

| argument | meaning |
|---|---|
| `--datafile` | the recording (gzip'd, as downloaded) |
| `--universe` | the unzipped universe JSON from step 3 |
| `--contract` | **required**: the one contract to build a book for and trade, e.g. `ZNZ6` (ZN front month). Must be in the universe file |
| `--config` | directory with `lights.ini` (shadow algorithm settings) and `som.ini` (position and size limits). `config_a` is the standard one; `config_zn` is tuned for ZN (use it with `--probe-size 100`: about 2% participation instead of 0.01%, see the comments in its `lights.ini`) |
| `--probe-size` | contracts per leg for the SlippageProbe. **Without it the lights have no position to work and place no orders** |
| `--probe-date` | the **trading days**, ET: `YYYYMMDD` for one day, or `YYYYMMDD-YYYYMMDD` for every weekday (Mon–Fri) in the range. The probe trades 09:29–16:00 ET on each. For a week-long recording pass its Monday to Friday. Default: the date in the file name, which is the Sunday the recording *started*, so always pass it for these files |
| `--probe-end` | end of each trading day, `HHMM` ET (default `1600`). **Use `1200` for Treasury futures** |
| `--probe-out` | CSV, one row per probe window |
| `--quiet` | keep the log small (otherwise ~600 MB per session) |

`sim/src/sim --help` lists the rest (latency model, placement rate, number of
lights, end time).

---

## 5. Check the result

**Success** means all of these:

1. Exit code `0`, and `out.txt` ends with `=== sim complete ===`.
2. The SOM summary line in `out.txt` shows orders and fills, i.e. `ORD` and `FIL`
   are above 0:
   ```
    TOT PNL: ...;   VOL: ...; FIL: ...; CAN: ...; ORD: ...; ...
   ```
3. `probe.csv` has a header and at least one data row.

```bash
tail -3 out.txt
grep "TOT PNL" out.txt | tail -1
wc -l probe.csv
```

### Where the output is saved

Everything a run writes goes into **the directory you start `sim` from**: the run
directory you made in step 4, e.g. `~/recdata/344/runs/20261019-20261023/`.
Nothing is written anywhere else. After a run it holds:

```
~/recdata/344/runs/20261019-20261023/
├── universe.json                 the universe you unzipped (input)
├── out.txt                       console output; the SOM summary line is at the end
├── err.txt                       setup messages: books, lights, probe schedule per day
├── probe.csv                     the measurement: one row per probe window (--probe-out)
├── sim_log.<date>.<ts>.<pid>.log the Logger's log: errors, warnings, trading-status changes
├── sim_log.<...>.audit.csv       the Logger's audit trail
├── som<pid>.out                  SOM order log
├── volume_<pid>.csv              volume per instrument
├── sim_universe_seed.csv         empty seed the sim writes for RefData (ignore)
└── slippage_report.csv           only after step 6
```

After a failure there is also `assertion__<...>.args` or `sig__<...>.args`: the
assertion or signal, with a call stack.

`probe.csv` columns that matter most (prices in ticks of the contract):

| column | meaning |
|---|---|
| `fire_ts` | window start, epoch ns |
| `mid_fire` | mid at the window's start (in ticks) |
| `buy_vwap`, `sel_vwap` | VWAP of our buy and sell fills in the window |
| `buy_filled`, `sel_filled` | contracts filled per leg (= `--probe-size` when complete) |
| `slip_buy_ticks`, `slip_sel_ticks`, `slip_paired_ticks` | slippage, positive = cost (see step 6) |
| `outcome` | `ok`, or `buy_short` / `sel_short` / `both_short` for a window cut off by the end of the data |

---

## 6. Slippage report

`sim/scripts/slippage_report.py` turns one or more `probe.csv` into a table: one
row per trading day, plus the total. It uses the same rules as the grid
aggregator (`aggregate_grid.py`): only completed (`ok`) windows are costed, and
participation is ours / (ours + market).

```bash
cd ~/recdata/344/runs/20261019-20261023
~/kaspar-hft/sim/scripts/slippage_report.py probe.csv --csv slippage_report.csv
```

Several runs at once (e.g. both weeks):

```bash
~/kaspar-hft/sim/scripts/slippage_report.py \
    ~/recdata/344/runs/20261012-20261016/probe.csv \
    ~/recdata/344/runs/20261019-20261023/probe.csv
```

Output for the week of 2026-10-19 (ZNZ6, 10 lots per leg, `config_a`):

```
slippage report: ZNZ6, parent 10 lots per leg, ticks (positive = cost)

day               ok/all     slip_paired  slip_buy  slip_sel    drift  vs_vwap  vs_touch  part_med  part_agg  leg_s  fills
--------------------------------------------------------------------------------------------------------------------------
2026-10-19 Mon    6/6     +0.217±1.161     -0.683    +1.117   -3.000   -0.754    -0.283    0.013%    0.009% 2510.7    8.0
2026-10-20 Tue    6/6     -0.343±2.309     -0.585    -0.100   -0.833   -0.944    -0.843    0.016%    0.007% 9790.4    6.3
2026-10-21 Wed    6/6     -0.225±0.887     -0.617    +0.167   -2.500   -0.308    -0.725    1.117%    0.007% 3072.6    7.2
2026-10-22 Thu   10/10    +0.160±0.813     +1.140    -0.820   +3.400   -0.155    -0.340    0.144%    0.009% 1380.0    6.5
2026-10-23 Fri    1/2     -0.200±-         -0.900    +0.500   -1.000   -0.298    -0.700    0.047%    0.047%  180.7   10.0
--------------------------------------------------------------------------------------------------------------------------
TOTAL            29/30    -0.024±0.597     -0.028    -0.021   -0.172   -0.479    -0.524    0.019%    0.008% 3662.9    7.0

not costed (leg did not fill before the data ended): sel_short=1
```

| column | meaning |
|---|---|
| `ok/all` | completed windows / all windows that day |
| `slip_paired` | (buy VWAP − sell VWAP) / 2, with a 95% confidence interval: the cost of the round trip per side. **Negative = we earned spread** |
| `slip_buy`, `slip_sel` | each leg against the mid at the window's start |
| `drift` | how far the mid moved over the window |
| `vs_vwap` | against the market's own VWAP over the leg |
| `vs_touch` | against crossing the spread at the start. Negative = waiting beat crossing |
| `part_med`, `part_agg` | participation: median per window, and total ours / (ours + market) |
| `leg_s` | mean seconds for the buy leg to fill its size |
| `fills` | mean number of child fills per leg |

All slippage numbers are in **ticks** of the contract (ZN: 1/64 of a point).
With `--csv FILE` the same table is also written as CSV.

---

## 7. When it goes wrong

| symptom | cause and fix |
|---|---|
| `Error: --contract required` | pass `--contract`. The simulator builds exactly one book |
| `contract X is not in universe.json` | wrong symbol for that session; list the futures: `python3 -c "import json;print(sorted(i['symbol'] for i in json.load(open('universe.json'))['instruments'] if i['type']=='FDF'))"` |
| `ASSERT a->maxpx > 1 ... has no usable daily price limit` | the universe file has no `high_limit_px` for the contract. Rebuild it with the current `build_universe` (step 3); an old file predates the default |
| `ORD: 0; FIL: 0` | no `--probe-size`, or `--probe-date` names days the recording does not contain (e.g. the Sunday in the file name) |
| `ASSERT best_bid <= best_ask ... CROSSED book` | the rebuilt book disagrees with the exchange while the market is matching. Keep the `assertion__*.args` file and the log; this is a bug to report, not a setting. (Crosses in pre-open are expected and allowed: a recording whose first record falls in CME's 17:00–18:00 ET break starts with the check off until CME changes the trading status) |
| `version 'GLIBCXX_3.4.3x' not found` | the binary was built with a newer GCC than the system `libstdc++`. Point the loader at the compiler's own: `export LD_LIBRARY_PATH=$(dirname $(g++ -print-file-name=libstdc++.so.6))` |
| `cannot read universe ...` | the universe path is wrong, or it is still `.json.gz`; unzip it (step 4) |

---

## For agents

- Run every step from the directory shown; `build_universe` and `sim` write into
  the **current directory**. Do not use `/tmp`.
- Steps are idempotent: rebuilding the universe overwrites the same files; a run
  directory can be reused (old logs stay, new ones get a new pid).
- The slippage report (step 6) needs only Python 3.9+ (standard library); run it
  after the run's exit code is 0.
- Treat a run as passed only if all three checks in step 5 hold. A run can exit 0
  with no orders (`ORD: 0`): that is a failed test, not a pass.
- One recording per run directory. Each recording is a week; pass its Monday to
  Friday as `--probe-date`:

  | recording | `--probe-date` |
  |---|---|
  | `344.20261011.1791754458012253470.21.34.1.bin` | `20261012-20261016` |
  | `344.20261018.1792363121000041934.22.38.1.bin` | `20261019-20261023` |
