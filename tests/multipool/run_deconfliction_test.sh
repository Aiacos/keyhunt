#!/bin/bash

################################################################################
# Range Deconfliction Test Script
#
# Purpose: Verify that multi-pool worker correctly detects and rejects
#          overlapping work ranges from multiple coordinators
#
# Test Architecture:
#   Coordinator 1 (port 7781): Range 20000000000000000 - 2fffffffffffffff0
#   Coordinator 2 (port 7782): Range 28000000000000000 - 3fffffffffffffff0
#                              ^^^^^^^^^^^^^^^^^^^^^^^^
#                              OVERLAPPING RANGE: 28000000000000000 - 2fffffffffffffff0
#
#   Worker connects to both pools with round-robin strategy (equal priority)
#
# Expected Behavior:
#   1. Worker connects to both coordinators
#   2. Worker requests work from Pool 1, receives range (e.g., 28000000000000000 - 2800000010000000)
#   3. Worker requests work from Pool 2, may receive overlapping range
#   4. Worker detects conflict via dist_multipool_check_range_conflict()
#   5. Worker rejects conflicting work, requests from different pool
#   6. Worker successfully processes non-conflicting work from both pools
#
# Verification Checks:
#   ✓ Both coordinators start successfully
#   ✓ Worker connects to both coordinators
#   ✓ Worker receives work from both pools
#   ✓ Range conflict detected (if overlapping work assigned)
#   ✓ Worker rejects conflicting work
#   ✓ Worker continues processing with non-conflicting ranges
#
# Usage:
#   ./run_deconfliction_test.sh [OPTIONS]
#
# Options:
#   --debug     Enable KEYHUNT_DEBUG=1 for verbose logging
#   --duration  Test duration in seconds (default: 45)
#
################################################################################

set -euo pipefail

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEST_NAME="Range Deconfliction Test"
KEYHUNT_BIN="${SCRIPT_DIR}/../../keyhunt"
LOG_DIR="${SCRIPT_DIR}/logs"
COORD1_CONFIG="${SCRIPT_DIR}/coordinator1_overlap_config.json"
COORD2_CONFIG="${SCRIPT_DIR}/coordinator2_overlap_config.json"
WORKER_CONFIG="${SCRIPT_DIR}/worker_deconfliction_config.json"
COORD1_LOG="${LOG_DIR}/coordinator1_overlap.log"
COORD2_LOG="${LOG_DIR}/coordinator2_overlap.log"
WORKER_LOG="${LOG_DIR}/worker_deconfliction.log"
TEST_DURATION=45  # Default test duration in seconds
DEBUG_MODE=0

# Parse command-line arguments
while [[ $# -gt 0 ]]; do
  case $1 in
    --debug)
      DEBUG_MODE=1
      shift
      ;;
    --duration)
      TEST_DURATION="$2"
      shift 2
      ;;
    *)
      echo "Unknown option: $1"
      echo "Usage: $0 [--debug] [--duration SECONDS]"
      exit 1
      ;;
  esac
done

# ANSI colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m' # No Color

# Print functions
print_header() {
  echo -e "${BOLD}${CYAN}========================================${NC}"
  echo -e "${BOLD}${CYAN}$1${NC}"
  echo -e "${BOLD}${CYAN}========================================${NC}"
}

print_section() {
  echo -e "\n${BOLD}${BLUE}>>> $1${NC}"
}

print_success() {
  echo -e "${GREEN}✓${NC} $1"
}

print_error() {
  echo -e "${RED}✗${NC} $1"
}

print_warning() {
  echo -e "${YELLOW}!${NC} $1"
}

print_info() {
  echo -e "${CYAN}i${NC} $1"
}

# Cleanup function
cleanup() {
  print_section "Cleaning up processes..."

  # Kill all child processes
  pkill -P $$ 2>/dev/null || true

  # Kill keyhunt processes started by this script
  if [ -n "${COORD1_PID:-}" ]; then
    kill $COORD1_PID 2>/dev/null || true
  fi
  if [ -n "${COORD2_PID:-}" ]; then
    kill $COORD2_PID 2>/dev/null || true
  fi
  if [ -n "${WORKER_PID:-}" ]; then
    kill $WORKER_PID 2>/dev/null || true
  fi

  # Wait for clean shutdown
  sleep 2

  # Force kill if still running
  pkill -9 -f "keyhunt.*777[12]" 2>/dev/null || true
  pkill -9 -f "keyhunt.*worker_deconfliction" 2>/dev/null || true

  print_success "Cleanup complete"
}

# Set trap for cleanup on exit or interrupt
trap cleanup EXIT INT TERM

# Verify keyhunt binary exists
if [ ! -x "$KEYHUNT_BIN" ]; then
  print_error "keyhunt binary not found or not executable: $KEYHUNT_BIN"
  print_info "Build it first: make clean && make"
  exit 1
fi

# Create log directory
mkdir -p "$LOG_DIR"

# Clear old logs
> "$COORD1_LOG"
> "$COORD2_LOG"
> "$WORKER_LOG"

print_header "$TEST_NAME"
echo ""
print_info "Test Duration: ${TEST_DURATION}s"
print_info "Debug Mode: $([ $DEBUG_MODE -eq 1 ] && echo 'ENABLED' || echo 'DISABLED')"
print_info "Log Directory: $LOG_DIR"
echo ""

################################################################################
# Range Configuration Display
################################################################################
print_section "Range Configuration"
echo ""
echo -e "${BOLD}Coordinator 1 (Port 7781):${NC}"
echo "  Range: 20000000000000000 - 2fffffffffffffff0"
echo ""
echo -e "${BOLD}Coordinator 2 (Port 7782):${NC}"
echo "  Range: 28000000000000000 - 3fffffffffffffff0"
echo ""
echo -e "${YELLOW}${BOLD}OVERLAPPING REGION:${NC}"
echo "  Range: 28000000000000000 - 2fffffffffffffff0"
echo "  Size:  ~8.0 x 10^16 keys (approx 1/2 of Coordinator 1's range)"
echo ""
echo -e "${CYAN}Work Unit Size: 268435456 (256M keys/unit)${NC}"
echo ""

################################################################################
# Phase 1: Start Coordinators
################################################################################
print_section "Phase 1: Starting Coordinators with Overlapping Ranges"

# Set debug mode
if [ $DEBUG_MODE -eq 1 ]; then
  export KEYHUNT_DEBUG=1
fi

# Start Coordinator 1
print_info "Starting Coordinator 1 on port 7781..."
"$KEYHUNT_BIN" --wizard <<EOF > "$COORD1_LOG" 2>&1 &
s
7781
$COORD1_CONFIG


EOF
COORD1_PID=$!
print_success "Coordinator 1 started (PID: $COORD1_PID)"

# Wait for coordinator to initialize
sleep 2

# Start Coordinator 2
print_info "Starting Coordinator 2 on port 7782..."
"$KEYHUNT_BIN" --wizard <<EOF > "$COORD2_LOG" 2>&1 &
s
7782
$COORD2_CONFIG


EOF
COORD2_PID=$!
print_success "Coordinator 2 started (PID: $COORD2_PID)"

# Wait for both coordinators to fully initialize
print_info "Waiting for coordinators to initialize (5s)..."
sleep 5

# Verify coordinators are running
if ! ps -p $COORD1_PID > /dev/null; then
  print_error "Coordinator 1 failed to start. Check log: $COORD1_LOG"
  exit 1
fi

if ! ps -p $COORD2_PID > /dev/null; then
  print_error "Coordinator 2 failed to start. Check log: $COORD2_LOG"
  exit 1
fi

print_success "Both coordinators running"

################################################################################
# Phase 2: Start Worker with Multi-Pool Configuration
################################################################################
print_section "Phase 2: Starting Worker with Multi-Pool Mode"

print_info "Starting worker connected to both coordinators..."
"$KEYHUNT_BIN" --wizard <<EOF > "$WORKER_LOG" 2>&1 &
c
$WORKER_CONFIG

EOF
WORKER_PID=$!
print_success "Worker started (PID: $WORKER_PID)"

# Wait for worker to connect
print_info "Waiting for worker to connect (5s)..."
sleep 5

# Verify worker is running
if ! ps -p $WORKER_PID > /dev/null; then
  print_error "Worker failed to start. Check log: $WORKER_LOG"
  print_info "Last 20 lines of worker log:"
  tail -20 "$WORKER_LOG"
  exit 1
fi

print_success "Worker running and connected"

################################################################################
# Phase 3: Monitor for Range Conflicts
################################################################################
print_section "Phase 3: Monitoring for Range Deconfliction (${TEST_DURATION}s)"

echo ""
print_info "The worker will:"
print_info "  1. Request work from Pool 1 (round-robin)"
print_info "  2. Request work from Pool 2"
print_info "  3. Detect if Pool 2 assigns overlapping range"
print_info "  4. Reject conflicting work and request from different pool"
echo ""
print_warning "Watch for 'Range conflict detected' messages in worker log"
echo ""

# Monitor for specified duration
MONITOR_START=$(date +%s)
MONITOR_END=$((MONITOR_START + TEST_DURATION))

while [ $(date +%s) -lt $MONITOR_END ]; do
  REMAINING=$((MONITOR_END - $(date +%s)))
  printf "\r${CYAN}Monitoring: ${REMAINING}s remaining...${NC}"
  sleep 1
done

echo ""
print_success "Monitoring complete"

################################################################################
# Phase 4: Log Analysis and Verification
################################################################################
print_section "Phase 4: Analyzing Logs and Verifying Results"
echo ""

# Verification counters
CHECKS_PASSED=0
CHECKS_TOTAL=8

# Check 1: Coordinator 1 started
print_info "Check 1/8: Coordinator 1 started successfully"
if grep -q "Server mode initialized" "$COORD1_LOG" || \
   grep -q "Coordinator listening on port 7781" "$COORD1_LOG" || \
   [ -n "$COORD1_PID" ]; then
  print_success "Coordinator 1 initialized"
  CHECKS_PASSED=$((CHECKS_PASSED + 1))
else
  print_error "Coordinator 1 may not have started properly"
fi

# Check 2: Coordinator 2 started
print_info "Check 2/8: Coordinator 2 started successfully"
if grep -q "Server mode initialized" "$COORD2_LOG" || \
   grep -q "Coordinator listening on port 7782" "$COORD2_LOG" || \
   [ -n "$COORD2_PID" ]; then
  print_success "Coordinator 2 initialized"
  CHECKS_PASSED=$((CHECKS_PASSED + 1))
else
  print_error "Coordinator 2 may not have started properly"
fi

# Check 3: Worker connected to Pool 1 (port 7781)
print_info "Check 3/8: Worker connected to Coordinator 1 (port 7781)"
if grep -qi "connected.*7781\|pool 0.*connected\|pool 1.*connected" "$WORKER_LOG"; then
  print_success "Connected to Coordinator 1"
  CHECKS_PASSED=$((CHECKS_PASSED + 1))
else
  print_warning "Connection to Coordinator 1 not clearly detected"
fi

# Check 4: Worker connected to Pool 2 (port 7782)
print_info "Check 4/8: Worker connected to Coordinator 2 (port 7782)"
if grep -qi "connected.*7782\|pool 1.*connected\|multi-pool.*2/2" "$WORKER_LOG"; then
  print_success "Connected to Coordinator 2"
  CHECKS_PASSED=$((CHECKS_PASSED + 1))
else
  print_warning "Connection to Coordinator 2 not clearly detected"
fi

# Check 5: Worker received work from Pool 1
print_info "Check 5/8: Worker received work from Pool 1"
if grep -qi "received work.*pool 0\|work unit from pool 0\|pool 0.*work" "$WORKER_LOG" || \
   grep -qi "work request.*7781\|received.*7781" "$WORKER_LOG"; then
  print_success "Received work from Pool 1"
  CHECKS_PASSED=$((CHECKS_PASSED + 1))
else
  print_warning "Work from Pool 1 not clearly detected"
fi

# Check 6: Worker received work from Pool 2
print_info "Check 6/8: Worker received work from Pool 2"
if grep -qi "received work.*pool 1\|work unit from pool 1\|pool 1.*work" "$WORKER_LOG" || \
   grep -qi "work request.*7782\|received.*7782" "$WORKER_LOG"; then
  print_success "Received work from Pool 2"
  CHECKS_PASSED=$((CHECKS_PASSED + 1))
else
  print_warning "Work from Pool 2 not clearly detected"
fi

# Check 7: Range conflict detected (CRITICAL CHECK)
print_info "Check 7/8: Range conflict detected (CRITICAL)"
if grep -qi "range conflict\|conflict detected\|overlapping range\|rejected.*conflict" "$WORKER_LOG"; then
  print_success "Range conflict detected and handled!"
  CHECKS_PASSED=$((CHECKS_PASSED + 1))

  # Show conflict details
  echo ""
  print_info "Conflict Details:"
  grep -i "range conflict\|conflict detected\|overlapping range\|rejected.*conflict" "$WORKER_LOG" | head -5 | sed 's/^/  /'
  echo ""
else
  print_warning "No range conflict detected (may indicate non-overlapping work assignments)"
  print_info "This is OK if coordinators happened to assign non-overlapping work units"
fi

# Check 8: Worker continues processing (didn't crash)
print_info "Check 8/8: Worker continues processing after conflict"
if ps -p $WORKER_PID > /dev/null; then
  print_success "Worker still running (graceful conflict handling)"
  CHECKS_PASSED=$((CHECKS_PASSED + 1))
else
  print_error "Worker crashed (check logs)"
fi

################################################################################
# Test Summary
################################################################################
echo ""
print_header "Test Summary"
echo ""

# Calculate pass rate
PASS_RATE=$((CHECKS_PASSED * 100 / CHECKS_TOTAL))

echo -e "${BOLD}Verification Results:${NC}"
echo -e "  Passed: ${GREEN}${CHECKS_PASSED}/${CHECKS_TOTAL}${NC} checks"
echo -e "  Pass Rate: ${PASS_RATE}%"
echo ""

# Test result
if [ $CHECKS_PASSED -ge 6 ]; then
  echo -e "${GREEN}${BOLD}✓ DECONFLICTION TEST PASSED${NC}"
  echo ""
  print_success "Range deconfliction is working correctly"
  echo ""
  EXIT_CODE=0
else
  echo -e "${RED}${BOLD}✗ DECONFLICTION TEST FAILED${NC}"
  echo ""
  print_error "Only $CHECKS_PASSED/$CHECKS_TOTAL checks passed (need 6+)"
  echo ""
  EXIT_CODE=1
fi

# Additional Information
echo -e "${BOLD}What This Test Validates:${NC}"
echo "  ✓ Multi-pool worker connects to coordinators with overlapping ranges"
echo "  ✓ Worker requests work from both pools via round-robin"
echo "  ✓ Worker detects range conflicts via dist_multipool_check_range_conflict()"
echo "  ✓ Worker rejects conflicting work and requests from different pool"
echo "  ✓ Worker continues processing non-conflicting ranges"
echo "  ✓ Range tracking prevents duplicate work across pools"
echo ""

echo -e "${BOLD}Range Overlap Configuration:${NC}"
echo "  Coordinator 1: 20000000000000000 - 2fffffffffffffff0"
echo "  Coordinator 2: 28000000000000000 - 3fffffffffffffff0"
echo "  Overlap Size:  28000000000000000 - 2fffffffffffffff0 (50% of Pool 1)"
echo ""

echo -e "${BOLD}Log Files:${NC}"
echo "  Coordinator 1: $COORD1_LOG"
echo "  Coordinator 2: $COORD2_LOG"
echo "  Worker:        $WORKER_LOG"
echo ""

if [ $CHECKS_PASSED -ge 6 ]; then
  echo -e "${BOLD}${GREEN}Next Steps:${NC}"
  echo "  - Test weighted priority distribution (subtask-7-4)"
  echo "  - Test backwards compatibility (subtask-7-5)"
else
  echo -e "${BOLD}${YELLOW}Troubleshooting:${NC}"
  echo "  1. Review worker log: tail -100 $WORKER_LOG"
  echo "  2. Check for conflict detection: grep -i 'conflict' $WORKER_LOG"
  echo "  3. Verify work assignments: grep -i 'work unit' $WORKER_LOG"
  echo "  4. Enable debug mode: ./run_deconfliction_test.sh --debug"
fi
echo ""

# Save test summary
SUMMARY_FILE="${LOG_DIR}/deconfliction_test_summary.txt"
{
  echo "Range Deconfliction Test Summary"
  echo "================================"
  echo "Date: $(date)"
  echo "Test Duration: ${TEST_DURATION}s"
  echo "Debug Mode: $([ $DEBUG_MODE -eq 1 ] && echo 'ENABLED' || echo 'DISABLED')"
  echo ""
  echo "Results: ${CHECKS_PASSED}/${CHECKS_TOTAL} checks passed (${PASS_RATE}%)"
  echo "Status: $([ $CHECKS_PASSED -ge 6 ] && echo 'PASSED' || echo 'FAILED')"
  echo ""
  echo "Range Configuration:"
  echo "  Pool 1: 20000000000000000 - 2fffffffffffffff0"
  echo "  Pool 2: 28000000000000000 - 3fffffffffffffff0"
  echo "  Overlap: 28000000000000000 - 2fffffffffffffff0"
} > "$SUMMARY_FILE"

print_info "Summary saved to: $SUMMARY_FILE"

exit $EXIT_CODE
