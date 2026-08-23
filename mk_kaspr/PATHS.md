<!--
    Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
    Licensed under the MIT License. See LICENSE file in the project root.
-->

# mk_kaspr — build-path environment variables

`mk_kaspr/glob_begin.mk` (and `actors/cpp/Makefile`) no longer hardcode
third-party library prefixes. Every externally-resolvable path uses GNU-make
`?=` assignment, so setting the same variable in your shell environment
overrides the default.

## What's overridable

| Variable | Points at | Default (Linux) | Default (macOS) |
|----------|-----------|-----------------|-----------------|
| `BOOST_PATH` | Boost prefix | `/usr/local` | `/opt/homebrew/opt/boost` |
| `ZLIB_PATH` | zlib prefix | `/usr` | `/opt/homebrew/opt/zlib` |
| `GSL_PATH` | GSL prefix | `/usr/local/gsl` | `/opt/homebrew/opt/gsl` |
| `CPPZMQ_PATH` | cppzmq (`zmq.hpp`) prefix | `/usr/local` | `/opt/homebrew/opt/cppzmq` |
| `ZMQ_PATH` | zeromq prefix | `/usr/local` | `/opt/homebrew/opt/zeromq` |
| `JSON_PATH` | nlohmann/json prefix | `/usr/local` | `/opt/homebrew/opt/nlohmann-json` |
| `CRYPTOPP_PATH` | Crypto++ prefix | `/usr/local` | `/opt/homebrew/opt/cryptopp` |
| `GTEST_PATH` | Google Test prefix (unit tests) | `/usr` | `/opt/homebrew/opt/googletest` |
| `LOCAL_LIB_PATH` | extra `-L`/rpath root for home-dir installs | `/usr/local` | `/usr/local` |

Every variable expects a **prefix** — Make appends `/include` or `/lib` as
needed. `KSPRPROJ` is still required (it points at the repo root).

## Easiest: auto-detect with `mk_kaspr/detect_paths.sh`

From a fresh checkout, run the detection script. It probes the common prefix
locations (`$HOME/local`, `/usr/local`, `/usr`, `/opt/local`, and
`/opt/homebrew/opt/<pkg>` on macOS) and emits a ready-to-source export block —
including `KSPRPROJ` for this checkout:

```bash
# Print what would be exported
./mk_kaspr/detect_paths.sh

# Apply to the current shell
eval "$(./mk_kaspr/detect_paths.sh)"

# Or append to ~/.bashrc (review first!)
./mk_kaspr/detect_paths.sh >> ~/.bashrc && source ~/.bashrc
```

Audit mode lists whether each library was found and where, with install hints
for anything missing:

```bash
./mk_kaspr/detect_paths.sh --check
```

Boost prefixes like `$HOME/local/boost190` / `boost188` are enumerated
newest-first, so a newer local install shadows an older one.

## Quick recipes

### Plain system install (`/usr`, `/usr/local`)

Nothing to set — the defaults work.

```bash
export KSPRPROJ=$(pwd)
make -j
```

### Home-directory prefixes (e.g. `$HOME/local/boost190`)

```bash
# ~/.bashrc
export KSPRPROJ=$HOME/kaspar-hft
export BOOST_PATH=$HOME/local/boost190
export JSON_PATH=$HOME/local
export CRYPTOPP_PATH=$HOME/local
export LOCAL_LIB_PATH=$HOME/local
```

### macOS with Homebrew (arm64)

Defaults already point at `/opt/homebrew/opt/<pkg>`; override only if a library
is installed elsewhere:

```bash
export BOOST_PATH=/opt/homebrew/opt/boost@1.90
```

## Why it used to be hardcoded

Earlier versions baked one developer's `$HOME/local` layout
(`/home/vmayeski/local/boost190`, …) into `=` assignments. That broke builds on
any other account and made merges noisy. `?=` leaves the assignment
conditional: set it in the environment and the build uses your values; unset it
and the build uses a sane default.

## Common gotchas

- `export …` in `.bashrc` only affects **new** shells — `source ~/.bashrc` or
  open a fresh terminal.
- Point a variable at the **prefix**, not at `.../include` or `.../lib`.
- Overriding one path doesn't force overrides elsewhere — set only the ones
  that differ from the defaults.
