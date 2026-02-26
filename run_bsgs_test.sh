#!/bin/bash
# Simple wrapper to run BSGS integration test
# This script runs the keyhunt executable with BSGS mode

set -e

echo "=== Running BSGS Integration Test ==="
echo ""
echo "Test: ./keyhunt -m bsgs -f tests/125.txt -b 125 -q -s 10 -R"
echo ""

# Run the test
./keyhunt -m bsgs -f tests/125.txt -b 125 -q -s 10 -R

echo ""
echo "=== Test Complete ==="
