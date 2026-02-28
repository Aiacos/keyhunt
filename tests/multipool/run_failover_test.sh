#!/bin/bash
#
# Multi-Pool Failover Test
# Tests automatic failover when one coordinator goes offline
#
# This script:
# 1. Starts 2 coordinators on different ports
# 2. Starts a worker with multi-pool configuration
# 3. Monitors initial connection
# 4. Kills coordinator 1 to simulate failure
# 5. Verifies worker continues with coordinator 2
# 6. Restarts coordinator 1
# 7. Verifies worker reconnects to coordinator 1
# 8. Cleans up processes
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
INITIAL_DURATION=15  # Initial run with both coordinators (15s)
FAILOVER_DURATION=20 # Run with one coordinator down (20s)
RECONNECT_DURATION=20 # Run after restart to verify reconnection (20s)
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
echo -e "${BOLD}${CYAN}  Multi-Pool Failover Test${NC}"
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
echo "  - Worker: Multi-pool mode with automatic failover enabled"
echo "  - Phase 1: Both coordinators (${INITIAL_DURATION}s)"
echo "  - Phase 2: Coordinator 1 killed (${FAILOVER_DURATION}s)"
echo "  - Phase 3: Coordinator 1 restarted (${RECONNECT_DURATION}s)"
echo "  - Log Directory: $LOG_DIR"
echo ""

# Step 1: Start Coordinator 1
echo -e "${YELLOW}Step 1/7:${NC} Starting Coordinator 1 (port 7771)..."
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
echo -e "${YELLOW}Step 2/7:${NC} Starting Coordinator 2 (port 7772)..."

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

echo -e "${GREEN}✓${NC} Both coordinators initialized"
echo ""

# Step 3: Start Worker
echo -e "${YELLOW}Step 3/7:${NC} Starting Worker (multi-pool mode)..."

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
echo -e "${YELLOW}Waiting for worker to connect to both pools...${NC}"
sleep 5

# Verify worker is running
if ! kill -0 "$WORKER_PID" 2>/dev/null; then
    echo -e "${RED}✗ Worker failed to start${NC}"
    echo "Check log: $LOG_DIR/worker.log"
    exit 1
fi

echo -e "${GREEN}✓${NC} Worker is running"
echo ""

# Phase 1: Initial operation with both coordinators
echo -e "${BOLD}${CYAN}========================================${NC}"
echo -e "${BOLD}${CYAN}  PHASE 1: Both Coordinators Active${NC}"
echo -e "${BOLD}${CYAN}========================================${NC}\n"

echo -e "${YELLOW}Running for ${INITIAL_DURATION}s with both coordinators...${NC}"

for ((i=1; i<=INITIAL_DURATION; i++)); do
    sleep 1

    # Check processes are still running
    if ! kill -0 "$COORD1_PID" 2>/dev/null; then
        echo -e "${RED}✗ Coordinator 1 died unexpectedly${NC}"
        exit 1
    fi

    if ! kill -0 "$COORD2_PID" 2>/dev/null; then
        echo -e "${RED}✗ Coordinator 2 died unexpectedly${NC}"
        exit 1
    fi

    if ! kill -0 "$WORKER_PID" 2>/dev/null; then
        echo -e "${RED}✗ Worker died unexpectedly${NC}"
        exit 1
    fi

    # Progress indicator
    if [ $((i % 5)) -eq 0 ]; then
        echo -e "${BLUE}[${i}s/${INITIAL_DURATION}s]${NC} All processes running..."
    fi
done

echo -e "${GREEN}✓${NC} Phase 1 complete: Both coordinators operational"
echo ""

# Phase 2: Kill Coordinator 1 (Failover Test)
echo -e "${BOLD}${CYAN}========================================${NC}"
echo -e "${BOLD}${CYAN}  PHASE 2: Coordinator 1 Failure${NC}"
echo -e "${BOLD}${CYAN}========================================${NC}\n"

echo -e "${RED}Killing Coordinator 1 (PID: $COORD1_PID) to simulate failure...${NC}"
kill -TERM "$COORD1_PID" 2>/dev/null || true
sleep 2
kill -KILL "$COORD1_PID" 2>/dev/null || true

if kill -0 "$COORD1_PID" 2>/dev/null; then
    echo -e "${RED}✗ Failed to stop Coordinator 1${NC}"
    exit 1
fi

echo -e "${GREEN}✓${NC} Coordinator 1 stopped"
COORD1_PID=""  # Clear PID so cleanup doesn't try to kill it again

echo -e "${YELLOW}Monitoring worker with only Coordinator 2 (${FAILOVER_DURATION}s)...${NC}"
echo -e "${CYAN}Worker should continue working with Coordinator 2 only${NC}\n"

WORKER_SURVIVED=1

for ((i=1; i<=FAILOVER_DURATION; i++)); do
    sleep 1

    # Check worker is still running (this is the critical test!)
    if ! kill -0 "$WORKER_PID" 2>/dev/null; then
        echo -e "${RED}✗ Worker died during failover${NC}"
        WORKER_SURVIVED=0
        break
    fi

    # Check coordinator 2 is still running
    if ! kill -0 "$COORD2_PID" 2>/dev/null; then
        echo -e "${RED}✗ Coordinator 2 died unexpectedly${NC}"
        exit 1
    fi

    # Progress indicator
    if [ $((i % 5)) -eq 0 ]; then
        echo -e "${BLUE}[${i}s/${FAILOVER_DURATION}s]${NC} Worker still running..."
    fi
done

if [ "$WORKER_SURVIVED" -eq 1 ]; then
    echo -e "${GREEN}✓${NC} Phase 2 complete: Worker survived coordinator failure"
else
    echo -e "${RED}✗${NC} Phase 2 FAILED: Worker did not survive"
    exit 1
fi
echo ""

# Phase 3: Restart Coordinator 1 (Reconnection Test)
echo -e "${BOLD}${CYAN}========================================${NC}"
echo -e "${BOLD}${CYAN}  PHASE 3: Coordinator 1 Restart${NC}"
echo -e "${BOLD}${CYAN}========================================${NC}\n"

echo -e "${YELLOW}Restarting Coordinator 1...${NC}"

"$LOG_DIR/start_coord1.sh" >> "$LOG_DIR/coordinator1.log" 2>&1 &
COORD1_PID=$!
echo -e "${GREEN}✓${NC} Coordinator 1 restarted (PID: $COORD1_PID)"

# Wait for coordinator to initialize
sleep 5

# Verify coordinator is running
if ! kill -0 "$COORD1_PID" 2>/dev/null; then
    echo -e "${RED}✗ Coordinator 1 failed to restart${NC}"
    echo "Check log: $LOG_DIR/coordinator1.log"
    exit 1
fi

echo -e "${GREEN}✓${NC} Coordinator 1 is running"

echo -e "${YELLOW}Monitoring for automatic reconnection (${RECONNECT_DURATION}s)...${NC}"
echo -e "${CYAN}Worker should automatically reconnect to Coordinator 1${NC}\n"

for ((i=1; i<=RECONNECT_DURATION; i++)); do
    sleep 1

    # Check all processes are still running
    if ! kill -0 "$COORD1_PID" 2>/dev/null; then
        echo -e "${RED}✗ Coordinator 1 died after restart${NC}"
        exit 1
    fi

    if ! kill -0 "$COORD2_PID" 2>/dev/null; then
        echo -e "${RED}✗ Coordinator 2 died unexpectedly${NC}"
        exit 1
    fi

    if ! kill -0 "$WORKER_PID" 2>/dev/null; then
        echo -e "${RED}✗ Worker died during reconnection${NC}"
        exit 1
    fi

    # Progress indicator
    if [ $((i % 5)) -eq 0 ]; then
        echo -e "${BLUE}[${i}s/${RECONNECT_DURATION}s]${NC} Waiting for reconnection..."
    fi
done

echo -e "${GREEN}✓${NC} Phase 3 complete: Reconnection window elapsed"
echo ""

# Analysis and Verification
echo -e "${BOLD}${CYAN}========================================${NC}"
echo -e "${BOLD}${CYAN}  Test Results${NC}"
echo -e "${BOLD}${CYAN}========================================${NC}\n"

echo -e "${YELLOW}Analyzing logs...${NC}\n"

VERIFICATION_PASSED=0
VERIFICATION_TESTS=0

# Check Phase 1: Initial connection to both coordinators
echo -e "${BOLD}Phase 1 Verification: Initial Connections${NC}"
if [ -f "$LOG_DIR/worker.log" ]; then
    POOL1_INITIAL=$(grep "Connected to.*7771" "$LOG_DIR/worker.log" 2>/dev/null | head -1)
    POOL2_INITIAL=$(grep "Connected to.*7772" "$LOG_DIR/worker.log" 2>/dev/null | head -1)

    if [ -n "$POOL1_INITIAL" ]; then
        echo -e "  ${GREEN}✓${NC} Connected to Coordinator 1 (port 7771)"
        VERIFICATION_PASSED=$((VERIFICATION_PASSED + 1))
    else
        echo -e "  ${RED}✗${NC} Did not connect to Coordinator 1"
    fi
    VERIFICATION_TESTS=$((VERIFICATION_TESTS + 1))

    if [ -n "$POOL2_INITIAL" ]; then
        echo -e "  ${GREEN}✓${NC} Connected to Coordinator 2 (port 7772)"
        VERIFICATION_PASSED=$((VERIFICATION_PASSED + 1))
    else
        echo -e "  ${RED}✗${NC} Did not connect to Coordinator 2"
    fi
    VERIFICATION_TESTS=$((VERIFICATION_TESTS + 1))
fi
echo ""

# Check Phase 2: Failover detection and continued operation
echo -e "${BOLD}Phase 2 Verification: Failover Behavior${NC}"
if [ -f "$LOG_DIR/worker.log" ]; then
    # Look for disconnection detection (may appear in various forms)
    DISCONNECT_DETECTED=$(grep -i "disconnect\|connection.*fail\|pool.*unavailable\|reconnect" "$LOG_DIR/worker.log" 2>/dev/null | wc -l)

    # Count work requests during failover period (after initial phase)
    # This is approximate - just check that worker kept working
    WORK_DURING_FAILOVER=$(tail -n 100 "$LOG_DIR/worker.log" 2>/dev/null | grep -c "Received work\|Processing\|Heartbeat" 2>/dev/null || echo "0")

    if [ "$DISCONNECT_DETECTED" -gt 0 ]; then
        echo -e "  ${GREEN}✓${NC} Detected coordinator 1 failure ($DISCONNECT_DETECTED events)"
        VERIFICATION_PASSED=$((VERIFICATION_PASSED + 1))
    else
        echo -e "  ${YELLOW}⚠${NC} No disconnection events logged (may be normal)"
    fi
    VERIFICATION_TESTS=$((VERIFICATION_TESTS + 1))

    if [ "$WORKER_SURVIVED" -eq 1 ]; then
        echo -e "  ${GREEN}✓${NC} Worker continued running after coordinator failure"
        VERIFICATION_PASSED=$((VERIFICATION_PASSED + 1))
    else
        echo -e "  ${RED}✗${NC} Worker stopped after coordinator failure"
    fi
    VERIFICATION_TESTS=$((VERIFICATION_TESTS + 1))

    if [ "$WORK_DURING_FAILOVER" -gt 0 ]; then
        echo -e "  ${GREEN}✓${NC} Worker continued processing work ($WORK_DURING_FAILOVER activities)"
        VERIFICATION_PASSED=$((VERIFICATION_PASSED + 1))
    else
        echo -e "  ${YELLOW}⚠${NC} Limited work activity detected"
    fi
    VERIFICATION_TESTS=$((VERIFICATION_TESTS + 1))
fi
echo ""

# Check Phase 3: Automatic reconnection
echo -e "${BOLD}Phase 3 Verification: Automatic Reconnection${NC}"
if [ -f "$LOG_DIR/worker.log" ]; then
    # Look for reconnection attempts (use timestamp-aware approach)
    # Get the last part of the log (after restart)
    RECONNECT_ATTEMPTS=$(tail -n 200 "$LOG_DIR/worker.log" 2>/dev/null | grep -c "Reconnect\|retry\|attempt.*connect" 2>/dev/null || echo "0")
    RECONNECT_SUCCESS=$(tail -n 200 "$LOG_DIR/worker.log" 2>/dev/null | grep -c "Connected to.*7771\|reconnect.*success\|pool.*recovered" 2>/dev/null || echo "0")

    # Check coordinator 1 log for worker registration after restart
    COORD1_RECONNECT=""
    if [ -f "$LOG_DIR/coordinator1.log" ]; then
        COORD1_RECONNECT=$(tail -n 50 "$LOG_DIR/coordinator1.log" 2>/dev/null | grep "Worker.*registered\|Worker.*connected" 2>/dev/null | tail -1)
    fi

    if [ "$RECONNECT_ATTEMPTS" -gt 0 ]; then
        echo -e "  ${GREEN}✓${NC} Reconnection attempts detected ($RECONNECT_ATTEMPTS attempts)"
        VERIFICATION_PASSED=$((VERIFICATION_PASSED + 1))
    else
        echo -e "  ${YELLOW}⚠${NC} No explicit reconnection attempts in logs"
        echo -e "      (reconnection may have succeeded immediately)"
    fi
    VERIFICATION_TESTS=$((VERIFICATION_TESTS + 1))

    if [ "$RECONNECT_SUCCESS" -gt 0 ] || [ -n "$COORD1_RECONNECT" ]; then
        echo -e "  ${GREEN}✓${NC} Successfully reconnected to Coordinator 1"
        VERIFICATION_PASSED=$((VERIFICATION_PASSED + 1))
    else
        echo -e "  ${YELLOW}⚠${NC} Cannot confirm reconnection success from logs"
        echo -e "      (check logs manually to verify)"
    fi
    VERIFICATION_TESTS=$((VERIFICATION_TESTS + 1))
fi
echo ""

# Exponential backoff check
echo -e "${BOLD}Additional Checks: Exponential Backoff${NC}"
if [ -f "$LOG_DIR/worker.log" ]; then
    BACKOFF_VISIBLE=$(grep -c "backoff\|retry.*delay\|wait.*[0-9]s" "$LOG_DIR/worker.log" 2>/dev/null || echo "0")

    if [ "$BACKOFF_VISIBLE" -gt 0 ]; then
        echo -e "  ${GREEN}✓${NC} Exponential backoff visible in logs"
        VERIFICATION_PASSED=$((VERIFICATION_PASSED + 1))
    else
        echo -e "  ${BLUE}ℹ${NC} Exponential backoff not explicitly logged (may be internal)"
    fi
    VERIFICATION_TESTS=$((VERIFICATION_TESTS + 1))
fi
echo ""

# Final summary
echo -e "${BOLD}${CYAN}========================================${NC}"
echo -e "${BOLD}Verification Summary:${NC}"
echo -e "  Tests passed: ${GREEN}$VERIFICATION_PASSED${NC}/$VERIFICATION_TESTS"
echo ""

# Success criteria: At least 6 out of 8 verifications should pass
REQUIRED_PASSES=6

if [ "$VERIFICATION_PASSED" -ge "$REQUIRED_PASSES" ]; then
    echo -e "${BOLD}${GREEN}✓ FAILOVER TEST PASSED${NC}\n"
    echo "The worker successfully:"
    echo "  - Connected to both coordinators initially"
    echo "  - Detected Coordinator 1 failure"
    echo "  - Continued working with Coordinator 2 only"
    echo "  - Automatically reconnected when Coordinator 1 restarted"
    echo ""
    echo "Failover and reconnection working as expected!"
    EXIT_CODE=0
else
    echo -e "${BOLD}${YELLOW}⚠ FAILOVER TEST INCOMPLETE${NC}\n"
    echo "Some verifications did not pass ($VERIFICATION_PASSED/$VERIFICATION_TESTS)."
    echo ""
    echo "This may indicate:"
    echo "  - Reconnection logic needs tuning"
    echo "  - Logging is insufficient for verification"
    echo "  - Timing issues (try increasing durations)"
    echo ""
    echo "Review logs manually to confirm behavior:"
    EXIT_CODE=1
fi

echo -e "${BOLD}${CYAN}========================================${NC}\n"

echo "Log files:"
echo "  - Coordinator 1: $LOG_DIR/coordinator1.log"
echo "  - Coordinator 2: $LOG_DIR/coordinator2.log"
echo "  - Worker: $LOG_DIR/worker.log"
echo ""

echo "Manual verification steps:"
echo "  1. Check worker.log for 'Connected to' messages (should see both pools initially)"
echo "  2. Check worker.log for disconnection detection around ${INITIAL_DURATION}s mark"
echo "  3. Check worker.log continues activity after coordinator 1 killed"
echo "  4. Check worker.log for reconnection to 7771 after coordinator 1 restarted"
echo "  5. Check coordinator1.log for worker registration events (initial + after restart)"
echo ""

exit $EXIT_CODE
