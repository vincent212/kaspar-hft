#!/usr/bin/env bash
#
# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Licensed under the MIT License. See LICENSE file in the project root.
#
# Convenience wrapper: sets the environment the Makefiles need (KSPRPROJ and,
# if present, the external-library paths from mk_kaspr/detect_paths.sh) and
# forwards to make. Run it from anywhere — it anchors KSPRPROJ to its own
# location, so you never have to export anything by hand.
#
#   ./build.sh                 # full build (== make all): check-schema then install
#   ./build.sh schema          # generate the CME SBE codecs (pinned MDP3 v12 / iLink v8)
#   ./build.sh check-schema    # just verify the codecs are present
#   ./build.sh debug           # debug build
#   ./build.sh clean           # clean
#   ./build.sh -C actors/cpp   # build only the actor framework (any make args pass through)
#
# Anything after the first argument is passed straight to make.

set -euo pipefail

# Repo root = directory this script lives in.
KSPRPROJ="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export KSPRPROJ
echo "[build] KSPRPROJ=$KSPRPROJ"

# Auto-detect external library paths (Boost/GSL/ZMQ/...) if the helper exists.
# It only prints `export VAR=...` lines; failures are non-fatal (the Makefile
# has sensible ?= defaults for /usr and /usr/local).
DETECT="$KSPRPROJ/mk_kaspr/detect_paths.sh"
if [ -x "$DETECT" ]; then
    eval "$("$DETECT" 2>/dev/null || true)"
fi

# Default target is `all`; otherwise forward every argument to make.
if [ "$#" -eq 0 ]; then
    set -- all
fi

cd "$KSPRPROJ"
echo "[build] make $*"
exec make "$@"
