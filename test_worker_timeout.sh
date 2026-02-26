#!/bin/bash
# Test script for worker timeout and work redistribution
# Tests work redistribution when a worker stops sending heartbeats (simulating crash)

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
MAGENTA='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}Worker Timeout - Test Suite${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

# Check if keyhunt binary exists
if [ ! -f "./keyhunt" ]; then
    echo -e "${RED}✗ keyhunt binary not found${NC}"
    echo "Please build first: make clean && make"
    exit 1
fi

echo -e "${GREEN}✓ keyhunt binary found${NC}"
echo ""

# Create test target file if it doesn't exist
if [ ! -f "tests/1to32.txt" ]; then
    echo -e "${YELLOW}⚠ Test target file not found, creating...${NC}"
    mkdir -p tests
    echo "1A838B13505B26867 13zb1hQbWVsc2S7ZTZnP2G4undNNpdh5so" > tests/1to32.txt
fi

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}Manual Test: Worker Timeout & Work Redistribution${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

echo "This test verifies that work is automatically redistributed when a worker"
echo "stops sending heartbeats (simulating a crash or network failure)."
echo ""

echo -e "${CYAN}Test Scenario:${NC}"
echo "1. Start coordinator with 60 second timeout"
echo "2. Start worker 1 and verify work assignment"
echo "3. Stop heartbeat from worker 1 (simulate crash)"
echo "4. Wait for timeout (60 seconds)"
echo "5. Verify work is redistributed within timeout window"
echo "6. Verify audit log shows timeout and reassignment messages"
echo ""

echo -e "${YELLOW}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${YELLOW}STEP 1: Start Coordinator with 60s Timeout${NC}"
echo -e "${YELLOW}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo ""
echo "In Terminal 1, run:"
echo ""
echo -e "${GREEN}  ./keyhunt -m bsgs -f tests/1to32.txt -b 32 -M server -p 7777${NC}"
echo ""
echo "Wait for coordinator to start and display:"
echo "  [SERVER] Coordinator started on 0.0.0.0:7777"
echo ""
echo "Note: The coordinator is initialized with default timeouts:"
echo "  - Worker timeout: 60 seconds (no heartbeat)"
echo "  - Work timeout: 60 seconds (stale work reassignment)"
echo ""
read -p "Press Enter when coordinator is running..."

echo ""
echo -e "${YELLOW}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${YELLOW}STEP 2: Start Worker 1${NC}"
echo -e "${YELLOW}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo ""
echo "In Terminal 2, run:"
echo ""
echo -e "${GREEN}  ./keyhunt -M client -H localhost -p 7777${NC}"
echo ""
echo "Wait for worker 1 to connect and display:"
echo "  [CLIENT] Connected to coordinator localhost:7777"
echo "  [CLIENT] Worker #1 registered"
echo ""
echo "The coordinator should show:"
echo "  [SERVER] Worker #1 (hostname) joined [CPU: X cores, GPU: ..., Perf: X.X]"
echo ""
read -p "Press Enter when worker 1 is connected..."

echo ""
echo -e "${YELLOW}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${YELLOW}STEP 3: Verify Work Assignment${NC}"
echo -e "${YELLOW}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo ""
echo "Wait a few seconds for the coordinator to assign work to worker 1."
echo "You should see in the coordinator logs:"
echo "  [SERVER] Work unit #X assigned to worker #1"
echo ""
echo "Worker 1 should show:"
echo "  [CLIENT] Received work unit: range ..."
echo ""
echo "Record the work unit number for verification later."
echo ""
read -p "Press Enter after work is assigned to worker 1..."

echo ""
echo -e "${YELLOW}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${YELLOW}STEP 4: Stop Heartbeat (Simulate Crash)${NC}"
echo -e "${YELLOW}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo ""
echo "To simulate a worker crash without sending a graceful leave message:"
echo ""
echo -e "${CYAN}Option A: Kill Worker Process (Recommended)${NC}"
echo "  In a new terminal, run:"
echo -e "${GREEN}    pkill -9 keyhunt${NC}"
echo "  This will kill all keyhunt worker processes abruptly."
echo "  Make sure to kill ONLY the worker process, not the coordinator!"
echo ""
echo -e "${CYAN}Option B: Suspend Worker Process${NC}"
echo "  In a new terminal, find the worker PID:"
echo -e "${GREEN}    ps aux | grep keyhunt | grep client${NC}"
echo "  Then suspend it:"
echo -e "${GREEN}    kill -STOP <worker_pid>${NC}"
echo "  This prevents heartbeat messages without terminating the process."
echo ""
echo -e "${CYAN}Option C: Block Network (Advanced)${NC}"
echo "  Use iptables or similar to block communication between worker and coordinator."
echo ""

# Create a helper script to kill worker only
cat > /tmp/kill_worker_only.sh << 'EOF'
#!/bin/bash
# Kill only the keyhunt worker process (client mode)
WORKER_PID=$(ps aux | grep 'keyhunt.*client' | grep -v grep | awk '{print $2}')
if [ -n "$WORKER_PID" ]; then
    echo "Killing worker process: $WORKER_PID"
    kill -9 $WORKER_PID
    echo "✓ Worker killed (simulating crash)"
else
    echo "✗ No worker process found"
    echo "  Make sure worker is running with -M client flag"
fi
EOF

chmod +x /tmp/kill_worker_only.sh

echo "Helper script created: /tmp/kill_worker_only.sh"
echo ""
echo "When ready, in a new terminal, run:"
echo -e "${GREEN}  /tmp/kill_worker_only.sh${NC}"
echo ""
echo "OR manually kill the worker process with Ctrl+\\ (SIGQUIT) in Terminal 2."
echo ""
read -p "Press Enter AFTER you have stopped the worker..."

echo ""
echo -e "${YELLOW}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${YELLOW}STEP 5: Monitor Timeout (60 seconds)${NC}"
echo -e "${YELLOW}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo ""
echo "Now we need to wait for the coordinator to detect the timeout."
echo "The coordinator checks for timeouts periodically (every 10 seconds)."
echo ""
echo "Starting 60-second countdown..."
echo ""

# Countdown timer
START_TIME=$(date +%s)
TARGET_TIME=$((START_TIME + 60))

while [ $(date +%s) -lt $TARGET_TIME ]; do
    REMAINING=$((TARGET_TIME - $(date +%s)))
    echo -ne "${CYAN}Time remaining: ${REMAINING}s${NC}\r"
    sleep 1
done

echo ""
echo -e "${GREEN}✓ 60 seconds elapsed${NC}"
echo ""

read -p "Press Enter to verify timeout detection..."

echo ""
echo -e "${YELLOW}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${YELLOW}STEP 6: Verify Timeout Detection${NC}"
echo -e "${YELLOW}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo ""
echo "Check the coordinator logs (Terminal 1). You should see:"
echo ""
echo -e "${CYAN}Expected output (within 60-70 seconds of worker crash):${NC}"
echo "  [SERVER] [WARN] Worker #1 (hostname) timed out (no heartbeat for 60 sec)"
echo "  [SERVER] Worker #1 disconnected"
echo ""
echo -e "${GREEN}Verification checklist:${NC}"
echo "  [ ] Timeout warning appeared"
echo "  [ ] Timeout duration (60 sec) is shown in message"
echo "  [ ] Worker ID and hostname are logged"
echo "  [ ] Worker status changed to disconnected"
echo ""

# Check if logs exist
if [ -f "coordinator.log" ]; then
    echo "Checking coordinator.log for timeout messages..."
    if grep -q "timed out" coordinator.log; then
        echo -e "${GREEN}✓ Timeout message found in log${NC}"
    else
        echo -e "${YELLOW}⚠ No timeout message found yet (check console output)${NC}"
    fi
fi

read -p "Press Enter to continue..."

echo ""
echo -e "${YELLOW}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${YELLOW}STEP 7: Verify Work Redistribution${NC}"
echo -e "${YELLOW}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo ""
echo "Check the coordinator logs for work redistribution messages:"
echo ""
echo -e "${CYAN}Expected output:${NC}"
echo "  [SERVER] [INFO] Work unit #X reassigned from worker #1 to pool (reason: stale work)"
echo "  [SERVER] Work units: Pending=Y, Assigned=0, Completed=Z"
echo ""
echo "Or if there's another worker connected:"
echo "  [SERVER] Work unit #X assigned to worker #2"
echo ""
echo -e "${GREEN}Verification checklist:${NC}"
echo "  [ ] Work reassignment message logged"
echo "  [ ] Reassignment reason is 'stale work' or 'worker timeout'"
echo "  [ ] Work unit ID matches the one assigned to worker 1"
echo "  [ ] Work is either back in pending pool OR assigned to another worker"
echo "  [ ] No work was lost"
echo ""
read -p "Press Enter to continue..."

echo ""
echo -e "${YELLOW}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${YELLOW}STEP 8: Optional - Start Worker 2 to Receive Work${NC}"
echo -e "${YELLOW}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo ""
echo "To verify work redistribution in action, start a second worker:"
echo ""
echo "In Terminal 2 (or new terminal), run:"
echo -e "${GREEN}  ./keyhunt -M client -H localhost -p 7777${NC}"
echo ""
echo "Worker 2 should immediately receive the reassigned work:"
echo "  [CLIENT] Worker #2 registered"
echo "  [CLIENT] Received work unit: range ... (from timed-out worker #1)"
echo ""
echo "Coordinator should show:"
echo "  [SERVER] Worker #2 (hostname) joined [CPU: X cores, GPU: ..., Perf: X.X]"
echo "  [SERVER] Work unit #X assigned to worker #2"
echo ""
echo -e "${CYAN}This verifies that:${NC}"
echo "  - Orphaned work is available for reassignment"
echo "  - New workers can pick up work immediately"
echo "  - No downtime for work processing"
echo ""
read -p "Press Enter after testing (or skip if not needed)..."

echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}Test Summary${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo "Manual test completed! Please verify the following:"
echo ""
echo -e "${CYAN}Acceptance Criteria:${NC}"
echo "  [ ] Coordinator detected worker timeout within 60 seconds"
echo "  [ ] Timeout message logged with worker ID, hostname, and duration"
echo "  [ ] Work unit was reassigned from timed-out worker"
echo "  [ ] Reassignment logged with work unit ID and reason"
echo "  [ ] Work is available in pending pool for new workers"
echo "  [ ] No work was lost during timeout/redistribution"
echo "  [ ] System continues to function after worker timeout"
echo ""
echo -e "${YELLOW}Test Result:${NC}"
echo "If all criteria above are met, this test PASSES ✓"
echo ""

echo -e "${MAGENTA}Additional Testing Scenarios:${NC}"
echo ""
echo "1. Test with multiple workers:"
echo "   - Start 3 workers, kill one, verify work goes to remaining workers"
echo ""
echo "2. Test with custom timeout:"
echo "   - Modify coordinator code to use shorter timeout (e.g., 30 seconds)"
echo "   - Use dist_coordinator_set_worker_timeout() API"
echo ""
echo "3. Test rapid worker churn:"
echo "   - Start workers, kill them, start new ones"
echo "   - Verify work continues to be distributed"
echo ""
echo "4. Test edge cases:"
echo "   - Worker times out exactly at work completion"
echo "   - Multiple workers timeout simultaneously"
echo "   - Worker reconnects after timeout"
echo ""

echo "To clean up:"
echo "  - Stop coordinator (Ctrl+C in Terminal 1)"
echo "  - Stop any remaining workers (Ctrl+C)"
echo "  - Clean up test files: rm /tmp/kill_worker_only.sh"
echo ""
echo -e "${BLUE}Test complete!${NC}"
echo ""
