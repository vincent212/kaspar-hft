#!/bin/bash
# Compile the C++ side of the hybrid example to object files:
#   - the generated bridge (interop/generated/cpp/CppActorBridge.cpp)
#   - the C++ actor setup (cpp/hybrid_cpp.cpp)
# Prereq: the C++ framework is built (cd actors/cpp && make opt) so libactors.a exists.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"     # .../interop/example/cpp
EX="$(dirname "$HERE")"                    # .../interop/example
REPO="$(cd "$EX/../../../.." && pwd)"      # repo root (example -> interop -> rust -> actors -> repo)
GEN="$EX/../generated/cpp"                 # .../interop/generated/cpp
MSGS="$EX/../messages"                     # .../interop/messages
BOOST_INC="${BOOST_INC_DIR:-/opt/homebrew/opt/boost/include}"

INC="-I$REPO/actors/cpp/include -I$REPO/chutil/include -I$GEN -I$MSGS -I$BOOST_INC"

g++ -std=gnu++20 -O3 -c $INC "$HERE/hybrid_cpp.cpp"        -o "$HERE/hybrid_cpp.o"
g++ -std=gnu++20 -O3 -c $INC "$GEN/CppActorBridge.cpp"     -o "$HERE/CppActorBridge.o"
echo "compiled: $HERE/hybrid_cpp.o  $HERE/CppActorBridge.o"
