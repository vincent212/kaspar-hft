#!/bin/bash
# BTC (CME chan 326) 2024 extraction from consolidated pcaps.
set -u
cd "$(dirname "$0")"
export KSPRPROJ=${KSPRPROJ:-$(cd ../.. && pwd)}
SRC=${SRC:-/vast/vendor/databento/pcaps/glbx/consolidated}
OUT_DIR=${OUT_DIR:-/vast/home/vmayeski/out/bin}
BIN=${BIN:-$KSPRPROJ/dbento_pcap_parse/dbento_pcap_to_bin/src/dbento_pcap_to_bin}
CHAN=326
NJOBS=${NJOBS:-8}
NICE_LEVEL=${NICE_LEVEL:-19}
IONICE_CLASS=${IONICE_CLASS:-3}

run_one() {
    local date=$1
    local src_day="$SRC/$date"
    local bin_dir="$OUT_DIR/$CHAN"
    local log_dir="$OUT_DIR/../log/$CHAN"
    local out_bin="$bin_dir/$CHAN.$date.databento.bin"
    local done="$out_bin.ok"
    local log="$log_dir/$CHAN.$date.log"
    [ -d "$src_day" ] || { echo "[skip-no-src] date=$date"; return 0; }
    if [ -s "$out_bin" ] && [ -f "$done" ]; then echo "[skip-exists] date=$date"; return 0; fi
    rm -f "$done"
    mkdir -p "$bin_dir" "$log_dir"
    local t0=$SECONDS
    ( cd "$bin_dir" && nice -n "$NICE_LEVEL" ionice -c "$IONICE_CLASS" "$BIN" \
          --pcap-dir "$src_day" --chan "$CHAN" --format consolidated ) > "$log" 2>&1
    local rc=$?
    local dt=$((SECONDS-t0))
    if [ $rc -eq 0 ] && [ -s "$out_bin" ]; then
        touch "$done"
        local sz; sz=$(du -h "$out_bin" | cut -f1)
        echo "[ok] date=$date ${dt}s ${sz}"
    else
        echo "[FAIL rc=$rc] date=$date ${dt}s  log=$log"
    fi
}
export -f run_one
export SRC OUT_DIR BIN CHAN NICE_LEVEL IONICE_CLASS
mapfile -t DATES < <(ls -1 "$SRC" | grep -E '^2024[0-9]{4}$' | sort)
total=${#DATES[@]}
echo "extract_btc_2024: $total dates, chan=$CHAN (consolidated), NJOBS=$NJOBS"
t_start=$SECONDS
printf '%s\n' "${DATES[@]}" | xargs -n 1 -P "$NJOBS" bash -c 'run_one "$@"' _
echo "done: elapsed $((SECONDS-t_start))s"
