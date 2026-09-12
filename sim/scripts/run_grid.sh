#!/usr/bin/env bash
# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
#
# Licensed under the MIT License. See LICENSE file in the project root.
#
# The shadow baseline grid: 29 configs x every runnable 2025 session.
# See models/PLAN.md, "The shadow baseline grid".
#
#   ./run_grid.sh                 # the whole grid
#   ./run_grid.sh --smoke         # 6 dates across the year, every config
#   ./run_grid.sh --configs A     # one grid only (A, B or C)
#   NJOBS=56 ./run_grid.sh
#
# A session that crashes costs one data point and is recorded in failures.csv
# with its exit code; it never stops the sweep.

set -u -o pipefail

KSPRPROJ=${KSPRPROJ:-$(cd "$(dirname "$0")/../.." && pwd)}
SRC=${SRC:-$KSPRPROJ/dbento_pcap_parse/scripts/out}
BIN_SRC=${BIN_SRC:-$KSPRPROJ/sim/src/sim}
OUT=${OUT:-/vast/home/vmayeski/gridruns/$(date +%Y%m%d_%H%M%S)}
NJOBS=${NJOBS:-56}
CONFIG_DIR=${CONFIG_DIR:-$KSPRPROJ/sim/config}
SEED=${SEED:-1}
GRID_SET=${GRID_SET:-1}   # 1 = full sweep (config_a); 2 = follow-up (config_b)
MIN_BIN_BYTES=${MIN_BIN_BYTES:-10000000}   # below this: weekend/holiday, no RTH

SMOKE=0; ONLY_GRID=""
while [ $# -gt 0 ]; do
  case "$1" in
    --smoke)   SMOKE=1 ;;
    --configs) ONLY_GRID="$2"; shift ;;
    *) echo "unknown arg: $1" >&2; exit 2 ;;
  esac
  shift
done

mkdir -p "$OUT"/{csv,log}
# Pin the binary. A rebuild mid-sweep would silently mix results from two
# different programs, which happened once before and cost a day's runs.
cp "$BIN_SRC" "$OUT/sim.pinned"
BIN="$OUT/sim.pinned"

# ---- the 29 configs ---------------------------------------------------
# name  grid  place_rate_bp  probe_size  ord_sz  delay_us  cancel_delay_us  max_dist
grid_tsv="$OUT/grid.tsv"

# Two grid sets. GRID_SET=1 is the full sweep and runs under config_a; GRID_SET=2
# is the follow-up under config_b, which is deliberately smaller.
#
# Set 2 drops grid A entirely -- the rate x size surface is established by set 1
# and does not need re-measuring at a second light count -- and starts B at 20
# lots, because the interesting question for the 12-light arm is what happens to
# LARGE parents when the shadow can rest at twice as many prices. Below 20 lots
# the two arms have little room to differ: the clip is capped by the shadowed
# order (73% of ES adds are 1 lot), so a small parent is filled in a handful of
# children either way.
{
  if [ "$GRID_SET" = "2" ]; then
    for q in 20 50 100 200; do
      printf 'Q%s\tB2\t300\t%s\t1\t500\t500\t-1\n' "$q" "$q"
    done
    for us in 0 100 200 400 500 800 1600 3200 6400; do
      printf 'lat%s\tC2\t300\t100\t-1\t%s\t%s\t-1\n' "$us" "$us" "$us"
    done
  else
    for bp in 50 100 300 500; do
      for sz in 1 10 100; do
        printf 'rate%s_sz%s\tA\t%s\t%s\t-1\t500\t500\t-1\n' "$bp" "$sz" "$bp" "$sz"
      done
    done
    for q in 1 2 5 10 20 50 100 200; do
      printf 'Q%s\tB\t300\t%s\t1\t500\t500\t-1\n' "$q" "$q"
    done
    for us in 0 100 200 400 500 800 1600 3200 6400; do
      printf 'lat%s\tC\t300\t100\t-1\t%s\t%s\t-1\n' "$us" "$us" "$us"
    done
  fi
  # Grid D (max_dist sweep) removed -- see the commit; a light holds one order at
  # one price, so there was no working-size cap for max_dist to relieve.
} > "$grid_tsv"

# ---- front month by date, from traded volume ---------------------------
# NOT a roll calendar. A hand-written one was 4-5 days early on every roll,
# which would have run part of the corpus in the illiquid contract: on
# 2025-03-13 ESH5 still traded 2,057,715 against ESM5's 318,192. The table is
# produced by volstats, which counts lastQty over the trade records and picks
# the most-traded outright. See models/PLAN.md, "Session inputs".
FRONT_TSV=${FRONT_TSV:-$KSPRPROJ/dbento_pcap_parse/scripts/front_month.310.tsv}
[ -f "$FRONT_TSV" ] || { echo "missing $FRONT_TSV -- regenerate with volstats" >&2; exit 2; }

# Looked up per call rather than held in an associative array: those do not
# survive into the xargs subshells, and one grep is nothing beside a 134s run.
front_month() {
  awk -F'\t' -v d="$1" '$1==d {print $2; exit}' "$FRONT_TSV"
}

# A run counts as done only if its CSV carries a fire, i.e. more than the header
# line. See the resume comment in run_one.
done_already() {
  [ -s "$1" ] && [ "$(wc -l < "$1")" -gt 1 ]
}

# ---- the runnable sessions -------------------------------------------
dates_all=()
for f in "$SRC"/bin/310/310.2025*.databento.bin; do
  [ -e "$f" ] || continue
  d=$(basename "$f" | cut -d. -f2)

  # Skip Sundays. A .bin spans 19:00 ET the previous evening -> 18:59 ET, so a
  # Sunday file covers Saturday evening through Sunday evening -- and the week
  # does not open until Sunday 18:00 ET, after the 16:30 ET cutoff below. There
  # is no 09:30-15:00 window in it, so the probe never fires: the run exits 0
  # with a header-only CSV and is recorded as a failure. Seven dates in 2025,
  # once per config, and they buried the one genuine crash in failures.csv.
  #
  # Day of week, NOT size: the ranges overlap. Sunday 2025-04-06 is 26 MB while
  # Presidents Day (a real session) is 20 MB and the July 4 half day is 24 MB,
  # so any threshold that drops the Sundays also drops real holiday sessions.
  [ "$(date -d "${d:0:4}-${d:4:2}-${d:6:2}" +%u)" = "7" ] && continue

  sz=$(stat -c %s "$f")
  [ "$sz" -ge "$MIN_BIN_BYTES" ] || continue     # weekend / holiday
  dates_all+=("$d")
done

if [ "$SMOKE" = 1 ]; then
  dates=()
  for pick in 20250115 20250310 20250612 20250815 20251031 20251222; do
    for d in "${dates_all[@]}"; do [ "$d" = "$pick" ] && dates+=("$d"); done
  done
else
  dates=("${dates_all[@]}")
fi

echo "grid   : $(wc -l < "$grid_tsv") configs${ONLY_GRID:+ (grid $ONLY_GRID only)}"
echo "dates  : ${#dates[@]} runnable 2025 sessions (of $(ls "$SRC"/bin/310/310.2025*.bin 2>/dev/null | wc -l))"
echo "out    : $OUT"
echo "jobs   : $NJOBS"

# ---- one run ----------------------------------------------------------
run_one() {
  local name=$1 gridid=$2 bp=$3 psz=$4 osz=$5 dly=$6 cdly=$7 mdist=$8 date=$9

  local csv="$OUT/csv/$name/$date.csv"
  local log="$OUT/log/$name/$date.log"
  # Resume. NOT `[ -s "$csv" ]`: --probe-out writes the header the instant the
  # sim opens the file, so every run that was in flight when a sweep was killed
  # leaves a non-empty, fire-less CSV behind. Testing for size alone would count
  # each of those as done and skip it forever -- a silent hole in the corpus,
  # with no failures.csv row to point at it. A finished run has at least one
  # fire, so require a second line.
  done_already "$csv" && return 0

  local contract; contract=$(front_month "$date")
  if [ -z "$contract" ]; then
    printf '%s,%s,%s,%s,"%s"\n' "$name" "$date" "no-front" "" \
      "no front month for this date in $FRONT_TSV" >> "$OUT/failures.csv"
    return 0
  fi

  # One universe for every session, so the corpus is not split between two
  # treatments. 152 of 265 per-date universes do not contain their own front
  # month -- the IR stream restarts its sequence each cycle, so which
  # definitions a date captured is arbitrary. Definitions are static anyway;
  # the only daily quantity is the price limit, and grid_universe widens each
  # contract's to the maximum seen in 2025 so the ladder cannot clip.
  local uni="$SRC/universe/310/grid_universe.310.json"
  # 16:30 ET, computed per date so the DST change on 2025-03-09 is handled.
  local cut; cut=$(TZ=America/New_York date -d "${date:0:4}-${date:4:2}-${date:6:2} 16:30:00" +%s)

  # No --ord-sz. Order size belongs to the arm's lights.ini; forcing it here
  # silently overrode whatever the config said, so a run labelled "arm A" was
  # not actually running arm A's sizing. Grid B's Q is the PARENT size
  # (--probe-size) and the clip comes from the config like everywhere else.

  local wd; wd=$(mktemp -d "${TMPDIR:-/tmp}/grid.$name.$date.XXXXXX")
  ( cd "$wd" && nice -n 19 ionice -c 3 "$BIN" \
      --datafile "$SRC/bin/310/310.$date.databento.bin" \
      --universe "$uni" \
      --contract "$contract" \
      --config   "$OUT/cfg/$name" \
      --end-ts   "$((cut * 1000000000))" \
      --probe-size "$psz" \
      --ob-delay-us "$dly" \
      --ob-cancel-delay-us "$cdly" \
      --probe-out "$csv" \
      --quiet ) > "$log" 2>&1
  local rc=$?

  # Keep the Logger's own file. The sim writes two different logs: stdout/stderr
  # (captured above as $log) and the Logger's sim_log.<date>.<pid>.log, written
  # into the working directory -- and the Logger's is the one carrying every
  # log_opr, log_err, _REJECT_ and _LIMIT_ line, i.e. everything --quiet was
  # kept for. Deleting $wd threw it away, so a failed session left only the
  # stderr tail, and grepping the surviving file for a diagnostic silently
  # found nothing whether or not the event had occurred.
  local slog; slog=$(ls -1 "$wd"/sim_log.*.log 2>/dev/null | head -1)
  [ -n "$slog" ] && mv "$slog" "$OUT/log/$name/$date.logger.log"
  rm -rf "$wd"

  if [ $rc -ne 0 ] || ! done_already "$csv"; then
    # One bad session is one missing data point, not a stopped sweep. Record
    # enough to reproduce it: config, date, exit code, and the last line of
    # the log, which is where an assert prints.
    printf '%s,%s,%s,%s,"%s"\n' "$name" "$date" "$rc" "$contract" \
      "$(tail -1 "$log" 2>/dev/null | tr -d '"' | cut -c1-300)" >> "$OUT/failures.csv"
    return 0
  fi
  return 0
}
export -f run_one front_month done_already
export OUT SRC BIN CONFIG_DIR SEED FRONT_TSV

echo "name,date,rc,contract,last_line" > "$OUT/failures.csv"

# ---- fan out ----------------------------------------------------------
started=$(date +%s)
while IFS=$'\t' read -r name gridid bp psz osz dly cdly mdist; do
  [ -n "$ONLY_GRID" ] && [ "$gridid" != "$ONLY_GRID" ] && continue
  mkdir -p "$OUT/csv/$name" "$OUT/log/$name" "$OUT/cfg/$name"

  # Materialise this cell's config instead of passing flags that override it.
  # Every value the sim reads is then in one file, on disk, next to the results
  # -- so "what was this run configured with" is answered by looking, not by
  # reconstructing a command line. It also means editing an arm's lights.ini
  # actually takes effect: --place-rate-bp and --ord-sz used to override the
  # config silently, so a run labelled with an arm was not necessarily running
  # that arm's settings.
  cp "$CONFIG_DIR"/*.ini "$OUT/cfg/$name/"
  set_key() {   # set_key <file> <key> <value> -- replace or append
    local f=$1 k=$2 v=$3
    if grep -qE "^$k " "$f"; then sed -i "s/^$k .*/$k $v/" "$f"
    else printf '%s %s\n' "$k" "$v" >> "$f"; fi
  }
  set_key "$OUT/cfg/$name/lights.ini" place_rate_bp "$bp"
  [ "$mdist" != "-1" ] && set_key "$OUT/cfg/$name/lights.ini" max_dist "$mdist"
  set_key "$OUT/cfg/$name/lights.ini" rng_seed "$SEED"
  printf '%s\n' "${dates[@]}" \
    | xargs -P "$NJOBS" -I{} bash -c 'run_one "$@"' _ \
        "$name" "$gridid" "$bp" "$psz" "$osz" "$dly" "$cdly" "$mdist" {}
  # Same definition as the resume guard: header-only files are not results.
  ok=0
  for c in "$OUT/csv/$name"/*.csv; do done_already "$c" && ok=$((ok + 1)); done
  echo "[$(date +%H:%M:%S)] $name: $ok/${#dates[@]} sessions  ($(( $(date +%s) - started ))s elapsed)"
done < "$grid_tsv"

nfail=$(( $(wc -l < "$OUT/failures.csv") - 1 ))
echo
echo "done in $(( $(date +%s) - started ))s"
echo "failures: $nfail  ->  $OUT/failures.csv"
[ "$nfail" -gt 0 ] && { echo "by config:"; tail -n +2 "$OUT/failures.csv" | cut -d, -f1 | sort | uniq -c | sort -rn | head; \
                        echo "by date:";   tail -n +2 "$OUT/failures.csv" | cut -d, -f2 | sort | uniq -c | sort -rn | head; }
echo "results: $OUT/csv/<config>/<date>.csv"
