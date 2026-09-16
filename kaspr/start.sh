#!/bin/bash
# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
#
# Licensed under the MIT License. See LICENSE file in the project root.

# Start kaspr trading system.
# Kills any existing instance and starts fresh, under OpenOnload.
#
# ONLOAD IS ON BY DEFAULT HERE. That is deliberate and it differs from
# run_local.sh, where onload is opt-in via -o. The reason: this script has no
# flags and no banner worth reading, so anything it does silently is what
# actually happens. Before 2026-09-16 it launched ./src/kaspr bare -- no
# onload prefix, no EF_* set, no LD_LIBRARY_PATH -- and printed "Kaspr
# running" either way. A kernel-socket run and a bypass run are different
# machines by roughly an order of magnitude at the socket read, and nothing
# in the output told them apart. Opt out with -n if you want the kernel path.
#
# EF_* VALUES. Copied from run_local.sh's onload block, which in turn was
# read out of the live process environment (the *trading* profile, not the
# recording one). Re-verify against a running recorder with:
#   tr '\0' '\n' < /proc/$(pgrep -x kaspr)/environ | grep '^EF_'
# EF_POLL_USEC and EF_INT_DRIVEN set how long a thread spins before it
# blocks on an interrupt. That interval lands inside the socket-read
# timestamp the latency probe uses as t0, so these are not free knobs.
#
# NOT the wrapper. kaspr_onload_wrapper.sh sources
# /home/vm/m2_kaspr/build/onload_rc_trading.sh -- a path that does not exist
# on this box. It is dead. Do not resurrect it.
#
# Usage:
#   kaspr/start.sh          # onload ON
#   kaspr/start.sh -n       # onload OFF (kernel sockets; A/B only)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR" || exit 1

USE_ONLOAD="${USE_ONLOAD:-1}"
case "${1:-}" in
    -n|--no-onload) USE_ONLOAD=0 ;;
    -h|--help)      sed -n '7,33p' "$0" >&2; exit 0 ;;
esac

# Kill any existing kaspr processes.
#
# NOTE: -9 skips the shutdown handlers, so a bin recorder in flight is left
# truncated at whatever byte it was on. m2_kspr/kaspr/stop_kaspr.sh does this
# properly (TERM, wait, then KILL). Use that if the recording matters.
#
# Also note this is `pkill kaspr`, not `pkill -x kaspr`: it is a substring
# match on the process name. md_perf_meter is deliberately named so it does
# NOT match -- see run_probe.sh, BINARY NAME.
pkill -9 kaspr 2>/dev/null && echo "Killed existing kaspr process" && sleep 1

# --- Runtime libs ----------------------------------------------------------
# The binary is linked against the /usr/local g++, not the system one. Without
# this it dies at load on a GLIBCXX version symbol.
GCCLIB="$(dirname "$(g++ -print-file-name=libstdc++.so)")"
export LD_LIBRARY_PATH="$GCCLIB:/usr/local/lib:/usr/local/lib64:${LD_LIBRARY_PATH:-}"
export KSPRPROJ="${KSPRPROJ:-$(cd "$SCRIPT_DIR/.." && pwd)}"

ulimit -S -c unlimited || true

# --- OpenOnload ------------------------------------------------------------
ONLOAD_PREFIX=()
if [[ "$USE_ONLOAD" == "1" ]]; then
    if ! command -v onload >/dev/null; then
        echo "ERROR: onload requested but 'onload' is not in PATH." >&2
        echo "       Run with -n to accept kernel sockets instead." >&2
        exit 1
    fi
    if ! lsmod 2>/dev/null | grep -q '^onload '; then
        echo "WARNING: the 'onload' kernel module is not loaded;" >&2
        echo "         onload will silently fall back to kernel sockets." >&2
    fi

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

# Start kaspr
echo "Starting kaspr (onload: $( [[ "$USE_ONLOAD" == "1" ]] && echo ON || echo off ))..."
"${ONLOAD_PREFIX[@]}" ./src/kaspr config/kaspr.ini 2>&1 &
KASPR_PID=$!

sleep 2

if ps -p $KASPR_PID > /dev/null 2>&1; then
    echo ""
    echo "=== Kaspr running ==="
    echo "PID: $KASPR_PID"
    echo "ZMQ port: 9001"

    # Do not take the prefix on faith: onload can exec the binary, fail to
    # accelerate, and leave a healthy-looking process on kernel sockets.
    #
    # Two different pieces of evidence, and they are NOT the same claim.
    # Verified 2026-09-16 with `onload sleep`: a process that never opens a
    # socket still maps libonload.so (5 regions). So libonload.so alone
    # proves only that the interposer loaded. What proves an accelerated
    # socket exists is a mapping of /dev/onload -- the stack device. A live
    # kaspr on the CME groups shows both, 77 regions in total.
    #
    # The 2s sleep above may land before the multicast joins, so "preloaded,
    # no stack yet" is a real and benign state. It is reported as itself
    # rather than folded into either of the other two.
    #
    # No `|| echo 0` on the grep -c below: `grep -c` PRINTS 0 and EXITS 1 when
    # nothing matches, so the fallback would append a second 0 and the
    # variable would hold "0\n0" -- which is not an integer and breaks the
    # comparison. The count is already 0 on no-match; only an unreadable
    # maps file yields empty, which the :-0 covers.
    MAPS="/proc/$KASPR_PID/maps"
    NMAPS="$(grep -ci onload "$MAPS" 2>/dev/null)"; NMAPS="${NMAPS:-0}"
    if grep -q '/dev/onload' "$MAPS" 2>/dev/null; then
        echo "onload: bypass ACTIVE -- stack device mapped ($NMAPS regions)"
    elif [[ "$NMAPS" -gt 0 ]]; then
        echo "onload: libonload.so preloaded, no stack yet ($NMAPS regions)"
        echo "        re-check once the feed is up:"
        echo "        grep -c /dev/onload /proc/$KASPR_PID/maps"
    elif [[ "$USE_ONLOAD" == "1" ]]; then
        echo "WARNING: no onload maps at all -- running on KERNEL SOCKETS" >&2
    else
        echo "onload: off (-n), kernel sockets"
    fi

    echo ""
    echo "To stop: kill $KASPR_PID      (TERM, so shutdown handlers run)"
    echo "Or run:  pkill -TERM -x kaspr"
else
    echo "ERROR: Kaspr failed to start"
    exit 1
fi
