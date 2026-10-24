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
RUN=~/recdata/344/runs/20261019
mkdir -p $RUN && cd $RUN
gunzip -c ~/recdata/344/universe.344.20261018.1792363121000041934.json.gz > universe.json

~/kaspar-hft/sim/src/sim \
    --datafile ~/recdata/344/344.20261018.1792363121000041934.22.38.1.bin \
    --universe universe.json \
    --contract ZNZ6 \
    --config ~/kaspar-hft/sim/config_a \
    --probe-size 10 \
    --probe-date 20261019 \
    --probe-out probe.csv \
    --quiet > out.txt 2> err.txt
echo "exit $?"
```

Takes about 2–3 minutes. The simulator always replays the **whole week**, but
the probe (and so the shadow algorithm's trading) runs on **one day**, the
`--probe-date`. To test every day of the week, run once per day (step 4b).

Tested 2026-10-24 on both October recordings with exactly this command:

| trading day | orders | fills | cancels | probe windows |
|---|---:|---:|---:|---:|
| 20261012 | 41 | 16 | 32 | 1 |
| 20261019 | 982 | 93 | 929 | 6 |

### 4b. Every day of the week

One run directory per day, same recording:

```bash
BIN=~/recdata/344/344.20261018.1792363121000041934.22.38.1.bin
UNI=~/recdata/344/universe.344.20261018.1792363121000041934.json.gz
for DAY in 20261019 20261020 20261021 20261022 20261023; do
  RUN=~/recdata/344/runs/$DAY
  mkdir -p $RUN && cd $RUN
  gunzip -c $UNI > universe.json
  ~/kaspar-hft/sim/src/sim --datafile $BIN --universe universe.json \
      --contract ZNZ6 --config ~/kaspar-hft/sim/config_a \
      --probe-size 10 --probe-date $DAY --probe-out probe.csv \
      --quiet > out.txt 2> err.txt
  echo "$DAY exit $?  $(grep 'TOT PNL' out.txt | tail -1)"
done
```

The days are independent, so they can also run in parallel (one per core).

| argument | meaning |
|---|---|
| `--datafile` | the recording (gzip'd, as downloaded) |
| `--universe` | the unzipped universe JSON from step 3 |
| `--contract` | **required**: the one contract to build a book for and trade, e.g. `ZNZ6` (ZN front month). Must be in the universe file |
| `--config` | directory with `lights.ini` (shadow algorithm settings) and `som.ini` (position and size limits). `config_a` is the standard one |
| `--probe-size` | contracts per leg for the SlippageProbe. **Without it the lights have no position to work and place no orders** |
| `--probe-date` | the **trading day**, `YYYYMMDD`, ET. The probe trades 09:29–16:00 ET on that day. Default: the date in the file name, which is the day the recording *started*, so for a Sunday-evening recording pass Monday's date |
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

**Files in the run directory**

| file | what it is |
|---|---|
| `out.txt`, `err.txt` | console output; the SOM summary is in `out.txt` |
| `sim_log.<date>.<ts>.<pid>.log` | the Logger's log: errors, warnings, operator lines (trading-status changes, ...) |
| `probe.csv` | per-window results: VWAP of each leg against the mid, the touch and the interval VWAP |
| `som<pid>.out`, `volume_<pid>.csv` | SOM order log, volume per instrument |
| `assertion__*.args`, `sig__*.args` | only after a failure: the assertion or signal, with a call stack |

---

## 6. When it goes wrong

| symptom | cause and fix |
|---|---|
| `Error: --contract required` | pass `--contract`. The simulator builds exactly one book |
| `contract X is not in universe.json` | wrong symbol for that session; list the futures: `python3 -c "import json;print(sorted(i['symbol'] for i in json.load(open('universe.json'))['instruments'] if i['type']=='FDF'))"` |
| `ASSERT a->maxpx > 1 ... has no usable daily price limit` | the universe file has no `high_limit_px` for the contract. Rebuild it with the current `build_universe` (step 3); an old file predates the default |
| `ORD: 0; FIL: 0` | no `--probe-size`, or `--probe-date` is a day the recording does not contain (e.g. the Sunday in the file name) |
| `ASSERT best_bid <= best_ask ... CROSSED book` | the rebuilt book disagrees with the exchange while the market is matching. Keep the `assertion__*.args` file and the log; this is a bug to report, not a setting. (Crosses in pre-open are expected and allowed: a recording whose first record falls in CME's 17:00–18:00 ET break starts with the check off until CME changes the trading status) |
| `version 'GLIBCXX_3.4.3x' not found` | the binary was built with a newer GCC than the system `libstdc++`. Point the loader at the compiler's own: `export LD_LIBRARY_PATH=$(dirname $(g++ -print-file-name=libstdc++.so.6))` |
| `cannot read universe ...` | the universe path is wrong, or it is still `.json.gz`; unzip it (step 4) |

---

## For agents

- Run every step from the directory shown; `build_universe` and `sim` write into
  the **current directory**. Do not use `/tmp`.
- Steps are idempotent: rebuilding the universe overwrites the same files; a run
  directory can be reused (old logs stay, new ones get a new pid).
- Treat a run as passed only if all three checks in step 5 hold. A run can exit 0
  with no orders (`ORD: 0`): that is a failed test, not a pass.
- One trading day per run directory. Each recording is a week; the trading days
  (`--probe-date`) are the Monday to Friday after the date in the file name:

  | recording | trading days |
  |---|---|
  | `344.20261011.1791754458012253470.21.34.1.bin` | 20261012 – 20261016 |
  | `344.20261018.1792363121000041934.22.38.1.bin` | 20261019 – 20261023 |
