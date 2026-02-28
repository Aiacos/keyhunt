# Quick Start Guide - Multi-Pool Integration Test

## TL;DR - Run the Test

```bash
cd tests/multipool
./run_test.sh
```

That's it! The script handles everything automatically.

## What the Test Does

1. **Starts 2 Coordinators** on ports 7771 and 7772
2. **Starts 1 Worker** configured to connect to both
3. **Monitors** for 60 seconds
4. **Verifies** connections and work distribution
5. **Cleans up** all processes automatically

## Expected Output

```
========================================
  Multi-Pool Integration Test
========================================

✓ Coordinator 1 started (PID: 12345)
✓ Coordinator 2 started (PID: 12346)
✓ Port 7771 is listening
✓ Port 7772 is listening
✓ Worker started (PID: 12347)

[60s monitoring...]

========================================
  Test Results
========================================

✓ Worker connected to both pools
✓ Received work from pools
✓ INTEGRATION TEST PASSED
========================================
```

## If Test Fails

1. Check logs in `tests/multipool/logs/`:
   - `coordinator1.log`
   - `coordinator2.log`
   - `worker.log`

2. Look for error messages

3. Common issues:
   - Ports already in use → Kill existing processes
   - Build not up to date → Run `make clean && make`
   - Timing issues → Increase TEST_DURATION in run_test.sh

## Debug Mode

For detailed logging:
```bash
export KEYHUNT_DEBUG=1
./run_test.sh
```

## Manual Testing

If you want to run components separately:

**Terminal 1** (Coordinator 1):
```bash
./keyhunt --wizard-server tests/multipool/coordinator1_config.json
```

**Terminal 2** (Coordinator 2):
```bash
./keyhunt --wizard-server tests/multipool/coordinator2_config.json
```

**Terminal 3** (Worker):
```bash
export KEYHUNT_DEBUG=1
./keyhunt --wizard-client tests/multipool/worker_multipool_config.json
```

Watch the output in all three terminals. You should see:
- Coordinators: "Worker registered"
- Worker: "Connected to pool 0" and "Connected to pool 1"
- Worker: "Received work from pool X"

## What's Being Tested

✓ Worker connects to multiple coordinators simultaneously
✓ Work distribution across pools
✓ Range deconfliction (no duplicate work)
✓ Priority-weighted work selection (60:40 ratio)
✓ Automatic failover capability
✓ Background reconnection thread

## Success Criteria

The test passes if:
- Worker connects to BOTH coordinators ✓
- Both coordinators register the worker ✓
- Worker receives work from at least one pool ✓
- No critical errors in logs ✓

## Next Steps After Test Passes

This test validates **subtask-7-1** of the multi-pool coordination feature.

Remaining integration tests:
- **subtask-7-2**: Automatic failover (kill one coordinator)
- **subtask-7-3**: Range deconfliction (overlapping work units)
- **subtask-7-4**: Weighted priority distribution
- **subtask-7-5**: Backwards compatibility (single-pool)

See `README.md` for full documentation.
