<!--
    Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
    Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
    Licensed under the MIT License. See LICENSE file in the project root.
-->

# dbento_pcap_parse — Databento PCAP → `.bin` conversion

Turns a day of Databento CME capture files into the gzip'd L3 record files
(`.bin`) that `BFA` replays, and extracts the instrument universe from them.

This is the offline half of the data pipeline. Nothing here touches the network
or trades; it is pure file-in / file-out, and it reuses the same decode path the
live system uses (`PCAPReader` → `MsgBuf` → `MessageProcessor` → `handler_if` →
`BinRecorder`), so a replayed session is byte-identical to what the live handler
would have produced.

```
.pcap.zst (many files/day)  →  dbento_pcap_to_bin  →  <chan>.<date>.databento.bin
                                                            │
                                                            ├→ build_universe  →  universe.<chan>.<date>.csv + .json.gz
                                                            ├→ merge_bins      →  one ts-ordered .bin
                                                            ├→ verify_merged   →  monotonicity check
                                                            └→ binstats        →  record/instrument inventory
```

## Tools

| Tool | Purpose |
|------|---------|
| `dbento_pcap_to_bin` | The converter. One channel's PCAPs for one day → one `.bin`. |
| `build_universe` | Scans a `.bin` for FDF/ODF/SDF instrument definitions → `universe.*.csv` + `universe.*.json.gz`. |
| `merge_bins` | Merges several `.bin` files into one timestamp-ordered stream. |
| `verify_merged` | Walks a merged `.bin` and reports any out-of-order timestamps. |
| `pcap_list_ips` | Dry-run inspector: lists src IPs / dst ports present in a capture directory. Use it when a channel yields zero packets. |
| `binstats` | Inventories a `.bin`: instrument-definition counts by updateAction, per-securityID MBO activity, full instrument list, and counts of other record types. |
| `extract_futures.sh` / `extract_options.sh` | Parallel, resume-safe batch wrappers over `dbento_pcap_to_bin`. |

## Layout

One directory per executable, each with a `src/Makefile` built from the
`mk_kaspr` templates — the same shape as `kaspr/src/` and every library in the
tree. There is no aggregate driver Makefile; the build system has no mechanism
for one executable per `APPNAM`, so a custom one would be off-standard.

```
dbento_pcap_parse/
├── dbento_pcap_to_bin/src/   the converter (+ PcapFileManager, Mdp3InfoParser)
├── build_universe/src/
├── merge_bins/src/
├── verify_merged/src/
├── pcap_list_ips/src/
├── binstats/src/
└── scripts/                  extract_futures.sh, extract_options.sh
```

## Build

Each tool builds like any other component in the tree:

```bash
export KSPRPROJ=~/kaspar-hft
cd dbento_pcap_parse/dbento_pcap_to_bin/src && make        # or: make debug / make clean
```

Build them all:

```bash
export KSPRPROJ=~/kaspar-hft
for t in dbento_pcap_to_bin build_universe merge_bins verify_merged pcap_list_ips binstats; do
    make -C dbento_pcap_parse/$t/src || break
done
```

Requires the core libraries to be built first (`KSPRPROJ=~/kaspar-hft make` at
the project root), plus `libpcap` and header-only `nlohmann/json` (resolved via
`JSON_PATH` in `mk_kaspr/glob_begin.mk`).

## Prerequisite: `genconfig/mdp3_prod.info`

`dbento_pcap_to_bin` resolves each channel's multicast IP and ports from
`$KSPRPROJ/genconfig/mdp3_prod.info`. That file is **downloaded from CME's SFTP
server, not committed** — generate it once:

```bash
cd genconfig && ./genconfig.sh
```

`KSPRPROJ` must be set in the environment; the tool refuses to guess, because the
batch scripts `chdir` into per-channel output directories before exec.

## Usage

Run each tool from its own `src/` directory (or put them on your `PATH`):

```bash
export KSPRPROJ=~/kaspar-hft
cd dbento_pcap_parse/dbento_pcap_to_bin/src

# per-channel layout (futures-xcme/, options-xcme/)
./dbento_pcap_to_bin --pcap-dir /path/to/pcaps/glbx/futures-xcme/20260119 --chan 310
./dbento_pcap_to_bin --pcap-dir ... --chan 310 --color b      # feed B

# consolidated layout (all channels muxed per 10-min file)
./dbento_pcap_to_bin --pcap-dir /path/to/pcaps/glbx/consolidated/20241230 \
    --chan 310 --format consolidated

# universe from the resulting bin
./build_universe 310.20260119.databento.bin.gz

# sanity-check what actually landed in the bin
./binstats --datafile 310.20260119.databento.bin.gz
```

`binstats` is the first thing to run on a new `.bin`. It flags the failure mode
that matters most downstream: a file whose FDF count is non-zero but which
carries no Add/Modify actions cannot build a symbol table, so every consumer of
it will come up empty.

Output is written to the **current working directory** (`BinRecorder` opens a
relative path), named `<chan>.<date>.databento.bin`. The batch scripts `cd` into
a per-channel output directory before exec for this reason.

### Batch

```bash
cd dbento_pcap_parse/scripts

# defaults; override any of SRC / NJOBS / BIN
SRC=/path/to/pcaps/glbx/futures-xcme NJOBS=8 ./extract_futures.sh 20260227
./extract_futures.sh          # every date found under $SRC
```

Resume-safe via `.ok` markers written only after a clean exit, so a partial
`.bin` from a killed run is never mistaken for a complete one.

## How files are selected

Each Databento PCAP covers one multicast group+port for one 10-minute window:

```
dc3-glbx-a-20260119T000000_224.0.31.1_14310.pcap.zst        ← chan 310 feed A
dc3-glbx-b-20260119T000000_224.0.32.1_15310.pcap.zst        ← chan 310 feed B
dc3-glbx-snap-a-20260119T000000_224.0.31.43_14310.pcap.zst  ← chan 310 IR (instrument defs)
dc3-glbx-snap-a-20260119T000000_224.0.31.22_14310.pcap.zst  ← chan 310 MBP snapshot
```

- **Incrementals**: filename ends in `_<incr_port>.pcap.zst` and does not contain
  `-snap-`.
- **Snap**: exactly one `-snap-` file is prepended, for the instrument
  definitions. It must match the **IR** multicast IP, not the MBP-snapshot IP —
  they share `port_ir` on different addresses and only IR carries FDF/SDF/ODF.
  Hence the `mdp3_prod.info` lookup.

Ports and IPs both come from config rather than the `14000+chan` convention: a
few channels (e.g. chan 323 with `port_a` 14346) don't follow it.

## Consolidated layout

In the consolidated layout every channel shares one file, so the reader needs a
per-packet `(dst_ip, dst_port)` allowlist — `create_PCAPReader_32_0_filtered()`.
Roughly 95% of packets are dropped for any one channel, so the reader drains
filter-misses inline rather than rescheduling per packet.

If a run produces **zero** matching packets with an active filter, `PCAPReader`
prints a loud warning at EOF — the usual causes are a wrong channel, a stale
`mdp3_prod.info`, or a CME multicast-IP migration. `pcap_list_ips` will tell you
what is actually in the directory.

## Architecture

```
dbento_pcap_to_bin (Manager)
│
├── actors::Group("dbento_pcap_group")
│   │
│   ├── persistent:  MsgBuf → MessageProcessor → handler_if → BinRecorder
│   └── per file:    PCAPReader(file N) → EOF → ShutdownThisActor
│                        → Group removes it → ActorRemoved → PcapFileManager
│
└── PcapFileManager
    scans + filters the directory, feeds one PCAPReader at a time via AddActor,
    waits for ActorRemoved, then sends the next. All files done → Shutdown.
```

One file at a time, sequenced by actor lifecycle messages rather than threads,
which is what keeps the output stream in capture order.

`PcapFileManager::failed()` is propagated to the process exit code so batch runs
treat an aborted scan as a failure instead of leaving a partial `.bin` behind.
