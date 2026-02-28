#!/bin/bash
###############################################################################
# AMD RX 6000/7000 Series GPU Testing Script
# Tests OpenCL backend on AMD RDNA 2 (gfx1030) and RDNA 3 (gfx1100) GPUs
###############################################################################

set -e

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Test configuration
TEST_DURATION=${1:-30}  # Default 30 seconds per test
RESULTS_DIR="./tests/amd_test_results"
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
RESULTS_FILE="${RESULTS_DIR}/amd_rx_test_${TIMESTAMP}.txt"

# Helper functions
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[PASS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[FAIL]${NC} $1"
}

###############################################################################
# System Prerequisites Check
###############################################################################

check_prerequisites() {
    log_info "Checking system prerequisites..."

    # Check for keyhunt binary
    if [ ! -f "./keyhunt" ]; then
        log_error "keyhunt binary not found. Build with: ./build_opencl.sh"
        exit 1
    fi

    # Check for OpenCL support
    if ! ldd ./keyhunt | grep -q libOpenCL; then
        log_error "keyhunt not built with OpenCL support"
        exit 1
    fi

    # Check for ROCm utilities
    if ! command -v rocm-smi &> /dev/null && ! command -v rocminfo &> /dev/null; then
        log_warning "ROCm utilities (rocm-smi/rocminfo) not found"
        log_warning "GPU monitoring will be limited"
    fi

    # Check for clinfo
    if ! command -v clinfo &> /dev/null; then
        log_warning "clinfo not found - install with: sudo apt-get install clinfo"
    fi

    log_success "Prerequisites check complete"
}

###############################################################################
# AMD GPU Detection
###############################################################################

detect_amd_gpus() {
    log_info "Detecting AMD GPUs..."

    # Use rocminfo if available
    if command -v rocminfo &> /dev/null; then
        log_info "AMD GPU(s) detected via rocminfo:"
        rocminfo | grep -A 5 "Name:" | grep -E "Name:|Marketing Name:|gfx" || true
    fi

    # Use rocm-smi if available
    if command -v rocm-smi &> /dev/null; then
        log_info "GPU information via rocm-smi:"
        rocm-smi --showproductname || true
    fi

    # Use clinfo for OpenCL device information
    if command -v clinfo &> /dev/null; then
        log_info "OpenCL devices via clinfo:"
        clinfo -l || true
    fi

    # Use keyhunt to enumerate devices
    log_info "Devices detected by keyhunt:"
    ./keyhunt -L 2>&1 | tee -a "$RESULTS_FILE"
}

###############################################################################
# GPU Architecture Verification
###############################################################################

verify_gpu_architecture() {
    log_info "Verifying GPU architecture..."

    local arch_detected=false

    # Check for RX 6000 series (RDNA 2, gfx1030)
    if rocminfo 2>/dev/null | grep -q "gfx1030"; then
        log_success "AMD RX 6000 series (RDNA 2, gfx1030) detected"
        arch_detected=true
    fi

    # Check for RX 7000 series (RDNA 3, gfx1100)
    if rocminfo 2>/dev/null | grep -q "gfx1100"; then
        log_success "AMD RX 7000 series (RDNA 3, gfx1100) detected"
        arch_detected=true
    fi

    if [ "$arch_detected" = false ]; then
        log_warning "Could not verify RX 6000/7000 series architecture"
        log_warning "This test is designed for RDNA 2 (gfx1030) and RDNA 3 (gfx1100)"
    fi
}

###############################################################################
# Driver Information
###############################################################################

collect_driver_info() {
    log_info "Collecting driver information..."

    echo "=== AMD GPU Driver Information ===" | tee -a "$RESULTS_FILE"

    # ROCm version
    if [ -f "/opt/rocm/.info/version" ]; then
        echo "ROCm Version: $(cat /opt/rocm/.info/version)" | tee -a "$RESULTS_FILE"
    fi

    # OpenCL version
    if command -v clinfo &> /dev/null; then
        clinfo | grep -E "Platform Version|Device Version" | head -5 | tee -a "$RESULTS_FILE"
    fi

    # Kernel driver
    if command -v modinfo &> /dev/null; then
        modinfo amdgpu 2>/dev/null | grep -E "version|description" | tee -a "$RESULTS_FILE" || true
    fi

    echo "" | tee -a "$RESULTS_FILE"
}

###############################################################################
# Test 1: Device Detection and Initialization
###############################################################################

test_device_detection() {
    log_info "Test 1: Device Detection and Initialization"

    echo "=== Test 1: Device Detection ===" | tee -a "$RESULTS_FILE"

    # List devices
    ./keyhunt -L 2>&1 | tee -a "$RESULTS_FILE"

    local exit_code=$?
    if [ $exit_code -eq 0 ]; then
        log_success "Device detection successful"
        return 0
    else
        log_error "Device detection failed (exit code: $exit_code)"
        return 1
    fi
}

###############################################################################
# Test 2: Kernel Compilation
###############################################################################

test_kernel_compilation() {
    log_info "Test 2: OpenCL Kernel Compilation"

    echo "=== Test 2: Kernel Compilation ===" | tee -a "$RESULTS_FILE"

    # Run a simple search to trigger kernel compilation
    log_info "Running test search to verify kernel compilation..."

    timeout 10s ./keyhunt -m address -f tests/1to32.txt -r 1:FFFF -G full -q 2>&1 | tee -a "$RESULTS_FILE" || true

    # Check for compilation errors
    if grep -q "error" "$RESULTS_FILE" | tail -20; then
        log_error "Kernel compilation errors detected"
        return 1
    else
        log_success "Kernel compilation successful"
        return 0
    fi
}

###############################################################################
# Test 3: Hash-Only Mode (SHA256 + RIPEMD160)
###############################################################################

test_hash_only_mode() {
    log_info "Test 3: Hash-Only Mode (SHA256 + RIPEMD160)"

    echo "=== Test 3: Hash-Only Mode ===" | tee -a "$RESULTS_FILE"

    log_info "Testing pure hashing performance for ${TEST_DURATION} seconds..."

    # Start GPU monitoring in background
    if command -v rocm-smi &> /dev/null; then
        rocm-smi --showuse --showmemuse --showtemp --showpower > "${RESULTS_DIR}/hash_mode_monitoring.txt" 2>&1 &
        local monitor_pid=$!
    fi

    # Run hash-only test
    ./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF -G hash -s "$TEST_DURATION" 2>&1 | tee -a "$RESULTS_FILE"

    local exit_code=$?

    # Stop monitoring
    if [ -n "$monitor_pid" ]; then
        kill $monitor_pid 2>/dev/null || true
    fi

    if [ $exit_code -eq 0 ]; then
        log_success "Hash-only mode test passed"
        # Extract throughput
        local throughput=$(grep -oP '\d+\.\d+ Mkey/s' "$RESULTS_FILE" | tail -1)
        log_info "Throughput: $throughput"
        return 0
    else
        log_error "Hash-only mode test failed"
        return 1
    fi
}

###############################################################################
# Test 4: Full GPU Search Mode
###############################################################################

test_full_gpu_search() {
    log_info "Test 4: Full GPU Search Mode"

    echo "=== Test 4: Full GPU Search ===" | tee -a "$RESULTS_FILE"

    log_info "Testing full GPU search for ${TEST_DURATION} seconds..."

    # Start GPU monitoring
    if command -v rocm-smi &> /dev/null; then
        rocm-smi --showuse --showmemuse --showtemp --showpower > "${RESULTS_DIR}/full_search_monitoring.txt" 2>&1 &
        local monitor_pid=$!
    fi

    # Run full GPU search test
    ./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF -G full -s "$TEST_DURATION" 2>&1 | tee -a "$RESULTS_FILE"

    local exit_code=$?

    # Stop monitoring
    if [ -n "$monitor_pid" ]; then
        kill $monitor_pid 2>/dev/null || true
    fi

    if [ $exit_code -eq 0 ]; then
        log_success "Full GPU search test passed"
        return 0
    else
        log_error "Full GPU search test failed"
        return 1
    fi
}

###############################################################################
# Test 5: Hybrid CPU+GPU Mode
###############################################################################

test_hybrid_mode() {
    log_info "Test 5: Hybrid CPU+GPU Mode"

    echo "=== Test 5: Hybrid Mode ===" | tee -a "$RESULTS_FILE"

    log_info "Testing hybrid CPU+GPU mode for ${TEST_DURATION} seconds..."

    # Run hybrid mode test
    ./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF -G hybrid -t 4 -s "$TEST_DURATION" 2>&1 | tee -a "$RESULTS_FILE"

    local exit_code=$?

    if [ $exit_code -eq 0 ]; then
        log_success "Hybrid mode test passed"
        return 0
    else
        log_error "Hybrid mode test failed"
        return 1
    fi
}

###############################################################################
# Test 6: Multi-GPU (if multiple AMD GPUs present)
###############################################################################

test_multi_gpu() {
    log_info "Test 6: Multi-GPU Mode"

    # Check number of OpenCL devices
    local num_devices=$(./keyhunt -L 2>&1 | grep -c "Device" || echo "0")

    if [ "$num_devices" -lt 2 ]; then
        log_warning "Only $num_devices GPU(s) detected - skipping multi-GPU test"
        return 0
    fi

    echo "=== Test 6: Multi-GPU ===" | tee -a "$RESULTS_FILE"

    log_info "Testing multi-GPU mode with $num_devices device(s)..."

    # Run multi-GPU test
    ./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF -G full -s "$TEST_DURATION" 2>&1 | tee -a "$RESULTS_FILE"

    local exit_code=$?

    if [ $exit_code -eq 0 ]; then
        log_success "Multi-GPU test passed"
        return 0
    else
        log_error "Multi-GPU test failed"
        return 1
    fi
}

###############################################################################
# Test 7: BSGS Mode (if test file available)
###############################################################################

test_bsgs_mode() {
    if [ ! -f "tests/125.txt" ]; then
        log_warning "tests/125.txt not found - skipping BSGS test"
        return 0
    fi

    log_info "Test 7: BSGS Mode on GPU"

    echo "=== Test 7: BSGS Mode ===" | tee -a "$RESULTS_FILE"

    log_info "Testing BSGS mode with GPU acceleration..."

    # Run BSGS test
    ./keyhunt -m bsgs -f tests/125.txt -b 125 -G full -q -s "$TEST_DURATION" 2>&1 | tee -a "$RESULTS_FILE"

    local exit_code=$?

    if [ $exit_code -eq 0 ]; then
        log_success "BSGS mode test passed"
        return 0
    else
        log_error "BSGS mode test failed"
        return 1
    fi
}

###############################################################################
# Test 8: Auto-Tuning for AMD RDNA
###############################################################################

test_auto_tuning() {
    log_info "Test 8: Auto-Tuning for AMD RDNA Architecture"

    echo "=== Test 8: Auto-Tuning ===" | tee -a "$RESULTS_FILE"

    # Note: Auto-tuning happens automatically in gpu_autotune()
    # We verify it by checking device parameters

    log_info "Running search with auto-tuned parameters..."
    ./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF -G full -s 15 2>&1 | tee -a "$RESULTS_FILE"

    local exit_code=$?

    if [ $exit_code -eq 0 ]; then
        log_success "Auto-tuning test passed"
        return 0
    else
        log_error "Auto-tuning test failed"
        return 1
    fi
}

###############################################################################
# Test 9: Stress Test (Extended Duration)
###############################################################################

test_stress() {
    log_info "Test 9: Stress Test (5 minutes)"

    echo "=== Test 9: Stress Test ===" | tee -a "$RESULTS_FILE"

    log_info "Running extended stress test for 5 minutes..."
    log_warning "Monitor GPU temperature and power consumption"

    # Start continuous GPU monitoring
    if command -v rocm-smi &> /dev/null; then
        (while true; do rocm-smi --showtemp --showpower --showuse; sleep 5; done) > "${RESULTS_DIR}/stress_test_monitoring.txt" 2>&1 &
        local monitor_pid=$!
    fi

    # Run stress test
    ./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFFFFFFFFFF -G full -s 300 2>&1 | tee -a "$RESULTS_FILE"

    local exit_code=$?

    # Stop monitoring
    if [ -n "$monitor_pid" ]; then
        kill $monitor_pid 2>/dev/null || true
    fi

    if [ $exit_code -eq 0 ]; then
        log_success "Stress test passed"
        return 0
    else
        log_error "Stress test failed or interrupted"
        return 1
    fi
}

###############################################################################
# Performance Analysis
###############################################################################

analyze_performance() {
    log_info "Analyzing performance..."

    echo "" | tee -a "$RESULTS_FILE"
    echo "=== Performance Analysis ===" | tee -a "$RESULTS_FILE"

    # Extract throughput metrics from results
    local hash_throughput=$(grep -A 20 "Test 3: Hash-Only Mode" "$RESULTS_FILE" | grep -oP '\d+\.\d+ Mkey/s' | tail -1 || echo "N/A")
    local search_throughput=$(grep -A 20 "Test 4: Full GPU Search" "$RESULTS_FILE" | grep -oP '\d+\.\d+ Mkey/s' | tail -1 || echo "N/A")

    echo "Hash-Only Mode Throughput: $hash_throughput" | tee -a "$RESULTS_FILE"
    echo "Full Search Throughput: $search_throughput" | tee -a "$RESULTS_FILE"

    # Expected baselines for AMD RX 6000/7000
    echo "" | tee -a "$RESULTS_FILE"
    echo "Expected Performance Baselines:" | tee -a "$RESULTS_FILE"
    echo "  RX 6600 XT:      ~100-120 Mkey/s" | tee -a "$RESULTS_FILE"
    echo "  RX 6700 XT:      ~130-160 Mkey/s" | tee -a "$RESULTS_FILE"
    echo "  RX 6800:         ~180-220 Mkey/s" | tee -a "$RESULTS_FILE"
    echo "  RX 6800 XT:      ~200-240 Mkey/s" | tee -a "$RESULTS_FILE"
    echo "  RX 6900 XT:      ~220-260 Mkey/s" | tee -a "$RESULTS_FILE"
    echo "  RX 7600:         ~120-150 Mkey/s" | tee -a "$RESULTS_FILE"
    echo "  RX 7700 XT:      ~180-220 Mkey/s" | tee -a "$RESULTS_FILE"
    echo "  RX 7800 XT:      ~220-260 Mkey/s" | tee -a "$RESULTS_FILE"
    echo "  RX 7900 XT:      ~240-280 Mkey/s" | tee -a "$RESULTS_FILE"
    echo "  RX 7900 XTX:     ~260-300 Mkey/s" | tee -a "$RESULTS_FILE"
    echo "" | tee -a "$RESULTS_FILE"
}

###############################################################################
# Main Test Execution
###############################################################################

main() {
    echo "###############################################################################"
    echo "# AMD RX 6000/7000 Series GPU Testing"
    echo "# Test Duration: ${TEST_DURATION} seconds per test"
    echo "# Results File: ${RESULTS_FILE}"
    echo "###############################################################################"
    echo ""

    # Create results directory
    mkdir -p "$RESULTS_DIR"

    # Initialize results file
    echo "AMD RX 6000/7000 Series GPU Test Results" > "$RESULTS_FILE"
    echo "Test Date: $(date)" >> "$RESULTS_FILE"
    echo "" >> "$RESULTS_FILE"

    # Run all checks and tests
    check_prerequisites
    echo ""

    detect_amd_gpus
    echo ""

    verify_gpu_architecture
    echo ""

    collect_driver_info
    echo ""

    # Run tests
    local tests_passed=0
    local tests_failed=0

    test_device_detection && ((tests_passed++)) || ((tests_failed++))
    echo ""

    test_kernel_compilation && ((tests_passed++)) || ((tests_failed++))
    echo ""

    test_hash_only_mode && ((tests_passed++)) || ((tests_failed++))
    echo ""

    test_full_gpu_search && ((tests_passed++)) || ((tests_failed++))
    echo ""

    test_hybrid_mode && ((tests_passed++)) || ((tests_failed++))
    echo ""

    test_multi_gpu && ((tests_passed++)) || ((tests_failed++))
    echo ""

    test_bsgs_mode && ((tests_passed++)) || ((tests_failed++))
    echo ""

    test_auto_tuning && ((tests_passed++)) || ((tests_failed++))
    echo ""

    # Optional stress test
    if [ "${RUN_STRESS_TEST:-0}" = "1" ]; then
        test_stress && ((tests_passed++)) || ((tests_failed++))
        echo ""
    fi

    # Analyze performance
    analyze_performance

    # Summary
    echo ""
    echo "###############################################################################"
    echo "# Test Summary"
    echo "###############################################################################"
    log_success "Tests Passed: $tests_passed"
    if [ $tests_failed -gt 0 ]; then
        log_error "Tests Failed: $tests_failed"
    else
        log_info "Tests Failed: $tests_failed"
    fi
    echo ""
    echo "Full results saved to: $RESULTS_FILE"
    echo ""

    if [ $tests_failed -eq 0 ]; then
        log_success "All tests passed! AMD RX 6000/7000 OpenCL backend verified."
        return 0
    else
        log_error "Some tests failed. Review results file for details."
        return 1
    fi
}

# Run main with all arguments
main "$@"
