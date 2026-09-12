#!/bin/bash
# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
#
# Licensed under the MIT License. See LICENSE file in the project root.

# Extract bin files from futures PCAPs for channels 310, 318, 326.
# Parallel, resume-safe (skips combos whose bin already exists), nice.
#
# Usage: ./extract_futures.sh [DATE ...]   (no args = all dates in source dir)
# Tune:  NJOBS=8 ./extract_futures.sh      (default: nproc)
# Product: CHANNELS="310" ./extract_futures.sh   (ES only; default 310 318 326)

set -u
cd "$(dirname "$0")" || { echo "[fatal] cannot cd to script directory" >&2; exit 1; }

# The converter needs KSPRPROJ to find genconfig/mdp3_prod.info. Derive it from
# the script's location (two levels up) unless the caller already set it.
export KSPRPROJ=${KSPRPROJ:-$(cd ../.. && pwd)}

SRC=${SRC:-/nvs/vendor/databento/pcaps/glbx/futures-xcme}
OUT_DIR=$PWD/out
BIN=${BIN:-$PWD/../dbento_pcap_to_bin/src/dbento_pcap_to_bin}
# Channels to extract. Override for a single-product pull, e.g.
#   CHANNELS="310" ./extract_futures.sh          # ES only
read -r -a CHANNELS <<< "${CHANNELS:-310 318 326}"
NJOBS=${NJOBS:-$(nproc 2>/dev/null || echo 16)}

# Per-(chan,date) worker. Exported so xargs bash subshells can find it.
run_one() {
    local chan=$1 date=$2
    local src_day="$SRC/$date"
    local bin_dir="$OUT_DIR/bin/$chan"
    local log_dir="$OUT_DIR/log/$chan"
    local out_bin="$bin_dir/$chan.$date.databento.bin"
    local done="$out_bin.ok"
    local log="$log_dir/$chan.$date.log"

    [ -d "$src_day" ] || { echo "[skip-no-src] chan=$chan date=$date"; return 0; }
    # Partial bins from a killed run leave a non-empty file. Gate resume on
    # the .ok marker that's only written after a clean exit.
    if [ -s "$out_bin" ] && [ -f "$done" ]; then
        echo "[skip-exists] chan=$chan date=$date"
        return 0
    fi
    rm -f "$done"  # stale marker if bin is missing/partial

    mkdir -p "$bin_dir" "$log_dir"
    local t0=$SECONDS
    # BinRecorder writes to CWD — run from bin_dir so the .bin lands in place.
    # Logger's rotating log files also land in CWD; that's OK, they live next
    # to the bin they document.
    ( cd "$bin_dir" && \
      nice -n 19 ionice -c 3 "$BIN" \
          --pcap-dir "$src_day" --chan "$chan" ) > "$log" 2>&1
    local rc=$?
    local dt=$((SECONDS-t0))
    if [ $rc -eq 0 ] && [ -s "$out_bin" ]; then
        touch "$done"
        local sz
        sz=$(du -h "$out_bin" | cut -f1)
        echo "[ok] chan=$chan date=$date ${dt}s ${sz}"
        return 0
    else
        echo "[FAIL rc=$rc] chan=$chan date=$date ${dt}s  log=$log"
        return "${rc:-1}"
    fi
}
export -f run_one
export SRC OUT_DIR BIN

# Dates
if [ $# -gt 0 ]; then
    DATES=("$@")
else
    mapfile -t DATES < <(ls -1 "$SRC" | grep -E '^20[0-9]{6}$' | sort)
fi

total=$((${#DATES[@]} * ${#CHANNELS[@]}))
echo "extract_futures: ${#DATES[@]} dates x ${#CHANNELS[@]} channels = $total runs, NJOBS=$NJOBS"
echo "src: $SRC"
echo "out: $OUT_DIR/bin/<chan>/"
echo

t_start=$SECONDS

# Emit (chan date) pairs, pipe to xargs -P.
{
    for date in "${DATES[@]}"; do
        for chan in "${CHANNELS[@]}"; do
            printf '%s %s\n' "$chan" "$date"
        done
    done
} | xargs -n 2 -P "$NJOBS" bash -c 'run_one "$@"' _
xargs_rc=$?

echo
echo "done: elapsed $((SECONDS-t_start))s (xargs rc=$xargs_rc)"
echo "bins: $OUT_DIR/bin/<chan>/"
echo "logs: $OUT_DIR/log/<chan>/"
exit "$xargs_rc"
