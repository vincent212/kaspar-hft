#!/usr/bin/env bash
#
# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Licensed under the MIT License. See LICENSE file in the project root.
#
# run.sh -- run bnsim for one symbol and market, restarting it if it exits.
#
#   run.sh <SYMBOL> [--futures] [bnsim args...]
#
# Each start gets its own directory, $BNRUNS/<SYMBOL>[_F]/<UTC start time>/,
# holding the probe CSV, the logs and stdout/stderr. A process that exits --
# a crash, or a kill -- is started again after 5 s in a new directory; the
# simulation state does not carry over. Stop with Ctrl-C (or kill this script).
#
# Probe size PROBE_SIZE chunks a leg (default 5); MTD's zmq port MQPORT (default
# none -- give each process its own); config sim/config_binance.
#
set -u

[ $# -ge 1 ] || { sed -n '7,15p' "$0"; exit 2; }
SYMBOL=$1; shift
FUT=""
if [ "${1:-}" = "--futures" ]; then FUT="--futures"; shift; fi

HERE=$(cd "$(dirname "$0")" && pwd)
BNSIM=${BNSIM:-$HERE/bnsim}
CONFIG=${CONFIG:-$HERE/../config_binance}
BNRUNS=${BNRUNS:-$HOME/bnruns}
NAME=$SYMBOL${FUT:+_F}

trap 'kill "$PID" 2>/dev/null; exit 0' INT TERM

while true; do
  DIR=$BNRUNS/$NAME/$(date -u +%Y%m%dT%H%M%SZ)
  mkdir -p "$DIR"
  echo "$(date -u +%FT%TZ) $NAME: starting in $DIR"
  (cd "$DIR" && exec "$BNSIM" --config "$CONFIG" --symbol "$SYMBOL" $FUT \
       --probe-size "${PROBE_SIZE:-5}" --probe-out probe.csv --mqport "${MQPORT:-0}" --quiet "$@" > out.txt 2> err.txt) &
  PID=$!
  wait "$PID"
  echo "$(date -u +%FT%TZ) $NAME: exited with $?, restarting in 5 s"
  sleep 5
done
