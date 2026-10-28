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
#   ./build.sh schema          # generate the CME SBE codecs (latest CME templates)
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
    schema          generate the CME SBE codecs (latest CME templates; uses
                    genschema/*.xml instead of CME SFTP if present)
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

# Header-only deps (nlohmann/json, cppzmq): if the system doesn't have them,
# fetch pinned releases into $KSPRPROJ/.deps (git-ignored) and point the
# Makefiles there. No compiling, no root. Skipped for clean.
DEPS="$KSPRPROJ/.deps"
JSON_VERSION=3.11.3
CPPZMQ_VERSION=4.10.0
fetch_header() {  # <url> <dest path under $DEPS>
    mkdir -p "$(dirname "$DEPS/$2")"
    echo "[build] fetching $1"
    curl -fsSL --retry 2 -o "$DEPS/$2.tmp" "$1" && mv "$DEPS/$2.tmp" "$DEPS/$2"
}
case "${1:-}" in
    clean) ;;
    *)
        if [ -z "${JSON_PATH:-}" ]; then
            [ -f "$DEPS/include/nlohmann/json.hpp" ] || fetch_header \
                "https://github.com/nlohmann/json/releases/download/v$JSON_VERSION/json.hpp" \
                include/nlohmann/json.hpp
            export JSON_PATH="$DEPS"
        fi
        if [ -z "${CPPZMQ_PATH:-}" ]; then
            for h in zmq.hpp zmq_addon.hpp; do
                [ -f "$DEPS/include/$h" ] || fetch_header \
                    "https://raw.githubusercontent.com/zeromq/cppzmq/v$CPPZMQ_VERSION/$h" \
                    "include/$h"
            done
            export CPPZMQ_PATH="$DEPS"
        fi
        ;;
esac

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

# CME SBE codec generation. A template XML already sitting in genschema/ is
# used as-is (--template-file); otherwise it's fetched from CME's SFTP. That
# lets a box with no CME access build from templates copied in by hand.
CME_SFTP=sftpng.cmegroup.com
MDP3_XML=genschema/templates_FixBinary.xml
ILINK_XML=genschema/ilinkbinary.xml

gen_schemas() {
    local need_sftp=()
    for pair in "mdp3:$MDP3_XML" "ilink:$ILINK_XML"; do
        local schema="${pair%%:*}" xml="${pair#*:}"
        if [ -f "$xml" ]; then
            echo "[build] $schema: using local template $xml"
            python3 genschema/genschema.py --schema "$schema" --template-file "$xml" || return 1
        else
            need_sftp+=("$schema")
        fi
    done
    [ "${#need_sftp[@]}" -eq 0 ] && return 0
    # Fail fast with a useful message instead of genschema's ~3 min of retries.
    if ! python3 -c 'import paramiko' 2>/dev/null; then
        echo "[build] ERROR: Python paramiko is not installed (needed to fetch from CME)." >&2
        return 1
    fi
    if ! timeout 10 bash -c "</dev/tcp/$CME_SFTP/22" 2>/dev/null; then
        echo "[build] ERROR: cannot reach $CME_SFTP:22 from this machine." >&2
        return 1
    fi
    for schema in "${need_sftp[@]}"; do
        python3 genschema/genschema.py --schema "$schema" || return 1
    done
}

schema_help() {
    cat >&2 <<EOF_SCHEMA
[build] Could not generate the CME SBE codecs (mdp3_sbe/, ilink3_sbe/).

  They are built from CME's SBE templates, fetched over SFTP from $CME_SFTP
  (needs Python paramiko and outbound port 22 to CME). If this machine can't
  reach CME:

  * Check routing first: on a box with a private CME circuit, a static route
    for CME's 164.74.0.0/16 can send SFTP down that circuit, where it is
    dropped. 'ip route get \$(getent hosts $CME_SFTP | cut -d' ' -f1)' should
    leave via the internet gateway; if not, add a /32 host route via it.

  * Or copy the templates in by hand from any machine that can reach CME
    (user cmeconfig, password: SFTP_PASS in genschema/genschema.py):

      scp -s cmeconfig@$CME_SFTP:SBEFix/Production/Templates/templates_FixBinary.xml $KSPRPROJ/genschema/
      scp -s cmeconfig@$CME_SFTP:MSGW/Production/Templates/ilinkbinary.xml $KSPRPROJ/genschema/

    (or run 'scp -3 -s cmeconfig@$CME_SFTP:<path> <this-host>:$KSPRPROJ/genschema/'
    from a third machine), then re-run ./build.sh -- it uses them automatically.

  See genschema/README.md.
EOF_SCHEMA
}

# If we're about to build and the generated CME SBE codecs aren't there yet,
# generate them first instead of letting the check-schema guard fail. Skip this
# for targets that don't compile (or that are the codegen/verify step itself).
case "${1:-}" in
    schema)
        # Same path as the auto-generate below, so local templates are honoured.
        if [ "$#" -eq 1 ]; then
            gen_schemas || { schema_help; exit 1; }
            exit 0
        fi
        ;;
    check-schema|clean) ;;
    *)
        if ! ls mdp3_sbe/*.h >/dev/null 2>&1 || ! ls ilink3_sbe/*.h >/dev/null 2>&1; then
            echo "[build] CME SBE codecs missing — generating..."
            gen_schemas || { schema_help; exit 1; }
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
