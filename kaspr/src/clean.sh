#!/bin/bash
# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
#
# Licensed under the MIT License. See LICENSE file in the project root.

# Clean kaspr build artifacts

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "Cleaning kaspr build artifacts..."

# Remove nohup files
rm -f nohup.out nohup.out.* 2>/dev/null

# Remove log and audit files
rm -f *.log kaspr_log.* *.audit.csv 2>/dev/null

# Remove args files (assertion/signal dumps)
rm -f *.args 2>/dev/null

# Remove core dumps
rm -f core core.* 2>/dev/null

echo "Done."
