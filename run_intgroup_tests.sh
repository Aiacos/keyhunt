#!/bin/bash
#
# Test runner for IntGroup AVX2 unit tests
# This script builds and runs the test suite for AVX2 ModMulK1 correctness
#

set -e

echo "==================================="
echo "IntGroup AVX2 Test Suite"
echo "==================================="
echo ""

# Build the test suite
echo "Building test suite..."
make test_intgroup_avx2

# Run the tests
echo ""
echo "Running tests..."
./test_intgroup_avx2

echo ""
echo "All tests completed successfully!"
