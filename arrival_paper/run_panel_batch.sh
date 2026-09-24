#!/usr/bin/env bash
# Copyright (c) 2026 Vincent Mayeski / M2 Tech.
# Licensed under the MIT License.
#
# Batch panel builder: run hourly_panel.py on every completed session in the
# tape corpus, in parallel with skip-if-exists. Each session emits one
# per-window panel parquet.
#
# Usage:
#   bash arrival_paper/run_panel_batch.sh --chan 318 [--jobs 20] [--service-us 7]

set -euo pipefail

CHAN=318
JOBS=20
SERVICE_US=7
OUT_DIR=""
KA_ROOT="${KA_ROOT:-/home/vmayeski/kaspar-hft}"
TAPES_ROOT="${TAPES_ROOT:-/vast/home/vmayeski/out/arrival_paper/tapes}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --chan)         CHAN="$2";       shift 2 ;;
        --jobs)         JOBS="$2";       shift 2 ;;
        --service-us)   SERVICE_US="$2"; shift 2 ;;
        --out-dir)      OUT_DIR="$2";    shift 2 ;;
        *) echo "unknown arg: $1" >&2; exit 2 ;;
    esac
done

MSG_DIR="$TAPES_ROOT/$CHAN/message"
FILL_DIR="$TAPES_ROOT/$CHAN/fill"
: "${OUT_DIR:=$TAPES_ROOT/$CHAN/panel_s${SERVICE_US}}"
mkdir -p "$OUT_DIR"

run_one() {
    local msg="$1"
    local base fill out
    base=$(basename "$msg" .csv)
    local ymd="${base%%.*}"
    local sym="${base#*.}"
    fill="$FILL_DIR/${base}.parquet"
    out="$OUT_DIR/${base}.parquet"
    if [[ ! -s "$fill" ]]; then
        echo "SKIP no-fill $base"; return 0
    fi
    if [[ -s "$out" ]]; then
        echo "SKIP exists $base"; return 0
    fi
    cd "$KA_ROOT" && nice -n 19 ionice -c 3 python3 -m arrival_paper.hourly_panel \
        --msg-tape "$msg" --fill-tape "$fill" \
        --session-date "$ymd" --symbol "$sym" \
        --qsim-service-us "$SERVICE_US" \
        --out "$out" >/dev/null 2>&1 && echo "OK $base" \
        || echo "FAIL $base"
}
export -f run_one
export KA_ROOT SERVICE_US FILL_DIR OUT_DIR

ls "$MSG_DIR"/*.csv 2>/dev/null | xargs -P "$JOBS" -n1 bash -c 'run_one "$@"' _
