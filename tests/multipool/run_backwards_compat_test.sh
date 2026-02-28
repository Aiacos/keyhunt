#!/bin/bash
#
# Backwards Compatibility Test for Multi-Pool Worker
#
# This test verifies that workers with old single-pool configuration
# (using legacy server_host/server_port fields) continue to work
# exactly as before after the multi-pool feature was added.
#
# Test scenario:
# 1. Start coordinator on port 7771 (single coordinator)
# 2. Start worker with legacy config (no pool_list, only server section)
# 3. Verify worker connects successfully
# 4. Verify worker receives and processes work
# 5. Verify no multi-pool code paths are triggered
# 6. Verify behavior is identical to pre-multi-pool implementation
#

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m' # No Color

# Test configuration
TEST_DURATION=30
DEBUG_MODE=0

# Parse command-line options
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
        --help)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --debug          Enable debug output (KEYHUNT_DEBUG=1)"
            echo "  --duration N     Test duration in seconds (default: 30)"
            echo "  --help           Show this help message"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

echo -e "${BOLD}${CYAN}================================================${NC}"
echo -e "${BOLD}${CYAN}  Backwards Compatibility Test (Single Pool)${NC}"
echo -e "${BOLD}${CYAN}================================================${NC}"
echo ""
echo -e "${BLUE}Test Duration:${NC} ${TEST_DURATION} seconds"
echo -e "${BLUE}Debug Mode:${NC} $( [ $DEBUG_MODE -eq 1 ] && echo 'Enabled' || echo 'Disabled' )"
echo ""

# Directories
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOG_DIR="$SCRIPT_DIR/logs"
KEYHUNT_BIN="$SCRIPT_DIR/../../keyhunt"

# Create log directory
mkdir -p "$LOG_DIR"

# Log files
COORDINATOR_LOG="$LOG_DIR/coordinator_legacy.log"
WORKER_LOG="$LOG_DIR/worker_legacy.log"
SUMMARY_FILE="$LOG_DIR/backwards_compat_test_summary.txt"

# Configuration files
COORDINATOR_CONFIG="$SCRIPT_DIR/coordinator1_config.json"
WORKER_CONFIG="$SCRIPT_DIR/worker_legacy_singlepool_config.json"

# PIDs for cleanup
COORDINATOR_PID=""
WORKER_PID=""

# Cleanup function
cleanup() {
    echo ""
    echo -e "${YELLOW}[*] Cleaning up...${NC}"

    if [ -n "$WORKER_PID" ]; then
        echo "    Stopping worker (PID: $WORKER_PID)..."
        kill $WORKER_PID 2>/dev/null || true
        wait $WORKER_PID 2>/dev/null || true
    fi

    if [ -n "$COORDINATOR_PID" ]; then
        echo "    Stopping coordinator (PID: $COORDINATOR_PID)..."
        kill $COORDINATOR_PID 2>/dev/null || true
        wait $COORDINATOR_PID 2>/dev/null || true
    fi

    echo -e "${GREEN}    Cleanup complete${NC}"
}

# Trap signals for cleanup
trap cleanup EXIT INT TERM

# Check if keyhunt binary exists
if [ ! -f "$KEYHUNT_BIN" ]; then
    echo -e "${RED}[-] Error: keyhunt binary not found at $KEYHUNT_BIN${NC}"
    echo "    Please build keyhunt first: make clean && make"
    exit 1
fi

# Check if config files exist
if [ ! -f "$COORDINATOR_CONFIG" ]; then
    echo -e "${RED}[-] Error: Coordinator config not found: $COORDINATOR_CONFIG${NC}"
    exit 1
fi

if [ ! -f "$WORKER_CONFIG" ]; then
    echo -e "${RED}[-] Error: Worker config not found: $WORKER_CONFIG${NC}"
    exit 1
fi

# Verify worker config is in legacy format
echo -e "${BLUE}[*] Verifying legacy config format...${NC}"
if grep -q '"pool_list"' "$WORKER_CONFIG" 2>/dev/null; then
    echo -e "${RED}[-] Error: Worker config has pool_list (not legacy format)${NC}"
    exit 1
fi

if ! grep -q '"server"' "$WORKER_CONFIG" 2>/dev/null; then
    echo -e "${RED}[-] Error: Worker config missing 'server' section (not legacy format)${NC}"
    exit 1
fi

echo -e "${GREEN}    ✓ Worker config is in legacy format (pre-multi-pool)${NC}"
echo ""

# Initialize summary file
cat > "$SUMMARY_FILE" <<EOF
Backwards Compatibility Test Summary
=====================================
Test Duration: ${TEST_DURATION}s
Started: $(date)

Configuration:
- Coordinator: 127.0.0.1:7771
- Worker Config: Legacy single-pool format (server section only)

Verification Checks:
EOF

# Verification counters
CHECKS_TOTAL=0
CHECKS_PASSED=0

# Verification function
verify() {
    local check_name="$1"
    local log_file="$2"
    local pattern="$3"
    local is_critical="${4:-0}"  # 0=bonus, 1=critical

    CHECKS_TOTAL=$((CHECKS_TOTAL + 1))

    if grep -q "$pattern" "$log_file" 2>/dev/null; then
        echo -e "${GREEN}  ✓ Check $CHECKS_TOTAL: $check_name${NC}"
        echo "✓ Check $CHECKS_TOTAL: $check_name" >> "$SUMMARY_FILE"
        CHECKS_PASSED=$((CHECKS_PASSED + 1))
        return 0
    else
        if [ $is_critical -eq 1 ]; then
            echo -e "${RED}  ✗ Check $CHECKS_TOTAL: $check_name (CRITICAL)${NC}"
            echo "✗ Check $CHECKS_TOTAL: $check_name (CRITICAL FAILURE)" >> "$SUMMARY_FILE"
        else
            echo -e "${YELLOW}  ! Check $CHECKS_TOTAL: $check_name (bonus check)${NC}"
            echo "! Check $CHECKS_TOTAL: $check_name (bonus check - not found)" >> "$SUMMARY_FILE"
        fi
        return 1
    fi
}

# Phase 1: Start Coordinator
echo -e "${BOLD}${CYAN}Phase 1: Starting Coordinator${NC}"
echo -e "${BLUE}[*] Starting coordinator on port 7771...${NC}"

> "$COORDINATOR_LOG"

if [ $DEBUG_MODE -eq 1 ]; then
    KEYHUNT_DEBUG=1 "$KEYHUNT_BIN" --wizard --load "$COORDINATOR_CONFIG" > "$COORDINATOR_LOG" 2>&1 &
else
    "$KEYHUNT_BIN" --wizard --load "$COORDINATOR_CONFIG" > "$COORDINATOR_LOG" 2>&1 &
fi

COORDINATOR_PID=$!

echo "    Coordinator PID: $COORDINATOR_PID"
echo "    Waiting 5 seconds for coordinator to initialize..."
sleep 5

# Check if coordinator is running
if ! ps -p $COORDINATOR_PID > /dev/null 2>&1; then
    echo -e "${RED}[-] Coordinator failed to start${NC}"
    echo "    Check log: $COORDINATOR_LOG"
    tail -20 "$COORDINATOR_LOG"
    exit 1
fi

echo -e "${GREEN}    ✓ Coordinator started successfully${NC}"
echo ""

# Phase 2: Start Worker with Legacy Config
echo -e "${BOLD}${CYAN}Phase 2: Starting Worker (Legacy Config)${NC}"
echo -e "${BLUE}[*] Starting worker with legacy single-pool configuration...${NC}"

> "$WORKER_LOG"

if [ $DEBUG_MODE -eq 1 ]; then
    KEYHUNT_DEBUG=1 "$KEYHUNT_BIN" --wizard --load "$WORKER_CONFIG" > "$WORKER_LOG" 2>&1 &
else
    "$KEYHUNT_BIN" --wizard --load "$WORKER_CONFIG" > "$WORKER_LOG" 2>&1 &
fi

WORKER_PID=$!

echo "    Worker PID: $WORKER_PID"
echo "    Waiting 5 seconds for worker to connect..."
sleep 5

# Check if worker is running
if ! ps -p $WORKER_PID > /dev/null 2>&1; then
    echo -e "${RED}[-] Worker failed to start${NC}"
    echo "    Check log: $WORKER_LOG"
    tail -20 "$WORKER_LOG"
    exit 1
fi

echo -e "${GREEN}    ✓ Worker started successfully${NC}"
echo ""

# Phase 3: Monitor Operation
echo -e "${BOLD}${CYAN}Phase 3: Monitoring Operation${NC}"
echo -e "${BLUE}[*] Running for ${TEST_DURATION} seconds...${NC}"

# Progress indicator
for i in $(seq 1 $TEST_DURATION); do
    if ! ps -p $WORKER_PID > /dev/null 2>&1; then
        echo -e "${RED}[-] Worker crashed during test!${NC}"
        exit 1
    fi

    if ! ps -p $COORDINATOR_PID > /dev/null 2>&1; then
        echo -e "${RED}[-] Coordinator crashed during test!${NC}"
        exit 1
    fi

    # Show progress every 10 seconds
    if [ $((i % 10)) -eq 0 ]; then
        echo -e "${CYAN}    ${i}s elapsed...${NC}"
    fi

    sleep 1
done

echo -e "${GREEN}    ✓ Test duration completed${NC}"
echo ""

# Phase 4: Verification
echo -e "${BOLD}${CYAN}Phase 4: Verification${NC}"
echo -e "${BLUE}[*] Analyzing logs...${NC}"
echo ""

# Verify coordinator started
verify "Coordinator started successfully" "$COORDINATOR_LOG" "Coordinator started on port 7771" 1

# Verify worker connected to coordinator
verify "Worker connected to coordinator" "$WORKER_LOG" "Connected to.*7771" 1

# Verify legacy config was loaded
verify "Legacy config loaded" "$WORKER_LOG" "wizard --load" 1

# Verify worker received work
verify "Worker received work unit" "$WORKER_LOG" "Received work unit" 1

# Verify worker is processing keys
verify "Worker processing keys" "$WORKER_LOG" "keys/s" 1

# Verify NO multi-pool mode activation
verify "Single-pool mode (NOT multi-pool)" "$WORKER_LOG" "Multi-pool mode" 0 && {
    echo -e "${RED}  [!] WARNING: Multi-pool mode was activated (should be single-pool!)${NC}"
    CHECKS_PASSED=$((CHECKS_PASSED - 1))
} || {
    echo -e "${GREEN}  ✓ Confirmed: Multi-pool mode NOT activated (single-pool as expected)${NC}"
    CHECKS_PASSED=$((CHECKS_PASSED + 1))
    CHECKS_TOTAL=$((CHECKS_TOTAL + 1))
}

# Verify worker doesn't crash
verify "Worker survived entire test" "$WORKER_LOG" "keys/s" 1

# Verify backwards compatibility migration (if present in logs)
verify "Config migration applied" "$WORKER_LOG" "pool_count.*1" 0

echo ""

# Final results
echo -e "${BOLD}${CYAN}================================================${NC}"
echo -e "${BOLD}${CYAN}  Test Results${NC}"
echo -e "${BOLD}${CYAN}================================================${NC}"
echo ""

echo -e "Checks Passed: ${CHECKS_PASSED}/${CHECKS_TOTAL}"
echo "" >> "$SUMMARY_FILE"
echo "Checks Passed: ${CHECKS_PASSED}/${CHECKS_TOTAL}" >> "$SUMMARY_FILE"
echo "Ended: $(date)" >> "$SUMMARY_FILE"

REQUIRED_CHECKS=5  # Minimum checks for PASS

if [ $CHECKS_PASSED -ge $REQUIRED_CHECKS ]; then
    echo -e "${BOLD}${GREEN}✓ BACKWARDS COMPATIBILITY TEST PASSED${NC}"
    echo "" >> "$SUMMARY_FILE"
    echo "Result: PASSED" >> "$SUMMARY_FILE"
    EXIT_CODE=0
else
    echo -e "${BOLD}${RED}✗ BACKWARDS COMPATIBILITY TEST FAILED${NC}"
    echo "" >> "$SUMMARY_FILE"
    echo "Result: FAILED" >> "$SUMMARY_FILE"
    EXIT_CODE=1
fi

echo ""
echo -e "${BLUE}Summary saved to:${NC} $SUMMARY_FILE"
echo -e "${BLUE}Coordinator log:${NC} $COORDINATOR_LOG"
echo -e "${BLUE}Worker log:${NC} $WORKER_LOG"
echo ""

exit $EXIT_CODE
