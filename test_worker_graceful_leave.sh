#!/bin/bash
# Test script for worker graceful leave functionality
# Tests worker leaving gracefully and work redistribution

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
echo -e "${BLUE}Worker Graceful Leave - Test Suite${NC}"
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
echo -e "${BLUE}Manual Test: Worker Graceful Leave${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

echo "This test verifies that workers can leave gracefully and work is properly"
echo "redistributed to other workers or back to the work pool."
echo ""

echo -e "${CYAN}Test Scenario:${NC}"
echo "1. Start coordinator with 2 worker slots"
echo "2. Connect 2 workers"
echo "3. Assign work to both workers"
echo "4. Send leave message from worker 1"
echo "5. Verify work is reassigned to worker 2 or pool"
echo "6. Verify audit log shows graceful leave message"
echo "7. Verify worker 1 is removed cleanly"
echo ""

echo -e "${YELLOW}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${YELLOW}STEP 1: Start Coordinator${NC}"
echo -e "${YELLOW}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo ""
echo "In Terminal 1, run:"
echo ""
echo -e "${GREEN}  ./keyhunt -m bsgs -f tests/1to32.txt -b 32 -M server -p 7777${NC}"
echo ""
echo "Wait for coordinator to start and display:"
echo "  [SERVER] Coordinator started on 0.0.0.0:7777"
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
read -p "Press Enter when worker 1 is connected..."

echo ""
echo -e "${YELLOW}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${YELLOW}STEP 3: Start Worker 2${NC}"
echo -e "${YELLOW}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo ""
echo "In Terminal 3, run:"
echo ""
echo -e "${GREEN}  ./keyhunt -M client -H localhost -p 7777${NC}"
echo ""
echo "Wait for worker 2 to connect and display:"
echo "  [CLIENT] Connected to coordinator localhost:7777"
echo "  [CLIENT] Worker #2 registered"
echo ""
echo "At this point, the coordinator should show both workers:"
echo "  [SERVER] Worker #1 (hostname) joined [CPU: X cores, GPU: ..., Perf: X.X]"
echo "  [SERVER] Worker #2 (hostname) joined [CPU: X cores, GPU: ..., Perf: X.X]"
echo ""
read -p "Press Enter when worker 2 is connected..."

echo ""
echo -e "${YELLOW}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${YELLOW}STEP 4: Verify Work Assignment${NC}"
echo -e "${YELLOW}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo ""
echo "Wait a few seconds for the coordinator to assign work to both workers."
echo "You should see in the coordinator logs:"
echo "  [SERVER] Work unit #X assigned to worker #1"
echo "  [SERVER] Work unit #Y assigned to worker #2"
echo ""
echo "Both workers should show:"
echo "  [CLIENT] Received work unit: range ..."
echo ""
read -p "Press Enter when both workers are processing work..."

echo ""
echo -e "${YELLOW}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${YELLOW}STEP 5: Send Leave Message from Worker 1${NC}"
echo -e "${YELLOW}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo ""
echo "To send a graceful leave message, you have two options:"
echo ""
echo -e "${CYAN}Option A: Use Ctrl+C (if implemented in worker code)${NC}"
echo "  In Terminal 2 (worker 1), press Ctrl+C"
echo "  This should trigger dist_worker_leave() before exit"
echo ""
echo -e "${CYAN}Option B: Use test helper script (more reliable)${NC}"
echo "  We'll create a simple script to send the leave message"
echo ""

# Create leave message sender
cat > /tmp/send_leave_message.py << 'EOF'
#!/usr/bin/env python3
"""
Send a graceful leave message to the coordinator.
This simulates a worker sending a leave request.
"""
import socket
import json
import sys
import time

def send_leave_message(host='localhost', port=7777, worker_id=1, reason="Manual test"):
    """Send a leave message to the coordinator"""

    # Create socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(5)

    try:
        # Connect to coordinator
        print(f"Connecting to {host}:{port}...")
        sock.connect((host, port))
        print("Connected!")

        # Send registration first (to establish worker identity)
        # This is a simplified version - real worker does full registration
        register_msg = {
            "type": "register",
            "hostname": f"test-worker-{worker_id}",
            "cpu_cores": 4,
            "gpu_name": "none",
            "perf_score": 1000.0
        }

        msg = json.dumps(register_msg) + "\n"
        sock.sendall(msg.encode('utf-8'))
        print(f"Sent: {register_msg}")

        # Receive response
        response = sock.recv(4096).decode('utf-8')
        print(f"Received: {response}")

        # Small delay to let registration complete
        time.sleep(0.5)

        # Send leave message
        leave_msg = {
            "type": "leave",
            "reason": reason
        }

        msg = json.dumps(leave_msg) + "\n"
        sock.sendall(msg.encode('utf-8'))
        print(f"Sent: {leave_msg}")

        # Receive acknowledgment
        response = sock.recv(4096).decode('utf-8')
        print(f"Received: {response}")

        print("\n✓ Leave message sent successfully!")

    except Exception as e:
        print(f"✗ Error: {e}", file=sys.stderr)
        return 1
    finally:
        sock.close()

    return 0

if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description="Send leave message to coordinator")
    parser.add_argument('-H', '--host', default='localhost', help='Coordinator host')
    parser.add_argument('-p', '--port', type=int, default=7777, help='Coordinator port')
    parser.add_argument('-r', '--reason', default='Manual test', help='Leave reason')

    args = parser.parse_args()
    sys.exit(send_leave_message(args.host, args.port, reason=args.reason))
EOF

chmod +x /tmp/send_leave_message.py

echo "Testing with Option B (recommended):"
echo ""
echo "The easier approach is to stop worker 1 with Ctrl+C in Terminal 2."
echo "The worker should call dist_worker_leave() before exiting."
echo ""
echo "If that doesn't work, you can use the Python helper script:"
echo -e "${GREEN}  python3 /tmp/send_leave_message.py -r 'Manual graceful leave test'${NC}"
echo ""
echo "For now, in Terminal 2, press Ctrl+C to stop worker 1."
echo ""
read -p "Press Enter after stopping worker 1..."

echo ""
echo -e "${YELLOW}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${YELLOW}STEP 6: Verify Graceful Leave Handling${NC}"
echo -e "${YELLOW}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo ""
echo "Check the coordinator logs (Terminal 1). You should see:"
echo ""
echo -e "${CYAN}Expected output:${NC}"
echo "  [SERVER] [INFO] Worker #1 (hostname) requesting graceful leave: Manual test"
echo "  [SERVER] [INFO] Work unit #X reassigned from worker #1 to pool (reason: graceful leave)"
echo "  [SERVER] [OK] Worker #1 (hostname) left gracefully"
echo "  [SERVER] Worker #1 disconnected"
echo ""
echo -e "${GREEN}Verification checklist:${NC}"
echo "  [ ] Graceful leave request logged"
echo "  [ ] Work unit was reassigned"
echo "  [ ] Leave reason was included in log"
echo "  [ ] Worker removed cleanly (no errors)"
echo ""
read -p "Press Enter to continue..."

echo ""
echo -e "${YELLOW}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${YELLOW}STEP 7: Verify Work Redistribution${NC}"
echo -e "${YELLOW}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo ""
echo "Check worker 2 (Terminal 3). It should receive the reassigned work:"
echo ""
echo -e "${CYAN}Expected output:${NC}"
echo "  [CLIENT] Received work unit: range ... (previously from worker 1)"
echo ""
echo "Or check coordinator to see work back in pending pool:"
echo "  [SERVER] Work units: Pending=X, Assigned=Y, Completed=Z"
echo ""
echo -e "${GREEN}Verification checklist:${NC}"
echo "  [ ] Worker 2 received reassigned work OR"
echo "  [ ] Work is back in pending pool (will be assigned next)"
echo "  [ ] No work was lost"
echo ""
read -p "Press Enter to continue..."

echo ""
echo -e "${YELLOW}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${YELLOW}STEP 8: Verify Worker 1 Cannot Reconnect${NC}"
echo -e "${YELLOW}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo ""
echo "Try to start worker 1 again in Terminal 2:"
echo ""
echo -e "${GREEN}  ./keyhunt -M client -H localhost -p 7777${NC}"
echo ""
echo "Worker 1 should be able to reconnect as a NEW worker (gets worker #3)."
echo "The old worker #1 slot should be marked as disconnected."
echo ""
read -p "Press Enter after testing reconnection..."

echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}Test Summary${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo "Manual test completed! Please verify the following:"
echo ""
echo -e "${CYAN}Acceptance Criteria:${NC}"
echo "  [ ] Worker can leave gracefully without affecting other workers"
echo "  [ ] Leave message is logged with worker ID, hostname, and reason"
echo "  [ ] Assigned work is reassigned to another worker or pool"
echo "  [ ] No errors or crashes during leave process"
echo "  [ ] Worker is removed cleanly from active workers list"
echo "  [ ] Other workers continue processing normally"
echo ""
echo -e "${YELLOW}Test Result:${NC}"
echo "If all criteria above are met, this test PASSES ✓"
echo ""
echo "To clean up:"
echo "  - Stop coordinator (Ctrl+C in Terminal 1)"
echo "  - Stop any remaining workers (Ctrl+C in Terminal 2/3)"
echo ""
echo -e "${BLUE}Test complete!${NC}"
echo ""
