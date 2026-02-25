#!/bin/bash

# Test GPU blocks_per_sm validation with suboptimal values
# Subtask 3-4: Test GPU validation with suboptimal blocks_per_sm

set -e

echo "========================================="
echo "GPU Blocks Per SM Validation Test"
echo "========================================="
echo ""

# Check if keyhunt exists
if [ ! -f "./keyhunt" ]; then
    echo "[ERROR] keyhunt executable not found. Build first with: ./build_cuda.sh"
    exit 1
fi

# Check if test file exists
if [ ! -f "tests/66.txt" ]; then
    echo "[ERROR] Test file tests/66.txt not found"
    exit 1
fi

echo "Test Case 1: Suboptimal blocks_per_sm = 1 (very low, should warn about underutilization)"
echo "-----------------------------------------------------------------------------------------"
export KEYHUNT_GPU_BLOCKS_PER_SM=1
echo "Command: KEYHUNT_GPU_BLOCKS_PER_SM=1 ./keyhunt -m address -f tests/66.txt -b 66 -G full -R -q"
echo ""
echo "Expected behavior:"
echo "  - [i] or [⚠] marker for blocks_per_sm"
echo "  - Warning message: 'Only 1 block/SM may underutilize GPU...'"
echo "  - Suggestion to use 2-X blocks for better occupancy"
echo ""
./keyhunt -m address -f tests/66.txt -b 66 -G full -R -q -s 1 || echo "[INFO] No GPU detected or test completed"
unset KEYHUNT_GPU_BLOCKS_PER_SM
echo ""

echo "Test Case 2: Suboptimal blocks_per_sm = 2 (low but acceptable)"
echo "----------------------------------------------------------------"
export KEYHUNT_GPU_BLOCKS_PER_SM=2
echo "Command: KEYHUNT_GPU_BLOCKS_PER_SM=2 ./keyhunt -m address -f tests/66.txt -b 66 -G full -R -q"
echo ""
echo "Expected behavior:"
echo "  - [✓] or [i] marker for blocks_per_sm"
echo "  - Should be accepted as reasonable"
echo ""
./keyhunt -m address -f tests/66.txt -b 66 -G full -R -q -s 1 || echo "[INFO] No GPU detected or test completed"
unset KEYHUNT_GPU_BLOCKS_PER_SM
echo ""

echo "Test Case 3: High blocks_per_sm = 64 (maximum allowed)"
echo "--------------------------------------------------------"
export KEYHUNT_GPU_BLOCKS_PER_SM=64
echo "Command: KEYHUNT_GPU_BLOCKS_PER_SM=64 ./keyhunt -m address -f tests/66.txt -b 66 -G full -R -q"
echo ""
echo "Expected behavior:"
echo "  - [⚠] marker for blocks_per_sm"
echo "  - Warning about register/shared memory pressure"
echo "  - Suggestion to use lower value"
echo ""
./keyhunt -m address -f tests/66.txt -b 66 -G full -R -q -s 1 || echo "[INFO] No GPU detected or test completed"
unset KEYHUNT_GPU_BLOCKS_PER_SM
echo ""

echo "Test Case 4: Optimal blocks_per_sm = 32 (typical optimal)"
echo "-----------------------------------------------------------"
export KEYHUNT_GPU_BLOCKS_PER_SM=32
echo "Command: KEYHUNT_GPU_BLOCKS_PER_SM=32 ./keyhunt -m address -f tests/66.txt -b 66 -G full -R -q"
echo ""
echo "Expected behavior:"
echo "  - [✓] marker for blocks_per_sm"
echo "  - Message: 'optimal for compute X.Y'"
echo ""
./keyhunt -m address -f tests/66.txt -b 66 -G full -R -q -s 1 || echo "[INFO] No GPU detected or test completed"
unset KEYHUNT_GPU_BLOCKS_PER_SM
echo ""

echo "========================================="
echo "Test Summary"
echo "========================================="
echo ""
echo "All test cases executed."
echo ""
echo "Manual verification checklist:"
echo "  [ ] Test Case 1: Saw warning about underutilization for blocks_per_sm=1"
echo "  [ ] Test Case 2: Accepted blocks_per_sm=2 as reasonable"
echo "  [ ] Test Case 3: Warned about resource pressure for blocks_per_sm=64"
echo "  [ ] Test Case 4: Confirmed blocks_per_sm=32 as optimal"
echo ""
echo "Note: If no GPU is detected, the validation code won't execute."
echo "      Run this test on a CUDA-enabled system to see actual validation output."
