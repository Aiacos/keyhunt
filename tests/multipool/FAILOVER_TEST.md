# Multi-Pool Automatic Failover Test

## Test Objective
Verify that the multi-pool worker can automatically detect coordinator failures, continue operating with remaining pools, and automatically reconnect when failed coordinators come back online.

## Test Scenario

### Architecture
```
                    PHASE 1: Both Active
┌─────────────────┐                    ┌─────────────────┐
│ Coordinator 1   │◄──────────────────►│ Coordinator 2   │
│   Port: 7771    │     BOTH ACTIVE    │   Port: 7772    │
│  Priority: 60   │                    │  Priority: 40   │
└────────┬────────┘                    └────────┬────────┘
         │                                      │
         └──────────────┬───────────────────────┘
                        │
                ┌───────▼────────┐
                │     Worker     │
                │   Multi-Pool   │
                └────────────────┘

                    PHASE 2: Failover
┌─────────────────┐                    ┌─────────────────┐
│ Coordinator 1   │                    │ Coordinator 2   │
│      DEAD       │      FAILOVER      │     ACTIVE      │
│       ✗         │                    │   Port: 7772    │
└─────────────────┘                    └────────┬────────┘
                                                │
                                        ┌───────▼────────┐
                                        │     Worker     │
                                        │  Continues...  │
                                        └────────────────┘

                    PHASE 3: Reconnection
┌─────────────────┐                    ┌─────────────────┐
│ Coordinator 1   │◄──────────────────►│ Coordinator 2   │
│  RECONNECTED    │  AUTO-RECONNECT    │     ACTIVE      │
│   Port: 7771    │                    │   Port: 7772    │
└────────┬────────┘                    └────────┬────────┘
         │                                      │
         └──────────────┬───────────────────────┘
                        │
                ┌───────▼────────┐
                │     Worker     │
                │ Both Pools OK  │
                └────────────────┘
```

## Test Phases

### Phase 1: Initial Operation (15 seconds)
- Start both coordinators (ports 7771 and 7772)
- Start worker with multi-pool configuration
- Verify worker connects to both coordinators
- Verify work distribution from both pools

**Expected Results:**
- ✓ Worker connects to both pools
- ✓ Worker receives work from both pools
- ✓ Heartbeats sent to both coordinators

### Phase 2: Failover Test (20 seconds)
- Kill Coordinator 1 (port 7771) with SIGTERM
- Verify worker detects the failure
- Verify worker continues operating with Coordinator 2 only
- Verify worker doesn't crash or hang

**Expected Results:**
- ✓ Worker detects Coordinator 1 disconnection
- ✓ Worker continues running (does NOT crash)
- ✓ Worker continues receiving work from Coordinator 2
- ✓ Worker logs show reconnection attempts to Coordinator 1

### Phase 3: Automatic Reconnection (20 seconds)
- Restart Coordinator 1 on port 7771
- Wait for automatic reconnection (should happen within ~5-10 seconds)
- Verify worker reconnects without manual intervention
- Verify work distribution resumes from both pools

**Expected Results:**
- ✓ Worker detects Coordinator 1 is back online
- ✓ Worker automatically reconnects to Coordinator 1
- ✓ Worker resumes receiving work from both pools
- ✓ Exponential backoff visible in reconnection attempts (1s, 2s, 4s, 8s...)

## Running the Test

### Automated Test (Recommended)
```bash
cd tests/multipool
./run_failover_test.sh
```

The script will:
1. Start both coordinators in background
2. Start worker with multi-pool configuration
3. Run Phase 1 (both coordinators active)
4. Kill Coordinator 1 and run Phase 2 (failover)
5. Restart Coordinator 1 and run Phase 3 (reconnection)
6. Analyze logs and report results
7. Clean up all processes

### Manual Test
Terminal 1 - Coordinator 1:
```bash
./keyhunt --wizard-server tests/multipool/coordinator1_config.json
```

Terminal 2 - Coordinator 2:
```bash
./keyhunt --wizard-server tests/multipool/coordinator2_config.json
```

Terminal 3 - Worker:
```bash
export KEYHUNT_DEBUG=1
./keyhunt --wizard-client tests/multipool/worker_multipool_config.json
```

Terminal 4 - Test Control:
```bash
# Wait 15 seconds, then:
pkill -f "wizard-server.*coordinator1_config.json"

# Wait 20 seconds, observe worker continues

# Restart coordinator 1:
./keyhunt --wizard-server tests/multipool/coordinator1_config.json

# Wait 20 seconds, observe reconnection
```

## Verification Checklist

### Phase 1: Initial Connection
- [ ] Worker logs show "Connected to localhost:7771"
- [ ] Worker logs show "Connected to localhost:7772"
- [ ] Both coordinators log "Worker registered"
- [ ] Worker status shows "2/2 pools connected"

### Phase 2: Failover
- [ ] Worker logs show connection failure to 7771
- [ ] Worker logs show "Pool 0 disconnected" or similar
- [ ] Worker continues running (no crash)
- [ ] Worker logs show work activity from pool 1 (7772)
- [ ] Worker logs show reconnection attempts to pool 0 (7771)
- [ ] Exponential backoff visible (1s, 2s, 4s, 8s delays)

### Phase 3: Reconnection
- [ ] Coordinator 1 logs show new worker registration
- [ ] Worker logs show "Connected to localhost:7771" (second time)
- [ ] Worker logs show "Pool 0 reconnected" or similar
- [ ] Worker status shows "2/2 pools connected" again
- [ ] Work distribution resumes from both pools

## Expected Log Patterns

### Worker Log (worker.log)

**Phase 1 - Initial Connection:**
```
[INFO] Connecting to pool 0: localhost:7771
[INFO] Connected to localhost:7771 successfully
[INFO] Connecting to pool 1: localhost:7772
[INFO] Connected to localhost:7772 successfully
[INFO] Multi-pool: 2/2 pools connected
```

**Phase 2 - Failover:**
```
[ERROR] Heartbeat failed for pool 0 (localhost:7771)
[WARN] Pool 0 disconnected, marking as unavailable
[DEBUG] Will retry pool 0 after 1s delay
[INFO] Requesting work from pool 1 (localhost:7772)
[DEBUG] Pool 0 reconnection attempt failed (delay now 2s)
[DEBUG] Pool 0 reconnection attempt failed (delay now 4s)
[DEBUG] Pool 0 reconnection attempt failed (delay now 8s)
```

**Phase 3 - Reconnection:**
```
[INFO] Pool 0 reconnection attempt...
[INFO] Connected to localhost:7771 successfully
[INFO] Pool 0 reconnected and healthy
[INFO] Multi-pool: 2/2 pools connected
```

### Coordinator 1 Log (coordinator1.log)

**Phase 1 - Initial:**
```
[INFO] Worker X registered from 127.0.0.1
[INFO] Work unit assigned to worker X
```

**Phase 2 - Shutdown:**
```
[Coordinator stopped - no logs]
```

**Phase 3 - After Restart:**
```
[INFO] Server starting on port 7771
[INFO] Worker X registered from 127.0.0.1
[INFO] Work unit assigned to worker X
```

## Success Criteria

The test passes if:
1. **At least 6 out of 8 verification checks pass**
2. Worker survives coordinator failure (doesn't crash)
3. Worker reconnects automatically when coordinator restarts

Minimum required verifications:
- ✓ Initial connection to both pools
- ✓ Worker continues after Coordinator 1 killed
- ✓ Reconnection attempts detected
- ✓ Worker reconnects when Coordinator 1 restarted

## Troubleshooting

### Worker crashes when coordinator dies
**Problem:** Worker doesn't handle disconnection gracefully
**Solution:** Check error handling in `dist_multipool_request_work()` and heartbeat logic

### Worker doesn't reconnect automatically
**Problem:** Reconnection thread not working
**Solution:**
- Verify `pool_failover_enabled` is true in config
- Check `dist_multipool_start_reconnect_thread()` was called
- Look for "Reconnection thread started" in debug logs

### Reconnection happens too slowly
**Problem:** Exponential backoff delays are too long
**Solution:**
- Check initial delay is 1 second
- Verify backoff maxes out at 60 seconds
- Adjust RECONNECT_DELAY_* constants if needed

### No logs showing reconnection attempts
**Problem:** Debug logging not enabled
**Solution:**
- Set `export KEYHUNT_DEBUG=1` before running
- Check KEYHUNT_DEBUG environment variable is set
- Look in worker.log for [DEBUG] lines

## Debug Mode

Enable detailed logging:
```bash
export KEYHUNT_DEBUG=1
./run_failover_test.sh
```

This will show:
- Connection attempts and results for each pool
- Disconnection detection events
- Exponential backoff delays
- Reconnection attempts and timing
- Work request routing decisions

## Performance Notes

### Reconnection Timing
- **Initial delay:** 1 second
- **Backoff sequence:** 1s → 2s → 4s → 8s → 16s → 32s → 60s (max)
- **Expected reconnection time:** 1-10 seconds (depending on when coordinator restarts)

### Work Continuity
- Worker should continue processing without interruption
- Work requests automatically route to available pools
- No work units should be lost during failover
- Active ranges remain tracked during failover

## Related Tests

- `run_test.sh` - Basic multi-pool connection test
- `subtask-7-3` - Range deconfliction test
- `subtask-7-4` - Weighted priority distribution test

## Implementation Details

This test validates the following implementation components:
- `dist_multipool_heartbeat_all()` - Detects disconnections
- `dist_multipool_reconnect()` - Handles reconnection with exponential backoff
- `dist_multipool_request_work()` - Routes work to available pools
- `reconnect_thread_func()` - Background reconnection thread
- `pool_connection_state_t` - Tracks connection state per pool
