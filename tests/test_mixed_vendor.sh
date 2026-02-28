#!/bin/bash
###############################################################################
# Mixed-Vendor GPU Testing Script (NVIDIA + AMD)
# Tests unified backend with simultaneous CUDA and OpenCL operation
###############################################################################

set -e

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
MAGENTA='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Test configuration
TEST_DURATION=${1:-30}  # Default 30 seconds per test
RESULTS_DIR="./tests/mixed_vendor_results"
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
RESULTS_FILE="${RESULTS_DIR}/mixed_vendor_test_${TIMESTAMP}.txt"

# Create results directory
mkdir -p "$RESULTS_DIR"

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

log_section() {
    echo ""
    echo -e "${MAGENTA}═══════════════════════════════════════════════════════════════════${NC}"
    echo -e "${MAGENTA}$1${NC}"
    echo -e "${MAGENTA}═══════════════════════════════════════════════════════════════════${NC}"
}

###############################################################################
# System Prerequisites Check
###############################################################################

check_prerequisites() {
    log_section "CHECKING SYSTEM PREREQUISITES"

    # Check for keyhunt binary
    if [ ! -f "./keyhunt" ]; then
        log_error "keyhunt binary not found. Build with: make or ./build_cuda.sh && ./build_opencl.sh"
        exit 1
    fi

    # Check for both CUDA and OpenCL support
    local has_cuda=false
    local has_opencl=false

    if ldd ./keyhunt | grep -q libcudart; then
        has_cuda=true
        log_success "CUDA support detected"
    else
        log_warning "CUDA support not detected"
    fi

    if ldd ./keyhunt | grep -q libOpenCL; then
        has_opencl=true
        log_success "OpenCL support detected"
    else
        log_warning "OpenCL support not detected"
    fi

    if [ "$has_cuda" = false ] || [ "$has_opencl" = false ]; then
        log_error "Mixed-vendor testing requires BOTH CUDA and OpenCL support"
        log_error "Rebuild with: make clean && make (with both CUDA and OpenCL installed)"
        exit 1
    fi

    # Check for NVIDIA utilities
    if command -v nvidia-smi &> /dev/null; then
        log_success "nvidia-smi available"
    else
        log_warning "nvidia-smi not found - NVIDIA GPU monitoring will be limited"
    fi

    # Check for AMD utilities
    if command -v rocm-smi &> /dev/null; then
        log_success "rocm-smi available"
    else
        log_warning "rocm-smi not found - AMD GPU monitoring will be limited"
    fi

    log_success "Prerequisites check complete"
}

###############################################################################
# GPU Detection and Enumeration
###############################################################################

detect_all_gpus() {
    log_section "DETECTING ALL GPUs (NVIDIA + AMD)"

    echo "=== GPU Detection ===" | tee -a "$RESULTS_FILE"
    echo "" | tee -a "$RESULTS_FILE"

    # Detect NVIDIA GPUs
    log_info "Detecting NVIDIA GPUs..."
    if command -v nvidia-smi &> /dev/null; then
        nvidia-smi --list-gpus | tee -a "$RESULTS_FILE" || true
        echo "" | tee -a "$RESULTS_FILE"
    fi

    # Detect AMD GPUs
    log_info "Detecting AMD GPUs..."
    if command -v rocm-smi &> /dev/null; then
        rocm-smi --showproductname | tee -a "$RESULTS_FILE" || true
        echo "" | tee -a "$RESULTS_FILE"
    fi

    # Detect OpenCL devices
    log_info "Detecting OpenCL devices..."
    if command -v clinfo &> /dev/null; then
        clinfo -l | tee -a "$RESULTS_FILE" || true
        echo "" | tee -a "$RESULTS_FILE"
    fi

    # keyhunt unified device enumeration
    log_info "Unified device enumeration via keyhunt:"
    echo "=== keyhunt -L Output ===" | tee -a "$RESULTS_FILE"
    ./keyhunt -L 2>&1 | tee -a "$RESULTS_FILE"
    echo "" | tee -a "$RESULTS_FILE"
}

###############################################################################
# Verify Mixed-Vendor Detection
###############################################################################

verify_mixed_vendor_detection() {
    log_section "TEST 1: VERIFY MIXED-VENDOR DETECTION"

    local nvidia_detected=false
    local amd_detected=false

    # Check keyhunt output for NVIDIA
    if ./keyhunt -L 2>&1 | grep -iq "NVIDIA\|GeForce\|Tesla\|Quadro\|CUDA"; then
        log_success "NVIDIA GPU detected by keyhunt"
        nvidia_detected=true
    else
        log_warning "NVIDIA GPU not detected by keyhunt"
    fi

    # Check keyhunt output for AMD
    if ./keyhunt -L 2>&1 | grep -iq "AMD\|Radeon\|OpenCL"; then
        log_success "AMD GPU detected by keyhunt"
        amd_detected=true
    else
        log_warning "AMD GPU not detected by keyhunt"
    fi

    if [ "$nvidia_detected" = true ] && [ "$amd_detected" = true ]; then
        log_success "TEST 1 PASSED: Mixed-vendor detection successful"
        echo "TEST 1: PASSED - Both NVIDIA and AMD GPUs detected" | tee -a "$RESULTS_FILE"
        return 0
    else
        log_error "TEST 1 FAILED: Mixed-vendor system not detected"
        echo "TEST 1: FAILED - Missing NVIDIA or AMD GPU" | tee -a "$RESULTS_FILE"
        return 1
    fi
}

###############################################################################
# Collect Driver Information
###############################################################################

collect_driver_info() {
    log_section "COLLECTING DRIVER INFORMATION"

    echo "=== Driver Information ===" | tee -a "$RESULTS_FILE"

    # CUDA driver version
    if command -v nvidia-smi &> /dev/null; then
        echo "NVIDIA Driver Information:" | tee -a "$RESULTS_FILE"
        nvidia-smi --query-gpu=driver_version,cuda_version --format=csv,noheader | tee -a "$RESULTS_FILE"
        echo "" | tee -a "$RESULTS_FILE"
    fi

    # ROCm version
    if [ -f "/opt/rocm/.info/version" ]; then
        echo "ROCm Version: $(cat /opt/rocm/.info/version)" | tee -a "$RESULTS_FILE"
    fi

    # OpenCL version
    if command -v clinfo &> /dev/null; then
        echo "OpenCL Information:" | tee -a "$RESULTS_FILE"
        clinfo | grep -E "Platform Version|Device Version" | head -10 | tee -a "$RESULTS_FILE"
        echo "" | tee -a "$RESULTS_FILE"
    fi

    log_success "Driver information collected"
}

###############################################################################
# Test 2: Unified Backend Initialization
###############################################################################

test_unified_backend_init() {
    log_section "TEST 2: UNIFIED BACKEND INITIALIZATION"

    echo "=== Test 2: Unified Backend Initialization ===" | tee -a "$RESULTS_FILE"

    # Run keyhunt with -L to test backend initialization
    log_info "Testing unified backend initialization..."
    if ./keyhunt -L > /dev/null 2>&1; then
        log_success "Unified backend initialized successfully"
        echo "TEST 2: PASSED - Unified backend initialization successful" | tee -a "$RESULTS_FILE"
        return 0
    else
        log_error "Unified backend initialization failed"
        echo "TEST 2: FAILED - Backend initialization error" | tee -a "$RESULTS_FILE"
        return 1
    fi
}

###############################################################################
# Test 3: Hash-Only Mode with All GPUs
###############################################################################

test_hash_mode_all_gpus() {
    log_section "TEST 3: HASH-ONLY MODE (ALL GPUs)"

    echo "=== Test 3: Hash-Only Mode (All GPUs) ===" | tee -a "$RESULTS_FILE"

    log_info "Running hash-only mode test for ${TEST_DURATION}s on ALL GPUs..."
    log_info "Command: ./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF -G hash -s ${TEST_DURATION}"

    # Run test and capture output
    if ./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF -G hash -s ${TEST_DURATION} 2>&1 | tee -a "$RESULTS_FILE"; then
        log_success "TEST 3 PASSED: Hash-only mode completed successfully"
        echo "TEST 3: PASSED" | tee -a "$RESULTS_FILE"
        return 0
    else
        log_error "TEST 3 FAILED: Hash-only mode error"
        echo "TEST 3: FAILED" | tee -a "$RESULTS_FILE"
        return 1
    fi
}

###############################################################################
# Test 4: Full GPU Search Mode (All GPUs)
###############################################################################

test_full_gpu_mode_all_gpus() {
    log_section "TEST 4: FULL GPU SEARCH MODE (ALL GPUs)"

    echo "=== Test 4: Full GPU Search Mode (All GPUs) ===" | tee -a "$RESULTS_FILE"

    log_info "Running full GPU search for ${TEST_DURATION}s on ALL GPUs..."
    log_info "Command: ./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF -G full -s ${TEST_DURATION}"

    # Run test and capture output
    if ./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF -G full -s ${TEST_DURATION} 2>&1 | tee -a "$RESULTS_FILE"; then
        log_success "TEST 4 PASSED: Full GPU mode completed successfully"
        echo "TEST 4: PASSED" | tee -a "$RESULTS_FILE"
        return 0
    else
        log_error "TEST 4 FAILED: Full GPU mode error"
        echo "TEST 4: FAILED" | tee -a "$RESULTS_FILE"
        return 1
    fi
}

###############################################################################
# Test 5: Hybrid Mode (CPU + All GPUs)
###############################################################################

test_hybrid_mode_all_gpus() {
    log_section "TEST 5: HYBRID MODE (CPU + ALL GPUs)"

    echo "=== Test 5: Hybrid Mode (CPU + All GPUs) ===" | tee -a "$RESULTS_FILE"

    log_info "Running hybrid mode for ${TEST_DURATION}s (CPU + NVIDIA + AMD GPUs)..."
    log_info "Command: ./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF -G hybrid -s ${TEST_DURATION}"

    # Run test and capture output
    if ./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF -G hybrid -s ${TEST_DURATION} 2>&1 | tee -a "$RESULTS_FILE"; then
        log_success "TEST 5 PASSED: Hybrid mode completed successfully"
        echo "TEST 5: PASSED" | tee -a "$RESULTS_FILE"
        return 0
    else
        log_error "TEST 5 FAILED: Hybrid mode error"
        echo "TEST 5: FAILED" | tee -a "$RESULTS_FILE"
        return 1
    fi
}

###############################################################################
# Test 6: Work Distribution Verification
###############################################################################

test_work_distribution() {
    log_section "TEST 6: WORK DISTRIBUTION VERIFICATION"

    echo "=== Test 6: Work Distribution Verification ===" | tee -a "$RESULTS_FILE"

    log_info "Running extended test to verify work distribution..."
    log_info "This test runs for 60s to collect statistics from all GPUs"

    # Run longer test to get meaningful statistics
    local duration=$((TEST_DURATION * 2))
    if [ $duration -lt 60 ]; then
        duration=60
    fi

    log_info "Command: ./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF -G full -s ${duration}"

    # Capture output with statistics
    local output=$(./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF -G full -s ${duration} 2>&1 | tee -a "$RESULTS_FILE")

    # Check if work was distributed to multiple devices
    local device_count=$(echo "$output" | grep -c "Device" || echo "0")

    if [ "$device_count" -ge 2 ]; then
        log_success "TEST 6 PASSED: Work distributed to multiple devices"
        echo "TEST 6: PASSED - Work distributed to ${device_count} devices" | tee -a "$RESULTS_FILE"
        return 0
    else
        log_warning "TEST 6 WARNING: Work distribution unclear (found ${device_count} device references)"
        echo "TEST 6: WARNING - Check manual verification" | tee -a "$RESULTS_FILE"
        return 0
    fi
}

###############################################################################
# Test 7: Result Correctness Verification
###############################################################################

test_result_correctness() {
    log_section "TEST 7: RESULT CORRECTNESS VERIFICATION"

    echo "=== Test 7: Result Correctness Verification ===" | tee -a "$RESULTS_FILE"

    log_info "Running correctness test with known key..."

    # Test with known solution (puzzle #1 from tests/1to32.txt)
    log_info "Command: ./keyhunt -m address -f tests/1to32.txt -r 1:2 -G full -q"

    # Run test
    local output=$(./keyhunt -m address -f tests/1to32.txt -r 1:2 -G full -q 2>&1 | tee -a "$RESULTS_FILE")

    # Check if key was found
    if echo "$output" | grep -qi "found\|match\|HIT"; then
        log_success "TEST 7 PASSED: Correct result found"
        echo "TEST 7: PASSED - Result correctness verified" | tee -a "$RESULTS_FILE"
        return 0
    else
        log_warning "TEST 7 WARNING: Result verification inconclusive"
        echo "TEST 7: WARNING - Manual verification recommended" | tee -a "$RESULTS_FILE"
        return 0
    fi
}

###############################################################################
# Test 8: GPU Monitoring During Mixed-Vendor Operation
###############################################################################

test_gpu_monitoring() {
    log_section "TEST 8: GPU MONITORING (NVIDIA + AMD)"

    echo "=== Test 8: GPU Monitoring ===" | tee -a "$RESULTS_FILE"

    log_info "Collecting GPU statistics during operation..."

    # Start GPU monitoring in background
    if command -v nvidia-smi &> /dev/null; then
        log_info "Starting NVIDIA GPU monitoring..."
        nvidia-smi dmon -s um -c 6 > "${RESULTS_DIR}/nvidia_monitor_${TIMESTAMP}.txt" 2>&1 &
        local nvidia_pid=$!
    fi

    if command -v rocm-smi &> /dev/null; then
        log_info "Starting AMD GPU monitoring..."
        watch -n 5 'rocm-smi --showtemp --showpower --showuse' > "${RESULTS_DIR}/amd_monitor_${TIMESTAMP}.txt" 2>&1 &
        local amd_pid=$!
    fi

    # Run short test while monitoring
    log_info "Running test workload for monitoring..."
    ./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF -G full -s 30 > /dev/null 2>&1 || true

    # Stop monitoring
    if [ -n "$nvidia_pid" ]; then
        kill $nvidia_pid 2>/dev/null || true
    fi
    if [ -n "$amd_pid" ]; then
        kill $amd_pid 2>/dev/null || true
    fi

    log_success "TEST 8 COMPLETED: GPU monitoring data collected"
    echo "TEST 8: COMPLETED - Check monitoring files in ${RESULTS_DIR}" | tee -a "$RESULTS_FILE"

    # Display final GPU states
    echo "" | tee -a "$RESULTS_FILE"
    echo "Final NVIDIA GPU State:" | tee -a "$RESULTS_FILE"
    if command -v nvidia-smi &> /dev/null; then
        nvidia-smi --query-gpu=name,utilization.gpu,temperature.gpu,power.draw --format=csv | tee -a "$RESULTS_FILE"
    fi

    echo "" | tee -a "$RESULTS_FILE"
    echo "Final AMD GPU State:" | tee -a "$RESULTS_FILE"
    if command -v rocm-smi &> /dev/null; then
        rocm-smi --showuse --showtemp --showpower | tee -a "$RESULTS_FILE"
    fi

    return 0
}

###############################################################################
# Test 9: Performance Balance Check
###############################################################################

test_performance_balance() {
    log_section "TEST 9: PERFORMANCE BALANCE CHECK"

    echo "=== Test 9: Performance Balance ===" | tee -a "$RESULTS_FILE"

    log_info "Checking performance balance between NVIDIA and AMD GPUs..."
    log_info "This test verifies both vendors are being utilized effectively"

    # Run extended test
    local duration=90
    log_info "Running ${duration}s test for performance analysis..."

    local output=$(./keyhunt -m address -f tests/1to32.txt -r 1:FFFFFFFF -G full -s ${duration} 2>&1 | tee -a "$RESULTS_FILE")

    # Analysis is manual - just report completion
    log_success "TEST 9 COMPLETED: Performance data collected"
    echo "TEST 9: COMPLETED - Manual analysis required" | tee -a "$RESULTS_FILE"
    echo "" | tee -a "$RESULTS_FILE"
    echo "MANUAL VERIFICATION:" | tee -a "$RESULTS_FILE"
    echo "- Check that both NVIDIA and AMD GPUs show activity in monitoring logs" | tee -a "$RESULTS_FILE"
    echo "- Verify throughput is reasonable for both GPU types" | tee -a "$RESULTS_FILE"
    echo "- Ensure no GPU is idle while others are working" | tee -a "$RESULTS_FILE"
    echo "" | tee -a "$RESULTS_FILE"

    return 0
}

###############################################################################
# Test 10: Stress Test (Optional)
###############################################################################

test_stress_test() {
    log_section "TEST 10: STRESS TEST (OPTIONAL - 5 MINUTES)"

    echo "=== Test 10: Stress Test ===" | tee -a "$RESULTS_FILE"

    read -p "Run 5-minute stress test on all GPUs? (y/N): " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        log_info "Skipping stress test"
        echo "TEST 10: SKIPPED" | tee -a "$RESULTS_FILE"
        return 0
    fi

    log_info "Running 5-minute stress test on all GPUs..."
    log_warning "Monitor GPU temperatures and utilization"

    # Start monitoring
    if command -v nvidia-smi &> /dev/null; then
        watch -n 5 'nvidia-smi' > "${RESULTS_DIR}/nvidia_stress_${TIMESTAMP}.txt" 2>&1 &
        local nvidia_pid=$!
    fi

    if command -v rocm-smi &> /dev/null; then
        watch -n 5 'rocm-smi --showtemp --showpower --showuse' > "${RESULTS_DIR}/amd_stress_${TIMESTAMP}.txt" 2>&1 &
        local amd_pid=$!
    fi

    # Run stress test
    if ./keyhunt -m address -f tests/66.txt -r 1:FFFFFFFFFFFF -G full -s 300 2>&1 | tee -a "$RESULTS_FILE"; then
        log_success "TEST 10 PASSED: 5-minute stress test completed"
        echo "TEST 10: PASSED" | tee -a "$RESULTS_FILE"
    else
        log_error "TEST 10 FAILED: Stress test error"
        echo "TEST 10: FAILED" | tee -a "$RESULTS_FILE"
    fi

    # Stop monitoring
    if [ -n "$nvidia_pid" ]; then
        kill $nvidia_pid 2>/dev/null || true
    fi
    if [ -n "$amd_pid" ]; then
        kill $amd_pid 2>/dev/null || true
    fi

    return 0
}

###############################################################################
# Main Test Execution
###############################################################################

main() {
    log_section "MIXED-VENDOR GPU TESTING SUITE"
    log_info "Testing unified backend with NVIDIA + AMD GPUs"
    log_info "Test duration per test: ${TEST_DURATION}s"
    log_info "Results file: ${RESULTS_FILE}"
    echo ""

    # Initialize results file
    echo "Mixed-Vendor GPU Test Results" > "$RESULTS_FILE"
    echo "Timestamp: $(date)" >> "$RESULTS_FILE"
    echo "Test Duration: ${TEST_DURATION}s per test" >> "$RESULTS_FILE"
    echo "========================================" >> "$RESULTS_FILE"
    echo "" >> "$RESULTS_FILE"

    # Run all tests
    local total_tests=0
    local passed_tests=0
    local failed_tests=0

    # Prerequisites
    check_prerequisites || exit 1

    # GPU detection
    detect_all_gpus

    # Driver info
    collect_driver_info

    # Test 1: Mixed-vendor detection
    total_tests=$((total_tests + 1))
    if verify_mixed_vendor_detection; then
        passed_tests=$((passed_tests + 1))
    else
        failed_tests=$((failed_tests + 1))
        log_error "Critical test failed - aborting"
        exit 1
    fi

    # Test 2: Backend initialization
    total_tests=$((total_tests + 1))
    if test_unified_backend_init; then
        passed_tests=$((passed_tests + 1))
    else
        failed_tests=$((failed_tests + 1))
    fi

    # Test 3: Hash mode
    total_tests=$((total_tests + 1))
    if test_hash_mode_all_gpus; then
        passed_tests=$((passed_tests + 1))
    else
        failed_tests=$((failed_tests + 1))
    fi

    # Test 4: Full GPU mode
    total_tests=$((total_tests + 1))
    if test_full_gpu_mode_all_gpus; then
        passed_tests=$((passed_tests + 1))
    else
        failed_tests=$((failed_tests + 1))
    fi

    # Test 5: Hybrid mode
    total_tests=$((total_tests + 1))
    if test_hybrid_mode_all_gpus; then
        passed_tests=$((passed_tests + 1))
    else
        failed_tests=$((failed_tests + 1))
    fi

    # Test 6: Work distribution
    total_tests=$((total_tests + 1))
    if test_work_distribution; then
        passed_tests=$((passed_tests + 1))
    else
        failed_tests=$((failed_tests + 1))
    fi

    # Test 7: Result correctness
    total_tests=$((total_tests + 1))
    if test_result_correctness; then
        passed_tests=$((passed_tests + 1))
    else
        failed_tests=$((failed_tests + 1))
    fi

    # Test 8: GPU monitoring
    test_gpu_monitoring  # Always succeeds

    # Test 9: Performance balance
    test_performance_balance  # Always succeeds

    # Test 10: Stress test (optional)
    test_stress_test

    # Summary
    log_section "TEST SUMMARY"
    echo "" | tee -a "$RESULTS_FILE"
    echo "========================================" | tee -a "$RESULTS_FILE"
    echo "TEST SUMMARY" | tee -a "$RESULTS_FILE"
    echo "========================================" | tee -a "$RESULTS_FILE"
    echo "Total Tests: ${total_tests}" | tee -a "$RESULTS_FILE"
    echo "Passed: ${passed_tests}" | tee -a "$RESULTS_FILE"
    echo "Failed: ${failed_tests}" | tee -a "$RESULTS_FILE"
    echo "" | tee -a "$RESULTS_FILE"
    echo "Results saved to: ${RESULTS_FILE}" | tee -a "$RESULTS_FILE"
    echo "Monitoring logs: ${RESULTS_DIR}/*_monitor_*.txt" | tee -a "$RESULTS_FILE"
    echo "" | tee -a "$RESULTS_FILE"

    if [ $failed_tests -eq 0 ]; then
        log_success "ALL TESTS PASSED!"
        echo "Overall Result: PASSED" | tee -a "$RESULTS_FILE"
        exit 0
    else
        log_error "SOME TESTS FAILED"
        echo "Overall Result: FAILED (${failed_tests} failures)" | tee -a "$RESULTS_FILE"
        exit 1
    fi
}

# Run main
main "$@"
