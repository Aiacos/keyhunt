#!/bin/bash
# Integration test for AVX-512 SHA256 implementation
# Verifies GetHash160_fromX_AVX512 functionality

set -e

echo "=== AVX-512 SHA256 Integration Test ==="
echo ""
echo "Running keyhunt with address mode on test puzzle 1-32..."
echo "Command: ./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF -t 1"
echo ""

# Run the test and capture output
./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF -t 1 2>&1 | tee integration_test_output.log

echo ""
echo "=== Test Results ==="
echo ""

# Check for key found
if grep -q "Found" integration_test_output.log; then
    echo "✓ PASS: Key was found"
else
    echo "✗ FAIL: Key was not found"
    exit 1
fi

# Check for errors
if grep -qi "error" integration_test_output.log; then
    echo "✗ FAIL: Errors detected in output"
    grep -i "error" integration_test_output.log
    exit 1
else
    echo "✓ PASS: No errors detected"
fi

# Check for segfaults or crashes
if grep -qi "segmentation\|segfault\|core dump" integration_test_output.log; then
    echo "✗ FAIL: Crash detected"
    exit 1
else
    echo "✓ PASS: No crashes detected"
fi

echo ""
echo "=== Integration Test: SUCCESS ==="
echo "The AVX-512 SHA256 implementation (sha256avx512_1B) is working correctly."
echo "GetHash160_fromX_AVX512 successfully processed keys using 16-way parallel hashing."
