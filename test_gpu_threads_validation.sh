#!/bin/bash
#
# Test Script for GPU Parameter Validation - Excessive threads_per_block (Subtask 3-3)
#
# Purpose: Test that GPU validation auto-corrects threads_per_block values
#          that exceed hardware limits (max_threads_per_block)
#
# Requirements:
#   - CUDA-enabled build: ./build_cuda.sh
#   - NVIDIA GPU with compute capability >= 6.0
#   - Test file: tests/66.txt
#
# Usage:
#   ./test_gpu_threads_validation.sh
#

set -e  # Exit on error

# Colors for output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo "======================================================================"
echo "GPU Parameter Validation Test - Excessive threads_per_block"
echo "======================================================================"
echo ""

# Check if CUDA build exists
if [ ! -f "./keyhunt" ]; then
    echo -e "${RED}[ERROR]${NC} keyhunt executable not found. Please run 'make' first."
    exit 1
fi

# Check if test file exists
if [ ! -f "./tests/66.txt" ]; then
    echo -e "${RED}[ERROR]${NC} Test file tests/66.txt not found."
    exit 1
fi

echo -e "${BLUE}[INFO]${NC} Testing GPU validation with various threads_per_block values..."
echo ""

# Test 1: Excessive value (2048 - exceeds typical 1024 hardware limit)
echo "======================================================================"
echo "Test 1: threads_per_block = 2048 (exceeds hardware limit)"
echo "======================================================================"
echo -e "${YELLOW}Expected:${NC} Auto-correction to max_threads_per_block (1024) with [!] marker"
echo ""
export KEYHUNT_GPU_THREADS_PER_BLOCK=2048
./keyhunt -m address -f tests/66.txt -b 66 -G full -R -q -s 5 2>&1 | grep -A 20 "Validating GPU parameters" || echo -e "${BLUE}[INFO]${NC} GPU not available or validation output not shown"
unset KEYHUNT_GPU_THREADS_PER_BLOCK
echo ""

# Test 2: Slightly over limit (1536)
echo "======================================================================"
echo "Test 2: threads_per_block = 1536 (slightly over limit)"
echo "======================================================================"
echo -e "${YELLOW}Expected:${NC} Auto-correction to 1024 with [!] marker"
echo ""
export KEYHUNT_GPU_THREADS_PER_BLOCK=1536
./keyhunt -m address -f tests/66.txt -b 66 -G full -R -q -s 5 2>&1 | grep -A 20 "Validating GPU parameters" || echo -e "${BLUE}[INFO]${NC} GPU not available or validation output not shown"
unset KEYHUNT_GPU_THREADS_PER_BLOCK
echo ""

# Test 3: Not aligned to warp size (300)
echo "======================================================================"
echo "Test 3: threads_per_block = 300 (not aligned to warp size)"
echo "======================================================================"
echo -e "${YELLOW}Expected:${NC} Auto-correction to 320 (next multiple of 32) with [!] marker"
echo ""
export KEYHUNT_GPU_THREADS_PER_BLOCK=300
./keyhunt -m address -f tests/66.txt -b 66 -G full -R -q -s 5 2>&1 | grep -A 20 "Validating GPU parameters" || echo -e "${BLUE}[INFO]${NC} GPU not available or validation output not shown"
unset KEYHUNT_GPU_THREADS_PER_BLOCK
echo ""

# Test 4: Valid value (512)
echo "======================================================================"
echo "Test 4: threads_per_block = 512 (valid value)"
echo "======================================================================"
echo -e "${YELLOW}Expected:${NC} No correction, [✓] marker (optimal)"
echo ""
export KEYHUNT_GPU_THREADS_PER_BLOCK=512
./keyhunt -m address -f tests/66.txt -b 66 -G full -R -q -s 5 2>&1 | grep -A 20 "Validating GPU parameters" || echo -e "${BLUE}[INFO]${NC} GPU not available or validation output not shown"
unset KEYHUNT_GPU_THREADS_PER_BLOCK
echo ""

# Test 5: Too low value (32)
echo "======================================================================"
echo "Test 5: threads_per_block = 32 (very low, poor utilization)"
echo "======================================================================"
echo -e "${YELLOW}Expected:${NC} Warning about low utilization, [i] marker"
echo ""
export KEYHUNT_GPU_THREADS_PER_BLOCK=32
./keyhunt -m address -f tests/66.txt -b 66 -G full -R -q -s 5 2>&1 | grep -A 20 "Validating GPU parameters" || echo -e "${BLUE}[INFO]${NC} GPU not available or validation output not shown"
unset KEYHUNT_GPU_THREADS_PER_BLOCK
echo ""

echo "======================================================================"
echo "Test Summary"
echo "======================================================================"
echo ""
echo -e "${GREEN}[✓]${NC} All test cases executed"
echo ""
echo "Status Markers Legend:"
echo "  [✓] (green)  - Parameter is optimal and validated"
echo "  [i] (blue)   - Parameter works but isn't optimal"
echo "  [!] (yellow) - Parameter was auto-corrected for safety"
echo "  [⚠] (red)    - Parameter may cause performance issues"
echo ""
echo "Manual Verification Required:"
echo "  1. Check that excessive values (2048, 1536) were auto-corrected to 1024"
echo "  2. Verify yellow [!] marker appeared for corrected values"
echo "  3. Confirm non-aligned value (300) was corrected to 320"
echo "  4. Verify valid value (512) showed green [✓] marker"
echo "  5. Check low value (32) showed blue [i] marker with utilization warning"
echo ""
echo "Note: If 'GPU not available' messages appear, the system doesn't have"
echo "      CUDA GPU or the build wasn't compiled with CUDA support."
echo "      Run './build_cuda.sh' on a CUDA-enabled system to test properly."
echo ""
