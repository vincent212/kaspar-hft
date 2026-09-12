#!/bin/bash
# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
#
# Licensed under the MIT License. See LICENSE file in the project root.

# Build the instrument universe for one (channel, date) by decoding each CME
# Instrument-Replay (IR) capture window SEPARATELY, then unioning the results.
#
# WHY SEPARATELY: MessageProcessor gates on a monotonic sequence number
# (seqnum <= qseq_num -> drop). The IR stream loops and restarts its sequence
# every cycle, so concatenating the windows into one decode stream makes the
# gate discard all but the first few packets. Each window therefore needs its
# own decode context (= its own process, qseq_num back to 0). No single window
# carries the whole universe; the union does — on 2025-01-15 chan 310 the
# 22:30 window is the one holding the ES front month (5002 ESH5).
#
# Usage: ./build_universe_day.sh <chan> <date> [outdir]
#   SRC=... to override the capture root.

set -u
cd "$(dirname "$0")" || exit 1
export KSPRPROJ=${KSPRPROJ:-$(cd ../.. && pwd)}

CHAN=${1:?usage: build_universe_day.sh <chan> <date> [outdir]}
DATE=${2:?usage: build_universe_day.sh <chan> <date> [outdir]}
OUT=${3:-$PWD/out/universe/$CHAN}
SRC=${SRC:-/nvs/vendor/databento/pcaps/glbx/futures-xcme}

CONV=$PWD/../dbento_pcap_to_bin/src/dbento_pcap_to_bin
BUNI=$PWD/../build_universe/src/build_universe
DAY=$SRC/$DATE

[ -d "$DAY" ] || { echo "[skip-no-src] chan=$CHAN date=$DATE"; exit 0; }

# IR multicast IP for this channel, from the same config the converter uses.
IR_IP=$(awk -v c="chan$CHAN" '
    $1==c {inb=1} inb && $1=="group_ir" {print $2; exit}' \
    "$KSPRPROJ/genconfig/mdp3_prod.info")
[ -n "$IR_IP" ] || { echo "[FAIL] no group_ir for chan $CHAN in mdp3_prod.info" >&2; exit 1; }

# Read port_ir from config rather than computing 14000+chan: the convention
# does not hold for every channel (mdp3_prod.info has chan323 -> port_ir 14346),
# and dbento_pcap_to_bin.cpp documents the same caveat. Computing it silently
# matches no files for those channels.
IR_PORT=$(awk -v c="chan$CHAN" '
    $1==c {inb=1} inb && $1=="port_ir" {print $2; exit}' \
    "$KSPRPROJ/genconfig/mdp3_prod.info")
[ -n "$IR_PORT" ] || { echo "[FAIL] no port_ir for chan $CHAN in mdp3_prod.info" >&2; exit 1; }
mapfile -t IR_FILES < <(ls "$DAY"/*-snap-*"${IR_IP}_${IR_PORT}.pcap.zst" 2>/dev/null | sort)
[ ${#IR_FILES[@]} -gt 0 ] || { echo "[FAIL] no IR files for chan=$CHAN date=$DATE (ip=$IR_IP)" >&2; exit 1; }

# The converter refuses a snap-only directory (it fails fast when the
# incremental filter matches nothing), so pair each IR window with the
# smallest incremental as filler. Its records are discarded — we only read
# the definitions back out.
FILLER=$(ls -S "$DAY"/*"_224"*"_${IR_PORT}.pcap.zst" 2>/dev/null | grep -v -- "-snap-" | tail -1)
[ -n "$FILLER" ] || { echo "[FAIL] no incremental filler for chan=$CHAN date=$DATE" >&2; exit 1; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$OUT"

n_win=0
for f in "${IR_FILES[@]}"; do
    w=$TMP/$(basename "$f" .pcap.zst)
    mkdir -p "$w/in"
    ln -sf "$f" "$w/in/"; ln -sf "$FILLER" "$w/in/"
    # Fresh process per window => fresh qseq_num.
    ( cd "$w" && "$CONV" --pcap-dir "$w/in" --chan "$CHAN" >conv.log 2>&1 \
                && "$BUNI" "$CHAN.$DATE.databento.bin" >uni.log 2>&1 ) && n_win=$((n_win+1))
done

# Union across windows: header once, then unique rows keyed on securityID.
CSV=$OUT/universe.$CHAN.$DATE.csv
{
    echo "type,securityID,symbol,venue,asset,minPriceIncrement,minCabPrice,dispFactor,tickSize,securityType"
    zcat "$TMP"/*/universe.*.csv.gz 2>/dev/null | grep -v "^type," | sort -t, -k2,2n -u
} > "$CSV"

# JSON union too — this is the form BFA's securityID map is loaded from
# ({"instruments":[...]}), so it must carry the full per-instrument record,
# not just the CSV columns. Newest sighting per securityID wins.
JSON=$OUT/universe.$CHAN.$DATE.json
python3 - "$JSON" "$CHAN" "$DATE" "$TMP" <<'PYEOF'
import glob, gzip, json, sys
out, chan, date, tmp = sys.argv[1:5]
by_id = {}
for f in sorted(glob.glob(f"{tmp}/*/universe.*.json.gz")):
    try:
        with gzip.open(f, "rt") as fh:
            for inst in json.load(fh).get("instruments", []):
                sid = inst.get("securityID")
                if sid is not None:
                    by_id[int(sid)] = inst
    except Exception:
        continue
json.dump({"channel": chan, "date": date,
           "instruments": [by_id[k] for k in sorted(by_id)]},
          open(out, "w"), indent=1)
PYEOF

n=$(($(wc -l < "$CSV") - 1))
fdf=$(awk -F, '$1=="FDF"' "$CSV" | wc -l)
echo "[ok] chan=$CHAN date=$DATE windows=$n_win instruments=$n fdf=$fdf -> $CSV + $(basename "$JSON")"
