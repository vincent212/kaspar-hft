#!/bin/sh
# Check the example field table, generate its header, and run the decode test.
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
python3 "$here/tools/fieldgen.py" "$here/examples/itch50.tbl" --check
python3 "$here/tools/fieldgen.py" "$here/examples/itch50.tbl" -o "$out/itch50.hpp"
${CXX:-c++} -std=c++20 -Wall -Wextra -Werror -I"$out" \
  "$here/examples/itch50_test.cpp" -o "$out/itch50_test"
"$out/itch50_test"
