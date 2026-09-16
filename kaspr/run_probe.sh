#!/usr/bin/env bash
# run_probe.sh — launch md_perf_meter (wire-to-book latency probe).
#
# Two modes.
#
# SOLO (default). Stop the live recorder, run the probe alone for a bounded
#   window, restart the live recorder. This is the mode you want:
#     - No SO_REUSEPORT question. Nothing else is bound to the multicast
#       groups, so there is no possibility of the kernel load-balancing
#       datagrams away from the recorder.
#     - No CPU contention. EF_POLL_USEC=3000 with EF_INT_DRIVEN=0 means
#       busy-spin; two spinning processes would measure each other.
#   COST: a hole in the recording for the length of the window. The restart
#   is in a trap EXIT, so it happens on Ctrl+C, on crash, and on error.
#
#   NOTE: solo mode no longer implies unpinned. This script does not pin
#   anything in solo mode -- the `taskset` PIN array is empty -- but the probe
#   pins its own actor threads from the config, via kaspr.general.tachbook_cpus
#   and the per-channel cme_cpus in cme.ini. `taskset -cp` on the PROCESS will
#   therefore still report 0-63 while the individual THREADS are pinned. Check
#   per-tid, not per-pid. If those keys are absent the probe runs unpinned,
#   exactly as the recorder runs.
#
# ALONGSIDE (-a). Leave the recorder up and run the probe next to it, pinned
#   off the recorder's cores. Only valid if the multicast fan-out question has
#   been settled; this script will not settle it for you. Kept for an A/B
#   against solo, since "does a second reader perturb the first" is itself a
#   measurable thing.
#
# ONLOAD PROFILE. The live recorder runs the *trading* profile. Confirmed by
# reading /proc/<pid>/environ of the running process, not by trusting a
# checked-in script:
#   EF_POLL_USEC=3000  EF_BUZZ_USEC=3000  EF_INT_DRIVEN=0
#   EF_STACK_LOCK_BUZZ=1  EF_STACK_PER_THREAD=1  EF_CLUSTER_SIZE=0
# The probe uses the same values. It has to: EF_POLL_USEC and EF_INT_DRIVEN
# set how long a thread spins before it blocks on an interrupt, and that
# interval lands inside the t0 we are measuring. The recording profile
# (EF_POLL_USEC=100, EF_INT_DRIVEN=1) would measure a different machine.
#
# BINARY NAME. The probe is the ordinary kaspr binary with perf_probe=true in
# its config -- there is no separate source tree. But it must NOT run under the
# name "kaspr": stop_kaspr.sh matches `pgrep -x kaspr`, so the Sunday cron (and
# this script's own solo-mode stop) would kill the probe mid-window, or worse,
# kill the probe while believing it had stopped the recorder. So the script
# copies kaspr -> md_perf_meter and runs that. The copy is refreshed whenever
# kaspr is newer, so it can never go stale against a rebuild.
#
# Usage:
#   kaspr/run_probe.sh                 # solo, onload ON, 300s window
#   kaspr/run_probe.sh -t 900          # 900s window
#   kaspr/run_probe.sh -a              # alongside live recorder (pinned)
#   kaspr/run_probe.sh -n              # onload OFF (kernel sockets; A/B)
#   kaspr/run_probe.sh --no-restart    # solo, but leave recorder down
#
# Env overrides:
#   KHPROJ     probe repo root (default /home/vincent/kh-probe)
#   LIVE_TREE  live repo root  (default /home/vincent/m2_kspr)
#   PROBE_CFG  cfg basename    (default md_perf.ini)
#   RUN_SECS   window seconds  (default 300; 0 = until Ctrl+C)
#   CPU_LIST   taskset list, ALONGSIDE mode only (default 24-31)

set -eu -o pipefail

readonly KHPROJ="${KHPROJ:-/home/vincent/kh-probe}"
readonly LIVE_TREE="${LIVE_TREE:-/home/vincent/m2_kspr}"
readonly PROBE_DIR="$KHPROJ/kaspr"
readonly BUILD_BIN="$PROBE_DIR/src/kaspr"
readonly PROBE_BIN="$PROBE_DIR/src/md_perf_meter"
readonly LIVE_STOP="$LIVE_TREE/kaspr/stop_kaspr.sh"
readonly LIVE_START="$LIVE_TREE/kaspr/run_local.sh"

CFG_BASENAME="${PROBE_CFG:-md_perf.ini}"
USE_ONLOAD=1
MODE=solo
RESTART_LIVE=1
CPU_LIST="${CPU_LIST:-24-31}"
RUN_SECS="${RUN_SECS:-300}"
OUTDIR="${OUTDIR:-$HOME/perf/mdperf}"

usage() { sed -n '2,54p' "$0" >&2; exit "${1:-0}"; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        -a|--alongside) MODE=alongside; shift ;;
        -s|--solo)      MODE=solo; shift ;;
        -n|--no-onload) USE_ONLOAD=0; shift ;;
        --no-restart)   RESTART_LIVE=0; shift ;;
        -t|--secs)      RUN_SECS="$2"; shift 2 ;;
        -c|--config)    CFG_BASENAME="$2"; shift 2 ;;
        -p|--cpus)      CPU_LIST="$2"; shift 2 ;;
        -h|--help)      usage 0 ;;
        -*)             echo "unknown flag: $1" >&2; usage 1 ;;
        *)              CFG_BASENAME="$1"; shift ;;
    esac
done

readonly PROBE_CFG_PATH="$PROBE_DIR/config/$CFG_BASENAME"

# --- Preconditions ---------------------------------------------------------

[[ -f "$PROBE_CFG_PATH" ]] || { echo "ERROR: cfg not found: $PROBE_CFG_PATH" >&2; exit 1; }

[[ -x "$BUILD_BIN" ]] || {
    echo "ERROR: kaspr binary missing: $BUILD_BIN" >&2
    echo "  build: $KHPROJ/build.sh -C kaspr/src USE_TACHBOOK=1" >&2
    echo "  (USE_TACHBOOK=1 is required -- the probe subscribes to TachBook,"
    echo "   and without that define create_probes() is not even compiled in)" >&2
    exit 1
}

# Refresh the renamed copy if the build is newer. See BINARY NAME above: the
# rename is what keeps stop_kaspr.sh's `pgrep -x kaspr` from matching us.
if [[ ! -x "$PROBE_BIN" || "$BUILD_BIN" -nt "$PROBE_BIN" ]]; then
    cp -f "$BUILD_BIN" "$PROBE_BIN"
    echo "probe binary  : refreshed from $(basename "$BUILD_BIN")"
fi

# The probe must not be mistaken for the recorder by anything that greps ps.
[[ "$(basename "$PROBE_BIN")" != "kaspr" ]] || {
    echo "ERROR: probe binary is named 'kaspr'; stop_kaspr.sh would kill it." >&2
    exit 1
}

# perf_probe must actually be on, or the run is a recorder with no samples.
grep -Eq '^[[:space:]]*perf_probe[[:space:]]+true' "$PROBE_CFG_PATH" || {
    echo "ERROR: $PROBE_CFG_PATH does not set 'perf_probe true'." >&2
    echo "       Without it kaspr runs as an ordinary recorder and writes no samples." >&2
    exit 1
}
grep -Eq '^[[:space:]]*tachbook[[:space:]]+true' "$PROBE_CFG_PATH" || {
    echo "ERROR: $PROBE_CFG_PATH does not set 'tachbook true'." >&2
    echo "       The probe subscribes to TachBook; with no books there is nothing to measure." >&2
    exit 1
}

LIVE_PID="$(pgrep -x kaspr || true)"
if [[ -n "$LIVE_PID" ]]; then
    echo "live recorder : pid=$LIVE_PID exe=$(readlink -f "/proc/$LIVE_PID/exe")"
else
    echo "live recorder : not running"
fi

# ===========================================================================
# SOLO
# ===========================================================================
if [[ "$MODE" == "solo" ]]; then
    [[ -x "$LIVE_STOP"  ]] || { echo "ERROR: missing $LIVE_STOP" >&2; exit 1; }
    [[ -x "$LIVE_START" ]] || { echo "ERROR: missing $LIVE_START" >&2; exit 1; }

    # Restart the recorder no matter how we leave: normal exit, error under
    # `set -e`, or Ctrl+C. Registered BEFORE the stop, so a failure inside
    # stop_kaspr.sh still triggers a restart attempt.
    restart_live() {
        local rc=$?
        if [[ "$RESTART_LIVE" == "1" ]]; then
            echo
            echo "--- restarting live recorder ---"
            # run_local.sh -o -d: onload on, daemonized. Same invocation as
            # the Sunday 16:00 crontab entry.
            "$LIVE_START" -o -d || echo "ERROR: recorder restart FAILED — start it by hand" >&2
            sleep 3
            local np
            np="$(pgrep -x kaspr || true)"
            if [[ -n "$np" ]]; then
                echo "live recorder back up, pid $np"
                grep -c onload "/proc/$np/maps" >/dev/null 2>&1 \
                    && echo "  onload maps present: bypass active" \
                    || echo "  WARNING: no onload maps — running on kernel sockets" >&2
            else
                echo "ALERT: live recorder did NOT come back up" >&2
            fi
        else
            echo "--no-restart given: recorder left DOWN. Start it with:" >&2
            echo "  $LIVE_START -o -d" >&2
        fi
        exit $rc
    }

    if [[ -n "$LIVE_PID" ]]; then
        echo
        echo "--- stopping live recorder (pid $LIVE_PID) ---"
        echo "    recording gap starts now; window is ${RUN_SECS}s"
        trap restart_live EXIT INT TERM
        "$LIVE_STOP"
    else
        trap restart_live EXIT INT TERM
        echo "nothing to stop."
    fi

    PIN=()   # unpinned: run the probe exactly as the recorder runs
else
# ===========================================================================
# ALONGSIDE
# ===========================================================================
    [[ -n "$LIVE_PID" ]] || {
        echo "ERROR: -a given but no live recorder is running. Use solo mode." >&2
        exit 1
    }
    echo
    echo "WARNING: alongside mode. Both processes bind the same multicast groups."
    echo "         chutil/include/chutil/udp_socket.hpp:49 sets SO_REUSEPORT, so the"
    echo "         bind succeeds. Whether the kernel fans out or load-balances the"
    echo "         datagrams is NOT settled here. If it load-balances, this run is"
    echo "         stealing packets from the recorder."

    # Collision guard 1: MQ0 / ZMQ port (kaspr.general.mqport, default 9001).
    PROBE_MQPORT="$(awk '/^[[:space:]]*mqport/ {print $2; exit}' "$PROBE_CFG_PATH" 2>/dev/null || true)"
    PROBE_MQPORT="${PROBE_MQPORT:-9001}"; PROBE_MQPORT="${PROBE_MQPORT//\"/}"
    if ss -ltnH "sport = :$PROBE_MQPORT" 2>/dev/null | grep -q .; then
        echo "ERROR: mqport $PROBE_MQPORT already bound (the live recorder)." >&2
        echo "       Set a different 'mqport' in $PROBE_CFG_PATH, e.g. 9101." >&2
        exit 1
    fi
    echo "mqport        : $PROBE_MQPORT (free)"

    # Collision guard 2: bin recorder output file. Two writers on one L3 file
    # interleave records and corrupt both runs.
    PROBE_REC="$(awk '/^[[:space:]]*(binrec_file|rec_file|outfile)/ {print $2; exit}' "$PROBE_CFG_PATH" 2>/dev/null || true)"
    PROBE_REC="${PROBE_REC//\"/}"
    if [[ -n "$PROBE_REC" ]]; then
        if ls -l "/proc/$LIVE_PID/fd" 2>/dev/null | awk '{print $NF}' | grep -Fxq "$PROBE_REC"; then
            echo "ERROR: live kaspr holds $PROBE_REC open. Change the probe's output path." >&2
            exit 1
        fi
        echo "binrec out    : $PROBE_REC (not held by live pid)"
    fi

    # Collision guard 3: do not share cores with a busy-spinning recorder.
    LIVE_CPUS="$(taskset -cp "$LIVE_PID" 2>/dev/null | sed 's/.*: //' || echo '?')"
    echo "live cpus     : $LIVE_CPUS"
    echo "probe cpus    : $CPU_LIST"
    [[ "$LIVE_CPUS" == "$CPU_LIST" ]] && {
        echo "ERROR: probe and recorder would share a CPU list; both busy-spin." >&2
        exit 1
    }
    PIN=(taskset -c "$CPU_LIST")
fi

mkdir -p "$OUTDIR"

# --- Config-driven thread pinning ------------------------------------------
# Read back what the probe will actually do, so the banner and the env log
# describe the run instead of describing the script's own taskset (which is
# empty in solo mode). Purely informational; the probe reads these itself.
CFG_TB_CPUS="$(awk '/^[[:space:]]*tachbook_cpus/ {print $2; exit}' "$PROBE_CFG_PATH" 2>/dev/null || true)"
CFG_TB_CPUS="${CFG_TB_CPUS:-none}"
CME_INI="$PROBE_DIR/config/cme.ini"
CFG_CME_CPUS="$(awk '/^[[:space:]]*cme_cpus/ {print $2}' "$CME_INI" 2>/dev/null | paste -sd' ' - || true)"
CFG_CME_CPUS="${CFG_CME_CPUS:-none}"

# --- Runtime libs ----------------------------------------------------------
GCCLIB="$(dirname "$(g++ -print-file-name=libstdc++.so)")"
export LD_LIBRARY_PATH="$GCCLIB:/usr/local/lib:/usr/local/lib64:/usr/local/boost188/lib:${LD_LIBRARY_PATH:-}"
export KSPRPROJ="$KHPROJ"

ulimit -S -c unlimited || true

# --- OpenOnload ------------------------------------------------------------
# Values copied from the LIVE process environment. Re-verify with:
#   tr '\0' '\n' < /proc/$(pgrep -x kaspr)/environ | grep '^EF_'
ONLOAD_PREFIX=()
if [[ "$USE_ONLOAD" == "1" ]]; then
    command -v onload >/dev/null || { echo "ERROR: 'onload' not in PATH" >&2; exit 1; }
    lsmod 2>/dev/null | grep -q '^onload ' || \
        echo "WARNING: onload kernel module not loaded; falling back to kernel sockets" >&2

    export EF_POLL_USEC=3000
    export EF_BUZZ_USEC=3000
    export EF_INT_DRIVEN=0
    export EF_UL_EPOLL=1
    export EF_STACK_PER_THREAD=1
    export EF_STACK_LOCK_BUZZ=1
    export EF_CLUSTER_SIZE=0
    export EF_FORCE_TCP_NODELAY=1
    export EF_UDP_SNDBUF=8388608
    export EF_UDP_RCVBUF=16777216
    export EF_RXQ_SIZE=4096
    export EF_TXQ_SIZE=2048
    export EF_MAX_PACKETS=524288
    export EF_PIO=enable
    export EF_PIO_THRESHOLD=56
    export EF_VI_LOG_LEVEL=0

    ONLOAD_PREFIX=(onload)
fi

# Record the exact environment next to the samples. A latency number without
# the EF_* set that produced it is not reproducible.
#
# Every command in this block is failure-tolerant ON PURPOSE. It runs AFTER the
# recorder has been stopped, so under `set -e -o pipefail` any non-zero here
# aborts the script during the recording gap -- the window is lost and the only
# artifact is a truncated env file. Two ways that bit:
#   - `onload --version | head -1`: head closes the pipe after one line, onload
#     dies of SIGPIPE, pipefail surfaces 141.
#   - `env | grep '^EF_'`: grep exits 1 when nothing matches, which is exactly
#     the -n (onload off) case.
STAMP="$(date +%Y%m%d_%H%M%S)"
ENVLOG="$OUTDIR/env_$STAMP.txt"
{
    echo "probe_bin   $PROBE_BIN"
    echo "probe_cfg   $PROBE_CFG_PATH"
    echo "mode        $MODE"
    echo "onload      $USE_ONLOAD"
    echo "run_secs    $RUN_SECS"
    echo "taskset     $( [[ "$MODE" == "solo" ]] && echo none || echo "$CPU_LIST" )"
    echo "tb_cpus     $CFG_TB_CPUS"
    echo "cme_cpus    $CFG_CME_CPUS"
    echo "kernel      $(uname -r)"
    echo "started     $(date -Is)"
    # sed -n '1p' consumes all input, so the writer never sees SIGPIPE.
    if [[ "$USE_ONLOAD" == "1" ]]; then
        onload --version 2>&1 | sed -n '1p' || true
    fi
    env | sed -n '/^EF_/p' | sort || true
} > "$ENVLOG" || true

echo "==================================================================="
echo "  md_perf_meter — wire-to-book probe"
echo "  binary  : $PROBE_BIN"
echo "  cfg     : $PROBE_CFG_PATH"
echo "  mode    : $MODE"
echo "  onload  : $( [[ "$USE_ONLOAD" == "1" ]] && echo ON || echo off )"
echo "  taskset : $( [[ "$MODE" == "solo" ]] && echo none || echo "$CPU_LIST" )"
echo "  tb_cpus : $CFG_TB_CPUS"
echo "  cme_cpus: $CFG_CME_CPUS   (recovery,msgproc,msgbuf,sock_a,sock_b per chan)"
echo "  window  : $( [[ "$RUN_SECS" == "0" ]] && echo "until Ctrl+C" || echo "${RUN_SECS}s" )"
echo "  env log : $ENVLOG"
echo "==================================================================="

cd "$PROBE_DIR"

# SIGTERM, not SIGKILL, at the end of the window: the probe's shutdown
# handlers must run so the bin recorder is flushed and closed. A SIGKILL here
# truncates the samples.
if [[ "$RUN_SECS" == "0" ]]; then
    "${PIN[@]}" "${ONLOAD_PREFIX[@]}" "$PROBE_BIN" "config/$CFG_BASENAME" || true
else
    "${PIN[@]}" "${ONLOAD_PREFIX[@]}" "$PROBE_BIN" "config/$CFG_BASENAME" &
    PROBE_PID=$!
    ( sleep "$RUN_SECS"; kill -TERM "$PROBE_PID" 2>/dev/null || true ) &
    TIMER_PID=$!
    wait "$PROBE_PID" || true
    kill "$TIMER_PID" 2>/dev/null || true
fi

echo "probe window complete. env: $ENVLOG"
# In solo mode the EXIT trap restarts the recorder from here.
