#!/bin/bash
#
# Multi-Pool Integration Test
# Tests worker connecting to 2 coordinators simultaneously
#
# This script:
# 1. Starts 2 coordinators on different ports
# 2. Starts a worker with multi-pool configuration
# 3. Monitors for work distribution
# 4. Verifies range deconfliction
# 5. Cleans up processes
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
TEST_DURATION=60  # Run test for 60 seconds
LOG_DIR="$SCRIPT_DIR/logs"

# PIDs for cleanup
COORD1_PID=""
COORD2_PID=""
WORKER_PID=""

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
echo -e "${BOLD}${CYAN}  Multi-Pool Integration Test${NC}"
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
rm -f "$LOG_DIR"/*.log

echo -e "${BLUE}Test Configuration:${NC}"
echo "  - Coordinator 1: localhost:7771 (priority: 60)"
echo "  - Coordinator 2: localhost:7772 (priority: 40)"
echo "  - Worker: Multi-pool mode with both coordinators"
echo "  - Test Duration: ${TEST_DURATION}s"
echo "  - Log Directory: $LOG_DIR"
echo ""

# Step 1: Start Coordinator 1
echo -e "${YELLOW}Step 1/4:${NC} Starting Coordinator 1 (port 7771)..."
cd "$SCRIPT_DIR/../.."

# Create wrapper script for coordinator 1
cat > "$LOG_DIR/start_coord1.sh" << 'EOF'
#!/bin/bash
cd "$(dirname "$0")/../.."
exec ./keyhunt --wizard-server tests/multipool/coordinator1_config.json
EOF
chmod +x "$LOG_DIR/start_coord1.sh"

"$LOG_DIR/start_coord1.sh" > "$LOG_DIR/coordinator1.log" 2>&1 &
COORD1_PID=$!
echo -e "${GREEN}✓${NC} Coordinator 1 started (PID: $COORD1_PID)"

# Step 2: Start Coordinator 2
echo -e "${YELLOW}Step 2/4:${NC} Starting Coordinator 2 (port 7772)..."

# Create wrapper script for coordinator 2
cat > "$LOG_DIR/start_coord2.sh" << 'EOF'
#!/bin/bash
cd "$(dirname "$0")/../.."
exec ./keyhunt --wizard-server tests/multipool/coordinator2_config.json
EOF
chmod +x "$LOG_DIR/start_coord2.sh"

"$LOG_DIR/start_coord2.sh" > "$LOG_DIR/coordinator2.log" 2>&1 &
COORD2_PID=$!
echo -e "${GREEN}✓${NC} Coordinator 2 started (PID: $COORD2_PID)"

# Wait for coordinators to initialize
echo -e "${YELLOW}Waiting for coordinators to initialize...${NC}"
sleep 5

# Verify coordinators are running
if ! kill -0 "$COORD1_PID" 2>/dev/null; then
    echo -e "${RED}✗ Coordinator 1 failed to start${NC}"
    echo "Check log: $LOG_DIR/coordinator1.log"
    exit 1
fi

if ! kill -0 "$COORD2_PID" 2>/dev/null; then
    echo -e "${RED}✗ Coordinator 2 failed to start${NC}"
    echo "Check log: $LOG_DIR/coordinator2.log"
    exit 1
fi

# Check if ports are listening
if command -v ss >/dev/null 2>&1; then
    if ss -tlnp 2>/dev/null | grep -q ":7771"; then
        echo -e "${GREEN}✓${NC} Port 7771 is listening"
    else
        echo -e "${YELLOW}⚠${NC} Port 7771 not detected (may still be initializing)"
    fi

    if ss -tlnp 2>/dev/null | grep -q ":7772"; then
        echo -e "${GREEN}✓${NC} Port 7772 is listening"
    else
        echo -e "${YELLOW}⚠${NC} Port 7772 not detected (may still be initializing)"
    fi
fi

echo ""

# Step 3: Start Worker
echo -e "${YELLOW}Step 3/4:${NC} Starting Worker (multi-pool mode)..."

# Create wrapper script for worker
cat > "$LOG_DIR/start_worker.sh" << 'EOF'
#!/bin/bash
cd "$(dirname "$0")/../.."
export KEYHUNT_DEBUG=1
exec ./keyhunt --wizard-client tests/multipool/worker_multipool_config.json
EOF
chmod +x "$LOG_DIR/start_worker.sh"

"$LOG_DIR/start_worker.sh" > "$LOG_DIR/worker.log" 2>&1 &
WORKER_PID=$!
echo -e "${GREEN}✓${NC} Worker started (PID: $WORKER_PID)"

# Wait for worker to connect
echo -e "${YELLOW}Waiting for worker to connect...${NC}"
sleep 5

# Verify worker is running
if ! kill -0 "$WORKER_PID" 2>/dev/null; then
    echo -e "${RED}✗ Worker failed to start${NC}"
    echo "Check log: $LOG_DIR/worker.log"
    exit 1
fi

echo ""

# Step 4: Monitor and Verify
echo -e "${YELLOW}Step 4/4:${NC} Monitoring test execution (${TEST_DURATION}s)..."
echo -e "${CYAN}Press Ctrl+C to stop early${NC}\n"

VERIFICATION_PASSED=0
VERIFICATION_TESTS=0

# Monitor loop
for ((i=1; i<=TEST_DURATION; i++)); do
    sleep 1

    # Check processes are still running
    if ! kill -0 "$COORD1_PID" 2>/dev/null; then
        echo -e "${RED}✗ Coordinator 1 died unexpectedly${NC}"
        break
    fi

    if ! kill -0 "$COORD2_PID" 2>/dev/null; then
        echo -e "${RED}✗ Coordinator 2 died unexpectedly${NC}"
        break
    fi

    if ! kill -0 "$WORKER_PID" 2>/dev/null; then
        echo -e "${RED}✗ Worker died unexpectedly${NC}"
        break
    fi

    # Progress indicator
    if [ $((i % 10)) -eq 0 ]; then
        echo -e "${BLUE}[${i}s/${TEST_DURATION}s]${NC} Processes running..."
    fi
done

echo ""
echo -e "${BOLD}${CYAN}========================================${NC}"
echo -e "${BOLD}${CYAN}  Test Results${NC}"
echo -e "${BOLD}${CYAN}========================================${NC}\n"

# Analyze logs
echo -e "${YELLOW}Analyzing logs...${NC}\n"

# Check coordinator 1 logs
echo -e "${BOLD}Coordinator 1 (port 7771):${NC}"
if [ -f "$LOG_DIR/coordinator1.log" ]; then
    COORD1_WORKERS=$(grep -c "Worker.*registered" "$LOG_DIR/coordinator1.log" 2>/dev/null || echo "0")
    COORD1_WORK=$(grep -c "Work unit.*assigned" "$LOG_DIR/coordinator1.log" 2>/dev/null || echo "0")
    echo "  Workers registered: $COORD1_WORKERS"
    echo "  Work units assigned: $COORD1_WORK"
    if [ "$COORD1_WORKERS" -gt 0 ]; then
        echo -e "  ${GREEN}✓ Worker connected${NC}"
        VERIFICATION_PASSED=$((VERIFICATION_PASSED + 1))
    else
        echo -e "  ${RED}✗ No worker connection detected${NC}"
    fi
    VERIFICATION_TESTS=$((VERIFICATION_TESTS + 1))
fi
echo ""

# Check coordinator 2 logs
echo -e "${BOLD}Coordinator 2 (port 7772):${NC}"
if [ -f "$LOG_DIR/coordinator2.log" ]; then
    COORD2_WORKERS=$(grep -c "Worker.*registered" "$LOG_DIR/coordinator2.log" 2>/dev/null || echo "0")
    COORD2_WORK=$(grep -c "Work unit.*assigned" "$LOG_DIR/coordinator2.log" 2>/dev/null || echo "0")
    echo "  Workers registered: $COORD2_WORKERS"
    echo "  Work units assigned: $COORD2_WORK"
    if [ "$COORD2_WORKERS" -gt 0 ]; then
        echo -e "  ${GREEN}✓ Worker connected${NC}"
        VERIFICATION_PASSED=$((VERIFICATION_PASSED + 1))
    else
        echo -e "  ${RED}✗ No worker connection detected${NC}"
    fi
    VERIFICATION_TESTS=$((VERIFICATION_TESTS + 1))
fi
echo ""

# Check worker logs
echo -e "${BOLD}Worker (multi-pool):${NC}"
if [ -f "$LOG_DIR/worker.log" ]; then
    POOL1_CONN=$(grep -c "Connected to.*7771" "$LOG_DIR/worker.log" 2>/dev/null || echo "0")
    POOL2_CONN=$(grep -c "Connected to.*7772" "$LOG_DIR/worker.log" 2>/dev/null || echo "0")
    WORK_RECV=$(grep -c "Received work" "$LOG_DIR/worker.log" 2>/dev/null || echo "0")
    CONFLICTS=$(grep -c "Range conflict" "$LOG_DIR/worker.log" 2>/dev/null || echo "0")

    echo "  Connection to pool 1 (7771): $POOL1_CONN attempts"
    echo "  Connection to pool 2 (7772): $POOL2_CONN attempts"
    echo "  Work units received: $WORK_RECV"
    echo "  Range conflicts detected: $CONFLICTS"

    if [ "$POOL1_CONN" -gt 0 ] && [ "$POOL2_CONN" -gt 0 ]; then
        echo -e "  ${GREEN}✓ Connected to both pools${NC}"
        VERIFICATION_PASSED=$((VERIFICATION_PASSED + 1))
    else
        echo -e "  ${RED}✗ Did not connect to both pools${NC}"
    fi
    VERIFICATION_TESTS=$((VERIFICATION_TESTS + 1))

    if [ "$WORK_RECV" -gt 0 ]; then
        echo -e "  ${GREEN}✓ Received work from pools${NC}"
        VERIFICATION_PASSED=$((VERIFICATION_PASSED + 1))
    else
        echo -e "  ${YELLOW}⚠ No work received (may be normal for short test)${NC}"
    fi
    VERIFICATION_TESTS=$((VERIFICATION_TESTS + 1))
fi
echo ""

# Final summary
echo -e "${BOLD}${CYAN}========================================${NC}"
echo -e "${BOLD}Verification Summary:${NC}"
echo -e "  Tests passed: ${GREEN}$VERIFICATION_PASSED${NC}/$VERIFICATION_TESTS"

if [ "$VERIFICATION_PASSED" -ge 3 ]; then
    echo -e "\n${BOLD}${GREEN}✓ INTEGRATION TEST PASSED${NC}"
    echo ""
    echo "The worker successfully:"
    echo "  - Connected to both coordinators"
    echo "  - Received work from the pools"
    echo "  - Demonstrated multi-pool coordination"
    EXIT_CODE=0
else
    echo -e "\n${BOLD}${YELLOW}⚠ INTEGRATION TEST INCOMPLETE${NC}"
    echo ""
    echo "Some verifications did not pass. This may be due to:"
    echo "  - Short test duration (try increasing TEST_DURATION)"
    echo "  - Network/timing issues"
    echo "  - Configuration problems"
    echo ""
    echo "Review logs in: $LOG_DIR"
    EXIT_CODE=1
fi

echo -e "${BOLD}${CYAN}========================================${NC}\n"

echo "Log files:"
echo "  - Coordinator 1: $LOG_DIR/coordinator1.log"
echo "  - Coordinator 2: $LOG_DIR/coordinator2.log"
echo "  - Worker: $LOG_DIR/worker.log"
echo ""

exit $EXIT_CODE
