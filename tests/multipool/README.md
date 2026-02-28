# Multi-Pool Coordination Integration Test

## Test Objective
Verify that a worker can simultaneously connect to 2 coordinators, receive work from both pools, and properly distribute work across them.

## Test Setup

### Architecture
```
┌─────────────────┐          ┌─────────────────┐
│ Coordinator 1   │          │ Coordinator 2   │
│   Port: 7771    │          │   Port: 7772    │
│  Priority: 60   │          │  Priority: 40   │
└────────┬────────┘          └────────┬────────┘
         │                            │
         └────────────┬───────────────┘
                      │
              ┌───────▼───────┐
              │    Worker     │
              │  Multi-Pool   │
              └───────────────┘
```

### Files
- `coordinator1_config.json` - Coordinator 1 configuration (port 7771)
- `coordinator2_config.json` - Coordinator 2 configuration (port 7772)
- `worker_multipool_config.json` - Worker with both coordinators
- `run_test.sh` - Automated test orchestration script

## Available Tests

This directory contains multiple test scripts:

1. **run_test.sh** - Basic multi-pool connection and work distribution test (subtask-7-1)
2. **run_failover_test.sh** - Automatic failover and reconnection test (subtask-7-2)
3. **run_deconfliction_test.sh** - Range deconfliction with overlapping work units (subtask-7-3)

See individual test documentation for details.

## Running the Test

### Automatic Test (Recommended)
```bash
cd tests/multipool
./run_test.sh
```

This script will:
1. Start both coordinators in the background
2. Wait for coordinators to initialize
3. Start the worker with multi-pool configuration
4. Monitor for 60 seconds
5. Verify work distribution and range deconfliction
6. Clean up all processes

### Manual Test
Terminal 1 - Start Coordinator 1:
```bash
./keyhunt --wizard
# Select: Server mode
# Port: 7771
# Load config: tests/multipool/coordinator1_config.json
```

Terminal 2 - Start Coordinator 2:
```bash
./keyhunt --wizard
# Select: Server mode
# Port: 7772
# Load config: tests/multipool/coordinator2_config.json
```

Terminal 3 - Start Worker:
```bash
./keyhunt --wizard
# Select: Client mode
# Multi-pool: Yes
# Load config: tests/multipool/worker_multipool_config.json
```

## Expected Results

### Connection Phase
✓ Worker connects to both coordinators
✓ Connection status shows "2/2 pools connected"
✓ Both coordinators show worker registration

### Work Distribution Phase
✓ Worker receives work from BOTH pools (round-robin or priority-weighted)
✓ Work distribution roughly matches priority ratio (60:40)
✓ No range conflicts between pools

### Progress Reporting
✓ Worker sends heartbeats to both coordinators
✓ Each coordinator tracks worker progress independently
✓ Per-pool statistics displayed in worker output

### Failover Testing
For detailed failover testing, use the dedicated failover test:
```bash
./run_failover_test.sh
```

See [FAILOVER_TEST.md](FAILOVER_TEST.md) for complete documentation.

Expected behavior:
✓ Kill one coordinator → worker continues with remaining pool
✓ Restart coordinator → worker reconnects automatically
✓ Exponential backoff visible in logs (1s, 2s, 4s, 8s...)

### Range Deconfliction Testing
For testing range conflict detection with overlapping work units:
```bash
./run_deconfliction_test.sh
```

See [DECONFLICTION_TEST.md](DECONFLICTION_TEST.md) for complete documentation.

Expected behavior:
✓ Coordinators configured with overlapping ranges (50% overlap)
✓ Worker detects range conflicts via dist_multipool_check_range_conflict()
✓ Worker rejects conflicting work and requests from different pool
✓ Worker continues processing non-conflicting ranges

## Verification Checklist

- [ ] Worker connects to both coordinators successfully
- [ ] Work comes from both pools (check work request logs)
- [ ] No range conflicts detected (check KEYHUNT_DEBUG logs)
- [ ] Progress reported to both coordinators (check heartbeat logs)
- [ ] Priority-weighted distribution approximately matches (60% vs 40%)
- [ ] Clean shutdown without errors

## Troubleshooting

### Worker can't connect to coordinators
- Check coordinators are running: `ps aux | grep keyhunt`
- Check ports are listening: `netstat -tlnp | grep 777`
- Check firewall settings

### No work distributed
- Verify coordinators have valid puzzle configuration
- Check coordinator logs for errors
- Ensure work_unit_size is set correctly

### Range conflicts detected
- This is EXPECTED behavior - test the deconfliction logic
- Worker should reject conflicting work and request from different pool
- Check logs for "Range conflict detected" messages

## Debug Mode

Enable detailed logging:
```bash
export KEYHUNT_DEBUG=1
./run_test.sh
```

This will show:
- Connection attempts and results
- Work requests and responses
- Range conflict checks
- Heartbeat messages
- Reconnection attempts
