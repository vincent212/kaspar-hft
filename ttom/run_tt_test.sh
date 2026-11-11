#!/usr/bin/env bash
# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
#
# Licensed under the MIT License. See LICENSE file in the project root.
#
# Run tt_logon against a TT FIX session.
#   ttom/run_tt_test.sh <cfg>                                  # logon only
#   ttom/run_tt_test.sh <cfg> <cfi> <secid> <sym> <px> <qty> <B|S>   # plus one order
# <cfg> is a file in ttom/config (default tt_uat.cfg). The config directory must
# also hold TT-FIX44.xml and, for TLS, TTFIX.crt; see ttom/README.md.

set -eu -o pipefail

readonly KSPRPROJ="${KSPRPROJ:?set KSPRPROJ to the kaspar-hft checkout}"
readonly CFG_DIR="$KSPRPROJ/ttom/config"
readonly TT_LOGON="$KSPRPROJ/ttom/test/tt_logon"
readonly CFG_FILE="${1:-tt_uat.cfg}"
shift || true

[[ -x "$TT_LOGON" ]] || { echo "ERROR: $TT_LOGON not built (cd ttom/test && make opt)"; exit 1; }
[[ -f "$CFG_DIR/$CFG_FILE" ]] || { echo "ERROR: no config $CFG_DIR/$CFG_FILE (copy tt_example.cfg)"; exit 1; }
[[ -f "$CFG_DIR/TT-FIX44.xml" ]] || { echo "ERROR: $CFG_DIR/TT-FIX44.xml missing; download it from TT"; exit 1; }

export LD_LIBRARY_PATH="${QUICKFIX_PREFIX:-/usr/local}/lib:${QUICKFIX_PREFIX:-/usr/local}/lib64:${LD_LIBRARY_PATH:-}"
cd "$CFG_DIR"
mkdir -p log filestore
exec "$TT_LOGON" "$CFG_FILE" "$@"
