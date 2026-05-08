#!/bin/bash

# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
#
# Licensed under the MIT License. See LICENSE file in the project root.

set -x  # Enable command tracing

echo "=== genconfig.sh started at $(date) ==="

# Use m2_kaspr instead of m2
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
echo "Script directory: $SCRIPT_DIR"

echo "Changing to $SCRIPT_DIR..."
cd "$SCRIPT_DIR"
echo "Current directory: $(pwd)"

echo ""
echo "=== Running genconfig_ilink_3.py --env prod ==="
echo "Start time: $(date)"
./genconfig_ilink_3.py --env prod > ilink_prod.info
echo "Finished genconfig_ilink_3.py --env prod at $(date)"
echo "Exit code: $?"

echo ""
echo "=== Running genconfig_mdp3_3.py --env prod ==="
echo "Start time: $(date)"
./genconfig_mdp3_3.py --env prod > mdp3_prod.info
echo "Finished genconfig_mdp3_3.py --env prod at $(date)"
echo "Exit code: $?"

echo ""
echo "=== Running genconfig_ilink_3.py --env nrcert ==="
echo "Start time: $(date)"
./genconfig_ilink_3.py --env nrcert > ilink_nrcert.info
echo "Finished genconfig_ilink_3.py --env nrcert at $(date)"
echo "Exit code: $?"

echo ""
echo "=== Running genconfig_mdp3_3.py --env nrcert ==="
echo "Start time: $(date)"
./genconfig_mdp3_3.py --env nrcert > mdp3_nrcert.info
echo "Finished genconfig_mdp3_3.py --env nrcert at $(date)"
echo "Exit code: $?"

echo ""
echo "=== Running genconfig_ilink_3.py --env cert ==="
echo "Start time: $(date)"
./genconfig_ilink_3.py --env cert > ilink_cert.info
echo "Finished genconfig_ilink_3.py --env cert at $(date)"
echo "Exit code: $?"

echo ""
echo "=== Running genconfig_mdp3_3.py --env cert ==="
echo "Start time: $(date)"
./genconfig_mdp3_3.py --env cert > mdp3_cert.info
echo "Finished genconfig_mdp3_3.py --env cert at $(date)"
echo "Exit code: $?"

echo ""
echo "=== genconfig.sh completed at $(date) ==="
