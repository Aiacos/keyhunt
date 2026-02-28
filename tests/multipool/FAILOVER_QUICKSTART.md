# Failover Test - Quick Start

## What This Test Does

Tests that a worker can **automatically survive coordinator failures** and **reconnect automatically** when they come back online.

## 3-Step Quick Test

### 1. Build keyhunt (if not already built)
```bash
cd /path/to/keyhunt
make
```

### 2. Run the automated failover test
```bash
cd tests/multipool
./run_failover_test.sh
```

### 3. Watch the test run
The test will:
- **0-15s:** Both coordinators active
- **15-35s:** Kill coordinator 1, verify worker survives
- **35-55s:** Restart coordinator 1, verify reconnection

Total test time: ~55 seconds

## Expected Output

```
========================================
  Multi-Pool Failover Test
========================================

Test Configuration:
  - Coordinator 1: localhost:7771 (priority: 60)
  - Coordinator 2: localhost:7772 (priority: 40)
  - Worker: Multi-pool mode with automatic failover enabled
  - Phase 1: Both coordinators (15s)
  - Phase 2: Coordinator 1 killed (20s)
  - Phase 3: Coordinator 1 restarted (20s)

Step 1/7: Starting Coordinator 1 (port 7771)...
✓ Coordinator 1 started (PID: 12345)

Step 2/7: Starting Coordinator 2 (port 7772)...
✓ Coordinator 2 started (PID: 12346)

...

========================================
  PHASE 1: Both Coordinators Active
========================================

Running for 15s with both coordinators...
[5s/15s] All processes running...
[10s/15s] All processes running...
[15s/15s] All processes running...
✓ Phase 1 complete: Both coordinators operational

========================================
  PHASE 2: Coordinator 1 Failure
========================================

Killing Coordinator 1 (PID: 12345) to simulate failure...
✓ Coordinator 1 stopped
Monitoring worker with only Coordinator 2 (20s)...
Worker should continue working with Coordinator 2 only

[5s/20s] Worker still running...
[10s/20s] Worker still running...
[15s/20s] Worker still running...
[20s/20s] Worker still running...
✓ Phase 2 complete: Worker survived coordinator failure

========================================
  PHASE 3: Coordinator 1 Restart
========================================

Restarting Coordinator 1...
✓ Coordinator 1 restarted (PID: 12347)
✓ Coordinator 1 is running
Monitoring for automatic reconnection (20s)...
Worker should automatically reconnect to Coordinator 1

[5s/20s] Waiting for reconnection...
[10s/20s] Waiting for reconnection...
[15s/20s] Waiting for reconnection...
[20s/20s] Waiting for reconnection...
✓ Phase 3 complete: Reconnection window elapsed

========================================
  Test Results
========================================

Analyzing logs...

Phase 1 Verification: Initial Connections
  ✓ Connected to Coordinator 1 (port 7771)
  ✓ Connected to Coordinator 2 (port 7772)

Phase 2 Verification: Failover Behavior
  ✓ Detected coordinator 1 failure (3 events)
  ✓ Worker continued running after coordinator failure
  ✓ Worker continued processing work (15 activities)

Phase 3 Verification: Automatic Reconnection
  ✓ Reconnection attempts detected (5 attempts)
  ✓ Successfully reconnected to Coordinator 1

Additional Checks: Exponential Backoff
  ✓ Exponential backoff visible in logs

========================================
Verification Summary:
  Tests passed: 8/9

✓ FAILOVER TEST PASSED

The worker successfully:
  - Connected to both coordinators initially
  - Detected Coordinator 1 failure
  - Continued working with Coordinator 2 only
  - Automatically reconnected when Coordinator 1 restarted

Failover and reconnection working as expected!
========================================

Log files:
  - Coordinator 1: ./logs/coordinator1.log
  - Coordinator 2: ./logs/coordinator2.log
  - Worker: ./logs/worker.log
```

## Pass Criteria

Test passes if **at least 6 out of 8 verifications succeed**:

1. ✓ Connected to both coordinators initially
2. ✓ Detected coordinator failure
3. ✓ Worker survived (didn't crash)
4. ✓ Worker continued processing work
5. ✓ Reconnection attempts detected
6. ✓ Successfully reconnected
7. ✓ Exponential backoff visible (bonus)

## Manual Verification (Optional)

If you want to manually verify the behavior, check the logs:

```bash
# Check worker connected to both pools initially
grep "Connected to" ./tests/multipool/logs/worker.log

# Check worker detected disconnection
grep -i "disconnect\|fail" ./tests/multipool/logs/worker.log

# Check worker reconnected
tail -50 ./tests/multipool/logs/worker.log | grep "7771"

# Check exponential backoff
grep -i "backoff\|retry.*delay" ./tests/multipool/logs/worker.log
```

## Troubleshooting

### Test fails with "keyhunt binary not found"
```bash
# Build keyhunt first
cd /path/to/keyhunt
make
```

### Test fails with "port already in use"
```bash
# Kill existing keyhunt processes
pkill keyhunt
sleep 2
# Run test again
./run_failover_test.sh
```

### Worker crashes during failover (Phase 2 fails)
This indicates a bug in failover handling. Check:
- `dist_multipool_request_work()` error handling
- Heartbeat thread disconnect detection
- Worker log for crash details

### Reconnection doesn't happen (Phase 3 fails)
Check:
- `pool_failover_enabled` is true in `worker_multipool_config.json`
- Reconnection thread started (check worker log)
- Exponential backoff delays aren't too long

## Debug Mode

For detailed troubleshooting:
```bash
export KEYHUNT_DEBUG=1
./run_failover_test.sh
```

Then check logs:
```bash
cat ./tests/multipool/logs/worker.log | grep -i "reconnect\|disconnect\|pool.*0"
```

## What's Being Tested

This test validates:
- **Disconnect detection:** Heartbeat failures trigger disconnect state
- **Graceful degradation:** Worker continues with remaining pools
- **Exponential backoff:** Reconnection delays increase (1s→2s→4s→8s→16s→32s→60s)
- **Automatic reconnection:** Background thread retries failed pools
- **Seamless recovery:** Work distribution resumes after reconnection

## Next Steps

After this test passes:
- **subtask-7-3:** Test range deconfliction with overlapping work
- **subtask-7-4:** Test weighted priority distribution
- **subtask-7-5:** Test backwards compatibility with single-pool config

## Need Help?

See [FAILOVER_TEST.md](FAILOVER_TEST.md) for detailed documentation.
