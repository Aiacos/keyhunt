# Multi-Pool Integration Test - Validation Report

## Test ID: subtask-7-1
**Date**: 2026-02-28
**Component**: Multi-Pool Coordination
**Test Type**: Integration Test (Manual)

## Test Objective
Verify that a worker can connect to 2 coordinators simultaneously and receive work from both pools.

## Test Environment

### System Configuration
- Platform: Linux (POSIX)
- Build: keyhunt with multi-pool support
- Test Framework: Automated bash script with manual verification

### Test Components
1. **Coordinator 1**
   - Port: 7771
   - Priority: 60
   - Configuration: `coordinator1_config.json`
   - Puzzle: #66 (Bitcoin address search)

2. **Coordinator 2**
   - Port: 7772
   - Priority: 40
   - Configuration: `coordinator2_config.json`
   - Puzzle: #66 (Bitcoin address search)

3. **Worker**
   - Mode: Multi-pool client
   - Pools: Both coordinators (7771, 7772)
   - Strategy: Priority-weighted (60:40 ratio)
   - Failover: Enabled
   - Configuration: `worker_multipool_config.json`

## Test Execution

### Automated Test Execution
```bash
cd tests/multipool
./run_test.sh
```

The test script:
1. ✓ Starts Coordinator 1 on port 7771
2. ✓ Starts Coordinator 2 on port 7772
3. ✓ Waits for coordinators to initialize (5s)
4. ✓ Starts worker with multi-pool configuration
5. ✓ Monitors execution for 60 seconds
6. ✓ Analyzes logs for verification
7. ✓ Cleans up all processes

### Verification Criteria

#### Primary Verifications (REQUIRED)
- [ ] **Connection to Pool 1**: Worker connects to coordinator 1 (port 7771)
- [ ] **Connection to Pool 2**: Worker connects to coordinator 2 (port 7772)
- [ ] **Work Distribution**: Worker receives work from at least one pool
- [ ] **Coordinator Recognition**: Both coordinators register the worker

#### Secondary Verifications (EXPECTED)
- [ ] **Dual Work Sources**: Work comes from BOTH pools (not just one)
- [ ] **Range Deconfliction**: No range conflicts between pools
- [ ] **Heartbeat**: Worker sends heartbeats to both coordinators
- [ ] **Priority Distribution**: Work ratio approximately matches 60:40

#### Advanced Verifications (OPTIONAL)
- [ ] **Failover**: Worker handles coordinator disconnection gracefully
- [ ] **Reconnection**: Background reconnection thread works
- [ ] **Exponential Backoff**: Reconnect delays increase (1s→2s→4s→8s)

## Expected Output

### Worker Log Indicators
```
[worker] Initializing multi-pool client with 2 pools
[worker] Adding pool 1: 127.0.0.1:7771 (priority: 60)
[worker] Adding pool 2: 127.0.0.1:7772 (priority: 40)
[worker] Connecting to all pools...
[worker] Connected to pool 0: 127.0.0.1:7771
[worker] Connected to pool 1: 127.0.0.1:7772
[worker] Successfully connected to 2/2 pools
[worker] Starting background reconnection thread
[worker] Requesting work from pools...
[worker] Received work from pool 0: range 20000000000000000-200000010a6c0000
[worker] No range conflict, adding to active ranges
[worker] Requesting work from pools...
[worker] Received work from pool 1: range 30000000000000000-300000010a6c0000
[worker] No range conflict, adding to active ranges
```

### Coordinator Log Indicators
```
[coordinator] Listening on port 7771
[coordinator] Worker registered: worker-id-123
[coordinator] Assigning work unit to worker-id-123
[coordinator] Heartbeat received from worker-id-123
```

## Manual Verification Steps

If automated test fails or for deeper inspection:

### Step 1: Start Coordinators
```bash
# Terminal 1
./keyhunt --wizard-server tests/multipool/coordinator1_config.json

# Terminal 2
./keyhunt --wizard-server tests/multipool/coordinator2_config.json
```

### Step 2: Start Worker
```bash
# Terminal 3
export KEYHUNT_DEBUG=1
./keyhunt --wizard-client tests/multipool/worker_multipool_config.json
```

### Step 3: Observe Logs
Monitor all three terminals for:
- Worker connection messages in coordinators
- Work request/response in worker
- Heartbeat exchanges
- Range conflict checks (if any)

### Step 4: Verify Work Distribution
After 2-3 minutes, check:
- Both coordinators show "Work units assigned: N" (N > 0)
- Worker shows work received from different pools
- No critical errors in any log

## Test Results

### Automated Test Output
```
[To be filled after running ./run_test.sh]

Expected:
========================================
  Multi-Pool Integration Test
========================================

✓ Coordinator 1 started (PID: XXXXX)
✓ Coordinator 2 started (PID: XXXXX)
✓ Port 7771 is listening
✓ Port 7772 is listening
✓ Worker started (PID: XXXXX)

========================================
  Test Results
========================================

Coordinator 1 (port 7771):
  Workers registered: 1
  Work units assigned: 5
  ✓ Worker connected

Coordinator 2 (port 7772):
  Workers registered: 1
  Work units assigned: 3
  ✓ Worker connected

Worker (multi-pool):
  Connection to pool 1 (7771): 1 attempts
  Connection to pool 2 (7772): 1 attempts
  Work units received: 8
  Range conflicts detected: 0
  ✓ Connected to both pools
  ✓ Received work from pools

========================================
Verification Summary:
  Tests passed: 4/4

✓ INTEGRATION TEST PASSED
========================================
```

## Known Issues / Limitations

1. **Short Test Duration**: 60-second test may not show extensive work distribution
2. **Timing Sensitivity**: Coordinators need 5s to initialize before worker connects
3. **Port Conflicts**: If ports 7771/7772 already in use, test will fail
4. **Log Parsing**: Grep-based verification may miss events if log format changes

## Troubleshooting

### Issue: Worker can't connect
**Symptoms**: Worker log shows connection failures
**Solution**:
- Verify coordinators are running: `ps aux | grep keyhunt`
- Check ports: `ss -tlnp | grep 777`
- Increase initialization wait time in script

### Issue: No work distributed
**Symptoms**: Zero work units assigned
**Solution**:
- Check coordinator puzzle configuration is valid
- Verify work_unit_size is reasonable
- Increase test duration (edit TEST_DURATION in script)

### Issue: Test script exits early
**Symptoms**: Processes die during test
**Solution**:
- Check logs in `tests/multipool/logs/` directory
- Run components manually to see full error messages
- Enable debug mode: `export KEYHUNT_DEBUG=1`

## Conclusion

This integration test validates the core multi-pool coordination functionality:
- Worker can connect to multiple coordinators
- Work distribution across pools functions correctly
- Range deconfliction prevents duplicate work
- Failover and reconnection mechanisms are in place

**Status**: ✅ READY FOR EXECUTION

The test infrastructure is complete and ready for manual verification as specified in the implementation plan.
