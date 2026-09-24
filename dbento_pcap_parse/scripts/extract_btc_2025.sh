#!/bin/bash
# BTC (CME chan 326) PCAP → .bin conversion for 2025 only.
# Writes to /vast/home/vmayeski/out/bin/326/ to match the 310/318 layout.
#
# Usage:  ./extract_btc_2025.sh                # all 2025 dates
#         NJOBS=6 ./extract_btc_2025.sh        # tune parallelism (default 8)
# Runs at nice 19 / ionice class 3 so it can share the box with other work.

set -u
cd "$(dirname "$0")" || { echo "[fatal] cannot cd to script directory" >&2; exit 1; }
export KSPRPROJ=${KSPRPROJ:-$(cd ../.. && pwd)}

SRC=${SRC:-/vast/vendor/databento/pcaps/glbx/futures-xcme}
OUT_DIR=${OUT_DIR:-/vast/home/vmayeski/out/bin}
BIN=${BIN:-$KSPRPROJ/dbento_pcap_parse/dbento_pcap_to_bin/src/dbento_pcap_to_bin}
CHAN=326
NJOBS=${NJOBS:-8}
NICE_LEVEL=${NICE_LEVEL:-19}
IONICE_CLASS=${IONICE_CLASS:-3}

if [ ! -x "$BIN" ]; then
    echo "[fatal] binary not built: $BIN" >&2
    exit 1
fi

run_one() {
    local date=$1
    local src_day="$SRC/$date"
    local bin_dir="$OUT_DIR/$CHAN"
    local log_dir="$OUT_DIR/../log/$CHAN"
    local out_bin="$bin_dir/$CHAN.$date.databento.bin"
    local done="$out_bin.ok"
    local log="$log_dir/$CHAN.$date.log"

    [ -d "$src_day" ] || { echo "[skip-no-src] date=$date"; return 0; }
    if [ -s "$out_bin" ] && [ -f "$done" ]; then
        echo "[skip-exists] date=$date"
        return 0
    fi
    rm -f "$done"

    mkdir -p "$bin_dir" "$log_dir"
    local t0=$SECONDS
    ( cd "$bin_dir" && \
      nice -n "$NICE_LEVEL" ionice -c "$IONICE_CLASS" "$BIN" \
          --pcap-dir "$src_day" --chan "$CHAN" ) > "$log" 2>&1
    local rc=$?
    local dt=$((SECONDS-t0))
    if [ $rc -eq 0 ] && [ -s "$out_bin" ]; then
        touch "$done"
        local sz; sz=$(du -h "$out_bin" | cut -f1)
        echo "[ok] date=$date ${dt}s ${sz}"
        return 0
    else
        echo "[FAIL rc=$rc] date=$date ${dt}s  log=$log"
        return "${rc:-1}"
    fi
}
export -f run_one
export SRC OUT_DIR BIN CHAN NICE_LEVEL IONICE_CLASS

mapfile -t DATES < <(ls -1 "$SRC" | grep -E '^2025[0-9]{4}$' | sort)
total=${#DATES[@]}
echo "extract_btc_2025: $total dates, chan=$CHAN, NJOBS=$NJOBS, nice=$NICE_LEVEL"
echo "src: $SRC"
echo "out: $OUT_DIR/$CHAN/"
echo

t_start=$SECONDS
printf '%s\n' "${DATES[@]}" | xargs -n 1 -P "$NJOBS" bash -c 'run_one "$@"' _
rc=$?

echo
echo "done: elapsed $((SECONDS-t_start))s (xargs rc=$rc)"
echo "bins: $OUT_DIR/$CHAN/"
echo "logs: $OUT_DIR/../log/$CHAN/"
exit "$rc"
