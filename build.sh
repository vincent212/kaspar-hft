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

# The generated *.P dependency files bake in ABSOLUTE header paths. If the repo
# was moved or renamed, those paths no longer exist; because make -include's the
# .P files, it silently "gives up" with no diagnostic (Error 2, no message).
# Detect that by sampling one .P for a referenced path that no longer exists,
# and purge them all if so (make regenerates them on the next build).
# `|| true`: head closing the pipe early can hand find a SIGPIPE, which would
# abort the script under `set -o pipefail`.
sample_P="$( { find . -name '*.P' 2>/dev/null | head -1; } || true )"
if [ -n "$sample_P" ]; then
    stale=0
    while IFS= read -r hdr; do
        [ -z "$hdr" ] && continue
        [ -e "$hdr" ] || { stale=1; break; }
    done < <(grep -oE '/[^ 	\\]+\.(hpp|h)' "$sample_P" | sort -u)
    if [ "$stale" -eq 1 ]; then
        echo "[build] stale .P dependency files (repo moved?) — clearing"
        find . -name '*.P' -delete
    fi
fi

# If we're about to build and the generated CME SBE codecs aren't there yet,
# generate them first instead of letting the check-schema guard fail. Skip this
# for targets that don't compile (or that are the codegen/verify step itself).
case "${1:-}" in
    schema|check-schema|clean) ;;
    *)
        if ! ls mktdata_v12/*.h >/dev/null 2>&1 || ! ls ilink_v8/*.h >/dev/null 2>&1; then
            echo "[build] CME SBE codecs missing — generating (make schema)..."
            if ! make schema; then
                echo "[build] ERROR: could not generate the SBE codecs (needs Java," >&2
                echo "        Python paramiko, and CME network access — see" >&2
                echo "        genschema/README.md). Generate them, then re-run." >&2
                exit 1
            fi
        fi
        ;;
esac

echo "[build] make $*"
exec make "$@"
