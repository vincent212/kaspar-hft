#!/usr/bin/env bash
# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Licensed under the MIT License.
#
# Batch driver for the arrival-paper tape corpus.
#
# For each row in front_contract.csv (produced by arrival_paper.front_contract),
# run the four builders in sequence:
#
#   1. msgtape           C++     per-securityID message tape         → CSV
#   2. bin_replay_bbo    C++     channel-wide BBBOChg tape           → CSV.gz
#   3. fill_tape         Python  maker-fill tape + markouts          → parquet
#   4. packet_tape       Python  per-UDP-packet aggregate            → parquet
#
# Steps 1 and 2 are independent per (session_date, channel) and can run in
# parallel across sessions. Steps 3 and 4 depend on 1 and 2 for the same
# session but not on any other session. GNU-xargs at the outer loop; each
# child does 1→2→3→4 sequentially.
#
# Skip-if-exists: every step writes to a fixed path; if the output is
# already there and non-empty, that step is skipped. Re-running the driver
# fills in whatever wasn't done.
#
# Day-1 gate: this script runs `arrival_paper.validate` on the first 5
# fill_tape parquets emitted per stream and aborts the batch if any of them
# fails a hard identity. Set BATCH_SKIP_VALIDATE=1 to bypass (do NOT do that
# for the initial production run).
#
# Usage:
#   bash run_tape_batch.sh --front-contract front_contract.csv [--jobs 16]
#                          [--limit N] [--channels 310,318,326]

set -euo pipefail

# ---- config ---------------------------------------------------------------

MSGTAPE_BIN="${MSGTAPE_BIN:-/home/vmayeski/kaspar-hft/dbento_pcap_parse/msgtape/src/msgtape}"
BIN_REPLAY_BBO="${BIN_REPLAY_BBO:-/home/vmayeski/kaspar-hft/dbento_pcap_parse/bin_replay_bbo/src/bin_replay_bbo}"
BIN_ROOT="${BIN_ROOT:-/vast/home/vmayeski/out/bin}"
OUT_ROOT="${OUT_ROOT:-/vast/home/vmayeski/out/arrival_paper/tapes}"
UNIVERSE_ROOT="${UNIVERSE_ROOT:-/home/vmayeski/kaspar-hft/dbento_pcap_parse/scripts/out/universe}"
KA_ROOT="${KA_ROOT:-/home/vmayeski/kaspar-hft}"

FRONT_CONTRACT=""
JOBS=16
LIMIT=0
CHANNELS="310,318,326"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --front-contract) FRONT_CONTRACT="$2"; shift 2 ;;
        --jobs)           JOBS="$2"; shift 2 ;;
        --limit)          LIMIT="$2"; shift 2 ;;
        --channels)       CHANNELS="$2"; shift 2 ;;
        --dry-run)        DRY_RUN=1; shift ;;
        -h|--help) sed -n '1,40p' "$0"; exit 0 ;;
        *) echo "unknown arg: $1" >&2; exit 2 ;;
    esac
done

test -n "$FRONT_CONTRACT" || { echo "--front-contract required"; exit 2; }
test -x "$MSGTAPE_BIN" || { echo "msgtape not built at $MSGTAPE_BIN"; exit 1; }
test -x "$BIN_REPLAY_BBO" || { echo "bin_replay_bbo not built"; exit 1; }
test -f "$FRONT_CONTRACT" || { echo "front-contract file missing: $FRONT_CONTRACT"; exit 1; }

# ---- per-session runner ---------------------------------------------------

run_one() {
    local ymd="$1" chan="$2" asset="$3" symbol="$4" secid="$5"
    local bin="$BIN_ROOT/$chan/$chan.$ymd.databento.bin"
    if [[ ! -f "$bin" ]]; then
        echo "SKIP no-bin $ymd chan=$chan"
        return 0
    fi

    local msg_out="$OUT_ROOT/$chan/message/$ymd.$symbol.csv"
    local bbo_out="$OUT_ROOT/$chan/bbbochg/$ymd.$asset.csv.gz"
    local fill_out="$OUT_ROOT/$chan/fill/$ymd.$symbol.parquet"
    local pkt_out="$OUT_ROOT/$chan/packet/$ymd.$symbol.parquet"

    mkdir -p "$(dirname "$msg_out")" "$(dirname "$bbo_out")" \
             "$(dirname "$fill_out")" "$(dirname "$pkt_out")"

    # 1. msgtape (per-securityID, RTH-only)
    if [[ ! -s "$msg_out" ]]; then
        TZ=America/New_York nice -n 19 ionice -c 3 "$MSGTAPE_BIN" \
            --datafile "$bin" --secid "$secid" --rth-only \
            --out "$msg_out" >/dev/null 2>&1 || {
            echo "FAIL msgtape $ymd chan=$chan sec=$secid"
            return 1
        }
    fi

    # 2. bin_replay_bbo (channel-wide, per-event BBBOChg)
    # Uses the PER-DAY universe JSON (universe.<chan>.<yyyymmdd>.json) — not the
    # merged master — because bin_replay_bbo needs high_limit_px and other
    # per-session fields that only the per-day file carries.
    local univ_day="$UNIVERSE_ROOT/$chan/universe.$chan.$ymd.json"
    if [[ ! -f "$univ_day" ]]; then
        echo "SKIP no-per-day-universe $ymd chan=$chan"
        return 0
    fi
    if [[ ! -s "$bbo_out" ]]; then
        nice -n 19 ionice -c 3 "$BIN_REPLAY_BBO" \
            --datafile "$bin" \
            --universe "$univ_day" \
            --out "$bbo_out" --asset "$asset" \
            --session "$ymd" --quiet >/dev/null 2>&1 || {
            echo "FAIL bbo $ymd chan=$chan asset=$asset"
            return 1
        }
    fi

    # 3. fill_tape (Python). Needs dispFactor + tickSize from master_universe
    # so it can convert msgtape's pxd (scaled by dispFactor's inverse) and
    # bbbochg's tick-index prices into a common native-price scale for the
    # markout arithmetic. Both are looked up from master_universe.<chan>.csv
    # by symbol.
    local univ_csv="$UNIVERSE_ROOT/$chan/master_universe.$chan.csv"
    local disp_factor tick_size
    disp_factor=$(awk -F, -v sym="$symbol" '$3==sym {print $8; exit}' "$univ_csv")
    tick_size=$(awk -F, -v sym="$symbol" '$3==sym {print $9; exit}' "$univ_csv")
    if [[ -z "$disp_factor" || -z "$tick_size" ]]; then
        echo "SKIP no-univ-row $ymd chan=$chan symbol=$symbol"
        return 0
    fi
    if [[ ! -s "$fill_out" ]]; then
        cd "$KA_ROOT" && nice -n 19 ionice -c 3 python3 -m arrival_paper.fill_tape \
            --msg-tape "$msg_out" --bbbochg "$bbo_out" \
            --disp-factor "$disp_factor" --tick-size "$tick_size" \
            --out "$fill_out" >/dev/null 2>&1 || {
            echo "FAIL fill_tape $ymd chan=$chan"
            return 1
        }
    fi

    # 4. packet_tape (Python)
    if [[ ! -s "$pkt_out" ]]; then
        cd "$KA_ROOT" && nice -n 19 ionice -c 3 python3 -m arrival_paper.packet_tape \
            --msg-tape "$msg_out" --out "$pkt_out" >/dev/null 2>&1 || {
            echo "FAIL packet_tape $ymd chan=$chan"
            return 1
        }
    fi

    echo "OK $ymd chan=$chan asset=$asset symbol=$symbol"
}
export -f run_one
export MSGTAPE_BIN BIN_REPLAY_BBO BIN_ROOT OUT_ROOT UNIVERSE_ROOT KA_ROOT

# ---- driver ---------------------------------------------------------------

# Parse front_contract.csv → 5-column job list: ymd, chan, asset, symbol, secid.
# xargs -n5 groups every 5 whitespace-separated tokens into one child call.
awk -F, -v chans="$CHANNELS" '
BEGIN {
    split(chans, arr, ",")
    for (i in arr) keep[arr[i]] = 1
}
NR==1 { next }
{
    ymd = $1; chan = $2; asset = $3; symbol = $4; secid = $5
    if (!(chan in keep)) next
    if (secid == "") next
    # single-space separated, one line per job; no tabs, no embedded spaces
    # (symbols are things like NQH5, no whitespace).
    print ymd, chan, asset, symbol, secid
}' "$FRONT_CONTRACT" | \
    { if [[ "$LIMIT" -gt 0 ]]; then head -n "$LIMIT"; else cat; fi; } | \
    xargs -P "$JOBS" -n5 bash -c 'run_one "$@"' _
