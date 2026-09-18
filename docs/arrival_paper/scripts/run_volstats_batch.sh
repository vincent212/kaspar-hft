#!/usr/bin/env bash
# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Licensed under the MIT License.
#
# Run volstats over every .bin for 2024-2026 across chan 310 (ES), 318 (NQ),
# 326 (BTC). Skips 2023 by design. Output JSONs go under
#   /vast/home/vmayeski/out/arrival_paper/volstats/<chan>/volume.<chan>.<yyyymmdd>.json
# and the front-contract picker (kaspar_arrival.front_contract) reads them.
#
# Usage:
#   bash run_volstats_batch.sh [--dry-run] [--jobs N]

set -euo pipefail

VOLSTATS="${VOLSTATS:-/home/vmayeski/kaspar-hft/dbento_pcap_parse/volstats/src/volstats}"
BIN_ROOT="${BIN_ROOT:-/vast/home/vmayeski/out/bin}"
OUT_ROOT="${OUT_ROOT:-/vast/home/vmayeski/out/arrival_paper/volstats}"
UNIVERSE_ROOT="${UNIVERSE_ROOT:-/home/vmayeski/kaspar-hft/dbento_pcap_parse/scripts/out/universe}"
JOBS="${JOBS:-32}"
DRY_RUN=0

for arg in "$@"; do
    case "$arg" in
        --dry-run)  DRY_RUN=1 ;;
        --jobs=*)   JOBS="${arg#*=}" ;;
        *) echo "unknown arg: $arg"; exit 2 ;;
    esac
done

test -x "$VOLSTATS" || { echo "volstats not built at $VOLSTATS"; exit 1; }

run_one() {
    local chan="$1" bin="$2" out="$3" univ_arg="$4"
    "$VOLSTATS" --datafile "$bin" $univ_arg --out "$out" >/dev/null 2>&1 \
        && echo "OK $out" \
        || echo "FAIL $bin"
}
export -f run_one
export VOLSTATS

tasks=()
for chan in 310 318 326; do
    mkdir -p "$OUT_ROOT/$chan"
    univ="$UNIVERSE_ROOT/$chan/master_universe.$chan.json"
    if [[ -f "$univ" ]]; then
        univ_arg="--universe $univ"
    else
        univ_arg=""    # chan 326 has no master universe; symbol name will be blank
    fi
    for bin in "$BIN_ROOT/$chan"/*.bin; do
        base=$(basename "$bin")
        # Skip 2023.
        if [[ "$base" =~ \.(2023) ]]; then
            continue
        fi
        # Extract yyyymmdd from filename like 318.20250310.databento.bin
        ymd=$(echo "$base" | grep -oE '20[0-9]{6}' | head -1)
        [[ -z "$ymd" ]] && continue
        out="$OUT_ROOT/$chan/volume.$chan.$ymd.json"
        if [[ -s "$out" ]]; then
            continue    # already done
        fi
        tasks+=("$chan|$bin|$out|$univ_arg")
    done
done

echo "queued ${#tasks[@]} volstats tasks; jobs=$JOBS"
if [[ "$DRY_RUN" -eq 1 ]]; then
    printf '%s\n' "${tasks[@]}" | head -5
    echo "(dry run)"
    exit 0
fi

# xargs parallel driver — one JSON per task
printf '%s\n' "${tasks[@]}" | \
    xargs -P "$JOBS" -I {} bash -c '
        IFS="|" read -r chan bin out univ_arg <<< "{}"
        run_one "$chan" "$bin" "$out" "$univ_arg"
    '

echo "done"
