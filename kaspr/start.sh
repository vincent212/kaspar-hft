#!/bin/bash
# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
#
# Licensed under the MIT License. See LICENSE file in the project root.

# Start kaspr trading system
# Kills any existing instance and starts fresh

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Kill any existing kaspr processes
pkill -9 kaspr 2>/dev/null && echo "Killed existing kaspr process" && sleep 1

# Start kaspr
echo "Starting kaspr..."
./src/kaspr config/kaspr.ini 2>&1 &
KASPR_PID=$!

sleep 2

if ps -p $KASPR_PID > /dev/null 2>&1; then
    echo ""
    echo "=== Kaspr running ==="
    echo "PID: $KASPR_PID"
    echo "ZMQ port: 9001"
    echo ""
    echo "To stop: kill $KASPR_PID"
    echo "Or run: pkill kaspr"
else
    echo "ERROR: Kaspr failed to start"
    exit 1
fi
