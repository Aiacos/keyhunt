#!/bin/bash
#
# Priority Distribution Test
# Tests that work distribution matches configured pool priorities
#
# This script:
# 1. Starts 2 coordinators on different ports with non-overlapping ranges
# 2. Starts a worker with priority-weighted configuration (75:25 = 3:1 ratio)
# 3. Monitors work requests from each pool
# 4. Verifies distribution ratio matches configured priorities (within tolerance)
# 5. Reports results and cleans up processes
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

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KEYHUNT_BIN="$SCRIPT_DIR/../../keyhunt"
TEST_DURATION=90  # Run test for 90 seconds to collect enough samples
LOG_DIR="$SCRIPT_DIR/logs"
DEBUG_MODE=0

# PIDs for cleanup
COORD1_PID=""
COORD2_PID=""
WORKER_PID=""

# Priority configuration
POOL1_PRIORITY=75
POOL2_PRIORITY=25
EXPECTED_RATIO=3.0  # 75:25 = 3:1
TOLERANCE=0.5       # Allow 50% deviation (2.5 to 3.5 ratio is acceptable)

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
        *)
            echo "Unknown option: $1"
            echo "Usage: $0 [--debug] [--duration SECONDS]"
            exit 1
            ;;
    esac
done

# Cleanup function
cleanup() {
    echo -e "\n${YELLOW}Cleaning up processes...${NC}"

    if [ -n "$WORKER_PID" ] && kill -0 "$WORKER_PID" 2>/dev/null; then
        echo "Stopping worker (PID: $WORKER_PID)"
        kill -TERM "$WORKER_PID" 2>/dev/null || true
        sleep 2
        kill -KILL "$WORKER_PID" 2>/dev/null || true
    fi

    if [ -n "$COORD1_PID" ] && kill -0 "$COORD1_PID" 2>/dev/null; then
        echo "Stopping coordinator 1 (PID: $COORD1_PID)"
        kill -TERM "$COORD1_PID" 2>/dev/null || true
        sleep 2
        kill -KILL "$COORD1_PID" 2>/dev/null || true
    fi

    if [ -n "$COORD2_PID" ] && kill -0 "$COORD2_PID" 2>/dev/null; then
        echo "Stopping coordinator 2 (PID: $COORD2_PID)"
        kill -TERM "$COORD2_PID" 2>/dev/null || true
        sleep 2
        kill -KILL "$COORD2_PID" 2>/dev/null || true
    fi

    echo -e "${GREEN}Cleanup complete${NC}"
}

# Set trap for cleanup
trap cleanup EXIT INT TERM

# Print header
echo -e "${BOLD}${CYAN}========================================${NC}"
echo -e "${BOLD}${CYAN}  Priority Distribution Test${NC}"
echo -e "${BOLD}${CYAN}========================================${NC}\n"

# Check keyhunt binary exists
if [ ! -f "$KEYHUNT_BIN" ]; then
    echo -e "${RED}Error: keyhunt binary not found at $KEYHUNT_BIN${NC}"
    echo "Please build keyhunt first: make"
    exit 1
fi

# Create log directory
mkdir -p "$LOG_DIR"

# Clean old logs
rm -f "$LOG_DIR"/coordinator1_priority.log
rm -f "$LOG_DIR"/coordinator2_priority.log
rm -f "$LOG_DIR"/worker_priority.log
rm -f "$LOG_DIR"/priority_test_summary.txt

echo -e "${BLUE}Test Configuration:${NC}"
echo "  - Coordinator 1: localhost:7791 (priority: $POOL1_PRIORITY)"
echo "  - Coordinator 2: localhost:7792 (priority: $POOL2_PRIORITY)"
echo "  - Expected Ratio: ${EXPECTED_RATIO}:1 (Pool 1:Pool 2)"
echo "  - Tolerance: ±${TOLERANCE} (${EXPECTED_RATIO} ± ${TOLERANCE})"
echo "  - Worker: Priority-weighted distribution mode"
echo "  - Test Duration: ${TEST_DURATION}s"
echo "  - Log Directory: $LOG_DIR"
if [ $DEBUG_MODE -eq 1 ]; then
    echo "  - Debug Mode: ENABLED (KEYHUNT_DEBUG=1)"
fi
echo ""

# Step 1: Start Coordinator 1
echo -e "${YELLOW}Step 1/4:${NC} Starting Coordinator 1 (port 7791, priority $POOL1_PRIORITY)..."
cd "$SCRIPT_DIR/../.."

if [ $DEBUG_MODE -eq 1 ]; then
    KEYHUNT_DEBUG=1 nohup "$KEYHUNT_BIN" --wizard-server \
        --config tests/multipool/coordinator1_priority_config.json \
        > "$LOG_DIR/coordinator1_priority.log" 2>&1 &
else
    nohup "$KEYHUNT_BIN" --wizard-server \
        --config tests/multipool/coordinator1_priority_config.json \
        > "$LOG_DIR/coordinator1_priority.log" 2>&1 &
fi

COORD1_PID=$!
echo "  Coordinator 1 PID: $COORD1_PID"
sleep 2

# Verify coordinator 1 is running
if ! kill -0 "$COORD1_PID" 2>/dev/null; then
    echo -e "${RED}Failed to start coordinator 1${NC}"
    echo "Last 20 lines of log:"
    tail -20 "$LOG_DIR/coordinator1_priority.log"
    exit 1
fi
echo -e "${GREEN}  Coordinator 1 started successfully${NC}"

# Step 2: Start Coordinator 2
echo -e "${YELLOW}Step 2/4:${NC} Starting Coordinator 2 (port 7792, priority $POOL2_PRIORITY)..."

if [ $DEBUG_MODE -eq 1 ]; then
    KEYHUNT_DEBUG=1 nohup "$KEYHUNT_BIN" --wizard-server \
        --config tests/multipool/coordinator2_priority_config.json \
        > "$LOG_DIR/coordinator2_priority.log" 2>&1 &
else
    nohup "$KEYHUNT_BIN" --wizard-server \
        --config tests/multipool/coordinator2_priority_config.json \
        > "$LOG_DIR/coordinator2_priority.log" 2>&1 &
fi

COORD2_PID=$!
echo "  Coordinator 2 PID: $COORD2_PID"
sleep 2

# Verify coordinator 2 is running
if ! kill -0 "$COORD2_PID" 2>/dev/null; then
    echo -e "${RED}Failed to start coordinator 2${NC}"
    echo "Last 20 lines of log:"
    tail -20 "$LOG_DIR/coordinator2_priority.log"
    exit 1
fi
echo -e "${GREEN}  Coordinator 2 started successfully${NC}"

# Step 3: Start Worker with Priority Configuration
echo -e "${YELLOW}Step 3/4:${NC} Starting worker with priority-weighted distribution..."
sleep 2  # Give coordinators time to initialize

if [ $DEBUG_MODE -eq 1 ]; then
    KEYHUNT_DEBUG=1 nohup "$KEYHUNT_BIN" --wizard-client \
        --config tests/multipool/worker_priority_config.json \
        > "$LOG_DIR/worker_priority.log" 2>&1 &
else
    nohup "$KEYHUNT_BIN" --wizard-client \
        --config tests/multipool/worker_priority_config.json \
        > "$LOG_DIR/worker_priority.log" 2>&1 &
fi

WORKER_PID=$!
echo "  Worker PID: $WORKER_PID"
sleep 3

# Verify worker is running
if ! kill -0 "$WORKER_PID" 2>/dev/null; then
    echo -e "${RED}Failed to start worker${NC}"
    echo "Last 30 lines of log:"
    tail -30 "$LOG_DIR/worker_priority.log"
    exit 1
fi
echo -e "${GREEN}  Worker started successfully${NC}"

# Step 4: Monitor work distribution
echo -e "${YELLOW}Step 4/4:${NC} Monitoring work distribution for ${TEST_DURATION}s..."
echo ""

# Monitor progress in 10-second intervals
ELAPSED=0
INTERVAL=10

while [ $ELAPSED -lt $TEST_DURATION ]; do
    sleep $INTERVAL
    ELAPSED=$((ELAPSED + INTERVAL))

    # Count work requests from each pool
    POOL1_COUNT=$(grep -c "Work assigned from pool 0" "$LOG_DIR/worker_priority.log" 2>/dev/null || echo "0")
    POOL2_COUNT=$(grep -c "Work assigned from pool 1" "$LOG_DIR/worker_priority.log" 2>/dev/null || echo "0")
    TOTAL_COUNT=$((POOL1_COUNT + POOL2_COUNT))

    # Calculate current ratio
    if [ $POOL2_COUNT -gt 0 ]; then
        CURRENT_RATIO=$(echo "scale=2; $POOL1_COUNT / $POOL2_COUNT" | bc)
    else
        CURRENT_RATIO="N/A"
    fi

    # Progress update
    echo -e "${CYAN}[${ELAPSED}s/${TEST_DURATION}s]${NC} Work distribution: Pool 1=${POOL1_COUNT}, Pool 2=${POOL2_COUNT}, Total=${TOTAL_COUNT}, Ratio=${CURRENT_RATIO}:1"

    # Check if worker is still running
    if ! kill -0 "$WORKER_PID" 2>/dev/null; then
        echo -e "${RED}Worker stopped unexpectedly${NC}"
        break
    fi
done

echo ""
echo -e "${YELLOW}Monitoring complete. Analyzing results...${NC}"
echo ""

# Final count
POOL1_COUNT=$(grep -c "Work assigned from pool 0" "$LOG_DIR/worker_priority.log" 2>/dev/null || echo "0")
POOL2_COUNT=$(grep -c "Work assigned from pool 1" "$LOG_DIR/worker_priority.log" 2>/dev/null || echo "0")
TOTAL_COUNT=$((POOL1_COUNT + POOL2_COUNT))

# Calculate percentages
if [ $TOTAL_COUNT -gt 0 ]; then
    POOL1_PERCENT=$(echo "scale=1; ($POOL1_COUNT * 100) / $TOTAL_COUNT" | bc)
    POOL2_PERCENT=$(echo "scale=1; ($POOL2_COUNT * 100) / $TOTAL_COUNT" | bc)
else
    POOL1_PERCENT="0"
    POOL2_PERCENT="0"
fi

# Calculate ratio
if [ $POOL2_COUNT -gt 0 ]; then
    ACTUAL_RATIO=$(echo "scale=2; $POOL1_COUNT / $POOL2_COUNT" | bc)
    RATIO_STR="${ACTUAL_RATIO}:1"
else
    ACTUAL_RATIO="0"
    RATIO_STR="N/A (no work from Pool 2)"
fi

# Calculate expected percentages
POOL1_EXPECTED_PERCENT=$(echo "scale=1; ($POOL1_PRIORITY * 100) / ($POOL1_PRIORITY + $POOL2_PRIORITY)" | bc)
POOL2_EXPECTED_PERCENT=$(echo "scale=1; ($POOL2_PRIORITY * 100) / ($POOL1_PRIORITY + $POOL2_PRIORITY)" | bc)

# Generate summary report
{
    echo "========================================="
    echo "  PRIORITY DISTRIBUTION TEST RESULTS"
    echo "========================================="
    echo ""
    echo "TEST CONFIGURATION:"
    echo "  Pool 1 Priority: $POOL1_PRIORITY"
    echo "  Pool 2 Priority: $POOL2_PRIORITY"
    echo "  Expected Ratio: ${EXPECTED_RATIO}:1"
    echo "  Tolerance: ±${TOLERANCE}"
    echo "  Test Duration: ${TEST_DURATION}s"
    echo ""
    echo "WORK DISTRIBUTION:"
    echo "  Pool 1 (priority $POOL1_PRIORITY): $POOL1_COUNT work units ($POOL1_PERCENT%)"
    echo "  Pool 2 (priority $POOL2_PRIORITY): $POOL2_COUNT work units ($POOL2_PERCENT%)"
    echo "  Total: $TOTAL_COUNT work units"
    echo ""
    echo "EXPECTED DISTRIBUTION:"
    echo "  Pool 1: ${POOL1_EXPECTED_PERCENT}%"
    echo "  Pool 2: ${POOL2_EXPECTED_PERCENT}%"
    echo ""
    echo "ACTUAL RATIO:"
    echo "  $RATIO_STR (expected: ${EXPECTED_RATIO}:1)"
    echo ""
} | tee "$LOG_DIR/priority_test_summary.txt"

# Verification checks
echo "========================================="
echo "  VERIFICATION CHECKS"
echo "========================================="
echo ""

CHECKS_PASSED=0
CHECKS_TOTAL=8

# Check 1: Coordinator 1 started
if grep -q "Server listening on" "$LOG_DIR/coordinator1_priority.log" 2>/dev/null; then
    echo -e "${GREEN}✓${NC} Check 1/8: Coordinator 1 started successfully"
    CHECKS_PASSED=$((CHECKS_PASSED + 1))
else
    echo -e "${RED}✗${NC} Check 1/8: Coordinator 1 failed to start"
fi

# Check 2: Coordinator 2 started
if grep -q "Server listening on" "$LOG_DIR/coordinator2_priority.log" 2>/dev/null; then
    echo -e "${GREEN}✓${NC} Check 2/8: Coordinator 2 started successfully"
    CHECKS_PASSED=$((CHECKS_PASSED + 1))
else
    echo -e "${RED}✗${NC} Check 2/8: Coordinator 2 failed to start"
fi

# Check 3: Worker connected to Pool 1
if grep -q "Connected to 127.0.0.1:7791\|Connected to localhost:7791" "$LOG_DIR/worker_priority.log" 2>/dev/null; then
    echo -e "${GREEN}✓${NC} Check 3/8: Worker connected to Pool 1 (port 7791)"
    CHECKS_PASSED=$((CHECKS_PASSED + 1))
else
    echo -e "${RED}✗${NC} Check 3/8: Worker failed to connect to Pool 1"
fi

# Check 4: Worker connected to Pool 2
if grep -q "Connected to 127.0.0.1:7792\|Connected to localhost:7792" "$LOG_DIR/worker_priority.log" 2>/dev/null; then
    echo -e "${GREEN}✓${NC} Check 4/8: Worker connected to Pool 2 (port 7792)"
    CHECKS_PASSED=$((CHECKS_PASSED + 1))
else
    echo -e "${RED}✗${NC} Check 4/8: Worker failed to connect to Pool 2"
fi

# Check 5: Worker received work from Pool 1
if [ $POOL1_COUNT -gt 0 ]; then
    echo -e "${GREEN}✓${NC} Check 5/8: Worker received work from Pool 1 ($POOL1_COUNT units)"
    CHECKS_PASSED=$((CHECKS_PASSED + 1))
else
    echo -e "${RED}✗${NC} Check 5/8: Worker received no work from Pool 1"
fi

# Check 6: Worker received work from Pool 2
if [ $POOL2_COUNT -gt 0 ]; then
    echo -e "${GREEN}✓${NC} Check 6/8: Worker received work from Pool 2 ($POOL2_COUNT units)"
    CHECKS_PASSED=$((CHECKS_PASSED + 1))
else
    echo -e "${RED}✗${NC} Check 6/8: Worker received no work from Pool 2"
fi

# Check 7: Sufficient sample size (at least 10 work units total)
if [ $TOTAL_COUNT -ge 10 ]; then
    echo -e "${GREEN}✓${NC} Check 7/8: Sufficient sample size ($TOTAL_COUNT work units)"
    CHECKS_PASSED=$((CHECKS_PASSED + 1))
else
    echo -e "${YELLOW}⚠${NC} Check 7/8: Small sample size ($TOTAL_COUNT work units, expected ≥10)"
    echo "  Note: Results may not be statistically significant with small samples"
fi

# Check 8: Distribution ratio matches priority (within tolerance)
RATIO_CHECK_PASSED=0
if [ "$ACTUAL_RATIO" != "0" ] && [ "$ACTUAL_RATIO" != "N/A" ]; then
    # Calculate bounds: expected ± tolerance
    MIN_RATIO=$(echo "scale=2; $EXPECTED_RATIO - $TOLERANCE" | bc)
    MAX_RATIO=$(echo "scale=2; $EXPECTED_RATIO + $TOLERANCE" | bc)

    # Check if actual ratio is within bounds
    if [ "$(echo "$ACTUAL_RATIO >= $MIN_RATIO" | bc)" -eq 1 ] && [ "$(echo "$ACTUAL_RATIO <= $MAX_RATIO" | bc)" -eq 1 ]; then
        echo -e "${GREEN}✓${NC} Check 8/8: Distribution ratio matches priority (${ACTUAL_RATIO}:1, expected ${EXPECTED_RATIO}±${TOLERANCE}:1)"
        CHECKS_PASSED=$((CHECKS_PASSED + 1))
        RATIO_CHECK_PASSED=1
    else
        echo -e "${RED}✗${NC} Check 8/8: Distribution ratio does NOT match priority"
        echo "  Expected: ${EXPECTED_RATIO}±${TOLERANCE}:1 (range: ${MIN_RATIO} to ${MAX_RATIO})"
        echo "  Actual: ${ACTUAL_RATIO}:1"
        echo "  Deviation: Outside acceptable range"
    fi
else
    echo -e "${RED}✗${NC} Check 8/8: Unable to calculate distribution ratio (insufficient data)"
fi

# Final summary
echo ""
echo "========================================="
echo "  TEST SUMMARY"
echo "========================================="
echo ""
echo "Checks passed: $CHECKS_PASSED / $CHECKS_TOTAL"
echo ""

if [ $CHECKS_PASSED -ge 6 ] && [ $RATIO_CHECK_PASSED -eq 1 ]; then
    echo -e "${BOLD}${GREEN}✓ PRIORITY DISTRIBUTION TEST PASSED${NC}"
    echo ""
    echo "The multi-pool worker correctly distributes work according to"
    echo "configured priorities. Pool 1 (priority $POOL1_PRIORITY) received ~${POOL1_PERCENT}%"
    echo "of work, Pool 2 (priority $POOL2_PRIORITY) received ~${POOL2_PERCENT}%."
    echo "Actual ratio ${ACTUAL_RATIO}:1 matches expected ${EXPECTED_RATIO}:1 within tolerance."
    EXIT_CODE=0
elif [ $CHECKS_PASSED -ge 6 ]; then
    echo -e "${BOLD}${YELLOW}⚠ PRIORITY DISTRIBUTION TEST PARTIAL PASS${NC}"
    echo ""
    echo "Basic connectivity and work distribution passed ($CHECKS_PASSED/8 checks),"
    echo "but the distribution ratio does NOT match configured priorities."
    echo ""
    echo "This suggests priority-weighted distribution is NOT YET IMPLEMENTED."
    echo "Current behavior appears to be simple round-robin distribution."
    echo ""
    echo "IMPLEMENTATION STATUS:"
    echo "  - Multi-pool connection: ✓ Working"
    echo "  - Work distribution: ✓ Working"
    echo "  - Priority weighting: ✗ NOT IMPLEMENTED"
    echo ""
    echo "TO FIX:"
    echo "  Implement priority-weighted selection in dist_multipool_request_work()"
    echo "  in src/distributed/distributed.c. Instead of simple round-robin,"
    echo "  use weighted random selection based on pool priorities."
    EXIT_CODE=1
else
    echo -e "${BOLD}${RED}✗ PRIORITY DISTRIBUTION TEST FAILED${NC}"
    echo ""
    echo "Only $CHECKS_PASSED out of $CHECKS_TOTAL checks passed."
    echo "See logs in $LOG_DIR/ for details."
    EXIT_CODE=1
fi

echo ""
echo "Logs saved to:"
echo "  - $LOG_DIR/coordinator1_priority.log"
echo "  - $LOG_DIR/coordinator2_priority.log"
echo "  - $LOG_DIR/worker_priority.log"
echo "  - $LOG_DIR/priority_test_summary.txt"
echo ""

if [ $DEBUG_MODE -eq 1 ]; then
    echo "Debug mode was enabled. Review logs for detailed debug output."
    echo ""
fi

exit $EXIT_CODE
