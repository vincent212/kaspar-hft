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
#   ./build.sh schema          # generate the CME SBE codecs (pinned versions)
#   ./build.sh check-schema    # just verify the codecs are present
#   ./build.sh debug           # debug build
#   ./build.sh clean           # clean
#
# Builds with -j<all cores> by default (the codecs are generated first, so the
# check-schema race doesn't apply). Override with JOBS=8 ./build.sh, or pass
# your own -j and it is left alone.
#   ./build.sh -C actors/cpp   # build only the actor framework (any make args pass through)
#
# Anything after the first argument is passed straight to make.

set -euo pipefail

usage() {
    cat <<'EOF_HELP'
build.sh -- build kaspar-hft. Sets KSPRPROJ and the external-library paths, then
forwards everything to make. Run it from anywhere; no exports needed.

USAGE
    ./build.sh [TARGET] [make args...]

TARGETS
    (none)          full build == all == install: check-schema, check-msgids, libs, kaspr
    install         same as the default
    debug           debug build (libs + binaries with -O0 -DDEBUG, the 'g' suffix)
    clean           remove objects, libraries, binaries and the .P dependency files
    test            build and run the Google Test suite (needs GTEST_PATH)
    schema          generate the CME SBE codecs (pinned versions)
    check-schema    verify the generated codecs are present, generate nothing
    msgids          print the hand-assigned message-id allocation report
    check-msgids    audit hand-assigned message ids for collisions
    depend          regenerate the .P dependency files
    libo|libd|libc  libraries only: opt | debug | clean

ENVIRONMENT
    JOBS=N          parallelism; defaults to nproc (this box: 59).
                    Passing your own -j suppresses the default.
    KSPRPROJ        forced to this script's own directory; do not set it.

EXAMPLES
    ./build.sh                      # full optimized build, all cores
    ./build.sh debug                # debug build
    JOBS=8 ./build.sh               # limit parallelism
    ./build.sh clean && ./build.sh  # from scratch
    ./build.sh test                 # run the unit tests
    ./build.sh -C actors/cpp        # one directory; any make args pass through

NOTES
    Stale .P files (repo moved or renamed) are detected and purged automatically.
    The SBE codecs are generated on demand if missing, before anything compiles.
EOF_HELP
}

case "${1:-}" in
    -h|--help|help) usage; exit 0 ;;
esac


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
        if ! ls mdp3_sbe/*.h >/dev/null 2>&1 || ! ls ilink3_sbe/*.h >/dev/null 2>&1; then
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

# Parallelism. The codecs are guaranteed present by the block above (or the
# target doesn't compile), so the check-schema/compile race the README warns
# about can't happen here — that caveat applies to running `make -j` by hand on
# a fresh, un-generated tree, not to this script. Default to all cores; override
# with JOBS=N, or pass your own -j and we won't add one.
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
jflag=(-j"$JOBS")
for a in "$@"; do
    case "$a" in
        -j|-j*|--jobs|--jobs=*) jflag=() ;;   # caller specified their own
    esac
done

echo "[build] make ${jflag[*]} $*"
exec make "${jflag[@]}" "$@"
