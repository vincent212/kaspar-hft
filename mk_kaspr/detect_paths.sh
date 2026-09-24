#!/bin/bash
#
# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Licensed under the MIT License. See LICENSE file in the project root.
#
# detect_paths.sh — auto-detect third-party library prefixes for kaspar.
#
# Searches the common prefix locations (system paths, /usr/local, ~/local,
# /opt/*, and Homebrew on macOS) for each library mk_kaspr/glob_begin.mk
# consumes, and emits a shell-sourceable `export` block on stdout.
#
# Usage:
#
#   ./mk_kaspr/detect_paths.sh                    # print export block to stdout
#   ./mk_kaspr/detect_paths.sh >> ~/.bashrc       # append to bashrc (review first)
#   eval "$(./mk_kaspr/detect_paths.sh)"          # apply to the current shell
#   ./mk_kaspr/detect_paths.sh --check            # table of findings + install hints
#   ./mk_kaspr/detect_paths.sh --verbose          # log every probed path to stderr
#
# The first probe that matches in the first prefix that contains it wins.

set -u

mode=emit
verbose=0
for arg in "$@"; do
    case "$arg" in
        --check|-c)   mode=check ;;
        --verbose|-v) verbose=1 ;;
        --help|-h)
            sed -n '6,21p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *)
            echo "unknown flag: $arg (use --help)" >&2
            exit 2
            ;;
    esac
done

log() { [ "$verbose" -eq 1 ] && echo "  $*" >&2 || true; }

# Candidate prefixes, highest priority first. Home-dir builds shadow
# /usr/local, which shadows /opt, which shadows /usr.
PREFIXES_GENERIC=(
    "$HOME/local"
    "/usr/local"
    "/opt/local"
    "/usr"
)

BREW_ROOT=/opt/homebrew/opt

# detect_with_probes <var> <brew-pkg> <extra-prefix...> -- <probe...>
# Tries each (prefix x probe) combo; first match wins. Prints the prefix.
detect_with_probes() {
    local var="$1" brew_pkg="$2"
    shift 2
    local -a extras=()
    while [ $# -gt 0 ] && [ "$1" != "--" ]; do
        extras+=("$1"); shift
    done
    [ "${1:-}" = "--" ] && shift
    local -a probes=("$@")

    local -a candidates=()
    if [ -n "$brew_pkg" ] && [ -d "$BREW_ROOT/$brew_pkg" ]; then
        candidates+=("$BREW_ROOT/$brew_pkg")
    fi
    candidates+=("${extras[@]}" "${PREFIXES_GENERIC[@]}")

    for pfx in "${candidates[@]}"; do
        for probe in "${probes[@]}"; do
            if [ -e "$pfx/$probe" ]; then
                log "$var: matched $probe at $pfx"
                echo "$pfx"; return 0
            fi
            log "$var: no $probe at $pfx"
        done
    done
    return 1
}

# ----- per-library detectors ----------------------------------------

# BOOST_PATH feeds BOTH -I$(BOOST_PATH)/include and -L$(BOOST_PATH)/lib
# (glob_begin.mk:97 and :169), so a prefix that satisfies only the header probe
# is not usable. Probing the header alone produced a prefix that compiled every
# TU and then failed at the link with
#
#     /usr/bin/ld: cannot find -lboost_program_options
#
# on a box where /usr/local/include/boost is a symlink into /usr/local/boost188
# while /usr/local/lib holds no boost libraries at all. The header was found,
# the libraries were somewhere else, and the error named the library rather
# than the prefix — so it read as "boost is not installed" when boost was
# installed and merely elsewhere. Require both halves of the prefix.
boost_prefix_ok() {
    local pfx="$1"
    if [ ! -e "$pfx/include/boost/version.hpp" ]; then
        log "BOOST_PATH: no include/boost/version.hpp at $pfx"
        return 1
    fi
    # Check lib/ only, because that is literally what -L is built from. A
    # lib64-only install is rejected here on purpose: a clear "not found" with
    # an install hint beats a link error that names the wrong thing.
    #
    # program_options specifically, not "any boost lib": it is the one the link
    # needs and the one a partial install is most likely to be missing.
    local f
    for f in "$pfx"/lib/libboost_program_options.so* \
             "$pfx"/lib/libboost_program_options.a; do
        if [ -e "$f" ]; then
            log "BOOST_PATH: header and lib both at $pfx"
            return 0
        fi
    done
    log "BOOST_PATH: header at $pfx but no lib/libboost_program_options"
    return 1
}

# Boost: builds often version the prefix (boost190, boost188), under a home
# dir or under /usr/local. Enumerate newest-first (version sort) so a newer
# install shadows an older one.
detect_boost() {
    local -a versioned=()
    for d in "$HOME"/local/boost* /usr/local/boost* /opt/boost*; do
        [ -d "$d" ] && versioned+=("$d")
    done
    if [ ${#versioned[@]} -gt 0 ]; then
        mapfile -t versioned < <(printf '%s\n' "${versioned[@]}" | sort -Vr)
    fi

    local -a candidates=()
    [ -d "$BREW_ROOT/boost" ] && candidates+=("$BREW_ROOT/boost")
    candidates+=("${versioned[@]}" "${PREFIXES_GENERIC[@]}")

    local pfx
    for pfx in "${candidates[@]}"; do
        if boost_prefix_ok "$pfx"; then
            echo "$pfx"; return 0
        fi
    done
    return 1
}

detect_zlib()    { detect_with_probes ZLIB_PATH    zlib          -- include/zlib.h; }
detect_gsl()     { detect_with_probes GSL_PATH     gsl "$HOME"/local/gsl /usr/local/gsl \
                       -- include/gsl/gsl_version.h include/gsl/gsl_math.h; }
detect_cppzmq()  { detect_with_probes CPPZMQ_PATH  cppzmq        -- include/zmq.hpp; }
detect_zmq()     { detect_with_probes ZMQ_PATH     zeromq        -- include/zmq.h; }
detect_json()    { detect_with_probes JSON_PATH    nlohmann-json -- include/nlohmann/json.hpp; }
detect_gtest()   { detect_with_probes GTEST_PATH   googletest    -- include/gtest/gtest.h; }

# Crypto++: cryptlib.h is the stable anchor header; config.h/version.h vary
# across releases.
detect_cryptopp() {
    detect_with_probes CRYPTOPP_PATH cryptopp \
        -- include/cryptopp/cryptlib.h include/cryptopp/config.h include/cryptopp/version.h
}

# LOCAL_LIB_PATH: any prefix that has a populated lib/ dir.
detect_local_lib() {
    for pfx in "$HOME/local" "/usr/local"; do
        [ -d "$pfx/lib" ] && { echo "$pfx"; return 0; }
    done
    return 1
}

# ----- install-hint text ---------------------------------------------

install_hint() {
    case "$1" in
        BOOST_PATH)    printf '      apt: libboost-all-dev | dnf: boost-devel | brew: boost\n      source: https://www.boost.org/users/download/ (need 1.88+)\n' ;;
        ZLIB_PATH)     printf '      apt: zlib1g-dev | dnf: zlib-devel | brew: zlib\n' ;;
        GSL_PATH)      printf '      apt: libgsl-dev | dnf: gsl-devel | brew: gsl\n' ;;
        CPPZMQ_PATH)   printf '      apt: cppzmq-dev | dnf: cppzmq-devel | brew: cppzmq\n' ;;
        ZMQ_PATH)      printf '      apt: libzmq3-dev | dnf: zeromq-devel | brew: zeromq\n' ;;
        JSON_PATH)     printf '      apt: nlohmann-json3-dev | dnf: json-devel | brew: nlohmann-json\n' ;;
        CRYPTOPP_PATH) printf '      apt: libcrypto++-dev | dnf: cryptopp-devel | brew: cryptopp\n' ;;
        GTEST_PATH)    printf '      apt: libgtest-dev | dnf: gtest-devel | brew: googletest\n' ;;
        LOCAL_LIB_PATH) printf '      No action needed — points at a prefix with a lib/ dir (default /usr/local).\n' ;;
    esac
}

# ----- run detection --------------------------------------------------

declare -A FOUND
FOUND[BOOST_PATH]=$(detect_boost       || echo "")
FOUND[ZLIB_PATH]=$(detect_zlib         || echo "")
FOUND[GSL_PATH]=$(detect_gsl           || echo "")
FOUND[CPPZMQ_PATH]=$(detect_cppzmq     || echo "")
FOUND[ZMQ_PATH]=$(detect_zmq           || echo "")
FOUND[JSON_PATH]=$(detect_json         || echo "")
FOUND[CRYPTOPP_PATH]=$(detect_cryptopp || echo "")
FOUND[GTEST_PATH]=$(detect_gtest       || echo "")
FOUND[LOCAL_LIB_PATH]=$(detect_local_lib || echo "")

VARS=(BOOST_PATH ZLIB_PATH GSL_PATH CPPZMQ_PATH ZMQ_PATH JSON_PATH CRYPTOPP_PATH GTEST_PATH LOCAL_LIB_PATH)

# ----- output ---------------------------------------------------------

if [ "$mode" = "check" ]; then
    printf "%-16s  %s\n" VAR "DETECTED PREFIX"
    printf "%-16s  %s\n" "----" "---------------"
    missing=()
    for v in "${VARS[@]}"; do
        val=${FOUND[$v]:-}
        if [ -n "$val" ]; then
            printf "\033[32m%-16s\033[0m  %s\n" "$v" "$val"
        else
            printf "\033[31m%-16s\033[0m  %s\n" "$v" "(not found)"
            missing+=("$v")
        fi
    done
    if [ ${#missing[@]} -gt 0 ]; then
        echo >&2
        echo "==== ${#missing[@]} dependency prefix(es) not found ====" >&2
        for v in "${missing[@]}"; do
            echo "  * $v" >&2
            install_hint "$v" >&2
        done
        echo >&2
        echo "Install the package, or set the variable by hand:" >&2
        echo "    export <VAR>=/absolute/path/to/prefix   # e.g. BOOST_PATH=\$HOME/local/boost190" >&2
        exit 1
    fi
    echo >&2
    echo "All dependencies detected. Apply via:  eval \"\$(mk_kaspr/detect_paths.sh)\"" >&2
    exit 0
fi

# Default (emit) mode. `printf %q` quotes metacharacters safely.
cat <<'HEAD'
# Auto-generated by mk_kaspr/detect_paths.sh
# Append to ~/.bashrc, or source via:  eval "$(mk_kaspr/detect_paths.sh)"
HEAD
for v in "${VARS[@]}"; do
    val=${FOUND[$v]:-}
    if [ -n "$val" ]; then
        printf 'export %s=%q\n' "$v" "$val"
    else
        echo "# $v NOT DETECTED — see 'mk_kaspr/detect_paths.sh --check' for install hints"
    fi
done

# KSPRPROJ is the repo root — this script lives in <repo>/mk_kaspr/.
repo=$(cd "$(dirname "$0")/.." && pwd)
echo
echo "# Repo root (detected from this script's location)"
printf 'export KSPRPROJ=%q\n' "$repo"
