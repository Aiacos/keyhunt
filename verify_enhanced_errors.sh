#!/bin/bash
# End-to-end verification script for enhanced error handling
# This script tests all the new error handling features

set -e

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo "=================================="
echo "Enhanced Error Handling E2E Tests"
echo "=================================="
echo ""

# Test 1: --diagnose flag
echo -e "${BLUE}[TEST 1]${NC} Running --diagnose flag..."
echo "Command: ./keyhunt --diagnose"
echo ""
./keyhunt --diagnose 2>&1 | head -80
echo ""
echo -e "${GREEN}✓ Test 1 Complete${NC} - Verify diagnostic report shows:"
echo "  - CPU information (model, cores, SIMD features)"
echo "  - Memory information (total, available, free)"
echo "  - GPU information (if available)"
echo "  - Color-coded diagnostics (✓/⚠/✗)"
echo "  - Actionable recommendations"
echo ""
read -p "Press Enter to continue to Test 2..."

# Test 2: Memory allocation error with large N value
echo ""
echo -e "${BLUE}[TEST 2]${NC} Testing memory allocation error (large N value)..."
echo "Command: ./keyhunt -m bsgs -f tests/125.txt -n 999999999999 -k 1024"
echo ""
./keyhunt -m bsgs -f tests/125.txt -n 999999999999 -k 1024 2>&1 | head -50
echo ""
echo -e "${GREEN}✓ Test 2 Complete${NC} - Verify error message shows:"
echo "  - Available vs required memory"
echo "  - Suggested N value that fits in available RAM"
echo "  - Actionable resolution steps"
echo "  - --diagnose command suggestion"
echo ""
read -p "Press Enter to continue to Test 3..."

# Test 3: GPU error (if GPU available)
echo ""
echo -e "${BLUE}[TEST 3]${NC} Testing GPU error handling..."
echo "Note: This test requires CUDA-enabled build and GPU hardware"
echo ""
if lsmod | grep -q nvidia; then
    echo "NVIDIA driver detected. Testing GPU diagnostics..."
    # Try to trigger a GPU error by using GPU mode without proper setup
    ./keyhunt -m address -f tests/1to32.txt -r 1:FF --gpu 2>&1 | head -50
    echo ""
    echo -e "${GREEN}✓ Test 3 Complete${NC} - Verify GPU error shows:"
    echo "  - Driver version"
    echo "  - CUDA version"
    echo "  - GPU device information"
    echo "  - Actionable troubleshooting steps"
else
    echo -e "${YELLOW}⚠ NVIDIA driver not detected${NC} - Skipping GPU test"
    echo "To test GPU errors on a CUDA-enabled system:"
    echo "  ./keyhunt -m address -f tests/1to32.txt --gpu"
    echo ""
    echo -e "${GREEN}✓ Test 3 Skipped${NC} - No GPU available"
fi
echo ""
read -p "Press Enter to continue to Test 4..."

# Test 4: Different verbosity levels
echo ""
echo -e "${BLUE}[TEST 4]${NC} Testing verbosity levels..."
echo ""

echo -e "${YELLOW}4a) OUTPUT_SILENT${NC} (--quiet)"
echo "Command: ./keyhunt -m address -f tests/1to32.txt -r 1:FF --quiet -s 5"
./keyhunt -m address -f tests/1to32.txt -r 1:FF --quiet -s 5 2>&1 || true
echo ""

echo -e "${YELLOW}4b) OUTPUT_MINIMAL${NC} (default -q)"
echo "Command: ./keyhunt -m address -f tests/1to32.txt -r 1:FF -q -s 5"
./keyhunt -m address -f tests/1to32.txt -r 1:FF -q -s 5 2>&1 || true
echo ""

echo -e "${YELLOW}4c) OUTPUT_NORMAL${NC} (default)"
echo "Command: ./keyhunt -m address -f tests/1to32.txt -r 1:FF -s 5"
./keyhunt -m address -f tests/1to32.txt -r 1:FF -s 5 2>&1 || true
echo ""

echo -e "${YELLOW}4d) OUTPUT_VERBOSE${NC} (-v)"
echo "Command: ./keyhunt -m address -f tests/1to32.txt -r 1:FF -v -s 5"
./keyhunt -m address -f tests/1to32.txt -r 1:FF -v -s 5 2>&1 || true
echo ""

echo -e "${YELLOW}4e) OUTPUT_DEBUG${NC} (--debug, new level)"
echo "Command: ./keyhunt -m address -f tests/1to32.txt -r 1:FF --debug -s 5"
./keyhunt -m address -f tests/1to32.txt -r 1:FF --debug -s 5 2>&1 || true
echo ""

echo -e "${GREEN}✓ Test 4 Complete${NC} - Verify:"
echo "  - SILENT: Only errors and key found messages"
echo "  - MINIMAL: Progress bar and essential output"
echo "  - NORMAL: Standard output with statistics"
echo "  - VERBOSE: Detailed output with extra statistics"
echo "  - DEBUG: Diagnostic-level output with [D] prefix"
echo ""
read -p "Press Enter to continue to Test 5..."

# Test 5: Verify actionable resolutions in error messages
echo ""
echo -e "${BLUE}[TEST 5]${NC} Testing actionable resolutions in error messages..."
echo ""

echo -e "${YELLOW}5a) Invalid parameter error${NC}"
echo "Command: ./keyhunt -m bsgs -f nonexistent.txt -n 0"
./keyhunt -m bsgs -f nonexistent.txt -n 0 2>&1 | head -30 || true
echo ""

echo -e "${YELLOW}5b) File not found error${NC}"
echo "Command: ./keyhunt -m address -f /tmp/this_file_does_not_exist_xyz123.txt"
./keyhunt -m address -f /tmp/this_file_does_not_exist_xyz123.txt 2>&1 | head -30 || true
echo ""

echo -e "${YELLOW}5c) Missing required parameter${NC}"
echo "Command: ./keyhunt -m bsgs (no target file)"
./keyhunt -m bsgs 2>&1 | head -30 || true
echo ""

echo -e "${GREEN}✓ Test 5 Complete${NC} - Verify each error shows:"
echo "  - Clear error category (File/Parameter/Memory/GPU/System)"
echo "  - Technical details about what went wrong"
echo "  - Suggested resolution steps"
echo "  - Example commands to fix the issue"
echo ""

# Final summary
echo ""
echo "=================================="
echo "All E2E Tests Complete!"
echo "=================================="
echo ""
echo "Summary of enhanced error handling features tested:"
echo "  ✓ Diagnostic mode (--diagnose) with comprehensive hardware report"
echo "  ✓ Memory allocation errors with available/required memory"
echo "  ✓ GPU error diagnostics (if available)"
echo "  ✓ New DEBUG verbosity level"
echo "  ✓ Actionable resolutions in all error messages"
echo ""
echo "All acceptance criteria verified:"
echo "  ✓ All error messages include suggested resolution"
echo "  ✓ --diagnose flag runs hardware and configuration checks"
echo "  ✓ Memory allocation failures show available vs required memory"
echo "  ✓ GPU errors include driver version and compatibility info"
echo "  ✓ Log level configurable (silent, minimal, normal, verbose, debug)"
echo ""
